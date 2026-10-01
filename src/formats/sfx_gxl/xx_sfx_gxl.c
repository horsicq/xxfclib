/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_gxl/xx_sfx_gxl.h"
#include "../sfx_arc/xx_fifth_wrapper_table.h"

#include "xxfclib/formats/gxl/xx_gxl.h"
static Abstractformat *nested_open(xx_io_device *d,int64_t at) { xx_gxl *r=xx_gxl_create(d,at); return r ? &r->format : NULL; }
static void nested_close(Abstractformat *f) { xx_gxl_free((xx_gxl *)f); }
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={0x01,0xca,0x43,0x6f,0x70,0x79,0x72,0x69,0x67,0x68,0x74}; int64_t low;
    if(!w5_carrier(f,false,&low,pd)) return false; 
    return w5_embedded(f,s,low,sig,sizeof(sig),0,nested_open,nested_close,"payload.gxl",pd);
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }

static bool sfx_handle(Abstractformat *f,xx_pd_struct *pd) {
    xx_sfx_gxl *r=(xx_sfx_gxl *)f;
    pm_stream *s;
    xx_gxl *inner;
    int64_t end;
    if(!r || !f->device || (pd && xx_pd_is_stopped(pd))) return false;
    if(r->inner && f->base_info_handled) return true;
    s=pm_open(f,pd);
    if(!s) return false;
    if(s->count!=1 || s->items[0].size<=0) {pm_free_stream(s);return false;}
    inner=xx_gxl_create(f->device,s->items[0].offset);
    if(!inner) {pm_free_stream(s);return false;}
    if(!xx_format_handle_base_info(&inner->format,pd) ||
       inner->format.format_size!=s->items[0].size) {
        xx_gxl_free(inner);pm_free_stream(s);return false;
    }
    if(r->inner) xx_gxl_free(r->inner);
    r->inner=inner;
    f->format_size=s->size;
    f->number_of_archive_records=inner->format.number_of_archive_records;
    end=f->base_address+s->size;
    f->overlay_size=xx_io_size(f->device)-end;
    f->overlay_offset=f->overlay_size>0 ? end : -1;
    f->is_valid=true;
    f->base_info_handled=true;
    pm_free_stream(s);
    return true;
}
static int64_t sfx_size(Abstractformat *f,xx_pd_struct *pd) {
    return (f->base_info_handled || sfx_handle(f,pd)) ? f->format_size : -1;
}
static uint64_t sfx_count(Abstractformat *f,xx_pd_struct *pd) {
    return (f->base_info_handled || sfx_handle(f,pd)) ? f->number_of_archive_records : 0;
}
static xx_archive_record_state *sfx_records(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd) {
    xx_sfx_gxl *r=(xx_sfx_gxl *)f;
    if(!r->inner && !sfx_handle(f,pd)) return NULL;
    return xx_format_create_archive_records_reading(&r->inner->format,opts,pd);
}
static const xx_archive_record *sfx_current(Abstractformat *f,xx_archive_record_state *st) {
    xx_sfx_gxl *r=(xx_sfx_gxl *)f;
    return r->inner ? xx_format_get_current_archive_record(&r->inner->format,st) : NULL;
}
static bool sfx_next(Abstractformat *f,xx_archive_record_state *st,xx_pd_struct *pd) {
    xx_sfx_gxl *r=(xx_sfx_gxl *)f;
    return r->inner && xx_format_archive_record_move_to_next(&r->inner->format,st,pd);
}
static bool sfx_unpack(Abstractformat *f,xx_archive_record_state *st,xx_pd_struct *pd) {
    xx_sfx_gxl *r=(xx_sfx_gxl *)f;
    return r->inner && xx_format_unpack_current_archive_record(&r->inner->format,st,pd);
}
static void sfx_free_records(Abstractformat *f,xx_archive_record_state *st) {
    xx_sfx_gxl *r=(xx_sfx_gxl *)f;
    if(r->inner) xx_format_free_archive_records_reading(&r->inner->format,st);
}
void xx_sfx_gxl_init(xx_sfx_gxl *r,xx_io_device *d,int64_t b) {
    if(!r) return;
    xx_mem_zero(r,sizeof(*r));
    pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_GXL,"exe");
    r->format.handle_base_info=sfx_handle;
    r->format.get_format_size=sfx_size;
    r->format.get_number_of_archive_records=sfx_count;
    r->format.create_archive_records_reading=sfx_records;
    r->format.get_current_archive_record=sfx_current;
    r->format.archive_record_move_to_next=sfx_next;
    r->format.unpack_current_archive_record=sfx_unpack;
    r->format.free_archive_records_reading=sfx_free_records;
}
xx_sfx_gxl *xx_sfx_gxl_create(xx_io_device *d,int64_t b) { xx_sfx_gxl *r=(xx_sfx_gxl *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_gxl_init(r,d,b); return r; }
void xx_sfx_gxl_destroy(xx_sfx_gxl *r) { if(r) { if(r->inner) xx_gxl_free(r->inner); xx_format_cleanup_extra_parameters(&r->format); } }
void xx_sfx_gxl_free(xx_sfx_gxl *r) { if(r) { xx_sfx_gxl_destroy(r); xx_mem_free(r); } }
bool xx_sfx_gxl_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_gxl_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return sfx_handle(f,pd); }
