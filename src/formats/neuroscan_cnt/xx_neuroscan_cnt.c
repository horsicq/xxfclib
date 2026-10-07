/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/mne-tools/mne-python/blob/main/mne/io/cnt/cnt.py */
#include "xxfclib/formats/neuroscan_cnt/xx_neuroscan_cnt.h"
#include "../xx_tenth_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};bool ok=false;uint32_t channels,count,event,offset;uint64_t start,bytes;unsigned width,i,event_width;uint32_t size;
    NH_NEED(nh_load(f,&b,pd) && nh_span(&b,0,900) && !xx_rt_memcmp(b.p,"Version 3.0",11));channels=xx_data_get_u16(b.p+370, 2, 0, false);count=xx_data_get_u32(b.p+864, 4, 0, false);event=xx_data_get_u32(b.p+886, 4, 0, false);offset=xx_data_get_u32(b.p+894, 4, 0, false);start=900+(uint64_t)channels*75;
    NH_NEED(channels && channels<=256 && count && count<=16000000 && xx_data_get_u16(b.p+376, 2, 0, false) && !xx_data_get_u32(b.p+12, 4, 0, false) && nh_span(&b,0,start) && event>start && event<=b.n && (event-start)%((uint64_t)channels*count)==0);
    bytes=event-start;width=(unsigned)(bytes/((uint64_t)channels*count));NH_NEED((width==2 || width==4) && (offset==0 || offset==1 || (offset%width==0 && offset/width<=count && count%(offset/width)==0)) && nh_add(f,s,&b,"setup",0,900));
    for(i=0;i<channels;++i) {uint64_t at=900+(uint64_t)i*75;const uint8_t *p=b.p+(size_t)at;unsigned j;bool named=false;for(j=0;j<10;++j) {if(!p[j]) break;if(p[j]<32 || p[j]>126) goto done;named=true;}NH_NEED(named && nh_floats(&b,at+59,4,4,false) && nh_floats(&b,at+71,4,4,false) && nh_add(f,s,&b,"channel",at,75));}
    NH_NEED(nh_add(f,s,&b,"signals",start,bytes) && nh_span(&b,event,9));event_width=b.p[event]==1 ? 8:(b.p[event]==2 || b.p[event]==3 ? 19:0);size=xx_data_get_u32(b.p+event+1, 4, 0, false);
    NH_NEED(event_width && !xx_data_get_u32(b.p+event+5, 4, 0, false) && size%event_width==0 && size/event_width<=4090 && nh_span(&b,event+9,size) && (uint64_t)event+9+size==b.n && nh_add(f,s,&b,"event-header",event,9));
    for(i=0;i<size/event_width;++i) {uint64_t at=event+9+(uint64_t)i*event_width;uint32_t pos=xx_data_get_u32(b.p+(size_t)at+4, 4, 0, false);NH_NEED(pos>=start && pos<event && (pos-start)%(channels*width)==0 && b.p[(size_t)at+2]<=11);if(event_width==19) NH_NEED(nh_floats(&b,at+12,4,4,false));NH_NEED(nh_add(f,s,&b,"event",at,event_width));}
    s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_neuroscan_cnt_init(xx_neuroscan_cnt *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NEUROSCAN_CNT,"neuroscan_cnt"); } }
xx_neuroscan_cnt *xx_neuroscan_cnt_create(xx_io_device *d,int64_t b) { xx_neuroscan_cnt *r=(xx_neuroscan_cnt *)xx_mem_alloc(sizeof(*r)); if(r) xx_neuroscan_cnt_init(r,d,b); return r; }
void xx_neuroscan_cnt_destroy(xx_neuroscan_cnt *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_neuroscan_cnt_free(xx_neuroscan_cnt *r) { if(r) { xx_neuroscan_cnt_destroy(r); xx_mem_free(r); } }
bool xx_neuroscan_cnt_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_neuroscan_cnt_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
