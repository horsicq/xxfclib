/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_installshield_3.h
 *  @brief InstallShield 3.x/5.x self-extracting EXE (IS3 SFX) reader. */

#ifndef XXFCLIB_FORMAT_INSTALLSHIELD_3_H
#define XXFCLIB_FORMAT_INSTALLSHIELD_3_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An InstallShield 3.x/5.x self-extracting executable.
 *
 * Three stub builds share one payload format: a 16-bit NE "InstallShield
 * Self-Extracting Stub Program", a PE stub linked with Greenleaf ArchiveLib,
 * and the PE "InstallShield Launcher SE v2.1".  Each stub carries a 0x194-byte
 * descriptor as its resource of type 1024 named "MYRESOURCE":
 *
 *   0x000  u32  0x194        descriptor size
 *   0x004  u32  6
 *   0x008  u32  data offset  first record; PE: exactly the end of the image
 *   0x00C  u32  record count
 *   0x010  u32  total size   end of the last record (the file size)
 *   0x014  u32  (unknown)
 *   0x018  u32  0
 *   0x01C  char[40]   obfuscated string, empty in every known file
 *   0x044  char[128]  obfuscated command run after extraction
 *   0x0C4  char[80]   obfuscated product title
 *   0x114  char[128]  obfuscated source directory, ending in '\'
 *
 * From the data offset, @c count records run back to back up to the total
 * size:
 *
 *   u32  path length (1..1024)
 *   path, obfuscated          the file's full path on the build machine
 *   u16  DOS date, u16 DOS time
 *   u32  data size
 *   data
 *
 * Every string is obfuscated per byte:
 *   plain[i] = ROL8(enc[i] ^ key[i & 7], (i + 1) & 7)
 *   key = CA DA 7A 5B 4A 76 3E A0
 * so the zero padding of the fixed fields reads as repeated key bytes.
 *
 * A member's data is stored verbatim: a one-file PKZIP archive in the NE and
 * Greenleaf builds, the plain file (often an IS3 ".Z" cabinet) in the
 * Launcher SE build.  Members are extracted as stored.  A member is named by
 * its path relative to the source directory, which is where the stub itself
 * puts it before running the command.
 */
typedef struct xx_installshield_3 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t descriptor_offset; /**< Absolute offset of the descriptor. */
    int64_t data_offset;       /**< Absolute offset of the first record. */
    int64_t archive_size;      /**< The descriptor's total size. */
    bool is_ne;                /**< 16-bit NE stub rather than PE. */
} xx_installshield_3;

typedef xx_installshield_3 xx_installshield_3_t;

/** Size of the descriptor resource. */
#define XX_INSTALLSHIELD_3_DESCRIPTOR_SIZE 0x194
/** Longest record path the reader accepts. */
#define XX_INSTALLSHIELD_3_MAX_PATH 1024U
/** Most records the reader accepts. */
#define XX_INSTALLSHIELD_3_MAX_RECORDS 32768U

XXFC_API void xx_installshield_3_init(xx_installshield_3 *archive,
                                      xx_io_device *device,
                                      int64_t base_address);
XXFC_API xx_installshield_3 *xx_installshield_3_create(xx_io_device *device,
                                                       int64_t base_address);
XXFC_API void xx_installshield_3_destroy(xx_installshield_3 *archive);
XXFC_API void xx_installshield_3_free(xx_installshield_3 *archive);

XXFC_API bool xx_installshield_3_check_is_valid(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_installshield_3_handle_base_info(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_installshield_3_get_format_size(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API uint64_t xx_installshield_3_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_installshield_3_create_archive_records_reading(Abstractformat *self,
                                                  const xx_list_s *options,
                                                  xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_installshield_3_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_installshield_3_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_installshield_3_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_installshield_3_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode one obfuscated IS3 SFX string in place.
 *
 * @p data holds @p size bytes whose first byte is string position 0.
 */
XXFC_API void xx_installshield_3_decode(uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INSTALLSHIELD_3_H */
