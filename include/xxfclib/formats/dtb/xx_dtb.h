/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_dtb.h @brief Flattened Device Tree (DTB / FDT) reader. */

/* A flattened device tree is a header, a memory reservation block, a
 * structure block and a strings block.  Every field is BIG endian.
 *
 *   header
 *     +0   u32  magic 0xD00DFEED
 *     +4   u32  totalsize, the whole blob including this header
 *     +8   u32  off_dt_struct
 *     +12  u32  off_dt_strings
 *     +16  u32  off_mem_rsvmap
 *     +20  u32  version
 *     +24  u32  last_comp_version
 *     +28  u32  boot_cpuid_phys      (version 2 and later)
 *     +32  u32  size_dt_strings      (version 3 and later)
 *     +36  u32  size_dt_struct       (version 17 and later)
 *
 * The memory reservation block is a list of (address, size) big endian u64
 * pairs closed by a pair of zeros.
 *
 * The structure block is a stream of big endian u32 tokens, each token and
 * everything after it aligned to 4 bytes:
 *
 *     1  FDT_BEGIN_NODE  followed by the node name, NUL terminated
 *     2  FDT_END_NODE
 *     3  FDT_PROP        followed by u32 length, u32 name offset, then the
 *                        value; the name offset indexes the strings block
 *     4  FDT_NOP
 *     9  FDT_END
 *
 * Before version 16 a property value of eight bytes or more starts on an
 * 8-byte boundary; from version 16 on everything is 4-byte aligned.
 *
 * WHAT IS PUBLISHED.  Every node becomes a folder member and every property
 * becomes a file member whose bytes are the property value, named by its
 * path - "soc/uart@1000/reg", say.  That makes a FIT image fall out for
 * free: a FIT is a device tree whose /images/<name> subnodes carry the
 * payload in their "data" property, so each payload is already a member.
 * The external-data FIT variant, where /images/<name> carries data-offset
 * and data-size instead and the payload sits past the aligned end of the
 * blob, is resolved into a synthetic "data" member for that node.
 *
 * Every offset, length and name offset in the blob is attacker controlled.
 * The structure walk carries an explicit depth bound so a stream of nested
 * FDT_BEGIN_NODE tokens cannot recurse without limit, a name offset outside
 * the strings block is refused rather than read, and the two blocks that are
 * staged in memory are both capped.
 */

#ifndef XXFCLIB_FORMAT_DTB_H
#define XXFCLIB_FORMAT_DTB_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_dtb xx_dtb;
typedef struct xx_dtb xx_dtb_t;
typedef struct xx_dtb XDtb;

struct xx_dtb {
    Abstractformat format;
    uint64_t number_of_records;    /**< Nodes plus properties. */
    uint64_t number_of_members;    /**< Same; every record is a member. */
    uint64_t number_of_nodes;      /**< Nodes, the root included. */
    uint64_t number_of_properties; /**< Properties across all nodes. */
    uint32_t total_size;           /**< totalsize from the header. */
    uint32_t version;              /**< version from the header. */
    uint32_t last_comp_version;    /**< last_comp_version from the header. */
    uint32_t boot_cpuid_phys;      /**< boot_cpuid_phys, 0 before version 2. */
    uint32_t struct_size;          /**< Size of the structure block. */
    uint32_t strings_size;         /**< Size of the strings block. */
    uint32_t reservations;         /**< Memory reservation entries, zero pair
                                        excluded. */
    bool is_fit;                   /**< True when an /images node is present. */
    int64_t archive_end;           /**< base_address + totalsize, or -1. */
    void *internal;
};

XXFC_API void xx_dtb_init(xx_dtb *dtb, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_dtb *xx_dtb_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_dtb_destroy(xx_dtb *dtb);
XXFC_API void xx_dtb_free(xx_dtb *dtb);

XXFC_API bool xx_dtb_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dtb_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_dtb_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_dtb_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dtb_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dtb_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dtb_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dtb_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dtb_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_dtb_get_number_of_records(const xx_dtb *dtb);
XXFC_API uint64_t xx_dtb_get_number_of_members(const xx_dtb *dtb);
XXFC_API uint64_t xx_dtb_get_number_of_nodes(const xx_dtb *dtb);
XXFC_API uint64_t xx_dtb_get_number_of_properties(const xx_dtb *dtb);
XXFC_API uint32_t xx_dtb_get_total_size(const xx_dtb *dtb);
XXFC_API uint32_t xx_dtb_get_version(const xx_dtb *dtb);
XXFC_API bool xx_dtb_get_is_fit(const xx_dtb *dtb);
XXFC_API int64_t xx_dtb_get_archive_end(const xx_dtb *dtb);

static inline Abstractformat *xx_dtb_to_format(xx_dtb *dtb) {
    return dtb ? &dtb->format : NULL;
}
static inline void XDtb_init(xx_dtb *dtb, xx_io_device *dev,
                             int64_t base_address) {
    xx_dtb_init(dtb, dev, base_address);
}
static inline xx_dtb *XDtb_create(xx_io_device *dev, int64_t base_address) {
    return xx_dtb_create(dev, base_address);
}
static inline void XDtb_free(xx_dtb *dtb) { xx_dtb_free(dtb); }
static inline bool XDtb_is_valid(xx_dtb *dtb, xx_pd_struct *pd) {
    return dtb ? xx_format_is_valid(&dtb->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DTB_H */
