/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Btrfs send stream reader.  xx_btrfs_stream.h carries the layout.
 *
 * Written from the published stream description (btrfs documentation,
 * "Send stream format").  The command replay mirrors what "btrfs receive"
 * does, but into an in-memory tree:
 *
 *   - nodes are inodes (regular file, directory, symlink, special);
 *   - dentries bind a name under a parent directory node to a node, so
 *     RENAME, LINK and UNLINK are cheap and hard links share contents;
 *   - every content change (WRITE, ENCODED_WRITE, CLONE, TRUNCATE, punch
 *     hole) is kept as an operation on its node, in stream order, pointing
 *     at the data inside the stream instead of copying it.
 *
 * Extraction applies a file's operations in order to the output, clipped to
 * the file's final size.  A CLONE reads its source "as of" the clone, which
 * is the source's operations older than the clone applied over zeros.
 *
 * Every length, count and offset is checked before use, every loop and
 * recursion is bounded, and memory grows only with what the stream holds.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/btrfs_stream/xx_btrfs_stream.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzo/xx_lzo.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef BTRFS_STREAM
#define XX_BTRFS_STREAM_FILE_TYPE XX_FILE_TYPE_BTRFS_STREAM
#else
#define XX_BTRFS_STREAM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define BS_MAGIC_SIZE 13U
#define BS_HEADER_SIZE 17U
#define BS_CMD_HEADER 10U
/* The kernel never emits a command above 16 KiB + 128 KiB (v2 encoded
 * write); 1 MiB leaves room and still bounds the payload buffer. */
#define BS_MAX_CMD 0x100000U
/* The first command (SUBVOL / SNAPSHOT) is a path, a UUID and a transid. */
#define BS_PROBE_MAX_CMD 0x10000U
#define BS_MAX_COMMANDS 0x1000000UL
#define BS_MAX_NODES 0x100000U
#define BS_MAX_DENTRIES 0x200000U
#define BS_MAX_OPS 0x200000U
#define BS_MAX_PATH 4095U
#define BS_MAX_NAME 255U
#define BS_MAX_DEPTH 256U
#define BS_MAX_FILE_SIZE UINT64_C(0x100000000)
/* BTRFS_MAX_UNCOMPRESSED is 128 KiB; allow more but stay bounded. */
#define BS_MAX_UNENCODED 0x100000U
#define BS_CHUNK 0x10000U
#define BS_CLONE_DEPTH 8U
#define BS_STEP_BUDGET 50000000UL
#define BS_NONE 0xFFFFFFFFU

enum {
    BS_C_UNSPEC = 0,
    BS_C_SUBVOL = 1,
    BS_C_SNAPSHOT = 2,
    BS_C_MKFILE = 3,
    BS_C_MKDIR = 4,
    BS_C_MKNOD = 5,
    BS_C_MKFIFO = 6,
    BS_C_MKSOCK = 7,
    BS_C_SYMLINK = 8,
    BS_C_RENAME = 9,
    BS_C_LINK = 10,
    BS_C_UNLINK = 11,
    BS_C_RMDIR = 12,
    BS_C_SET_XATTR = 13,
    BS_C_REMOVE_XATTR = 14,
    BS_C_WRITE = 15,
    BS_C_CLONE = 16,
    BS_C_TRUNCATE = 17,
    BS_C_CHMOD = 18,
    BS_C_CHOWN = 19,
    BS_C_UTIMES = 20,
    BS_C_END = 21,
    BS_C_UPDATE_EXTENT = 22,
    BS_C_FALLOCATE = 23,
    BS_C_FILEATTR = 24,
    BS_C_ENCODED_WRITE = 25,
    BS_C_ENABLE_VERITY = 26
};

enum {
    BS_A_UUID = 1,
    BS_A_CTRANSID = 2,
    BS_A_INO = 3,
    BS_A_SIZE = 4,
    BS_A_MODE = 5,
    BS_A_UID = 6,
    BS_A_GID = 7,
    BS_A_RDEV = 8,
    BS_A_CTIME = 9,
    BS_A_MTIME = 10,
    BS_A_ATIME = 11,
    BS_A_OTIME = 12,
    BS_A_XATTR_NAME = 13,
    BS_A_XATTR_DATA = 14,
    BS_A_PATH = 15,
    BS_A_PATH_TO = 16,
    BS_A_PATH_LINK = 17,
    BS_A_FILE_OFFSET = 18,
    BS_A_DATA = 19,
    BS_A_CLONE_UUID = 20,
    BS_A_CLONE_CTRANSID = 21,
    BS_A_CLONE_PATH = 22,
    BS_A_CLONE_OFFSET = 23,
    BS_A_CLONE_LEN = 24,
    BS_A_FALLOCATE_MODE = 25,
    BS_A_FILEATTR = 26,
    BS_A_UNENCODED_FILE_LEN = 27,
    BS_A_UNENCODED_LEN = 28,
    BS_A_UNENCODED_OFFSET = 29,
    BS_A_COMPRESSION = 30,
    BS_A_ENCRYPTION = 31,
    BS_A_COUNT = 36
};

#define BS_FALLOC_KEEP_SIZE 0x1U
#define BS_FALLOC_PUNCH_HOLE 0x2U

enum { BS_T_FILE = 0, BS_T_DIR = 1, BS_T_LINK = 2, BS_T_SPECIAL = 3 };
enum { BS_OP_WRITE = 0, BS_OP_ENCODED, BS_OP_CLONE, BS_OP_TRUNC, BS_OP_ZERO };

typedef struct bs_op_s {
    uint64_t off;   /**< File offset (TRUNC: new size). */
    uint64_t len;   /**< Bytes affected in the file. */
    uint64_t a;     /**< ENCODED: unencoded offset; CLONE: source offset. */
    uint64_t b;     /**< ENCODED: unencoded length; CLONE: source op limit. */
    int64_t data;   /**< Device offset of the data (WRITE / ENCODED). */
    uint32_t data_len;
    uint32_t next;  /**< Next op of the same node. */
    uint32_t src;   /**< CLONE: source node. */
    uint8_t kind;
    uint8_t comp;
} bs_op;

typedef struct bs_node_s {
    uint64_t size;
    int64_t mtime;
    uint32_t mode;
    uint32_t first_op;
    uint32_t last_op;
    uint32_t dentry;  /**< Directory's own dentry (BS_NONE for the root). */
    char *link;
    uint8_t type;
    bool oversize;
    bool unresolved;
} bs_node;

typedef struct bs_dentry_s {
    char *name;
    uint32_t parent;
    uint32_t node;
    uint32_t hnext;
    bool alive;
} bs_dentry;

typedef struct bs_attr_s {
    const uint8_t *p;
    uint32_t len;
    bool present;
} bs_attr;

typedef struct bs_tree_s {
    bs_node *nodes;
    size_t node_count, node_cap;
    bs_dentry *dents;
    size_t dent_count, dent_cap;
    uint32_t *buckets;
    size_t bucket_count;
    bs_op *ops;
    size_t op_count, op_cap;
    uint8_t *buf;
    uint8_t subvol_uuid[16];
    bool have_uuid;
    uint32_t version;
    bool incremental;
    bool damaged;
    bool has_end;
    int64_t end;       /**< Stream bytes consumed (relative to the start). */
    uint64_t commands;
} bs_tree;

typedef struct bs_entry_s {
    uint32_t dentry;
    char *path;
} bs_entry;

typedef struct bs_stream_s {
    bs_tree tree;
    bs_entry *entries;
    size_t count;
    size_t index;
} bs_stream;

/* ------------------------------------------------------------ helpers --- */

static bool bs_read_at(xx_io_device *device, int64_t offset, void *buffer,
                       size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

uint32_t xx_btrfs_stream_crc32c(uint32_t crc, const void *data, size_t size) {
    /* xx_crc32c_calc inverts on entry and exit; undo both. */
    return ~xx_crc32c_calc(~crc, data, size);
}

static bool bs_grow(void **array, size_t *capacity, size_t count,
                    size_t element, size_t limit) {
    size_t wanted;
    void *grown;
    if (count < *capacity) return true;
    if (count >= limit) return false;
    wanted = *capacity ? *capacity * 2U : 64U;
    if (wanted > limit) wanted = limit;
    if (wanted > ((size_t)-1) / element) return false;
    grown = xx_mem_realloc(*array, wanted * element);
    if (!grown) return false;
    *array = grown;
    *capacity = wanted;
    return true;
}

/* --------------------------------------------------------------- tree --- */

static void bs_tree_free(bs_tree *tree) {
    size_t index;
    if (!tree) return;
    for (index = 0U; index < tree->node_count; ++index)
        if (tree->nodes[index].link) xx_mem_free(tree->nodes[index].link);
    for (index = 0U; index < tree->dent_count; ++index)
        if (tree->dents[index].name) xx_mem_free(tree->dents[index].name);
    if (tree->nodes) xx_mem_free(tree->nodes);
    if (tree->dents) xx_mem_free(tree->dents);
    if (tree->buckets) xx_mem_free(tree->buckets);
    if (tree->ops) xx_mem_free(tree->ops);
    if (tree->buf) xx_mem_free(tree->buf);
    xx_mem_zero(tree, sizeof(*tree));
}

static uint32_t bs_hash(uint32_t parent, const char *name, size_t length) {
    uint32_t hash = 2166136261U ^ (parent * 0x9E3779B1U);
    size_t index;
    for (index = 0U; index < length; ++index) {
        hash ^= (uint8_t)name[index];
        hash *= 16777619U;
    }
    return hash;
}

static bool bs_rehash(bs_tree *tree, size_t buckets) {
    uint32_t *table;
    size_t index;
    table = (uint32_t *)xx_mem_alloc(buckets * sizeof(uint32_t));
    if (!table) return false;
    for (index = 0U; index < buckets; ++index) table[index] = BS_NONE;
    for (index = 0U; index < tree->dent_count; ++index) {
        bs_dentry *d = &tree->dents[index];
        uint32_t slot;
        if (!d->alive) continue;
        slot = bs_hash(d->parent, d->name, xx_str_len(d->name)) &
               (uint32_t)(buckets - 1U);
        d->hnext = table[slot];
        table[slot] = (uint32_t)index;
    }
    if (tree->buckets) xx_mem_free(tree->buckets);
    tree->buckets = table;
    tree->bucket_count = buckets;
    return true;
}

static uint32_t bs_lookup(const bs_tree *tree, uint32_t parent,
                          const char *name, size_t length) {
    uint32_t index;
    size_t steps = 0U;
    if (!tree->buckets) return BS_NONE;
    index = tree->buckets[bs_hash(parent, name, length) &
                          (uint32_t)(tree->bucket_count - 1U)];
    while (index != BS_NONE && steps++ <= tree->dent_count) {
        const bs_dentry *d = &tree->dents[index];
        if (d->parent == parent && xx_rt_memcmp(d->name, name, length) == 0 &&
            d->name[length] == 0)
            return index;
        index = d->hnext;
    }
    return BS_NONE;
}

static void bs_unhash(bs_tree *tree, uint32_t dentry) {
    bs_dentry *d = &tree->dents[dentry];
    uint32_t *link;
    size_t steps = 0U;
    if (!tree->buckets) return;
    link = &tree->buckets[bs_hash(d->parent, d->name, xx_str_len(d->name)) &
                          (uint32_t)(tree->bucket_count - 1U)];
    while (*link != BS_NONE && steps++ <= tree->dent_count) {
        if (*link == dentry) {
            *link = d->hnext;
            break;
        }
        link = &tree->dents[*link].hnext;
    }
    d->hnext = BS_NONE;
}

static void bs_hash_in(bs_tree *tree, uint32_t dentry) {
    bs_dentry *d = &tree->dents[dentry];
    uint32_t slot = bs_hash(d->parent, d->name, xx_str_len(d->name)) &
                    (uint32_t)(tree->bucket_count - 1U);
    d->hnext = tree->buckets[slot];
    tree->buckets[slot] = dentry;
}

static uint32_t bs_new_node(bs_tree *tree, uint8_t type) {
    bs_node *node;
    if (!bs_grow((void **)&tree->nodes, &tree->node_cap, tree->node_count,
                 sizeof(bs_node), BS_MAX_NODES))
        return BS_NONE;
    node = &tree->nodes[tree->node_count];
    xx_mem_zero(node, sizeof(*node));
    node->type = type;
    node->mode = type == BS_T_DIR ? 040755U : 0100644U;
    node->first_op = node->last_op = node->dentry = BS_NONE;
    return (uint32_t)tree->node_count++;
}

static uint32_t bs_new_dentry(bs_tree *tree, uint32_t parent,
                              const char *name, size_t length,
                              uint32_t node) {
    bs_dentry *d;
    char *copy;
    if (!bs_grow((void **)&tree->dents, &tree->dent_cap, tree->dent_count,
                 sizeof(bs_dentry), BS_MAX_DENTRIES))
        return BS_NONE;
    if (tree->dent_count + 1U > tree->bucket_count / 2U &&
        !bs_rehash(tree, tree->bucket_count ? tree->bucket_count * 2U : 256U))
        return BS_NONE;
    copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return BS_NONE;
    xx_mem_copy(copy, name, length);
    copy[length] = 0;
    d = &tree->dents[tree->dent_count];
    d->name = copy;
    d->parent = parent;
    d->node = node;
    d->alive = true;
    d->hnext = BS_NONE;
    bs_hash_in(tree, (uint32_t)tree->dent_count);
    if (tree->nodes[node].type == BS_T_DIR)
        tree->nodes[node].dentry = (uint32_t)tree->dent_count;
    return (uint32_t)tree->dent_count++;
}

static void bs_kill_dentry(bs_tree *tree, uint32_t dentry) {
    bs_dentry *d = &tree->dents[dentry];
    if (!d->alive) return;
    bs_unhash(tree, dentry);
    d->alive = false;
    if (tree->nodes[d->node].dentry == dentry)
        tree->nodes[d->node].dentry = BS_NONE;
}

/* A path component: 1..255 bytes, no separator, no NUL, not a dot name. */
static bool bs_component_ok(const uint8_t *p, size_t length) {
    size_t index;
    if (length == 0U || length > BS_MAX_NAME) return false;
    if (length == 1U && p[0] == '.') return false;
    if (length == 2U && p[0] == '.' && p[1] == '.') return false;
    for (index = 0U; index < length; ++index)
        if (p[index] == 0U || p[index] == '/') return false;
    return true;
}

/* Resolve every component but the last.  In an incremental stream a
 * missing directory stands for one the parent snapshot holds, so it is
 * created on the fly; a full stream always creates parents first. */
static bool bs_resolve_parent(bs_tree *tree, const bs_attr *path,
                              uint32_t *parent, const char **name,
                              size_t *name_length) {
    const uint8_t *p;
    size_t length, start = 0U, index, depth = 0U;
    uint32_t dir = 0U;
    if (!path || !path->present || path->len == 0U || path->len > BS_MAX_PATH)
        return false;
    p = path->p;
    length = path->len;
    for (index = 0U; index <= length; ++index) {
        if (index < length && p[index] != '/') continue;
        if (!bs_component_ok(p + start, index - start)) return false;
        if (index == length) {
            *parent = dir;
            *name = (const char *)(p + start);
            *name_length = index - start;
            return true;
        }
        if (++depth >= BS_MAX_DEPTH) return false;
        {
            uint32_t d = bs_lookup(tree, dir, (const char *)(p + start),
                                   index - start);
            if (d == BS_NONE) {
                uint32_t node;
                if (!tree->incremental) return false;
                node = bs_new_node(tree, BS_T_DIR);
                if (node == BS_NONE) return false;
                d = bs_new_dentry(tree, dir, (const char *)(p + start),
                                  index - start, node);
                if (d == BS_NONE) return false;
            }
            if (tree->nodes[tree->dents[d].node].type != BS_T_DIR) return false;
            dir = tree->dents[d].node;
        }
        start = index + 1U;
    }
    return false;
}

/* The dentry a path names, or BS_NONE.  In an incremental stream an
 * unknown path is created as a regular file of the parent snapshot when
 * @p implicit is set. */
static uint32_t bs_find(bs_tree *tree, const bs_attr *path, bool implicit) {
    uint32_t parent, d;
    const char *name;
    size_t length;
    if (!bs_resolve_parent(tree, path, &parent, &name, &length)) return BS_NONE;
    d = bs_lookup(tree, parent, name, length);
    if (d == BS_NONE && implicit && tree->incremental) {
        uint32_t node = bs_new_node(tree, BS_T_FILE);
        if (node == BS_NONE) return BS_NONE;
        d = bs_new_dentry(tree, parent, name, length, node);
    }
    return d;
}

static bool bs_add_op(bs_tree *tree, uint32_t node, const bs_op *op) {
    bs_node *n = &tree->nodes[node];
    if (!bs_grow((void **)&tree->ops, &tree->op_cap, tree->op_count,
                 sizeof(bs_op), BS_MAX_OPS))
        return false;
    tree->ops[tree->op_count] = *op;
    tree->ops[tree->op_count].next = BS_NONE;
    if (n->last_op == BS_NONE) n->first_op = (uint32_t)tree->op_count;
    else tree->ops[n->last_op].next = (uint32_t)tree->op_count;
    n->last_op = (uint32_t)tree->op_count;
    ++tree->op_count;
    return true;
}

static void bs_extend(bs_node *node, uint64_t off, uint64_t len) {
    uint64_t end;
    if (len > BS_MAX_FILE_SIZE || off > BS_MAX_FILE_SIZE - len) {
        node->oversize = true;
        return;
    }
    end = off + len;
    if (end > node->size) node->size = end;
}

/* ----------------------------------------------------------- commands --- */

static bool bs_parse_tlvs(const bs_tree *tree, const uint8_t *payload,
                          uint32_t length, bs_attr *attrs) {
    uint32_t pos = 0U;
    xx_mem_zero(attrs, sizeof(bs_attr) * BS_A_COUNT);
    while (pos < length) {
        uint16_t type, tlen;
        if (length - pos < 2U) return false;
        type = xx_data_get_u16(payload + pos, 2, 0, false);
        if (tree->version >= 2U && type == BS_A_DATA) {
            attrs[type].p = payload + pos + 2U;
            attrs[type].len = length - pos - 2U;
            attrs[type].present = true;
            return true;
        }
        if (length - pos < 4U) return false;
        tlen = xx_data_get_u16(payload + pos + 2U, 2, 0, false);
        if ((uint32_t)tlen > length - pos - 4U) return false;
        if (type < BS_A_COUNT && !attrs[type].present) {
            attrs[type].p = payload + pos + 4U;
            attrs[type].len = tlen;
            attrs[type].present = true;
        }
        pos += 4U + tlen;
    }
    return true;
}

static bool bs_u64(const bs_attr *attrs, int type, uint64_t *value) {
    if (!attrs[type].present || attrs[type].len != 8U) return false;
    *value = xx_data_get_u64(attrs[type].p, 8, 0, false);
    return true;
}

static bool bs_u32(const bs_attr *attrs, int type, uint32_t *value) {
    if (!attrs[type].present) return false;
    if (attrs[type].len == 4U) *value = xx_data_get_u32(attrs[type].p, 4, 0, false);
    else if (attrs[type].len == 8U) {
        uint64_t wide = xx_data_get_u64(attrs[type].p, 8, 0, false);
        if (wide > 0xFFFFFFFFU) return false;
        *value = (uint32_t)wide;
    } else return false;
    return true;
}

static bool bs_cmd_create(bs_tree *tree, uint16_t cmd, const bs_attr *attrs) {
    uint32_t parent, node, d;
    const char *name;
    size_t length;
    uint8_t type;
    uint64_t mode;
    if (!bs_resolve_parent(tree, &attrs[BS_A_PATH], &parent, &name, &length))
        return false;
    type = cmd == BS_C_MKFILE ? BS_T_FILE
           : cmd == BS_C_MKDIR ? BS_T_DIR
           : cmd == BS_C_SYMLINK ? BS_T_LINK : BS_T_SPECIAL;
    d = bs_lookup(tree, parent, name, length);
    if (d != BS_NONE) {
        /* An incremental stream may recreate a name it just cleared up;
         * a full stream never creates a name twice. */
        if (!tree->incremental) return false;
        bs_kill_dentry(tree, d);
    }
    node = bs_new_node(tree, type);
    if (node == BS_NONE) return false;
    if (type == BS_T_SPECIAL && bs_u64(attrs, BS_A_MODE, &mode))
        tree->nodes[node].mode = (uint32_t)mode;
    if (type == BS_T_LINK) {
        const bs_attr *target = &attrs[BS_A_PATH_LINK];
        char *copy;
        uint32_t index;
        if (!target->present || target->len > BS_MAX_PATH) return false;
        for (index = 0U; index < target->len; ++index)
            if (target->p[index] == 0U) return false;
        copy = (char *)xx_mem_alloc((size_t)target->len + 1U);
        if (!copy) return false;
        if (target->len) xx_mem_copy(copy, target->p, target->len);
        copy[target->len] = 0;
        tree->nodes[node].link = copy;
        tree->nodes[node].size = target->len;
        tree->nodes[node].mode = 0120777U;
    }
    return bs_new_dentry(tree, parent, name, length, node) != BS_NONE;
}

static bool bs_cmd_rename(bs_tree *tree, const bs_attr *attrs) {
    uint32_t from, parent, existing, moved, walk;
    const char *name;
    size_t length, steps;
    char *copy;
    from = bs_find(tree, &attrs[BS_A_PATH], true);
    if (from == BS_NONE ||
        !bs_resolve_parent(tree, &attrs[BS_A_PATH_TO], &parent, &name, &length))
        return false;
    moved = tree->dents[from].node;
    /* A directory must not move below itself. */
    if (tree->nodes[moved].type == BS_T_DIR) {
        walk = parent;
        for (steps = 0U; walk != 0U; ++steps) {
            uint32_t up;
            if (walk == moved || steps > BS_MAX_DEPTH) return false;
            up = tree->nodes[walk].dentry;
            if (up == BS_NONE || !tree->dents[up].alive) return false;
            walk = tree->dents[up].parent;
        }
    }
    existing = bs_lookup(tree, parent, name, length);
    if (existing == from) return true;
    copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return false;
    xx_mem_copy(copy, name, length);
    copy[length] = 0;
    if (existing != BS_NONE) bs_kill_dentry(tree, existing);
    bs_unhash(tree, from);
    xx_mem_free(tree->dents[from].name);
    tree->dents[from].name = copy;
    tree->dents[from].parent = parent;
    bs_hash_in(tree, from);
    return true;
}

static bool bs_cmd_link(bs_tree *tree, const bs_attr *attrs) {
    uint32_t target, parent;
    const char *name;
    size_t length;
    target = bs_find(tree, &attrs[BS_A_PATH_LINK], true);
    if (target == BS_NONE ||
        tree->nodes[tree->dents[target].node].type == BS_T_DIR ||
        !bs_resolve_parent(tree, &attrs[BS_A_PATH], &parent, &name, &length) ||
        bs_lookup(tree, parent, name, length) != BS_NONE)
        return false;
    return bs_new_dentry(tree, parent, name, length,
                         tree->dents[target].node) != BS_NONE;
}

static bool bs_cmd_remove(bs_tree *tree, const bs_attr *attrs, bool dir) {
    uint32_t d = bs_find(tree, &attrs[BS_A_PATH], false);
    if (d == BS_NONE) return tree->incremental;
    if ((tree->nodes[tree->dents[d].node].type == BS_T_DIR) != dir) return false;
    bs_kill_dentry(tree, d);
    return true;
}

static uint32_t bs_file_node(bs_tree *tree, const bs_attr *attrs) {
    uint32_t d = bs_find(tree, &attrs[BS_A_PATH], true);
    uint32_t node;
    if (d == BS_NONE) return BS_NONE;
    node = tree->dents[d].node;
    return tree->nodes[node].type == BS_T_FILE ? node : BS_NONE;
}

static bool bs_cmd_write(bs_tree *tree, const bs_attr *attrs,
                         const uint8_t *payload, int64_t payload_offset) {
    uint32_t node = bs_file_node(tree, attrs);
    uint64_t off;
    bs_op op;
    if (node == BS_NONE || !bs_u64(attrs, BS_A_FILE_OFFSET, &off) ||
        !attrs[BS_A_DATA].present)
        return false;
    if (attrs[BS_A_DATA].len == 0U) return true;
    xx_mem_zero(&op, sizeof(op));
    op.kind = BS_OP_WRITE;
    op.off = off;
    op.len = attrs[BS_A_DATA].len;
    op.data_len = attrs[BS_A_DATA].len;
    op.data = payload_offset + (int64_t)(attrs[BS_A_DATA].p - payload);
    bs_extend(&tree->nodes[node], op.off, op.len);
    return bs_add_op(tree, node, &op);
}

static bool bs_cmd_encoded(bs_tree *tree, const bs_attr *attrs,
                           const uint8_t *payload, int64_t payload_offset) {
    uint32_t node = bs_file_node(tree, attrs);
    uint64_t off, file_len, ulen, uoff;
    uint32_t comp, enc = 0U;
    bs_op op;
    if (node == BS_NONE || !bs_u64(attrs, BS_A_FILE_OFFSET, &off) ||
        !bs_u64(attrs, BS_A_UNENCODED_FILE_LEN, &file_len) ||
        !bs_u64(attrs, BS_A_UNENCODED_LEN, &ulen) ||
        !bs_u64(attrs, BS_A_UNENCODED_OFFSET, &uoff) ||
        !bs_u32(attrs, BS_A_COMPRESSION, &comp) ||
        !attrs[BS_A_DATA].present)
        return false;
    if (attrs[BS_A_ENCRYPTION].present &&
        (!bs_u32(attrs, BS_A_ENCRYPTION, &enc) || enc != 0U))
        return false;
    if (comp > 7U || ulen == 0U || ulen > BS_MAX_UNENCODED ||
        uoff > ulen || file_len > ulen - uoff)
        return false;
    if (file_len == 0U) return true;
    xx_mem_zero(&op, sizeof(op));
    op.kind = BS_OP_ENCODED;
    op.off = off;
    op.len = file_len;
    op.a = uoff;
    op.b = ulen;
    op.comp = (uint8_t)comp;
    op.data_len = attrs[BS_A_DATA].len;
    op.data = payload_offset + (int64_t)(attrs[BS_A_DATA].p - payload);
    bs_extend(&tree->nodes[node], op.off, op.len);
    return bs_add_op(tree, node, &op);
}

static bool bs_cmd_clone(bs_tree *tree, const bs_attr *attrs) {
    uint32_t node = bs_file_node(tree, attrs), src_d;
    uint64_t off, len, src_off;
    bs_op op;
    bool same_subvol;
    if (node == BS_NONE || !bs_u64(attrs, BS_A_FILE_OFFSET, &off) ||
        !bs_u64(attrs, BS_A_CLONE_LEN, &len) ||
        !bs_u64(attrs, BS_A_CLONE_OFFSET, &src_off))
        return false;
    if (len == 0U) return true;
    xx_mem_zero(&op, sizeof(op));
    op.kind = BS_OP_CLONE;
    op.off = off;
    op.len = len;
    op.a = src_off;
    op.b = tree->op_count;
    op.src = BS_NONE;
    same_subvol = !attrs[BS_A_CLONE_UUID].present ||
                  (attrs[BS_A_CLONE_UUID].len == 16U && tree->have_uuid &&
                   xx_rt_memcmp(attrs[BS_A_CLONE_UUID].p, tree->subvol_uuid,
                                16U) == 0);
    if (same_subvol) {
        src_d = bs_find(tree, &attrs[BS_A_CLONE_PATH], false);
        if (src_d != BS_NONE &&
            tree->nodes[tree->dents[src_d].node].type == BS_T_FILE)
            op.src = tree->dents[src_d].node;
    }
    if (op.src == BS_NONE || len > BS_MAX_FILE_SIZE ||
        src_off > BS_MAX_FILE_SIZE - len)
        tree->nodes[node].unresolved = true;
    bs_extend(&tree->nodes[node], op.off, op.len);
    return bs_add_op(tree, node, &op);
}

static bool bs_cmd_truncate(bs_tree *tree, const bs_attr *attrs) {
    uint32_t node = bs_file_node(tree, attrs);
    uint64_t size;
    bs_op op;
    if (node == BS_NONE || !bs_u64(attrs, BS_A_SIZE, &size)) return false;
    if (size > BS_MAX_FILE_SIZE) {
        tree->nodes[node].oversize = true;
        return true;
    }
    xx_mem_zero(&op, sizeof(op));
    op.kind = BS_OP_TRUNC;
    op.off = size;
    tree->nodes[node].size = size;
    return bs_add_op(tree, node, &op);
}

static bool bs_cmd_fallocate(bs_tree *tree, const bs_attr *attrs) {
    uint32_t node = bs_file_node(tree, attrs), mode;
    uint64_t off, len;
    if (node == BS_NONE || !bs_u32(attrs, BS_A_FALLOCATE_MODE, &mode) ||
        !bs_u64(attrs, BS_A_FILE_OFFSET, &off) ||
        !bs_u64(attrs, BS_A_SIZE, &len))
        return false;
    if (len == 0U) return true;
    if ((mode & BS_FALLOC_PUNCH_HOLE) != 0U) {
        bs_op op;
        xx_mem_zero(&op, sizeof(op));
        op.kind = BS_OP_ZERO;
        op.off = off;
        op.len = len;
        if (!bs_add_op(tree, node, &op)) return false;
    }
    if ((mode & BS_FALLOC_KEEP_SIZE) == 0U) bs_extend(&tree->nodes[node], off, len);
    return true;
}

static bool bs_cmd_meta(bs_tree *tree, uint16_t cmd, const bs_attr *attrs) {
    uint32_t d = bs_find(tree, &attrs[BS_A_PATH], true);
    bs_node *node;
    uint64_t value;
    if (d == BS_NONE) return false;
    node = &tree->nodes[tree->dents[d].node];
    if (cmd == BS_C_CHMOD) {
        if (!bs_u64(attrs, BS_A_MODE, &value)) return false;
        node->mode = (node->mode & ~07777U) | ((uint32_t)value & 07777U);
    } else if (cmd == BS_C_UTIMES) {
        if (!attrs[BS_A_MTIME].present || attrs[BS_A_MTIME].len != 12U)
            return false;
        node->mtime = (int64_t)xx_data_get_u64(attrs[BS_A_MTIME].p, 8, 0, false);
    }
    return true;
}

/* Apply one command.  False marks the command as not applicable (the
 * stream is then flagged damaged, and parsing goes on). */
static bool bs_apply(bs_tree *tree, uint16_t cmd, const uint8_t *payload,
                     uint32_t length, int64_t payload_offset) {
    bs_attr attrs[BS_A_COUNT];
    if (!bs_parse_tlvs(tree, payload, length, attrs)) return false;
    switch (cmd) {
    case BS_C_SUBVOL:
    case BS_C_SNAPSHOT:
        if (attrs[BS_A_UUID].present && attrs[BS_A_UUID].len == 16U &&
            !tree->have_uuid) {
            xx_mem_copy(tree->subvol_uuid, attrs[BS_A_UUID].p, 16U);
            tree->have_uuid = true;
        }
        return true;
    case BS_C_MKFILE:
    case BS_C_MKDIR:
    case BS_C_MKNOD:
    case BS_C_MKFIFO:
    case BS_C_MKSOCK:
    case BS_C_SYMLINK:
        return bs_cmd_create(tree, cmd, attrs);
    case BS_C_RENAME:
        return bs_cmd_rename(tree, attrs);
    case BS_C_LINK:
        return bs_cmd_link(tree, attrs);
    case BS_C_UNLINK:
        return bs_cmd_remove(tree, attrs, false);
    case BS_C_RMDIR:
        return bs_cmd_remove(tree, attrs, true);
    case BS_C_WRITE:
        return bs_cmd_write(tree, attrs, payload, payload_offset);
    case BS_C_ENCODED_WRITE:
        return bs_cmd_encoded(tree, attrs, payload, payload_offset);
    case BS_C_CLONE:
        return bs_cmd_clone(tree, attrs);
    case BS_C_TRUNCATE:
        return bs_cmd_truncate(tree, attrs);
    case BS_C_FALLOCATE:
        return bs_cmd_fallocate(tree, attrs);
    case BS_C_CHMOD:
    case BS_C_UTIMES:
        return bs_cmd_meta(tree, cmd, attrs);
    default:
        /* CHOWN, xattrs, UPDATE_EXTENT, FILEATTR, ENABLE_VERITY, END:
         * nothing to rebuild. */
        return true;
    }
}

/* Header and first command only: cheap enough for detection. */
static bool bs_probe(Abstractformat *format, uint32_t *version_out) {
    uint8_t head[BS_HEADER_SIZE + BS_CMD_HEADER];
    uint8_t *payload;
    int64_t total, size;
    uint32_t version, length, crc, computed;
    uint16_t cmd;
    bool ok;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)sizeof(head) ||
        !bs_read_at(format->device, format->base_address, head, sizeof(head)) ||
        xx_rt_memcmp(head, "btrfs-stream", BS_MAGIC_SIZE) != 0)
        return false;
    version = xx_data_get_u32(head + BS_MAGIC_SIZE, 4, 0, false);
    if (version < 1U || version > 3U) return false;
    length = xx_data_get_u32(head + BS_HEADER_SIZE, 4, 0, false);
    cmd = xx_data_get_u16(head + BS_HEADER_SIZE + 4U, 2, 0, false);
    crc = xx_data_get_u32(head + BS_HEADER_SIZE + 6U, 4, 0, false);
    if ((cmd != BS_C_SUBVOL && cmd != BS_C_SNAPSHOT) || length == 0U ||
        length > BS_PROBE_MAX_CMD ||
        (int64_t)length > size - (int64_t)sizeof(head))
        return false;
    payload = (uint8_t *)xx_mem_alloc(length);
    if (!payload) return false;
    ok = bs_read_at(format->device,
                    format->base_address + (int64_t)sizeof(head), payload,
                    length);
    if (ok) {
        head[BS_HEADER_SIZE + 6U] = head[BS_HEADER_SIZE + 7U] =
            head[BS_HEADER_SIZE + 8U] = head[BS_HEADER_SIZE + 9U] = 0U;
        computed = xx_btrfs_stream_crc32c(0U, head + BS_HEADER_SIZE,
                                          BS_CMD_HEADER);
        computed = xx_btrfs_stream_crc32c(computed, payload, length);
        ok = computed == crc;
    }
    xx_mem_free(payload);
    if (ok && version_out) *version_out = version;
    return ok;
}

static bool bs_parse(Abstractformat *format, bs_tree *tree, xx_pd_struct *pd) {
    uint8_t header[BS_CMD_HEADER];
    int64_t total, pos, base;
    uint32_t root;
    xx_mem_zero(tree, sizeof(*tree));
    if (!bs_probe(format, &tree->version)) return false;
    base = format->base_address;
    total = xx_io_total_size(format->device) - base;
    tree->buf = (uint8_t *)xx_mem_alloc(BS_MAX_CMD);
    root = tree->buf ? bs_new_node(tree, BS_T_DIR) : BS_NONE;
    if (root != 0U || !bs_rehash(tree, 256U)) {
        bs_tree_free(tree);
        return false;
    }
    pos = BS_HEADER_SIZE;
    while (total - pos >= (int64_t)BS_CMD_HEADER) {
        uint32_t length, crc, computed;
        uint16_t cmd;
        if ((tree->commands & 0xFFFU) == 0xFFFU && pd && xx_pd_is_stopped(pd)) {
            bs_tree_free(tree);
            return false;
        }
        if (tree->commands >= BS_MAX_COMMANDS ||
            !bs_read_at(format->device, base + pos, header, sizeof(header))) {
            tree->damaged = true;
            break;
        }
        length = xx_data_get_u32(header, 4, 0, false);
        cmd = xx_data_get_u16(header + 4U, 2, 0, false);
        crc = xx_data_get_u32(header + 6U, 4, 0, false);
        if (cmd == BS_C_UNSPEC || cmd > BS_C_ENABLE_VERITY ||
            length > BS_MAX_CMD ||
            (int64_t)length > total - pos - (int64_t)BS_CMD_HEADER ||
            (length && !bs_read_at(format->device,
                                   base + pos + (int64_t)BS_CMD_HEADER,
                                   tree->buf, length))) {
            tree->damaged = true;
            break;
        }
        header[6] = header[7] = header[8] = header[9] = 0U;
        computed = xx_btrfs_stream_crc32c(0U, header, sizeof(header));
        computed = xx_btrfs_stream_crc32c(computed, tree->buf, length);
        if (computed != crc) {
            tree->damaged = true;
            break;
        }
        if (tree->commands == 0U && cmd == BS_C_SNAPSHOT) tree->incremental = true;
        ++tree->commands;
        if (!bs_apply(tree, cmd, tree->buf, length,
                      base + pos + (int64_t)BS_CMD_HEADER))
            tree->damaged = true;
        pos += (int64_t)BS_CMD_HEADER + (int64_t)length;
        if (cmd == BS_C_END) {
            tree->has_end = true;
            break;
        }
    }
    if (!tree->has_end && pos != total) tree->damaged = true;
    tree->end = pos;
    xx_mem_free(tree->buf);
    tree->buf = NULL;
    return tree->commands != 0U;
}

/* Full path of a live dentry, or NULL when an ancestor was removed. */
static char *bs_path_of(const bs_tree *tree, uint32_t dentry) {
    uint32_t chain[BS_MAX_DEPTH];
    size_t depth = 0U, total = 0U, index;
    uint32_t cur = dentry;
    char *path, *out;
    for (;;) {
        const bs_dentry *d;
        uint32_t parent;
        if (cur == BS_NONE || cur >= tree->dent_count || depth >= BS_MAX_DEPTH)
            return NULL;
        d = &tree->dents[cur];
        if (!d->alive) return NULL;
        chain[depth++] = cur;
        total += xx_str_len(d->name) + 1U;
        if (total > BS_MAX_PATH + 1U) return NULL;
        parent = d->parent;
        if (parent == 0U) break;
        cur = tree->nodes[parent].dentry;
    }
    path = (char *)xx_mem_alloc(total);
    if (!path) return NULL;
    out = path;
    for (index = depth; index-- > 0U;) {
        const char *name = tree->dents[chain[index]].name;
        size_t length = xx_str_len(name);
        xx_mem_copy(out, name, length);
        out += length;
        *out++ = index ? '/' : 0;
    }
    return path;
}

/* ------------------------------------------------------------- stream --- */

static void bs_stream_free(void *opaque) {
    bs_stream *stream = (bs_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->entries[index].path)
            xx_mem_free(stream->entries[index].path);
    if (stream->entries) xx_mem_free(stream->entries);
    bs_tree_free(&stream->tree);
    xx_mem_free(stream);
}

static bs_stream *bs_stream_build(Abstractformat *format, xx_pd_struct *pd) {
    bs_stream *stream = (bs_stream *)xx_mem_calloc(1U, sizeof(*stream));
    size_t index, live = 0U;
    if (!stream) return NULL;
    if (!bs_parse(format, &stream->tree, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    for (index = 0U; index < stream->tree.dent_count; ++index)
        if (stream->tree.dents[index].alive) ++live;
    if (live) {
        stream->entries = (bs_entry *)xx_mem_calloc(live, sizeof(bs_entry));
        if (!stream->entries) {
            bs_stream_free(stream);
            return NULL;
        }
    }
    for (index = 0U; index < stream->tree.dent_count; ++index) {
        char *path;
        if (!stream->tree.dents[index].alive) continue;
        path = bs_path_of(&stream->tree, (uint32_t)index);
        if (!path) continue; /* below a removed directory */
        stream->entries[stream->count].dentry = (uint32_t)index;
        stream->entries[stream->count].path = path;
        ++stream->count;
    }
    return stream;
}

/* ------------------------------------------------------------ content --- */

typedef struct bs_ctx_s {
    xx_io_device *device;
    const bs_tree *tree;
    unsigned long budget;
    xx_pd_struct *pd;
} bs_ctx;

/* Decode an ENCODED_WRITE extent into @p out (exactly op->b bytes). */
static bool bs_decode(bs_ctx *ctx, const bs_op *op, uint8_t *out) {
    uint8_t *in;
    size_t size = (size_t)op->b, written = 0U;
    bool ok = false;
    if (op->b == 0U || op->b > BS_MAX_UNENCODED || op->data_len > BS_MAX_CMD)
        return false;
    in = (uint8_t *)xx_mem_alloc(op->data_len ? op->data_len : 1U);
    if (!in) return false;
    xx_mem_zero(out, size);
    if (!bs_read_at(ctx->device, op->data, in, op->data_len)) goto done;
    switch (op->comp) {
    case 0:
        if (op->data_len < size) goto done;
        xx_mem_copy(out, in, size);
        ok = true;
        break;
    case 1:
        ok = op->data_len > 0U &&
             xx_zlib_stream_decode_memory(in, op->data_len, out, size,
                                          &written) &&
             written <= size;
        break;
    case 2:
        ok = op->data_len > 0U &&
             xx_zstd_decompress_memory(in, op->data_len, out, size, &written) &&
             written <= size;
        break;
    default: {
        /* LZO framing: u32 total length, then {u32 segment length, LZO1X
         * data} segments that never straddle a sector; fewer than four
         * bytes left in a sector are padding. */
        size_t sector = (size_t)0x1000U << (op->comp - 3U);
        size_t total, pos = 4U, produced = 0U;
        if (op->data_len < 4U) goto done;
        total = xx_data_get_u32(in, 4, 0, false);
        if (total < 4U || total > op->data_len) goto done;
        while (pos < total) {
            size_t room = sector - (pos % sector), seg, part = 0U, want;
            if (room < 4U) {
                pos += room;
                continue;
            }
            if (total - pos < 4U) goto done;
            seg = xx_data_get_u32(in + pos, 4, 0, false);
            pos += 4U;
            if (seg == 0U || seg > total - pos || produced >= size) goto done;
            want = size - produced < sector ? size - produced : sector;
            if (!xx_lzo1x_decompress(in + pos, seg, out + produced, want, &part) ||
                part > want)
                goto done;
            produced += part;
            pos += seg;
        }
        ok = true;
        break;
    }
    }
done:
    xx_mem_free(in);
    return ok;
}

/* Bytes [off, off + len) of @p node as they stood before op @p limit. */
static bool bs_overlay(bs_ctx *ctx, uint32_t node, uint64_t limit,
                       uint64_t off, size_t len, uint8_t *out, unsigned depth) {
    const bs_tree *tree = ctx->tree;
    uint32_t index;
    uint64_t end = off + len;
    if (depth > BS_CLONE_DEPTH || node >= tree->node_count) return false;
    xx_mem_zero(out, len);
    for (index = tree->nodes[node].first_op;
         index != BS_NONE && (uint64_t)index < limit;
         index = tree->ops[index].next) {
        const bs_op *op = &tree->ops[index];
        uint64_t s, e;
        if (ctx->budget == 0U) return false;
        --ctx->budget;
        if (op->kind == BS_OP_TRUNC) {
            if (op->off < end)
                xx_mem_zero(out + (op->off > off ? op->off - off : 0U),
                            (size_t)(end - (op->off > off ? op->off : off)));
            continue;
        }
        s = op->off > off ? op->off : off;
        e = op->off + op->len < end ? op->off + op->len : end;
        if (s >= e) continue;
        switch (op->kind) {
        case BS_OP_WRITE:
            if (!bs_read_at(ctx->device, op->data + (int64_t)(s - op->off),
                            out + (s - off), (size_t)(e - s)))
                return false;
            break;
        case BS_OP_ENCODED: {
            uint8_t *dec = (uint8_t *)xx_mem_alloc((size_t)op->b);
            bool ok = dec && bs_decode(ctx, op, dec);
            if (ok)
                xx_mem_copy(out + (s - off), dec + op->a + (s - op->off),
                            (size_t)(e - s));
            if (dec) xx_mem_free(dec);
            if (!ok) return false;
            break;
        }
        case BS_OP_CLONE:
            if (op->src == BS_NONE ||
                !bs_overlay(ctx, op->src, op->b, op->a + (s - op->off),
                            (size_t)(e - s), out + (s - off), depth + 1U))
                return false;
            break;
        default: /* ZERO */
            xx_mem_zero(out + (s - off), (size_t)(e - s));
            break;
        }
    }
    return true;
}

typedef struct bs_sink_s {
    xx_io_device *device;  /**< NULL: verify only. */
    uint64_t phys;         /**< Bytes the output holds. */
    uint8_t *zeros;
} bs_sink;

static bool bs_sink_put(bs_sink *sink, uint64_t at, const uint8_t *data,
                        size_t size) {
    size_t done = 0U;
    if (!sink->device || size == 0U) return true;
    if (xx_io_seek64(sink->device, (int64_t)at, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t wrote = xx_io_write(sink->device, data + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return false;
        done += (size_t)wrote;
    }
    return true;
}

static bool bs_sink_zero(bs_sink *sink, uint64_t at, uint64_t size) {
    while (size) {
        size_t part = size > BS_CHUNK ? BS_CHUNK : (size_t)size;
        if (!bs_sink_put(sink, at, sink->zeros, part)) return false;
        at += part;
        size -= part;
    }
    return true;
}

/* Explicitly zero any gap before @p at, so the output never depends on how
 * the host fills a seek past the end. */
static bool bs_sink_reach(bs_sink *sink, uint64_t at) {
    if (at <= sink->phys) return true;
    if (!bs_sink_zero(sink, sink->phys, at - sink->phys)) return false;
    sink->phys = at;
    return true;
}

static bool bs_materialize(xx_io_device *device, const bs_tree *tree,
                           uint32_t node_index, xx_io_device *destination,
                           uint64_t max_size, xx_pd_struct *pd) {
    const bs_node *node = &tree->nodes[node_index];
    bs_ctx ctx;
    bs_sink sink;
    uint8_t *chunk = NULL, *dec = NULL;
    uint64_t final_size = node->size;
    uint32_t index;
    bool ok = false;
    if (node->oversize || node->unresolved || final_size > BS_MAX_FILE_SIZE ||
        (max_size && final_size > max_size))
        return false;
    ctx.device = device;
    ctx.tree = tree;
    ctx.budget = BS_STEP_BUDGET;
    ctx.pd = pd;
    sink.device = destination;
    sink.phys = 0U;
    chunk = (uint8_t *)xx_mem_alloc(BS_CHUNK);
    sink.zeros = (uint8_t *)xx_mem_calloc(1U, BS_CHUNK);
    if (!chunk || !sink.zeros) goto done;
    for (index = node->first_op; index != BS_NONE; index = tree->ops[index].next) {
        const bs_op *op = &tree->ops[index];
        uint64_t s, e;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (op->kind == BS_OP_TRUNC) {
            uint64_t stop = sink.phys < final_size ? sink.phys : final_size;
            if (op->off < stop && !bs_sink_zero(&sink, op->off, stop - op->off))
                goto done;
            continue;
        }
        s = op->off;
        if (s >= final_size) continue;
        e = op->len > final_size - s ? final_size : s + op->len;
        if (!bs_sink_reach(&sink, s)) goto done;
        switch (op->kind) {
        case BS_OP_WRITE: {
            uint64_t at = s;
            while (at < e) {
                size_t part = e - at > BS_CHUNK ? BS_CHUNK : (size_t)(e - at);
                if (!bs_read_at(device, op->data + (int64_t)(at - op->off),
                                chunk, part) ||
                    !bs_sink_put(&sink, at, chunk, part))
                    goto done;
                at += part;
            }
            break;
        }
        case BS_OP_ENCODED:
            dec = (uint8_t *)xx_mem_alloc((size_t)op->b);
            if (!dec || !bs_decode(&ctx, op, dec) ||
                !bs_sink_put(&sink, s, dec + op->a, (size_t)(e - s)))
                goto done;
            xx_mem_free(dec);
            dec = NULL;
            break;
        case BS_OP_CLONE: {
            uint64_t at = s;
            while (at < e) {
                size_t part = e - at > BS_CHUNK ? BS_CHUNK : (size_t)(e - at);
                if (!bs_overlay(&ctx, op->src, op->b, op->a + (at - op->off),
                                part, chunk, 1U) ||
                    !bs_sink_put(&sink, at, chunk, part))
                    goto done;
                at += part;
            }
            break;
        }
        default:
            if (!bs_sink_zero(&sink, s, e - s)) goto done;
            break;
        }
        if (e > sink.phys) sink.phys = e;
    }
    ok = bs_sink_reach(&sink, final_size);
done:
    if (dec) xx_mem_free(dec);
    if (chunk) xx_mem_free(chunk);
    if (sink.zeros) xx_mem_free(sink.zeros);
    return ok;
}

/* ------------------------------------------------------------- naming --- */

static char bs_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool bs_device_name(const char *name, size_t length) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index, k;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        const char *word = devices[index];
        for (k = 0U; k < stem && word[k] && bs_upper(name[k]) == word[k]; ++k) {}
        if (k == stem && word[k] == 0) return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((bs_upper(name[0]) == 'C' && bs_upper(name[1]) == 'O' &&
          bs_upper(name[2]) == 'M') ||
         (bs_upper(name[0]) == 'L' && bs_upper(name[1]) == 'P' &&
          bs_upper(name[2]) == 'T')))
        return true;
    return false;
}

/* Extraction-time check: every component must stay inside the output
 * directory on every host the library builds for. */
static bool bs_safe_path(const char *path) {
    const char *component = path, *cursor;
    if (!path || !path[0] || path[0] == '/' || path[0] == '\\') return false;
    for (cursor = path;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || ch == '\\' || (ch != 0U && ch < 32U) ||
            ch == 0x7FU)
            return false;
        if (ch == '/' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                component[length - 1U] == ' ' ||
                component[length - 1U] == '.' ||
                bs_device_name(component, length))
                return false;
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

/* ------------------------------------------------------------- record --- */

static bool bs_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *bs_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool bs_set_record(xx_archive_record *record, const bs_stream *stream) {
    const bs_entry *entry = &stream->entries[stream->index];
    const bs_node *node =
        &stream->tree.nodes[stream->tree.dents[entry->dentry].node];
    uint64_t size = node->type == BS_T_DIR || node->type == BS_T_SPECIAL
                        ? 0U : node->size;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = -1;
    record->compressed_size = (int64_t)size;
    if (!xx_archive_record_set_original_name(record, entry->path) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        node->mode) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        (uint64_t)node->mtime) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                         node->type == BS_T_DIR))
        return false;
    if (node->type == BS_T_LINK && node->link &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_LINK_TARGET,
                                        node->link))
        return false;
    return true;
}

/* ---------------------------------------------------------------- API --- */

void xx_btrfs_stream_init(xx_btrfs_stream *archive, xx_io_device *device,
                          int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BTRFS_STREAM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-btrfs-stream");
    xx_format_set_extension(&archive->format, "btrfs");
    archive->format.check_is_valid = xx_btrfs_stream_check_is_valid;
    archive->format.handle_base_info = xx_btrfs_stream_handle_base_info;
    archive->format.get_format_size = xx_btrfs_stream_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_btrfs_stream_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_btrfs_stream_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_btrfs_stream_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_btrfs_stream_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_btrfs_stream_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_btrfs_stream_free_archive_records_reading;
    archive->stream_size = -1;
}

xx_btrfs_stream *xx_btrfs_stream_create(xx_io_device *device,
                                        int64_t base_address) {
    xx_btrfs_stream *archive =
        (xx_btrfs_stream *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_btrfs_stream_init(archive, device, base_address);
    return archive;
}

void xx_btrfs_stream_destroy(xx_btrfs_stream *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_btrfs_stream_free(xx_btrfs_stream *archive) {
    if (!archive) return;
    xx_btrfs_stream_destroy(archive);
    xx_mem_free(archive);
}

bool xx_btrfs_stream_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    (void)pd;
    return bs_probe(format, NULL);
}

bool xx_btrfs_stream_handle_base_info(Abstractformat *format,
                                      xx_pd_struct *pd) {
    xx_btrfs_stream *archive;
    bs_stream *stream;
    if (!format) return false;
    stream = bs_stream_build(format, pd);
    if (!stream) return false;
    archive = (xx_btrfs_stream *)format;
    archive->version = stream->tree.version;
    archive->number_of_records = stream->count;
    archive->number_of_commands = stream->tree.commands;
    archive->stream_size = stream->tree.end;
    archive->has_end = stream->tree.has_end;
    archive->incremental = stream->tree.incremental;
    archive->damaged = stream->tree.damaged;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->tree.end;
    format->is_valid = true;
    format->base_info_handled = true;
    bs_stream_free(stream);
    return true;
}

int64_t xx_btrfs_stream_get_format_size(Abstractformat *format,
                                        xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_btrfs_stream_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_btrfs_stream_get_number_of_archive_records(Abstractformat *format,
                                                       xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_btrfs_stream_handle_base_info(format, pd))
               ? ((xx_btrfs_stream *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_btrfs_stream_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    bs_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = bs_stream_build(format, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        bs_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = bs_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!bs_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->count) {
        if (!bs_set_record(&state->current_record, stream)) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
    }
    return state;
}

const xx_archive_record *xx_btrfs_stream_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_btrfs_stream_archive_record_move_to_next(Abstractformat *format,
                                                 xx_archive_record_state *state,
                                                 xx_pd_struct *pd) {
    bs_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (bs_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!bs_set_record(&state->current_record, stream)) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_btrfs_stream_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    bs_stream *stream;
    const bs_entry *entry;
    const bs_node *node;
    uint32_t node_index;
    const xx_var *path_option, *limit_option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL;
    uint64_t max_size = 0U;
    xx_io_device *destination = NULL;
    bool result = false, created = false;
    if (!format || !format->device || !state || state->format != format ||
        !state->has_record ||
        !(stream = (bs_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    entry = &stream->entries[stream->index];
    node_index = stream->tree.dents[entry->dentry].node;
    node = &stream->tree.nodes[node_index];
    if (!bs_safe_path(entry->path)) return false;
    limit_option = bs_option(&state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit_option) max_size = xx_var_get_u64(limit_option);
    path_option = bs_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        if (node->type != BS_T_FILE) return true;
        return bs_materialize(format->device, &stream->tree, node_index, NULL,
                              max_size, pd);
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", entry->path)
               : xx_str_concat(base, entry->path);
    if (!path) goto done;
    if (node->type == BS_T_DIR) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (node->type == BS_T_SPECIAL) {
        /* Device, FIFO and socket nodes carry no contents. */
        result = true;
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    if (!destination) goto done;
    created = true;
    if (node->type == BS_T_LINK) {
        /* The library creates no links: the target is kept as the file's
         * contents rather than dropped. */
        size_t length = node->link ? xx_str_len(node->link) : 0U;
        result = node->link != NULL &&
                 (length == 0U ||
                  xx_store_unpack_memory_to_device(node->link, length,
                                                   destination, pd));
    } else {
        result = bs_materialize(format->device, &stream->tree, node_index,
                                destination, max_size, pd);
    }
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (destination) xx_io_close(destination);
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_btrfs_stream_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
