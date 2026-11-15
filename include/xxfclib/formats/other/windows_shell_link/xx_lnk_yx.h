/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* YARA-X-exact Windows Shell Link (.lnk) helpers.
 *
 * The regular reader in xx_windows_shell_link.h is a validation-only
 * detection stub for the script API. The functions in this header instead
 * reproduce, bit for bit, the parsing behavior of YARA-X 1.21.0's lnk module
 * (and of the xxyara port of it): ShellLinkHeader fields with FILETIME to
 * Unix-time conversion, LinkTargetIDList shell items (volume names, root
 * folder GUIDs, file entry names, network locations), LinkInfo volume/local
 * base/common suffix strings, the counted StringData fields with the 260
 * character cap, the TrackerDataBlock GUIDs, and the overlay offset/size
 * computed from the terminal ExtraData block.
 *
 * All strings use Rust's lossy UTF-8/UTF-16 conversion semantics. The parser
 * never modifies its input and only reads inside the supplied buffer; result
 * strings own heap copies which xx_lnk_yx_result_free releases.
 */

#ifndef XXFCLIB_LNK_YX_H
#define XXFCLIB_LNK_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional values mirror the proto2 presence rules of the YARA-X lnk module. */
typedef struct {
  bool defined;
  uint64_t value;
} xx_lnk_yx_uint;

typedef struct {
  bool defined;
  char *data;              /* UTF-8, NUL-terminated for convenience. */
  size_t length;           /* Bytes before the final NUL; may contain NULs. */
} xx_lnk_yx_string;

typedef struct {
  bool defined;
  uint8_t *data;
  size_t length;
} xx_lnk_yx_bytes;

typedef struct {
  xx_lnk_yx_uint item_type;
  xx_lnk_yx_bytes data;
  xx_lnk_yx_string cpl_file_path;
  xx_lnk_yx_string root_folder_id;
  xx_lnk_yx_string volume_name;
  xx_lnk_yx_string volume_id;
  xx_lnk_yx_string file_entry_name;
  xx_lnk_yx_string network_location;
} xx_lnk_yx_shell_item;

typedef struct {
  bool defined;
  xx_lnk_yx_uint version;
  xx_lnk_yx_string machine_id;
  xx_lnk_yx_string droid_volume_id;
  xx_lnk_yx_string droid_file_id;
  xx_lnk_yx_string droid_birth_volume_id;
  xx_lnk_yx_string droid_birth_file_id;
} xx_lnk_yx_tracker;

typedef struct {
  bool is_lnk;
  xx_lnk_yx_string name;
  xx_lnk_yx_uint creation_time;
  xx_lnk_yx_uint access_time;
  xx_lnk_yx_uint write_time;
  xx_lnk_yx_uint file_size;
  xx_lnk_yx_uint file_attributes;
  xx_lnk_yx_string icon_location;
  xx_lnk_yx_uint icon_index;
  xx_lnk_yx_uint show_command;
  xx_lnk_yx_uint drive_type;
  xx_lnk_yx_uint drive_serial_number;
  xx_lnk_yx_string volume_label;
  xx_lnk_yx_string local_base_path;
  xx_lnk_yx_string common_path_suffix;
  xx_lnk_yx_string relative_path;
  xx_lnk_yx_string working_dir;
  xx_lnk_yx_string cmd_line_args;
  xx_lnk_yx_uint overlay_size;
  xx_lnk_yx_uint overlay_offset;
  xx_lnk_yx_tracker tracker_data;
  xx_lnk_yx_shell_item *target_id_list;
  size_t target_id_list_count;
} xx_lnk_yx_result;

/* Returns false only for an invalid argument or allocation failure. A non-LNK
 * input succeeds with result->is_lnk == false and all optional fields absent.
 * Initialize result to zero before first use; free it after every parse. */
XXFC_API bool xx_lnk_yx_parse(const uint8_t *data, size_t size,
                              xx_lnk_yx_result *result);

/* Releases every owned string, the shell item array and then zeroes the
 * result. Safe on a zero-initialized or already-freed result. */
XXFC_API void xx_lnk_yx_result_free(xx_lnk_yx_result *result);

/* Names include inline FileAttributes constants and qualified enum members,
 * such as "FILE_ATTRIBUTE_READONLY" and "ShowCommand.NORMAL". */
XXFC_API bool xx_lnk_yx_constant(const char *name, uint64_t *value);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_LNK_YX_H */
