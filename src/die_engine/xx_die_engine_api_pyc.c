/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_pyc.c - PYC script API dispatch. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_pyc_api_table[] = {
    {"isConstPresent", A_isConstPresent, 1},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_pyc(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_pyc_api_table);
}
bool die_engine_api_dispatch_pyc(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        case A_isConstPresent: {
            char *pValue = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasPyc ? xx_pyc_const_present(&pEngine->pyc, pValue) : 0;

            xx_str_free(pValue);

            *pResult = js_bool(bResult);
            return true;
        }

        /* Util: exact 64-bit arithmetic (JS numbers are doubles). Mirrors
         * Util_script; a zero divisor yields -1 as in the reference, and the
         * shifts take the low 6 bits of the count as x86 does. */
        default:
            break;
    }

    return false;
}
