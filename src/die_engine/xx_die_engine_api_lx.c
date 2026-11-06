/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_lx.c - LX script API registration. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_lx_api_table[] = {
    {NULL, A_NONE, 0}
};

void die_engine_api_register_lx(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_lx_api_table);
}
