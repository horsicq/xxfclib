/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_btrfs_stream.h @brief Btrfs send stream ("btrfs send -f") reader. */

#ifndef XXFCLIB_FORMAT_BTRFS_STREAM_H
#define XXFCLIB_FORMAT_BTRFS_STREAM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Btrfs send stream: the serialized output of "btrfs send".
 *
 * Unlike the on-disk file system (src/formats/btrfs, "_BHRfS_M" superblock),
 * a send stream is a log of file-system operations that "btrfs receive"
 * replays to rebuild a subvolume.  All integers are little endian.
 *
 *   0x00  char[13] "btrfs-stream\0"
 *   0x0D  u32      version (1, 2 or 3)
 *   0x11  commands, each:
 *           +0  u32  payload length
 *           +4  u16  command (1 SUBVOL .. 26 ENABLE_VERITY)
 *           +6  u32  CRC-32C (Castagnoli, seed 0, no final xor) of the
 *                    10-byte header with this field zeroed, then the payload
 *           +10 payload: TLV attributes {u16 type, u16 length, value}.
 *               From version 2 the DATA attribute (19) has no length field
 *               and runs to the end of the payload.
 *
 * The first command is SUBVOL (a full send) or SNAPSHOT (an incremental
 * send against a parent the stream does not carry); END (21) closes the
 * stream.  The reader replays MKFILE / MKDIR / MKNOD / MKFIFO / MKSOCK /
 * SYMLINK / RENAME / LINK / UNLINK / RMDIR / WRITE / CLONE / TRUNCATE /
 * CHMOD / CHOWN / UTIMES / FALLOCATE / ENCODED_WRITE (none, zlib, zstd and
 * the btrfs LZO framing for 4K..64K sectors) into an in-memory tree, then
 * lists every surviving name as a record.  File contents are rebuilt on
 * extraction by applying that file's operations in stream order.
 *
 * Symlinks are extracted as plain files holding their target; device,
 * FIFO and socket nodes are listed but produce no output.  An incremental
 * stream yields only what it changes: data the parent snapshot held reads
 * back as zeros.  A CLONE whose source lies in another subvolume cannot be
 * resolved, and extraction of that file fails.
 */
typedef struct xx_btrfs_stream {
    Abstractformat format;
    uint32_t version;
    uint64_t number_of_records;
    uint64_t number_of_commands;
    /** Bytes from the stream start through END (or the last good command). */
    int64_t stream_size;
    /** The END command was reached. */
    bool has_end;
    /** The first command is SNAPSHOT (incremental send). */
    bool incremental;
    /** A command was skipped, or parsing stopped at a bad command. */
    bool damaged;
} xx_btrfs_stream;

typedef xx_btrfs_stream xx_btrfs_stream_t;

XXFC_API void xx_btrfs_stream_init(xx_btrfs_stream *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_btrfs_stream *xx_btrfs_stream_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_btrfs_stream_destroy(xx_btrfs_stream *archive);
XXFC_API void xx_btrfs_stream_free(xx_btrfs_stream *archive);

XXFC_API bool xx_btrfs_stream_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_btrfs_stream_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_btrfs_stream_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_btrfs_stream_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_btrfs_stream_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_btrfs_stream_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_btrfs_stream_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_btrfs_stream_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_btrfs_stream_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Raw CRC-32C as the send stream uses it: seed @p crc (0 to start),
 * no pre- or post-inversion.
 */
XXFC_API uint32_t xx_btrfs_stream_crc32c(uint32_t crc, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BTRFS_STREAM_H */
