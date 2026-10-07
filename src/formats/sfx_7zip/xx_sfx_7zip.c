/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_7zip/xx_sfx_7zip.h"
#include "../sfx_arcv2/xx_sixth_wrapper_table.h"

#include "xxfclib/formats/7zip/xx_7zip.h"
static bool w6_at_parse(Abstractformat *f,pm_stream *s,int64_t at,xx_pd_struct *pd) {
    xx_7zip *r; bool ok; int64_t size; uint8_t h[32],kind;uint64_t off,n;if(!pm_read(f,at,h,32) || (n=xx_data_get_u64(h+20, 8, 0, false))>4194304 || !(off=xx_data_get_u64(h+12, 8, 0, false)) || !wg_range(pm_available(f),at+32,off) || !wg_range(pm_available(f),at+32+(int64_t)off,n) || !pm_read(f,at+32+(int64_t)off,&kind,1) || (kind!=1 && kind!=23)) return false;
    if(wg_stop(pd)) { return false; } r=xx_7zip_create(f->device,f->base_address+at); if(!r) return false;
    ok=xx_format_handle_base_info(&r->format,pd);size=r->format.format_size;
    ok=ok && !wg_stop(pd) && r->format.number_of_archive_records>0 && r->format.number_of_archive_records<=4096 && wg_range(pm_available(f),at,(uint64_t)size);xx_7zip_free(r);
    return ok && w6_component(f,s,at,size,"payload.7z");
}
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={55,122,188,175,39,28};return w6_scan(f,s,sig,sizeof(sig),0,false,false,w6_at_parse,pd); }



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }

/* Keep the SFX carrier reader as the outer format, but delegate its archive
 * records to the authenticated 7z payload.  That avoids returning an opaque
 * payload.7z when the caller asked to list or extract files from the SFX. */
static xx_7zip *sfx7_nested(Abstractformat *f,xx_pd_struct *pd) {
    xx_sfx_7zip *outer=(xx_sfx_7zip *)f;
    if(!outer) return NULL;
    if(!outer->nested_7zip && !xx_sfx_7zip_handle_base_info(f,pd)) return NULL;
    return (xx_7zip *)outer->nested_7zip;
}
static int64_t sfx7_size(Abstractformat *f,xx_pd_struct *pd) {
    return sfx7_nested(f,pd) ? f->format_size : -1;
}
static uint64_t sfx7_count(Abstractformat *f,xx_pd_struct *pd) {
    return sfx7_nested(f,pd) ? f->number_of_archive_records : 0;
}
static xx_archive_record_state *sfx7_records(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd) {
    xx_7zip *r=sfx7_nested(f,pd);
    return r ? xx_7zip_create_archive_records_reading(&r->format,opts,pd) : NULL;
}
static const xx_archive_record *sfx7_current(Abstractformat *f,xx_archive_record_state *state) {
    xx_7zip *r=f ? (xx_7zip *)((xx_sfx_7zip *)f)->nested_7zip : NULL;
    return r ? xx_7zip_get_current_archive_record(&r->format,state) : NULL;
}
static bool sfx7_next(Abstractformat *f,xx_archive_record_state *state,xx_pd_struct *pd) {
    xx_7zip *r=f ? (xx_7zip *)((xx_sfx_7zip *)f)->nested_7zip : NULL;
    return r && xx_7zip_archive_record_move_to_next(&r->format,state,pd);
}
static bool sfx7_unpack(Abstractformat *f,xx_archive_record_state *state,xx_pd_struct *pd) {
    xx_7zip *r=f ? (xx_7zip *)((xx_sfx_7zip *)f)->nested_7zip : NULL;
    return r && xx_7zip_unpack_current_archive_record(&r->format,state,pd);
}
static void sfx7_free_records(Abstractformat *f,xx_archive_record_state *state) {
    xx_7zip *r=f ? (xx_7zip *)((xx_sfx_7zip *)f)->nested_7zip : NULL;
    if(r) xx_7zip_free_archive_records_reading(&r->format,state);
}
void xx_sfx_7zip_init(xx_sfx_7zip *r,xx_io_device *d,int64_t b) {
    if(!r) return;
    xx_mem_zero(r,sizeof(*r));
    pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_7ZIP,"exe");
    r->format.check_is_valid=xx_sfx_7zip_check_is_valid;
    r->format.handle_base_info=xx_sfx_7zip_handle_base_info;
    r->format.get_format_size=sfx7_size;
    r->format.get_number_of_archive_records=sfx7_count;
    r->format.create_archive_records_reading=sfx7_records;
    r->format.get_current_archive_record=sfx7_current;
    r->format.archive_record_move_to_next=sfx7_next;
    r->format.unpack_current_archive_record=sfx7_unpack;
    r->format.free_archive_records_reading=sfx7_free_records;
}
xx_sfx_7zip *xx_sfx_7zip_create(xx_io_device *d,int64_t b) { xx_sfx_7zip *r=(xx_sfx_7zip *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_7zip_init(r,d,b); return r; }
void xx_sfx_7zip_destroy(xx_sfx_7zip *r) {
    if(!r) return;
    if(r->nested_7zip) { xx_7zip_free((xx_7zip *)r->nested_7zip); r->nested_7zip=NULL; }
    xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_7zip_free(xx_sfx_7zip *r) { if(r) { xx_sfx_7zip_destroy(r); xx_mem_free(r); } }
bool xx_sfx_7zip_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_7zip_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {
    xx_sfx_7zip *outer=(xx_sfx_7zip *)f;
    pm_stream *s;
    xx_7zip *nested;
    int64_t end;
    if(!outer) return false;
    if(outer->nested_7zip) return true;
    s=pm_open(f,pd);
    if(!s) return false;
    if(s->count!=1U || s->items[0].offset<f->base_address) {
        pm_free_stream(s); return false;
    }
    nested=xx_7zip_create(f->device,s->items[0].offset);
    if(!nested || !xx_7zip_handle_base_info(&nested->format,pd) ||
       nested->format.format_size!=s->items[0].size) {
        if(nested) xx_7zip_free(nested);
        pm_free_stream(s); return false;
    }
    f->format_size=s->size;
    f->number_of_archive_records=nested->format.number_of_archive_records;
    end=f->base_address+s->size;
    f->overlay_size=xx_io_size(f->device)-end;
    f->overlay_offset=f->overlay_size>0 ? end:-1;
    f->is_valid=true;
    f->base_info_handled=true;
    outer->nested_7zip=nested;
    pm_free_stream(s);
    return true;
}
