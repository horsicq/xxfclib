/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_msdos.c - MSDOS script API dispatch. */

#include "xx_die_engine_api_internal.h"

bool die_engine_api_dispatch_msdos(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    xx_pe_inspection *pPE = &pEngine->pe;

    switch (id) {
        case A_isRichSignaturePresent:
            *pResult = js_bool(pEngine->bHasPE && pPE->bRichPresent);
            return true;

        case A_getNumberOfRichIDs:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nRichCount : 0));
            return true;

        case A_isRichVersionPresent: {
            cd_i64 nVersion = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            int i = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nRichCount; i++) {
                    if (pPE->pRichRecords[i].nVersion == (cd_u16)nVersion) {
                        *pResult = js_bool(1);
                        return true;
                    }
                }
            }

            *pResult = js_bool(0);
            return true;
        }

        case A_getRichVersion:
        case A_getRichID:
        case A_getRichCount: {
            cd_i64 nIndex = arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nIndex < 0) || (nIndex >= pPE->nRichCount)) {
                *pResult = js_num(0);
                return true;
            }

            if (id == A_getRichVersion) {
                *pResult = js_num((double)pPE->pRichRecords[nIndex].nVersion);
                return true;
            }

            if (id == A_getRichID) {
                *pResult = js_num((double)pPE->pRichRecords[nIndex].nId);
                return true;
            }

            *pResult = js_num((double)pPE->pRichRecords[nIndex].nCount);
            return true;
        }

        /* XMSDOS::getDosStubSize clamps e_lfanew - sizeof(IMAGE_DOS_HEADEREX)
         * at zero, and MSDOS_Script's constructor keeps offset and size at 0
         * unless isDosStubPresent(). A PE whose e_lfanew is at or below 0x40
         * therefore reports offset 0 and size 0, not offset 64 and a negative
         * size.                                                            */
        case A_getDosStubOffset:
        case A_getDosStubSize:
        case A_isDosStubPresent: {
            cd_i64 nStubSize = pEngine->bHasPE ? (pPE->nLfanew - 64) : 0;

            if (nStubSize < 0) {
                nStubSize = 0;
            }

            if (id == A_getDosStubSize) {
                *pResult = js_num((double)nStubSize);
                return true;
            }

            if (id == A_isDosStubPresent) {
                *pResult = js_bool(nStubSize != 0);
                return true;
            }

            *pResult = js_num((double)((nStubSize != 0) ? 64 : 0));
            return true;
        }

        default:
            break;
    }

    return false;
}
