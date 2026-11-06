/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_iso9660.c - ISO9660 script API dispatch. */

#include "xx_die_engine_api_internal.h"

static char *native_iso_identifier(xx_io_device *file, int64_t offset, size_t size)
{
    xx_iso9660 iso;
    char *text;
    xx_iso9660_init(&iso, file, 0);
    text = xx_iso9660_get_identifier(&iso, offset, size, NULL);
    xx_iso9660_destroy(&iso);
    return text;
}

static const ApiEntry g_iso9660_api_table[] = {
    {"getApplicationIdentifier", A_getApplicationIdentifier, 0},
    {"getDataPreparerIdentifier", A_getDataPreparerIdentifier, 0},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_iso9660(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_iso9660_api_table);
}
bool die_engine_api_dispatch_iso9660(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    xx_io_device *pFile = pEngine->device;

    switch (id) {
        case A_getApplicationIdentifier:
            *pResult = native_str_take(pCtx, native_iso_identifier(pFile, 574, 128));
            return true;
        case A_getDataPreparerIdentifier:
            *pResult = native_str_take(pCtx, native_iso_identifier(pFile, 446, 128));
            return true;

        default:
            break;
    }

    return false;
}
