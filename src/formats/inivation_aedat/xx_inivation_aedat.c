/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.inivation.com/software/software-advanced-usage/file-formats/aedat-2.0.html */
#include "xxfclib/formats/inivation_aedat/xx_inivation_aedat.h"
#include "../common/xx_phylogenetic_text.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};bool ok=false;uint64_t at=0,start;uint32_t prev=0;unsigned count=0;BLOB_NEED(blob_load(f,&b,pd) && b.n>=21 && !xx_rt_memcmp(b.p,"#!AER-DAT2.0",12) && (b.p[12]=='\n' || (b.p[12]=='\r' && b.p[13]=='\n')));
    while(at<b.n && b.p[(size_t)at]=='#') {uint64_t line=at;while(at<b.n && b.p[(size_t)at]!='\n') {uint8_t c=b.p[(size_t)at++];BLOB_NEED((c>=32 && c<=126) || c=='\r' || c=='\t');}BLOB_NEED(at<b.n && at-line<=4096 && at<65536);++at;}
    start=at;BLOB_NEED(start>=13 && start<b.n && (b.n-start)%8==0 && (b.n-start)/8<=4090 && blob_add(f,s,&b,"header",0,start));
    while(at<b.n) {uint32_t time=xx_data_get_u32(b.p+(size_t)at+4, 4, 0, true);BLOB_NEED(time<0x80000000U && (!count || time>=prev) && blob_add(f,s,&b,"event",at,8));prev=time;++count;at+=8;}
    BLOB_NEED(count);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_inivation_aedat_init(xx_inivation_aedat *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_INIVATION_AEDAT,"inivation_aedat"); } }
xx_inivation_aedat *xx_inivation_aedat_create(xx_io_device *d,int64_t b) { xx_inivation_aedat *r=(xx_inivation_aedat *)xx_mem_alloc(sizeof(*r)); if(r) xx_inivation_aedat_init(r,d,b); return r; }
void xx_inivation_aedat_destroy(xx_inivation_aedat *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_inivation_aedat_free(xx_inivation_aedat *r) { if(r) { xx_inivation_aedat_destroy(r); xx_mem_free(r); } }
bool xx_inivation_aedat_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_inivation_aedat_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
