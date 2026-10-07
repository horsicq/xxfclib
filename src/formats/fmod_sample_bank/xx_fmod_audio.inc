/* SPDX-License-Identifier: MIT. Original FSB PCM/WAV reconstruction.
 * Metadata bit positions checked against FMOD's published sample structures
 * and vgmstream's FSB5 parser. No third-party extraction code is copied.
 */

static uint64_t fsba_option_bytes(const xx_list_s *options) {
    uint64_t bytes; size_t i;
    if (!options) return 0;
    if (options->count > 4096U) return UINT64_MAX;
    bytes = options->count * sizeof(xx_meta) * 2U;
    for (i = 0; i < options->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(options, i); uint64_t size = 0;
        if (!item) return UINT64_MAX;
        if (item->var.type == XX_VAR_TYPE_STRING || item->var.type == XX_VAR_TYPE_STRING_VIEW) {
            if (item->var.val.str.len > 65536U) return UINT64_MAX;
            size = item->var.val.str.len + 1U;
        } else if (item->var.type == XX_VAR_TYPE_WSTRING || item->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            if (item->var.val.wstr.len > 32768U) return UINT64_MAX;
            size = (item->var.val.wstr.len + 1U) * sizeof(wchar_t);
        } else if (item->var.type == XX_VAR_TYPE_BYTES || item->var.type == XX_VAR_TYPE_BYTES_VIEW) size = item->var.val.bytes.size;
        if (size > UINT64_MAX - bytes) return UINT64_MAX;
        bytes += size;
    }
    return bytes;
}

static bool fsba_extension(fsb_member *m, const char *extension) {
    const char *old = xx_rt_strrchr(m->name, '.');
    char *name;
    if (old && fsb_same(old, extension)) return true;
    name = xx_str_concat(m->name, extension);
    if (!name) return false;
    xx_str_free(m->name); m->name = name; return true;
}

static bool fsb_prepare_audio(fsb_member *m) {
    bool floating, extended, rf64;
    unsigned header, at, format_size;
    uint64_t raw, padding;
    uint32_t mask = 0, align;
    m->output_data_size = m->output_size = (uint64_t)m->size;
    if (!m->codec && m->mode) {
        if (m->mode & 0x200U) m->codec = 11U; /* Legacy MPEG storage flag. */
        else if (m->mode & 0x00400000U) m->codec = 7U;
        else if (!(m->mode & 0x0b800e00U)) {
            if (m->mode & 8U) { m->codec = 1U; m->bits = 8U; m->flip_sign = (m->mode & 0x100U) != 0; }
            else if (m->mode & 16U) { m->codec = 2U; m->bits = 16U; m->flip_sign = (m->mode & 0x80U) != 0 && !(m->mode & 0x100U); }
            if (!m->channels) m->channels = (m->mode & 0x40U) ? 2U : 1U;
        }
    }
    if (m->codec == 11U && m->channels <= 2U) return fsba_extension(m, ".mp3");
    if (m->codec < 1U || m->codec > 5U) return true; /* Retain opaque codec payload. */
    if (!m->bits) m->bits = m->codec == 1U ? 8U : m->codec == 2U ? 16U : m->codec == 3U ? 24U : 32U;
    if (!m->channels || m->channels > 32U || !m->rate || m->rate > 384000U) return false;
    m->byte_width = (uint8_t)(m->bits / 8U);
    raw = (uint64_t)m->samples * m->channels * m->byte_width;
    if (raw > (uint64_t)m->size) return false;
    m->output_data_size = raw; padding = raw & 1U;
    floating = m->codec == 5U; extended = m->channels > 2U;
    format_size = extended ? 40U : 16U;
    header = 12U + 8U + format_size + (floating ? 12U : 0U) + 8U;
    rf64 = raw + padding + header - 8U > UINT32_MAX;
    if (rf64) header += 36U;
    if (header > sizeof(m->wave_header)) return false;
    m->wave_size = (uint8_t)header;
    m->output_size = raw + padding + header;
    xx_mem_zero(m->wave_header, sizeof(m->wave_header));
    xx_rt_memcpy(m->wave_header, rf64 ? "RF64" : "RIFF", 4U);
    xx_data_set_u32(m->wave_header + 4U, 4, 0, rf64 ? UINT32_MAX : (uint32_t)(m->output_size - 8U), false);
    xx_rt_memcpy(m->wave_header + 8U, "WAVE", 4U); at = 12U;
    if (rf64) {
        xx_rt_memcpy(m->wave_header + at, "ds64", 4U); xx_data_set_u32(m->wave_header + at + 4U, 4, 0, 28U, false);
        xx_data_set_u64(m->wave_header + at + 8U, 8, 0, m->output_size - 8U, false); xx_data_set_u64(m->wave_header + at + 16U, 8, 0, raw, false);
        xx_data_set_u64(m->wave_header + at + 24U, 8, 0, m->samples, false); at += 36U;
    }
    align = (uint32_t)m->channels * m->byte_width;
    xx_rt_memcpy(m->wave_header + at, "fmt ", 4U); xx_data_set_u32(m->wave_header + at + 4U, 4, 0, format_size, false);
    xx_data_set_u16(m->wave_header + at + 8U, 2, 0, extended ? 0xfffeU : floating ? 3U : 1U, false);
    xx_data_set_u16(m->wave_header + at + 10U, 2, 0, m->channels, false); xx_data_set_u32(m->wave_header + at + 12U, 4, 0, m->rate, false);
    xx_data_set_u32(m->wave_header + at + 16U, 4, 0, m->rate * align, false); xx_data_set_u16(m->wave_header + at + 20U, 2, 0, (uint16_t)align, false);
    xx_data_set_u16(m->wave_header + at + 22U, 2, 0, m->bits, false);
    if (extended) {
        static const uint8_t guid[16] = {1,0,0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
        if (m->channels == 6U) mask = 0x3fU;
        if (m->channels == 8U) mask = 0x63fU;
        xx_data_set_u16(m->wave_header + at + 24U, 2, 0, 22U, false); xx_data_set_u16(m->wave_header + at + 26U, 2, 0, m->bits, false);
        xx_data_set_u32(m->wave_header + at + 28U, 4, 0, mask, false); xx_rt_memcpy(m->wave_header + at + 32U, guid, sizeof(guid));
        if (floating) m->wave_header[at + 32U] = 3U;
    }
    at += 8U + format_size;
    if (floating) { xx_rt_memcpy(m->wave_header + at, "fact", 4U); xx_data_set_u32(m->wave_header + at + 4U, 4, 0, 4U, false); xx_data_set_u32(m->wave_header + at + 8U, 4, 0, m->samples, false); at += 12U; }
    xx_rt_memcpy(m->wave_header + at, "data", 4U); xx_data_set_u32(m->wave_header + at + 4U, 4, 0, rf64 ? UINT32_MAX : (uint32_t)raw, false);
    return fsba_extension(m, ".wav");
}

static bool fsba_write(xx_io_device *output, const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    while (output && size) { ssize_t n = xx_io_write(output, bytes, size); if (n <= 0 || (size_t)n > size) return false; bytes += n; size -= (size_t)n; }
    return true;
}

/* Complete reconstructed output flows through one bounded stack buffer.
 * A NULL output performs TEST without opening a temporary stream or file. */
static bool fsba_transfer(Abstractformat *self, const fsb_member *m, xx_io_device *output, xx_pd_struct *pd) {
    uint8_t buffer[16384]; uint64_t done = 0, current = 0; int level;
    int64_t saved = xx_io_tell(self->device); bool okay = false;
    level = xx_pd_enter_level(pd, m->output_size, "FMOD sample");
    if (xx_pd_is_stopped(pd) || !fsba_write(output, m->wave_header, m->wave_size)) goto finish;
    current = m->wave_size; xx_pd_set_current(pd, level, current);
    if (xx_io_seek64(self->device, self->base_address + m->data_offset, SEEK_SET) != 0) goto finish;
    while (done < m->output_data_size) {
        uint64_t left = m->output_data_size - done;
        size_t take = left < sizeof(buffer) ? (size_t)left : sizeof(buffer), read = 0, i;
        if (m->byte_width > 1U && take < left) take -= take % m->byte_width;
        if (xx_pd_is_stopped(pd)) goto finish;
        while (read < take) { ssize_t n = xx_io_read(self->device, buffer + read, take - read); if (n <= 0 || (size_t)n > take - read) goto finish; read += (size_t)n; }
        if (m->swap_endian) for (i = 0; i < take; i += m->byte_width) {
            unsigned j; for (j = 0; j < m->byte_width / 2U; ++j) { uint8_t byte = buffer[i + j]; buffer[i + j] = buffer[i + m->byte_width - j - 1U]; buffer[i + m->byte_width - j - 1U] = byte; }
        }
        if (m->flip_sign) for (i = m->byte_width - 1U; i < take; i += m->byte_width) buffer[i] ^= 0x80U;
        if (!fsba_write(output, buffer, take)) goto finish;
        done += take; current += take; xx_pd_set_current(pd, level, current);
    }
    if (m->wave_size && (m->output_data_size & 1U)) { const uint8_t zero = 0; if (!fsba_write(output, &zero, 1U)) goto finish; ++current; }
    xx_pd_set_current(pd, level, current); okay = !xx_pd_is_stopped(pd) && current == m->output_size;
finish:
    xx_pd_leave_level(pd, level);
    if (saved >= 0 && xx_io_seek64(self->device, saved, SEEK_SET) != 0) okay = false;
    return okay && !xx_pd_is_stopped(pd);
}
