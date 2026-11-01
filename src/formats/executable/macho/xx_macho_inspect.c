#include "xxfclib/formats/executable/macho/xx_macho_inspect.h"
#include "../../xx_executable_inspect_internal.h"
/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Native Mach-O inspection helpers used by DIE and other metadata queries.
 *
 * Walks the load commands to collect the LC_LOAD_DYLIB libraries (matched by
 * basename, as MACH_Script does), the sections (by sectname), and the
 * entry-point file offset from LC_MAIN. Fat binaries are a different file type
 * (Mach-O FAT) and are not handled here.
 */

#define MACH_MAGIC 0xFEEDFACEu
#define MACH_MAGIC_64 0xFEEDFACFu
#define MACH_CIGAM 0xCEFAEDFEu
#define MACH_CIGAM_64 0xCFFAEDFEu

#define MACH_LC_SEGMENT 0x1
#define MACH_LC_LOAD_DYLIB 0xC
#define MACH_LC_SEGMENT_64 0x19
#define MACH_LC_MAIN 0x80000028u
#define MACH_LC_VERSION_MIN_MACOSX 0x24
#define MACH_LC_VERSION_MIN_IPHONEOS 0x25
#define MACH_LC_VERSION_MIN_TVOS 0x2F
#define MACH_LC_VERSION_MIN_WATCHOS 0x30
#define MACH_LC_BUILD_VERSION 0x32
#define MACH_LC_SYMTAB 0x2
#define MACH_LC_DYLD_INFO 0x22
#define MACH_LC_DYLD_EXPORTS_TRIE 0x80000033u
#define MACH_N_STAB 0xe0
#define MACH_N_TYPE 0x0e
#define MACH_N_UNDF 0x00
#define MACH_N_EXT 0x01

static int mach_inspect_error(xx_executable_input *file, int code, const char *message)
{
    if (file) {
        file->failed = true;
        xx_pd_set_error(file->pd, code, message);
    }
    return 0;
}

/* Unlike best-effort metadata strings, table strings must terminate within
 * their declared range. Never turn a malformed name into a partial result. */
static char *mach_inspect_string(xx_executable_input *file, uint64_t offset, uint64_t maximum)
{
    xx_buf_t buffer;
    uint8_t bytes[256];
    xx_buf_init(&buffer);
    if (!file || offset > (uint64_t)file->size || maximum > (uint64_t)file->size - offset) goto invalid;
    while (maximum && buffer.size < 65536U) {
        size_t amount = maximum < sizeof(bytes) ? (size_t)maximum : sizeof(bytes), i;
        if (!xx_exec_read(file, (int64_t)offset, bytes, amount)) goto invalid;
        for (i = 0; i < amount && bytes[i]; ++i) {}
        if (!xx_exec_account_string(file, i) || !xx_buf_append(&buffer, bytes, i)) {
            mach_inspect_error(file, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate Mach-O table string");
            xx_buf_free(&buffer);
            return NULL;
        }
        if (i < amount) {
            char *result = xx_buf_detach(&buffer, NULL);
            if (!result) mach_inspect_error(file, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate Mach-O table string");
            return result;
        }
        maximum -= amount;
        offset += amount;
    }
invalid:
    xx_buf_free(&buffer);
    mach_inspect_error(file, XXFC_ERR_INVALID_ARG, "Invalid or unterminated Mach-O table string");
    return NULL;
}

typedef struct {
    uint32_t hash;
    uint32_t symbol_index_plus_one; /* Zero marks an unused slot. */
} MachExportSymbolSlot;

typedef struct {
    xx_executable_input *file;
    xx_macho_inspection *state;
    uint32_t offset;
    uint32_t size;
    uint32_t visited;
    uint32_t symbol_capacity;
    MachExportSymbolSlot *symbol_slots;
    uint32_t symbol_slot_capacity;
    uint32_t indexed_symbols;
    char name[4096];
} MachExportTrie;

static uint32_t mach_export_symbol_hash(const char *name)
{
    uint32_t hash = 2166136261U;
    const unsigned char *byte = (const unsigned char *)name;
    while (*byte) hash = (hash ^ *byte++) * 16777619U;
    return hash;
}

/* The index borrows names and stores array indices, so symbol reallocations
 * cannot invalidate it. Keep the first occurrence of a duplicate nlist name,
 * matching the former linear search. At most half the slots are occupied. */
static uint32_t mach_export_symbol_slot(MachExportTrie *trie, const char *name, uint32_t hash)
{
    uint32_t mask = trie->symbol_slot_capacity - 1U;
    uint32_t slot = hash & mask;
    for (uint32_t probes = 0; probes < trie->symbol_slot_capacity; ++probes) {
        const MachExportSymbolSlot *entry = &trie->symbol_slots[slot];
        if ((probes & 1023U) == 0 && xx_pd_is_stopped(trie->file->pd)) break;
        if (!entry->symbol_index_plus_one ||
            (entry->hash == hash &&
             !xx_rt_strcmp(trie->state->pSymbols[entry->symbol_index_plus_one - 1U].pName, name))) return slot;
        slot = (slot + 1U) & mask;
    }
    mach_inspect_error(trie->file, XXFC_ERR_INVALID_ARG, "Cannot search Mach-O export symbol index");
    return UINT32_MAX;
}

static int mach_export_symbol_index_grow(MachExportTrie *trie, uint32_t capacity)
{
    MachExportSymbolSlot *slots;
    uint32_t mask = capacity - 1U;
    /* The parser accepts at most 2^20 symbols. A half-full index therefore
     * needs at most 2^21 eight-byte slots (16 MiB). */
    if (capacity > 2097152U) return mach_inspect_error(trie->file, XXFC_ERR_INVALID_ARG, "Too many Mach-O export symbols");
    slots = (MachExportSymbolSlot *)xx_mem_calloc(capacity, sizeof(*slots));
    if (!slots) return mach_inspect_error(trie->file, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate Mach-O export symbol index");
    for (uint32_t i = 0; i < trie->symbol_slot_capacity; ++i) {
        MachExportSymbolSlot entry = trie->symbol_slots[i];
        uint32_t slot, probes = 0;
        if ((i & 1023U) == 0 && xx_pd_is_stopped(trie->file->pd)) {
            xx_mem_free(slots);
            return mach_inspect_error(trie->file, XXFC_ERR_INVALID_ARG, "Mach-O export symbol indexing cancelled");
        }
        if (!entry.symbol_index_plus_one) continue;
        slot = entry.hash & mask;
        while (slots[slot].symbol_index_plus_one) {
            if ((++probes & 1023U) == 0 && xx_pd_is_stopped(trie->file->pd)) {
                xx_mem_free(slots);
                return mach_inspect_error(trie->file, XXFC_ERR_INVALID_ARG, "Mach-O export symbol indexing cancelled");
            }
            slot = (slot + 1U) & mask;
        }
        slots[slot] = entry;
    }
    xx_mem_free(trie->symbol_slots);
    trie->symbol_slots = slots;
    trie->symbol_slot_capacity = capacity;
    return 1;
}

static int mach_export_symbol_index_init(MachExportTrie *trie)
{
    uint32_t capacity = 256U;
    while (capacity < (uint32_t)trie->state->nSymbolCount * 2U) capacity *= 2U;
    if (!mach_export_symbol_index_grow(trie, capacity)) return 0;
    for (int i = 0; i < trie->state->nSymbolCount; ++i) {
        const char *name = trie->state->pSymbols[i].pName;
        uint32_t hash, slot;
        if (!name) continue;
        hash = mach_export_symbol_hash(name);
        slot = mach_export_symbol_slot(trie, name, hash);
        if (slot == UINT32_MAX) return 0;
        if (!trie->symbol_slots[slot].symbol_index_plus_one) {
            trie->symbol_slots[slot].hash = hash;
            trie->symbol_slots[slot].symbol_index_plus_one = (uint32_t)i + 1U;
            ++trie->indexed_symbols;
        }
    }
    return 1;
}

static int mach_uleb(xx_executable_input *file, uint64_t *position, uint64_t end, uint64_t *value)
{
    uint64_t result = 0;
    unsigned shift = 0;
    while (*position < end && shift < 64) {
        uint8_t byte = xx_exec_u8(file, (int64_t)(*position)++);
        if (file->failed || (shift == 63 && (byte & 0x7e))) return 0;
        result |= (uint64_t)(byte & 0x7f) << shift;
        if (!(byte & 0x80)) { *value = result; return 1; }
        shift += 7;
    }
    return 0;
}

static int mach_export_address(MachExportTrie *trie, uint64_t flags, uint64_t relative, uint64_t *address)
{
    if ((flags & 3U) == 2U) { *address = relative; return 1; }
    if ((flags & 3U) == 3U) return 0;
    if (trie->state->nImageBase == UINT64_MAX) { *address = UINT64_MAX; return 1; }
    if (relative >= UINT64_MAX - trie->state->nImageBase) return 0;
    *address = trie->state->nImageBase + relative;
    return 1;
}

static int mach_export_trie_node(MachExportTrie *trie, uint64_t node, unsigned depth)
{
    uint64_t cursor, terminal_size, terminal_end, flags = 0, value = 0, address = UINT64_MAX;
    uint64_t resolver = UINT64_MAX, ordinal = 0;
    char *forwarder = NULL;
    uint8_t children;
    size_t prefix;
    if (depth > 256 || ++trie->visited > 1048576U || node >= trie->size || xx_pd_is_stopped(trie->file->pd)) goto invalid;
    cursor = (uint64_t)trie->offset + node;
    if (!mach_uleb(trie->file, &cursor, (uint64_t)trie->offset + trie->size, &terminal_size) ||
        terminal_size > (uint64_t)trie->offset + trie->size - cursor) goto invalid;
    terminal_end = cursor + terminal_size;
    if (terminal_size) {
        xx_macho_inspect_symbol *symbol = NULL;
        uint32_t hash, slot;
        if (!trie->name[0] || !mach_uleb(trie->file, &cursor, terminal_end, &flags)) goto invalid;
        if (flags & 0x08U) {
            char *target;
            const char *public_name, *library;
            size_t library_length, target_length;
            if (!mach_uleb(trie->file, &cursor, terminal_end, &ordinal) || !ordinal || ordinal > (uint64_t)trie->state->nLibraryCount) goto invalid;
            target = mach_inspect_string(trie->file, cursor, terminal_end - cursor);
            if (!target) return 0;
            if ((uint64_t)xx_rt_strlen(target) + 1 != terminal_end - cursor) { xx_mem_free(target); goto invalid; }
            public_name = target[0] ? target : trie->name;
            library = trie->state->pLibraries[ordinal - 1].pName;
            library_length = xx_rt_strlen(library);
            target_length = xx_rt_strlen(public_name);
            if (!xx_exec_account_string(trie->file, library_length + target_length + 1)) { xx_mem_free(target); goto invalid; }
            forwarder = (char *)xx_mem_alloc(library_length + target_length + 2);
            if (!forwarder) { xx_mem_free(target); goto memory; }
            xx_rt_memcpy(forwarder, library, library_length);
            forwarder[library_length] = '.';
            xx_rt_memcpy(forwarder + library_length + 1, public_name, target_length + 1);
            xx_mem_free(target);
        } else {
            if (!mach_uleb(trie->file, &cursor, terminal_end, &value) || !mach_export_address(trie, flags, value, &address)) goto invalid;
            if (flags & 0x10U) {
                if (!mach_uleb(trie->file, &cursor, terminal_end, &value) || !mach_export_address(trie, 0, value, &resolver)) goto invalid;
            }
            if (cursor != terminal_end) goto invalid;
        }
        hash = mach_export_symbol_hash(trie->name);
        slot = mach_export_symbol_slot(trie, trie->name, hash);
        if (slot == UINT32_MAX) { xx_mem_free(forwarder); return 0; }
        if (trie->symbol_slots[slot].symbol_index_plus_one) {
            symbol = &trie->state->pSymbols[trie->symbol_slots[slot].symbol_index_plus_one - 1U];
        }
        if (!symbol) {
            uint32_t count = (uint32_t)trie->state->nSymbolCount;
            if (count >= 1048576U) goto invalid;
            if (trie->indexed_symbols >= trie->symbol_slot_capacity / 2U) {
                if (!mach_export_symbol_index_grow(trie, trie->symbol_slot_capacity * 2U)) {
                    xx_mem_free(forwarder);
                    return 0;
                }
                slot = mach_export_symbol_slot(trie, trie->name, hash);
                if (slot == UINT32_MAX) { xx_mem_free(forwarder); return 0; }
            }
            if (count == trie->symbol_capacity) {
                uint32_t next = trie->symbol_capacity ? trie->symbol_capacity * 2U : 256U;
                xx_macho_inspect_symbol *symbols;
                if (next > 1048576U) next = 1048576U;
                symbols = (xx_macho_inspect_symbol *)xx_mem_realloc(trie->state->pSymbols, (size_t)next * sizeof(*symbols));
                if (!symbols) goto memory;
                trie->state->pSymbols = symbols;
                trie->symbol_capacity = next;
            }
            symbol = &trie->state->pSymbols[count];
            xx_rt_memset(symbol, 0, sizeof(*symbol));
            symbol->pName = xx_exec_copy_string(trie->file, trie->name);
            if (!symbol->pName) goto memory;
            symbol->nValue = address == UINT64_MAX ? 0 : address;
            symbol->nType = 0x01; /* External; section provenance is unknown. */
            symbol->bExternal = 1;
            symbol->nTableIndex = UINT64_MAX;
            symbol->nRecordOffset = -1;
            ++trie->state->nSymbolCount;
            trie->symbol_slots[slot].hash = hash;
            trie->symbol_slots[slot].symbol_index_plus_one = count + 1U;
            ++trie->indexed_symbols;
        }
        if (symbol->bFromExportTrie) goto invalid;
        symbol->bExported = symbol->bFromExportTrie = 1;
        symbol->nExportFlags = flags;
        symbol->nExportAddress = address;
        symbol->nResolverAddress = resolver;
        symbol->nReexportOrdinal = ordinal;
        xx_mem_free(symbol->pForwarder);
        symbol->pForwarder = forwarder;
        forwarder = NULL;
    }
    cursor = terminal_end;
    if (cursor >= (uint64_t)trie->offset + trie->size) goto invalid;
    children = xx_exec_u8(trie->file, (int64_t)cursor++);
    prefix = xx_rt_strlen(trie->name);
    for (uint8_t child = 0; child < children; ++child) {
        uint64_t child_node;
        size_t edge_length = 0;
        while (cursor < (uint64_t)trie->offset + trie->size && prefix + edge_length + 1 < sizeof(trie->name)) {
            uint8_t byte = xx_exec_u8(trie->file, (int64_t)cursor++);
            if (!byte) break;
            trie->name[prefix + edge_length++] = (char)byte;
        }
        if (cursor > (uint64_t)trie->offset + trie->size || prefix + edge_length + 1 >= sizeof(trie->name) ||
            !mach_uleb(trie->file, &cursor, (uint64_t)trie->offset + trie->size, &child_node)) goto invalid;
        trie->name[prefix + edge_length] = '\0';
        if (!mach_export_trie_node(trie, child_node, depth + 1)) return 0;
        trie->name[prefix] = '\0';
    }
    return !trie->file->failed;
invalid:
    xx_mem_free(forwarder);
    return mach_inspect_error(trie->file, XXFC_ERR_INVALID_ARG, "Malformed Mach-O export trie");
memory:
    xx_mem_free(forwarder);
    return mach_inspect_error(trie->file, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate Mach-O export record");
}

/* CPU types (XMACH_DEF). */
#define MACH_CPU_MC680x0 0x6
#define MACH_CPU_I386 0x7
#define MACH_CPU_X86_64 0x1000007
#define MACH_CPU_ARM 0xC
#define MACH_CPU_ARM64 0x100000C
#define MACH_CPU_POWERPC 0x12
#define MACH_CPU_POWERPC64 0x1000012

#define MACH_SUBTYPE_ARM_V6 6
#define MACH_SUBTYPE_ARM_V7 9

/* Platforms (build_version_command). */
#define MACH_PLAT_MACOS 1
#define MACH_PLAT_IOS 2
#define MACH_PLAT_TVOS 3
#define MACH_PLAT_WATCHOS 4
#define MACH_PLAT_BRIDGEOS 5
#define MACH_PLAT_MACCATALYST 6
#define MACH_PLAT_IOSSIMULATOR 7
#define MACH_PLAT_TVOSSIMULATOR 8
#define MACH_PLAT_WATCHOSSIMULATOR 9
#define MACH_PLAT_DRIVERKIT 10
#define MACH_PLAT_FIRMWARE 13
#define MACH_PLAT_SEPOS 14

#define MACH_FULL_VERSION(a, b, c) (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(c))

/* getHeaderCpuTypesS: CPU type -> arch name. */
static const char *mach_cpu_type_name(uint32_t nType)
{
    switch (nType) {
        case 1: return "VAX";
        case 2: return "ROMP";
        case 4: return "NS32032";
        case 5: return "NS32332";
        case 6: return "MC680x0";
        case 7: return "I386";
        case 0x1000007: return "X86_64";
        case 8: return "MIPS";
        case 9: return "NS32532";
        case 0xB: return "HPPA";
        case 0xC: return "ARM";
        case 0x100000C: return "ARM64";
        case 0x200000C: return "ARM64_32";
        case 0xD: return "MC88000";
        case 0xE: return "SPARC";
        case 0xF: return "I860";
        case 0x10: return "I860_LITTLE";
        case 0x11: return "RS6000";
        case 0x12: return "POWERPC";
        case 0x1000012: return "POWERPC64";
        case 255: return "VEO";
        default: return "Unknown";
    }
}

static const char *mach_mc680_subtype(uint32_t nSub)
{
    switch (nSub) {
        case 1: return "MC68030";
        case 2: return "MC68040";
        case 3: return "MC68030_ONLY";
        default: return NULL;
    }
}

static const char *mach_arm_subtype(uint32_t nSub)
{
    switch (nSub) {
        case 0: return "ARM_ALL";
        case 1: return "ARM_A500_ARCH";
        case 2: return "ARM_A500";
        case 3: return "ARM_A440";
        case 4: return "ARM_M4";
        case 5: return "ARM_V4T";
        case 6: return "ARM_V6";
        case 7: return "ARM_V5TEJ";
        case 8: return "ARM_XSCALE";
        case 9: return "ARM_V7";
        case 10: return "ARM_V7F";
        case 11: return "ARM_V7S";
        case 12: return "ARM_V7K";
        case 14: return "ARM_V6M";
        case 15: return "ARM_V7M";
        case 16: return "ARM_V7EM";
        case 0x80000002: return "ARM64E";
        default: return NULL;
    }
}

/* xx_macho_inspection::_getArch: the CPU-type name, refined by the sub-type for the MC680x0
 * and ARM families. */
static const char *mach_arch_name(uint32_t nType, uint32_t nSub)
{
    const char *pName = mach_cpu_type_name(nType);

    if (nType == MACH_CPU_MC680x0) {
        const char *pSub = mach_mc680_subtype(nSub);

        if (pSub != NULL) {
            pName = pSub;
        }
    } else if ((nType == MACH_CPU_ARM) || (nType == MACH_CPU_ARM64)) {
        if (nSub != 0) {
            const char *pSub = mach_arm_subtype(nSub);

            if (pSub != NULL) {
                pName = pSub;
            }
        }
    }

    return pName;
}

/* XBinary::get_uint32_full_version: "X.Y.Z" from packed nibbles. */
static void mach_full_version(uint32_t nValue, char *pOut, size_t nOutSize)
{
    xx_rt_snprintf(pOut, nOutSize, "%u.%u.%u", (unsigned)((nValue >> 16) & 0xFFFF), (unsigned)((nValue >> 8) & 0xFF), (unsigned)(nValue & 0xFF));
}

static void mach_set_str(char *pDst, size_t nDstSize, const char *pSrc)
{
    xx_rt_strncpy(pDst, pSrc, nDstSize - 1);
    pDst[nDstSize - 1] = 0;
}

/* The Foundation-library current version maps to a macOS/iOS release when no
 * LC_BUILD_VERSION / LC_VERSION_MIN command carries the version. Mirrors the
 * fallback branch of xx_macho_inspection::getFileFormatInfo. */
static void mach_foundation_fallback(xx_macho_inspection *pMach, const char **ppOsName, char *pVer, size_t nVerSize)
{
    uint32_t v = 0;

    if (!xx_macho_inspect_library_present(pMach, "Foundation")) {
        return;
    }

    v = xx_macho_inspect_library_current_version(pMach, "Foundation");

    if ((xx_rt_strcmp(*ppOsName, "Mac OS X") == 0) || (xx_rt_strcmp(*ppOsName, "OS X") == 0) || (xx_rt_strcmp(*ppOsName, "macOS") == 0)) {
        if ((v >= MACH_FULL_VERSION(397, 40, 0)) && (v < MACH_FULL_VERSION(425, 0, 0))) mach_set_str(pVer, nVerSize, "10.0.0");
        else if (v < MACH_FULL_VERSION(567, 0, 0)) mach_set_str(pVer, nVerSize, "10.3.0");
        else if (v < MACH_FULL_VERSION(677, 0, 0)) mach_set_str(pVer, nVerSize, "10.4.0");
        else if (v < MACH_FULL_VERSION(677, 24, 0)) mach_set_str(pVer, nVerSize, "10.5.0");
        else if (v < MACH_FULL_VERSION(751, 0, 0)) mach_set_str(pVer, nVerSize, "10.5.7");
        else if (v < MACH_FULL_VERSION(833, 10, 0)) mach_set_str(pVer, nVerSize, "10.6.0");
        else if (v < MACH_FULL_VERSION(833, 25, 0)) mach_set_str(pVer, nVerSize, "10.7.0");
        else if (v < MACH_FULL_VERSION(945, 18, 0)) mach_set_str(pVer, nVerSize, "10.7.4");
        else if (v < MACH_FULL_VERSION(1151, 16, 0)) mach_set_str(pVer, nVerSize, "10.8.4");
        else if (v < MACH_FULL_VERSION(1200, 0, 0)) mach_set_str(pVer, nVerSize, "10.10.0");

        if (v < MACH_FULL_VERSION(833, 10, 0)) {
            *ppOsName = "Mac OS X";
        }
    } else if ((xx_rt_strcmp(*ppOsName, "iPhone OS") == 0) || (xx_rt_strcmp(*ppOsName, "iOS") == 0) || (xx_rt_strcmp(*ppOsName, "iPadOS") == 0)) {
        if (v < MACH_FULL_VERSION(678, 24, 0)) mach_set_str(pVer, nVerSize, "1.0.0");
        else if (v < MACH_FULL_VERSION(678, 26, 0)) mach_set_str(pVer, nVerSize, "2.0.0");
        else if (v < MACH_FULL_VERSION(678, 29, 0)) mach_set_str(pVer, nVerSize, "2.1.0");
        else if (v < MACH_FULL_VERSION(678, 47, 0)) mach_set_str(pVer, nVerSize, "2.2.0");
        else if (v < MACH_FULL_VERSION(678, 51, 0)) mach_set_str(pVer, nVerSize, "3.0.0");
        else if (v < MACH_FULL_VERSION(678, 60, 0)) mach_set_str(pVer, nVerSize, "3.1.0");
        else if (v < MACH_FULL_VERSION(751, 32, 0)) mach_set_str(pVer, nVerSize, "3.2.0");
        else if (v < MACH_FULL_VERSION(751, 37, 0)) mach_set_str(pVer, nVerSize, "4.0.0");
        else if (v < MACH_FULL_VERSION(751, 49, 0)) mach_set_str(pVer, nVerSize, "4.1.0");
        else if (v < MACH_FULL_VERSION(881, 0, 0)) mach_set_str(pVer, nVerSize, "4.2.0");
        else if (v < MACH_FULL_VERSION(890, 10, 0)) mach_set_str(pVer, nVerSize, "5.0.0");
        else if (v < MACH_FULL_VERSION(992, 0, 0)) mach_set_str(pVer, nVerSize, "5.1.0");
        else if (v < MACH_FULL_VERSION(993, 0, 0)) mach_set_str(pVer, nVerSize, "6.0.0");
        else if (v < MACH_FULL_VERSION(1047, 20, 0)) mach_set_str(pVer, nVerSize, "6.1.0");
        else if (v < MACH_FULL_VERSION(1047, 25, 0)) mach_set_str(pVer, nVerSize, "7.0.0");
        else if (v < MACH_FULL_VERSION(1140, 11, 0)) mach_set_str(pVer, nVerSize, "7.1.0");
        else if (v < MACH_FULL_VERSION(1141, 1, 0)) mach_set_str(pVer, nVerSize, "8.0.0");
        else if (v < MACH_FULL_VERSION(1142, 14, 0)) mach_set_str(pVer, nVerSize, "8.1.0");
        else if (v < MACH_FULL_VERSION(1144, 17, 0)) mach_set_str(pVer, nVerSize, "8.2.0");
        else if (v < MACH_FULL_VERSION(1200, 0, 0)) mach_set_str(pVer, nVerSize, "8.3.0");

        *ppOsName = (v < MACH_FULL_VERSION(751, 32, 0)) ? "iPhone OS" : "iOS";
    }
}

static void mach_compute_os(xx_macho_inspection *pMach, int bBuildVer, uint32_t nPlatform, uint32_t nMinos, int bVersionMin, uint32_t nVersionMinCmd,
                            uint32_t nVersionMinValue)
{
    uint32_t nType = pMach->nCpuType;
    uint32_t nSub = pMach->nCpuSubType;
    const char *pOsName = "Mac OS";
    char sVer[24];

    sVer[0] = 0;

    mach_set_str(pMach->sArch, sizeof(pMach->sArch), mach_arch_name(nType, nSub));

    /* CPU-type default OS + version range. */
    if (nType == MACH_CPU_MC680x0) {
        pOsName = "Mac OS";
        mach_set_str(sVer, sizeof(sVer), "1.0-8.1");
    } else if (nType == MACH_CPU_POWERPC) {
        pOsName = "Mac OS";
        mach_set_str(sVer, sizeof(sVer), "7.1.2-9.22");
    } else if (nType == MACH_CPU_POWERPC64) {
        pOsName = "Mac OS X";
        mach_set_str(sVer, sizeof(sVer), "10.4-10.6");
    } else if ((nType == MACH_CPU_I386) || (nType == MACH_CPU_X86_64)) {
        pOsName = "Mac OS X";
        mach_set_str(sVer, sizeof(sVer), "10.4-10.14");
    } else if ((nType == MACH_CPU_ARM) || (nType == MACH_CPU_ARM64)) {
        pOsName = "iOS";

        if (nSub == MACH_SUBTYPE_ARM_V6) {
            pOsName = "iPhone OS";
            mach_set_str(sVer, sizeof(sVer), "1.0-4.2.1");
        } else if (nSub == MACH_SUBTYPE_ARM_V7) {
            pOsName = "iPhone OS";
            mach_set_str(sVer, sizeof(sVer), "3.0-10.3.4");
        } else if (nType == MACH_CPU_ARM64) {
            pOsName = "iOS";
            mach_set_str(sVer, sizeof(sVer), "7.0-16.0");
        }
    }

    /* A LC_VERSION_MIN command names the OS even before its version is read. */
    if (!bBuildVer && bVersionMin) {
        if (nVersionMinCmd == MACH_LC_VERSION_MIN_IPHONEOS) pOsName = "iOS";
        else if (nVersionMinCmd == MACH_LC_VERSION_MIN_MACOSX) pOsName = "macOS";
        else if (nVersionMinCmd == MACH_LC_VERSION_MIN_TVOS) pOsName = "tvOS";
        else if (nVersionMinCmd == MACH_LC_VERSION_MIN_WATCHOS) pOsName = "watchOS";
    }

    if (bBuildVer) {
        if (nPlatform == MACH_PLAT_MACOS) pOsName = "macOS";
        else if ((nPlatform == MACH_PLAT_IOS) || (nPlatform == MACH_PLAT_IOSSIMULATOR)) pOsName = "iOS";
        else if ((nPlatform == MACH_PLAT_TVOS) || (nPlatform == MACH_PLAT_TVOSSIMULATOR)) pOsName = "tvOS";
        else if ((nPlatform == MACH_PLAT_WATCHOS) || (nPlatform == MACH_PLAT_WATCHOSSIMULATOR)) pOsName = "watchOS";
        else if (nPlatform == MACH_PLAT_BRIDGEOS) pOsName = "bridgeOS";
        else if (nPlatform == MACH_PLAT_MACCATALYST) pOsName = "Mac Catalyst";
        else if (nPlatform == MACH_PLAT_DRIVERKIT) pOsName = "Mac DriverKit";
        else if (nPlatform == MACH_PLAT_FIRMWARE) pOsName = "Mac Firmware";
        else if (nPlatform == MACH_PLAT_SEPOS) pOsName = "sepOS";

        if (nMinos) {
            mach_full_version(nMinos, sVer, sizeof(sVer));
        }
    } else if (bVersionMin) {
        mach_full_version(nVersionMinValue, sVer, sizeof(sVer));
    } else {
        mach_foundation_fallback(pMach, &pOsName, sVer, sizeof(sVer));
    }

    mach_set_str(pMach->sOsName, sizeof(pMach->sOsName), pOsName);
    mach_set_str(pMach->sOsVersion, sizeof(pMach->sOsVersion), sVer);
}

/* basename after the last '/'. */
static char *mach_basename(const char *pPath)
{
    const char *pLast = pPath;
    const char *p = pPath;

    for (; *p; p++) {
        if (*p == '/') {
            pLast = p + 1;
        }
    }

    return xx_str_create(pLast);
}

static int inspect_parse_input(xx_executable_input *pFile, xx_macho_inspection *pMach)
{
    uint32_t nMagic = 0;
    int bBE = 0;
    int b64 = 0;
    uint32_t nCmds = 0;
    int64_t nHeaderSize = 0;
    int64_t commands_end = 0;
    int64_t nOffset = 0;
    uint32_t i = 0;
    xx_list_s vecLibs;
    xx_list_s vecSections;
    int bBuildVer = 0;
    uint32_t nPlatform = 0;
    uint32_t nMinos = 0;
    int bVersionMin = 0;
    uint32_t nVersionMinCmd = 0;
    uint32_t nVersionMinValue = 0;
    uint32_t nSymOffset = 0, nSymCount = 0, nStrOffset = 0, nStrSize = 0;
    uint32_t nExportOffset = 0, nExportSize = 0;

    xx_rt_memset(pMach, 0, sizeof(xx_macho_inspection));
    pMach->pInput = pFile;
    pMach->nEntryPointOffset = -1;
    pMach->nImageBase = UINT64_MAX;
    xx_list_init(&vecLibs, sizeof(xx_macho_inspect_library), NULL);
    xx_list_init(&vecSections, sizeof(xx_macho_inspect_section), NULL);

    if ((pFile == NULL) || (pFile->size < 0x1C)) {
        return 0;
    }

    nMagic = xx_exec_u32(pFile, 0, false);

    if ((nMagic == MACH_MAGIC) || (nMagic == MACH_MAGIC_64)) {
        bBE = 0;
    } else if ((nMagic == MACH_CIGAM) || (nMagic == MACH_CIGAM_64)) {
        bBE = 1;
    } else {
        return 0;
    }

    b64 = ((nMagic == MACH_MAGIC_64) || (nMagic == MACH_CIGAM_64)) ? 1 : 0;
    pMach->bIs64 = b64;
    pMach->bBigEndian = bBE;
    pMach->nCpuType = xx_exec_u32(pFile, 4, bBE ? true : false);
    pMach->nCpuSubType = xx_exec_u32(pFile, 8, bBE ? true : false);

    nCmds = xx_exec_u32(pFile, 16, bBE ? true : false); /* ncmds */
    nHeaderSize = b64 ? 32 : 28;
    nOffset = nHeaderSize;

    if (pFile->size < nHeaderSize || nCmds > 0x10000) return 0;
    commands_end = nHeaderSize + xx_exec_u32(pFile, 20, bBE != 0);
    if (commands_end > pFile->size || commands_end - nHeaderSize < (int64_t)nCmds * 8) return 0;

    for (i = 0; i < nCmds && !pFile->failed && !xx_pd_is_stopped(pFile->pd); i++) {
        uint32_t nCmd = 0;
        uint32_t nCmdSize = 0;

        if (nOffset + 8 > pFile->size) {
            break;
        }

        nCmd = xx_exec_u32(pFile, nOffset, bBE ? true : false);
        nCmdSize = xx_exec_u32(pFile, nOffset + 4, bBE ? true : false);

        if ((nCmdSize < 8) || (nOffset + nCmdSize > commands_end)) {
            break;
        }
        if ((nCmd == MACH_LC_SYMTAB && nCmdSize < 24) ||
            (nCmd == MACH_LC_DYLD_EXPORTS_TRIE && nCmdSize < 16) ||
            ((nCmd & 0x7fffffffU) == MACH_LC_DYLD_INFO && nCmdSize < 48) ||
            ((nCmd == MACH_LC_LOAD_DYLIB || nCmd == 0x80000018U || nCmd == 0x20U || nCmd == 0x8000001fU || nCmd == 0x80000023U) && nCmdSize < 24) ||
            (nCmd == MACH_LC_SEGMENT && nCmdSize < 56) ||
            (nCmd == MACH_LC_SEGMENT_64 && nCmdSize < 72)) {
            mach_inspect_error(pFile, XXFC_ERR_INVALID_ARG, "Short Mach-O load command");
            break;
        }

        if (nCmd == MACH_LC_SYMTAB && nCmdSize >= 24) {
            nSymOffset = xx_exec_u32(pFile, nOffset + 8, bBE != 0);
            nSymCount = xx_exec_u32(pFile, nOffset + 12, bBE != 0);
            nStrOffset = xx_exec_u32(pFile, nOffset + 16, bBE != 0);
            nStrSize = xx_exec_u32(pFile, nOffset + 20, bBE != 0);
        } else if (nCmd == MACH_LC_DYLD_EXPORTS_TRIE && nCmdSize >= 16) {
            nExportOffset = xx_exec_u32(pFile, nOffset + 8, bBE != 0);
            nExportSize = xx_exec_u32(pFile, nOffset + 12, bBE != 0);
        } else if ((nCmd & 0x7fffffffu) == MACH_LC_DYLD_INFO && nCmdSize >= 48) {
            nExportOffset = xx_exec_u32(pFile, nOffset + 40, bBE != 0);
            nExportSize = xx_exec_u32(pFile, nOffset + 44, bBE != 0);
        } else if ((nCmd == MACH_LC_LOAD_DYLIB || nCmd == 0x80000018u || nCmd == 0x20u || nCmd == 0x8000001fu || nCmd == 0x80000023u) && nCmdSize >= 24) {
            /* dylib_command: load_command(8) + name(u32 offset from cmd),
             * timestamp, current_version, compatibility_version. */
            uint32_t nNameOffset = xx_exec_u32(pFile, nOffset + 8, bBE ? true : false);
            uint32_t nCurrentVersion = xx_exec_u32(pFile, nOffset + 16, bBE ? true : false);
            char *pFullName;
            if (nNameOffset < 24 || nNameOffset >= nCmdSize) {
                mach_inspect_error(pFile, XXFC_ERR_INVALID_ARG, "Invalid Mach-O dylib name offset");
                break;
            }
            pFullName = mach_inspect_string(pFile, (uint64_t)nOffset + nNameOffset, nCmdSize - nNameOffset);
            if (!pFullName) {
                pFile->failed = true;
                break;
            }
            xx_macho_inspect_library library = {0};
            xx_macho_inspect_library *pLib = &library;

            pLib->pName = mach_basename(pFullName);
            if (!pLib->pName) pFile->failed = true;
            pLib->nCurrentVersion = nCurrentVersion;
            xx_mem_free(pFullName);
            if (!xx_list_append(&vecLibs, pLib)) {
                xx_mem_free(pLib->pName);
                pFile->failed = true;
                break;
            }
        } else if ((nCmd == MACH_LC_SEGMENT) || (nCmd == MACH_LC_SEGMENT_64)) {
            int bSeg64 = (nCmd == MACH_LC_SEGMENT_64) ? 1 : 0;
            int64_t nNsectsOffset = nOffset + (bSeg64 ? 64 : 48);
            uint32_t nNsects = xx_exec_u32(pFile, nNsectsOffset, bBE ? true : false);
            int64_t nSectOffset = nOffset + (bSeg64 ? 72 : 56);
            int64_t nSectSize = bSeg64 ? 80 : 68;
            int64_t nCmdEnd = nOffset + (int64_t)nCmdSize;
            uint32_t s = 0;
            uint64_t nVmAddress = bSeg64 ? xx_exec_u64(pFile, nOffset + 24, bBE != 0) : xx_exec_u32(pFile, nOffset + 24, bBE != 0);
            uint64_t nFileOffset = bSeg64 ? xx_exec_u64(pFile, nOffset + 40, bBE != 0) : xx_exec_u32(pFile, nOffset + 32, bBE != 0);
            uint64_t nFileSize = bSeg64 ? xx_exec_u64(pFile, nOffset + 48, bBE != 0) : xx_exec_u32(pFile, nOffset + 36, bBE != 0);
            if (!nFileOffset && nFileSize && nVmAddress < pMach->nImageBase) pMach->nImageBase = nVmAddress;

            if ((uint64_t)nNsects > (uint64_t)(nCmdEnd - nSectOffset) / (uint64_t)nSectSize ||
                nNsects > 1048576U - vecSections.count) {
                mach_inspect_error(pFile, XXFC_ERR_INVALID_ARG, "Invalid Mach-O section count");
                break;
            }

            for (s = 0; s < nNsects; s++) {
                xx_macho_inspect_section section = {0};
                xx_macho_inspect_section *pSection = &section;

                if ((nSectOffset + nSectSize > pFile->size) || (nSectOffset + nSectSize > nCmdEnd)) {
                    break;
                }

                xx_exec_read(pFile, nSectOffset, pSection->sName, 16);
                pSection->sName[16] = 0;

                if (bSeg64) {
                    pSection->nSize = xx_exec_u64(pFile, nSectOffset + 40, bBE ? true : false);
                    pSection->nOffset = xx_exec_u32(pFile, nSectOffset + 48, bBE ? true : false);
                } else {
                    pSection->nSize = xx_exec_u32(pFile, nSectOffset + 36, bBE ? true : false);
                    pSection->nOffset = xx_exec_u32(pFile, nSectOffset + 40, bBE ? true : false);
                }

                if (!xx_list_append(&vecSections, pSection)) {
                    pFile->failed = true;
                    break;
                }
                nSectOffset += nSectSize;
            }
        } else if (nCmd == MACH_LC_MAIN && nCmdSize >= 24) {
            /* entry_point_command: entryoff is a file offset. */
            pMach->nEntryPointOffset = (int64_t)xx_exec_u64(pFile, nOffset + 8, bBE ? true : false);
        } else if (nCmd == MACH_LC_BUILD_VERSION && nCmdSize >= 24) {
            if (!bBuildVer) {
                bBuildVer = 1;
                nPlatform = xx_exec_u32(pFile, nOffset + 8, bBE ? true : false);
                nMinos = xx_exec_u32(pFile, nOffset + 12, bBE ? true : false);
            }
        } else if ((nCmd == MACH_LC_VERSION_MIN_MACOSX) || (nCmd == MACH_LC_VERSION_MIN_IPHONEOS) || (nCmd == MACH_LC_VERSION_MIN_TVOS) ||
                   (nCmd == MACH_LC_VERSION_MIN_WATCHOS)) {
            if (!bVersionMin && nCmdSize >= 16) {
                bVersionMin = 1;
                nVersionMinCmd = nCmd;
                nVersionMinValue = xx_exec_u32(pFile, nOffset + 8, bBE ? true : false);
            }
        }

        nOffset += nCmdSize;
    }

    mach_compute_os(pMach, bBuildVer, nPlatform, nMinos, bVersionMin, nVersionMinCmd, nVersionMinValue);

    pMach->nLibraryCount = (int)vecLibs.count;
    pMach->pLibraries = (xx_macho_inspect_library *)vecLibs.data;
    pMach->nSectionCount = (int)vecSections.count;
    pMach->pSections = (xx_macho_inspect_section *)vecSections.data;
    if (nSymCount) {
        uint64_t stride = b64 ? 16U : 12U;
        if (nSymCount > 1048576U || nSymOffset > (uint64_t)pFile->size || nStrOffset > (uint64_t)pFile->size ||
            !nStrSize || nStrSize > (uint64_t)pFile->size - nStrOffset ||
            (uint64_t)nSymCount > ((uint64_t)pFile->size - nSymOffset) / stride) {
            mach_inspect_error(pFile, XXFC_ERR_OUT_OF_BOUNDS, "Mach-O symbol table outside input");
        } else {
            pMach->pSymbols = (xx_macho_inspect_symbol *)xx_mem_calloc(nSymCount, sizeof(*pMach->pSymbols));
            if (!pMach->pSymbols) mach_inspect_error(pFile, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate Mach-O symbols");
            for (uint32_t si = 0; pMach->pSymbols && si < nSymCount && !pFile->failed; ++si) {
                int64_t so = (int64_t)nSymOffset + (int64_t)si * (int64_t)stride;
                uint32_t strx = xx_exec_u32(pFile, so, bBE != 0);
                uint8_t type = xx_exec_u8(pFile, so + 4);
                uint8_t sect = xx_exec_u8(pFile, so + 5);
                uint16_t desc = xx_exec_u16(pFile, so + 6, bBE != 0);
                uint64_t value = b64 ? xx_exec_u64(pFile, so + 8, bBE != 0) : xx_exec_u32(pFile, so + 8, bBE != 0);
                xx_macho_inspect_symbol *symbol = &pMach->pSymbols[pMach->nSymbolCount];
                if (strx >= nStrSize) { mach_inspect_error(pFile, XXFC_ERR_OUT_OF_BOUNDS, "Invalid Mach-O symbol string offset"); break; }
                symbol->pName = strx ? mach_inspect_string(pFile, (uint64_t)nStrOffset + strx, nStrSize - strx) : xx_exec_copy_string(pFile, "");
                if (!symbol->pName) { pFile->failed = true; break; }
                symbol->nValue = value;
                symbol->nType = type;
                symbol->nSection = sect;
                symbol->nDescription = desc;
                symbol->bExternal = (type & MACH_N_EXT) != 0;
                symbol->bUndefined = (type & MACH_N_STAB) == 0 &&
                                    ((type & MACH_N_TYPE) == MACH_N_UNDF || (type & MACH_N_TYPE) == 0x0cU);
                symbol->bFromSymbolTable = 1;
                symbol->nTableIndex = si;
                symbol->nRecordOffset = so;
                symbol->bExported = (type & (MACH_N_STAB | 0x10U)) == 0 && symbol->bExternal && !symbol->bUndefined;
                symbol->nExportFlags = (symbol->bExported && (desc & 0x80U) ? 4U : 0U) |
                                       ((type & MACH_N_TYPE) == 2U ? 2U : 0U);
                symbol->nExportAddress = symbol->bExported ? value : UINT64_MAX;
                symbol->nResolverAddress = UINT64_MAX;
                if ((type & MACH_N_STAB) == 0 && (type & MACH_N_TYPE) == 0x0aU) {
                    /* N_INDR's native value is a string-table index. */
                    if (value >= nStrSize) {
                        xx_mem_free(symbol->pName); symbol->pName = NULL;
                        mach_inspect_error(pFile, XXFC_ERR_OUT_OF_BOUNDS, "Invalid Mach-O indirect symbol target");
                        break;
                    }
                    symbol->pForwarder = mach_inspect_string(pFile, (uint64_t)nStrOffset + value, nStrSize - value);
                    if (!symbol->pForwarder) { xx_mem_free(symbol->pName); symbol->pName = NULL; break; }
                    symbol->nExportAddress = UINT64_MAX;
                }
                ++pMach->nSymbolCount;
            }
        }
    }
    if (nExportSize && !pFile->failed) {
        if (nExportSize > (uint64_t)pFile->size || nExportOffset > (uint64_t)pFile->size - nExportSize) {
            mach_inspect_error(pFile, XXFC_ERR_OUT_OF_BOUNDS, "Mach-O export trie outside input");
        } else {
            MachExportTrie trie = {0};
            pMach->bHasExportTrie = 1;
            for (int si = 0; si < pMach->nSymbolCount; ++si) pMach->pSymbols[si].bExported = 0;
            trie.file = pFile;
            trie.state = pMach;
            trie.offset = nExportOffset;
            trie.size = nExportSize;
            trie.symbol_capacity = nSymCount;
            if (mach_export_symbol_index_init(&trie)) mach_export_trie_node(&trie, 0, 0);
            xx_mem_free(trie.symbol_slots);
        }
    }
    vecLibs.data = vecSections.data = NULL;
    vecLibs.count = vecSections.count = vecLibs.capacity = vecSections.capacity = 0;
    xx_list_cleanup(&vecLibs);
    xx_list_cleanup(&vecSections);

    pMach->bValid = (i == nCmds && nOffset == commands_end && !pFile->failed);

    return pMach->bValid;
}

void xx_macho_inspect_free(xx_macho_inspection *pMach)
{
    if (!pMach) return;
    int i = 0;

    for (i = 0; i < pMach->nLibraryCount; i++) {
        xx_mem_free(pMach->pLibraries[i].pName);
    }
    for (i = 0; i < pMach->nSymbolCount; i++) {
        xx_mem_free(pMach->pSymbols[i].pName);
        xx_mem_free(pMach->pSymbols[i].pForwarder);
    }

    xx_mem_free(pMach->pLibraries);
    xx_mem_free(pMach->pSections);
    xx_mem_free(pMach->pSymbols);
    xx_exec_input_free(pMach->pInput);
    xx_rt_memset(pMach, 0, sizeof(*pMach));
}

int xx_macho_inspect_library_present(xx_macho_inspection *pMach, const char *pName)
{
    if (!pMach || !pName) return 0;
    int i = 0;

    for (i = 0; i < pMach->nLibraryCount; i++) {
        if (xx_rt_strcmp(pMach->pLibraries[i].pName, pName) == 0) {
            return 1;
        }
    }

    return 0;
}

uint32_t xx_macho_inspect_library_current_version(xx_macho_inspection *pMach, const char *pName)
{
    if (!pMach || !pName) return 0;
    int i = 0;

    for (i = 0; i < pMach->nLibraryCount; i++) {
        if (xx_rt_strcmp(pMach->pLibraries[i].pName, pName) == 0) {
            return pMach->pLibraries[i].nCurrentVersion;
        }
    }

    return 0;
}

int xx_macho_inspect_section_number(xx_macho_inspection *pMach, const char *pName)
{
    if (!pMach || !pName) return -1;
    int i = 0;

    for (i = 0; i < pMach->nSectionCount; i++) {
        if (xx_rt_strcmp(pMach->pSections[i].sName, pName) == 0) {
            return i;
        }
    }

    return -1;
}

int xx_macho_inspect_section_present(xx_macho_inspection *pMach, const char *pName)
{
    if (!pMach || !pName) return 0;
    return (xx_macho_inspect_section_number(pMach, pName) != -1) ? 1 : 0;
}

uint64_t xx_macho_inspect_section_offset(xx_macho_inspection *pMach, int nNumber)
{
    if (!pMach) return 0;
    if ((nNumber < 0) || (nNumber >= pMach->nSectionCount)) {
        return 0;
    }

    return pMach->pSections[nNumber].nOffset;
}

uint64_t xx_macho_inspect_section_size(xx_macho_inspection *pMach, int nNumber)
{
    if (!pMach) return 0;
    if ((nNumber < 0) || (nNumber >= pMach->nSectionCount)) {
        return 0;
    }

    return pMach->pSections[nNumber].nSize;
}

int xx_macho_inspect_parse(xx_macho *reader, xx_macho_inspection *state, xx_pd_struct *pd)
{
    xx_executable_input *input;
    int result;
    int64_t saved;
    if (!state) return 0;
    xx_rt_memset(state, 0, sizeof(*state));
    if (!reader || !reader->format.device) return 0;
    saved = xx_io_tell(reader->format.device);
    if (reader->format.handle_base_info && !reader->format.handle_base_info(&reader->format, pd)) {
        if (saved >= 0) xx_io_seek64(reader->format.device, saved, XX_RT_SEEK_SET);
        return 0;
    }
    input = xx_exec_input_create(&reader->format, pd);
    if (!input) {
        if (saved >= 0) xx_io_seek64(reader->format.device, saved, XX_RT_SEEK_SET);
        return 0;
    }
    result = inspect_parse_input(input, state);
    if (saved >= 0 && xx_io_seek64(reader->format.device, saved, XX_RT_SEEK_SET) != 0) input->failed = true;
    if (!result || input->failed || xx_pd_is_stopped(pd)) {
        xx_macho_inspect_free(state);
        return 0;
    }
    input->pd = NULL;
    input->parsing = false;
    input->read_work = 0;
    return result;
}

int xx_macho_inspect_analyze_from_device(xx_macho_inspection *state, xx_io_device *device, int64_t base, xx_pd_struct *pd)
{
    xx_macho reader = {0};
    reader.format.device = device;
    reader.format.base_address = base;
    return xx_macho_inspect_parse(&reader, state, pd);
}
