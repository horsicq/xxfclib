/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/imageio/imageio/blob/master/imageio/plugins/spe.py */
#include "xxfclib/formats/princeton_spe/xx_princeton_spe.h"
#include "../xx_ninth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[4100];nh_blob b={0};bool ok=false;uint32_t x,y,frames,type,width;uint64_t bytes,at;unsigned i;
    if(!pm_read(f,0,h,sizeof(h)) || pm_le16(h+4098)!=0x5555) return false;
    x=pm_le16(h+42);y=pm_le16(h+656);frames=pm_le32(h+1446);type=pm_le16(h+108);
    if(!x || !y || !frames || frames>4095 || type>3 || (pm_le32(h+1992)!=0x40000000U && pm_le32(h+1992)!=0x40200000U)) return false;
    width=type==0 || type==1 ? 4:2;NH_NEED(nh_load(f,&b,pd) && fd_mul((uint64_t)x*y,width,&bytes) && bytes*frames==b.n-4100);
    NH_NEED(nh_add(f,s,&b,"header",0,4100));at=4100;
    for(i=0;i<frames;++i) {if(type==0) NH_NEED(nh_floats(&b,at,bytes,4,false));NH_NEED(nh_add(f,s,&b,"pixels",at,bytes));at+=bytes;}
    s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_princeton_spe_init(xx_princeton_spe *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PRINCETON_SPE,"princeton_spe"); } }
xx_princeton_spe *xx_princeton_spe_create(xx_io_device *d,int64_t b) { xx_princeton_spe *r=(xx_princeton_spe *)xx_mem_alloc(sizeof(*r)); if(r) xx_princeton_spe_init(r,d,b); return r; }
void xx_princeton_spe_destroy(xx_princeton_spe *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_princeton_spe_free(xx_princeton_spe *r) { if(r) { xx_princeton_spe_destroy(r); xx_mem_free(r); } }
bool xx_princeton_spe_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_princeton_spe_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
