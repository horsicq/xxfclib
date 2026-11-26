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

/* xx_die_engine_internal.h - the engine's own state.
 *
 * Private to src/die_engine/. It embeds every format parser by value, so it
 * pulls in the whole of format/; that is why it is not part of the public
 * die_engine.h, which consumers include instead.
 */

#ifndef DIE_ENGINE_INTERNAL_H
#define DIE_ENGINE_INTERNAL_H

#include "xxfclib/die_engine/die_engine.h"
#include "xxfclib/die_engine/die_engine_debug.h"
#include "xxfclib/js/xx_js.h"

#include "xx_die_engine_compat.h"
#include "xx_die_engine_engine.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/formats/xx_data_signature.h"
#include "xxfclib/formats/xx_memory_map.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet.h"
#include "xxfclib/formats/image/jpeg/xx_jpeg.h"
#include "xxfclib/formats/image/png/xx_png.h"
#include "xxfclib/formats/installer/apk/xx_apk.h"
#include "xxfclib/formats/document/pdf/xxpdf.h"
#include "xxfclib/formats/executable/elf/xx_elf.h"
#include "xxfclib/formats/executable/dex/xx_dex.h"
#include "xxfclib/formats/executable/dex/xx_dex_legacy.h"
#include "xxfclib/formats/executable/macho/xx_macho.h"
#include "xxfclib/formats/executable/pyc/xx_pyc.h"
#include "xxfclib/formats/archive/zip/xx_zip.h"
#include "xxfclib/formats/filesystem/iso9660/xx_iso9660.h"
/* Native format compatibility alias conflicts with DIE public member names. */
#undef fileType

/* ---------------------------------------------------------------- engine  */

typedef struct {
    char *pType;
    char *pName;
} BLRecord;

/* One outstanding startTiming() handle: the reference keeps the same map of
 * handle to running timer. */
typedef struct {
    cd_i64 nHandle;
    cd_i64 nStart;
} TimingRecord;

struct DieEngine {
    DBase *pDb;
    xx_scan_options *pOptions;
    xx_scan_result *pResult;
    xx_pd_struct *pd; /* Borrowed progress/cancellation for this synchronous pass. */

    xx_io_device *device;  /* Owned buffered input for this signature pass. */
    const char *file_name; /* Borrowed scan-option/source name. */
    xx_pe_inspection pe;
    int bHasPE;
    xx_dotnet_inspection dotnet;
    int bHasDotNet;
    xx_jpeg jpeg;
    int bHasJpeg;
    xx_png png;
    int bHasPng;
    xx_apk apk;
    int bHasApk;
    xx_pdf pdf;
    int bHasPdf;
    xx_elf_inspection elf;
    int bHasElf;
    cd_u64 nElfEntryPointAddress; /* XELF::getMemoryMap nEntryPointAddress */
    xx_dex dex;
    int bHasDex;
    /* APKiD DEX: YARA 3.11 legacy dex module values (DEX.getUnicodeMemberNameCount),
     * computed on first use. */
    xx_dex_legacy dexLegacy;
    int bDexLegacyDone;
    xx_macho_inspection mach;
    int bHasMach;
    xx_pyc pyc;
    int bHasPyc;
    xx_zip zip;
    int bHasZip;
    XFileType fileType;
    /* Address width for the disassembler and the is16/is32/is64 accessors.
     * It belongs to the image, not to its memory map, which is why it is
     * here rather than in xx_memory_map. */
    int nBits;
    int bIsCliAssembly; /* a .NET PE — bind DOTNET, run PE/DOTNET scripts */
    xx_memory_map *pMap;
    /* How xx_data_signature_match resolves addresses and what it does with a
     * pointer read past the end. Built once per scan from pMap, because
     * every signature the databases throw at the file uses the same one. */
    xx_data_sig_context sigContext;

    JSCtx *pJs;
    const die_engine_debug_config *pDebug; /* Borrowed for the synchronous debug pass. */
    cd_i64 nLastLogTime;

    BLRecord *pBlackList;
    int nBlackListCount;

    int bStop;
    char *pCurrentScript;
    const char *pCurrentScriptPath; /* borrowed database path of pCurrentScript */
    char *pCurrentFormatId;         /* bundled audio.1.sg //fmt[...] tag, if any */
    char *pCurrentFormatName;       /* name assigned by that exact marked branch */
    /* Latest progress error. The reference overwrites this during a pass,
     * then adds one "Last error" record after the detection scripts. */
    char *pLastError;

    /* Outstanding startTiming() handles, for the -l trace. */
    TimingRecord *pTimings;
    int nTimingCount;
    int nTimingCapacity;
    cd_i64 nNextTimingHandle;

    /* Cached header / entry point / overlay signature strings. */
    char *pHeaderSignature;
    char *pEntryPointSignature;
    char *pOverlaySignature;
};

/* Installs the DIE script API on a JavaScript context. */
XXFC_API void die_engine_install_api(DieEngine *pEngine);

/* Initialise shared record metadata for the current signature scan pass. */
void die_engine_record_init(DieEngine *pEngine, xx_scan_record *pRecord);

/* Signature validation with the reference's progress-error handling.
 * Compare kind: 0 = no progress error (entry point/.NET), 1 = header,
 * 2 = overlay. Header/overlay cached comparisons do not report parse errors. */
XXFC_API int die_engine_signature_compare(DieEngine *pEngine, cd_i64 nOffset, const char *pText, int nKind, cd_i64 nCacheBase);
XXFC_API cd_i64 die_engine_signature_find(DieEngine *pEngine, cd_i64 nOffset, cd_i64 nSize, const char *pText);
XXFC_API int die_engine_signature_find_any(DieEngine *pEngine, cd_i64 nOffset, cd_i64 nSize, const char *const *ppTexts, size_t nCount);

/* The reference emits its profiling trace through warningMessage, which the
 * console prints to stdout as "[WARNING] <text>"; a measured step adds
 * " [<n> ms]". These three reproduce that shape (xx_die_engine_api.c).
 *
 * die_engine_profile_start returns the stamp die_engine_profile_end needs, or -1 when
 * profiling is off - which is also the signal to skip building the text. */
XXFC_API cd_i64 die_engine_profile_start(DieEngine *pEngine);
XXFC_API void die_engine_profile_text(DieEngine *pEngine, const char *pText);
XXFC_API X_PRINTF_LIKE(3, 4) void die_engine_profile_end(DieEngine *pEngine, cd_i64 nStart, const char *pFormat, ...);
/* Releases the outstanding startTiming() handles. */
XXFC_API void die_engine_profile_free(DieEngine *pEngine);

#endif /* DIE_ENGINE_INTERNAL_H */
