/* Independently authored FEAD preprocessing interface; MIT. RAM only. */
#ifndef XFU_FEAD_RESTORE_PRIVATE_H
#define XFU_FEAD_RESTORE_PRIVATE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "xxfclib/formats/xx_format.h"
typedef struct fead_outer_member {
    const char *name;
    uint32_t size, flags;
    uint64_t filetime;
} fead_outer_member;
typedef struct fead_resource {
    uint32_t tag, id;
    const uint8_t *data;
    size_t size;
} fead_resource;
typedef struct fead_action {
    uint32_t end, type;
} fead_action;
/* Returned bytes are borrowed for the duration of the callback. For transformed
 * CAB/PE entries, emit a parent directory and original inner members, never a
 * fabricated parent compressed image. directory=true ignores bytes/length. */
typedef bool (*fead_emit_fn)(void *user, const char *name, const uint8_t *bytes, size_t length, uint64_t filetime, uint32_t flags, bool directory);
typedef struct fead_restore_context {
    const uint8_t *data;
    size_t size;
    const fead_outer_member *members;
    size_t member_count;
    const fead_resource *resources;
    size_t resource_count;
    const fead_action *actions;
    size_t action_count;
    uint64_t memory_limit;
    /* Shared peak accounting includes caller retained buffers and inverse
     * workspace. The inverse increments before allocation and decrements on
     * release; the emitter charges retained copies through the same pointer. */
    uint64_t *memory_used;
    xx_pd_struct *pd;
    fead_emit_fn emit;
    void *user;
} fead_restore_context;
bool fead_restore_archive(const fead_restore_context *context);
#endif
