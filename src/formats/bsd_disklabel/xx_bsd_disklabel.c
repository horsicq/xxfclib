/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from FreeBSD disk/bsd.h layout facts.
 * Classic BSD disklabels: either endian, dual magic, XOR checksum, bounded
 * partitions. Partition payloads retain their declared bytes; no FS guessed.
 */
#include "xxfclib/formats/bsd_disklabel/xx_bsd_disklabel.h"
#include "../apple_family/xx_apple_family_private.h"
static uint32_t dl32(const uint8_t *p,bool big) { return big ? pm_be32(p):pm_le32(p); }
static uint16_t dl16(const uint8_t *p,bool big) { return big ? pm_be16(p):pm_le16(p); }
static bool dl_header(af_work *w,const uint8_t *p,size_t available,bool big,uint32_t *sec,uint32_t *parts) {
    uint32_t units,i;uint16_t checksum=0;
    if(available<148U || dl32(p,big)!=UINT32_C(0x82564557) || dl32(p+132,big)!=UINT32_C(0x82564557))return false;
    *sec=dl32(p+40,big);*parts=dl16(p+138,big);units=dl32(p+60,big);
    if(*sec<128U || *sec>65536U || (*sec&(*sec-1U)) || !*parts || *parts>64U ||
       148U+(uint64_t)*parts*16U>available || !units || (uint64_t)units**sec>(uint64_t)pm_available(w->f))return false;
    for(i=0;i<148U+*parts*16U;i+=2U)checksum^=dl16(p+i,big);
    if(checksum)return false;
    for(i=0;i<*parts;++i) { const uint8_t *t=p+148U+i*16U;uint32_t n=dl32(t,big),at=dl32(t+4,big);
        if(n && (at>units || n>units-at || (uint64_t)(at+n)**sec>(uint64_t)pm_available(w->f)))return false; }
    return af_poll(w);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    af_work w;uint8_t probe[8192];size_t n;uint32_t at,found=UINT32_MAX,sec=0,parts=0,i;bool big=false;
    if(!af_init(&w,f,s,pd) || w.limit<sizeof(probe))return false;
    n=(uint64_t)pm_available(f)>sizeof(probe)?sizeof(probe):(size_t)pm_available(f);
    if(!af_read(&w,0,probe,n))return false;
    for(at=0;at+148U<=n;at+=4U) { bool endian;uint32_t ss,pp;
        if(pm_le32(probe+at)==UINT32_C(0x82564557))endian=false;
        else if(pm_be32(probe+at)==UINT32_C(0x82564557))endian=true;else continue;
        if(!dl_header(&w,probe+at,n-at,endian,&ss,&pp))continue;
        if(found!=UINT32_MAX) {return false; } found=at;sec=ss;parts=pp;big=endian;
    }
    if(found==UINT32_MAX)return false;
    for(i=0;i<parts;++i) {const uint8_t *t=probe+found+148U+i*16U;uint32_t z=dl32(t,big),a=dl32(t+4,big);char name[48];
        if(!z || !t[12])continue;
        xx_rt_snprintf(name,sizeof(name),"partition-%02u-type-%u.img",i,t[12]);
        if(!af_add(&w,name,(int64_t)a*sec,(uint64_t)z*sec,NULL))return false;
    }
    ((xx_bsd_disklabel *)f)->note=big?"big-endian BSD label; XOR verified; declared partitions":"little-endian BSD label; XOR verified; declared partitions";
    ((xx_bsd_disklabel *)f)->number_of_records=s->count;s->size=pm_available(f);
    return af_poll(&w);
}
AF_DEFINE_READER(bsd_disklabel,XX_FILE_TYPE_BSD_DISKLABEL,"img")

