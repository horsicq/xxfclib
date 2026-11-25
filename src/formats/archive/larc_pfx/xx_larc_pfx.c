/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT
 * XArchive/archives/xlarcpfx.cpp: GEMDOS LArc PFX carrier. Known PFX 1.13P
 * instruction bodies use either an overstated LZ5 packed length bounded by
 * GEMDOS DATA, or an ordered chain of bounded members. This producer's LZ5
 * CRC field is non-authoritative; unrelated LHA and LH1 checks stay strict.
 * Exact long534/556 bodies also admit one bounded, canonical LZ5 member,
 * or structurally verified recovery of customized member metadata.
 * PFX 2.1P has a separate exact-body, single-member structural path.
 * No executable bytes are run. */
#include "xxfclib/formats/archive/larc_pfx/xx_larc_pfx.h"
#include "xxfclib/data/xx_data.h"
#include "../lha/xx_lha_legacy_native.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "../../xx_payload_members.h"
#include "xx_larc_pfx_stub.h"
#define PFX_MAX_BUFFER (UINT64_C(256) * 1024U * 1024U)
/* Eager metadata recovery never allocates more than 80 MiB for the
 * packed/raw buffers, and honors smaller caller memory/member limits first. */
#define PFX_RECOVERY_PACKED (UINT64_C(16) * 1024U * 1024U)
#define PFX_RECOVERY_RAW (UINT64_C(64) * 1024U * 1024U)
typedef struct pfx_member {
    int64_t header_offset, header_size;
    uint32_t method;
    uint16_t crc;
    bool producer_crc;
    bool terminal_framing_retry;
    bool structural_recovery;
    bool bounded_long;
    bool bounded_probe;
    bool bounded_structural;
    bool custom_metadata;
    bool custom_standard;
    bool pfx21;
    bool pfx334;
    uint8_t program_header[28], member_header[257];
    uint8_t banner[44], framing_byte;
    uint8_t banner21[64];
    uint8_t seed334[18];
} pfx_member;
/* This exact producer has two differently encoded physical LHA members and
 * an optional stored resource attachment. It never enters old CRC policies. */
typedef struct pfx420_snapshot {
    uint8_t program[28], banner[44], header[2][257];
    unsigned hs[2];
    uint64_t header_at[2], packed_at[2], packed[2], raw[2];
    uint16_t crc[2];
    uint64_t data_end, file_size;
    char name[2][256];
} pfx420_snapshot;
typedef struct pfx420_member {
    pfx420_snapshot snapshot;
    unsigned index;
} pfx420_member;
static uint32_t pfx_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static bool pfx_name(const uint8_t *p, size_t n, char *out)
{
    size_t i, component = 0;
    if (!n || n > 255 || p[0] == '/' || p[0] == '\\') return false;
    for (i = 0; i < n; ++i) {
        unsigned c = p[i];
        if (i == 1 && c == ':' && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'))) c = '_';
        if (c == '\\') c = '/';
        if (c < 0x21 || c > 0x7e || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
        if (c == '/') {
            if (i == component || (i - component == 1 && out[component] == '.') || (i - component == 2 && out[component] == '.' && out[component + 1] == '.'))
                return false;
            component = i + 1;
        }
        out[i] = (char)c;
    }
    out[n] = 0;
    return component < n && xx_rt_strcmp(out + component, ".") && xx_rt_strcmp(out + component, "..");
}
static bool pfx_producer(Abstractformat *f, uint64_t text)
{
    uint8_t bytes[556];
    size_t size;
    const uint8_t *body;
    unsigned i;
    bool erased = true;
    if (text == sizeof(pfx_113p_standard_body) + 46U) {
        size = sizeof(pfx_113p_standard_body);
        body = pfx_113p_standard_body;
    } else if (text == sizeof(pfx_113p_compact_body) + 46U) {
        size = sizeof(pfx_113p_compact_body);
        body = pfx_113p_compact_body;
    } else if (text == sizeof(pfx_113p_long534_body) + 46U) {
        size = sizeof(pfx_113p_long534_body);
        body = pfx_113p_long534_body;
    } else if (text == sizeof(pfx_113p_long556_body) + 46U) {
        size = sizeof(pfx_113p_long556_body);
        body = pfx_113p_long556_body;
    } else return false;
    if (!pm_read(f, 28, bytes, 46) || bytes[0] != 0x60 || bytes[1] != 0x2c || !pm_read(f, 74, bytes + 46, size) || xx_rt_memcmp(bytes + 46, body, size)) return false;
    for (i = 2; i < 46; ++i)
        if (bytes[i]) erased = false;
    /* Some PFX programs erase the text banner, while preserving every
     * instruction byte. A banner alone never relaxes payload checks. */
    return erased || !xx_rt_memcmp(bytes + 2, "LArc's PFX 1.13P", 16);
}
/* This exact shorter loader ignores the declared LH5 method and CRC.
 * Its private bounded codec and complete inner GEMDOS checks stand apart
 * from every ordinary LHA and other PFX producer policy. */
static bool pfx334_producer(Abstractformat *f)
{
    uint8_t branch[2], banner[44], body[sizeof(pfx_113p_334_body)];
    return pm_read(f, 28, branch, sizeof(branch)) && branch[0] == 0x60 && branch[1] == 0x2c && pm_read(f, 30, banner, sizeof(banner)) &&
           !xx_rt_memcmp(banner, "LArc's PFX 1.13P", 16) && pm_read(f, 74, body, sizeof(body)) && !xx_rt_memcmp(body, pfx_113p_334_body, sizeof(body));
}
/* This version has a different loader, banner and DATA layout. Its full
 * instruction body is authenticated independently of the 1.13 policies. */
static bool pfx21_producer(Abstractformat *f)
{
    uint8_t branch[2], banner[64], body[sizeof(pfx_21p_body)];
    return pm_read(f, 28, branch, sizeof(branch)) && branch[0] == 0x60 && branch[1] == 0x40 && pm_read(f, 30, banner, sizeof(banner)) &&
           !xx_rt_memcmp(banner, "LArc's PFX 2.1P", 15) && pm_read(f, 94, body, sizeof(body)) && !xx_rt_memcmp(body, pfx_21p_body, sizeof(body));
}
/* A customized banner is metadata only in the separate, eagerly validated
 * long-producer recovery path. Normal producer recognition stays strict. */
static bool pfx_long_body(Abstractformat *f, uint64_t text)
{
    uint8_t branch[2], bytes[sizeof(pfx_113p_long556_body)];
    const uint8_t *body;
    size_t size;
    if (text == 534U) {
        body = pfx_113p_long534_body;
        size = sizeof(pfx_113p_long534_body);
    } else if (text == 556U) {
        body = pfx_113p_long556_body;
        size = sizeof(pfx_113p_long556_body);
    } else return false;
    return pm_read(f, 28, branch, sizeof(branch)) && branch[0] == 0x60 && branch[1] == 0x2c && pm_read(f, 74, bytes, size) && !xx_rt_memcmp(bytes, body, size);
}
static bool pfx_custom_long_envelope(Abstractformat *f, const uint8_t *program, const uint8_t *header, unsigned hs, uint64_t at, uint8_t *framing_byte)
{
    uint8_t trailer[4], framing = 0;
    uint64_t text, data, packed, end, padding;
    unsigned i, sum = 0;
    char name[256];
    int64_t size = pm_available(f);
    if (size < 4 || program[0] != 0x60 || program[1] != 0x1a || pfx_be32(program + 14) || program[26] || program[27]) return false;
    text = pfx_be32(program + 2);
    data = pfx_be32(program + 6);
    if (at != 28U + text || !pfx_long_body(f, text) || hs < 25U || hs > 257U || (unsigned)header[0] + 2U != hs || 24U + header[21] != hs || !header[21] || header[20] ||
        xx_rt_memcmp(header + 2, "-lz5-", 5))
        return false;
    /* Names may be decorative or unsafe, but never contain erased or control
     * bytes. The source basename is used instead of this untrusted metadata. */
    for (i = 0; i < header[21]; ++i)
        if (header[22U + i] < 0x20 || header[22U + i] > 0x7e) return false;
    packed = xx_data_get_u32(header + 7, 4, 0, false);
    end = 28U + text + data;
    if (!packed || data < hs || packed > data - hs || end != (uint64_t)size - 4U) return false;
    padding = data - hs - packed;
    if (padding > 1U || !pm_read(f, (int64_t)end, trailer, sizeof(trailer)) || trailer[0] || trailer[1] || trailer[2] || trailer[3]) return false;
    if (padding && !pm_read(f, (int64_t)end - 1, &framing, 1)) return false;
    for (i = 2; i < hs; ++i) sum += header[i];
    /* Valid ordinary long members must retain their existing path, which
     * does not require the decoded payload to itself be a GEMDOS program. */
    if (!padding && (uint8_t)sum == header[1] && pfx_name(header + 22, header[21], name) && pfx_producer(f, text)) return false;
    if (framing_byte) *framing_byte = framing;
    return true;
}
static bool pfx_custom_long_unchanged(Abstractformat *f, const pm_member *m, const pfx_member *context)
{
    uint8_t program[28], header[257], banner[44], framing;
    if (context->header_offset < f->base_address || context->header_size < 25 || context->header_size > (int64_t)sizeof(header) ||
        m->offset != context->header_offset + context->header_size || !pm_read(f, 0, program, sizeof(program)) ||
        xx_rt_memcmp(program, context->program_header, sizeof(program)) || !pm_read(f, context->header_offset - f->base_address, header, (size_t)context->header_size) ||
        xx_rt_memcmp(header, context->member_header, (size_t)context->header_size) || !pm_read(f, 30, banner, sizeof(banner)) ||
        xx_rt_memcmp(banner, context->banner, sizeof(banner)) || (uint64_t)m->packed_size != xx_data_get_u32(header + 7, 4, 0, false) ||
        (uint64_t)m->size != xx_data_get_u32(header + 11, 4, 0, false))
        return false;
    return pfx_custom_long_envelope(f, program, header, (unsigned)context->header_size, (uint64_t)(context->header_offset - f->base_address), &framing) &&
           framing == context->framing_byte;
}
/* Only this observed space-blanked banner opts the exact standard body
 * into a separate complete-object recovery. Ordinary recognition stays strict. */
static const uint8_t pfx_standard_blank_banner[44] = {0x20, 0x20, 0x20, 0x20, 0x27, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
                                                      0x20, 0x20, 0xbd, 0x20, 0x20, 0x20, 0x20, 0x20, 0xe4, 0x20, 0x20, 0x20, 0x20, 0x20, 0x2c,
                                                      0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
static bool pfx_custom_standard_producer(Abstractformat *f)
{
    uint8_t branch[2], banner[44], body[sizeof(pfx_113p_standard_body)];
    return pm_read(f, 28, branch, sizeof(branch)) && branch[0] == 0x60 && branch[1] == 0x2c && pm_read(f, 30, banner, sizeof(banner)) &&
           !xx_rt_memcmp(banner, pfx_standard_blank_banner, sizeof(banner)) && pm_read(f, 74, body, sizeof(body)) &&
           !xx_rt_memcmp(body, pfx_113p_standard_body, sizeof(body));
}
static bool pfx_custom_standard_envelope(Abstractformat *f, const uint8_t *program, const uint8_t *header, unsigned hs, uint64_t at)
{
    uint8_t trailer[4], framing = 0;
    uint64_t data, packed, end, gap;
    unsigned i, sum = 0;
    char name[256];
    int64_t size = pm_available(f);
    if (size < 4 || program[0] != 0x60 || program[1] != 0x1a || pfx_be32(program + 2) != 392U || pfx_be32(program + 14) || program[26] || program[27] || at != 420U ||
        !pfx_custom_standard_producer(f) || hs < 25U || hs > 257U || (unsigned)header[0] + 2U != hs || 24U + header[21] != hs || header[20] ||
        xx_rt_memcmp(header + 2, "-lz5-", 5) || !pfx_name(header + 22, header[21], name))
        return false;
    for (i = 2; i < hs; ++i) sum += header[i];
    if ((uint8_t)sum != header[1]) return false;
    data = pfx_be32(program + 6);
    packed = xx_data_get_u32(header + 7, 4, 0, false);
    end = 420U + data;
    if (!packed || data < hs || packed > data - hs || end != (uint64_t)size - 4U) return false;
    gap = data - hs - packed;
    if (gap > 1U || !pm_read(f, (int64_t)end, trailer, sizeof(trailer)) || trailer[0] || trailer[1] || trailer[2] || trailer[3]) return false;
    if (gap && (!pm_read(f, (int64_t)end - 1, &framing, 1) || framing)) return false;
    return true;
}
static bool pfx_custom_standard_unchanged(Abstractformat *f, const pm_member *m, const pfx_member *context)
{
    uint8_t program[28], header[257], banner[44];
    if (context->header_offset < f->base_address || context->header_size < 25 || context->header_size > (int64_t)sizeof(header) ||
        m->offset != context->header_offset + context->header_size || !pm_read(f, 0, program, sizeof(program)) ||
        xx_rt_memcmp(program, context->program_header, sizeof(program)) || !pm_read(f, context->header_offset - f->base_address, header, (size_t)context->header_size) ||
        xx_rt_memcmp(header, context->member_header, (size_t)context->header_size) || !pm_read(f, 30, banner, sizeof(banner)) ||
        xx_rt_memcmp(banner, context->banner, sizeof(banner)) || (uint64_t)m->packed_size != xx_data_get_u32(header + 7, 4, 0, false) ||
        (uint64_t)m->size != xx_data_get_u32(header + 11, 4, 0, false))
        return false;
    return pfx_custom_standard_envelope(f, program, header, (unsigned)context->header_size, (uint64_t)(context->header_offset - f->base_address));
}
/* The original single-stream producer can leave relocation/framing bytes
 * at the end of GEMDOS DATA. Authenticate the envelope again before accepting
 * that byte, including the exact headers observed when creating the member. */
static bool pfx_terminal_framing(Abstractformat *f, const pm_member *m, const pfx_member *context)
{
    uint8_t program[28], header[257], trailer[4];
    uint64_t text, data, end, header_at, data_at;
    int64_t size = pm_available(f);
    if (size < 4 || m->offset < f->base_address || context->header_offset < f->base_address || m->packed_size <= 1 || context->header_size < 24 ||
        context->header_size > (int64_t)sizeof(header) || !pm_read(f, 0, program, sizeof(program)) || xx_rt_memcmp(program, context->program_header, sizeof(program)) ||
        program[0] != 0x60 || program[1] != 0x1a || pfx_be32(program + 14) || program[26] || program[27])
        return false;
    text = pfx_be32(program + 2);
    data = pfx_be32(program + 6);
    end = 28U + text + data;
    header_at = (uint64_t)(context->header_offset - f->base_address);
    data_at = (uint64_t)(m->offset - f->base_address);
    if (header_at != 28U + text || data_at != header_at + (uint64_t)context->header_size || data_at + (uint64_t)m->packed_size != end || end != (uint64_t)size - 4U ||
        xx_data_get_u32(context->member_header + 7, 4, 0, false) <= (uint64_t)m->packed_size ||
        xx_data_get_u32(context->member_header + 11, 4, 0, false) != (uint64_t)m->size || !pfx_producer(f, text) ||
        !pm_read(f, (int64_t)header_at, header, (size_t)context->header_size) || xx_rt_memcmp(header, context->member_header, (size_t)context->header_size) ||
        !pm_read(f, (int64_t)end, trailer, sizeof(trailer)))
        return false;
    return !trailer[0] && !trailer[1] && !trailer[2] && !trailer[3];
}
static bool pfx_erased_name(const uint8_t *header, unsigned hs)
{
    unsigned i = 0, n = header[21];
    /* Only the three observed fixed-width erased fields are supported.
     * Their distinct banner policies are checked in the envelope below.
     * The new 41-byte form is exactly forty NULs and one final space;
     * the older fields keep their existing NUL-prefix/space grammar. */
    if (!((hs == 46U && n == 22U) || (hs == 55U && n == 31U) || (hs == 65U && n == 41U)) || (unsigned)header[0] + 2U != hs) return false;
    if (hs == 65U) {
        for (i = 0; i < 40U; ++i)
            if (header[22U + i]) return false;
        return header[62] == 0x20;
    }
    while (i < n && !header[22U + i]) ++i;
    if (!i) return false;
    for (; i < n; ++i)
        if (header[22U + i] != 0x20) return false;
    return true;
}
static bool pfx_erased_envelope(Abstractformat *f, const uint8_t *program, const uint8_t *header, unsigned hs, uint64_t at)
{
    uint8_t banner[44], trailer[4], alignment[2];
    uint64_t data, packed, end, padding;
    unsigned i;
    int64_t size = pm_available(f);
    if (size < 4 || program[0] != 0x60 || program[1] != 0x1a || pfx_be32(program + 2) != 392U || pfx_be32(program + 14) || program[26] || program[27] || at != 420U ||
        !pfx_erased_name(header, hs) || header[20] || xx_rt_memcmp(header + 2, "-lz5-", 5) || !pfx_producer(f, 392U) || !pm_read(f, 30, banner, sizeof(banner)))
        return false;
    if (hs == 46U || hs == 65U) {
        for (i = 0; i < sizeof(banner); ++i)
            if (banner[i]) return false;
    } else if (xx_rt_memcmp(banner, "LArc's PFX 1.13P", 16)) return false;
    data = pfx_be32(program + 6);
    packed = xx_data_get_u32(header + 7, 4, 0, false);
    end = 420U + data;
    if (!packed || data < hs || packed > data - hs || end != (uint64_t)size - 4U) return false;
    padding = data - hs - packed;
    if (padding > sizeof(alignment) || !pm_read(f, (int64_t)end, trailer, sizeof(trailer)) || trailer[0] || trailer[1] || trailer[2] || trailer[3]) return false;
    if (padding && !pm_read(f, (int64_t)(at + hs + packed), alignment, (size_t)padding)) return false;
    for (i = 0; i < padding; ++i)
        if (alignment[i]) return false;
    return true;
}
/* Known long instruction bodies also produce a bounded single LZ5 member.
 * Only the observed complete envelope is enabled; long member chains and DATA
 * padding need independent evidence before sharing the short-producer path. */
static bool pfx_bounded_long_envelope(Abstractformat *f, const uint8_t *program, const uint8_t *header, unsigned hs, uint64_t at)
{
    uint8_t trailer[4];
    uint64_t text, data, packed, end;
    unsigned i, sum = 0;
    char name[256];
    int64_t size = pm_available(f);
    if (size < 4 || program[0] != 0x60 || program[1] != 0x1a || pfx_be32(program + 14) || program[26] || program[27]) return false;
    text = pfx_be32(program + 2);
    data = pfx_be32(program + 6);
    if ((text != 534U && text != 556U) || at != 28U + text || !pfx_producer(f, text) || hs < 24U || hs > 257U || (unsigned)header[0] + 2U != hs ||
        24U + header[21] != hs || header[20] || xx_rt_memcmp(header + 2, "-lz5-", 5) || !pfx_name(header + 22, header[21], name))
        return false;
    for (i = 2; i < hs; ++i) sum += header[i];
    if ((uint8_t)sum != header[1]) return false;
    packed = xx_data_get_u32(header + 7, 4, 0, false);
    end = 28U + text + data;
    if (!packed || data != (uint64_t)hs + packed || end != (uint64_t)size - 4U || !pm_read(f, (int64_t)end, trailer, sizeof(trailer))) return false;
    return !trailer[0] && !trailer[1] && !trailer[2] && !trailer[3];
}
static bool pfx_bounded_long_unchanged(Abstractformat *f, const pm_member *m, const pfx_member *context)
{
    uint8_t program[28], header[257], banner[44];
    if (context->header_offset < f->base_address || context->header_size < 24 || context->header_size > (int64_t)sizeof(header) ||
        m->offset != context->header_offset + context->header_size || !pm_read(f, 0, program, sizeof(program)) ||
        xx_rt_memcmp(program, context->program_header, sizeof(program)) || !pm_read(f, context->header_offset - f->base_address, header, (size_t)context->header_size) ||
        xx_rt_memcmp(header, context->member_header, (size_t)context->header_size) || (uint64_t)m->packed_size != xx_data_get_u32(header + 7, 4, 0, false) ||
        (uint64_t)m->size != xx_data_get_u32(header + 11, 4, 0, false))
        return false;
    if ((context->bounded_probe || context->bounded_structural) && (!pm_read(f, 30, banner, sizeof(banner)) || xx_rt_memcmp(banner, context->banner, sizeof(banner))))
        return false;
    return pfx_bounded_long_envelope(f, program, header, (unsigned)context->header_size, (uint64_t)(context->header_offset - f->base_address));
}
static bool pfx_recovery_limits(Abstractformat *f, uint64_t packed, uint64_t plain)
{
    const xx_var *v;
    if (!packed || !plain || packed > PFX_RECOVERY_PACKED || plain > PFX_RECOVERY_RAW) return false;
    v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (v && packed + plain > xx_var_get_u64(v)) return false;
    v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    return !v || plain <= xx_var_get_u64(v);
}
static bool pfx_erased_unchanged(Abstractformat *f, const pm_member *m, const pfx_member *context)
{
    uint8_t program[28], header[257], banner[44];
    if (context->header_offset < f->base_address || (context->header_size != 46 && context->header_size != 55 && context->header_size != 65) ||
        m->offset != context->header_offset + context->header_size || !pm_read(f, 0, program, sizeof(program)) ||
        xx_rt_memcmp(program, context->program_header, sizeof(program)) || !pm_read(f, context->header_offset - f->base_address, header, (size_t)context->header_size) ||
        xx_rt_memcmp(header, context->member_header, (size_t)context->header_size) || !pm_read(f, 30, banner, sizeof(banner)) ||
        xx_rt_memcmp(banner, context->banner, sizeof(banner)) || (uint64_t)m->packed_size != xx_data_get_u32(header + 7, 4, 0, false) ||
        (uint64_t)m->size != xx_data_get_u32(header + 11, 4, 0, false))
        return false;
    return pfx_erased_envelope(f, program, header, (unsigned)context->header_size, (uint64_t)(context->header_offset - f->base_address));
}
static bool pfx334_envelope(Abstractformat *f, const uint8_t *program, const uint8_t *header, unsigned hs, uint64_t at, xx_pd_struct *pd)
{
    uint8_t trailer[4], padding[2];
    uint64_t data, packed, end, gap;
    unsigned i, sum = 0;
    char name[256];
    int64_t size = pm_available(f);
    if (size < 4 || (pd && xx_pd_is_stopped(pd)) || program[0] != 0x60 || program[1] != 0x1a || pfx_be32(program + 2) != 334U || pfx_be32(program + 14) || program[26] ||
        program[27] || at != 362U || hs < 25U || hs > 257U || (unsigned)header[0] + 2U != hs || 24U + header[21] != hs || header[20] ||
        xx_rt_memcmp(header + 2, "-lh5-", 5) || !pfx_name(header + 22, header[21], name))
        return false;
    data = pfx_be32(program + 6);
    packed = xx_data_get_u32(header + 7, 4, 0, false);
    end = at + data;
    for (i = 2; i < hs; ++i) sum += header[i];
    if ((uint8_t)sum != header[1] || !data || data > PFX_RECOVERY_PACKED || data < hs || !packed || packed > data - hs || end != (uint64_t)size - 4U ||
        !pfx334_producer(f) || !pm_read(f, (int64_t)end, trailer, sizeof(trailer)) || trailer[0] || trailer[1] || trailer[2] || trailer[3])
        return false;
    gap = data - hs - packed;
    if (gap > sizeof(padding) || (gap && !pm_read(f, (int64_t)(at + hs + packed), padding, (size_t)gap))) return false;
    for (i = 0; i < gap; ++i)
        if (padding[i]) return false;
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pfx334_unchanged(Abstractformat *f, const pm_member *m, const pfx_member *context, xx_pd_struct *pd)
{
    uint8_t program[28], header[257], banner[44], seed[18];
    if (context->header_offset < f->base_address || context->header_size < 25 || context->header_size > (int64_t)sizeof(header) ||
        m->offset != context->header_offset + context->header_size || !pm_read(f, 0, program, sizeof(program)) ||
        xx_rt_memcmp(program, context->program_header, sizeof(program)) || !pm_read(f, context->header_offset - f->base_address, header, (size_t)context->header_size) ||
        xx_rt_memcmp(header, context->member_header, (size_t)context->header_size) || !pm_read(f, 30, banner, sizeof(banner)) ||
        xx_rt_memcmp(banner, context->banner, sizeof(banner)) || !pm_read(f, (int64_t)(362U + pfx_be32(program + 6) - sizeof(seed)), seed, sizeof(seed)) ||
        xx_rt_memcmp(seed, context->seed334, sizeof(seed)) || (uint64_t)m->packed_size != xx_data_get_u32(header + 7, 4, 0, false) ||
        (uint64_t)m->size != xx_data_get_u32(header + 11, 4, 0, false))
        return false;
    return pfx334_envelope(f, program, header, (unsigned)context->header_size, (uint64_t)(context->header_offset - f->base_address), pd);
}
/* The loader clears exactly the first 4078 ring slots to spaces. The final
 * 18 slots alias the loaded carrier's final DATA bytes before BSS begins.
 * Preserve that exact seed rather than inventing zero bytes or inheriting
 * the unrelated generic LZ5 patterned dictionary. */
static bool pfx334_decode(const uint8_t *packed, size_t packed_size, uint8_t *plain, size_t plain_size, const uint8_t seed[18], xx_pd_struct *pd)
{
    uint8_t history[4096];
    size_t input = 0, output = 0;
    unsigned ring = 4078U;
    for (unsigned i = 0; i < 4078U; ++i) history[i] = 0x20;
    xx_mem_copy(history + 4078U, seed, 18U);
    while (output < plain_size) {
        unsigned flags;
        if ((pd && xx_pd_is_stopped(pd)) || input >= packed_size) return false;
        flags = packed[input++];
        for (unsigned bit = 0; bit < 8U && output < plain_size; ++bit) {
            if ((flags >> bit) & 1U) {
                uint8_t value;
                if (input >= packed_size) return false;
                value = packed[input++];
                plain[output++] = value;
                history[ring] = value;
                ring = (ring + 1U) & 4095U;
            } else {
                unsigned source, length;
                if (packed_size - input < 2U) return false;
                source = packed[input] | ((unsigned)(packed[input + 1U] & 0xf0U) << 4);
                length = (packed[input + 1U] & 15U) + 3U;
                input += 2U;
                if (plain_size - output < length) return false;
                for (unsigned i = 0; i < length; ++i) {
                    unsigned slot = (source + i) & 4095U;
                    uint8_t value;
                    value = history[slot];
                    plain[output++] = value;
                    history[ring] = value;
                    ring = (ring + 1U) & 4095U;
                }
            }
        }
    }
    return input == packed_size && !(pd && xx_pd_is_stopped(pd));
}
/* PFX 2.1P is admitted only as a canonical single bounded member. DATA
 * (including its suffix) is capped at 16 MiB; the raw image is capped at
 * 64 MiB by pfx_recovery_limits before any eager payload allocation. */
static bool pfx21_envelope(Abstractformat *f, const uint8_t *program, const uint8_t *header, unsigned hs, uint64_t at, xx_pd_struct *pd)
{
    uint8_t trailer[4];
    uint64_t data, packed, end;
    unsigned i, sum = 0;
    char name[256];
    int64_t size = pm_available(f);
    if (size < 4 || (pd && xx_pd_is_stopped(pd)) || program[0] != 0x60 || program[1] != 0x1a || pfx_be32(program + 2) != 1022U || pfx_be32(program + 14) || program[26] ||
        program[27] || at != 1050U || hs < 25U || hs > 257U || (unsigned)header[0] + 2U != hs || 24U + header[21] != hs || header[20] ||
        xx_rt_memcmp(header + 2, "-lz5-", 5) || !pfx_name(header + 22, header[21], name))
        return false;
    data = pfx_be32(program + 6);
    packed = xx_data_get_u32(header + 7, 4, 0, false);
    end = at + data;
    for (i = 2; i < hs; ++i) sum += header[i];
    if ((uint8_t)sum != header[1] || !data || data > PFX_RECOVERY_PACKED || data < hs || !packed || packed > data - hs || end != (uint64_t)size - 4U ||
        !pfx21_producer(f) || !pm_read(f, (int64_t)end, trailer, sizeof(trailer)) || trailer[0] || trailer[1] || trailer[2] || trailer[3])
        return false;
    /* This loader stops decoding at the declared raw count and then performs
     * relocations. Bytes after the bounded encoded member are opaque loader
     * workspace, not a member chain. Both zero and nonzero suffixes occur.
     * Their geometry is bounded above; their contents are not decoded. */
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pfx21_unchanged(Abstractformat *f, const pm_member *m, const pfx_member *context, xx_pd_struct *pd)
{
    uint8_t program[28], header[257], banner[64];
    if (context->header_offset < f->base_address || context->header_size < 25 || context->header_size > (int64_t)sizeof(header) ||
        m->offset != context->header_offset + context->header_size || !pm_read(f, 0, program, sizeof(program)) ||
        xx_rt_memcmp(program, context->program_header, sizeof(program)) || !pm_read(f, context->header_offset - f->base_address, header, (size_t)context->header_size) ||
        xx_rt_memcmp(header, context->member_header, (size_t)context->header_size) || !pm_read(f, 30, banner, sizeof(banner)) ||
        xx_rt_memcmp(banner, context->banner21, sizeof(banner)) || (uint64_t)m->packed_size != xx_data_get_u32(header + 7, 4, 0, false) ||
        (uint64_t)m->size != xx_data_get_u32(header + 11, 4, 0, false))
        return false;
    return pfx21_envelope(f, program, header, (unsigned)context->header_size, (uint64_t)(context->header_offset - f->base_address), pd);
}
static bool pfx_inner_gemdos(const uint8_t *plain, size_t size, xx_pd_struct *pd)
{
    uint64_t body, bss, symbols, at;
    uint32_t target;
    size_t steps = 0;
    if (size < 32U || plain[0] != 0x60 || plain[1] != 0x1a || !pfx_be32(plain + 2) || plain[26] || plain[27] || (pd && xx_pd_is_stopped(pd))) return false;
    body = (uint64_t)pfx_be32(plain + 2) + pfx_be32(plain + 6);
    bss = pfx_be32(plain + 10);
    symbols = pfx_be32(plain + 14);
    at = 28U + body + symbols;
    if (body + bss > UINT32_MAX || at > size || size - (size_t)at < 4U) return false;
    target = pfx_be32(plain + (size_t)at);
    at += 4U;
    if (!target) return at == size;
    if ((target & 1U) || (uint64_t)target + 4U > body) return false;
    while (at < size) {
        unsigned delta = plain[(size_t)at++];
        uint32_t add;
        if ((steps++ & 1023U) == 0U && pd && xx_pd_is_stopped(pd)) return false;
        if (!delta) return at == size;
        add = delta == 1U ? 254U : delta;
        if (target > UINT32_MAX - add) return false;
        target += add;
        /* A 1 byte is a gap extension, not a relocation operation. */
        if (delta != 1U && ((target & 1U) || (uint64_t)target + 4U > body)) return false;
    }
    return false;
}
static uint16_t pfx_crc16(const uint8_t *bytes, size_t size)
{
    size_t i;
    unsigned bit;
    uint16_t crc = 0;
    for (i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (bit = 0; bit < 8; ++bit) crc = (uint16_t)((crc >> 1) ^ ((crc & 1) ? 0xa001 : 0));
    }
    return crc;
}
static bool pfx420_conflict(const char *left, const char *right)
{
    size_t i = 0;
    while (left[i] && right[i]) {
        unsigned a = (unsigned char)left[i], b = (unsigned char)right[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
        ++i;
    }
    return (!left[i] && (!right[i] || right[i] == '/')) || (!right[i] && left[i] == '/');
}
static bool pfx420_snapshot_read(Abstractformat *f, pfx420_snapshot *snapshot, xx_pd_struct *pd)
{
    uint8_t branch[2], body[sizeof(pfx_113p_420_body)], markers[6];
    uint64_t at = 448U, end, gap;
    unsigned i, j, sum;
    int64_t size = pm_available(f);
    if (size < 505 || size > INT32_MAX || (pd && xx_pd_is_stopped(pd))) return false;
    xx_mem_zero(snapshot, sizeof(*snapshot));
    if (!pm_read(f, 0, snapshot->program, sizeof(snapshot->program)) || snapshot->program[0] != 0x60 || snapshot->program[1] != 0x1a ||
        pfx_be32(snapshot->program + 2) != 420U || pfx_be32(snapshot->program + 14) || snapshot->program[26] || snapshot->program[27] ||
        !pm_read(f, 28, branch, sizeof(branch)) || branch[0] != 0x60 || branch[1] != 0x2c || !pm_read(f, 30, snapshot->banner, sizeof(snapshot->banner)) ||
        xx_rt_memcmp(snapshot->banner, pfx_113p_420_banner, sizeof(snapshot->banner)) || !pm_read(f, 74, body, sizeof(body)) ||
        xx_rt_memcmp(body, pfx_113p_420_body, sizeof(body)))
        return false;
    end = 448U + (uint64_t)pfx_be32(snapshot->program + 6);
    if (end > (uint64_t)size - 4U || end <= at) return false;
    for (i = 0; i < 2U; ++i) {
        uint8_t *h = snapshot->header[i];
        if ((pd && xx_pd_is_stopped(pd)) || at > end || end - at < 25U || !pm_read(f, (int64_t)at, h, 24)) return false;
        snapshot->hs[i] = (unsigned)h[0] + 2U;
        if (snapshot->hs[i] < 25U || snapshot->hs[i] > sizeof(snapshot->header[i]) || snapshot->hs[i] > end - at || !pm_read(f, (int64_t)at, h, snapshot->hs[i]) ||
            24U + h[21] != snapshot->hs[i] || h[20] || xx_rt_memcmp(h + 2, "-lz5-", 5) || !pfx_name(h + 22, h[21], snapshot->name[i]))
            return false;
        /* Short Win32 paths collapse trailing dots. Reject that alias before
         * comparing the normalized file names and file/directory prefixes. */
        for (j = 0; snapshot->name[i][j]; ++j)
            if (snapshot->name[i][j] == '.' && (!snapshot->name[i][j + 1U] || snapshot->name[i][j + 1U] == '/')) return false;
        sum = 0;
        for (j = 2; j < snapshot->hs[i]; ++j) sum += h[j];
        if ((uint8_t)sum != h[1]) return false;
        snapshot->header_at[i] = at;
        snapshot->packed_at[i] = at + snapshot->hs[i];
        snapshot->packed[i] = xx_data_get_u32(h + 7, 4, 0, false);
        snapshot->raw[i] = xx_data_get_u32(h + 11, 4, 0, false);
        snapshot->crc[i] = xx_data_get_u16(h + 22U + h[21], 2, 0, false);
        if (!snapshot->packed[i] || !snapshot->raw[i] || snapshot->packed[i] > end - snapshot->packed_at[i] ||
            (!i && (uint64_t)(snapshot->hs[i] - 2U) + snapshot->packed[i] > INT16_MAX))
            return false;
        at = snapshot->packed_at[i] + snapshot->packed[i];
    }
    /* One zero terminates the member chain; the same exact producer may
     * add one zero DATA alignment byte before its four-zero relocations. */
    gap = end - at;
    if (!gap || gap > 2U || !pm_read(f, (int64_t)at, markers, (size_t)gap + 4U)) return false;
    for (i = 0; i < (unsigned)gap + 4U; ++i)
        if (markers[i]) return false;
    snapshot->data_end = end;
    snapshot->file_size = (uint64_t)size;
    if (pfx420_conflict(snapshot->name[0], snapshot->name[1]) ||
        ((uint64_t)size > end + 4U && (pfx420_conflict(snapshot->name[0], "overlay.bin") || pfx420_conflict(snapshot->name[1], "overlay.bin"))))
        return false;
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pfx420_unchanged(Abstractformat *f, const pfx420_snapshot *snapshot, xx_pd_struct *pd)
{
    pfx420_snapshot current;
    unsigned i;
    if (!pfx420_snapshot_read(f, &current, pd) || current.file_size != snapshot->file_size || current.data_end != snapshot->data_end ||
        xx_rt_memcmp(current.program, snapshot->program, sizeof(current.program)) || xx_rt_memcmp(current.banner, snapshot->banner, sizeof(current.banner)))
        return false;
    for (i = 0; i < 2U; ++i)
        if (current.hs[i] != snapshot->hs[i] || current.header_at[i] != snapshot->header_at[i] || current.packed_at[i] != snapshot->packed_at[i] ||
            current.packed[i] != snapshot->packed[i] || current.raw[i] != snapshot->raw[i] || xx_rt_memcmp(current.header[i], snapshot->header[i], current.hs[i]))
            return false;
    return true;
}
static bool pfx_space_history_decode(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, bool inverse, xx_pd_struct *pd)
{
    uint8_t history[4096], valid[4096];
    size_t at = 0, written = 0;
    unsigned ring = 4078U;
    xx_mem_set(history, 32, sizeof(history));
    xx_mem_set(valid, 1, 4078U);
    xx_mem_zero(valid + 4078U, 18U);
    while (written < output_size) {
        unsigned flags, bit;
        if ((pd && xx_pd_is_stopped(pd)) || at >= input_size) return false;
        flags = input[at++];
        for (bit = 0; bit < 8U && written < output_size; ++bit) {
            if (((flags >> bit) & 1U) == (unsigned)inverse) {
                unsigned origin, count, i;
                if (input_size - at < 2U) return false;
                origin = input[at] | ((unsigned)(input[at + 1U] & 0xf0U) << 4U);
                count = (input[at + 1U] & 15U) + 3U;
                at += 2U;
                if (count > output_size - written) return false;
                for (i = 0; i < count; ++i) {
                    unsigned slot = (origin + i) & 4095U;
                    uint8_t value;
                    if (!valid[slot] || (pd && xx_pd_is_stopped(pd))) return false;
                    value = history[slot];
                    output[written++] = value;
                    history[ring] = value;
                    valid[ring] = 1;
                    ring = (ring + 1U) & 4095U;
                }
            } else {
                uint8_t value;
                if (at >= input_size) return false;
                value = input[at++];
                output[written++] = value;
                history[ring] = value;
                valid[ring] = 1;
                ring = (ring + 1U) & 4095U;
            }
        }
    }
    return at == input_size && !(pd && xx_pd_is_stopped(pd));
}
static bool pfx420_inverse(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, xx_pd_struct *pd)
{
    return pfx_space_history_decode(input, input_size, output, output_size, true, pd);
}
static bool pfx420_crc_matches(const uint8_t *bytes, size_t size, uint16_t expected, xx_pd_struct *pd)
{
    uint16_t crc = 0;
    size_t i;
    unsigned bit;
    for (i = 0; i < size; ++i) {
        if (!(i & 65535U) && pd && xx_pd_is_stopped(pd)) return false;
        crc ^= bytes[i];
        for (bit = 0; bit < 8U; ++bit) crc = (uint16_t)((crc >> 1) ^ ((crc & 1U) ? 0xa001U : 0U));
    }
    return crc == expected && !(pd && xx_pd_is_stopped(pd));
}
static bool pfx420_write(xx_io_device *out, const uint8_t *bytes, size_t size, xx_pd_struct *pd)
{
    size_t at = 0;
    while (out && at < size) {
        ssize_t n;
        if (pd && xx_pd_is_stopped(pd)) return false;
        n = xx_io_write(out, bytes + at, size - at);
        if (n <= 0 || (size_t)n > size - at) return false;
        at += (size_t)n;
    }
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pfx420_read_all(Abstractformat *f, pm_member *m, xx_io_device *out, xx_pd_struct *pd)
{
    pfx420_member *context = (pfx420_member *)m->context;
    const pfx420_snapshot *snapshot = &context->snapshot;
    const xx_var *limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    uint8_t *packed = NULL, *plain = NULL;
    size_t at = 0;
    uint64_t offset, packed_size, raw_size;
    bool ok = false;
    if (context->index > 2U || m->offset < f->base_address || m->packed_size <= 0 || m->size <= 0 || (limit && (uint64_t)m->size > xx_var_get_u64(limit)) ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    offset = context->index < 2U ? snapshot->packed_at[context->index] : snapshot->data_end + 4U;
    packed_size = context->index < 2U ? snapshot->packed[context->index] : snapshot->file_size - offset;
    raw_size = context->index < 2U ? snapshot->raw[context->index] : packed_size;
    if ((uint64_t)(m->offset - f->base_address) != offset || (uint64_t)m->packed_size != packed_size || (uint64_t)m->size != raw_size ||
        (context->index < 2U && !pfx_recovery_limits(f, packed_size, raw_size)) || !pfx420_unchanged(f, snapshot, pd))
        return false;
    if (context->index == 2U) {
        size_t capacity = 65536U;
        uint64_t copied = 0;
        limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
        if (limit && xx_var_get_u64(limit) < capacity) capacity = (size_t)xx_var_get_u64(limit);
        if (!capacity) return false;
        packed = (uint8_t *)xx_mem_alloc(capacity);
        if (!packed) return false;
        while (copied < packed_size) {
            size_t n = (size_t)(packed_size - copied < capacity ? packed_size - copied : capacity);
            if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)(offset + copied), packed, n) || !pfx420_write(out, packed, n, pd)) goto done;
            copied += n;
        }
        ok = !(pd && xx_pd_is_stopped(pd));
        goto done;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)packed_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)raw_size);
    if (!packed || !plain) goto done;
    while (at < (size_t)packed_size) {
        size_t n = (size_t)packed_size - at;
        if (n > 65536U) n = 65536U;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)(offset + at), packed + at, n)) goto done;
        at += n;
    }
    if (!(context->index ? pfx420_inverse(packed, (size_t)packed_size, plain, (size_t)raw_size, pd)
                         : xx_lha_legacy_decode_native_exact(XX_LHA_LEGACY_LZ5, packed, (size_t)packed_size, plain, (size_t)raw_size, pd)) ||
        !pfx420_crc_matches(plain, (size_t)raw_size, snapshot->crc[context->index], pd))
        goto done;
    for (at = 0; at < (size_t)raw_size;) {
        size_t n = (size_t)raw_size - at;
        if (n > 65536U) n = 65536U;
        if (!pfx420_write(out, plain + at, n, pd)) goto done;
        at += n;
    }
    ok = !(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    return ok;
}
static bool pfx420_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    pfx420_snapshot snapshot;
    const xx_var *limit;
    unsigned i, count;
    if (s->count || !pfx420_snapshot_read(f, &snapshot, pd)) return false;
    /* Check both decoders' caps and lower caller limits before either eager
     * allocation. Opaque attachment memory is bounded by its streaming budget. */
    if (!pfx_recovery_limits(f, snapshot.packed[0], snapshot.raw[0]) || !pfx_recovery_limits(f, snapshot.packed[1], snapshot.raw[1])) return false;
    count = snapshot.file_size > snapshot.data_end + 4U ? 3U : 2U;
    limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (count == 3U && limit && snapshot.file_size - snapshot.data_end - 4U > xx_var_get_u64(limit)) return false;
    for (i = 0; i < count; ++i) {
        uint64_t at = i < 2U ? snapshot.packed_at[i] : snapshot.data_end + 4U;
        uint64_t packed = i < 2U ? snapshot.packed[i] : snapshot.file_size - at;
        pfx420_member *context;
        pm_member *m;
        if (!pm_add(f, s, "payload", (int64_t)at, (int64_t)packed)) return false;
        m = &s->items[s->count - 1U];
        m->size = (int64_t)(i < 2U ? snapshot.raw[i] : packed);
        m->display_name = xx_str_dup(i < 2U ? snapshot.name[i] : "overlay.bin");
        context = (pfx420_member *)xx_mem_alloc(sizeof(*context));
        if (!context || !m->display_name) {
            xx_mem_free(context);
            return false;
        }
        context->snapshot = snapshot;
        context->index = i;
        m->context = context;
        m->free_context = xx_mem_free;
        m->read_all = pfx420_read_all;
        if (i < 2U && !pfx420_read_all(f, m, NULL, pd)) return false;
    }
    s->size = (int64_t)snapshot.file_size;
    xx_format_set_version(f, "LArc PFX 1.13P (TEXT420 inverse flags)");
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pfx_read_all(Abstractformat *f, pm_member *m, xx_io_device *out, xx_pd_struct *pd)
{
    uint8_t *packed = NULL, *plain = NULL;
    size_t at = 0;
    bool valid = false;
    pfx_member *context = (pfx_member *)m->context;
    const xx_var *limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (m->packed_size <= 0 || m->size <= 0 || (uint64_t)m->packed_size > PFX_MAX_BUFFER || (uint64_t)m->size > PFX_MAX_BUFFER ||
        (limit && (uint64_t)m->packed_size + (uint64_t)m->size > xx_var_get_u64(limit)) || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (context->pfx334 && (!pfx_recovery_limits(f, (uint64_t)m->packed_size, (uint64_t)m->size) || !pfx334_unchanged(f, m, context, pd))) return false;
    if (context->pfx21 && (!pfx_recovery_limits(f, (uint64_t)m->packed_size, (uint64_t)m->size) || !pfx21_unchanged(f, m, context, pd))) return false;
    if (context->structural_recovery && !pfx_recovery_limits(f, (uint64_t)m->packed_size, (uint64_t)m->size)) return false;
    if (context->custom_standard && !pfx_custom_standard_unchanged(f, m, context)) return false;
    if (context->bounded_structural && (!pfx_recovery_limits(f, (uint64_t)m->packed_size, (uint64_t)m->size) || !pfx_bounded_long_unchanged(f, m, context))) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)m->packed_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)m->size);
    if (!packed || !plain) goto done;
    while (at < (size_t)m->packed_size) {
        size_t n = (size_t)m->packed_size - at;
        if (n > 65536) n = 65536;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, m->offset - f->base_address + (int64_t)at, packed + at, n)) goto done;
        at += n;
    }
    if (context->pfx334) {
        if (!pfx334_decode(packed, (size_t)m->packed_size, plain, (size_t)m->size, context->seed334, pd) || !pfx_inner_gemdos(plain, (size_t)m->size, pd)) goto done;
    } else if (context->pfx21) {
        if (!xx_lha_legacy_decode_native_exact(XX_LHA_LEGACY_LZ5, packed, (size_t)m->packed_size, plain, (size_t)m->size, pd) ||
            !pfx_inner_gemdos(plain, (size_t)m->size, pd))
            goto done;
    } else if (context->custom_standard) {
        if (!pfx_custom_standard_unchanged(f, m, context) || !pfx_space_history_decode(packed, (size_t)m->packed_size, plain, (size_t)m->size, false, pd) ||
            !pfx_inner_gemdos(plain, (size_t)m->size, pd))
            goto done;
    } else if (context->structural_recovery) {
        if (!(context->custom_metadata ? pfx_custom_long_unchanged(f, m, context) : pfx_erased_unchanged(f, m, context))) goto done;
        if (!xx_lha_legacy_decode_native_exact(XX_LHA_LEGACY_LZ5, packed, (size_t)m->packed_size, plain, (size_t)m->size, pd)) {
            /* Only custom-long recovery may contain one counted zero in
             * addition to its separately bounded outside framing byte.
             * The retry is exact and still requires the complete inner
             * GEMDOS object; erased metadata retains exact whole input. */
            if (!context->custom_metadata || (pd && xx_pd_is_stopped(pd)) || m->packed_size <= 1 || packed[(size_t)m->packed_size - 1U] != 0 ||
                !xx_lha_legacy_decode_native_exact(XX_LHA_LEGACY_LZ5, packed, (size_t)m->packed_size - 1U, plain, (size_t)m->size, pd))
                goto done;
        }
        if (!pfx_inner_gemdos(plain, (size_t)m->size, pd)) goto done;
    } else if (context->bounded_structural) {
        /* A catalog-authorized structural member must stay on this stricter
         * path even if changed payload bytes would satisfy a legacy route. */
        if (m->packed_size <= 1 || packed[(size_t)m->packed_size - 1U] != 0 ||
            !xx_lha_legacy_decode_native_exact(XX_LHA_LEGACY_LZ5, packed, (size_t)m->packed_size - 1U, plain, (size_t)m->size, pd) ||
            !pfx_inner_gemdos(plain, (size_t)m->size, pd))
            goto done;
    } else if (context->bounded_long) {
        if (!pfx_bounded_long_unchanged(f, m, context)) goto done;
        if (!xx_lha_legacy_decode_native_exact(XX_LHA_LEGACY_LZ5, packed, (size_t)m->packed_size, plain, (size_t)m->size, pd)) {
            /* Preserve CRC-authenticated padding, including arbitrary inner
             * data or overlays. Only a capped eager catalog probe may admit
             * a stale-CRC alternative with complete inner relocation EOF. */
            if (m->packed_size <= 1 || packed[(size_t)m->packed_size - 1U] != 0 ||
                !xx_lha_legacy_decode_native_exact(XX_LHA_LEGACY_LZ5, packed, (size_t)m->packed_size - 1U, plain, (size_t)m->size, pd))
                goto done;
            if (pfx_crc16(plain, (size_t)m->size) != context->crc) {
                if (!context->bounded_probe || (pd && xx_pd_is_stopped(pd)) || !pfx_recovery_limits(f, (uint64_t)m->packed_size, (uint64_t)m->size) ||
                    !pfx_inner_gemdos(plain, (size_t)m->size, pd) || (pd && xx_pd_is_stopped(pd)))
                    goto done;
                context->bounded_structural = true;
            }
        }
    } else if (context->method == XX_LHA_LEGACY_LZ5) {
        if (!xx_lha_legacy_decode_native(XX_LHA_LEGACY_LZ5, packed, (size_t)m->packed_size, plain, (size_t)m->size, pd)) {
            /* Retry only the authenticated first/single overstated stream.
             * Preserve the one-byte framing rule before structural recovery. */
            if (!context->terminal_framing_retry || !pfx_terminal_framing(f, m, context)) goto done;
            if (!xx_lha_legacy_decode_native_exact(XX_LHA_LEGACY_LZ5, packed, (size_t)m->packed_size - 1U, plain, (size_t)m->size, pd)) {
                /* Two framing bytes require a complete GEMDOS program, not
                 * the non-authoritative producer CRC. Never ignore more than
                 * two bytes, clip tokens or expand structural recovery caps. */
                if (m->packed_size <= 2 || !pfx_recovery_limits(f, (uint64_t)m->packed_size, (uint64_t)m->size) ||
                    !xx_lha_legacy_decode_native_exact(XX_LHA_LEGACY_LZ5, packed, (size_t)m->packed_size - 2U, plain, (size_t)m->size, pd) ||
                    !pfx_inner_gemdos(plain, (size_t)m->size, pd))
                    goto done;
            }
        }
    } else if (context->method == UINT32_C(0x6c6831)) {
        size_t written = 0;
        if (!xx_lzh1_decode_memory(packed, (size_t)m->packed_size, plain, (size_t)m->size, &written) || written != (size_t)m->size) goto done;
    } else goto done;
    if (!context->pfx334 && !context->pfx21 && !context->producer_crc && pfx_crc16(plain, (size_t)m->size) != context->crc) {
        xx_pd_set_error(pd, 1, "PFX member payload CRC mismatch");
        goto done;
    }
    for (at = 0; out && at < (size_t)m->size;) {
        size_t n = (size_t)m->size - at;
        ssize_t wrote;
        if (n > 65536) n = 65536;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        wrote = xx_io_write(out, plain + at, n);
        if (wrote <= 0 || (size_t)wrote > n) goto done;
        at += (size_t)wrote;
    }
    valid = !(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    return valid;
}
static bool pfx_add(Abstractformat *f, pm_stream *s, const uint8_t *header, uint64_t at, unsigned hs, uint64_t packed, bool producer_crc, bool fallback_name,
                    const uint8_t *terminal_program, bool structural_recovery, bool bounded_long, bool custom_metadata, bool custom_standard)
{
    char name[256];
    pm_member *m;
    pfx_member *context;
    uint32_t method = xx_rt_memcmp(header + 2, "-lz5-", 5) ? UINT32_C(0x6c6831) : XX_LHA_LEGACY_LZ5;
    if (custom_metadata || custom_standard || !pfx_name(header + 22, header[21], name)) {
        const char *source = xx_io_source_path(f->device), *leaf = source, *p;
        if (!fallback_name) return false;
        for (p = source; p && *p; ++p)
            if (*p == '/' || *p == '\\') leaf = p + 1;
        if (!leaf || !pfx_name((const uint8_t *)leaf, xx_rt_strlen(leaf), name)) xx_mem_copy(name, "data", 5);
    }
    if (!pm_add(f, s, "payload", (int64_t)(at + hs), (int64_t)packed)) return false;
    m = &s->items[s->count - 1];
    m->size = xx_data_get_u32(header + 11, 4, 0, false);
    m->read_all = pfx_read_all;
    m->display_name = xx_str_dup(name);
    context = (pfx_member *)xx_mem_alloc(sizeof(*context));
    if (!context || !m->display_name) {
        xx_mem_free(context);
        return false;
    }
    context->header_offset = f->base_address + (int64_t)at;
    context->header_size = hs;
    context->method = method;
    context->crc = xx_data_get_u16(header + 22 + header[21], 2, 0, false);
    context->producer_crc = producer_crc && method == XX_LHA_LEGACY_LZ5;
    context->terminal_framing_retry = false;
    context->structural_recovery = structural_recovery;
    context->bounded_long = bounded_long;
    context->bounded_probe = false;
    context->bounded_structural = false;
    context->custom_metadata = custom_metadata;
    context->custom_standard = custom_standard;
    context->pfx21 = false;
    context->pfx334 = false;
    if (bounded_long && !pm_read(f, 30, context->banner, sizeof(context->banner))) {
        xx_mem_free(context);
        return false;
    }
    if (structural_recovery && !custom_metadata && !pm_read(f, 30, context->banner, sizeof(context->banner))) {
        xx_mem_free(context);
        return false;
    }
    if (custom_standard &&
        (!terminal_program || !pm_read(f, 30, context->banner, sizeof(context->banner)) || !pfx_custom_standard_envelope(f, terminal_program, header, hs, at))) {
        xx_mem_free(context);
        return false;
    }
    if (custom_metadata && (!terminal_program || !pm_read(f, 30, context->banner, sizeof(context->banner)) ||
                            !pfx_custom_long_envelope(f, terminal_program, header, hs, at, &context->framing_byte))) {
        xx_mem_free(context);
        return false;
    }
    if (terminal_program) {
        xx_mem_copy(context->program_header, terminal_program, sizeof(context->program_header));
        xx_mem_copy(context->member_header, header, hs);
        context->terminal_framing_retry = !structural_recovery && !bounded_long && pfx_terminal_framing(f, m, context);
    }
    m->context = context;
    m->free_context = xx_mem_free;
    return true;
}
static bool pfx334_parse(Abstractformat *f, pm_stream *s, const uint8_t *program, xx_pd_struct *pd)
{
    uint8_t header[257];
    uint64_t data = pfx_be32(program + 6), packed, plain;
    unsigned hs;
    pfx_member *context;
    pm_member *m;
    if (s->count || data < 25U || data > PFX_RECOVERY_PACKED || !pfx334_producer(f) || !pm_read(f, 362, header, 24)) return false;
    hs = (unsigned)header[0] + 2U;
    if (hs < 25U || hs > data || !pm_read(f, 362, header, hs)) return false;
    packed = xx_data_get_u32(header + 7, 4, 0, false);
    plain = xx_data_get_u32(header + 11, 4, 0, false);
    if (!pfx_recovery_limits(f, packed, plain) || !pfx334_envelope(f, program, header, hs, 362U, pd) ||
        !pfx_add(f, s, header, 362U, hs, packed, false, false, NULL, false, false, false, false))
        return false;
    m = &s->items[s->count - 1U];
    context = (pfx_member *)m->context;
    context->pfx334 = true;
    /* The exact loader chooses its private LZ5-style codec, independently of
     * the original LH5 method bytes retained in the immutable snapshot. */
    context->method = XX_LHA_LEGACY_LZ5;
    xx_mem_copy(context->program_header, program, sizeof(context->program_header));
    xx_mem_copy(context->member_header, header, hs);
    if (!pm_read(f, 30, context->banner, sizeof(context->banner)) ||
        !pm_read(f, (int64_t)(362U + data - sizeof(context->seed334)), context->seed334, sizeof(context->seed334)) || !pfx_read_all(f, m, NULL, pd))
        return false;
    s->size = pm_available(f);
    xx_format_set_version(f, "LArc PFX 1.13P (TEXT334)");
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pfx21_parse(Abstractformat *f, pm_stream *s, const uint8_t *program, xx_pd_struct *pd)
{
    uint8_t header[257];
    uint64_t data = pfx_be32(program + 6), packed, plain;
    unsigned hs;
    pfx_member *context;
    pm_member *m;
    if (s->count || data < 25U || data > PFX_RECOVERY_PACKED || !pfx21_producer(f) || !pm_read(f, 1050, header, 24)) return false;
    hs = (unsigned)header[0] + 2U;
    if (hs < 25U || hs > data || !pm_read(f, 1050, header, hs)) return false;
    packed = xx_data_get_u32(header + 7, 4, 0, false);
    plain = xx_data_get_u32(header + 11, 4, 0, false);
    if (!pfx_recovery_limits(f, packed, plain) || !pfx21_envelope(f, program, header, hs, 1050U, pd) ||
        !pfx_add(f, s, header, 1050U, hs, packed, false, false, NULL, false, false, false, false))
        return false;
    m = &s->items[s->count - 1U];
    context = (pfx_member *)m->context;
    context->pfx21 = true;
    xx_mem_copy(context->program_header, program, sizeof(context->program_header));
    xx_mem_copy(context->member_header, header, hs);
    if (!pm_read(f, 30, context->banner21, sizeof(context->banner21)) || !pfx_read_all(f, m, NULL, pd)) return false;
    s->size = pm_available(f);
    xx_format_set_version(f, "LArc PFX 2.1P");
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t program[28], marker[4], header[257], tail[4];
    uint64_t text, data, bss, symbols, at, end, packed, plain, stream;
    unsigned hs, i, sum;
    bool bounded_producer, strict_producer;
    int64_t size = pm_available(f);
    if (size < 60 || size > INT32_MAX || (pd && xx_pd_is_stopped(pd)) || !pm_read(f, 0, program, sizeof(program)) || program[0] != 0x60 || program[1] != 0x1a)
        return false;
    text = pfx_be32(program + 2);
    data = pfx_be32(program + 6);
    bss = pfx_be32(program + 10);
    symbols = pfx_be32(program + 14);
    if (text < 32 || text > INT32_MAX || !data || data > INT32_MAX || bss > INT32_MAX || symbols > INT32_MAX) return false;
    at = 28 + text;
    end = at + data;
    if (text == 420U) return pfx420_parse(f, s, pd);
    if (text == 334U) return pfx334_parse(f, s, program, pd);
    if (text == 1022U) return pfx21_parse(f, s, program, pd);
    if (at > (uint64_t)size || end > (uint64_t)size || data < 24 || !pm_read(f, (int64_t)at - 4, marker, 4) || xx_rt_memcmp(marker, "\xde\xad\xfa\xce", 4)) return false;
    strict_producer = pfx_producer(f, text);
    if (!strict_producer && !pfx_long_body(f, text) && !(text == 392U && pfx_custom_standard_producer(f))) return false;
    bounded_producer = text == 392 || text == 386;
    while (at < end) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        /* GEMDOS DATA may include up to two zero alignment bytes, or one
         * relocation byte immediately before its four-zero trailer. */
        if (s->count && end - at <= 2) {
            if (!pm_read(f, (int64_t)at, tail, (size_t)(end - at))) return false;
            if (!tail[0] && (end - at == 1 || !tail[1])) break;
            if (end - at == 1 && end <= (uint64_t)size - 4 && pm_read(f, (int64_t)end, tail, 4) && !tail[0] && !tail[1] && !tail[2] && !tail[3]) break;
            return false;
        }
        if (s->count >= 1024 || end - at < 24 || !pm_read(f, (int64_t)at, header, 24)) return false;
        hs = (unsigned)header[0] + 2U;
        if (hs < 24 || hs > end - at || !pm_read(f, (int64_t)at, header, hs) || header[20] != 0 ||
            (xx_rt_memcmp(header + 2, "-lz5-", 5) && (s->count == 0 || xx_rt_memcmp(header + 2, "-lh1-", 5))))
            return false;
        sum = 0;
        for (i = 2; i < hs; ++i) sum += header[i];
        if (24U + header[21] > hs) return false;
        packed = xx_data_get_u32(header + 7, 4, 0, false);
        plain = xx_data_get_u32(header + 11, 4, 0, false);
        stream = end - at - hs;
        if (!packed || !plain || plain > PFX_MAX_BUFFER || packed > INT32_MAX) return false;
        /* This separate exact space-banner producer always requires
         * the full object, private initialized history and no padding retry. */
        if (!s->count && pfx_custom_standard_envelope(f, program, header, hs, at)) {
            if (plain > packed * 9U + 144U || !pfx_recovery_limits(f, packed, plain) ||
                !pfx_add(f, s, header, at, hs, packed, true, true, program, true, false, false, true) || !pfx_read_all(f, &s->items[s->count - 1U], NULL, pd))
                return false;
            at = end;
            break;
        }
        /* Customized long metadata has a distinct first/single envelope.
         * Require an exact stream and full inner GEMDOS before cataloging. */
        if (!s->count && pfx_custom_long_envelope(f, program, header, hs, at, NULL)) {
            if (plain > packed * 9U + 144U || !pfx_recovery_limits(f, packed, plain) ||
                !pfx_add(f, s, header, at, hs, packed, true, true, program, true, false, true, false) || !pfx_read_all(f, &s->items[s->count - 1U], NULL, pd))
                return false;
            at = end;
            break;
        }
        /* A custom banner must never reach an ordinary producer path. */
        if (!strict_producer) return false;
        /* Erased metadata is recovered structurally, never by trusting stale
         * header/payload checksums. Authenticate the complete single-stream
         * envelope, exact packed consumption and inner GEMDOS relocation EOF
         * before exposing even a catalog record. Ordinary headers stay strict. */
        if (!s->count && pfx_erased_envelope(f, program, header, hs, at)) {
            if (plain > packed * 9U + 144U || !pfx_recovery_limits(f, packed, plain) ||
                !pfx_add(f, s, header, at, hs, packed, true, true, program, true, false, false, false) || !pfx_read_all(f, &s->items[s->count - 1U], NULL, pd))
                return false;
            at = end;
            break;
        }
        if ((uint8_t)sum != header[1]) return false;
        if (packed > stream) {
            /* Original PFX layout counts bytes absent from the DATA segment.
             * The exact known instruction body gates this producer policy. */
            if (s->count || !stream || plain > stream * 9U + 144U || !pfx_add(f, s, header, at, hs, stream, true, true, program, false, false, false, false))
                return false;
            at = end;
            break;
        }
        if (!s->count && (text == 534U || text == 556U)) {
            if (plain > packed * 9U + 144U || !pfx_bounded_long_envelope(f, program, header, hs, at) ||
                !pfx_add(f, s, header, at, hs, packed, true, false, program, false, true, false, false))
                return false;
            /* Keep larger or caller-limit-ineligible legacy entries lazy.
             * They cannot acquire structural authorization on extraction.
             * Eligible entries classify exact and CRC-authenticated routes
             * first, then the narrowly bounded complete-object alternative. */
            if (pfx_recovery_limits(f, packed, plain)) {
                pfx_member *context = (pfx_member *)s->items[s->count - 1U].context;
                bool valid;
                context->bounded_probe = true;
                valid = pfx_read_all(f, &s->items[s->count - 1U], NULL, pd);
                context->bounded_probe = false;
                if (!valid) return false;
            }
            at = end;
            break;
        }
        if (!bounded_producer || plain > packed * 9U + 144U || !pfx_add(f, s, header, at, hs, packed, true, false, NULL, false, false, false, false)) return false;
        at += hs + packed;
    }
    if (!s->count) return false;
    s->size = size;
    xx_format_set_version(f, "LArc PFX 1.13P");
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pfx_record_context(xx_archive_record_state *st)
{
    pm_stream *s;
    pfx_member *m;
    if (!st || !st->has_record) return false;
    s = (pm_stream *)st->internal_state;
    if (s->items[s->index].read_all == pfx420_read_all) {
        const pfx420_member *context = (const pfx420_member *)s->items[s->index].context;
        const pfx420_snapshot *snapshot = &context->snapshot;
        unsigned i = context->index;
        st->current_record.header_offset = st->format->base_address + (int64_t)(i < 2U ? snapshot->header_at[i] : snapshot->data_end + 4U);
        st->current_record.header_size = i < 2U ? snapshot->hs[i] : 0;
        return xx_archive_record_set_meta_u64(&st->current_record, XX_META_ID_COMPRESSION_METHOD, i < 2U ? XX_LHA_LEGACY_LZ5 : 0) &&
               xx_archive_record_set_meta_str(&st->current_record, XX_META_ID_COMMENT,
                                              i == 0U
                                                  ? "PFX TEXT420: standard LZ5; original CRC required"
                                                  : (i == 1U ? "PFX TEXT420: inverse LZ5 flags; original CRC required" : "PFX TEXT420: stored opaque resource overlay"));
    }
    m = (pfx_member *)s->items[s->index].context;
    st->current_record.header_offset = m->header_offset;
    st->current_record.header_size = m->header_size;
    if (!xx_archive_record_set_meta_u64(&st->current_record, XX_META_ID_COMPRESSION_METHOD, m->method)) return false;
    if (m->pfx334)
        return xx_archive_record_set_meta_str(
            &st->current_record, XX_META_ID_COMMENT,
            "PFX TEXT334: private space-history LZ5-style codec despite LH5 field; complete structural GEMDOS validation; stored CRC non-authoritative");
    if (m->pfx21)
        return xx_archive_record_set_meta_str(&st->current_record, XX_META_ID_COMMENT,
                                              "PFX 2.1P: complete structural GEMDOS validation; stored payload CRC non-authoritative");
    if (m->bounded_structural)
        return xx_archive_record_set_meta_str(&st->current_record, XX_META_ID_COMMENT,
                                              "PFX bounded-long structural counted-zero recovery: complete GEMDOS relocation EOF; stored payload CRC non-authoritative");
    return !m->structural_recovery || xx_archive_record_set_meta_str(&st->current_record, XX_META_ID_COMMENT,
                                                                     "Recovered PFX metadata: structural GEMDOS recovery; producer checksums non-authoritative");
}
static xx_archive_record_state *pfx_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    xx_archive_record_state *st = pm_create_records(f, opts, pd);
    if (st && st->has_record && !pfx_record_context(st)) {
        pm_free_records(f, st);
        return NULL;
    }
    return st;
}
static bool pfx_next(Abstractformat *f, xx_archive_record_state *st, xx_pd_struct *pd)
{
    if (!pm_next(f, st, pd)) return false;
    st->has_record = pfx_record_context(st);
    return st->has_record;
}
void xx_larc_pfx_init(xx_larc_pfx *r, xx_io_device *d, int64_t base)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, base, XX_FILE_TYPE_LARC_PFX, "prg");
        r->format.endian = XX_ENDIAN_BIG;
        xx_format_set_mime_type(&r->format, "application/x-atari-st-executable");
        r->format.create_archive_records_reading = pfx_records;
        r->format.archive_record_move_to_next = pfx_next;
    }
}
xx_larc_pfx *xx_larc_pfx_create(xx_io_device *d, int64_t base)
{
    xx_larc_pfx *r = (xx_larc_pfx *)xx_mem_alloc(sizeof(*r));
    if (r) xx_larc_pfx_init(r, d, base);
    return r;
}
void xx_larc_pfx_destroy(xx_larc_pfx *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_larc_pfx_free(xx_larc_pfx *r)
{
    if (r) {
        xx_larc_pfx_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_larc_pfx_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_larc_pfx_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
xx_file_type_t xx_larc_pfx_detect(xx_io_device *d, int64_t base)
{
    xx_larc_pfx r;
    bool valid;
    xx_larc_pfx_init(&r, d, base);
    valid = pm_valid(&r.format, NULL);
    xx_larc_pfx_destroy(&r);
    return valid ? XX_FILE_TYPE_LARC_PFX : XX_FILE_TYPE_UNKNOWN;
}
