/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://github.com/libsidplayfp/libsidplayfp/blob/master/src/sidtune/PSID.cpp
 * PSID v1/v2 and RSID v2, single SID, bounded declared data offset/load extent/song/flag fields. Exports original header/load prefix/program bytes without execution.
 */
#include "xxfclib/formats/psid_sid/xx_psid_sid.h"
#include "../common/xx_retro_disk_components.h"
#include "xxfclib/data/xx_data.h"

static bool parse_blob(Abstractformat *f,pm_stream *s,retro_disk_blob *b) {
 uint32_t version,header,data,load,size,songs,start,flags=0; bool rsid;
 if(!retro_disk_range(b,0,118)) { return false; } rsid=!xx_rt_memcmp(b->p,"RSID",4);
 if((!rsid && xx_rt_memcmp(b->p,"PSID",4)) || !(version=xx_data_get_u16(b->p+4, 2, 0, true)) || version>2 || (rsid && version!=2)) return false;
 header=version==1 ? 118U : 124U; if(!retro_disk_range(b,0,header) || (data=xx_data_get_u16(b->p+6, 2, 0, true))<header || data>=b->n || !(songs=xx_data_get_u16(b->p+14, 2, 0, true)) || songs>256 || !(start=xx_data_get_u16(b->p+16, 2, 0, true)) || start>songs) return false;
 if(version==2) { flags=xx_data_get_u16(b->p+118, 2, 0, true); if(flags&~63U || b->p[122] || b->p[123] || (!b->p[120] && b->p[121]) || (b->p[120]==255 && b->p[121]) || (b->p[120]>0 && b->p[120]<255 && ((uint32_t)b->p[120]+b->p[121]>256U))) return false; }
 if(rsid && (xx_data_get_u16(b->p+8, 2, 0, true) || xx_data_get_u16(b->p+12, 2, 0, true) || xx_data_get_u32(b->p+18, 4, 0, true) || (flags&1U))) return false;
 load=xx_data_get_u16(b->p+8, 2, 0, true); if(!retro_disk_emit(f,s,b,"music-descriptor.bin",0,data)) return false;
 if(!load) { if(!retro_disk_range(b,data,3)) return false; load=xx_data_get_u16(b->p+data, 2, 0, false); if(!retro_disk_emit(f,s,b,"load-address.bin",data,2)) return false; data+=2; }
 size=b->n-data; if(!size || size>65536U-load || (rsid && load<0x07e8U) || !retro_disk_emit(f,s,b,"program.bin",data,size)) return false;
 s->size=b->n; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { retro_disk_blob b; bool ok; if(!retro_disk_load(f,&b,pd)) return false; ok=parse_blob(f,s,&b); xx_mem_free(b.p); return ok; }

void xx_psid_sid_init(xx_psid_sid *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PSID_SID,"sid"); } }
xx_psid_sid *xx_psid_sid_create(xx_io_device *d,int64_t b) { xx_psid_sid *r=(xx_psid_sid *)xx_mem_alloc(sizeof(*r)); if(r) xx_psid_sid_init(r,d,b); return r; }
void xx_psid_sid_destroy(xx_psid_sid *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_psid_sid_free(xx_psid_sid *r) { if(r) { xx_psid_sid_destroy(r); xx_mem_free(r); } }
bool xx_psid_sid_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_psid_sid_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
