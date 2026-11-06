/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_apk.c - APK script API dispatch. */

#include "xx_die_engine_api_internal.h"
#include "xxfclib/formats/installer/apk/xx_apk.h"

static const ApiEntry g_apk_api_table[] = {
    {"getAndroidManifestRecord", A_getAndroidManifestRecord, 1},
    {"getAndroidManifest", A_getAndroidManifest, 0},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_apk(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_apk_api_table);
}
bool die_engine_api_dispatch_apk(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        case A_getAndroidManifestRecord: {
            char *pRecordName = arg_string(pCtx, nArgc, pArgv, 0);
            char *pValue = pEngine->bHasApk ? xx_apk_manifest_record(&pEngine->apk, pRecordName) : NULL;
            JSVal res = js_str(pCtx, pValue ? pValue : "");

            cd_free(pRecordName);
            xx_str_free(pValue);

            *pResult = res;
            return true;
        }

        case A_getAndroidManifest:
            *pResult = js_str(pCtx, (pEngine->bHasApk && xx_apk_get_manifest(&pEngine->apk)) ? xx_apk_get_manifest(&pEngine->apk) : "");
            return true;

        /* ------------------------------------------------------- PDF  */
        default:
            break;
    }

    return false;
}
