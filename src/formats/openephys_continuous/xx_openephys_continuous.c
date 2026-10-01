/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/open-ephys/analysis-tools/blob/master/OpenEphys.py */
#include "xxfclib/formats/openephys_continuous/xx_openephys_continuous.h"
#include "../xx_ninth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[1024];char text[1025],v[256];nh_blob b={0};bool ok=false;uint64_t at,last=0,n;uint16_t rec=0;unsigned records=0,i;
    if(!pm_read(f,0,h,1024) || xx_rt_memcmp(h,"header.",7) || !nh_ascii(h,1024,true)) return false;
    xx_rt_memcpy(text,h,1024);text[1024]=0;for(i=0;i<1024;++i) if(!text[i]) text[i]=' ';
    NH_NEED(nh_field(text,"header.header_bytes",v,sizeof(v)) && sd_uint(v,&n) && n==1024 && nh_field(text,"header.blockLength",v,sizeof(v)) && sd_uint(v,&n) && n==1024);
    NH_NEED(nh_field(text,"header.version",v,sizeof(v)) && !xx_rt_strcmp(v,"0.4") && nh_field(text,"header.sampleRate",v,sizeof(v)) && sd_positive_float(v) && nh_field(text,"header.bitVolts",v,sizeof(v)) && sd_positive_float(v));
    NH_NEED(nh_load(f,&b,pd) && b.n>1024 && (b.n-1024)%2070==0 && nh_add(f,s,&b,"header",0,1024));at=1024;
    while(at<b.n) {const uint8_t *p=b.p+(size_t)at;uint64_t time=fd_le64(p);uint16_t recording=pm_be16(p+10);static const uint8_t marker[]={0,1,2,3,4,5,6,7,8,255};
        NH_NEED(++records<=4095 && !(time>>63) && pm_le16(p+8)==1024 && !xx_rt_memcmp(p+2060,marker,10));NH_NEED(records==1 || recording>rec || (recording==rec && time>=last));
        NH_NEED(nh_add(f,s,&b,"continuous-record",at,2070));last=time+1024;rec=recording;at+=2070;
    }s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_openephys_continuous_init(xx_openephys_continuous *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_OPENEPHYS_CONTINUOUS,"openephys_continuous"); } }
xx_openephys_continuous *xx_openephys_continuous_create(xx_io_device *d,int64_t b) { xx_openephys_continuous *r=(xx_openephys_continuous *)xx_mem_alloc(sizeof(*r)); if(r) xx_openephys_continuous_init(r,d,b); return r; }
void xx_openephys_continuous_destroy(xx_openephys_continuous *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_openephys_continuous_free(xx_openephys_continuous *r) { if(r) { xx_openephys_continuous_destroy(r); xx_mem_free(r); } }
bool xx_openephys_continuous_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_openephys_continuous_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
