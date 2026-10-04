/* SPDX-License-Identifier: MIT. Version 1.2.1 encoder to reproduce FEAD bytes.
 * No inflate/file entry points and no source image code are linked or run. */
#include "fead_zlib_encoder.h"
#include "encoder/zlib.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/data/xx_pd.h"
#include <limits.h>

/* Historical zlib default allocators are deliberately unavailable. Every call
 * below supplies the bounded allocator, so they are not reached. */
voidpf fead_z_zcalloc(voidpf opaque, unsigned items, unsigned size)
{ (void)opaque; (void)items; (void)size; return NULL; }
void fead_z_zcfree(voidpf opaque, voidpf ptr)
{ (void)opaque; (void)ptr; }
const char *const fead_z_z_errmsg[10] = {
 "need dictionary", "stream end", "", "file error", "stream error",
 "data error", "insufficient memory", "buffer error", "incompatible version", ""
};

typedef union fead_z_alloc_header {
 uint64_t alignment[2];
 struct { size_t bytes; } value;
} fead_z_alloc_header;
typedef struct fead_z_budget {
 const fead_restore_context *ctx;
 uint64_t used;
} fead_z_budget;
static voidpf fead_z_allocate(voidpf opaque, unsigned items, unsigned size)
{
 fead_z_budget *budget = (fead_z_budget *)opaque;
 fead_z_alloc_header *block;
 size_t bytes;
 uint64_t used;
 if (size && (size_t)items > (SIZE_MAX - sizeof(*block)) / size) return NULL;
 bytes = (size_t)items * size + sizeof(*block);
 used = budget->ctx->memory_used ? *budget->ctx->memory_used : budget->used;
 if (used > budget->ctx->memory_limit || bytes > budget->ctx->memory_limit - used ||
     budget->used > 1024U * 1024U || bytes > 1024U * 1024U - budget->used ||
     xx_pd_is_stopped(budget->ctx->pd)) return NULL;
 block = (fead_z_alloc_header *)xx_mem_alloc(bytes);
 if (!block) return NULL;
 block->value.bytes = bytes;
 budget->used += bytes;
 if (budget->ctx->memory_used) *budget->ctx->memory_used += bytes;
 return block + 1;
}
static void fead_z_release(voidpf opaque, voidpf pointer)
{
 fead_z_budget *budget = (fead_z_budget *)opaque;
 fead_z_alloc_header *block;
 size_t bytes;
 if (!pointer) return;
 block = ((fead_z_alloc_header *)pointer) - 1;
 bytes = block->value.bytes;
 budget->used -= bytes;
 if (budget->ctx->memory_used) *budget->ctx->memory_used -= bytes;
 xx_mem_free(block);
}
bool fead_zlib_encode_bounded(const fead_restore_context *ctx,
 const uint8_t *plain, size_t length, uint8_t *output, size_t capacity,
 unsigned level, unsigned window, unsigned memory, unsigned flush,
 size_t *written)
{
 z_stream stream;
 fead_z_budget budget;
 size_t input = 0;
 int code;
 bool ok = false;
 if (!ctx || !written || (!plain && length) || (!output && capacity) ||
     length > UINT_MAX || capacity > UINT_MAX || level > 9 || window < 8 ||
     window > 15 || memory < 1 || memory > 9 || flush > Z_FULL_FLUSH ||
     xx_pd_is_stopped(ctx->pd)) return false;
 *written = 0;
 xx_mem_zero(&stream, sizeof(stream));
 xx_mem_zero(&budget, sizeof(budget)); budget.ctx = ctx;
 stream.zalloc = fead_z_allocate; stream.zfree = fead_z_release;
 stream.opaque = &budget;
 code = deflateInit2(&stream, (int)level, Z_DEFLATED, (int)window,
                    (int)memory, Z_DEFAULT_STRATEGY);
 if (code != Z_OK) return false;
 stream.next_out = output; stream.avail_out = (uInt)capacity;
 /* Feed large inputs in bounded pieces without an extra flush. This retains
  * the historical compressor's lookahead while polling cancellation. */
 while (length - input > 65536U) {
  stream.next_in = (Bytef *)(plain + input); stream.avail_in = 65536U;
  if (xx_pd_is_stopped(ctx->pd)) goto done;
  code = deflate(&stream, Z_NO_FLUSH);
  if (code != Z_OK || stream.avail_in != 0 || stream.avail_out == 0) goto done;
  input += 65536U;
 }
 stream.next_in = (Bytef *)(plain ? plain + input : NULL); stream.avail_in = (uInt)(length - input);
 if (xx_pd_is_stopped(ctx->pd)) goto done;
 code = deflate(&stream, (int)flush);
 if (code != Z_OK || stream.avail_in != 0 || stream.avail_out == 0) goto done;
 code = deflate(&stream, Z_FINISH);
 if (code != Z_STREAM_END || stream.avail_in != 0 || xx_pd_is_stopped(ctx->pd)) goto done;
 *written = stream.total_out; ok = true;
done:
 if (deflateEnd(&stream) != Z_OK) ok = false;
 return ok && budget.used == 0;
}
