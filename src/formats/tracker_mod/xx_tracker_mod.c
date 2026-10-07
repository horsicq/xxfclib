/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/mod_load.c
 * 31-sample four-channel ProTracker-compatible MOD with M.K./M!K!/4CHN/FLT4 signature,1-128 orders/patterns and original signed8-bit samples. Checks order/sample/note references, loops and all physical extents; exports descriptor, each1024-byte pattern and sample.15-sample/signatureless modules, alternate channels and packed/external samples rejected; no playback.256MiB physical cap.
 */
#include "xxfclib/formats/tracker_mod/xx_tracker_mod.h"
#include "../tracker_mod/xx_eighth_media.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 uint8_t h[1084],p[1024]; uint32_t lengths[31],i,j,count,patterns=0; fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0}; char label[40];
 if(!fd_get(&c,h,sizeof(h)) || (xx_rt_memcmp(h+1080,"M.K.",4) && xx_rt_memcmp(h+1080,"M!K!",4) && xx_rt_memcmp(h+1080,"4CHN",4) && xx_rt_memcmp(h+1080,"FLT4",4)) || !(count=h[950]) || count>128 || (h[951]>127 && h[951]!=255)) return false;
 for(i=0;i<31;++i) { uint8_t *b=h+20+i*30; uint32_t n=(uint32_t)xx_data_get_u16(b+22, 2, 0, true)*2,a=(uint32_t)xx_data_get_u16(b+26, 2, 0, true)*2,z=(uint32_t)xx_data_get_u16(b+28, 2, 0, true)*2; if(b[24]>15 || b[25]>64 || (z>2 && !em_loop(a,z,n))) return false; lengths[i]=n; }
 for(i=0;i<128;++i) { if(h[952+i]>127) return false; if(i<count && patterns<=h[952+i]) patterns=h[952+i]+1U; }
 if(!em_emit(f,s,"descriptor.bin",0,1084,c.end)) return false;
 for(i=0;i<patterns;++i) { uint64_t at=c.at; if(!fd_get(&c,p,sizeof(p))) return false; for(j=0;j<256;++j) { uint8_t *b=p+j*4; uint32_t sample=(b[0]&0xf0U)|(b[2]>>4),period=((uint32_t)b[0]&15U)*256+b[1]; if(sample>31 || (period && (period<28 || period>3424))) return false; } xx_rt_snprintf(label,sizeof(label),"pattern-%u.bin",i); if(!em_emit(f,s,label,at,1024,c.end)) return false; }
 for(i=0;i<31;++i) if(lengths[i]) { xx_rt_snprintf(label,sizeof(label),"sample-%u.pcm",i+1); if(!em_take_emit(&c,s,label,lengths[i])) return false; }
 s->size=(int64_t)c.at; return true;
}

void xx_tracker_mod_init(xx_tracker_mod *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_MOD,"mod"); } }
xx_tracker_mod *xx_tracker_mod_create(xx_io_device *d,int64_t b) { xx_tracker_mod *r=(xx_tracker_mod *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_mod_init(r,d,b); return r; }
void xx_tracker_mod_destroy(xx_tracker_mod *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_mod_free(xx_tracker_mod *r) { if(r) { xx_tracker_mod_destroy(r); xx_mem_free(r); } }
bool xx_tracker_mod_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_mod_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
