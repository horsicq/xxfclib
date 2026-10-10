/* SPDX-License-Identifier: MIT
 * Independently implemented from https://grischa-hahn.hier-im-netz.de/astro/ser/SER%20Doc%20V2.pdf */
#include "xxfclib/formats/astronomy_ser/xx_astronomy_ser.h"
#include "../common/xx_binary_records.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[178];uint32_t color,depth,frames,i;uint64_t pixels,bytes,end,total=(uint64_t)pm_available(f);char label[48];
    if(total>67108864 || !pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h,"LUCAM-RECORDER",14) || xx_data_get_u32(h+22, 4, 0, false)>1) return false;
    color=xx_data_get_u32(h+18, 4, 0, false);depth=xx_data_get_u32(h+34, 4, 0, false);frames=xx_data_get_u32(h+38, 4, 0, false);
    if(!(color==0 || (color>=8 && color<=11) || (color>=16 && color<=19) || color==100 || color==101) || !depth || depth>16 || !frames || frames>4094 || !xx_data_get_u32(h+26, 4, 0, false) || !xx_data_get_u32(h+30, 4, 0, false) || !binary_mul(xx_data_get_u32(h+26, 4, 0, false),xx_data_get_u32(h+30, 4, 0, false),&pixels) || pixels>1048576 || !binary_mul(pixels,depth>8 ? 2:1,&bytes) || !binary_mul(bytes,color>=100 ? 3:1,&bytes) || !binary_mul(bytes,frames,&end) || end>total-178) return false;
    end+=178;
    if(total!=end && total!=end+(uint64_t)frames*8) return false;
    if(!pm_add(f,s,"ser-header.bin",0,178)) return false;
    for(i=0;i<frames;++i) {if(binary_stop(pd)) return false;xx_rt_snprintf(label,sizeof(label),"frame-%u.bin",i);if(!pm_add(f,s,label,178+(int64_t)i*(int64_t)bytes,(int64_t)bytes)) return false;}
    if(total!=end && !pm_add(f,s,"timestamps.bin",(int64_t)end,(int64_t)frames*8)) return false;
    s->size=(int64_t)total;return true;
}

void xx_astronomy_ser_init(xx_astronomy_ser *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ASTRONOMY_SER,"astronomy_ser"); } }
xx_astronomy_ser *xx_astronomy_ser_create(xx_io_device *d,int64_t b) { xx_astronomy_ser *r=(xx_astronomy_ser *)xx_mem_alloc(sizeof(*r)); if(r) xx_astronomy_ser_init(r,d,b); return r; }
void xx_astronomy_ser_destroy(xx_astronomy_ser *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_astronomy_ser_free(xx_astronomy_ser *r) { if(r) { xx_astronomy_ser_destroy(r); xx_mem_free(r); } }
bool xx_astronomy_ser_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_astronomy_ser_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
