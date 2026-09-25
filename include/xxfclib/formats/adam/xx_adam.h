/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_adam.h @brief Coleco Adam disk image (EOS filesystem) reader. */

#ifndef XXFCLIB_FORMAT_ADAM_H
#define XXFCLIB_FORMAT_ADAM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest volume name: the 12-byte name field without its ETX. */
#define XX_ADAM_VOLUME_NAME_FIELD 12

/**
 * @brief A Coleco Adam .dsk image holding an EOS (Elementary Operating
 * System) filesystem.
 *
 * Image: a raw sector dump of 163840 (5.25" SSDD), 327680 (5.25" DSDD),
 * 737280 (3.5" DD) or 1474560 (3.5" HD) bytes.  EOS addresses 1024-byte
 * blocks; block B is made of the 512-byte image sectors 2B and (2B) XOR 5,
 * which is the drive's (1,6,3,8,5,2,7,4) interleave as .dsk images store it
 * for every capacity.  So block 0 is sectors 0+5, block 1 sectors 2+7,
 * block 2 sectors 4+1, block 3 sectors 6+3, and so on in groups of eight.
 *
 * Block 0 is the boot block.  The directory starts in block 1 and is a run
 * of 26-byte entries, never straddling a block (39 slots per block).  Slot 0
 * of block 1 is the volume descriptor:
 *   0   char[12] volume name, ETX (0x03) terminated unless 12 long
 *   12  u8  directory size in blocks (bits 0..6); bit 7 = protected
 *   13  u32 0xFF00AA55 (bytes 55 AA 00 FF), the EOS directory check
 *   17  u32 volume size in blocks
 *   23  u8[3] BCD year, month, day
 * Every other slot is a file entry (all integers little endian):
 *   0   char[12] name: the name, then ONE file-type byte, then ETX (0x03)
 *       unless the 12 bytes are full; the type is 'A', 'H', 'a', 'h', 'C'
 *       or 0x02
 *   12  u8  attributes: 0x80 permanent, 0x40 write-protected, 0x20
 *       read-protected, 0x10 user, 0x08 system, 0x04 deleted, 0x02
 *       execute-protected, 0x01 "BLOCKS LEFT" hole = end of the directory
 *   13  u32 first block
 *   17  u16 allocated blocks
 *   19  u16 used blocks
 *   21  u16 bytes used in the last block (clamped to 1024)
 *   23  u8[3] BCD year, month, day
 * A file occupies used blocks consecutively from its first block; its size
 * is (used - 1) * 1024 + last count, or 0 when nothing is used.  A freshly
 * formatted EOS directory holds BOOT (block 0), DIRECTORY (block 1) and the
 * BLOCKS LEFT hole.  BOOT and DIRECTORY carry no file-type byte and are
 * bookkeeping, so - like deleted entries - they are not members.
 *
 * Detection has no magic in the first 64 bytes (block 0 is boot code), so it
 * is a late probe: an image of one of the four sizes (or, inside a larger
 * device, the volume size of the descriptor names one of them), then the
 * directory check and the BOOT and DIRECTORY entries of block 1, i.e. image
 * bytes 0x40D..0x444, which EOS formatters always write.  The directory size
 * must fit the volume.
 *
 * Members are the live entries with a valid type byte, in directory order.
 * The name is "<name>.<type>" ("STX" for type 0x02, e.g. HELLO.A, GAME.H,
 * RUN.STX).  Bytes outside printable ASCII and the characters / \ : * ? " <
 * > | ~ become '_'; an empty name becomes "_"; a Windows device stem (CON,
 * PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$, CLOCK$) gets a '_' in
 * front.  A name that repeats an earlier one, compared case-insensitively,
 * gets "~<entry index>" before its type suffix; '~' never survives from the
 * disk, so every member name is unique.  An entry whose blocks run past the
 * end of the volume is listed, but refuses to unpack.
 */
typedef struct xx_adam {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t image_size;          /**< Bytes of the image, 160K..1.44M. */
    uint32_t volume_blocks;      /**< image_size / 1024. */
    uint32_t declared_blocks;    /**< Volume size field of the descriptor. */
    uint32_t directory_blocks;   /**< 1..127. */
    char volume_name[XX_ADAM_VOLUME_NAME_FIELD + 1];
} xx_adam;

typedef xx_adam xx_adam_t;

XXFC_API void xx_adam_init(xx_adam *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_adam *xx_adam_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_adam_destroy(xx_adam *archive);
XXFC_API void xx_adam_free(xx_adam *archive);

XXFC_API bool xx_adam_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_adam_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_adam_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_adam_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_adam_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_adam_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_adam_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_adam_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_adam_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ADAM_H */
