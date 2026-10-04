/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original read-only volume adapters. Filesystem identity, layout and member
 * decoding are separate capabilities; metadata readers never claim to test
 * the filesystem's data blocks. Wire-format facts are referenced in the
 * format coverage document. */
#include "xxfclib/formats/volume/xx_volume.h"
#include "../xx_mapped_members.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/formats/zlib/xx_zlib.h"
#include "xxfclib/formats/grub_backend/xx_grub_backend.h"
#include <stdlib.h>
static bool vr_poll(xx_pd_struct *pd) { return !pd || !xx_pd_is_stopped(pd); }
static bool vr_power(uint64_t n) { return n && !(n&(n-1)); }
static uint16_t vr_u16(const uint8_t *p,bool be) { return be?pm_be16(p):pm_le16(p); }
static uint32_t vr_u32(const uint8_t *p,bool be) { return be?pm_be32(p):pm_le32(p); }
static uint64_t vr_u64(const uint8_t *p,bool be) { return be?mm_be64(p):mm_le64(p); }
static bool vr_label(xx_volume *r,const uint8_t *p,size_t n) {
    size_t i; if(n>=sizeof(r->label)) n=sizeof(r->label)-1;
    for(i=0;i<n && p[i];++i) r->label[i]=(p[i]>=32 && p[i]<127)?(char)p[i]:'_';
    r->label[i]=0; return true;
}
static bool vr_equal(Abstractformat *f,int64_t at,const void *magic,size_t n) {
    uint8_t b[32]; return n<=sizeof(b) && pm_read(f,at,b,n) && !xx_rt_memcmp(b,magic,n);
}
static bool vr_info(Abstractformat *f,pm_stream *s,const char *extra) {
    xx_volume *r=(xx_volume *)f; char text[2048];
    xx_rt_snprintf(text,sizeof(text),"Format: %s\nCapability: %s\nVolume label: %s\nVersion: %u\nByte order: %s\nBlock size: %llu\nBlock count: %llu\nSource bytes: %llu\n%s",
        xx_format_file_type_to_string(f->file_type),r->capability?r->capability:"metadata",r->label,r->version,
        r->big_endian?"big":"little",(unsigned long long)r->block_size,(unsigned long long)r->blocks,
        (unsigned long long)pm_available(f),extra?extra:"");
    return mm_text(f,s,"volume-info.txt",text);
}
#include "xx_volume_gfs2.inc"
#include "xx_volume_grub.inc"
#include "xx_volume_classic_fs.inc"
#include "xx_volume_hpfs.inc"
#include "xx_volume_legacy_fs.inc"
#include "xx_volume_modern_fs.inc"
#include "xx_volume_filesystems.inc"
#include "xx_volume_partitions.inc"
#include "xx_volume_aff.inc"
#include "xx_volume_raid.inc"
#include "xx_volume_containers.inc"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    xx_volume *r=(xx_volume *)f; bool ok=false;
    if(!f || !f->device || !vr_poll(pd) || pm_available(f)<0) return false;
    r->block_size=0; r->blocks=0; r->version=0; r->big_endian=false; r->label[0]=0;
    if(f->file_type>=XX_FILE_TYPE_ATHEOS_FS && f->file_type<=XX_FILE_TYPE_GFS2) ok=vr_filesystem(f,s,pd);
    else if(f->file_type>=XX_FILE_TYPE_ACORN_PARTITIONS && f->file_type<=XX_FILE_TYPE_XENIX_PARTITIONS) ok=vr_partitions(f,s,pd);
    else ok=vr_container(f,s,pd);
    if(ok) s->size=pm_available(f);
    return ok && vr_poll(pd);
}
static xx_archive_record_state *vr_records(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    xx_volume *r=(xx_volume *)f; const xx_list_s *old=r->parse_options; xx_archive_record_state *state;
    r->parse_options=options; state=pm_create_records(f,options,pd); r->parse_options=old;
    if(state) f->number_of_archive_records=state->total_records;
    return state;
}
static bool vr_valid(Abstractformat *f,xx_pd_struct *pd) {
    xx_volume *r=(xx_volume *)f; bool old=r->identity_only,result; r->identity_only=true;
    result=pm_valid(f,pd); r->identity_only=old; return result;
}
static bool vr_unpack(Abstractformat *f,xx_archive_record_state *state,xx_pd_struct *pd) {
    xx_volume *r=(xx_volume *)f; const xx_list_s *old=r->parse_options; bool result;
    pm_stream *stream; pm_member *member;
    if(!state || state->format!=f || !state->has_record || !state->internal_state) return false;
    stream=(pm_stream *)state->internal_state;
    if(stream->index>=stream->count) return false;
    member=&stream->items[stream->index]; r->parse_options=&state->options;
    /* The payload adapter creates/accepts directories before read_all. Give
     * filesystem contexts a chance to enforce active limits before that path. */
    result=!member->directory || !member->read_all || member->read_all(f,member,NULL,pd);
    if(result) result=pm_unpack(f,state,pd);
    r->parse_options=old; return result;
}
static bool vr_handle(Abstractformat *f,xx_pd_struct *pd) {
    xx_volume *r=(xx_volume *)f; bool old=r->identity_only,result; r->identity_only=true;
    result=pm_handle(f,pd); r->identity_only=old; return result;
}
static const xx_archive_record *vr_current(Abstractformat *f,xx_archive_record_state *state) {
    const xx_archive_record *record=pm_current(f,state); xx_volume *r=(xx_volume *)f;
    if(record && r->capability && !xx_archive_record_set_meta_str(&state->current_record,XX_META_ID_COMMENT,r->capability)) return NULL;
    return record;
}
void xx_volume_init(xx_volume *r,xx_io_device *d,int64_t base,xx_file_type_t type,const char *ext) {
    if(!r) return; xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,base,type,ext);
    r->format.create_archive_records_reading=vr_records; r->format.get_current_archive_record=vr_current;
    r->format.check_is_valid=vr_valid; r->format.handle_base_info=vr_handle;
    r->format.unpack_current_archive_record=vr_unpack;
}
void xx_volume_destroy(xx_volume *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
bool xx_volume_set_geometry(xx_volume *r,uint32_t bytes,uint32_t heads,uint32_t sectors) {
    if(!r || bytes<128 || bytes>8192 || !vr_power(bytes) || !heads || heads>255 || !sectors || sectors>255) return false;
    r->logical_sector_size=bytes; r->heads=heads; r->sectors_per_track=sectors; return true;
}
