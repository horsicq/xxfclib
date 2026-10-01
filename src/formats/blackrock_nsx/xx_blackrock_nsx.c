/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/BlackrockNeurotech/NPMK/blob/master/NPMK/openNSx.m */
#include "xxfclib/formats/blackrock_nsx/xx_blackrock_nsx.h"
#include "../xx_ninth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[314];nh_blob b={0};bool ok=false;uint32_t header,channels;uint64_t at,last=0;unsigned i,records=0;uint16_t ids[1024];
    if(!pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h,"NEURALCD",8) || h[8]!=2 || (h[9]!=2 && h[9]!=3)) return false;
    header=pm_le32(h+10);channels=pm_le32(h+310);
    NH_NEED(channels && channels<=1024 && header==314+channels*66 && pm_le32(h+286) && pm_le32(h+290) && nh_date(h+294));
    NH_NEED(nh_load(f,&b,pd) && nh_span(&b,0,header));
    for(i=0;i<channels;++i) {const uint8_t *p=b.p+314+i*66;unsigned j;NH_NEED(p[0]=='C' && p[1]=='C' && pm_le16(p+2));ids[i]=pm_le16(p+2);for(j=0;j<i;++j) NH_NEED(ids[j]!=ids[i]);NH_NEED((int16_t)pm_le16(p+22)<(int16_t)pm_le16(p+24) && (int16_t)pm_le16(p+26)<(int16_t)pm_le16(p+28));}
    NH_NEED(nh_add(f,s,&b,"header",0,header));at=header;
    while(at<b.n) {uint32_t time,count;uint64_t bytes;NH_NEED(++records<=4095 && nh_span(&b,at,9) && b.p[(size_t)at]==1);time=pm_le32(b.p+(size_t)at+1);count=pm_le32(b.p+(size_t)at+5);bytes=(uint64_t)count*channels*2;
        NH_NEED(count && (!last || time>=last) && nh_span(&b,at+9,bytes) && nh_add(f,s,&b,"packet",at,9+bytes));last=(uint64_t)time+(uint64_t)count*pm_le32(h+286);at+=9+bytes;
    }NH_NEED(records);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_blackrock_nsx_init(xx_blackrock_nsx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_BLACKROCK_NSX,"blackrock_nsx"); } }
xx_blackrock_nsx *xx_blackrock_nsx_create(xx_io_device *d,int64_t b) { xx_blackrock_nsx *r=(xx_blackrock_nsx *)xx_mem_alloc(sizeof(*r)); if(r) xx_blackrock_nsx_init(r,d,b); return r; }
void xx_blackrock_nsx_destroy(xx_blackrock_nsx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_blackrock_nsx_free(xx_blackrock_nsx *r) { if(r) { xx_blackrock_nsx_destroy(r); xx_mem_free(r); } }
bool xx_blackrock_nsx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_blackrock_nsx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
