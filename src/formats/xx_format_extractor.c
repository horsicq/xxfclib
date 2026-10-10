/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_format_extractor.c - the shared raw-data search, and the lookup from a
 * file type to its format's search. See xx_format_extractor_engine.h for how
 * a search decides what counts as a find. */

#include "xx_format_extractor_engine.h"
#include "xx_format_fast_detect_rules.h"

#include "xxfclib/data/xx_pd.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

struct xx_format_search_state {
    const xx_format_search_desc *desc;
    xx_io_device *device;
    int64_t total;

    uint8_t *buffer;
    uint8_t *probe;       /* separate bounded window for crossing anchors */
    size_t buffer_cap;
    int64_t buffer_start; /* device offset of buffer[0] */
    size_t buffer_len;    /* valid bytes in buffer */
    size_t lookahead;     /* deepest anchor end, from a candidate start */

    int64_t next;         /* first candidate start not yet tested */
    bool done;
    bool has_current;
    xx_format_search_info current;
};

static bool xx_format_search_type_matches(const xx_format_search_desc *desc,
                                          xx_file_type_t type) {
    size_t i;
    for (i = 0; i < desc->type_count; ++i) {
        if (desc->types[i] == type) return true;
    }
    return false;
}

/* Inspect one candidate on a view whose offset zero is the format start.
 * The caller decides whether it needs the measured extent. This helper does
 * not preserve the parent device cursor; direct callers restore it below. */
static bool xx_format_search_probe(const xx_format_search_desc *desc,
                                   xx_io_device *device, int64_t start,
                                   int64_t total, bool is_mapped,
                                   bool measure_size, xx_pd_struct *pd,
                                   xx_file_type_t *type_out,
                                   int64_t *size_out) {
    xx_io_volume volume;
    xx_io_device *window;
    Abstractformat *format;
    xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
    int64_t size = -1;

    if (!desc || !desc->types || !desc->type_count || !desc->open ||
        !desc->close || !device || start < 0 || start >= total)
        return false;
    volume.device = device;
    volume.offset = start;
    volume.size = total - start;
    window = xx_io_multivolume_open(&volume, 1U, false);
    if (!window) return false;

    format = desc->open(window);
    if (!format) {
        xx_io_close(window);
        return false;
    }
    xx_format_set_mapped(format, is_mapped);
    if (!xx_format_is_valid(format, pd)) {
        desc->close(format);
        xx_io_close(window);
        return false;
    }
    if (measure_size && xx_format_handle_base_info(format, pd)) {
        int64_t measured = xx_format_get_format_size(format, pd);
        /* A size larger than the remaining view is not a usable extent. */
        if (measured > 0 && measured <= volume.size) size = measured;
    } else if (desc->reader_classifies && !measure_size) {
        /* A classifying reader may assign its final type during base info. */
        (void)xx_format_handle_base_info(format, pd);
    }
    if (desc->reader_classifies) type = format->file_type;
    desc->close(format);
    if (!desc->reader_classifies)
        type = xx_format_get_file_type_device(window);
    xx_io_close(window);
    if (!xx_format_search_type_matches(desc, type)) return false;

    if (type_out) *type_out = type;
    if (size_out) *size_out = size;
    return true;
}

/* The borrowed view and the detector may seek the parent. A direct callback
 * succeeds only when its caller's cursor can be restored. */
static bool xx_format_search_probe_direct(const xx_format_search_desc *desc,
                                          xx_io_device *device,
                                          int64_t base_address,
                                          bool is_mapped,
                                          bool measure_size,
                                          int64_t *size_out) {
    int64_t saved_position;
    int64_t total;
    int64_t size = -1;
    bool matched = false;

    if (size_out) *size_out = -1;
    if (!device) return false;
    saved_position = xx_io_tell(device);
    if (saved_position < 0) return false;
    total = xx_io_total_size(device);
    if (base_address >= 0 && base_address < total)
        matched = xx_format_search_probe(desc, device, base_address, total,
                                         is_mapped, measure_size, NULL, NULL,
                                         &size);
    if (xx_io_seek64(device, saved_position, SEEK_SET) != 0) return false;
    if (matched && size_out) *size_out = size;
    return matched;
}

xx_file_type_t xx_format_search_file_type(const xx_format_search_desc *desc,
                                         xx_io_device *device,
                                         int64_t base_address, bool is_mapped) {
    xx_io_volume volume;
    xx_io_device *window;
    Abstractformat *format;
    int64_t saved_position;
    int64_t total;
    xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;

    if (!desc || !desc->types || !desc->type_count)
        return type;
    if (desc->type_count == 1U) return desc->types[0];
    if (!device || !desc->open || !desc->close) return type;
    saved_position = xx_io_tell(device);
    if (saved_position < 0) return type;
    total = xx_io_total_size(device);
    if (base_address < 0 || base_address >= total) goto done;

    volume.device = device;
    volume.offset = base_address;
    volume.size = total - base_address;
    window = xx_io_multivolume_open(&volume, 1U, false);
    if (!window) goto done;
    format = desc->open(window);
    if (format) {
        xx_format_set_mapped(format, is_mapped);
        /* Ask this reader for its own variant. Global detection may instead
         * name a derived container such as APK or JAR for a valid ZIP. */
        if (xx_format_is_valid(format, NULL) &&
            xx_format_handle_base_info(format, NULL) &&
            xx_format_search_type_matches(desc, format->file_type))
            type = format->file_type;
        desc->close(format);
    }
    xx_io_close(window);
done:
    if (xx_io_seek64(device, saved_position, SEEK_SET) != 0)
        return XX_FILE_TYPE_UNKNOWN;
    return type;
}

char *xx_format_search_get_version(const xx_format_search_desc *desc,
                                   xx_io_device *device,
                                   int64_t base_address, bool is_mapped) {
    xx_io_volume volume;
    xx_io_device *window;
    Abstractformat *format;
    int64_t saved_position;
    int64_t total;
    char *version = NULL;

    if (!desc || !desc->types || !desc->type_count || !desc->open ||
        !desc->close || !device || base_address < 0)
        return NULL;
    saved_position = xx_io_tell(device);
    if (saved_position < 0) return NULL;
    total = xx_io_total_size(device);
    if (base_address >= total) goto done;

    volume.device = device;
    volume.offset = base_address;
    volume.size = total - base_address;
    window = xx_io_multivolume_open(&volume, 1U, false);
    if (!window) goto done;
    format = desc->open(window);
    if (format) {
        xx_format_set_mapped(format, is_mapped);
        if (xx_format_is_valid(format, NULL) &&
            xx_format_handle_base_info(format, NULL) &&
            xx_format_search_type_matches(desc, format->file_type)) {
            const char *borrowed = xx_format_get_version(format);
            /* Reader accessors can point into their own metadata buffers. */
            version = xx_str_dup(borrowed ? borrowed : "");
        }
        desc->close(format);
    }
    xx_io_close(window);

done:
    if (xx_io_seek64(device, saved_position, SEEK_SET) != 0) {
        xx_str_free(version);
        version = NULL;
    }
    return version;
}

bool xx_format_search_fast_detect(const xx_format_search_desc *desc,
                                  xx_io_device *device, int64_t base_address,
                                  bool is_mapped) {
    int signature;
    if (!desc) return false;
    signature=xx_format_fast_detect_rules(desc->types,desc->type_count,
                                          device,base_address,is_mapped);
    if (signature>=0) return signature==1;
    /* Headerless formats and content-defined container subtypes still need
     * the existing structural classifier. Size and carving always validate. */
    return xx_format_search_probe_direct(desc, device, base_address,
                                         is_mapped, false, NULL);
}

int64_t xx_format_search_size(const xx_format_search_desc *desc,
                              xx_io_device *device, int64_t base_address,
                              bool is_mapped) {
    int64_t size;
    if (!xx_format_search_probe_direct(desc, device, base_address,
                                        is_mapped, true, &size))
        return -1;
    return size;
}

/* Test one candidate start. On a find, sets the current info and moves the
 * scan past it.
 *
 * The format's own reader looks first: it turns most chance anchor matches
 * away after reading a header, where the detector would run every late
 * structural probe it has over the rest of the device. Only a candidate the
 * reader accepts goes on to the detector. A find therefore needs both: the
 * reader to accept it (so it can be read) and the detector to name one of
 * the format's types (so it is what detection would call it). */
static bool xx_format_search_test(xx_format_search_state *state, int64_t start,
                                  xx_pd_struct *pd) {
    xx_file_type_t type;
    int64_t size = -1;

    if (!xx_format_search_probe(state->desc, state->device, start,
                                state->total, false, true, pd, &type, &size))
        return false;

    state->current.file_type = type;
    state->current.offset = start;
    state->current.size = size;
    state->has_current = true;
    state->next = start + (size > 0 ? size : 1);
    return true;
}

/* Load the buffer so that it starts at state->next. */
static bool xx_format_search_refill(xx_format_search_state *state) {
    size_t got = 0;
    size_t want = state->buffer_cap;

    if ((uint64_t)want > (uint64_t)(state->total - state->next))
        want = (size_t)(state->total - state->next);

    if (xx_io_seek64(state->device, state->next, SEEK_SET) != 0) return false;
    while (got < want) {
        ssize_t n = xx_io_read(state->device, state->buffer + got,
                               want - got);
        if (n <= 0 || (size_t)n > want - got) return false;
        got += (size_t)n;
    }
    state->buffer_start = state->next;
    state->buffer_len = got;
    return got != 0;
}

/* An anchor is a complete grammar field. It may span arbitrarily many
 * global-sized windows; the main scan window remains intact. */
static bool xx_format_search_anchor_matches(xx_format_search_state *state,
                                             int64_t start,
                                             const xx_format_search_anchor *anchor) {
    int64_t absolute,relative;
    size_t completed = 0;
    if ((uint64_t)anchor->offset > (uint64_t)(state->total - start)) return false;
    absolute = start + (int64_t)anchor->offset;
    if ((uint64_t)anchor->size > (uint64_t)(state->total - absolute)) return false;
    relative = absolute - state->buffer_start;
    if (relative >= 0 && (uint64_t)relative <= state->buffer_len &&
        anchor->size <= state->buffer_len - (size_t)relative) {
        return xx_rt_memcmp(state->buffer + (size_t)relative, anchor->bytes,
                             anchor->size) == 0;
    }
    if (relative >= 0 && (uint64_t)relative < state->buffer_len &&
        state->buffer[(size_t)relative] != anchor->bytes[0]) return false;
    if (!state->probe) {
        state->probe = (uint8_t *)xx_mem_alloc(state->buffer_cap);
        if (!state->probe) { state->done = true; return false; }
    }
    if (xx_io_seek64(state->device, absolute, SEEK_SET) != 0) {
        state->done = true; return false;
    }
    while (completed < anchor->size) {
        size_t portion = anchor->size - completed;
        size_t received = 0;
        if (portion > state->buffer_cap) portion = state->buffer_cap;
        while (received < portion) {
            ssize_t n = xx_io_read(state->device, state->probe + received,
                                    portion - received);
            if (n <= 0 || (size_t)n > portion - received) {
                state->done = true; return false;
            }
            received += (size_t)n;
        }
        if (xx_rt_memcmp(state->probe, anchor->bytes + completed, portion)) return false;
        completed += portion;
    }
    return true;
}

static bool xx_format_search_advance(xx_format_search_state *state,
                                     xx_pd_struct *pd) {
    const xx_format_search_desc *desc = state->desc;

    state->has_current = false;
    if (state->done) return false;

    if (desc->anchor_count == 0U) {
        /* No signature to scan for: the format can only be recognised where
         * the detector would recognise it, at the start. */
        state->done = true;
        if (pd && xx_pd_is_stopped(pd)) return false;
        return state->next == 0 && xx_format_search_test(state, 0, pd);
    }

    while (state->next < state->total) {
        int64_t buffer_end;
        int64_t limit;
        int64_t s;

        if (pd && xx_pd_is_stopped(pd)) break;

        buffer_end = state->buffer_start + (int64_t)state->buffer_len;
        if (state->next < state->buffer_start || state->next >= buffer_end) {
            if (!xx_format_search_refill(state)) break;
            buffer_end = state->buffer_start + (int64_t)state->buffer_len;
        }
        limit = buffer_end;
        /* A full refill always moves the limit forward. One that cannot means
         * the device stopped delivering bytes before its reported end. */
        if (limit <= state->next) break;

        for (s = state->next; s < limit; ++s) {
            size_t a;
            bool candidate = false;
            for (a = 0; a < desc->anchor_count && !candidate; ++a) {
                const xx_format_search_anchor *anchor = &desc->anchors[a];
                candidate = xx_format_search_anchor_matches(state, s, anchor);
                if (state->done) return false;
            }
            /* Offset 0 is always a candidate: it is where the detector looks,
             * so a search never finds less than detection does, even for a
             * variant of the format that has no anchor. */
            if ((candidate || s == 0) && xx_format_search_test(state, s, pd))
                return true;
        }
        state->next = limit;
    }
    state->done = true;
    return false;
}

xx_format_search_state *xx_format_search_create(const xx_format_search_desc *desc,
                                                xx_io_device *device,
                                                const xx_list_s *options,
                                                xx_pd_struct *pd) {
    xx_format_search_state *state;
    size_t i;

    (void)options; /* reserved */
    if (!desc || !device || desc->type_count == 0U || !desc->open || !desc->close)
        return NULL;

    state = (xx_format_search_state *)xx_mem_calloc(1U, sizeof(*state));
    if (!state) return NULL;
    state->desc = desc;
    state->device = device;
    state->buffer_cap = xx_get_file_buffer_size();
    if (state->buffer_cap > (SIZE_MAX >> 1U)) state->buffer_cap = SIZE_MAX >> 1U;
    state->total = xx_io_total_size(device);
    if (state->total < 0) state->total = 0;

    for (i = 0; i < desc->anchor_count; ++i) {
        size_t end = (size_t)desc->anchors[i].offset + desc->anchors[i].size;
        if (desc->anchors[i].size == 0U || end > XX_FORMAT_SEARCH_MAX_LOOKAHEAD) {
            xx_mem_free(state);
            return NULL;
        }
        if (end > state->lookahead) state->lookahead = end;
    }
    if (desc->anchor_count != 0U) {
        state->buffer = (uint8_t *)xx_mem_alloc(state->buffer_cap);
        if (!state->buffer) {
            xx_mem_free(state);
            return NULL;
        }
        /* Empty buffer positioned before the device: the first advance fills it. */
        state->buffer_start = -1;
    }

    (void)xx_format_search_advance(state, pd);
    return state;
}

const xx_format_search_info *xx_format_search_current(xx_format_search_state *state) {
    return (state && state->has_current) ? &state->current : NULL;
}

bool xx_format_search_find_next(xx_format_search_state *state, xx_pd_struct *pd) {
    if (!state) return false;
    return xx_format_search_advance(state, pd);
}

void xx_format_search_free(xx_format_search_state *state) {
    if (!state) return;
    xx_mem_free(state->buffer);
    xx_mem_free(state->probe);
    xx_mem_free(state);
}

/* ------------------------------------------------------------------------ */
/* One extractor per format, and the file types each answers to. Generated. */

#include "xx_format_extractor_list.inc"

xx_format_extractor *xx_format_extractor_get(xx_file_type_t type) {
    size_t i;
    for (i = 0; i < sizeof(g_xx_format_extractors) / sizeof(g_xx_format_extractors[0]); ++i) {
        if (g_xx_format_extractors[i].type == type) return g_xx_format_extractors[i].extractor;
    }
    return NULL;
}

size_t xx_format_extractor_count(void) {
    return sizeof(g_xx_format_extractors) / sizeof(g_xx_format_extractors[0]);
}

xx_format_extractor *xx_format_extractor_at(size_t index, xx_file_type_t *type) {
    if (index >= xx_format_extractor_count()) return NULL;
    if (type) *type = g_xx_format_extractors[index].type;
    return g_xx_format_extractors[index].extractor;
}

/* Every registered legacy extractor has one abstract extractor and detector.
 * Use the legacy pointer as the key: entries such as PE32/PE64 and ZIP/ZIP64
 * that share one search implementation also share their abstract adapters. */
#include "xx_format_abstract_extractor_list.inc"
#include "xx_format_reader_only_decls.inc"
#include "xxfclib/formats/zxml/xx_zxml.h"
#include "xxfclib/formats/zisofs/xx_zisofs.h"
#include "xxfclib/formats/nvp/xx_nvp.h"
#include "xxfclib/formats/sqze/xx_sqze.h"
#include "xxfclib/formats/zip_psc/xx_zip_psc.h"
#include "xxfclib/formats/crx_native/xx_crx_native.h"
#include "xxfclib/formats/dcs/xx_dcs.h"
#include "xxfclib/formats/mskn1/xx_mskn1.h"
#include "xxfclib/formats/mskn2/xx_mskn2.h"
#include "xxfclib/formats/mskn3/xx_mskn3.h"
#include "xxfclib/formats/csq/xx_csq.h"
#include "xxfclib/formats/zoot1/xx_zoot1.h"
#include "xxfclib/formats/rdfz/xx_rdfz.h"
#include "xxfclib/formats/zbeos/xx_zbeos.h"
#include "xxfclib/formats/solarispkg_zip/xx_solarispkg_zip.h"
#include "xxfclib/formats/gta_img/xx_gta_img.h"
#include "xxfclib/formats/pbo/xx_pbo.h"
#include "xxfclib/formats/pam_pak/xx_pam_pak.h"
#include "xxfclib/formats/pfpk/xx_pfpk.h"
#include "xxfclib/formats/birdies/xx_birdies.h"
#include "xxfclib/formats/xuiz/xx_xuiz.h"
#include "xxfclib/formats/titan_quest/xx_titan_quest.h"
#include "xxfclib/formats/sbpak/xx_sbpak.h"
#include "xxfclib/formats/cgjp/xx_cgjp.h"
#include "xxfclib/formats/pirs/xx_pirs.h"
#include "xxfclib/formats/px/xx_px.h"
#include "xxfclib/formats/binsh_starkit/xx_binsh_starkit.h"
#include "xxfclib/formats/evd/xx_evd.h"
#include "xxfclib/formats/desksoft/xx_desksoft.h"
#include "xxfclib/formats/metaproducts/xx_metaproducts.h"
#include "xxfclib/formats/visualware/xx_visualware.h"
#include "xxfclib/formats/lyme_sfx/xx_lyme_sfx.h"
#include "xxfclib/formats/audials/xx_audials.h"
#include "xxfclib/formats/psa_disk/xx_psa_disk.h"
#include "xxfclib/formats/webexe/xx_webexe.h"
#include "xxfclib/formats/asd/xx_asd.h"
#include "xxfclib/formats/cfd/xx_cfd.h"
#include "xxfclib/formats/rdc/xx_rdc.h"
#include "xxfclib/formats/fox_sqz/xx_fox_sqz.h"
#include "xxfclib/formats/skf/xx_skf.h"
#include "xxfclib/formats/osl2000/xx_osl2000.h"
#include "xxfclib/formats/lds/xx_lds.h"
#include "xxfclib/formats/dcp_disk/xx_dcp_disk.h"
#include "xxfclib/formats/sony_image/xx_sony_image.h"

static const struct {
    xx_file_type_t type;
    xx_abstract_extractor_getter get;
    xx_abstract_detector_getter get_detector;
} g_xx_reader_only_abstract_extractors[] = {
#include "xx_format_reader_only_rows.inc"
};

static Abstractextractor *xx_abstract_extractor_from_legacy(
    const xx_format_extractor *legacy) {
    size_t i;
    if (!legacy) return NULL;
    if (legacy == xx_format_extractor_get(XX_FILE_TYPE_CRX))
        return xx_crx_native_get_abstract_extractor();
    for (i = 0; i < sizeof(g_xx_abstract_extractor_getters) /
                        sizeof(g_xx_abstract_extractor_getters[0]); ++i) {
        if (g_xx_abstract_extractor_getters[i].legacy == legacy)
            return g_xx_abstract_extractor_getters[i].get();
    }
    return NULL;
}

Abstractextractor *xx_abstract_extractor_get(xx_file_type_t type) {
    xx_format_extractor *legacy = xx_format_extractor_get(type);
    size_t i;
    if (legacy) return xx_abstract_extractor_from_legacy(legacy);
    for (i = 0; i < sizeof(g_xx_reader_only_abstract_extractors) /
                        sizeof(g_xx_reader_only_abstract_extractors[0]); ++i) {
        if (g_xx_reader_only_abstract_extractors[i].type == type)
            return g_xx_reader_only_abstract_extractors[i].get();
    }
    return NULL;
}

static bool xx_abstract_reader_only_visible(size_t index) {
    /* CRX replaces the old adapter at its existing catalog position. */
    return g_xx_reader_only_abstract_extractors[index].type != XX_FILE_TYPE_CRX ||
           xx_format_extractor_get(XX_FILE_TYPE_CRX) == NULL;
}
size_t xx_abstract_extractor_count(void) {
    size_t count=xx_format_extractor_count(),i;
    for(i=0;i<sizeof(g_xx_reader_only_abstract_extractors)/sizeof(g_xx_reader_only_abstract_extractors[0]);++i)
        if(xx_abstract_reader_only_visible(i))++count;
    return count;
}

Abstractextractor *xx_abstract_extractor_at(size_t index,
                                            xx_file_type_t *type) {
    size_t legacy_count = xx_format_extractor_count();
    if (index < legacy_count)
        return xx_abstract_extractor_from_legacy(
            xx_format_extractor_at(index, type));
    index -= legacy_count;
    for(size_t i=0;i<sizeof(g_xx_reader_only_abstract_extractors)/sizeof(g_xx_reader_only_abstract_extractors[0]);++i) {
        if(!xx_abstract_reader_only_visible(i))continue;
        if(index--==0){if(type)*type=g_xx_reader_only_abstract_extractors[i].type;return g_xx_reader_only_abstract_extractors[i].get();}
    }
    return NULL;
}

static Abstractdetector *xx_abstract_detector_from_legacy(
    const xx_format_extractor *legacy) {
    size_t i;
    if (!legacy) return NULL;
    if (legacy == xx_format_extractor_get(XX_FILE_TYPE_CRX))
        return xx_crx_native_get_abstract_detector();
    for (i = 0; i < sizeof(g_xx_abstract_extractor_getters) /
                        sizeof(g_xx_abstract_extractor_getters[0]); ++i) {
        if (g_xx_abstract_extractor_getters[i].legacy == legacy)
            return g_xx_abstract_extractor_getters[i].get_detector();
    }
    return NULL;
}

Abstractdetector *xx_abstract_detector_get(xx_file_type_t type) {
    xx_format_extractor *legacy = xx_format_extractor_get(type);
    size_t i;
    if (legacy) return xx_abstract_detector_from_legacy(legacy);
    for (i = 0; i < sizeof(g_xx_reader_only_abstract_extractors) /
                        sizeof(g_xx_reader_only_abstract_extractors[0]); ++i) {
        if (g_xx_reader_only_abstract_extractors[i].type == type)
            return g_xx_reader_only_abstract_extractors[i].get_detector();
    }
    return NULL;
}

size_t xx_abstract_detector_count(void) {
    return xx_abstract_extractor_count();
}

Abstractdetector *xx_abstract_detector_at(size_t index,
                                         xx_file_type_t *type) {
    size_t legacy_count = xx_format_extractor_count();
    if (index < legacy_count)
        return xx_abstract_detector_from_legacy(
            xx_format_extractor_at(index, type));
    index -= legacy_count;
    for(size_t i=0;i<sizeof(g_xx_reader_only_abstract_extractors)/sizeof(g_xx_reader_only_abstract_extractors[0]);++i) {
        if(!xx_abstract_reader_only_visible(i))continue;
        if(index--==0){if(type)*type=g_xx_reader_only_abstract_extractors[i].type;return g_xx_reader_only_abstract_extractors[i].get_detector();}
    }
    return NULL;
}
