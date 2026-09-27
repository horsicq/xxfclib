/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "die_engine_disasm.h"
#include <cdisasm/cdisasm_x86.h>
#include <cdisasm/cdisasm_format.h>
#include <string.h>

DieDisasmResult die_disasm(const DieFile *pFile, cd_i64 nOffset, int nBits)
{
    DieDisasmResult result = {1, 0, {0}};
    cdisasm_instruction instruction;
    char text[128];
    const char *mnemonic;
    size_t available, length;
    uint32_t decoded;

    if (pFile == NULL || pFile->pData == NULL || nOffset < 0
        || nOffset >= pFile->nSize
        || (nBits != 16 && nBits != 32 && nBits != 64)) {
        return result;
    }
    available = pFile->nSize - nOffset > 15
        ? 15u : (size_t)(pFile->nSize - nOffset);
    /* NULL family flags select base opcodes even when another consumer
     * supplies a cdisasm target built with extra opcode support. */
    decoded = cdisasm_x86_decode(CDISASM_CPU_X86, (cdisasm_mode)nBits,
        pFile->pData + nOffset, available, 0, NULL, &instruction);
    if (decoded == 0) {
        return result;
    }
    if (decoded > available || decoded > 15) return result;
    result.nSize = (int)decoded;
    /* DIE signatures use MOV for the 64-bit immediate form too. */
    if (instruction.name_id == CDISASM_X86_NAME_MOVABS) {
        result.bValid = 1;
        memcpy(result.sMnemonic, "MOV", 4);
        return result;
    }
    if (cdisasm_x86_format(&instruction, CDISASM_FORMAT_UPPERCASE_OPCODE,
            text, sizeof(text)) == 0) {
        return result;
    }
    /* The script API returns only the mnemonic, without operands or
     * instruction-prefix words. */
    mnemonic = text;
    while (*mnemonic != '\0') {
        length = strcspn(mnemonic, " ");
        if ((length == 4 && strncmp(mnemonic, "LOCK", 4) == 0)
            || (length == 3 && strncmp(mnemonic, "REP", 3) == 0)
            || (length == 4 && strncmp(mnemonic, "REPE", 4) == 0)
            || (length == 5 && strncmp(mnemonic, "REPNE", 5) == 0)
            || (length == 8 && strncmp(mnemonic, "XACQUIRE", 8) == 0)
            || (length == 8 && strncmp(mnemonic, "XRELEASE", 8) == 0)) {
            mnemonic += length;
            while (*mnemonic == ' ') ++mnemonic;
            continue;
        }
        if (length >= sizeof(result.sMnemonic))
            length = sizeof(result.sMnemonic) - 1;
        memcpy(result.sMnemonic, mnemonic, length);
        result.sMnemonic[length] = '\0';
        break;
    }
    result.bValid = result.sMnemonic[0] != 0;
    return result;
}
