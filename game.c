#include "game.h"
#include "metroid_sprites.h"
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "ff.h"
#include "sound.h"

/* Metroid STM32 F446RE - ILI9341 320x240
 * 3 niveles + sala final. No requiere framebuffer ni memoria dinamica.
 * GAME_SetControls necesita la mascara completa de botones (incluido cero).
 * OPTIONS 0x20. X 0x04. POWERUP 0x10. CUADRADO 0x40.
 */
#define SW 23
#define SH 24
#define FLOOR_Y 220
#define GRAV 1
#define JUMP_V -15
#define BULLET_W 6
#define BULLET_H 3
#define PLAYER_MAX_HP 100
#define HIT_COOLDOWN 1000u
#define SHOT_COOLDOWN 280u
#define FRAME_MS 32u
#define C_DARK 0x0842u
#define C_PANEL 0x10A5u
#define C_PURPLE 0x8010u
#define C_PINK 0xF81Fu
#define C_ORANGE 0xFD20u
#define C_CYAN 0x07FFu

/* Fuente de columnas verticales 5x7. */
static const uint8_t font[39][5]={
 {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
 {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
 {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
 {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
 {0x41,0x41,0x7f,0x41,0x41},{0x20,0x40,0x41,0x3f,0x01},
 {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
 {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
 {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
 {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
 {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
 {0x7f,0x20,0x18,0x20,0x7f},{0x63,0x14,0x08,0x14,0x63},
 {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},
 {0x3e,0x45,0x49,0x51,0x3e},{0,0x42,0x7f,0x40,0},
 {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
 {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
 {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
 {0,0,0,0,0},{0,0x08,0x08,0x08,0},{0,0x36,0x36,0,0}
};
static const uint8_t *glyph(char c){
 if(c>='a'&&c<='z')c=(char)(c-'a'+'A');
 if(c>='A'&&c<='Z')return font[c-'A'];
 if(c>='0'&&c<='9')return font[26+c-'0'];
 if(c=='-')return font[37];
 if(c==':')return font[38];
 return font[36];
}
static void label(int16_t x,int16_t y,const char *s,uint16_t color,uint8_t scale){
 while(*s){const uint8_t *g=glyph(*s++);
  for(uint8_t i=0;i<5;i++)for(uint8_t j=0;j<7;j++)
   if(g[i]&(1u<<j))ILI9341_FillRectangle((uint16_t)(x+i*scale),(uint16_t)(y+j*scale),scale,scale,color);
  x+=(int16_t)(6*scale);
 }
}
static void centered(int16_t y,const char *s,uint16_t color,uint8_t scale){
 uint16_t n=0;while(s[n])n++;
 label((int16_t)(160-(int16_t)(n*6*scale)/2),y,s,color,scale);
}
static void rect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t color){
 int16_t x1=x+w,y1=y+h;
 if(x<0)x=0;
 if(y<0)y=0;
 if(x1>320)x1=320;
 if(y1>240)y1=240;
 if(x>=x1||y>=y1)return;
 ILI9341_FillRectangle((uint16_t)x,(uint16_t)y,(uint16_t)(x1-x),(uint16_t)(y1-y),color);
}
static uint8_t overlaps(int16_t ax,int16_t ay,int16_t aw,int16_t ah,
                        int16_t bx,int16_t by,int16_t bw,int16_t bh){
 return ax<bx+bw&&ax+aw>bx&&ay<by+bh&&ay+ah>by;
}


/* ===== Motor: 4 mapas, 3 enemigos, jefes, mejoras y menus ===== */
#define ENEMY_COUNT 5
#define SHOT_COUNT 8
#define TICK_MS 30u
#define PLAYER_SHOTS 4
#define IMMUNE_MS 850u
#define C_HP 0x07e0u
#define C_MISSILE 0xffe0u
#define C_HAZARD 0xf800u
#define C_WALL 0x4208u

typedef enum { UI_MENU, UI_PLAY, UI_DEAD, UI_EXIT, UI_WIN } UI;
typedef struct { int16_t x,y,w,h; } Rect;
typedef struct { int16_t x,y,vx,vy,lo,hi; uint8_t type,alive,hp,flash; } Enemy;
typedef struct { int16_t x,y,vx,vy; uint8_t alive; } Shot;
typedef struct { int16_t x,y,vx,vy; uint8_t alive,missile; } PlayerShot;
typedef struct { int16_t x,y,w,h; uint8_t present; } Item;
typedef struct { int16_t x,y,w,h; uint8_t active; } Dirty;

static const Rect platforms[4][4]={
 {{28,178,70,9},{115,139,73,9},{221,105,76,9},{155,191,34,8}},
 {{30,126,79,9},{122,177,84,9},{213,137,81,9},{75,204,32,8}},
 {{36,169,72,9},{135,122,74,9},{224,161,78,9},{105,195,33,8}},
 {{27,187,85,9},{122,146,69,9},{211,111,82,9},{175,198,35,8}}
};
static const int16_t hazardPos[4][2]={{120,152},{195,220},{109,210},{114,195}};
static const uint16_t floorColors[4]={0x528a,0x7b44,0x4209,0x4814};
static const uint16_t platformColors[4]={ILI9341_BLUE,0xfd20u,C_CYAN,C_PINK};
static const uint16_t ambientColors[4]={0x0000u,0x1004u,0x0085u,0x1008u};
static UI ui=UI_MENU;
static volatile uint8_t controls=0;
static volatile uint8_t inputEdges=0;
static uint8_t lastReceivedControls=0;
static uint8_t jumpPrev=0,hasRun=0,choice=0;
static uint8_t level=1,hp=100,lives=3,missiles=0,missileAmmo=0,ability=0,abilityOn=0;
static uint32_t lastRender=0,lastTick=0,pausedAt=0,lastHit=0,lastShot=0,lastMissile=0,abilityUntil=0,abilityReadyAt=0;
static int16_t px=18,py=196,vy=0;
static int8_t face=1;
static uint8_t grounded=1,bossAlive=1,bossHp=10,bossMax=10;
static int16_t bossX=259,bossY=172,bossV=-1;
static uint32_t bossAttackAt=0,bossMoveAt=0,lastTurretShot=0;
static uint8_t bossPattern=0;
static Enemy enemies[ENEMY_COUNT];
static Shot attacks[SHOT_COUNT];
static PlayerShot playerShots[PLAYER_SHOTS];
/* Arma temporal: 0 normal, 1 misil, 2 plasma, 3 pulso. 5 cargas. */
static uint8_t specialType=0, specialCharges=0;
static uint8_t rapidBoost=0,armorBoost=0,damageBoost=0;
/* Secuencia CUADRADO CUADRADO CUADRADO X (BTN_CUADRADO tres veces + BTN_SALTO).
   Cada pulsacion dentro de 1500 ms de la anterior. */
/* GOD se controla unicamente en el menu. No es un power-up. */
static volatile uint8_t masterMode=0;
/* Estado redundante de proteccion: se conserva al iniciar o cambiar nivel. */
static volatile uint8_t godProtection=0;
static uint8_t godEnabled(void){return (uint8_t)(masterMode || godProtection);}
static void setGodMode(uint8_t enabled){
 masterMode=(uint8_t)(enabled!=0u);
 godProtection=masterMode;
}

static volatile uint8_t masterSequence=0;
static volatile uint8_t masterReady=0;
static uint8_t masterSequenceShown=0;
static uint8_t missileButtonHeld=0, missilePending=0;
static Item rapidPickup,armorPickup,damagePickup;
static uint8_t rapidUsed[4],armorUsed[4],damageUsed[4];
static Item medkit,ammoPickup,abilityPickup;
static uint8_t medUsed[4],ammoUsed[4],abilityUsed=0;
/* SD: single-slot checkpoint. No I/O from UART interrupts or render loop. */
#define SAVE_MAGIC 0x4D545256u
#define SAVE_VERSION 1u
static uint8_t sdReady=0, saveAvailable=0;
typedef struct {
 uint32_t magic;
 uint16_t version;
 uint16_t size;
 int16_t playerX,playerY;
 uint8_t gameLevel,health,lifeCount,facing;
 uint8_t acquiredAbility,abilityTaken;
 uint8_t equippedWeapon,weaponCharges;
 uint8_t rapid,armor,damage,hasMissiles;
 uint8_t medFlags[4],ammoFlags[4],rapidFlags[4],armorFlags[4],damageFlags[4];
 uint8_t bossIsAlive,bossHealth;
 uint8_t enemyAlive[ENEMY_COUNT],enemyHealth[ENEMY_COUNT];
 int16_t enemyX[ENEMY_COUNT],enemyY[ENEMY_COUNT];
 uint32_t checksum;
} GameSave;
static uint32_t saveChecksum(const GameSave *v){
 const uint8_t *p=(const uint8_t*)v;
 uint32_t h=2166136261u;
 for(uint32_t i=0;i<(uint32_t)offsetof(GameSave,checksum);i++)h=(h^p[i])*16777619u;
 return h;
}
static uint8_t validSave(const GameSave *v){
 return v->magic==SAVE_MAGIC && v->version==SAVE_VERSION &&
  v->size==sizeof(GameSave) && v->gameLevel>=1 && v->gameLevel<=4 &&
  v->health>=1 && v->health<=100 && v->lifeCount<=9 &&
  v->playerX>=0 && v->playerX<=GAME_WIDTH-SW &&
  v->playerY>=37 && v->playerY<=FLOOR_Y-SH &&
  v->equippedWeapon<=3 && v->weaponCharges<=5 &&
  v->acquiredAbility<=1 && v->abilityTaken<=1 &&
  v->bossIsAlive<=1 && v->checksum==saveChecksum(v);
}
static uint8_t readSave(GameSave *out){
 FIL f; UINT nr=0; FRESULT r;
 if(!sdReady)return 0;
 r=f_open(&f,"0:/SAVE.DAT",FA_READ);
 if(r!=FR_OK)return 0;
 r=f_read(&f,out,sizeof(*out),&nr);
 FRESULT closeStatus=f_close(&f);
 return (uint8_t)(r==FR_OK && closeStatus==FR_OK && nr==sizeof(*out) && validSave(out));
}
static uint8_t writeSave(void){
 if(!sdReady || ui!=UI_PLAY)return 0;
 GameSave v={0}; FIL f; UINT nw=0;
 v.magic=SAVE_MAGIC;v.version=SAVE_VERSION;v.size=sizeof(v);
 v.playerX=px;v.playerY=py;v.gameLevel=level;v.health=hp;v.lifeCount=lives;
 v.facing=(uint8_t)(face>0?1:0);
 v.acquiredAbility=ability;v.abilityTaken=abilityUsed;
 v.equippedWeapon=specialType;v.weaponCharges=specialCharges;
 v.rapid=rapidBoost;v.armor=armorBoost;v.damage=damageBoost;v.hasMissiles=missiles;
 memcpy(v.medFlags,medUsed,4);memcpy(v.ammoFlags,ammoUsed,4);
 memcpy(v.rapidFlags,rapidUsed,4);memcpy(v.armorFlags,armorUsed,4);
 memcpy(v.damageFlags,damageUsed,4);
 v.bossIsAlive=bossAlive;v.bossHealth=bossHp;
 for(uint8_t i=0;i<ENEMY_COUNT;i++){
  v.enemyAlive[i]=enemies[i].alive;v.enemyHealth[i]=enemies[i].hp;
  v.enemyX[i]=enemies[i].x;v.enemyY[i]=enemies[i].y;
 }
 v.checksum=saveChecksum(&v);
 /* Temporary file + rename keeps the previous checkpoint if the write fails. */
 FRESULT r=f_open(&f,"0:/SAVE.TMP",FA_CREATE_ALWAYS|FA_WRITE);
 if(r!=FR_OK)return 0;
 r=f_write(&f,&v,sizeof(v),&nw);
 FRESULT cr=f_close(&f);
 if(r!=FR_OK||cr!=FR_OK||nw!=sizeof(v))return 0;
 GameSave verify;
 r=f_open(&f,"0:/SAVE.TMP",FA_READ);
 if(r!=FR_OK)return 0;
 UINT nr=0;r=f_read(&f,&verify,sizeof(verify),&nr);
 cr=f_close(&f);
 if(r!=FR_OK||cr!=FR_OK||nr!=sizeof(verify)||!validSave(&verify))return 0;
 /* Rename replacement is not atomic in FAT; save only at checkpoints. */
 f_unlink("0:/SAVE.DAT");
 if(f_rename("0:/SAVE.TMP","0:/SAVE.DAT")!=FR_OK)return 0;
 saveAvailable=1;
 return 1;
}

static Dirty before[2+ENEMY_COUNT+SHOT_COUNT+PLAYER_SHOTS+6];
static uint16_t scanline[320];
/* Coalescencia de regiones modificadas: sin framebuffer de pantalla. */
#define MAX_DIRTY 64u
static Dirty dirtyQueue[MAX_DIRTY];
static uint8_t dirtyCount=0, batching=0;
static void flushDirty(void);

/* Sprites 16x16 guardados DIRECTAMENTE en Flash (const).
 * Caracteres: . transparente, B borde, R rojo, Y amarillo, G verde,
 * P violeta, C cian, O naranja, W blanco. No se necesita SD. */
static const char spriteWalker[16][17]={
 "................",".....BBBBBB.....","...BBPPPPPPBB...","..BPPPPPPPPPPB..",
 "..BPWWPPPPWWPB..",".BPPWWPPPPWWPPB.",".BPPPPPPPPPPPPB.",".BPPPPPPPPPPPPB.",
 ".BPPBBBBBBBBPPB.","..BPPPPPPPPPPB..","..BPPPPPPPPPPB..","...BPPPPPPPPB...",
 "....BBPPPPBB....","...BB......BB...","..BB........BB..","................"};
static const char spriteFlyer[16][17]={
 "................","..CCC......CCC..",".CCCCC....CCCCC.","CCCCCCC..CCCCCCC",
 "CCCOOCCCCOOCCCC.",".CCOOCCCCOOCC...","..CCCOOOOCCC....","....COOOOOC.....",
 "....COOWWOC.....","....COOWWOC.....","...CCOOOOCC.....","..CCOOOOOOCC....",
 ".CC........CC...","CCC........CCC..",".C..........C...","................"};
static const char spriteHunter[16][17]={
 ".....BBBBBB.....","....BRRRRRRB....","...BRRRRRRRRB...","..BRRYYRRYYRRB..",
 "..BRRYYRRYYRRB..","..BRRRRRRRRRRB..","...BRRRRRRRRB...","...BBRRRRRRBB...",
 "..BRRRBBBBRRRB..",".BRRRBRRRRBRRRB.",".BRRRBRRRRBRRRB.","..BRRBRRRRBRRB..",
 "...BRBRRRRBRB...","...BB......BB...","..BB........BB..","................"};
static const char spriteTurret[16][17]={
 "................","................","......BBBB......","....BBCCCCBB....",
 "..BBCCCCCCCCBB..",".BCCWWCCCCWWCCB.",".BCCWWCCCCWWCCB.","BBCCCCCCCCCCCCBB",
 "BOOOCCCCCCCCOOOB","BOOOCCCCCCCCOOOB","BBBBBBBBBBBBBBBB","..BBRRRRRRRRBB..",
 "..BRRRRRRRRRRB..","..BRRRRRRRRRRB..","..BBBBBBBBBBBB..","................"};
static const char spriteTank[16][17]={
 "....BBBBBBBB....","..BBGGGGGGGGBB..",".BGGGGGGGGGGGGB.","BGGGGGGGGGGGGGGB",
 "BGGWWGGGGGGWWGGB","BGGWWGGGGGGWWGGB","BGGGGGGGGGGGGGGB","BGGGGGGGGGGGGGGB",
 "BGGGBBBBBBGGGGB.","BGGGGGGGGGGGGGB.","BGGGGGGGGGGGGGB.",".BGGGGGGGGGGGB..",
 "..BBBBBBBBBBBB..",".BB..BB..BB..BB.","BB...BB..BB...BB","................"};
static uint16_t spritePalette(char c){
 switch(c){case 'B':return 0x2104u;case 'R':return ILI9341_RED;
 case 'Y':return ILI9341_YELLOW;case 'G':return ILI9341_GREEN;
 case 'P':return C_PURPLE;case 'C':return C_CYAN;case 'O':return C_ORANGE;
 case 'W':return ILI9341_WHITE;default:return 0;}
}
static const char (*enemySprite(uint8_t type))[17]{
 switch(type){case 0:return spriteWalker;case 1:return spriteFlyer;
 case 2:return spriteHunter;case 3:return spriteTurret;default:return spriteTank;}
}
static void drawFull(void);
static void drawMenu(void);
static void drawDead(void);
static void hud(void);
static uint8_t onSupport(void);

/* Captura los flancos de botones AL RECIBIR UART, no durante el render.
 * Permite detectar pulsaciones breves incluso con la pantalla ocupada.
 * IMPORTANTE: state es una mascara COMPLETA e incluye liberaciones (0).
 */
void GAME_SetControls(uint8_t state){
 uint8_t rising=(uint8_t)(state & (uint8_t)~lastReceivedControls);
 lastReceivedControls=state;
 controls=state;
 inputEdges=(uint8_t)(inputEdges|rising);
 if((rising&BTN_CUADRADO) && ui==UI_PLAY) missilePending=1u;
}
static Rect playerRect(void){Rect r={px,py,SW,SH};return r;}
static uint8_t intersect(Rect a,Rect b){return overlaps(a.x,a.y,a.w,a.h,b.x,b.y,b.w,b.h);}
static int16_t clamp(int16_t v,int16_t lo,int16_t hi){return v<lo?lo:(v>hi?hi:v);}
static void paintUnion(Dirty a,Dirty b){
 if(!a.active&&!b.active)return;
 int16_t x0=a.active?a.x:b.x,y0=a.active?a.y:b.y;
 int16_t x1=a.active?a.x+a.w:b.x+b.w,y1=a.active?a.y+a.h:b.y+b.h;
 if(b.active){if(b.x<x0)x0=b.x;if(b.y<y0)y0=b.y;if(b.x+b.w>x1)x1=b.x+b.w;if(b.y+b.h>y1)y1=b.y+b.h;}
 if(a.active){if(a.x<x0)x0=a.x;if(a.y<y0)y0=a.y;if(a.x+a.w>x1)x1=a.x+a.w;if(a.y+a.h>y1)y1=a.y+a.h;}
 if(x0<0)x0=0;
 if(y0<0)y0=0;
 if(x1>320)x1=320;
 if(y1>240)y1=240;
 if(x1<=x0||y1<=y0)return;
 /* Si las posiciones estan separadas, evitar pintar la enorme
  * zona vacia entre ambas. Dos ventanas pequenas son mas rapidas. */
 if(a.active && b.active){
  int32_t sum=(int32_t)a.w*a.h+(int32_t)b.w*b.h;
  int32_t united=(int32_t)(x1-x0)*(y1-y0);
  if(united>sum*3 && sum>0){
   Dirty empty={0,0,0,0,0};
   paintUnion(a,empty);
   paintUnion(b,empty);
   return;
  }
 }
 /* Filtrar los sprites fuera de la region antes de procesar pixeles. */
 uint8_t enemyMask=0,attackMask=0,playerMask=0;
 for(uint8_t k=0;k<ENEMY_COUNT;k++)
  if(enemies[k].alive&&overlaps(x0,y0,x1-x0,y1-y0,enemies[k].x,enemies[k].y,18,16))enemyMask|=(uint8_t)(1u<<k);
 for(uint8_t k=0;k<SHOT_COUNT;k++)
  if(attacks[k].alive&&overlaps(x0,y0,x1-x0,y1-y0,attacks[k].x,attacks[k].y,7,7))attackMask|=(uint8_t)(1u<<k);
 for(uint8_t k=0;k<PLAYER_SHOTS;k++)
  if(playerShots[k].alive&&overlaps(x0,y0,x1-x0,y1-y0,playerShots[k].x,playerShots[k].y,10,6))playerMask|=(uint8_t)(1u<<k);
 ILI9341_BeginRGB565((uint16_t)x0,(uint16_t)y0,(uint16_t)(x1-x0),(uint16_t)(y1-y0));
 for(int16_t y=y0;y<y1;y++){
  /* Solo evaluar las plataformas que existen en esta fila. */
  uint8_t platformRowMask=0;
  const Rect *rowPlatforms=platforms[level-1];
  for(uint8_t k=0;k<4;k++){
   if(y>=rowPlatforms[k].y&&y<rowPlatforms[k].y+rowPlatforms[k].h)
    platformRowMask|=(uint8_t)(1u<<k);
  }
  const uint8_t hazardRow=(uint8_t)(y>=211&&y<220);
  for(int16_t x=x0;x<x1;x++){
   uint16_t color=ambientColors[level-1];
   if(y>=220)color=floorColors[level-1];
   for(uint8_t k=0;k<4;k++)if(platformRowMask&(1u<<k)){Rect r=rowPlatforms[k];if(x>=r.x&&x<r.x+r.w)color=platformColors[level-1];}
   /* Columnas y decoraciones distintas por nivel. */
   if(level==1 && y>=59 && y<62 && x>=24 && x<296 && (x&63)<28)color=0x10a2u;
   if(level==2 && x>=45 && x<275 && ((y>=49&&y<53)||(y>=79&&y<82)))color=0x39c5u;
   if(level==3 && x>=25&&x<295 && y>=52&&y<57)color=0x0437u;
   if(level==4 && ((x>35&&x<44)||(x>276&&x<286)) && y>=44&&y<220)color=0x580cu;
   if(hazardRow&&((x>=hazardPos[level-1][0]&&x<hazardPos[level-1][0]+22)||(x>=hazardPos[level-1][1]&&x<hazardPos[level-1][1]+22)))color=C_HAZARD;
   if(x>=301&&y>=182&&y<220)color=bossAlive?C_HAZARD:C_HP;
   /* Iconos de pickups distintos: cruz, misil, rayo, escudo, energia, estrella.
      Se calculan durante el barrido actual, sin nuevas escrituras LCD. */
   if(medkit.present&&x>=medkit.x&&x<medkit.x+medkit.w&&y>=medkit.y&&y<medkit.y+medkit.h){
    int16_t dx=x-medkit.x,dy=y-medkit.y;
    color=(dx>=4&&dx<=6)||(dy>=4&&dy<=6)?ILI9341_WHITE:C_HP;
   }
   if(ammoPickup.present&&x>=ammoPickup.x&&x<ammoPickup.x+ammoPickup.w&&y>=ammoPickup.y&&y<ammoPickup.y+ammoPickup.h){
    int16_t dx=x-ammoPickup.x,dy=y-ammoPickup.y;
    color=(dy>=3&&dy<=7&&dx>=2&&dx<=8)?C_ORANGE:C_MISSILE;
   }
   if(rapidPickup.present&&x>=rapidPickup.x&&x<rapidPickup.x+rapidPickup.w&&y>=rapidPickup.y&&y<rapidPickup.y+rapidPickup.h){
    int16_t dx=x-rapidPickup.x,dy=y-rapidPickup.y;
    color=((dx>=3&&dx<=6&&dy<=4)||(dx>=4&&dx<=5&&dy>=5))?ILI9341_YELLOW:ILI9341_WHITE;
   }
   if(armorPickup.present&&x>=armorPickup.x&&x<armorPickup.x+armorPickup.w&&y>=armorPickup.y&&y<armorPickup.y+armorPickup.h){
    int16_t dx=x-armorPickup.x,dy=y-armorPickup.y;
    color=(dx==dy||dx+dy==9)?ILI9341_WHITE:ILI9341_BLUE;
   }
   if(damagePickup.present&&x>=damagePickup.x&&x<damagePickup.x+damagePickup.w&&y>=damagePickup.y&&y<damagePickup.y+damagePickup.h){
    int16_t dx=x-damagePickup.x,dy=y-damagePickup.y;
    color=(dx==4||dy==4)?ILI9341_WHITE:C_PINK;
   }
   if(abilityPickup.present&&x>=abilityPickup.x&&x<abilityPickup.x+abilityPickup.w&&y>=abilityPickup.y&&y<abilityPickup.y+abilityPickup.h){
    int16_t dx=x-abilityPickup.x,dy=y-abilityPickup.y;
    color=(dx>=3&&dx<=7&&dy>=3&&dy<=7)?ILI9341_WHITE:C_CYAN;
   }
   for(uint8_t k=0;k<ENEMY_COUNT;k++){
    Enemy *e=&enemies[k];
    if((enemyMask&(1u<<k))&&x>=e->x&&x<e->x+18&&y>=e->y&&y<e->y+16){
     int16_t sx=x-e->x-1,sy=y-e->y;
     if(sx>=0&&sx<16&&sy>=0&&sy<16){
      char pixel=enemySprite(e->type)[sy][sx];
      if(pixel!='.')color=e->flash?ILI9341_WHITE:spritePalette(pixel);
     }
    }
   }
   if(bossAlive&&x>=bossX&&x<bossX+29&&y>=bossY&&y<bossY+43){
    int16_t u=x-bossX,v=y-bossY;
    if(u>=3&&u<26&&v>=3&&v<40)color=(level==4)?C_PINK:(level==3?C_CYAN:C_PURPLE);
    if(u>=7&&u<22&&v>=10&&v<16)color=C_ORANGE;
    if((u==5||u==23)&&v>=5&&v<35)color=ILI9341_WHITE;
    if(v>=20&&v<24&&u>=9&&u<20)color=ILI9341_YELLOW;
    if(v>=35&&v<41&&((u>=5&&u<10)||(u>=19&&u<24)))color=0x2104u;
   }
   if(x>=px&&x<px+SW&&y>=py&&y<py+SH){
    int16_t sx=x-px,sy=y-py;if(face<0)sx=SW-1-sx;
    char pixel=samusRetroSprite[sy][sx];
    if(pixel!='.')color=spritePalette(pixel);
   }
   for(uint8_t k=0;k<SHOT_COUNT;k++)if((attackMask&(1u<<k))&&x>=attacks[k].x&&x<attacks[k].x+7&&y>=attacks[k].y&&y<attacks[k].y+7)color=C_ORANGE;
   for(uint8_t k=0;k<PLAYER_SHOTS;k++){
    PlayerShot *s=&playerShots[k];
    if((playerMask&(1u<<k))&&x>=s->x&&x<s->x+(s->missile?10:6)&&y>=s->y&&y<s->y+(s->missile?6:3)){
      int16_t u=x-s->x, v=y-s->y;
      if(s->missile){
        if(s->missile==1u)color=(u==0||u==9)?ILI9341_WHITE:((v==0||v==5)?C_CYAN:C_ORANGE);
        else if(s->missile==2u)color=(u==0||u==9)?ILI9341_WHITE:((v==0||v==5)?0x03FFu:C_HP);
        else color=(u<=1||u>=8)?ILI9341_WHITE:((v==0||v==5)?C_PINK:((u>=3&&u<=6)?ILI9341_YELLOW:C_ORANGE));
      }else{
        color=damageBoost?(u==5?ILI9341_WHITE:C_PINK):(rapidBoost?C_HP:ILI9341_YELLOW);
      }
    }
   }
   scanline[x-x0]=color;
  }
  ILI9341_WriteRGB565(scanline,(uint16_t)(x1-x0));
 }
 ILI9341_EndRGB565();
}
/* Se unen solo ventanas superpuestas o cercanas, para no repintar
 * el espacio enorme entre dos objetos lejanos. */
static void queueRect(Dirty r){
 if(!r.active||r.w<=0||r.h<=0)return;
 int16_t x0=r.x,y0=r.y,x1=r.x+r.w,y1=r.y+r.h;
 if(x0<0)x0=0;
 if(y0<0)y0=0;
 if(x1>320)x1=320;
 if(y1>240)y1=240;
 if(x1<=x0||y1<=y0)return;
 r=(Dirty){x0,y0,(int16_t)(x1-x0),(int16_t)(y1-y0),1};
 /* Merge greedily only when the bounding area is not wasteful. */
 uint8_t merged=1;
 while(merged){
  merged=0;
  for(uint8_t k=0;k<dirtyCount;k++){
   Dirty o=dirtyQueue[k];
   int16_t nx=(r.x<o.x?r.x:o.x),ny=(r.y<o.y?r.y:o.y);
   int16_t mx=(r.x+r.w>o.x+o.w?r.x+r.w:o.x+o.w);
   int16_t my=(r.y+r.h>o.y+o.h?r.y+r.h:o.y+o.h);
   int32_t area=(int32_t)(mx-nx)*(my-ny);
   int32_t sum=(int32_t)r.w*r.h+(int32_t)o.w*o.h;
   if(area<=sum+(sum/5)){
    r=(Dirty){nx,ny,(int16_t)(mx-nx),(int16_t)(my-ny),1};
    dirtyQueue[k]=dirtyQueue[--dirtyCount];merged=1;break;
   }
  }
 }
 if(dirtyCount>=MAX_DIRTY){flushDirty();}
 dirtyQueue[dirtyCount++]=r;
}
static void flushDirty(void){
 uint8_t count=dirtyCount;dirtyCount=0;
 for(uint8_t k=0;k<count;k++)paintUnion(dirtyQueue[k],(Dirty){0,0,0,0,0});
}
static void rectUnion(Dirty a,Dirty b){
 if(batching){queueRect(a);queueRect(b);}
 else paintUnion(a,b);
}
static Dirty d(int16_t x,int16_t y,int16_t w,int16_t h,uint8_t active){Dirty r={x,y,w,h,active};return r;}
static uint8_t capture(Dirty *s){
 uint8_t i=0;s[i++]=d(px,py,SW,SH,1);
 s[i++]=d(bossX,bossY,29,43,bossAlive);
 for(uint8_t k=0;k<ENEMY_COUNT;k++)s[i++]=d(enemies[k].x,enemies[k].y,18,16,enemies[k].alive);
 for(uint8_t k=0;k<SHOT_COUNT;k++)s[i++]=d(attacks[k].x,attacks[k].y,7,7,attacks[k].alive);
 for(uint8_t k=0;k<PLAYER_SHOTS;k++){
  PlayerShot *ps=&playerShots[k];s[i++]=d(ps->x,ps->y,ps->missile?10:6,ps->missile?6:3,ps->alive);
 }
 s[i++]=d(medkit.x,medkit.y,medkit.w,medkit.h,medkit.present);
 s[i++]=d(ammoPickup.x,ammoPickup.y,ammoPickup.w,ammoPickup.h,ammoPickup.present);
 s[i++]=d(abilityPickup.x,abilityPickup.y,abilityPickup.w,abilityPickup.h,abilityPickup.present);
 s[i++]=d(rapidPickup.x,rapidPickup.y,rapidPickup.w,rapidPickup.h,rapidPickup.present);
 s[i++]=d(armorPickup.x,armorPickup.y,armorPickup.w,armorPickup.h,armorPickup.present);
 s[i++]=d(damagePickup.x,damagePickup.y,damagePickup.w,damagePickup.h,damagePickup.present);
 return i;
}
static void drawChanges(void){
 Dirty after[2+ENEMY_COUNT+SHOT_COUNT+PLAYER_SHOTS+6];uint8_t n=capture(after);
 batching=1;
 for(uint8_t i=0;i<n;i++){
  Dirty a=before[i],b=after[i];
  if(a.active!=b.active||a.x!=b.x||a.y!=b.y||a.w!=b.w||a.h!=b.h)rectUnion(a,b);
 }
 batching=0;
 flushDirty();
}
/* Solo repintar los indicadores que cambiaron. Evita transferir
 * el HUD completo por cada disparo especial o impacto al jefe. */
static void hudSpecial(void){
 rect(181,5,61,15,C_DARK);
 label(184,7,specialType==1?"MISIL":(specialType==2?"PLASMA":(specialType==3?"PULSO":"BLASTER")),
       specialType==1?C_CYAN:(specialType==2?C_HP:(specialType==3?C_PINK:ILI9341_WHITE)),1);
 rect(32,21,28,13,C_DARK);
 label(35,22,"E",C_CYAN,1);
 char t[3]={(char)('0'+specialCharges/10u),(char)('0'+specialCharges%10u),0};
 label(43,22,t,C_CYAN,1);
}
static void hudBoss(void){
 if(!bossAlive){rect(110,24,200,9,C_DARK);return;}
 rect(110,24,200,9,ILI9341_WHITE);
 rect(112,26,196,5,C_DARK);
 if(bossMax)rect(112,26,(int16_t)(196u*bossHp/bossMax),5,C_PINK);
}
static void hud(void){
 rect(0,0,320,37,C_DARK);rect(8,7,104,12,ILI9341_WHITE);rect(10,9,100,8,C_DARK);
 rect(10,9,hp,8,(hp>60)?C_HP:((hp>25)?ILI9341_YELLOW:ILI9341_RED));
 /* Vidas siempre visibles. HUD compacto sin refrescar toda la pantalla. */
 label(249,17,"VIDAS",ILI9341_WHITE,1);
 char count[2]={(char)('0'+lives),'\0'};
 label(292,17,count,ILI9341_YELLOW,1);
 label(118,7,"NIVEL",ILI9341_WHITE,1);
 label(156,7,(level==4)?"FINAL":(level==3?"3":(level==2?"2":"1")),ILI9341_YELLOW,1);
 label(184,7,specialType==1?"MISIL":(specialType==2?"PLASMA":(specialType==3?"PULSO":"BLASTER")),specialType==1?C_CYAN:(specialType==2?C_HP:(specialType==3?C_PINK:ILI9341_WHITE)),1);
 if(godEnabled()){rect(243,3,75,19,0x03E0u);label(250,8,"GOD ON",ILI9341_BLACK,1);}
 else if(ability)label(250,7,abilityOn?"ESCUDO":"PODER",C_CYAN,1);
 if(armorBoost)rect(5,22,10,5,ILI9341_BLUE);
 if(damageBoost)rect(18,22,10,5,C_PINK);
 /* Cargas restantes de arma especial, maximo cinco. */
 label(35,22,"E",C_CYAN,1);
 char ammoText[4];
 ammoText[0]=(char)('0'+(specialCharges/10u)%10u);
 ammoText[1]=(char)('0'+specialCharges%10u);
 ammoText[2]='\0';
 label(43,22,ammoText,C_CYAN,1);
 if(rapidBoost)rect(63,23,7,5,C_HP);
 if(armorBoost)rect(73,23,7,5,ILI9341_BLUE);
 if(damageBoost)rect(83,23,7,5,C_PINK);
 if(bossAlive){rect(110,24,200,9,ILI9341_WHITE);rect(112,26,196,5,C_DARK);
  rect(112,26,(int16_t)(196u*bossHp/bossMax),5,C_PINK);}
}
void GAME_DrawScene(void){
 ILI9341_FillScreen(ambientColors[level-1]);
 for(int16_t y=38;y<240;y+=20)rectUnion(d(0,y,320,20,1),d(0,0,0,0,0));
 hud();
}
void GAME_DrawSamus(uint16_t x,uint16_t y){
 int16_t oldX=px,oldY=py;px=(int16_t)x;py=(int16_t)y;
 rectUnion(d(px,py,SW,SH,1),d(0,0,0,0,0));px=oldX;py=oldY;
}
static void drawFull(void){dirtyCount=0;batching=0;GAME_DrawScene();capture(before);lastRender=HAL_GetTick();}
/* Menu inicio: NUEVA / GOD / SALIR.
 * Menu pausa: NUEVA / CONTINUAR / GOD / SALIR. */
static uint8_t menuCount(void){return (hasRun||saveAvailable)?4u:3u;}
static uint8_t godChoice(void){return (hasRun||saveAvailable)?2u:1u;}
static const char *menuName(uint8_t n){
 if(n==0u)return "NUEVA PARTIDA";
 if((hasRun||saveAvailable) && n==1u)return "CONTINUAR";
 if(n==godChoice())return godEnabled()?"GOD MODE ON":"GOD MODE OFF";
 return "SALIR";
}
static void drawMenu(void){
 ILI9341_FillScreen(C_DARK);rect(12,12,296,3,ILI9341_BLUE);
 centered(25,"METROID",ILI9341_YELLOW,3);
 centered(59,"STM32 EDITION",C_HP,1);
 for(uint8_t i=0;i<menuCount();i++){
  int16_t y=((hasRun||saveAvailable)?78:91)+(int16_t)i*((hasRun||saveAvailable)?35:41);
  rect(36,y,248,31,i==choice?ILI9341_YELLOW:ILI9341_BLUE);
  rect(38,y+2,244,27,C_DARK);
  centered(y+7,menuName(i),i==choice?ILI9341_YELLOW:ILI9341_WHITE,2);
 }
 centered(215,"IZQ DER - X OK",C_HP,1);
}
static uint8_t deadCount(void){return lives?3u:2u;}
static const char *deadName(uint8_t c){
 if(lives){if(c==0)return "REINTENTAR NIVEL";if(c==1)return "VOLVER A EMPEZAR";return "MENU INICIO";}
 return c==0?"VOLVER A EMPEZAR":"MENU INICIO";
}
static void drawDead(void){
 ILI9341_FillScreen(C_DARK);
 centered(20,lives?"SAMUS DERROTADA":"GAME OVER",ILI9341_RED,2);
 char n[8]={'V','I','D','A','S',' ',(char)('0'+lives),'\0'};
 centered(57,n,ILI9341_YELLOW,2);
 for(uint8_t i=0;i<deadCount();i++){
  int16_t y=(int16_t)(95+i*39);
  rect(17,y,286,32,(i==choice)?ILI9341_YELLOW:ILI9341_BLUE);
  rect(19,y+2,282,28,C_DARK);
  centered(y+9,deadName(i),(i==choice)?ILI9341_YELLOW:ILI9341_WHITE,1);
 }
 centered(219,"IZQ DER - X OK",C_HP,1);
}
static void drawVictory(void){ILI9341_FillScreen(C_DARK);centered(37,"VICTORIA",ILI9341_YELLOW,3);
 centered(98,"MISION CUMPLIDA",C_HP,2);centered(190,"X MENU",ILI9341_WHITE,2);}
static void drawExit(void){ILI9341_FillScreen(C_DARK);centered(95,"JUEGO CERRADO",ILI9341_YELLOW,2);
 centered(176,"OPTIONS MENU",C_HP,1);}
void GAME_MenuInit(void){ui=UI_MENU;hasRun=0;choice=0;level=1;
 controls=0;inputEdges=0;lastReceivedControls=0;jumpPrev=0;masterReady=0;masterSequence=0;setGodMode(0);masterSequenceShown=0;drawMenu();}
/* Los power-ups duran SOLO durante el nivel donde se obtienen.
 * No se pierden puntos de vida ni vidas al avanzar.
 * El codigo MASTER no es un power-up coleccionable. */
static void clearLevelPowerups(void){
 rapidBoost=0u;
 armorBoost=0u;
 damageBoost=0u;
 missiles=0u;
 missileAmmo=0u;
 ability=0u;
 abilityOn=0u;
 abilityUntil=0u;
 abilityReadyAt=0u;
 missilePending=0u;
 missileButtonHeld=0u;
 specialType=0u; specialCharges=0u;
}
static void startLevel(uint8_t number,uint8_t resetHealth){
 uint32_t now=HAL_GetTick();level=number;ui=UI_PLAY;hasRun=1;
 px=18;py=196;vy=0;face=1;grounded=1;jumpPrev=(controls&BTN_SALTO)!=0;
 if(resetHealth)hp=100;
 lastHit=now-IMMUNE_MS;lastShot=now-450u;lastMissile=now-650u;lastTick=now;
 missileButtonHeld=(controls&BTN_CUADRADO)!=0;missilePending=0; specialType=0; specialCharges=0;
 for(uint8_t i=0;i<PLAYER_SHOTS;i++)playerShots[i].alive=0;
 for(uint8_t i=0;i<SHOT_COUNT;i++)attacks[i].alive=0;
 bossAlive=1;bossX=258;bossY=(number==4)?163:172;bossV=-1;
 bossMax=(uint8_t)(9+number*5);bossHp=bossMax;
 bossAttackAt=now;bossMoveAt=now;lastTurretShot=now;bossPattern=0;
 for(uint8_t i=0;i<ENEMY_COUNT;i++){
  Enemy *e=&enemies[i];e->type=i;e->alive=1;e->flash=0;e->hp=(i==4)?14:((i==2||i==3)?8:5);
  e->x=(int16_t)(35+i*49);e->y=(i==1)?100:((i==3)?163:204);
  e->vx=(i==1)?2:((i==2)?-2:1);e->vy=0;
  e->lo=(int16_t)(20+i*42);e->hi=(int16_t)(88+i*44);
 }
 /* Objetos situados inmediatamente SOBRE plataformas alcanzables. */
 Rect p0=platforms[number-1][0];
 Rect p1=platforms[number-1][1];
 Rect p2=platforms[number-1][2];
 medkit=(Item){(int16_t)(p0.x+15),(int16_t)(p0.y-11),11,11,!medUsed[number-1]};
 ammoPickup=(Item){(int16_t)(p1.x+20),(int16_t)(p1.y-11),11,11,!ammoUsed[number-1]};
 abilityPickup=(Item){(int16_t)(p2.x+20),(int16_t)(p2.y-11),11,11,(uint8_t)(number==3&&!abilityUsed)};
 rapidPickup=(Item){(int16_t)(p0.x+45),(int16_t)(p0.y-10),10,10,(uint8_t)(number==1&&!rapidUsed[number-1])};
 armorPickup=(Item){(int16_t)(p1.x+46),(int16_t)(p1.y-10),10,10,(uint8_t)(number==2&&!armorUsed[number-1])};
 damagePickup=(Item){(int16_t)(p2.x+46),(int16_t)(p2.y-10),10,10,(uint8_t)(number==3&&!damageUsed[number-1])};
 drawFull();
}
static void startNewGame(void){SOUND_Stop();masterSequence=0;masterReady=0;masterSequenceShown=0;missileButtonHeld=0;missilePending=0;lives=3;rapidBoost=0;armorBoost=0;damageBoost=0;missiles=0;missileAmmo=0;specialType=0;specialCharges=0;ability=0;abilityOn=0;abilityReadyAt=0;abilityUsed=0;
 for(uint8_t i=0;i<4;i++){medUsed[i]=0;ammoUsed[i]=0;rapidUsed[i]=0;armorUsed[i]=0;damageUsed[i]=0;}
 startLevel(1,1);
 writeSave();
}
/* Compatible incluso si main.c todavia llama GAME_Init(): abre menu. */
void GAME_Init(void){GAME_MenuInit();}
void GAME_SaveSystemReady(uint8_t mounted){
 sdReady=(uint8_t)(mounted!=0);
 GameSave probe;
 saveAvailable=readSave(&probe);
 if(ui==UI_MENU){choice=0;drawMenu();}
}
static uint8_t loadSavedGame(void){
 GameSave v;
 if(!readSave(&v)){saveAvailable=0;drawMenu();return 0;}
 /* startLevel resets transient bullets/enemies; then restore checkpoint. */
 startLevel(v.gameLevel,1);
 hp=v.health;lives=v.lifeCount;px=v.playerX;py=v.playerY;face=v.facing?1:-1;
 grounded=onSupport();vy=0;
 ability=v.acquiredAbility;abilityUsed=v.abilityTaken;
 specialType=v.equippedWeapon;specialCharges=v.weaponCharges;
 missileAmmo=v.weaponCharges;missiles=v.hasMissiles;
 rapidBoost=v.rapid;armorBoost=v.armor;damageBoost=v.damage;
 memcpy(medUsed,v.medFlags,4);memcpy(ammoUsed,v.ammoFlags,4);
 memcpy(rapidUsed,v.rapidFlags,4);memcpy(armorUsed,v.armorFlags,4);
 memcpy(damageUsed,v.damageFlags,4);
 bossAlive=v.bossIsAlive;bossHp=(v.bossHealth<=bossMax)?v.bossHealth:bossMax;
 for(uint8_t i=0;i<ENEMY_COUNT;i++){
  enemies[i].alive=v.enemyAlive[i];enemies[i].hp=v.enemyHealth[i];
  enemies[i].x=v.enemyX[i];enemies[i].y=v.enemyY[i];
 }
 uint8_t idx=(uint8_t)(level-1);
 medkit.present=(uint8_t)!medUsed[idx];ammoPickup.present=(uint8_t)!ammoUsed[idx];
 rapidPickup.present=(uint8_t)(level==1&&!rapidUsed[idx]);
 armorPickup.present=(uint8_t)(level==2&&!armorUsed[idx]);
 damagePickup.present=(uint8_t)(level==3&&!damageUsed[idx]);
 abilityPickup.present=(uint8_t)(level==3&&!abilityUsed);
 abilityOn=0;abilityUntil=0;abilityReadyAt=0;
 lastTick=HAL_GetTick();lastHit=lastTick;
 drawFull();return 1;
}
static void enterMenu(void){SOUND_PlayPause();if(ui==UI_PLAY)writeSave();pausedAt=HAL_GetTick();ui=UI_MENU;choice=hasRun?1:0;drawMenu();}
static void resume(void){if(!hasRun)return;SOUND_Stop();
 uint32_t t=HAL_GetTick()-pausedAt;lastHit+=t;lastShot+=t;lastMissile+=t;
 bossAttackAt+=t;bossMoveAt+=t;lastTurretShot+=t;abilityUntil+=t;abilityReadyAt+=t;lastTick=HAL_GetTick();
 jumpPrev=(controls&BTN_SALTO)!=0;ui=UI_PLAY;drawFull();
}
/* Perder una vida elimina TODAS las mejoras.
 * Los coleccionables vuelven a estar disponibles al reintentar. */
static void clearUpgradesOnDeath(void){
 setGodMode(0);masterSequence=0;masterReady=0;masterSequenceShown=0;missileButtonHeld=0;missilePending=0;
 rapidBoost=0;armorBoost=0;damageBoost=0;
 missiles=0;missileAmmo=0;specialType=0;specialCharges=0;ability=0;abilityOn=0;
 abilityReadyAt=0;abilityUntil=0;abilityUsed=0;
 for(uint8_t i=0;i<4;i++){
  rapidUsed[i]=0;armorUsed[i]=0;damageUsed[i]=0;
  ammoUsed[i]=0;medUsed[i]=0;
 }
}
static void damagePlayer(uint8_t amount,uint32_t now){
 if(ui!=UI_PLAY||godEnabled()||abilityOn||(uint32_t)(now-lastHit)<IMMUNE_MS)return;
 if(armorBoost)amount=(uint8_t)((amount+1u)/2u);
 lastHit=now;hp=hp>amount?(uint8_t)(hp-amount):0;
 if(!hp){if(lives>0)lives--;clearUpgradesOnDeath();ui=UI_DEAD;hasRun=0;choice=0;SOUND_PlayDefeat();drawDead();}
 else hud();
}
static void spawnAttack(int16_t tx,int16_t ty,int16_t speed){
 for(uint8_t i=0;i<SHOT_COUNT;i++)if(!attacks[i].alive){
  Shot *s=&attacks[i];s->x=bossX+4;s->y=bossY+19;
  int16_t dx=tx-s->x,dy=ty-s->y;
  int16_t length=(int16_t)((dx<0?-dx:dx)+(dy<0?-dy:dy));if(!length)length=1;
  s->vx=(int16_t)((int32_t)dx*speed/length);s->vy=(int16_t)((int32_t)dy*speed/length);
  if(!s->vx&&dx)s->vx=dx>0?1:-1;
  if(!s->vy&&dy)s->vy=dy>0?1:-1;
  s->alive=1;break;
 }
}
static void enemyFire(Enemy *e,uint32_t now){
 (void)now;
 for(uint8_t i=0;i<SHOT_COUNT;i++)if(!attacks[i].alive){
  Shot *s=&attacks[i];s->alive=1;s->x=e->x+8;s->y=e->y+6;
  int16_t dx=(int16_t)(px+SW/2-s->x),dy=(int16_t)(py+SH/2-s->y);
  int16_t len=(int16_t)((dx<0?-dx:dx)+(dy<0?-dy:dy));if(!len)len=1;
  s->vx=(int16_t)((int32_t)dx*6/len);s->vy=(int16_t)((int32_t)dy*6/len);
  if(!s->vx&&dx)s->vx=dx<0?-1:1;
  break;
 }
}
static uint8_t hitsPlayer(int16_t x,int16_t y,int16_t w,int16_t h){return overlaps(x,y,w,h,px,py,SW,SH);}
static uint8_t moveAttack(Shot *s,uint32_t now){
 /* Subpasos: las balas rapidas no atraviesan a Samus entre fotogramas. */
 for(uint8_t j=0;j<4;j++){
  s->x+=(int16_t)(s->vx/4+(j<(uint8_t)((s->vx<0?-s->vx:s->vx)%4)?(s->vx<0?-1:1):0));
  s->y+=(int16_t)(s->vy/4+(j<(uint8_t)((s->vy<0?-s->vy:s->vy)%4)?(s->vy<0?-1:1):0));
  if(hitsPlayer(s->x,s->y,7,7)){if(!godEnabled())damagePlayer((uint8_t)(12+level*4),now);s->alive=0;return 1;}
 }
 if(s->x<-8||s->x>325||s->y<36||s->y>240)s->alive=0;
 return 0;
}
static uint8_t landsAt(int16_t oldFeet,int16_t newFeet){
 if(oldFeet<=220&&newFeet>=220)return 220;
 for(uint8_t i=0;i<4;i++){Rect r=platforms[level-1][i];
  if(px+SW>r.x&&px<r.x+r.w&&oldFeet<=r.y&&newFeet>=r.y)return(uint8_t)r.y;
 }
 return 255;
}
static uint8_t onSupport(void){
 int16_t feet=py+SH;if(feet==220)return 1;
 for(uint8_t i=0;i<4;i++){Rect r=platforms[level-1][i];
  if(feet==r.y&&px+SW>r.x&&px<r.x+r.w)return 1;
 }return 0;
}
static void nextLevel(void){
 if(level==4){hasRun=0;ui=UI_WIN;SOUND_PlayVictory();drawVictory();return;}
 if(level==3&&!ability)return;
 clearLevelPowerups();
 startLevel((uint8_t)(level+1),0); /* Conservar HP y las vidas */
 writeSave();
}
static void handleUI(uint8_t press){
 if(ui==UI_PLAY){if(press&BTN_PAUSA)enterMenu();return;}
 if(ui==UI_EXIT){if(press&BTN_PAUSA){ui=UI_MENU;choice=0;drawMenu();}return;}
 if(ui==UI_WIN){if(press&(BTN_PAUSA|BTN_SALTO)){ui=UI_MENU;choice=0;drawMenu();}return;}
 if(ui==UI_DEAD){
  uint8_t count=deadCount();
  if(press&BTN_DERECHA){choice=(uint8_t)((choice+1)%count);drawDead();}
  else if(press&BTN_IZQUIERDA){choice=(uint8_t)((choice+count-1)%count);drawDead();}
  if(press&BTN_PAUSA){ui=UI_MENU;choice=0;drawMenu();return;}
  if(press&BTN_SALTO){
   if(lives){
    if(choice==0)startLevel(level,1);
    else if(choice==1)startNewGame();
    else{ui=UI_MENU;choice=0;drawMenu();}
   }else{
    if(choice==0)startNewGame();
    else{ui=UI_MENU;choice=0;drawMenu();}
   }
  }
  return;
 }
 if(ui==UI_MENU){
  if((press&BTN_PAUSA)&&hasRun){resume();return;}
  if(press&BTN_DERECHA){choice=(uint8_t)((choice+1)%menuCount());drawMenu();}
  else if(press&BTN_IZQUIERDA){choice=(uint8_t)((choice+menuCount()-1)%menuCount());drawMenu();}
  if(press&BTN_SALTO){
   if(choice==0)startNewGame();
   else if(choice==1 && (hasRun||saveAvailable)){if(hasRun)resume();else loadSavedGame();}
   else if(choice==godChoice()){
    setGodMode((uint8_t)!godEnabled());
    drawMenu();
   }else{hasRun=0;ui=UI_EXIT;drawExit();}
  }
 }
}
void GAME_Update(void){
 /* Lectura y borrado atomicos, por si UART llama GAME_SetControls
  * desde una interrupcion. Evita perder pulsaciones breves. */
 uint8_t press;
 uint32_t primask=__get_PRIMASK();
 __disable_irq();
 press=inputEdges;
 inputEdges=0;
 if(!primask)__enable_irq();

 /* GOD se controla solo desde el menu. No requiere combinaciones. */
 if(ui!=UI_PLAY){handleUI(press);return;}
 if(press&BTN_PAUSA){enterMenu();return;}
 uint32_t now=HAL_GetTick();if((uint32_t)(now-lastTick)<TICK_MS)return;lastTick=now;
 if((controls&BTN_IZQUIERDA)&&!(controls&BTN_DERECHA)){px-=3;face=-1;}
 else if((controls&BTN_DERECHA)&&!(controls&BTN_IZQUIERDA)){px+=3;face=1;}
 px=clamp(px,0,320-SW);
 uint8_t jump=(controls&BTN_SALTO)!=0;
 if(grounded&&jump&&!jumpPrev){vy=-16;grounded=0;}jumpPrev=jump;
 if(!grounded){int16_t feet=py+SH;vy++;py+=vy;
  if(py<37){py=37;vy=0;}
  if(vy>=0){uint8_t top=landsAt(feet,py+SH);if(top!=255){py=top-SH;vy=0;grounded=1;}}
 }else if(!onSupport())grounded=0;
 /* Recoger objetos. Las mejoras solo duran durante ESTE nivel. */
 Rect me=playerRect();
 if(medkit.present&&intersect(me,(Rect){medkit.x,medkit.y,medkit.w,medkit.h})){
  medkit.present=0;medUsed[level-1]=1;hp=(uint8_t)((hp+40>100)?100:hp+40);hud();}
 if(ammoPickup.present&&intersect(me,(Rect){ammoPickup.x,ammoPickup.y,ammoPickup.w,ammoPickup.h})){
  ammoPickup.present=0;ammoUsed[level-1]=1;specialType=1;specialCharges=5u;missiles=1;missileAmmo=5u;rapidBoost=0;damageBoost=0;hud();}
 if(abilityPickup.present&&intersect(me,(Rect){abilityPickup.x,abilityPickup.y,abilityPickup.w,abilityPickup.h})){
  abilityPickup.present=0;ability=1;abilityUsed=1;hud();}
 if(rapidPickup.present&&intersect(me,(Rect){rapidPickup.x,rapidPickup.y,rapidPickup.w,rapidPickup.h})){
  rapidPickup.present=0;rapidUsed[level-1]=1;specialType=2;specialCharges=5u;rapidBoost=0;damageBoost=0;missiles=0;missileAmmo=0;hud();}
 if(armorPickup.present&&intersect(me,(Rect){armorPickup.x,armorPickup.y,armorPickup.w,armorPickup.h})){
  armorPickup.present=0;armorUsed[level-1]=1;armorBoost=1;hud();}
 if(damagePickup.present&&intersect(me,(Rect){damagePickup.x,damagePickup.y,damagePickup.w,damagePickup.h})){
  damagePickup.present=0;damageUsed[level-1]=1;specialType=3;specialCharges=5u;rapidBoost=0;damageBoost=0;missiles=0;missileAmmo=0;hud();}
 if((press&BTN_POWERUP)&&!godEnabled()&&ability&&(int32_t)(now-abilityReadyAt)>=0){abilityOn=1;abilityUntil=now+3000u;abilityReadyAt=now+12000u;}
 if(abilityOn&&(int32_t)(now-abilityUntil)>=0){abilityOn=0;hud();}
 /* DISPARO CONTINUO: mantener pulsado dispara automaticamente.
  * Cuatro balas pueden coexistir sin esperar que desaparezca la anterior.
  * Rapido reduce el intervalo: 150 ms frente a 450 ms. */
 if((controls&BTN_DISPARO)&&(uint32_t)(now-lastShot)>=(rapidBoost?150u:450u)){
  for(uint8_t i=0;i<PLAYER_SHOTS;i++)if(!playerShots[i].alive){
   /* El disparo NORMAL nunca consume misiles. */
   playerShots[i]=(PlayerShot){face>0?px+SW:px-6,py+11,
      (int16_t)(face*(rapidBoost?8:5)),0,1,0};
   lastShot=now;break;
  }
 }
 /* CUADRADO: dispara el arma especial equipada (5 cargas).
  * La pulsacion pendiente se conserva hasta disponer de un slot.
  * No se pierde al coincidir con el cooldown.
  * Si no hay cargas, CUADRADO dispara el blaster ordinario. */
 if(missilePending && (uint32_t)(now-lastMissile)>=240u){
  for(uint8_t i=0;i<PLAYER_SHOTS;i++)if(!playerShots[i].alive){
   uint8_t type=(specialCharges>0u)?specialType:0u;
   uint8_t speed=(type==1u)?8u:((type==2u)?10u:((type==3u)?6u:5u));
   playerShots[i]=(PlayerShot){face>0?px+SW:px-(type?10:6),py+11,
      (int16_t)(face*speed),0,1,type};
   if(type){
    --specialCharges;
    missileAmmo=specialCharges;
    if(specialCharges==0u){specialType=0u;missiles=0u;rapidBoost=0u;damageBoost=0u;}
   }
   lastMissile=now;missilePending=0u;hudSpecial();break;
  }
 }
 for(uint8_t i=0;i<PLAYER_SHOTS;i++){
  PlayerShot *p=&playerShots[i];if(!p->alive)continue;
  int16_t dir=p->vx<0?-1:1;
  for(uint8_t step=0;step<((p->missile==1u)?8u:((p->missile==2u)?10u:((p->missile==3u)?6u:5u)))&&p->alive;step++){
   p->x+=dir;
   if(p->x<-12||p->x>320){p->alive=0;break;}
   for(uint8_t k=0;k<ENEMY_COUNT;k++){
    Enemy *e=&enemies[k];if(e->alive&&overlaps(p->x,p->y,p->missile?10:6,5,e->x,e->y,18,16)){
     uint8_t dmg=(p->missile==1u)?12u:((p->missile==2u)?5u:((p->missile==3u)?9u:1u));
     e->hp=e->hp>dmg?(uint8_t)(e->hp-dmg):0;e->flash=3u;
     if(!e->hp)e->alive=0;
     p->alive=0;break;
    }
   }
   if(p->alive&&bossAlive&&overlaps(p->x,p->y,p->missile?10:6,5,bossX,bossY,29,43)){
    uint8_t dmg=(p->missile==1u)?15u:((p->missile==2u)?6u:((p->missile==3u)?12u:1u));
    bossHp=bossHp>dmg?(uint8_t)(bossHp-dmg):0;
    p->alive=0;if(!bossHp){bossAlive=0;for(uint8_t k=0;k<SHOT_COUNT;k++)attacks[k].alive=0;}
    hudBoss();break;
   }
  }
 }
 /* Cinco enemigos: patrulla, volador, perseguidor, torreta y tanque. */
 for(uint8_t k=0;k<ENEMY_COUNT;k++){
  Enemy *e=&enemies[k];if(!e->alive)continue;
  if(e->flash)e->flash--;
  if(k==0){e->x+=e->vx;if(e->x<=e->lo||e->x>=e->hi)e->vx=(int16_t)-e->vx;}
  if(k==1){e->x+=e->vx;if(e->x<=e->lo||e->x>=e->hi)e->vx=(int16_t)-e->vx;
   e->y=(int16_t)(106+((now/120u)%16u));}
  if(k==2){if((now/90u)%2u==0u)e->x+=(px>e->x?1:-1);
   e->x=clamp(e->x,55,253);}
  if(k==3 && (uint32_t)(now-lastTurretShot)>=1300u){enemyFire(e,now);lastTurretShot=now;}
  if(k==4){if((now/90u)%2u==0u)e->x+=e->vx;
   if(e->x<=e->lo||e->x>=e->hi)e->vx=(int16_t)-e->vx;}
  if(!godEnabled() && hitsPlayer(e->x,e->y,18,16))damagePlayer((uint8_t)(10+level*2),now);
  if(ui!=UI_PLAY)return;
 }
 /* Contacto con hazard de suelo: obliga a saltar y usar plataformas. */
 if(py+SH>=213){for(uint8_t i=0;i<2;i++)if(!godEnabled() && hitsPlayer(hazardPos[level-1][i],211,22,9))damagePlayer((uint8_t)(9+level*2),now);}
 if(ui!=UI_PLAY)return;
 if(bossAlive){
  if((uint32_t)(now-bossMoveAt)>=((level==4)?45u:65u)){
   bossMoveAt=now;bossX+=bossV*(level>=3?3:2);
   if(bossX<232){bossX=232;bossV=1;}if(bossX>277){bossX=277;bossV=-1;}
  }
  if(!godEnabled() && hitsPlayer(bossX,bossY,29,43))damagePlayer((uint8_t)(20+level*3),now);
  if(ui!=UI_PLAY)return;
  if((uint32_t)(now-bossAttackAt)>=(uint32_t)(1700-level*240)){
   bossAttackAt=now;bossPattern++;
   /* Disparo apuntado a la posicion actual de Samus. */
   spawnAttack(px+SW/2,py+SH/2,(int16_t)(7+level));
   if(level>=2){spawnAttack(px+SW/2,py+SH/2+(bossPattern&1?25:-25),(int16_t)(6+level));}
   if(level==4){spawnAttack(px+SW/2,py+SH/2+38,10);}
  }
 }
 for(uint8_t i=0;i<SHOT_COUNT;i++){
  if(!attacks[i].alive)continue;
  moveAttack(&attacks[i],now);
  if(ui!=UI_PLAY)return;
 }
 /* Fisica 30 ms, pantalla ~60 ms: conserva respuesta y reduce 50%
  * de transferencias y calculos graficos. Snapshot = ultimo dibujo. */
 if((uint32_t)(now-lastRender)>=60u){
  drawChanges();
  capture(before);
  lastRender=HAL_GetTick();
 }
 if(!bossAlive&&px+SW>=303&&py+SH>=182){nextLevel();return;}
}
