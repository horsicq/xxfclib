/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_mpq/xx_sfx_mpq.h"
#include "../sfx_arcv2/xx_sixth_wrapper_table.h"

#include "xxfclib/formats/mpq/xx_mpq.h"
static bool w6_at_parse(Abstractformat *f,pm_stream *s,int64_t at,xx_pd_struct *pd) {
    xx_mpq *r; bool ok; int64_t size; uint8_t h[32];uint64_t hn,bn,hs,bs;int64_t limit=pm_available(f);if(!pm_read(f,at,h,32) || pm_le16(h+12)>1 || !(hn=pm_le32(h+24)) || hn>65536 || !(bn=pm_le32(h+28)) || bn>65536) return false;hs=pm_le32(h+16);bs=pm_le32(h+20);if(hs<pm_le32(h+4) || bs<pm_le32(h+4) || !wg_range(limit,at,pm_le32(h+8)) || !wg_range(pm_le32(h+8),hs,hn*16) || !wg_range(pm_le32(h+8),bs,bn*16) || (hs<bs+bn*16 && bs<hs+hn*16)) return false;
    if(wg_stop(pd)) return false; r=xx_mpq_create(f->device,f->base_address+at); if(!r) return false;
    ok=xx_format_handle_base_info(&r->format,pd);size=r->format.format_size;
    ok=ok && !wg_stop(pd) && r->format.number_of_archive_records>0 && r->format.number_of_archive_records<=4096 && wg_range(pm_available(f),at,(uint64_t)size);xx_mpq_free(r);
    return ok && w6_component(f,s,at,size,"payload.mpq");
}
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={77,80,81,26};return w6_scan(f,s,sig,sizeof(sig),0,false,false,w6_at_parse,pd); }



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_sfx_mpq_init(xx_sfx_mpq *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_MPQ,"exe"); } }
xx_sfx_mpq *xx_sfx_mpq_create(xx_io_device *d,int64_t b) { xx_sfx_mpq *r=(xx_sfx_mpq *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_mpq_init(r,d,b); return r; }
void xx_sfx_mpq_destroy(xx_sfx_mpq *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_mpq_free(xx_sfx_mpq *r) { if(r) { xx_sfx_mpq_destroy(r); xx_mem_free(r); } }
bool xx_sfx_mpq_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_mpq_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
