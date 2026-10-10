/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ren'Py RPA archive.  xx_rpa.h carries the layout.
 *
 * The index is a Python pickle.  It is decoded by a small interpreter that
 * only builds data (dicts, lists, tuples, ints, str and bytes) and accepts
 * exactly two globals, builtins.bytes and _codecs.encode, which is how
 * Python 3 writes bytes at protocol 2; any other global, object
 * construction or unknown opcode rejects the archive.  Every table it keeps
 * grows under one byte budget, every count is bounded by the pickle's own
 * length, and the walk over the result is a fixed three levels deep, so a
 * self-referencing list built through the memo cannot make it loop.
 *
 * The case-folding used to keep member names distinct on case-insensitive
 * file systems follows xx_godot_engine_pck.c of this library (MIT).
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rpa/xx_rpa.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: the alias macro next to the enumerator in
 * xxfc_defs.h is tested, so the real file type is picked up as soon as RPA
 * is registered there. */
#ifdef RPA
#define XX_RPA_FILE_TYPE XX_FILE_TYPE_RPA
#else
#define XX_RPA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define RPA_LINE_MAX 128U
#define RPA_ALT_KEY UINT64_C(0xDABE8DF0)
/* Compressed index read at most; a real index of tens of thousands of
 * members is a few MiB. */
#define RPA_MAX_PACKED ((size_t)32U * 1024U * 1024U)
/* Decompressed pickle at most. */
#define RPA_MAX_PLAIN ((size_t)64U * 1024U * 1024U)
/* Everything the index decode allocates together (packed, plain, the
 * interpreter's tables, the member table), unless a lower
 * XX_META_ID_OPT_MEMORY_LIMIT is set. */
#define RPA_BUDGET ((size_t)128U * 1024U * 1024U)
#define RPA_MAX_MEMBERS 1000000U
#define RPA_MAX_NAME 4096U
#define RPA_MAX_SUFFIX_TRIES 100000U
#define RPA_MARK UINT32_MAX

/* ------------------------------------------------------------------ io -- */

static bool rpa_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    const size_t io_capacity = xx_get_file_buffer_size();
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (io_capacity && request > io_capacity) request = io_capacity;
        amount = xx_io_read(device, (uint8_t *)buffer + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

/* A growable memory sink for the inflater, refusing to pass @limit. */
typedef struct rpa_sink {
    uint8_t *data;
    size_t size, capacity, position, limit;
    bool overflow;
} rpa_sink;

static ssize_t rpa_sink_write(xx_io_device *device, const void *buffer, size_t size)
{
    rpa_sink *sink = (rpa_sink *)device->priv;
    size_t end;
    if (!sink || (!buffer && size)) return -1;
    if (size > sink->limit || sink->position > sink->limit - size) {
        sink->overflow = true;
        return -1;
    }
    end = sink->position + size;
    if (end > sink->capacity) {
        size_t capacity = sink->capacity ? sink->capacity : 65536U;
        uint8_t *grown;
        while (capacity < end) capacity = capacity > sink->limit / 2U ? sink->limit : capacity * 2U;
        grown = (uint8_t *)xx_mem_realloc(sink->data, capacity);
        if (!grown) {
            sink->overflow = true;
            return -1;
        }
        sink->data = grown;
        sink->capacity = capacity;
    }
    if (size) xx_rt_memcpy(sink->data + sink->position, buffer, size);
    sink->position = end;
    if (end > sink->size) sink->size = end;
    return (ssize_t)size;
}

static ssize_t rpa_sink_read(xx_io_device *device, void *buffer, size_t size)
{
    rpa_sink *sink = (rpa_sink *)device->priv;
    size_t amount;
    if (!sink || (!buffer && size)) return -1;
    if (sink->position >= sink->size) return 0;
    amount = sink->size - sink->position;
    if (amount > size) amount = size;
    if (amount) xx_rt_memcpy(buffer, sink->data + sink->position, amount);
    sink->position += amount;
    return (ssize_t)amount;
}

static int rpa_sink_seek64(xx_io_device *device, int64_t offset, int whence)
{
    rpa_sink *sink = (rpa_sink *)device->priv;
    int64_t base;
    if (!sink) return -1;
    base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int64_t)sink->position : whence == SEEK_END ? (int64_t)sink->size : -1;
    if (base < 0 || (offset < 0 && -offset > base) || (offset > 0 && (uint64_t)offset > (uint64_t)sink->limit - (uint64_t)base)) return -1;
    sink->position = (size_t)(base + offset);
    return 0;
}

static int rpa_sink_seek(xx_io_device *device, long offset, int whence)
{
    return rpa_sink_seek64(device, (int64_t)offset, whence);
}

static int64_t rpa_sink_tell(xx_io_device *device)
{
    rpa_sink *sink = (rpa_sink *)device->priv;
    return sink ? (int64_t)sink->position : -1;
}

static int64_t rpa_sink_total(xx_io_device *device)
{
    rpa_sink *sink = (rpa_sink *)device->priv;
    return sink ? (int64_t)sink->size : -1;
}

static int rpa_sink_close(xx_io_device *device)
{
    (void)device;
    return 0;
}

/* ------------------------------------------------------------- pickle -- */

enum {
    RPO_NONE = 1,
    RPO_OTHER, /* bools and ints too wide for 64 bits */
    RPO_INT,
    RPO_BYTES,
    RPO_TEXT,
    RPO_LIST,
    RPO_TUPLE,
    RPO_DICT,
    RPO_FN_BYTES,
    RPO_FN_ENCODE
};

/* 24 bytes: an int keeps its 64-bit value in offset (low) and length
 * (high); plain and blob both stay far below 4 GiB. */
typedef struct rpa_obj {
    uint8_t type;
    uint8_t in_blob;
    uint16_t reserved;
    uint32_t count; /* container items (dict: keys + values) */
    uint32_t head;  /* first cell, 1-based; 0 = none */
    uint32_t tail;
    uint32_t offset; /* string start in plain or blob */
    uint32_t length; /* string bytes */
} rpa_obj;

typedef struct rpa_cell {
    uint32_t value;
    uint32_t next;
} rpa_cell;

typedef struct rpa_vm {
    const uint8_t *code;
    size_t size, at;
    rpa_obj *objs;
    uint32_t nobj, capobj;
    rpa_cell *cells;
    uint32_t ncell, capcell;
    uint32_t *stack;
    uint32_t nstack, capstack;
    uint32_t *memo;
    uint32_t capmemo, nextmemo;
    uint8_t *blob;
    size_t nblob, capblob;
    size_t budget; /* bytes still allowed */
    uint32_t result;
    xx_pd_struct *pd;
} rpa_vm;

static bool rpa_vm_grow(rpa_vm *vm, void **array, uint32_t *capacity, size_t element, uint64_t need)
{
    uint64_t cap = *capacity ? *capacity : 64U;
    void *grown;
    size_t add;
    if (need <= *capacity) return true;
    if (need > UINT32_MAX - 1U) return false;
    while (cap < need) cap *= 2U;
    if (cap > UINT32_MAX - 1U) cap = UINT32_MAX - 1U;
    if (cap > ((uint64_t)SIZE_MAX) / element) return false;
    add = (size_t)(cap - *capacity) * element;
    if (add > vm->budget) return false;
    grown = xx_mem_realloc(*array, (size_t)cap * element);
    if (!grown) return false;
    vm->budget -= add;
    *array = grown;
    *capacity = (uint32_t)cap;
    return true;
}

static uint32_t rpa_new(rpa_vm *vm, uint8_t type)
{
    rpa_obj *obj;
    if (!rpa_vm_grow(vm, (void **)&vm->objs, &vm->capobj, sizeof(rpa_obj), (uint64_t)vm->nobj + 1U)) return 0U;
    obj = &vm->objs[vm->nobj];
    xx_mem_zero(obj, sizeof(*obj));
    obj->type = type;
    return vm->nobj++;
}

static bool rpa_push(rpa_vm *vm, uint32_t value)
{
    if (value == 0U) return false;
    if (!rpa_vm_grow(vm, (void **)&vm->stack, &vm->capstack, sizeof(uint32_t), (uint64_t)vm->nstack + 1U)) return false;
    vm->stack[vm->nstack++] = value;
    return true;
}

static bool rpa_push_new(rpa_vm *vm, uint8_t type)
{
    return rpa_push(vm, rpa_new(vm, type));
}

/* Pop one value (never a mark). */
static uint32_t rpa_pop(rpa_vm *vm)
{
    uint32_t value;
    if (!vm->nstack) return 0U;
    value = vm->stack[vm->nstack - 1U];
    if (value == RPA_MARK) return 0U;
    --vm->nstack;
    return value;
}

static uint32_t rpa_top(rpa_vm *vm)
{
    if (!vm->nstack || vm->stack[vm->nstack - 1U] == RPA_MARK) return 0U;
    return vm->stack[vm->nstack - 1U];
}

/* Index of the topmost mark, or -1. */
static int64_t rpa_find_mark(rpa_vm *vm)
{
    uint32_t index = vm->nstack;
    while (index) {
        --index;
        if (vm->stack[index] == RPA_MARK) return (int64_t)index;
    }
    return -1;
}

static bool rpa_add_cell(rpa_vm *vm, uint32_t container, uint32_t value)
{
    rpa_obj *obj;
    rpa_cell *cell;
    if (value == 0U || value == RPA_MARK) return false;
    if (!rpa_vm_grow(vm, (void **)&vm->cells, &vm->capcell, sizeof(rpa_cell), (uint64_t)vm->ncell + 2U)) return false;
    if (vm->ncell == 0U) vm->ncell = 1U; /* cell 0 is "none" */
    cell = &vm->cells[vm->ncell];
    cell->value = value;
    cell->next = 0U;
    obj = &vm->objs[container];
    if (obj->count == UINT32_MAX) return false;
    if (obj->tail) vm->cells[obj->tail].next = vm->ncell;
    else obj->head = vm->ncell;
    obj->tail = vm->ncell;
    ++obj->count;
    ++vm->ncell;
    return true;
}

static const uint8_t *rpa_str(const rpa_vm *vm, const rpa_obj *obj)
{
    return obj->in_blob ? vm->blob + obj->offset : vm->code + obj->offset;
}

static bool rpa_str_is(const rpa_vm *vm, uint32_t index, const char *text)
{
    const rpa_obj *obj = &vm->objs[index];
    size_t length = xx_str_len(text);
    return (obj->type == RPO_TEXT || obj->type == RPO_BYTES) && obj->length == length && xx_rt_memcmp(rpa_str(vm, obj), text, length) == 0;
}

static bool rpa_mem_is(const uint8_t *data, size_t size, const char *text)
{
    size_t length = xx_str_len(text);
    return size == length && xx_rt_memcmp(data, text, length) == 0;
}

/* Reserve @size blob bytes; the returned offset stays valid, pointers not. */
static bool rpa_blob_reserve(rpa_vm *vm, size_t size, size_t *offset)
{
    size_t need, cap;
    uint8_t *grown;
    if (size > SIZE_MAX - vm->nblob) return false;
    need = vm->nblob + size;
    if (need > UINT32_MAX) return false;
    if (need > vm->capblob) {
        cap = vm->capblob ? vm->capblob : 4096U;
        while (cap < need) {
            if (cap > SIZE_MAX / 2U) return false;
            cap *= 2U;
        }
        if (cap - vm->capblob > vm->budget) return false;
        grown = (uint8_t *)xx_mem_realloc(vm->blob, cap);
        if (!grown) return false;
        vm->budget -= cap - vm->capblob;
        vm->blob = grown;
        vm->capblob = cap;
    }
    *offset = vm->nblob;
    vm->nblob = need;
    return true;
}

static bool rpa_need(const rpa_vm *vm, uint64_t count)
{
    return count <= (uint64_t)(vm->size - vm->at);
}

/* A string whose bytes stay in the pickle. */
static bool rpa_push_slice(rpa_vm *vm, uint8_t type, uint64_t length)
{
    uint32_t index;
    if (!rpa_need(vm, length) || length > UINT32_MAX) return false;
    index = rpa_new(vm, type);
    if (!index) return false;
    vm->objs[index].offset = (uint32_t)vm->at;
    vm->objs[index].length = (uint32_t)length;
    vm->at += (size_t)length;
    return rpa_push(vm, index);
}

/* The text of a newline-terminated argument, without the newline. */
static bool rpa_line(rpa_vm *vm, const uint8_t **line, size_t *length)
{
    size_t end = vm->at;
    while (end < vm->size && vm->code[end] != '\n') {
        if (end - vm->at > 1U * 1024U * 1024U) return false;
        ++end;
    }
    if (end >= vm->size) return false;
    *line = vm->code + vm->at;
    *length = end - vm->at;
    vm->at = end + 1U;
    return true;
}

/* Signed decimal (optionally with Python 2's trailing 'L'); false when it
 * is not a number, *wide when it does not fit 64 bits. */
static bool rpa_decimal(const uint8_t *text, size_t length, int64_t *value, bool *wide)
{
    size_t index = 0U;
    bool negative = false;
    uint64_t magnitude = 0U;
    *wide = false;
    if (length && text[length - 1U] == 'L') --length;
    if (index < length && (text[index] == '-' || text[index] == '+')) {
        negative = text[index] == '-';
        ++index;
    }
    if (index >= length) return false;
    for (; index < length; ++index) {
        unsigned digit;
        if (text[index] < '0' || text[index] > '9') return false;
        digit = (unsigned)(text[index] - '0');
        if (magnitude > (UINT64_C(0x8000000000000000) - digit) / 10U) *wide = true;
        else magnitude = magnitude * 10U + digit;
    }
    if (*wide) return true;
    if (negative) {
        if (magnitude > UINT64_C(0x8000000000000000)) {
            *wide = true;
            return true;
        }
        *value = magnitude == UINT64_C(0x8000000000000000) ? INT64_MIN : -(int64_t)magnitude;
    } else {
        if (magnitude > (uint64_t)INT64_MAX) {
            *wide = true;
            return true;
        }
        *value = (int64_t)magnitude;
    }
    return true;
}

static bool rpa_push_int(rpa_vm *vm, int64_t value)
{
    uint32_t index = rpa_new(vm, RPO_INT);
    if (!index) return false;
    vm->objs[index].offset = (uint32_t)(uint64_t)value;
    vm->objs[index].length = (uint32_t)((uint64_t)value >> 32);
    return rpa_push(vm, index);
}

/* Little-endian two's complement of @length bytes. */
static bool rpa_push_long(rpa_vm *vm, uint64_t length)
{
    const uint8_t *p;
    uint64_t raw = 0U;
    size_t index, significant;
    if (!rpa_need(vm, length)) return false;
    p = vm->code + vm->at;
    vm->at += (size_t)length;
    if (length == 0U) return rpa_push_int(vm, 0);
    /* Bytes beyond the eighth must only sign-extend it. */
    significant = (size_t)length;
    while (significant > 8U) {
        uint8_t fill = (p[7] & 0x80U) ? 0xFFU : 0x00U;
        if (p[significant - 1U] != fill) return rpa_push_new(vm, RPO_OTHER);
        --significant;
    }
    for (index = 0U; index < significant; ++index) raw |= (uint64_t)p[index] << (8U * index);
    if (significant < 8U && (p[significant - 1U] & 0x80U)) raw |= ~((UINT64_C(1) << (8U * significant)) - 1U);
    return rpa_push_int(vm, (int64_t)raw);
}

static void rpa_put_utf8(uint8_t *out, size_t *used, uint32_t c)
{
    if (c < 0x80U) {
        out[(*used)++] = (uint8_t)c;
    } else if (c < 0x800U) {
        out[(*used)++] = (uint8_t)(0xC0U | (c >> 6));
        out[(*used)++] = (uint8_t)(0x80U | (c & 0x3FU));
    } else if (c < 0x10000U) {
        out[(*used)++] = (uint8_t)(0xE0U | (c >> 12));
        out[(*used)++] = (uint8_t)(0x80U | ((c >> 6) & 0x3FU));
        out[(*used)++] = (uint8_t)(0x80U | (c & 0x3FU));
    } else {
        out[(*used)++] = (uint8_t)(0xF0U | (c >> 18));
        out[(*used)++] = (uint8_t)(0x80U | ((c >> 12) & 0x3FU));
        out[(*used)++] = (uint8_t)(0x80U | ((c >> 6) & 0x3FU));
        out[(*used)++] = (uint8_t)(0x80U | (c & 0x3FU));
    }
}

static int rpa_hex_digit(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool rpa_hex_run(const uint8_t *p, size_t count, uint32_t *value)
{
    size_t index;
    uint32_t v = 0U;
    for (index = 0U; index < count; ++index) {
        int d = rpa_hex_digit(p[index]);
        if (d < 0) return false;
        v = (v << 4) | (uint32_t)d;
    }
    *value = v;
    return true;
}

/* Protocol 0 UNICODE: raw-unicode-escape text, stored as UTF-8. */
static bool rpa_push_raw_unicode(rpa_vm *vm, const uint8_t *line, size_t length)
{
    size_t offset, used = 0U, index = 0U;
    uint32_t object;
    uint8_t *out;
    const size_t line_at = (size_t)(line - vm->code);
    if (length > SIZE_MAX / 4U || !rpa_blob_reserve(vm, length * 4U, &offset)) return false;
    out = vm->blob + offset;
    line = vm->code + line_at;
    while (index < length) {
        uint32_t c = line[index];
        if (c == '\\' && index + 1U < length && (line[index + 1U] == 'u' || line[index + 1U] == 'U')) {
            size_t digits = line[index + 1U] == 'u' ? 4U : 8U;
            if (length - index - 2U < digits || !rpa_hex_run(line + index + 2U, digits, &c) || c > 0x10FFFFU || (c >= 0xD800U && c <= 0xDFFFU)) return false;
            index += 2U + digits;
        } else {
            ++index;
        }
        rpa_put_utf8(out, &used, c);
    }
    vm->nblob = offset + used;
    object = rpa_new(vm, RPO_TEXT);
    if (!object) return false;
    vm->objs[object].in_blob = 1U;
    vm->objs[object].offset = (uint32_t)offset;
    vm->objs[object].length = (uint32_t)used;
    return rpa_push(vm, object);
}

/* Protocol 0 STRING: a quoted Python 2 repr, stored as bytes. */
static bool rpa_push_repr(rpa_vm *vm, const uint8_t *line, size_t length)
{
    size_t offset, used = 0U, index;
    uint32_t object;
    uint8_t *out, quote;
    const size_t line_at = (size_t)(line - vm->code);
    if (length < 2U) return false;
    quote = line[0];
    if ((quote != '\'' && quote != '"') || line[length - 1U] != quote) return false;
    if (!rpa_blob_reserve(vm, length, &offset)) return false;
    out = vm->blob + offset;
    line = vm->code + line_at;
    for (index = 1U; index + 1U < length; ++index) {
        uint8_t c = line[index];
        if (c == '\\') {
            if (index + 2U >= length) return false;
            c = line[++index];
            switch (c) {
                case '\\':
                case '\'':
                case '"': break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'a': c = 7U; break;
                case 'b': c = 8U; break;
                case 'f': c = 12U; break;
                case 'v': c = 11U; break;
                case 'x': {
                    uint32_t v;
                    if (index + 3U >= length || !rpa_hex_run(line + index + 1U, 2U, &v)) return false;
                    c = (uint8_t)v;
                    index += 2U;
                    break;
                }
                default: return false;
            }
        }
        out[used++] = c;
    }
    vm->nblob = offset + used;
    object = rpa_new(vm, RPO_BYTES);
    if (!object) return false;
    vm->objs[object].in_blob = 1U;
    vm->objs[object].offset = (uint32_t)offset;
    vm->objs[object].length = (uint32_t)used;
    return rpa_push(vm, object);
}

/* The two globals an RPA index may name. */
static bool rpa_push_global(rpa_vm *vm, const uint8_t *module, size_t module_length, const uint8_t *name, size_t name_length)
{
    if ((rpa_mem_is(module, module_length, "__builtin__") || rpa_mem_is(module, module_length, "builtins")) && rpa_mem_is(name, name_length, "bytes"))
        return rpa_push_new(vm, RPO_FN_BYTES);
    if (rpa_mem_is(module, module_length, "_codecs") && rpa_mem_is(name, name_length, "encode")) return rpa_push_new(vm, RPO_FN_ENCODE);
    return false;
}

/* The Nth item (0-based) of a tuple. */
static uint32_t rpa_item(const rpa_vm *vm, uint32_t container, uint32_t n)
{
    uint32_t cell = vm->objs[container].head, guard = 0U;
    while (cell && guard < n) {
        cell = vm->cells[cell].next;
        ++guard;
    }
    return cell ? vm->cells[cell].value : 0U;
}

/* _codecs.encode(text, "latin1"): UTF-8 text to Latin-1 bytes. */
static bool rpa_latin1(rpa_vm *vm, uint32_t text, uint32_t *result)
{
    rpa_obj source = vm->objs[text];
    size_t offset, used = 0U, index = 0U;
    uint32_t object;
    const uint8_t *in;
    if (source.type != RPO_TEXT || !rpa_blob_reserve(vm, source.length, &offset)) return false;
    in = rpa_str(vm, &source);
    while (index < source.length) {
        uint8_t c = in[index];
        if (c < 0x80U) {
            vm->blob[offset + used++] = c;
            ++index;
        } else if ((c & 0xE0U) == 0xC0U && index + 1U < source.length && (in[index + 1U] & 0xC0U) == 0x80U) {
            uint32_t v = ((uint32_t)(c & 0x1FU) << 6) | (in[index + 1U] & 0x3FU);
            if (v < 0x80U || v > 0xFFU) return false;
            vm->blob[offset + used++] = (uint8_t)v;
            index += 2U;
        } else {
            return false;
        }
    }
    vm->nblob = offset + used;
    object = rpa_new(vm, RPO_BYTES);
    if (!object) return false;
    vm->objs[object].in_blob = 1U;
    vm->objs[object].offset = (uint32_t)offset;
    vm->objs[object].length = (uint32_t)used;
    *result = object;
    return true;
}

static bool rpa_reduce(rpa_vm *vm)
{
    uint32_t args = rpa_pop(vm), callable = rpa_pop(vm), result = 0U;
    if (!args || !callable || vm->objs[args].type != RPO_TUPLE) return false;
    if (vm->objs[callable].type == RPO_FN_BYTES) {
        if (vm->objs[args].count != 0U) return false;
        result = rpa_new(vm, RPO_BYTES);
        if (!result) return false;
        vm->objs[result].offset = 0U;
        vm->objs[result].length = 0U;
    } else if (vm->objs[callable].type == RPO_FN_ENCODE) {
        uint32_t text, codec;
        if (vm->objs[args].count != 2U) return false;
        text = rpa_item(vm, args, 0U);
        codec = rpa_item(vm, args, 1U);
        if (!text || !codec || !(rpa_str_is(vm, codec, "latin1") || rpa_str_is(vm, codec, "latin-1") || rpa_str_is(vm, codec, "iso-8859-1")) ||
            !rpa_latin1(vm, text, &result))
            return false;
    } else {
        return false;
    }
    return rpa_push(vm, result);
}

/* Build a tuple/list/dict of everything above the topmost mark. */
static bool rpa_collect(rpa_vm *vm, uint8_t type)
{
    int64_t mark = rpa_find_mark(vm);
    uint32_t container, index;
    if (mark < 0) return false;
    if (type == RPO_DICT && ((vm->nstack - (uint32_t)mark - 1U) & 1U)) return false;
    container = rpa_new(vm, type);
    if (!container) return false;
    for (index = (uint32_t)mark + 1U; index < vm->nstack; ++index)
        if (!rpa_add_cell(vm, container, vm->stack[index])) return false;
    vm->nstack = (uint32_t)mark;
    return rpa_push(vm, container);
}

static bool rpa_tuple_n(rpa_vm *vm, uint32_t n)
{
    uint32_t container, index;
    if (vm->nstack < n) return false;
    for (index = vm->nstack - n; index < vm->nstack; ++index)
        if (vm->stack[index] == RPA_MARK) return false;
    container = rpa_new(vm, RPO_TUPLE);
    if (!container) return false;
    for (index = vm->nstack - n; index < vm->nstack; ++index)
        if (!rpa_add_cell(vm, container, vm->stack[index])) return false;
    vm->nstack -= n;
    return rpa_push(vm, container);
}

/* APPENDS / SETITEMS: move the items above the mark into the container
 * just below it. */
static bool rpa_extend(rpa_vm *vm, uint8_t type)
{
    int64_t mark = rpa_find_mark(vm);
    uint32_t container, index;
    if (mark < 1) return false;
    container = vm->stack[(uint32_t)mark - 1U];
    if (container == RPA_MARK || vm->objs[container].type != type) return false;
    if (type == RPO_DICT && ((vm->nstack - (uint32_t)mark - 1U) & 1U)) return false;
    for (index = (uint32_t)mark + 1U; index < vm->nstack; ++index)
        if (!rpa_add_cell(vm, container, vm->stack[index])) return false;
    vm->nstack = (uint32_t)mark;
    return true;
}

static bool rpa_memo_put(rpa_vm *vm, uint64_t key)
{
    uint32_t top = rpa_top(vm), old;
    if (!top || key >= (uint64_t)vm->size + 1U || key >= UINT32_MAX - 1U) return false;
    old = vm->capmemo;
    if (!rpa_vm_grow(vm, (void **)&vm->memo, &vm->capmemo, sizeof(uint32_t), key + 1U)) return false;
    if (vm->capmemo > old) xx_mem_zero(vm->memo + old, (size_t)(vm->capmemo - old) * sizeof(uint32_t));
    vm->memo[key] = top;
    return true;
}

static bool rpa_memo_get(rpa_vm *vm, uint64_t key)
{
    if (key >= vm->capmemo || !vm->memo[key]) return false;
    return rpa_push(vm, vm->memo[key]);
}

static bool rpa_decimal_key(const uint8_t *line, size_t length, uint64_t *key)
{
    int64_t value;
    bool wide;
    if (!rpa_decimal(line, length, &value, &wide) || wide || value < 0) return false;
    *key = (uint64_t)value;
    return true;
}

/* Run the pickle to STOP.  True with vm->result set to the loaded object. */
static bool rpa_vm_run(rpa_vm *vm)
{
    unsigned long steps = 0U;
    /* Object 0 is "none"; objects start at 1. */
    (void)rpa_new(vm, RPO_NONE);
    if (vm->nobj != 1U) return false;
    while (vm->at < vm->size) {
        uint8_t op = vm->code[vm->at++];
        const uint8_t *line, *line2;
        size_t length, length2;
        uint64_t n;
        int64_t value;
        bool wide;
        if ((++steps & 0xFFFFUL) == 0UL && vm->pd && xx_pd_is_stopped(vm->pd)) return false;
        switch (op) {
            case 0x80: /* PROTO */
                if (!rpa_need(vm, 1U) || vm->code[vm->at] > 5U) return false;
                ++vm->at;
                break;
            case 0x95: /* FRAME */
                if (!rpa_need(vm, 8U)) return false;
                vm->at += 8U;
                break;
            case '.': /* STOP */
                if (vm->nstack != 1U || vm->stack[0] == RPA_MARK) return false;
                vm->result = vm->stack[0];
                return true;
            case '(':
                if (!rpa_vm_grow(vm, (void **)&vm->stack, &vm->capstack, sizeof(uint32_t), (uint64_t)vm->nstack + 1U)) return false;
                vm->stack[vm->nstack++] = RPA_MARK;
                break;
            case ')':
                if (!rpa_push_new(vm, RPO_TUPLE)) return false;
                break;
            case ']':
                if (!rpa_push_new(vm, RPO_LIST)) return false;
                break;
            case '}':
                if (!rpa_push_new(vm, RPO_DICT)) return false;
                break;
            case 'N':
                if (!rpa_push_new(vm, RPO_NONE)) return false;
                break;
            case 0x88:
            case 0x89: /* NEWTRUE / NEWFALSE */
                if (!rpa_push_new(vm, RPO_OTHER)) return false;
                break;
            case 'J': /* BININT */
                if (!rpa_need(vm, 4U)) return false;
                value = (int64_t)(int32_t)xx_data_get_u32(vm->code + vm->at, 4, 0, false);
                vm->at += 4U;
                if (!rpa_push_int(vm, value)) return false;
                break;
            case 'K': /* BININT1 */
                if (!rpa_need(vm, 1U)) return false;
                if (!rpa_push_int(vm, vm->code[vm->at++])) return false;
                break;
            case 'M': /* BININT2 */
                if (!rpa_need(vm, 2U)) return false;
                value = (int64_t)vm->code[vm->at] | ((int64_t)vm->code[vm->at + 1U] << 8);
                vm->at += 2U;
                if (!rpa_push_int(vm, value)) return false;
                break;
            case 0x8A: /* LONG1 */
                if (!rpa_need(vm, 1U)) return false;
                n = vm->code[vm->at++];
                if (!rpa_push_long(vm, n)) return false;
                break;
            case 0x8B: /* LONG4 */
                if (!rpa_need(vm, 4U)) return false;
                value = (int64_t)(int32_t)xx_data_get_u32(vm->code + vm->at, 4, 0, false);
                vm->at += 4U;
                if (value < 0 || !rpa_push_long(vm, (uint64_t)value)) return false;
                break;
            case 'I': /* INT line */
            case 'L': /* LONG line */
                if (!rpa_line(vm, &line, &length)) return false;
                if (op == 'I' && length == 2U && line[0] == '0' && (line[1] == '0' || line[1] == '1')) {
                    if (!rpa_push_new(vm, RPO_OTHER)) return false;
                    break;
                }
                if (!rpa_decimal(line, length, &value, &wide)) return false;
                if (wide ? !rpa_push_new(vm, RPO_OTHER) : !rpa_push_int(vm, value)) return false;
                break;
            case 'X': /* BINUNICODE */
            case 'B': /* BINBYTES */
                if (!rpa_need(vm, 4U)) return false;
                n = xx_data_get_u32(vm->code + vm->at, 4, 0, false);
                vm->at += 4U;
                if (!rpa_push_slice(vm, op == 'X' ? RPO_TEXT : RPO_BYTES, n)) return false;
                break;
            case 'T': /* BINSTRING */
                if (!rpa_need(vm, 4U)) return false;
                value = (int64_t)(int32_t)xx_data_get_u32(vm->code + vm->at, 4, 0, false);
                vm->at += 4U;
                if (value < 0 || !rpa_push_slice(vm, RPO_BYTES, (uint64_t)value)) return false;
                break;
            case 0x8C: /* SHORT_BINUNICODE */
            case 'U':  /* SHORT_BINSTRING */
            case 'C':  /* SHORT_BINBYTES */
                if (!rpa_need(vm, 1U)) return false;
                n = vm->code[vm->at++];
                if (!rpa_push_slice(vm, op == 0x8C ? RPO_TEXT : RPO_BYTES, n)) return false;
                break;
            case 0x8D: /* BINUNICODE8 */
            case 0x8E: /* BINBYTES8 */
            case 0x96: /* BYTEARRAY8 */
                if (!rpa_need(vm, 8U)) return false;
                n = xx_data_get_u64(vm->code + vm->at, 8, 0, false);
                vm->at += 8U;
                if (!rpa_push_slice(vm, op == 0x8D ? RPO_TEXT : RPO_BYTES, n)) return false;
                break;
            case 'V': /* UNICODE line */
                if (!rpa_line(vm, &line, &length) || !rpa_push_raw_unicode(vm, line, length)) return false;
                break;
            case 'S': /* STRING line */
                if (!rpa_line(vm, &line, &length)) return false;
                while (length && (line[length - 1U] == '\r' || line[length - 1U] == ' ')) --length;
                if (!rpa_push_repr(vm, line, length)) return false;
                break;
            case 'c': /* GLOBAL */
                if (!rpa_line(vm, &line, &length) || !rpa_line(vm, &line2, &length2) || !rpa_push_global(vm, line, length, line2, length2)) return false;
                break;
            case 0x93: { /* STACK_GLOBAL */
                uint32_t name = rpa_pop(vm), module = rpa_pop(vm);
                const rpa_obj *mo, *no;
                if (!name || !module) return false;
                mo = &vm->objs[module];
                no = &vm->objs[name];
                if (mo->type != RPO_TEXT || no->type != RPO_TEXT || !rpa_push_global(vm, rpa_str(vm, mo), mo->length, rpa_str(vm, no), no->length)) return false;
                break;
            }
            case 'R': /* REDUCE */
                if (!rpa_reduce(vm)) return false;
                break;
            case 'q': /* BINPUT */
                if (!rpa_need(vm, 1U) || !rpa_memo_put(vm, vm->code[vm->at++])) return false;
                break;
            case 'r': /* LONG_BINPUT */
                if (!rpa_need(vm, 4U)) return false;
                n = xx_data_get_u32(vm->code + vm->at, 4, 0, false);
                vm->at += 4U;
                if (!rpa_memo_put(vm, n)) return false;
                break;
            case 0x94: /* MEMOIZE */
                if (!rpa_memo_put(vm, vm->nextmemo)) return false;
                ++vm->nextmemo;
                break;
            case 'p': /* PUT line */
                if (!rpa_line(vm, &line, &length) || !rpa_decimal_key(line, length, &n) || !rpa_memo_put(vm, n)) return false;
                break;
            case 'h': /* BINGET */
                if (!rpa_need(vm, 1U) || !rpa_memo_get(vm, vm->code[vm->at++])) return false;
                break;
            case 'j': /* LONG_BINGET */
                if (!rpa_need(vm, 4U)) return false;
                n = xx_data_get_u32(vm->code + vm->at, 4, 0, false);
                vm->at += 4U;
                if (!rpa_memo_get(vm, n)) return false;
                break;
            case 'g': /* GET line */
                if (!rpa_line(vm, &line, &length) || !rpa_decimal_key(line, length, &n) || !rpa_memo_get(vm, n)) return false;
                break;
            case 'a': { /* APPEND */
                uint32_t item = rpa_pop(vm), list = rpa_top(vm);
                if (!item || !list || vm->objs[list].type != RPO_LIST || !rpa_add_cell(vm, list, item)) return false;
                break;
            }
            case 'e': /* APPENDS */
                if (!rpa_extend(vm, RPO_LIST)) return false;
                break;
            case 's': { /* SETITEM */
                uint32_t item = rpa_pop(vm), key = rpa_pop(vm), dict = rpa_top(vm);
                if (!item || !key || !dict || vm->objs[dict].type != RPO_DICT || !rpa_add_cell(vm, dict, key) || !rpa_add_cell(vm, dict, item)) return false;
                break;
            }
            case 'u': /* SETITEMS */
                if (!rpa_extend(vm, RPO_DICT)) return false;
                break;
            case 't':
                if (!rpa_collect(vm, RPO_TUPLE)) return false;
                break;
            case 'l':
                if (!rpa_collect(vm, RPO_LIST)) return false;
                break;
            case 'd':
                if (!rpa_collect(vm, RPO_DICT)) return false;
                break;
            case 0x85:
                if (!rpa_tuple_n(vm, 1U)) return false;
                break;
            case 0x86:
                if (!rpa_tuple_n(vm, 2U)) return false;
                break;
            case 0x87:
                if (!rpa_tuple_n(vm, 3U)) return false;
                break;
            case '0': /* POP */
                if (!vm->nstack) return false;
                --vm->nstack;
                break;
            case '1': { /* POP_MARK */
                int64_t mark = rpa_find_mark(vm);
                if (mark < 0) return false;
                vm->nstack = (uint32_t)mark;
                break;
            }
            case '2': /* DUP */
                if (!rpa_push(vm, rpa_top(vm))) return false;
                break;
            default: return false;
        }
    }
    return false;
}

static void rpa_vm_free(rpa_vm *vm)
{
    if (vm->objs) xx_mem_free(vm->objs);
    if (vm->cells) xx_mem_free(vm->cells);
    if (vm->stack) xx_mem_free(vm->stack);
    if (vm->memo) xx_mem_free(vm->memo);
    if (vm->blob) xx_mem_free(vm->blob);
    xx_mem_zero(vm, sizeof(*vm));
}

/* -------------------------------------------------------------- index -- */

typedef struct rpa_member {
    const uint8_t *name;
    const uint8_t *prefix;
    uint32_t name_length;
    uint32_t prefix_length;
    int64_t offset;  /* of the stored part */
    uint64_t length; /* whole member, prefix included */
} rpa_member;

typedef struct rpa_layout {
    uint32_t version;
    uint32_t header_size;
    uint64_t index_offset;
    uint64_t key;
    int64_t base;
    int64_t format_size; /* relative to base */
    uint64_t count;
    uint8_t *packed;
    rpa_sink plain;
    rpa_vm vm;
    rpa_member *members;
} rpa_layout;

static void rpa_layout_free(rpa_layout *layout)
{
    if (!layout) return;
    if (layout->packed) xx_mem_free(layout->packed);
    if (layout->plain.data) xx_mem_free(layout->plain.data);
    rpa_vm_free(&layout->vm);
    if (layout->members) xx_mem_free(layout->members);
    xx_mem_zero(layout, sizeof(*layout));
}

static bool rpa_hex_field(const uint8_t *p, size_t length, uint64_t *value)
{
    size_t index;
    uint64_t v = 0U;
    if (length == 0U || length > 16U) return false;
    for (index = 0U; index < length; ++index) {
        int d = rpa_hex_digit(p[index]);
        if (d < 0) return false;
        v = (v << 4) | (uint64_t)d;
    }
    *value = v;
    return true;
}

/* The header line: tag and hexadecimal fields separated by spaces. */
static bool rpa_header(const uint8_t *head, size_t available, uint64_t size, rpa_layout *layout)
{
    size_t end = 0U, index, fields = 0U;
    size_t start[8], length[8];
    uint64_t offset, key = 0U;
    while (end < available && head[end] != '\n') ++end;
    if (end >= available || end < 9U) return false;
    /* "XXX-d.d " */
    if (head[3] != '-' || head[4] < '0' || head[4] > '9' || head[5] != '.' || head[6] < '0' || head[6] > '9' || head[7] != ' ') return false;
    index = 0U;
    while (index < end) {
        size_t s;
        while (index < end && (head[index] == ' ' || (head[index] == '\r' && index + 1U == end))) ++index;
        if (index >= end) break;
        s = index;
        while (index < end && head[index] != ' ' && !(head[index] == '\r' && index + 1U == end)) {
            if (head[index] < 0x21U || head[index] > 0x7EU) return false;
            ++index;
        }
        if (fields >= 8U) return false;
        start[fields] = s;
        length[fields] = index - s;
        ++fields;
    }
    if (fields < 2U || length[0] != 7U) return false;
    if (rpa_mem_is(head, 7U, "RPA-2.0")) {
        if (fields != 2U || !rpa_hex_field(head + start[1], length[1], &offset)) return false;
        layout->version = 20U;
    } else if (rpa_mem_is(head, 7U, "RPA-3.0") || rpa_mem_is(head, 7U, "RPA-4.0") || rpa_mem_is(head, 7U, "RPA-3.2")) {
        bool v32 = head[4] == '3' && head[6] == '2';
        if ((v32 ? fields < 3U : fields != 3U) || !rpa_hex_field(head + start[1], length[1], &offset) || !rpa_hex_field(head + start[2], length[2], &key)) return false;
        for (index = 3U; index < fields; ++index) {
            uint64_t ignored;
            if (!rpa_hex_field(head + start[index], length[index], &ignored)) return false;
        }
        layout->version = v32 ? 32U : head[4] == '4' ? 40U : 30U;
    } else if (rpa_mem_is(head, 7U, "ALT-1.0")) {
        if (fields != 3U || !rpa_hex_field(head + start[1], length[1], &key) || !rpa_hex_field(head + start[2], length[2], &offset)) return false;
        key ^= RPA_ALT_KEY;
        layout->version = 1U;
    } else {
        return false;
    }
    /* The index follows the header and holds at least a zlib header and
     * trailer. */
    if (offset <= (uint64_t)end || offset > size || size - offset < 6U) return false;
    layout->header_size = (uint32_t)end + 1U;
    layout->index_offset = offset;
    layout->key = key;
    return true;
}

static size_t rpa_memory_limit(Abstractformat *format)
{
    const xx_var *option = xx_format_resolve_extra_parameter(format, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    size_t budget = RPA_BUDGET;
    if (option) {
        uint64_t limit = xx_var_get_u64(option);
        if (limit < (uint64_t)budget) budget = (size_t)limit;
    }
    return budget;
}

/* An index value that is a Python int; false for anything else. */
static bool rpa_int_of(const rpa_vm *vm, uint32_t index, int64_t *value)
{
    if (!index || vm->objs[index].type != RPO_INT) return false;
    *value = (int64_t)(((uint64_t)vm->objs[index].length << 32) | vm->objs[index].offset);
    return true;
}

/* offset ^ key as Python computes it, when it is a non-negative number. */
static bool rpa_unkey(int64_t raw, uint64_t key, uint64_t *out)
{
    if (raw < 0) return false;
    *out = (uint64_t)raw ^ key;
    return true;
}

static bool rpa_members(rpa_layout *layout, xx_pd_struct *pd, size_t *budget)
{
    rpa_vm *vm = &layout->vm;
    /* Object pointers are not kept across rpa_latin1(), which may move the
     * object table; the dict's first cell is copied instead. */
    const uint32_t dict_head = vm->objs[vm->result].head;
    uint32_t cell, done = 0U;
    size_t bytes;
    if (vm->objs[vm->result].type != RPO_DICT || (vm->objs[vm->result].count & 1U)) return false;
    layout->count = vm->objs[vm->result].count / 2U;
    if (layout->count > RPA_MAX_MEMBERS) return false;
    if (layout->count == 0U) return true;
    bytes = (size_t)layout->count * sizeof(rpa_member);
    if (bytes > *budget) return false;
    layout->members = (rpa_member *)xx_mem_calloc((size_t)layout->count, sizeof(rpa_member));
    if (!layout->members) return false;
    *budget -= bytes;
    cell = dict_head;
    while (cell) {
        uint32_t key = vm->cells[cell].value, value, next, first, tuple;
        const rpa_obj *ko, *vo, *to;
        rpa_member *member = &layout->members[done];
        int64_t raw_offset, raw_length;
        uint64_t offset, length;
        next = vm->cells[cell].next;
        if (!next || done >= layout->count) return false;
        value = vm->cells[next].value;
        cell = vm->cells[next].next;
        if ((done & 0xFFFU) == 0U && pd && xx_pd_is_stopped(pd)) return false;
        ko = &vm->objs[key];
        vo = &vm->objs[value];
        if ((ko->type != RPO_TEXT && ko->type != RPO_BYTES) || ko->length == 0U || ko->length > RPA_MAX_NAME) return false;
        if ((vo->type != RPO_LIST && vo->type != RPO_TUPLE) || !vo->count) return false;
        first = vm->cells[vo->head].value;
        to = &vm->objs[first];
        if (to->type != RPO_TUPLE || to->count < 2U || to->count > 3U) return false;
        tuple = first;
        if (!rpa_int_of(vm, rpa_item(vm, tuple, 0U), &raw_offset) || !rpa_int_of(vm, rpa_item(vm, tuple, 1U), &raw_length) ||
            !rpa_unkey(raw_offset, layout->key, &offset) || !rpa_unkey(raw_length, layout->key, &length))
            return false;
        member->name = rpa_str(vm, ko);
        member->name_length = ko->length;
        if (to->count == 3U) {
            uint32_t prefix = rpa_item(vm, tuple, 2U);
            const rpa_obj *po = &vm->objs[prefix];
            if (po->type == RPO_TEXT) {
                /* A str prefix is its Latin-1 bytes, as Ren'Py encodes it. */
                uint32_t converted;
                if (!rpa_latin1(vm, prefix, &converted)) return false;
                po = &vm->objs[converted];
            } else if (po->type != RPO_BYTES) {
                return false;
            }
            member->prefix_length = po->length;
            /* Blob pointers are fixed up after the loop. */
            member->prefix = (const uint8_t *)(uintptr_t)(po->offset + 1U);
            if (po->in_blob) member->prefix_length |= 0x80000000U;
        }
        if ((uint64_t)(member->prefix_length & 0x7FFFFFFFU) > length) return false;
        {
            uint64_t stored = length - (member->prefix_length & 0x7FFFFFFFU);
            if (offset < layout->header_size || offset > layout->index_offset || stored > layout->index_offset - offset) return false;
        }
        member->offset = (int64_t)offset;
        member->length = length;
        ++done;
    }
    if (done != layout->count) return false;
    /* The blob no longer moves: turn prefix offsets into pointers. */
    for (done = 0U; done < layout->count; ++done) {
        rpa_member *member = &layout->members[done];
        if (member->prefix) {
            uint64_t at = (uint64_t)(uintptr_t)member->prefix - 1U;
            bool in_blob = (member->prefix_length & 0x80000000U) != 0U;
            member->prefix_length &= 0x7FFFFFFFU;
            member->prefix = in_blob ? vm->blob + at : vm->code + at;
        }
    }
    /* Names in the blob (protocol 0) were resolved by rpa_str before any
     * later blob growth could move them, so refresh them too. */
    cell = dict_head;
    done = 0U;
    while (cell && done < layout->count) {
        const rpa_obj *ko = &vm->objs[vm->cells[cell].value];
        layout->members[done].name = rpa_str(vm, ko);
        cell = vm->cells[vm->cells[cell].next].next;
        ++done;
    }
    return true;
}

static bool rpa_parse(Abstractformat *format, rpa_layout *layout, bool want_members, xx_pd_struct *pd)
{
    uint8_t head[RPA_LINE_MAX];
    int64_t total;
    uint64_t size, available;
    size_t budget, packed_size, used = 0U;
    xx_io_device sink_device;
    bool ok;
    xx_mem_zero(layout, sizeof(*layout));
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = (uint64_t)(total - format->base_address);
    if (size < 16U) return false;
    available = size < RPA_LINE_MAX ? size : RPA_LINE_MAX;
    if (!rpa_read_at(format->device, format->base_address, head, (size_t)available) || !rpa_header(head, (size_t)available, size, layout)) return false;
    layout->base = format->base_address;
    budget = rpa_memory_limit(format);

    /* Compressed index. */
    available = size - layout->index_offset;
    packed_size = available > RPA_MAX_PACKED ? RPA_MAX_PACKED : (size_t)available;
    if (packed_size > budget) packed_size = budget;
    if (packed_size < 6U) return false;
    layout->packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!layout->packed) return false;
    budget -= packed_size;
    if (!rpa_read_at(format->device, format->base_address + (int64_t)layout->index_offset, layout->packed, packed_size) ||
        !xx_zlib_stream_header_is_valid(layout->packed, packed_size))
        goto fail;

    /* Inflate into the sink, never past the plain cap or the budget. */
    xx_mem_zero(&sink_device, sizeof(sink_device));
    layout->plain.limit = budget < RPA_MAX_PLAIN ? budget : RPA_MAX_PLAIN;
    sink_device.priv = &layout->plain;
    sink_device.read = rpa_sink_read;
    sink_device.write = rpa_sink_write;
    sink_device.seek = rpa_sink_seek;
    sink_device.seek64 = rpa_sink_seek64;
    sink_device.tell = rpa_sink_tell;
    sink_device.close = rpa_sink_close;
    sink_device.total_size = rpa_sink_total;
    sink_device.get_total_size = rpa_sink_total;
    sink_device.size = rpa_sink_total;
    ok = xx_deflate_unpack_memory_to_device_ex(layout->packed + 2U, packed_size - 2U, &sink_device, &used, false, pd);
    if (!ok || layout->plain.overflow || used > packed_size - 6U || layout->plain.size < 2U ||
        xx_zlib_stream_adler32(layout->plain.data, layout->plain.size) != (((uint32_t)layout->packed[2U + used] << 24) | ((uint32_t)layout->packed[3U + used] << 16) |
                                                                           ((uint32_t)layout->packed[4U + used] << 8) | (uint32_t)layout->packed[5U + used]))
        goto fail;
    layout->format_size = (int64_t)(layout->index_offset + 2U + used + 4U);
    budget -= layout->plain.capacity < budget ? layout->plain.capacity : budget;
    /* The packed copy is no longer needed. */
    xx_mem_free(layout->packed);
    layout->packed = NULL;
    budget += packed_size;

    /* Unpickle. */
    layout->vm.code = layout->plain.data;
    layout->vm.size = layout->plain.size;
    layout->vm.budget = budget;
    layout->vm.pd = pd;
    if (!rpa_vm_run(&layout->vm)) goto fail;
    budget = layout->vm.budget;
    if (!rpa_members(layout, pd, &budget)) goto fail;
    layout->vm.budget = budget;
    if (!want_members) {
        /* Keep only the count and extent. */
        uint64_t count = layout->count;
        int64_t format_size = layout->format_size;
        uint32_t version = layout->version;
        uint64_t key = layout->key, index_offset = layout->index_offset;
        rpa_layout_free(layout);
        layout->count = count;
        layout->format_size = format_size;
        layout->version = version;
        layout->key = key;
        layout->index_offset = index_offset;
    }
    return true;
fail:
    rpa_layout_free(layout);
    return false;
}

/* -------------------------------------------------------------- names -- */

static char rpa_upper_ascii(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* CON, PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$ and CLOCK$, with or
 * without an extension, in any case. */
static bool rpa_is_device(const char *name, size_t length)
{
    static const char *const words[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, word, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (word = 0U; word < sizeof(words) / sizeof(words[0]); ++word) {
        const char *text = words[word];
        for (index = 0U; index < stem && text[index]; ++index)
            if (rpa_upper_ascii(name[index]) != text[index]) break;
        if (index == stem && text[index] == '\0') return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9') {
        char a = rpa_upper_ascii(name[0]);
        char b = rpa_upper_ascii(name[1]);
        char c = rpa_upper_ascii(name[2]);
        if ((a == 'C' && b == 'O' && c == 'M') || (a == 'L' && b == 'P' && c == 'T')) return true;
    }
    return false;
}

/* Relative path of plain components only. */
static bool rpa_path_safe(const char *path)
{
    const char *cursor = path;
    if (!path || !path[0] || path[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') {
            unsigned char c = (unsigned char)*end;
            if (c < 0x20U || c == 0x7FU || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
            ++end;
        }
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (cursor[length - 1U] == '.' || cursor[length - 1U] == ' ') return false;
        if (rpa_is_device(cursor, length)) return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* Valid UTF-8 is kept; otherwise every byte is read as Latin-1.  Either way
 * backslashes become slashes. */
static char *rpa_name_text(const uint8_t *name, uint32_t length)
{
    size_t index = 0U, used = 0U;
    bool valid = true;
    char *out;
    while (index < length) {
        uint8_t c = name[index];
        size_t extra, k;
        uint32_t v, minimum;
        if (c == 0U) {
            valid = false;
            break;
        }
        if (c < 0x80U) {
            ++index;
            continue;
        }
        if ((c & 0xE0U) == 0xC0U) {
            extra = 1U;
            v = c & 0x1FU;
            minimum = 0x80U;
        } else if ((c & 0xF0U) == 0xE0U) {
            extra = 2U;
            v = c & 0x0FU;
            minimum = 0x800U;
        } else if ((c & 0xF8U) == 0xF0U) {
            extra = 3U;
            v = c & 0x07U;
            minimum = 0x10000U;
        } else {
            valid = false;
            break;
        }
        if (extra > length - index - 1U) {
            valid = false;
            break;
        }
        for (k = 1U; k <= extra; ++k) {
            if ((name[index + k] & 0xC0U) != 0x80U) {
                valid = false;
                break;
            }
            v = (v << 6) | (name[index + k] & 0x3FU);
        }
        if (!valid || v < minimum || v > 0x10FFFFU || (v >= 0xD800U && v <= 0xDFFFU) || (v >= 0x80U && v <= 0x9FU)) {
            valid = false;
            break;
        }
        index += extra + 1U;
    }
    out = (char *)xx_mem_alloc((size_t)length * 2U + 1U);
    if (!out) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = name[index];
        if (valid) {
            out[used++] = c == '\\' ? '/' : (char)c;
        } else if (c < 0x80U) {
            /* NUL would cut the name short; 0x01 keeps it refusable. */
            out[used++] = c == '\\' ? '/' : c == 0U ? (char)1 : (char)c;
        } else {
            rpa_put_utf8((uint8_t *)out, &used, c);
        }
    }
    out[used] = '\0';
    return out;
}

/* Case folding as far as case-insensitive file systems fold the scripts
 * game data uses (after xx_godot_engine_pck.c, MIT). */
static uint32_t rpa_fold_next(const char **cursor)
{
    const uint8_t *s = (const uint8_t *)*cursor;
    uint32_t c = s[0];
    size_t used = 1U;
    if (c == 0U) return 0U;
    if ((c & 0xE0U) == 0xC0U && s[1]) {
        c = ((c & 0x1FU) << 6) | (s[1] & 0x3FU);
        used = 2U;
    } else if ((c & 0xF0U) == 0xE0U && s[1] && s[2]) {
        c = ((c & 0x0FU) << 12) | ((uint32_t)(s[1] & 0x3FU) << 6) | (s[2] & 0x3FU);
        used = 3U;
    } else if ((c & 0xF8U) == 0xF0U && s[1] && s[2] && s[3]) {
        c = ((c & 0x07U) << 18) | ((uint32_t)(s[1] & 0x3FU) << 12) | ((uint32_t)(s[2] & 0x3FU) << 6) | (s[3] & 0x3FU);
        used = 4U;
    }
    *cursor += used;
    if (c >= 'a' && c <= 'z') return c - 0x20U;
    if (c < 0x80U) return c;
    if (c >= 0xE0U && c <= 0xFEU && c != 0xF7U) return c - 0x20U;
    if (c == 0xFFU) return 0x178U;
    if ((c >= 0x100U && c <= 0x137U) || (c >= 0x14AU && c <= 0x177U) || (c >= 0x1E00U && c <= 0x1EFFU) || (c >= 0x460U && c <= 0x481U) || (c >= 0x48AU && c <= 0x4BFU) ||
        (c >= 0x4D0U && c <= 0x52FU))
        return c & ~1U;
    if ((c >= 0x139U && c <= 0x148U) || (c >= 0x179U && c <= 0x17EU) || (c >= 0x4C1U && c <= 0x4CEU)) return (c & 1U) ? c : c - 1U;
    if (c >= 0x3B1U && c <= 0x3CBU && c != 0x3C2U) return c - 0x20U;
    if (c >= 0x430U && c <= 0x44FU) return c - 0x20U;
    if (c >= 0x450U && c <= 0x45FU) return c - 0x50U;
    if (c >= 0xFF41U && c <= 0xFF5AU) return c - 0x20U;
    return c;
}

static uint64_t rpa_hash(const char *text)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    uint32_t c;
    while ((c = rpa_fold_next(&text)) != 0U) {
        hash ^= (uint64_t)c;
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1U;
}

typedef struct rpa_set {
    uint64_t *keys;
    size_t mask, used;
} rpa_set;

static bool rpa_set_init(rpa_set *set, uint64_t expected)
{
    size_t size = 16U;
    xx_mem_zero(set, sizeof(*set));
    while ((uint64_t)size < expected * 2U + 2U && size < ((size_t)1 << 22)) size *= 2U;
    set->keys = (uint64_t *)xx_mem_calloc(size, sizeof(uint64_t));
    set->mask = size - 1U;
    return set->keys != NULL;
}

static void rpa_set_cleanup(rpa_set *set)
{
    if (set->keys) xx_mem_free(set->keys);
    xx_mem_zero(set, sizeof(*set));
}

static size_t rpa_set_slot(const rpa_set *set, uint64_t key)
{
    size_t slot = (size_t)(key ^ (key >> 29)) & set->mask;
    while (set->keys[slot] && set->keys[slot] != key) slot = (slot + 1U) & set->mask;
    return slot;
}

static bool rpa_set_has(const rpa_set *set, uint64_t key)
{
    return set->keys[rpa_set_slot(set, key)] != 0U;
}

static bool rpa_set_put(rpa_set *set, uint64_t key)
{
    size_t slot;
    if ((set->used + 1U) * 2U > set->mask + 1U) {
        rpa_set bigger;
        size_t index, size = set->mask + 1U;
        if (size >= ((size_t)1 << 24)) return false;
        bigger.keys = (uint64_t *)xx_mem_calloc(size * 2U, sizeof(uint64_t));
        if (!bigger.keys) return false;
        bigger.mask = size * 2U - 1U;
        bigger.used = 0U;
        for (index = 0U; index < size; ++index) {
            if (set->keys[index]) {
                bigger.keys[rpa_set_slot(&bigger, set->keys[index])] = set->keys[index];
                ++bigger.used;
            }
        }
        rpa_set_cleanup(set);
        *set = bigger;
    }
    slot = rpa_set_slot(set, key);
    if (!set->keys[slot]) {
        set->keys[slot] = key;
        ++set->used;
    }
    return true;
}

/* "dir/name.ext" with "_<n>" before the extension of the last component. */
static char *rpa_suffixed(const char *path, uint32_t number)
{
    size_t length = xx_str_len(path), last = 0U, dot = length, index;
    char digits[16];
    int written;
    char *out;
    for (index = 0U; index < length; ++index)
        if (path[index] == '/') last = index + 1U;
    for (index = length; index > last + 1U; --index) {
        if (path[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    }
    written = xx_rt_snprintf(digits, sizeof(digits), "_%u", (unsigned)number);
    if (written <= 0 || (size_t)written >= sizeof(digits)) return NULL;
    out = (char *)xx_mem_alloc(length + (size_t)written + 1U);
    if (!out) return NULL;
    xx_rt_memcpy(out, path, dot);
    xx_rt_memcpy(out + dot, digits, (size_t)written);
    xx_rt_memcpy(out + dot + (size_t)written, path + dot, length - dot);
    out[length + (size_t)written] = '\0';
    return out;
}

/* ------------------------------------------------------------- records -- */

typedef struct rpa_stream {
    rpa_layout layout;
    uint64_t index;
    char *display;     /* member name as text */
    char *output_name; /* safe, unique, or NULL when refused */
    rpa_set taken;
} rpa_stream;

static void rpa_stream_free(void *opaque)
{
    rpa_stream *stream = (rpa_stream *)opaque;
    if (!stream) return;
    rpa_layout_free(&stream->layout);
    if (stream->display) xx_mem_free(stream->display);
    if (stream->output_name) xx_mem_free(stream->output_name);
    rpa_set_cleanup(&stream->taken);
    xx_mem_free(stream);
}

static char *rpa_output_name(rpa_stream *stream, const char *display)
{
    size_t length = xx_str_len(display);
    char *path = (char *)xx_mem_alloc(length + 1U);
    uint64_t key;
    uint32_t next;
    if (!path) return NULL;
    xx_rt_memcpy(path, display, length + 1U);
    if (!rpa_path_safe(path)) {
        xx_mem_free(path);
        return NULL;
    }
    key = rpa_hash(path);
    if (!rpa_set_has(&stream->taken, key)) {
        if (!rpa_set_put(&stream->taken, key)) {
            xx_mem_free(path);
            return NULL;
        }
        return path;
    }
    for (next = 1U; next <= RPA_MAX_SUFFIX_TRIES; ++next) {
        char *candidate = rpa_suffixed(path, next);
        uint64_t candidate_key;
        if (!candidate) break;
        candidate_key = rpa_hash(candidate);
        if (!rpa_set_has(&stream->taken, candidate_key)) {
            if (!rpa_set_put(&stream->taken, candidate_key)) {
                xx_mem_free(candidate);
                break;
            }
            xx_mem_free(path);
            return candidate;
        }
        xx_mem_free(candidate);
    }
    xx_mem_free(path);
    return NULL;
}

static bool rpa_set_record(rpa_stream *stream, xx_archive_record *record)
{
    const rpa_member *member = &stream->layout.members[stream->index];
    if (stream->display) xx_mem_free(stream->display);
    if (stream->output_name) xx_mem_free(stream->output_name);
    stream->output_name = NULL;
    stream->display = rpa_name_text(member->name, member->name_length);
    if (!stream->display) return false;
    stream->output_name = rpa_output_name(stream, stream->display);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = stream->layout.base;
    record->header_size = stream->layout.header_size;
    record->data_offset = stream->layout.base + member->offset;
    record->compressed_size = (int64_t)(member->length - member->prefix_length);
    return xx_archive_record_set_original_name(record, stream->output_name ? stream->output_name : stream->display) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->length - member->prefix_length) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->length) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool rpa_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *rpa_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

/* Prefix, then the stored bytes, to @destination (NULL only reads). */
static bool rpa_copy_member(xx_io_device *device, int64_t base, const rpa_member *member, xx_io_device *destination, xx_pd_struct *pd)
{
    const size_t io_capacity = xx_get_file_buffer_size();
    uint8_t *buffer;
    int64_t offset = base + member->offset;
    uint64_t remaining = member->length - member->prefix_length;
    bool result = false;
    if (destination && member->prefix_length) {
        size_t written = 0U;
        while (written < member->prefix_length) {
            ssize_t sent = xx_io_write(destination, member->prefix + written, member->prefix_length - written);
            if (sent <= 0 || (size_t)sent > member->prefix_length - written) return false;
            written += (size_t)sent;
        }
    }
    buffer = (uint8_t *)xx_mem_alloc(io_capacity ? io_capacity : 65536U);
    if (!buffer) return false;
    while (remaining != 0U) {
        size_t cap = io_capacity ? io_capacity : 65536U;
        size_t chunk = remaining > cap ? cap : (size_t)remaining;
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) || !rpa_read_at(device, offset, buffer, chunk)) goto done;
        while (destination && written < chunk) {
            ssize_t sent = xx_io_write(destination, buffer + written, chunk - written);
            if (sent <= 0 || (size_t)sent > chunk - written) goto done;
            written += (size_t)sent;
        }
        offset += (int64_t)chunk;
        remaining -= chunk;
    }
    result = true;
done:
    xx_mem_free(buffer);
    return result;
}

/* --------------------------------------------------------------- api -- */

void xx_rpa_init(xx_rpa *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_RPA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-renpy-rpa");
    xx_format_set_extension(&archive->format, "rpa");
    archive->format.check_is_valid = xx_rpa_check_is_valid;
    archive->format.handle_base_info = xx_rpa_handle_base_info;
    archive->format.get_format_size = xx_rpa_get_format_size;
    archive->format.get_number_of_archive_records = xx_rpa_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_rpa_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_rpa_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_rpa_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_rpa_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_rpa_free_archive_records_reading;
}

xx_rpa *xx_rpa_create(xx_io_device *device, int64_t base_address)
{
    xx_rpa *archive = (xx_rpa *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_rpa_init(archive, device, base_address);
    return archive;
}

void xx_rpa_destroy(xx_rpa *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_rpa_free(xx_rpa *archive)
{
    if (!archive) return;
    xx_rpa_destroy(archive);
    xx_mem_free(archive);
}

bool xx_rpa_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    rpa_layout layout;
    bool ok = rpa_parse(format, &layout, false, pd);
    rpa_layout_free(&layout);
    return ok;
}

bool xx_rpa_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    rpa_layout layout;
    xx_rpa *archive;
    if (!format || !rpa_parse(format, &layout, false, pd)) return false;
    archive = (xx_rpa *)format;
    archive->number_of_records = layout.count;
    archive->index_offset = layout.index_offset;
    archive->key = layout.key;
    archive->version = layout.version;
    format->number_of_archive_records = layout.count;
    format->format_size = layout.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    rpa_layout_free(&layout);
    return true;
}

int64_t xx_rpa_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_rpa_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_rpa_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_rpa_handle_base_info(format, pd)) ? ((xx_rpa *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_rpa_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    rpa_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (rpa_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!rpa_parse(format, &stream->layout, true, pd) || !rpa_set_init(&stream->taken, stream->layout.count)) {
        rpa_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        rpa_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = rpa_stream_free;
    state->total_records = (int64_t)stream->layout.count;
    if (!rpa_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = stream->layout.count != 0U && rpa_set_record(stream, &state->current_record);
    return state;
}

const xx_archive_record *xx_rpa_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_rpa_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    rpa_stream *stream;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (rpa_stream *)state->internal_state) || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (stream->index + 1U >= stream->layout.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    if (!rpa_set_record(stream, &state->current_record)) {
        state->has_record = false;
        return false;
    }
    return true;
}

bool xx_rpa_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    rpa_stream *stream;
    const rpa_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool created = false;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (rpa_stream *)state->internal_state) || stream->index >= stream->layout.count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->layout.members[stream->index];
    path_option = rpa_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return rpa_copy_member(format->device, stream->layout.base, member, NULL, pd);
    if (!stream->output_name) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", stream->output_name)
                                                                                                  : xx_str_concat(base, stream->output_name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = rpa_copy_member(format->device, stream->layout.base, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_rpa_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
