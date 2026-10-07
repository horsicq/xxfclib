/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/AFM-analysis/igor2/blob/master/igor2/binarywave.py */
#include "xxfclib/formats/igor_ibw/xx_igor_ibw.h"
#include "../xx_ninth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[384];nh_blob b={0};bool be,ok=false,ended=false;uint32_t type,width,count;uint64_t points=1,bytes,at;unsigned i;uint16_t checksum=0;
    if(!pm_read(f,0,h,sizeof(h))) { return false; } if(xx_data_get_u16(h, 2, 0, false)==5) be=false;else if(xx_data_get_u16(h, 2, 0, true)==5) be=true;else return false;
    for(i=0;i<384;i+=2) { checksum=(uint16_t)(checksum+xx_data_get_u16(h+i, 2, 0, be)); } count=xx_data_get_u32(h+76, 4, 0, be);type=xx_data_get_u16(h+80, 2, 0, be);
    width=type==2 || type==0x20 || type==0x60 ? 4:type==4 ? 8:type==8 || type==0x48 ? 1:type==0x10 || type==0x50 ? 2:0;
    NH_NEED(!checksum && width && count && count<=16000000 && xx_data_get_u16(h+90, 2, 0, be)==1 && !xx_data_get_u32(h+52, 4, 0, be) && !xx_data_get_u32(h+56, 4, 0, be) && !xx_data_get_u32(h+60, 4, 0, be));
    for(i=0;i<4;++i) {uint32_t n=xx_data_get_u32(h+132+i*4, 4, 0, be);if(!n) ended=true;else {NH_NEED(!ended && n<=16000000 && fd_mul(points,n,&points));}}
    NH_NEED(points==count && fd_mul(points,width,&bytes) && xx_data_get_u32(h+4, 4, 0, be)==320+bytes && nh_ascii(h+92,32,true) && nh_load(f,&b,pd) && nh_span(&b,384,bytes) && nh_floats(&b,148,64,8,be));
    if(type==2 || type==4) {NH_NEED(nh_floats(&b,384,bytes,width,be));}NH_NEED(nh_add(f,s,&b,"header",0,384) && nh_add(f,s,&b,"wave",384,bytes));at=384+bytes;
    for(i=8;i<52;i+=4) {uint32_t n=xx_data_get_u32(h+i, 4, 0, be);if(n) {NH_NEED(n<=1048576 && nh_span(&b,at,n));if(i>=36) NH_NEED(n%32==0);NH_NEED(nh_add(f,s,&b,i==8 ? "formula":i==12 ? "note":i==16 ? "units":i<36 ? "dimension-units":"dimension-labels",at,n));at+=n;}}
    NH_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_igor_ibw_init(xx_igor_ibw *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_IGOR_IBW,"igor_ibw"); } }
xx_igor_ibw *xx_igor_ibw_create(xx_io_device *d,int64_t b) { xx_igor_ibw *r=(xx_igor_ibw *)xx_mem_alloc(sizeof(*r)); if(r) xx_igor_ibw_init(r,d,b); return r; }
void xx_igor_ibw_destroy(xx_igor_ibw *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_igor_ibw_free(xx_igor_ibw *r) { if(r) { xx_igor_ibw_destroy(r); xx_mem_free(r); } }
bool xx_igor_ibw_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_igor_ibw_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
