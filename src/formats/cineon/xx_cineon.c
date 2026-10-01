/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.kodak.com/content/products-brochures/Film/Cineon-File-Format-Description.pdf
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "xxfclib/formats/cineon/xx_cineon.h"
#include "../xx_payload_members.h"

static uint32_t cn_u32(const uint8_t *p,bool little) { return little ? pm_le32(p) : pm_be32(p); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[1024]; bool little; unsigned count,i,width=0,height=0,depth=0,cell,interleave,packing; uint32_t total,image,industry,user,eol,eoc; uint64_t header_end,row,plane,needed; char label[48];
    if(!pm_read(f,0,h,sizeof(h)) || (pm_be32(h)!=0x802A5FD7U && pm_le32(h)!=0x802A5FD7U)) return false;
    little=h[0]==0xD7; total=cn_u32(h+20,little); image=cn_u32(h+4,little); industry=cn_u32(h+12,little); user=cn_u32(h+16,little);
    count=h[193]; interleave=h[680]; packing=h[681]; eol=cn_u32(h+684,little); eoc=cn_u32(h+688,little); header_end=1024U+(uint64_t)industry+user;
    if(xx_rt_memcmp(h+24,"V4.5",4) || cn_u32(h+8,little)!=1024 || (industry!=0 && industry!=1024) || user>1048576 || header_end>image || total<image || total>(uint64_t)pm_available(f) || h[192]>7 || !count || count>8 || h[682] || h[683]>1 || (interleave!=0 && interleave!=2) || eol>1048576 || eoc>1048576) return false;
    for(i=0;i<count;++i) { const uint8_t *c=h+196+28*i; unsigned w=cn_u32(c+4,little),y=cn_u32(c+8,little);
        if((pd && xx_pd_is_stopped(pd)) || !w || !y || w>32768 || y>32768 || (uint64_t)w*y>67108864) return false;
        if(!i) { width=w; height=y; depth=c[2]; } else if(w!=width || y!=height || c[2]!=depth) return false;
    }
    if(depth==8 && (packing==1 || packing==2)) cell=1;
    else if(depth==16 && (packing==3 || packing==4)) cell=2;
    else if(depth==10 && count==3 && interleave==0 && (packing==5 || packing==6)) cell=4;
    else return false;
    if(interleave==0) { row=depth==10 ? (uint64_t)width*4U : (uint64_t)width*count*cell; plane=(row+eol)*height+eoc; needed=plane; }
    else { row=(uint64_t)width*cell; plane=(row+eol)*height+eoc; needed=plane*count; }
    if(needed>total-image || !pm_add(f,s,"generic-header.bin",0,1024) || (industry && !pm_add(f,s,"industry-header.bin",1024,industry)) || (user && !pm_add(f,s,"user-data.bin",1024+industry,user))) return false;
    if(!interleave) { if(!pm_add(f,s,"interleaved-raster.cineon",image,(int64_t)plane)) return false; }
    else for(i=0;i<count;++i) { if(pd && xx_pd_is_stopped(pd)) return false; xx_rt_snprintf(label,sizeof(label),"channel-%u.cineon",i); if(!pm_add(f,s,label,image+(int64_t)(plane*i),(int64_t)plane)) return false; }
    s->size=total; return true;
}

void xx_cineon_init(xx_cineon *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_CINEON,"cin"); } }
xx_cineon *xx_cineon_create(xx_io_device *d,int64_t b) { xx_cineon *r=(xx_cineon *)xx_mem_alloc(sizeof(*r)); if(r) xx_cineon_init(r,d,b); return r; }
void xx_cineon_destroy(xx_cineon *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_cineon_free(xx_cineon *r) { if(r) { xx_cineon_destroy(r); xx_mem_free(r); } }
bool xx_cineon_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_cineon_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
