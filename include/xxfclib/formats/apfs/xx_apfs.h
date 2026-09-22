/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_apfs.h @brief Apple File System (APFS) container reader. */

/* APFS, per Apple's "Apple File System Reference". Everything is LITTLE
 * endian and every object begins with obj_phys_t:
 *
 *   obj_phys_t, 32 bytes:
 *     +0   u8[8] o_cksum, a Fletcher-64 over the rest of the block
 *     +8   u64   o_oid
 *     +16  u64   o_xid, the transaction this version belongs to
 *     +24  u32   o_type, low 16 bits the type, high bits the storage class
 *     +28  u32   o_subtype
 *
 *   nx_superblock_t (o_type 0x0001), block 0 and again in the checkpoint
 *   descriptor area:
 *     +32  u32  nx_magic, "NXSB" = 0x4253584E
 *     +36  u32  nx_block_size
 *     +40  u64  nx_block_count
 *     +72  u8[16] nx_uuid
 *     +104 u32  nx_xp_desc_blocks     (high bit set => the area is a tree)
 *     +112 i64  nx_xp_desc_base
 *     +136 u32  nx_xp_desc_index
 *     +140 u32  nx_xp_desc_len
 *     +160 u64  nx_omap_oid, a PHYSICAL oid: the block address itself
 *     +180 u32  nx_max_file_systems
 *     +184 u64[100] nx_fs_oid, VIRTUAL oids of the volume superblocks
 *
 *   omap_phys_t (o_type 0x000b):
 *     +48  u64  om_tree_oid, physical oid of the mapping B-tree
 *
 *   btree_node_phys_t (o_type 0x0003):
 *     +32  u16  btn_flags: 1 ROOT, 2 LEAF, 4 FIXED_KV_SIZE
 *     +34  u16  btn_level, 0 for a leaf
 *     +36  u32  btn_nkeys
 *     +40  nloc_t btn_table_space {u16 off; u16 len}
 *     +56  btn_data[]
 *   The table of contents sits at btn_data + table_space.off. Keys then
 *   start after it and grow up; values grow DOWN from the end of the node,
 *   minus the 40-byte btree_info_t that a root node carries.
 *
 *   apfs_superblock_t (o_type 0x000d), a volume:
 *     +32  u32  apfs_magic, "APSB" = 0x42535041
 *     +56  u64  apfs_incompatible_features
 *     +128 u64  apfs_omap_oid, physical
 *     +136 u64  apfs_root_tree_oid, virtual - resolved through the volume omap
 *     +240 u8[16] apfs_vol_uuid
 *     +704 char[256] apfs_volname
 *
 * File-system records are keyed by j_key_t: one u64 whose top 4 bits are the
 * record type (3 INODE, 4 XATTR, 8 FILE_EXTENT, 9 DIR_REC) and whose low 60
 * bits are the object id. The root directory is object id 2.
 *
 * SCOPE. This reader reaches a directory listing:
 *   1. container superblock identification and metadata, including a
 *      checkpoint-descriptor scan for the newest valid nx_superblock_t, with
 *      every object's Fletcher-64 verified;
 *   2. object-map translation, both the container omap (virtual volume oids
 *      -> block addresses) and each volume's own omap;
 *   3. a directory listing per volume, from the FS tree's INODE and DIR_REC
 *      records, with sizes taken from the inode's DSTREAM extended field;
 *   4. extraction, but ONLY for a file described by a single uncompressed
 *      FILE_EXTENT starting at logical offset 0.
 *
 * NOT supported, and reported as such rather than guessed at: compressed
 * files (the com.apple.decmpfs xattr - zlib is available in this library but
 * LZFSE and LZVN are not, so no decmpfs path is attempted at all), encrypted
 * volumes, Fusion containers, snapshots, sparse/multi-extent files, and
 * clones. A volume whose FS tree cannot be reached is still reported with its
 * name, UUID and counters.
 */

#ifndef XXFCLIB_FORMAT_APFS_H
#define XXFCLIB_FORMAT_APFS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** "NXSB" read as a little-endian 32-bit value, at offset 32 of block 0. */
#define XX_APFS_NX_MAGIC UINT32_C(0x4253584E)
/** "APSB", a volume superblock. */
#define XX_APFS_VOLUME_MAGIC UINT32_C(0x42535041)

#define XX_APFS_OBJ_PHYS_SIZE 32U
#define XX_APFS_MAX_VOLUMES 100U
#define XX_APFS_VOLNAME_SIZE 256U
#define XX_APFS_UUID_SIZE 16U

/** Low 16 bits of obj_phys_t.o_type. */
#define XX_APFS_OBJECT_TYPE_NX_SUPERBLOCK 0x0001U
#define XX_APFS_OBJECT_TYPE_BTREE 0x0002U
#define XX_APFS_OBJECT_TYPE_BTREE_NODE 0x0003U
#define XX_APFS_OBJECT_TYPE_OMAP 0x000BU
#define XX_APFS_OBJECT_TYPE_CHECKPOINT_MAP 0x000CU
#define XX_APFS_OBJECT_TYPE_FS 0x000DU

/** j_key_t record types, the top 4 bits of the key's first u64. */
#define XX_APFS_TYPE_INODE 3U
#define XX_APFS_TYPE_XATTR 4U
#define XX_APFS_TYPE_FILE_EXTENT 8U
#define XX_APFS_TYPE_DIR_REC 9U

/** Root directory object id. */
#define XX_APFS_ROOT_DIR_INO 2U

typedef struct xx_apfs_volume_info_s {
    uint64_t oid;             /**< Virtual oid from nx_fs_oid. */
    int64_t block;            /**< Block address it resolved to, or -1. */
    char name[XX_APFS_VOLNAME_SIZE + 1U];
    uint8_t uuid[XX_APFS_UUID_SIZE];
    uint64_t incompatible_features;
    uint64_t fs_flags;
    uint64_t num_files;
    uint64_t num_directories;
    uint64_t root_tree_oid;
    uint64_t omap_oid;
    uint64_t records;         /**< Entries listed for this volume. */
    bool superblock_valid;    /**< APSB magic and Fletcher-64 both held. */
    bool tree_reached;        /**< The FS tree root was found and walked. */
} xx_apfs_volume_info;

typedef struct xx_apfs xx_apfs;
typedef struct xx_apfs xx_apfs_t;
typedef struct xx_apfs XApfs;

struct xx_apfs {
    Abstractformat format;

    uint64_t number_of_records;
    uint64_t number_of_members;

    /* Container superblock. */
    uint32_t block_size;
    uint64_t block_count;
    uint64_t xid;             /**< Transaction id of the checkpoint used. */
    uint8_t uuid[XX_APFS_UUID_SIZE];
    uint64_t features;
    uint64_t readonly_compatible_features;
    uint64_t incompatible_features;
    uint64_t omap_oid;
    uint32_t max_file_systems;
    uint32_t next_version;    /**< nx_newest_mounted_version, informational. */

    /** Device offset of the nx_superblock_t that was accepted. */
    int64_t super_offset;
    /** True when the accepted superblock came from the checkpoint area. */
    bool from_checkpoint;
    /** Container object-map entries recovered. */
    uint64_t omap_entries;

    uint32_t volume_count;
    xx_apfs_volume_info volumes[XX_APFS_MAX_VOLUMES];

    /** Records listed but not extractable (compressed, sparse, ...). */
    uint64_t number_of_unsupported;

    void *internal;
};

XXFC_API void xx_apfs_init(xx_apfs *apfs, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_apfs *xx_apfs_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_apfs_destroy(xx_apfs *apfs);
XXFC_API void xx_apfs_free(xx_apfs *apfs);

XXFC_API bool xx_apfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_apfs_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_apfs_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_apfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_apfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_apfs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_apfs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_apfs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint32_t xx_apfs_get_block_size(const xx_apfs *apfs);
XXFC_API uint32_t xx_apfs_get_volume_count(const xx_apfs *apfs);
XXFC_API const xx_apfs_volume_info *xx_apfs_get_volume(const xx_apfs *apfs,
                                                       uint32_t index);

/** APFS's Fletcher-64 over size bytes starting after the 8-byte checksum. */
XXFC_API uint64_t xx_apfs_fletcher64(const void *data, size_t size);

static inline Abstractformat *xx_apfs_to_format(xx_apfs *apfs) {
    return apfs ? &apfs->format : NULL;
}
static inline void XApfs_init(xx_apfs *apfs, xx_io_device *dev,
                              int64_t base_address) {
    xx_apfs_init(apfs, dev, base_address);
}
static inline xx_apfs *XApfs_create(xx_io_device *dev, int64_t base_address) {
    return xx_apfs_create(dev, base_address);
}
static inline void XApfs_free(xx_apfs *apfs) { xx_apfs_free(apfs); }
static inline bool XApfs_is_valid(xx_apfs *apfs, xx_pd_struct *pd) {
    return apfs ? xx_format_is_valid(&apfs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_APFS_H */
