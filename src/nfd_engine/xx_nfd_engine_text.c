/* Copyright (c) 2019-2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Derived from SpecAbstract/modules/nfd_text.cpp and the active text rules in
 * modules/nfd_binary.cpp. Source snapshot: 2026-11-06.
 */

#include "xx_nfd_engine_internal.h"
#include "xxfclib/regex/xx_regex.h"
#include "xxfclib/rt/xx_rt.h"

static bool regex_abort(void *user)
{
    return xx_pd_is_stopped((xx_pd_struct *)user);
}

static int regex_find(const char *pattern, const char *flags, const char *text,
                      int32_t **captures, int *groups, xx_pd_struct *pd)
{
    xx_regex *regex;
    char *error = NULL;
    int32_t *local;
    int count;
    xx_regex_exec_status status;
    *captures = NULL;
    *groups = 0;
    regex = xx_regex_compile(pattern, flags, &error);
    if (!regex) {
        xx_rt_free(error);
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Cannot compile NFD text expression");
        return -1;
    }
    count = xx_regex_ngroups(regex);
    local = (int32_t *)xx_rt_malloc((size_t)(count + 1) * 2U * sizeof(*local));
    if (!local) {
        xx_regex_free(regex);
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate NFD text captures");
        return -1;
    }
    status = xx_regex_exec_ex(regex, text, xx_rt_strlen(text), 0, local,
                              pd ? regex_abort : NULL, pd);
    xx_regex_free(regex);
    if (status == XX_REGEX_EXEC_CANCELLED) {
        xx_rt_free(local);
        return -1;
    }
    if (status != XX_REGEX_EXEC_MATCH) {
        xx_rt_free(local);
        return 0;
    }
    *captures = local;
    *groups = count;
    return 1;
}

static int regex_test(const char *pattern, const char *flags, const char *text,
                      xx_pd_struct *pd)
{
    int32_t *captures;
    int groups;
    int result = regex_find(pattern, flags, text, &captures, &groups, pd);
    (void)groups;
    xx_rt_free(captures);
    return result;
}

static char *capture_copy(const char *text, const int32_t *captures, int group)
{
    int32_t start = captures[group * 2];
    int32_t end = captures[group * 2 + 1];
    size_t size;
    char *result;
    if (start < 0 || end < start) return NULL;
    size = (size_t)(end - start);
    result = (char *)xx_rt_malloc(size + 1);
    if (!result) return NULL;
    if (size) xx_rt_memcpy(result, text + start, size);
    result[size] = 0;
    return result;
}

static bool contains(const char *text, const char *part)
{
    size_t size = xx_rt_strlen(part);
    if (!size) return true;
    while (*text) {
        if (xx_rt_memcmp(text, part, size) == 0) return true;
        ++text;
    }
    return false;
}

static bool add_text(xx_nfd_binary_state *state, const char *token,
                     const char *type, const char *name, const char *version,
                     const char *info, xx_pd_struct *pd)
{
    return xx_nfd_pending_put(state, XX_NFD_GROUP_TEXT, token, type, name,
                              version, info, false, false, false, pd);
}

static bool add_text_format(xx_nfd_binary_state *state, xx_pd_struct *pd)
{
    size_t size = state->region->size < 4096 ? (size_t)state->region->size : 4096U;
    unsigned char *bytes = NULL;
    const char *info = "";
    size_t i;
    if (size) {
        bytes = (unsigned char *)xx_rt_malloc(size);
        if (!bytes) {
            xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate NFD text prefix");
            return false;
        }
        if (!xx_io_read_at(state->device, 0, bytes, size)) {
            xx_rt_free(bytes);
            xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot read NFD text prefix");
            return false;
        }
        for (i = 0; i < size && bytes[i] != '\n'; ++i) {}
        if (i < size) {
            info = ((i && bytes[i - 1] == '\r') ||
                    (i + 1 < size && bytes[i + 1] == '\r')) ? "CRLF" : "LF";
        } else {
            for (i = 0; i < size && bytes[i] != '\r'; ++i) {}
            if (i < size) info = "CR";
        }
        xx_rt_free(bytes);
    }
    return xx_nfd_pending_put(state, XX_NFD_GROUP_FORMAT, "PLAIN", "format",
                              state->is_utf8 ? "UTF-8 text" : "Plain text",
                              "", info, false, false, false, pd);
}

static bool scan_c_source(xx_nfd_binary_state *state, xx_pd_struct *pd)
{
    const char *text = state->header_text;
    int header_guard = regex_test("^#ifndef\\s+(\\w+)[^\\r\\n]*\\s+^#define\\s+\\1\\b", "m", text, pd);
    int pragma = regex_test("#\\s*pragma\\s+(?:once|hdrstop)", "", text, pd);
    int cpp = regex_test("^(?:class\\b|virtual\\b|public:|private:|template\\b)", "m", text, pd);
    int def = regex_test("\\sdef\\s", "", text, pd);
    int include;
    int define;
    bool is_header;
    bool is_cpp;
    bool is_c;
    if (header_guard < 0 || pragma < 0 || cpp < 0 || def < 0) return false;
    is_header = header_guard || pragma;
    is_cpp = cpp && !def;
    include = regex_test("^#include\\s+[\"<].*?[>\"]", "m", text, pd);
    if (include < 0) return false;
    is_c = is_header || is_cpp || include;
    if (include) {
        const char *cursor = text;
        while (*cursor) {
            const char *end = cursor;
            const char *dot;
            while (*end && *end != '\n' && *end != '\r') ++end;
            if ((size_t)(end - cursor) >= 9 &&
                xx_rt_memcmp(cursor, "#include", 8) == 0 &&
                (cursor[8] == ' ' || cursor[8] == '\t')) {
                dot = (const char *)xx_rt_memchr(cursor, '.', (size_t)(end - cursor));
                if (!dot) is_cpp = true;
            }
            cursor = *end ? end + 1 : end;
            if (*cursor == '\n' || *cursor == '\r') ++cursor;
            if (!*cursor) break;
        }
    }
    if (!is_c) {
        define = regex_test("^#define\\b", "m", text, pd);
        if (define < 0) return false;
        is_c = define != 0;
    }
    return !is_c || add_text(state, "CCPP", "source", is_cpp ? "C++" : "C/C++",
                             "", is_header ? "header" : "", pd);
}

static bool scan_sources(xx_nfd_binary_state *state, xx_pd_struct *pd)
{
    const char *text = state->header_text;
    int html;
    int import_record;
    int class_record;
    int def_record;
    int32_t *captures = NULL;
    int groups = 0;
    int match;
    char *version = NULL;
    if (!scan_c_source(state, pd)) return false;
    html = regex_test("^<\\s*(?:!DOCTYPE\\s+)?html\\b[^>]*>", "mi", text, pd);
    if (html < 0 || (html && !add_text(state, "HTML", "source", "HTML", "", "", pd))) return false;
    import_record = regex_test("import\\s", "", text, pd);
    class_record = regex_test("class\\s", "", text, pd);
    def_record = regex_test("\\sdef\\s", "", text, pd);
    if (import_record < 0 || class_record < 0 || def_record < 0) return false;
    if (import_record && class_record && def_record && contains(text, "self") &&
        !add_text(state, "PYTHON", "source", "Python", "", "", pd)) return false;
    if (xx_rt_strncmp(text, "<?xml", 5) == 0) {
        match = regex_find("version=\"(.*?)\"", "", text, &captures, &groups, pd);
        if (match < 0) return false;
        if (match && groups >= 1) {
            version = capture_copy(text, captures, 1);
            if (!version) {
                xx_rt_free(captures);
                xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate XML version");
                return false;
            }
        }
        if (!add_text(state, "XML", "source", "XML", version ? version : "", "", pd)) {
            xx_rt_free(version); xx_rt_free(captures); return false;
        }
        xx_rt_free(version);
        xx_rt_free(captures);
    }
    return xx_rt_strncmp(text, "<?php", 5) != 0 ||
           add_text(state, "PHP", "source", "PHP", "", "", pd);
}

static bool ascii_equal_i(const char *left, const char *right)
{
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return !*left && !*right;
}

static bool scan_shebang(xx_nfd_binary_state *state, xx_pd_struct *pd)
{
    const char *text = state->header_text;
    size_t length = 0;
    char *line;
    char *trimmed;
    int32_t *captures = NULL;
    int groups = 0;
    int match;
    char *interpreter;
    char *name;
    size_t i;
    size_t size;
    while (text[length] && text[length] != '\n') ++length;
    line = (char *)xx_rt_malloc(length + 1);
    if (!line) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate NFD script line");
        return false;
    }
    if (length) xx_rt_memcpy(line, text, length);
    line[length] = 0;
    trimmed = line;
    while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\r') ++trimmed;
    size = xx_rt_strlen(trimmed);
    while (size && (trimmed[size - 1] == ' ' || trimmed[size - 1] == '\t' || trimmed[size - 1] == '\r')) trimmed[--size] = 0;
    match = regex_find("^#!\\s*(?:.*/)?(?:env\\s+)?([^\\s]+)", "", trimmed,
                       &captures, &groups, pd);
    if (match < 0) { xx_rt_free(line); return false; }
    if (!match || groups < 1) { xx_rt_free(captures); xx_rt_free(line); return true; }
    interpreter = capture_copy(trimmed, captures, 1);
    xx_rt_free(captures);
    if (!interpreter) {
        xx_rt_free(line);
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate NFD interpreter");
        return false;
    }
    size = xx_rt_strlen(interpreter);
    if (size >= 4 && interpreter[size - 4] == '.' &&
        (interpreter[size - 3] == 'e' || interpreter[size - 3] == 'E') &&
        (interpreter[size - 2] == 'x' || interpreter[size - 2] == 'X') &&
        (interpreter[size - 1] == 'e' || interpreter[size - 1] == 'E')) interpreter[size -= 4] = 0;
    if (ascii_equal_i(interpreter, "sh")) {
        name = (char *)xx_rt_malloc(6);
        if (name) xx_rt_memcpy(name, "Shell", 6);
    } else {
        name = (char *)xx_rt_malloc(size + 1);
        if (name) {
            for (i = 0; i < size; ++i) {
                char c = interpreter[i];
                name[i] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
            }
            name[size] = 0;
            if (name[0] >= 'a' && name[0] <= 'z') name[0] = (char)(name[0] - 'a' + 'A');
        }
    }
    if (!name) {
        xx_rt_free(interpreter); xx_rt_free(line);
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate NFD interpreter name");
        return false;
    }
    if (!add_text(state, "SHELL", "script", name, "", "", pd)) {
        xx_rt_free(name); xx_rt_free(interpreter); xx_rt_free(line); return false;
    }
    xx_rt_free(name);
    xx_rt_free(interpreter);
    xx_rt_free(line);
    return true;
}

bool xx_nfd_text_scan(xx_nfd_binary_state *state, xx_pd_struct *pd)
{
    bool text;
    if (!state || !state->header_text) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Missing NFD text state");
        return false;
    }
    text = state->is_plain_text || state->is_utf8 || state->unicode_type != 0;
    if (text && (!add_text_format(state, pd) || !scan_sources(state, pd))) return false;
    return scan_shebang(state, pd);
}
