/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://github.com/libgme/game-music-emu/blob/master/gme/Hes_Emu.h
 * HES version 0 DATA chunks; bounded nonoverlapping 1MiB physical ROM ranges, mapped initializer must belong to a chunk. Exports original DATA/program components; no emulation.
 */
#include "xxfclib/formats/hes_sound/xx_hes_sound.h"
#include "../common/xx_retro_disk_components.h"
#include "xxfclib/data/xx_data.h"

static bool parse_blob(Abstractformat *f,pm_stream *s,retro_disk_blob *b) {
 uint32_t at=16,count=0,n=0,initial,bank; bool mapped=false; retro_disk_span spans[64]; char name[48];
 if(!retro_disk_range(b,0,32) || xx_rt_memcmp(b->p,"HESM",4) || b->p[4] || !retro_disk_emit(f,s,b,"music-descriptor.bin",0,16)) return false;
 bank=b->p[8+(xx_data_get_u16(b->p+6, 2, 0, false)>>13)]; if(bank>127) return false; initial=bank*8192U+(xx_data_get_u16(b->p+6, 2, 0, false)&8191U);
 while(at<b->n) {
  uint32_t z,load;
  if(!retro_disk_range(b,at,16) || xx_rt_memcmp(b->p+at,"DATA",4) || !(z=xx_data_get_u32(b->p+at+4, 4, 0, false)) || (load=xx_data_get_u32(b->p+at+8, 4, 0, false))>=1048576U || z>1048576U-load || xx_data_get_u32(b->p+at+12, 4, 0, false) || !retro_disk_range(b,at+16,z) || !retro_disk_disjoint(spans,&count,64,load,z)) return false;
  if(initial>=load && initial-load<z) mapped=true;
  xx_rt_snprintf(name,sizeof(name),"data-%u.bin",n++); if(!retro_disk_emit(f,s,b,name,at,z+16)) return false; at+=16+z;
 }
 if(!mapped || !n) { return false; } s->size=at; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { retro_disk_blob b; bool ok; if(!retro_disk_load(f,&b,pd)) return false; ok=parse_blob(f,s,&b); xx_mem_free(b.p); return ok; }

void xx_hes_sound_init(xx_hes_sound *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_HES_SOUND,"hes"); } }
xx_hes_sound *xx_hes_sound_create(xx_io_device *d,int64_t b) { xx_hes_sound *r=(xx_hes_sound *)xx_mem_alloc(sizeof(*r)); if(r) xx_hes_sound_init(r,d,b); return r; }
void xx_hes_sound_destroy(xx_hes_sound *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_hes_sound_free(xx_hes_sound *r) { if(r) { xx_hes_sound_destroy(r); xx_mem_free(r); } }
bool xx_hes_sound_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_hes_sound_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
