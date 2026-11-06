/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_elf.c - ELF script API dispatch. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_elf_api_table[] = {
    {"isStringInTablePresent", A_isStringInTablePresent, 2},
    {"getNumberOfPrograms", A_getNumberOfPrograms, 0},
    {"getProgramFileOffset", A_getProgramFileOffset, 1},
    {"getProgramFileSize", A_getProgramFileSize, 1},
    {"getElfHeader_type", A_getElfHeader_type, 0},
    {"getElfHeader_machine", A_getElfHeader_machine, 0},
    {"getElfHeader_entry", A_getElfHeader_entry, 0},
    {"getElfHeader_phoff", A_getElfHeader_phoff, 0},
    {"getElfHeader_shoff", A_getElfHeader_shoff, 0},
    {"getElfHeader_phnum", A_getElfHeader_phnum, 0},
    {"getElfHeader_shnum", A_getElfHeader_shnum, 0},
    {"getElfHeader_shentsize", A_getElfHeader_shentsize, 0},
    {"getElfHeader_phentsize", A_getElfHeader_phentsize, 0},
    {"getElfHeader_shstrndx", A_getElfHeader_shstrndx, 0},
    {"getRunPath", A_getRunPath, 0},
    {"isNotePresent", A_isNotePresent, 1},
    {"getNumberOfSections", A_getNumberOfSections, 0},
    {"getSectionFileSize", A_getSectionFileSize, 1},
    {"getSectionFileOffset", A_getSectionFileOffset, 1},
    {"isSectionNamePresent", A_isSectionNamePresent, 1},
    {"getGeneralOptions", A_getGeneralOptions, 0},
    {"isLibraryPresent", A_isLibraryPresent, 2},
    {"getSectionNumber", A_getSectionNumber, 1},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_elf(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_elf_api_table);
}
bool die_engine_api_dispatch_elf(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        case A_getNumberOfSections:
            *pResult = js_num((double)pEngine->elf.nShnum);
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
                *pResult = js_num((double)xx_elf_inspect_section_size(&pEngine->elf, nNumber));
            } else if (id == A_getSectionFileOffset) {
                *pResult = js_num((double)xx_elf_inspect_section_offset(&pEngine->elf, nNumber));
            } else {
                *pResult = js_num(0);
            }
            return true;
        }

        case A_isSectionNamePresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = xx_elf_inspect_section_present(&pEngine->elf, pName);
            cd_free(pName);
            *pResult = js_bool(bResult);
            return true;
        }

        case A_getSectionNumber: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int nResult = xx_elf_inspect_section_number(&pEngine->elf, pName);
            cd_free(pName);
            *pResult = js_num((double)nResult);
            return true;
        }

        case A_getGeneralOptions:
            *pResult = js_str(pCtx, pEngine->elf.sGeneralOptions);
            return true;

        case A_isLibraryPresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = xx_elf_inspect_library_present(&pEngine->elf, pName);
            cd_free(pName);
            *pResult = js_bool(bResult);
            return true;
        }

        case A_isStringInTablePresent: {
            char *pSectionName = arg_string(pCtx, nArgc, pArgv, 0);
            char *pString = arg_string(pCtx, nArgc, pArgv, 1);
            int bResult = pEngine->bHasElf ? xx_elf_inspect_string_in_table_present(&pEngine->elf, pSectionName, pString) : 0;

            cd_free(pSectionName);
            cd_free(pString);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_getNumberOfPrograms:
            *pResult = js_num((double)(pEngine->bHasElf ? pEngine->elf.nPhnum : 0));
            return true;

        case A_getProgramFileOffset:
            *pResult = js_num((double)(pEngine->bHasElf ? xx_elf_inspect_program_offset(&pEngine->elf, (int)arg_i64(pCtx, nArgc, pArgv, 0, 0)) : 0));
            return true;

        case A_getProgramFileSize:
            *pResult = js_num((double)(pEngine->bHasElf ? xx_elf_inspect_program_size(&pEngine->elf, (int)arg_i64(pCtx, nArgc, pArgv, 0, 0)) : 0));
            return true;

        case A_getElfHeader_type:
            *pResult = js_num((double)(pEngine->bHasElf ? pEngine->elf.nType : 0));
            return true;

        case A_getElfHeader_machine:
            *pResult = js_num((double)(pEngine->bHasElf ? pEngine->elf.nMachine : 0));
            return true;

        case A_getElfHeader_entry:
            *pResult = js_num((double)(pEngine->bHasElf ? pEngine->elf.nEntry : 0));
            return true;

        case A_getElfHeader_phoff:
            *pResult = js_num((double)(pEngine->bHasElf ? pEngine->elf.nPhoff : 0));
            return true;

        case A_getElfHeader_shoff:
            *pResult = js_num((double)(pEngine->bHasElf ? pEngine->elf.nShoff : 0));
            return true;

        case A_getElfHeader_phnum:
            *pResult = js_num((double)(pEngine->bHasElf ? pEngine->elf.nPhnum : 0));
            return true;

        case A_getElfHeader_shnum:
            *pResult = js_num((double)(pEngine->bHasElf ? pEngine->elf.nShnum : 0));
            return true;

        case A_getElfHeader_shentsize:
            *pResult = js_num((double)(pEngine->bHasElf ? pEngine->elf.nShentsize : 0));
            return true;

        case A_getElfHeader_phentsize:
            *pResult = js_num((double)(pEngine->bHasElf ? pEngine->elf.nPhentsize : 0));
            return true;

        case A_getElfHeader_shstrndx:
            *pResult = js_num((double)(pEngine->bHasElf ? pEngine->elf.nShstrndx : 0));
            return true;

        case A_getRunPath:
            *pResult = js_str(pCtx, (pEngine->bHasElf && pEngine->elf.pRunPath) ? pEngine->elf.pRunPath : "");
            return true;

        case A_isNotePresent:
            *pResult = js_bool(0);
            return true;

        default:
            break;
    }

    return false;
}
