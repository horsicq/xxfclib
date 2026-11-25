/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SCAN_FLOPPY_INTERNAL_H
#define XX_SCAN_FLOPPY_INTERNAL_H

#include "xxfclib/scan/xx_scan.h"

/* Floppy disk images as the scan engines open them: a raw FAT12/FAT16 image
 * (Atari ST .st, MSX and PC .img/.ima) and an Atari MSA image (Magic Shadow
 * Archiver, per-track RLE). The rules here are the ones Qt's XFATImage and
 * XMSA follow line for line (XArchive/diskimages/xfatimage.cpp, xmsa.cpp), so
 * both engines type the same files and list the same members in the same
 * order. They are deliberately narrower than the general FAT reader
 * (formats/filesystem/fat): no FAT32, no long names, no truncated volumes.
 *
 * Raw FAT image. Nothing but the BPB and the root directory identifies it:
 * Atari boot sectors carry no 0x55AA, often no media byte, and many images
 * have zeroed FAT ids.
 *   - bytes/sector (+11) is 512; sectors/cluster (+13) is a power of two up
 *     to 64; reserved sectors (+14) of 0 reads as 1 (some Atari formatters);
 *     1 or 2 FATs (+16); root entries (+17) non-zero and a multiple of 16;
 *     total sectors (+19, or +32 when that is 0) and sectors/FAT (+22) non-zero
 *   - reserved + FATs + root is below the total, the cluster count is
 *     1..65524 (below 4085: FAT12, else FAT16) and one FAT holds that many
 *     entries
 *   - the BPB volume is within half and five quarters of the image size, and
 *     cluster 2 lies inside the image
 *   - the root directory, up to its end marker, has at least one live entry
 *     and at most one impossible entry per four live ones. An entry is
 *     impossible with attribute bits 0xC0, both the directory and the label
 *     bit, a name byte below 0x20 or one of 0x7F " * / : < > ? \ |, a leading
 *     space, a start cluster outside the volume, or a size above the data
 *     area. Deleted entries and long-name pieces do not count.
 *
 * Members. The tree is walked in directory order, a subdirectory where its
 * entry stands, up to 8 levels deep; every directory cluster is read once
 * (a cycle ends there) and at most 2 MiB of one directory. Listed are files
 * with a non-zero size whose cluster chain covers the size inside the image;
 * labels, deleted entries, long-name pieces, "." entries and directories
 * themselves are not. The name is the 8.3 short name, base and extension
 * right-trimmed, bytes outside 0x20..0x7E and the separators / \ shown as '_',
 * joined to its directories with '/'. The walk ends after 10,000 members or
 * when the listed sizes would pass twice the data area.
 * A file whose clusters follow one another is a stored extent of the image
 * (XFATImage: a "Store" record at the offset of its first cluster). A
 * fragmented file has no extent: XFATImage publishes it as an index-paired
 * archive-stream record, and both engines then print offset 0 with its size.
 *
 * MSA. Big-endian words: 0x0E0F, sectors per track (1..36), sides - 1 (0..1),
 * first track, last track (first <= last <= 85); then for each track and side
 * a length word and that many bytes: a track of sectors * 512 bytes as it is,
 * a shorter one RLE packed (0xE5, value, big-endian run length; other bytes
 * literal). The file is an MSA when every length word is 1..track size and
 * its data lies inside the file. Its single member "msa-content" is the
 * decoded image (an archive-stream record without an extent: offset 0 and
 * the decoded size are printed); a run that is empty or leaves the track, or
 * a track that does not decode to its exact size, fails the member. */

typedef struct xx_scan_fat_record {
    char *name;       /* '/'-joined path inside the volume. */
    int64_t offset;   /* Image offset of the first cluster. */
    uint32_t size;    /* File size in bytes. */
    uint32_t cluster; /* First cluster. */
    bool contiguous;  /* Its clusters follow one another: offset/size is its extent. */
} xx_scan_fat_record;

typedef struct xx_scan_fat_image xx_scan_fat_image;

/* The device cursor is not preserved by any of these. */
bool xx_scan_floppy_is_fat(xx_io_device *device, int64_t size);
bool xx_scan_floppy_is_msa(xx_io_device *device, int64_t size);

/* A cheap superset of the two checks above over the first bytes of a file
 * (512 or more for a FAT image): whether an archive member can be one. */
bool xx_scan_floppy_head_may_be_image(const uint8_t *data, size_t size);

/* NULL when the device is no FAT image or cannot be read; *oom is set when
 * memory ran out instead. */
xx_scan_fat_image *xx_scan_fat_open(xx_io_device *device, int64_t size, bool *oom);
size_t xx_scan_fat_count(const xx_scan_fat_image *image);
const xx_scan_fat_record *xx_scan_fat_record_at(const xx_scan_fat_image *image, size_t index);
/* Write the record's bytes to destination. False when a cluster cannot be
 * read or the destination refuses a write. */
bool xx_scan_fat_read(xx_scan_fat_image *image, size_t index, xx_io_device *destination);
void xx_scan_fat_close(xx_scan_fat_image *image);

/* packed_size: bytes of track records after the 10-byte header; image_size:
 * decoded bytes. */
bool xx_scan_msa_info(xx_io_device *device, int64_t size, int64_t *packed_size, int64_t *image_size);
/* Decode all tracks to destination. *oom as above. */
bool xx_scan_msa_decode(xx_io_device *device, int64_t size, xx_io_device *destination, bool *oom);

#endif /* XX_SCAN_FLOPPY_INTERNAL_H */
