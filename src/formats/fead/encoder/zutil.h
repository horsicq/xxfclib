/* FEAD port: symbol isolation and xxfclib memory operations; original notice retained. */
/* zutil.h -- internal interface and configuration of the compression library
 * Copyright (C) 1995-2003 Jean-loup Gailly.
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

/* @(#) $Id$ */

#ifndef ZUTIL_H
#define ZUTIL_H
#include "xxfclib/memory/xx_memory.h"

#define ZLIB_INTERNAL
#include "zlib.h"

#ifdef STDC
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#endif
#ifdef NO_ERRNO_H
extern int errno;
#else
#include <errno.h>
#endif

#ifndef local
#define local static
#endif
/* compile with -Dlocal if your debugger can't find static symbols */

typedef unsigned char uch;
typedef uch FAR uchf;
typedef unsigned short ush;
typedef ush FAR ushf;
typedef unsigned long ulg;

extern const char *const z_errmsg[10]; /* indexed by 2-zlib_error */
/* (size given to avoid silly warnings with Visual C++) */

#define ERR_MSG(err) z_errmsg[Z_NEED_DICT - (err)]

#define ERR_RETURN(strm, err) return (strm->msg = (char *)ERR_MSG(err), (err))
/* To be used only when the state is known to be valid */

/* common constants */

#ifndef DEF_WBITS
#define DEF_WBITS MAX_WBITS
#endif
/* default windowBits for decompression. MAX_WBITS is for compression only */

#if MAX_MEM_LEVEL >= 8
#define DEF_MEM_LEVEL 8
#else
#define DEF_MEM_LEVEL MAX_MEM_LEVEL
#endif
/* default memLevel */

#define STORED_BLOCK 0
#define STATIC_TREES 1
#define DYN_TREES 2
/* The three kinds of block type */

#define MIN_MATCH 3
#define MAX_MATCH 258
/* The minimum and maximum match lengths */

#define PRESET_DICT 0x20 /* preset dictionary flag in zlib header */

/* target dependencies */

#if defined(__MSDOS__) || (defined(_WINDOWS) && !defined(_WIN32))
#define OS_CODE 0x00
#if defined(__TURBOC__) || defined(__BORLANDC__)
#if (__STDC__ == 1) && (defined(__LARGE__) || defined(__COMPACT__))
/* Allow compilation with ANSI keywords only enabled */
void _Cdecl farfree(void *block);
void *_Cdecl farmalloc(unsigned long nbytes);
#else
#include <alloc.h>
#endif
#else /* MSC or DJGPP */
#include <malloc.h>
#endif
#endif

#ifdef AMIGA
#define OS_CODE 0x01
#endif

#if defined(VAXC) || defined(VMS)
#define OS_CODE 0x02
#define F_OPEN(name, mode) fopen((name), (mode), "mbc=60", "ctx=stm", "rfm=fix", "mrs=512")
#endif

#if defined(ATARI) || defined(atarist)
#define OS_CODE 0x05
#endif

#ifdef OS2
#define OS_CODE 0x06
#endif

#if defined(MACOS) || defined(TARGET_OS_MAC)
#define OS_CODE 0x07
#if defined(__MWERKS__) && __dest_os != __be_os && __dest_os != __win32_os
#include <unix.h> /* for fdopen */
#else
#ifndef fdopen
#define fdopen(fd, mode) NULL /* No fdopen() */
#endif
#endif
#endif

#ifdef TOPS20
#define OS_CODE 0x0a
#endif

#ifdef WIN32
#ifndef __CYGWIN__ /* Cygwin is Unix, not Win32 */
#define OS_CODE 0x0b
#endif
#endif

#ifdef __50SERIES /* Prime/PRIMOS */
#define OS_CODE 0x0f
#endif

#if defined(_BEOS_) || defined(RISCOS)
#define fdopen(fd, mode) NULL /* No fdopen() */
#endif

#if (defined(_MSC_VER) && (_MSC_VER > 600))
#if defined(_WIN32_WCE)
#define fdopen(fd, mode) NULL /* No fdopen() */
#ifndef _PTRDIFF_T_DEFINED
typedef int ptrdiff_t;
#define _PTRDIFF_T_DEFINED
#endif
#else
#define fdopen(fd, type) _fdopen(fd, type)
#endif
#endif

/* common defaults */

#ifndef OS_CODE
#define OS_CODE 0x03 /* assume Unix */
#endif

#ifndef F_OPEN
#define F_OPEN(name, mode) fopen((name), (mode))
#endif

/* functions */

#if defined(STDC99) || (defined(__TURBOC__) && __TURBOC__ >= 0x550)
#ifndef HAVE_VSNPRINTF
#define HAVE_VSNPRINTF
#endif
#endif
#if defined(__CYGWIN__)
#ifndef HAVE_VSNPRINTF
#define HAVE_VSNPRINTF
#endif
#endif
#ifndef HAVE_VSNPRINTF
#ifdef MSDOS
/* vsnprintf may exist on some MS-DOS compilers (DJGPP?),
   but for now we just assume it doesn't. */
#define NO_vsnprintf
#endif
#ifdef __TURBOC__
#define NO_vsnprintf
#endif
#ifdef WIN32
/* In Win32, vsnprintf is available as the "non-ANSI" _vsnprintf. */
#if !defined(vsnprintf) && !defined(NO_vsnprintf)
#define vsnprintf _vsnprintf
#endif
#endif
#ifdef __SASC
#define NO_vsnprintf
#endif
#endif

#ifdef HAVE_STRERROR
extern char *strerror OF((int));
#define zstrerror(errnum) strerror(errnum)
#else
#define zstrerror(errnum) ""
#endif

/* FEAD port: use xxfclib routines in every build, including freestanding. */
#define zmemcpy xx_mem_copy
#define zmemcmp xx_mem_compare
#define zmemzero(dest, len) xx_mem_zero(dest, len)

/* Diagnostic functions */
#ifdef FEAD_ZLIB_DEBUG
#include <stdio.h>
extern int z_verbose;
extern void z_error OF((char *m));
#define Assert(cond, msg)          \
    {                              \
        if (!(cond)) z_error(msg); \
    }
#define Trace(x)                       \
    {                                  \
        if (z_verbose >= 0) fprintf x; \
    }
#define Tracev(x)                     \
    {                                 \
        if (z_verbose > 0) fprintf x; \
    }
#define Tracevv(x)                    \
    {                                 \
        if (z_verbose > 1) fprintf x; \
    }
#define Tracec(c, x)                         \
    {                                        \
        if (z_verbose > 0 && (c)) fprintf x; \
    }
#define Tracecv(c, x)                        \
    {                                        \
        if (z_verbose > 1 && (c)) fprintf x; \
    }
#else
#define Assert(cond, msg)
#define Trace(x)
#define Tracev(x)
#define Tracevv(x)
#define Tracec(c, x)
#define Tracecv(c, x)
#endif

voidpf zcalloc OF((voidpf opaque, unsigned items, unsigned size));
void zcfree OF((voidpf opaque, voidpf ptr));

#define ZALLOC(strm, items, size) (*((strm)->zalloc))((strm)->opaque, (items), (size))
#define ZFREE(strm, addr) (*((strm)->zfree))((strm)->opaque, (voidpf)(addr))
#define TRY_FREE(s, p)      \
    {                       \
        if (p) ZFREE(s, p); \
    }

#endif /* ZUTIL_H */
