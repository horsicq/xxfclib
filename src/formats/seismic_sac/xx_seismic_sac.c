/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/obspy/obspy/tree/master/obspy/io/sac */
#include "xxfclib/formats/seismic_sac/xx_seismic_sac.h"
#include "../common/xx_memory_blob.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[632];memory_blob b={0};bool be,ok=false;uint32_t count,i;
    if(!pm_read(f,0,h,sizeof(h))) return false;
    if(xx_data_get_u32(h+304, 4, 0, false)==6) be=false;else if(xx_data_get_u32(h+304, 4, 0, true)==6) be=true;else return false;
    count=xx_data_get_u32(h+316, 4, 0, be);if(!count || count>16000000 || xx_data_get_u32(h+340, 4, 0, be)!=1 || xx_data_get_u32(h+420, 4, 0, be)!=1 || xx_data_get_u32(h+424, 4, 0, be)>1 || xx_data_get_u32(h+432, 4, 0, be)>1) return false;
    BLOB_NEED(blob_load(f,&b,pd) && b.n==632+(uint64_t)count*4 && blob_floats(&b,0,280,4,be) && blob_floats(&b,632,(uint64_t)count*4,4,be));
    BLOB_NEED(!(xx_data_get_u32(h, 4, 0, be)>>31) && (xx_data_get_u32(h, 4, 0, be)&0x7fffffffU) && blob_ascii(h+440,192,true));
    for(i=0;i<5;++i) {uint32_t v=xx_data_get_u32(h+280+i*4, 4, 0, be);BLOB_NEED(v==0xffffcfc7U || (i==0 ? v>=1900 && v<=3000:i==1 ? v>=1 && v<=366:i==2 ? v<24:i==3 ? v<60:v<61));}
    BLOB_NEED(blob_add(f,s,&b,"header",0,632) && blob_add(f,s,&b,"samples",632,(uint64_t)count*4));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_seismic_sac_init(xx_seismic_sac *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SEISMIC_SAC,"seismic_sac"); } }
xx_seismic_sac *xx_seismic_sac_create(xx_io_device *d,int64_t b) { xx_seismic_sac *r=(xx_seismic_sac *)xx_mem_alloc(sizeof(*r)); if(r) xx_seismic_sac_init(r,d,b); return r; }
void xx_seismic_sac_destroy(xx_seismic_sac *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_seismic_sac_free(xx_seismic_sac *r) { if(r) { xx_seismic_sac_destroy(r); xx_mem_free(r); } }
bool xx_seismic_sac_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_seismic_sac_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
