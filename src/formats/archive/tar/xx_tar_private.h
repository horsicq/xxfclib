/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reader-private declarations: shared by the reader's own units and, where
 * a DataStructs script needs reader state, src/datastructs/xx_xxds_readers.c.
 */
#ifndef XX_TAR_PRIVATE_H
#define XX_TAR_PRIVATE_H
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/formats/archive/tar/xx_tar.h"
#include "xx_tar_defs.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct xx_tar_sparse_extent_s {
    uint64_t offset;
    uint64_t size;
    int64_t packed_offset;
} xx_tar_sparse_extent;

typedef struct xx_tar_member_s {
    int64_t header_offset;
    int64_t data_offset;
    int64_t data_size;
    int64_t padded_size;
    uint64_t mode;
    uint64_t uid;
    uint64_t gid;
    uint64_t mtime;
    char typeflag;
    char *name;
    char *linkname;
    xx_tar_sparse_extent *sparse_extents;
    size_t sparse_count;
    uint64_t logical_size;
    bool sparse;
    bool metadata;
    bool directory;
    bool regular;
    /* In a cut prefix, the cut runs through its data or padding. */
    bool at_cut;
    /* In a cut prefix, its header or data ends past what a check of the
     * stream vouches for: it is read without one, as GNU tar reads it. */
    bool unverified;
} xx_tar_member;

typedef struct xx_tar_private_s {
    xx_tar_member *members;
    size_t count;
    size_t capacity;
    uint64_t visible_count;
    size_t sparse_extent_total;
    int64_t end_marker_offset;
    int64_t archive_end;
    /* What a hard link names is the path a member extracts to
     * (xx_tar_safe_output_name), so "/f", "./f" and "f" are one name.  The
     * first hard link lookup builds that path for every member (NULL when
     * it has none), the members that have one in path order, then archive
     * order, and link_source, what each hard link resolves to. */
    size_t *name_order;
    size_t name_order_count;
    char **output_names;
    size_t *link_source;
    /* The last member unpacked is unverified, or a hard link to one. */
    bool last_unverified;
    /* The last member unpacked is the one the cut runs through, or a hard
     * link to it. */
    bool last_cut;
} xx_tar_private;

bool xx_tar_read_exact_at(xx_io_device *device, int64_t offset, void *buffer, size_t size);

#endif
