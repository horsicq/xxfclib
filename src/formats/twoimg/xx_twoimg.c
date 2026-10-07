/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Apple II 2IMG disk image.  ProDOS contents are
 * delegated through a bounded view of the image extent. Other images expose
 * the stored image, comment and creator-private block as members.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/twoimg/xx_twoimg.h"
#include "xxfclib/formats/prodos/xx_prodos.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"

#ifdef TWOIMG
#define XX_TWOIMG_FILE_TYPE XX_FILE_TYPE_TWOIMG
#else
#define XX_TWOIMG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define TWOIMG_MAX_MEMBERS 65536U
#define TWOIMG_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct twoimg_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;      /* 0 = stored, non-zero = format codec */
    bool decode;
} twoimg_member;

typedef struct twoimg_stream_s {
    twoimg_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    int64_t image_offset;
    int64_t image_size;
    uint32_t image_format;
    uint64_t prodos_count;
    xx_io_device *prodos_device;
    xx_prodos *prodos;
    xx_archive_record_state *prodos_state;
} twoimg_stream;

static bool twoimg_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction.  The helper only has to be CRT free. */
static char *twoimg_make_name(const char *prefix, int a, int b,
                           const char *suffix) {
    char buffer[64];
    size_t used = 0U;
    size_t index;
    char *result;
    for (index = 0U; prefix && prefix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[index];
    }
    if (a >= 0) {
        char digits[8];
        size_t count = 0U;
        int value = a;
        do {
            digits[count++] = (char)('0' + (value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 2U) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    if (b >= 0) {
        if (used >= sizeof(buffer) - 2U) return NULL;
        buffer[used++] = '_';
        buffer[used++] = (char)('0' + (b % 10));
    }
    for (index = 0U; suffix && suffix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[index];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

static void twoimg_stream_free(void *opaque) {
    twoimg_stream *stream = (twoimg_stream *)opaque;
    size_t index;
    if (!stream) return;
    if (stream->prodos_state)
        xx_prodos_free_archive_records_reading(&stream->prodos->format,
                                                stream->prodos_state);
    if (stream->prodos) xx_prodos_free(stream->prodos);
    if (stream->prodos_device) (void)xx_io_close(stream->prodos_device);
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool twoimg_add_member(twoimg_stream *stream, const twoimg_member *member) {
    twoimg_member *grown;
    if (!stream || !member || stream->count >= TWOIMG_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (twoimg_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define TWOIMG_HEADER_SIZE 64

static bool twoimg_range_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total && size <= total - offset;
}

/* "2IMG", a u16 header size, a u16 version, a u32 image format (0 DOS order,
 * 1 ProDOS order, 2 nibble), a u32 block count and four offset/size pairs for
 * the image, the comment and the creator block.  Several widely circulated
 * writers leave the image length zero while the block count stays right, so
 * for the two sector orders the block count wins - it is bounded by the file
 * either way. */
static bool twoimg_parse(Abstractformat *format, twoimg_stream **result) {
    uint8_t header[TWOIMG_HEADER_SIZE];
    twoimg_stream *stream;
    twoimg_member member;
    int64_t total, size;
    int64_t header_size, data_offset, data_size, comment_offset, comment_size;
    int64_t creator_offset, creator_size;
    uint32_t version, image_format, blocks;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)TWOIMG_HEADER_SIZE ||
        !twoimg_read_at(format->device, format->base_address, header,
                        sizeof(header)) ||
        header[0] != '2' || header[1] != 'I' || header[2] != 'M' ||
        header[3] != 'G')
        return false;
    header_size = (int64_t)xx_data_get_u16(header + 8U, 2, 0, false);
    version = xx_data_get_u16(header + 10U, 2, 0, false);
    image_format = xx_data_get_u32(header + 12U, 4, 0, false);
    blocks = xx_data_get_u32(header + 20U, 4, 0, false);
    data_offset = (int64_t)xx_data_get_u32(header + 24U, 4, 0, false);
    data_size = (int64_t)xx_data_get_u32(header + 28U, 4, 0, false);
    comment_offset = (int64_t)xx_data_get_u32(header + 32U, 4, 0, false);
    comment_size = (int64_t)xx_data_get_u32(header + 36U, 4, 0, false);
    creator_offset = (int64_t)xx_data_get_u32(header + 40U, 4, 0, false);
    creator_size = (int64_t)xx_data_get_u32(header + 44U, 4, 0, false);
    if (header_size < (int64_t)TWOIMG_HEADER_SIZE || header_size > size ||
        version > 1U || image_format > 2U || blocks == 0U ||
        data_offset < header_size || data_offset > size)
        return false;
    if (image_format <= 1U) {
        uint64_t block_bytes = (uint64_t)blocks * 512U;
        if (block_bytes > (uint64_t)(size - data_offset)) return false;
        if ((uint64_t)data_size != block_bytes) data_size = (int64_t)block_bytes;
    }
    if (data_size <= 0 || !twoimg_range_within(size, data_offset, data_size) ||
        (uint64_t)data_size > TWOIMG_MAX_OUTPUT)
        return false;
    if (comment_size != 0 &&
        (!twoimg_range_within(size, comment_offset, comment_size) ||
         comment_offset < header_size))
        return false;
    if (creator_size != 0 &&
        (!twoimg_range_within(size, creator_offset, creator_size) ||
         creator_offset < header_size))
        return false;
    stream = (twoimg_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    xx_mem_zero(&member, sizeof(member));
    member.name = twoimg_make_name("image", -1, -1,
                                   image_format == 2U ? ".nib" : ".dsk");
    member.header_offset = format->base_address;
    member.header_size = header_size;
    member.data_offset = format->base_address + data_offset;
    member.packed_size = data_size;
    member.unpacked_size = (uint64_t)data_size;
    stream->image_offset=member.data_offset;
    stream->image_size=data_size;
    stream->image_format=image_format;
    if (!member.name || !twoimg_add_member(stream, &member)) goto fail;
    if (comment_size != 0) {
        xx_mem_zero(&member, sizeof(member));
        member.name = twoimg_make_name("comment", -1, -1, ".txt");
        member.header_offset = format->base_address + 32;
        member.header_size = 8;
        member.data_offset = format->base_address + comment_offset;
        member.packed_size = comment_size;
        member.unpacked_size = (uint64_t)comment_size;
        if (!member.name || !twoimg_add_member(stream, &member)) goto fail;
    }
    if (creator_size != 0) {
        xx_mem_zero(&member, sizeof(member));
        member.name = twoimg_make_name("creator", -1, -1, ".bin");
        member.header_offset = format->base_address + 40;
        member.header_size = 8;
        member.data_offset = format->base_address + creator_offset;
        member.packed_size = creator_size;
        member.unpacked_size = (uint64_t)creator_size;
        if (!member.name || !twoimg_add_member(stream, &member)) goto fail;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    if (member.name) xx_mem_free(member.name);
    twoimg_stream_free(stream);
    return false;
}

static bool twoimg_decode(Abstractformat *format, const twoimg_member *member,
                          uint8_t **plain, size_t *plain_size) {
    (void)format;
    (void)member;
    (void)plain;
    (void)plain_size;
    return false;
}

static bool twoimg_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *twoimg_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}
static bool twoimg_forward_parameters(Abstractformat *outer,
                                      const xx_list_s *options,
                                      xx_prodos *inner) {
    const xx_list_s *sources[2]={&outer->list_extra_parameters,options};
    const xx_var *path_option;
    const char *base=NULL;
    char *owned=NULL,*prefixed=NULL;
    xx_var target;
    size_t source,index;
    bool result=true;
    for (source=0U;source<2U;++source) {
        const xx_list_s *list=sources[source];
        if (!list) continue;
        for (index=0U;index<list->count;++index) {
            const xx_meta *meta=(const xx_meta *)xx_list_at(list,index);
            if (meta && !xx_format_set_extra_parameter(&inner->format,
                                                        meta->meta_id,&meta->var))
                return false;
        }
    }
    path_option=xx_format_resolve_extra_parameter(
        outer,options,XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
    if (path_option->type==XX_VAR_TYPE_STRING ||
        path_option->type==XX_VAR_TYPE_STRING_VIEW)
        base=xx_var_get_str(path_option);
    else if (path_option->type==XX_VAR_TYPE_WSTRING ||
             path_option->type==XX_VAR_TYPE_WSTRING_VIEW) {
        owned=xx_str_unicode_to_utf8(xx_var_get_wstr(path_option)); base=owned;
    }
    if (!base) { xx_str_free(owned); return false; }
    prefixed=*base && base[strlen(base)-1U]!='/' &&
             base[strlen(base)-1U]!='\\' ?
        xx_str_concat3(base,"/","ProDOS") : xx_str_concat(base,"ProDOS");
    xx_var_init(&target);
    if (!prefixed || !xx_var_set_str(&target,prefixed) ||
        !xx_format_set_extra_parameter(&inner->format,
                                       XX_META_ID_OPT_UNPACK_PATH,&target))
        result=false;
    xx_var_cleanup(&target);
    xx_str_free(prefixed); xx_str_free(owned);
    return result;
}
static bool twoimg_try_prodos(Abstractformat *format,twoimg_stream *stream,
                              const xx_list_s *options,bool need_state) {
    xx_prodos *nested;
    xx_io_device *window;
    xx_io_volume range;
    if (!format || !stream || stream->image_format>1U) return false;
    range.device=format->device;
    range.offset=stream->image_offset;
    range.size=stream->image_size;
    window=xx_io_multivolume_open(&range,1U,false);
    if (!window) return false;
    nested=xx_prodos_create(window,0);
    if (!nested) { (void)xx_io_close(window); return false; }
    if (!xx_prodos_check_is_valid(&nested->format,NULL) ||
        !xx_prodos_handle_base_info(&nested->format,NULL) ||
        nested->truncated ||
        (uint64_t)nested->total_blocks*512U>(uint64_t)stream->image_size ||
        nested->number_of_records==0U ||
        (need_state && !twoimg_forward_parameters(format,options,nested))) {
        xx_prodos_free(nested); (void)xx_io_close(window); return false;
    }
    stream->prodos_count=nested->number_of_records;
    if (need_state) {
        stream->prodos_state=xx_prodos_create_archive_records_reading(
            &nested->format,&nested->format.list_extra_parameters,NULL);
        if (!stream->prodos_state || !stream->prodos_state->has_record) {
            if (stream->prodos_state)
                xx_prodos_free_archive_records_reading(&nested->format,
                                                        stream->prodos_state);
            stream->prodos_state=NULL;
            stream->prodos_count=0U;
            xx_prodos_free(nested); (void)xx_io_close(window); return false;
        }
        stream->prodos_device=window;
        stream->prodos=nested;
    } else { xx_prodos_free(nested); (void)xx_io_close(window); }
    return true;
}
static bool twoimg_prodos_record(xx_archive_record *record,
                                 twoimg_stream *stream) {
    const xx_archive_record *original=xx_prodos_get_current_archive_record(
        &stream->prodos->format,stream->prodos_state);
    const char *name;
    char *prefixed;
    size_t index;
    if (!original || !(name=xx_archive_record_get_original_name(original)))
        return false;
    if (original->header_offset<0 || original->header_size<0 ||
        original->header_offset>stream->image_size-original->header_size ||
        (original->data_offset>=0 &&
         original->data_offset>=stream->image_size))
        return false;
    prefixed=xx_str_concat3("ProDOS","/",name);
    if (!prefixed) return false;
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset=original->header_offset<0 ? -1 :
        original->header_offset+stream->image_offset;
    record->header_size=original->header_size;
    record->data_offset=original->data_offset<0 ? -1 :
        original->data_offset+stream->image_offset;
    record->compressed_size=original->compressed_size;
    for (index=0U;index<original->list_meta.count;++index) {
        const xx_meta *meta=(const xx_meta *)xx_list_at(&original->list_meta,index);
        if (meta && !xx_archive_record_set_meta(record,meta->meta_id,&meta->var)) {
            xx_str_free(prefixed); return false;
        }
    }
    {
        bool ok=xx_archive_record_set_original_name(record,prefixed);
        xx_str_free(prefixed); return ok;
    }
}

static bool twoimg_set_record(xx_archive_record *record,
                           const twoimg_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Stored members are copied verbatim; everything else goes to the format
 * codec above, which is the only place a size can grow. */
static bool twoimg_extract(Abstractformat *format, const twoimg_member *member,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *output;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->decode) return twoimg_decode(format, member, plain, plain_size);
    if (member->packed_size < 0 ||
        (uint64_t)member->packed_size > TWOIMG_MAX_OUTPUT) return false;
    output = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    if (!output) return false;
    if (member->packed_size != 0 &&
        !twoimg_read_at(format->device, member->data_offset, output,
                     (size_t)member->packed_size)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->packed_size;
    return true;
}

void xx_twoimg_init(xx_twoimg *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TWOIMG_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-apple2-2img");
    xx_format_set_extension(&archive->format, "2mg");
    archive->format.check_is_valid = xx_twoimg_check_is_valid;
    archive->format.handle_base_info = xx_twoimg_handle_base_info;
    archive->format.get_format_size = xx_twoimg_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_twoimg_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_twoimg_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_twoimg_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_twoimg_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_twoimg_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_twoimg_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_twoimg *xx_twoimg_create(xx_io_device *device, int64_t base_address) {
    xx_twoimg *archive = (xx_twoimg *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_twoimg_init(archive, device, base_address);
    return archive;
}

void xx_twoimg_destroy(xx_twoimg *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_twoimg_free(xx_twoimg *archive) {
    if (!archive) return;
    xx_twoimg_destroy(archive);
    xx_mem_free(archive);
}

bool xx_twoimg_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    twoimg_stream *stream;
    (void)pd;
    if (!twoimg_parse(format, &stream)) return false;
    twoimg_stream_free(stream);
    return true;
}

bool xx_twoimg_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    twoimg_stream *stream;
    xx_twoimg *archive;
    (void)pd;
    if (!format || !twoimg_parse(format, &stream)) return false;
    archive = (xx_twoimg *)format;
    (void)twoimg_try_prodos(format,stream,NULL,false);
    archive->number_of_records = stream->prodos_count ?
        stream->prodos_count : stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    twoimg_stream_free(stream);
    return true;
}

int64_t xx_twoimg_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_twoimg_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_twoimg_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_twoimg_handle_base_info(format, pd))
               ? ((xx_twoimg *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_twoimg_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    twoimg_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!twoimg_parse(format, &stream)) return NULL;
    (void)twoimg_try_prodos(format,stream,options,true);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        twoimg_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = twoimg_stream_free;
    state->total_records = (int64_t)(stream->prodos ? stream->prodos_count :
                                    stream->count);
    if (!twoimg_copy_options(&state->options, options) ||
        !(stream->prodos ?
          twoimg_prodos_record(&state->current_record,stream) :
          twoimg_set_record(&state->current_record,&stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_twoimg_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_twoimg_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    twoimg_stream *stream;
    (void)pd;
    if (format && state && state->format==format &&
        (stream=(twoimg_stream *)state->internal_state) && stream->prodos) {
        if (!xx_prodos_archive_record_move_to_next(&stream->prodos->format,
                                                    stream->prodos_state,pd)) {
            state->has_record=false; return false;
        }
        ++state->current_index;
        state->has_record=twoimg_prodos_record(&state->current_record,stream);
        return state->has_record;
    }
    if (!format || !state || state->format != format ||
        !(stream = (twoimg_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = twoimg_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_twoimg_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    twoimg_stream *stream;
    twoimg_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (twoimg_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    if (stream->prodos)
        return xx_prodos_unpack_current_archive_record(&stream->prodos->format,
                                                        stream->prodos_state,pd);
    if (stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!twoimg_extract(format, member, &plain, &plain_size)) goto done;
    path_option = twoimg_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_twoimg_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
