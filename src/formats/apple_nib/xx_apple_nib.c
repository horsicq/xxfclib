/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native NIB/NB2 carrier with authenticated 5&3/6&2 sector reconstruction
 * and real DOS/ProDOS/Pascal file extraction. Original track paths retained.
 */
#include "xxfclib/formats/apple_nib/xx_apple_nib.h"
#include "../apple_family/xx_apple_gcr.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){
    af_work w;af_blob b;uint32_t stride=6656,tracks,i,j;uint8_t *image=NULL;unsigned sectors=0;bool full=true,ok=false,recognized=false;xx_apple_nib *r=(xx_apple_nib *)f;
    if(!af_init(&w,f,s,pd) || !af_load(&w,&b))return false;
    if(b.n%stride || b.n/stride<35U || b.n/stride>50U){stride=6384;if(b.n%stride || b.n/stride<35U || b.n/stride>40U)goto done;}
    tracks=b.n/stride;image=af_alloc(&w,tracks*4096U,false);if(!image)goto done;
    for(i=0;i<tracks;++i){char name[48];unsigned found_sectors=0;int found;const uint8_t *track=b.p+i*stride;
        for(j=0;j<stride;++j)if(!(track[j]&0x80U))goto done;
        found=ag_track(&w,track,stride,i,image+i*4096U,&found_sectors);if(found<=0 || (sectors && found_sectors!=sectors))goto done;
        sectors=found_sectors;if((unsigned)found!=sectors)full=false;
        xx_rt_snprintf(name,sizeof(name),"%04u-track-%03u.nib",i,i);if(!af_add(&w,name,(int64_t)i*stride,stride,NULL))goto done;}
    if(full){uint32_t bytes=tracks*sectors*256U;
        if(sectors==13U)for(i=1;i<tracks;++i)xx_rt_memmove(image+i*3328U,image+i*4096U,3328U);
        if(!ag_files(&w,image,tracks,sectors,&recognized) || !af_copy(&w,"decoded-sectors.do",image,bytes))goto done;}
    /* A framed NIB track remains completely preserved even if its capture
     * lacks enough authenticated sectors for a normalized logical image. */
    r->cylinders=tracks;r->heads=1;r->sector_size=256;r->sectors_per_track=sectors;r->incomplete=false;
    r->note=full?(recognized?"NIB/NB2 sectors checksum verified; native filesystem files extracted":"NIB/NB2 sectors checksum verified; no supported filesystem root"):
        "Valid original nibble components; missing sectors prevent a complete disk or filesystem";
    r->number_of_records=s->count;s->size=b.n;ok=af_poll(&w);
done:af_release(&w,image,(b.n/stride)*4096U);af_release(&w,b.p,b.n);return ok;
}
AF_DEFINE_READER(apple_nib,XX_FILE_TYPE_APPLE_NIB,"nib")
