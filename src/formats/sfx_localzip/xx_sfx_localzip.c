/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Self-extractors containing ZIP records or a bounded SoftPaq stored table.
 * INFTool, WinImage, ZeroG and SoftPaq program code is never executed.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_localzip/xx_sfx_localzip.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef SFX_LOCALZIP
#define XX_SFX_LOCALZIP_FILE_TYPE XX_FILE_TYPE_SFX_LOCALZIP
#else
#define XX_SFX_LOCALZIP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The table-fits-in-the-file check is what actually bounds the allocation. */
#define XX_SFX_LOCALZIP_MAX_SLOTS 1000000U

typedef struct xx_sfx_localzip_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint32_t method;
    bool has_crc;
    bool is_folder;
} xx_sfx_localzip_member;

typedef struct xx_sfx_localzip_stream_s {
    xx_sfx_localzip_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_sfx_localzip_stream;

static void xx_sfx_localzip_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_sfx_localzip_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_sfx_localzip_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static XXFC_MAYBE_UNUSED uint16_t xx_sfx_localzip_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[0] << 8));
}

static XXFC_MAYBE_UNUSED uint32_t xx_sfx_localzip_be32(const uint8_t *data) {
    return (uint32_t)data[3] | ((uint32_t)data[2] << 8) |
           ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

static bool xx_sfx_localzip_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_sfx_localzip_path_safe(const char *path) {
    const char *cursor = path;

    if (!path || !path[0] || path[0] == '/') return false;
    if (path[1] == ':') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* Build a filesystem-safe name from raw 8-bit bytes.  Backslashes become
 * path separators, everything a filesystem would object to becomes '_'. */
static char *xx_sfx_localzip_make_name(const uint8_t *raw, size_t size,
                                 bool keep_path) {
    char *text;
    size_t length = 0U;
    size_t index;

    if (!raw && size != 0U) return NULL;
    text = (char *)xx_mem_alloc(size + 2U);
    if (!text) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t c = raw[index];
        if (c == 0x00U) break;
        if ((c == '/' || c == '\\') && keep_path) {
            if (length != 0U && text[length - 1U] == '/') continue;
            text[length++] = '/';
            continue;
        }
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            text[length++] = '_';
        } else {
            text[length++] = (char)c;
        }
    }
    while (length != 0U &&
           (text[length - 1U] == ' ' || text[length - 1U] == '.' ||
            text[length - 1U] == '/')) {
        --length;
    }
    while (length != 0U && text[0] == '/') {
        xx_rt_memmove(text, text + 1, length - 1U);
        --length;
    }
    if (length == 0U) text[length++] = '_';
    text[length] = 0;
    return text;
}

static void xx_sfx_localzip_stream_free(void *pointer) {
    xx_sfx_localzip_stream *stream = (xx_sfx_localzip_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Grow the member vector one entry at a time.  The caller has already bounded
 * the member count against the real file size, so this cannot be driven to an
 * unbounded allocation by a small header. */
static bool xx_sfx_localzip_add(xx_sfx_localzip_stream *stream,
                          const xx_sfx_localzip_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_sfx_localzip_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_sfx_localzip_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* Slots carry no names; they are filed under a zero-padded index, the width
 * taken from the slot count the way U3's listing does it. */
static XXFC_MAYBE_UNUSED char *xx_sfx_localzip_slot_name(uint32_t index, uint32_t width) {
    char text[32];
    size_t length = 0U;
    uint32_t scale = 1U;
    uint32_t digits = 1U;

    while (digits < width && scale <= 100000000U) {
        scale *= 10U;
        ++digits;
    }
    while (index / scale >= 10U && scale <= 100000000U) scale *= 10U;
    for (; scale != 0U; scale /= 10U) {
        text[length++] = (char)('0' + ((index / scale) % 10U));
    }
    text[length++] = '.';
    text[length++] = 'b';
    text[length++] = 'i';
    text[length++] = 'n';
    text[length] = 0;
    return xx_str_dup(text);
}

static XXFC_MAYBE_UNUSED uint32_t xx_sfx_localzip_digits(uint32_t value) {
    uint32_t digits = 1U;

    while (value >= 10U) {
        value /= 10U;
        ++digits;
    }
    return digits;
}


static bool xx_sfx_localzip_has(const uint8_t *p, size_t size,
                               const char *word, size_t len) {
    size_t i;
    if (len>size) return false;
    for (i=0U;i<=size-len;++i)
        if (!xx_rt_memcmp(p+i,word,len)) return true;
    return false;
}

static bool xx_sfx_localzip_decode(Abstractformat *self,
                                   const xx_sfx_localzip_member *member,
                                   uint8_t **out,size_t *out_size,
                                   xx_pd_struct *pd) {
    uint8_t *compressed=NULL,*plain=NULL;
    size_t psize,usize,written=0U;
    bool ok;
    if (!self || !member || !out || !out_size || member->packed_size<0 ||
        member->unpacked_size>256U*1024U*1024U ||
        member->packed_size>256*1024*1024 || (pd && xx_pd_is_stopped(pd)))
        return false;
    psize=(size_t)member->packed_size;
    usize=(size_t)member->unpacked_size;
    compressed=(uint8_t *)xx_mem_alloc(psize ? psize : 1U);
    plain=(uint8_t *)xx_mem_alloc(usize ? usize : 1U);
    if (!compressed || !plain ||
        (psize && !xx_sfx_localzip_read_at(self,member->data_offset,
                                           compressed,psize))) goto bad;
    if (member->method==0U) {
        if (psize!=usize) goto bad;
        if (usize) xx_mem_copy(plain,compressed,usize);
        written=usize;
    } else if (member->method==8U) {
        if (!xx_deflate_decompress_memory(compressed,psize,plain,
                                          usize,&written,false)) goto bad;
    } else goto bad;
    ok=written==usize && (!member->has_crc ||
        xx_crc32_calc(0,plain,usize)==member->crc32);
    if (!ok) goto bad;
    xx_mem_free(compressed);
    *out=plain;*out_size=usize;return true;
bad:
    if (compressed) xx_mem_free(compressed);
    if (plain) xx_mem_free(plain);
    return false;
}

/* ZIP executables may put the archive after a PE stub.  The central
 * directory's local offsets are relative to the first ZIP byte. */
static bool xx_sfx_localzip_parse_central(Abstractformat *self,
                                           const uint8_t *data, size_t n,
                                           xx_sfx_localzip_stream *stream,
                                           xx_pd_struct *pd) {
    size_t search, eocd = SIZE_MAX, central, cursor, base, end;
    uint16_t count, i;
    uint32_t central_size, declared_offset;
    if (n < 22U || !stream) return false;
    search = n > 65557U ? n - 65557U : 0U;
    for (cursor = n - 22U;; --cursor) {
        if (!xx_rt_memcmp(data + cursor, "PK\x05\x06", 4U) &&
            xx_sfx_localzip_le16(data + cursor + 4U) == 0U &&
            xx_sfx_localzip_le16(data + cursor + 6U) == 0U &&
            xx_sfx_localzip_le16(data + cursor + 8U) ==
                xx_sfx_localzip_le16(data + cursor + 10U) &&
            (size_t)xx_sfx_localzip_le16(data + cursor + 20U) <=
                n - cursor - 22U) {
            eocd = cursor;
            break;
        }
        if (cursor == search) break;
    }
    if (eocd == SIZE_MAX) return false;
    count = xx_sfx_localzip_le16(data + eocd + 10U);
    central_size = xx_sfx_localzip_le32(data + eocd + 12U);
    declared_offset = xx_sfx_localzip_le32(data + eocd + 16U);
    if (!count || count > 4096U || central_size > eocd ||
        declared_offset > eocd - central_size) return false;
    central = eocd - (size_t)central_size;
    base = central - (size_t)declared_offset;
    cursor = central;
    end = eocd;
    for (i = 0U; i < count; ++i) {
        uint16_t flags, method, name_len, extra_len, comment_len;
        uint16_t local_name_len, local_extra_len;
        uint32_t packed, plain, crc, relative;
        size_t local, payload, record_size;
        xx_sfx_localzip_member member;
        if ((pd && xx_pd_is_stopped(pd)) || cursor > end ||
            end - cursor < 46U ||
            xx_rt_memcmp(data + cursor, "PK\x01\x02", 4U)) return false;
        flags = xx_sfx_localzip_le16(data + cursor + 8U);
        method = xx_sfx_localzip_le16(data + cursor + 10U);
        crc = xx_sfx_localzip_le32(data + cursor + 16U);
        packed = xx_sfx_localzip_le32(data + cursor + 20U);
        plain = xx_sfx_localzip_le32(data + cursor + 24U);
        name_len = xx_sfx_localzip_le16(data + cursor + 28U);
        extra_len = xx_sfx_localzip_le16(data + cursor + 30U);
        comment_len = xx_sfx_localzip_le16(data + cursor + 32U);
        relative = xx_sfx_localzip_le32(data + cursor + 42U);
        record_size = 46U + (size_t)name_len + (size_t)extra_len +
                      (size_t)comment_len;
        if ((flags & 1U) || (method != 0U && method != 8U) ||
            !name_len || name_len > 4096U || record_size > end - cursor ||
            (size_t)relative > central - base) return false;
        local = base + (size_t)relative;
        if (local > central || central - local < 30U ||
            xx_rt_memcmp(data + local, "PK\x03\x04", 4U) ||
            xx_sfx_localzip_le16(data + local + 8U) != method) return false;
        local_name_len = xx_sfx_localzip_le16(data + local + 26U);
        local_extra_len = xx_sfx_localzip_le16(data + local + 28U);
        if ((size_t)local_name_len + (size_t)local_extra_len >
            central - local - 30U || local_name_len != name_len ||
            xx_rt_memcmp(data + local + 30U, data + cursor + 46U,
                         name_len)) return false;
        payload = local + 30U + (size_t)local_name_len +
                  (size_t)local_extra_len;
        if (packed > central - payload || plain > 256U * 1024U * 1024U)
            return false;
        xx_mem_zero(&member, sizeof(member));
        member.name = xx_sfx_localzip_make_name(data + cursor + 46U,
                                                name_len, true);
        if (!member.name || !xx_sfx_localzip_path_safe(member.name)) {
            xx_str_free(member.name);
            return false;
        }
        member.header_offset = self->base_address + (int64_t)local;
        member.header_size = (int64_t)(payload - local);
        member.data_offset = self->base_address + (int64_t)payload;
        member.packed_size = (int64_t)packed;
        member.unpacked_size = (uint64_t)plain;
        member.method = method;
        member.crc32 = crc;
        member.has_crc = true;
        if (!xx_sfx_localzip_add(stream, &member)) {
            xx_str_free(member.name);
            return false;
        }
        cursor += record_size;
    }
    return cursor == end;
}

/* SoftPaq's !3PS table names three stored records by absolute file offsets.
 * Each record starts with an 8-bit filename length and then raw bytes. */
static bool xx_sfx_localzip_parse_softpaq(Abstractformat *self,
                                          const uint8_t *data, size_t n,
                                          xx_sfx_localzip_stream *stream) {
    size_t marker, i;
    uint32_t offsets[3], lengths[3];
    for (marker=0U;marker+40U<=n && marker<131072U;++marker)
        if (!xx_rt_memcmp(data+marker,"!3PS",4U)) break;
    if (marker+40U>n || marker>=131072U) return false;
    offsets[0]=xx_sfx_localzip_le32(data+marker+12U);
    offsets[1]=xx_sfx_localzip_le32(data+marker+28U);
    offsets[2]=xx_sfx_localzip_le32(data+marker+36U);
    lengths[0]=xx_sfx_localzip_le32(data+marker+8U);
    lengths[1]=xx_sfx_localzip_le32(data+marker+24U);
    lengths[2]=xx_sfx_localzip_le32(data+marker+32U);
    if (offsets[0]<marker+40U || offsets[0]>n ||
        lengths[0]>n-offsets[0] ||
        offsets[1]!=offsets[0]+lengths[0] ||
        lengths[1]>n-offsets[1] ||
        offsets[2]!=offsets[1]+lengths[1] ||
        lengths[2]!=n-offsets[2]) return false;
    for (i=0U;i<3U;++i) {
        uint8_t name_len;
        xx_sfx_localzip_member member;
        size_t payload;
        if (!lengths[i] || offsets[i]>=n) return false;
        name_len=data[offsets[i]];
        if (!name_len || name_len>128U ||
            (uint32_t)name_len+1U>=lengths[i]) return false;
        payload=(size_t)offsets[i]+1U+(size_t)name_len;
        xx_mem_zero(&member,sizeof(member));
        member.name=xx_sfx_localzip_make_name(data+offsets[i]+1U,
                                               name_len,false);
        if (!member.name || !xx_sfx_localzip_path_safe(member.name)) {
            xx_str_free(member.name);
            return false;
        }
        member.header_offset=self->base_address+(int64_t)offsets[i];
        member.header_size=(int64_t)(1U+name_len);
        member.data_offset=self->base_address+(int64_t)payload;
        member.packed_size=(int64_t)(lengths[i]-1U-name_len);
        member.unpacked_size=(uint64_t)member.packed_size;
        member.method=0U;
        if (!xx_sfx_localzip_add(stream,&member)) {
            xx_str_free(member.name);
            return false;
        }
    }
    return true;
}

static xx_sfx_localzip_stream *xx_sfx_localzip_parse(Abstractformat *self,
                                                      xx_pd_struct *pd) {
    xx_sfx_localzip_stream *stream=NULL;
    uint8_t *data=NULL;
    int64_t total,span;
    size_t at,n,selected=0U;
    bool is_inftool,is_winimage,is_zerog,is_softpaq;
    if (!self || !self->device || self->base_address<0 ||
        (pd && xx_pd_is_stopped(pd))) return NULL;
    total=xx_io_total_size(self->device);
    if (total<self->base_address) return NULL;
    span=total-self->base_address;
    if (span<1000 || span>32*1024*1024 || (uint64_t)span>SIZE_MAX) return NULL;
    n=(size_t)span;
    data=(uint8_t *)xx_mem_alloc(n);
    if (!data || !xx_sfx_localzip_read_at(self,self->base_address,data,n) ||
        data[0]!='M' || data[1]!='Z') goto fail;
    is_inftool=xx_sfx_localzip_has(data,n<131072U?n:131072U,
                                  "Error reading archive.",22U);
    is_winimage=xx_sfx_localzip_has(data,n<131072U?n:131072U,
                                   "WinImage",8U);
    is_zerog=xx_sfx_localzip_has(data,n<131072U?n:131072U,
                                "InstallAnywhere",15U) ||
        /* Some UPX stubs omit the product banner.  These three distinctive
         * InstallAnywhere paths occur in their complete ZIP directory;
         * parse_central still proves every local header and member range. */
        (xx_sfx_localzip_has(data,n,"InstallerData/IAClasses.zip",
                               sizeof("InstallerData/IAClasses.zip")-1U) &&
         xx_sfx_localzip_has(data,n,"InstallerData/Execute.zip",
                               sizeof("InstallerData/Execute.zip")-1U) &&
         xx_sfx_localzip_has(data,n,
                            "Windows/resource/ZGWin32LaunchHelper.exe",
                            sizeof("Windows/resource/ZGWin32LaunchHelper.exe")-1U));
    is_softpaq=xx_sfx_localzip_has(data,n<131072U?n:131072U,
                                  "!3PS",4U);
    if (!is_inftool && !is_winimage && !is_zerog && !is_softpaq) goto fail;
    stream=(xx_sfx_localzip_stream *)xx_mem_calloc(1U,sizeof(*stream));
    if (!stream) goto fail;
    if (is_softpaq) {
        if (!xx_sfx_localzip_parse_softpaq(self,data,n,stream)) goto fail;
        stream->archive_size=span;
        xx_mem_free(data);
        return stream;
    }
    if (is_zerog) {
        if (!xx_sfx_localzip_parse_central(self,data,n,stream,pd)) goto fail;
        stream->archive_size=span;
        xx_mem_free(data);
        return stream;
    }
    for (at=0U;at+30U<=n;++at) {
        uint16_t version,flags,method,name_len,extra_len;
        uint32_t packed,plain,crc;
        size_t payload;
        xx_sfx_localzip_member member;
        uint8_t *decoded=NULL;
        size_t decoded_size=0U;
        if ((pd && xx_pd_is_stopped(pd))) goto fail;
        if (xx_rt_memcmp(data+at,"PK\x03\x04",4U)) continue;
        version=xx_sfx_localzip_le16(data+at+4U);
        flags=xx_sfx_localzip_le16(data+at+6U);
        method=xx_sfx_localzip_le16(data+at+8U);
        crc=xx_sfx_localzip_le32(data+at+14U);
        packed=xx_sfx_localzip_le32(data+at+18U);
        plain=xx_sfx_localzip_le32(data+at+22U);
        name_len=xx_sfx_localzip_le16(data+at+26U);
        extra_len=xx_sfx_localzip_le16(data+at+28U);
        if (version<10U || version>45U || flags&9U ||
            (method!=0U && method!=8U) || !name_len || name_len>4096U ||
            (size_t)name_len+(size_t)extra_len>n-at-30U) continue;
        payload=at+30U+(size_t)name_len+(size_t)extra_len;
        if (packed>n-payload || plain>256U*1024U*1024U) continue;
        xx_mem_zero(&member,sizeof(member));
        member.name=xx_sfx_localzip_make_name(data+at+30U,name_len,true);
        if (!member.name || !xx_sfx_localzip_path_safe(member.name)) {
            xx_str_free(member.name);goto fail;
        }
        member.header_offset=self->base_address+(int64_t)at;
        member.header_size=(int64_t)(payload-at);
        member.data_offset=self->base_address+(int64_t)payload;
        member.packed_size=(int64_t)packed;
        member.unpacked_size=(uint64_t)plain;
        member.method=method;
        member.crc32=crc;
        member.has_crc=true;
        if (!xx_sfx_localzip_decode(self,&member,&decoded,&decoded_size,pd)) {
            xx_str_free(member.name);continue;
        }
        xx_mem_free(decoded);
        if (!xx_sfx_localzip_add(stream,&member)) {
            xx_str_free(member.name);goto fail;
        }
        selected++;
        at=payload+(size_t)packed-1U;
    }
    if (!selected || (is_inftool && selected<2U)) goto fail;
    stream->archive_size=span;
    xx_mem_free(data);
    return stream;
fail:
    if (data) xx_mem_free(data);
    xx_sfx_localzip_stream_free(stream);
    return NULL;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_sfx_localzip_init(xx_sfx_localzip *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_LOCALZIP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sfx-localzip");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_localzip_check_is_valid;
    archive->format.handle_base_info = xx_sfx_localzip_handle_base_info;
    archive->format.get_format_size = xx_sfx_localzip_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_localzip_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_localzip_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_localzip_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_localzip_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_localzip_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_localzip_free_archive_records_reading;
    archive->format.destroy = xx_sfx_localzip_vtable_destroy;
}

xx_sfx_localzip *xx_sfx_localzip_create(xx_io_device *device, int64_t base_address) {
    xx_sfx_localzip *archive = (xx_sfx_localzip *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_sfx_localzip_init(archive, device, base_address);
    return archive;
}

void xx_sfx_localzip_destroy(xx_sfx_localzip *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_sfx_localzip_free(xx_sfx_localzip *archive) {
    if (!archive) return;
    xx_sfx_localzip_destroy(archive);
    xx_mem_free(archive);
}

static void xx_sfx_localzip_vtable_destroy(Abstractformat *self) {
    xx_sfx_localzip_destroy((xx_sfx_localzip *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_sfx_localzip_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_sfx_localzip_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_sfx_localzip_parse(self, pd);
    if (!stream) return false;
    xx_sfx_localzip_stream_free(stream);
    return true;
}

bool xx_sfx_localzip_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_sfx_localzip *archive = (xx_sfx_localzip *)self;
    xx_sfx_localzip_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_sfx_localzip_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_sfx_localzip_stream_free(stream);
    return true;
}

int64_t xx_sfx_localzip_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_sfx_localzip_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_sfx_localzip *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_sfx_localzip_set_record(xx_archive_record *record,
                                 const xx_sfx_localzip_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    if (member->has_crc &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                        member->crc32)) {
        return false;
    }
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_sfx_localzip_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!options) return true;
    if (!target) return false;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_sfx_localzip_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_sfx_localzip_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_sfx_localzip_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_sfx_localzip_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_sfx_localzip_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_sfx_localzip_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_sfx_localzip_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_sfx_localzip_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_sfx_localzip_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sfx_localzip_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_sfx_localzip_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_sfx_localzip_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_sfx_localzip_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_sfx_localzip_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_sfx_localzip_stream *stream;
    const xx_sfx_localzip_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_sfx_localzip_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_sfx_localzip_path_safe(member->name)) return false;

    path_option =
        xx_sfx_localzip_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_sfx_localzip_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_sfx_localzip_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_sfx_localzip_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
