/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_uboot.h @brief U-Boot environment block reader. */

/*
 * This is the U-Boot *environment*, the block of `key=value` settings U-Boot
 * keeps in flash - the thing `fw_printenv` reads.  It is NOT binwalk's
 * "uboot" signature, which matches the ASCII string "U-Boot <digit>" in a
 * bootloader binary and has nothing to do with this block; binwalk has no
 * module for the environment at all, so there is nothing to port and the
 * layout below comes from U-Boot's own env/ and tools/env/ sources.
 *
 *   +0   u32   crc32, in the TARGET's byte order (U-Boot stores it with a
 *              plain native u32 write; mkenvimage -b makes a big-endian one)
 *   +4   u8    flags       - ONLY in the redundant layout
 *   +4/5 ...   NUL separated "key=value" entries, terminated by an empty
 *              entry (a second NUL), padded to the end of the block with 0xFF
 *              on NOR flash or 0x00 elsewhere
 *
 * The critical and awkward property: the CRC32 covers the ENTIRE data area,
 * padding included, and the size of that area is a board configuration
 * constant (CONFIG_ENV_SIZE) that is nowhere in the block.  So the block has
 * no magic, and the only way to recognise one is to guess the size and see
 * whether the CRC comes out.  This reader tries a table of the sizes real
 * boards use, largest first, plus the whole remainder of the device, in both
 * the plain and redundant layouts (never a size below XX_UBOOT_MIN_ENV_SIZE
 * or above XX_UBOOT_MAX_ENV_SIZE), and accepts the first combination whose
 * CRC32 matches (read little or big endian) and whose data parses as at
 * least one `key=value` pair.  format.endian reports which byte order the
 * stored CRC used.
 *
 * Detection cost: before any CRC is computed, the first entry must already
 * look like a variable name - 1..XX_UBOOT_MAX_KEY_LENGTH printable,
 * non-space ASCII bytes followed by '=' - right after the 4- or 5-byte
 * header.  Only a layout that passes this check is CRC-tested, and all the
 * candidate sizes are checked in ONE incremental CRC pass over one bounded
 * read, so garbage costs a single small read and a genuine block costs at
 * most two passes over min(file, 1 MiB).
 *
 * The CRC is the ordinary ISO-HDLC CRC32 - U-Boot's crc32() is zlib's - so
 * xx_crc32_calc(0, ...) computes it directly, with no JAMCRC complement of
 * the kind the TRX reader needs.
 *
 * This is a key/value store, not a container: nothing inside it is a payload
 * that could be handed to another reader.  It is therefore published with
 * is_archive = false and format_type = XX_TYPE_RAW, and the variables are
 * reached through xx_uboot_get_variable_*() rather than through the archive
 * record API.  Publishing each variable as a one-record "member" would make
 * the generic enumerators work, but it would also make every consumer of
 * xx_format_unpack_current_archive_record() try to write a file per setting,
 * which is not what anyone wants from an environment block.
 *
 * Bounds: the number of variables, the length of any one entry, and the size
 * of the buffered block are all capped, so a block with no terminating double
 * NUL stops at the end of the data area rather than scanning on.
 */

#ifndef XXFCLIB_FORMAT_UBOOT_H
#define XXFCLIB_FORMAT_UBOOT_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The CRC32 field, always present. */
#define XX_UBOOT_CRC_SIZE 4U
/** Largest environment block buffered, and the largest candidate size tried. */
#define XX_UBOOT_MAX_ENV_SIZE (1024U * 1024U)
/** Smallest block accepted.  Every size tried must be at least this: the
 * whole remainder of the device, each candidate-table size (the table starts
 * at 0x400) and a size pinned with xx_uboot_set_env_size().  A device with
 * fewer bytes left than this is refused before anything is read, so tiny
 * files never reach the CRC. */
#define XX_UBOOT_MIN_ENV_SIZE 256U
/** Upper bound on the variable count in one block. */
#define XX_UBOOT_MAX_VARIABLES 4096U
/** Upper bound on one "key=value" entry, key and value together. */
#define XX_UBOOT_MAX_ENTRY_LENGTH 32768U
/** Upper bound on a variable name.  Names are printable ASCII other than
 * space and '='; U-Boot's own names are identifiers well under this. */
#define XX_UBOOT_MAX_KEY_LENGTH 128U

/** Which of the two on-flash layouts was recognised. */
typedef enum xx_uboot_layout_e {
    XX_UBOOT_LAYOUT_NONE = 0,
    XX_UBOOT_LAYOUT_PLAIN = 1,    /**< crc32 then data. */
    XX_UBOOT_LAYOUT_REDUNDANT = 2 /**< crc32, a flags byte, then data. */
} xx_uboot_layout_t;

typedef struct xx_uboot xx_uboot;
typedef struct xx_uboot xx_uboot_t;
typedef struct xx_uboot XUboot;

struct xx_uboot {
    Abstractformat format;
    xx_uboot_layout_t layout;
    uint64_t number_of_variables;
    uint32_t env_size;   /**< The block size the CRC validated against. */
    uint32_t data_size;  /**< env_size minus the CRC (and flags) field. */
    uint32_t crc32;      /**< The stored CRC, verified on parse. */
    uint8_t flags;       /**< The redundant layout's flags byte, else 0. */
    int64_t archive_end; /**< base_address + env_size, or -1. */
    void *internal;
};

XXFC_API void xx_uboot_init(xx_uboot *uboot, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_uboot *xx_uboot_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_uboot_destroy(xx_uboot *uboot);
XXFC_API void xx_uboot_free(xx_uboot *uboot);

XXFC_API bool xx_uboot_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_uboot_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_uboot_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);

/**
 * @brief Pin the block size instead of searching for it.
 *
 * CONFIG_ENV_SIZE is not in the block, so by default a table of common sizes
 * is tried.  A caller that knows the board's value can set it here before
 * validation; the candidate search is then skipped entirely.  Pass 0 to go
 * back to searching.  Must be called before the first validation.  A value
 * above XX_UBOOT_MAX_ENV_SIZE is ignored; a non-zero value below
 * XX_UBOOT_MIN_ENV_SIZE is kept, and validation then refuses the block.
 */
XXFC_API void xx_uboot_set_env_size(xx_uboot *uboot, uint32_t env_size);

XXFC_API uint64_t xx_uboot_get_number_of_variables(const xx_uboot *uboot);
XXFC_API uint32_t xx_uboot_get_env_size(const xx_uboot *uboot);
XXFC_API uint32_t xx_uboot_get_crc32(const xx_uboot *uboot);
XXFC_API xx_uboot_layout_t xx_uboot_get_layout(const xx_uboot *uboot);
/** Key of variable @p index, or NULL when it is out of range. */
XXFC_API const char *xx_uboot_get_variable_key(const xx_uboot *uboot,
                                               uint64_t index);
/** Value of variable @p index, or NULL when it is out of range. */
XXFC_API const char *xx_uboot_get_variable_value(const xx_uboot *uboot,
                                                 uint64_t index);
/** Value for @p key, or NULL when the block does not define it. */
XXFC_API const char *xx_uboot_find_variable(const xx_uboot *uboot,
                                            const char *key);

static inline Abstractformat *xx_uboot_to_format(xx_uboot *uboot) {
    return uboot ? &uboot->format : NULL;
}
static inline void XUboot_init(xx_uboot *uboot, xx_io_device *dev,
                               int64_t base_address) {
    xx_uboot_init(uboot, dev, base_address);
}
static inline xx_uboot *XUboot_create(xx_io_device *dev,
                                      int64_t base_address) {
    return xx_uboot_create(dev, base_address);
}
static inline void XUboot_free(xx_uboot *uboot) { xx_uboot_free(uboot); }
static inline bool XUboot_is_valid(xx_uboot *uboot, xx_pd_struct *pd) {
    return uboot ? xx_format_is_valid(&uboot->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UBOOT_H */
