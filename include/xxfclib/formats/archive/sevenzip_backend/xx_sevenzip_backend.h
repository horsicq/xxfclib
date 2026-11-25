/* SPDX-License-Identifier: MIT. Framed interface to a separately licensed 7-Zip engine. */
#ifndef XX_SEVENZIP_BACKEND_H
#define XX_SEVENZIP_BACKEND_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_sevenzip_backend_status {
    XX_SEVENZIP_BACKEND_OK = 0,
    XX_SEVENZIP_BACKEND_UNAVAILABLE,
    XX_SEVENZIP_BACKEND_CANCELLED,
    XX_SEVENZIP_BACKEND_TIMEOUT,
    XX_SEVENZIP_BACKEND_IO,
    XX_SEVENZIP_BACKEND_FORMAT,
    XX_SEVENZIP_BACKEND_LIMIT,
    XX_SEVENZIP_BACKEND_PASSWORD,
    XX_SEVENZIP_BACKEND_UNSUPPORTED
} xx_sevenzip_backend_status;
typedef struct xx_sevenzip_backend_entry {
    uint32_t index;
    const char *path;           /* Borrowed UTF-8 name during callback. */
    uint64_t size, packed_size; /* UINT64_MAX when unknown. */
    int64_t mtime;
    bool directory, encrypted;
} xx_sevenzip_backend_entry;
typedef bool (*xx_sevenzip_backend_entry_fn)(void *, const xx_sevenzip_backend_entry *);
typedef struct xx_sevenzip_backend_options {
    const char *helper_path; /* NULL: xfu_sevenzip_helper.exe beside current executable. */
    const char *password;
    const char *source_path; /* Optional original path; sibling volumes are opened read-only. */
    bool start_only;         /* false: allow embedded scans; true: archive must start at base. */
    char *detected_handler;
    size_t detected_handler_capacity; /* Optional LIST result. */
    uint64_t memory_limit;            /* zero:256MiB; minimum256KiB, maximum256MiB. */
    uint64_t max_member_size;         /* zero is a real zero ceiling; UINT64_MAX unlimited. */
    unsigned timeout_ms;              /* zero:60seconds per entire operation. */
    xx_pd_struct *pd;
    xx_sevenzip_backend_status *status;
} xx_sevenzip_backend_options;
/* No filenames or temporary output files are passed to the helper. Input is
 * borrowed, seekable IO; its cursor is restored on every exit. handler is the
 * exact upstream name (e.g. "APFS", "7z") or NULL for automatic selection. */
XXFC_API bool xx_sevenzip_backend_list(xx_io_device *, int64_t base, int64_t length, const char *handler, const xx_sevenzip_backend_options *,
                                       xx_sevenzip_backend_entry_fn, void *);
/* index and expected_size come from LIST; UINT64_MAX is an unknown size.
 * NULL output performs full decompression/integrity TEST through a RAM sink. */
XXFC_API bool xx_sevenzip_backend_read(xx_io_device *, int64_t base, int64_t length, const char *handler, uint32_t index, uint64_t expected_size, xx_io_device *output,
                                       const xx_sevenzip_backend_options *);
#ifdef __cplusplus
}
#endif
#endif
