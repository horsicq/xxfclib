/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xxfc_unpack -- unpack archives with xxfclib, driven like 7-Zip.
 *
 * The command letters are 7-Zip's, because that is what fingers already know:
 *
 *   x   extract, keeping the paths stored in the archive
 *   l   list what is inside
 *   t   test -- extract to a scratch directory and throw it away
 *   a   add files to a new archive
 *
 * 7-Zip's `e` (extract flattened) is deliberately absent rather than aliased
 * to `x`: xxfclib places extracted files itself from the names in the
 * archive, and this program has no say in the layout. Offering `e` would mean
 * promising something it does not do.
 */

#include "xxfc_readers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xxfclib/strings/xx_string.h>
#include <xxfclib/formats/nitroplus_npa/xx_nitroplus_npa.h>
#include <xxfclib/formats/qlie_pack/xx_qlie_pack.h>

#include "xxfc_duplicate_paths.inc"

typedef enum {
    CMD_NONE = 0,
    CMD_EXTRACT,
    CMD_LIST,
    CMD_TEST,
    CMD_ADD
} command_t;

/* A listing this long is a malformed or hostile archive rather than a real
 * one; stop walking and say so instead of spinning. */
#define RECORD_LIMIT 1000000

static void usage(FILE *out, const char *program) {
    fprintf(out,
            "xxfc_unpack -- unpack archives with xxfclib\n"
            "\n"
            "Usage: %s <command> <archive> [arguments]\n"
            "\n"
            "Commands (7-Zip letters):\n"
            "  formats                   Show available reader names\n"
            "  x <archive> [-o<dir>]     Extract with full paths\n"
            "  l <archive>               List contents\n"
            "  t <archive>               Test: extract to a scratch dir, then discard\n"
            "  a <archive> <file>...     Add files to a new archive\n"
            "\n"
            "Options:\n"
            "  --format=<name> Select a reader for raw or ambiguous images\n"
            "                  CP/M diskdefs: cpm:<diskdef-name>\n"
            "  --password=<value> Supply an archive password or reader-specific hex key\n"
            "  --npa-payload=auto|stored|zlib Select Nitroplus NPA member encoding\n"
            "  --qlie-legacy=1|2|3 Select a Qlie PACK 1.0 layout\n"
            "  -o<dir>   Output directory for x (default: the current directory)\n"
            "\n"
            "Notes:\n"
            "  `a` writes the container the archive's extension names, and only\n"
            "  the formats xxfclib can write: .tar .tar.gz .tar.bz2 .tar.xz\n"
            "  .tar.zst .tar.lz4 .zip .cpio\n",
            program);
}

static command_t parse_command(const char *text) {
    if (!text || !text[0] || text[1]) return CMD_NONE;
    switch (text[0]) {
        case 'x': case 'X': return CMD_EXTRACT;
        case 'l': case 'L': return CMD_LIST;
        case 't': case 'T': return CMD_TEST;
        case 'a': case 'A': return CMD_ADD;
        default: return CMD_NONE;
    }
}

/* Names arrive as either narrow or wide text depending on what the container
 * stores, so both are asked for before giving up. The returned pointer is
 * either borrowed from the record or owned by the caller; *owned says which. */
static const char *record_name(const xx_archive_record *record, char **owned) {
    const char *name = xx_archive_record_get_original_name(record);
    const wchar_t *wide;

    *owned = NULL;
    if (name && name[0]) return name;

    wide = xx_archive_record_get_original_name_w(record);
    if (wide && wide[0]) {
        char *utf8 = xx_str_unicode_to_utf8(wide);
        if (utf8 && utf8[0]) {
            *owned = utf8;
            return utf8;
        }
        xx_str_free(utf8);
    }
    return "<unnamed>";
}

/* ------------------------------------------------------------- reading -- */

static bool ends_with(const char *text, const char *suffix);

/* Walks the archive once. `unpack_to` NULL lists without extracting. */
static int walk(const char *archive_path, const char *unpack_to, bool quiet,
                const char *reader_name, const char *password, int npa_payload,
                int qlie_profile) {
    xx_io_device *device = xx_io_file_open(archive_path, "rb");
    xxfc_opened opened = {0};
    xx_archive_record_state *state;
    xx_list_t options;
    long long total = 0, failed = 0;
    int status = 0;
    bool selected = false;
    xxfc_seen_paths seen = {0};

    if (!device) {
        fprintf(stderr, "cannot open %s\n", archive_path);
        return 2;
    }

    if (reader_name) {
        selected = xxfc_open_named(&opened, device, 0, reader_name);
    } else {
        /* AnaDisk has no signature. Probe it only for its own extension,
         * and only when the whole stream passes the strict sector checks. */
        if (ends_with(archive_path, ".ana") &&
            xxfc_open_named(&opened, device, 0, "pce_anadisk")) {
            xx_pce_anadisk_set_conservative_probe(
                (xx_pce_anadisk *)opened.format, true);
            if (xx_format_is_valid(opened.format, NULL)) selected = true;
            else xxfc_close(&opened);
        }
        if (!selected) selected = xxfc_open(&opened, device, 0);
    }
    if (!selected) {
        if (reader_name) {
            fprintf(stderr, "%s: unknown reader %s\n", archive_path, reader_name);
            xx_io_close(device);
            return 2;
        }
        if (opened.type == XX_FILE_TYPE_UNKNOWN) {
            fprintf(stderr, "%s: not a recognised format\n", archive_path);
        } else {
            fprintf(stderr, "%s: %s is recognised but no reader is built in\n",
                    archive_path,
                    xx_format_file_type_to_string(opened.type));
        }
        xx_io_close(device);
        return 2;
    }

    if (password) {
        xx_var value;
        bool set;
        xx_var_init(&value);
        set = xx_var_set_str(&value, password) &&
              xx_format_set_extra_parameter(opened.format, XX_META_ID_OPT_PASSWORD, &value);
        xx_var_cleanup(&value);
        if (!set) { status = 2; goto done; }
    }
    if (npa_payload >= 0) {
        xx_var value;
        bool set;
        if (opened.type != XX_FILE_TYPE_NITROPLUS_NPA) {
            fprintf(stderr, "--npa-payload requires the Nitroplus NPA reader\n");
            status = 2; goto done;
        }
        xx_var_init(&value); xx_var_set_u64(&value, (uint64_t)npa_payload);
        set = xx_format_set_extra_parameter(opened.format, XX_NITROPLUS_NPA_OPT_PAYLOAD_MODE, &value);
        xx_var_cleanup(&value);
        if (!set) { status = 2; goto done; }
    }
    if (qlie_profile) {
        xx_var value;
        bool set;
        if (opened.type != XX_FILE_TYPE_QLIE_PACK) {
            fprintf(stderr, "--qlie-legacy requires the Qlie PACK reader\n");
            status = 2; goto done;
        }
        xx_var_init(&value); xx_var_set_u64(&value, (uint64_t)qlie_profile);
        set = xx_format_set_extra_parameter(opened.format,
                                            XX_QLIE_PACK_OPT_LEGACY_PROFILE, &value);
        xx_var_cleanup(&value);
        if (!set) { status = 2; goto done; }
    }
    if (!xx_format_is_valid(opened.format, NULL)) {
        fprintf(stderr, "%s: %s header does not hold up\n", archive_path,
                opened.reader_name);
        status = 2;
        goto done;
    }
    if (!xx_format_handle_base_info(opened.format, NULL)) {
        fprintf(stderr, "%s: %s could not be parsed\n", archive_path,
                opened.reader_name);
        status = 2;
        goto done;
    }
    xxfc_attach_source_files(&opened, archive_path);
    if (xxfc_is_incomplete(&opened)) {
        fprintf(stderr, "%s: incomplete archive; showing recovered members\n", archive_path);
        status = 1;
    }

    if (!quiet) {
        printf("%s: %s\n", archive_path,
               xx_format_file_type_to_string(opened.type));
    }

    if (!opened.format->is_archive) {
        fprintf(stderr, "%s: %s holds no archive members\n", archive_path,
                xx_format_file_type_to_string(opened.type));
        status = 1;
        goto done;
    }

    xx_list_init(&options, sizeof(xx_meta), xx_meta_free_elem);
    if (unpack_to) {
        xx_meta meta;
        xx_meta_init(&meta, XX_META_ID_OPT_UNPACK_PATH);
        xx_var_set_str(&meta.var, unpack_to);
        xx_list_append(&options, &meta);
    }

    state = xx_format_create_archive_records_reading(
        opened.format, (const xx_list_s *)&options, NULL);
    if (!state) {
        fprintf(stderr, "%s: members could not be read\n", archive_path);
        xx_list_cleanup(&options);
        status = 2;
        goto done;
    }

    for (;;) {
        const xx_archive_record *record =
            xx_format_get_current_archive_record(opened.format, state);
        if (!record) break;

        ++total;
        {
            char *owned = NULL;
            const char *name = record_name(record, &owned);
            bool folder = xx_archive_record_get_meta_bool(
                record, XX_META_ID_IS_FOLDER, false);
            char *key = NULL, *saved = NULL;
            bool can_extract = true;

            if (unpack_to) {
                if (!folder && xxfc_safe_relative_name(name)) {
                    key = xxfc_normalized_name(name);
                    if (!key) can_extract = false;
                    else if (xxfc_seen_contains(&seen, key)) {
                        can_extract = xxfc_copy_previous_member(
                            unpack_to, name, (unsigned long long)total,
                            &seen, &saved);
                        if (saved && !quiet)
                            printf("  previous %s preserved as %s\n",
                                   name, saved);
                    }
                }
                if (!can_extract) {
                    ++failed;
                    fprintf(stderr, "  %s -- FAILED (cannot preserve prior member)\n",
                            name);
                } else if (xx_format_unpack_current_archive_record(opened.format,
                                                                   state, NULL)) {
                    if (key && !xxfc_seen_insert(&seen, key)) {
                        free(key);
                        key = NULL;
                        ++failed;
                        fprintf(stderr, "  %s -- FAILED (cannot track output name)\n",
                                name);
                        free(saved);
                        xx_str_free(owned);
                        status = 1;
                        break;
                    }
                    key = NULL;
                    if (!quiet) printf("  %s\n", name);
                } else {
                    ++failed;
                    fprintf(stderr, "  %s -- FAILED\n", name);
                }
            } else {
                printf("  %12lld  %s\n",
                       (long long)record->compressed_size, name);
            }
            free(key);
            free(saved);
            xx_str_free(owned);
        }

        if (total >= RECORD_LIMIT) {
            fprintf(stderr, "%s: stopping after %d members\n", archive_path,
                    RECORD_LIMIT);
            break;
        }
        if (!xx_format_archive_record_move_to_next(opened.format, state, NULL))
            break;
    }

    xx_format_free_archive_records_reading(opened.format, state);
    xx_list_cleanup(&options);

    if (!quiet) {
        printf("%lld member(s)", total);
        if (failed) printf(", %lld failed", failed);
        printf("\n");
    }
    if (failed) status = 1;
    if (!status && xxfc_is_incomplete(&opened)) {
        fprintf(stderr, "%s: incomplete archive; showing recovered members\n", archive_path);
        status = 1;
    }

done:
    xxfc_seen_cleanup(&seen);
    xxfc_close(&opened);
    xx_io_close(device);
    return status;
}

/* ------------------------------------------------------------- writing -- */

static bool ends_with(const char *text, const char *suffix) {
    size_t n = strlen(text), m = strlen(suffix);
    size_t i;

    if (m > n) return false;
    text += n - m;
    for (i = 0; i < m; ++i) {
        char a = text[i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

/* The writable containers, chosen by what the output name says it is. */
static Abstractformat *make_writer(const char *path, xx_io_device *device,
                                   xxfc_release_fn *release,
                                   const char **kind) {
    struct {
        const char *suffix;
        const char *kind;
    } table[] = {
        { ".tar.gz",  "tar.gz"  }, { ".tgz",     "tar.gz"  },
        { ".tar.bz2", "tar.bz2" }, { ".tbz2",    "tar.bz2" },
        { ".tar.xz",  "tar.xz"  }, { ".txz",     "tar.xz"  },
        { ".tar.zst", "tar.zst" }, { ".tar.lz4", "tar.lz4" },
        { ".tar",     "tar"     }, { ".zip",     "zip"     },
        { ".cpio",    "cpio"    },
    };
    size_t i;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (!ends_with(path, table[i].suffix)) continue;
        *kind = table[i].kind;
        return xxfc_make_writer(table[i].kind, device, release);
    }
    return NULL;
}

static int add(const char *archive_path, char **files, int count) {
    xx_io_device *device;
    Abstractformat *format;
    xxfc_release_fn release = NULL;
    const char *kind = NULL;
    xx_archive_write_state *state;
    int i, status = 0;

    device = xx_io_file_open(archive_path, "wb");
    if (!device) {
        fprintf(stderr, "cannot create %s\n", archive_path);
        return 2;
    }

    format = make_writer(archive_path, device, &release, &kind);
    if (!format) {
        fprintf(stderr,
                "%s: xxfclib cannot write this container. Writable: .tar "
                ".tar.gz .tar.bz2 .tar.xz .tar.zst .tar.lz4 .zip .cpio\n",
                archive_path);
        xx_io_close(device);
        return 2;
    }

    state = xx_format_create_archive_records_writing(format, NULL, NULL);
    if (!state) {
        fprintf(stderr, "%s: cannot start writing\n", archive_path);
        release(format);
        xx_io_close(device);
        return 2;
    }

    for (i = 0; i < count; ++i) {
        xx_io_device *source = xx_io_file_open(files[i], "rb");
        xx_archive_record record;

        if (!source) {
            fprintf(stderr, "  %s -- cannot read\n", files[i]);
            status = 1;
            continue;
        }

        xx_archive_record_init(&record);
        if (!xx_archive_record_set_original_name(&record, files[i]) ||
            !xx_format_pack_archive_record(format, state, &record, source,
                                           NULL)) {
            fprintf(stderr, "  %s -- FAILED\n", files[i]);
            status = 1;
        } else {
            printf("  %s\n", files[i]);
        }
        xx_archive_record_cleanup(&record);
        xx_io_close(source);
    }

    if (!xx_format_finalize_archive_records_writing(format, state, NULL)) {
        fprintf(stderr, "%s: could not be finished\n", archive_path);
        status = 2;
    } else {
        printf("%s: %d file(s) as %s\n", archive_path, count, kind);
    }

    xx_format_free_archive_records_writing(format, state);
    release(format);
    xx_io_close(device);
    return status;
}

/* ---------------------------------------------------------------- main -- */

static int xxfc_main(int argc, char **argv) {
    command_t command;
    const char *archive;
    const char *out_dir = ".";
    const char *reader_name = NULL;
    const char *password = NULL;
    int npa_payload = -1;
    int qlie_profile = 0;
    int i;

    if (argc == 2 && strcmp(argv[1], "formats") == 0) {
        xxfc_reader_entry *readers = xxfc_reader_table();
        size_t index;
        for (index = 0; index < xxfc_reader_count(); ++index)
            printf("%s\t%s\n", readers[index].name,
                   xx_format_file_type_to_string(readers[index].type));
        for (index = 18U; index <= xx_cpm_preset_count(); ++index)
            printf("cpm:%s\t%s\n",
                   xx_cpm_preset_name((xx_cpm_preset)index),
                   xx_format_file_type_to_string(XX_FILE_TYPE_CPM));
        return 0;
    }
    if (argc < 3) {
        usage(argc < 2 ? stdout : stderr, argv[0]);
        return argc < 2 ? 0 : 2;
    }

    command = parse_command(argv[1]);
    if (command == CMD_NONE) {
        fprintf(stderr, "unknown command: %s\n\n", argv[1]);
        usage(stderr, argv[0]);
        return 2;
    }
    archive = argv[2];

    for (i = 3; i < argc; ++i) {
        if (strncmp(argv[i], "--format=", 9) == 0) reader_name = argv[i] + 9;
        else if (strncmp(argv[i], "--password=", 11) == 0) password = argv[i] + 11;
        else if (strncmp(argv[i], "--npa-payload=", 14) == 0) {
            const char *mode = argv[i] + 14;
            if (!strcmp(mode, "auto")) npa_payload = XX_NITROPLUS_NPA_AUTO;
            else if (!strcmp(mode, "stored")) npa_payload = XX_NITROPLUS_NPA_STORED;
            else if (!strcmp(mode, "zlib")) npa_payload = XX_NITROPLUS_NPA_ZLIB;
            else { fprintf(stderr, "invalid NPA payload mode: %s\n", mode); return 2; }
        }
        else if (strncmp(argv[i], "--qlie-legacy=", 14) == 0) {
            const char *profile = argv[i] + 14;
            if (profile[0] >= '1' && profile[0] <= '3' && !profile[1])
                qlie_profile = profile[0] - '0';
            else { fprintf(stderr, "invalid Qlie legacy profile: %s\n", profile); return 2; }
        }
        else if (strncmp(argv[i], "-o", 2) == 0) out_dir = argv[i] + 2;
        else if (command != CMD_ADD) {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }

    switch (command) {
        case CMD_EXTRACT:
            return walk(archive, out_dir, false, reader_name, password, npa_payload, qlie_profile);
        case CMD_LIST:
            return walk(archive, NULL, false, reader_name, password, npa_payload, qlie_profile);
        case CMD_TEST: {
            /* Extraction is the only way to find out whether the payload
             * decodes: asking a reader to unpack with nowhere to put the
             * bytes is answered "fine" without decoding anything. */
            const char *scratch = "xxfc_unpack_test_tmp";
            int status;
            if (!xx_io_create_dirs_a(scratch, true)) {
                fprintf(stderr, "cannot create %s\n", scratch);
                return 2;
            }
            status = walk(archive, scratch, false, reader_name, password, npa_payload, qlie_profile);
            fprintf(stderr, "(tested into %s -- remove it when done)\n",
                    scratch);
            return status;
        }
        case CMD_ADD: {
            if (reader_name || password || npa_payload >= 0 || qlie_profile) {
                fprintf(stderr, "--format, --password, --npa-payload and --qlie-legacy are reading options\n");
                return 2;
            }
            int first = 3, count;
            while (first < argc && strncmp(argv[first], "-o", 2) == 0) ++first;
            count = argc - first;
            if (count < 1) {
                fprintf(stderr, "a: name at least one file to add\n");
                return 2;
            }
            return add(archive, argv + first, count);
        }
        default:
            return 2;
    }
}

#ifdef _WIN32
/* The Windows narrow CRT argv uses the active ANSI code page.  The library's
 * filesystem paths are UTF-8, so accepting that argv loses filenames which
 * cannot be represented in the current code page. */
int wmain(int argc, wchar_t **wide_argv) {
    char **utf8_argv = (char **)calloc((size_t)argc + 1U, sizeof(*utf8_argv));
    int i;
    int result;

    if (!utf8_argv) {
        fprintf(stderr, "out of memory while reading arguments\n");
        return 2;
    }
    for (i = 0; i < argc; ++i) {
        utf8_argv[i] = xx_str_unicode_to_utf8(wide_argv[i]);
        if (!utf8_argv[i]) {
            fprintf(stderr, "cannot convert command-line argument to UTF-8\n");
            while (i > 0) xx_str_free(utf8_argv[--i]);
            free(utf8_argv);
            return 2;
        }
    }
    result = xxfc_main(argc, utf8_argv);
    for (i = 0; i < argc; ++i) xx_str_free(utf8_argv[i]);
    free(utf8_argv);
    return result;
}
#else
int main(int argc, char **argv) {
    return xxfc_main(argc, argv);
}
#endif
