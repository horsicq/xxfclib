/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout facts: https://ciderpress2.com/formatdoc/{DOS800,CFFA,FocusDrive,
 * MicroDrive,MacTS,PPM,Hybrid}-notes.html. Original native implementation.
 */
#ifndef XX_APPLE_LAYOUTS_PRIVATE_H
#define XX_APPLE_LAYOUTS_PRIVATE_H
#include "xx_apple_volumes.h"
#include "xxfclib/data/xx_data.h"
typedef struct al_part { uint64_t at,size;unsigned kind; } al_part;
static bool al_parts(af_work *w,const al_part *parts,unsigned count,uint64_t header) {
    unsigned i,j;int64_t n=pm_available(w->f);char prefix[24];
    if(!count || count>32U || n<0)return false;
    for(i=0;i<count;++i){if(parts[i].at<header || !parts[i].size || parts[i].at>(uint64_t)n || parts[i].size>(uint64_t)n-parts[i].at)return false;
        for(j=0;j<i;++j)if(parts[i].at<parts[j].at+parts[j].size && parts[j].at<parts[i].at+parts[i].size)return false;}
    for(i=0;i<count;++i){xx_rt_snprintf(prefix,sizeof(prefix),"VOLUME%02u",i+1U);if(!av_extent(w,parts[i].at,parts[i].size,prefix,parts[i].kind))return false;}
    return true;
}
static bool al_empty(af_work *w,uint64_t at,uint64_t n) {
    uint8_t block[4096];while(n){size_t size=n>sizeof(block)?sizeof(block):(size_t)n;
        if(at>INT64_MAX || !af_read(w,(int64_t)at,block,size) || !af_zero(block,size)) {return false; } at+=size;n-=size;}return true;
}
static bool al_dos800(af_work *w,unsigned kind) {
    al_part p[2]={{0,409600,5},{409600,409600,5}};
    if(pm_available(w->f)!=819200)return false;
    if(kind!=2U)return al_parts(w,p,2,0);
    {uint8_t *split=af_alloc(w,409600,false);uint8_t pair[512];unsigned side;uint32_t block;bool ok=false;
        if(!split)return false;
        for(side=0;side<2U;++side){xx_io_device *d;char prefix[24];
            for(block=0;block<1600U;++block){if(!af_read(w,(int64_t)block*512,pair,sizeof(pair)))goto done;xx_rt_memcpy(split+block*256U,pair+side*256U,256);}
            d=xx_io_mem_open_ro(split,409600);if(!d)goto done;xx_rt_snprintf(prefix,sizeof(prefix),"VOLUME%02u",side+1U);
            ok=av_volume(w,d,prefix,5);xx_io_close(d);if(!ok)goto done;}
        ok=true;
done:af_release(w,split,409600);return ok;}
}
static bool al_focus(af_work *w) {
    uint8_t h[512];al_part p[30];unsigned i,count;
    if(!af_read(w,0,h,sizeof(h)) || xx_rt_memcmp(h,"Parsons Engin.",14) || !(count=h[15]) || count>30U)return false;
    for(i=0;i<count;++i){const uint8_t *e=h+32+i*16U;if(!af_zero(e+8,8))return false;
        p[i].at=(uint64_t)xx_data_get_u32(e, 4, 0, false)*512U;p[i].size=(uint64_t)xx_data_get_u32(e+4, 4, 0, false)*512U;p[i].kind=0;}
    return al_parts(w,p,count,1536);
}
static bool al_micro(af_work *w) {
    uint8_t h[512];al_part p[16];unsigned i,j=0,first,second;
    if(!af_read(w,0,h,sizeof(h)) || xx_data_get_u16(h, 2, 0, false)!=0xcccaU || (first=h[12])>8U || (second=h[13])>8U || !(first+second))return false;
    for(i=0;i<first+second;++i){unsigned slot=i<first?i:i-first;unsigned starts=i<first?32U:128U,sizes=i<first?64U:160U;
        p[j].at=(uint64_t)xx_data_get_u32(h+starts+slot*4U, 4, 0, false)*512U;p[j].size=(uint64_t)(xx_data_get_u32(h+sizes+slot*4U, 4, 0, false)&0xffffffU)*512U;p[j++].kind=0;}
    return al_parts(w,p,j,256U*512U);
}
static bool al_ts(af_work *w) {
    uint8_t h[1024];al_part p[42];unsigned count=0,at;uint32_t blocks;bool ended=false;
    if(!af_read(w,0,h,sizeof(h)) || xx_data_get_u16(h, 2, 0, true)!=0x4552U || xx_data_get_u16(h+2, 2, 0, true)!=512U || xx_data_get_u16(h+512, 2, 0, true)!=0x5453U)return false;
    blocks=xx_data_get_u32(h+4, 4, 0, true);if(!blocks || (uint64_t)blocks*512U>(uint64_t)pm_available(w->f))return false;
    for(at=514;at+12U<=1024;at+=12U){uint32_t start=xx_data_get_u32(h+at, 4, 0, true),size=xx_data_get_u32(h+at+4, 4, 0, true),fs=xx_data_get_u32(h+at+8, 4, 0, true);
        /* Historical producers may leave garbage after a zero start. */
        if(!start){ended=true;break;}
        if(fs!=0x54465331U || !size || start>=blocks || size>blocks-start || count>=42U)return false;
        p[count].at=(uint64_t)start*512U;p[count].size=(uint64_t)size*512U;p[count++].kind=3;}
    return ended && al_parts(w,p,count,1024);
}
static bool al_cffa(af_work *w,uint32_t profile) {
    const uint64_t slot=UINT64_C(32)*1024U*1024U,gig=UINT64_C(1024)*1024U*1024U;
    int64_t total=pm_available(w->f);unsigned i,count;uint64_t at=0;bool found=false;
    if(total<0)return false;
    if(!profile){if((uint64_t)total==4U*slot || (uint64_t)total+512U==4U*slot)profile=4;
        else if((uint64_t)total==8U*slot || (uint64_t)total+512U==8U*slot)profile=8;else return false;}
    if(profile!=4U && profile!=6U && profile!=8U) {return false; } count=profile;
    for(i=0;i<count;++i){uint64_t size=(profile==6U && i>=4U)?gig:slot;uint8_t h[512];char prefix[24];
        if(at>=(uint64_t)total){if(profile==6U && i>=4U)break;return false;}
        if(size>(uint64_t)total-at)size=(uint64_t)total-at;
        if(size<3072U || !af_read(w,(int64_t)at+1024,h,sizeof(h)))return false;
        if(af_zero(h,sizeof(h))){if(!al_empty(w,at,size))return false;}
        else {unsigned kind=(h[4]&0xf0U)==0xf0U && h[35]==39U && h[36]==13U?1U:h[0]=='B' && h[1]=='D'?3U:0U;
            if(!kind) {return false; } xx_rt_snprintf(prefix,sizeof(prefix),"VOLUME%02u",i+1U);if(!av_extent(w,at,size,prefix,kind))return false;found=true;}
        at+=size;}
    return found && at==(uint64_t)total;
}
static bool al_ppm(af_work *w) {
    uint8_t h[512],map[1024],seen[8192];uint32_t block=2,last=0,area=0,used=0,total;unsigned count,i;al_part p[31];
    if(!af_read(w,1024,h,sizeof(h)) || (h[4]&0xf0U)!=0xf0U || h[35]!=39U || h[36]!=13U || !(total=xx_data_get_u16(h+41, 2, 0, false)) || (uint64_t)total*512U>(uint64_t)pm_available(w->f))return false;
    xx_mem_zero(seen,sizeof(seen));
    while(block){if(block>=total || (seen[block>>3]&(1U<<(block&7))) || !af_read(w,(int64_t)block*512,h,sizeof(h)) || xx_data_get_u16(h, 2, 0, false)!=last)return false;
        seen[block>>3]|=(uint8_t)(1U<<(block&7));
        for(i=block==2U?1U:0U;i<13U;++i){const uint8_t *e=h+4+i*39U;
            if((e[0]>>4U)==4U){if(area || (e[0]&15U)!=11U || xx_rt_memcmp(e+1,"PASCAL.AREA",11) || e[16]!=0xefU)return false;area=xx_data_get_u16(e+17, 2, 0, false);used=xx_data_get_u16(e+19, 2, 0, false);}}
        last=block;block=xx_data_get_u16(h+2, 2, 0, false);}
    if(area<3U || used<3U || area>=total || used>total-area || !af_read(w,(int64_t)area*512,map,sizeof(map)) || xx_data_get_u16(map, 2, 0, false)!=used || !(count=xx_data_get_u16(map+2, 2, 0, false)) || count>31U || xx_rt_memcmp(map+4,"\003PPM",4))return false;
    for(i=0;i<count;++i){const uint8_t *e=map+16+i*8U;uint32_t start=xx_data_get_u16(e, 2, 0, false),size=xx_data_get_u16(e+2, 2, 0, false);
        if(start<area+2U || start>=area+used || !size || size>area+used-start)return false;
        p[i].at=(uint64_t)start*512U;p[i].size=(uint64_t)size*512U;p[i].kind=2;}
    return av_extent(w,0,(uint64_t)total*512U,"PRODOS",1) && al_parts(w,p,count,(uint64_t)(area+2U)*512U);
}
static bool al_hybrid(af_work *w) {
    uint8_t h[512];unsigned i;
    if(pm_available(w->f)!=143360 || !av_extent(w,0,143360,"DOS",4))return false;
    /* Block filesystem roots take precedence over signatureless CP/M. A
     * malformed identified root remains a failure rather than being replaced
     * by a different interpretation of its directory bytes. */
    for(i=0;i<2U;++i){if(!af_read(w,i?2816:1024,h,sizeof(h)))return false;
        if(((h[4]&0xf0U)==0xf0U && (h[4]&15U) && h[35]==39U && h[36]==13U) ||
           (xx_data_get_u16(h, 2, 0, false)==0U && xx_data_get_u16(h+2, 2, 0, false)==6U && xx_data_get_u16(h+4, 2, 0, false)==0U && h[6]>=1U && h[6]<=7U) ||
           (!i && h[0]=='B' && h[1]=='D'))return av_extent(w,0,143360,"BLOCK",0);}
    return av_extent(w,0,143360,"CPM",7);
}
/* Explicit DOS.MASTER profile is the embedded volume byte size. The profile
 * prevents arbitrary nested disk-image files from claiming this disk layout.
 * Scanned child volumes must fully validate and lie in allocated ProDOS blocks.
 * File ownership is not inferred from anonymous allocated sectors. */
static bool al_master(af_work *w,uint32_t profile) {
    uint8_t h[512],bitmap[8192];uint32_t total,bmap,blocks,start,i;unsigned found=0;char prefix[24];
    if(profile!=143360U && profile!=163840U && profile!=204800U && profile!=409600U)return false;
    if(!af_read(w,1024,h,sizeof(h)) || (h[4]&0xf0U)!=0xf0U || h[35]!=39U || h[36]!=13U || !(total=xx_data_get_u16(h+41, 2, 0, false)) || (uint64_t)total*512U>(uint64_t)pm_available(w->f))return false;
    bmap=xx_data_get_u16(h+39, 2, 0, false);blocks=profile/512U;
    if(!bmap || bmap>=total || (total+7U)/8U>(total-bmap)*512U || !af_read(w,(int64_t)bmap*512,bitmap,(total+7U)/8U) || !av_extent(w,0,(uint64_t)total*512U,"PRODOS",1))return false;
    for(start=0;start+blocks<=total;++start){uint64_t vtoc=(uint64_t)start*512U+17U*(profile==409600U?32U:16U)*256U;uint8_t v[256];bool allocated=true;
        if(!af_read(w,(int64_t)vtoc,v,sizeof(v)))return false;
        if(v[1]!=17U || !v[2] || v[2]>=(profile==409600U?32U:16U) || v[0x34]!=(profile==143360U?35U:profile==163840U?40U:50U) || v[0x35]!=(profile==409600U?32U:16U) || xx_data_get_u16(v+0x36, 2, 0, false)!=256U)continue;
        for(i=start;i<start+blocks;++i)if(bitmap[i>>3]&(0x80U>>(i&7))){allocated=false;break;}
        if(!allocated)continue;
        xx_rt_snprintf(prefix,sizeof(prefix),"DOS%02u",++found);
        if(!av_extent(w,(uint64_t)start*512U,profile,prefix,profile==409600U?5U:4U))return false;
        start+=blocks-1U;}
    return found!=0U;
}
static bool al_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd,unsigned kind) {
    af_work w;xx_apple_family_info *info=(xx_apple_family_info *)f;bool ok=false;
    if(!af_init(&w,f,s,pd)) {return false; } info->incomplete=false;
    switch(kind){
    case 1:case 2:case 3:ok=al_dos800(&w,kind);info->note=kind==2?"OzDOS split-sector volumes; native DOS file extents verified":"AmDOS/UniDOS contiguous DOS volumes; native DOS file extents verified";break;
    case 4:ok=al_cffa(&w,info->profile);info->note="CFFA profile validated with native ProDOS/HFS files; empty slots are checked zero";break;
    case 5:ok=al_hybrid(&w);info->note="Overlapping DOS and ProDOS/Pascal/HFS or Apple-DO CP/M views; files retain their filesystem prefix";break;
    case 6:ok=al_master(&w,info->profile);info->note="Explicit DOS.MASTER size profile; allocated embedded DOS and ProDOS files";break;
    case 7:ok=al_focus(&w);info->note="FocusDrive partition map; native filesystem file extraction";break;
    case 8:ok=al_micro(&w);info->note="MicroDrive partition map; native filesystem file extraction";break;
    case 9:ok=al_ts(&w);info->note="Macintosh TS partition map; native HFS forks";break;
    case 10:ok=al_ppm(&w);info->note="Pascal ProFile Manager absolute volume map; native Pascal files";break;
    }
    if(ok){s->size=pm_available(f);info->number_of_records=s->count;}return ok;
}
#endif
