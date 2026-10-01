/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Search adapters for readers added from the ARC corpus. The anchors are
 * cheap filters; each reader validates its complete container before the
 * search engine reports a hit.
 */
#include "xx_format_extractor_engine.h"
#include "xxfclib/formats/fss/xx_fss.h"
#include "xxfclib/formats/mlb_ft/xx_mlb_ft.h"
#include "xxfclib/formats/sfx_localzip/xx_sfx_localzip.h"
#include "xxfclib/formats/thebat_msb/xx_thebat_msb.h"

#define EXTRA_EXTRACTOR(tag, file_type, anchor_list)                         \
    static const xx_file_type_t tag##_types[] = { file_type };              \
    static Abstractformat *tag##_open(xx_io_device *device) {              \
        xx_##tag *reader = xx_##tag##_create(device, 0);                    \
        return reader ? &reader->format : NULL;                             \
    }                                                                        \
    static void tag##_close(Abstractformat *format) {                      \
        xx_##tag##_free((xx_##tag *)format);                                \
    }                                                                        \
    static const xx_format_search_desc tag##_search = {                    \
        tag##_types, 1U, anchor_list, 1U, tag##_open, tag##_close           \
    };                                                                       \
    static xx_format_search_state *tag##_create_search(                    \
        xx_format_extractor *self, xx_io_device *device,                    \
        const xx_list_s *options, xx_pd_struct *pd) {                       \
        (void)self;                                                         \
        return xx_format_search_create(&tag##_search, device, options, pd); \
    }                                                                        \
    static const xx_format_search_info *tag##_current(                     \
        xx_format_extractor *self, xx_format_search_state *state) {         \
        (void)self;                                                         \
        return xx_format_search_current(state);                            \
    }                                                                        \
    static bool tag##_next(xx_format_extractor *self,                       \
                           xx_format_search_state *state,                  \
                           xx_pd_struct *pd) {                             \
        (void)self;                                                         \
        return xx_format_search_find_next(state, pd);                      \
    }                                                                        \
    static void tag##_free_search(xx_format_extractor *self,                \
                                  xx_format_search_state *state) {         \
        (void)self;                                                         \
        xx_format_search_free(state);                                       \
    }                                                                        \
    xx_format_extractor xx_##tag##_extractor = {                            \
        tag##_create_search, tag##_current, tag##_next, tag##_free_search   \
    }

static const uint8_t fss_magic[] = {'S', 'S', 'B', 'O', 'B', 1};
static const xx_format_search_anchor fss_anchors[] = {
    { fss_magic, sizeof(fss_magic), 0U }
};
EXTRA_EXTRACTOR(fss, XX_FILE_TYPE_FSS, fss_anchors);

static const uint8_t mlb_ft_version[] = {6, 0};
static const xx_format_search_anchor mlb_ft_anchors[] = {
    { mlb_ft_version, sizeof(mlb_ft_version), 2U }
};
EXTRA_EXTRACTOR(mlb_ft, XX_FILE_TYPE_MLB_FT, mlb_ft_anchors);

static const uint8_t sfx_localzip_mz[] = {'M', 'Z'};
static const xx_format_search_anchor sfx_localzip_anchors[] = {
    { sfx_localzip_mz, sizeof(sfx_localzip_mz), 0U }
};
EXTRA_EXTRACTOR(sfx_localzip, XX_FILE_TYPE_SFX_LOCALZIP,
                sfx_localzip_anchors);

static const uint8_t thebat_msb_header[] = {
    0x40, 0, 0, 0, 0x40, 0, 0, 0, 0xff, 0xff, 0xff, 0xff
};
static const xx_format_search_anchor thebat_msb_anchors[] = {
    { thebat_msb_header, sizeof(thebat_msb_header), 0U }
};
EXTRA_EXTRACTOR(thebat_msb, XX_FILE_TYPE_THEBAT_MSB,
                thebat_msb_anchors);

#undef EXTRA_EXTRACTOR
