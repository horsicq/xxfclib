/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_archive.c - Archive script API dispatch. */

#include "xx_die_engine_api_internal.h"
#include "../regex/xx_regex_die.h"

static const ApiEntry g_archive_api_table[] = {
    {"isArchiveRecordPresent", A_isArchiveRecordPresent, 1},
    {"isArchiveRecordPresentExp", A_isArchiveRecordPresentExp, 1},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_archive(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_archive_api_table);
}
bool die_engine_api_dispatch_archive(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        case A_isArchiveRecordPresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasZip ? xx_zip_record_present(&pEngine->zip, pName) : 0;

            cd_free(pName);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isArchiveRecordPresentExp: {
            char *pPattern = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = xx_regex_die_archive_present(pEngine, pPattern);

            cd_free(pPattern);

            *pResult = js_bool(bResult);
            return true;
        }

        default:
            break;
    }

    return false;
}
