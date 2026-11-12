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

/* JavaScript String methods that accept regular expressions. Keep their
 * interpreter coercion, captures and replacement behavior in this adapter. */
#include "xx_regex_js_strings.h"

/* The engine's contract for a catastrophic pattern is that it terminates
 * and reports no match (the test suite pins that), so a spent budget stays
 * an ordinary miss; a memory failure or an overlong subject is raised as a
 * RangeError instead of masquerading as "no match". */
int js_regex_exec_checked(JSCtx *pCtx, const xx_regex *pRegExp, const char *pText, size_t nSize, size_t nStart, int32_t *pCaps)
{
    xx_regex_exec_status status = xx_regex_exec_ex(pRegExp, pText, nSize, nStart, pCaps, NULL, NULL);

    if (status == XX_REGEX_EXEC_MATCH) {
        return 1;
    }

    if (status == XX_REGEX_EXEC_NO_MEMORY) {
        js_throw(pCtx, "RangeError: regular expression allocation failed");
    } else if ((status == XX_REGEX_EXEC_INVALID) && (nSize > (size_t)INT32_MAX)) {
        js_throw(pCtx, "RangeError: string too long for regular expression");
    }

    return 0;
}

#define ARG(i) (((i) < nArgc) ? pArgv[i] : js_undefined())

JSVal js_regex_string_split(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    JSVal result = js_new_array(pCtx);
    uint32_t nCount = 0;
    uint32_t nLimit;
    (void)pUser;
    if (pCtx->bException) goto done;
    nLimit = ((nArgc > 1) && (pArgv[1].tag != JT_UNDEF)) ? (uint32_t)js_to_int32(pCtx, pArgv[1]) : UINT32_MAX;
    if (pCtx->bException || nLimit == 0) goto done;

    if ((nArgc == 0) || (pArgv[0].tag == JT_UNDEF)) {
        js_set_index(pCtx, result, 0, js_dup(text));
        goto done;
    }

    if (js_get_regexp(ARG(0))) {
        JSRegExp *pRegExp = js_get_regexp(pArgv[0]);
        const char *pData = js_str_data(text);
        size_t nSize = js_str_len(text), nPos = 0, nLast = 0;
        int nGroups = xx_regex_ngroups(pRegExp);
        int32_t *pCaps = (int32_t *)xx_js_malloc((size_t)(nGroups + 1) * 2 * sizeof(int32_t));
        if (nSize == 0) {
            if (!js_regex_exec_checked(pCtx, pRegExp, pData, nSize, 0, pCaps)) js_set_index(pCtx, result, 0, js_dup(text));
            xx_js_free(pCaps);
            goto done;
        }
        /* A zero-width separator never splits at the end of the subject.
         * At the previous split boundary advance one byte instead; the engine
         * deliberately uses byte offsets rather than UTF-16 code units. */
        while (nPos < nSize && nCount < nLimit) {
            int g;
            if (!js_regex_exec_checked(pCtx, pRegExp, pData, nSize, nPos, pCaps) || (size_t)pCaps[0] == nSize) break;
            if ((size_t)pCaps[1] == nLast) {
                nPos++;
                continue;
            }
            js_set_index(pCtx, result, nCount++, js_strn(pCtx, pData + nLast, (size_t)pCaps[0] - nLast));
            for (g = 1; g <= nGroups && nCount < nLimit; g++) {
                js_set_index(pCtx, result, nCount++, pCaps[g * 2] >= 0 ?
                    js_strn(pCtx, pData + pCaps[g * 2], (size_t)(pCaps[g * 2 + 1] - pCaps[g * 2])) : js_undefined());
            }
            nLast = (size_t)pCaps[1];
            nPos = nLast;
        }
        if (nCount < nLimit) js_set_index(pCtx, result, nCount, js_strn(pCtx, pData + nLast, nSize - nLast));
        xx_js_free(pCaps);
    } else {
        JSVal separator = js_to_string(pCtx, pArgv[0]);
        const char *pData = js_str_data(text);
        size_t nSize = js_str_len(text), nSepSize = js_str_len(separator), nPos = 0;
        if (!pCtx->bException) {
            if (nSepSize == 0) {
                size_t i;
                for (i = 0; i < nSize && nCount < nLimit; i++) {
                    js_set_index(pCtx, result, nCount++, js_strn(pCtx, pData + i, 1));
                }
            } else {
                while (nCount < nLimit) {
                    int64_t nFound = js_builtin_find_substring(pCtx, pData, nSize, js_str_data(separator), nSepSize, nPos);
                    if (nFound < 0) break;
                    js_set_index(pCtx, result, nCount++, js_strn(pCtx, pData + nPos, (size_t)nFound - nPos));
                    nPos = (size_t)nFound + nSepSize;
                }
                if (nCount < nLimit) js_set_index(pCtx, result, nCount, js_strn(pCtx, pData + nPos, nSize - nPos));
            }
        }
        js_release(pCtx, separator);
    }
done:
    js_release(pCtx, text);
    if (pCtx->bException) {
        js_release(pCtx, result);
        return js_undefined();
    }
    return result;
}

/* Builds the array returned by String.match / RegExp.exec. */
static JSVal build_match_array(JSCtx *pCtx, JSRegExp *pRegExp, const char *pData, int32_t *pCaps, JSVal input)
{
    JSVal result = js_new_array(pCtx);
    int g = 0;
    int nGroups = xx_regex_ngroups(pRegExp);

    for (g = 0; g <= nGroups; g++) {
        if (pCaps[g * 2] >= 0) {
            js_set_index(pCtx, result, g, js_strn(pCtx, pData + pCaps[g * 2], (size_t)(pCaps[g * 2 + 1] - pCaps[g * 2])));
        } else {
            js_set_index(pCtx, result, g, js_undefined());
        }
    }

    result.u.o->nArrayLen = nGroups + 1;

    jsobj_put_hidden(pCtx, result.u.o, "index", js_num((double)pCaps[0]));
    jsobj_put_hidden(pCtx, result.u.o, "input", js_dup(input));

    return result;
}

static JSRegExp *coerce_regexp(JSCtx *pCtx, JSVal value, JSVal *pOwned)
{
    JSRegExp *pRegExp = js_get_regexp(value);

    *pOwned = js_undefined();

    if (pRegExp) {
        return pRegExp;
    }

    {
        JSVal text = value.tag == JT_UNDEF ? js_str(pCtx, "") : js_to_string(pCtx, value);
        JSVal created = pCtx->bException ? js_undefined() :
            js_new_regexp_n_val(pCtx, js_str_data(text), js_str_len(text), "", 0);

        js_release(pCtx, text);
        *pOwned = created;

        return js_get_regexp(created);
    }
}

JSVal js_regex_string_match(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    JSVal owned = js_undefined();
    JSRegExp *pRegExp;
    const char *pData = js_str_data(text);
    size_t nSize = js_str_len(text);
    int32_t *pCaps = NULL;
    JSVal result = js_null();

    (void)pUser;
    pRegExp = pCtx->bException ? NULL : coerce_regexp(pCtx, ARG(0), &owned);

    if (pRegExp == NULL) {
        js_release(pCtx, text);
        js_release(pCtx, owned);

        return js_null();
    }

    pCaps = (int32_t *)xx_js_malloc((size_t)(xx_regex_ngroups(pRegExp) + 1) * 2 * sizeof(int32_t));

    if (xx_regex_global(pRegExp)) {
        size_t nPos = 0;
        int64_t nCount = 0;

        result = js_new_array(pCtx);
        js_set(pCtx, ARG(0), "lastIndex", js_num(0));

        while ((nPos <= nSize) && js_regex_exec_checked(pCtx, pRegExp, pData, nSize, nPos, pCaps)) {
            js_set_index(pCtx, result, nCount++, js_strn(pCtx, pData + pCaps[0], (size_t)(pCaps[1] - pCaps[0])));
            nPos = (pCaps[1] > pCaps[0]) ? (size_t)pCaps[1] : (size_t)(pCaps[1] + 1);
        }

        js_set(pCtx, ARG(0), "lastIndex", js_num(0));
        if (nCount == 0) {
            js_release(pCtx, result);
            result = js_null();
        }
    } else if (js_regex_exec_checked(pCtx, pRegExp, pData, nSize, 0, pCaps)) {
        result = build_match_array(pCtx, pRegExp, pData, pCaps, text);
    }

    xx_js_free(pCaps);
    js_release(pCtx, text);
    js_release(pCtx, owned);

    return result;
}

JSVal js_regex_string_search(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    JSVal owned = js_undefined();
    JSRegExp *pRegExp;
    int32_t *pCaps = NULL;
    JSVal result = js_num(-1);

    (void)pUser;
    pRegExp = pCtx->bException ? NULL : coerce_regexp(pCtx, ARG(0), &owned);

    if (pRegExp) {
        pCaps = (int32_t *)xx_js_malloc((size_t)(xx_regex_ngroups(pRegExp) + 1) * 2 * sizeof(int32_t));

        if (js_regex_exec_checked(pCtx, pRegExp, js_str_data(text), js_str_len(text), 0, pCaps)) {
            result = js_num((double)pCaps[0]);
        }

        xx_js_free(pCaps);
    }

    js_release(pCtx, text);
    js_release(pCtx, owned);

    return result;
}

/* Expands $1..$9, $&, $` and $' inside a replacement template. */
static void expand_replacement(JSCtx *pCtx, xx_buf_t *pBuf, const char *pTemplate, size_t nTemplateSize, const char *pData, size_t nDataSize, int32_t *pCaps, int nGroups)
{
    size_t i = 0;

    for (i = 0; i < nTemplateSize; i++) {
        if ((pTemplate[i] == '$') && (i + 1 < nTemplateSize)) {
            char nNext = pTemplate[i + 1];

            if (nNext == '$') {
                xx_buf_append_char(pBuf, '$');
                i++;
                continue;
            }

            if (nNext == '&') {
                xx_buf_append(pBuf, pData + pCaps[0], (size_t)(pCaps[1] - pCaps[0]));
                i++;
                continue;
            }

            if (nNext == '`') {
                xx_buf_append(pBuf, pData, (size_t)pCaps[0]);
                i++;
                continue;
            }

            if (nNext == '\'') {
                xx_buf_append(pBuf, pData + pCaps[1], nDataSize - (size_t)pCaps[1]);
                i++;
                continue;
            }

            if ((nNext >= '0') && (nNext <= '9')) {
                int nIndex = nNext - '0';
                size_t nSkip = 1;

                if ((i + 2 < nTemplateSize) && (pTemplate[i + 2] >= '0') && (pTemplate[i + 2] <= '9')) {
                    int nTwo = nIndex * 10 + (pTemplate[i + 2] - '0');

                    if (nTwo <= nGroups) {
                        nIndex = nTwo;
                        nSkip = 2;
                    }
                }

                if ((nIndex >= 1) && (nIndex <= nGroups)) {
                    if (pCaps[nIndex * 2] >= 0) {
                        xx_buf_append(pBuf, pData + pCaps[nIndex * 2], (size_t)(pCaps[nIndex * 2 + 1] - pCaps[nIndex * 2]));
                    }

                    i += nSkip;
                    continue;
                }
            }
        }

        xx_buf_append_char(pBuf, pTemplate[i]);
    }

    (void)pCtx;
}

JSVal js_regex_string_replace(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    JSVal pattern = ARG(0);
    JSVal replacement = ARG(1);
    JSVal template_ = js_undefined();
    int bCallable = js_is_callable(replacement);
    const char *pData = js_str_data(text);
    size_t nSize = js_str_len(text);
    xx_buf_t buf;
    JSVal result;
    JSRegExp *pRegExp = js_get_regexp(pattern);

    (void)pUser;

    xx_buf_init(&buf);

    if (pRegExp && !pCtx->bException) {
        int32_t *pCaps;
        size_t nPos = 0;
        size_t nLast = 0;
        int bGlobal = xx_regex_global(pRegExp);
        if (!bCallable) template_ = js_to_string(pCtx, replacement);
        if (pCtx->bException) goto done;
        pCaps = (int32_t *)xx_js_malloc((size_t)(xx_regex_ngroups(pRegExp) + 1) * 2 * sizeof(int32_t));
        if (bGlobal) js_set(pCtx, pattern, "lastIndex", js_num(0));

        while ((nPos <= nSize) && js_regex_exec_checked(pCtx, pRegExp, pData, nSize, nPos, pCaps)) {
            xx_buf_append(&buf, pData + nLast, (size_t)pCaps[0] - nLast);

            if (bCallable) {
                int nGroups = xx_regex_ngroups(pRegExp);
                JSVal *pCallArgs = (JSVal *)xx_js_malloc((size_t)(nGroups + 3) * sizeof(JSVal));
                JSVal callResult;
                JSVal callText;
                int g = 0;

                for (g = 0; g <= nGroups; g++) {
                    if (pCaps[g * 2] >= 0) {
                        pCallArgs[g] = js_strn(pCtx, pData + pCaps[g * 2], (size_t)(pCaps[g * 2 + 1] - pCaps[g * 2]));
                    } else {
                        pCallArgs[g] = js_undefined();
                    }
                }

                pCallArgs[nGroups + 1] = js_num((double)pCaps[0]);
                pCallArgs[nGroups + 2] = js_dup(text);

                callResult = js_call(pCtx, replacement, js_undefined(), nGroups + 3, pCallArgs);
                callText = pCtx->bException ? js_undefined() : js_to_string(pCtx, callResult);
                if (!pCtx->bException) xx_buf_append(&buf, js_str_data(callText), js_str_len(callText));

                for (g = 0; g < nGroups + 3; g++) {
                    js_release(pCtx, pCallArgs[g]);
                }

                xx_js_free(pCallArgs);
                js_release(pCtx, callResult);
                js_release(pCtx, callText);
            } else {
                expand_replacement(pCtx, &buf, js_str_data(template_), js_str_len(template_), pData, nSize, pCaps, xx_regex_ngroups(pRegExp));
            }
            if (pCtx->bException) break;

            nLast = (size_t)pCaps[1];
            nPos = (pCaps[1] > pCaps[0]) ? (size_t)pCaps[1] : (size_t)(pCaps[1] + 1);

            if (!bGlobal) {
                break;
            }
        }

        xx_buf_append(&buf, pData + nLast, nSize - nLast);
        xx_js_free(pCaps);
    } else if (!pCtx->bException) {
        JSVal needle = js_to_string(pCtx, pattern);
        int64_t nFound;
        if (!pCtx->bException && !bCallable) template_ = js_to_string(pCtx, replacement);
        if (pCtx->bException) {
            js_release(pCtx, needle);
            goto done;
        }
        nFound = js_builtin_find_substring(pCtx, pData, nSize, js_str_data(needle), js_str_len(needle), 0);

        if (nFound < 0) {
            xx_buf_append(&buf, pData, nSize);
        } else {
            xx_buf_append(&buf, pData, (size_t)nFound);

            if (bCallable) {
                JSVal callArgs[3];
                JSVal callResult;
                JSVal callText;

                callArgs[0] = js_dup(needle);
                callArgs[1] = js_num((double)nFound);
                callArgs[2] = js_dup(text);
                callResult = js_call(pCtx, replacement, js_undefined(), 3, callArgs);
                callText = pCtx->bException ? js_undefined() : js_to_string(pCtx, callResult);
                if (!pCtx->bException) xx_buf_append(&buf, js_str_data(callText), js_str_len(callText));
                js_release(pCtx, callArgs[0]);
                js_release(pCtx, callArgs[2]);
                js_release(pCtx, callResult);
                js_release(pCtx, callText);
            } else {
                int32_t caps[2];

                caps[0] = (int32_t)nFound;
                caps[1] = (int32_t)(nFound + (int64_t)js_str_len(needle));
                expand_replacement(pCtx, &buf, js_str_data(template_), js_str_len(template_), pData, nSize, caps, 0);
            }

            xx_buf_append(&buf, pData + nFound + js_str_len(needle), nSize - (size_t)nFound - js_str_len(needle));
        }

        js_release(pCtx, needle);
    }

done:
    result = pCtx->bException ? js_undefined() : js_strn(pCtx, buf.data, buf.size);
    xx_buf_free(&buf);
    js_release(pCtx, text);
    js_release(pCtx, template_);
    return result;
}
