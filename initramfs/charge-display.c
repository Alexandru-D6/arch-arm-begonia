/* Minimal fbdev charging card. No rootfs, fonts or graphics libraries needed. */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static struct fb_var_screeninfo v;
static struct fb_fix_screeninfo f;
static unsigned char *pixels;
static uint32_t color(unsigned r, unsigned g, unsigned b)
{
    return ((r >> (8-v.red.length)) << v.red.offset) |
           ((g >> (8-v.green.length)) << v.green.offset) |
           ((b >> (8-v.blue.length)) << v.blue.offset);
}
static void rect(int x, int y, int w, int h, uint32_t c)
{
    for (int j=y; j<y+h; j++) for (int i=x; i<x+w; i++) {
        if (i<0 || j<0 || i>=(int)v.xres || j>=(int)v.yres) continue;
        size_t pos=(size_t)(j+v.yoffset)*f.line_length+(i+v.xoffset)*(v.bits_per_pixel/8);
        if (pos+v.bits_per_pixel/8 > f.smem_len) continue;
        if (v.bits_per_pixel==32) memcpy(pixels+pos,&c,4);
        else { uint16_t c16=c; memcpy(pixels+pos,&c16,2); }
    }
}
/* 5x7 font, enough for voltage, percentage and an explicit estimate label. */
static const char alphabet[]="0123456789.%VEST -?";
static const unsigned char glyphs[][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},{0,0,0,0,0,12,12},{17,2,4,8,16,17,0},
 {17,17,17,17,17,10,4},{31,16,16,30,16,16,31},{15,16,16,14,1,1,30},
 {31,4,4,4,4,4,4},{0,0,0,0,0,0,0},{0,0,0,31,0,0,0},
 {14,17,1,2,4,0,4}};
static void text(const char *s,int y,int scale,uint32_t c)
{
    int x=((int)v.xres-(int)strlen(s)*6*scale)/2;
    for (;*s;s++,x+=6*scale) {
        const char *p=strchr(alphabet,*s);
        if (!p) continue;
        for(int j=0;j<7;j++) for(int i=0;i<5;i++)
            if(glyphs[p-alphabet][j] & (1<<(4-i))) rect(x+i*scale,y+j*scale,scale,scale,c);
    }
}
int main(int argc,char **argv)
{
    int tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);
    if(argc==2 && !strcmp(argv[1],"--restore")) {
        if(tty>=0) { ioctl(tty,KDSETMODE,KD_TEXT); close(tty); }
        return 0;
    }
    if(argc!=4) return 2;
    int percent=atoi(argv[1]), mv=atoi(argv[2]), charging=atoi(argv[3]);
    int fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);
    if(fd<0 || ioctl(fd,FBIOGET_VSCREENINFO,&v) || ioctl(fd,FBIOGET_FSCREENINFO,&f)) return 1;
    if((v.bits_per_pixel!=32 && v.bits_per_pixel!=16) || v.xres<240 || v.yres<320 ||
       !v.red.length || v.red.length>8 || !v.green.length || v.green.length>8 ||
       !v.blue.length || v.blue.length>8 || f.visual!=FB_VISUAL_TRUECOLOR) return 1;
    pixels=mmap(NULL,f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(pixels==MAP_FAILED) return 1;
    if(tty>=0) ioctl(tty,KDSETMODE,KD_GRAPHICS);
    rect(0,0,v.xres,v.yres,0);
    int s=v.xres/120, x=v.xres/2-30*s, y=v.yres/2-50*s;
    uint32_t white=color(210,220,230), green=color(70,210,130);
    rect(x+20*s,y-5*s,20*s,5*s,white);
    rect(x,y,60*s,90*s,white); rect(x+3*s,y+3*s,54*s,84*s,0);
    /* Voltage is a coarse estimate under charge, never an invented percentage. */
    int bars=percent>=0 ? (percent+19)/20 : mv>0 ? 1+(mv>=3650)+(mv>=3800)+(mv>=4000)+(mv>=4250) : 0;
    if(bars>5) bars=5;
    for(int n=0;n<bars;n++) rect(x+7*s,y+(73-16*n)*s,46*s,11*s,charging?green:white);
    char label[40];
    if(percent>=0) snprintf(label,sizeof(label),"%d%%",percent);
    else if(mv>0) snprintf(label,sizeof(label),"%d.%02dV",mv/1000,(mv%1000)/10);
    else snprintf(label,sizeof(label),"?%%");
    text(label,y+105*s,s,white);
    if(percent<0 && mv>0) text("EST.",y+118*s,s,white);
    /* Small lightning bolt indicates actual Charging status. */
    if(charging) { rect(x+29*s,y+134*s,6*s,10*s,green); rect(x+25*s,y+142*s,7*s,10*s,green); }
    munmap(pixels,f.smem_len); close(fd); if(tty>=0) close(tty);
    return 0;
}
