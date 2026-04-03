/*
 * animation.c — wizard faithfully traced from hand-drawn sketch
 * Includes: hat, face, long flowing hair strands, shoulders/collar, pipe+swirl
 * Smoke animated from pipe swirl.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "animation.h"

#define HIDE_CUR "\033[?25l"
#define SHOW_CUR "\033[?25h"
#define CLR_SCR  "\033[2J\033[H"
#define AX       "\033[0m"

#define CH   "\033[38;5;57m"     /* hat indigo          */
#define CHB  "\033[38;5;99m"     /* brim bright indigo  */
#define CK   "\033[38;5;69m"     /* tick/dash slate     */
#define CST1 "\033[38;5;220m"    /* star gold  (anim)   */
#define CST2 "\033[38;5;214m"    /* star orange         */
#define CST3 "\033[38;5;178m"    /* star amber          */
#define CF   "\033[38;5;223m"    /* skin                */
#define CE   "\033[1;36m"        /* eyes open           */
#define CE2  "\033[0;36m"        /* eyes blink          */
#define CHR  "\033[38;5;245m"    /* hair/beard grey     */
#define CP   "\033[38;5;136m"    /* pipe brown          */
#define CG1  "\033[38;5;202m"    /* ember bright (anim) */
#define CG2  "\033[38;5;208m"    /* ember mid           */
#define CG3  "\033[38;5;160m"    /* ember dim           */
#define CRB  "\033[38;5;54m"     /* robe/shoulders      */
#define SM0  "\033[38;5;255m"
#define SM1  "\033[38;5;250m"
#define SM2  "\033[38;5;244m"
#define SM3  "\033[38;5;238m"

#define RBUF (1<<17)
static char rb[RBUF];
static int  rp;

static void rb_reset(void) { rp = 0; }
static void rb_p(const char *fmt, ...)
{
    va_list a; va_start(a,fmt);
    int n = vsnprintf(rb+rp,(size_t)(RBUF-rp),fmt,a);
    va_end(a);
    if(n>0 && rp+n<RBUF) rp+=n;
}
static void rb_cat(const char *s)
{
    size_t l=strlen(s);
    if(rp+(int)l+1<RBUF){memcpy(rb+rp,s,l);rp+=(int)l;}
}
#define AT(r,c,...) do{ rb_p("\033[%d;%dH",(r),(c)); rb_p(__VA_ARGS__); }while(0)

static void erase(int r0,int c0,int rows,int cols)
{
    char bl[300]; int n=cols<299?cols:299;
    memset(bl,' ',(size_t)n); bl[n]='\0';
    for(int r=0;r<rows;r++) rb_p("\033[%d;%dH%s",r0+r,c0,bl);
}

/* particles */
#define MAXP 100
#define FPS  24
#define FNS  (1000000000L/FPS)

typedef struct { float x,y,vx,vy,ph; int age,life,t; } Ptcl;
static Ptcl ps[MAXP];
static int np=0;
static unsigned seed=0xFACEB00Cu;
static unsigned rn(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static float rf(void){return(float)(rn()&0xFFFF)/65536.f;}
static float rr(float a,float b){return a+rf()*(b-a);}

static void spawn(float x,float y)
{
    if(np>=MAXP) return;
    Ptcl *p=&ps[np++];
    p->x=x+rr(-.3f,.3f); p->y=y;
    p->vx=rr(-.12f,.22f); p->vy=rr(-.50f,-.18f);
    p->ph=rr(0.f,6.28f);
    p->age=0; p->life=(int)rr(20.f,50.f);
    p->t=(int)(rn()%4);
}
static void pupdate(void)
{
    for(int i=0;i<np;){
        Ptcl *p=&ps[i];
        p->ph+=.18f;
        p->x+=p->vx+.06f*sinf(p->ph);
        p->y+=p->vy; p->vy+=.007f;
        p->vx+=rr(-.025f,.025f);
        if(++p->age>=p->life){ps[i]=ps[--np];}else{i++;}
    }
}
static const char *pch[4][5]={
    {"o","o",".","."," "},
    {"O","o","o","."," "},
    {"*","+",".","."," "},
    {"@","o",".","."," "},
};
static void pdraw(void)
{
    for(int i=0;i<np;i++){
        Ptcl *p=&ps[i];
        int row=(int)(p->y+.5f),col=(int)(p->x+.5f);
        if(row<1||col<1) continue;
        float f=(float)p->age/(float)p->life;
        int ci=(int)(f*4.f); if(ci>4) ci=4;
        const char *c=pch[p->t][ci];
        const char *cl=f<.25f?SM0:f<.5f?SM1:f<.75f?SM2:SM3;
        rb_p("\033[%d;%dH%s%s"AX,row,col,cl,c);
    }
}

static const char *HKEYS=
    "\033[38;5;70m[+/-]\033[0m Vol  "
    "\033[38;5;70m[L/l]\033[0m LFO  "
    "\033[38;5;70m[D/d]\033[0m Depth  "
    "\033[38;5;70m[R/r]\033[0m Reverb  "
    "\033[38;5;70m[q]\033[0m Quit";

static void hud(int rows,app_state_t *st)
{
    char buf[256];
    pthread_mutex_lock(&st->lock);
    if(st->hud_line[0])
        snprintf(buf,sizeof(buf),"\033[1;37m%.200s\033[0m",st->hud_line);
    else
        snprintf(buf,sizeof(buf),
            "\033[38;5;99m Vol:%.2f LFO:%.2fHz/%.2f Rev:%.2f \033[0m",
            (double)st->volume,(double)st->lfo_rate,
            (double)st->lfo_depth,(double)st->reverb_mix);
    pthread_mutex_unlock(&st->lock);
    rb_p("\033[%d;1H%s",rows-2,buf);
    rb_p("\033[%d;1H%s",rows-1,HKEYS);
}

/*
 * WIZARD — full sketch trace including hair, shoulders, pipe bowl
 *
 * Columns indexed from wc (left edge of block, width ~24):
 *
 *  0         1         2
 *  0123456789012345678901234
 *
 *  r0:          *                  col wc+10  star
 *  r1:        - / \ -              dashed hat sides
 *  r2:       - /   \ -
 *  r3:      | /  /\ \ |            tick marks, inner /\ chevron
 *  r4:      |/   \/  \|            V chevron bottom
 *  r5:     /|    |    |\           widening, tick marks
 *  r6:    ====================     brim top
 *  r7:    ====================     brim bottom
 *  r8:   \  // +   + \\  /         hair strands flanking, + + eyes
 *  r9:   \  |   (o)   |  /         hair continues, round nose δ
 *  r10:  \  |  . ==(  |  /         hair, mouth dot, pipe stem, bowl
 *  r11:   \ |          | /         hair lower
 *  r12:    \|          |/          hair meets shoulders
 *  r13:   __\__________/__         shoulder line with V collar notch
 *
 * Pipe bowl ( is at r10, col wc+13. Smoke exits from there rightward.
 * PIPE_DR=10, PIPE_DC=13
 */

#define WR  14
#define WC  22
#define PIPE_DR 10
#define PIPE_DC 13

static void draw_wiz(int wr,int wc,int frame,int tick)
{
    /* star colour cycle */
    const char *sc;
    switch((tick/5)%3){case 0:sc=CST1;break;case 1:sc=CST2;break;default:sc=CST3;}

    /* eyes */
    const char *ec; char el,er;
    switch(frame){
        case 1: ec=CE;  el=er='+'; break;
        case 3: ec=CE2; el=er='-'; break;
        default:ec=CE;  el=er='+'; break;
    }

    /* ember flicker */
    const char *gl;
    switch((tick/2)%3){case 0:gl=CG1;break;case 1:gl=CG2;break;default:gl=CG3;}

    /* r0 — star */
    AT(wr+0, wc+10, "%s*"AX, sc);

    /* r1 — dashed hat sides */
    AT(wr+1, wc+7,  CK"-"AX CH"/"AX);
    AT(wr+1, wc+12, CH"\\"AX CK"-"AX);

    /* r2 */
    AT(wr+2, wc+5,  CK"-"AX CH"/"AX);
    AT(wr+2, wc+13, CH"\\"AX CK"-"AX);

    /* r3 — inner tick marks + /\ chevron */
    AT(wr+3, wc+3,  CK"|"AX);
    AT(wr+3, wc+5,  CH"/"AX);
    AT(wr+3, wc+8,  CH"/\\"AX);
    AT(wr+3, wc+12, CH"\\"AX);
    AT(wr+3, wc+15, CK"|"AX);

    /* r4 — V chevron bottom, side bars */
    AT(wr+4, wc+3,  CK"|"AX);
    AT(wr+4, wc+5,  CH"/"AX);
    AT(wr+4, wc+8,  CH"\\/"AX);
    AT(wr+4, wc+12, CH"\\"AX);
    AT(wr+4, wc+15, CK"|"AX);

    /* r5 — wide base of hat cone, tick marks on sides */
    AT(wr+5, wc+2,  CH"/"AX);
    AT(wr+5, wc+4,  CK"|"AX);
    AT(wr+5, wc+9,  CK"|"AX);
    AT(wr+5, wc+14, CK"|"AX);
    AT(wr+5, wc+16, CH"\\"AX);

    /* r6 — brim top */
    AT(wr+6, wc+1,  CHB"===================="AX);

    /* r7 — brim bottom */
    AT(wr+7, wc+1,  CHB"===================="AX);

    /* r8 — flowing hair both sides + eyes
     * Left hair: \ strokes at wc+0, wc+2
     * Right hair: / strokes at wc+18, wc+20
     * Eyes at centre */
    AT(wr+8, wc+0,  CHR"\\"AX);
    AT(wr+8, wc+2,  CHR"\\"AX);
    AT(wr+8, wc+6,  "%s%c"AX, ec, el);
    AT(wr+8, wc+10, "%s%c"AX, ec, er);
    AT(wr+8, wc+18, CHR"/"AX);
    AT(wr+8, wc+20, CHR"/"AX);

    /* r9 — hair continues + nose (δ = circle-with-tail, sketch shows (o) round face) */
    AT(wr+9, wc+0,  CHR"\\"AX);
    AT(wr+9, wc+2,  CHR"\\"AX);
    AT(wr+9, wc+7,  CF"("AX CF"o"AX CF")"AX);
    AT(wr+9, wc+18, CHR"/"AX);
    AT(wr+9, wc+20, CHR"/"AX);

    /* r10 — hair + mouth dot + pipe: . == ( bowl  — smoke exits from bowl */
    AT(wr+10, wc+0,  CHR"\\"AX);
    AT(wr+10, wc+2,  CHR"\\"AX);
    AT(wr+10, wc+6,  CF"."AX);           /* mouth dot */
    AT(wr+10, wc+8,  CP"=="AX);          /* pipe stem */
    AT(wr+10, wc+10, "%s"CP"("AX, gl);   /* pipe bowl with ember glow */
    AT(wr+10, wc+18, CHR"/"AX);
    AT(wr+10, wc+20, CHR"/"AX);

    /* r11 — hair lower strands */
    AT(wr+11, wc+0,  CHR"\\"AX);
    AT(wr+11, wc+3,  CHR"\\"AX);
    AT(wr+11, wc+17, CHR"/"AX);
    AT(wr+11, wc+20, CHR"/"AX);

    /* r12 — hair meets edge, converging to shoulders */
    AT(wr+12, wc+1,  CHR"\\"AX);
    AT(wr+12, wc+4,  CHR"\\"AX);
    AT(wr+12, wc+16, CHR"/"AX);
    AT(wr+12, wc+19, CHR"/"AX);

    /* r13 — shoulder line: flat bar with V collar notch in middle */
    AT(wr+13, wc+0,  CRB"______"AX CF"\\  /"AX CRB"______"AX);
}

void anim_init(anim_state_t *a)
{
    (void)a;
    ssize_t r=write(STDOUT_FILENO,HIDE_CUR CLR_SCR,strlen(HIDE_CUR CLR_SCR));
    (void)r;
}
void anim_cleanup(anim_state_t *a)
{
    (void)a;
    ssize_t r=write(STDOUT_FILENO,SHOW_CUR"\033[999;1H\n",
                    strlen(SHOW_CUR"\033[999;1H\n"));
    (void)r;
}

void *anim_thread(void *arg)
{
    anim_state_t *a=(anim_state_t*)arg;
    struct timespec ts={.tv_sec=0,.tv_nsec=FNS};
    int frame=0,tick=0,stk=FPS/5;

    while(a->state->running){
        struct winsize ws;
        if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)<0){ws.ws_row=24;ws.ws_col=80;}
        int rows=ws.ws_row, cols=ws.ws_col;

        int wr=(rows-WR)/2, wc=(cols-WC)/2;
        if(wr<4){wr=4;} if(wc<4){wc=4;}

        /* smoke exits from pipe bowl: row PIPE_DR, col PIPE_DC */
        float sr=(float)(wr+PIPE_DR);
        float sc2=(float)(wc+PIPE_DC);

        int er=wr-35; if(er<1){er=1;}
        erase(er, wc-2, rows-er-2, WC+10);

        rb_reset();

        pupdate();
        if((tick%stk)==0){
            spawn(sc2, sr);
            spawn(sc2+rr(-.4f,.4f), sr-.2f);
        }
        if((tick%(stk*2))==0){
            spawn(sc2+rr(-.2f,.5f), sr-.4f);
        }

        pdraw();
        draw_wiz(wr,wc,frame,tick);
        hud(rows,a->state);
        rb_cat(AX HIDE_CUR);

        ssize_t done=0;
        while(done<rp){
            ssize_t n=write(STDOUT_FILENO,rb+done,(size_t)(rp-done));
            if(n<=0){break;}
            done+=n;
        }

        tick++; frame=(frame+1)&3;
        nanosleep(&ts,NULL);
    }
    return NULL;
}
