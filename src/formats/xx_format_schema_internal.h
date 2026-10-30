/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Internal bounded on-disk schema helpers, shared by the engine
 * (xx_format_datastructs.c) and every schema family unit and per-format data
 * file. All of them are compiled as one translation unit: the engine includes
 * them through xx_format_schema_units.inc, so the helpers below stay static.
 */
#ifndef XX_FORMAT_SCHEMA_INTERNAL_H
#define XX_FORMAT_SCHEMA_INTERNAL_H
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/global/xx_global.h"
#include <limits.h>
#include <string.h>
#define SX_LE 0
#define SX_BE 1
#define SX_FILE 2
#define SX_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define SX_FIELD(n,t,o,s,p) {L##n,L##t,(o),(s),(p)}
#define SX_NONE XX_DATA_STRUCT_RECORD_PROPERTY_NONE
#define SX_ID XX_DATA_STRUCT_RECORD_PROPERTY_ID
#define SX_ADDRESS XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS
#define SX_OFFSET XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET
#define SX_SIZE XX_DATA_STRUCT_RECORD_PROPERTY_SIZE
#define SX_COUNT_PROP XX_DATA_STRUCT_RECORD_PROPERTY_COUNT
#define SX_POINTER XX_DATA_STRUCT_RECORD_PROPERTY_POINTER
#define SX_FLAGS XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS
#define SX_TIME XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP
#define SX_STRING XX_DATA_STRUCT_RECORD_PROPERTY_STRING
#define SX_RESERVED XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED
#define SX_SIGNATURE XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE
#define SX_CHECKSUM XX_DATA_STRUCT_RECORD_PROPERTY_CHECKSUM

typedef struct sx_layout {
    uint32_t id;
    const char *name;
    const xx_data_struct_field_desc *fields;
    size_t field_count;
    int endian;
} sx_layout;
typedef struct sx_context {
    Abstractformat *f;
    Abstractformat *owned_reader; /* Canonical alias reader, owned until state cleanup. */
    xx_pd_struct *pd;
    int64_t size;
    xx_data_struct *items;
    size_t count, capacity;
    bool failed;
} sx_context;
/* One schema family: the formats it describes, how to walk them, the layouts
 * it can name and the record comments it adds. Each family unit defines one
 * as sx_<family>_family; xx_format_schema_families.inc lists them in order. */
typedef struct sx_family {
    bool (*supports)(xx_file_type_t type);
    bool (*build)(sx_context *ctx);
    const sx_layout *const *layouts;
    size_t layout_count;
    bool (*comment)(xx_data_struct_record *record, uint32_t id);
} sx_family;
/* Focused emulator and DIE engine profiles omit the reader factory and the
 * canonical readers, so alias formats keep only their standalone schemas
 * instead of pulling in the full registry link closure. */
#ifndef SX_ALIAS_READER_FACTORY
#if defined(XXFC_PROFILE_APK_EMUL) || defined(XXFC_PROFILE_DOTNET_EMUL) || defined(XXFC_PROFILE_DIE_ENGINE)
#define SX_ALIAS_READER_FACTORY 0
#else
#define SX_ALIAS_READER_FACTORY 1
#endif
#endif
/* Offsets passed below are relative to f->base_address. The emitted descriptor
 * has an absolute device offset, matching the public data-structure contract.
 * Reads and additions check cancellation, arithmetic and the device bounds.
 * Builders return false on malformed structure or incomplete/cancelled walk.
 * Layout IDs must be globally unique (image 0x6001..., container 0x6002...). */
static bool sx_read(sx_context *ctx, int64_t off, void *out, size_t n);
static bool sx_u16(sx_context *ctx, int64_t off, bool be, uint16_t *out);
static bool sx_u32(sx_context *ctx, int64_t off, bool be, uint32_t *out);
static bool sx_u64(sx_context *ctx, int64_t off, bool be, uint64_t *out);
static bool sx_add(sx_context *ctx, const sx_layout *layout, int64_t off, int64_t size);
static bool sx_raw(sx_context *ctx, int64_t off, int64_t size);
static bool sx_table(sx_context* ctx, const sx_layout* layout, int64_t off, int64_t entry_size, uint64_t count);

static inline const wchar_t* sx_wcsstr(const wchar_t* h, const wchar_t* n)
{
  size_t i, j;
  if (!h || !n) return NULL;
  if (!*n) return h;
  for (i = 0; h[i]; i++) {
    for (j = 0; n[j] && h[i + j] == n[j]; j++) {}
    if (!n[j]) return h + i;
  }
  return NULL;
}

static inline void sx_wappend(wchar_t* dst, size_t cap, const wchar_t* src)
{
  size_t n = 0;
  while (n < cap && dst[n]) n++;
  if (n >= cap) return;
  while (*src && n + 1 < cap) dst[n++] = *src++;
  dst[n] = 0;
}

#endif
