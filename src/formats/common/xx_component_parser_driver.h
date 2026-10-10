/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Private RAM component parser drivers.
 * Callback declarations and container grammars remain in the includer.
 * Separate drivers preserve the original single-read and chunked-read paths.
 */
#ifndef XX_COMPONENT_PARSER_DRIVER_H
#define XX_COMPONENT_PARSER_DRIVER_H
#include "xxfclib/data/xx_pd.h"

static __inline bool xx_component_parser_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

#define XX_COMPONENT_SINGLE_READ_DRIVER(prefix, limit)                                                                                      \
    static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)                                                                 \
    {                                                                                                                                       \
        int64_t available = pm_available(f);                                                                                                \
        uint8_t *b;                                                                                                                         \
        bool result;                                                                                                                        \
        if (available < 1 || available > limit || xx_component_parser_stopped(pd) || !prefix##_quick(f, (uint64_t)available)) return false; \
        b = (uint8_t *)xx_mem_alloc((size_t)available);                                                                                     \
        if (!b) return false;                                                                                                               \
        result = pm_read(f, 0, b, (size_t)available) && prefix##_parse(f, s, b, (uint64_t)available, pd);                                   \
        xx_mem_free(b);                                                                                                                     \
        return result;                                                                                                                      \
    }

#define XX_COMPONENT_CHUNKED_READ_DRIVER(prefix, limit, on_success)                                                                         \
    static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)                                                                 \
    {                                                                                                                                       \
        int64_t available = pm_available(f);                                                                                                \
        uint8_t *b;                                                                                                                         \
        uint64_t p = 0;                                                                                                                     \
        bool ok = false;                                                                                                                    \
        if (available < 1 || available > limit || xx_component_parser_stopped(pd) || !prefix##_quick(f, (uint64_t)available)) return false; \
        b = (uint8_t *)xx_mem_alloc((size_t)available);                                                                                     \
        if (!b) return false;                                                                                                               \
        while (p < (uint64_t)available) {                                                                                                   \
            size_t z = (uint64_t)available - p > 65536 ? 65536 : (size_t)((uint64_t)available - p);                                         \
            if (xx_component_parser_stopped(pd) || !pm_read(f, (int64_t)p, b + p, z)) {                                                     \
                goto done;                                                                                                                  \
            }                                                                                                                               \
            p += z;                                                                                                                         \
        }                                                                                                                                   \
        ok = !xx_component_parser_stopped(pd) && prefix##_parse(f, s, b, (uint64_t)available, pd) && !xx_component_parser_stopped(pd);      \
        on_success done : xx_mem_free(b);                                                                                                   \
        return ok;                                                                                                                          \
    }

#endif
