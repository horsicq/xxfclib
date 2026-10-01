/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.teledynelecroy.com/support/knowledgebase.aspx?docid=556 */
#include "xxfclib/formats/lecroy_trc/xx_lecroy_trc.h"
#include "../xx_ninth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[346];nh_blob b={0};bool be,ok=false;uint64_t at=0;uint32_t sizes[8],count,width;unsigned i;
    if(!pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h,"WAVEDESC",8) || xx_rt_memcmp(h+16,"LECROY_2_3",10)) return false;
    if(h[34]==1 && h[35]==0) be=false;else if(!h[34] && !h[35]) be=true;else return false;
    NH_NEED(fd_u16(h+32,be)<=1 && !fd_u32(h+44,be) && !fd_u32(h+56,be) && !fd_u32(h+68,be) && !fd_u32(h+72,be));width=fd_u16(h+32,be)+1;count=fd_u32(h+116,be);
    sizes[0]=fd_u32(h+36,be);sizes[1]=fd_u32(h+40,be);sizes[2]=fd_u32(h+48,be);sizes[3]=fd_u32(h+52,be);sizes[4]=fd_u32(h+60,be);sizes[5]=fd_u32(h+64,be);
    NH_NEED(sizes[0]>=346 && sizes[0]<=65536 && count && sizes[4]==(uint64_t)count*width && (!sizes[5] || sizes[5]==sizes[4]) && sizes[2]%16==0 && sizes[3]%8==0 && fd_u32(h+144,be) && nh_load(f,&b,pd));
    NH_NEED(nh_floats(&b,156,8,4,be) && nh_floats(&b,176,4,4,be) && nh_floats(&b,180,16,8,be));
    for(i=0;i<6;++i) {static const char *labels[]={"descriptor","user-text","trigger-times","ris-times","wave1","wave2"};if(sizes[i]) {NH_NEED(nh_span(&b,at,sizes[i]));if(i==2 || i==3) NH_NEED(nh_floats(&b,at,sizes[i],8,be));NH_NEED(nh_add(f,s,&b,labels[i],at,sizes[i]));}at+=sizes[i];}
    NH_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_lecroy_trc_init(xx_lecroy_trc *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LECROY_TRC,"lecroy_trc"); } }
xx_lecroy_trc *xx_lecroy_trc_create(xx_io_device *d,int64_t b) { xx_lecroy_trc *r=(xx_lecroy_trc *)xx_mem_alloc(sizeof(*r)); if(r) xx_lecroy_trc_init(r,d,b); return r; }
void xx_lecroy_trc_destroy(xx_lecroy_trc *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_lecroy_trc_free(xx_lecroy_trc *r) { if(r) { xx_lecroy_trc_destroy(r); xx_mem_free(r); } }
bool xx_lecroy_trc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_lecroy_trc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
