/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <xxfclib/formats/other/windows_shell_link/xx_lnk_yx.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const uint8_t *data;
  size_t size;
  size_t pos;
  xx_lnk_yx_result *result;
  bool oom;
} lnk_parser;

static uint16_t read16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read64(const uint8_t *p) {
  return (uint64_t)read32(p) | ((uint64_t)read32(p + 4) << 32);
}

static void set_uint(xx_lnk_yx_uint *v, uint64_t x) {
  v->defined = true;
  v->value = x;
}

static void append_codepoint(char *out, size_t *used, uint32_t cp) {
  if (cp <= 0x7f) {
    out[(*used)++] = (char)cp;
  } else if (cp <= 0x7ff) {
    out[(*used)++] = (char)(0xc0 | (cp >> 6));
    out[(*used)++] = (char)(0x80 | (cp & 0x3f));
  } else if (cp <= 0xffff) {
    out[(*used)++] = (char)(0xe0 | (cp >> 12));
    out[(*used)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[(*used)++] = (char)(0x80 | (cp & 0x3f));
  } else {
    out[(*used)++] = (char)(0xf0 | (cp >> 18));
    out[(*used)++] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[(*used)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[(*used)++] = (char)(0x80 | (cp & 0x3f));
  }
}

/* The module's strings use Rust's lossy UTF-8 and UTF-16 conversion. */
static bool set_string(xx_lnk_yx_string *out, const uint8_t *input,
                       size_t length, bool utf16, bool stop_at_nul) {
  if (length > (SIZE_MAX - 1) / 3) return false;
  char *buf = (char *)malloc(length * 3 + 1);
  if (!buf) return false;
  size_t used = 0;
  if (utf16) {
    for (size_t i = 0; i + 1 < length;) {
      uint16_t u = read16(input + i);
      if (stop_at_nul && u == 0) break;
      i += 2;
      uint32_t cp;
      if (u >= 0xd800 && u <= 0xdbff) {
        if (i + 1 < length) {
          uint16_t next = read16(input + i);
          if (next >= 0xdc00 && next <= 0xdfff) {
            cp = 0x10000 + (((uint32_t)u - 0xd800) << 10) +
                 ((uint32_t)next - 0xdc00);
            i += 2;
          } else {
            cp = 0xfffd;
          }
        } else {
          cp = 0xfffd;
        }
      } else if (u >= 0xdc00 && u <= 0xdfff) {
        cp = 0xfffd;
      } else {
        cp = u;
      }
      append_codepoint(buf, &used, cp);
    }
  } else {
    for (size_t i = 0; i < length;) {
      uint8_t c = input[i];
      if (stop_at_nul && c == 0) break;
      if (c < 0x80) {
        buf[used++] = (char)c;
        i++;
        continue;
      }
      unsigned width = c >= 0xc2 && c <= 0xdf ? 2u :
                       c >= 0xe0 && c <= 0xef ? 3u :
                       c >= 0xf0 && c <= 0xf4 ? 4u : 0u;
      if (width == 0) {
        append_codepoint(buf, &used, 0xfffd);
        i++;
        continue;
      }
      uint32_t cp = c & (width == 2 ? 0x1fu : width == 3 ? 0x0fu : 0x07u);
      unsigned valid = 1;
      for (; valid < width && i + valid < length; valid++) {
        uint8_t b = input[i + valid];
        bool continuation = b >= 0x80 && b <= 0xbf;
        if (valid == 1) {
          if (c == 0xe0) continuation = b >= 0xa0 && b <= 0xbf;
          if (c == 0xed) continuation = b >= 0x80 && b <= 0x9f;
          if (c == 0xf0) continuation = b >= 0x90 && b <= 0xbf;
          if (c == 0xf4) continuation = b >= 0x80 && b <= 0x8f;
        }
        if (!continuation) break;
        cp = (cp << 6) | (uint32_t)(b & 0x3f);
      }
      if (valid == width) append_codepoint(buf, &used, cp);
      else append_codepoint(buf, &used, 0xfffd);
      i += valid;
    }
  }
  buf[used] = 0;
  free(out->data);
  out->data = buf;
  out->length = used;
  out->defined = true;
  return true;
}

static bool set_guid(xx_lnk_yx_string *out, const uint8_t *p) {
  static const char digits[] = "0123456789abcdef";
  static const uint8_t order[] = {
      3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
  char *s = (char *)malloc(37);
  if (!s) return false;
  size_t j = 0;
  for (size_t i = 0; i < 16; i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) s[j++] = '-';
    uint8_t b = p[order[i]];
    s[j++] = digits[b >> 4];
    s[j++] = digits[b & 15];
  }
  s[36] = 0;
  free(out->data);
  out->defined = true;
  out->data = s;
  out->length = 36;
  return true;
}

static bool set_bytes(xx_lnk_yx_bytes *out, const uint8_t *input,
                      size_t length) {
  uint8_t *buf = (uint8_t *)malloc(length ? length : 1);
  if (!buf) return false;
  if (length) memcpy(buf, input, length);
  free(out->data);
  out->data = buf;
  out->length = length;
  out->defined = true;
  return true;
}

static uint32_t item_type(uint8_t class_byte) {
  switch (class_byte & 0x70) {
    case 0x20: return 0x20;
    case 0x30: return 0x30;
    case 0x40: return 0x40;
    default: break;
  }
  switch (class_byte) {
    case 0x00: return 0x00;
    case 0x01: return 0x01;
    case 0x1e: case 0x1f: return 0x1f;
    case 0x52: return 0x52;
    case 0x61: return 0x61;
    case 0x70: case 0x71: return 0x71;
    case 0x72: return 0x72;
    case 0x73: return 0x73;
    case 0x74: return 0x74;
    default: return class_byte;
  }
}

static bool append_shell_item(lnk_parser *p, const uint8_t *data,
                              size_t length) {
  if (length == 0) return true;
  xx_lnk_yx_result *r = p->result;
  if (r->target_id_list_count == SIZE_MAX / sizeof(xx_lnk_yx_shell_item))
    return false;
  size_t count = r->target_id_list_count + 1;
  xx_lnk_yx_shell_item *items = (xx_lnk_yx_shell_item *)realloc(
      r->target_id_list, count * sizeof(xx_lnk_yx_shell_item));
  if (!items) return false;
  r->target_id_list = items;
  xx_lnk_yx_shell_item *item = &items[count - 1];
  memset(item, 0, sizeof(*item));
  r->target_id_list_count = count;
  uint8_t class_byte = data[0];
  set_uint(&item->item_type, item_type(class_byte));
  if (!set_bytes(&item->data, data + 1, length - 1)) return false;

  if (class_byte == 0x00) {
    if (length > 10) {
      bool unicode = data[10] == 0;
      size_t start = unicode ? 22 : 10;
      if (length >= start && !set_string(&item->cpl_file_path,
                                          data + start, length - start,
                                          unicode, true)) return false;
    }
  } else if (class_byte == 0x1e || class_byte == 0x1f) {
    if (length >= 18 && !set_guid(&item->root_folder_id, data + 2))
      return false;
  } else if ((class_byte & 0x70) == 0x20) {
    if (class_byte & 1) {
      if (length >= 21 && !set_string(&item->volume_name, data + 1,
                                      20, false, true)) return false;
    } else if (length >= 18 && !set_guid(&item->volume_id, data + 2)) {
      return false;
    }
  } else if ((class_byte & 0x70) == 0x30) {
    if (length >= 12 && !set_string(&item->file_entry_name, data + 12,
                                    length - 12, (class_byte & 4) != 0,
                                    true)) return false;
  } else if ((class_byte & 0x70) == 0x40) {
    if (length >= 3 && !set_string(&item->network_location, data + 3,
                                   length - 3, false, true)) return false;
  }
  return true;
}

static bool parse_id_list(lnk_parser *p) {
  if (p->size - p->pos < 2) return false;
  size_t list_size = read16(p->data + p->pos);
  p->pos += 2;
  if (list_size > p->size - p->pos) list_size = p->size - p->pos;
  size_t end = p->pos + list_size;
  while (end - p->pos >= 2) {
    uint16_t declared = read16(p->data + p->pos);
    p->pos += 2;
    if (declared == 0) break;
    size_t data_len = declared > 2 ? (size_t)declared - 2 : 0;
    if (data_len > end - p->pos) data_len = end - p->pos;
    if (!append_shell_item(p, p->data + p->pos, data_len)) {
      p->oom = true;
      return false;
    }
    p->pos += data_len;
  }
  p->pos = end;
  return true;
}

static bool parse_volume_id(lnk_parser *p, const uint8_t *input,
                            size_t available) {
  if (available < 16) return false;
  uint32_t volume_size = read32(input);
  uint32_t drive_type = read32(input + 4);
  uint32_t serial = read32(input + 8);
  uint32_t label_offset = read32(input + 12);
  if (drive_type <= INT32_MAX) set_uint(&p->result->drive_type, drive_type);
  set_uint(&p->result->drive_serial_number, serial);
  if (volume_size > available) return false;
  if (label_offset == 0x14) {
    if (available < 20) return false;
    label_offset = read32(input + 16);
    if (label_offset <= volume_size &&
        !set_string(&p->result->volume_label, input + label_offset,
                    volume_size - label_offset, true, true)) {
      p->oom = true;
      return false;
    }
  } else if (label_offset <= volume_size &&
             !set_string(&p->result->volume_label, input + label_offset,
                         volume_size - label_offset, false, true)) {
    p->oom = true;
    return false;
  }
  return true;
}

static bool parse_link_info(lnk_parser *p) {
  size_t available = p->size - p->pos;
  if (available < 28) return false;
  const uint8_t *input = p->data + p->pos;
  uint32_t size = read32(input);
  uint32_t header_size = read32(input + 4);
  uint32_t flags = read32(input + 8);
  uint32_t volume_offset = read32(input + 12);
  uint32_t local_offset = read32(input + 16);
  uint32_t suffix_offset = read32(input + 24);
  uint32_t local_unicode_offset = 0;
  uint32_t suffix_unicode_offset = 0;
  if (header_size >= 0x24) {
    if (available < 36) return false;
    local_unicode_offset = read32(input + 28);
    suffix_unicode_offset = read32(input + 32);
  }
  if (size > available) return false;
  p->pos += size;
  if (flags & 1) {
    if (volume_offset <= size)
      (void)parse_volume_id(p, input + volume_offset, size - volume_offset);
    if (p->oom) return false;
    uint32_t off = local_unicode_offset ? local_unicode_offset : local_offset;
    if (off <= size &&
        !set_string(&p->result->local_base_path, input + off, size - off,
                    local_unicode_offset != 0, true)) {
      p->oom = true;
      return false;
    }
  }
  if (flags & 2) {
    uint32_t off = suffix_unicode_offset ? suffix_unicode_offset : suffix_offset;
    if (off <= size &&
        !set_string(&p->result->common_path_suffix, input + off,
                    size - off, suffix_unicode_offset != 0, true)) {
      p->oom = true;
      return false;
    }
  }
  return true;
}

static bool parse_string_data(lnk_parser *p, xx_lnk_yx_string *out,
                              bool unicode, bool cap_260) {
  if (p->size - p->pos < 2) return false;
  size_t length = read16(p->data + p->pos);
  p->pos += 2;
  if (cap_260 && length > 260) length = 260;
  size_t bytes = unicode ? length * 2 : length;
  if (bytes > p->size - p->pos) return false;
  if (!set_string(out, p->data + p->pos, bytes, unicode, unicode)) {
    p->oom = true;
    return false;
  }
  p->pos += bytes;
  return true;
}

static bool parse_tracker(lnk_parser *p, const uint8_t *data,
                          size_t length) {
  /* TrackerDataBlock: length, version, 16-byte machine ID, four GUIDs. */
  if (length < 88) return true;
  xx_lnk_yx_tracker *t = &p->result->tracker_data;
  set_uint(&t->version, read32(data + 4));
  if (!set_string(&t->machine_id, data + 8, 16, false, true) ||
      !set_guid(&t->droid_volume_id, data + 24) ||
      !set_guid(&t->droid_file_id, data + 40) ||
      !set_guid(&t->droid_birth_volume_id, data + 56) ||
      !set_guid(&t->droid_birth_file_id, data + 72)) {
    p->oom = true;
    return false;
  }
  t->defined = true;
  return true;
}

static bool parse_extra_data(lnk_parser *p) {
  while (p->size - p->pos >= 4) {
    uint32_t block_size = read32(p->data + p->pos);
    if (block_size < 4) {
      p->pos += 4;
      size_t overlay_size = p->size - p->pos;
      set_uint(&p->result->overlay_size, overlay_size);
      if (overlay_size) set_uint(&p->result->overlay_offset, p->pos);
      return true;
    }
    if (block_size > p->size - p->pos) return false;
    if (block_size >= 8 && read32(p->data + p->pos + 4) == 0xa0000003) {
      if (!parse_tracker(p, p->data + p->pos + 8, block_size - 8))
        return false;
    }
    p->pos += block_size;
  }
  return false;
}

static void parse_body(lnk_parser *p, uint32_t flags) {
  bool unicode = (flags & 0x80) != 0;
  xx_lnk_yx_result *r = p->result;
  if ((flags & 1) && !parse_id_list(p)) return;
  if ((flags & 2) && !parse_link_info(p)) return;
  if ((flags & 4) && !parse_string_data(p, &r->name, unicode, true)) return;
  if ((flags & 8) && !parse_string_data(p, &r->relative_path, unicode, true)) return;
  if ((flags & 0x10) && !parse_string_data(p, &r->working_dir, unicode, true))
    return;
  if ((flags & 0x20) && !parse_string_data(p, &r->cmd_line_args, unicode,
                                          false)) return;
  if ((flags & 0x40) && !parse_string_data(p, &r->icon_location, unicode,
                                          true)) return;
  (void)parse_extra_data(p);
}

static void free_string(xx_lnk_yx_string *s) {
  free(s->data);
  memset(s, 0, sizeof(*s));
}

void xx_lnk_yx_result_free(xx_lnk_yx_result *r) {
  if (!r) return;
  free_string(&r->name);
  free_string(&r->icon_location);
  free_string(&r->volume_label);
  free_string(&r->local_base_path);
  free_string(&r->common_path_suffix);
  free_string(&r->relative_path);
  free_string(&r->working_dir);
  free_string(&r->cmd_line_args);
  free_string(&r->tracker_data.machine_id);
  free_string(&r->tracker_data.droid_volume_id);
  free_string(&r->tracker_data.droid_file_id);
  free_string(&r->tracker_data.droid_birth_volume_id);
  free_string(&r->tracker_data.droid_birth_file_id);
  for (size_t i = 0; i < r->target_id_list_count; i++) {
    xx_lnk_yx_shell_item *item = &r->target_id_list[i];
    free(item->data.data);
    free_string(&item->cpl_file_path);
    free_string(&item->root_folder_id);
    free_string(&item->volume_name);
    free_string(&item->volume_id);
    free_string(&item->file_entry_name);
    free_string(&item->network_location);
  }
  free(r->target_id_list);
  memset(r, 0, sizeof(*r));
}

bool xx_lnk_yx_parse(const uint8_t *data, size_t size, xx_lnk_yx_result *result) {
  static const uint8_t clsid[16] = {
      0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
  if (!result || (!data && size)) return false;
  memset(result, 0, sizeof(*result));
  if (size < 76 || read32(data) != 0x4c || memcmp(data + 4, clsid, 16))
    return true;
  result->is_lnk = true;
  uint32_t flags = read32(data + 20);
  set_uint(&result->file_attributes, read32(data + 24));
  static const uint64_t epoch = UINT64_C(11644473600);
  uint64_t filetime = read64(data + 28) / UINT64_C(10000000);
  if (filetime >= epoch) set_uint(&result->creation_time, filetime - epoch);
  filetime = read64(data + 36) / UINT64_C(10000000);
  if (filetime >= epoch) set_uint(&result->access_time, filetime - epoch);
  filetime = read64(data + 44) / UINT64_C(10000000);
  if (filetime >= epoch) set_uint(&result->write_time, filetime - epoch);
  set_uint(&result->file_size, read32(data + 52));
  set_uint(&result->icon_index, read32(data + 56));
  uint32_t show = read32(data + 60);
  if (show <= INT32_MAX) set_uint(&result->show_command, show);
  lnk_parser parser = {data, size, 76, result, false};
  parse_body(&parser, flags);
  if (parser.oom) {
    xx_lnk_yx_result_free(result);
    return false;
  }
  return true;
}

bool xx_lnk_yx_constant(const char *name, uint64_t *value) {
  static const struct { const char *name; uint32_t value; } constants[] = {
      {"FILE_ATTRIBUTE_READONLY", 0x1},
      {"FILE_ATTRIBUTE_HIDDEN", 0x2},
      {"FILE_ATTRIBUTE_SYSTEM", 0x4},
      {"RESERVED_1", 0x8},
      {"FILE_ATTRIBUTE_DIRECTORY", 0x10},
      {"FILE_ATTRIBUTE_ARCHIVE", 0x20},
      {"RESERVED_2", 0x40},
      {"FILE_ATTRIBUTE_NORMAL", 0x80},
      {"FILE_ATTRIBUTE_TEMPORARY", 0x100},
      {"FILE_ATTRIBUTE_SPARSE_FILE", 0x200},
      {"FILE_ATTRIBUTE_REPARSE_POINT", 0x400},
      {"FILE_ATTRIBUTE_COMPRESSED", 0x800},
      {"FILE_ATTRIBUTE_OFFLINE", 0x1000},
      {"FILE_ATTRIBUTE_NOT_CONTENT_INDEXED", 0x2000},
      {"FILE_ATTRIBUTE_ENCRYPTED", 0x4000},
      {"ShowCommand.NORMAL", 1},
      {"ShowCommand.MAXIMIZED", 3},
      {"ShowCommand.MIN_NO_ACTIVE", 7},
      {"DriveType.UNKNOWN", 0},
      {"DriveType.NO_ROOT_DIR", 1},
      {"DriveType.REMOVABLE", 2},
      {"DriveType.FIXED", 3},
      {"DriveType.REMOTE", 4},
      {"DriveType.CDROM", 5},
      {"DriveType.RAMDISK", 6},
      {"ShellItemType.CONTROL_PANEL_CPL", 0x00},
      {"ShellItemType.CONTROL_PANEL_CATEGORY", 0x01},
      {"ShellItemType.ROOT_FOLDER", 0x1f},
      {"ShellItemType.VOLUME", 0x20},
      {"ShellItemType.FILE_ENTRY", 0x30},
      {"ShellItemType.NETWORK_LOCATION", 0x40},
      {"ShellItemType.COMPRESSED_FOLDER", 0x52},
      {"ShellItemType.URI", 0x61},
      {"ShellItemType.CONTROL_PANEL", 0x71},
      {"ShellItemType.PRINTERS", 0x72},
      {"ShellItemType.COMMON_PLACES_FOLDER", 0x73},
      {"ShellItemType.USERS_FILES_FOLDER", 0x74},
  };
  if (!name || !value) return false;
  for (size_t i = 0; i < sizeof(constants) / sizeof(constants[0]); i++) {
    if (strcmp(name, constants[i].name) == 0) {
      *value = constants[i].value;
      return true;
    }
  }
  return false;
}
