/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_pcapng.h @brief PCAP Next Generation (pcapng) capture file reader. */

/* A pcapng file is a plain sequence of blocks.  Every block has the same
 * frame, in the byte order announced by the section it belongs to:
 *
 *     +0        u32  block type
 *     +4        u32  block total length (header + body + trailer)
 *     +8        ...  body
 *     +len-4    u32  block total length again
 *
 * The file opens with a Section Header Block (SHB):
 *
 *     +0x00  u32  0x0A0D0D0A   block type, a byte palindrome, so it reads the
 *                              same in either byte order
 *     +0x04  u32  block total length
 *     +0x08  u32  0x1A2B3C4D   byte-order magic; 4D 3C 2B 1A on disk means
 *                              little endian, 1A 2B 3C 4D big endian
 *     +0x0C  u16  major version (1)
 *     +0x0E  u16  minor version (0)
 *     +0x10  u64  section length, 0xFFFFFFFFFFFFFFFF = not specified
 *     +0x18  ...  options
 *     +len-4 u32  block total length again
 *
 * followed by Interface Description Blocks (type 1), Enhanced Packet Blocks
 * (type 6), Simple Packet Blocks (type 3), Name Resolution (4), Interface
 * Statistics (5), further SHBs opening further sections, and so on.
 *
 * Sources: the pcapng specification (IETF draft-ietf-opsawg-pcapng) for the
 * layout, and binwalk's signatures/pcap.rs, structures/pcap.rs and
 * extractors/pcap.rs (pcapng_carver) for the validation and the carve
 * length, which this reader reproduces.  See xx_pcapng.c for the rules.
 *
 * Not an archive: binwalk carves the capture as one file, and so does this
 * reader - it validates and measures, and reports what follows as overlay.
 */

#ifndef XXFCLIB_FORMAT_PCAPNG_H
#define XXFCLIB_FORMAT_PCAPNG_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Section Header Block type; the same four bytes in either byte order. */
#define XX_PCAPNG_SHB_TYPE UINT32_C(0x0A0D0D0A)
/** Byte-order magic as read in the section's own byte order. */
#define XX_PCAPNG_BYTE_ORDER_MAGIC UINT32_C(0x1A2B3C4D)
/** The same magic read in the opposite byte order. */
#define XX_PCAPNG_BYTE_ORDER_MAGIC_SWAPPED UINT32_C(0x4D3C2B1A)
/** Smallest SHB the specification allows: 24 fixed bytes plus the trailer. */
#define XX_PCAPNG_SHB_MIN_SIZE 28U
/** Fixed SHB bytes read for validation (type .. section length). */
#define XX_PCAPNG_SHB_FIXED_SIZE 24U
/** Block type codes with bit 31 set are for local use; binwalk stops there. */
#define XX_PCAPNG_BLOCK_TYPE_RESERVED_MASK UINT32_C(0x80000000)
/** Block type + block total length. */
#define XX_PCAPNG_BLOCK_HEADER_SIZE 8U
/** Trailing copy of the block total length. */
#define XX_PCAPNG_BLOCK_TRAILER_SIZE 4U
/** The only major version the specification defines. */
#define XX_PCAPNG_MAJOR_VERSION 1U

#define XX_PCAPNG_BLOCK_IDB UINT32_C(0x00000001) /**< Interface Description */
#define XX_PCAPNG_BLOCK_PB  UINT32_C(0x00000002) /**< Packet (obsolete) */
#define XX_PCAPNG_BLOCK_SPB UINT32_C(0x00000003) /**< Simple Packet */
#define XX_PCAPNG_BLOCK_NRB UINT32_C(0x00000004) /**< Name Resolution */
#define XX_PCAPNG_BLOCK_ISB UINT32_C(0x00000005) /**< Interface Statistics */
#define XX_PCAPNG_BLOCK_EPB UINT32_C(0x00000006) /**< Enhanced Packet */

/** link_type value when the capture holds no measurable IDB. */
#define XX_PCAPNG_LINK_TYPE_NONE UINT32_C(0xFFFFFFFF)

typedef struct xx_pcapng xx_pcapng;
typedef struct xx_pcapng xx_pcapng_t;
typedef struct xx_pcapng XPcapng;

struct xx_pcapng {
    Abstractformat format;        /**< Base format structure (first member) */
    bool big_endian;              /**< Byte order of the first section */
    uint16_t major_version;       /**< First SHB, +0x0C */
    uint16_t minor_version;       /**< First SHB, +0x0E */
    uint64_t section_length;      /**< First SHB, +0x10 (all ones = unknown) */
    uint32_t shb_size;            /**< First SHB's block total length */
    uint64_t number_of_blocks;    /**< Blocks inside the measured size, SHB included */
    uint64_t number_of_sections;  /**< SHBs inside the measured size */
    uint64_t number_of_interfaces;/**< IDBs inside the measured size */
    uint64_t number_of_packets;   /**< EPB + SPB + PB inside the measured size */
    uint32_t link_type;           /**< First IDB's link type, or XX_PCAPNG_LINK_TYPE_NONE */
    int64_t capture_end;          /**< Absolute end of the last block, or -1 */
};

XXFC_API void xx_pcapng_init(xx_pcapng *pcapng, xx_io_device *dev,
                             int64_t base_address);
XXFC_API xx_pcapng *xx_pcapng_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_pcapng_destroy(xx_pcapng *pcapng);
XXFC_API void xx_pcapng_free(xx_pcapng *pcapng);

XXFC_API bool xx_pcapng_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pcapng_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pcapng_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);

XXFC_API bool xx_pcapng_is_big_endian(const xx_pcapng *pcapng);
XXFC_API uint16_t xx_pcapng_get_major_version(const xx_pcapng *pcapng);
XXFC_API uint16_t xx_pcapng_get_minor_version(const xx_pcapng *pcapng);
XXFC_API uint64_t xx_pcapng_get_number_of_blocks(const xx_pcapng *pcapng);
XXFC_API uint64_t xx_pcapng_get_number_of_sections(const xx_pcapng *pcapng);
XXFC_API uint64_t xx_pcapng_get_number_of_interfaces(const xx_pcapng *pcapng);
XXFC_API uint64_t xx_pcapng_get_number_of_packets(const xx_pcapng *pcapng);
XXFC_API uint32_t xx_pcapng_get_link_type(const xx_pcapng *pcapng);
XXFC_API int64_t xx_pcapng_get_capture_end(const xx_pcapng *pcapng);

static inline Abstractformat *xx_pcapng_to_format(xx_pcapng *pcapng) {
    return pcapng ? &pcapng->format : NULL;
}
static inline void XPcapng_init(xx_pcapng *pcapng, xx_io_device *dev,
                                int64_t base_address) {
    xx_pcapng_init(pcapng, dev, base_address);
}
static inline xx_pcapng *XPcapng_create(xx_io_device *dev,
                                        int64_t base_address) {
    return xx_pcapng_create(dev, base_address);
}
static inline void XPcapng_free(xx_pcapng *pcapng) {
    xx_pcapng_free(pcapng);
}
static inline bool XPcapng_is_valid(xx_pcapng *pcapng, xx_pd_struct *pd) {
    return pcapng ? xx_format_is_valid(&pcapng->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PCAPNG_H */
