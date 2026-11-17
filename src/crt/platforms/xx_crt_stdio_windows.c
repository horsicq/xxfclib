/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_stdio_windows.c
 * @brief <stdio.h> for a Windows program linked without any Microsoft C
 *        runtime: FILE streams over Win32 handles, the small descriptor
 *        table behind _fileno/_setmode/_chsize_s, and the UCRT entry points
 *        that the inline printf/scanf functions of the SDK headers call
 *        (__stdio_common_vfprintf, __stdio_common_vsprintf,
 *        __stdio_common_vsscanf, __acrt_iob_func).
 *
 * Only compiled into a CRT-free link (see xx_crt_windows.c).
 *
 * Behaviour follows UCRT where a program can observe it: descriptors 0, 1
 * and 2 start in text mode (LF written as CR LF, CR LF read as LF), streams
 * opened without 'b' are text streams, a stream that is not a console is
 * fully buffered with 4096 bytes unless setvbuf says otherwise, console
 * output is written once per call, tmpfile() opens a binary read/write file
 * that the system deletes on close, and every stream is flushed by exit().
 * Each stream has its own lock, so concurrent calls on one stream do not
 * interleave their characters (fgets holds it for the whole line).
 *
 * Text-mode reading follows UCRT down to the block boundaries, because they
 * decide what ftell answers and how a CR at the end of a read is treated:
 * 4096 bytes per read, 512 on the first read after fseek, two for an
 * unbuffered stream; a one-byte look-ahead on pipes. docs/CRT.md (section
 * 4.2) lists the rules; tests/crt/crt_platform_probe.c compares them with
 * Microsoft's runtime.
 */

#if defined(_WIN32) && defined(_MSC_VER)

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xxfclib/crt/xx_crt.h"
#include "xx_crt_windows_internal.h"

/* ------------------------------------------------------------ descriptors -- */

#define XX_CRT_FD_OPEN 0x01
#define XX_CRT_FD_TEXT 0x02
#define XX_CRT_FD_APPEND 0x04
#define XX_CRT_FD_EOF 0x08 /* text mode read hit Ctrl+Z */

typedef struct xx_crt_fd_s {
    HANDLE hHandle;
    int nFlags;
    int nPeek; /* text mode, not seekable: the byte read after a CR, plus 1; 0 = none */
} xx_crt_fd;

#define XX_CRT_MAX_FDS 2048

static xx_crt_fd s_xx_crt_fds[XX_CRT_MAX_FDS];
static SRWLOCK s_xx_crt_fd_lock = SRWLOCK_INIT;
static volatile LONG s_xx_crt_std_fds_ready;

static void xx_crt_std_fds_init(void)
{
    if (s_xx_crt_std_fds_ready == 2) return;
    AcquireSRWLockExclusive(&s_xx_crt_fd_lock);
    if (s_xx_crt_std_fds_ready != 2) {
        static const DWORD s_std[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        int i;
        for (i = 0; i < 3; ++i) {
            if (!(s_xx_crt_fds[i].nFlags & XX_CRT_FD_OPEN)) {
                s_xx_crt_fds[i].hHandle = GetStdHandle(s_std[i]);
                s_xx_crt_fds[i].nFlags = XX_CRT_FD_OPEN | XX_CRT_FD_TEXT;
            }
        }
        InterlockedExchange(&s_xx_crt_std_fds_ready, 2);
    }
    ReleaseSRWLockExclusive(&s_xx_crt_fd_lock);
}

static int xx_crt_fd_alloc(HANDLE hHandle, int nFlags)
{
    int i;
    xx_crt_std_fds_init();
    AcquireSRWLockExclusive(&s_xx_crt_fd_lock);
    for (i = 3; i < XX_CRT_MAX_FDS; ++i) {
        if (!(s_xx_crt_fds[i].nFlags & XX_CRT_FD_OPEN)) {
            s_xx_crt_fds[i].hHandle = hHandle;
            s_xx_crt_fds[i].nFlags = nFlags | XX_CRT_FD_OPEN;
            s_xx_crt_fds[i].nPeek = 0;
            ReleaseSRWLockExclusive(&s_xx_crt_fd_lock);
            return i;
        }
    }
    ReleaseSRWLockExclusive(&s_xx_crt_fd_lock);
    return -1;
}

static xx_crt_fd *xx_crt_fd_get(int nFd)
{
    if (nFd < 0 || nFd >= XX_CRT_MAX_FDS) return NULL;
    if (nFd < 3) xx_crt_std_fds_init();
    if (!(s_xx_crt_fds[nFd].nFlags & XX_CRT_FD_OPEN)) return NULL;
    return &s_xx_crt_fds[nFd];
}

static void xx_crt_fd_release(int nFd)
{
    if (nFd < 0 || nFd >= XX_CRT_MAX_FDS) return;
    AcquireSRWLockExclusive(&s_xx_crt_fd_lock);
    s_xx_crt_fds[nFd].nFlags = 0;
    s_xx_crt_fds[nFd].nPeek = 0;
    s_xx_crt_fds[nFd].hHandle = INVALID_HANDLE_VALUE;
    ReleaseSRWLockExclusive(&s_xx_crt_fd_lock);
}

int __cdecl _setmode(int nFd, int nMode)
{
    xx_crt_fd *pFd = xx_crt_fd_get(nFd);
    int nOld;
    if (!pFd) {
        errno = EBADF;
        return -1;
    }
    nOld = (pFd->nFlags & XX_CRT_FD_TEXT) ? _O_TEXT : _O_BINARY;
    if (nMode == _O_BINARY) pFd->nFlags &= ~XX_CRT_FD_TEXT;
    else if (nMode == _O_TEXT) pFd->nFlags |= XX_CRT_FD_TEXT;
    else {
        errno = EINVAL;
        return -1;
    }
    return nOld;
}

intptr_t __cdecl _get_osfhandle(int nFd)
{
    xx_crt_fd *pFd = xx_crt_fd_get(nFd);
    if (!pFd) {
        errno = EBADF;
        return -1;
    }
    return (intptr_t)pFd->hHandle;
}

int __cdecl _isatty(int nFd)
{
    xx_crt_fd *pFd = xx_crt_fd_get(nFd);
    if (!pFd) {
        errno = EBADF;
        return 0;
    }
    return GetFileType(pFd->hHandle) == FILE_TYPE_CHAR ? 64 : 0;
}

errno_t __cdecl _chsize_s(int nFd, long long nSize)
{
    xx_crt_fd *pFd = xx_crt_fd_get(nFd);
    LARGE_INTEGER zero, current, target;
    if (!pFd) return errno = EBADF;
    if (nSize < 0) return errno = EINVAL;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(pFd->hHandle, zero, &current, FILE_CURRENT)) return errno = xx_crt_errno_from_oserror(GetLastError());
    target.QuadPart = nSize;
    if (!SetFilePointerEx(pFd->hHandle, target, NULL, FILE_BEGIN) || !SetEndOfFile(pFd->hHandle)) {
        DWORD nError = GetLastError();
        SetFilePointerEx(pFd->hHandle, current, NULL, FILE_BEGIN);
        return errno = xx_crt_errno_from_oserror(nError);
    }
    SetFilePointerEx(pFd->hHandle, current, NULL, FILE_BEGIN);
    return 0;
}

/* Writes nSize bytes to the descriptor, translating LF to CR LF in text
 * mode. Returns the number of source bytes written (nSize on success). */
static size_t xx_crt_fd_write(int nFd, const char *pData, size_t nSize)
{
    xx_crt_fd *pFd = xx_crt_fd_get(nFd);
    size_t nDone = 0;
    if (!pFd || pFd->hHandle == INVALID_HANDLE_VALUE || pFd->hHandle == NULL) {
        errno = EBADF;
        return 0;
    }
    if (pFd->nFlags & XX_CRT_FD_APPEND) {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        SetFilePointerEx(pFd->hHandle, zero, NULL, FILE_END);
    }
    if (!(pFd->nFlags & XX_CRT_FD_TEXT)) {
        while (nDone < nSize) {
            DWORD nChunk = (nSize - nDone) > 0x40000000u ? 0x40000000u : (DWORD)(nSize - nDone);
            DWORD nWritten = 0;
            if (!WriteFile(pFd->hHandle, pData + nDone, nChunk, &nWritten, NULL)) {
                errno = xx_crt_errno_from_oserror(GetLastError());
                return nDone;
            }
            if (nWritten == 0) {
                errno = ENOSPC;
                return nDone;
            }
            nDone += nWritten;
        }
        return nDone;
    }
    {
        char sChunk[2048];
        while (nDone < nSize) {
            size_t nUsed = 0;
            size_t nTaken = nDone;
            DWORD nWritten = 0;
            while (nTaken < nSize && nUsed + 2 <= sizeof(sChunk)) {
                char c = pData[nTaken++];
                if (c == '\n') sChunk[nUsed++] = '\r';
                sChunk[nUsed++] = c;
            }
            if (!WriteFile(pFd->hHandle, sChunk, (DWORD)nUsed, &nWritten, NULL) || nWritten != nUsed) {
                errno = xx_crt_errno_from_oserror(GetLastError());
                return nDone;
            }
            nDone = nTaken;
        }
    }
    return nDone;
}

/* Reads up to nSize bytes (binary). Returns the count, 0 at end of file,
 * (size_t)-1 on error. A broken pipe is end of file, as in UCRT. */
static size_t xx_crt_fd_read_raw(xx_crt_fd *pFd, char *pData, size_t nSize)
{
    DWORD nRead = 0;
    DWORD nChunk = nSize > 0x40000000u ? 0x40000000u : (DWORD)nSize;
    if (!ReadFile(pFd->hHandle, pData, nChunk, &nRead, NULL)) {
        DWORD nError = GetLastError();
        if (nError == ERROR_BROKEN_PIPE || nError == ERROR_HANDLE_EOF) return 0;
        errno = xx_crt_errno_from_oserror(nError);
        return (size_t)-1;
    }
    return nRead;
}

/* Reads with text-mode translation when the descriptor is in text mode:
 * CR LF becomes LF and Ctrl+Z ends the input. A CR at the end of a block
 * needs the next byte. On a disk file that byte is put back by seeking, and
 * when it is LF the CR is dropped (the pair completes in the next block);
 * a block that is only that CR becomes the line feed. On a pipe or a console nothing can
 * be put back: as in UCRT, the CR is delivered as it is and the byte read
 * after it (LF included) waits in the descriptor's one-byte look-ahead and
 * opens the next read untranslated, so no byte is lost. *pnRaw (optional)
 * receives the number of bytes taken from the handle by this call that the
 * returned text accounts for, in UCRT's reckoning (see the leading line feed
 * below). */
static size_t xx_crt_fd_read(int nFd, char *pData, size_t nSize, size_t *pnRaw)
{
    xx_crt_fd *pFd = xx_crt_fd_get(nFd);
    size_t nRead = 0;
    size_t nRaw = 0;
    size_t nLiteral = 0; /* the look-ahead byte at the front is not translated */
    size_t nLeadingLf = 0;
    size_t i, o;
    if (pnRaw) *pnRaw = 0;
    if (!pFd) {
        errno = EBADF;
        return (size_t)-1;
    }
    if (!(pFd->nFlags & XX_CRT_FD_TEXT)) {
        nRead = xx_crt_fd_read_raw(pFd, pData, nSize);
        if (pnRaw && nRead != (size_t)-1) *pnRaw = nRead;
        return nRead;
    }
    if ((pFd->nFlags & XX_CRT_FD_EOF) || nSize == 0) return 0;
    if (pFd->nPeek) {
        pData[nRead++] = (char)(pFd->nPeek - 1);
        pFd->nPeek = 0;
        nLiteral = 1;
    }
    if (nRead < nSize) {
        size_t nMore = xx_crt_fd_read_raw(pFd, pData + nRead, nSize - nRead);
        if (nMore == (size_t)-1) {
            if (nRead == 0) return (size_t)-1;
            nMore = 0; /* deliver the look-ahead byte; the error shows next time */
        }
        nRaw = nMore;
        nRead += nMore;
    }
    /* A block that opens with a line feed counts one byte more for ftell:
     * UCRT takes that LF for the second half of a pair whose CR the block
     * before dropped (true or not; measured against UCRT either way). */
    if (nRead && !nLiteral && pData[0] == '\n') nLeadingLf = 1;
    if (nRead == 0) return 0;
    for (i = nLiteral, o = nLiteral; i < nRead; ++i) {
        char c = pData[i];
        if (c == 0x1A) {
            pFd->nFlags |= XX_CRT_FD_EOF;
            break;
        }
        if (c == '\r') {
            if (i + 1 < nRead) {
                if (pData[i + 1] == '\n') continue;
            } else {
                /* a CR at the end of the block: look at one more byte */
                char cNext = 0;
                if (xx_crt_fd_read_raw(pFd, &cNext, 1) == 1) {
                    if (GetFileType(pFd->hHandle) != FILE_TYPE_DISK) {
                        ++nRaw;
                        pFd->nPeek = (int)(unsigned char)cNext + 1;
                    } else if (cNext == '\n' && o == 0) {
                        /* the pair is the whole block: one line feed */
                        ++nRaw;
                        pData[o++] = '\n';
                        continue;
                    } else {
                        /* put the byte back. Before a line feed the CR is
                         * dropped here and the LF opens the next block, as
                         * in UCRT (ftell after this block therefore already
                         * counts the CR) */
                        LARGE_INTEGER back;
                        back.QuadPart = -1;
                        SetFilePointerEx(pFd->hHandle, back, NULL, FILE_CURRENT);
                        if (cNext == '\n') continue;
                    }
                }
            }
        }
        pData[o++] = c;
    }
    if (pnRaw) *pnRaw = nRaw + nLeadingLf;
    return o;
}

/* ---------------------------------------------------------------- FILE -- */

#define XX_CRT_FILE_READ 0x0001
#define XX_CRT_FILE_WRITE 0x0002
#define XX_CRT_FILE_EOF 0x0010
#define XX_CRT_FILE_ERROR 0x0020
#define XX_CRT_FILE_STD 0x0040
#define XX_CRT_FILE_OWNBUF 0x0080
#define XX_CRT_FILE_UNBUF 0x0100
#define XX_CRT_FILE_SETBUF 0x0200 /* setvbuf chose the buffering */
#define XX_CRT_FILE_INUSE 0x0400
#define XX_CRT_FILE_SMALLFILL 0x0800 /* after a seek: the next fill reads 512 bytes */

#define XX_CRT_STATE_IDLE 0
#define XX_CRT_STATE_READ 1
#define XX_CRT_STATE_WRITE 2

typedef struct xx_crt_file_s {
    int nFd;
    int nFlags;
    int nState;
    char *pBuffer;
    size_t nBufferSize;
    size_t nPos; /* read: next byte; write: bytes buffered */
    size_t nLen; /* read: bytes valid */
    size_t nFillRaw; /* read: bytes of the file behind the buffered text */
    char sSmall[2];  /* the read buffer of an unbuffered stream (UCRT reads those two bytes at a time) */
    int nUnget;  /* pushed-back character or -1 */
    CRITICAL_SECTION lock;
    volatile LONG nLockReady;
    struct xx_crt_file_s *pNext;
} xx_crt_file;

static xx_crt_file s_xx_crt_std_files[3];
static xx_crt_file *s_xx_crt_open_files;
static SRWLOCK s_xx_crt_files_lock = SRWLOCK_INIT;

static void xx_crt_file_lock(xx_crt_file *pFile)
{
    if (pFile->nLockReady != 2) {
        if (InterlockedCompareExchange(&pFile->nLockReady, 1, 0) == 0) {
            InitializeCriticalSection(&pFile->lock);
            InterlockedExchange(&pFile->nLockReady, 2);
        } else {
            while (pFile->nLockReady != 2) YieldProcessor();
        }
    }
    EnterCriticalSection(&pFile->lock);
}

static void xx_crt_file_unlock(xx_crt_file *pFile)
{
    LeaveCriticalSection(&pFile->lock);
}

/* Where a stream's read characters are held, and how many fit: its buffer,
 * or for an unbuffered stream the two bytes inside the stream itself. Like
 * UCRT, an unbuffered stream still reads two bytes at a time, which decides
 * how CR LF pairs meet the ends of its reads. */
static char *xx_crt_file_read_buffer(xx_crt_file *pFile)
{
    return (pFile->nFlags & XX_CRT_FILE_UNBUF) ? pFile->sSmall : pFile->pBuffer;
}

static size_t xx_crt_file_read_size(const xx_crt_file *pFile)
{
    return (pFile->nFlags & XX_CRT_FILE_UNBUF) ? sizeof(pFile->sSmall) : pFile->nBufferSize;
}

/* stdin, stdout and stderr are set up exactly once, lock included, before
 * any thread can use them: a second thread never rewrites a stream that the
 * first one is already using. A closed standard stream stays closed. */
static volatile LONG s_xx_crt_std_files_ready;

static void xx_crt_std_files_init(void)
{
    if (s_xx_crt_std_files_ready == 2) return;
    AcquireSRWLockExclusive(&s_xx_crt_files_lock);
    if (s_xx_crt_std_files_ready != 2) {
        int i;
        for (i = 0; i < 3; ++i) {
            xx_crt_file *pFile = &s_xx_crt_std_files[i];
            pFile->nFd = i;
            pFile->nUnget = -1;
            pFile->nFlags = XX_CRT_FILE_INUSE | XX_CRT_FILE_STD | (i == 0 ? XX_CRT_FILE_READ : XX_CRT_FILE_WRITE);
            InitializeCriticalSection(&pFile->lock);
            InterlockedExchange(&pFile->nLockReady, 2);
        }
        InterlockedExchange(&s_xx_crt_std_files_ready, 2);
    }
    ReleaseSRWLockExclusive(&s_xx_crt_files_lock);
}

FILE *__cdecl __acrt_iob_func(unsigned nIndex)
{
    if (nIndex > 2) return NULL;
    xx_crt_std_files_init();
    return (FILE *)&s_xx_crt_std_files[nIndex];
}

static xx_crt_file *xx_crt_file_of(FILE *pStream)
{
    return (xx_crt_file *)pStream;
}

/* Allocates the buffer on first use: none for a console (each call writes
 * at once), 4096 bytes otherwise. */
static void xx_crt_file_buffer(xx_crt_file *pFile)
{
    xx_crt_fd *pFd;
    if (pFile->pBuffer || (pFile->nFlags & XX_CRT_FILE_UNBUF)) return;
    pFd = xx_crt_fd_get(pFile->nFd);
    /* UCRT: stderr is never buffered, and neither is output to a console. */
    if (!(pFile->nFlags & XX_CRT_FILE_SETBUF) &&
        (((pFile->nFlags & XX_CRT_FILE_STD) && pFile->nFd == 2) || (pFd && GetFileType(pFd->hHandle) == FILE_TYPE_CHAR))) {
        pFile->nFlags |= XX_CRT_FILE_UNBUF;
        return;
    }
    pFile->pBuffer = (char *)malloc(4096);
    if (!pFile->pBuffer) {
        pFile->nFlags |= XX_CRT_FILE_UNBUF;
        return;
    }
    pFile->nBufferSize = 4096;
    pFile->nFlags |= XX_CRT_FILE_OWNBUF;
}

/* The file position of the next character a read stream delivers, given
 * the position of the handle, computed the way UCRT's ftell does it:
 *  - nothing buffered or everything consumed: the handle's position;
 *  - binary: minus the unread bytes;
 *  - text: each line feed already consumed counts as two bytes, and the
 *    buffered block counts as the bytes read for it (when the handle is at
 *    the end of the file UCRT reconstructs that count as "characters plus
 *    line feeds, plus one after Ctrl+Z", which is exact for CR LF files and
 *    off for bare LF ones; that is UCRT's answer too).
 * A pending ungetc that is not in the buffer takes one off. */
static long long xx_crt_file_read_position(xx_crt_file *pFile, const xx_crt_fd *pFd, long long nHandle)
{
    const char *pRead = xx_crt_file_read_buffer(pFile);
    long long nUnget = pFile->nUnget >= 0 ? 1 : 0;
    long long nOffset = (long long)pFile->nPos;
    long long nBlock;
    size_t i;
    if (pFile->nLen == 0) return nHandle - nUnget;
    if (!(pFd->nFlags & XX_CRT_FD_TEXT)) return nHandle - (long long)(pFile->nLen - pFile->nPos) - nUnget;
    for (i = 0; i < pFile->nPos; ++i) nOffset += pRead[i] == '\n';
    if (nHandle == 0) return nOffset - nUnget;
    if (pFile->nPos == pFile->nLen) return nHandle - nUnget;
    {
        LARGE_INTEGER size;
        if (GetFileSizeEx(pFd->hHandle, &size) && size.QuadPart == nHandle) {
            nBlock = (long long)pFile->nLen + ((pFd->nFlags & XX_CRT_FD_EOF) ? 1 : 0);
            for (i = 0; i < pFile->nLen; ++i) nBlock += pRead[i] == '\n';
        } else {
            nBlock = (long long)pFile->nFillRaw;
        }
    }
    return nHandle - nBlock + nOffset - nUnget;
}

static int xx_crt_file_flush_nolock(xx_crt_file *pFile)
{
    if (pFile->nState == XX_CRT_STATE_WRITE && pFile->nPos) {
        size_t nWritten = xx_crt_fd_write(pFile->nFd, pFile->pBuffer, pFile->nPos);
        if (nWritten != pFile->nPos) {
            pFile->nFlags |= XX_CRT_FILE_ERROR;
            pFile->nPos = 0;
            return EOF;
        }
        pFile->nPos = 0;
    }
    if (pFile->nState == XX_CRT_STATE_READ) {
        /* drop the read-ahead: move the descriptor back to the logical position */
        xx_crt_fd *pFd = xx_crt_fd_get(pFile->nFd);
        long long nUnread = (long long)(pFile->nLen - pFile->nPos) + (pFile->nUnget >= 0 ? 1 : 0);
        if (nUnread && pFd && GetFileType(pFd->hHandle) == FILE_TYPE_DISK) {
            LARGE_INTEGER zero, here, target;
            zero.QuadPart = 0;
            if (SetFilePointerEx(pFd->hHandle, zero, &here, FILE_CURRENT)) {
                target.QuadPart = xx_crt_file_read_position(pFile, pFd, here.QuadPart);
                if (target.QuadPart < 0) target.QuadPart = 0;
                SetFilePointerEx(pFd->hHandle, target, NULL, FILE_BEGIN);
                pFd->nFlags &= ~XX_CRT_FD_EOF;
            }
        }
        pFile->nPos = pFile->nLen = 0;
        pFile->nUnget = -1;
    }
    pFile->nState = XX_CRT_STATE_IDLE;
    return 0;
}

static size_t xx_crt_file_write_nolock(xx_crt_file *pFile, const char *pData, size_t nSize)
{
    size_t nDone = 0;
    if (!(pFile->nFlags & XX_CRT_FILE_WRITE)) {
        pFile->nFlags |= XX_CRT_FILE_ERROR;
        errno = EBADF;
        return 0;
    }
    if (pFile->nState == XX_CRT_STATE_READ) xx_crt_file_flush_nolock(pFile);
    pFile->nState = XX_CRT_STATE_WRITE;
    xx_crt_file_buffer(pFile);
    if (pFile->nFlags & XX_CRT_FILE_UNBUF) {
        size_t nWritten = xx_crt_fd_write(pFile->nFd, pData, nSize);
        if (nWritten != nSize) pFile->nFlags |= XX_CRT_FILE_ERROR;
        return nWritten;
    }
    while (nDone < nSize) {
        size_t nRoom = pFile->nBufferSize - pFile->nPos;
        size_t nLeft = nSize - nDone;
        if (pFile->nPos == 0 && nLeft >= pFile->nBufferSize) {
            /* a block at least as large as the buffer goes straight out */
            size_t nDirect = nLeft - nLeft % pFile->nBufferSize;
            size_t nWritten = xx_crt_fd_write(pFile->nFd, pData + nDone, nDirect);
            nDone += nWritten;
            if (nWritten != nDirect) {
                pFile->nFlags |= XX_CRT_FILE_ERROR;
                return nDone;
            }
            continue;
        }
        if (nLeft < nRoom) nRoom = nLeft;
        memcpy(pFile->pBuffer + pFile->nPos, pData + nDone, nRoom);
        pFile->nPos += nRoom;
        nDone += nRoom;
        if (pFile->nPos == pFile->nBufferSize && xx_crt_file_flush_nolock(pFile) != 0) return nDone;
        pFile->nState = XX_CRT_STATE_WRITE;
    }
    return nDone;
}

/* Fills the read buffer; returns the number of bytes now available. */
static size_t xx_crt_file_fill(xx_crt_file *pFile)
{
    size_t nRead;
    xx_crt_file_buffer(pFile);
    {
        /* UCRT reads only 512 bytes into its own buffer right after a seek;
         * the block boundaries decide where a CR LF pair is split, and with
         * it what ftell answers there */
        size_t nWanted = xx_crt_file_read_size(pFile);
        if ((pFile->nFlags & XX_CRT_FILE_SMALLFILL) && nWanted > 512) nWanted = 512;
        pFile->nFlags &= ~XX_CRT_FILE_SMALLFILL;
        nRead = xx_crt_fd_read(pFile->nFd, xx_crt_file_read_buffer(pFile), nWanted, &pFile->nFillRaw);
    }
    if (nRead == (size_t)-1) {
        pFile->nFlags |= XX_CRT_FILE_ERROR;
        pFile->nPos = pFile->nLen = 0;
        return 0;
    }
    if (nRead == 0) pFile->nFlags |= XX_CRT_FILE_EOF;
    pFile->nPos = 0;
    pFile->nLen = nRead;
    return nRead;
}

static size_t xx_crt_file_read_nolock(xx_crt_file *pFile, char *pData, size_t nSize)
{
    size_t nDone = 0;
    if (!(pFile->nFlags & XX_CRT_FILE_READ)) {
        pFile->nFlags |= XX_CRT_FILE_ERROR;
        errno = EBADF;
        return 0;
    }
    if (pFile->nState == XX_CRT_STATE_WRITE) xx_crt_file_flush_nolock(pFile);
    if (pFile->nState != XX_CRT_STATE_READ) {
        pFile->nState = XX_CRT_STATE_READ;
        pFile->nPos = pFile->nLen = 0;
    }
    if (pFile->nUnget >= 0 && nSize) {
        pData[nDone++] = (char)pFile->nUnget;
        pFile->nUnget = -1;
    }
    xx_crt_file_buffer(pFile);
    while (nDone < nSize) {
        size_t nAvailable = pFile->nLen - pFile->nPos;
        if (nAvailable == 0) {
            size_t nLeft = nSize - nDone;
            if (nLeft >= xx_crt_file_read_size(pFile)) {
                size_t nRead;
                pFile->nPos = pFile->nLen = 0; /* nothing buffered: the handle is the position */
                nRead = xx_crt_fd_read(pFile->nFd, pData + nDone, nLeft, NULL);
                if (nRead == (size_t)-1) {
                    pFile->nFlags |= XX_CRT_FILE_ERROR;
                    break;
                }
                if (nRead == 0) {
                    pFile->nFlags |= XX_CRT_FILE_EOF;
                    break;
                }
                nDone += nRead;
                continue;
            }
            if (xx_crt_file_fill(pFile) == 0) break;
            nAvailable = pFile->nLen;
        }
        if (nAvailable > nSize - nDone) nAvailable = nSize - nDone;
        memcpy(pData + nDone, xx_crt_file_read_buffer(pFile) + pFile->nPos, nAvailable);
        pFile->nPos += nAvailable;
        nDone += nAvailable;
    }
    return nDone;
}

/* ------------------------------------------------------------- opening -- */

static FILE *xx_crt_file_new(HANDLE hHandle, int nFdFlags, int nFileFlags)
{
    xx_crt_file *pFile = (xx_crt_file *)calloc(1, sizeof(xx_crt_file));
    if (!pFile) {
        errno = ENOMEM;
        return NULL;
    }
    pFile->nFd = xx_crt_fd_alloc(hHandle, nFdFlags);
    if (pFile->nFd < 0) {
        free(pFile);
        errno = EMFILE;
        return NULL;
    }
    pFile->nFlags = nFileFlags | XX_CRT_FILE_INUSE;
    pFile->nUnget = -1;
    AcquireSRWLockExclusive(&s_xx_crt_files_lock);
    pFile->pNext = s_xx_crt_open_files;
    s_xx_crt_open_files = pFile;
    ReleaseSRWLockExclusive(&s_xx_crt_files_lock);
    return (FILE *)pFile;
}

static FILE *xx_crt_open(const wchar_t *pName, const wchar_t *pMode)
{
    DWORD nAccess = 0, nDisposition = 0, nAttributes = FILE_ATTRIBUTE_NORMAL;
    int nFdFlags = XX_CRT_FD_TEXT;
    int nFileFlags = 0;
    int bPlus = 0;
    const wchar_t *p = pMode;
    HANDLE hHandle;
    if (!pName || !pMode) {
        errno = EINVAL;
        return NULL;
    }
    while (*p == L' ') ++p;
    switch (*p) {
        case L'r': nAccess = GENERIC_READ; nDisposition = OPEN_EXISTING; nFileFlags = XX_CRT_FILE_READ; break;
        case L'w': nAccess = GENERIC_WRITE; nDisposition = CREATE_ALWAYS; nFileFlags = XX_CRT_FILE_WRITE; break;
        case L'a': nAccess = GENERIC_WRITE; nDisposition = OPEN_ALWAYS; nFileFlags = XX_CRT_FILE_WRITE; nFdFlags |= XX_CRT_FD_APPEND; break;
        default: errno = EINVAL; return NULL;
    }
    for (++p; *p; ++p) {
        if (*p == L'+') bPlus = 1;
        else if (*p == L'b') nFdFlags &= ~XX_CRT_FD_TEXT;
        else if (*p == L't') nFdFlags |= XX_CRT_FD_TEXT;
        else if (*p == L'D') nAttributes |= FILE_FLAG_DELETE_ON_CLOSE;
        else if (*p == L'T') nAttributes |= FILE_ATTRIBUTE_TEMPORARY;
        else if (*p == L',') break; /* ccs= is not supported: byte streams only */
    }
    if (bPlus) {
        nAccess = GENERIC_READ | GENERIC_WRITE;
        nFileFlags = XX_CRT_FILE_READ | XX_CRT_FILE_WRITE;
    }
    hHandle = CreateFileW(pName, nAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | ((nAttributes & FILE_FLAG_DELETE_ON_CLOSE) ? FILE_SHARE_DELETE : 0), NULL,
                          nDisposition, nAttributes, NULL);
    if (hHandle == INVALID_HANDLE_VALUE) {
        errno = xx_crt_errno_from_oserror(GetLastError());
        return NULL;
    }
    {
        FILE *pFile = xx_crt_file_new(hHandle, nFdFlags, nFileFlags);
        if (!pFile) CloseHandle(hHandle);
        return pFile;
    }
}

FILE *__cdecl _wfopen(const wchar_t *pName, const wchar_t *pMode)
{
    return xx_crt_open(pName, pMode);
}

FILE *__cdecl fopen(const char *pName, const char *pMode)
{
    wchar_t *pWideName = xx_crt_narrow_to_wide(pName);
    wchar_t *pWideMode = xx_crt_narrow_to_wide(pMode);
    FILE *pFile = NULL;
    if (pWideName && pWideMode) pFile = xx_crt_open(pWideName, pWideMode);
    else errno = pName && pMode ? ENOMEM : EINVAL;
    free(pWideName);
    free(pWideMode);
    return pFile;
}

errno_t __cdecl fopen_s(FILE **ppFile, const char *pName, const char *pMode)
{
    if (!ppFile) return EINVAL;
    *ppFile = fopen(pName, pMode);
    return *ppFile ? 0 : errno;
}

FILE *__cdecl tmpfile(void)
{
    static volatile LONG s_counter;
    wchar_t sDirectory[MAX_PATH + 1];
    wchar_t sName[MAX_PATH + 64];
    DWORD nLength = GetTempPathW(MAX_PATH + 1, sDirectory);
    int nAttempt;
    if (nLength == 0 || nLength > MAX_PATH) {
        errno = xx_crt_errno_from_oserror(GetLastError());
        return NULL;
    }
    for (nAttempt = 0; nAttempt < 1000; ++nAttempt) {
        LONG nCounter = InterlockedIncrement(&s_counter);
        HANDLE hHandle;
        DWORD nPid = GetCurrentProcessId();
        DWORD nTick = GetTickCount();
        static const wchar_t s_hex[] = L"0123456789abcdef";
        size_t i = 0;
        int k;
        while (sDirectory[i]) {
            sName[i] = sDirectory[i];
            ++i;
        }
        sName[i++] = L'x';
        sName[i++] = L'x';
        sName[i++] = L'c';
        for (k = 28; k >= 0; k -= 4) sName[i++] = s_hex[(nPid >> k) & 15];
        sName[i++] = L'-';
        for (k = 28; k >= 0; k -= 4) sName[i++] = s_hex[((DWORD)nCounter >> k) & 15];
        for (k = 12; k >= 0; k -= 4) sName[i++] = s_hex[(nTick >> k) & 15];
        sName[i++] = L'.';
        sName[i++] = L't';
        sName[i++] = L'm';
        sName[i++] = L'p';
        sName[i] = 0;
        hHandle = CreateFileW(sName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
        if (hHandle != INVALID_HANDLE_VALUE) {
            FILE *pFile = xx_crt_file_new(hHandle, 0, XX_CRT_FILE_READ | XX_CRT_FILE_WRITE);
            if (!pFile) CloseHandle(hHandle);
            return pFile;
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    errno = xx_crt_errno_from_oserror(GetLastError());
    return NULL;
}

int __cdecl fclose(FILE *pStream)
{
    xx_crt_file *pFile = xx_crt_file_of(pStream);
    int nResult;
    if (!pFile) {
        errno = EINVAL;
        return EOF;
    }
    xx_crt_file_lock(pFile);
    nResult = xx_crt_file_flush_nolock(pFile);
    if (pFile->nFlags & XX_CRT_FILE_STD) {
        /* closing a standard stream closes its descriptor */
        xx_crt_fd *pFd = xx_crt_fd_get(pFile->nFd);
        if (pFd && pFd->hHandle && pFd->hHandle != INVALID_HANDLE_VALUE) CloseHandle(pFd->hHandle);
        xx_crt_fd_release(pFile->nFd);
        pFile->nFlags = 0;
        xx_crt_file_unlock(pFile);
        return nResult;
    }
    {
        xx_crt_fd *pFd = xx_crt_fd_get(pFile->nFd);
        if (pFd && !CloseHandle(pFd->hHandle)) nResult = EOF;
        xx_crt_fd_release(pFile->nFd);
    }
    if (pFile->nFlags & XX_CRT_FILE_OWNBUF) free(pFile->pBuffer);
    xx_crt_file_unlock(pFile);
    AcquireSRWLockExclusive(&s_xx_crt_files_lock);
    {
        xx_crt_file **pp = &s_xx_crt_open_files;
        while (*pp && *pp != pFile) pp = &(*pp)->pNext;
        if (*pp) *pp = pFile->pNext;
    }
    ReleaseSRWLockExclusive(&s_xx_crt_files_lock);
    if (pFile->nLockReady == 2) DeleteCriticalSection(&pFile->lock);
    free(pFile);
    return nResult;
}

/* Flushes every open stream (exit, fflush(NULL)). */
int xx_crt_flush_all(void)
{
    int nResult = 0;
    int i;
    for (i = 0; i < 3; ++i) {
        xx_crt_file *pFile = &s_xx_crt_std_files[i];
        if (pFile->nFlags & XX_CRT_FILE_INUSE) {
            xx_crt_file_lock(pFile);
            if (pFile->nState == XX_CRT_STATE_WRITE && xx_crt_file_flush_nolock(pFile) != 0) nResult = EOF;
            xx_crt_file_unlock(pFile);
        }
    }
    AcquireSRWLockShared(&s_xx_crt_files_lock);
    {
        xx_crt_file *pFile;
        for (pFile = s_xx_crt_open_files; pFile; pFile = pFile->pNext) {
            xx_crt_file_lock(pFile);
            if (pFile->nState == XX_CRT_STATE_WRITE && xx_crt_file_flush_nolock(pFile) != 0) nResult = EOF;
            xx_crt_file_unlock(pFile);
        }
    }
    ReleaseSRWLockShared(&s_xx_crt_files_lock);
    return nResult;
}

/* Releases what the stream layer owns when a DLL that contains this
 * runtime is unloaded: every stream still open is closed (a tmpfile is
 * deleted with its handle), the standard streams are flushed and give up
 * their buffers and locks. The standard handles themselves belong to the
 * process and stay open. */
void xx_crt_stdio_shutdown(void)
{
    int i;
    for (;;) {
        xx_crt_file *pFile;
        AcquireSRWLockShared(&s_xx_crt_files_lock);
        pFile = s_xx_crt_open_files;
        ReleaseSRWLockShared(&s_xx_crt_files_lock);
        if (!pFile) break;
        fclose((FILE *)pFile);
    }
    if (s_xx_crt_std_files_ready == 2) {
        for (i = 0; i < 3; ++i) {
            xx_crt_file *pFile = &s_xx_crt_std_files[i];
            if (pFile->nFlags & XX_CRT_FILE_INUSE) {
                xx_crt_file_lock(pFile);
                xx_crt_file_flush_nolock(pFile);
                if (pFile->nFlags & XX_CRT_FILE_OWNBUF) free(pFile->pBuffer);
                pFile->pBuffer = NULL;
                pFile->nBufferSize = 0;
                pFile->nFlags &= ~(XX_CRT_FILE_OWNBUF | XX_CRT_FILE_UNBUF);
                xx_crt_file_unlock(pFile);
            }
            DeleteCriticalSection(&pFile->lock);
            pFile->nLockReady = 0;
            pFile->nFlags = 0;
        }
        InterlockedExchange(&s_xx_crt_std_files_ready, 0);
    }
}

int __cdecl fflush(FILE *pStream)
{
    xx_crt_file *pFile;
    int nResult;
    if (!pStream) return xx_crt_flush_all();
    pFile = xx_crt_file_of(pStream);
    xx_crt_file_lock(pFile);
    if (pFile->nState == XX_CRT_STATE_WRITE) {
        nResult = xx_crt_file_flush_nolock(pFile);
    } else {
        nResult = 0;
    }
    xx_crt_file_unlock(pFile);
    return nResult;
}

int __cdecl setvbuf(FILE *pStream, char *pBuffer, int nMode, size_t nSize)
{
    xx_crt_file *pFile = xx_crt_file_of(pStream);
    if (!pFile || (nMode != _IOFBF && nMode != _IOLBF && nMode != _IONBF)) {
        errno = EINVAL;
        return -1;
    }
    if (nMode != _IONBF && (nSize < 2 || nSize > INT_MAX)) {
        errno = EINVAL;
        return -1;
    }
    xx_crt_file_lock(pFile);
    xx_crt_file_flush_nolock(pFile);
    if (pFile->nFlags & XX_CRT_FILE_OWNBUF) free(pFile->pBuffer);
    pFile->pBuffer = NULL;
    pFile->nBufferSize = 0;
    pFile->nFlags &= ~(XX_CRT_FILE_OWNBUF | XX_CRT_FILE_UNBUF);
    pFile->nFlags |= XX_CRT_FILE_SETBUF;
    if (nMode == _IONBF) {
        pFile->nFlags |= XX_CRT_FILE_UNBUF;
    } else {
        nSize &= ~(size_t)1; /* UCRT rounds the size down to even */
        if (pBuffer) {
            pFile->pBuffer = pBuffer;
        } else {
            pFile->pBuffer = (char *)malloc(nSize);
            if (!pFile->pBuffer) {
                xx_crt_file_unlock(pFile);
                errno = ENOMEM;
                return -1;
            }
            pFile->nFlags |= XX_CRT_FILE_OWNBUF;
        }
        pFile->nBufferSize = nSize;
    }
    xx_crt_file_unlock(pFile);
    return 0;
}

void __cdecl setbuf(FILE *pStream, char *pBuffer)
{
    if (pBuffer) setvbuf(pStream, pBuffer, _IOFBF, BUFSIZ);
    else setvbuf(pStream, NULL, _IONBF, 0);
}

int __cdecl _fileno(FILE *pStream)
{
    if (!pStream) {
        errno = EINVAL;
        return -1;
    }
    return xx_crt_file_of(pStream)->nFd;
}

int __cdecl ferror(FILE *pStream)
{
    return pStream ? (xx_crt_file_of(pStream)->nFlags & XX_CRT_FILE_ERROR) : 0;
}

int __cdecl feof(FILE *pStream)
{
    return pStream ? (xx_crt_file_of(pStream)->nFlags & XX_CRT_FILE_EOF) : 0;
}

void __cdecl clearerr(FILE *pStream)
{
    if (pStream) xx_crt_file_of(pStream)->nFlags &= ~(XX_CRT_FILE_ERROR | XX_CRT_FILE_EOF);
}

/* -------------------------------------------------------------- output -- */

size_t __cdecl fwrite(const void *pData, size_t nSize, size_t nCount, FILE *pStream)
{
    xx_crt_file *pFile = xx_crt_file_of(pStream);
    size_t nTotal;
    size_t nWritten;
    if (!pFile || nSize == 0 || nCount == 0) return 0;
    if (nCount > (size_t)-1 / nSize) {
        errno = EINVAL;
        return 0;
    }
    nTotal = nSize * nCount;
    xx_crt_file_lock(pFile);
    nWritten = xx_crt_file_write_nolock(pFile, (const char *)pData, nTotal);
    xx_crt_file_unlock(pFile);
    return nWritten / nSize;
}

int __cdecl fputs(const char *pText, FILE *pStream)
{
    size_t nLength = strlen(pText);
    xx_crt_file *pFile = xx_crt_file_of(pStream);
    size_t nWritten;
    if (!pFile) return EOF;
    xx_crt_file_lock(pFile);
    nWritten = xx_crt_file_write_nolock(pFile, pText, nLength);
    xx_crt_file_unlock(pFile);
    return nWritten == nLength ? 0 : EOF;
}

int __cdecl fputc(int nChar, FILE *pStream)
{
    char c = (char)nChar;
    xx_crt_file *pFile = xx_crt_file_of(pStream);
    size_t nWritten;
    if (!pFile) return EOF;
    xx_crt_file_lock(pFile);
    if (pFile->nState == XX_CRT_STATE_WRITE && pFile->pBuffer && pFile->nPos + 1 < pFile->nBufferSize) {
        pFile->pBuffer[pFile->nPos++] = c;
        nWritten = 1;
    } else {
        nWritten = xx_crt_file_write_nolock(pFile, &c, 1);
    }
    xx_crt_file_unlock(pFile);
    return nWritten == 1 ? (int)(unsigned char)c : EOF;
}

int __cdecl putc(int nChar, FILE *pStream)
{
    return fputc(nChar, pStream);
}

int __cdecl putchar(int nChar)
{
    return fputc(nChar, __acrt_iob_func(1));
}

int __cdecl puts(const char *pText)
{
    xx_crt_file *pFile = xx_crt_file_of(__acrt_iob_func(1));
    size_t nLength = strlen(pText);
    int nResult = 0;
    xx_crt_file_lock(pFile);
    if (xx_crt_file_write_nolock(pFile, pText, nLength) != nLength || xx_crt_file_write_nolock(pFile, "\n", 1) != 1) nResult = EOF;
    xx_crt_file_unlock(pFile);
    return nResult;
}

/* --------------------------------------------------------------- input -- */

size_t __cdecl fread(void *pData, size_t nSize, size_t nCount, FILE *pStream)
{
    xx_crt_file *pFile = xx_crt_file_of(pStream);
    size_t nTotal;
    size_t nRead;
    if (!pFile || nSize == 0 || nCount == 0) return 0;
    if (nCount > (size_t)-1 / nSize) {
        errno = EINVAL;
        return 0;
    }
    nTotal = nSize * nCount;
    xx_crt_file_lock(pFile);
    nRead = xx_crt_file_read_nolock(pFile, (char *)pData, nTotal);
    xx_crt_file_unlock(pFile);
    return nRead / nSize;
}

static int xx_crt_file_getc_nolock(xx_crt_file *pFile)
{
    unsigned char c;
    if (pFile->nState == XX_CRT_STATE_READ && pFile->nUnget < 0 && pFile->nPos < pFile->nLen) {
        return (unsigned char)xx_crt_file_read_buffer(pFile)[pFile->nPos++];
    }
    return xx_crt_file_read_nolock(pFile, (char *)&c, 1) == 1 ? (int)c : EOF;
}

int __cdecl fgetc(FILE *pStream)
{
    xx_crt_file *pFile = xx_crt_file_of(pStream);
    int nResult;
    if (!pFile) return EOF;
    xx_crt_file_lock(pFile);
    nResult = xx_crt_file_getc_nolock(pFile);
    xx_crt_file_unlock(pFile);
    return nResult;
}

int __cdecl getc(FILE *pStream)
{
    return fgetc(pStream);
}

int __cdecl getchar(void)
{
    return fgetc(__acrt_iob_func(0));
}

int __cdecl ungetc(int nChar, FILE *pStream)
{
    xx_crt_file *pFile = xx_crt_file_of(pStream);
    if (!pFile || nChar == EOF) return EOF;
    xx_crt_file_lock(pFile);
    if (pFile->nState == XX_CRT_STATE_WRITE) xx_crt_file_flush_nolock(pFile);
    pFile->nState = XX_CRT_STATE_READ;
    if (pFile->nPos > 0 && pFile->nUnget < 0) {
        xx_crt_file_read_buffer(pFile)[--pFile->nPos] = (char)nChar;
    } else {
        pFile->nUnget = (unsigned char)nChar;
    }
    pFile->nFlags &= ~XX_CRT_FILE_EOF;
    xx_crt_file_unlock(pFile);
    return (unsigned char)nChar;
}

/* The stream stays locked for the whole line, as in UCRT: two threads that
 * read lines from one stream each get whole lines. */
char *__cdecl fgets(char *pBuffer, int nSize, FILE *pStream)
{
    xx_crt_file *pFile = xx_crt_file_of(pStream);
    int i = 0;
    if (!pBuffer || nSize <= 0 || !pFile) {
        errno = EINVAL;
        return NULL;
    }
    xx_crt_file_lock(pFile);
    while (i < nSize - 1) {
        int c = xx_crt_file_getc_nolock(pFile);
        if (c == EOF) break;
        pBuffer[i++] = (char)c;
        if (c == '\n') break;
    }
    xx_crt_file_unlock(pFile);
    if (i == 0 && nSize > 1) return NULL;
    pBuffer[i] = 0;
    return pBuffer;
}

/* ----------------------------------------------------------- positions -- */

long long __cdecl _ftelli64(FILE *pStream)
{
    xx_crt_file *pFile = xx_crt_file_of(pStream);
    xx_crt_fd *pFd;
    LARGE_INTEGER zero, current;
    long long nResult;
    if (!pFile) {
        errno = EINVAL;
        return -1;
    }
    xx_crt_file_lock(pFile);
    pFd = xx_crt_fd_get(pFile->nFd);
    zero.QuadPart = 0;
    if (!pFd || !SetFilePointerEx(pFd->hHandle, zero, &current, FILE_CURRENT)) {
        xx_crt_file_unlock(pFile);
        errno = pFd ? xx_crt_errno_from_oserror(GetLastError()) : EBADF;
        return -1;
    }
    nResult = current.QuadPart;
    if (pFile->nState == XX_CRT_STATE_WRITE) {
        nResult += (long long)pFile->nPos;
        if (pFd->nFlags & XX_CRT_FD_TEXT) {
            size_t i;
            for (i = 0; i < pFile->nPos; ++i) nResult += pFile->pBuffer[i] == '\n';
        }
    } else if (pFile->nState == XX_CRT_STATE_READ) {
        nResult = xx_crt_file_read_position(pFile, pFd, nResult);
    }
    xx_crt_file_unlock(pFile);
    return nResult;
}

long __cdecl ftell(FILE *pStream)
{
    long long nPosition = _ftelli64(pStream);
    if (nPosition > 0x7FFFFFFF) {
        errno = EINVAL;
        return -1;
    }
    return (long)nPosition;
}

int __cdecl _fseeki64(FILE *pStream, long long nOffset, int nOrigin)
{
    xx_crt_file *pFile = xx_crt_file_of(pStream);
    xx_crt_fd *pFd;
    LARGE_INTEGER distance;
    DWORD nMethod;
    if (!pFile || (nOrigin != SEEK_SET && nOrigin != SEEK_CUR && nOrigin != SEEK_END)) {
        errno = EINVAL;
        return -1;
    }
    if (nOrigin == SEEK_CUR) {
        long long nHere = _ftelli64(pStream);
        if (nHere < 0) return -1;
        nOffset += nHere;
        nOrigin = SEEK_SET;
    }
    xx_crt_file_lock(pFile);
    if (pFile->nState == XX_CRT_STATE_WRITE) {
        if (xx_crt_file_flush_nolock(pFile) != 0) {
            xx_crt_file_unlock(pFile);
            return -1;
        }
    }
    if (pFile->nState == XX_CRT_STATE_READ && !(pFile->nFlags & XX_CRT_FILE_SETBUF)) pFile->nFlags |= XX_CRT_FILE_SMALLFILL;
    pFile->nState = XX_CRT_STATE_IDLE;
    pFile->nPos = pFile->nLen = 0;
    pFile->nUnget = -1;
    pFd = xx_crt_fd_get(pFile->nFd);
    distance.QuadPart = nOffset;
    nMethod = nOrigin == SEEK_SET ? FILE_BEGIN : FILE_END;
    if (!pFd || !SetFilePointerEx(pFd->hHandle, distance, NULL, nMethod)) {
        xx_crt_file_unlock(pFile);
        errno = pFd ? xx_crt_errno_from_oserror(GetLastError()) : EBADF;
        return -1;
    }
    pFd->nFlags &= ~XX_CRT_FD_EOF;
    pFile->nFlags &= ~XX_CRT_FILE_EOF;
    xx_crt_file_unlock(pFile);
    return 0;
}

int __cdecl fseek(FILE *pStream, long nOffset, int nOrigin)
{
    return _fseeki64(pStream, nOffset, nOrigin);
}

void __cdecl rewind(FILE *pStream)
{
    if (_fseeki64(pStream, 0, SEEK_SET) == 0) {
        xx_crt_file *pFile = xx_crt_file_of(pStream);
        clearerr(pStream);
        /* unlike fseek, UCRT's rewind is followed by a full-size read */
        xx_crt_file_lock(pFile);
        pFile->nFlags &= ~XX_CRT_FILE_SMALLFILL;
        xx_crt_file_unlock(pFile);
    }
}

int __cdecl fgetpos(FILE *pStream, fpos_t *pPosition)
{
    long long nPosition = _ftelli64(pStream);
    if (nPosition < 0 || !pPosition) return -1;
    *pPosition = nPosition;
    return 0;
}

int __cdecl fsetpos(FILE *pStream, const fpos_t *pPosition)
{
    if (!pPosition) {
        errno = EINVAL;
        return -1;
    }
    return _fseeki64(pStream, *pPosition, SEEK_SET);
}

void __cdecl _lock_file(FILE *pStream)
{
    if (pStream) xx_crt_file_lock(xx_crt_file_of(pStream));
}

void __cdecl _unlock_file(FILE *pStream)
{
    if (pStream) xx_crt_file_unlock(xx_crt_file_of(pStream));
}

/* ---------------------------------------------------- printf and scanf -- */

/* Writes formatted pieces to a locked stream. A console stream (no buffer)
 * gathers one call's output and writes it at once, as UCRT's temporary
 * buffering does. */
typedef struct xx_crt_stream_sink_s {
    xx_crt_file *pFile;
    char sTemp[4096];
    size_t nTemp;
    int bTemp;
    int bError;
} xx_crt_stream_sink;

static int xx_crt_stream_sink_write(void *pContext, const char *pData, size_t nSize)
{
    xx_crt_stream_sink *pSink = (xx_crt_stream_sink *)pContext;
    if (pSink->bTemp) {
        if (pSink->nTemp + nSize > sizeof(pSink->sTemp)) {
            if (pSink->nTemp && xx_crt_file_write_nolock(pSink->pFile, pSink->sTemp, pSink->nTemp) != pSink->nTemp) pSink->bError = 1;
            pSink->nTemp = 0;
            if (nSize > sizeof(pSink->sTemp)) {
                if (xx_crt_file_write_nolock(pSink->pFile, pData, nSize) != nSize) pSink->bError = 1;
                return pSink->bError;
            }
        }
        memcpy(pSink->sTemp + pSink->nTemp, pData, nSize);
        pSink->nTemp += nSize;
        return 0;
    }
    if (xx_crt_file_write_nolock(pSink->pFile, pData, nSize) != nSize) pSink->bError = 1;
    return pSink->bError;
}

int __cdecl __stdio_common_vfprintf(unsigned __int64 nOptions, FILE *pStream, const char *pFormat, _locale_t pLocale, va_list args)
{
    xx_crt_stream_sink sink;
    int nResult;
    (void)nOptions;
    (void)pLocale;
    if (!pStream || !pFormat) {
        errno = EINVAL;
        return -1;
    }
    sink.pFile = xx_crt_file_of(pStream);
    sink.nTemp = 0;
    sink.bError = 0;
    xx_crt_file_lock(sink.pFile);
    xx_crt_file_buffer(sink.pFile);
    sink.bTemp = (sink.pFile->nFlags & XX_CRT_FILE_UNBUF) ? 1 : 0;
    nResult = xx_crt_vformat(xx_crt_stream_sink_write, &sink, pFormat, args);
    if (sink.bTemp && sink.nTemp && xx_crt_file_write_nolock(sink.pFile, sink.sTemp, sink.nTemp) != sink.nTemp) sink.bError = 1;
    xx_crt_file_unlock(sink.pFile);
    return sink.bError ? -1 : nResult;
}

int __cdecl __stdio_common_vsprintf(unsigned __int64 nOptions, char *pBuffer, size_t nCount, const char *pFormat, _locale_t pLocale, va_list args)
{
    int nResult;
    (void)pLocale;
    if (!pFormat || (!pBuffer && nCount)) {
        errno = EINVAL;
        return -1;
    }
    if (nOptions & _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR) {
        return xx_crt_vsnprintf(pBuffer, nCount, pFormat, args);
    }
    if (!pBuffer) return xx_crt_vsnprintf(NULL, 0, pFormat, args);
    /* Legacy _vsnprintf: room for nCount characters, a terminator only when
     * it fits, -1 when the output was cut. */
    nResult = xx_crt_vsnprintf_raw(pBuffer, nCount, pFormat, args);
    if (nResult < 0) return -1;
    if ((size_t)nResult < nCount) {
        pBuffer[nResult] = 0;
        return nResult;
    }
    if ((size_t)nResult == nCount) return nResult;
    if (!(nOptions & _CRT_INTERNAL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION) && nCount) pBuffer[nCount - 1] = 0;
    return -1;
}

int __cdecl __stdio_common_vsnprintf_s(unsigned __int64 nOptions, char *pBuffer, size_t nSize, size_t nMaxCount, const char *pFormat, _locale_t pLocale,
                                       va_list args)
{
    int nResult;
    size_t nLimit;
    (void)nOptions;
    (void)pLocale;
    if (!pFormat || !pBuffer || nSize == 0) {
        errno = EINVAL;
        return -1;
    }
    nLimit = nMaxCount == _TRUNCATE ? nSize - 1 : (nMaxCount < nSize - 1 ? nMaxCount : nSize - 1);
    nResult = xx_crt_vsnprintf(pBuffer, nLimit + 1, pFormat, args);
    if (nResult < 0) {
        pBuffer[0] = 0;
        return -1;
    }
    if ((size_t)nResult > nLimit) {
        if (nMaxCount == _TRUNCATE || nMaxCount < nSize) return -1;
        pBuffer[0] = 0;
        errno = ERANGE;
        return -1;
    }
    return nResult;
}

int __cdecl __stdio_common_vsprintf_s(unsigned __int64 nOptions, char *pBuffer, size_t nSize, const char *pFormat, _locale_t pLocale, va_list args)
{
    int nResult;
    (void)nOptions;
    (void)pLocale;
    if (!pFormat || !pBuffer || nSize == 0) {
        errno = EINVAL;
        return -1;
    }
    nResult = xx_crt_vsnprintf(pBuffer, nSize, pFormat, args);
    if (nResult < 0 || (size_t)nResult >= nSize) {
        pBuffer[0] = 0;
        errno = ERANGE;
        return -1;
    }
    return nResult;
}

int __cdecl __stdio_common_vsscanf(unsigned __int64 nOptions, const char *pInput, size_t nCount, const char *pFormat, _locale_t pLocale, va_list args)
{
    (void)pLocale;
    if (!pInput || !pFormat) {
        errno = EINVAL;
        return -1;
    }
    if (nCount != (size_t)-1) {
        /* _snscanf: scan at most nCount characters */
        char *pCopy;
        int nResult;
        nCount = strnlen(pInput, nCount); /* never read past the terminator */
        pCopy = (char *)malloc(nCount + 1);
        if (!pCopy) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(pCopy, pInput, nCount);
        pCopy[nCount] = 0;
        nResult = xx_crt_vsscanf(pCopy, pFormat, (nOptions & _CRT_INTERNAL_SCANF_SECURECRT) ? 1 : 0, args);
        free(pCopy);
        return nResult;
    }
    return xx_crt_vsscanf(pInput, pFormat, (nOptions & _CRT_INTERNAL_SCANF_SECURECRT) ? 1 : 0, args);
}

void __cdecl perror(const char *pMessage)
{
    FILE *pError = __acrt_iob_func(2);
    int nError = errno;
    if (pMessage && *pMessage) {
        fputs(pMessage, pError);
        fputs(": ", pError);
    }
    fputs(xx_crt_strerror_text(nError), pError);
    fputs("\n", pError);
}

#endif /* _WIN32 && _MSC_VER */
