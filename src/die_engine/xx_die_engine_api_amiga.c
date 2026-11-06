/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_amiga.c - AMIGA script API registration. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_amiga_api_table[] = {
    {NULL, A_NONE, 0}
};

void die_engine_api_register_amiga(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_amiga_api_table);
}
