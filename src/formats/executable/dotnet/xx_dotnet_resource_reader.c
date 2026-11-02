/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
/* A core metadata reader, independent of optional format-resource streams. */
#include "xxfclib/formats/executable/dotnet/xx_dotnet_resources.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet_edit.h"
#include <stdlib.h>
#include <string.h>

#define RESOURCE_BOUND UINT32_C(0x10000)
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int range(size_t size, size_t at, size_t n) { return at <= size && n <= size - at; }
static bool fail(const char **error, const char *why) { if (error) *error = why; return false; }
static bool seven(const uint8_t *data, size_t end, size_t *at, uint32_t *value) {
    unsigned i; uint32_t n = 0;
    for (i = 0; i < 5; ++i) {
        uint8_t b;
        if (*at >= end) return false;
        b = data[(*at)++];
        if (i == 4 && b > 15) return false;
        n |= (uint32_t)(b & 127) << (i * 7);
        if (!(b & 128)) { *value = n; return true; }
    }
    return false;
}
bool xx_dotnet_resources_entry(const xx_dotnet_resources *v, uint32_t index,
    xx_dotnet_resource_entry *entry, const char **error)
{
    size_t at, payload, length; uint32_t name_size, relative, type, encoded_length;
    static const uint8_t widths[17] = {0,0,1,2,1,1,2,2,4,4,8,8,4,8,16,8,8};
    if (error) *error = NULL;
    if (entry) memset(entry, 0, sizeof(*entry));
    if (!v || !entry || !v->data || v->size > XX_DOTNET_EDIT_MAX_IMAGE || index >= v->count ||
        v->count > RESOURCE_BOUND || v->names > v->data_start || v->data_start > v->size ||
        !range(v->size,v->positions,(size_t)v->count*4)) return fail(error,"invalid resource entry arguments");
    relative = u32(v->data + v->positions + (size_t)index * 4);
    if (relative > v->data_start - v->names) return fail(error,"resource name offset exceeds name section");
    at = v->names + relative;
    if (!seven(v->data,v->data_start,&at,&name_size) || name_size > RESOURCE_BOUND ||
        (name_size & 1) || !range(v->data_start,at,(size_t)name_size+4)) return fail(error,"invalid resource name extent");
    relative = u32(v->data + at + name_size);
    if (relative > v->size - v->data_start) return fail(error,"resource value offset exceeds data section");
    payload = v->data_start + relative;
    if (!seven(v->data,v->size,&payload,&type)) return fail(error,"truncated resource type code");
    if (type < 17) {
        length = widths[type];
        if (type == 1) {
            size_t units;
            if (!seven(v->data,v->size,&payload,&encoded_length)) return fail(error,"truncated resource string length");
            length = encoded_length;
            if (!range(v->size,payload,length) || !xx_dotnet_utf8_to_utf16(v->data+payload,length,NULL,0,&units))
                return fail(error,"invalid resource string");
        }
    } else if (type == 32 || type == 33) {
        if (!range(v->size,payload,4)) return fail(error,"truncated byte-array/stream resource length");
        length = u32(v->data+payload); payload += 4;
    } else return fail(error,"serialized or unknown resource types are unsupported");
    if (!range(v->size,payload,length)) return fail(error,"resource value exceeds input");
    entry->name_utf16le = v->data + at; entry->name_size = name_size;
    entry->type = type; entry->data = v->data + payload; entry->size = length; entry->offset = payload;
    return true;
}
bool xx_dotnet_resources_open(const uint8_t *data, size_t size, xx_dotnet_resources *view, const char **error)
{
    xx_dotnet_resources parsed = {0}; size_t at, positions; uint32_t i, types, length;
    if (error) *error = NULL;
    if (view) memset(view,0,sizeof(*view));
    if (!view || !data || size > XX_DOTNET_EDIT_MAX_IMAGE || size < 12 ||
        u32(data) != UINT32_C(0xbeefcace) || u32(data+4) != 1) return fail(error,"invalid .resources header");
    at = 12;
    if (!range(size,at,u32(data+8))) return fail(error,"resource manager header exceeds input");
    at += u32(data+8);
    if (!range(size,at,12) || u32(data+at) != 2) return fail(error,"only ResourceWriter version 2 is supported");
    parsed.count = u32(data+at+4); types = u32(data+at+8); at += 12;
    if (parsed.count > RESOURCE_BOUND || types > RESOURCE_BOUND) return fail(error,"resource count exceeds bound");
    for (i=0;i<types;++i) {
        if (!seven(data,size,&at,&length) || length > RESOURCE_BOUND || !range(size,at,length)) return fail(error,"invalid resource type name extent");
        at += length;
    }
    at = (at+7) & ~(size_t)7;
    if (!range(size,at,(size_t)parsed.count*8+4)) return fail(error,"truncated resource lookup tables");
    positions = at + (size_t)parsed.count*4;
    at += (size_t)parsed.count*8;
    parsed.data_start = u32(data+at); parsed.names = at+4;
    if (parsed.data_start < parsed.names || parsed.data_start > size) return fail(error,"invalid resource data section offset");
    parsed.positions = positions; parsed.data = data; parsed.size = size;
    for(i=0;i<parsed.count;++i) {
        xx_dotnet_resource_entry entry;
        if (!xx_dotnet_resources_entry(&parsed,i,&entry,error)) return false;
    }
    *view = parsed; return true;
}
bool xx_dotnet_resources_find(const xx_dotnet_resources *view, const uint8_t *key, size_t key_size,
    xx_dotnet_resource_entry *entry, const char **error)
{
    uint16_t *units; size_t count=0,i; uint32_t index; bool found=false;
    if (error) *error = NULL;
    if (entry) memset(entry,0,sizeof(*entry));
    if (!view || !view->data || view->size > XX_DOTNET_EDIT_MAX_IMAGE ||
        view->count > RESOURCE_BOUND || view->names > view->data_start || view->data_start > view->size ||
        !range(view->size,view->positions,(size_t)view->count*4)) return fail(error,"invalid resource view");
    if (!view || !entry || key_size > RESOURCE_BOUND || !xx_dotnet_utf8_to_utf16(key,key_size,NULL,0,&count)) return fail(error,"invalid resource key");
    units = malloc((count?count:1)*sizeof(*units));
    if (!units) return fail(error,"resource key allocation failed");
    if (!xx_dotnet_utf8_to_utf16(key,key_size,units,count,&count)) {free(units);return fail(error,"invalid resource key");}
    for(index=0;index<view->count;++index) {
        xx_dotnet_resource_entry item;
        if (!xx_dotnet_resources_entry(view,index,&item,error)) break;
        if (item.name_size != count*2) continue;
        for(i=0;i<count;++i) if (units[i] != ((uint16_t)item.name_utf16le[i*2] | (uint16_t)item.name_utf16le[i*2+1]<<8)) break;
        if (i==count) {*entry=item;found=true;break;}
    }
    free(units); return found;
}
