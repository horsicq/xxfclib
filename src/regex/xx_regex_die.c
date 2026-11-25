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

/* Regex matching over DIE archive record names; preserves scan budgets. */
#include "xx_regex_die.h"
#include "xxfclib/regex/xx_regex.h"
#include "../js/xx_js_internal.h"
#include "../die_engine/xx_die_engine_compat.h"
#include "../die_engine/platforms/xx_die_engine_platform.h"
#include "xxfclib/list/xx_list.h"

/* Work budget for one isArchiveRecordPresentExp call.
 *
 * xx_regex_exec caps the backtracking of a single match, but the cap is per
 * subject: an archive with many long entry names multiplies it, and the
 * total grows with the entry count without limit. Measured against the
 * Obfuscapk pattern over 64 KB names: 8.6 s for 40 entries, 32 s for 200,
 * and on from there in proportion to the entry count.
 *
 * The regular expression engine reports no step count, so a budget over the
 * expensive names can only be wall clock, and a wall-clock budget must never
 * be what decides whether a name matches: the same file would answer
 * differently on a different machine, or on the same machine twice. A plain
 * deadline over the whole walk does exactly that - with 6 crafted names in
 * front of a matching one, cdie answered "detected" on some runs and "not
 * detected" on others, where the reference always answers "detected".
 *
 * So the walk goes over the short names first, with no budget at all. Every
 * name a real archive holds is short - a ZIP entry name is a path, and paths
 * stop at PATH_MAX - so for real input the second pass has nothing to do and
 * the answer is exactly the reference's, whatever the machine. Only names
 * longer than any path, which is to say only names that were built to be
 * slow, are left to the deadline, and only when the short names produced no
 * match.                                                                    */
#define ARCHIVE_EXP_SHORT_NAME 4096
#define ARCHIVE_EXP_BUDGET_MS 1500

/* isArchiveRecordPresentExp: true if any archive record name matches the
 * regular expression pPattern with a non-empty captured(0). Mirrors
 * XArchive::isArchiveRecordPresentExp over XBinary::isRegExpPresent (the same
 * unanchored QRegularExpression match). */
int xx_regex_die_archive_present(DieEngine *pEngine, const char *pPattern)
{
    xx_regex *pRegExp = NULL;
    char *pError = NULL;
    cd_i32 *pnCaps = NULL;
    int bFound = 0;
    int nPass = 0;
    size_t i = 0;
    cd_i64 nDeadline = 0;

    if ((!pEngine->bHasZip) || (pPattern == NULL)) {
        return 0;
    }

    pRegExp = js_compile_regexp(pPattern, "", &pError);

    if (pError != NULL) {
        cd_free(pError);
    }

    if (pRegExp == NULL) {
        return 0;
    }

    pnCaps = (cd_i32 *)cd_malloc((size_t)(xx_regex_ngroups(pRegExp) + 1) * 2 * sizeof(cd_i32));

    /* Pass 0 takes the names no longer than a path, pass 1 the rest. The
     * reference walks the record list under isPdStructNotCanceled, so a
     * cancelled scan drops out of the loop instead of running the expression
     * over every remaining name.                                           */
    for (nPass = 0; (nPass < 2) && (!bFound); nPass++) {
        if (nPass == 1) {
            nDeadline = (cd_i64)x_clock_ms() + ARCHIVE_EXP_BUDGET_MS;
        }

        for (i = 0; (xx_zip_get_record_names(&pEngine->zip) && i < xx_zip_get_record_names(&pEngine->zip)->count) && (!pEngine->bStop); i++) {
            const char *pName = *(char **)xx_list_at(xx_zip_get_record_names(&pEngine->zip), i);
            size_t nLength = 0;

            if (pName == NULL) {
                continue;
            }

            nLength = x_strlen(pName);

            if ((nLength > ARCHIVE_EXP_SHORT_NAME) != (nPass == 1)) {
                continue;
            }

            /* Checked once per name rather than in batches: one name can be
             * the whole budget on its own, and a clock reading costs nothing
             * next to a match attempt on a name this long. */
            if ((nPass == 1) && ((cd_i64)x_clock_ms() > nDeadline)) {
                break;
            }

            if (xx_regex_exec(pRegExp, pName, nLength, 0, pnCaps)) {
                if (pnCaps[1] > pnCaps[0]) { /* captured(0) non-empty */
                    bFound = 1;

                    break;
                }
            }
        }
    }

    cd_free(pnCaps);
    xx_regex_free(pRegExp);

    return bFound;
}
