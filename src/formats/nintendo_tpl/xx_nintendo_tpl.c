/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/devkitPro/libogc/master/libogc/tpl.c
 * TPL tiled texture and palette components, standard GX I/IA/RGB/RGBA/CI/CMPR formats, one mip level. Checks tile-rounded data lengths. No pixel decoding, extended TPL or mip chains.
 */
#include "xxfclib/formats/nintendo_tpl/xx_nintendo_tpl.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint64_t g64(const uint8_t *p,bool be) { return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true)<<32)|xx_data_get_u32(p+4, 4, 0, true) : ((uint64_t)xx_data_get_u32(p+4, 4, 0, false)<<32)|xx_data_get_u32(p, 4, 0, false); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i;
    if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) { uint64_t a=(uint64_t)(s->items[i].offset-f->base_address),b=(uint64_t)s->items[i].size;
        if(n && b && at<a+b && a<at+n) return false; }
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static XXFC_MAYBE_UNUSED bool zname(Abstractformat *f,uint64_t at,uint64_t end) {
    uint8_t c; uint64_t i; if(at>=end || end>(uint64_t)pm_available(f)) return false;
    for(i=0;i<4096 && at+i<end;++i) { if(!pm_read(f,(int64_t)(at+i),&c,1)) return false; if(!c) return i!=0; } return false;
}


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[12],e[8],im[36],pa[12],other[8]; uint32_t count,table,i; uint64_t total=(uint64_t)pm_available(f),end,table_end; char label[40];
    if(!pm_read(f,0,h,12) || xx_data_get_u32(h, 4, 0, true)!=0x20af30) return false;
    count=xx_data_get_u32(h+4, 4, 0, true); table=xx_data_get_u32(h+8, 4, 0, true); if(!count || count>1024 || table<12 || !span(table,(uint64_t)count*8,total)) return false; table_end=table+(uint64_t)count*8; end=table_end;
    for(i=0;i<count;++i) { uint64_t ia,pal,at,n; uint32_t w,he,fmt,bw=0,bh=0,bs=32;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,table+(int64_t)i*8,e,8)) return false;
        ia=xx_data_get_u32(e, 4, 0, true); pal=xx_data_get_u32(e+4, 4, 0, true); if(ia<table_end || !pm_read(f,(int64_t)ia,im,36)) return false;
        he=xx_data_get_u16(im, 2, 0, true); w=xx_data_get_u16(im+2, 2, 0, true); fmt=xx_data_get_u32(im+4, 4, 0, true); at=xx_data_get_u32(im+8, 4, 0, true);
        if(!w || !he || w>8192 || he>8192 || xx_data_get_u32(im+12, 4, 0, true)>2 || xx_data_get_u32(im+16, 4, 0, true)>2 || xx_data_get_u32(im+20, 4, 0, true)>5 || xx_data_get_u32(im+24, 4, 0, true)>1 || im[33] || im[34] || im[35]) return false;
        switch(fmt) { case 0: case 8: case 14: bw=bh=8; break; case 1: case 2: case 9: bw=8; bh=4; break; case 3: case 4: case 5: case 10: bw=bh=4; break; case 6: bw=bh=4; bs=64; break; default: return false; }
        n=(uint64_t)((w+bw-1)/bw)*((he+bh-1)/bh)*bs;
        if(at<ia+36 || at<table_end || !span(at,n,total)) return false;
        { uint32_t j; for(j=0;j<count;++j) { uint64_t a,p; if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,table+(int64_t)j*8,other,8)) return false; a=xx_data_get_u32(other, 4, 0, true); p=xx_data_get_u32(other+4, 4, 0, true);
            if(!span(a,36,total) || (at<a+36 && a<at+n) || (p && (!span(p,12,total) || (at<p+12 && p<at+n)))) return false; } }
        if((fmt==8 || fmt==9 || fmt==10)!=!!pal) return false;
        if(pal) { uint64_t p,ps; uint32_t entries,j; if(pal<table_end || !pm_read(f,(int64_t)pal,pa,12)) return false;
            entries=xx_data_get_u16(pa, 2, 0, true); p=xx_data_get_u32(pa+8, 4, 0, true); ps=(uint64_t)entries*2;
            if(!entries || entries>(fmt==8 ? 16U : fmt==9 ? 256U : 16384U) || pa[2] || pa[3] || xx_data_get_u32(pa+4, 4, 0, true)>2 || p<pal+12 || (p<ia+36 && ia<p+ps) || (at<pal+12 && pal<at+n)) return false;
            for(j=0;j<count;++j) { uint64_t a,q; if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,table+(int64_t)j*8,other,8)) return false; a=xx_data_get_u32(other, 4, 0, true); q=xx_data_get_u32(other+4, 4, 0, true);
                if((p<a+36 && a<p+ps) || (q && p<q+12 && q<p+ps)) return false; }
            xx_rt_snprintf(label,sizeof(label),"palette-%u.bin",i); if(!emit(f,s,label,p,ps,total)) return false; if(p+ps>end) end=p+ps; }
        xx_rt_snprintf(label,sizeof(label),"texture-%u.bin",i); if(!emit(f,s,label,at,n,total)) return false; if(at+n>end) end=at+n;
    }
    s->size=(int64_t)end; return true;

}

void xx_nintendo_tpl_init(xx_nintendo_tpl *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_TPL,"tpl"); } }
xx_nintendo_tpl *xx_nintendo_tpl_create(xx_io_device *d,int64_t b) { xx_nintendo_tpl *r=(xx_nintendo_tpl *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_tpl_init(r,d,b); return r; }
void xx_nintendo_tpl_destroy(xx_nintendo_tpl *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_tpl_free(xx_nintendo_tpl *r) { if(r) { xx_nintendo_tpl_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_tpl_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_tpl_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
