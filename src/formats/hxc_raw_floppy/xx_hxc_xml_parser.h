/* SPDX-License-Identifier: MIT. Original bounded HxC descriptor interpreter.
 * Timing/encoding fields remain in the retained descriptor. The exported
 * image is logical sector data, not synthesized magnetic tracks. */
#ifndef XX_HXC_XML_PARSER_H
#define XX_HXC_XML_PARSER_H
typedef struct hx_sector {
    uint32_t id, size;
    uint64_t offset;
    uint8_t fill;
    bool has_offset, has_size, has_fill;
    uint8_t *data;
    size_t data_size;
} hx_sector;
typedef struct hx_track {
    uint32_t spt, size, start;
    uint64_t offset;
    uint8_t fill;
    bool present, has_spt, has_size, has_start, has_offset;
    hx_sector *sectors;
    size_t count, capacity;
} hx_track;
static bool hx_number(const char *text, uint64_t maximum, uint64_t *result)
{
    uint64_t n = 0;
    unsigned base = 10;
    bool digit = false;
    if (!text) return false;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    for (; *text; ++text) {
        unsigned d;
        if (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') break;
        if (*text >= '0' && *text <= '9') d = (unsigned)(*text - '0');
        else if (*text >= 'a' && *text <= 'f') d = (unsigned)(*text - 'a' + 10);
        else if (*text >= 'A' && *text <= 'F') d = (unsigned)(*text - 'A' + 10);
        else return false;
        if (d >= base || d > maximum || n > (maximum - d) / base) return false;
        n = n * base + d;
        digit = true;
    }
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    if (!digit || *text) {
        return false;
    }
    *result = n;
    return true;
}
static bool hx_attribute_number(xx_xml *xml, const char *key, uint64_t max, uint64_t *n, bool required)
{
    const char *value = xx_xml_attribute_value(xml, key);
    if (!value) {
        return !required;
    }
    return hx_number(value, max, n);
}
static bool hx_hex(hx_sector *s, const char *text)
{
    uint8_t *data;
    size_t count = 0, i;
    int high = -1;
    if (s->data) return false;
    for (i = 0; text[i]; ++i)
        if (text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n') ++count;
    if (!count || (count & 1U) || count > 65536U) return false;
    data = (uint8_t *)xx_mem_alloc(count / 2U);
    if (!data) return false;
    count = 0;
    for (i = 0; text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        int d;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else {
            xx_mem_free(data);
            return false;
        }
        if (high < 0) high = d;
        else {
            data[count++] = (uint8_t)(high * 16 + d);
            high = -1;
        }
    }
    s->data = data;
    s->data_size = count;
    return true;
}
static int hx_sector_order(const void *a, const void *b)
{
    const hx_sector *x = (const hx_sector *)a, *y = (const hx_sector *)b;
    return x->id < y->id ? -1 : x->id > y->id ? 1 : 0;
}
static bool hx_run_add(xx_hxc_raw_extent **runs, size_t *count, size_t *capacity, uint64_t at, uint32_t size, uint8_t fill, unsigned mode, const uint8_t *data)
{
    xx_hxc_raw_extent *e;
    if (!size || *count >= HX_MAX_RUNS) return false;
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2U : 128U;
        void *p;
        if (next > HX_MAX_RUNS) next = HX_MAX_RUNS;
        p = xx_mem_realloc(*runs, next * sizeof(**runs));
        if (!p) return false;
        *runs = (xx_hxc_raw_extent *)p;
        *capacity = next;
    }
    e = &(*runs)[(*count)++];
    xx_mem_zero(e, sizeof(*e));
    e->offset = at;
    e->size = size;
    e->count = 1;
    e->stride = size;
    e->fill = fill;
    e->mode = (uint8_t)mode;
    e->data = data;
    return true;
}
static hx_plan *hx_xml_plan(const void *input, size_t length, bool source, Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    xx_xml xml;
    hx_track *tracks = NULL, *track = NULL;
    hx_sector *sector = NULL;
    xx_hxc_raw_extent *runs = NULL;
    size_t count = 0, capacity = 0, total_sectors = 0, i, j, used = 0;
    uint32_t cylinders = 0, heads = 0, spt = 0, sector_size = 0, start = 1;
    uint8_t fill = 0;
    uint32_t ignored_tracks = 0;
    uint64_t file_size = 0, cursor = 0, max_source = 0, n;
    char name[128] = "HxC XML layout";
    char stack[16][48];
    char *text = NULL;
    int depth = 0;
    bool root = false, closed = false, layout = false, ok = true;
    hx_plan *result = NULL;
    size_t work = length * 6U + 512U * sizeof(hx_track) + 131073U;
    if (!input || !length || length > HX_MAX_XML || (pd && xx_pd_is_stopped(pd)) || (f && !hx_budget(f, opts, work))) return NULL;
    tracks = (hx_track *)xx_mem_alloc(512U * sizeof(*tracks));
    text = (char *)xx_mem_alloc(131073U);
    if (!tracks || !text) {
        xx_mem_free(tracks);
        xx_mem_free(text);
        return NULL;
    }
    xx_mem_zero(tracks, 512U * sizeof(*tracks));
    xx_mem_zero(stack, sizeof(stack));
    text[0] = 0;
    xx_xml_init(&xml, input, length);
    while (ok && xx_xml_next(&xml)) {
        xx_xml_type_t type = xx_xml_type(&xml);
        const char *tag = xx_xml_name(&xml);
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        if (type == XX_XML_DOCTYPE) {
            ok = false;
            break;
        }
        if (type == XX_XML_START) {
            const char *parent = depth ? stack[depth - 1] : "";
            if (depth >= 15 || !tag || strlen(tag) >= sizeof(stack[0]) || closed) {
                ok = false;
                break;
            }
            if (depth && strcmp(parent, "disk_layout") && strcmp(parent, "layout") && strcmp(parent, "track_list") && strcmp(parent, "track") &&
                strcmp(parent, "sector_list") && strcmp(parent, "sector")) {
                ok = false;
                break;
            }
            if (!depth) {
                if (root || strcmp(tag, "disk_layout")) {
                    ok = false;
                    break;
                }
                root = true;
            } else if (!strcmp(tag, "layout")) {
                if (strcmp(parent, "disk_layout") || layout) {
                    ok = false;
                    break;
                }
                layout = true;
            } else if (!strcmp(tag, "track_list")) {
                if (strcmp(parent, "layout")) {
                    ok = false;
                    break;
                }
            } else if (!strcmp(tag, "sector_list")) {
                if (strcmp(parent, "track") || !track) {
                    ok = false;
                    break;
                }
            } else if (!strcmp(tag, "track")) {
                uint64_t c = 0, h = 0;
                if (strcmp(parent, "track_list") || track || !hx_attribute_number(&xml, "track_number", 255, &c, true) ||
                    !hx_attribute_number(&xml, "side_number", 1, &h, true)) {
                    ok = false;
                    break;
                }
                track = &tracks[(size_t)c * 2U + (size_t)h];
                if (track->present) {
                    ok = false;
                    break;
                }
                track->present = true;
            } else if (!strcmp(tag, "sector")) {
                hx_sector *p;
                uint64_t id = UINT64_MAX, sz = 0;
                if (strcmp(parent, "sector_list") || !track || sector || track->count >= 128U || total_sectors >= HX_MAX_RUNS ||
                    !hx_attribute_number(&xml, "sector_id", 255, &id, false) || !hx_attribute_number(&xml, "sector_size", 32768, &sz, false)) {
                    ok = false;
                    break;
                }
                if (xx_xml_attribute_value(&xml, "sector_size") && !sz) {
                    ok = false;
                    break;
                }
                if (track->count == track->capacity) {
                    size_t cap = track->capacity ? track->capacity * 2U : 8U;
                    p = (hx_sector *)xx_mem_realloc(track->sectors, cap * sizeof(*p));
                    if (!p) {
                        ok = false;
                        break;
                    }
                    track->sectors = p;
                    track->capacity = cap;
                }
                sector = &track->sectors[track->count++];
                ++total_sectors;
                xx_mem_zero(sector, sizeof(*sector));
                sector->id = id == UINT64_MAX ? UINT32_MAX : (uint32_t)id;
                sector->size = (uint32_t)sz;
                sector->has_size = sz != 0;
            }
            strcpy(stack[depth++], tag);
            used = 0;
            text[0] = 0;
            if (!xml.self_closing) continue;
            /* Self-closing leaves are processed like empty end elements. */
            type = XX_XML_END;
        }
        if (type == XX_XML_TEXT) {
            const char *value = xx_xml_text(&xml);
            size_t size = value ? strlen(value) : 0;
            if (!depth) {
                for (i = 0; i < size; ++i)
                    if (value[i] != ' ' && value[i] != '\t' && value[i] != '\r' && value[i] != '\n') ok = false;
            } else {
                if (size > 131072U - used) {
                    ok = false;
                    break;
                }
                memcpy(text + used, value, size);
                used += size;
                text[used] = 0;
            }
        } else if (type == XX_XML_END) {
            const char *parent;
            if (!depth || !tag || strcmp(stack[depth - 1], tag)) {
                ok = false;
                break;
            }
            parent = depth >= 2 ? stack[depth - 2] : "";
            if (!strcmp(tag, "disk_layout_name") && !strcmp(parent, "disk_layout")) {
                if (used >= sizeof(name)) {
                    ok = false;
                    break;
                }
                memcpy(name, text, used + 1U);
            } else if (!strcmp(tag, "file_size") && !strcmp(parent, "disk_layout")) {
                if (!hx_number(text, HX_MAX_IMAGE, &file_size)) {
                    ok = false;
                    break;
                }
            } else if (!strcmp(parent, "layout") || !strcmp(parent, "track") || !strcmp(parent, "sector")) {
                bool numeric = !strcmp(tag, "number_of_track") || !strcmp(tag, "number_of_side") || !strcmp(tag, "sector_per_track") || !strcmp(tag, "sector_size") ||
                               !strcmp(tag, "start_sector_id") || !strcmp(tag, "formatvalue") || !strcmp(tag, "data_fill") || !strcmp(tag, "data_offset") ||
                               !strcmp(tag, "sector_id");
                if (numeric) {
                    if (!hx_number(text, HX_MAX_IMAGE, &n)) {
                        ok = false;
                        break;
                    }
                    if (!strcmp(parent, "layout")) {
                        if (!strcmp(tag, "number_of_track")) {
                            if (cylinders || !n || n > 256) ok = false;
                            cylinders = (uint32_t)n;
                        } else if (!strcmp(tag, "number_of_side")) {
                            if (heads || !n || n > 2) ok = false;
                            heads = (uint32_t)n;
                        } else if (!strcmp(tag, "sector_per_track")) {
                            if (spt || !n || n > 128) ok = false;
                            spt = (uint32_t)n;
                        } else if (!strcmp(tag, "sector_size")) {
                            if (sector_size || !n || n > 32768) ok = false;
                            sector_size = (uint32_t)n;
                        } else if (!strcmp(tag, "start_sector_id")) {
                            if (n > 255) ok = false;
                            start = (uint32_t)n;
                        } else if (!strcmp(tag, "formatvalue")) {
                            if (n > 255) ok = false;
                            fill = (uint8_t)n;
                        } else ok = false;
                    } else if (!strcmp(parent, "track") && track) {
                        if (!strcmp(tag, "sector_per_track")) {
                            if (track->has_spt || !n || n > 128) ok = false;
                            track->spt = (uint32_t)n;
                            track->has_spt = true;
                        } else if (!strcmp(tag, "sector_size")) {
                            if (track->has_size || !n || n > 32768) ok = false;
                            track->size = (uint32_t)n;
                            track->has_size = true;
                        } else if (!strcmp(tag, "start_sector_id")) {
                            if (track->has_start || n > 255) ok = false;
                            track->start = (uint32_t)n;
                            track->has_start = true;
                        } else if (!strcmp(tag, "data_offset")) {
                            if (track->has_offset) ok = false;
                            track->offset = n;
                            track->has_offset = true;
                        } else ok = false;
                    } else if (!strcmp(parent, "sector") && sector) {
                        if (!strcmp(tag, "sector_size")) {
                            if (sector->has_size || !n || n > 32768) ok = false;
                            sector->size = (uint32_t)n;
                            sector->has_size = true;
                        } else if (!strcmp(tag, "sector_id")) {
                            if (sector->id != UINT32_MAX || n > 255) ok = false;
                            sector->id = (uint32_t)n;
                        } else if (!strcmp(tag, "data_offset")) {
                            if (sector->has_offset) ok = false;
                            sector->offset = n;
                            sector->has_offset = true;
                        } else if (!strcmp(tag, "data_fill")) {
                            if (sector->has_fill || n > 255) ok = false;
                            sector->fill = (uint8_t)n;
                            sector->has_fill = true;
                        } else ok = false;
                    } else ok = false;
                } else if (!strcmp(tag, "sector_data")) {
                    if (!sector || !hx_hex(sector, text)) ok = false;
                }
            }
            if (!strcmp(tag, "sector")) sector = NULL;
            else if (!strcmp(tag, "track")) track = NULL;
            else if (!strcmp(tag, "disk_layout")) closed = true;
            --depth;
            used = 0;
            text[0] = 0;
        }
    }
    if (xx_xml_failed(&xml) || depth || !root || !closed || !layout || !cylinders || !heads) ok = false;
    xx_xml_cleanup(&xml);
    /* HxC's builder discards bounded track declarations outside the declared
     * disk geometry (one published FLEX preset contains such a trailer).
     * Preserve the complete descriptor and explicitly report this omission. */
    for (i = 0; ok && i < 512U; ++i)
        if (tracks[i].present && (i / 2U >= cylinders || i % 2U >= heads)) {
            hx_track *t = &tracks[i];
            uint64_t consumed = 0, base = t->has_offset ? t->offset : 0;
            uint32_t size = t->has_size ? t->size : sector_size;
            for (j = 0; ok && j < t->count; ++j) {
                hx_sector *s = &t->sectors[j];
                uint32_t bytes = s->has_size ? s->size : size;
                size_t k;
                uint64_t offset = s->has_offset ? s->offset : base + consumed;
                if (s->id == UINT32_MAX || !bytes || bytes > 32768U || s->data_size > bytes || offset > HX_MAX_IMAGE || bytes > HX_MAX_IMAGE - offset) {
                    ok = false;
                    break;
                }
                for (k = 0; k < j; ++k)
                    if (t->sectors[k].id == s->id) {
                        ok = false;
                        break;
                    }
                consumed += bytes;
            }
            ++ignored_tracks;
        }
    for (i = 0; ok && i < cylinders * heads; ++i) {
        uint32_t c = (uint32_t)(i / heads), h = (uint32_t)(i % heads);
        hx_track *t = &tracks[c * 2U + h];
        uint32_t tspt = t->has_spt ? t->spt : spt, tsize = t->has_size ? t->size : sector_size;
        uint32_t first = t->has_start ? t->start : start;
        size_t sectors = t->count ? t->count : (t->present && !t->has_spt ? 0U : tspt);
        uint64_t track_base = t->has_offset ? t->offset : (t->present ? 0U : cursor), track_bytes = 0;
        if (!sectors) {
            if (t->present) continue;
            ok = false;
            break;
        }
        if (sectors > 128U || (!t->count && (!tsize || first + sectors > 256U))) {
            ok = false;
            break;
        }
        /* Offsets advance in descriptor order. Sorting IDs only happens after
         * each sector has acquired its source offset. */
        for (j = 0; ok && j < sectors; ++j) {
            hx_sector temporary, *s = t->count ? &t->sectors[j] : &temporary;
            if (!t->count) {
                xx_mem_zero(s, sizeof(*s));
                s->id = first + (uint32_t)j;
            }
            if (s->id == UINT32_MAX) {
                ok = false;
                break;
            }
            if (!s->has_size) s->size = tsize;
            if (!s->size || s->size > 32768U || s->data_size > s->size) {
                ok = false;
                break;
            }
            if (!s->has_fill) s->fill = fill;
            if (!s->has_offset) s->offset = track_base + track_bytes;
            if (s->offset > HX_MAX_IMAGE || s->size > HX_MAX_IMAGE - s->offset) {
                ok = false;
                break;
            }
            if (s->offset + s->size > max_source) max_source = s->offset + s->size;
            track_bytes += s->size;
            if (!t->count) {
                uint64_t available = source && (!file_size || s->offset < file_size) ? s->size : 0;
                if (file_size && available > file_size - s->offset) available = file_size - s->offset;
                if (available && !hx_run_add(&runs, &count, &capacity, s->offset, (uint32_t)available, s->fill, XX_HXC_RAW_SOURCE, NULL)) ok = false;
                if (s->size > available && !hx_run_add(&runs, &count, &capacity, 0, s->size - (uint32_t)available, s->fill, XX_HXC_RAW_FILL, NULL)) ok = false;
            }
        }
        cursor = track_base + track_bytes;
        if (!ok) break;
        if (t->count) {
            xx_rt_qsort(t->sectors, t->count, sizeof(*t->sectors), hx_sector_order);
            for (j = 0; ok && j < t->count; ++j) {
                hx_sector *s = &t->sectors[j];
                uint64_t available = source && (!file_size || s->offset < file_size) ? s->size : 0;
                if (j && s->id == t->sectors[j - 1U].id) {
                    ok = false;
                    break;
                }
                if (file_size && available > file_size - s->offset) available = file_size - s->offset;
                if (available) {
                    if (!hx_run_add(&runs, &count, &capacity, s->offset, (uint32_t)available, s->fill, XX_HXC_RAW_SOURCE, NULL)) ok = false;
                } else if (s->data) {
                    if (!hx_run_add(&runs, &count, &capacity, 0, (uint32_t)s->data_size, 0, XX_HXC_RAW_INLINE, s->data)) ok = false;
                    available = s->data_size;
                }
                if (s->size > available &&
                    !hx_run_add(&runs, &count, &capacity, 0, s->size - (uint32_t)available, s->data && !source ? 0 : s->fill, XX_HXC_RAW_FILL, NULL))
                    ok = false;
            }
        }
        if (pd && xx_pd_is_stopped(pd)) ok = false;
        if (f && !hx_budget(f, opts, work + capacity * sizeof(*runs) + total_sectors * sizeof(hx_sector) * 2U)) ok = false;
    }
    if (ok) {
        xx_hxc_raw_profile p;
        uint64_t logical;
        size_t bytes;
        p.name = name;
        p.tracks = (uint16_t)cylinders;
        p.sides = (uint16_t)heads;
        p.source_size = file_size ? file_size : max_source;
        p.extents = runs;
        p.extent_count = count;
        if (hx_profile_valid(&p, &logical, &bytes) && (!f || hx_budget(f, opts, work + capacity * sizeof(*runs) + total_sectors * sizeof(hx_sector) * 2U + bytes))) {
            result = hx_clone(&p, NULL, NULL);
            if (result) result->ignored_tracks = ignored_tracks;
        }
    }
    for (i = 0; i < 512U; ++i) {
        for (j = 0; j < tracks[i].count; ++j) xx_mem_free(tracks[i].sectors[j].data);
        xx_mem_free(tracks[i].sectors);
    }
    xx_mem_free(tracks);
    xx_mem_free(runs);
    xx_mem_free(text);
    return result;
}
#endif
