/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ar/xx_ar.h"
#include "xx_ar_defs.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct xx_ar_member_s {
    int64_t header_offset;
    int64_t stored_data_offset;
    int64_t data_offset;
    int64_t stored_size;
    int64_t data_size;
    uint64_t name_prefix_size;
    uint64_t timestamp;
    uint64_t owner_id;
    uint64_t group_id;
    uint64_t mode;
    char *name;
    bool special;
} xx_ar_member;

typedef struct xx_ar_members_s {
    xx_ar_member *items;
    size_t count;
    uint64_t visible_count;
    int64_t archive_end;
} xx_ar_members;

typedef struct xx_ar_archive_stream_s {
    xx_ar_members members;
    size_t member_index;
} xx_ar_archive_stream;

typedef struct xx_ar_ds_stream_s {
    xx_data_struct *items;
    size_t count;
} xx_ar_ds_stream;

typedef struct xx_ar_record_stream_s {
    const xx_data_struct_field_desc *fields;
    size_t count;
} xx_ar_record_stream;

static void xx_ar_vtable_destroy(Abstractformat *self);

static bool xx_ar_read_exact_at(xx_io_device *device, int64_t offset,
                                void *buffer, size_t size) {
    size_t done = 0;
    uint8_t *bytes = (uint8_t *)buffer;
    if (!device || (!buffer && size != 0U) || offset < 0 || offset > LONG_MAX) {
        return false;
    }
    if (xx_io_seek(device, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, bytes + done, size - done);
        if (got <= 0) {
            return false;
        }
        done += (size_t)got;
    }
    return true;
}

static bool xx_ar_parse_uint(const char *field, size_t size, unsigned base,
                             bool allow_blank, uint64_t *value) {
    size_t begin = 0;
    size_t end = size;
    uint64_t result = 0;
    bool have_digit = false;

    if (!field || !value || (base != 8U && base != 10U)) {
        return false;
    }
    while (begin < end && field[begin] == ' ') {
        ++begin;
    }
    while (end > begin && (field[end - 1U] == ' ' || field[end - 1U] == '\0')) {
        --end;
    }
    if (begin == end) {
        *value = 0;
        return allow_blank;
    }
    for (size_t i = begin; i < end; ++i) {
        unsigned digit;
        if (field[i] < '0' || field[i] > '9') {
            return false;
        }
        digit = (unsigned)(field[i] - '0');
        if (digit >= base || result > (UINT64_MAX - digit) / base) {
            return false;
        }
        result = result * base + digit;
        have_digit = true;
    }
    if (!have_digit) {
        return false;
    }
    *value = result;
    return true;
}

static void xx_ar_copy_raw_name(const xx_ar_member_header *header,
                                char output[17]) {
    size_t length = 16U;
    xx_mem_copy(output, header->name, 16U);
    while (length > 0U && (output[length - 1U] == ' ' ||
                           output[length - 1U] == '\0')) {
        --length;
    }
    output[length] = '\0';
}

static bool xx_ar_read_header(xx_io_device *device, int64_t total_size,
                              int64_t offset, xx_ar_member_header *header,
                              uint64_t *member_size, int64_t *next_offset) {
    uint64_t size_value;
    int64_t data_offset;
    int64_t data_end;

    if (!device || !header || !member_size || !next_offset || offset < 0 ||
        total_size < offset || total_size - offset < XX_AR_MEMBER_HEADER_SIZE ||
        !xx_ar_read_exact_at(device, offset, header, sizeof(*header)) ||
        header->trailer[0] != '`' || header->trailer[1] != '\n' ||
        !xx_ar_parse_uint(header->size, sizeof(header->size), 10U, false,
                          &size_value) || size_value > (uint64_t)INT64_MAX) {
        return false;
    }
    data_offset = offset + XX_AR_MEMBER_HEADER_SIZE;
    if (size_value > (uint64_t)(INT64_MAX - data_offset)) {
        return false;
    }
    data_end = data_offset + (int64_t)size_value;
    if (data_end > total_size) {
        return false;
    }
    if ((size_value & 1U) != 0U) {
        /* An odd-sized member is followed by one filler byte so that the next
         * header starts on an even offset. GNU ar writes '\n', but OS/2 emx,
         * Apple and several DOS librarians write '\0' or leave the byte
         * uninitialised, so the value must not be validated. A writer that
         * omits the filler entirely for the LAST member is also accepted:
         * there is no following header that could be misaligned by it. */
        if (data_end < total_size) {
            ++data_end;
        }
    }
    *member_size = size_value;
    *next_offset = data_end;
    return true;
}

static bool xx_ar_name_is_special(const char *name) {
    return name &&
           (xx_str_cmp(name, "/") == 0 || xx_str_cmp(name, "//") == 0 ||
            xx_str_cmp(name, "/SYM64/") == 0 ||
            xx_str_cmp(name, "__.SYMDEF") == 0 ||
            xx_str_cmp(name, "__.SYMDEF SORTED") == 0 ||
            xx_str_cmp(name, "__.SYMDEF_64") == 0 ||
            xx_str_cmp(name, "__.SYMDEF_64 SORTED") == 0 ||
            xx_str_cmp(name, "__.GOSYMDEF") == 0);
}

static char *xx_ar_read_name_bytes(xx_io_device *device, int64_t offset,
                                   uint64_t size) {
    char *name;
    size_t length;
    if (size > XX_AR_MAX_NAME_SIZE || size > (uint64_t)(SIZE_MAX - 1U)) {
        return NULL;
    }
    length = (size_t)size;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) {
        return NULL;
    }
    if (length != 0U && !xx_ar_read_exact_at(device, offset, name, length)) {
        xx_mem_free(name);
        return NULL;
    }
    while (length > 0U && name[length - 1U] == '\0') {
        --length;
    }
    name[length] = '\0';
    return name;
}

static char *xx_ar_resolve_gnu_name(xx_io_device *device,
                                    int64_t table_offset,
                                    uint64_t table_size,
                                    uint64_t name_offset) {
    uint64_t remaining;
    size_t read_size;
    char *buffer;
    size_t length = 0U;
    bool terminated = false;

    if (!device || table_offset < 0 || name_offset >= table_size) {
        return NULL;
    }
    remaining = table_size - name_offset;
    read_size = remaining > XX_AR_MAX_NAME_SIZE
                    ? XX_AR_MAX_NAME_SIZE
                    : (size_t)remaining;
    buffer = (char *)xx_mem_alloc(read_size + 1U);
    if (!buffer) {
        return NULL;
    }
    if (!xx_ar_read_exact_at(device, table_offset + (int64_t)name_offset,
                             buffer, read_size)) {
        xx_mem_free(buffer);
        return NULL;
    }
    while (length < read_size) {
        if (buffer[length] == '\0' || buffer[length] == '\n') {
            terminated = true;
            break;
        }
        if (buffer[length] == '/' && length + 1U < read_size &&
            buffer[length + 1U] == '\n') {
            terminated = true;
            break;
        }
        ++length;
    }
    if (!terminated && remaining > (uint64_t)read_size) {
        xx_mem_free(buffer);
        return NULL;
    }
    buffer[length] = '\0';
    return buffer;
}

static void xx_ar_members_cleanup(xx_ar_members *members) {
    if (!members) {
        return;
    }
    if (members->items) {
        for (size_t i = 0; i < members->count; ++i) {
            if (members->items[i].name) {
                xx_str_free(members->items[i].name);
            }
        }
        xx_mem_free(members->items);
    }
    xx_mem_zero(members, sizeof(*members));
}

static bool xx_ar_parse_archive(Abstractformat *self, xx_ar_members *members,
                                xx_pd_struct *pd) {
    uint8_t magic[XX_AR_MAGIC_SIZE];
    int64_t total_size;
    int64_t offset;
    int64_t gnu_table_offset = -1;
    uint64_t gnu_table_size = 0;
    size_t member_count = 0U;

    if (!self || !self->device || !members || self->base_address < 0) {
        return false;
    }
    xx_mem_zero(members, sizeof(*members));
    total_size = xx_io_total_size(self->device);
    if (total_size < 0 || self->base_address > total_size ||
        total_size - self->base_address < XX_AR_MAGIC_SIZE ||
        !xx_ar_read_exact_at(self->device, self->base_address, magic,
                             sizeof(magic))) {
        ((xx_ar *)self)->is_thin = false;
        return false;
    }
    if (xx_rt_memcmp(magic, XX_AR_THIN_MAGIC, XX_AR_MAGIC_SIZE) == 0) {
        ((xx_ar *)self)->is_thin = true;
        return false;
    }
    ((xx_ar *)self)->is_thin = false;
    if (xx_rt_memcmp(magic, XX_AR_MAGIC, XX_AR_MAGIC_SIZE) != 0) {
        return false;
    }

    offset = self->base_address + XX_AR_MAGIC_SIZE;
    while (offset < total_size) {
        xx_ar_member_header header;
        uint64_t member_size;
        int64_t next_offset;
        char raw_name[17];
        if (pd && xx_pd_is_stopped(pd)) {
            return false;
        }
        /* The chain ends where it stops making sense.  Archives that were
         * moved through a shell archive or a text-mode transfer routinely
         * lose their last few bytes, or grow a block of unrelated text after
         * the members; the members that DID survive are still readable, and
         * refusing the whole file for the sake of its tail loses them.  The
         * archive is still gated on "!<arch>\n" plus a first member header
         * that parses, so a break here can never turn unrelated data into an
         * archive - it only stops a good chain early. */
        if (!xx_ar_read_header(self->device, total_size, offset, &header,
                               &member_size, &next_offset)) {
            if (member_count == 0U) return false;
            break;
        }
        xx_ar_copy_raw_name(&header, raw_name);
        if (xx_str_cmp(raw_name, "//") == 0) {
            gnu_table_offset = offset + XX_AR_MEMBER_HEADER_SIZE;
            gnu_table_size = member_size;
        }
        if (member_count == SIZE_MAX) {
            return false;
        }
        ++member_count;
        offset = next_offset;
    }
    /* No member count check here. A first header that fails to parse is
     * already refused inside the loop, so zero members can only mean the
     * file ends right after "!<arch>\n" -- an empty archive, which is
     * valid and simply has nothing to list. */

    if (member_count > SIZE_MAX / sizeof(*members->items)) {
        return false;
    }
    if (member_count != 0U) {
        members->items = (xx_ar_member *)xx_mem_alloc(
            member_count * sizeof(xx_ar_member));
        if (!members->items) {
            return false;
        }
        xx_mem_zero(members->items, member_count * sizeof(xx_ar_member));
    }
    members->count = member_count;
    /* The archive ends at the last boundary the chain reached, which is EOF
     * for an intact file and the start of the damaged tail otherwise. */
    members->archive_end = offset;

    offset = self->base_address + XX_AR_MAGIC_SIZE;
    for (size_t i = 0; i < member_count; ++i) {
        xx_ar_member_header header;
        xx_ar_member *member = &members->items[i];
        uint64_t member_size;
        int64_t next_offset;
        char raw_name[17];
        char *name = NULL;

        if (!xx_ar_read_header(self->device, total_size, offset, &header,
                               &member_size, &next_offset)) {
            xx_ar_members_cleanup(members);
            return false;
        }
        /* The timestamp/uid/gid/mode fields are informational only: the member
         * chain is defined by name/size/trailer alone. Real archives abuse
         * them freely - MS import libraries pack several values into the
         * timestamp field ("787117117 0 "), Atari/DRI .A libraries store a raw
         * binary 4-byte time inside the mode field. Treat an unparsable field
         * as "unknown" (0) instead of discarding the whole archive. */
        if (!xx_ar_parse_uint(header.timestamp, sizeof(header.timestamp), 10U,
                              true, &member->timestamp)) {
            member->timestamp = 0U;
        }
        if (!xx_ar_parse_uint(header.owner_id, sizeof(header.owner_id), 10U,
                              true, &member->owner_id)) {
            member->owner_id = 0U;
        }
        if (!xx_ar_parse_uint(header.group_id, sizeof(header.group_id), 10U,
                              true, &member->group_id)) {
            member->group_id = 0U;
        }
        if (!xx_ar_parse_uint(header.mode, sizeof(header.mode), 8U, true,
                              &member->mode)) {
            member->mode = 0U;
        }

        member->header_offset = offset;
        member->stored_data_offset = offset + XX_AR_MEMBER_HEADER_SIZE;
        member->data_offset = member->stored_data_offset;
        member->stored_size = (int64_t)member_size;
        member->data_size = (int64_t)member_size;
        xx_ar_copy_raw_name(&header, raw_name);

        if (xx_rt_strncmp(raw_name, "#1/", 3U) == 0) {
            uint64_t name_size;
            size_t raw_length = xx_str_len(raw_name);
            if (raw_length <= 3U ||
                !xx_ar_parse_uint(raw_name + 3U, raw_length - 3U, 10U, false,
                                  &name_size) ||
                name_size > member_size || name_size > XX_AR_MAX_NAME_SIZE) {
                xx_ar_members_cleanup(members);
                return false;
            }
            name = xx_ar_read_name_bytes(self->device,
                                         member->stored_data_offset, name_size);
            if (!name) {
                xx_ar_members_cleanup(members);
                return false;
            }
            member->name_prefix_size = name_size;
            member->data_offset += (int64_t)name_size;
            member->data_size -= (int64_t)name_size;
        } else if (raw_name[0] == '/' && raw_name[1] >= '0' &&
                   raw_name[1] <= '9') {
            uint64_t name_offset;
            size_t raw_length = xx_str_len(raw_name);
            if (!xx_ar_parse_uint(raw_name + 1U, raw_length - 1U, 10U, false,
                                  &name_offset)) {
                xx_ar_members_cleanup(members);
                return false;
            }
            name = xx_ar_resolve_gnu_name(self->device, gnu_table_offset,
                                          gnu_table_size, name_offset);
            if (!name) {
                xx_ar_members_cleanup(members);
                return false;
            }
        } else {
            size_t name_length = xx_str_len(raw_name);
            if (name_length > 1U && raw_name[name_length - 1U] == '/' &&
                xx_str_cmp(raw_name, "/SYM64/") != 0) {
                raw_name[--name_length] = '\0';
            }
            name = xx_str_dup(raw_name);
            if (!name) {
                xx_ar_members_cleanup(members);
                return false;
            }
        }

        member->name = name;
        member->special = xx_ar_name_is_special(name);
        if (!member->special) {
            ++members->visible_count;
        }
        offset = next_offset;
    }
    return true;
}

static bool xx_ar_copy_options(xx_list_s *destination,
                               const xx_list_s *source) {
    if (!destination || !source) {
        return source == NULL;
    }
    for (size_t i = 0; i < source->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
        xx_meta copy;
        if (!item) {
            continue;
        }
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_ar_find_option(const xx_list_s *options,
                                       uint32_t meta_id) {
    if (!options) {
        return NULL;
    }
    for (size_t i = 0; i < options->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (item && item->meta_id == meta_id) {
            return &item->var;
        }
    }
    return NULL;
}

static bool xx_ar_populate_archive_record(xx_archive_record *record,
                                          const xx_ar_member *member) {
    if (!record || !member || member->special) {
        return false;
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = XX_AR_MEMBER_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          member->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->mode) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static void xx_ar_archive_stream_free(void *pointer) {
    xx_ar_archive_stream *stream = (xx_ar_archive_stream *)pointer;
    if (stream) {
        xx_ar_members_cleanup(&stream->members);
        xx_mem_free(stream);
    }
}

static bool xx_ar_safe_member_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') {
        return false;
    }
    if (((name[0] >= 'A' && name[0] <= 'Z') ||
         (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':') {
        return false;
    }
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            /* A "." component is harmless and is dropped by
             * xx_ar_make_extract_name; "..", an empty component and an
             * absolute path are not. */
            if (length == 0U ||
                (length == 2U && component[0] == '.' && component[1] == '.')) {
                return false;
            }
            if (ch == 0U) {
                return true;
            }
            component = cursor + 1;
        }
    }
}

/* Build the relative path a member is extracted to. The stored name is kept
 * verbatim in the record meta, but MSVC librarians write names such as
 * ".\\Release\\foo.obj", so the extraction path drops the redundant "."
 * components and normalises separators. A name that escapes the destination
 * (absolute, drive-qualified, "..", empty component, control characters)
 * still yields NULL and the member is refused. */
static char *xx_ar_make_extract_name(const char *name) {
    char *result;
    size_t out = 0U;
    const char *component;
    const char *cursor;

    if (!xx_ar_safe_member_name(name)) {
        return NULL;
    }
    result = xx_str_create_len(xx_str_len(name));
    if (!result) {
        return NULL;
    }
    component = name;
    for (cursor = name;; ++cursor) {
        char ch = *cursor;
        if (ch == '/' || ch == '\\' || ch == '\0') {
            size_t length = (size_t)(cursor - component);
            if (!(length == 1U && component[0] == '.')) {
                if (out != 0U) {
                    result[out++] = '/';
                }
                for (size_t i = 0; i < length; ++i) {
                    result[out++] = component[i];
                }
            }
            if (ch == '\0') {
                break;
            }
            component = cursor + 1;
        }
    }
    result[out] = '\0';
    if (out == 0U) {
        xx_str_free(result);
        return NULL;
    }
    return result;
}

void xx_ar_init(xx_ar *ar, xx_io_device *dev, int64_t base_address) {
    if (!ar) {
        return;
    }
    xx_mem_zero(ar, sizeof(*ar));
    xx_format_init(&ar->format, dev, base_address);
    ar->format.endian = XX_ENDIAN_UNKNOWN;
    ar->format.file_type = XX_FILE_TYPE_AR;
    ar->format.format_type = XX_TYPE_ARCHIVE;
    ar->format.is_archive = true;
    xx_format_set_mime_type(&ar->format, "application/x-archive");
    xx_format_set_extension(&ar->format, "a");

    ar->format.check_is_valid = xx_ar_check_is_valid;
    ar->format.handle_base_info = xx_ar_handle_base_info;
    ar->format.get_format_size = xx_ar_get_format_size;
    ar->format.get_number_of_archive_records =
        xx_ar_get_number_of_archive_records;
    ar->format.create_archive_records_reading =
        xx_ar_create_archive_records_reading;
    ar->format.get_current_archive_record = xx_ar_get_current_archive_record;
    ar->format.unpack_current_archive_record =
        xx_ar_unpack_current_archive_record;
    ar->format.archive_record_move_to_next =
        xx_ar_archive_record_move_to_next;
    ar->format.free_archive_records_reading =
        xx_ar_free_archive_records_reading;
    ar->format.data_struct_id_to_string = xx_ar_data_struct_id_to_string;
    ar->format.data_struct_string_to_id = xx_ar_data_struct_string_to_id;
    ar->format.create_data_structs_reading = xx_ar_create_data_structs_reading;
    ar->format.get_current_data_struct = xx_ar_get_current_data_struct;
    ar->format.data_struct_move_to_next = xx_ar_data_struct_move_to_next;
    ar->format.free_data_structs_reading = xx_ar_free_data_structs_reading;
    ar->format.create_data_struct_records_reading =
        xx_ar_create_data_struct_records_reading;
    ar->format.get_current_data_struct_record =
        xx_ar_get_current_data_struct_record;
    ar->format.data_struct_record_move_to_next =
        xx_ar_data_struct_record_move_to_next;
    ar->format.free_data_struct_records_reading =
        xx_ar_free_data_struct_records_reading;
    ar->format.destroy = xx_ar_vtable_destroy;
    ar->archive_end = -1;
}

xx_ar *xx_ar_create(xx_io_device *dev, int64_t base_address) {
    xx_ar *ar = (xx_ar *)xx_mem_alloc(sizeof(*ar));
    if (ar) {
        xx_ar_init(ar, dev, base_address);
    }
    return ar;
}

void xx_ar_destroy(xx_ar *ar) {
    if (ar && ar->format.close) {
        ar->format.close(&ar->format);
    }
    if (ar) xx_format_cleanup_extra_parameters(&ar->format);
}

static void xx_ar_vtable_destroy(Abstractformat *self) {
    xx_ar_destroy((xx_ar *)self);
}

void xx_ar_free(xx_ar *ar) {
    if (ar) {
        xx_ar_destroy(ar);
        xx_mem_free(ar);
    }
}

bool xx_ar_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    uint8_t magic[XX_AR_MAGIC_SIZE];
    int64_t total_size;
    (void)pd;
    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    ((xx_ar *)self)->is_thin = false;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < XX_AR_MAGIC_SIZE ||
        !xx_ar_read_exact_at(self->device, self->base_address, magic,
                             sizeof(magic))) {
        return false;
    }
    if (xx_rt_memcmp(magic, XX_AR_THIN_MAGIC, XX_AR_MAGIC_SIZE) == 0) {
        ((xx_ar *)self)->is_thin = true;
        return false;
    }
    return xx_rt_memcmp(magic, XX_AR_MAGIC, XX_AR_MAGIC_SIZE) == 0;
}

bool xx_ar_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ar_members members;
    xx_ar *ar;
    if (!self || !self->device || !xx_ar_parse_archive(self, &members, pd)) {
        if (self) {
            self->is_valid = false;
        }
        return false;
    }
    ar = (xx_ar *)self;
    ar->number_of_members = (uint64_t)members.count;
    ar->number_of_records = members.visible_count;
    ar->archive_end = members.archive_end;
    ar->is_thin = false;
    self->format_size = members.archive_end - self->base_address;
    {
        /* Whatever follows the last member the chain reached is not part of
         * the archive: a damaged tail or an appended block, reported as
         * overlay rather than silently folded into the format size. */
        int64_t total = xx_io_total_size(self->device);
        bool has_tail = total > members.archive_end;
        self->overlay_offset = has_tail ? members.archive_end : -1;
        self->overlay_size = has_tail ? total - members.archive_end : 0;
    }
    self->number_of_archive_records = members.visible_count;
    self->file_type = XX_FILE_TYPE_AR;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_valid = true;
    self->base_info_handled = true;
    xx_ar_members_cleanup(&members);
    return true;
}

int64_t xx_ar_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_ar_get_number_of_archive_records(Abstractformat *self,
                                             xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return ((xx_ar *)self)->number_of_records;
}

xx_archive_record_state *xx_ar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_ar_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ar_archive_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    xx_mem_zero(stream, sizeof(*stream));
    if (!xx_ar_copy_options(&state->options, options) ||
        !xx_ar_parse_archive(self, &stream->members, pd)) {
        xx_ar_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->member_index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_ar_archive_stream_free;
    state->total_records = (int64_t)stream->members.visible_count;
    while (stream->member_index < stream->members.count &&
           stream->members.items[stream->member_index].special) {
        ++stream->member_index;
    }
    if (stream->member_index < stream->members.count &&
        xx_ar_populate_archive_record(
            &state->current_record,
            &stream->members.items[stream->member_index])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_ar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ar_archive_record_move_to_next(Abstractformat *self,
                                       xx_archive_record_state *state,
                                       xx_pd_struct *pd) {
    xx_ar_archive_stream *stream;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ar_archive_stream *)state->internal_state;
    ++stream->member_index;
    while (stream->member_index < stream->members.count &&
           stream->members.items[stream->member_index].special) {
        ++stream->member_index;
    }
    if (stream->member_index >= stream->members.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_ar_populate_archive_record(
            &state->current_record,
            &stream->members.items[stream->member_index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    ++state->current_index;
    return true;
}

bool xx_ar_unpack_current_archive_record(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *path_value;
    char *name;
    const char *base_utf8 = NULL;
    char *owned_base = NULL;
    char *destination;
    bool result;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    record = &state->current_record;
    name = xx_ar_make_extract_name(xx_archive_record_get_original_name(record));
    if (!name) {
        return false;
    }
    path_value = xx_ar_find_option(&state->options,
                                   XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        int64_t total = xx_io_total_size(self->device);
        xx_str_free(name);
        return record->data_offset >= 0 && record->compressed_size >= 0 &&
               record->data_offset <= total &&
               record->compressed_size <= total - record->data_offset;
    }
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_utf8 = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_utf8 = owned_base;
    }
    if (!base_utf8) {
        if (owned_base) xx_str_free(owned_base);
        xx_str_free(name);
        return false;
    }
    if (base_utf8[0] != '\0' &&
        base_utf8[xx_str_len(base_utf8) - 1U] != '/' &&
        base_utf8[xx_str_len(base_utf8) - 1U] != '\\') {
        destination = xx_str_concat3(base_utf8, "/", name);
    } else {
        destination = xx_str_concat(base_utf8, name);
    }
    if (owned_base) {
        xx_str_free(owned_base);
    }
    xx_str_free(name);
    if (!destination) {
        return false;
    }
    if (!xx_store_create_dirs_a(destination, false)) {
        xx_str_free(destination);
        return false;
    }
    result = xx_store_unpack_device_to_file(
        self->device, record->data_offset, record->compressed_size,
        destination, pd);
    if (!result) {
        xx_rt_remove(destination);
    }
    xx_str_free(destination);
    return result;
}

void xx_ar_free_archive_records_reading(Abstractformat *self,
                                        xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

typedef struct xx_ar_ds_name_s {
    xx_ar_data_struct_id_t id;
    const char *name;
} xx_ar_ds_name;

static const xx_ar_ds_name xx_ar_ds_names[] = {
    {XX_AR_DS_UNKNOWN, "UNKNOWN"},
    {XX_AR_DS_GLOBAL_HEADER, "GLOBAL_HEADER"},
    {XX_AR_DS_MEMBER_HEADER, "MEMBER_HEADER"},
    {XX_AR_DS_BSD_EXTENDED_NAME, "BSD_EXTENDED_NAME"},
    {XX_AR_DS_DATA, "DATA"},
    {XX_AR_DS_PADDING, "PADDING"}
};

const char *xx_ar_data_struct_id_to_string(Abstractformat *self, uint32_t id) {
    (void)self;
    for (size_t i = 0; i < sizeof(xx_ar_ds_names) / sizeof(xx_ar_ds_names[0]);
         ++i) {
        if ((uint32_t)xx_ar_ds_names[i].id == id) {
            return xx_ar_ds_names[i].name;
        }
    }
    return "UNKNOWN";
}

uint32_t xx_ar_data_struct_string_to_id(Abstractformat *self,
                                        const char *name) {
    (void)self;
    if (name) {
        for (size_t i = 0;
             i < sizeof(xx_ar_ds_names) / sizeof(xx_ar_ds_names[0]); ++i) {
            if (xx_str_cmp(name, xx_ar_ds_names[i].name) == 0) {
                return (uint32_t)xx_ar_ds_names[i].id;
            }
        }
    }
    return (uint32_t)XX_AR_DS_UNKNOWN;
}

static void xx_ar_ds_stream_free(void *pointer) {
    xx_ar_ds_stream *stream = (xx_ar_ds_stream *)pointer;
    if (stream) {
        if (stream->items) xx_mem_free(stream->items);
        xx_mem_free(stream);
    }
}

static void xx_ar_set_ds(xx_data_struct *item, uint32_t id, int64_t offset,
                         int64_t size, xx_data_struct_type_t type,
                         bool is_mapped) {
    item->id = id;
    item->offset = offset;
    item->address = is_mapped ? offset : -1;
    item->entry_size = size;
    item->total_size = size;
    item->count = 1U;
    item->type = type;
}

xx_data_struct_state *xx_ar_create_data_structs_reading(Abstractformat *self,
                                                        xx_pd_struct *pd) {
    xx_data_struct_state *state;
    xx_ar_ds_stream *stream;
    xx_ar_members members;
    size_t capacity;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !xx_ar_parse_archive(self, &members, pd)) {
        return NULL;
    }
    if (members.count > (SIZE_MAX - 1U) / 4U) {
        xx_ar_members_cleanup(&members);
        return NULL;
    }
    capacity = 1U + members.count * 4U;
    if (capacity > SIZE_MAX / sizeof(*stream->items)) {
        xx_ar_members_cleanup(&members);
        return NULL;
    }
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ar_ds_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        xx_ar_members_cleanup(&members);
        return NULL;
    }
    xx_data_struct_state_init(state, self);
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_data_struct *)xx_mem_alloc(
        capacity * sizeof(xx_data_struct));
    if (!stream->items) {
        xx_mem_free(stream);
        xx_data_struct_state_free(state);
        xx_ar_members_cleanup(&members);
        return NULL;
    }
    xx_ar_set_ds(&stream->items[stream->count++], XX_AR_DS_GLOBAL_HEADER,
                 self->base_address, XX_AR_MAGIC_SIZE,
                 XX_DATA_STRUCT_TYPE_STRUCT, self->is_mapped);
    for (size_t i = 0; i < members.count; ++i) {
        const xx_ar_member *member = &members.items[i];
        xx_ar_set_ds(&stream->items[stream->count++], XX_AR_DS_MEMBER_HEADER,
                     member->header_offset, XX_AR_MEMBER_HEADER_SIZE,
                     XX_DATA_STRUCT_TYPE_STRUCT, self->is_mapped);
        if (member->name_prefix_size != 0U) {
            xx_ar_set_ds(&stream->items[stream->count++],
                         XX_AR_DS_BSD_EXTENDED_NAME,
                         member->stored_data_offset,
                         (int64_t)member->name_prefix_size,
                         XX_DATA_STRUCT_TYPE_RAW_DATA, self->is_mapped);
        }
        if (member->data_size != 0) {
            xx_ar_set_ds(&stream->items[stream->count++], XX_AR_DS_DATA,
                         member->data_offset, member->data_size,
                         XX_DATA_STRUCT_TYPE_RAW_DATA, self->is_mapped);
        }
        if ((member->stored_size & 1) != 0) {
            xx_ar_set_ds(&stream->items[stream->count++], XX_AR_DS_PADDING,
                         member->stored_data_offset + member->stored_size, 1,
                         XX_DATA_STRUCT_TYPE_RAW_DATA, self->is_mapped);
        }
    }
    xx_ar_members_cleanup(&members);
    state->internal_state = stream;
    state->free_internal = xx_ar_ds_stream_free;
    state->total_structs = (int64_t)stream->count;
    state->current_index = 0;
    state->has_struct = stream->count != 0U;
    if (state->has_struct) {
        state->current_struct = stream->items[0];
    }
    return state;
}

const xx_data_struct *xx_ar_get_current_data_struct(Abstractformat *self,
                                                    xx_data_struct_state *state) {
    return self && state && state->format == self && state->has_struct
               ? &state->current_struct
               : NULL;
}

bool xx_ar_data_struct_move_to_next(Abstractformat *self,
                                    xx_data_struct_state *state,
                                    xx_pd_struct *pd) {
    xx_ar_ds_stream *stream;
    int64_t next;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_struct || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ar_ds_stream *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= stream->count) {
        state->has_struct = false;
        return false;
    }
    state->current_index = next;
    state->current_struct = stream->items[next];
    state->has_struct = true;
    return true;
}

void xx_ar_free_data_structs_reading(Abstractformat *self,
                                     xx_data_struct_state *state) {
    (void)self;
    xx_data_struct_state_free(state);
}

static const xx_data_struct_field_desc xx_ar_global_fields[] = {
    {L"magic", L"char[8]", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID}
};

static const xx_data_struct_field_desc xx_ar_member_fields[] = {
    {L"name", L"char[16]", 0, 16, XX_DATA_STRUCT_RECORD_PROPERTY_STRING},
    {L"timestamp", L"char[12]", 16, 12,
     XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
    {L"owner_id", L"char[6]", 28, 6, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"group_id", L"char[6]", 34, 6, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"mode", L"char[8]", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"size", L"char[10]", 48, 10, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"trailer", L"char[2]", 58, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID}
};

static void xx_ar_record_stream_free(void *pointer) {
    if (pointer) xx_mem_free(pointer);
}

static bool xx_ar_populate_field(xx_io_device *device,
                                 const xx_data_struct *parent,
                                 const xx_data_struct_field_desc *field,
                                 xx_data_struct_record *record) {
    char *text;
    wchar_t *display;
    size_t length;
    int64_t total_size;
    if (!device || !parent || !field || !record || field->size < 0 ||
        (uint64_t)field->size > (uint64_t)(SIZE_MAX - 1U)) {
        return false;
    }
    total_size = xx_io_total_size(device);
    if (parent->offset < 0 || field->rel_offset < 0 ||
        parent->offset > INT64_MAX - field->rel_offset ||
        parent->offset + field->rel_offset > total_size ||
        field->size > total_size - (parent->offset + field->rel_offset)) {
        return false;
    }
    length = (size_t)field->size;
    text = (char *)xx_mem_alloc(length + 1U);
    if (!text || !xx_ar_read_exact_at(device,
                                      parent->offset + field->rel_offset,
                                      text, length)) {
        if (text) xx_mem_free(text);
        return false;
    }
    while (length > 0U && (text[length - 1U] == ' ' ||
                           text[length - 1U] == '\0')) {
        --length;
    }
    text[length] = '\0';
    display = xx_str_utf8_to_unicode(text);
    if (!display) {
        xx_mem_free(text);
        return false;
    }
    xx_data_struct_record_init(record);
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    if (!xx_var_set_str(&record->value, text) ||
        !xx_data_struct_record_set_name(record, field->name) ||
        !xx_data_struct_record_set_type(record, field->type) ||
        !xx_data_struct_record_set_display_value(record, display)) {
        xx_str_wfree(display);
        xx_mem_free(text);
        xx_data_struct_record_cleanup(record);
        return false;
    }
    xx_str_wfree(display);
    xx_mem_free(text);
    return true;
}

xx_data_struct_record_state *xx_ar_create_data_struct_records_reading(
    Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd) {
    xx_data_struct_record_state *state;
    xx_ar_record_stream *stream;
    const xx_data_struct_field_desc *fields;
    size_t count;
    (void)pd;
    if (!self || !self->device || !ds) {
        return NULL;
    }
    if (ds->id == (uint32_t)XX_AR_DS_GLOBAL_HEADER) {
        fields = xx_ar_global_fields;
        count = sizeof(xx_ar_global_fields) / sizeof(xx_ar_global_fields[0]);
    } else if (ds->id == (uint32_t)XX_AR_DS_MEMBER_HEADER) {
        fields = xx_ar_member_fields;
        count = sizeof(xx_ar_member_fields) / sizeof(xx_ar_member_fields[0]);
    } else {
        return NULL;
    }
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ar_record_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_data_struct_record_state_init(state, self, ds);
    stream->fields = fields;
    stream->count = count;
    state->internal_state = stream;
    state->free_internal = xx_ar_record_stream_free;
    state->total_records = (int64_t)count;
    if (count != 0U && xx_ar_populate_field(self->device, ds, &fields[0],
                                            &state->current_record)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_data_struct_record *xx_ar_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ar_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    xx_ar_record_stream *stream;
    int64_t next;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ar_record_stream *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= stream->count) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_ar_populate_field(self->device, &state->parent_struct,
                              &stream->fields[next],
                              &state->current_record)) {
        state->has_record = false;
        return false;
    }
    state->current_index = next;
    state->has_record = true;
    return true;
}

void xx_ar_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state) {
    (void)self;
    xx_data_struct_record_state_free(state);
}

uint64_t xx_ar_get_number_of_records(const xx_ar *ar) {
    return ar ? ar->number_of_records : 0U;
}

uint64_t xx_ar_get_number_of_members(const xx_ar *ar) {
    return ar ? ar->number_of_members : 0U;
}

int64_t xx_ar_get_archive_end(const xx_ar *ar) {
    return ar ? ar->archive_end : -1;
}

bool xx_ar_is_thin(const xx_ar *ar) {
    return ar ? ar->is_thin : false;
}
