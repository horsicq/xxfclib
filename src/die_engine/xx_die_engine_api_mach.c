/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_mach.c - Mach-O script API dispatch. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_mach_api_table[] = {
    {"getLibraryCurrentVersion", A_getLibraryCurrentVersion, 1},
    {"getNumberOfSections", A_getNumberOfSections, 0},
    {"getSectionFileSize", A_getSectionFileSize, 1},
    {"getSectionFileOffset", A_getSectionFileOffset, 1},
    {"isSectionNamePresent", A_isSectionNamePresent, 1},
    {"getGeneralOptions", A_getGeneralOptions, 0},
    {"isLibraryPresent", A_isLibraryPresent, 2},
    {"getSectionNumber", A_getSectionNumber, 1},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_mach(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_mach_api_table);
}
bool die_engine_api_dispatch_mach(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        case A_getNumberOfSections:
            *pResult = js_num((double)pEngine->mach.nSectionCount);
            return true;

        case A_getSectionName:
            *pResult = js_str(pCtx, "");
            return true;

        case A_getSectionVirtualSize:
        case A_getSectionVirtualAddress:
        case A_getSectionFileSize:
        case A_getSectionFileOffset:
        case A_getSectionCharacteristics: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);
            if (id == A_getSectionFileSize) {
                *pResult = js_num((double)xx_macho_inspect_section_size(&pEngine->mach, nNumber));
            } else if (id == A_getSectionFileOffset) {
                *pResult = js_num((double)xx_macho_inspect_section_offset(&pEngine->mach, nNumber));
            } else {
                *pResult = js_num(0);
            }
            return true;
        }

        case A_isSectionNamePresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = xx_macho_inspect_section_present(&pEngine->mach, pName);
            cd_free(pName);
            *pResult = js_bool(bResult);
            return true;
        }

        case A_getSectionNumber: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int nResult = xx_macho_inspect_section_number(&pEngine->mach, pName);
            cd_free(pName);
            *pResult = js_num((double)nResult);
            return true;
        }

        case A_getGeneralOptions:
            *pResult = js_str(pCtx, "");
            return true;

        case A_isLibraryPresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = xx_macho_inspect_library_present(&pEngine->mach, pName);
            cd_free(pName);
            *pResult = js_bool(bResult);
            return true;
        }

        case A_getLibraryCurrentVersion: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            cd_u32 nVersion = pEngine->bHasMach ? xx_macho_inspect_library_current_version(&pEngine->mach, pName) : 0;

            cd_free(pName);

            *pResult = js_num((double)nVersion);
            return true;
        }

        default:
            break;
    }

    return false;
}
