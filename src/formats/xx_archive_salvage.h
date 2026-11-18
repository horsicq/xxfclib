/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Internal salvage helpers for XX_META_ID_OPT_FORCE.
 *
 * A reader opts in at the one place where a member has decoded bytes in hand
 * and then fails a completeness check (declared size, checksum, cut stream):
 * instead of discarding them it keeps exactly those bytes at the member's
 * path and notes the fact (xx_io_extract_salvaged). The member still fails:
 * every helper leaves the reader's own result untouched, so the caller keeps
 * returning false and an archive with a salvaged member never reads as clean.
 *
 * Rules shared by every helper, so that readers cannot get them wrong:
 * - nothing is kept unless the operation set XX_META_ID_OPT_FORCE to true;
 * - nothing is kept after a cancel, or after a filesystem change failed
 *   (xx_io_extract_errno): salvage is for damaged data only;
 * - an empty prefix is not kept;
 * - bytes are never padded to the declared size, and the caller passes only
 *   what the decoder produced before it reported an error.
 * Encrypted members are the caller's to exclude: a wrong password decodes to
 * noise that no completeness check tells from damage.
 */
#ifndef XX_ARCHIVE_SALVAGE_H
#define XX_ARCHIVE_SALVAGE_H
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

/* Whether a failed member may be salvaged now: the operation asked for it
 * (options: the record state's list), it was not cancelled, and no filesystem
 * change has failed for this member. */
static XXFC_MAYBE_UNUSED bool xx_archive_salvage_wanted(const xx_list_s *options, xx_pd_struct *pd)
{
    size_t index;

    if (!options || (pd && xx_pd_is_stopped(pd)) || xx_io_extract_errno()) return false;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == XX_META_ID_OPT_FORCE) return xx_var_get_bool(&meta->var);
    }
    return false;
}

/* The member's file at path already holds `written` decoded bytes (a reader
 * that decodes straight into it): keep it. False when there is nothing worth
 * keeping; the caller then removes the file as it always did. */
static XXFC_MAYBE_UNUSED bool xx_archive_salvage_keep(uint64_t written, int64_t declared)
{
    if (!written || xx_io_extract_errno()) return false;
    xx_io_extract_salvaged(written, declared);
    return true;
}

/* The decoded bytes are in a staged file next to path: publish it as the
 * member. overwrite follows the reader's own policy, so a file that would not
 * have been replaced by a whole member is not replaced by a part of one.
 * False when nothing was published; the caller then removes the stage. */
static XXFC_MAYBE_UNUSED bool xx_archive_salvage_publish(const char *stage, const char *path, bool overwrite, uint64_t written, int64_t declared)
{
    if (!stage || !path || !written || xx_io_extract_errno()) return false;
    if (!xx_io_file_replace_a(stage, path, overwrite)) return false;
    xx_io_extract_salvaged(written, declared);
    return true;
}

/* The decoded bytes are in memory and no file was opened yet: write exactly
 * size bytes to path. A failed write removes the file and keeps nothing. */
static XXFC_MAYBE_UNUSED bool xx_archive_salvage_buffer(const char *path, const void *data, size_t size, int64_t declared)
{
    xx_io_device *output;
    size_t done = 0U;
    bool result = true;

    if (!path || !data || !size || xx_io_extract_errno()) return false;
    output = xx_io_file_open(path, "wb");
    if (!output) return false;
    while (done < size) {
        ssize_t sent = xx_io_write(output, (const uint8_t *)data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) {
            result = false;
            break;
        }
        done += (size_t)sent;
    }
    if (xx_io_close(output) != 0) result = false;
    if (!result) {
        (void)xx_io_file_remove_a(path);
        return false;
    }
    xx_io_extract_salvaged((uint64_t)size, declared);
    return true;
}
#endif
