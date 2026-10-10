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

/* xx_die_engine_xdisasm.h - x86 / x86-64 disassembly for DIE scripts. */

#ifndef XDISASM_H
#define XDISASM_H

#include "xx_die_engine_bin.h"

typedef struct {
    int nSize;              /* instruction length in bytes (0 when unknown) */
    char sInstruction[512]; /* complete uppercase Intel instruction      */
    int bInvalid;           /* undecodable byte; next address is zero       */
} XDisasmResult;

XDisasmResult xdisasm(DieFile *pFile, cd_i64 nOffset, int nBits);
XDisasmResult xdisasm_at(DieFile *pFile, cd_i64 nOffset, int nBits, cd_u64 nAddress);
cd_u64 xdisasm_next_address(const XDisasmResult *pResult, cd_u64 nAddress);
void xdisasm_close(DieFile *pFile);

#endif /* XDISASM_H */
