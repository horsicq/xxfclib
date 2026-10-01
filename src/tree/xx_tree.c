/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/tree/xx_tree.h"
#include "xxfclib/memory/xx_memory.h"

struct xx_tree_node_s {
    xx_tree_t *tree;
    xx_tree_node_t *parent;
    xx_tree_node_t *first_child, *last_child;
    xx_tree_node_t *prev_sibling, *next_sibling;
    size_t child_count;
    void *data;
};

static xx_tree_node_t *new_node(xx_tree_t *tree, const void *element)
{
    xx_tree_node_t *node;
    if (!tree || !tree->elem_size || !element || tree->count == SIZE_MAX) return NULL;
    node = (xx_tree_node_t *)xx_mem_calloc(1, sizeof(*node));
    if (!node) return NULL;
    node->data = xx_mem_alloc(tree->elem_size);
    if (!node->data) { xx_mem_free(node); return NULL; }
    xx_mem_copy(node->data, element, tree->elem_size);
    node->tree = tree;
    ++tree->count;
    return node;
}

static void unlink_node(xx_tree_node_t *node)
{
    xx_tree_node_t *parent = node->parent;
    if (!parent) return;
    if (node->prev_sibling) node->prev_sibling->next_sibling = node->next_sibling;
    else parent->first_child = node->next_sibling;
    if (node->next_sibling) node->next_sibling->prev_sibling = node->prev_sibling;
    else parent->last_child = node->prev_sibling;
    --parent->child_count;
    node->parent = node->prev_sibling = node->next_sibling = NULL;
}

static void append_node(xx_tree_node_t *parent, xx_tree_node_t *node)
{
    node->parent = parent;
    node->prev_sibling = parent->last_child;
    node->next_sibling = NULL;
    if (parent->last_child) parent->last_child->next_sibling = node;
    else parent->first_child = node;
    parent->last_child = node;
    ++parent->child_count;
}

xx_tree_t *xx_tree_create(size_t elem_size, xx_elem_free_fn elem_free)
{
    xx_tree_t *tree = (xx_tree_t *)xx_mem_alloc(sizeof(*tree));
    if (!tree) return NULL;
    if (!xx_tree_init(tree, elem_size, elem_free)) { xx_mem_free(tree); return NULL; }
    return tree;
}

bool xx_tree_init(xx_tree_t *tree, size_t elem_size, xx_elem_free_fn elem_free)
{
    if (!tree || !elem_size) return false;
    tree->root = NULL;
    tree->count = 0;
    tree->elem_size = elem_size;
    tree->elem_free = elem_free;
    return true;
}

size_t xx_tree_count(const xx_tree_t *tree) { return tree ? tree->count : 0; }
bool xx_tree_is_empty(const xx_tree_t *tree) { return !tree || !tree->count; }
size_t xx_tree_elem_size(const xx_tree_t *tree) { return tree ? tree->elem_size : 0; }
xx_tree_node_t *xx_tree_root(const xx_tree_t *tree) { return tree ? tree->root : NULL; }

xx_tree_node_t *xx_tree_set_root(xx_tree_t *tree, const void *element)
{
    xx_tree_node_t *root;
    if (!tree || tree->root) return NULL;
    root = new_node(tree, element);
    if (root) tree->root = root;
    return root;
}

xx_tree_node_t *xx_tree_append_child(xx_tree_t *tree, xx_tree_node_t *parent, const void *element)
{
    xx_tree_node_t *node;
    if (!tree || !parent || parent->tree != tree || parent->child_count == SIZE_MAX) return NULL;
    node = new_node(tree, element);
    if (node) append_node(parent, node);
    return node;
}

xx_tree_node_t *xx_tree_prepend_child(xx_tree_t *tree, xx_tree_node_t *parent, const void *element)
{
    xx_tree_node_t *node;
    if (!tree || !parent || parent->tree != tree || parent->child_count == SIZE_MAX) return NULL;
    node = new_node(tree, element);
    if (!node) return NULL;
    node->parent = parent;
    node->next_sibling = parent->first_child;
    if (parent->first_child) parent->first_child->prev_sibling = node;
    else parent->last_child = node;
    parent->first_child = node;
    ++parent->child_count;
    return node;
}

void *xx_tree_node_data(xx_tree_node_t *node) { return node ? node->data : NULL; }
const void *xx_tree_node_const_data(const xx_tree_node_t *node) { return node ? node->data : NULL; }
xx_tree_node_t *xx_tree_node_parent(const xx_tree_node_t *node) { return node ? node->parent : NULL; }
xx_tree_node_t *xx_tree_node_first_child(const xx_tree_node_t *node) { return node ? node->first_child : NULL; }
xx_tree_node_t *xx_tree_node_last_child(const xx_tree_node_t *node) { return node ? node->last_child : NULL; }
xx_tree_node_t *xx_tree_node_next_sibling(const xx_tree_node_t *node) { return node ? node->next_sibling : NULL; }
xx_tree_node_t *xx_tree_node_prev_sibling(const xx_tree_node_t *node) { return node ? node->prev_sibling : NULL; }
size_t xx_tree_node_child_count(const xx_tree_node_t *node) { return node ? node->child_count : 0; }

xx_tree_node_t *xx_tree_node_child_at(const xx_tree_node_t *node, size_t index)
{
    xx_tree_node_t *child;
    if (!node || index >= node->child_count) return NULL;
    child = node->first_child;
    while (index--) child = child->next_sibling;
    return child;
}

size_t xx_tree_node_depth(const xx_tree_node_t *node)
{
    size_t depth = 0;
    while (node && node->parent) { ++depth; node = node->parent; }
    return depth;
}

bool xx_tree_move(xx_tree_t *tree, xx_tree_node_t *node, xx_tree_node_t *new_parent)
{
    xx_tree_node_t *ancestor;
    if (!tree || !node || !new_parent || node->tree != tree || new_parent->tree != tree ||
        node == tree->root || new_parent->child_count == SIZE_MAX) return false;
    for (ancestor = new_parent; ancestor; ancestor = ancestor->parent)
        if (ancestor == node) return false;
    unlink_node(node);
    append_node(new_parent, node);
    return true;
}

bool xx_tree_remove(xx_tree_t *tree, xx_tree_node_t *node)
{
    xx_tree_node_t *current, *next;
    if (!tree || !node || node->tree != tree) return false;
    if (node == tree->root) tree->root = NULL;
    else unlink_node(node);
    current = node;
    while (current) {
        if (current->first_child) { current = current->first_child; continue; }
        next = current->next_sibling ? current->next_sibling : current->parent;
        if (current->parent) {
            current->parent->first_child = current->next_sibling;
            if (current->next_sibling) current->next_sibling->prev_sibling = NULL;
            else current->parent->last_child = NULL;
        }
        if (tree->elem_free) tree->elem_free(current->data);
        xx_mem_free(current->data);
        xx_mem_free(current);
        --tree->count;
        current = next;
    }
    return true;
}

void xx_tree_clear(xx_tree_t *tree)
{
    if (tree && tree->root) xx_tree_remove(tree, tree->root);
}

void xx_tree_cleanup(xx_tree_t *tree) { xx_tree_clear(tree); }

void xx_tree_destroy(xx_tree_t *tree)
{
    if (!tree) return;
    xx_tree_cleanup(tree);
    xx_mem_free(tree);
}

bool xx_tree_foreach(xx_tree_t *tree, xx_tree_visit_fn visit, void *user)
{
    xx_tree_node_t *node;
    if (!tree || !visit) return false;
    node = tree->root;
    while (node) {
        if (!visit(node, user)) return false;
        if (node->first_child) { node = node->first_child; continue; }
        while (node && !node->next_sibling) node = node->parent;
        node = node ? node->next_sibling : NULL;
    }
    return true;
}
