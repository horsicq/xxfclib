/* SPDX-License-Identifier: MIT
 * Independently implemented from https://spider.wadsworth.org/spider_doc/spider/docs/image_doc.html */
#include "xxfclib/formats/microscopy_spider/xx_microscopy_spider.h"
#include "../xx_ninth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[108];nh_blob b={0};bool be,ok=false;uint64_t z,y,form,x,records,header,row,total;
    if(!pm_read(f,0,h,sizeof(h))) return false;
    if(pm_le32(h+16)==0x3f800000U || pm_le32(h+16)==0x40400000U) be=false;else if(pm_be32(h+16)==0x3f800000U || pm_be32(h+16)==0x40400000U) be=true;else return false;
    NH_NEED(sd_float32_uint(fd_u32(h,be),&z) && sd_float32_uint(fd_u32(h+4,be),&y) && sd_float32_uint(fd_u32(h+16,be),&form) && sd_float32_uint(fd_u32(h+44,be),&x));
    NH_NEED(sd_float32_uint(fd_u32(h+48,be),&records) && sd_float32_uint(fd_u32(h+84,be),&header) && sd_float32_uint(fd_u32(h+88,be),&row));
    NH_NEED(z && y && x && records && header>=108 && header<=1048576 && row==x*4 && records*row==header && (form==1 ? z==1:form==3) && fd_u32(h+92,be)==0 && fd_u32(h+104,be)==0);
    NH_NEED(nh_load(f,&b,pd) && fd_mul(x,y,&total) && fd_mul(total,z,&total) && fd_mul(total,4,&total) && b.n==header+total && nh_floats(&b,0,header,4,be) && nh_floats(&b,header,total,4,be));
    NH_NEED(nh_add(f,s,&b,"header",0,header) && nh_add(f,s,&b,"pixels",header,total));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_microscopy_spider_init(xx_microscopy_spider *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MICROSCOPY_SPIDER,"microscopy_spider"); } }
xx_microscopy_spider *xx_microscopy_spider_create(xx_io_device *d,int64_t b) { xx_microscopy_spider *r=(xx_microscopy_spider *)xx_mem_alloc(sizeof(*r)); if(r) xx_microscopy_spider_init(r,d,b); return r; }
void xx_microscopy_spider_destroy(xx_microscopy_spider *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_microscopy_spider_free(xx_microscopy_spider *r) { if(r) { xx_microscopy_spider_destroy(r); xx_mem_free(r); } }
bool xx_microscopy_spider_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_microscopy_spider_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
