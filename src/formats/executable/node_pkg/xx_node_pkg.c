/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Node.js single executables made by pkg (vercel/pkg 5.x and the yao-pkg
 * fork). Layout, measured on pnpm 6..10, Serverless and Lando builds and
 * matching pkg's lib/producer.ts:
 *
 *   [patched node image][payload][prelude]
 *
 * The prelude is JavaScript text that ends the file; it starts with
 *   (function(process, require, console, EXECPATH_FD, PAYLOAD_POSITION,
 *   PAYLOAD_SIZE) { return (function (REQUIRE_COMMON, VIRTUAL_FILESYSTEM,
 *   DEFAULT_ENTRYPOINT, SYMLINKS, DICT, DOCOMPRESS) {
 * and, after the bootstrap and common functions ("\n},\n"), carries the
 * virtual filesystem as JSON: absolute snapshot paths ("C:\\snapshot\\...",
 * "/snapshot/...") mapped to stores "0" (V8 code cache of a compiled
 * script), "1" (file content), "2" (directory listing), "3" (stat record),
 * each [offset, size] relative to the payload, which begins at the PE
 * overlay. The arguments that follow ("\n,\n"-separated) are the entry point,
 * the symlinks, the name dictionary and DOCOMPRESS: 0 stored, 1 GZip,
 * 2 Brotli applied to every blob and content entry. The image also holds the
 * four position placeholders as text ("PAYLOAD_POSITION = '31009280   ' |
 * 0;"), but not every build injects them, so the prelude text is the
 * locator. Content entries become members named by their path below the
 * snapshot root; a script that exists only as code cache becomes
 * "<name>.jsc". Nothing is executed.
 */
#include "xxfclib/formats/executable/node_pkg/xx_node_pkg.h"
#include "../../common/xx_executable_carrier.h"
#include "xxfclib/json/xx_json.h"
#include "xxfclib/formats/archive/gz/xx_gz.h"
#include "xxfclib/algo/brotli/xx_brotli.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/strings/xx_string.h"

#define PKG_MAX_PRELUDE (64 * 1024 * 1024)
#define PKG_MAX_ENTRIES 200000U
#define PKG_MAX_PACKED (256 * 1024 * 1024)
#define PKG_STORED 0U
#define PKG_GZIP 1U
#define PKG_BROTLI 2U

static const char pkg_prelude_head[] = "(function(process, require, console, EXECPATH_FD, PAYLOAD_POSITION, PAYLOAD_SIZE) {";
static const char pkg_vfs_lead[] = "\n},\n{";

typedef struct pkg_member {
    unsigned compress;
} pkg_member;

static bool pkg_write_all(xx_io_device *device, const uint8_t *buffer, size_t count)
{
    size_t done = 0;
    while (done < count) {
        ssize_t w = xx_io_write(device, buffer + done, count - done);
        if (w <= 0 || (size_t)w > count - done) return false;
        done += (size_t)w;
    }
    return true;
}

/* A GZip or Brotli entry decoded into the destination (a temporary device
 * when only validated). */
static bool pkg_read_all(Abstractformat *f, pm_member *m, xx_io_device *device, xx_pd_struct *pd)
{
    pkg_member *ctx = (pkg_member *)m->context;
    xx_io_device *sink = device ? device : xx_io_temp_open();
    bool ok = false;
    if (!ctx || !sink || m->packed_size <= 0 || m->packed_size > PKG_MAX_PACKED) {
        if (sink && !device) xx_io_close(sink);
        return false;
    }
    if (ctx->compress == PKG_GZIP) {
        xx_io_device *view = xx_io_sub_open_ro(f->device, m->offset, m->packed_size);
        xx_gz *gz = view ? xx_gz_create(view, 0) : NULL;
        ok = gz && xx_gz_check_is_valid(xx_gz_to_format(gz), pd) && xx_gz_unpack_to_device(gz, sink, pd);
        if (gz) xx_gz_free(gz);
        if (view) xx_io_close(view);
    } else if (ctx->compress == PKG_BROTLI) {
        uint8_t *packed = (uint8_t *)xx_mem_alloc((size_t)m->packed_size), *plain = NULL;
        size_t plain_size = 0;
        ok = packed && pm_read(f, m->offset - f->base_address, packed, (size_t)m->packed_size) && xx_brotli_decompress_alloc(packed, (size_t)m->packed_size, &plain, &plain_size);
        if (ok) ok = pkg_write_all(sink, plain, plain_size);
        if (plain) xx_mem_free(plain);
        if (packed) xx_mem_free(packed);
    }
    if (!device) xx_io_close(sink);
    return ok && !(pd && xx_pd_is_stopped(pd));
}

/* The member name: the path below the snapshot root, with forward slashes. */
static const char *pkg_member_name(const char *path)
{
    const char *p = path, *root = NULL;
    while (*p) {
        if ((p == path || p[-1] == '\\' || p[-1] == '/') && xx_rt_strncmp(p, "snapshot", 8) == 0 && (p[8] == '\\' || p[8] == '/' || p[8] == 0)) {
            root = p + 8;
            break;
        }
        ++p;
    }
    if (!root) root = path;
    while (*root == '\\' || *root == '/') ++root;
    return root;
}

static bool pkg_store_pair(xx_json *json, int64_t limit, int64_t *offset, int64_t *size)
{
    if (!xx_json_array_begin(json) || xx_json_array_empty(json)) return false;
    if (!xx_json_integer(json, limit, offset) || !xx_json_more(json) || !xx_json_integer(json, limit, size) || xx_json_more(json)) return false;
    return xx_json_array_end(json);
}

static bool pkg_add(Abstractformat *f, pm_stream *s, const char *name, int64_t payload, int64_t offset, int64_t size, unsigned compress, bool blob)
{
    char label[128];
    pm_member *m;
    pkg_member *ctx;
    const char *leaf = pkg_member_name(name);
    size_t n = xx_rt_strlen(leaf), i;
    if (!n) return true; /* the root itself */
    if (n > 90U) leaf += n - 90U, n = 90U;
    for (i = 0; i < n; ++i) label[i] = leaf[i] == '\\' ? '/' : leaf[i];
    label[n] = 0;
    if (blob) xx_rt_memcpy(label + n, ".jsc", 5U);
    if (!pm_add(f, s, label, payload + offset, size)) return false;
    if (compress == PKG_STORED) return true;
    m = &s->items[s->count - 1U];
    ctx = (pkg_member *)xx_mem_calloc(1U, sizeof(*ctx));
    if (!ctx) return false;
    ctx->compress = compress;
    m->context = ctx;
    m->free_context = xx_mem_free;
    m->read_all = pkg_read_all;
    m->size = -1; /* known only after decoding */
    m->compression_method = compress == PKG_GZIP ? 8U : 0U;
    return true;
}

static const uint8_t *pkg_find(const uint8_t *hay, size_t hay_size, const char *needle)
{
    size_t n = xx_rt_strlen(needle), i;
    if (!n || hay_size < n) return NULL;
    for (i = 0; i + n <= hay_size; ++i)
        if (hay[i] == (uint8_t)needle[0] && xx_rt_memcmp(hay + i, needle, n) == 0) return hay + i;
    return NULL;
}

/* DOCOMPRESS: the last argument before the closing ");" of the prelude. */
static unsigned pkg_docompress(const uint8_t *prelude, size_t size)
{
    size_t i = size;
    while (i > 0 && (prelude[i - 1] == ')' || prelude[i - 1] == ';' || prelude[i - 1] == '}' || prelude[i - 1] == '\n' || prelude[i - 1] == '\r' || prelude[i - 1] == ' ')) --i;
    if (i == 0 || prelude[i - 1] < '0' || prelude[i - 1] > '2') return PKG_STORED;
    return (unsigned)(prelude[i - 1] - '0');
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t overlay, cabinet, cabinet_end, available = pm_available(f), found, prelude_at, prelude_size, payload_size;
    uint8_t *prelude = NULL;
    const uint8_t *vfs;
    xx_json json;
    unsigned compress;
    bool ok = false, first = true;
    if (!carrier_pe(f, &overlay, &cabinet, &cabinet_end, pd) || overlay <= 0 || available - overlay < 1024) return false;
    found = xx_io_find_bytes_buffer_optimize_ex(f->device, f->base_address + overlay, available - overlay, pkg_prelude_head, sizeof(pkg_prelude_head) - 1U, xx_get_file_buffer_size(), pd);
    if (found < f->base_address + overlay || (pd && xx_pd_is_stopped(pd))) return false;
    prelude_at = found - f->base_address;
    prelude_size = available - prelude_at;
    payload_size = prelude_at - overlay;
    if (prelude_size < 256 || prelude_size > PKG_MAX_PRELUDE || payload_size < 0) return false;
    prelude = (uint8_t *)xx_mem_alloc((size_t)prelude_size);
    if (!prelude || !pm_read(f, prelude_at, prelude, (size_t)prelude_size)) goto done;
    vfs = pkg_find(prelude, (size_t)prelude_size, pkg_vfs_lead);
    if (!vfs) goto done;
    vfs += sizeof(pkg_vfs_lead) - 2U; /* at the "{" */
    compress = pkg_docompress(prelude, (size_t)prelude_size);
    xx_json_init(&json, vfs, (size_t)(prelude + prelude_size - vfs));
    if (!xx_json_object_begin(&json)) goto done;
    if (!xx_json_object_empty(&json)) {
        for (;;) {
            char *path = NULL;
            int64_t blob_offset = -1, blob_size = 0, content_offset = -1, content_size = 0;
            bool entry_ok = true;
            if ((pd && xx_pd_is_stopped(pd)) || s->count >= PKG_MAX_ENTRIES || !xx_json_object_key(&json, &path)) goto done;
            if (!xx_json_object_begin(&json)) {
                xx_str_free(path);
                goto done;
            }
            if (!xx_json_object_empty(&json)) {
                for (;;) {
                    char *store = NULL;
                    int64_t offset = 0, size = 0;
                    if (!xx_json_object_key(&json, &store)) {
                        entry_ok = false;
                        break;
                    }
                    if (xx_str_cmp(store, "0") == 0 || xx_str_cmp(store, "1") == 0) {
                        entry_ok = pkg_store_pair(&json, payload_size, &offset, &size) && size <= payload_size - offset;
                        if (entry_ok && store[0] == '0') blob_offset = offset, blob_size = size;
                        if (entry_ok && store[0] == '1') content_offset = offset, content_size = size;
                    } else {
                        entry_ok = xx_json_skip(&json);
                    }
                    xx_str_free(store);
                    if (!entry_ok || !xx_json_more(&json)) break;
                }
            }
            if (entry_ok) entry_ok = xx_json_object_end(&json);
            if (entry_ok && content_offset >= 0) entry_ok = pkg_add(f, s, path, overlay, content_offset, content_size, compress, false);
            if (entry_ok && content_offset < 0 && blob_offset >= 0) entry_ok = pkg_add(f, s, path, overlay, blob_offset, blob_size, compress, true);
            xx_str_free(path);
            if (!entry_ok) goto done;
            first = false;
            if (!xx_json_more(&json)) break;
        }
    }
    if (!xx_json_object_end(&json)) goto done;
    (void)first;
    s->size = available;
    ok = s->count > 0;
done:
    if (prelude) xx_mem_free(prelude);
    return ok && !(pd && xx_pd_is_stopped(pd));
}

void xx_node_pkg_init(xx_node_pkg *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NODE_PKG, "exe");
    }
}
xx_node_pkg *xx_node_pkg_create(xx_io_device *d, int64_t b)
{
    xx_node_pkg *r = (xx_node_pkg *)xx_mem_alloc(sizeof(*r));
    if (r) xx_node_pkg_init(r, d, b);
    return r;
}
void xx_node_pkg_destroy(xx_node_pkg *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_node_pkg_free(xx_node_pkg *r)
{
    if (r) {
        xx_node_pkg_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_node_pkg_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_node_pkg_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
