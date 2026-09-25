/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_ej_technologies_install.h
 * @brief ej-technologies install4j / exe4j launcher (real SFX) reader.
 *
 * The carrier is a Windows PE (PE32 or PE32+) launcher stub.  The container
 * starts exactly at the PE overlay, i.e. behind the raw data of the last
 * section; the executable is parsed only as far as needed to find that
 * offset and is never executed or emulated.
 *
 *   u32 LE   0xE8E413D5                  stored D5 13 E4 E8
 *   s32 LE   variable count              0 < n < 0x400
 *   n x { s32 LE key; s32 LE length; length bytes }
 *                                        the first key is 101 (product name);
 *                                        key 2003 is the member list, the
 *                                        member names joined and terminated
 *                                        by ';' ("exe4jlib.jar;i4jdel.exe;")
 *   s32 LE   extra count
 *   extra count x { s32 LE key; s32 LE length; length bytes }
 *
 * The members follow back to back, in the order of the key 2003 list:
 *
 *   u32 LE   size
 *   [u32 LE  0]                          only in the later builds
 *   size bytes, every byte XOR 0x88
 *
 * The later builds (the ones with the zero word) end the container with a
 * trailer that is the magic byte-reversed plus a big-endian member count:
 *
 *   E8 E4 13 D5, u32 BE count,
 *   count x { u16 BE name length; name; u64 BE size; size bytes, stored }
 *
 * Every carrier seen so far declares a trailer count of 0.
 */

#ifndef XXFCLIB_FORMAT_EJ_TECHNOLOGIES_INSTALL_H
#define XXFCLIB_FORMAT_EJ_TECHNOLOGIES_INSTALL_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest product name (variable 101) kept, UTF-8, terminator included. */
#define XX_EJ_TECHNOLOGIES_INSTALL_PRODUCT_MAX 256

/** Record compression-method values. */
#define XX_EJ_TECHNOLOGIES_INSTALL_METHOD_STORED 0U
#define XX_EJ_TECHNOLOGIES_INSTALL_METHOD_XOR88 1U

typedef struct xx_ej_technologies_install {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t container_offset; /**< Overlay offset, relative to the base. */
    int64_t records_offset;   /**< First member header, relative. */
    uint32_t variable_count;
    uint32_t extra_count;
    uint32_t listed_members;  /**< Names in the key 2003 list. */
    uint32_t trailer_members; /**< Count declared by the trailer. */
    bool padded;              /**< Later build: zero word behind each size. */
    bool has_trailer;
    bool truncated;           /**< A member runs past the end of the file. */
    char product_name[XX_EJ_TECHNOLOGIES_INSTALL_PRODUCT_MAX];
} xx_ej_technologies_install;

typedef xx_ej_technologies_install xx_ej_technologies_install_t;

XXFC_API void xx_ej_technologies_install_init(
    xx_ej_technologies_install *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_ej_technologies_install *xx_ej_technologies_install_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_ej_technologies_install_destroy(
    xx_ej_technologies_install *archive);
XXFC_API void xx_ej_technologies_install_free(
    xx_ej_technologies_install *archive);

XXFC_API bool xx_ej_technologies_install_check_is_valid(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API bool xx_ej_technologies_install_handle_base_info(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API int64_t xx_ej_technologies_install_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_ej_technologies_install_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_ej_technologies_install_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_ej_technologies_install_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ej_technologies_install_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ej_technologies_install_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ej_technologies_install_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Product name (variable 101) after a successful handle_base_info, or "". */
XXFC_API const char *xx_ej_technologies_install_get_product_name(
    const xx_ej_technologies_install *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_EJ_TECHNOLOGIES_INSTALL_H */
