/* SQLite 3.53.4 is public domain. Canonical source bundled in upstream/.
 * No operating-system VFS, file access, extensions, worker threads or mmap. */
#ifndef XXFC_SQLITE_HOSTED_DISABLED
#define SQLITE_OS_OTHER 1
#define SQLITE_TEMP_STORE 3
#define SQLITE_THREADSAFE 0
#define SQLITE_OMIT_LOAD_EXTENSION 1
#define SQLITE_OMIT_WAL 1
#define SQLITE_OMIT_SHARED_CACHE 1
#define SQLITE_MAX_ALLOCATION_SIZE 33554432
#define SQLITE_DEFAULT_MEMSTATUS 1
#include "upstream/sqlite3.c"
static int xxfu_sqlite_open(sqlite3_vfs *v, const char *n, sqlite3_file *f, int flags, int *out)
{
    (void)v;
    (void)n;
    (void)f;
    (void)flags;
    (void)out;
    return SQLITE_CANTOPEN;
}
static int xxfu_sqlite_delete(sqlite3_vfs *v, const char *n, int s)
{
    (void)v;
    (void)n;
    (void)s;
    return SQLITE_IOERR_DELETE;
}
static int xxfu_sqlite_access(sqlite3_vfs *v, const char *n, int flags, int *out)
{
    (void)v;
    (void)n;
    (void)flags;
    *out = 0;
    return SQLITE_OK;
}
static int xxfu_sqlite_path(sqlite3_vfs *v, const char *n, int cap, char *out)
{
    (void)v;
    if ((int)strlen(n) >= cap) return SQLITE_CANTOPEN;
    memcpy(out, n, strlen(n) + 1);
    return SQLITE_OK;
}
static int xxfu_sqlite_random(sqlite3_vfs *v, int n, char *out)
{
    (void)v;
    memset(out, 0, n);
    return n;
}
static int xxfu_sqlite_sleep(sqlite3_vfs *v, int n)
{
    (void)v;
    return n;
}
static int xxfu_sqlite_time(sqlite3_vfs *v, double *out)
{
    (void)v;
    *out = 2440587.5;
    return SQLITE_OK;
}
int sqlite3_os_init(void)
{
    static sqlite3_vfs v;
    memset(&v, 0, sizeof(v));
    v.iVersion = 1;
    v.szOsFile = sizeof(sqlite3_file);
    v.mxPathname = 1024;
    v.zName = "xfu-ram-only";
    v.xOpen = xxfu_sqlite_open;
    v.xDelete = xxfu_sqlite_delete;
    v.xAccess = xxfu_sqlite_access;
    v.xFullPathname = xxfu_sqlite_path;
    v.xRandomness = xxfu_sqlite_random;
    v.xSleep = xxfu_sqlite_sleep;
    v.xCurrentTime = xxfu_sqlite_time;
    return sqlite3_vfs_register(&v, 1);
}
int sqlite3_os_end(void)
{
    return SQLITE_OK;
}
#endif
