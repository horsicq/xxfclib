/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_RTPATCH_NAMED_210_PRIVATE_H
#define XX_RTPATCH_NAMED_210_PRIVATE_H

/* This 2.10 layout uses counted names and a 33-byte metadata tail, rather
 * than a fixed-name descriptor. Publish only fully framed whole-file data. */
static bool xx_rtpatch_named_210(const uint8_t *data, int64_t size, int64_t base, xx_rtpatch_stream *stream, xx_pd_struct *pd)
{
    int64_t start, decoded;
    bool directory;
    size_t candidates = 0U;
    if (size < 30 || xx_data_get_u16(data + size - 2, 2, 0, false) != 0x1000U || !xx_rtpatch_string_list(data, size, 26, &start, &decoded, &directory) || !directory)
        return false;
    for (int64_t at = start; at <= size - XX_RTPATCH_STREAM_MIN_SIZE; ++at) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_rtpatch_stream_at(data, size, at)) continue;
        if (++candidates > XX_RTPATCH_MAX_MEMBERS) return false;
        int64_t tail = at - 33;
        if (tail < start + 12) continue;
        uint32_t plain = xx_data_get_u32(data + tail, 4, 0, false);
        uint16_t attrs = xx_data_get_u16(data + tail + 4, 2, 0, false);
        uint16_t date = xx_data_get_u16(data + tail + 16, 2, 0, false);
        uint16_t time = xx_data_get_u16(data + tail + 18, 2, 0, false);
        if (plain == 0U || plain > XX_RTPATCH_MAX_DECODED || (attrs & ~0x27U) != 0U || !xx_rtpatch_dos_valid(date, time)) continue;
        bool reserved = true;
        for (int64_t i = 6; i < 16; ++i)
            if (data[tail + i] != 0U) reserved = false;
        if (!reserved) continue;
        int64_t lowest = tail - XX_RTPATCH_NAME_WINDOW;
        if (lowest < start + 10) lowest = start + 10;
        for (int64_t name_at = tail - 2; name_at >= lowest; --name_at) {
            uint8_t length = data[name_at];
            char name[XX_RTPATCH_MAX_NAME], unique[XX_RTPATCH_MAX_NAME + 16];
            if (length < 2U || name_at + 1 + length != tail || data[tail - 1] != 0U || !xx_rtpatch_safe_name(data + name_at + 1, length - 1U, false, name) ||
                !xx_rtpatch_path_safe(name))
                continue;
            bool safe = true;
            for (const char *c = name; *c; ++c)
                if (*c == ':' || *c == '*' || *c == '?' || *c == '"' || *c == '<' || *c == '>' || *c == '|') safe = false;
            if (!safe) continue;
            int64_t header = name_at - 10;
            uint32_t packed = xx_data_get_u32(data + header + 6, 4, 0, false);
            if (data[header] != 0xc2U || data[header + 1] != 1U || xx_data_get_u32(data + header + 2, 4, 0, false) != plain || packed < XX_RTPATCH_STREAM_MIN_SIZE ||
                packed > (uint64_t)(size - at - 2))
                continue;
            if (stream->count >= XX_RTPATCH_MAX_MEMBERS || !xx_rtpatch_unique(stream, name, unique, sizeof(unique))) return false;
            xx_rtpatch_member member;
            xx_mem_zero(&member, sizeof(member));
            member.name = xx_str_dup(unique);
            if (!member.name) return false;
            member.header_offset = base + header;
            member.header_size = at - header;
            member.data_offset = base + at;
            member.compressed_size = packed;
            member.uncompressed_size = plain;
            member.timestamp = ((uint64_t)date << 16) | time;
            member.method = XX_RTPATCH_METHOD_STREAM;
            if (!xx_rtpatch_add(stream, &member)) {
                xx_str_free(member.name);
                return false;
            }
            at += packed - 1;
            break;
        }
    }
    return stream->count != 0U;
}
#endif
