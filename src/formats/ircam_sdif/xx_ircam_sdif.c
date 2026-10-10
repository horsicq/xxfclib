/* SPDX-License-Identifier: MIT
 * Independently implemented from https://sdif.sourceforge.net/standard/sdif-standard.html */
#include "xxfclib/formats/ircam_sdif/xx_ircam_sdif.h"
#include "../common/xx_memory_blob.h"

static bool signature(const uint8_t *p) {return !xx_rt_memcmp(p,"1FQ0",4) || !xx_rt_memcmp(p,"1TRC",4) || !xx_rt_memcmp(p,"1STF",4) || !xx_rt_memcmp(p,"1RES",4) || !xx_rt_memcmp(p,"1FRE",4) || !xx_rt_memcmp(p,"1ENV",4) || !xx_rt_memcmp(p,"1GAI",4) || !xx_rt_memcmp(p,"1SND",4);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[16];memory_blob b={0};bool ok=false;uint64_t at=16;uint32_t streams[128];uint64_t times[128];unsigned stream_count=0,frames=0;
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"SDIF",4) || xx_data_get_u32(h+4, 4, 0, true)!=8 || xx_data_get_u32(h+8, 4, 0, true)!=3 || xx_data_get_u32(h+12, 4, 0, true)!=1) return false;
    BLOB_NEED(blob_load(f,&b,pd) && blob_add(f,s,&b,"header",0,16));
    while(at<b.n) {uint64_t end,time;uint32_t stream,count;unsigned j,i;BLOB_NEED(++frames<=1024 && blob_span(&b,at,24) && signature(b.p+(size_t)at));end=at+8+xx_data_get_u32(b.p+(size_t)at+4, 4, 0, true);time=xx_data_get_u64(b.p+(size_t)at+8, 8, 0, true);stream=xx_data_get_u32(b.p+(size_t)at+16, 4, 0, true);count=xx_data_get_u32(b.p+(size_t)at+20, 4, 0, true);
        BLOB_NEED(end>=at+24 && end<=b.n && numeric_finite64(time) && !(time>>63) && count && count<=32);
        for(j=0;j<stream_count && streams[j]!=stream;++j) {}if(j==stream_count) {BLOB_NEED(stream_count<128);streams[j]=stream;times[j]=0;++stream_count;}BLOB_NEED(numeric_ordered64(time)>=numeric_ordered64(times[j]));times[j]=time;at+=24;
        for(i=0;i<count;++i) {uint32_t type,rows,cols,width;uint64_t bytes,pad;BLOB_NEED(record_span(at,16,end) && signature(b.p+(size_t)at));type=xx_data_get_u32(b.p+(size_t)at+4, 4, 0, true);rows=xx_data_get_u32(b.p+(size_t)at+8, 4, 0, true);cols=xx_data_get_u32(b.p+(size_t)at+12, 4, 0, true);width=type&255;
            BLOB_NEED(rows && cols && rows<=1000000 && cols<=1024 && (width==1 || width==2 || width==4 || width==8) && ((type>>8)==1 || (type>>8)==2 || (! (type>>8) && (width==4 || width==8))) && binary_mul((uint64_t)rows*cols,width,&bytes));at+=16;pad=(8-bytes%8)%8;BLOB_NEED(record_span(at,bytes+pad,end));if(!(type>>8)) BLOB_NEED(blob_floats(&b,at,bytes,width,true));BLOB_NEED(blob_zero(&b,at+bytes,pad) && blob_add(f,s,&b,"matrix",at,bytes));at+=bytes+pad;
        }BLOB_NEED(at==end);
    }BLOB_NEED(frames);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_ircam_sdif_init(xx_ircam_sdif *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_IRCAM_SDIF,"ircam_sdif"); } }
xx_ircam_sdif *xx_ircam_sdif_create(xx_io_device *d,int64_t b) { xx_ircam_sdif *r=(xx_ircam_sdif *)xx_mem_alloc(sizeof(*r)); if(r) xx_ircam_sdif_init(r,d,b); return r; }
void xx_ircam_sdif_destroy(xx_ircam_sdif *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_ircam_sdif_free(xx_ircam_sdif *r) { if(r) { xx_ircam_sdif_destroy(r); xx_mem_free(r); } }
bool xx_ircam_sdif_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_ircam_sdif_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
