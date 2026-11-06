/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_cfbf.c - CFBF script API registration. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_cfbf_api_table[] = {
    {NULL, A_NONE, 0}
};

void die_engine_api_register_cfbf(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_cfbf_api_table);
}
