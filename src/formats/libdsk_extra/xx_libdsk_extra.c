/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native implementation of MYZ80, NanoWasp and Gotek layouts.
 * LibDsk is an independent test producer only; no LibDsk code is imported.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/libdsk_extra/xx_libdsk_extra.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#include <limits.h>

#define LDSK_MYZ80 1U
#define LDSK_NANOWASP 2U
#define LDSK_GOTEK_720 3U
#define LDSK_GOTEK_1440 4U
#define LDSK_MYZ80_BYTES (UINT64_C(8) * 1024U * 1024U)
#define LDSK_NANO_BYTES (UINT64_C(40) * 2U * 10U * 512U)
#define LDSK_WORK_BYTES 65536U
#define LDSK_MAX_SLOTS 1000U

typedef struct ldsk_info_s {
    uint32_t kind;
    uint64_t length, stored, disk_size, gap, slots;
} ldsk_info;
typedef struct ldsk_stream_s {
    ldsk_info info;
    size_t index, count;
} ldsk_stream;

static bool ldsk_read_at(xx_io_device *device, int64_t at, void *data,
                         size_t n, xx_pd_struct *pd) {
    size_t done=0U;
    if (!device || at<0 || (!data && n) || (pd && xx_pd_is_stopped(pd)) ||
        xx_io_seek64(device,at,SEEK_SET)!=0) return false;
    while (done<n) {
        ssize_t got=xx_io_read(device,(uint8_t *)data+done,n-done);
        if (got<=0 || (size_t)got>n-done || (pd && xx_pd_is_stopped(pd)))
            return false;
        done+=(size_t)got;
    }
    return true;
}

static bool ldsk_write(xx_io_device *device, const uint8_t *data, size_t n,
                       xx_pd_struct *pd) {
    size_t done=0U;
    if (!device || (pd && xx_pd_is_stopped(pd))) return false;
    while (done<n) {
        ssize_t put=xx_io_write(device,data+done,n-done);
        if (put<=0 || (size_t)put>n-done || (pd && xx_pd_is_stopped(pd)))
            return false;
        done+=(size_t)put;
    }
    return true;
}

static bool ldsk_parse(Abstractformat *format, ldsk_info *out,
                       xx_pd_struct *pd) {
    xx_myz80 *reader=(xx_myz80 *)format;
    int64_t saved,total;
    bool result=false;
    if (!format || !format->device || !out || format->base_address<0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    saved=xx_io_tell(format->device);
    if (saved<0) return false;
    total=xx_io_total_size(format->device);
    xx_rt_memset(out,0,sizeof(*out));
    out->kind=reader->variant;
    if (total<format->base_address) goto done;
    out->length=(uint64_t)(total-format->base_address);
    if (out->kind==LDSK_MYZ80) {
        uint8_t header[256]; size_t i;
        if (out->length<256U || out->length>256U+LDSK_MYZ80_BYTES ||
            !ldsk_read_at(format->device,format->base_address,header,
                          sizeof(header),pd)) goto done;
        for (i=0U;i<sizeof(header);++i)
            if (header[i]!=0xe5U || (pd && xx_pd_is_stopped(pd))) goto done;
        out->stored=out->length-256U;
        out->disk_size=LDSK_MYZ80_BYTES;
        out->slots=2U; /* header and reconstructed disk */
    } else if (out->kind==LDSK_NANOWASP) {
        if (out->length!=LDSK_NANO_BYTES) goto done;
        out->disk_size=LDSK_NANO_BYTES;
        out->slots=2U; /* logical disk and original physical order */
    } else if (out->kind==LDSK_GOTEK_720 ||
               out->kind==LDSK_GOTEK_1440) {
        out->gap=out->kind==LDSK_GOTEK_720 ? UINT64_C(0x100000) :
                                                UINT64_C(0x180000);
        out->disk_size=out->kind==LDSK_GOTEK_720 ? UINT64_C(737280) :
                                                      UINT64_C(1474560);
        if (out->length<out->disk_size) goto done;
        out->slots=1U+(out->length-1U)/out->gap;
        if (out->slots>LDSK_MAX_SLOTS) goto done;
        /* LibDsk's sector writer leaves the last slot's padding unwritten.
         * Every listed slot still must contain its complete media payload. */
        if (out->length-(out->slots-1U)*out->gap<out->disk_size)
            goto done;
    } else goto done;
    result=true;
done:
    if (xx_io_seek64(format->device,saved,SEEK_SET)!=0) result=false;
    return result && !(pd && xx_pd_is_stopped(pd));
}

static size_t ldsk_member_count(const ldsk_info *info) {
    return (size_t)info->slots;
}

static uint64_t ldsk_member_size(const ldsk_info *info,size_t index) {
    if (info->kind==LDSK_MYZ80 && index==0U) return 256U;
    return info->disk_size;
}

static uint64_t ldsk_member_packed(const ldsk_info *info,size_t index) {
    if (info->kind==LDSK_MYZ80)
        return index==0U ? 256U : info->stored;
    return info->disk_size;
}

static void ldsk_member_name(const ldsk_info *info, size_t index,
                             char name[48]) {
    if (info->kind==LDSK_MYZ80)
        (void)xx_rt_snprintf(name,48U,"%s",index==0U ?
                            "myz80-header.bin" : "myz80.img");
    else if (info->kind==LDSK_NANOWASP)
        (void)xx_rt_snprintf(name,48U,"%s",index==0U ?
                            "nanowasp.img" : "nanowasp-physical.raw");
    else
        (void)xx_rt_snprintf(name,48U,"disk-%03u.img",(unsigned)index);
}

static bool ldsk_limit_ok(Abstractformat *format,const xx_list_s *options,
                          const ldsk_info *info,size_t index) {
    const xx_var *v;
    uint64_t work=LDSK_WORK_BYTES+sizeof(ldsk_stream)+4096U;
    if (index>=ldsk_member_count(info)) return false;
    v=xx_format_resolve_extra_parameter(format,options,
                                         XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (v && ldsk_member_size(info,index)>xx_var_get_u64(v)) return false;
    v=xx_format_resolve_extra_parameter(format,options,
                                         XX_META_ID_OPT_MEMORY_LIMIT);
    return !v || work<=xx_var_get_u64(v);
}

static bool ldsk_copy(Abstractformat *format,uint64_t source_at,
                      uint64_t n,xx_io_device *target,xx_pd_struct *pd) {
    uint8_t buffer[LDSK_WORK_BYTES];
    uint64_t cursor=0U;
    while (cursor<n) {
        size_t take=n-cursor>sizeof(buffer) ? sizeof(buffer) :
                                                (size_t)(n-cursor);
        if ((pd && xx_pd_is_stopped(pd)) ||
            source_at+cursor>(uint64_t)INT64_MAX ||
            !ldsk_read_at(format->device,(int64_t)(source_at+cursor),
                          buffer,take,pd) ||
            !ldsk_write(target,buffer,take,pd)) return false;
        cursor+=take;
    }
    return true;
}

static bool ldsk_fill(xx_io_device *target,uint64_t n,uint8_t value,
                      xx_pd_struct *pd) {
    uint8_t buffer[LDSK_WORK_BYTES];
    xx_rt_memset(buffer,value,sizeof(buffer));
    while (n) {
        size_t take=n>sizeof(buffer) ? sizeof(buffer) : (size_t)n;
        if (!ldsk_write(target,buffer,take,pd)) return false;
        n-=take;
    }
    return true;
}

static bool ldsk_emit(Abstractformat *format,const ldsk_info *info,
                      size_t index,xx_io_device *target,xx_pd_struct *pd) {
    uint64_t base=(uint64_t)format->base_address;
    if (info->kind==LDSK_MYZ80) {
        if (index==0U) return ldsk_copy(format,base,256U,target,pd);
        return ldsk_copy(format,base+256U,info->stored,target,pd) &&
               ldsk_fill(target,LDSK_MYZ80_BYTES-info->stored,0xe5U,pd);
    }
    if (info->kind==LDSK_NANOWASP) {
        static const uint8_t skew[10]={1,4,7,0,3,6,9,2,5,8};
        unsigned c,h,s;
        if (index==1U) return ldsk_copy(format,base,LDSK_NANO_BYTES,
                                         target,pd);
        for (c=0U;c<40U;++c) for (h=0U;h<2U;++h)
            for (s=0U;s<10U;++s) {
                uint8_t sector[512];
                uint64_t at=base+204800U*h+5120U*c+512U*skew[s];
                if ((pd && xx_pd_is_stopped(pd)) ||
                    !ldsk_read_at(format->device,(int64_t)at,sector,
                                  sizeof(sector),pd) ||
                    !ldsk_write(target,sector,sizeof(sector),pd)) return false;
            }
        return true;
    }
    return ldsk_copy(format,base+(uint64_t)index*info->gap,
                     info->disk_size,target,pd);
}

static bool ldsk_unpack_device(Abstractformat *format,const ldsk_info *info,
                               size_t index,const xx_list_s *options,
                               xx_io_device *destination,xx_pd_struct *pd) {
    int64_t saved;
    bool result;
    if (!format || !format->device || !destination ||
        destination==format->device ||
        !ldsk_limit_ok(format,options,info,index) ||
        (pd && xx_pd_is_stopped(pd))) return false;
    saved=xx_io_tell(format->device);
    if (saved<0) return false;
    result=ldsk_emit(format,info,index,destination,pd);
    if (xx_io_seek64(format->device,saved,SEEK_SET)!=0) result=false;
    return result && !(pd && xx_pd_is_stopped(pd));
}

static bool ldsk_check(Abstractformat *format,xx_pd_struct *pd) {
    ldsk_info info;
    return ldsk_parse(format,&info,pd);
}
static bool ldsk_handle(Abstractformat *format,xx_pd_struct *pd) {
    ldsk_info info;
    if (!ldsk_parse(format,&info,pd)) {
        if (format) { format->is_valid=false; format->base_info_handled=false; }
        return false;
    }
    format->format_size=(int64_t)info.length;
    format->number_of_archive_records=info.slots;
    format->overlay_offset=-1;
    format->overlay_size=0;
    format->is_valid=true;
    format->base_info_handled=true;
    return true;
}
static int64_t ldsk_size(Abstractformat *f,xx_pd_struct *pd) {
    return f && (f->base_info_handled || ldsk_handle(f,pd)) ?
           f->format_size : -1;
}
static uint64_t ldsk_count(Abstractformat *f,xx_pd_struct *pd) {
    return f && (f->base_info_handled || ldsk_handle(f,pd)) ?
           f->number_of_archive_records : 0U;
}
static void ldsk_free_stream(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}
static bool ldsk_record(xx_archive_record_state *state) {
    ldsk_stream *stream=(ldsk_stream *)state->internal_state;
    xx_archive_record *record=&state->current_record;
    char name[48];
    uint64_t packed=ldsk_member_packed(&stream->info,stream->index);
    uint64_t length=ldsk_member_size(&stream->info,stream->index);
    uint64_t base=(uint64_t)state->format->base_address;
    ldsk_member_name(&stream->info,stream->index,name);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset=state->format->base_address;
    record->header_size=stream->info.kind==LDSK_MYZ80 ? 256 : 0;
    record->data_offset=(int64_t)(base+
        (stream->info.kind==LDSK_MYZ80 ?
            (stream->index==0U ? 0U : 256U) :
         stream->info.kind==LDSK_NANOWASP ? 0U :
            (uint64_t)stream->index*stream->info.gap));
    record->compressed_size=(int64_t)packed;
    return xx_archive_record_set_original_name(record,name) &&
           xx_archive_record_set_meta_u64(record,XX_META_ID_COMPRESSED_SIZE,
                                          packed) &&
           xx_archive_record_set_meta_u64(record,XX_META_ID_UNCOMPRESSED_SIZE,
                                          length) &&
           xx_archive_record_set_meta_u64(record,XX_META_ID_COMPRESSION_METHOD,
                                          (stream->info.kind==LDSK_MYZ80 &&
                                           stream->index==1U) ||
                                          (stream->info.kind==LDSK_NANOWASP &&
                                           stream->index==0U) ? 1U : 0U) &&
           xx_archive_record_set_meta_bool(record,XX_META_ID_IS_FOLDER,false)&&
           xx_archive_record_set_meta_bool(record,XX_META_ID_IS_ENCRYPTED,false);
}
static xx_archive_record_state *ldsk_create(Abstractformat *format,
                                             const xx_list_s *options,
                                             xx_pd_struct *pd) {
    xx_archive_record_state *state;
    ldsk_stream *stream;
    size_t i;
    if (!format || !format->device ||
        (!format->base_info_handled && !ldsk_handle(format,pd))) return NULL;
    state=(xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream=(ldsk_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    if (!ldsk_parse(format,&stream->info,pd)) {
        xx_mem_free(stream);xx_mem_free(state);return NULL;
    }
    stream->index=0U;
    stream->count=ldsk_member_count(&stream->info);
    xx_archive_record_state_init(state,format);
    state->internal_state=stream;
    state->free_internal=ldsk_free_stream;
    state->total_records=(int64_t)stream->count;
    for (i=0U;options && i<options->count;++i) {
        const xx_meta *m=(const xx_meta *)xx_list_at(options,i);
        xx_meta copy;
        if (!m) continue;
        xx_meta_init(&copy,m->meta_id);
        if (!xx_var_copy(&copy.var,&m->var) ||
            !xx_list_append(&state->options,&copy)) {
            xx_meta_cleanup(&copy);
            xx_archive_record_state_free(state);
            return NULL;
        }
    }
    state->has_record=stream->count>0U && ldsk_record(state);
    if (!state->has_record) {
        xx_archive_record_state_free(state);return NULL;
    }
    return state;
}
static const xx_archive_record *ldsk_current(Abstractformat *f,
                                              xx_archive_record_state *state) {
    return f && state && state->format==f && state->has_record ?
           &state->current_record : NULL;
}
static bool ldsk_next(Abstractformat *f,xx_archive_record_state *state,
                      xx_pd_struct *pd) {
    ldsk_stream *stream;
    if (!f || !state || state->format!=f || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream=(ldsk_stream *)state->internal_state;
    if (++stream->index>=stream->count) {
        state->has_record=false;
        return false;
    }
    ++state->current_index;
    state->has_record=ldsk_record(state);
    return state->has_record;
}
static ssize_t ldsk_discard(xx_io_device *device,const void *data,size_t n) {
    (void)device;(void)data;return (ssize_t)n;
}
static bool ldsk_same_path(const char *a,const char *b) {
    while (*a && *b) {
        char x=*a++,y=*b++;
        if (x=='\\') x='/';
        if (y=='\\') y='/';
        if (x>='A' && x<='Z') x=(char)(x+32);
        if (y>='A' && y<='Z') y=(char)(y+32);
        if (x!=y) return false;
    }
    return *a==*b;
}
static xx_io_device *ldsk_stage(const char *destination,char **stage) {
    char *directory;
    size_t i,parent=0U;
    unsigned attempt;
    *stage=NULL;
    directory=xx_str_dup(destination);
    if (!directory) return NULL;
    for (i=0U;directory[i];++i)
        if (directory[i]=='/' || directory[i]=='\\') parent=i+1U;
    directory[parent]=0;
    for (attempt=0U;attempt<128U;++attempt) {
        char suffix[48],*candidate;
        xx_io_device *device;
        (void)xx_rt_snprintf(suffix,sizeof(suffix),
                             ".xx_libdsk.tmp.%u",attempt);
        candidate=xx_str_concat(directory,suffix);
        if (!candidate) break;
        if (ldsk_same_path(candidate,destination)) {
            xx_str_free(candidate);continue;
        }
        device=xx_io_file_open(candidate,"wbx");
        if (device) {
            *stage=candidate;
            xx_str_free(directory);
            return device;
        }
        xx_str_free(candidate);
    }
    xx_str_free(directory);
    return NULL;
}
static bool ldsk_unpack(Abstractformat *format,xx_archive_record_state *state,
                        xx_pd_struct *pd) {
    ldsk_stream *stream;
    const xx_var *v,*ov;
    const char *base=NULL;
    char *owned=NULL,*path=NULL,*stage=NULL;
    xx_io_device *output=NULL;
    xx_io_device discard;
    bool overwrite=false,result=false;
    char name[48];
    if (!format || !state || state->format!=format || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream=(ldsk_stream *)state->internal_state;
    if (!ldsk_limit_ok(format,&state->options,&stream->info,stream->index))
        return false;
    v=xx_format_resolve_extra_parameter(format,&state->options,
                                         XX_META_ID_OPT_UNPACK_PATH);
    if (!v) {
        xx_rt_memset(&discard,0,sizeof(discard));
        discard.write=ldsk_discard;
        return ldsk_unpack_device(format,&stream->info,stream->index,
                                  &state->options,&discard,pd);
    }
    if (v->type==XX_VAR_TYPE_STRING || v->type==XX_VAR_TYPE_STRING_VIEW)
        base=xx_var_get_str(v);
    else if (v->type==XX_VAR_TYPE_WSTRING ||
             v->type==XX_VAR_TYPE_WSTRING_VIEW)
        base=owned=xx_str_unicode_to_utf8(xx_var_get_wstr(v));
    if (!base) goto done;
    ldsk_member_name(&stream->info,stream->index,name);
    path=base[0] && base[xx_str_len(base)-1U]!='/' &&
         base[xx_str_len(base)-1U]!='\\' ?
         xx_str_concat3(base,"/",name) : xx_str_concat(base,name);
    if (!path) goto done;
    ov=xx_format_resolve_extra_parameter(format,&state->options,
                                          XX_META_ID_OPT_OVERWRITE);
    overwrite=ov && xx_var_get_bool(ov);
    if ((!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path,false) ||
        (pd && xx_pd_is_stopped(pd))) goto done;
    output=ldsk_stage(path,&stage);
    if (!output) goto done;
    result=ldsk_unpack_device(format,&stream->info,stream->index,
                              &state->options,output,pd);
    if (xx_io_close(output)!=0) result=false;
    output=NULL;
    if (result && !(pd && xx_pd_is_stopped(pd)))
        result=xx_io_file_replace_a(stage,path,overwrite);
    else result=false;
done:
    if (output) { (void)xx_io_close(output);result=false; }
    if (stage) {
        if (!result) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    if (path) xx_str_free(path);
    if (owned) xx_str_free(owned);
    return result;
}
static void ldsk_free_records(Abstractformat *f,xx_archive_record_state *s) {
    (void)f;xx_archive_record_state_free(s);
}
static void ldsk_destroy_vtable(Abstractformat *f) {
    if (f) xx_format_cleanup_extra_parameters(f);
}
static void ldsk_init(xx_myz80 *reader,xx_io_device *device,int64_t base,
                      uint32_t variant,xx_file_type_t type,const char *ext) {
    if (!reader) return;
    xx_rt_memset(reader,0,sizeof(*reader));
    xx_format_init(&reader->format,device,base);
    reader->variant=variant;
    reader->format.file_type=type;
    reader->format.format_type=XX_TYPE_ARCHIVE;
    reader->format.is_archive=true;
    xx_format_set_extension(&reader->format,ext);
    reader->format.check_is_valid=ldsk_check;
    reader->format.handle_base_info=ldsk_handle;
    reader->format.get_format_size=ldsk_size;
    reader->format.get_number_of_archive_records=ldsk_count;
    reader->format.create_archive_records_reading=ldsk_create;
    reader->format.get_current_archive_record=ldsk_current;
    reader->format.archive_record_move_to_next=ldsk_next;
    reader->format.unpack_current_archive_record=ldsk_unpack;
    reader->format.free_archive_records_reading=ldsk_free_records;
    reader->format.destroy=ldsk_destroy_vtable;
}
void xx_myz80_init(xx_myz80 *r,xx_io_device *d,int64_t b) {
    ldsk_init(r,d,b,LDSK_MYZ80,XX_FILE_TYPE_MYZ80,"myz80");
}
xx_myz80 *xx_myz80_create(xx_io_device *d,int64_t b) {
    xx_myz80 *r=(xx_myz80 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_myz80_init(r,d,b);
    return r;
}
void xx_myz80_destroy(xx_myz80 *r) {
    if (r) ldsk_destroy_vtable(&r->format);
}
void xx_myz80_free(xx_myz80 *r) {
    if (r) { xx_myz80_destroy(r);xx_mem_free(r); }
}
void xx_nanowasp_init(xx_nanowasp *r,xx_io_device *d,int64_t b) {
    ldsk_init(r,d,b,LDSK_NANOWASP,XX_FILE_TYPE_NANOWASP,"nwasp");
}
xx_nanowasp *xx_nanowasp_create(xx_io_device *d,int64_t b) {
    xx_nanowasp *r=(xx_nanowasp *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nanowasp_init(r,d,b);
    return r;
}
void xx_nanowasp_destroy(xx_nanowasp *r) {
    if (r) ldsk_destroy_vtable(&r->format);
}
void xx_nanowasp_free(xx_nanowasp *r) {
    if (r) { xx_nanowasp_destroy(r);xx_mem_free(r); }
}
void xx_gotek_init(xx_gotek *r,xx_io_device *d,int64_t b,
                   unsigned density_kib) {
    ldsk_init(r,d,b,
              density_kib==720U ? LDSK_GOTEK_720 :
              density_kib==1440U ? LDSK_GOTEK_1440 : 0U,
              XX_FILE_TYPE_GOTEK,"gotek");
}
xx_gotek *xx_gotek_create(xx_io_device *d,int64_t b,unsigned density_kib) {
    xx_gotek *r=(xx_gotek *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gotek_init(r,d,b,density_kib);
    return r;
}
void xx_gotek_destroy(xx_gotek *r) {
    if (r) ldsk_destroy_vtable(&r->format);
}
void xx_gotek_free(xx_gotek *r) {
    if (r) { xx_gotek_destroy(r);xx_mem_free(r); }
}
static bool ldsk_direct(xx_myz80 *reader,size_t member,
                        xx_io_device *target,xx_pd_struct *pd) {
    ldsk_info info;
    return reader && ldsk_parse(&reader->format,&info,pd) &&
           ldsk_unpack_device(&reader->format,&info,member,NULL,target,pd);
}
bool xx_myz80_unpack_to_device(xx_myz80 *r,xx_io_device *d,
                               xx_pd_struct *pd) {
    return ldsk_direct(r,1U,d,pd);
}
bool xx_nanowasp_unpack_to_device(xx_nanowasp *r,xx_io_device *d,
                                  xx_pd_struct *pd) {
    return ldsk_direct(r,0U,d,pd);
}
bool xx_gotek_unpack_disk_to_device(xx_gotek *r,size_t slot,
                                    xx_io_device *d,xx_pd_struct *pd) {
    return ldsk_direct(r,slot,d,pd);
}
