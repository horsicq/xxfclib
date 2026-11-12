/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XFILEUNPACKER_CORE_H
#define XFILEUNPACKER_CORE_H

#include <xxfclib/formats/xx_format_support.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xxfclib/data/xx_pd.h>
#include <xxfclib/xxfc_defs.h>

typedef enum xfu_command {
    XFU_COMMAND_NONE = 0,
    XFU_COMMAND_EXTRACT,
    XFU_COMMAND_LIST,
    XFU_COMMAND_TEST,
    XFU_COMMAND_ADD
} xfu_command;

typedef struct xfu_property {
    const char *name;
    const char *value;
} xfu_property;

typedef enum xfu_encryption_status {
    XFU_ENCRYPTION_DETECTED = 1,
    XFU_ENCRYPTION_PASSWORD_REQUIRED,
    /* A supplied password did not unlock the data. This can also indicate
     * damaged encrypted data; it is not proof that the password is wrong. */
    XFU_ENCRYPTION_PASSWORD_REJECTED
} xfu_encryption_status;

/* Names and messages are UTF-8; callback arguments are borrowed for the call. */
typedef struct xfu_entry {
    const char *name;
    int64_t packed_size;
    int64_t unpacked_size; /* -1 when the format does not report a size. */
    bool is_directory;
    char modified[20]; /* Display timestamp, or empty when not reported. */
    char attributes[24]; /* Format attributes as hexadecimal, or empty. */
    const xfu_property *properties; /* All reader metadata and record fields. */
    size_t property_count;
    /* Optional raw NUL-terminated credential bytes recovered and validated
     * by the reader; this field can contain control or non-UTF-8 bytes.
     * Borrowed only during the entry callback; callers must copy to retain it.
     * NULL means no recovered credential, independently of caller passwords. */
    const char *embedded_password;
    /* test_result only, for a failed member: why, in a few words ("already
     * exists; not replaced", "permission denied", "unsafe member path"), or
     * NULL when nothing tells (a defect of its data). Borrowed for the call. */
    const char *failure;
    bool is_encrypted; /* This member's payload requires decryption. */
} xfu_entry;

typedef struct xfu_callbacks {
    void *user;
    void (*log)(void *user, bool error, const char *line);
    void (*entry)(void *user, const xfu_entry *entry);
    /* total == 0 means that the member count is not known yet. */
    void (*progress)(void *user, uint64_t completed, uint64_t total,
                     const char *name);
    bool (*cancelled)(void *user);
    /* Detected interpretations, generic first, and the type used to open.
     * Called before parsing; the array is borrowed for this call. A read
     * inside an envelope or outer stream lists those layers first; any type
     * listed can be selected through file_type. */
    void (*file_types)(void *user, const xx_file_type_t *types, size_t count,
                       xx_file_type_t selected);
    /* TEST only: the result of decoding/verifying one member in memory.
     * Arguments are borrowed for this call. The native readers do not report
     * a member cut short by Cancel here (the log says "-- cancelled" for it);
     * other read paths may report it as a failure. */
    void (*test_result)(void *user, const xfu_entry *entry, bool success);
    /* TEST only: monotonic whole-archive percentage (0..100). Reaches 100
     * after all members have been processed, including failed members;
     * cancellation and premature iterator failure do not reach 100. */
    void (*test_progress)(void *user, unsigned percent);
    /* LIST/TEST/EXTRACT, optional: called once when the member walk ends.
     * complete is true when iteration reached the archive's end, so every
     * member was read (some may still have failed); false after a cancel,
     * the record limit, an iterator error or an incomplete archive. */
    void (*walk_end)(void *user, bool complete);
    /* LIST/TEST/EXTRACT, optional: the input is not an archive. type is the
     * interpretation it was read as: Binary, or a recognised format without
     * members (Text, MSDOS, ...) or without a built-in reader (PE64, ELF64,
     * ...). The run then logs that the type "cannot be opened as an archive"
     * and returns 2. */
    void (*not_archive)(void *user, xx_file_type_t type);
    /* Actionable unpack interpretations, outer layers first. Values belong
     * to the independent XX_HANDLE_TYPE_* namespace; ordinary Binary/PE/COM
     * inspection types are never offered. Borrowed for this call. */
    void (*unpack_handle_types)(void *user, const xx_handle_type_t *types,
                                size_t count, xx_handle_type_t selected);
    /* Reader-confirmed encryption, independent of localized log text.
     * DETECTED does not imply failure: an encrypted archive can be listed.
     * REQUIRED/REJECTED mean an attempted operation could not read encrypted
     * metadata or a member. member_name is NULL for archive metadata.
     * Notifications are bounded per archive layer; member flags remain on
     * every entry. Arguments are borrowed, and callbacks run on the worker. */
    void (*encryption)(void *user, xfu_encryption_status status,
                       const char *member_name);
} xfu_callbacks;

typedef struct xfu_request {
    xfu_command command;
    const char *archive_path;
    /* NULL uses "." for extraction. Every reader follows one policy there:
     * a file already at a member's path is replaced (a link there is
     * replaced, not followed), and the member's log line says so ("  name
     * -- replaced an existing file"). */
    const char *output_dir;
    const char *const *files;
    size_t file_count;
    xfu_callbacks callbacks;
    /* Optional caller-initialized monitor. xx_pd_stop() also cancels decoding
     * inside a member; callbacks run synchronously on the calling thread.
     * With a cancelled callback, a read that decodes one compressed stream
     * shows how much of it has been read as a level ("Reading archive"). */
    xx_pd_struct *progress_state;
    /* EXTRACT only: sorted, unique zero-based reader record indexes. Selection
     * is explicit: an empty/invalid selection never falls back to all members. */
    bool extract_selected;
    const size_t *selected_records;
    size_t selected_record_count;
    /* UNKNOWN (zero) detects automatically. Other values select an exact
     * interpretation from the detected chain; BINARY cannot open an archive.
     * ADD chooses its writer from the destination extension unless overridden. */
    xx_file_type_t file_type;
    /* Optional UTF-8 password, borrowed for the duration of xfu_run(). */
    const char *password;
    /* Optional explicit reader factory name, including hxc-raw:PROFILE.
     * Its grammar is still validated. Cannot combine with file_type. */
    const char *reader_name;
    /* ADD: optional explicit writer format and outer output filter.
     * NULL selects the destination extension/defaults. Borrowed strings. */
    const char *archive_format;
    const char *archive_filter;
    /* Format-specific reader or writer options: key=value pairs separated
     * by ';', such as "cpio:hdrcharset=CP437". */
    const char *archive_options;
    /* ADD: WIM accepts stored level 0 in this build; other writers validate
     * their method and range. */
    const char *compression_method;
    int compression_level;
    bool compression_level_set;
    uint64_t volume_size; /* ADD split: bytes per part; zero uses 10 MiB. */
    /* UNKNOWN detects the unpack method automatically. Any other value is
     * an exact unpack interpretation and must match this input. Cannot be
     * combined with reader_name; file_type remains a compatibility selector. */
    xx_handle_type_t handle_type;
} xfu_request;

xfu_command xfu_parse_command(const char *text);
/* 0: success, 1: partial failure/cancellation, 2: usage/open/format failure.
 * The core never prints; diagnostics only go to the attached log callback. */
int xfu_run(const xfu_request *request);

/* archive_options helpers: whether `pair` is one of its pairs, and the first
 * pair outside `known` (its length in *length), or NULL. */
bool xfu_option_listed(const char *options, const char *pair);
const char *xfu_option_unknown(const char *options, const char *const *known,
                               size_t count, size_t *length);

#endif
