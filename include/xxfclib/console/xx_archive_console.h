/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_ARCHIVE_CONSOLE_H
#define XX_ARCHIVE_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* One profile per xxfclib archive family (the XFU_SUPPORT_FORMAT_* flags of
 * xxfclib/formats/xx_format_support.h), from the generated
 * xx_archive_console_profiles.inc: XX_ARCHIVE_CONSOLE_<flag>. A profile's
 * tool reads and creates that family only; the build must link the family's
 * component. */
typedef enum xx_archive_console_format {
    XX_ARCHIVE_CONSOLE_ALL = 0,
#define XX_ARCHIVE_PROFILE_ID(id) XX_ARCHIVE_CONSOLE_##id,
#define XX_ARCHIVE_PROFILE(id, label, reader, type, create, filter)
#include "xxfclib/console/xx_archive_console_profiles.inc"
#undef XX_ARCHIVE_PROFILE
#undef XX_ARCHIVE_PROFILE_ID
    XX_ARCHIVE_CONSOLE_COUNT
} xx_archive_console_format;

typedef struct xx_archive_console_config {
    const char *application_name;
    const char *application_version;
    xx_archive_console_format format;
} xx_archive_console_config;

/* Shared XFileUnpacker command-line interface. Configuration strings are
 * borrowed for this call. Format profiles constrain both reads and writes;
 * ALL exposes the complete unpacker engine. Returns 0 for success, 1 for
 * partial failure/cancellation, or 2 for usage, format or I/O failure.
 *
 * Process entry point: owns standard-console setup (including Windows UTF-8
 * arguments and SIGXFSZ handling). Invoke once from main, not concurrently. */
int xx_archive_console_main(int argc, char **argv,
                            const xx_archive_console_config *config);

#ifdef __cplusplus
}
#endif

#endif
