/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented primary-format framing; no payload execution.
 */
#include "xxfclib/formats/sc68_music/xx_sc68_music.h"
#include "../common/xx_retro_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f,pm_stream *s,retro_music_blob *b) {
 static const char signature[]="SC68 Music-file / (c) (BeN)jamin Gerard / SasHipA-Dev  ";
 const uint8_t *p=b->p;uint32_t a=64,tracks=0,count=0,def=0;bool data=false,end=false;
 if(b->n<=72 || xx_rt_memcmp(p,signature,sizeof(signature)) || xx_rt_memcmp(p+56,"SC68",4) || xx_data_get_u32(p+60, 4, 0, false)!=b->n-56 || !retro_music_emit(f,s,b,"music-descriptor.bin",0,64)) return false;
 while(a<b->n) {
  uint32_t z;char name[64];const uint8_t *id;bool integer,string;
  if(!retro_music_poll(b) || !retro_music_range(b,a,8) || ++count>2048 || p[a]!='S' || p[a+1]!='C') { return false; } id=p+a;z=xx_data_get_u32(p+a+4, 4, 0, false);if(!retro_music_range(b,a+8,z)) return false;
  if(!xx_rt_memcmp(id,"SCMU",4)) {if(z || ++tracks>99 || (tracks>1 && !data)) return false;}
  if(!xx_rt_memcmp(id,"SCDA",4)) {if(!tracks || !z) return false;data=true;}
  if(!xx_rt_memcmp(id,"SCEF",4)) {if(z || a+8!=b->n) return false;end=true;}
  if(!xx_rt_memcmp(id,"SC68",4)) return false;
  integer=!xx_rt_memcmp(id,"SCDF",4) || !xx_rt_memcmp(id,"SCD0",4) || !xx_rt_memcmp(id,"SCAT",4) || !xx_rt_memcmp(id,"SCTI",4) || !xx_rt_memcmp(id,"SCFR",4) || !xx_rt_memcmp(id,"SCFQ",4) || !xx_rt_memcmp(id,"SCLP",4) || !xx_rt_memcmp(id,"SCTY",4);
  string=!xx_rt_memcmp(id,"SCFN",4) || !xx_rt_memcmp(id,"SCMN",4) || !xx_rt_memcmp(id,"SCAN",4) || !xx_rt_memcmp(id,"SCCN",4) || !xx_rt_memcmp(id,"SCRE",4);
  if((integer && z!=4) || (string && (!z || (p[a+8+z-1] && (z<2 || p[a+8+z-2]))))) return false;
  if(!xx_rt_memcmp(id,"SCDF",4)) def=xx_data_get_u32(p+a+8, 4, 0, false);
  xx_rt_snprintf(name,sizeof(name),"chunk-%u-%c%c%c%c.sc68",count-1,id[0],id[1],id[2],id[3]);if(!retro_music_emit(f,s,b,name,a,8+z)) return false;a+=8+z;
 }
 if(!tracks || !data || !end || def>=tracks) { return false; } s->size=b->n;return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 retro_music_blob b;bool ok;if(!retro_music_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok;
}
void xx_sc68_music_init(xx_sc68_music *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_SC68_MUSIC,"sc68_music"); } }
xx_sc68_music *xx_sc68_music_create(xx_io_device *d,int64_t b) { xx_sc68_music *r=(xx_sc68_music *)xx_mem_alloc(sizeof(*r));if(r) xx_sc68_music_init(r,d,b);return r; }
void xx_sc68_music_destroy(xx_sc68_music *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sc68_music_free(xx_sc68_music *r) { if(r) {xx_sc68_music_destroy(r);xx_mem_free(r);} }
bool xx_sc68_music_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sc68_music_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
