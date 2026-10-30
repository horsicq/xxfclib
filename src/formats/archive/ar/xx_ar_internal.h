/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_AR_INTERNAL_H
#define XXFCLIB_AR_INTERNAL_H

/* Reader state and helpers shared by xx_ar.c and xx_ar_datastructs.c. */

#include "xxfclib/formats/xx_format.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum xx_ar_kind_e {
    XX_AR_KIND_PLAIN = 0,
    XX_AR_KIND_DEB,  /* first member "debian-binary" */
    XX_AR_KIND_MSLIB /* two leading "/" linker members (Microsoft .lib) */
} xx_ar_kind;

typedef struct xx_ar_member_s {
    int64_t header_offset;
    int64_t stored_data_offset;
    int64_t data_offset;
    int64_t stored_size;
    int64_t data_size;
    uint64_t name_prefix_size;
    uint64_t timestamp;
    uint64_t owner_id;
    uint64_t group_id;
    uint64_t mode;
    char *name;         /* resolved member name (renamed when duplicated) */
    char *extract_name; /* relative output path, NULL when refused */
    bool special;
} xx_ar_member;

typedef struct xx_ar_members_s {
    xx_ar_member *items;
    size_t count;
    uint64_t visible_count;
    int64_t archive_end;
    xx_ar_kind kind;
} xx_ar_members;

/* What xx_ar_parse_archive builds beyond the member walk. */
#define XX_AR_PARSE_MEMBERS 1U       /* member table with resolved names */
#define XX_AR_PARSE_EXTRACT_NAMES 2U /* plus unique extraction paths */

bool xx_ar_read_exact_at(xx_io_device *device, int64_t offset, void *buffer, size_t size);
void xx_ar_members_cleanup(xx_ar_members *members);
bool xx_ar_parse_archive(Abstractformat *self, xx_ar_members *members, unsigned flags, xx_pd_struct *pd);

#endif /* XXFCLIB_AR_INTERNAL_H */
