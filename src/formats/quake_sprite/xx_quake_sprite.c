/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/spritegn.h
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/quake_sprite/xx_quake_sprite.h"
#include "../bethesda_bsa/xx_game_table.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[36],r[16],b[4]; uint32_t count,i,j,frames,prev=0,interval; uint64_t at=36,n; int64_t total=pm_available(f);
    if(!gm_read(f,total,0,h,36) || xx_rt_memcmp(h,"IDSP",4) || pm_le32(h+4)!=1 || pm_le32(h+8)>4 || !pm_le32(h+16) || !pm_le32(h+20) || pm_le32(h+32)>1) return false;
    count=pm_le32(h+24); if(!count || count>1024) return false;
    for(i=0;i<count;++i) {
        uint32_t type; if(gm_stopped(pd) || !gm_read(f,total,at,b,4)) return false; type=pm_le32(b); at+=4; frames=1;
        if(type==1) {
            if(!gm_read(f,total,at,b,4) || !(frames=pm_le32(b)) || frames>1024) { return false; } at+=4; prev=0;
            for(j=0;j<frames;++j) { if(!gm_read(f,total,at+(uint64_t)j*4,b,4)) return false; interval=pm_le32(b); if(!interval || interval>=0x7f800000 || interval<=prev) return false; prev=interval; }
            if(!gm_add(f,s,"intervals.bin",at,(uint64_t)frames*4,36,total)) { return false; } at+=(uint64_t)frames*4;
        } else if(type!=0) return false;
        for(j=0;j<frames;++j) { uint32_t w,hh;
            if(gm_stopped(pd) || !gm_read(f,total,at,r,16)) { return false; } w=pm_le32(r+8); hh=pm_le32(r+12);
            if(!w || !hh || w>16384 || hh>16384) { return false; } n=(uint64_t)w*hh; at+=16;
            if(!gm_add(f,s,"indexed-pixels.bin",at,n,36,total)) { return false; } at+=n;
        }
    }
    s->size=(int64_t)at; return true;
}
void xx_quake_sprite_init(xx_quake_sprite *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_QUAKE_SPRITE,"bin"); } }
xx_quake_sprite *xx_quake_sprite_create(xx_io_device *d,int64_t b) { xx_quake_sprite *r=(xx_quake_sprite *)xx_mem_alloc(sizeof(*r)); if(r) xx_quake_sprite_init(r,d,b); return r; }
void xx_quake_sprite_destroy(xx_quake_sprite *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_quake_sprite_free(xx_quake_sprite *r) { if(r) { xx_quake_sprite_destroy(r); xx_mem_free(r); } }
bool xx_quake_sprite_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_quake_sprite_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
