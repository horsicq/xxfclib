/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented primary-format framing; no payload execution.
 */
#include "xxfclib/formats/acorn_uef/xx_acorn_uef.h"
#include "../common/xx_retro_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f,pm_stream *s,retro_music_blob *b) {
 const uint8_t *p=b->p;uint32_t a=12,count=0;bool data=false;
 if(b->n<19 || xx_rt_memcmp(p,"UEF File!\0",10) || xx_data_get_u16(p+10, 2, 0, false)!=10 || !retro_music_emit(f,s,b,"tape-descriptor.bin",0,12)) return false;
 while(a<b->n) {uint32_t z;unsigned type;char name[64];if(!retro_music_poll(b) || ++count>2048 || !retro_music_range(b,a,6)) return false;type=xx_data_get_u16(p+a, 2, 0, false);z=xx_data_get_u32(p+a+2, 4, 0, false);if(!retro_music_range(b,a+6,z)) return false;
  switch(type) {
   case 0:if(!z || p[a+6+z-1]) return false;break;
   case 0x100:if(!z) return false;data=true;break;
   case 0x110:case 0x112:case 0x115:if(z!=2) return false;break;
   case 0x111:if(z!=4) return false;break;
   default:return false;
  }
  xx_rt_snprintf(name,sizeof(name),"chunk-%u-%04x.uef",count-1,type);if(!retro_music_emit(f,s,b,name,a,6+z)) return false;a+=6+z;
 }
 if(!data) { return false; } s->size=b->n;return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 retro_music_blob b;bool ok;if(!retro_music_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok;
}
void xx_acorn_uef_init(xx_acorn_uef *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ACORN_UEF,"acorn_uef"); } }
xx_acorn_uef *xx_acorn_uef_create(xx_io_device *d,int64_t b) { xx_acorn_uef *r=(xx_acorn_uef *)xx_mem_alloc(sizeof(*r));if(r) xx_acorn_uef_init(r,d,b);return r; }
void xx_acorn_uef_destroy(xx_acorn_uef *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_acorn_uef_free(xx_acorn_uef *r) { if(r) {xx_acorn_uef_destroy(r);xx_mem_free(r);} }
bool xx_acorn_uef_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_acorn_uef_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
