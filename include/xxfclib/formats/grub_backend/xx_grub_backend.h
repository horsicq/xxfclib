/* SPDX-License-Identifier: MIT. Pipe interface to a separately licensed helper.
 * No GRUB definitions or GPL implementation are linked into this library.
 */
#ifndef XX_GRUB_BACKEND_H
#define XX_GRUB_BACKEND_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_grub_backend_entry {
 const char *path; /* borrowed, absolute filesystem path during callback */
 uint64_t size;int64_t mtime;bool directory,has_mtime;
} xx_grub_backend_entry;
typedef bool (*xx_grub_backend_entry_fn)(void *,const xx_grub_backend_entry *);
typedef enum xx_grub_backend_status {XX_GRUB_BACKEND_OK=0,XX_GRUB_BACKEND_UNAVAILABLE,
 XX_GRUB_BACKEND_CANCELLED,XX_GRUB_BACKEND_TIMEOUT,XX_GRUB_BACKEND_IO,
 XX_GRUB_BACKEND_FORMAT,XX_GRUB_BACKEND_LIMIT} xx_grub_backend_status;
typedef struct xx_grub_backend_options {
 const char *helper_path; /* NULL: xfu_grub_fs_helper beside current executable */
 uint64_t memory_limit; /* zero:256MiB; capped256MiB, minimum64KiB */
 uint64_t max_member_size; /* zero is a real zero ceiling; UINT64_MAX unlimited */
 unsigned timeout_ms; /* zero:60seconds, per whole helper operation */
 xx_pd_struct *pd;
 xx_grub_backend_status *status; /* optional; always set when provided */
} xx_grub_backend_options;
/* The source and callback/output are borrowed; source cursor is restored.
 * length<0 uses the remaining source. No output paths or temp files are used.
 * LIST returns false on any failed member-size lookup or incomplete walk.
 */
XXFC_API bool xx_grub_backend_list(xx_io_device *source,int64_t base,int64_t length,
 const char *filesystem,const xx_grub_backend_options *options,
 xx_grub_backend_entry_fn callback,void *user);
/* expected_size comes from a successful LIST. A NULL output still consumes
 * and checks all returned file bytes. Output-device cursor is not restored.
 */
XXFC_API bool xx_grub_backend_read(xx_io_device *source,int64_t base,int64_t length,
 const char *filesystem,const char *path,uint64_t expected_size,
 xx_io_device *output,const xx_grub_backend_options *options);
#ifdef __cplusplus
}
#endif
#endif
