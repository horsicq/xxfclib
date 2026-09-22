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
            "  x <archive> [-o<dir>]     Extract with full paths\n"
            "  l <archive>               List contents\n"
            "  t <archive>               Test: extract to a scratch dir, then discard\n"
            "  a <archive> <file>...     Add files to a new archive\n"
            "\n"
            "Options:\n"
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

/* Walks the archive once. `unpack_to` NULL lists without extracting. */
static int walk(const char *archive_path, const char *unpack_to, bool quiet) {
    xx_io_device *device = xx_io_file_open(archive_path, "rb");
    xxfc_opened opened;
    xx_archive_record_state *state;
    xx_list_t options;
    long long total = 0, failed = 0;
    int status = 0;

    if (!device) {
        fprintf(stderr, "cannot open %s\n", archive_path);
        return 2;
    }

    if (!xxfc_open(&opened, device, 0)) {
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

            if (unpack_to) {
                if (xx_format_unpack_current_archive_record(opened.format,
                                                            state, NULL)) {
                    if (!quiet) printf("  %s\n", name);
                } else {
                    ++failed;
                    fprintf(stderr, "  %s -- FAILED\n", name);
                }
            } else {
                printf("  %12lld  %s\n",
                       (long long)record->compressed_size, name);
            }
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

done:
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

int main(int argc, char **argv) {
    command_t command;
    const char *archive;
    const char *out_dir = ".";
    int i;

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
        if (strncmp(argv[i], "-o", 2) == 0) out_dir = argv[i] + 2;
    }

    switch (command) {
        case CMD_EXTRACT:
            return walk(archive, out_dir, false);
        case CMD_LIST:
            return walk(archive, NULL, false);
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
            status = walk(archive, scratch, false);
            fprintf(stderr, "(tested into %s -- remove it when done)\n",
                    scratch);
            return status;
        }
        case CMD_ADD: {
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
