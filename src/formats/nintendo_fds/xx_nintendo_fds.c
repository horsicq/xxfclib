/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented primary-format framing; no payload execution.
 */
#include "xxfclib/formats/nintendo_fds/xx_nintendo_fds.h"
#include "../snes_spc/xx_tenth_retro.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f,pm_stream *s,th_blob *b) {
 const uint8_t *p=b->p;unsigned side,sides;uint32_t total;
 if(b->n<16 || xx_rt_memcmp(p,"FDS\x1a",4) || !p[4] || p[4]>8 || !th_zero(p+5,11)) { return false; } sides=p[4];total=16+65500U*sides;if(!th_range(b,0,total) || !th_emit(f,s,b,"disk-descriptor.bin",0,16)) return false;
 for(side=0;side<sides;++side) {
  uint32_t a=16+65500U*side,end=a+65500;unsigned count,i;char name[64];if(!th_poll(b) || xx_rt_memcmp(p+a,"\1*NINTENDO-HVC*",15) || p[a+56]!=2) return false;count=p[a+57];if(!count || count>128) return false;
  xx_rt_snprintf(name,sizeof(name),"side-%u-descriptor.bin",side);if(!th_emit(f,s,b,name,a,58)) return false;a+=58;
  for(i=0;i<count;++i) {uint32_t z,load;if(!th_poll(b) || end-a<17 || p[a]!=3 || p[a+15]>2 || p[a+16]!=4) return false;z=xx_data_get_u16(p+a+13, 2, 0, false);load=xx_data_get_u16(p+a+11, 2, 0, false);if(!z || z>end-a-17 || z>65536-load) return false;xx_rt_snprintf(name,sizeof(name),"side-%u-file-%u.fds-record",side,i);if(!th_emit(f,s,b,name,a,17+z)) return false;a+=17+z;}
  if(!th_zero(p+a,end-a)) return false;
 }
 s->size=total;return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 th_blob b;bool ok;if(!th_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok;
}
void xx_nintendo_fds_init(xx_nintendo_fds *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_FDS,"nintendo_fds"); } }
xx_nintendo_fds *xx_nintendo_fds_create(xx_io_device *d,int64_t b) { xx_nintendo_fds *r=(xx_nintendo_fds *)xx_mem_alloc(sizeof(*r));if(r) xx_nintendo_fds_init(r,d,b);return r; }
void xx_nintendo_fds_destroy(xx_nintendo_fds *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_fds_free(xx_nintendo_fds *r) { if(r) {xx_nintendo_fds_destroy(r);xx_mem_free(r);} }
bool xx_nintendo_fds_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_fds_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
