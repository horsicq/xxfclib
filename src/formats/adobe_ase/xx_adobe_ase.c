/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/GNOME/gimp/master/app/core/gimppalette-load.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/adobe_ase/xx_adobe_ase.h"
#include "../xx_fifth_data.h"

static bool sm_utf16(Abstractformat *f,uint64_t at,uint32_t units,uint64_t end,bool nul,xx_pd_struct *pd) {
    uint8_t b[2048]; uint32_t i; bool high=false;
    if(!units || units>1024 || !fd_range(at,(uint64_t)units*2,end) || !pm_read(f,(int64_t)at,b,(size_t)units*2)) return false;
    for(i=0;i<units;++i) { uint16_t v=pm_be16(b+i*2); if(fd_stop(pd)) return false;
        if(nul && i==units-1) return !high && !v;
        if(!v) return false;
        if(high) { if(v<0xdc00 || v>0xdfff) return false; high=false; }
        else if(v>=0xd800 && v<=0xdbff) high=true;
        else if(v>=0xdc00 && v<=0xdfff) return false;
    } return !high;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[12],b[32]; uint64_t at=12,end=(uint64_t)pm_available(f); uint32_t count,i,depth=0,colors=0;
    if(!pm_read(f,0,h,12) || xx_rt_memcmp(h,"ASEF\0\1\0\0",8) || !(count=pm_be32(h+8)) || count>4096 || !pm_add(f,s,"ase-header.bin",0,12)) return false;
    for(i=0;i<count;++i) { uint16_t kind,units; uint32_t len; uint64_t payload,stop; char label[48];
        if(fd_stop(pd) || !fd_range(at,6,end) || !pm_read(f,(int64_t)at,b,6)) return false;
        kind=pm_be16(b); len=pm_be32(b+2); payload=at+6; if(!fd_range(payload,len,end)) return false; stop=payload+len;
        if(kind==0xc002) { if(len || !depth) return false; --depth; }
        else if(kind==1 || kind==0xc001) { uint64_t model;
            if(len<2 || !pm_read(f,(int64_t)payload,b,2) || !sm_utf16(f,payload+2,(units=pm_be16(b)),stop,true,pd)) return false;
            model=payload+2+(uint64_t)units*2;
            if(kind==0xc001) { if(model!=stop || ++depth>32) return false; }
            else { unsigned components,j; bool lab;
                if(!fd_range(model,4,stop) || !pm_read(f,(int64_t)model,b,4)) return false;
                lab=!xx_rt_memcmp(b,"LAB ",4); components=(!xx_rt_memcmp(b,"RGB ",4) || lab) ? 3:!xx_rt_memcmp(b,"CMYK",4) ? 4:(!xx_rt_memcmp(b,"Gray",4) || !xx_rt_memcmp(b,"GRAY",4)) ? 1:0;
                if(!components || model+4+components*4+2!=stop || !pm_read(f,(int64_t)model+4,b,components*4+2) || pm_be16(b+components*4)>2) return false;
                for(j=0;j<components;++j) { uint32_t v=pm_be32(b+j*4); if((v&0x7f800000)==0x7f800000 || (!lab && ((v&0x80000000) && (v&0x7fffffff))) || (!lab && (v&0x7fffffff)>0x3f800000)) return false; }
                ++colors;
            }
        } else return false;
        xx_rt_snprintf(label,sizeof(label),"ase-block-%u-%04x.bin",i,kind); if(!pm_add(f,s,label,(int64_t)at,(int64_t)(stop-at))) return false; at=stop;
    } s->size=(int64_t)at; return colors && !depth;
}

void xx_adobe_ase_init(xx_adobe_ase *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ADOBE_ASE,"ase"); } }
xx_adobe_ase *xx_adobe_ase_create(xx_io_device *d,int64_t b) { xx_adobe_ase *r=(xx_adobe_ase *)xx_mem_alloc(sizeof(*r)); if(r) xx_adobe_ase_init(r,d,b); return r; }
void xx_adobe_ase_destroy(xx_adobe_ase *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_adobe_ase_free(xx_adobe_ase *r) { if(r) { xx_adobe_ase_destroy(r); xx_mem_free(r); } }
bool xx_adobe_ase_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_adobe_ase_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
