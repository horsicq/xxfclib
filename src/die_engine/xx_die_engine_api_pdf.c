/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_pdf.c - PDF script API dispatch. */

#include "xx_die_engine_api_internal.h"
#include "xxfclib/list/xx_list.h"
#include "xxfclib/strings/xx_string.h"

static const ApiEntry g_pdf_api_table[] = {
    {"getHeaderCommentAsHex", A_getHeaderCommentAsHex, 0},
    {"getStringValuesByKey", A_getStringValuesByKey, 1},
    {"getValuesByKey", A_getValuesByKey, 1},
    {"isValuesHexByKey", A_isValuesHexByKey, 1},
    {"isEncrypted", A_isEncrypted, 0},
    {"getEncryption", A_getEncryption, 0},
    {"getPermissions", A_getPermissions, 0},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_pdf(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_pdf_api_table);
}
bool die_engine_api_dispatch_pdf(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        case A_getHeaderCommentAsHex: {
            char *pHex = pEngine->bHasPdf ? xx_pdf_get_header_comment_hex(&pEngine->pdf, NULL) : NULL;
            JSVal res = js_str(pCtx, pHex ? pHex : "");

            xx_str_free(pHex);

            *pResult = res;
            return true;
        }

        case A_getStringValuesByKey:
        case A_getValuesByKey: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal res = js_new_array(pCtx);

            if (pEngine->bHasPdf) {
                xx_list_s values;
                size_t i = 0;

                if (xx_list_init(&values, sizeof(char *), NULL)) {
                    /* pdf_script inspects at most 20 parts per object. */
                    (void)xx_pdf_get_values_by_key(&pEngine->pdf, pKey, id == A_getStringValuesByKey, 20, &values, NULL);

                    for (i = 0; i < values.count; i++) {
                        char **pValue = (char **)xx_list_at(&values, i);
                        js_array_push(pCtx, res, js_str(pCtx, *pValue ? *pValue : ""));
                        xx_str_free(*pValue);
                    }

                    xx_list_cleanup(&values);
                }
            }

            cd_free(pKey);

            *pResult = res;
            return true;
        }

        case A_isValuesHexByKey: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasPdf ? xx_pdf_is_values_hex_by_key(&pEngine->pdf, pKey, 20, NULL) : 0;

            cd_free(pKey);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isEncrypted:
            *pResult = js_bool(pEngine->bHasPdf ? xx_pdf_is_encrypted(&pEngine->pdf) : 0);
            return true;

        case A_getEncryption:
        case A_getPermissions: {
            char *pInfo = NULL;
            JSVal res;

            if (pEngine->bHasPdf) {
                pInfo = id == A_getEncryption ? xx_pdf_get_encryption(&pEngine->pdf, NULL) : xx_pdf_get_permissions(&pEngine->pdf, NULL);
            }

            res = js_str(pCtx, pInfo ? pInfo : "");
            xx_str_free(pInfo);

            *pResult = res;
            return true;
        }

        /* ------------------------------------------------------- DEX  */
        default:
            break;
    }

    return false;
}
