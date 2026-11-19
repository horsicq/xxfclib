/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reader-private declarations: shared by the reader's own units and, where
 * a DataStructs script needs reader state, src/datastructs/xx_xxds_readers.c.
 */
#ifndef XX_AR_PRIVATE_H
#define XX_AR_PRIVATE_H
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/archive/ar/xx_ar.h"
#include "xx_ar_defs.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define XX_AR_PARSE_MEMBERS 1U       /* member table with resolved names */

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

bool xx_ar_read_exact_at(xx_io_device *device, int64_t offset, void *buffer, size_t size);
void xx_ar_members_cleanup(xx_ar_members *members);
bool xx_ar_parse_archive(Abstractformat *self, xx_ar_members *members, unsigned flags, xx_pd_struct *pd);

#endif
