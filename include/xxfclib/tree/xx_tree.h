/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tree.h @brief Ordered, rooted tree of fixed-size values. */
#ifndef XX_TREE_H
#define XX_TREE_H

#include "xxfclib/list/xx_list.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_tree_s xx_tree_t;
typedef struct xx_tree_node_s xx_tree_node_t;

/* The tree owns all nodes and shallow-copies each supplied element. If an
 * element contains owned pointers, elem_free must release them and must not
 * mutate the tree. Do not copy or move a live xx_tree_t by value; nodes retain
 * a pointer to their tree. */
struct xx_tree_s {
    xx_tree_node_t *root;
    size_t count;
    size_t elem_size;
    xx_elem_free_fn elem_free;
};

typedef struct xx_tree_s xx_tree_s;
typedef struct xx_tree_node_s xx_tree_node_s;

XXFC_API xx_tree_t *xx_tree_create(size_t elem_size, xx_elem_free_fn elem_free);
XXFC_API bool xx_tree_init(xx_tree_t *tree, size_t elem_size, xx_elem_free_fn elem_free);
XXFC_API void xx_tree_clear(xx_tree_t *tree);
XXFC_API void xx_tree_cleanup(xx_tree_t *tree);
XXFC_API void xx_tree_destroy(xx_tree_t *tree);

XXFC_API size_t xx_tree_count(const xx_tree_t *tree);
XXFC_API bool xx_tree_is_empty(const xx_tree_t *tree);
XXFC_API size_t xx_tree_elem_size(const xx_tree_t *tree);
XXFC_API xx_tree_node_t *xx_tree_root(const xx_tree_t *tree);

/* A tree has at most one root. These return NULL on invalid input or memory
 * failure. parent must belong to tree. Insertion preserves child order. */
XXFC_API xx_tree_node_t *xx_tree_set_root(xx_tree_t *tree, const void *element);
XXFC_API xx_tree_node_t *xx_tree_append_child(xx_tree_t *tree,
    xx_tree_node_t *parent, const void *element);
XXFC_API xx_tree_node_t *xx_tree_prepend_child(xx_tree_t *tree,
    xx_tree_node_t *parent, const void *element);

/* Borrowed node/value pointers remain valid until that node is removed or
 * its tree is cleared/destroyed. Reparenting keeps pointers stable. */
XXFC_API void *xx_tree_node_data(xx_tree_node_t *node);
XXFC_API const void *xx_tree_node_const_data(const xx_tree_node_t *node);
XXFC_API xx_tree_node_t *xx_tree_node_parent(const xx_tree_node_t *node);
XXFC_API xx_tree_node_t *xx_tree_node_first_child(const xx_tree_node_t *node);
XXFC_API xx_tree_node_t *xx_tree_node_last_child(const xx_tree_node_t *node);
XXFC_API xx_tree_node_t *xx_tree_node_next_sibling(const xx_tree_node_t *node);
XXFC_API xx_tree_node_t *xx_tree_node_prev_sibling(const xx_tree_node_t *node);
/* O(index) for this linked child sequence. */
XXFC_API xx_tree_node_t *xx_tree_node_child_at(const xx_tree_node_t *node, size_t index);
XXFC_API size_t xx_tree_node_child_count(const xx_tree_node_t *node);
XXFC_API size_t xx_tree_node_depth(const xx_tree_node_t *node);

/* Move a non-root subtree to the end of new_parent's children. The nodes must
 * belong to tree; cycles are rejected without changing the tree. */
XXFC_API bool xx_tree_move(xx_tree_t *tree, xx_tree_node_t *node, xx_tree_node_t *new_parent);

/* Remove node and all descendants, calling elem_free exactly once per value
 * in postorder. The root may also be removed. No recursion is used. */
XXFC_API bool xx_tree_remove(xx_tree_t *tree, xx_tree_node_t *node);

typedef bool (*xx_tree_visit_fn)(xx_tree_node_t *node, void *user);
/* Preorder walk; false callback stops early and makes this return false.
 * Do not modify tree structure from the visitor. */
XXFC_API bool xx_tree_foreach(xx_tree_t *tree, xx_tree_visit_fn visit, void *user);

#ifdef __cplusplus
}
#endif

#endif /* XX_TREE_H */
