/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/magland/mountainlab_pytools */
#include "xxfclib/formats/mountainsort_mda/xx_mountainsort_mda.h"
#include "../xx_ninth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[12];nh_blob b={0};bool ok=false,wide,ended=false;uint32_t type,width,nd;uint64_t at=12,total=1,i,n;
    if(!pm_read(f,0,h,12)) return false;type=0U-pm_le32(h);width=pm_le32(h+4);nd=pm_le32(h+8);wide=(nd>>31)!=0;if(wide) nd=0U-nd;
    if(type<2 || type>8 || (type==2 ? width!=1:type==3 || type==5 || type==8 ? width!=4:type==4 || type==6 ? width!=2:width!=8) || nd<2 || nd>6) return false;
    NH_NEED(nh_load(f,&b,pd));
    for(i=0;i<nd;++i) {unsigned k=wide ? 8:4;NH_NEED(nh_span(&b,at,k));n=wide ? fd_le64(b.p+(size_t)at):pm_le32(b.p+(size_t)at);at+=k;NH_NEED(n && n<=16000000 && !ended && fd_mul(total,n,&total));}
    NH_NEED(fd_mul(total,width,&n) && b.n==at+n);if(type==3 || type==7) NH_NEED(nh_floats(&b,at,n,width,false));
    NH_NEED(nh_add(f,s,&b,"header",0,at) && nh_add(f,s,&b,"array",at,n));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_mountainsort_mda_init(xx_mountainsort_mda *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MOUNTAINSORT_MDA,"mountainsort_mda"); } }
xx_mountainsort_mda *xx_mountainsort_mda_create(xx_io_device *d,int64_t b) { xx_mountainsort_mda *r=(xx_mountainsort_mda *)xx_mem_alloc(sizeof(*r)); if(r) xx_mountainsort_mda_init(r,d,b); return r; }
void xx_mountainsort_mda_destroy(xx_mountainsort_mda *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_mountainsort_mda_free(xx_mountainsort_mda *r) { if(r) { xx_mountainsort_mda_destroy(r); xx_mem_free(r); } }
bool xx_mountainsort_mda_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_mountainsort_mda_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
