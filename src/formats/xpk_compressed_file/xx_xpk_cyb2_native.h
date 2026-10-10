/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C CYB2/MASH view, following Ancient CYB2Decoder.cpp,
 * commit 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_CYB2_NATIVE_H
#define XX_XPK_CYB2_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>

/* Version 26=CYB2. A CYB2 chunk contains the child method FourCC, six
 * informational bytes, then raw child payload. The original producer corpus
 * uses MASH; pass this checked view to the existing native XPK-MASH decoder.
 * Other child methods remain explicitly unsupported until verified.
 */
static bool xpk_cyb2_mash_view(const uint8_t *packed, size_t size, const uint8_t **child, size_t *child_size, xx_pd_struct *pd)
{
    if (!packed || !child || !child_size || size <= 10U || packed[0] != 'M' || packed[1] != 'A' || packed[2] != 'S' || packed[3] != 'H' || xx_pd_is_stopped(pd))
        return false;
    *child = packed + 10U;
    *child_size = size - 10U;
    return true;
}
#endif
