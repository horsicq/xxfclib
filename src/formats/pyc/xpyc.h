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

#ifndef CDIE_XPYC_H
#define CDIE_XPYC_H

#include "../../die_engine/die_engine_bin.h"
#include "../../die_engine/die_engine_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A Python compiled module. The database only needs the interpreter version
 * that the two-byte magic number encodes; the "\r\n" marker at offset 2 has
 * already gated detection. sVersion is XPYC::getVersion() = _magicToVersion:
 * the interpreter release string, or "" when the magic is not in the table.
 * Mirrors XPYC.                                                             */
typedef struct {
    int bValid;
    cd_u16 nMagic;
    char sVersion[24]; /* longest table entry is "3.14 will start with" */
    CDVec vecConsts;   /* owned char *: the module code object's top-level
                        * string constants, as far as XPYC::getCodeObject
                        * reaches them (it stops at the first code object). */
} XPyc;

/* True when the two-byte magic is a release listed in the table. Detection
 * requires this (XBinary::getFileTypes gates FT_PYC on XPYC::_isMagicKnown). */
int xpyc_is_known_magic(cd_u16 nMagic);

int xpyc_parse(DieFile *pFile, XPyc *pPyc);
void xpyc_free(XPyc *pPyc);

/* isConstPresent: a top-level string constant equals pValue exactly. */
int xpyc_const_present(XPyc *pPyc, const char *pValue);

#ifdef __cplusplus
}
#endif

#endif
