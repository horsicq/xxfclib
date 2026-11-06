/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_dos4g.c - DOS4G script API registration. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_dos4g_api_table[] = {
    {NULL, A_NONE, 0}
};

void die_engine_api_register_dos4g(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_dos4g_api_table);
}
