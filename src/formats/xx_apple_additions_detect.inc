/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Cheap conservative candidates. Final native parsing remains mandatory.
 * Ambiguous signatureless layouts deliberately require an explicit reader.
 */
#include "xxfclib/rt/xx_rt.h"
static bool xx_apple_addition_read(xx_io_device *d,int64_t at,void *p,size_t n) {
    int64_t saved=xx_io_tell(d);bool ok=false;
    if(saved<0)return false;
    if(at>=0 && xx_io_seek64(d,at,SEEK_SET)==0 && xx_io_read(d,p,n)==(ssize_t)n)ok=true;
    if(xx_io_seek64(d,saved,SEEK_SET)!=0) {ok=false; } return ok;
}
static bool xx_apple_addition_candidate(uint32_t type,xx_io_device *d,const uint8_t *first512,int64_t first_size,int64_t source_total) {
    uint8_t h[660];const uint8_t *p=first512;int64_t base=0,size=source_total;uint32_t i;
    if(!d || !p || first_size<0 || source_total<first_size)return false;
    switch(type) {
    case XX_FILE_TYPE_APPLELINK_PE:return first_size>=20 && !xx_rt_memcmp(p+4,"fZink",5) && p[9]==1 && p[10]==54 && !p[11];
    case XX_FILE_TYPE_TRACKSTAR:return first_size>=129 && (size==266240 || size==532480) && p[128]<2;
    case XX_FILE_TYPE_GUTENBERG:
        if(size!=143360 || !xx_apple_addition_read(d,base+(17*16+7)*256,h,256))return false;
        if(h[2]==17 && h[3]==0x87 && h[16]==0xc4 && h[17]==0xc9 && h[18]==0xd2 && h[15]==0x8d)return true;
        if(!xx_apple_addition_read(d,base+(17*16+8)*256,h,256))return false;
        return h[2]==17 && h[3]==0x87 && h[16]==0xc4 && h[17]==0xc9 && h[18]==0xd2 && h[15]==0x8d;
    case XX_FILE_TYPE_APPLE_RDOS:
        if(size!=116480 && size!=143360)return false;
        if(!xx_apple_addition_read(d,base+(size==116480?13:16)*256,h,32))return false;
        for(i=0;i<24;++i)h[i]&=0x7fU;
        return !xx_rt_memcmp(h,"RDOS 2.1 COPYRIGHT 1981",23) || !xx_rt_memcmp(h,"RDOS 3.3 COPYRIGHT 1986",23) ||
               !xx_rt_memcmp(h,"SSI SAVE GAME DISK RDOS",23) || !xx_rt_memcmp(h,">-SSI GAME SAVE DISK-<",21);
    case XX_FILE_TYPE_FOCUSDRIVE:return first_size>=16 && size>=1536 && !xx_rt_memcmp(p,"Parsons Engin.",14) && p[15]>=1 && p[15]<=30;
    case XX_FILE_TYPE_MICRODRIVE:return first_size>=14 && size>=131072 && p[0]==0xca && p[1]==0xcc && p[12]<=8 && p[13]<=8 && (p[12] || p[13]);
    case XX_FILE_TYPE_MAC_TS:
        return first_size>=4 && size>=1536 && p[0]=='E' && p[1]=='R' && p[2]==2 && !p[3] &&
            xx_apple_addition_read(d,base+512,h,512) && h[0]=='T' && h[1]=='S' && h[10]=='T' && h[11]=='F' && h[12]=='S' && h[13]=='1';
    case XX_FILE_TYPE_PASCAL_PROFILE_MANAGER:
        /* PASCAL.AREA is a root entry, so bounded root-directory probing is
         * still done by the native reader; a ProDOS header is only a hint. */
        if(size<4096 || size%512 || !xx_apple_addition_read(d,base+1024,h,512))return false;
        if((h[4]&0xf0U)!=0xf0U || h[35]!=39 || h[36]!=13)return false;
        for(i=1;i<13;++i){const uint8_t *e=h+4+i*39;if(e[0]==0x4b && e[16]==0xef && !xx_rt_memcmp(e+1,"PASCAL.AREA",11))return true;}return false;
    case XX_FILE_TYPE_OPERA_FS:return first_size>=7 && size>=2048 && p[0]==1 && p[1]==0x5a && p[2]==0x5a && p[3]==0x5a && p[4]==0x5a && p[5]==0x5a && p[6]==1;
    case XX_FILE_TYPE_CDI_FS:
        if(size>=17*2048 && xx_apple_addition_read(d,base+16*2048,h,512) && h[0]==1 && !xx_rt_memcmp(h+1,"CD-I ",5) && h[6]==1)return true;
        return size>=17*2352 && xx_apple_addition_read(d,base+16*2352+24,h,512) && h[0]==1 && !xx_rt_memcmp(h+1,"CD-I ",5) && h[6]==1;
    case XX_FILE_TYPE_BSD_DISKLABEL:
        for(i=0;i<8192 && (int64_t)i+148<=size;i+=512){uint32_t j,n=(uint32_t)(size-i>660?660:size-i);
            if(n>8192U-i) {n=8192U-i; } if(!xx_apple_addition_read(d,base+i,h,n))break;
            for(j=0;j<512 && j+148<=n;j+=4)if((h[j]==0x57 && h[j+1]==0x45 && h[j+2]==0x56 && h[j+3]==0x82 && !xx_rt_memcmp(h+j,h+j+132,4)) ||
                (h[j]==0x82 && h[j+1]==0x56 && h[j+2]==0x45 && h[j+3]==0x57 && !xx_rt_memcmp(h+j,h+j+132,4)))return true;}return false;
    case XX_FILE_TYPE_LISA_FS:return first_size>=84 && size>=84+512 && p[0]<=63 && p[82]==1 && !p[83] &&
        xx_apple_addition_read(d,base+84,h,512) && h[4]==0xaa && h[5]==0xaa && h[6]==8 && h[7]==0x50;
    case XX_FILE_TYPE_APPLE_CASSETTE:return first_size>=12 && size>=44 && !xx_rt_memcmp(p,"RIFF",4) && !xx_rt_memcmp(p+8,"WAVE",4);
    /* These share untagged geometry. No extension-only or size-only automatic
     * detection claims a particular layout. Explicit profiles validate files. */
    case XX_FILE_TYPE_AMDOS:case XX_FILE_TYPE_OZDOS:case XX_FILE_TYPE_UNIDOS:case XX_FILE_TYPE_CFFA:
    case XX_FILE_TYPE_APPLE_DOS_HYBRID:case XX_FILE_TYPE_DOS_MASTER:return false;
    default:return false;
    }
}
