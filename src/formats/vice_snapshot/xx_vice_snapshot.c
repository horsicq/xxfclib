/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://github.com/VICE-Team/svn-mirror/blob/main/vice/src/snapshot.c
 * VICE snapshot v1.0/v1.1 module framing and optional VICE version header; exports original modules; module-specific emulator state is not decoded.
 */
#include "xxfclib/formats/vice_snapshot/xx_vice_snapshot.h"
#include "../vice_x64/xx_ninth_retro.h"
#include "xxfclib/data/xx_data.h"

static bool parse_blob(Abstractformat *f,pm_stream *s,nh_blob *b) {
 uint32_t at=37,n=0; char label[48];
 if(!nh_range(b,0,37) || xx_rt_memcmp(b->p,"VICE Snapshot File\x1a",19) || b->p[19]!=1 || b->p[20]>1 || !b->p[21] || !nh_ascii(b->p+21,16,true)) return false;
 if(nh_range(b,at,13) && !xx_rt_memcmp(b->p+at,"VICE Version\x1a",13)) { if(!nh_range(b,at,21)) return false; at+=21; }
 if(!nh_emit(f,s,b,"snapshot-descriptor.bin",0,at)) return false;
 while(at<b->n) {
  uint32_t z;
  if(!nh_range(b,at,22) || !b->p[at] || !nh_ascii(b->p+at,16,true) || (z=xx_data_get_u32(b->p+at+18, 4, 0, false))<=22 || !nh_range(b,at,z) || ++n>4095) return false;
  xx_rt_snprintf(label,sizeof(label),"module-%u.bin",n-1); if(!nh_emit(f,s,b,label,at,z)) return false; at+=z;
 }
 if(!n) { return false; } s->size=at; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { nh_blob b; bool ok; if(!nh_load(f,&b,pd)) return false; ok=parse_blob(f,s,&b); xx_mem_free(b.p); return ok; }

void xx_vice_snapshot_init(xx_vice_snapshot *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_VICE_SNAPSHOT,"vsf"); } }
xx_vice_snapshot *xx_vice_snapshot_create(xx_io_device *d,int64_t b) { xx_vice_snapshot *r=(xx_vice_snapshot *)xx_mem_alloc(sizeof(*r)); if(r) xx_vice_snapshot_init(r,d,b); return r; }
void xx_vice_snapshot_destroy(xx_vice_snapshot *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_vice_snapshot_free(xx_vice_snapshot *r) { if(r) { xx_vice_snapshot_destroy(r); xx_mem_free(r); } }
bool xx_vice_snapshot_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_vice_snapshot_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
