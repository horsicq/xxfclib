/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_RTPATCH_EXTENDED_STORED_PRIVATE_H
#define XX_RTPATCH_EXTENDED_STORED_PRIVATE_H

static bool xx_rtp_extended_descriptor(const uint8_t *data, int64_t size, int64_t *at, char *name, int64_t *plain, uint64_t *timestamp)
{
    char short_name[XX_RTPATCH_MAX_NAME];
    if (!xx_rtpatch_descriptor(data, size, *at, short_name, plain, timestamp) || *at > size - 43) return false;
    *at += 42;
    uint8_t length = data[(*at)++];
    if (length < 2U || *at > size - length || data[*at + length - 1] != 0U || !xx_rtpatch_safe_name(data + *at, length - 1U, false, name) || !xx_rtpatch_path_safe(name))
        return false;
    for (const char *c = name; *c; ++c)
        if (*c == ':' || *c == '*' || *c == '?' || *c == '"' || *c == '<' || *c == '>' || *c == '|') return false;
    *at += length;
    return true;
}

/* Banner-bearing state-command packages have independently framed whole files.
 * Require their leading external descriptor and state-command envelope, then
 * publish only stored records corroborated by names and three equal sizes. */
static bool xx_rtp_extended_banner_stored(const uint8_t *data, int64_t size, int64_t start, int64_t decoded, int64_t base, xx_rtpatch_stream *stream, xx_pd_struct *pd)
{
    char name[XX_RTPATCH_MAX_NAME], unique[XX_RTPATCH_MAX_NAME + 16];
    int64_t plain, at = start;
    uint64_t timestamp;
    if (decoded <= 0 || at > size - 15 || xx_data_get_u16(data + at, 2, 0, false) != 0x3441U || data[at + 12] != 1U) return false;
    for (int64_t i = 2; i < 12; ++i)
        if (data[at + i] != 0U) return false;
    at += 13;
    if (!xx_rtp_extended_descriptor(data, size, &at, name, &plain, &timestamp) || at > size - 4 || xx_data_get_u16(data + at, 2, 0, false) != 0x5006U ||
        xx_data_get_u16(data + size - 2, 2, 0, false) != 0x1000U)
        return false;
    for (; at <= size - 47; ++at) {
        int64_t record = at, descriptor, payload;
        uint16_t flags = xx_data_get_u16(data + at, 2, 0, false);
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (flags == 0x2441U) {
            if (at > size - 57 || data[at + 12] != 1U) continue;
            bool reserved = true;
            for (int64_t i = 2; i < 12; ++i)
                if (data[at + i] != 0U) reserved = false;
            if (!reserved) continue;
            descriptor = at + 21;
        } else if ((flags == 0x0262U || flags == 0x02e2U) && data[at + 2] == 1U) descriptor = at + 11;
        else continue;
        payload = descriptor;
        if (!xx_rtp_extended_descriptor(data, size, &payload, name, &plain, &timestamp) || plain > XX_RTPATCH_MAX_DECODED ||
            xx_data_get_u32(data + descriptor - 8, 4, 0, false) != (uint64_t)plain || xx_data_get_u32(data + descriptor - 4, 4, 0, false) != (uint64_t)plain ||
            payload > size - 2 || plain > size - payload - 2)
            continue;
        if (stream->count >= XX_RTPATCH_MAX_MEMBERS || !xx_rtpatch_unique(stream, name, unique, sizeof(unique))) return false;
        xx_rtpatch_member member;
        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(unique);
        if (!member.name) return false;
        member.header_offset = base + record;
        member.header_size = payload - record;
        member.data_offset = base + payload;
        member.compressed_size = member.uncompressed_size = plain;
        member.timestamp = timestamp;
        member.method = XX_RTPATCH_METHOD_STORED;
        if (!xx_rtpatch_add(stream, &member)) {
            xx_str_free(member.name);
            return false;
        }
        at = payload + plain - 1;
    }
    if (stream->count == 0U || !xx_rtpatch_unique(stream, "Comments.txt", unique, sizeof(unique))) return false;
    xx_rtpatch_member banner;
    xx_mem_zero(&banner, sizeof(banner));
    banner.name = xx_str_dup(unique);
    if (!banner.name) return false;
    banner.header_offset = banner.data_offset = base + 30;
    banner.header_size = 2;
    banner.compressed_size = start - 30;
    banner.uncompressed_size = decoded;
    banner.method = XX_RTPATCH_METHOD_TEXT;
    if (!xx_rtpatch_add(stream, &banner)) {
        xx_str_free(banner.name);
        return false;
    }
    return true;
}

static bool xx_rtp_descriptor_mode(const uint8_t *data, int64_t size, int64_t *at, bool extra, char *name, int64_t *plain, uint64_t *timestamp)
{
    if (extra) return xx_rtp_extended_descriptor(data, size, at, name, plain, timestamp);
    if (!xx_rtpatch_descriptor(data, size, *at, name, plain, timestamp) || !xx_rtpatch_path_safe(name)) return false;
    for (const char *c = name; *c; ++c)
        if (*c == ':' || *c == '*' || *c == '?' || *c == '"' || *c == '<' || *c == '>' || *c == '|') return false;
    *at += 34;
    return true;
}

/* Extended 3.00 record chains: authenticate every record boundary before returning
 * independently stored members. Delta programs are bounded but never applied. */
static bool xx_rtpatch_extended_records(const uint8_t *data, int64_t size, int64_t base, xx_rtpatch_stream *stream, xx_pd_struct *pd)
{
    int64_t at, decoded;
    bool directory;
    bool extra = xx_data_get_u32(data + 6, 4, 0, false) == 0x00010000U;
    size_t count = 0U;
    for (int64_t i = 26; i < 30; ++i)
        if (data[i] != 0U) return false;
    at = 30;
    if ((data[5] & 3U) != 0U) {
        if (!xx_rtpatch_string_list(data, size, 30, &at, &decoded, &directory)) return false;
        if (!directory) return extra && xx_rtp_extended_banner_stored(data, size, at, decoded, base, stream, pd);
    }
    while (at < size - 2) {
        int64_t start = at, plain, source_size;
        uint64_t timestamp;
        char name[XX_RTPATCH_MAX_NAME], source[XX_RTPATCH_MAX_NAME];
        uint32_t program_size, packed;
        if (++count > XX_RTPATCH_MAX_MEMBERS || at > size - 4 || (pd && xx_pd_is_stopped(pd))) return false;
        uint16_t flags = xx_data_get_u16(data + at, 2, 0, false);
        at += 2;
        if (flags != 0x2441U && flags != 0x2443U && flags != 0x4441U && flags != 0x3441U && flags != 0x3440U && flags != 0x2440U && flags != 0x4440U &&
            flags != 0x2442U && flags != 0x4442U)
            return false;
        if (flags == 0x2442U || flags == 0x4442U) {
            if (at > size - 2 || xx_data_get_u16(data + at, 2, 0, false) != 0x0004U) return false;
            at += 2;
        }
        if (flags == 0x2443U) {
            if (at > size - 2 || xx_data_get_u16(data + at, 2, 0, false) != 0x01a0U) return false;
            at += 2;
            xx_rtp_raw_cursor c = {data, (size_t)size, (size_t)at};
            int64_t seek, checksum;
            checksum = 0;
            if (!xx_rtp_raw_vli(&c, &seek) || (extra && !xx_rtp_raw_vli(&c, &checksum)) || seek < 0 || seek > UINT32_MAX || checksum < 0 || checksum > UINT32_MAX)
                return false;
            at = (int64_t)c.at;
        }
        if (at > size - 12) return false;
        for (int64_t i = 0; i < 10; ++i)
            if (data[at + i] != 0U) return false;
        at += 10;
        if (flags == 0x3441U || flags == 0x3440U) {
            if (at >= size || data[at++] != 1U || !xx_rtp_descriptor_mode(data, size, &at, extra, name, &plain, &timestamp)) return false;
            continue;
        }
        if (flags == 0x4441U || flags == 0x4440U || flags == 0x4442U) {
            if (at > size - 12) return false;
            uint16_t modification = xx_data_get_u16(data + at, 2, 0, false);
            if ((modification != 8U && modification != 9U && modification != 13U && !(modification == 1U && (flags & 1U) == 0U)) || data[at + 2] != 1U ||
                data[at + 3] != 1U)
                return false;
            program_size = xx_data_get_u32(data + at + 4, 4, 0, false);
            packed = xx_data_get_u32(data + at + 8, 4, 0, false);
            at += 12;
            int64_t source_descriptor = at;
            if (!xx_rtp_descriptor_mode(data, size, &at, extra, source, &source_size, &timestamp)) return false;
            int64_t destination_descriptor = at;
            if (!xx_rtp_descriptor_mode(data, size, &at, extra, name, &plain, &timestamp)) return false;
            if (program_size == 0U) {
                if (modification != 8U || packed != 0U || source_size != plain || xx_rt_strcmp(source, name) != 0 ||
                    xx_rt_memcmp(data + source_descriptor + 24, data + destination_descriptor + 24, 10U) != 0)
                    return false;
                continue;
            }
            if (modification == 8U || packed == 0U || at > size - 2 || packed > (uint64_t)(size - at - 2) ||
                ((flags & 1U) != 0U ? packed != program_size : (packed < XX_RTPATCH_STREAM_MIN_SIZE || !xx_rtpatch_stream_at(data, size, at))))
                return false;
            at += packed;
        } else {
            if (at > size - 9 || data[at] != 1U) return false;
            program_size = xx_data_get_u32(data + at + 1, 4, 0, false);
            packed = xx_data_get_u32(data + at + 5, 4, 0, false);
            at += 9;
            if (!xx_rtp_descriptor_mode(data, size, &at, extra, name, &plain, &timestamp) || plain > XX_RTPATCH_MAX_DECODED || program_size != (uint64_t)plain ||
                packed == 0U || at > size - 2 || packed > (uint64_t)(size - at - 2) ||
                ((flags & 1U) != 0U ? packed != program_size : (packed < XX_RTPATCH_STREAM_MIN_SIZE || !xx_rtpatch_stream_at(data, size, at))))
                return false;
            char unique[XX_RTPATCH_MAX_NAME + 16];
            if (!xx_rtpatch_unique(stream, name, unique, sizeof(unique))) return false;
            xx_rtpatch_member member;
            xx_mem_zero(&member, sizeof(member));
            member.name = xx_str_dup(unique);
            if (!member.name) return false;
            member.header_offset = base + start;
            member.header_size = at - start;
            member.data_offset = base + at;
            member.compressed_size = packed;
            member.uncompressed_size = plain;
            member.timestamp = timestamp;
            member.method = (flags & 1U) != 0U ? XX_RTPATCH_METHOD_STORED : XX_RTPATCH_METHOD_STREAM;
            if (!xx_rtpatch_add(stream, &member)) {
                xx_str_free(member.name);
                return false;
            }
            at += packed;
        }
    }
    return count != 0U && at == size - 2 && xx_data_get_u16(data + at, 2, 0, false) == 0x1000U;
}
#endif
