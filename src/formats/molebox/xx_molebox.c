/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent SVFS/SPACK grammar from GPL-3.0 MoleBox packager/runtime sources.
 * Private Blowfish implementation has its own retained permissive notice.
 * See MOLEBOX_PROVENANCE.json. No packaged executable is run. */
#include "xxfclib/formats/molebox/xx_molebox.h"
#include "../ue2_indexed.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "molebox_blowfish.inc"
#define MB_TYPE XX_FILE_TYPE_MOLEBOX
#define MB_BLOCK 49152U
#define MB_TREE (16U * 1024U * 1024U)
#define MB_META (64U * 1024U * 1024U)
#define MB_MEMORY (UINT64_C(128) * 1024 * 1024)
#define MB_MAX_MEMORY (UINT64_C(256) * 1024 * 1024)
#define MB_DIRECTORY UINT32_C(0x80000000)
#define MB_CREDENTIAL "molebox-md5:"
#define MB_CREDENTIAL_LENGTH (sizeof(MB_CREDENTIAL) - 1)
typedef struct mb_package {
    uint8_t *tree;
    uint32_t size;
    int64_t base, tree_at;
    mb_cipher cipher;
    uint8_t key[16];
    bool embedded;
} mb_package;
typedef struct mb_index {
    ue2_index records;
    mb_package *packages;
    uint32_t count, capacity, blocks;
    uint64_t owned;
} mb_index;
typedef struct mb_sink {
    uint8_t *bytes;
    size_t size, limit;
} mb_sink;
typedef struct mb_state {
    ue2_state records;
    uint64_t generation;
} mb_state;
static bool mb_fail(xx_pd_struct *pd, const char *s)
{
    xx_pd_set_error(pd, 1, s);
    return false;
}
static void mb_cbc(const mb_cipher *c, uint8_t *p, size_t n)
{
    uint8_t previous[8] = {0};
    size_t i, j;
    for (i = 0; i < n; i += 8) {
        uint8_t now[8];
        xx_rt_memcpy(now, p + i, 8);
        mb_decrypt8(c, p + i);
        for (j = 0; j < 8; ++j) p[i + j] ^= previous[j];
        xx_rt_memcpy(previous, now, 8);
    }
}
static bool mb_key(const char *password, uint8_t key[16])
{
    size_t i;
    if (!password) password = "password";
    if (!xx_rt_strncmp(password, MB_CREDENTIAL, MB_CREDENTIAL_LENGTH)) {
        password += MB_CREDENTIAL_LENGTH;
        if (xx_rt_strlen(password) != 32) return false;
        for (i = 0; i < 16; ++i) {
            unsigned a = (unsigned char)password[i * 2], b = (unsigned char)password[i * 2 + 1];
            a = xx_rt_ascii_tolower(a);
            b = xx_rt_ascii_tolower(b);
            if (a >= '0' && a <= '9') a -= '0';
            else if (a >= 'a' && a <= 'f') a = a - 'a' + 10;
            else return false;
            if (b >= '0' && b <= '9') b -= '0';
            else if (b >= 'a' && b <= 'f') b = b - 'a' + 10;
            else return false;
            key[i] = (uint8_t)(a * 16 + b);
        }
        return true;
    }
    return xx_md5_memory(password, xx_rt_strlen(password), key);
}
static void mb_credential(const uint8_t key[16], char out[45])
{
    static const char hex[] = "0123456789abcdef";
    size_t i;
    xx_rt_memcpy(out, MB_CREDENTIAL, MB_CREDENTIAL_LENGTH);
    for (i = 0; i < 16; ++i) {
        out[MB_CREDENTIAL_LENGTH + i * 2] = hex[key[i] >> 4];
        out[MB_CREDENTIAL_LENGTH + 1 + i * 2] = hex[key[i] & 15];
    }
    out[MB_CREDENTIAL_LENGTH + 32] = 0;
}
static bool mb_catalog_header(Abstractformat *f, uint8_t h[8], mb_cipher *cipher)
{
    static const char pwd[] = "7233DA04FCF24B388408701CB1F874F3";
    uint8_t key[16];
    int64_t size = xx_io_total_size(f->device);
    if (size - f->base_address < 32 || !xx_md5_memory(pwd, sizeof(pwd) - 1, key) || !ue2_read(f, size - 8, h, 8)) return false;
    mb_cipher_init(cipher, key, 15);
    mb_decrypt8(cipher, h);
    return xx_data_get_u32(h, 4, 0, false) >= 24 && xx_data_get_u32(h, 4, 0, false) <= 65536 && !(xx_data_get_u32(h, 4, 0, false) & 7) &&
           xx_data_get_u32(h, 4, 0, false) < (uint64_t)(size - f->base_address - 8);
}
bool xx_molebox_has_candidate_device(xx_io_device *io, int64_t base)
{
    Abstractformat f;
    mb_cipher cipher;
    uint8_t h[48], key[16];
    int64_t saved, size;
    bool found = false;
    if (!io || base < 0) {
        return false;
    }
    xx_rt_memset(&f, 0, sizeof(f));
    f.device = io;
    f.base_address = base;
    saved = xx_io_tell(io);
    size = xx_io_total_size(io);
    found = mb_catalog_header(&f, h, &cipher);
    if (!found && size - base >= 64 && mb_key(NULL, key) && ue2_read(&f, size - 48, h, 48)) {
        mb_cipher_init(&cipher, key, 15);
        mb_cbc(&cipher, h, 48);
        found = !xx_rt_memcmp(h + 40, "STELPACK", 8);
    }
    if (saved >= 0) {
        xx_io_seek64(io, saved, XX_RT_SEEK_SET);
    }
    return found;
}
static void mb_free_index(mb_index *ix)
{
    uint32_t i;
    if (!ix) return;
    for (i = 0; i < ix->count; ++i) xx_mem_free(ix->packages[i].tree);
    xx_mem_zero(ix->packages, (size_t)ix->capacity * sizeof(mb_package));
    xx_mem_free(ix->packages);
    ue2_index_free(&ix->records);
}
static bool mb_range(uint32_t size, uint32_t at, uint64_t n)
{
    return at <= size && n <= size - at;
}
static char *mb_name(const mb_package *p, uint32_t at, uint32_t length)
{
    char *out;
    size_t used = 0, i;
    uint32_t v;
    if (!length || length > 4096 || !mb_range(p->size, at, ((uint64_t)length + 1) * 2) || xx_data_get_u16(p->tree + at + length * 2, 2, 0, false)) return NULL;
    out = xx_mem_alloc((size_t)length * 3 + 1);
    if (!out) return NULL;
    for (i = 0; i < length; ++i) {
        v = xx_data_get_u16(p->tree + at + i * 2, 2, 0, false);
        if (!v) goto failed;
        if (v >= 0xd800 && v <= 0xdbff) {
            uint32_t low;
            if (++i >= length) goto failed;
            low = xx_data_get_u16(p->tree + at + i * 2, 2, 0, false);
            if (low < 0xdc00 || low > 0xdfff) goto failed;
            v = 0x10000 + ((v - 0xd800) << 10) + low - 0xdc00;
        } else if (v >= 0xdc00 && v <= 0xdfff) goto failed;
        if (v < 0x80) out[used++] = (char)v;
        else if (v < 0x800) {
            out[used++] = (char)(0xc0 | (v >> 6));
            out[used++] = (char)(0x80 | (v & 63));
        } else if (v < 0x10000) {
            out[used++] = (char)(0xe0 | (v >> 12));
            out[used++] = (char)(0x80 | ((v >> 6) & 63));
            out[used++] = (char)(0x80 | (v & 63));
        } else {
            out[used++] = (char)(0xf0 | (v >> 18));
            out[used++] = (char)(0x80 | ((v >> 12) & 63));
            out[used++] = (char)(0x80 | ((v >> 6) & 63));
            out[used++] = (char)(0x80 | (v & 63));
        }
    }
    out[used] = 0;
    if (!ue2_safe_name(out) || xx_rt_strchr(out, '/')) goto failed;
    return out;
failed:
    xx_mem_free(out);
    return NULL;
}
static bool mb_add(mb_index *ix, const char *name, uint32_t pkg, uint32_t node, uint32_t size, uint64_t stored, bool folder)
{
    uint64_t add = xx_rt_strlen(name) + 1;
    size_t capacity = ix->records.capacity;
    if (ix->records.count >= 65535 || stored > INT64_MAX) {
        return false;
    }
    if (ix->records.count == capacity) add += (capacity ? capacity : 32) * sizeof(ue2_member);
    if (ix->owned > MB_META || add > MB_META - ix->owned || !ue2_add(&ix->records, name, -1, (int64_t)stored, ((uint64_t)pkg << 32) | node)) return false;
    ix->owned += add;
    ix->records.members[ix->records.count - 1].original_size = size;
    ix->records.members[ix->records.count - 1].is_folder = folder;
    return true;
}
static bool mb_node(Abstractformat *f, mb_index *ix, uint32_t pkg, uint32_t at, const char *prefix, unsigned depth, uint8_t *seen, bool root, xx_pd_struct *pd)
{
    mb_package *p = &ix->packages[pkg];
    const uint8_t *n;
    uint32_t flags, count, size, i;
    char *name = NULL, *path = NULL;
    bool ok = false;
    uint64_t sum = 0, stored = 0;
    if (depth > 64 || !mb_range(p->size, at, 52) || (pd && xx_pd_is_stopped(pd)) || (seen[at >> 3] & (1 << (at & 7)))) {
        return false;
    }
    seen[at >> 3] |= (uint8_t)(1 << (at & 7));
    n = p->tree + at;
    flags = xx_data_get_u32(n + 40, 4, 0, false);
    size = xx_data_get_u32(n + 44, 4, 0, false);
    count = xx_data_get_u32(n + 48, 4, 0, false);
    if (flags & ~(MB_DIRECTORY | 127U) || count > 1000000 || !mb_range(p->size, at + 52, (uint64_t)count * ((flags & MB_DIRECTORY) ? 4 : 12))) return false;
    if (root) {
        if (!(flags & MB_DIRECTORY)) return false;
        path = xx_str_dup(prefix ? prefix : "");
    } else {
        if (xx_data_get_u32(n + 8, 4, 0, false)) name = mb_name(p, xx_data_get_u32(n + 8, 4, 0, false), xx_data_get_u32(n + 12, 4, 0, false));
        else if (!(flags & MB_DIRECTORY) && (flags & 1)) {
            static const char hex[] = "0123456789abcdef";
            char hidden[44];
            xx_rt_memcpy(hidden, "hidden-", 7);
            for (i = 0; i < 16; ++i) {
                hidden[7 + i * 2] = hex[n[20 + i] >> 4];
                hidden[8 + i * 2] = hex[n[20 + i] & 15];
            }
            xx_rt_memcpy(hidden + 39, ".bin", 5);
            name = xx_str_dup(hidden);
        }
        if (!name) {
            goto done;
        }
        path = prefix && prefix[0] ? xx_str_concat3(prefix, "/", name) : xx_str_dup(name);
    }
    if (!path || xx_rt_strlen(path) > 16384) goto done;
    if (flags & MB_DIRECTORY) {
        if (!mb_range(p->size, at + 52 + (uint32_t)count * 4, 4) || xx_data_get_u32(n + 52 + count * 4, 4, 0, false) != UINT32_C(0xdeadbeaf)) goto done;
        if (!root && !mb_add(ix, path, pkg, at, 0, 0, true)) goto done;
        for (i = 0; i < count; ++i)
            if (!mb_node(f, ix, pkg, xx_data_get_u32(n + 52 + i * 4, 4, 0, false), path, depth + 1, seen, false, pd)) goto done;
    } else {
        if (count > 1000000 - ix->blocks) goto done;
        ix->blocks += count;
        for (i = 0; i < count; ++i) {
            const uint8_t *b = n + 52 + i * 12;
            uint32_t packed = xx_data_get_u16(b, 2, 0, false), real = xx_data_get_u16(b + 2, 2, 0, false), bf = xx_data_get_u16(b + 4, 2, 0, false),
                     off = xx_data_get_u32(b + 8, 4, 0, false);
            if (!packed || !real || real > MB_BLOCK || packed > MB_BLOCK || bf & ~6U || (bf & 4 && packed % 8) || off < 12 || p->base < 0 || p->tree_at < p->base ||
                off > (uint64_t)(p->tree_at - p->base) || packed > (uint64_t)(p->tree_at - p->base) - off)
                goto done;
            if (!(bf & 2) && (packed < real || packed - real > (bf & 4 ? 7U : 0U))) {
                goto done;
            }
            sum += real;
            stored += packed;
        }
        if (sum != size || !mb_add(ix, path, pkg, at, size, stored, false)) goto done;
    }
    ok = true;
done:
    xx_str_free(name);
    xx_str_free(path);
    return ok;
}
static bool mb_header(Abstractformat *f, int64_t at, const uint8_t key[16], uint8_t h[48], mb_cipher *c)
{
    if (!ue2_read(f, at, h, 48)) return false;
    mb_cipher_init(c, key, 15);
    mb_cbc(c, h, 48);
    return !xx_rt_memcmp(h + 40, "STELPACK", 8);
}
static bool mb_load_package(Abstractformat *f, mb_index *ix, int64_t at, const uint8_t key[16], bool embedded, const char *prefix, xx_pd_struct *pd)
{
    uint8_t h[48], *seen = NULL;
    mb_package *p;
    uint32_t size, root, map, count;
    bool ok = false, attached = false;
    if (ix->count >= 64) return false;
    if (ix->count == ix->capacity) {
        uint32_t cap = ix->capacity ? ix->capacity * 2 : 4;
        mb_package *next;
        if (ix->owned + (uint64_t)(cap - ix->capacity) * sizeof(*next) > MB_META) return false;
        next = xx_mem_realloc(ix->packages, cap * sizeof(*next));
        if (!next) return false;
        xx_mem_zero(next + ix->capacity, (cap - ix->capacity) * sizeof(*next));
        ix->owned += (uint64_t)(cap - ix->capacity) * sizeof(*next);
        ix->packages = next;
        ix->capacity = cap;
    }
    p = &ix->packages[ix->count];
    if (!mb_header(f, at, key, h, &p->cipher)) return false;
    size = xx_data_get_u32(h + 4, 4, 0, false);
    root = xx_data_get_u32(h + 8, 4, 0, false);
    map = xx_data_get_u32(h + 20, 4, 0, false);
    count = xx_data_get_u32(h + 24, 4, 0, false);
    if (!size || size > MB_TREE || size % 8 || size > (uint64_t)(at - f->base_address) || ix->owned + size > MB_META || count > 65535 ||
        !mb_range(size, map, (uint64_t)count * 4))
        return false;
    p->tree = xx_mem_alloc(size);
    seen = xx_mem_calloc(((size_t)size + 7) / 8, 1);
    if (!p->tree || !seen) goto done;
    p->size = size;
    p->base = at - (int64_t)xx_data_get_u32(h + 12, 4, 0, false);
    p->tree_at = at - size;
    p->embedded = embedded;
    xx_rt_memcpy(p->key, key, 16);
    ++ix->count;
    attached = true;
    ix->owned += size;
    if (p->base < f->base_address || p->base > p->tree_at || !ue2_read(f, p->tree_at, p->tree, size)) goto done;
    mb_cbc(&p->cipher, p->tree, size);
    if (xx_crc32_calc(0, p->tree, size) != xx_data_get_u32(h + 16, 4, 0, false) || xx_data_get_u32(p->tree, 4, 0, false) != 123456789U) goto done;
    if (!mb_node(f, ix, ix->count - 1, root, prefix, 0, seen, true, pd)) goto done;
    {
        uint32_t i;
        for (i = 0; i < count; ++i) {
            uint32_t node = xx_data_get_u32(p->tree + map + i * 4, 4, 0, false);
            if (node >= size || !(seen[node >> 3] & (1 << (node & 7)))) goto done;
        }
    }
    ok = true;
done:
    xx_mem_free(seen);
    if (!ok && p->tree && !attached) {
        xx_mem_free(p->tree);
        p->tree = NULL;
    }
    return ok;
}
typedef struct mb_sort_name {
    const char *name;
} mb_sort_name;
static int mb_compare(const void *a, const void *b)
{
    const char *x = ((const mb_sort_name *)a)->name, *y = ((const mb_sort_name *)b)->name;
    while (*x && *y) {
        int c = xx_rt_ascii_tolower((unsigned char)*x++), d = xx_rt_ascii_tolower((unsigned char)*y++);
        if (c != d) return c < d ? -1 : 1;
    }
    return *x ? 1 : *y ? -1 : 0;
}
static bool mb_unique(mb_index *ix)
{
    mb_sort_name *names;
    size_t i;
    bool ok = true;
    if (!ix->records.count) return true;
    names = xx_mem_alloc(ix->records.count * sizeof(*names));
    if (!names) return false;
    for (i = 0; i < ix->records.count; ++i) {
        names[i].name = ix->records.members[i].name;
    }
    xx_rt_qsort(names, ix->records.count, sizeof(*names), mb_compare);
    for (i = 1; i < ix->records.count; ++i)
        if (!mb_compare(names + i - 1, names + i)) {
            ok = false;
            break;
        }
    xx_mem_free(names);
    return ok;
}
static mb_index *mb_parse(Abstractformat *f, xx_pd_struct *pd)
{
    mb_index *ix = NULL;
    mb_cipher cipher;
    uint8_t h[48], key[16], *catalog = NULL;
    uint32_t bytes, pkgs, mounts, pt, mt, i, j;
    int64_t total;
    bool ok = false;
    if (!f || !f->device || f->base_address < 0 || (pd && xx_pd_is_stopped(pd)) || !mb_key(xx_format_get_password(f), key)) {
        return NULL;
    }
    total = xx_io_total_size(f->device);
    ix = xx_mem_calloc(1, sizeof(*ix));
    if (!ix) return NULL;
    ix->owned = sizeof(*ix);
    if (mb_catalog_header(f, h, &cipher)) {
        bytes = xx_data_get_u32(h, 4, 0, false);
        catalog = xx_mem_alloc(bytes);
        if (!catalog || !ue2_read(f, total - 8 - bytes, catalog, bytes)) goto done;
        mb_cbc(&cipher, catalog, bytes);
        if (xx_crc32_calc(0, catalog, bytes) != xx_data_get_u32(h + 4, 4, 0, false)) {
            goto done;
        }
        pt = xx_data_get_u32(catalog, 4, 0, false);
        pkgs = xx_data_get_u32(catalog + 4, 4, 0, false);
        mt = xx_data_get_u32(catalog + 8, 4, 0, false);
        mounts = xx_data_get_u32(catalog + 12, 4, 0, false);
        if (!pkgs || pkgs > 64 || mounts > 64 || !mb_range(bytes, pt, (uint64_t)pkgs * 8) || !mb_range(bytes, mt, (uint64_t)mounts * 24)) goto done;
        for (i = 0; i < pkgs; ++i) {
            int64_t at = xx_data_get_u32(catalog + pt + i * 8, 4, 0, false);
            uint32_t no = xx_data_get_u32(catalog + pt + i * 8 + 4, 4, 0, false);
            const char *prefix = "";
            bool embedded = false;
            uint8_t use[16];
            if (no >= bytes || !xx_rt_memchr(catalog + no, 0, bytes - no)) {
                goto done;
            }
            if (pkgs > 1) {
                prefix = (const char *)catalog + no;
                if (!ue2_safe_name(prefix) || xx_rt_strchr(prefix, '/')) goto done;
            }
            xx_rt_memcpy(use, key, 16);
            if (!mb_header(f, at, use, h, &cipher)) {
                for (j = 0; j < mounts; ++j) {
                    const uint8_t *m = catalog + mt + j * 24;
                    if (!(xx_data_get_u32(m, 4, 0, false) & 1)) continue;
                    if (mb_header(f, at, m + 8, h, &cipher)) {
                        xx_rt_memcpy(use, m + 8, 16);
                        embedded = true;
                        break;
                    }
                }
                if (j == mounts) goto done;
            }
            if (!mb_load_package(f, ix, at, use, embedded, prefix, pd)) goto done;
        }
    } else if (total - f->base_address >= 64) {
        if (!mb_load_package(f, ix, total - 48, key, false, "", pd)) goto done;
    } else goto done;
    if (!mb_unique(ix)) {
        goto done;
    }
    ix->records.size = total - f->base_address;
    ok = true;
done:
    xx_mem_free(catalog);
    if (!ok) {
        mb_free_index(ix);
        return NULL;
    }
    return ix;
}
static bool mb_valid(Abstractformat *f, xx_pd_struct *pd)
{
    mb_index *ix = mb_parse(f, pd);
    bool ok = ix != NULL;
    mb_free_index(ix);
    return ok;
}
static bool mb_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_molebox *a = (xx_molebox *)f;
    mb_index *ix = mb_parse(f, pd);
    uint32_t i;
    if (!ix) return mb_fail(pd, "MoleBox password required, or package is damaged/unsupported");
    mb_free_index(a->index);
    a->index = ix;
    ++a->generation;
    if (!a->generation) ++a->generation;
    f->format_size = ix->records.size;
    f->number_of_archive_records = ix->records.count;
    f->base_info_handled = f->is_valid = true;
    f->is_crypted = true;
    for (i = 0; i < ix->count; ++i)
        if (ix->packages[i].embedded) {
            char text[45];
            mb_credential(ix->packages[i].key, text);
            if ((!xx_format_get_password(f) || xx_rt_strcmp(xx_format_get_password(f), text)) && !xx_format_set_password(f, text)) {
                f->base_info_handled = f->is_valid = false;
                return false;
            }
            break;
        }
    if (i == ix->count && !xx_format_get_password(f) && !xx_format_set_password(f, "password")) {
        f->base_info_handled = f->is_valid = false;
        return false;
    }
    f->base_info_handled = f->is_valid = true;
    f->number_of_archive_records = ix->records.count;
    f->format_size = ix->records.size;
    return true;
}
static xx_archive_record_state *mb_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    size_t i;
    uint64_t limit = MB_MEMORY;
    mb_index *ix;
    for (i = 0; opts && i < opts->count; ++i) {
        const xx_meta *m = xx_list_at((const xx_list_t *)opts, i);
        if (m && m->meta_id == XX_META_ID_OPT_MEMORY_LIMIT) limit = xx_var_get_u64(&m->var);
        if (m && m->meta_id == XX_META_ID_OPT_PASSWORD) {
            const char *password = NULL;
            char *converted = NULL;
            if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) password = xx_var_get_str(&m->var);
            else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
                converted = xx_str_unicode_to_utf8(xx_var_get_wstr(&m->var));
                password = converted;
            }
            if (!password) {
                xx_str_free(converted);
                return NULL;
            }
            if (!xx_format_get_password(f) || xx_rt_strcmp(xx_format_get_password(f), password)) {
                if (!xx_format_set_password(f, password)) {
                    xx_str_free(converted);
                    return NULL;
                }
            }
            xx_str_free(converted);
        }
    }
    if (!f->base_info_handled && !mb_info(f, pd)) {
        return NULL;
    }
    ix = ((xx_molebox *)f)->index;
    if (limit > MB_MAX_MEMORY) limit = MB_MAX_MEMORY;
    if (ix->owned + 2 * MB_BLOCK + 1024 * 1024 > limit) {
        mb_fail(pd, "MoleBox decoder exceeds archive memory limit");
        return NULL;
    }
    {
        xx_archive_record_state *s = ue2_records(f, opts, pd);
        mb_state *state;
        if (!s) return NULL;
        state = xx_mem_realloc(s->internal_state, sizeof(*state));
        if (!state) {
            ue2_free_records(f, s);
            return NULL;
        }
        s->internal_state = state;
        state->generation = ((xx_molebox *)f)->generation;
        return s;
    }
}
static const xx_archive_record *mb_current(Abstractformat *f, xx_archive_record_state *s)
{
    const xx_archive_record *r = ue2_current(f, s);
    mb_state *owned = s ? s->internal_state : NULL;
    ue2_state *state = owned ? &owned->records : NULL;
    mb_index *ix = f ? ((xx_molebox *)f)->index : NULL;
    if (!r || !state || !f->base_info_handled || !ix || state->index != &ix->records || owned->generation != ((xx_molebox *)f)->generation ||
        state->cursor >= ix->records.count)
        return NULL;
    {
        const ue2_member *m = &ix->records.members[state->cursor];
        const mb_package *p = &ix->packages[m->tag >> 32];
        char text[45];
        mb_credential(p->key, text);
        if (!xx_archive_record_set_meta_bool(&s->current_record, XX_META_ID_IS_ENCRYPTED, true) ||
            !xx_archive_record_set_meta_str(&s->current_record, XX_META_ID_OPT_PASSWORD, text))
            return NULL;
        if (!m->is_folder && (p->tree[(uint32_t)m->tag + 40] & 1))
            xx_archive_record_set_meta_str(&s->current_record, XX_META_ID_COMMENT, "MoleBox hidden name; exported using stored MD5 path signature");
    }
    return r;
}
static bool mb_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    return mb_current(f, s) && ue2_next(f, s, pd);
}
static ssize_t mb_write_sink(xx_io_device *d, const void *p, size_t n)
{
    mb_sink *s = d->priv;
    if (s->size > s->limit || n > s->limit - s->size) return -1;
    if (n) xx_rt_memcpy(s->bytes + s->size, p, n);
    s->size += n;
    return (ssize_t)n;
}
static bool mb_decode_block(Abstractformat *f, const mb_package *p, const uint8_t *b, uint8_t *raw, uint8_t *decoded, xx_pd_struct *pd)
{
    uint32_t packed = xx_data_get_u16(b, 2, 0, false), real = xx_data_get_u16(b + 2, 2, 0, false), flags = xx_data_get_u16(b + 4, 2, 0, false);
    uint64_t off = xx_data_get_u32(b + 8, 4, 0, false);
    if (p->base < 0 || p->tree_at < p->base || off > (uint64_t)(p->tree_at - p->base) || packed > (uint64_t)(p->tree_at - p->base) - off) return false;
    if (!ue2_read(f, p->base + (int64_t)off, raw, packed)) {
        return false;
    }
    if (flags & 4) mb_cbc(&p->cipher, raw, packed);
    if (flags & 2) {
        xx_io_device sink;
        mb_sink state = {decoded, 0, real};
        size_t consumed = 0, remaining;
        uint32_t adler = 1, s1 = 1, s2 = 0, j;
        if (packed < 6 || (raw[0] & 15) != 8 || (raw[0] >> 4) > 7 || (((unsigned)raw[0] * 256 + raw[1]) % 31) || raw[1] & 32) return false;
        xx_mem_zero(&sink, sizeof(sink));
        sink.priv = &state;
        sink.write = mb_write_sink;
        if (!xx_deflate_unpack_memory_to_device_ex(raw + 2, packed - 2, &sink, &consumed, false, pd) || state.size != real || consumed > packed - 6) return false;
        remaining = packed - consumed - 6;
        if (remaining > (flags & 4 ? 7U : 0U)) return false;
        for (j = 0; j < real; ++j) {
            s1 = (s1 + decoded[j]) % 65521;
            s2 = (s2 + s1) % 65521;
        }
        adler = (s2 << 16) | s1;
        if (adler != ((uint32_t)raw[consumed + 2] << 24 | (uint32_t)raw[consumed + 3] << 16 | (uint32_t)raw[consumed + 4] << 8 | raw[consumed + 5])) return false;
    } else xx_rt_memcpy(decoded, raw, real);
    return xx_crc16_arc_calc(0, decoded, real) == xx_data_get_u16(b + 6, 2, 0, false) && !(pd && xx_pd_is_stopped(pd));
}
static bool mb_unpack(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    const xx_archive_record *r = mb_current(f, s);
    ue2_state *state = s ? s->internal_state : NULL;
    mb_index *ix = ((xx_molebox *)f)->index;
    const ue2_member *m;
    const mb_package *p;
    const uint8_t *n;
    uint32_t count, i;
    uint8_t *raw = NULL, *data = NULL;
    const xx_var *option = NULL;
    const char *base = NULL;
    char *owned = NULL, *path = NULL;
    xx_io_device *out = NULL;
    bool ok = false;
    uint64_t done = 0;
    int level = -1;
    if (!r || !state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    m = &ix->records.members[state->cursor];
    p = &ix->packages[m->tag >> 32];
    n = p->tree + (uint32_t)m->tag;
    count = m->is_folder ? 0 : xx_data_get_u32(n + 48, 4, 0, false);
    for (i = 0; i < s->options.count; ++i) {
        const xx_meta *meta = xx_list_at((const xx_list_t *)&s->options, i);
        if (!meta) continue;
        if (meta->meta_id == XX_META_ID_OPT_UNPACK_PATH) option = &meta->var;
        if (meta->meta_id == XX_META_ID_OPT_MAX_MEMBER_SIZE && (uint64_t)m->original_size > xx_var_get_u64(&meta->var))
            return mb_fail(pd, "MoleBox member exceeds configured size limit");
    }
    raw = xx_mem_alloc(MB_BLOCK);
    data = xx_mem_alloc(MB_BLOCK);
    if (!raw || !data) goto done;
    level = xx_pd_enter_level(pd, (uint64_t)m->original_size, option ? "Extracting MoleBox member" : "Testing MoleBox member");
    for (i = 0; i < count; ++i) {
        const uint8_t *b = n + 52 + i * 12;
        if (!mb_decode_block(f, p, b, raw, data, pd)) goto done;
        done += xx_data_get_u16(b + 2, 2, 0, false);
        xx_pd_set_current(pd, level, done);
    }
    if (!option) {
        ok = done == (uint64_t)m->original_size;
        goto done;
    }
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (base && ue2_safe_name(m->name)) {
        path = xx_str_concat3(base, "/", m->name);
    }
    if (!path || !xx_store_create_dirs_a(path, m->is_folder)) goto done;
    if (m->is_folder) {
        ok = true;
        goto done;
    }
    out = xx_io_file_open(path, "wb");
    if (!out) goto done;
    done = 0;
    for (i = 0; i < count; ++i) {
        const uint8_t *b = n + 52 + i * 12;
        uint32_t real = xx_data_get_u16(b + 2, 2, 0, false);
        if (!mb_decode_block(f, p, b, raw, data, pd) || xx_io_write(out, data, real) != (ssize_t)real) goto done;
        done += real;
        xx_pd_set_current(pd, level, done);
    }
    ok = done == (uint64_t)m->original_size && !(pd && xx_pd_is_stopped(pd));
done:
    if (level >= 0) xx_pd_leave_level(pd, level);
    if (out) xx_io_close(out);
    xx_mem_free(raw);
    xx_mem_free(data);
    xx_str_free(owned);
    xx_str_free(path);
    if (!ok && (!pd || !xx_pd_is_stopped(pd))) mb_fail(pd, "MoleBox member CRC/data validation failed");
    return ok;
}
static void mb_destroy(Abstractformat *f)
{
    xx_molebox *a = (xx_molebox *)f;
    mb_free_index(a->index);
    a->index = NULL;
    xx_format_cleanup_extra_parameters(f);
}
void xx_molebox_init(xx_molebox *a, xx_io_device *d, int64_t b)
{
    if (!a) return;
    xx_mem_zero(a, sizeof(*a));
    ue2_init_format(&a->format, d, b, MB_TYPE, "svfs", "application/x-molebox");
    a->format.is_crypted = true;
    a->format.check_is_valid = mb_valid;
    a->format.handle_base_info = mb_info;
    a->format.create_archive_records_reading = mb_records;
    a->format.get_current_archive_record = mb_current;
    a->format.archive_record_move_to_next = mb_next;
    a->format.unpack_current_archive_record = mb_unpack;
    a->format.destroy = mb_destroy;
}
xx_molebox *xx_molebox_create(xx_io_device *d, int64_t b)
{
    xx_molebox *a = xx_mem_alloc(sizeof(*a));
    if (a) xx_molebox_init(a, d, b);
    return a;
}
void xx_molebox_destroy(xx_molebox *a)
{
    if (a) mb_destroy(&a->format);
}
void xx_molebox_free(xx_molebox *a)
{
    if (a) {
        xx_molebox_destroy(a);
        xx_mem_free(a);
    }
}
xx_file_type_t xx_molebox_detect_device(xx_io_device *d, const char *password, xx_pd_struct *pd)
{
    xx_molebox *a;
    int64_t saved;
    xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
    if (!d || !password || (pd && xx_pd_is_stopped(pd))) {
        return type;
    }
    saved = xx_io_tell(d);
    a = xx_molebox_create(d, 0);
    if (a && xx_format_set_password(&a->format, password) && mb_valid(&a->format, pd) && mb_info(&a->format, pd)) type = MB_TYPE;
    xx_molebox_free(a);
    if (saved >= 0) xx_io_seek64(d, saved, XX_RT_SEEK_SET);
    return type;
}
