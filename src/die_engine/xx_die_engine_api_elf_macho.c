/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_elf_macho.c - ELF and Mach-O script API dispatch. */

#include "xx_die_engine_api_internal.h"

bool die_engine_api_dispatch_elf_macho(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        /* ------------------------------------------------- ELF / Mach-O */
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
