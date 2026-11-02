/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_DOTNET_RESOURCES_H
#define XXFCLIB_DOTNET_RESOURCES_H
#include "xxfclib/xxfc_defs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Borrowed, bounded ResourceWriter v2 view. No constructor or serialized
 * object is instantiated. Keep input unchanged and alive while using it. */
typedef struct xx_dotnet_resources {
    const uint8_t *data;
    size_t size, positions, names, data_start;
    uint32_t count;
} xx_dotnet_resources;
typedef struct xx_dotnet_resource_entry {
    const uint8_t *name_utf16le;
    size_t name_size; /* Bytes, not UTF16 units. Lone surrogates are preserved. */
    uint32_t type; /* ResourceTypeCode: 0..16 primitives, 32 byte[], 33 Stream. */
    const uint8_t *data;
    size_t size, offset; /* Payload excluding the type and length prefixes. */
} xx_dotnet_resource_entry;

/* Input <=256 MiB; <=65536 entries and <=65536 bytes per name/type name.
 * v1 and user-serialized object values are unsupported. Failure clears output;
 * optional error points to static text. open validates all entry extents. */
XXFC_API bool xx_dotnet_resources_open(const uint8_t *data, size_t size,
    xx_dotnet_resources *view, const char **error);
XXFC_API bool xx_dotnet_resources_entry(const xx_dotnet_resources *view, uint32_t index,
    xx_dotnet_resource_entry *entry, const char **error);
/* UTF8 key; NULL error on a valid missing key, non-NULL on malformed input or
 * invalid UTF8. An empty key is supported. No case folding/normalization. */
XXFC_API bool xx_dotnet_resources_find(const xx_dotnet_resources *view,
    const uint8_t *key_utf8, size_t key_size, xx_dotnet_resource_entry *entry, const char **error);

#ifdef __cplusplus
}
#endif
#endif
