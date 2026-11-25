/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#if defined(__FreeBSD__)
/* FreeBSD's public non-POSIX ABI must remain visible. Older supported releases
 * do not interpret _BSD_SOURCE, so remove restrictive feature selections in
 * this native-only translation unit before any header. */
#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif
#ifdef _POSIX_SOURCE
#undef _POSIX_SOURCE
#endif
#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif
#ifndef _BSD_SOURCE
#define _BSD_SOURCE 1
#endif
#include "../xx_process_structs_internal.h"
#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/exec.h>
#include <sys/elf_common.h> /* EI_*, ELFCLASS*, ELFDATA*, and AT_* declarations. */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Public FreeBSD ABI references, checked against main and releng/14.4:
 * https://github.com/freebsd/freebsd-src/blob/main/sys/sys/user.h
 * https://github.com/freebsd/freebsd-src/blob/main/sys/sys/sysctl.h
 * https://github.com/freebsd/freebsd-src/blob/main/sys/sys/exec.h
 * https://github.com/freebsd/freebsd-src/blob/main/sys/kern/kern_proc.c
 * https://github.com/freebsd/freebsd-src/blob/main/lib/libprocstat/libprocstat.c
 * https://github.com/freebsd/freebsd-src/blob/main/sys/x86/include/elf.h
 * https://github.com/freebsd/freebsd-src/blob/main/sys/arm64/include/elf.h
 * kinfo_* records describe kernel query output, never remotely addressable
 * kernel structs. No kernel pointer from those records is dereferenced. */
#define PSF_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define PSF_WIDE_(s) L##s
#define PSF_WIDE(s) PSF_WIDE_(s)
#define PSF_N XX_DATA_STRUCT_RECORD_PROPERTY_NONE
#define PSF_ID XX_DATA_STRUCT_RECORD_PROPERTY_ID
#define PSF_SZ XX_DATA_STRUCT_RECORD_PROPERTY_SIZE
#define PSF_CT XX_DATA_STRUCT_RECORD_PROPERTY_COUNT
#define PSF_FL XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS
#define PSF_TS XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP
#define PSF_STR XX_DATA_STRUCT_RECORD_PROPERTY_STRING
#define PSF_PTR (XX_DATA_STRUCT_RECORD_PROPERTY_POINTER | XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
#define PSF_TYPE(v)                                                        \
    _Generic((v),                                                          \
        char: L"char",                                                     \
        signed char: L"int8",                                              \
        unsigned char: L"uint8",                                           \
        short: L"int16",                                                   \
        unsigned short: L"uint16",                                         \
        int: L"int32",                                                     \
        unsigned int: L"uint32",                                           \
        long: sizeof(long) == 8 ? L"int64" : L"int32",                     \
        unsigned long: sizeof(unsigned long) == 8 ? L"uint64" : L"uint32", \
        long long: L"int64",                                               \
        unsigned long long: L"uint64",                                     \
        char *: L"char[]",                                                 \
        const char *: L"char[]",                                           \
        default: L"bytes[]")
#define PSF_FIELD(t, m, p) {PSF_WIDE(#m), PSF_TYPE(((t *)0)->m), offsetof(t, m), sizeof(((t *)0)->m), p}

static const xx_data_struct_field_desc psf_process_fields[] = {
    PSF_FIELD(struct kinfo_proc, ki_structsize, PSF_SZ),   PSF_FIELD(struct kinfo_proc, ki_layout, PSF_ID),   PSF_FIELD(struct kinfo_proc, ki_pid, PSF_ID),
    PSF_FIELD(struct kinfo_proc, ki_ppid, PSF_ID),         PSF_FIELD(struct kinfo_proc, ki_pgid, PSF_ID),     PSF_FIELD(struct kinfo_proc, ki_tpgid, PSF_ID),
    PSF_FIELD(struct kinfo_proc, ki_sid, PSF_ID),          PSF_FIELD(struct kinfo_proc, ki_tsid, PSF_ID),     PSF_FIELD(struct kinfo_proc, ki_jobc, PSF_CT),
    PSF_FIELD(struct kinfo_proc, ki_uid, PSF_ID),          PSF_FIELD(struct kinfo_proc, ki_ruid, PSF_ID),     PSF_FIELD(struct kinfo_proc, ki_svuid, PSF_ID),
    PSF_FIELD(struct kinfo_proc, ki_rgid, PSF_ID),         PSF_FIELD(struct kinfo_proc, ki_svgid, PSF_ID),    PSF_FIELD(struct kinfo_proc, ki_ngroups, PSF_CT),
    PSF_FIELD(struct kinfo_proc, ki_groups, PSF_N),        PSF_FIELD(struct kinfo_proc, ki_size, PSF_SZ),     PSF_FIELD(struct kinfo_proc, ki_rssize, PSF_CT),
    PSF_FIELD(struct kinfo_proc, ki_tsize, PSF_CT),        PSF_FIELD(struct kinfo_proc, ki_dsize, PSF_CT),    PSF_FIELD(struct kinfo_proc, ki_ssize, PSF_CT),
    PSF_FIELD(struct kinfo_proc, ki_xstat, PSF_N),         PSF_FIELD(struct kinfo_proc, ki_acflag, PSF_FL),   PSF_FIELD(struct kinfo_proc, ki_pctcpu, PSF_N),
    PSF_FIELD(struct kinfo_proc, ki_estcpu, PSF_N),        PSF_FIELD(struct kinfo_proc, ki_slptime, PSF_N),   PSF_FIELD(struct kinfo_proc, ki_swtime, PSF_N),
    PSF_FIELD(struct kinfo_proc, ki_cow, PSF_CT),          PSF_FIELD(struct kinfo_proc, ki_runtime, PSF_N),   PSF_FIELD(struct kinfo_proc, ki_start.tv_sec, PSF_TS),
    PSF_FIELD(struct kinfo_proc, ki_start.tv_usec, PSF_N), PSF_FIELD(struct kinfo_proc, ki_flag, PSF_FL),     PSF_FIELD(struct kinfo_proc, ki_kiflag, PSF_FL),
    PSF_FIELD(struct kinfo_proc, ki_traceflag, PSF_FL),    PSF_FIELD(struct kinfo_proc, ki_stat, PSF_N),      PSF_FIELD(struct kinfo_proc, ki_nice, PSF_N),
    PSF_FIELD(struct kinfo_proc, ki_tdname, PSF_STR),      PSF_FIELD(struct kinfo_proc, ki_wmesg, PSF_STR),   PSF_FIELD(struct kinfo_proc, ki_login, PSF_STR),
    PSF_FIELD(struct kinfo_proc, ki_lockname, PSF_STR),    PSF_FIELD(struct kinfo_proc, ki_comm, PSF_STR),    PSF_FIELD(struct kinfo_proc, ki_emul, PSF_STR),
    PSF_FIELD(struct kinfo_proc, ki_loginclass, PSF_STR),  PSF_FIELD(struct kinfo_proc, ki_tdev, PSF_ID),     PSF_FIELD(struct kinfo_proc, ki_oncpu, PSF_ID),
    PSF_FIELD(struct kinfo_proc, ki_lastcpu, PSF_ID),      PSF_FIELD(struct kinfo_proc, ki_tracer, PSF_ID),   PSF_FIELD(struct kinfo_proc, ki_flag2, PSF_FL),
    PSF_FIELD(struct kinfo_proc, ki_fibnum, PSF_ID),       PSF_FIELD(struct kinfo_proc, ki_cr_flags, PSF_FL), PSF_FIELD(struct kinfo_proc, ki_jid, PSF_ID),
    PSF_FIELD(struct kinfo_proc, ki_numthreads, PSF_CT),   PSF_FIELD(struct kinfo_proc, ki_tid, PSF_ID),      PSF_FIELD(struct kinfo_proc, ki_sflag, PSF_FL),
    PSF_FIELD(struct kinfo_proc, ki_tdflags, PSF_FL)};
static const xx_data_struct_field_desc psf_vm_fields[] = {PSF_FIELD(struct kinfo_vmentry, kve_structsize, PSF_SZ),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_type, PSF_ID),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_start, PSF_PTR),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_end, PSF_PTR),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_offset, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_vn_fileid, PSF_ID),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_flags, PSF_FL),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_resident, PSF_CT),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_private_resident, PSF_CT),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_protection, PSF_FL),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_ref_count, PSF_CT),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_shadow_count, PSF_CT),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_vn_type, PSF_ID),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_vn_size, PSF_SZ),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_vn_mode, PSF_FL),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_status, PSF_FL),
                                                          PSF_FIELD(struct kinfo_vmentry, kve_vn_rdev, PSF_ID)};
static const xx_data_struct_field_desc psf_strings32[] = {{L"ps_argvstr", L"pointer32", 0, 4, PSF_PTR},
                                                          {L"ps_nargvstr", L"uint32", 4, 4, PSF_CT},
                                                          {L"ps_envstr", L"pointer32", 8, 4, PSF_PTR},
                                                          {L"ps_nenvstr", L"uint32", 12, 4, PSF_CT}};
static const xx_data_struct_field_desc psf_strings64[] = {{L"ps_argvstr", L"pointer64", 0, 8, PSF_PTR},
                                                          {L"ps_nargvstr", L"uint32", 8, 4, PSF_CT},
                                                          {L"ps_envstr", L"pointer64", 16, 8, PSF_PTR},
                                                          {L"ps_nenvstr", L"uint32", 24, 4, PSF_CT}};

static bool psf_big_endian(void)
{
    const uint16_t one = 1;
    return *(const unsigned char *)&one == 0;
}

static uint64_t psf_number(const unsigned char *bytes, size_t size, bool big_endian)
{
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) value = (value << 8) | bytes[big_endian ? i : size - i - 1];
    return value;
}

static xx_process_structs_status psf_error(int error)
{
    if (error == ENOMEM) return XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
    if (error == ESRCH || error == ENOENT || error == EPERM || error == EACCES || error == ENOTSUP) return XX_PROCESS_STRUCTS_UNAVAILABLE;
    return XX_PROCESS_STRUCTS_IO_ERROR;
}

/* Retry growing sysctl results without a fixed record cap. A finite retry
 * budget prevents a continuously changing target from keeping this call busy. */
static int psf_blob(int operation, pid_t pid, unsigned char **out, size_t *out_size)
{
    int mib[4] = {CTL_KERN, KERN_PROC, operation, (int)pid};
    unsigned char *bytes = NULL;
    size_t capacity = 0;
    *out = NULL;
    *out_size = 0;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        size_t required = 0, size;
        if (sysctl(mib, 4, NULL, &required, NULL, 0) != 0) {
            int error = errno;
            /* AUXV can report ENOMEM when target stack reads fail. Keep that
             * query failure distinct from our own allocation failure. */
            free(bytes);
            return error == ENOMEM ? EAGAIN : error;
        }
        if (!required && !capacity) return 0;
        if (required > capacity) capacity = required;
        else {
            size_t extra = capacity / 2 + 1;
            if (capacity > SIZE_MAX - extra) {
                free(bytes);
                return EOVERFLOW;
            }
            capacity += extra;
        }
        unsigned char *grown = (unsigned char *)realloc(bytes, capacity);
        if (!grown) {
            free(bytes);
            return ENOMEM;
        }
        bytes = grown;
        size = capacity;
        if (sysctl(mib, 4, bytes, &size, NULL, 0) == 0) {
            if (size > capacity) {
                free(bytes);
                return EOVERFLOW;
            }
            *out = bytes;
            *out_size = size;
            return 0;
        }
        int error = errno;
        if (error != ENOMEM) {
            free(bytes);
            return error;
        }
    }
    free(bytes);
    return EAGAIN;
}

static xx_process_structs_status psf_identity(pid_t pid, struct kinfo_proc *info, uint64_t *identity)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid};
    size_t size = sizeof(*info);
    memset(info, 0, sizeof(*info));
    if (sysctl(mib, 4, info, &size, NULL, 0) != 0) return errno == ENOMEM ? XX_PROCESS_STRUCTS_UNSUPPORTED : psf_error(errno);
    if (!size) return XX_PROCESS_STRUCTS_UNAVAILABLE;
    if (size != sizeof(*info) || info->ki_structsize != (int)sizeof(*info)) return XX_PROCESS_STRUCTS_UNSUPPORTED;
    if (info->ki_pid != pid || info->ki_start.tv_sec <= 0 || info->ki_start.tv_usec < 0 || info->ki_start.tv_usec >= 1000000) return XX_PROCESS_STRUCTS_UNAVAILABLE;
    uint64_t seconds = (uint64_t)info->ki_start.tv_sec, micros = (uint64_t)info->ki_start.tv_usec;
    if (seconds > (UINT64_MAX - micros) / UINT64_C(1000000)) return XX_PROCESS_STRUCTS_UNAVAILABLE;
    *identity = seconds * UINT64_C(1000000) + micros;
    return XX_PROCESS_STRUCTS_OK;
}

static xx_process_structure *psf_record(xx_ps_context *context, const char *name, const char *layout, uint64_t address, bool has_address, uint64_t size, uint64_t thread,
                                        const xx_data_struct_field_desc *fields, size_t count, const void *bytes, size_t length, bool big_endian)
{
    xx_process_structure *item = xx_ps_add(context, name, layout, address, has_address, size, thread);
    if (item && !xx_ps_decode(context, item, fields, count, bytes, length, big_endian)) return NULL;
    return item;
}

static void psf_query_failed(xx_ps_context *context, const char *name, int error)
{
    if (error == ENOMEM) context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
    else xx_ps_warn(context, "%s was unavailable (%s).", name, strerror(error));
}

static bool psf_room(xx_ps_context *context)
{
    return context->snapshot->structure_count < context->limits.max_structures;
}

/* The location query is always represented, including when unavailable.
 * Before it is collected, exhausting the cap proves at least one omission. */
static bool psf_pending_metadata(xx_ps_context *context)
{
    if (psf_room(context)) return true;
    context->snapshot->truncated = true;
    xx_ps_warn(context, "Additional process metadata omitted by structure limit.");
    return false;
}

/* Query kinfo_proc records use the inspector's ABI, including for ELF32 targets. */
static void psf_threads(xx_ps_context *context, pid_t pid, uint64_t identity)
{
    unsigned char *bytes = NULL;
    size_t length = 0, used = 0;
    int error = psf_blob(KERN_PROC_PID | KERN_PROC_INC_THREAD, pid, &bytes, &length);
    if (error) {
        psf_query_failed(context, "KERN_PROC_PID|KERN_PROC_INC_THREAD", error);
        return;
    }
    if (length % sizeof(struct kinfo_proc)) {
        xx_ps_warn(context, "Thread kinfo_proc query ABI size mismatch.");
        free(bytes);
        return;
    }
    for (size_t at = 0; at < length && context->status == XX_PROCESS_STRUCTS_OK; at += sizeof(struct kinfo_proc)) {
        struct kinfo_proc info;
        char name[96];
        memcpy(&info, bytes + at, sizeof(info));
        if (info.ki_structsize != (int)sizeof(info) || info.ki_pid != pid) {
            xx_ps_warn(context, "Thread kinfo_proc query ABI/identity mismatch.");
            break;
        }
        if (info.ki_start.tv_sec <= 0 || info.ki_start.tv_usec < 0 || info.ki_start.tv_usec >= 1000000 ||
            (uint64_t)info.ki_start.tv_sec > (UINT64_MAX - (uint64_t)info.ki_start.tv_usec) / UINT64_C(1000000) ||
            (uint64_t)info.ki_start.tv_sec * UINT64_C(1000000) + (uint64_t)info.ki_start.tv_usec != identity) {
            context->status = XX_PROCESS_STRUCTS_CHANGED;
            break;
        }
        if (used >= context->limits.max_threads) {
            context->snapshot->truncated = true;
            xx_ps_warn(context, "Thread limit reached.");
            break;
        }
        ++used;
        snprintf(name, sizeof(name), "kinfo_proc [TID %" PRIu64 "]", info.ki_tid > 0 ? (uint64_t)info.ki_tid : 0);
        if (!psf_record(context, name, "FreeBSD native thread sysctl query result", 0, false, sizeof(info), info.ki_tid > 0 ? (uint64_t)info.ki_tid : 0,
                        psf_process_fields, PSF_COUNT(psf_process_fields), &info, sizeof(info), psf_big_endian()))
            break;
    }
    free(bytes);
}

static void psf_maps(xx_ps_context *context, pid_t pid)
{
    unsigned char *bytes = NULL;
    size_t length = 0, used = 0, at = 0;
    if (!psf_room(context)) {
        /* A size probe confirms pending mappings without allocating a whole
         * VM-map blob for a snapshot that cannot retain any more records. */
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, (int)pid};
        if (sysctl(mib, 4, NULL, &length, NULL, 0)) psf_query_failed(context, "KERN_PROC_VMMAP", errno == ENOMEM ? EAGAIN : errno);
        else if (length) {
            context->snapshot->truncated = true;
            xx_ps_warn(context, "VM-map records omitted by structure limit.");
        }
        return;
    }
    int error = psf_blob(KERN_PROC_VMMAP, pid, &bytes, &length);
    if (error) {
        psf_query_failed(context, "KERN_PROC_VMMAP", error);
        return;
    }
    while (at < length && context->status == XX_PROCESS_STRUCTS_OK) {
        int packed_size;
        struct kinfo_vmentry map;
        char name[96];
        size_t path_offset = offsetof(struct kinfo_vmentry, kve_path), path_size;
        if (length - at < sizeof(packed_size)) {
            xx_ps_warn(context, "Incomplete VM-map query record.");
            break;
        }
        memcpy(&packed_size, bytes + at, sizeof(packed_size));
        if (packed_size <= 0 || (size_t)packed_size > length - at || (size_t)packed_size > sizeof(map) || (size_t)packed_size <= path_offset) {
            xx_ps_warn(context, "VM-map query record ABI size mismatch.");
            break;
        }
        /* A VM mapping is not a loader module. max_structures limits these
         * query records; max_modules is inapplicable without loader discovery. */
        memset(&map, 0, sizeof(map));
        memcpy(&map, bytes + at, (size_t)packed_size);
        path_size = (size_t)packed_size - path_offset;
        const char *end = (const char *)memchr(map.kve_path, 0, path_size);
        if (!end) {
            xx_ps_warn(context, "VM-map query path has no terminator.");
            break;
        }
        snprintf(name, sizeof(name), "kinfo_vmentry [%zu]", used++);
        xx_process_structure *item = psf_record(context, name, "FreeBSD packed native sysctl query result", 0, false, (uint64_t)packed_size, 0, psf_vm_fields,
                                                PSF_COUNT(psf_vm_fields), &map, (size_t)packed_size, psf_big_endian());
        if (!item) break;
        if (!xx_ps_bytes(context, item, L"kve_path", L"char[]", path_offset, (size_t)(end - map.kve_path) + 1, PSF_STR, map.kve_path, true)) break;
        at += (size_t)packed_size;
    }
    free(bytes);
}

static void psf_path(xx_ps_context *context, pid_t pid, char **raw_path)
{
    unsigned char *bytes = NULL;
    size_t length = 0;
    int error = psf_blob(KERN_PROC_PATHNAME, pid, &bytes, &length);
    *raw_path = NULL;
    if (error) {
        psf_query_failed(context, "KERN_PROC_PATHNAME", error);
        return;
    }
    if (!length || !memchr(bytes, 0, length)) {
        if (length) xx_ps_warn(context, "Executable path query was not terminated.");
        free(bytes);
        return;
    }
    snprintf(context->snapshot->path, sizeof(context->snapshot->path), "%s", (const char *)bytes);
    *raw_path = (char *)bytes;
}

/* KERN_PROC_SV_NAME is also used by libprocstat for ELF32 detection. Current
 * kinfo_proc has no KI_32BIT member/flag. Unknown emulation layouts stay unknown. */
static unsigned psf_abi(xx_ps_context *context, pid_t pid, const struct kinfo_proc *info, const char *path, bool *native_freebsd, bool *big_endian)
{
    unsigned char *bytes = NULL, elf[20];
    size_t length = 0;
    char abi[64];
    unsigned width = 0;
    int error = psf_blob(KERN_PROC_SV_NAME, pid, &bytes, &length);
    *native_freebsd = false;
    *big_endian = psf_big_endian();
    if (!error && length && memchr(bytes, 0, length)) snprintf(abi, sizeof(abi), "%s", (const char *)bytes);
    else snprintf(abi, sizeof(abi), "%.*s", (int)sizeof(info->ki_emul), info->ki_emul);
    if (error == ENOMEM) context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
    free(bytes);
    if (!strcmp(abi, "FreeBSD ELF32")) {
        width = 4;
        *native_freebsd = true;
    } else if (!strcmp(abi, "FreeBSD ELF64")) {
        width = 8;
        *native_freebsd = true;
    } else if (!strcmp(abi, "Linux ELF32")) width = 4;
    else if (!strcmp(abi, "Linux ELF64")) width = 8;
    snprintf(context->snapshot->architecture, sizeof(context->snapshot->architecture), "%.*s", (int)sizeof(context->snapshot->architecture) - 1,
             abi[0] ? abi : "unknown ABI");
    if (path && path[0]) {
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            size_t used = 0;
            while (used < sizeof(elf)) {
                ssize_t got = read(fd, elf + used, sizeof(elf) - used);
                if (got < 0 && errno == EINTR) continue;
                if (got <= 0) break;
                used += (size_t)got;
            }
            (void)close(fd);
            if (used == sizeof(elf) && !memcmp(elf, "\177ELF", 4) && (elf[EI_CLASS] == ELFCLASS32 || elf[EI_CLASS] == ELFCLASS64) &&
                (elf[EI_DATA] == ELFDATA2LSB || elf[EI_DATA] == ELFDATA2MSB)) {
                unsigned file_width = elf[EI_CLASS] == ELFCLASS64 ? 8 : 4;
                if (width && width != file_width) {
                    xx_ps_warn(context, "Target ABI and executable ELF class disagree; remote layouts are omitted.");
                    width = 0;
                    *native_freebsd = false;
                } else {
                    width = file_width;
                    *big_endian = elf[EI_DATA] == ELFDATA2MSB;
                    uint64_t machine = psf_number(elf + 18, 2, *big_endian);
                    snprintf(context->snapshot->architecture, sizeof(context->snapshot->architecture), "ELF%u machine %" PRIu64 "%s", width * 8, machine,
                             *big_endian ? " BE" : " LE");
                }
            }
        }
    }
    if (!width) xx_ps_warn(context, "Target pointer width is unknown; no target-width layout is guessed.");
    return width;
}

static const char *psf_aux_name(uint64_t type)
{
    switch (type) {
        case AT_NULL: return "AT_NULL";
        case AT_PHDR: return "AT_PHDR";
        case AT_PHENT: return "AT_PHENT";
        case AT_PHNUM: return "AT_PHNUM";
        case AT_PAGESZ: return "AT_PAGESZ";
        case AT_BASE: return "AT_BASE";
        case AT_FLAGS: return "AT_FLAGS";
        case AT_ENTRY: return "AT_ENTRY";
#ifdef AT_EXECPATH
        case AT_EXECPATH: return "AT_EXECPATH";
#endif
#ifdef AT_CANARY
        case AT_CANARY: return "AT_CANARY";
#endif
#ifdef AT_CANARYLEN
        case AT_CANARYLEN: return "AT_CANARYLEN";
#endif
#ifdef AT_OSRELDATE
        case AT_OSRELDATE: return "AT_OSRELDATE";
#endif
#ifdef AT_NCPUS
        case AT_NCPUS: return "AT_NCPUS";
#endif
#ifdef AT_PAGESIZES
        case AT_PAGESIZES: return "AT_PAGESIZES";
#endif
#ifdef AT_PAGESIZESLEN
        case AT_PAGESIZESLEN: return "AT_PAGESIZESLEN";
#endif
#ifdef AT_TIMEKEEP
        case AT_TIMEKEEP: return "AT_TIMEKEEP";
#endif
        default: return "unknown";
    }
}

static bool psf_aux_pointer(uint64_t type)
{
    if (type == AT_PHDR || type == AT_BASE || type == AT_ENTRY) return true;
#ifdef AT_EXECPATH
    if (type == AT_EXECPATH) return true;
#endif
#ifdef AT_CANARY
    if (type == AT_CANARY) return true;
#endif
#ifdef AT_PAGESIZES
    if (type == AT_PAGESIZES) return true;
#endif
#ifdef AT_TIMEKEEP
    if (type == AT_TIMEKEEP) return true;
#endif
    return false;
}

static void psf_auxv(xx_ps_context *context, pid_t pid, unsigned width, bool native_freebsd, bool big_endian)
{
    unsigned char *bytes = NULL;
    size_t length = 0;
    bool ended = false;
    if (!native_freebsd || (width != 4 && width != 8)) {
        xx_ps_warn(context, "AUXV decoding requires a confirmed FreeBSD ELF32/ELF64 ABI.");
        return;
    }
    int error = psf_blob(KERN_PROC_AUXV, pid, &bytes, &length);
    if (error) {
        psf_query_failed(context, "KERN_PROC_AUXV", error);
        return;
    }
    /* FreeBSD machine ELF headers define Elf64_Auxinfo with long a_type,
     * followed by a union of long/pointer: two 8-byte slots, not int+padding.
     * Elf32_Auxinfo uses int a_type followed by a 4-byte union. Confirmed in
     * x86, arm64, powerpc and riscv public headers; for example:
     * https://github.com/freebsd/freebsd-src/blob/main/sys/arm64/include/elf.h#L48-L61
     * AT_* identifiers are in sys/sys/elf_common.h, not these machine layouts. */
    size_t stride = 2 * width;
    if (length % stride) {
        xx_ps_warn(context, "AUXV query has an incomplete target-width entry.");
        free(bytes);
        return;
    }
    for (size_t at = 0; at < length && context->status == XX_PROCESS_STRUCTS_OK; at += stride) {
        uint64_t type = psf_number(bytes + at, width, big_endian);
        char name[96];
        snprintf(name, sizeof(name), "Elf%u_Auxinfo [%zu] %s", width * 8, at / stride, psf_aux_name(type));
        xx_process_structure *item = xx_ps_add(context, name, "KERN_PROC_AUXV target-width query bytes (no target address)", 0, false, stride, 0);
        if (!item) break;
        if (!xx_ps_uint(context, item, L"a_type", width == 8 ? L"int64" : L"int32", 0, width, PSF_ID, type, true) ||
            !xx_ps_uint(context, item, L"a_un.a_val", width == 8 ? L"uint64" : L"uint32", width, width, psf_aux_pointer(type) ? PSF_PTR : PSF_N,
                        psf_number(bytes + at + width, width, big_endian), true))
            break;
        if (type == AT_NULL) {
            ended = true;
            break;
        }
    }
    if (length && !ended && !context->snapshot->truncated && context->status == XX_PROCESS_STRUCTS_OK) xx_ps_warn(context, "AUXV query had no AT_NULL terminator.");
    free(bytes);
}

static void psf_ps_strings(xx_ps_context *context, pid_t pid, xx_io_device *io, unsigned width, bool native_freebsd, bool big_endian)
{
    uintptr_t native_address = 0;
    size_t length = sizeof(native_address);
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PS_STRINGS, (int)pid};
    bool available = sysctl(mib, 4, &native_address, &length, NULL, 0) == 0 && length == sizeof(native_address);
    uint64_t address = (uint64_t)native_address;
    xx_process_structure *item = xx_ps_add(context, "KERN_PROC_PS_STRINGS", "Native-width sysctl query result (location only)", 0, false, sizeof(native_address), 0);
    if (!item) return;
    if (!xx_ps_uint(context, item, L"ps_strings_address", sizeof(native_address) == 8 ? L"uint64" : L"uint32", 0, sizeof(native_address), PSF_PTR, address, available))
        return;
    if (!available || !address) {
        xx_ps_warn(context, "The target ps_strings address was unavailable.");
        return;
    }
    if (!native_freebsd || (width != 4 && width != 8) || (width == 4 && address > UINT32_MAX)) {
        xx_ps_warn(context, "ps_strings memory decoding requires a confirmed matching FreeBSD target ABI.");
        return;
    }
    unsigned char bytes[32];
    size_t size = width == 8 ? 32 : 16;
    available = xx_ps_read(io, address, bytes, size);
    (void)psf_record(context, width == 8 ? "ps_strings64" : "ps_strings32", "FreeBSD public ps_strings target-memory layout", address, true, size, 0,
                     width == 8 ? psf_strings64 : psf_strings32, 4, available ? bytes : NULL, available ? size : 0, big_endian);
    if (!available) xx_ps_warn(context, "ps_strings memory is unreadable; its fields remain unavailable.");
}

xx_process_structs_status xx_ps_collect(xx_ps_context *context, uint64_t requested_pid, uint64_t expected_start)
{
    struct kinfo_proc initial, after;
    uint64_t identity = 0, final_identity = 0;
    xx_io_device *io = NULL;
    char *path = NULL;
    bool native_freebsd, big_endian;
    unsigned width;
    xx_process_structs_status status;
    if (!context || !context->snapshot || requested_pid > INT_MAX) return XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
    pid_t pid = requested_pid ? (pid_t)requested_pid : getpid();
    if (pid <= 0) return XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
    status = psf_identity(pid, &initial, &identity);
    if (status != XX_PROCESS_STRUCTS_OK) return status;
    if (expected_start && identity != expected_start) return XX_PROCESS_STRUCTS_CHANGED;
    context->snapshot->pid = (uint64_t)pid;
    context->snapshot->start_time = identity;
    snprintf(context->snapshot->name, sizeof(context->snapshot->name), "%.*s", (int)sizeof(initial.ki_comm), initial.ki_comm);
    if (!psf_record(context, "kinfo_proc", "FreeBSD native process sysctl query result", 0, false, sizeof(initial), 0, psf_process_fields, PSF_COUNT(psf_process_fields),
                    &initial, sizeof(initial), psf_big_endian()))
        goto done;
    psf_path(context, pid, &path);
    width = psf_abi(context, pid, &initial, path, &native_freebsd, &big_endian);
    context->snapshot->pointer_size = width;
    if (context->status != XX_PROCESS_STRUCTS_OK) goto done;
    if (!psf_pending_metadata(context)) goto done;
    psf_threads(context, pid, identity);
    if (context->status != XX_PROCESS_STRUCTS_OK) goto done;
    if (!psf_pending_metadata(context)) goto done;
    psf_auxv(context, pid, width, native_freebsd, big_endian);
    if (context->status != XX_PROCESS_STRUCTS_OK) goto done;
    if (!psf_pending_metadata(context)) goto done;
    io = xx_io_process_open_ro((uint64_t)pid);
    if (!io) xx_ps_warn(context, "Read-only procfs process memory is unavailable; kernel query records remain usable.");
    status = psf_identity(pid, &after, &final_identity);
    if (status != XX_PROCESS_STRUCTS_OK || final_identity != identity) {
        status = XX_PROCESS_STRUCTS_CHANGED;
        goto cleanup;
    }
    psf_ps_strings(context, pid, io, width, native_freebsd, big_endian);
    if (context->status != XX_PROCESS_STRUCTS_OK) goto done;
    psf_maps(context, pid);
done:
    if (context->status != XX_PROCESS_STRUCTS_OK) status = context->status;
    else {
        status = psf_identity(pid, &after, &final_identity);
        if (status != XX_PROCESS_STRUCTS_OK || final_identity != identity) status = XX_PROCESS_STRUCTS_CHANGED;
    }
cleanup:
    if (io) (void)xx_io_process_close(io);
    free(path);
    return status;
}
#endif /* __FreeBSD__ */
