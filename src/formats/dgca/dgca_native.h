/* Independent archive-byte implementation; SPDX-License-Identifier: MIT. */
#ifndef XFU_DGCA_NATIVE_DECODE_H
#define XFU_DGCA_NATIVE_DECODE_H
#include <stddef.h>
#include <stdint.h>

typedef enum dg_status {
    DG_OK = 0,
    DG_IO,
    DG_FORMAT,
    DG_CHECKSUM,
    DG_UNSUPPORTED_CODEC,
    DG_PASSWORD_REQUIRED,
    DG_UNSUPPORTED_CRYPT,
    DG_MEMORY,
    DG_MEMBER_LIMIT,
    DG_CANCELLED
} dg_status;

typedef struct dg_callbacks {
    void *opaque;
    /* Borrowed positional IO: return actual bytes, including short reads. */
    size_t (*read_at)(void *, uint64_t, void *, size_t);
    /* Every dynamic allocation, including index/name/payload allocations. */
    void *(*allocate)(void *, size_t);
    void (*release)(void *, void *);
    int (*cancelled)(void *);
} dg_callbacks;

typedef struct dg_member {
    char *name;           /* UTF-8, slash-separated, owned. */
    unsigned char *bytes; /* Owned; NULL for directory/empty file. */
    uint64_t size;
    uint64_t timestamp;   /* Windows FILETIME. */
    uint64_t data_offset; /* DATA-relative chunk offset. */
    uint64_t compressed_size;
    uint32_t attributes; /* Windows attributes; 0x10 directory. */
    uint32_t crc32;
} dg_member;

typedef struct dg_result {
    dg_callbacks callbacks;
    dg_member *members;
    size_t count;
    uint64_t format_size;
    dg_status status;
    const char *detail; /* Constant, borrowed diagnostic. */
} dg_result;

/* input_size is the bounded format view, offsets are relative to format base.
 * Decoder never closes/seeks a device, calls a filesystem API, or launches a
 * process. Caller implements limits and cancellation through the callbacks.
 * max_members and member_limit apply before index/payload allocation.
 * Output is zero-initialized on entry; failure releases all owned allocations.
 */
dg_status dg_native_decode(const dg_callbacks *, uint64_t input_size, const char *password_utf8, uint64_t member_limit, uint64_t max_members, dg_result *);
void dg_native_result_free(dg_result *);
#endif
