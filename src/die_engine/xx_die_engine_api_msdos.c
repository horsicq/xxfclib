/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_msdos.c - MSDOS script API dispatch. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_msdos_api_table[] = {{"isRichSignaturePresent", A_isRichSignaturePresent, 0},
                                             {"getNumberOfRichIDs", A_getNumberOfRichIDs, 0},
                                             {"isRichVersionPresent", A_isRichVersionPresent, 1},
                                             {"getRichVersion", A_getRichVersion, 1},
                                             {"getRichID", A_getRichID, 1},
                                             {"getRichCount", A_getRichCount, 1},
                                             {"getDosStubOffset", A_getDosStubOffset, 0},
                                             {"getDosStubSize", A_getDosStubSize, 0},
                                             {"isDosStubPresent", A_isDosStubPresent, 0},
                                             {"isNE", A_isNE, 0},
                                             {"isLE", A_isLE, 0},
                                             {"isLX", A_isLX, 0},
                                             {NULL, A_NONE, 0}};

void die_engine_api_register_msdos(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_msdos_api_table);
}
/* XMSDOS::isNE/isLE/isLX: the word e_lfanew points at. A file too short to
 * hold e_lfanew has no extended header; without the size test the failed
 * read yields 0 and the file's first two bytes would be compared instead. */
static int msdos_new_header_is(xx_io_device *pFile, cd_u16 nSignature)
{
    if (xx_io_total_size(pFile) < 0x40) {
        return 0;
    }

    return xx_io_get_u16(pFile, (cd_i64)xx_io_get_u32(pFile, 0x3C, false), false) == nSignature;
}

bool die_engine_api_dispatch_msdos(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    xx_pe_inspection *pPE = &pEngine->pe;
    xx_io_device *pFile = pEngine->device;

    switch (id) {
        case A_isNE: *pResult = js_bool(msdos_new_header_is(pFile, 0x454E)); return true;
        case A_isLE: *pResult = js_bool(msdos_new_header_is(pFile, 0x454C)); return true;
        case A_isLX: *pResult = js_bool(msdos_new_header_is(pFile, 0x584C)); return true;

        case A_isRichSignaturePresent: *pResult = js_bool(pEngine->bHasPE && pPE->bRichPresent); return true;

        case A_getNumberOfRichIDs: *pResult = js_num((double)(pEngine->bHasPE ? pPE->nRichCount : 0)); return true;

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
            /* MSDOS_Script caches the MZ stub for every extended DOS format,
             * including when the current script object is MSDOS itself. */
            int bExtended = msdos_new_header_is(pFile, 0x4550) || msdos_new_header_is(pFile, 0x454E) ||
                            msdos_new_header_is(pFile, 0x454C) || msdos_new_header_is(pFile, 0x584C);
            cd_i64 nStubSize = bExtended ? (cd_i64)xx_io_get_u32(pFile, 0x3C, false) - 64 : 0;

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

        default: break;
    }

    return false;
}
