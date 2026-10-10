/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xx_die_engine_bin.h"
#include "xx_die_engine_xdisasm.h"

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/entropy/xx_entropy.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/global/xx_global.h"

/* --------------------------------------------------------------- file --- */

/* This wrapper owns the logical cursor and the cache. Copies of DieFile
 * share it, so a read error cannot be lost between scan passes. The child
 * cursor is an implementation detail and is repositioned only on misses. */
typedef struct {
    xx_io_device device;
    xx_io_device *source;
    unsigned char *buffer;
    size_t capacity, valid;
    cd_i64 size, position, base;
    unsigned char previous_tail[8];
    size_t previous_valid;
    cd_i64 previous_base;
    int own_source, failed, resident;
    /* Whole-file load (die_buffer_load): the bytes read from the source so
     * far, the memory limit captured at open, and the window buffer the
     * loaded file replaced, kept so a borrowed view stays readable. */
    cd_u64 streamed, memory_limit;
    unsigned char *window_buffer;
    int loaded, load_failed;
} DieBufferedFile;

static ssize_t die_buffer_read(xx_io_device *device, void *data, size_t count);
static int die_file_read_device_sized(DieFile *file, xx_io_device *source, size_t capacity);

static DieBufferedFile *die_buffer_state(const DieFile *file)
{
    return file && file->pDevice && file->pDevice->read == die_buffer_read ? (DieBufferedFile *)file->pDevice->priv : NULL;
}

size_t die_file_buffer_size(const DieFile *file)
{
    DieBufferedFile *state = die_buffer_state(file);
    size_t capacity = state ? state->capacity : xx_get_file_buffer_size();
    return capacity ? capacity : XX_DEFAULT_FILE_BUFFER_SIZE;
}

/* A streamed file is read again by every search over a range, so a scan that
 * searches a 100 MB section forty times copies it forty times. Once the bytes
 * read from the source plus the bytes the caller will certainly read next
 * reach the file size, reading the whole file once costs no more than
 * streaming has already cost: load it, within the memory limit captured at
 * open, and serve every later view from memory. A scan that only reads
 * headers never gets here. A search may stop at its first match, so it
 * counts only the window it reads, never its whole range.
 *
 * The load is optional. x_malloc, not cd_try_malloc, so a refusal does not
 * raise the scan's soft-OOM flag; on any failure the window keeps streaming
 * and reports read errors itself, exactly as before. */
static int die_buffer_load(DieBufferedFile *state, cd_u64 upcoming)
{
    unsigned char *whole;
    size_t size;
    size_t done = 0;
    if (state->loaded) return 1;
    if (state->resident || state->load_failed || state->failed || !state->memory_limit) return 0;
    if ((cd_u64)state->size > state->memory_limit || (cd_u64)state->size >= (cd_u64)SIZE_MAX) return 0;
    if (state->streamed < (cd_u64)state->size && upcoming < (cd_u64)state->size - state->streamed) return 0;
    size = (size_t)state->size;
    whole = (unsigned char *)x_malloc(size + 1);
    if (!whole || xx_io_seek64(state->source, 0, SEEK_SET) != 0) {
        cd_free(whole);
        state->load_failed = 1;
        return 0;
    }
    while (done < size) {
        size_t request = size - done;
        ssize_t got;
        if (request > state->capacity) request = state->capacity;
        if (request > ((size_t)-1 >> 1)) request = (size_t)-1 >> 1;
        got = xx_io_read(state->source, whole + done, request);
        if (got <= 0 || (size_t)got > request) {
            cd_free(whole);
            state->load_failed = 1;
            return 0;
        }
        done += (size_t)got;
    }
    whole[size] = 0;
    state->window_buffer = state->buffer;
    state->buffer = whole;
    state->base = 0;
    state->valid = size;
    state->previous_valid = 0;
    state->loaded = 1;
    return 1;
}

static int die_buffer_fill(DieBufferedFile *state, cd_i64 offset)
{
    cd_i64 base;
    size_t wanted, done = 0;
    if (state->failed || offset < 0 || offset >= state->size) return 0;
    if (offset >= state->base && (cd_u64)(offset - state->base) < state->valid) return 1;
    /* Loaded, the buffer holds every offset below the size. */
    if (die_buffer_load(state, (cd_u64)state->capacity)) return 1;
    base = offset - (cd_i64)((cd_u64)offset % state->capacity);
    wanted = state->capacity;
    if ((cd_u64)(state->size - base) < wanted) wanted = (size_t)(state->size - base);
    if (!state->buffer) state->buffer = (unsigned char *)cd_try_malloc(state->capacity);
    /* A cross-block reader may revisit the last one/two starts after its
     * first bridge has already loaded the next block. Retain a fixed tail
     * so that visit does not evict and reread both adjacent file blocks. */
    if (state->valid) {
        size_t tail_capacity = state->capacity < sizeof(state->previous_tail) ? state->capacity : sizeof(state->previous_tail);
        state->previous_valid = state->valid < tail_capacity ? state->valid : tail_capacity;
        state->previous_base = state->base + (cd_i64)(state->valid - state->previous_valid);
        x_memcpy(state->previous_tail, state->buffer + state->valid - state->previous_valid, state->previous_valid);
    }
    state->valid = 0;
    if (!state->buffer || xx_io_seek64(state->source, base, SEEK_SET) != 0) {
        state->failed = 1;
        return 0;
    }
    while (done < wanted) {
        size_t request = wanted - done;
        ssize_t got;
        if (request > state->capacity) request = state->capacity;
        if (request > ((size_t)-1 >> 1)) request = (size_t)-1 >> 1;
        got = xx_io_read(state->source, state->buffer + done, request);
        if (got <= 0 || (size_t)got > request) {
            state->failed = 1;
            return 0;
        }
        done += (size_t)got;
    }
    state->base = base;
    state->valid = done;
    state->streamed += done;
    return 1;
}

static const unsigned char *die_buffer_view(DieBufferedFile *state, cd_i64 offset, size_t *count)
{
    const unsigned char *view;
    size_t within, available;
    if (state->failed || offset < 0 || offset >= state->size) return NULL;
    if (offset >= state->base && (cd_u64)(offset - state->base) < state->valid) {
        within = (size_t)(offset - state->base);
        view = state->buffer + within;
        available = state->valid - within;
    } else if (offset >= state->previous_base && (cd_u64)(offset - state->previous_base) < state->previous_valid) {
        within = (size_t)(offset - state->previous_base);
        view = state->previous_tail + within;
        available = state->previous_valid - within;
    } else {
        if (!die_buffer_fill(state, offset)) return NULL;
        within = (size_t)(offset - state->base);
        view = state->buffer + within;
        available = state->valid - within;
    }
    if (*count > available) *count = available;
    return view;
}

static ssize_t die_buffer_read(xx_io_device *device, void *data, size_t count)
{
    DieBufferedFile *state = (DieBufferedFile *)device->priv;
    size_t done = 0;
    if (!count) return 0;
    if (!data || state->failed) return -1;
    if (count > ((size_t)-1 >> 1)) count = (size_t)-1 >> 1;
    if ((cd_u64)(state->size - state->position) < count) count = (size_t)(state->size - state->position);
    while (done < count) {
        size_t chunk = count - done;
        if (chunk > state->capacity) chunk = state->capacity;
        const unsigned char *view = die_buffer_view(state, state->position, &chunk);
        if (!view) return done ? (ssize_t)done : -1;
        x_memcpy((unsigned char *)data + done, view, chunk);
        state->position += (cd_i64)chunk;
        done += chunk;
    }
    return (ssize_t)done;
}

static int die_buffer_seek64(xx_io_device *device, int64_t offset, int whence)
{
    DieBufferedFile *state = (DieBufferedFile *)device->priv;
    cd_i64 base;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = state->position;
    else if (whence == SEEK_END) base = state->size;
    else return -1;
    /* base is nonnegative, so this also handles INT64_MIN safely. */
    if (offset < -base || offset > state->size - base) return -1;
    state->position = base + offset;
    return 0;
}

static int die_buffer_seek(xx_io_device *device, long offset, int whence)
{
    return die_buffer_seek64(device, (int64_t)offset, whence);
}

static int64_t die_buffer_size(xx_io_device *device)
{
    return ((DieBufferedFile *)device->priv)->size;
}

static int64_t die_buffer_tell(xx_io_device *device)
{
    return ((DieBufferedFile *)device->priv)->position;
}

static int die_buffer_close(xx_io_device *device)
{
    DieBufferedFile *state = (DieBufferedFile *)device->priv;
    int result = state->own_source ? xx_io_close(state->source) : 0;
    if (!state->resident) cd_free(state->buffer);
    cd_free(state->window_buffer);
    cd_free(state);
    return result;
}

/* Keep capacity on the shared device for both modes, without changing the
 * private DieFile layout used by the other library translation units. */
static int die_file_install_device(DieFile *file, xx_io_device *source, size_t capacity, int take_ownership, int resident)
{
    DieBufferedFile *state;
    state = (DieBufferedFile *)cd_try_malloc(sizeof(*state));
    if (!state) return 0;
    x_memset(state, 0, sizeof(*state));
    state->source = source;
    state->size = file->nSize;
    state->capacity = capacity;
    state->base = -1;
    state->own_source = take_ownership;
    state->resident = resident;
    /* A memory device already holds the bytes: streaming it only copies
     * windows, and a whole copy would double it. */
    state->memory_limit = resident || xx_io_is_memory(source) ? 0 : xx_get_file_memory_limit();
    if (resident) {
        state->buffer = file->pData;
        state->valid = (size_t)file->nSize;
        state->base = 0;
    }
    state->device.read = die_buffer_read;
    state->device.seek = die_buffer_seek;
    state->device.seek64 = die_buffer_seek64;
    state->device.tell = die_buffer_tell;
    state->device.total_size = die_buffer_size;
    state->device.close = die_buffer_close;
    state->device.priv = state;
    file->pDevice = &state->device;
    return 1;
}

int die_file_open_device(DieFile *file, xx_io_device *source, int take_ownership)
{
    cd_i64 size;
    size_t capacity = die_file_buffer_size(NULL);
    x_memset(file, 0, sizeof(*file));
    if (!source || (size = xx_io_total_size(source)) < 0) return 0;
    if ((cd_u64)size < (cd_u64)capacity) {
        if (!die_file_read_device_sized(file, source, capacity)) return 0;
        if (take_ownership) xx_io_close(source);
        return 1;
    }
    if (!source->seek64 && !source->seek) return 0;
    file->nSize = size;
    if (!die_file_install_device(file, source, capacity, take_ownership, 0)) {
        x_memset(file, 0, sizeof(*file));
        return 0;
    }
    return 1;
}

int die_file_read_failed(const DieFile *file)
{
    DieBufferedFile *state = die_buffer_state(file);
    return state ? state->failed : 0;
}

cd_i64 die_file_search_access(const DieFile *file, cd_i64 range)
{
    cd_i64 window = (cd_i64)die_file_buffer_size(file);
    return range < window ? range : window;
}

const unsigned char *die_file_whole(DieFile *file, cd_i64 access)
{
    DieBufferedFile *state;
    if (!file) return NULL;
    if (file->pData) return file->pData;
    state = die_buffer_state(file);
    if (!state) return NULL;
    if (!state->loaded && access > 0) die_buffer_load(state, (cd_u64)access);
    return state->loaded ? state->buffer : NULL;
}

int die_file_read_at(DieFile *file, cd_i64 offset, void *data, size_t count)
{
    cd_i64 cursor;
    size_t done = 0;
    size_t capacity = die_file_buffer_size(file);
    int result = 1;
    const unsigned char *whole;
    if (!file || offset < 0 || offset > file->nSize || (cd_u64)count > (cd_u64)(file->nSize - offset)) return 0;
    if (!count) return 1;
    if (!data) return 0;
    whole = die_file_whole(file, (cd_i64)count);
    if (whole) {
        x_memcpy(data, whole + (size_t)offset, count);
        return 1;
    }
    cursor = xx_io_tell(file->pDevice);
    if (cursor < 0 || xx_io_seek64(file->pDevice, offset, SEEK_SET) != 0) return 0;
    while (done < count) {
        size_t request = count - done;
        ssize_t got;
        if (request > capacity) request = capacity;
        if (request > ((size_t)-1 >> 1)) request = (size_t)-1 >> 1;
        got = xx_io_read(file->pDevice, (unsigned char *)data + done, request);
        if (got <= 0 || (size_t)got > request) {
            result = 0;
            break;
        }
        done += (size_t)got;
    }
    if (xx_io_seek64(file->pDevice, cursor, SEEK_SET) != 0) result = 0;
    return result;
}

const unsigned char *die_file_window(DieFile *file, cd_i64 offset, size_t *count)
{
    DieBufferedFile *state;
    size_t wanted = *count;
    const unsigned char *view;
    const unsigned char *whole;
    *count = 0;
    if (!wanted || !file || offset < 0 || offset >= file->nSize) return NULL;
    if (wanted > die_file_buffer_size(file)) wanted = die_file_buffer_size(file);
    if ((cd_u64)(file->nSize - offset) < wanted) wanted = (size_t)(file->nSize - offset);
    whole = die_file_whole(file, (cd_i64)wanted);
    if (whole) {
        *count = wanted;
        return whole + (size_t)offset;
    }
    state = die_buffer_state(file);
    if (!state || !(view = die_buffer_view(state, offset, &wanted))) return NULL;
    *count = wanted;
    return view;
}

static int die_file_read_device_sized(DieFile *pFile, xx_io_device *pSource, size_t capacity)
{
    int64_t nTotal = 0;
    size_t nRead = 0;

    x_memset(pFile, 0, sizeof(*pFile));

    if (pSource == NULL) {
        return 0;
    }

    nTotal = xx_io_total_size(pSource);

    /* One allocation holds the file, so the length has to fit a size_t with
     * room for the terminator. */
    if ((nTotal < 0) || ((cd_u64)nTotal >= (cd_u64)(size_t)-1)) {
        return 0;
    }

    pFile->pData = (unsigned char *)cd_malloc((size_t)nTotal + 1);

    while (nRead < (size_t)nTotal) {
        size_t nRequest = (size_t)nTotal - nRead;
        ssize_t nGot;

        /* Exact reads retry short transfers; the global file-buffer capacity
         * bounds each transfer even for the explicit whole-load helper. */
        if (nRequest > capacity) nRequest = capacity;
        if (nRequest > ((size_t)-1 >> 1)) nRequest = (size_t)-1 >> 1;
        nGot = xx_io_read(pSource, pFile->pData + nRead, nRequest);
        if (nGot <= 0 || (size_t)nGot > nRequest) {
            cd_free(pFile->pData);
            x_memset(pFile, 0, sizeof(*pFile));
            return 0;
        }
        nRead += (size_t)nGot;
    }

    pFile->pData[nRead] = 0;
    pFile->nSize = nTotal;

    pFile->pDevice = xx_io_mem_open_ro(pFile->pData, (size_t)pFile->nSize);

    if (pFile->pDevice == NULL) {
        cd_free(pFile->pData);
        x_memset(pFile, 0, sizeof(*pFile));
        return 0;
    }

    {
        xx_io_device *memory = pFile->pDevice;
        if (!die_file_install_device(pFile, memory, capacity, 1, 1)) {
            xx_io_close(memory);
            cd_free(pFile->pData);
            x_memset(pFile, 0, sizeof(*pFile));
            return 0;
        }
    }

    return 1;
}

int die_file_read_device(DieFile *pFile, xx_io_device *pSource)
{
    return die_file_read_device_sized(pFile, pSource, die_file_buffer_size(NULL));
}

int die_file_open(DieFile *pFile, const char *pFileName)
{
    xx_io_device *pSource;
    int bResult;

    x_memset(pFile, 0, sizeof(*pFile));
    pSource = xx_io_file_open(pFileName, "rb");
    if (pSource == NULL) {
        return 0;
    }
    bResult = die_file_open_device(pFile, pSource, 1);
    if (!bResult) xx_io_close(pSource);
    if (bResult) {
        pFile->pFileName = cd_strdup(pFileName);
    }
    return bResult;
}

int die_file_adopt(DieFile *pFile, unsigned char *pData, cd_i64 nSize, const char *pName)
{
    size_t capacity = die_file_buffer_size(NULL);
    xx_io_device *memory;
    x_memset(pFile, 0, sizeof(*pFile));

    if ((pData == NULL) || (nSize < 0)) {
        cd_free(pData);
        return 0;
    }

    pFile->pDevice = xx_io_mem_open_ro(pData, (size_t)nSize);

    if (pFile->pDevice == NULL) {
        cd_free(pData);
        return 0;
    }

    pFile->pData = pData;
    pFile->nSize = nSize;
    memory = pFile->pDevice;
    if (!die_file_install_device(pFile, memory, capacity, 1, 1)) {
        xx_io_close(memory);
        cd_free(pData);
        x_memset(pFile, 0, sizeof(*pFile));
        return 0;
    }
    pFile->pFileName = cd_strdup(pName ? pName : "");

    return 1;
}

void die_file_close(DieFile *pFile)
{
    xdisasm_close(pFile);
    if (pFile->pDevice != NULL) {
        xx_io_close(pFile->pDevice);
    }

    cd_free(pFile->pData);
    cd_free(pFile->pFileName);
    x_memset(pFile, 0, sizeof(*pFile));
}

/* Overflow-safe range check. Signature scripts can pass arbitrary numbers as
 * offsets (a few database rules genuinely do), so the arithmetic must never
 * wrap around. */
static int die_check(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    if ((nOffset < 0) || (nSize < 0) || (nOffset > pFile->nSize)) {
        return 0;
    }

    return (nSize <= pFile->nSize - nOffset) ? 1 : 0;
}

/* Clamps [nOffset, nOffset + *pnSize) to the file. Returns 0 when the range
 * lies completely outside. */
int die_range_clamp(DieFile *pFile, cd_i64 nOffset, cd_i64 *pnSize)
{
    if ((nOffset < 0) || (nOffset >= pFile->nSize)) {
        return 0;
    }

    if ((*pnSize < 0) || (*pnSize > pFile->nSize - nOffset)) {
        *pnSize = pFile->nSize - nOffset;
    }

    return 1;
}

/* ---------------------------------------------------------- memory map --- */

int die_map_add_part(xx_memory_map *pMap, cd_i64 nOffset, cd_i64 nSize, cd_u64 nAddress, cd_u64 nVirtualSize, xx_file_part_t filePart, const char *pName)
{
    xx_memory_record record;

    if (pMap == NULL) {
        return 0;
    }

    if (nSize > 0) {
        x_memset(&record, 0, sizeof(record));
        record.offset = nOffset;
        /* No virtual extent means no address: an overlay is file content
         * that is not loaded anywhere, and an address lookup must pass over
         * it rather than resolve into it. */
        record.address = (nVirtualSize > 0U) ? nAddress : XX_INVALID_ADDRESS;
        record.size = nSize;
        record.file_part = filePart;
        x_strncpy(record.name, pName ? pName : "", sizeof(record.name) - 1);

        if (!xx_memory_map_add_record(pMap, &record)) {
            return 0;
        }
    }

    /* The part of the virtual extent with no file bytes behind it. Reaching
     * it during an address lookup means the address belongs to this part but
     * has nothing to read, which is an answer, not a reason to keep looking. */
    if ((nVirtualSize > 0U) && (nVirtualSize > (cd_u64)(nSize > 0 ? nSize : 0))) {
        cd_u64 nTailSize = nVirtualSize - (cd_u64)(nSize > 0 ? nSize : 0);

        if (nTailSize > (cd_u64)INT64_MAX) {
            return 0;
        }

        x_memset(&record, 0, sizeof(record));
        record.offset = -1;
        record.address = nAddress + (cd_u64)(nSize > 0 ? nSize : 0);
        record.size = (cd_i64)nTailSize;
        record.file_part = filePart;
        record.is_virtual = true;
        x_strncpy(record.name, pName ? pName : "", sizeof(record.name) - 1);

        if (!xx_memory_map_add_record(pMap, &record)) {
            return 0;
        }
    }

    return 1;
}

/* ------------------------------------------------------------ strings --- */

char *die_ansi_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize)
{
    CDBuf buf;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    if (nMaxSize <= 0) {
        nMaxSize = 0x10000;
    }

    for (i = 0; i < nMaxSize; i++) {
        cd_u8 nChar = 0;

        if (!die_check(pFile, nOffset + i, 1)) {
            break;
        }

        nChar = xx_io_get_u8(pFile->pDevice, nOffset + i);

        if (nChar == 0) {
            break;
        }

        cdbuf_append_ch(&buf, (char)nChar);
    }

    return cdbuf_detach(&buf, NULL);
}

char *die_utf8_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize)
{
    return die_ansi_string(pFile, nOffset, nMaxSize);
}

static void append_utf8(CDBuf *pBuf, unsigned int nCode)
{
    if (nCode < 0x80) {
        cdbuf_append_ch(pBuf, (char)nCode);
    } else if (nCode < 0x800) {
        cdbuf_append_ch(pBuf, (char)(0xC0 | (nCode >> 6)));
        cdbuf_append_ch(pBuf, (char)(0x80 | (nCode & 0x3F)));
    } else {
        cdbuf_append_ch(pBuf, (char)(0xE0 | (nCode >> 12)));
        cdbuf_append_ch(pBuf, (char)(0x80 | ((nCode >> 6) & 0x3F)));
        cdbuf_append_ch(pBuf, (char)(0x80 | (nCode & 0x3F)));
    }
}

char *die_unicode_string_n(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize, int bBigEndian, cd_i64 *pnUnits)
{
    CDBuf buf;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    if (nMaxSize <= 0) {
        nMaxSize = 0x10000;
    }

    for (i = 0; i < nMaxSize; i++) {
        cd_u16 nChar = 0;

        if (!die_check(pFile, nOffset + i * 2, 2)) {
            break;
        }

        nChar = xx_io_get_u16(pFile->pDevice, nOffset + i * 2, bBigEndian ? true : false);

        if (nChar == 0) {
            break;
        }

        append_utf8(&buf, nChar);
    }

    if (pnUnits != NULL) {
        *pnUnits = i;
    }

    return cdbuf_detach(&buf, NULL);
}

char *die_unicode_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize, int bBigEndian)
{
    return die_unicode_string_n(pFile, nOffset, nMaxSize, bBigEndian, NULL);
}

char *die_ucsd_string(DieFile *pFile, cd_i64 nOffset)
{
    CDBuf buf;
    cd_i64 nSize = 0;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    nSize = (cd_i64)xx_io_get_u8(pFile->pDevice, nOffset);

    if (nSize > 0x10000) {
        nSize = 0x10000;
    }

    /* read_uint8 yields 0 past EOF, so the payload is always nSize characters
     * long; every embedded 0x00 (real or out of range) becomes a space. */
    for (i = 0; i < nSize; i++) {
        cd_u8 nByte = xx_io_get_u8(pFile->pDevice, nOffset + 1 + i);

        if (nByte == 0) {
            nByte = 0x20;
        }

        cdbuf_append_ch(&buf, (char)nByte);
    }

    return cdbuf_detach(&buf, NULL);
}

char *die_uuid(DieFile *pFile, cd_i64 nOffset)
{
    /* XBinary::read_UUID with the default little-endian flag: the read and the
     * hex formatting swap by the same flag, so on a little-endian target the
     * two swaps cancel and the first four fields are the little-endian values;
     * the last field is the six bytes in file order. Lower case, hyphenated. */
    static const char *pDigits = "0123456789abcdef";
    CDBuf buf;
    cd_u32 nA = xx_io_get_u32(pFile->pDevice, nOffset + 0, false);
    cd_u32 nB = xx_io_get_u16(pFile->pDevice, nOffset + 4, false);
    cd_u32 nC = xx_io_get_u16(pFile->pDevice, nOffset + 6, false);
    cd_u32 nD = xx_io_get_u16(pFile->pDevice, nOffset + 8, false);
    int nShift = 0;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    for (nShift = 28; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nA >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (nShift = 12; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nB >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (nShift = 12; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nC >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (nShift = 12; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nD >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (i = 0; i < 6; i++) {
        cd_u8 nByte = xx_io_get_u8(pFile->pDevice, nOffset + 10 + i);

        cdbuf_append_ch(&buf, pDigits[nByte >> 4]);
        cdbuf_append_ch(&buf, pDigits[nByte & 0x0F]);
    }

    return cdbuf_detach(&buf, NULL);
}

char *die_signature_hex(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    static const char *pDigits = "0123456789ABCDEF";
    CDBuf buf;
    cd_i64 done = 0;

    cdbuf_init(&buf);

    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        return cdbuf_detach(&buf, NULL);
    }

    while (done < nSize) {
        size_t count = (size_t)((cd_u64)(nSize - done) < die_file_buffer_size(pFile) ? (cd_u64)(nSize - done) : die_file_buffer_size(pFile)), i;
        const unsigned char *view = die_file_window(pFile, nOffset + done, &count);
        if (!view) break;
        for (i = 0; i < count; ++i) {
            cdbuf_append_ch(&buf, pDigits[view[i] >> 4]);
            cdbuf_append_ch(&buf, pDigits[view[i] & 0x0F]);
        }
        done += (cd_i64)count;
    }

    return cdbuf_detach(&buf, NULL);
}

/* ---------------------------------------------------------- searching --- */

static int die_bytes_equal(DieFile *file, cd_i64 offset, const unsigned char *bytes, size_t size)
{
    size_t done = 0;
    while (done < size) {
        size_t count = size - done;
        const unsigned char *view = die_file_window(file, offset + (cd_i64)done, &count);
        if (!view || x_memcmp(view, bytes + done, count)) return 0;
        done += count;
    }
    return 1;
}

cd_i64 die_find_bytes(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, const unsigned char *pNeedle, cd_i64 nNeedleSize)
{
    cd_i64 nFound = 0;
    const unsigned char *pWhole;

    if (!pNeedle || nNeedleSize <= 0 || (cd_u64)nNeedleSize > (cd_u64)(size_t)-1) {
        return -1;
    }

    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        return -1;
    }

    if (nNeedleSize > nSize) {
        return -1;
    }

    /* A match can end the search in its first window. */
    pWhole = die_file_whole(pFile, die_file_search_access(pFile, nSize));

    /* xx_data_find_bytes_buffer_optimize searches to the end of whatever buffer it is given,
     * so the window is expressed by shortening the buffer rather than by a
     * length argument. The result is relative to that buffer. */
    if (pWhole) {
        nFound = xx_data_find_bytes_buffer_optimize(pWhole + nOffset, (size_t)nSize, 0, pNeedle, (size_t)nNeedleSize, NULL);
        return (nFound < 0) ? -1 : (nOffset + nFound);
    } else {
        cd_i64 position = nOffset, end = nOffset + nSize;
        cd_i64 last = end - nNeedleSize;
        while (position <= last) {
            size_t count = (size_t)((cd_u64)(end - position) < die_file_buffer_size(pFile) ? (cd_u64)(end - position) : die_file_buffer_size(pFile));
            size_t anchor = nNeedleSize > 1 && count > 1 ? 2 : 1;
            const unsigned char *view = die_file_window(pFile, position, &count);
            if (!view) return -1;
            /* A one-byte cache is supported too. Longer candidates are
             * verified in pieces, without allocating a needle-sized overlap. */
            if (count < anchor) anchor = count;
            nFound = xx_data_find_bytes_buffer_optimize(view, count, 0, pNeedle, anchor, NULL);
            if (nFound < 0) {
                position += (cd_i64)(count - anchor + 1);
                continue;
            }
            position += nFound;
            if (position > last) return -1;
            if (die_bytes_equal(pFile, position, pNeedle, (size_t)nNeedleSize)) return position;
            if (die_file_read_failed(pFile)) return -1;
            ++position;
        }
        return -1;
    }
}

cd_i64 die_find_ansi_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, const char *pString)
{
    return die_find_bytes(pFile, nOffset, nSize, (const unsigned char *)pString, (cd_i64)x_strlen(pString));
}

cd_i64 die_find_unicode_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, const char *pString, int bBigEndian)
{
    size_t nLength = x_strlen(pString);
    unsigned char *pNeedle = (unsigned char *)cd_malloc(nLength * 2 + 2);
    size_t i = 0;
    cd_i64 nResult = 0;

    for (i = 0; i < nLength; i++) {
        if (bBigEndian) {
            pNeedle[i * 2] = 0;
            pNeedle[i * 2 + 1] = (unsigned char)pString[i];
        } else {
            pNeedle[i * 2] = (unsigned char)pString[i];
            pNeedle[i * 2 + 1] = 0;
        }
    }

    nResult = die_find_bytes(pFile, nOffset, nSize, pNeedle, (cd_i64)(nLength * 2));
    cd_free(pNeedle);

    return nResult;
}

cd_i64 die_find_u8(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u8 nValue)
{
    return die_find_bytes(pFile, nOffset, nSize, &nValue, 1);
}

cd_i64 die_find_u16(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u16 nValue)
{
    unsigned char sBuf[2];

    sBuf[0] = (unsigned char)(nValue & 0xFF);
    sBuf[1] = (unsigned char)(nValue >> 8);

    return die_find_bytes(pFile, nOffset, nSize, sBuf, 2);
}

cd_i64 die_find_u32(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u32 nValue)
{
    unsigned char sBuf[4];

    sBuf[0] = (unsigned char)(nValue & 0xFF);
    sBuf[1] = (unsigned char)((nValue >> 8) & 0xFF);
    sBuf[2] = (unsigned char)((nValue >> 16) & 0xFF);
    sBuf[3] = (unsigned char)((nValue >> 24) & 0xFF);

    return die_find_bytes(pFile, nOffset, nSize, sBuf, 4);
}

/* --------------------------------------------------------- statistics --- */

double die_entropy(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    const unsigned char *pWhole;

    if ((!die_range_clamp(pFile, nOffset, &nSize)) || (nSize <= 0)) {
        return 0.0;
    }

    pWhole = die_file_whole(pFile, nSize);
    /* xx_entropy_calculate totals its counts in 32 bits; a larger loaded
     * range takes the 64-bit loop below, over the buffer. */
    if (pWhole && (cd_u64)nSize <= 0xFFFFFFFFu) return xx_entropy_calculate(pWhole + nOffset, (size_t)nSize);
    else {
        cd_u64 counts[256] = {0};
        cd_i64 done = 0;
        double sum = 0.0, size = (double)nSize;
        int k;
        while (done < nSize) {
            size_t count = (size_t)((cd_u64)(nSize - done) < die_file_buffer_size(pFile) ? (cd_u64)(nSize - done) : die_file_buffer_size(pFile)), i;
            const unsigned char *view = pWhole ? pWhole + (size_t)(nOffset + done) : die_file_window(pFile, nOffset + done, &count);
            if (!view) return 0.0;
            for (i = 0; i < count; ++i) ++counts[view[i]];
            done += (cd_i64)count;
        }
        for (k = 0; k < 256; ++k) {
            if (counts[k]) {
                double c = (double)counts[k];
                sum += c * x_log(c);
            }
        }
        return (x_log(size) - sum / size) * 1.44269504088896340736;
    }
}

int die_is_zero_filled(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    cd_i64 done = 0;

    if ((nSize <= 0) || (!die_check(pFile, nOffset, nSize))) {
        return 0;
    }

    while (done < nSize) {
        size_t count = (size_t)((cd_u64)(nSize - done) < die_file_buffer_size(pFile) ? (cd_u64)(nSize - done) : die_file_buffer_size(pFile)), i;
        const unsigned char *view = die_file_window(pFile, nOffset + done, &count);
        if (!view) return 0;
        for (i = 0; i < count; ++i)
            if (view[i]) return 0;
        done += (cd_i64)count;
    }

    return 1;
}

/* ------------------------------------------------------------ digests --- */

static char *die_digest_hex(const unsigned char *sDigest)
{
    static const char *pDigits = "0123456789ABCDEF";
    char *pResult = (char *)cd_malloc(33);
    int i = 0;

    /* Uppercase, unlike xx_hash_to_hex: the reference prints it that way. */
    for (i = 0; i < XX_MD5_DIGEST_SIZE; i++) {
        pResult[i * 2] = pDigits[sDigest[i] >> 4];
        pResult[i * 2 + 1] = pDigits[sDigest[i] & 0x0F];
    }

    pResult[32] = 0;

    return pResult;
}

char *die_md5_hex(const void *pData, size_t nSize)
{
    unsigned char digest[XX_MD5_DIGEST_SIZE];
    if (!xx_md5_memory(pData, nSize, digest)) x_memset(digest, 0, sizeof(digest));
    return die_digest_hex(digest);
}

char *die_md5(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    const unsigned char *pWhole;

    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        nOffset = 0;
        nSize = 0;
    }

    pWhole = die_file_whole(pFile, nSize);
    if (pWhole) return die_md5_hex(pWhole + nOffset, (size_t)nSize);
    else {
        xx_hash_context context;
        unsigned char digest[XX_MD5_DIGEST_SIZE];
        cd_i64 done = 0;
        int ok = xx_hash_init(&context, XX_HASH_MD5);
        while (ok && done < nSize) {
            size_t count = (size_t)((cd_u64)(nSize - done) < die_file_buffer_size(pFile) ? (cd_u64)(nSize - done) : die_file_buffer_size(pFile));
            const unsigned char *view = die_file_window(pFile, nOffset + done, &count);
            if (!view) {
                ok = 0;
                break;
            }
            xx_hash_update(&context, view, count);
            done += (cd_i64)count;
        }
        if (!ok || !xx_hash_final(&context, digest, sizeof(digest))) x_memset(digest, 0, sizeof(digest));
        return die_digest_hex(digest);
    }
}

cd_u32 die_crc32(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u32 nInit)
{
    const unsigned char *pWhole;

    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        return nInit;
    }

    pWhole = die_file_whole(pFile, nSize);
    if (pWhole) return xx_crc32_calc(nInit, pWhole + nOffset, (size_t)nSize);

    while (nSize > 0) {
        size_t count = (size_t)((cd_u64)nSize < die_file_buffer_size(pFile) ? (cd_u64)nSize : die_file_buffer_size(pFile));
        const unsigned char *view = die_file_window(pFile, nOffset, &count);
        if (!view) return nInit;
        nInit = xx_crc32_calc(nInit, view, count);
        nOffset += (cd_i64)count;
        nSize -= (cd_i64)count;
    }
    return nInit;
}

cd_u32 die_adler32(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    const unsigned char *pWhole;

    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        return 1;
    }

    pWhole = die_file_whole(pFile, nSize);
    if (pWhole) return xx_adler32(pWhole + nOffset, (size_t)nSize);

    {
        cd_u32 value = 1;
        while (nSize > 0) {
            size_t count = (size_t)((cd_u64)nSize < die_file_buffer_size(pFile) ? (cd_u64)nSize : die_file_buffer_size(pFile));
            const unsigned char *view = die_file_window(pFile, nOffset, &count);
            if (!view) return value;
            value = xx_adler32_update(value, view, count);
            nOffset += (cd_i64)count;
            nSize -= (cd_i64)count;
        }
        return value;
    }
}

cd_u16 die_crc16(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u16 nInit)
{
    const unsigned char *pWhole;

    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        return nInit;
    }

    pWhole = die_file_whole(pFile, nSize);
    if (pWhole) return xx_crc16_arc_calc(nInit, pWhole + nOffset, (size_t)nSize);

    while (nSize > 0) {
        size_t count = (size_t)((cd_u64)nSize < die_file_buffer_size(pFile) ? (cd_u64)nSize : die_file_buffer_size(pFile));
        const unsigned char *view = die_file_window(pFile, nOffset, &count);
        if (!view) return nInit;
        nInit = xx_crc16_arc_calc(nInit, view, count);
        nOffset += (cd_i64)count;
        nSize -= (cd_i64)count;
    }
    return nInit;
}

cd_u32 die_string_crc32c(const char *pString)
{
    size_t nSize = pString ? x_strlen(pString) : 0;

    return xx_crc32c_calc(0xFFFFFFFFu, pString, nSize);
}
