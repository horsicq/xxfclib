/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Windows Installer packages inside the resources of a setup stub. Two
 * shapes, both measured on the winget corpus: a resource whose data starts
 * with the compound-file signature holds the .msi as is (HashHash: RCDATA
 * "MSI"); the Classic Shell / Open-Shell setup keeps its x86 and x64 .msi
 * in "MSI_FILE" resources with every byte inverted (XOR 0xFF, see its
 * Src/Setup/Setup.cpp, WriteFileXOR), so their data starts with the
 * inverted signature 2F 30 EE 1F 5E 4E E5 1E. Each package becomes one
 * member named after its resource offset; the cfbf reader opens it.
 * Bounded resource walk; nothing is executed.
 */
#include "xxfclib/formats/installer/msi_resource/xx_msi_resource.h"
#include "../../common/xx_executable_carrier.h"
#include "../../common/xx_pe_resource_locator.h"
#include "../../common/xx_pe_resource_archives.h"
#include <stdio.h>

static const uint8_t mr_plain_signature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
static const uint8_t mr_inverted_signature[8] = {0x2F, 0x30, 0xEE, 0x1F, 0x5E, 0x4E, 0xE5, 0x1E};

/* The compound file's own extent: a version 3 (512) or 4 (4 KiB) header
 * with sane fields; the resource may be padded, which the reader tolerates. */
static bool mr_header_ok(Abstractformat *f, int64_t at, uint32_t bytes, bool inverted)
{
    uint8_t h[64];
    size_t i;
    uint16_t byte_order, shift, mini_shift;
    if (bytes < 1536U || !pm_read(f, at, h, sizeof(h))) return false;
    if (inverted) for (i = 0; i < sizeof(h); ++i) h[i] = (uint8_t)~h[i];
    byte_order = xx_data_get_u16(h + 0x1C, 2, 0, false);
    shift = xx_data_get_u16(h + 0x1E, 2, 0, false);
    mini_shift = xx_data_get_u16(h + 0x20, 2, 0, false);
    return byte_order == 0xFFFEU && (shift == 9U || shift == 12U) && mini_shift == 6U;
}

static bool mr_plain_candidate(Abstractformat *f, pm_stream *s, int64_t at, uint32_t bytes, xx_pd_struct *pd)
{
    char label[64];
    if (carrier_stop(pd)) return false;
    if (!mr_header_ok(f, at, bytes, false)) return true; /* not a package: pass over */
    snprintf(label, sizeof(label), "msi_%lld.msi", (long long)at);
    return executable_carrier_component(f, s, at, bytes, label);
}

static bool mr_inverted_read(Abstractformat *f, pm_member *m, uint64_t position, void *buffer, size_t n, xx_pd_struct *pd)
{
    uint8_t *p = (uint8_t *)buffer;
    size_t i;
    if ((pd && xx_pd_is_stopped(pd)) || m->size < 0 || position > (uint64_t)m->size || n > (uint64_t)m->size - position) return false;
    if (n && !pm_read(f, m->offset - f->base_address + (int64_t)position, buffer, n)) return false;
    for (i = 0; i < n; ++i) p[i] = (uint8_t)~p[i];
    return true;
}

static bool mr_inverted_candidate(Abstractformat *f, pm_stream *s, int64_t at, uint32_t bytes, xx_pd_struct *pd)
{
    char label[64];
    if (carrier_stop(pd)) return false;
    if (!mr_header_ok(f, at, bytes, true)) return true;
    snprintf(label, sizeof(label), "msi_file_%lld.msi", (long long)at);
    if (!executable_carrier_component(f, s, at, bytes, label)) return false;
    s->items[s->count - 1U].read_range = mr_inverted_read;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    bool plain = xx_sfx_pe_resource_archives(f, s, mr_plain_signature, sizeof(mr_plain_signature), mr_plain_candidate, pd);
    bool inverted = !carrier_stop(pd) && xx_sfx_pe_resource_archives(f, s, mr_inverted_signature, sizeof(mr_inverted_signature), mr_inverted_candidate, pd);
    return (plain || inverted) && s->count > 0 && carrier_members(s, pd);
}

void xx_msi_resource_init(xx_msi_resource *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_MSI_RESOURCE, "exe");
    }
}
xx_msi_resource *xx_msi_resource_create(xx_io_device *d, int64_t b)
{
    xx_msi_resource *r = (xx_msi_resource *)xx_mem_alloc(sizeof(*r));
    if (r) xx_msi_resource_init(r, d, b);
    return r;
}
void xx_msi_resource_destroy(xx_msi_resource *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_msi_resource_free(xx_msi_resource *r)
{
    if (r) {
        xx_msi_resource_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_msi_resource_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_msi_resource_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
