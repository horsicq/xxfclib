/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SETTINGS_H
#define XX_SETTINGS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_settings_s xx_settings;

typedef enum xx_settings_format_e {
    XX_SETTINGS_FORMAT_MEMORY = 0,
    XX_SETTINGS_FORMAT_INI = 1,
    XX_SETTINGS_FORMAT_NATIVE = 2
} xx_settings_format_t;

/** @brief Persistent storage policy for an application. */
typedef enum xx_settings_mode_e {
    XX_SETTINGS_MODE_AUTO = 0,     /**< A regular PORTABLE marker beside the executable selects INI. */
    XX_SETTINGS_MODE_PORTABLE = 1, /**< Always use an INI beside the executable. */
    XX_SETTINGS_MODE_NATIVE = 2    /**< Always use the current user's platform settings. */
} xx_settings_mode_t;

typedef enum xx_settings_value_type_e {
    XX_SETTINGS_VALUE_NONE = 0,
    XX_SETTINGS_VALUE_BOOL = 1,
    XX_SETTINGS_VALUE_INT64 = 2,
    XX_SETTINGS_VALUE_UINT64 = 3,
    XX_SETTINGS_VALUE_DOUBLE = 4,
    XX_SETTINGS_VALUE_STRING = 5,
    XX_SETTINGS_VALUE_BYTES = 6,
    XX_SETTINGS_VALUE_STRING_LIST = 7,
    XX_SETTINGS_VALUE_OPAQUE = 8 /**< Consumer-defined serialized data. */
} xx_settings_value_type_t;

typedef struct xx_settings_value_s {
    xx_settings_value_type_t type;
    union {
        bool boolean;
        int64_t integer;
        uint64_t unsigned_integer;
        double real;
        struct {
            const char *data;
            size_t size;
        } buffer; /**< UTF-8 string or binary data. */
        struct {
            const char *const *items;
            size_t count;
        } list;
    } data;
} xx_settings_value;

/** @brief Create an empty store. Creation does not read or write persistent storage. */
XXFC_API xx_settings *xx_settings_create_memory(void);
XXFC_API xx_settings *xx_settings_create_ini(const char *utf8_path);
/**
 * @brief Create current-user native settings for an organization/application.
 * Windows: HKCU/Software/organization/application. macOS: CFPreferences domain
 * organization.application. Linux/FreeBSD: XDG_CONFIG_HOME/organization/application.conf,
 * falling back to HOME/.config. Names must be nonempty path components.
 */
XXFC_API xx_settings *xx_settings_create_native(const char *organization, const char *application);
/**
 * @brief Check for a portable application marker beside the executable.
 * @p application_directory must be the absolute UTF-8 executable directory;
 * the current working directory is never searched. A regular file named
 * "PORTABLE" or legacy "portable" selects portable mode; its contents are ignored.
 * Directories with these names do not select portable mode. @p portable is always
 * initialized to false, including on failure. Invalid arguments, allocation or
 * filesystem query failures return an error rather than selecting native mode.
 */
XXFC_API xxfc_status_t xx_settings_is_portable(const char *application_directory, bool *portable);
/**
 * @brief Create application settings using the selected portable/native policy.
 * @p application_directory is the absolute UTF-8 directory containing the executable,
 * supplied by the caller; the current working directory is never searched. It is
 * required for AUTO/PORTABLE and ignored for NATIVE. Portable settings use
 * application_directory/application.ini. AUTO selects portable storage only when
 * xx_settings_is_portable finds a regular "PORTABLE" or legacy "portable" file.
 * Creation does not load or save. Invalid input, allocation failure or a failed
 * marker query returns NULL; a portable failure never switches to native storage.
 * Explicit paths, including environment overrides, can use xx_settings_create_ini.
 */
XXFC_API xx_settings *xx_settings_create_application(const char *application_directory, const char *organization, const char *application, xx_settings_mode_t mode);
/** @brief Destroy the store; detach it if it is the global settings pointer. Does not save. */
XXFC_API void xx_settings_destroy(xx_settings *settings);

/**
 * @brief Write files in the Qt QSettings INI dialect so Qt 5 and Qt 6 can share them.
 * When enabled, INI and POSIX native files are written with Qt escaping (non-ASCII and
 * control characters as \x<UTF-16 code unit>, quoting, a leading '@' doubled) and a new
 * file gets Qt-style [Group] sections without the "; xxfclib settings v1" header; an
 * existing v1 file loses that header on the next save and its entries are rewritten in
 * the Qt dialect. Saving to any other existing file rewrites only the changed entries and
 * keeps every other byte; a file without the v1 header (which may follow comment lines) is
 * always read with QSettings escaping rules and patched in the Qt dialect, even when this
 * flag is off. Only a Qt-compatible store reads it exactly as QSettings does: a section line
 * without ']' is tolerated and unquoted commas make a list, read as a STRING_LIST (Qt's
 * one-item "@Variant(...)" and empty "@Invalid()" lists stay text); other stores reject
 * that section line and keep commas as written ("Ctrl+,"). As in QSettings, ';'
 * starts a comment and a "#name=value" line is a key. Not matched: QSettings' precedence
 * between duplicate keys in different sections and its case-insensitive keys on Windows;
 * xxfclib reads and patches the last occurrence of a key exactly as spelled. The Windows
 * registry and macOS preferences are unaffected. Typed values (bool, numbers, lists, bytes)
 * keep the xxfclib encoding, so callers that share a store with Qt should write plain strings.
 * Call before xx_settings_load/save. Returns XXFC_ERR_INVALID_ARG when @p settings is NULL.
 */
XXFC_API xxfc_status_t xx_settings_set_qt_compatible(xx_settings *settings, bool enabled);

XXFC_API xx_settings_format_t xx_settings_get_format(const xx_settings *settings);
XXFC_API const char *xx_settings_get_location(const xx_settings *settings);
XXFC_API const char *xx_settings_get_organization(const xx_settings *settings);
XXFC_API const char *xx_settings_get_application(const xx_settings *settings);
/** @brief Borrow a value until the store is modified. NULL means missing/disabled. */
XXFC_API const xx_settings_value *xx_settings_get(const xx_settings *settings, const char *key);
/** @brief Copy a value into the store. Slash-separated keys identify groups. */
XXFC_API xxfc_status_t xx_settings_set(xx_settings *settings, const char *key, const xx_settings_value *value);
XXFC_API xxfc_status_t xx_settings_remove(xx_settings *settings, const char *key);
XXFC_API void xx_settings_clear(xx_settings *settings);
XXFC_API size_t xx_settings_count(const xx_settings *settings);
XXFC_API const char *xx_settings_key_at(const xx_settings *settings, size_t index);

/** @brief Reload transactionally; missing storage is an empty store. Failure preserves current values. */
XXFC_API xxfc_status_t xx_settings_load(xx_settings *settings);
/**
 * @brief Save changed keys, preserving unrelated stored keys. INI writes use atomic replacement.
 * Saving to an existing INI file rewrites only the lines of changed keys (a key set to the
 * value it already has keeps its bytes); comments, order and values written by Qt stay as
 * they are. On Windows, native (registry) keys are matched without regard to ASCII case.
 * NULL means disabled and is a successful no-op for load/save. Synchronize access externally.
 */
XXFC_API xxfc_status_t xx_settings_save(xx_settings *settings);
XXFC_API bool xx_settings_is_writable(const xx_settings *settings);

#ifdef __cplusplus
}
#endif
#endif
