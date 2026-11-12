/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Microsoft MS-MPI setup bootstrapper ("msmpisetup.exe", winget
 * Microsoft.msmpi). The stub writes its payload files to a temporary
 * directory and runs msiexec on them; they travel in the overlay as whole
 * files separated by marker blocks, measured on MS-MPI 10.1.1:
 *
 *   overlay (the end of the furthest section's raw data)
 *     marker block     512 bytes, the u32 0x0FABFAB0 repeated 128 times
 *     file 1           mpi_x86.msi, a compound file, as long as it is
 *     marker block
 *     file 2           mpi_x64.msi
 *     marker block
 *     file 3           PurgeMsmpi_x86.exe
 *     marker block
 *     file 4           PurgeMsmpi_x64.exe
 *     marker block     the terminator
 *     Authenticode signature (not a member)
 *
 * There is no directory, so a file ends where the next marker block begins;
 * the bytes after the last marker block (the signature, or nothing) are
 * skipped. The marker blocks alone do not make a bootstrapper: at least one
 * file must be a compound file or a PE image.
 * Members are named by their position and content (package.msi,
 * program.exe, library.dll or data.bin), since the stub keeps the real names
 * only in its own strings. The compound files open with the cfbf reader.
 * Nothing is executed.
 */
#include "xxfclib/formats/installer/msmpi_bootstrapper/xx_msmpi_bootstrapper.h"
#include "../../common/xx_executable_carrier.h"

#define MB_BLOCK 512U
#define MB_MARK 0x0FABFAB0U
#define MB_CHUNK 65536U
#define MB_LEAD 1024U /* tolerance before the first marker block: a debug record may trail the sections, as the InstallShield launcher reader allows */
#define MB_MAX_MEMBERS 64U

static const uint8_t mb_cfbf_signature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

/* Is this 512-byte block a marker block? */
static bool mb_marker(const uint8_t *block)
{
    size_t i;
    for (i = 0; i < MB_BLOCK; i += 4) {
        if (xx_data_get_u32(block + i, 4, 0, false) != MB_MARK) return false;
    }
    return true;
}

/* The first marker block at or after `from` and wholly below `limit`:
 * windows of MB_CHUNK bytes with a one-block overlap. False when none. */
static bool mb_find_marker(Abstractformat *f, int64_t from, int64_t limit, int64_t *where, xx_pd_struct *pd)
{
    uint8_t *buffer;
    int64_t at = from;
    bool found = false;
    if (from < 0 || limit - from < (int64_t)MB_BLOCK) return false;
    buffer = (uint8_t *)xx_mem_alloc(MB_CHUNK + MB_BLOCK);
    if (!buffer) return false;
    while (!found && limit - at >= (int64_t)MB_BLOCK) {
        size_t n = limit - at < (int64_t)(MB_CHUNK + MB_BLOCK) ? (size_t)(limit - at) : MB_CHUNK + MB_BLOCK;
        size_t k;
        if (carrier_stop(pd) || !pm_read(f, at, buffer, n)) break;
        for (k = 0; k + MB_BLOCK <= n; ++k) {
            if (buffer[k] == 0xB0U && buffer[k + 1] == 0xFAU && buffer[k + 2] == 0xABU && buffer[k + 3] == 0x0FU && mb_marker(buffer + k)) {
                *where = at + (int64_t)k;
                found = true;
                break;
            }
        }
        if (n < MB_CHUNK + MB_BLOCK) break;
        at += (int64_t)MB_CHUNK;
    }
    xx_mem_free(buffer);
    return found;
}

/* A member's name from its first bytes (the framework prefixes the position);
 * `known` tells whether the content was recognised. */
static const char *mb_extension(Abstractformat *f, int64_t at, int64_t size, bool *known)
{
    uint8_t h[64];
    *known = true;
    if (size >= 64 && pm_read(f, at, h, sizeof(h))) {
        if (xx_rt_memcmp(h, mb_cfbf_signature, sizeof(mb_cfbf_signature)) == 0) return "package.msi";
        if (h[0] == 'M' && h[1] == 'Z') {
            uint32_t pe = xx_data_get_u32(h + 60, 4, 0, false);
            uint8_t coff[24];
            if (pe >= 64 && (int64_t)pe + 24 <= size && pm_read(f, at + (int64_t)pe, coff, sizeof(coff)) && xx_rt_memcmp(coff, "PE\0\0", 4) == 0)
                return (xx_data_get_u16(coff + 22, 2, 0, false) & 0x2000U) ? "library.dll" : "program.exe";
        }
    }
    *known = false;
    return "data.bin";
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t overlay, cabinet, cabinet_end, available = pm_available(f), at, next;
    unsigned index = 0;
    bool recognised = false, known;
    if (!carrier_pe(f, &overlay, &cabinet, &cabinet_end, pd) || overlay <= 0 || available - overlay < (int64_t)(2U * MB_BLOCK + 1U)) return false;
    /* The first marker block opens the payload. */
    if (!mb_find_marker(f, overlay, overlay + (int64_t)(MB_LEAD + MB_BLOCK) < available ? overlay + (int64_t)(MB_LEAD + MB_BLOCK) : available, &at, pd)) return false;
    at += (int64_t)MB_BLOCK;
    while (at < available) {
        if (carrier_stop(pd)) return false;
        if (!mb_find_marker(f, at, available, &next, pd)) break; /* the signature, or nothing, follows the last marker */
        if (next > at) {
            if (++index > MB_MAX_MEMBERS) return false;
            if (!executable_carrier_component(f, s, at, next - at, mb_extension(f, at, next - at, &known))) return false;
            recognised = recognised || known;
        }
        at = next + (int64_t)MB_BLOCK;
    }
    return recognised && carrier_members(s, pd);
}

void xx_msmpi_bootstrapper_init(xx_msmpi_bootstrapper *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_MSMPI_BOOTSTRAPPER, "exe");
    }
}
xx_msmpi_bootstrapper *xx_msmpi_bootstrapper_create(xx_io_device *d, int64_t b)
{
    xx_msmpi_bootstrapper *r = (xx_msmpi_bootstrapper *)xx_mem_alloc(sizeof(*r));
    if (r) xx_msmpi_bootstrapper_init(r, d, b);
    return r;
}
void xx_msmpi_bootstrapper_destroy(xx_msmpi_bootstrapper *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_msmpi_bootstrapper_free(xx_msmpi_bootstrapper *r)
{
    if (r) {
        xx_msmpi_bootstrapper_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_msmpi_bootstrapper_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_msmpi_bootstrapper_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
