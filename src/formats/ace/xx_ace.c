/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ace/xx_ace.h"
#include "xx_ace_dec.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"

#define ACE_MAX_ENTRIES 100000U
#define ACE_MAX_NAME 4096U
#define ACE_ADDSIZE 0x0001U
#define ACE_64BIT 0x0004U
#define ACE_V20 0x0100U
#define ACE_SOLID 0x8000U
#define ACE_PASSWORD 0x4000U
#define ACE_SPLIT 0x3000U
#define ACE_DIRECTORY 0x0010U
#define ACE_FILE32 1U
#define ACE_FILE64 3U
typedef struct ace_entry_s { char *name; int64_t header_offset,data_offset; uint64_t packed_size,unpacked_size; uint32_t crc32,attributes; uint16_t flags,parameter; uint8_t method; } ace_entry;
typedef struct ace_parsed_s { ace_entry *entries; size_t count,capacity; int64_t end; uint8_t extract_version,create_version; bool v20,solid; } ace_parsed;
typedef struct ace_stream_s { ace_parsed parsed; size_t index,solid_next; xx_ace_history solid_history; } ace_stream;
static bool ace_read(xx_io_device *d,int64_t off,void *out,size_t n){size_t at=0;uint8_t *p=(uint8_t *)out;if(!d||(!out&&n)||off<0||off>LONG_MAX||xx_io_seek(d,(long)off,SEEK_SET)!=0)return false;while(at<n){ssize_t got=xx_io_read(d,p+at,n-at);if(got<=0||(size_t)got>n-at)return false;at+=(size_t)got;}return true;}
/* ACE stores the complement of the ordinary ISO-HDLC/PKZIP checksum. */
static bool ace_crc_matches(const void *bytes, size_t size, uint32_t expected) {
    return (xx_crc32_calc(0U, bytes, size) ^ UINT32_MAX) == expected;
}
static bool ace_stored_verify(Abstractformat *s, const ace_entry *e,
                               xx_pd_struct *pd) {
    uint8_t buffer[65536];
    uint64_t left = e->packed_size;
    int64_t cursor = e->data_offset;
    uint32_t crc = 0U;
    if (e->packed_size != e->unpacked_size || e->packed_size > INT64_MAX)
        return false;
    while (left) {
        size_t amount = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !ace_read(s->device, cursor, buffer, amount)) return false;
        crc = xx_crc32_calc(crc, buffer, amount);
        cursor += (int64_t)amount;
        left -= amount;
    }
    return !(pd && xx_pd_is_stopped(pd)) && (crc ^ UINT32_MAX) == e->crc32;
}
static bool ace_name_ok(const char *s){const char *start,*p;if(!s||!*s||*s=='/'||*s=='\\')return false;start=s;for(p=s;;++p){unsigned char c=(unsigned char)*p;size_t n;if(c==':'||c=='<'||c=='>'||c=='"'||c=='|'||c=='?'||c=='*'||(c&&c<32))return false;if(c!='/'&&c!='\\'&&c)continue;n=(size_t)(p-start);if(!n||(n==1&&start[0]=='.')||(n==2&&start[0]=='.'&&start[1]=='.'))return false;if(!c)return true;start=p+1;}}
static void ace_cleanup(ace_parsed *p){size_t i;if(!p)return;for(i=0;i<p->count;i++)xx_str_free(p->entries[i].name);xx_mem_free(p->entries);xx_mem_zero(p,sizeof(*p));p->end=-1;}
static bool ace_append(ace_parsed *p,ace_entry *e){ace_entry *grown;size_t cap;if(!p||!e||!e->name||p->count>=ACE_MAX_ENTRIES)return false;if(p->count==p->capacity){cap=p->capacity?p->capacity*2U:32U;if(cap<p->count||cap>SIZE_MAX/sizeof(*p->entries))return false;grown=(ace_entry *)xx_mem_realloc(p->entries,cap*sizeof(*p->entries));if(!grown)return false;p->entries=grown;p->capacity=cap;}p->entries[p->count++]=*e;xx_mem_zero(e,sizeof(*e));return true;}
static bool ace_file(Abstractformat *s,ace_parsed *p,int64_t total,int64_t off,uint16_t hsize,const uint8_t *generic,xx_pd_struct *pd,int64_t *next){uint8_t fixed[39];ace_entry e;uint16_t flags=xx_data_get_u16(generic+5, 2, 0, false),name_size;size_t body;bool wide=(flags&ACE_64BIT)!=0;uint64_t end;if(!(flags&ACE_ADDSIZE)||(generic[4]==ACE_FILE32&&wide)||(generic[4]==ACE_FILE64&&(!wide||!p->v20)))return false;body=wide?39U:31U;if(hsize<body||(uint64_t)(total-off)<4U+body||!ace_read(s->device,off+4,fixed,body))return false;xx_mem_zero(&e,sizeof(e));e.header_offset=off;e.flags=flags;e.packed_size=wide?xx_data_get_u64(fixed+3, 8, 0, false):xx_data_get_u32(fixed+3, 4, 0, false);e.unpacked_size=wide?xx_data_get_u64(fixed+11, 8, 0, false):xx_data_get_u32(fixed+7, 4, 0, false);e.attributes=xx_data_get_u32(fixed+(wide?23:15), 4, 0, false);e.crc32=xx_data_get_u32(fixed+(wide?27:19), 4, 0, false);e.method=fixed[wide?31:23];e.parameter=xx_data_get_u16(fixed+(wide?33:25), 2, 0, false);name_size=xx_data_get_u16(fixed+(wide?37:29), 2, 0, false);if(!name_size||name_size>ACE_MAX_NAME||name_size>(uint64_t)(hsize-body))return false;e.data_offset=off+4+(int64_t)hsize;end=(uint64_t)e.data_offset+e.packed_size;if(e.data_offset<0||end>(uint64_t)total||end>(uint64_t)INT64_MAX)return false;e.name=(char *)xx_mem_alloc((size_t)name_size+1U);if(!e.name||!ace_read(s->device,off+4+(int64_t)body,e.name,name_size)){xx_str_free(e.name);return false;}e.name[name_size]='\0';{size_t i;for(i=0;i<name_size;i++)if(e.name[i]=='\\')e.name[i]='/';}if((pd&&xx_pd_is_stopped(pd))||!ace_name_ok(e.name)||!ace_append(p,&e)){xx_str_free(e.name);return false;}*next=(int64_t)end;return true;}
/* p is zeroed before the guard clause: xx_ace_check_is_valid() runs
 * ace_cleanup() on its stack copy whatever this returns, and cleaning up an
 * uninitialised one would free indeterminate pointers. */
static bool ace_parse(Abstractformat *s,ace_parsed *p,xx_pd_struct *pd){uint8_t main[31],generic[15];int64_t total,off;uint16_t head,main_flags;if(p){xx_mem_zero(p,sizeof(*p));p->end=-1;}if(!s||!s->device||!p||s->base_address<0||(pd&&xx_pd_is_stopped(pd)))return false;total=xx_io_total_size(s->device);off=s->base_address;if(total<off||total-off<(int64_t)sizeof(main)||!ace_read(s->device,off,main,sizeof(main))||xx_data_get_u16(main+2, 2, 0, false)<27U||main[4]!=0U||xx_rt_memcmp(main+7,"**ACE**",7)!=0)goto fail;head=xx_data_get_u16(main+2, 2, 0, false);main_flags=xx_data_get_u16(main+5, 2, 0, false);if((uint64_t)head>(uint64_t)(total-off-4)||main[14]<10U||main[14]>20U)goto fail;p->extract_version=main[14];p->create_version=main[15];p->v20=(main_flags&ACE_V20)!=0;p->solid=(main_flags&ACE_SOLID)!=0;off+=4+(int64_t)head;while(off<total){uint16_t flags,hsize;int64_t next;if((pd&&xx_pd_is_stopped(pd))||total-off<7||!ace_read(s->device,off,generic,7))goto fail;hsize=xx_data_get_u16(generic+2, 2, 0, false);if(hsize<3U||(uint64_t)hsize>(uint64_t)(total-off-4))goto fail;flags=xx_data_get_u16(generic+5, 2, 0, false);if(generic[4]==ACE_FILE32||generic[4]==ACE_FILE64){if(!ace_file(s,p,total,off,hsize,generic,pd,&next))goto fail;off=next;continue;}next=off+4+(int64_t)hsize;if(flags&ACE_ADDSIZE){size_t n=(flags&ACE_64BIT)?8U:4U;uint64_t payload;if(hsize<3U+n||total-off<(int64_t)(7U+n)||!ace_read(s->device,off+7,generic+7,n))goto fail;payload=n==8U?xx_data_get_u64(generic+7, 8, 0, false):xx_data_get_u32(generic+7, 4, 0, false);if(payload>(uint64_t)(total-next)||payload>(uint64_t)INT64_MAX)goto fail;next+=(int64_t)payload;}if(next<=off||next>total)goto fail;off=next;}p->end=off;return p->count!=0U;fail:ace_cleanup(p);return false;}
static const xx_var *ace_option(const xx_list_s *o,uint32_t id){size_t i;for(i=0;o&&i<o->count;i++){const xx_meta *m=(const xx_meta *)xx_list_at((const xx_list_t *)o,i);if(m&&m->meta_id==id)return &m->var;}return NULL;}
static bool ace_options(xx_list_s *to,const xx_list_s *from){size_t i;if(!to)return false;for(i=0;from&&i<from->count;i++){const xx_meta *m=(const xx_meta *)xx_list_at((const xx_list_t *)from,i);xx_meta c;if(!m)continue;xx_meta_init(&c,m->meta_id);if(!xx_var_copy(&c.var,&m->var)||!xx_list_append(to,&c)){xx_meta_cleanup(&c);return false;}}return true;}
static bool ace_record(xx_archive_record *r,const ace_entry *e){bool folder;if(!r||!e)return false;folder=(e->attributes&ACE_DIRECTORY)!=0U;xx_archive_record_cleanup(r);xx_archive_record_init(r);r->header_offset=e->header_offset;r->header_size=e->data_offset-e->header_offset;r->data_offset=e->data_offset;r->compressed_size=e->packed_size<=INT64_MAX?(int64_t)e->packed_size:-1;return xx_archive_record_set_original_name(r,e->name)&&xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,e->unpacked_size)&&xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,e->packed_size)&&xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSION_METHOD,e->method)&&xx_archive_record_set_meta_u64(r,XX_META_ID_CRC32,e->crc32)&&xx_archive_record_set_meta_u64(r,XX_META_ID_ATTRIBUTES,e->attributes)&&xx_archive_record_set_meta_u64(r,XX_META_ID_FLAGS,e->flags)&&xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,folder)&&xx_archive_record_set_meta_bool(r,XX_META_ID_IS_ENCRYPTED,(e->flags&ACE_PASSWORD)!=0U);}
static void ace_stream_free(void *v){ace_stream *x=(ace_stream *)v;if(x){xx_ace_history_clear(&x->solid_history);ace_cleanup(&x->parsed);xx_mem_free(x);}}
static void ace_vdestroy(Abstractformat *s){xx_ace_destroy((xx_ace *)s);}
static void ace_clear(xx_ace *a){if(a&&a->internal){ace_cleanup((ace_parsed *)a->internal);xx_mem_free(a->internal);a->internal=NULL;}}
void xx_ace_init(xx_ace *a,xx_io_device *d,int64_t b){if(!a)return;xx_mem_zero(a,sizeof(*a));xx_format_init(&a->format,d,b);a->format.endian=XX_ENDIAN_LITTLE;a->format.file_type=XX_FILE_TYPE_ACE;a->format.format_type=XX_TYPE_ARCHIVE;a->format.is_archive=true;xx_format_set_mime_type(&a->format,"application/x-ace");xx_format_set_extension(&a->format,"ace");a->format.check_is_valid=xx_ace_check_is_valid;a->format.handle_base_info=xx_ace_handle_base_info;a->format.get_format_size=xx_ace_get_format_size;a->format.get_number_of_archive_records=xx_ace_get_number_of_archive_records;a->format.create_archive_records_reading=xx_ace_create_archive_records_reading;a->format.get_current_archive_record=xx_ace_get_current_archive_record;a->format.unpack_current_archive_record=xx_ace_unpack_current_archive_record;a->format.archive_record_move_to_next=xx_ace_archive_record_move_to_next;a->format.free_archive_records_reading=xx_ace_free_archive_records_reading;a->format.destroy=ace_vdestroy;a->archive_end=-1;}
xx_ace *xx_ace_create(xx_io_device *d,int64_t b){xx_ace *a=(xx_ace *)xx_mem_alloc(sizeof(*a));if(a)xx_ace_init(a,d,b);return a;}
void xx_ace_destroy(xx_ace *a){if(a){ace_clear(a);xx_format_cleanup_extra_parameters(&a->format);}}
void xx_ace_free(xx_ace *a){if(a){xx_ace_destroy(a);xx_mem_free(a);}}
bool xx_ace_check_is_valid(Abstractformat *s,xx_pd_struct *pd){ace_parsed p;bool ok=ace_parse(s,&p,pd);ace_cleanup(&p);return ok;}
bool xx_ace_handle_base_info(Abstractformat *s,xx_pd_struct *pd){xx_ace *a=(xx_ace *)s;ace_parsed *p;if(!a)return false;p=(ace_parsed *)xx_mem_alloc(sizeof(*p));if(!p||!ace_parse(s,p,pd)){xx_mem_free(p);s->is_valid=false;return false;}ace_clear(a);a->internal=p;a->number_of_records=p->count;a->archive_end=p->end;a->extract_version=p->extract_version;a->create_version=p->create_version;s->format_size=p->end-s->base_address;s->number_of_archive_records=p->count;s->is_valid=true;s->base_info_handled=true;return true;}
int64_t xx_ace_get_format_size(Abstractformat *s,xx_pd_struct *pd){return s&&(s->base_info_handled||xx_format_handle_base_info(s,pd))?s->format_size:-1;}
uint64_t xx_ace_get_number_of_archive_records(Abstractformat *s,xx_pd_struct *pd){return s&&(s->base_info_handled||xx_format_handle_base_info(s,pd))?((xx_ace *)s)->number_of_records:0U;}
xx_archive_record_state *xx_ace_create_archive_records_reading(Abstractformat *s,const xx_list_s *o,xx_pd_struct *pd){xx_archive_record_state *st;ace_stream *x;if(!s||!s->device||(!s->base_info_handled&&!xx_format_handle_base_info(s,pd)))return NULL;st=(xx_archive_record_state *)xx_mem_alloc(sizeof(*st));x=(ace_stream *)xx_mem_calloc(1,sizeof(*x));if(!st||!x){xx_mem_free(st);xx_mem_free(x);return NULL;}xx_archive_record_state_init(st,s);if(!ace_options(&st->options,o)||!ace_parse(s,&x->parsed,pd)){ace_stream_free(x);xx_archive_record_state_free(st);return NULL;}st->internal_state=x;st->free_internal=ace_stream_free;st->total_records=(int64_t)x->parsed.count;if(ace_record(&st->current_record,&x->parsed.entries[0])){st->has_record=true;st->current_index=0;}return st;}
const xx_archive_record *xx_ace_get_current_archive_record(Abstractformat *s,xx_archive_record_state *st){return s&&st&&st->format==s&&st->has_record?&st->current_record:NULL;}
bool xx_ace_archive_record_move_to_next(Abstractformat *s,xx_archive_record_state *st,xx_pd_struct *pd){ace_stream *x;if(!s||!st||st->format!=s||!st->has_record||(pd&&xx_pd_is_stopped(pd)))return false;x=(ace_stream *)st->internal_state;if(++x->index>=x->parsed.count){xx_archive_record_cleanup(&st->current_record);xx_archive_record_init(&st->current_record);st->has_record=false;return false;}if(!ace_record(&st->current_record,&x->parsed.entries[x->index])){st->has_record=false;return false;}++st->current_index;return true;}
/* A solid ACE file starts a fresh bitstream and Huffman model, but retains the
 * LZ dictionary from earlier compressed members.  Replay skipped members when
 * a caller extracts a later record directly from a fresh archive iterator. */
static bool ace_decode_solid_to(Abstractformat *s, ace_stream *x,
                                const uint8_t *target_packed, uint8_t *target_plain,
                                xx_pd_struct *pd) {
    size_t i, target;
    if (!s || !x || !target_packed || !target_plain) return false;
    target = x->index;
    if (target < x->solid_next) {
        xx_ace_history_clear(&x->solid_history);
        x->solid_next = 0;
    }
    for (i = x->solid_next; i <= target; ++i) {
        const ace_entry *e = &x->parsed.entries[i];
        uint8_t *packed = NULL, *plain = NULL;
        bool ok;
        if (pd && xx_pd_is_stopped(pd)) break;
        if ((e->attributes & ACE_DIRECTORY) != 0U ||
            (!e->packed_size && !e->unpacked_size)) {
            x->solid_next = i + 1U;
            continue;
        }
        if (e->method == 0U) {
            if (e->packed_size != e->unpacked_size ||
                e->packed_size > SIZE_MAX) break;
            packed = (uint8_t *)xx_mem_alloc((size_t)e->packed_size);
            if (!packed || !ace_read(s->device, e->data_offset, packed,
                                     (size_t)e->packed_size) ||
                !ace_crc_matches(packed, (size_t)e->packed_size, e->crc32)) {
                xx_mem_free(packed);
                break;
            }
            ok = xx_ace_history_append(&x->solid_history, packed,
                                        (size_t)e->packed_size,
                                        (unsigned)(e->parameter & 15U) + 10U);
            xx_mem_free(packed);
            if (!ok) break;
            x->solid_next = i + 1U;
            continue;
        }
        if (e->method > 2U || (e->flags & (ACE_PASSWORD | ACE_SPLIT)) != 0U ||
            e->packed_size > SIZE_MAX || e->unpacked_size > SIZE_MAX) break;
        if (i == target) {
            packed = (uint8_t *)target_packed;
            plain = target_plain;
        } else {
            packed = (uint8_t *)xx_mem_alloc((size_t)e->packed_size);
            plain = (uint8_t *)xx_mem_alloc((size_t)e->unpacked_size ?
                                            (size_t)e->unpacked_size : 1U);
            if (!packed || !plain ||
                !ace_read(s->device, e->data_offset, packed, (size_t)e->packed_size)) {
                xx_mem_free(packed);
                xx_mem_free(plain);
                break;
            }
        }
        ok = e->method == 1U ?
            xx_ace_decode_lzh_solid(packed, (size_t)e->packed_size, plain,
                                    (size_t)e->unpacked_size,
                                    (unsigned)(e->parameter & 15U) + 10U,
                                    &x->solid_history) :
            xx_ace_decode_blocked_solid(packed, (size_t)e->packed_size, plain,
                                        (size_t)e->unpacked_size,
                                        (unsigned)(e->parameter & 15U) + 10U,
                                        &x->solid_history);
        if (ok) ok = ace_crc_matches(plain, (size_t)e->unpacked_size, e->crc32);
        if (i != target) {
            xx_mem_free(packed);
            xx_mem_free(plain);
        }
        if (!ok) break;
        x->solid_next = i + 1U;
    }
    if (x->solid_next == target + 1U) return true;
    xx_ace_history_clear(&x->solid_history);
    x->solid_next = 0;
    return false;
}
static bool ace_decode_entry(Abstractformat *s, ace_stream *x,
                             const ace_entry *e, const uint8_t *packed,
                             uint8_t *plain, xx_pd_struct *pd) {
    unsigned bits = (unsigned)(e->parameter & 15U) + 10U;
    if (x->parsed.solid) return ace_decode_solid_to(s, x, packed, plain, pd);
    if (e->method == 1U)
        return xx_ace_decode_lzh(packed, (size_t)e->packed_size, plain,
                                 (size_t)e->unpacked_size, bits);
    if (e->method == 2U)
        return xx_ace_decode_blocked(packed, (size_t)e->packed_size, plain,
                                     (size_t)e->unpacked_size, bits);
    return false;
}
bool xx_ace_unpack_current_archive_record(Abstractformat *s,
                                          xx_archive_record_state *st,
                                          xx_pd_struct *pd) {
    ace_stream *x;
    const ace_entry *e;
    const xx_var *o, *limit;
    const char *base;
    char *dest = NULL;
    uint8_t *packed = NULL, *plain = NULL;
    bool ok = false, created = false;
    uint64_t memory_limit = UINT64_C(256) * 1024U * 1024U;
    if (!s || !st || st->format != s || !st->has_record ||
        !st->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    x = (ace_stream *)st->internal_state;
    if (x->index >= x->parsed.count) return false;
    e = &x->parsed.entries[x->index];
    if (e->method > 2U || (e->flags & (ACE_PASSWORD | ACE_SPLIT)) ||
        e->packed_size > SIZE_MAX || e->unpacked_size > SIZE_MAX) return false;
    limit = xx_format_resolve_extra_parameter(s, &st->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && e->unpacked_size > xx_var_get_u64(limit)) return false;
    o = ace_option(&st->options, XX_META_ID_OPT_UNPACK_PATH);
    /* Validation always reads/decodes and verifies content before any output
     * is opened. No destination means the verified bytes are discarded. */
    if ((e->attributes & ACE_DIRECTORY) != 0U) {
        if (e->packed_size || e->unpacked_size) return false;
    } else if (e->method == 0U) {
        if (!ace_stored_verify(s, e, pd)) return false;
    } else if (!e->packed_size && !e->unpacked_size) {
        if (!ace_crc_matches(NULL, 0U, e->crc32)) return false;
    } else {
        limit = xx_format_resolve_extra_parameter(s, &st->options,
                                                  XX_META_ID_OPT_MEMORY_LIMIT);
        if (limit) memory_limit = xx_var_get_u64(limit);
        if (e->packed_size > memory_limit ||
            e->unpacked_size > memory_limit - e->packed_size) return false;
        packed = (uint8_t *)xx_mem_alloc(e->packed_size ?
                                           (size_t)e->packed_size : 1U);
        plain = (uint8_t *)xx_mem_alloc(e->unpacked_size ?
                                          (size_t)e->unpacked_size : 1U);
        if (!packed || !plain ||
            !ace_read(s->device, e->data_offset, packed, (size_t)e->packed_size) ||
            !ace_decode_entry(s, x, e, packed, plain, pd) ||
            !ace_crc_matches(plain, (size_t)e->unpacked_size, e->crc32) ||
            (pd && xx_pd_is_stopped(pd))) goto done;
    }
    if (!o) { ok = true; goto done; }
    base = xx_var_get_str(o);
    if (!base || !ace_name_ok(e->name)) goto done;
    dest = xx_str_concat3(base, "/", e->name);
    if (!dest) goto done;
    if ((e->attributes & ACE_DIRECTORY) != 0U) {
        ok = xx_store_create_dirs_a(dest, true);
    } else if (xx_store_create_dirs_a(dest, false)) {
        if (e->method == 0U) {
            ok = xx_store_unpack_device_to_file(s->device, e->data_offset,
                                                (int64_t)e->packed_size, dest, pd);
        } else {
            xx_io_device *out = xx_io_file_open(dest, "wb");
            if (out) {
                size_t at = 0U;
                created = true;
                ok = true;
                while (at < (size_t)e->unpacked_size) {
                    ssize_t wrote;
                    if (pd && xx_pd_is_stopped(pd)) { ok = false; break; }
                    wrote = xx_io_write(out, plain + at,
                                        (size_t)e->unpacked_size - at);
                    if (wrote <= 0 || (size_t)wrote > (size_t)e->unpacked_size - at) {
                        ok = false; break;
                    }
                    at += (size_t)wrote;
                }
                if (xx_io_close(out) != 0) ok = false;
            }
        }
    }
done:
    if (!ok && created) xx_rt_remove(dest);
    xx_mem_free(packed);
    xx_mem_free(plain);
    xx_str_free(dest);
    return ok;
}
void xx_ace_free_archive_records_reading(Abstractformat *s,xx_archive_record_state *st){(void)s;xx_archive_record_state_free(st);}
