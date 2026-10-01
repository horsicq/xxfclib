/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://github.com/libsidplayfp/libsidplayfp/blob/master/src/sidtune/PSID.cpp
 * PSID v1/v2 and RSID v2, single SID, bounded declared data offset/load extent/song/flag fields. Exports original header/load prefix/program bytes without execution.
 */
#include "xxfclib/formats/psid_sid/xx_psid_sid.h"
#include "../vice_x64/xx_ninth_retro.h"

static bool parse_blob(Abstractformat *f,pm_stream *s,nh_blob *b) {
 uint32_t version,header,data,load,size,songs,start,flags=0; bool rsid;
 if(!nh_range(b,0,118)) return false; rsid=!xx_rt_memcmp(b->p,"RSID",4);
 if((!rsid && xx_rt_memcmp(b->p,"PSID",4)) || !(version=pm_be16(b->p+4)) || version>2 || (rsid && version!=2)) return false;
 header=version==1 ? 118U : 124U; if(!nh_range(b,0,header) || (data=pm_be16(b->p+6))<header || data>=b->n || !(songs=pm_be16(b->p+14)) || songs>256 || !(start=pm_be16(b->p+16)) || start>songs) return false;
 if(version==2) { flags=pm_be16(b->p+118); if(flags&~63U || b->p[122] || b->p[123] || (!b->p[120] && b->p[121]) || (b->p[120]==255 && b->p[121]) || (b->p[120]>0 && b->p[120]<255 && ((uint32_t)b->p[120]+b->p[121]>256U))) return false; }
 if(rsid && (pm_be16(b->p+8) || pm_be16(b->p+12) || pm_be32(b->p+18) || (flags&1U))) return false;
 load=pm_be16(b->p+8); if(!nh_emit(f,s,b,"music-descriptor.bin",0,data)) return false;
 if(!load) { if(!nh_range(b,data,3)) return false; load=pm_le16(b->p+data); if(!nh_emit(f,s,b,"load-address.bin",data,2)) return false; data+=2; }
 size=b->n-data; if(!size || size>65536U-load || (rsid && load<0x07e8U) || !nh_emit(f,s,b,"program.bin",data,size)) return false;
 s->size=b->n; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { nh_blob b; bool ok; if(!nh_load(f,&b,pd)) return false; ok=parse_blob(f,s,&b); xx_mem_free(b.p); return ok; }

void xx_psid_sid_init(xx_psid_sid *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PSID_SID,"sid"); } }
xx_psid_sid *xx_psid_sid_create(xx_io_device *d,int64_t b) { xx_psid_sid *r=(xx_psid_sid *)xx_mem_alloc(sizeof(*r)); if(r) xx_psid_sid_init(r,d,b); return r; }
void xx_psid_sid_destroy(xx_psid_sid *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_psid_sid_free(xx_psid_sid *r) { if(r) { xx_psid_sid_destroy(r); xx_mem_free(r); } }
bool xx_psid_sid_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_psid_sid_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
