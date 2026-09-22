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

/* xdisasm.h - compact x86 / x86-64 instruction length decoder.
 *
 * The signature database only uses the disassembler to walk instruction
 * chains and to test a handful of mnemonics, so a full decoder is not
 * required: this module reports the instruction length and the mnemonic of
 * the common integer opcodes.                                             */

#ifndef XDISASM_H
#define XDISASM_H

#include "die_engine_bin.h"

typedef struct {
    int nSize;          /* instruction length in bytes (0 when unknown) */
    char sMnemonic[16]; /* uppercase mnemonic, empty when unknown       */
} XDisasmResult;

XDisasmResult xdisasm(DieFile *pFile, cd_i64 nOffset, int nBits);

#endif /* XDISASM_H */
