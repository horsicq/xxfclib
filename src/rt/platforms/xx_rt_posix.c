/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_rt_posix.c
 * @brief POSIX runtime implementations, delegating to libc. Cygwin takes this path too, since CMake's if(WIN32) is false there.
 *
 * Split out of xx_rt.c, which keeps the portable remainder. Every function
 * here is defined under its public xx_rt_ name with no wrapper layer, the
 * shape xx_rt_utf8_to_utf16 already uses in the string platform files.
 */

#if !defined(_WIN32)

#include "xxfclib/rt/xx_rt.h"
#include "xx_rt_platform.h"

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

/* Hosted build: the standard headers back the stubs. */
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

XX_RT_NORETURN void xx_rt_exit(int nCode)
{
    exit(nCode);
}

char *xx_rt_getenv(const char *pName)
{
    return getenv(pName);
}


void xx_rt_qsort(void *pBase, size_t nCount, size_t nSize,
                 xx_rt_compare_fn fnCompare)
{
    if ((pBase == NULL) || (fnCompare == NULL) || (nCount < 2) ||
        (nSize == 0) || (nCount > ((size_t)-1) / nSize)) {
        return;
    }

    qsort(pBase, nCount, nSize, fnCompare);
}

int xx_rt_rand(void)
{
    return rand() % (XX_RT_RAND_MAX + 1);
}


void *xx_rt_stdout(void)
{
    return (void *)stdout;
}

void *xx_rt_stderr(void)
{
    return (void *)stderr;
}

int xx_rt_platform_write_stream(void *pStream, const char *pData, size_t nSize)
{
    return (int)fwrite(pData, 1, nSize, (FILE *)pStream);
}

void *xx_rt_fopen(const char *pFileName, const char *pMode)
{
    return (void *)fopen(pFileName, pMode);
}

int xx_rt_fclose(void *pFile)
{
    return fclose((FILE *)pFile);
}

int xx_rt_remove(const char *pFileName)
{
    return remove(pFileName);
}

size_t xx_rt_fread(void *pBuffer, size_t nSize, size_t nCount, void *pFile)
{
    return fread(pBuffer, nSize, nCount, (FILE *)pFile);
}

int xx_rt_fseek(void *pFile, long long nOffset, int nOrigin)
{
    int nWhence = SEEK_SET;
    long nSeek = (long)nOffset;

    if (nOrigin == XX_RT_SEEK_CUR) {
        nWhence = SEEK_CUR;
    } else if (nOrigin == XX_RT_SEEK_END) {
        nWhence = SEEK_END;
    }

    /* fseek() carries a long. That is 64 bit on the LP64 targets cdie ships
     * for; where it is narrower the offset simply cannot be expressed, and
     * fseeko() is not visible under -std=c99.                              */
    if ((long long)nSeek != nOffset) {
        return -1;
    }

    return fseek((FILE *)pFile, nSeek, nWhence);
}

long long xx_rt_ftell(void *pFile)
{
    return (long long)ftell((FILE *)pFile);
}

void xx_rt_rewind(void *pFile)
{
    rewind((FILE *)pFile);
}

int xx_rt_fflush(void *pStream)
{
    return fflush((FILE *)pStream);
}



XX_RT_PRINTF_LIKE(3, 0) int xx_rt_vsnprintf(char *pBuffer, size_t nSize, const char *pFormat, XX_RT_VA_LIST args)
{
    return vsnprintf(pBuffer, nSize, pFormat, args);
}


long long xx_rt_clock_ms(void)
{
    struct timespec time;

    if (clock_gettime(CLOCK_MONOTONIC, &time) == 0) {
        return (long long)time.tv_sec * 1000 + (long long)(time.tv_nsec / 1000000);
    }

    /* No monotonic clock: processor time is the next best answer. */
    return (long long)(((double)clock() * 1000.0) / (double)CLOCKS_PER_SEC);
}

#endif /* !_WIN32 */
