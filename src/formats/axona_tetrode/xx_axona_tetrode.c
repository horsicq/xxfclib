/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/NeuralEnsemble/python-neo/blob/master/neo/rawio/axonarawio.py */
#include "xxfclib/formats/axona_tetrode/xx_axona_tetrode.h"
#include "../xx_tenth_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};bool ok=false;uint64_t at=0,head=0,count,channels,samples,tbytes,width,rate,bytes;uint32_t previous=0;unsigned i,j;
    NH_NEED(nh_load(f,&b,pd) && b.n>=100 && !xx_rt_memcmp(b.p,"trial_date ",11));
    while(at<b.n && at<65536) {if(nh_span(&b,at,10) && !xx_rt_memcmp(b.p+(size_t)at,"data_start",10)) {head=at+10;break;}while(at<b.n && b.p[(size_t)at]!='\n') {if(b.p[(size_t)at]<32 && b.p[(size_t)at]!='\r') goto done;++at;}NH_NEED(at<b.n);++at;
    }
    NH_NEED(head && tn_line_uint(&b,head-10,"num_spikes",&count) && tn_line_uint(&b,head-10,"num_chans",&channels) && tn_line_uint(&b,head-10,"samples_per_spike",&samples) && tn_line_uint(&b,head-10,"bytes_per_timestamp",&tbytes) && tn_line_uint(&b,head-10,"bytes_per_sample",&width) && tn_line_uint(&b,head-10,"timebase",&rate));
    NH_NEED(count && count<=261760 && channels==4 && samples && samples<=1024 && tbytes==4 && width==1 && rate && rate<=100000000 && fd_mul(count,4*(4+samples),&bytes) && nh_span(&b,head,bytes) && head+bytes+12==b.n && !xx_rt_memcmp(b.p+(size_t)(head+bytes),"\r\ndata_end\r\n",12) && nh_add(f,s,&b,"header",0,head));
    at=head;for(i=0;i<count;++i) {uint32_t time=xx_data_get_u32(b.p+(size_t)at, 4, 0, true);NH_NEED(!i || time>=previous);for(j=1;j<4;++j) NH_NEED(xx_data_get_u32(b.p+(size_t)(at+j*(4+samples)), 4, 0, true)==time);previous=time;at+=4*(4+samples);if(i%64==63 || i+1==count) {uint64_t block=i%64+1;NH_NEED(nh_add(f,s,&b,"spike-block",at-block*4*(4+samples),block*4*(4+samples)));}}
    NH_NEED(nh_add(f,s,&b,"end",at,12));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_axona_tetrode_init(xx_axona_tetrode *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_AXONA_TETRODE,"axona_tetrode"); } }
xx_axona_tetrode *xx_axona_tetrode_create(xx_io_device *d,int64_t b) { xx_axona_tetrode *r=(xx_axona_tetrode *)xx_mem_alloc(sizeof(*r)); if(r) xx_axona_tetrode_init(r,d,b); return r; }
void xx_axona_tetrode_destroy(xx_axona_tetrode *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_axona_tetrode_free(xx_axona_tetrode *r) { if(r) { xx_axona_tetrode_destroy(r); xx_mem_free(r); } }
bool xx_axona_tetrode_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_axona_tetrode_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
