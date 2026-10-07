/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Trackstar framing: https://ciderpress2.com/formatdoc/Trackstar-notes.html
 * Native backward-byte payload decoding and Apple II GCR/file extraction.
 */
#include "xxfclib/formats/trackstar/xx_trackstar.h"
#include "../apple_family/xx_apple_gcr.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){
    af_work w;af_blob b;uint32_t records,i,j;uint8_t *image=NULL;unsigned sectors=0;bool full=true,ok=false,recognized=false;xx_trackstar *r=(xx_trackstar *)f;
    if(!af_init(&w,f,s,pd) || !af_load(&w,&b))return false;
    if((b.n!=40U*6656U && b.n!=80U*6656U) || !(image=af_alloc(&w,35U*4096U,false))) {goto done; } records=b.n/6656U;
    for(i=0;i<records;++i){const uint8_t *h=b.p+i*6656U;uint32_t n=xx_data_get_u16(h+6654, 2, 0, false);uint8_t *out;char name[48];
        if(!af_poll(&w) || !af_zero(h+46,82) || h[128]!=(records==80U?1U:0U) || n>6525U)goto done;
        for(j=0;j<46U;++j)if(h[j] && (h[j]<32U || h[j]>126U))goto done;
        if(!n){if(!af_zero(h+129,6525U))goto done;if(i<(records==80U?70U:35U) && (records==40U || !(i&1U)))full=false;continue;}
        out=af_alloc(&w,n,true);if(!out)goto done;for(j=0;j<n;++j)out[j]=h[128U+n-j];
        xx_rt_snprintf(name,sizeof(name),"track-%02u%s.nib",records==80U?i/2U:i,records==80U && (i&1U)?"-half":"");
        if(!af_add(&w,name,0,n,out)){af_release(&w,out,n);goto done;}
        if(i<(records==80U?70U:35U) && (records==40U || !(i&1U))){unsigned this_sectors=0;int found;
            /* The framing permits short captures. Preserve them exactly;
             * the optional sector decoder requires at least512bytes. */
            if(n<512U){full=false;continue;}
            found=ag_track(&w,out,n,records==80U?i/2U:i,image+(records==80U?i/2U:i)*4096U,&this_sectors);
            if(found<0 || (sectors && this_sectors && sectors!=this_sectors)) {goto done; } if(this_sectors)sectors=this_sectors;if(!this_sectors || found!=(int)this_sectors)full=false;}
    }
    if(full && sectors){if(sectors==13U)for(i=1;i<35U;++i)xx_rt_memmove(image+i*3328U,image+i*4096U,3328U);
        if(!ag_files(&w,image,35U,sectors,&recognized) || !af_copy(&w,"decoded-sectors.do",image,35U*sectors*256U))goto done;}
    r->cylinders=records==80U?40U:records;r->heads=1;r->sector_size=256;r->sectors_per_track=sectors;r->incomplete=!full;
    r->note=full?(recognized?"Trackstar reverse-byte tracks; sector checksums and native filesystem files verified":"Trackstar reverse-byte tracks; complete authenticated sectors, no supported filesystem root"):
        "Trackstar framing and original tracks; incomplete/nonstandard sector capture prevents filesystem reconstruction";
    r->number_of_records=s->count;s->size=b.n;ok=af_poll(&w);
done:af_release(&w,image,35U*4096U);af_release(&w,b.p,b.n);return ok;
}
AF_DEFINE_READER(trackstar,XX_FILE_TYPE_TRACKSTAR,"app")
