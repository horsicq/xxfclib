/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT */
#include "../src/formats/xx_archive_output_name.h"
#include "../src/formats/archive/warc/xx_warc.c"
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "names line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void mapped(const char *raw, unsigned cp, unsigned flags, const char *want)
{
    char *name = xx_archive_output_name(raw, cp, flags);
    CHECK(name && !strcmp(name, want)); xx_mem_free(name);
}
static void refused(const char *raw, unsigned flags)
{
    char *name = xx_archive_output_name(raw, 1252, flags); CHECK(!name);
}
static size_t add_record(char *buffer, size_t at, const char *type, const char *uri, const char *payload, unsigned id)
{
    int n = snprintf(buffer + at, 16000 - at,
        "WARC/1.0\r\nWARC-Type: %s\r\nWARC-Date: 2026-10-20T00:00:00Z\r\n"
        "WARC-Record-ID: <urn:test:%u>\r\nWARC-Target-URI: <%s>\r\nContent-Length: %u\r\n\r\n%s\r\n\r\n",
        type, id, uri, (unsigned)strlen(payload), payload);
    CHECK(n > 0 && (size_t)n < 16000 - at); return at + (size_t)n;
}
int main(void)
{
    mapped("A\xa5\x8f\x8f" "EU\x8f\xa5.SAM", 437, 0, "A\xc3\x91\xc3\x85\xc3\x85" "EU\xc3\x85\xc3\x91.SAM");
    mapped("Fran\xe7" "ais.mbm", 1252, 0, "Fran\xc3\xa7" "ais.mbm");
    mapped("Portugu\xea" "s/amftp.catalog", 28591, 0, "Portugu\xc3\xaa" "s/amftp.catalog");
    mapped("_\xd7" "Link.txt", 1252, 0, "_\xc3\x97" "Link.txt");
    mapped("Fran\xc3\xa7" "ais.mbm", 1252, 0, "Fran\xc3\xa7" "ais.mbm");
    mapped("bad\x81.txt", 1252, 0, "bad%81.txt");
    mapped("x%3A.txt", 0, 0, "x%253A.txt");
    mapped("file~xx-00000000", 0, 0, "file%7Exx-00000000");
    mapped("\\folder\\file", 0, XX_ARCHIVE_NAME_ROOTED, "folder/file");
    refused("../outside", 0); refused("safe/../../outside", 0);
    refused("safe\\..\\outside", 0); refused("/../outside", XX_ARCHIVE_NAME_ROOTED);
    refused("C:\\..\\outside", XX_ARCHIVE_NAME_DRIVE); refused("C:outside", 0);
    refused("/outside", 0); refused("\\outside", 0);
    refused("\\\\server\\share\\file", XX_ARCHIVE_NAME_ROOTED);
    refused("//?/C:/file", XX_ARCHIVE_NAME_ROOTED | XX_ARCHIVE_NAME_DRIVE);
    refused("bad\nname", 0);
#ifdef _WIN32
    mapped(":patch.c", 0, 0, "%3Apatch.c");
    mapped("CON.txt", 0, 0, "%43ON.txt");
    mapped("COM1", 0, 0, "%43OM1");
    mapped("CONIN$", 0, 0, "%43ONIN$");
    mapped("COM\xc2\xb9.txt", 0, 0, "%43OM\xc2\xb9.txt");
    mapped("tail. ", 0, 0, "tail.%20");
#else
    mapped(":patch.c", 0, 0, ":patch.c");
#endif
    {
        const struct { const char *name; } entries[] = {{"dir"}, {"dir/child"}, {"Dir"}, {"dir~xx-00000000"}};
        char *a = xx_archive_output_name(entries[0].name, 0, 0);
        char *b = xx_archive_output_name(entries[3].name, 0, 0);
        XX_ARCHIVE_OUTPUT_RESOLVE(a, entries, 4, 0, name, 0, 0);
        CHECK(a && !strcmp(a, "dir~xx-00000000") && strcmp(a, b));
        xx_mem_free(a); xx_mem_free(b);
    }
    {
        char archive[16000], names[6][100]; size_t n = 0, i = 0, j;
        const char *uris[] = {"http://example/a", "http://example/a/b", "http://example/a/", "http://example/A", "http://example/a", "http://example"};
        const char *bodies[] = {"one", "two", "three", "four", "five", "six"};
        xx_io_device *dev; xx_warc reader; xx_archive_record_state *state; xx_pd_struct pd = xx_pd_init();
        for (i = 0; i < 6; ++i) n = add_record(archive, n, i == 1 ? "resource" : "response", uris[i], bodies[i], (unsigned)i);
        n = add_record(archive, n, "warcinfo", "http://example/info", "metadata", 6);
        n = add_record(archive, n, "response", "http://example/../escape", "unsafe", 7);
        dev = xx_io_buffer_open_memory(archive, n, 4096); CHECK(dev);
        xx_warc_init(&reader, dev, 0); CHECK(xx_warc_check_is_valid(&reader.format, &pd));
        CHECK(xx_warc_get_number_of_archive_records(&reader.format, &pd) == 6);
        state = xx_warc_create_archive_records_reading(&reader.format, NULL, &pd); CHECK(state);
        i = 0;
        do {
            const xx_archive_record *r = xx_warc_get_current_archive_record(&reader.format, state);
            const char *name = xx_archive_record_get_original_name(r);
            CHECK(r && name && strlen(name) < sizeof(names[0]));
            if (i == 0 || i == 2 || i == 4) CHECK(!strncmp(name, "records/", 8));
            if (i == 1) CHECK(!strcmp(name, "a/b"));
            CHECK(!strcmp(xx_archive_record_get_meta_str(r, XX_META_ID_COMMENT), uris[i]));
            CHECK(xx_archive_record_get_meta_str(r, XX_META_ID_EXTRA_FIELD));
            CHECK(r->compressed_size == (int64_t)strlen(bodies[i]));
            CHECK(!memcmp(archive + r->data_offset, bodies[i], strlen(bodies[i])));
            for (j = 0; j < i; ++j) CHECK(strcmp(names[j], name));
            strcpy(names[i++], name);
            CHECK(xx_warc_unpack_current_archive_record(&reader.format, state, &pd));
        } while (xx_warc_archive_record_move_to_next(&reader.format, state, &pd));
        CHECK(i == 6); xx_warc_free_archive_records_reading(&reader.format, state);
        xx_warc_destroy(&reader); CHECK(!xx_io_close(dev));
        dev = xx_io_buffer_open_memory(archive, n - 1, 4096); CHECK(dev);
        xx_warc_init(&reader, dev, 0); CHECK(!xx_warc_check_is_valid(&reader.format, &pd));
        xx_warc_destroy(&reader); CHECK(!xx_io_close(dev));
    }
    puts("legacy output names and WARC records: OK"); return 0;
}
