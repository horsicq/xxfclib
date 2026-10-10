/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Explicit bounded FZF component reader. Format facts: Casio
 * FZ-1 Data Structures (T. Sasaki, 1987), bank/voice/wave block layouts, and
 * Jacob Vosmaer's one-bank FZF writer. Full/bank wrapper counts at0x3E8
 * follow the published HxC FZF loader and independently match retained
 * Casio factory dumps. No missing disk sectors are invented.
 */
#include "xxfclib/formats/casio_fzf/xx_casio_fzf.h"
#include "../xx_hxc_sector.h"
#include "xxfclib/data/xx_data.h"
static bool fzf_parse(Abstractformat *f, pm_stream *s, hc_blob *b)
{
    xx_casio_fzf *r = (xx_casio_fzf *)f;
    uint32_t areas, i, k, banks = 1U, voice_count = 0U, audio, samples;
    uint32_t start, end, genstart, genend, maxend = 0U, ptr, wave_blocks;
    bool referenced[64] = {false}, declared;
    char name[64];
    if (b->n < 3072U || (b->n & 1023U)) return false;
    wave_blocks = xx_data_get_u16(b->p + 0x3E8U + 12U, 2, 0, false);
    declared = b->p[0x3E8U + 7U] || b->p[0x3E8U + 8U] || wave_blocks;
    if (declared) {
        banks = b->p[0x3E8U + 7U];
        voice_count = b->p[0x3E8U + 8U];
        if ((b->p[0x3E8U + 5U] != 0U && b->p[0x3E8U + 5U] != 2U) || b->p[0x3E8U + 6U] || !banks || banks > 8U || !voice_count || voice_count > 64U || !wave_blocks ||
            (b->p[0x3E8U + 5U] == 2U && banks != 1U) || (banks + (voice_count + 3U) / 4U + wave_blocks) * 1024U != b->n)
            return false;
    }
    for (k = 0U; k < banks; ++k) {
        const uint8_t *bank = b->p + k * 1024U;
        if (!hc_poll(b) || !hc_ascii_name(bank + 0x282U, 12U)) return false;
        areas = xx_data_get_u16(bank, 2, 0, false);
        if (!areas || areas > 64U) return false;
        for (i = 0U; i < areas; ++i) {
            ptr = xx_data_get_u16(bank + 0x202U + i * 2U, 2, 0, false);
            if (ptr >= (declared ? voice_count : 64U) || bank[2U + i] > 127U || bank[0x42U + i] > bank[2U + i] || bank[0x82U + i] > 127U ||
                bank[0xC2U + i] > bank[0x82U + i])
                return false;
            referenced[ptr] = true;
            if (!declared && ptr + 1U > voice_count) voice_count = ptr + 1U;
        }
    }
    audio = banks * 1024U + ((voice_count + 3U) / 4U) * 1024U;
    if (audio >= b->n) return false;
    samples = (b->n - audio) / 2U;
    for (i = 0U; i < voice_count; ++i)
        if (declared || referenced[i]) {
            const uint8_t *v = b->p + banks * 1024U + i * 256U;
            if (!hc_poll(b)) return false;
            /* The manual specifies zero padding; retained factory files also
             * use ASCII spaces. Preserve either independently verified form. */
            if (!hc_ascii_name(v + 0xB2U, 12U) || (v[0xBEU] && v[0xBEU] != 32U) || (v[0xBFU] && v[0xBFU] != 32U)) return false;
            start = xx_data_get_u32(v, 4, 0, false);
            end = xx_data_get_u32(v + 4U, 4, 0, false);
            genstart = xx_data_get_u32(v + 8U, 4, 0, false);
            genend = xx_data_get_u32(v + 12U, 4, 0, false);
            if (start > genstart || genstart > genend || genend > end || end > samples) return false;
            if (end > maxend) maxend = end;
        }
    /* Exact rounded waveform extent additionally protects the metadata-free
     * canonical one-bank case against unproven trailing layouts. */
    if (!declared && (!maxend || ((maxend * 2U + 1023U) & ~1023U) != b->n - audio)) return false;
    for (k = 0U; k < banks; ++k) {
        xx_rt_snprintf(name, sizeof(name), "bank-%03u.bin", k);
        if (!hc_emit(f, s, b, name, k * 1024U, 1024U)) return false;
    }
    for (i = 0U; i < voice_count; ++i)
        if (declared || referenced[i]) {
            xx_rt_snprintf(name, sizeof(name), "voice-%03u.parameters", i);
            if (!hc_emit(f, s, b, name, banks * 1024U + i * 256U, 256U)) return false;
        }
    if (!hc_emit(f, s, b, "waveforms.pcm", audio, b->n - audio)) return false;
    r->note = declared ? "explicit full/bank FZF; exact declared bank/voice/PCM blocks; no synthesized disk data"
                       : "explicit canonical one-bank FZF; original bank, referenced voice parameters and complete stored little-endian PCM blocks";
    return true;
}
HC_PARSE_WRAPPER(fzf_parse)
HC_DEFINE_READER(casio_fzf, XX_FILE_TYPE_CASIO_FZF, "fzf")
