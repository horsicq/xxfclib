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

/* xx_scan_result.c - result ownership, merging and output formatting. */

#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/scan/xx_scan_console.h"
#include "xxfclib/terminal/xx_terminal.h"
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/rt/xx_rt.h"
#undef fileType /* xx_format.h compatibility alias; scan results use file_type. */
#include <limits.h>

void xx_scan_result_free(xx_scan_result *pResult)
{
    int i = 0;

    if (!pResult) return;

    for (i = 0; i < pResult->nCount; i++) {
        xx_rt_free((void *)pResult->pRecords[i].type);
        xx_rt_free((void *)pResult->pRecords[i].name);
        xx_rt_free((void *)pResult->pRecords[i].version);
        xx_rt_free((void *)pResult->pRecords[i].info);
        xx_rt_free((void *)pResult->pRecords[i].format_id);
        xx_rt_free((void *)pResult->pRecords[i].archive_member);
        xx_rt_free((void *)pResult->pRecords[i].source_ref);
    }

    xx_rt_free(pResult->pRecords);

    for (i = 0; i < pResult->nErrorCount; i++) {
        xx_rt_free(pResult->ppErrors[i]);
    }

    xx_rt_free(pResult->ppErrors);
    xx_rt_free(pResult->pFileName);
    xx_rt_memset(pResult, 0, sizeof(*pResult));
}

static bool xx_scan_copy_string(const char *source, const char **destination)
{
    size_t size;
    char *copy;
    *destination = NULL;
    if (!source) return true;
    size = xx_rt_strlen(source) + 1;
    copy = (char *)xx_rt_malloc(size);
    if (!copy) return false;
    xx_rt_memcpy(copy, source, size);
    *destination = copy;
    return true;
}

bool xx_scan_result_add_error(xx_scan_result *result, const char *text, xx_pd_struct *pd)
{
    const char *copy;
    char **errors;
    size_t count;

    if (!result || !text || result->nErrorCount < 0 || result->nErrorCount == INT_MAX ||
        (result->nErrorCount && !result->ppErrors)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan result error arguments");
        return false;
    }
    count = (size_t)result->nErrorCount + 1;
    if (count > SIZE_MAX / sizeof(char *)) goto out_of_memory;
    if (!xx_scan_copy_string(text, &copy)) goto out_of_memory;
    errors = (char **)xx_rt_realloc(result->ppErrors, count * sizeof(char *));
    if (!errors) {
        xx_rt_free((void *)copy);
        goto out_of_memory;
    }
    result->ppErrors = errors;
    result->ppErrors[result->nErrorCount++] = (char *)copy;
    return true;

out_of_memory:
    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot add scan result error");
    return false;
}

bool xx_scan_append_result(xx_scan_engine *engine, xx_scan_result *destination, const xx_scan_result *source,
                           int64_t source_offset, xx_pd_struct *pd)
{
    xx_scan_result copy = {0};
    xx_scan_record *records;
    char **errors;
    int count;
    int error_count;
    int i;
    (void)engine;
    if (!destination || !source || destination == source || source_offset < 0 ||
        source->nCount < 0 || source->nErrorCount < 0 || destination->nCount < 0 || destination->nErrorCount < 0 ||
        (source->nCount && !source->pRecords) || (source->nErrorCount && !source->ppErrors) ||
        (destination->nCount && !destination->pRecords) || (destination->nErrorCount && !destination->ppErrors) ||
        source->nCount > INT_MAX - destination->nCount || source->nErrorCount > INT_MAX - destination->nErrorCount) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan result append arguments");
        return false;
    }
    if (xx_pd_is_stopped(pd)) return false;
    count = destination->nCount + source->nCount;
    error_count = destination->nErrorCount + source->nErrorCount;
    if ((size_t)count > SIZE_MAX / sizeof(xx_scan_record) || (size_t)error_count > SIZE_MAX / sizeof(char *)) goto out_of_memory;
    if (source->nCount) {
        copy.pRecords = (xx_scan_record *)xx_rt_calloc((size_t)source->nCount, sizeof(xx_scan_record));
        if (!copy.pRecords) goto out_of_memory;
    }
    for (i = 0; i < source->nCount; ++i) {
        const xx_scan_record *input = &source->pRecords[i];
        xx_scan_record *output = &copy.pRecords[i];
        if (xx_pd_is_stopped(pd)) goto failed;
        if (input->offset < 0 || input->offset > INT64_MAX - source_offset) {
            xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Scan record offset overflows");
            goto failed;
        }
        *output = *input;
        output->offset += source_offset;
        output->type = output->name = output->version = output->info = output->format_id = output->archive_member = output->source_ref = NULL;
        copy.nCount++;
        if (!xx_scan_copy_string(input->type, &output->type) || !xx_scan_copy_string(input->name, &output->name) ||
            !xx_scan_copy_string(input->version, &output->version) || !xx_scan_copy_string(input->info, &output->info) ||
            !xx_scan_copy_string(input->format_id, &output->format_id) ||
            !xx_scan_copy_string(input->archive_member, &output->archive_member) ||
            !xx_scan_copy_string(input->source_ref, &output->source_ref)) goto out_of_memory;
    }
    if (source->nErrorCount) {
        copy.ppErrors = (char **)xx_rt_calloc((size_t)source->nErrorCount, sizeof(char *));
        if (!copy.ppErrors) goto out_of_memory;
    }
    for (i = 0; i < source->nErrorCount; ++i) {
        const char *error;
        if (!xx_scan_copy_string(source->ppErrors[i], &error)) goto out_of_memory;
        copy.ppErrors[copy.nErrorCount++] = (char *)error;
    }
    if (xx_pd_is_stopped(pd)) goto failed;
    if (source->nCount) {
        records = (xx_scan_record *)xx_rt_realloc(destination->pRecords, (size_t)count * sizeof(xx_scan_record));
        if (!records) goto out_of_memory;
        destination->pRecords = records;
        destination->nCapacity = count;
    }
    if (source->nErrorCount) {
        errors = (char **)xx_rt_realloc(destination->ppErrors, (size_t)error_count * sizeof(char *));
        if (!errors) goto out_of_memory;
        destination->ppErrors = errors;
    }
    if (source->nCount) xx_rt_memcpy(destination->pRecords + destination->nCount, copy.pRecords, (size_t)source->nCount * sizeof(xx_scan_record));
    if (source->nErrorCount) xx_rt_memcpy(destination->ppErrors + destination->nErrorCount, copy.ppErrors, (size_t)source->nErrorCount * sizeof(char *));
    destination->nCount = count;
    destination->nErrorCount = error_count;
    if (!source_offset) {
        destination->file_type = source->file_type;
        destination->compatibility_file_type = source->compatibility_file_type;
        destination->engine_id = source->engine_id;
        destination->nFileSize = source->nFileSize;
    }
    xx_rt_free(copy.pRecords);
    xx_rt_free(copy.ppErrors);
    return true;
out_of_memory:
    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot copy scan results");
failed:
    xx_scan_result_free(&copy);
    return false;
}

