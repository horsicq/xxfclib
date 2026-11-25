/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#if !defined(_WIN32) && !defined(__FreeBSD__)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "../xx_process_structs_internal.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <mach/thread_info.h>
#include <mach-o/loader.h>
#include <sys/proc_info.h>
#elif defined(__linux__)
#include <dirent.h>
#endif

#define PS_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define PS_WIDE_(s) L##s
#define PS_WIDE(s) PS_WIDE_(s)
#define PS_N XX_DATA_STRUCT_RECORD_PROPERTY_NONE
#define PS_ID XX_DATA_STRUCT_RECORD_PROPERTY_ID
#define PS_PTR (XX_DATA_STRUCT_RECORD_PROPERTY_POINTER | XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
#define PS_SZ XX_DATA_STRUCT_RECORD_PROPERTY_SIZE
#define PS_CT XX_DATA_STRUCT_RECORD_PROPERTY_COUNT
#define PS_FL XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS
#define PS_TS XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP
#define PS_STR XX_DATA_STRUCT_RECORD_PROPERTY_STRING
#define PS_FIELD_TYPE(v)                                                   \
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
#define PS_FIELD(t, m, p) {PS_WIDE(#m), PS_FIELD_TYPE(((t *)0)->m), offsetof(t, m), sizeof(((t *)0)->m), p}

#if defined(__linux__) || defined(__APPLE__)
static int ps_room(const xx_ps_context *c)
{
    return c->status == XX_PROCESS_STRUCTS_OK && c->snapshot->structure_count < c->limits.max_structures;
}

static void ps_omitted(xx_ps_context *c, const char *message)
{
    if (c->status == XX_PROCESS_STRUCTS_OK) {
        c->snapshot->truncated = true;
        xx_ps_warn(c, "%s", message);
    }
}

static uint64_t ps_number(const unsigned char *p, size_t n, int be)
{
    uint64_t result = 0;
    size_t i;
    for (i = 0; i < n; ++i) result = (result << 8) | p[be ? i : n - 1 - i];
    return result;
}

static xx_process_structure *ps_record(xx_ps_context *c, const char *name, const char *layout, uint64_t address, int has_address, size_t size, uint64_t thread,
                                       const xx_data_struct_field_desc *fields, size_t count, const void *data, size_t length, int be)
{
    xx_process_structure *s = xx_ps_add(c, name, layout, address, has_address != 0, size, thread);
    if (s) (void)xx_ps_decode(c, s, fields, count, data, length, be != 0);
    return s;
}

static void ps_remote_string(xx_io_device *io, uint64_t address, char *out, size_t capacity)
{
    size_t i;
    if (!out || !capacity) return;
    out[0] = '\0';
    if (!address) return;
    for (i = 0; i + 1 < capacity && (uint64_t)i <= UINT64_MAX - address; ++i) {
        unsigned char ch;
        if (!xx_ps_read(io, address + i, &ch, 1)) {
            out[i] = '\0';
            return;
        }
        if (!ch) {
            out[i] = '\0';
            return;
        }
        /* The field comment is bounded printable ASCII; raw target pointers
         * remain exact even if the remote path has another encoding. */
        out[i] = ch >= 32 && ch < 127 ? (char)ch : '?';
    }
    out[i] = '\0';
}

static xx_process_structs_status ps_error(int error)
{
    if (error == ENOMEM) return XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
    if (error == EACCES || error == EPERM || error == ENOENT || error == ESRCH || error == ENOTSUP) return XX_PROCESS_STRUCTS_UNAVAILABLE;
    return XX_PROCESS_STRUCTS_IO_ERROR;
}
#endif

#if defined(__APPLE__)
/* These are kernel query result schemas, not addresses in the target. */
static const xx_data_struct_field_desc ps_bsd_fields[] = {
    PS_FIELD(struct proc_bsdinfo, pbi_flags, PS_FL), PS_FIELD(struct proc_bsdinfo, pbi_status, PS_N),       PS_FIELD(struct proc_bsdinfo, pbi_xstatus, PS_N),
    PS_FIELD(struct proc_bsdinfo, pbi_pid, PS_ID),   PS_FIELD(struct proc_bsdinfo, pbi_ppid, PS_ID),        PS_FIELD(struct proc_bsdinfo, pbi_uid, PS_ID),
    PS_FIELD(struct proc_bsdinfo, pbi_gid, PS_ID),   PS_FIELD(struct proc_bsdinfo, pbi_ruid, PS_ID),        PS_FIELD(struct proc_bsdinfo, pbi_rgid, PS_ID),
    PS_FIELD(struct proc_bsdinfo, pbi_svuid, PS_ID), PS_FIELD(struct proc_bsdinfo, pbi_svgid, PS_ID),       PS_FIELD(struct proc_bsdinfo, pbi_comm, PS_STR),
    PS_FIELD(struct proc_bsdinfo, pbi_name, PS_STR), PS_FIELD(struct proc_bsdinfo, pbi_nfiles, PS_CT),      PS_FIELD(struct proc_bsdinfo, pbi_pgid, PS_ID),
    PS_FIELD(struct proc_bsdinfo, pbi_pjobc, PS_CT), PS_FIELD(struct proc_bsdinfo, e_tdev, PS_ID),          PS_FIELD(struct proc_bsdinfo, e_tpgid, PS_ID),
    PS_FIELD(struct proc_bsdinfo, pbi_nice, PS_N),   PS_FIELD(struct proc_bsdinfo, pbi_start_tvsec, PS_TS), PS_FIELD(struct proc_bsdinfo, pbi_start_tvusec, PS_TS)};
static const xx_data_struct_field_desc ps_task_fields[] = {
    PS_FIELD(struct proc_taskinfo, pti_virtual_size, PS_SZ),  PS_FIELD(struct proc_taskinfo, pti_resident_size, PS_SZ),
    PS_FIELD(struct proc_taskinfo, pti_total_user, PS_N),     PS_FIELD(struct proc_taskinfo, pti_total_system, PS_N),
    PS_FIELD(struct proc_taskinfo, pti_threads_user, PS_N),   PS_FIELD(struct proc_taskinfo, pti_threads_system, PS_N),
    PS_FIELD(struct proc_taskinfo, pti_policy, PS_N),         PS_FIELD(struct proc_taskinfo, pti_faults, PS_CT),
    PS_FIELD(struct proc_taskinfo, pti_pageins, PS_CT),       PS_FIELD(struct proc_taskinfo, pti_cow_faults, PS_CT),
    PS_FIELD(struct proc_taskinfo, pti_messages_sent, PS_CT), PS_FIELD(struct proc_taskinfo, pti_messages_received, PS_CT),
    PS_FIELD(struct proc_taskinfo, pti_syscalls_mach, PS_CT), PS_FIELD(struct proc_taskinfo, pti_syscalls_unix, PS_CT),
    PS_FIELD(struct proc_taskinfo, pti_csw, PS_CT),           PS_FIELD(struct proc_taskinfo, pti_threadnum, PS_CT),
    PS_FIELD(struct proc_taskinfo, pti_numrunning, PS_CT),    PS_FIELD(struct proc_taskinfo, pti_priority, PS_N)};
static const xx_data_struct_field_desc ps_thread_fields[] = {
    PS_FIELD(struct proc_threadinfo, pth_user_time, PS_N),  PS_FIELD(struct proc_threadinfo, pth_system_time, PS_N),
    PS_FIELD(struct proc_threadinfo, pth_cpu_usage, PS_N),  PS_FIELD(struct proc_threadinfo, pth_policy, PS_N),
    PS_FIELD(struct proc_threadinfo, pth_run_state, PS_N),  PS_FIELD(struct proc_threadinfo, pth_flags, PS_FL),
    PS_FIELD(struct proc_threadinfo, pth_sleep_time, PS_N), PS_FIELD(struct proc_threadinfo, pth_curpri, PS_N),
    PS_FIELD(struct proc_threadinfo, pth_priority, PS_N),   PS_FIELD(struct proc_threadinfo, pth_maxpriority, PS_N),
    PS_FIELD(struct proc_threadinfo, pth_name, PS_STR)};
static const xx_data_struct_field_desc ps_identifier_fields[] = {PS_FIELD(struct thread_identifier_info, thread_id, PS_ID),
                                                                 /* A libproc handle is not promised to be a remotely dereferenceable pthread_t. */
                                                                 PS_FIELD(struct thread_identifier_info, thread_handle, PS_ID),
                                                                 PS_FIELD(struct thread_identifier_info, dispatch_qaddr, PS_PTR)};
static const xx_data_struct_field_desc ps_basic_thread_fields[] = {
    PS_FIELD(struct thread_basic_info, user_time.seconds, PS_N),   PS_FIELD(struct thread_basic_info, user_time.microseconds, PS_N),
    PS_FIELD(struct thread_basic_info, system_time.seconds, PS_N), PS_FIELD(struct thread_basic_info, system_time.microseconds, PS_N),
    PS_FIELD(struct thread_basic_info, cpu_usage, PS_N),           PS_FIELD(struct thread_basic_info, policy, PS_N),
    PS_FIELD(struct thread_basic_info, run_state, PS_N),           PS_FIELD(struct thread_basic_info, flags, PS_FL),
    PS_FIELD(struct thread_basic_info, suspend_count, PS_CT),      PS_FIELD(struct thread_basic_info, sleep_time, PS_N)};
static const xx_data_struct_field_desc ps_dyld_query_fields[] = {PS_FIELD(struct task_dyld_info, all_image_info_addr, PS_PTR),
                                                                 PS_FIELD(struct task_dyld_info, all_image_info_size, PS_SZ),
                                                                 PS_FIELD(struct task_dyld_info, all_image_info_format, PS_N)};

static uint64_t ps_mac_identity(pid_t pid, struct proc_bsdinfo *out)
{
    struct proc_bsdinfo value = {0};
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &value, sizeof(value)) != (int)sizeof(value) || value.pbi_pid != (uint32_t)pid || value.pbi_start_tvusec >= 1000000 ||
        value.pbi_start_tvsec > (UINT64_MAX - value.pbi_start_tvusec) / 1000000)
        return 0;
    if (out) *out = value;
    return value.pbi_start_tvsec * 1000000 + value.pbi_start_tvusec;
}

static void ps_mac_cpu(xx_process_structs_snapshot *s, uint32_t cpu)
{
    const char *name = NULL;
    switch (cpu) {
        case CPU_TYPE_X86: name = "x86"; break;
        case CPU_TYPE_X86_64: name = "x86_64"; break;
        case CPU_TYPE_ARM: name = "arm"; break;
        case CPU_TYPE_ARM64: name = "arm64"; break;
        default: break;
    }
    if (name) snprintf(s->architecture, sizeof(s->architecture), "%s", name);
}

static void ps_mac_thread_record(xx_ps_context *c, pid_t pid, uint64_t handle, uint64_t id)
{
    struct proc_threadinfo value = {0};
    char name[96];
    int got = proc_pidinfo(pid, PROC_PIDTHREADINFO, handle, &value, sizeof(value));
    snprintf(name, sizeof(name), id ? "proc_threadinfo [TID %" PRIu64 "]" : "proc_threadinfo [handle 0x%" PRIX64 "]", id ? id : handle);
    (void)ps_record(c, name, "proc_pidinfo(PROC_PIDTHREADINFO) query result", 0, 0, sizeof(value), id, ps_thread_fields, PS_COUNT(ps_thread_fields),
                    got == (int)sizeof(value) ? &value : NULL, got == (int)sizeof(value) ? sizeof(value) : 0, 0);
    if (got != (int)sizeof(value)) xx_ps_warn(c, "Some thread metadata is unavailable or changed during inspection.");
}

static void ps_mac_threads(xx_ps_context *c, pid_t pid, task_t task)
{
    thread_act_array_t threads = NULL;
    mach_msg_type_number_t count = 0;
    if (task != MACH_PORT_NULL && task_threads(task, &threads, &count) == KERN_SUCCESS) {
        size_t i, used = count < c->limits.max_threads ? count : c->limits.max_threads;
        for (i = 0; i < used; ++i) {
            if (!ps_room(c)) {
                ps_omitted(c, "Thread structures omitted by structure limit.");
                break;
            }
            struct thread_identifier_info identity = {0};
            struct thread_basic_info basic = {0};
            mach_msg_type_number_t n = THREAD_IDENTIFIER_INFO_COUNT;
            char name[96];
            int have = thread_info(threads[i], THREAD_IDENTIFIER_INFO, (thread_info_t)&identity, &n) == KERN_SUCCESS && n >= THREAD_IDENTIFIER_INFO_COUNT;
            snprintf(name, sizeof(name), "THREAD_IDENTIFIER_INFO [%zu]", i);
            (void)ps_record(c, name, "thread_info query result", 0, 0, sizeof(identity), have ? identity.thread_id : 0, ps_identifier_fields,
                            PS_COUNT(ps_identifier_fields), have ? &identity : NULL, have ? sizeof(identity) : 0, 0);
            if (!ps_room(c)) {
                ps_omitted(c, "Thread structures omitted by structure limit.");
                break;
            }
            n = THREAD_BASIC_INFO_COUNT;
            int have_basic = thread_info(threads[i], THREAD_BASIC_INFO, (thread_info_t)&basic, &n) == KERN_SUCCESS && n >= THREAD_BASIC_INFO_COUNT;
            snprintf(name, sizeof(name), "THREAD_BASIC_INFO [%zu]", i);
            (void)ps_record(c, name, "thread_info query result", 0, 0, sizeof(basic), have ? identity.thread_id : 0, ps_basic_thread_fields,
                            PS_COUNT(ps_basic_thread_fields), have_basic ? &basic : NULL, have_basic ? sizeof(basic) : 0, 0);
            if (have && identity.thread_handle) {
                if (ps_room(c)) ps_mac_thread_record(c, pid, identity.thread_handle, identity.thread_id);
                else ps_omitted(c, "Thread structures omitted by structure limit.");
            }
            if (!have || !have_basic) xx_ps_warn(c, "Some Mach thread queries failed; exited threads remain explicitly unavailable.");
        }
        /* task_threads returns one send right per thread, including unused rows. */
        for (i = 0; i < count; ++i) (void)mach_port_deallocate(mach_task_self(), threads[i]);
        if (threads) (void)vm_deallocate(mach_task_self(), (vm_address_t)threads, (vm_size_t)count * sizeof(*threads));
        if ((size_t)count > used) {
            c->snapshot->truncated = true;
            xx_ps_warn(c, "Thread limit reached.");
        }
        return;
    }
    /* libproc can remain available when task_for_pid is denied. Its thread
     * handles are names for libproc queries, not invented target addresses. */
    size_t capacity = 64, count_handles = 0, maximum = (size_t)INT_MAX / sizeof(uint64_t);
    uint64_t *handles = NULL;
    int got;
    /* PROC_PIDLISTTHREADS reports bytes copied, not a required buffer size.
     * A full buffer needs another probe. Derive allocations from discovery,
     * never from an unlimited SIZE_MAX option. The native API takes int bytes. */
    if (c->limits.max_threads < maximum) maximum = c->limits.max_threads + 1;
    if (capacity > maximum) capacity = maximum;
    for (;;) {
        uint64_t *grown = (uint64_t *)realloc(handles, capacity * sizeof(*handles));
        if (!grown) {
            free(handles);
            c->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
            return;
        }
        handles = grown;
        got = proc_pidinfo(pid, PROC_PIDLISTTHREADS, 0, handles, (int)(capacity * sizeof(*handles)));
        if (got <= 0 || (size_t)got > capacity * sizeof(*handles) || (size_t)got % sizeof(*handles)) {
            xx_ps_warn(c, "Thread list is unavailable.");
            free(handles);
            return;
        }
        count_handles = (size_t)got / sizeof(*handles);
        if (count_handles < capacity || count_handles > c->limits.max_threads) break;
        if (capacity == maximum) {
            xx_ps_warn(c, "Thread discovery reached the native API buffer range; list completeness is unconfirmed.");
            break;
        }
        capacity = capacity > maximum / 2 ? maximum : capacity * 2;
    }
    size_t used = count_handles < c->limits.max_threads ? count_handles : c->limits.max_threads;
    for (size_t i = 0; i < used; ++i) {
        if (!ps_room(c)) {
            ps_omitted(c, "Thread structures omitted by structure limit.");
            break;
        }
        ps_mac_thread_record(c, pid, handles[i], 0);
    }
    if (count_handles > used) ps_omitted(c, "Thread limit reached.");
    free(handles);
}

/* Decode only the published dyld prefix through version 15. Pointer offsets
 * use the TARGET data model, never sizeof(void*) in the inspecting process.
 * Source: Apple dyld include/mach-o/dyld_images.h. */
static void ps_mac_dyld(xx_ps_context *c, xx_io_device *io, const struct task_dyld_info *query)
{
    unsigned ps = c->snapshot->pointer_size;
    unsigned char bytes[256], first[8], verify[16];
    xx_data_struct_field_desc fields[28];
    size_t count = 0, offset, wanted, i, collected_images = 0;
    uint64_t address = query->all_image_info_addr, array, images;
    uint32_t version;
    char layout[96], name[96];
    xx_process_structure *s;
    if (!address || (ps != 4 && ps != 8)) {
        xx_ps_warn(c, "No supported dyld image-info layout was reported.");
        return;
    }
    if (query->all_image_info_size < 8 || !xx_ps_read(io, address, first, sizeof(first))) {
        static const xx_data_struct_field_desc head[] = {{L"version", L"uint32", 0, 4, PS_N}, {L"infoArrayCount", L"uint32", 4, 4, PS_CT}};
        (void)ps_record(c, "dyld_all_image_infos", "unreadable remote dyld prefix", address, 1, 8, 0, head, PS_COUNT(head), NULL, 0, 0);
        xx_ps_warn(c, "Remote dyld image-info bytes are unavailable.");
        return;
    }
    version = (uint32_t)ps_number(first, 4, 0);
    if (!version) {
        xx_ps_warn(c, "Unsupported dyld image-info version zero.");
        return;
    }
#define DYLD_FIELD(n, t, o, z, p)                                                                 \
    do {                                                                                          \
        fields[count++] = (xx_data_struct_field_desc){L##n, L##t, (int64_t)(o), (int64_t)(z), p}; \
    } while (0)
    DYLD_FIELD("version", "uint32", 0, 4, PS_N);
    DYLD_FIELD("infoArrayCount", "uint32", 4, 4, PS_CT);
    DYLD_FIELD("infoArray", "pointer", 8, ps, PS_PTR);
    DYLD_FIELD("notification", "pointer", 8 + ps, ps, PS_PTR);
    DYLD_FIELD("processDetachedFromSharedRegion", "bool", 8 + 2 * ps, 1, PS_FL);
    wanted = 9 + 2 * ps;
    if (version >= 2) {
        DYLD_FIELD("libSystemInitialized", "bool", 9 + 2 * ps, 1, PS_FL);
        offset = (10 + 2 * ps + ps - 1) & ~(size_t)(ps - 1);
        DYLD_FIELD("dyldImageLoadAddress", "pointer", offset, ps, PS_PTR);
        offset += ps;
        if (version >= 3) {
            DYLD_FIELD("jitInfo", "pointer", offset, ps, PS_PTR);
            offset += ps;
        }
        if (version >= 5) {
            DYLD_FIELD("dyldVersion", "pointer", offset, ps, PS_PTR);
            offset += ps;
            DYLD_FIELD("errorMessage", "pointer", offset, ps, PS_PTR);
            offset += ps;
            DYLD_FIELD("terminationFlags", "uintptr", offset, ps, PS_FL);
            offset += ps;
        }
        if (version >= 6) {
            DYLD_FIELD("coreSymbolicationShmPage", "pointer", offset, ps, PS_PTR);
            offset += ps;
        }
        if (version >= 7) {
            DYLD_FIELD("systemOrderFlag", "uintptr", offset, ps, PS_FL);
            offset += ps;
        }
        if (version >= 8) {
            DYLD_FIELD("uuidArrayCount", "uintptr", offset, ps, PS_CT);
            offset += ps;
            DYLD_FIELD("uuidArray", "pointer", offset, ps, PS_PTR);
            offset += ps;
        }
        if (version >= 9) {
            DYLD_FIELD("dyldAllImageInfosAddress", "pointer", offset, ps, PS_PTR);
            offset += ps;
        }
        if (version >= 10) {
            DYLD_FIELD("initialImageCount", "uintptr", offset, ps, PS_CT);
            offset += ps;
        }
        if (version >= 11) {
            DYLD_FIELD("errorKind", "uintptr", offset, ps, PS_N);
            offset += ps;
            DYLD_FIELD("errorClientOfDylibPath", "pointer", offset, ps, PS_PTR);
            offset += ps;
            DYLD_FIELD("errorTargetDylibPath", "pointer", offset, ps, PS_PTR);
            offset += ps;
            DYLD_FIELD("errorSymbol", "pointer", offset, ps, PS_PTR);
            offset += ps;
        }
        if (version >= 12) {
            DYLD_FIELD("sharedCacheSlide", "uintptr", offset, ps, PS_N);
            offset += ps;
        }
        if (version >= 13) {
            DYLD_FIELD("sharedCacheUUID", "uuid", offset, 16, PS_N);
            offset += 16;
        }
        if (version >= 15) {
            DYLD_FIELD("sharedCacheBaseAddress", "pointer", offset, ps, PS_PTR);
            offset += ps;
        }
        wanted = offset;
    }
#undef DYLD_FIELD
    snprintf(layout, sizeof(layout), "dyld_all_image_infos v%u, %u-bit published prefix", version, ps * 8);
    size_t readable = wanted < query->all_image_info_size ? wanted : (size_t)query->all_image_info_size;
    int have = readable <= sizeof(bytes) && xx_ps_read(io, address, bytes, readable);
    s = ps_record(c, "dyld_all_image_infos", layout, address, 1, wanted, 0, fields, count, have ? bytes : NULL, have ? readable : 0, 0);
    if (!s || !have || readable < 8 + ps) {
        xx_ps_warn(c, "Some dyld fields are unavailable.");
        return;
    }
    if (version >= 15) xx_ps_warn(c, "dyld image-info is decoded through sharedCacheBaseAddress; later private/runtime fields are omitted.");
    images = ps_number(bytes + 4, 4, 0);
    array = ps_number(bytes + 8, ps, 0);
    if (!array && images) {
        xx_ps_warn(c, "dyld is updating its image list; no unstable array was followed.");
        return;
    }
    size_t used = images < c->limits.max_modules ? (size_t)images : c->limits.max_modules;
    for (i = 0; i < used && ps_room(c); ++i) {
        unsigned char entry[24];
        xx_data_struct_field_desc row[] = {
            {L"imageLoadAddress", L"pointer", 0, ps, PS_PTR}, {L"imageFilePath", L"pointer", ps, ps, PS_PTR}, {L"imageFileModDate", L"uintptr", 2 * ps, ps, PS_TS}};
        uint64_t at;
        if ((uint64_t)i > (UINT64_MAX - array) / (3 * ps)) {
            xx_ps_warn(c, "dyld image array address overflow.");
            break;
        }
        at = array + (uint64_t)i * 3 * ps;
        have = xx_ps_read(io, at, entry, 3 * ps);
        snprintf(name, sizeof(name), "dyld_image_info [%zu]", i);
        s = ps_record(c, name, ps == 8 ? "dyld_image_info64" : "dyld_image_info32", at, 1, 3 * ps, 0, row, PS_COUNT(row), have ? entry : NULL, have ? 3 * ps : 0, 0);
        if (s) ++collected_images;
        if (!have) {
            xx_ps_warn(c, "A dyld image entry could not be read.");
            break;
        }
        if (s && s->field_count > 1) ps_remote_string(io, ps_number(entry + ps, ps, 0), s->fields[1].comment, sizeof(s->fields[1].comment));
        if (!i) {
            unsigned char macho[12];
            uint64_t base = ps_number(entry, ps, 0);
            if (xx_ps_read(io, base, macho, sizeof(macho))) {
                uint32_t magic = (uint32_t)ps_number(macho, 4, 0);
                if (magic == MH_MAGIC || magic == MH_MAGIC_64) ps_mac_cpu(c->snapshot, (uint32_t)ps_number(macho + 4, 4, 0));
            }
        }
    }
    if (collected_images < used && !ps_room(c)) ps_omitted(c, "Image structures omitted by structure limit.");
    if (images > used) ps_omitted(c, "Module limit reached.");
    if (!xx_ps_read(io, address, verify, 8 + ps) || memcmp(bytes, verify, 8 + ps))
        xx_ps_warn(c, "dyld image list changed while it was read; the live snapshot may mix generations.");
}

xx_process_structs_status xx_ps_collect(xx_ps_context *c, uint64_t requested, uint64_t expected)
{
    pid_t pid;
    struct proc_bsdinfo bsd = {0}, final = {0};
    struct proc_taskinfo pt = {0};
    struct task_dyld_info dyld = {0};
    xx_io_device *io = NULL;
    task_t task = MACH_PORT_NULL;
    uint64_t start;
    int own_task = 0;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    if (!requested) requested = (uint64_t)getpid();
    if (requested > INT_MAX) return XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
    pid = (pid_t)requested;
    start = ps_mac_identity(pid, &bsd);
    if (!start) return ps_error(errno);
    if (expected && start != expected) return XX_PROCESS_STRUCTS_CHANGED;
    c->snapshot->pid = requested;
    c->snapshot->start_time = start;
    c->snapshot->pointer_size = bsd.pbi_flags & PROC_FLAG_LP64 ? 8 : 4;
    snprintf(c->snapshot->architecture, sizeof(c->snapshot->architecture), "Mach-O %u-bit", c->snapshot->pointer_size * 8);
    snprintf(c->snapshot->name, sizeof(c->snapshot->name), "%.*s", bsd.pbi_name[0] ? (int)sizeof(bsd.pbi_name) : (int)sizeof(bsd.pbi_comm),
             bsd.pbi_name[0] ? bsd.pbi_name : bsd.pbi_comm);
    if (proc_pidpath(pid, c->snapshot->path, sizeof(c->snapshot->path)) <= 0) c->snapshot->path[0] = '\0';
    (void)ps_record(c, "proc_bsdinfo", "proc_pidinfo(PROC_PIDTBSDINFO) query result", 0, 0, sizeof(bsd), 0, ps_bsd_fields, PS_COUNT(ps_bsd_fields), &bsd, sizeof(bsd), 0);
    if (ps_room(c)) {
        int got = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &pt, sizeof(pt));
        (void)ps_record(c, "proc_taskinfo", "proc_pidinfo(PROC_PIDTASKINFO) query result", 0, 0, sizeof(pt), 0, ps_task_fields, PS_COUNT(ps_task_fields),
                        got == (int)sizeof(pt) ? &pt : NULL, got == (int)sizeof(pt) ? sizeof(pt) : 0, 0);
        if (got != (int)sizeof(pt)) xx_ps_warn(c, "Task metadata query is unavailable.");
    } else ps_omitted(c, "Task metadata omitted by structure limit.");
    if (pid == getpid()) task = mach_task_self();
    else if (task_for_pid(mach_task_self(), pid, &task) == KERN_SUCCESS) own_task = 1;
    else xx_ps_warn(c, "Mach task access denied; kernel metadata is shown without remote memory.");
    if (ps_room(c)) ps_mac_threads(c, pid, task);
    if (ps_room(c)) {
        int have_dyld = task != MACH_PORT_NULL && task_info(task, TASK_DYLD_INFO, (task_info_t)&dyld, &count) == KERN_SUCCESS && count >= TASK_DYLD_INFO_COUNT;
        (void)ps_record(c, "TASK_DYLD_INFO", "task_info query result", 0, 0, sizeof(dyld), 0, ps_dyld_query_fields, PS_COUNT(ps_dyld_query_fields),
                        have_dyld ? &dyld : NULL, have_dyld ? sizeof(dyld) : 0, 0);
        if (have_dyld) {
            unsigned ps = dyld.all_image_info_format == TASK_DYLD_ALL_IMAGE_INFO_64 ? 8 : dyld.all_image_info_format == TASK_DYLD_ALL_IMAGE_INFO_32 ? 4 : 0;
            if (!ps || ps != c->snapshot->pointer_size) xx_ps_warn(c, "Target dyld pointer format is unknown or changed; remote layout was not guessed.");
            else if (ps_room(c)) {
                io = xx_io_process_open_ro(requested);
                if (!io) xx_ps_warn(c, "Remote process memory is unavailable; dyld query metadata remains visible.");
                ps_mac_dyld(c, io, &dyld);
            } else if (dyld.all_image_info_addr) ps_omitted(c, "Remote dyld structures omitted by structure limit.");
        } else xx_ps_warn(c, "TASK_DYLD_INFO is unavailable.");
    } else ps_omitted(c, "TASK_DYLD_INFO omitted by structure limit.");
    if (io) xx_io_process_close(io);
    if (own_task) (void)mach_port_deallocate(mach_task_self(), task);
    if (ps_mac_identity(pid, &final) != start || ((final.pbi_flags ^ bsd.pbi_flags) & PROC_FLAG_LP64)) return XX_PROCESS_STRUCTS_CHANGED;
    xx_ps_warn(c, "Live read-only snapshot; pthread_t private layouts are not guessed and query results have no target address.");
    return c->status;
}

#elif defined(__linux__)

/* /proc stat is a textual KERNEL QUERY, not a C object in target memory.
 * Offsets and raw bytes refer to that query text. Never invent a PEB/TCB. */
static const wchar_t *const ps_stat_names[] = {L"pid",        L"comm",        L"state",       L"ppid",      L"pgrp",        L"session",     L"tty_nr",
                                               L"tpgid",      L"flags",       L"minflt",      L"cminflt",   L"majflt",      L"cmajflt",     L"utime",
                                               L"stime",      L"cutime",      L"cstime",      L"priority",  L"nice",        L"num_threads", L"itrealvalue",
                                               L"starttime",  L"vsize",       L"rss",         L"rsslim",    L"startcode",   L"endcode",     L"startstack",
                                               L"kstkesp",    L"kstkeip",     L"signal",      L"blocked",   L"sigignore",   L"sigcatch",    L"wchan",
                                               L"nswap",      L"cnswap",      L"exit_signal", L"processor", L"rt_priority", L"policy",      L"delayacct_blkio_ticks",
                                               L"guest_time", L"cguest_time", L"start_data",  L"end_data",  L"start_brk",   L"arg_start",   L"arg_end",
                                               L"env_start",  L"env_end",     L"exit_code"};
typedef struct ps_linux_stat {
    char text[16384];
    size_t length, offset[52], length_field[52], fields;
    uint64_t pid, start;
} ps_linux_stat;

typedef struct ps_linux_elf {
    unsigned ps;
    int be;
    uint16_t machine, type, phentsize, phnum;
    uint64_t phoff;
    unsigned char header[64];
} ps_linux_elf;
typedef struct ps_linux_aux {
    uint64_t phdr, phent, phnum, entry;
} ps_linux_aux;

static size_t ps_linux_read_fd(int fd, void *buffer, size_t size)
{
    size_t done = 0;
    while (done < size) {
        ssize_t got = read(fd, (unsigned char *)buffer + done, size - done);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        done += (size_t)got;
    }
    return done;
}

static int ps_linux_pread(int fd, uint64_t offset, void *buffer, size_t size)
{
    size_t done = 0;
    if (offset > INT64_MAX || (size && (uint64_t)(size - 1) > (uint64_t)INT64_MAX - offset)) return 0;
    while (done < size) {
        ssize_t got = pread(fd, (unsigned char *)buffer + done, size - done, (off_t)(offset + done));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return 0;
        done += (size_t)got;
    }
    return 1;
}

static int ps_linux_stat_read(int proc, ps_linux_stat *value)
{
    char *left, *right, *cursor, *end;
    int fd = openat(proc, "stat", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    memset(value, 0, sizeof(*value));
    value->length = ps_linux_read_fd(fd, value->text, sizeof(value->text) - 1);
    int saved = errno;
    close(fd);
    errno = saved;
    if (!value->length || value->length >= sizeof(value->text) - 1) return 0;
    value->text[value->length] = '\0';
    left = strchr(value->text, '(');
    right = strrchr(value->text, ')');
    if (!left || !right || left <= value->text || right <= left || right[1] != ' ') return 0;
    end = left;
    while (end > value->text && isspace((unsigned char)end[-1])) --end;
    value->offset[0] = 0;
    value->length_field[0] = (size_t)(end - value->text);
    value->offset[1] = (size_t)(left + 1 - value->text);
    value->length_field[1] = (size_t)(right - left - 1);
    cursor = right + 2;
    value->fields = 2;
    while (*cursor && value->fields < PS_COUNT(value->offset)) {
        while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
        if (!*cursor) break;
        end = cursor;
        while (*end && !isspace((unsigned char)*end)) ++end;
        value->offset[value->fields] = (size_t)(cursor - value->text);
        value->length_field[value->fields++] = (size_t)(end - cursor);
        cursor = end;
    }
    if (value->fields < 22) return 0;
    errno = 0;
    value->pid = strtoull(value->text, &end, 10);
    if (errno || end == value->text || value->pid > INT_MAX) return 0;
    cursor = value->text + value->offset[21];
    if (*cursor < '0' || *cursor > '9') return 0;
    errno = 0;
    value->start = strtoull(cursor, &end, 10);
    return !errno && end > cursor && (size_t)(end - cursor) == value->length_field[21] && value->start;
}

static void ps_linux_stat_record(xx_ps_context *c, const ps_linux_stat *value, uint64_t thread)
{
    char name[96];
    snprintf(name, sizeof(name), thread ? "/proc task stat [TID %" PRIu64 "]" : "/proc process stat [PID %" PRIu64 "]", thread ? thread : value->pid);
    xx_process_structure *s = xx_ps_add(c, name, "Linux /proc stat text query (not target memory)", 0, false, value->length, thread);
    if (!s) return;
    for (size_t i = 0; i < value->fields; ++i) {
        xx_data_struct_record_property_t prop = PS_STR;
        if (i == 0 || i == 3 || i == 4 || i == 5) prop |= PS_ID;
        if (i == 21) prop |= PS_TS;
        if (!xx_ps_bytes(c, s, ps_stat_names[i], L"kernel query text", value->offset[i], value->length_field[i], prop, value->text + value->offset[i], true)) break;
    }
}

static int ps_linux_executable(int proc, ps_linux_elf *elf, struct stat *identity)
{
    int fd = openat(proc, "exe", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    memset(elf, 0, sizeof(*elf));
    if (fstat(fd, identity) || !ps_linux_pread(fd, 0, elf->header, 16) || memcmp(elf->header, "\177ELF", 4) || (elf->header[4] != 1 && elf->header[4] != 2) ||
        (elf->header[5] != 1 && elf->header[5] != 2) || elf->header[6] != 1) {
        close(fd);
        errno = ENOTSUP;
        return -1;
    }
    elf->ps = elf->header[4] == 2 ? 8 : 4;
    elf->be = elf->header[5] == 2;
    if (!ps_linux_pread(fd, 0, elf->header, elf->ps == 8 ? 64 : 52)) {
        close(fd);
        errno = EIO;
        return -1;
    }
    elf->type = (uint16_t)ps_number(elf->header + 16, 2, elf->be);
    elf->machine = (uint16_t)ps_number(elf->header + 18, 2, elf->be);
    elf->phoff = ps_number(elf->header + (elf->ps == 8 ? 32 : 28), elf->ps, elf->be);
    elf->phentsize = (uint16_t)ps_number(elf->header + (elf->ps == 8 ? 54 : 42), 2, elf->be);
    elf->phnum = (uint16_t)ps_number(elf->header + (elf->ps == 8 ? 56 : 44), 2, elf->be);
    return fd;
}

static void ps_linux_arch(xx_process_structs_snapshot *s, const ps_linux_elf *elf)
{
    const char *name = NULL;
    switch (elf->machine) {
        case 3: name = "x86"; break;
        case 62: name = elf->ps == 4 ? "x86_64 x32 ABI" : "x86_64"; break;
        case 40: name = "arm"; break;
        case 183: name = "aarch64"; break;
        case 243: name = elf->ps == 8 ? "riscv64" : "riscv32"; break;
        case 20: name = "powerpc"; break;
        case 21: name = "powerpc64"; break;
        case 8: name = elf->ps == 8 ? "mips64" : "mips"; break;
        case 22: name = "s390"; break;
        default: break;
    }
    s->pointer_size = elf->ps;
    if (name) snprintf(s->architecture, sizeof(s->architecture), "%s", name);
    else snprintf(s->architecture, sizeof(s->architecture), "ELF%u machine %u", elf->ps * 8, elf->machine);
}

static const char *ps_aux_name(uint64_t type)
{
    switch (type) {
        case 0: return "AT_NULL";
        case 1: return "AT_IGNORE";
        case 2: return "AT_EXECFD";
        case 3: return "AT_PHDR";
        case 4: return "AT_PHENT";
        case 5: return "AT_PHNUM";
        case 6: return "AT_PAGESZ";
        case 7: return "AT_BASE";
        case 8: return "AT_FLAGS";
        case 9: return "AT_ENTRY";
        case 11: return "AT_UID";
        case 12: return "AT_EUID";
        case 13: return "AT_GID";
        case 14: return "AT_EGID";
        case 15: return "AT_PLATFORM";
        case 16: return "AT_HWCAP";
        case 17: return "AT_CLKTCK";
        case 23: return "AT_SECURE";
        case 24: return "AT_BASE_PLATFORM";
        case 25: return "AT_RANDOM";
        case 26: return "AT_HWCAP2";
        case 31: return "AT_EXECFN";
        case 33: return "AT_SYSINFO_EHDR";
        default: return "AT_UNKNOWN";
    }
}

static void ps_linux_auxv(xx_ps_context *c, int proc, const ps_linux_elf *elf, ps_linux_aux *aux)
{
    unsigned char bytes[16];
    size_t i = 0, entry_size = 2 * elf->ps;
    int fd = openat(proc, "auxv", O_RDONLY | O_CLOEXEC), terminated = 0, omitted = 0;
    if (fd < 0) {
        xx_ps_warn(c, "Kernel AUXV is unavailable (permission denied or target exited).");
        return;
    }
    /* The kernel-supplied file/AT_NULL bounds the scan. Read the next entry
     * before deciding that an explicit structure cap omitted anything. */
    while (c->status == XX_PROCESS_STRUCTS_OK) {
        size_t got = ps_linux_read_fd(fd, bytes, entry_size);
        uint64_t type = got >= elf->ps ? ps_number(bytes, elf->ps, elf->be) : 0;
        uint64_t value = got == entry_size ? ps_number(bytes + elf->ps, elf->ps, elf->be) : 0;
        char name[96];
        if (!got) break;
        if (!ps_room(c)) {
            ps_omitted(c, "AUXV entry omitted by structure limit.");
            omitted = 1;
            break;
        }
        xx_data_struct_record_property_t prop = type == 3 || type == 7 || type == 9 || type == 15 || type == 24 || type == 25 || type == 31 || type == 33 ? PS_PTR : PS_N;
        xx_data_struct_field_desc fields[] = {{L"a_type", L"uintptr", 0, elf->ps, PS_ID}, {L"a_un.a_val", L"uintptr", elf->ps, elf->ps, prop}};
        snprintf(name, sizeof(name), "AUXV [%zu] %s", i, got == entry_size ? ps_aux_name(type) : "incomplete");
        (void)ps_record(c, name, elf->ps == 8 ? "ELF64_auxv_t kernel query" : "ELF32_auxv_t kernel query", 0, 0, entry_size, 0, fields, PS_COUNT(fields), bytes, got,
                        elf->be);
        if (got != entry_size) {
            xx_ps_warn(c, "Kernel AUXV ends in an incomplete entry.");
            break;
        }
        if (!type) {
            terminated = 1;
            break;
        }
        if (type == 3) aux->phdr = value;
        else if (type == 4) aux->phent = value;
        else if (type == 5) aux->phnum = value;
        else if (type == 9) aux->entry = value;
        if (i == SIZE_MAX) {
            c->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
            break;
        }
        ++i;
    }
    if (!terminated && !omitted && c->status == XX_PROCESS_STRUCTS_OK) xx_ps_warn(c, "Kernel AUXV has no complete AT_NULL terminator.");
    close(fd);
}

static void ps_linux_threads(xx_ps_context *c, int proc)
{
    int fd = openat(proc, "task", O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    DIR *dir = fd < 0 ? NULL : fdopendir(fd);
    struct dirent *entry;
    size_t used = 0;
    if (!dir) {
        if (fd >= 0) close(fd);
        xx_ps_warn(c, "Thread list is unavailable.");
        return;
    }
    while (c->status == XX_PROCESS_STRUCTS_OK && (entry = readdir(dir)) != NULL) {
        char *end;
        uint64_t tid;
        int thread;
        ps_linux_stat stat;
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        errno = 0;
        tid = strtoull(entry->d_name, &end, 10);
        if (errno || *end || tid > INT_MAX || !tid) continue;
        thread = openat(fd, entry->d_name, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
        if (thread >= 0 && ps_linux_stat_read(thread, &stat) && stat.pid == tid) {
            if (used >= c->limits.max_threads || !ps_room(c)) {
                ps_omitted(c, used >= c->limits.max_threads ? "Thread limit reached." : "Thread stat omitted by structure limit.");
                close(thread);
                break;
            }
            ++used;
            ps_linux_stat_record(c, &stat, tid);
        } else xx_ps_warn(c, "Some thread stat queries disappeared or were denied.");
        if (thread >= 0) close(thread);
    }
    closedir(dir);
}

/* Target-layout ELF schemas, constants, and dynamic-union roles are from:
 * https://man7.org/linux/man-pages/man5/elf.5.html
 * https://man7.org/linux/man-pages/man3/getauxval.3.html (AT_PHDR/PHENT/PHNUM)
 * https://github.com/bminor/glibc/blob/master/elf/elf.h (GNU program/dynamic tag values)
 * Host sizeof/offsetof is never used for these remote ELF layouts. Unknown
 * OS/processor tags retain uninterpreted union bits; no semantics are guessed. */
static const xx_data_struct_field_desc ps_elf32_header_fields[] = {{L"e_ident", L"uint8[16]", 0, 16, XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE},
                                                                   {L"e_type", L"uint16", 16, 2, PS_ID},
                                                                   {L"e_machine", L"uint16", 18, 2, PS_ID},
                                                                   {L"e_version", L"uint32", 20, 4, PS_N},
                                                                   {L"e_entry", L"uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS},
                                                                   {L"e_phoff", L"uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
                                                                   {L"e_shoff", L"uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
                                                                   {L"e_flags", L"uint32", 36, 4, PS_FL},
                                                                   {L"e_ehsize", L"uint16", 40, 2, PS_SZ},
                                                                   {L"e_phentsize", L"uint16", 42, 2, PS_SZ},
                                                                   {L"e_phnum", L"uint16", 44, 2, PS_CT},
                                                                   {L"e_shentsize", L"uint16", 46, 2, PS_SZ},
                                                                   {L"e_shnum", L"uint16", 48, 2, PS_CT},
                                                                   {L"e_shstrndx", L"uint16", 50, 2, PS_ID}};
static const xx_data_struct_field_desc ps_elf64_header_fields[] = {{L"e_ident", L"uint8[16]", 0, 16, XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE},
                                                                   {L"e_type", L"uint16", 16, 2, PS_ID},
                                                                   {L"e_machine", L"uint16", 18, 2, PS_ID},
                                                                   {L"e_version", L"uint32", 20, 4, PS_N},
                                                                   {L"e_entry", L"uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS},
                                                                   {L"e_phoff", L"uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
                                                                   {L"e_shoff", L"uint64", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
                                                                   {L"e_flags", L"uint32", 48, 4, PS_FL},
                                                                   {L"e_ehsize", L"uint16", 52, 2, PS_SZ},
                                                                   {L"e_phentsize", L"uint16", 54, 2, PS_SZ},
                                                                   {L"e_phnum", L"uint16", 56, 2, PS_CT},
                                                                   {L"e_shentsize", L"uint16", 58, 2, PS_SZ},
                                                                   {L"e_shnum", L"uint16", 60, 2, PS_CT},
                                                                   {L"e_shstrndx", L"uint16", 62, 2, PS_ID}};
static const xx_data_struct_field_desc ps_elf32_program_fields[] = {{L"p_type", L"uint32", 0, 4, PS_ID},
                                                                    {L"p_offset", L"uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
                                                                    {L"p_vaddr", L"uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS},
                                                                    {L"p_paddr", L"uint32", 12, 4, PS_N},
                                                                    {L"p_filesz", L"uint32", 16, 4, PS_SZ},
                                                                    {L"p_memsz", L"uint32", 20, 4, PS_SZ},
                                                                    {L"p_flags", L"uint32", 24, 4, PS_FL},
                                                                    {L"p_align", L"uint32", 28, 4, PS_SZ}};
static const xx_data_struct_field_desc ps_elf64_program_fields[] = {{L"p_type", L"uint32", 0, 4, PS_ID},
                                                                    {L"p_flags", L"uint32", 4, 4, PS_FL},
                                                                    {L"p_offset", L"uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
                                                                    {L"p_vaddr", L"uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS},
                                                                    {L"p_paddr", L"uint64", 24, 8, PS_N},
                                                                    {L"p_filesz", L"uint64", 32, 8, PS_SZ},
                                                                    {L"p_memsz", L"uint64", 40, 8, PS_SZ},
                                                                    {L"p_align", L"uint64", 48, 8, PS_SZ}};

static const char *ps_linux_program_name(uint64_t type)
{
    switch (type) {
        case 0: return "PT_NULL";
        case 1: return "PT_LOAD";
        case 2: return "PT_DYNAMIC";
        case 3: return "PT_INTERP";
        case 4: return "PT_NOTE";
        case 5: return "PT_SHLIB";
        case 6: return "PT_PHDR";
        case 7: return "PT_TLS";
        case UINT64_C(0x6474e550): return "PT_GNU_EH_FRAME";
        case UINT64_C(0x6474e551): return "PT_GNU_STACK";
        case UINT64_C(0x6474e552): return "PT_GNU_RELRO";
        case UINT64_C(0x6474e553): return "PT_GNU_PROPERTY";
        default: return "PT_UNKNOWN";
    }
}

static const char *ps_linux_dynamic_name(uint64_t tag)
{
    switch (tag) {
        case 0: return "DT_NULL";
        case 1: return "DT_NEEDED";
        case 2: return "DT_PLTRELSZ";
        case 3: return "DT_PLTGOT";
        case 4: return "DT_HASH";
        case 5: return "DT_STRTAB";
        case 6: return "DT_SYMTAB";
        case 7: return "DT_RELA";
        case 8: return "DT_RELASZ";
        case 9: return "DT_RELAENT";
        case 10: return "DT_STRSZ";
        case 11: return "DT_SYMENT";
        case 12: return "DT_INIT";
        case 13: return "DT_FINI";
        case 14: return "DT_SONAME";
        case 15: return "DT_RPATH";
        case 16: return "DT_SYMBOLIC";
        case 17: return "DT_REL";
        case 18: return "DT_RELSZ";
        case 19: return "DT_RELENT";
        case 20: return "DT_PLTREL";
        case 21: return "DT_DEBUG";
        case 22: return "DT_TEXTREL";
        case 23: return "DT_JMPREL";
        case 24: return "DT_BIND_NOW";
        case 25: return "DT_INIT_ARRAY";
        case 26: return "DT_FINI_ARRAY";
        case 27: return "DT_INIT_ARRAYSZ";
        case 28: return "DT_FINI_ARRAYSZ";
        case 29: return "DT_RUNPATH";
        case 30: return "DT_FLAGS";
        case 32: return "DT_PREINIT_ARRAY";
        case 33: return "DT_PREINIT_ARRAYSZ";
        case 34: return "DT_SYMTAB_SHNDX";
        case 35: return "DT_RELRSZ";
        case 36: return "DT_RELR";
        case 37: return "DT_RELRENT";
        case UINT64_C(0x6ffffef5): return "DT_GNU_HASH";
        case UINT64_C(0x6ffffef6): return "DT_TLSDESC_PLT";
        case UINT64_C(0x6ffffef7): return "DT_TLSDESC_GOT";
        case UINT64_C(0x6ffffff0): return "DT_VERSYM";
        case UINT64_C(0x6ffffff9): return "DT_RELACOUNT";
        case UINT64_C(0x6ffffffa): return "DT_RELCOUNT";
        case UINT64_C(0x6ffffffb): return "DT_FLAGS_1";
        case UINT64_C(0x6ffffffc): return "DT_VERDEF";
        case UINT64_C(0x6ffffffd): return "DT_VERDEFNUM";
        case UINT64_C(0x6ffffffe): return "DT_VERNEED";
        case UINT64_C(0x6fffffff): return "DT_VERNEEDNUM";
        default: return "DT_UNKNOWN";
    }
}

static xx_data_struct_record_property_t ps_linux_dynamic_property(uint64_t tag)
{
    switch (tag) {
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 12:
        case 13:
        case 17:
        case 21:
        case 23:
        case 25:
        case 26:
        case 32:
        case 34:
        case 36:
        case UINT64_C(0x6ffffef5):
        case UINT64_C(0x6ffffef6):
        case UINT64_C(0x6ffffef7):
        case UINT64_C(0x6ffffff0):
        case UINT64_C(0x6ffffffc):
        case UINT64_C(0x6ffffffe): return PS_PTR;
        case 2:
        case 8:
        case 9:
        case 10:
        case 11:
        case 18:
        case 19:
        case 27:
        case 28:
        case 33:
        case 35:
        case 37: return PS_SZ;
        case 1:
        case 14:
        case 15:
        case 29: return XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET;
        case 30:
        case UINT64_C(0x6ffffffb): return PS_FL;
        case UINT64_C(0x6ffffff9):
        case UINT64_C(0x6ffffffa):
        case UINT64_C(0x6ffffffd):
        case UINT64_C(0x6fffffff): return PS_CT;
        case 20: return PS_ID;
        default: return PS_N;
    }
}

static int ps_linux_mapped_header(xx_ps_context *c, xx_io_device *io, const ps_linux_elf *elf, uint64_t address, uint64_t bias, uint64_t entry)
{
    unsigned char bytes[64];
    size_t size = elf->ps == 8 ? 64 : 52;
    if (!ps_room(c)) {
        ps_omitted(c, "Mapped ELF header omitted by structure limit.");
        return 0;
    }
    int have = xx_ps_read(io, address, bytes, size);
    if (have && (memcmp(bytes, "\177ELF", 4) || bytes[4] != elf->header[4] || bytes[5] != elf->header[5] || bytes[6] != 1)) {
        xx_ps_warn(c, "The expected mapped ELF header changed or has an unsupported data model; its layout was not guessed.");
        return 1;
    }
    if (have && memcmp(bytes, elf->header, size)) xx_ps_warn(c, "Mapped ELF header differs from the executable; displayed header values are the actual remote bytes.");
    xx_process_structure *s =
        ps_record(c, "Mapped ELF header (main image)", elf->ps == 8 ? "Elf64_Ehdr in PT_LOAD offset zero" : "Elf32_Ehdr in PT_LOAD offset zero", address, 1, size, 0,
                  elf->ps == 8 ? ps_elf64_header_fields : ps_elf32_header_fields, elf->ps == 8 ? PS_COUNT(ps_elf64_header_fields) : PS_COUNT(ps_elf32_header_fields),
                  have ? bytes : NULL, have ? size : 0, elf->be);
    if (!s) return 0;
    if (have && s->field_count > 4)
        snprintf(s->fields[4].comment, sizeof(s->fields[4].comment), "ELF virtual entry; main-image load bias 0x%" PRIX64 "; kernel AT_ENTRY 0x%" PRIX64 ".", bias,
                 entry);
    if (!have) xx_ps_warn(c, "Mapped ELF header bytes are unavailable.");
    return c->status == XX_PROCESS_STRUCTS_OK;
}

static int ps_linux_program_records(xx_ps_context *c, xx_io_device *io, int exe, const ps_linux_elf *elf, const ps_linux_aux *aux, uint64_t bias, int have_bias,
                                    int *changed)
{
    unsigned char bytes[56], file_bytes[56];
    size_t size = elf->ps == 8 ? 56 : 32;
    int unavailable = 0;
    for (uint64_t i = 0; i < aux->phnum; ++i) {
        char name[96];
        if (!ps_room(c)) {
            ps_omitted(c, "ELF program-header entry omitted by structure limit.");
            return 0;
        }
        if (i > (UINT64_MAX - aux->phdr) / size) {
            xx_ps_warn(c, "Remote ELF program-header array address overflow.");
            return 0;
        }
        uint64_t address = aux->phdr + i * size;
        int have = xx_ps_read(io, address, bytes, size);
        uint64_t type = have ? ps_number(bytes, 4, elf->be) : 0;
        if (have && (!ps_linux_pread(exe, elf->phoff + i * size, file_bytes, size) || memcmp(bytes, file_bytes, size))) *changed = 1;
        snprintf(name, sizeof(name), "Elf%u_Phdr [%" PRIu64 "] %s", elf->ps * 8, i, have ? ps_linux_program_name(type) : "unreadable");
        xx_process_structure *s =
            ps_record(c, name, "main-image program header from kernel AT_PHDR", address, 1, size, 0, elf->ps == 8 ? ps_elf64_program_fields : ps_elf32_program_fields,
                      elf->ps == 8 ? PS_COUNT(ps_elf64_program_fields) : PS_COUNT(ps_elf32_program_fields), have ? bytes : NULL, have ? size : 0, elf->be);
        if (!s) return 0;
        size_t virtual_field = elf->ps == 8 ? 3 : 2;
        if (have && have_bias && s->field_count > virtual_field) {
            uint64_t virtual_address = ps_number(bytes + (elf->ps == 8 ? 16 : 8), elf->ps, elf->be);
            if (virtual_address <= UINT64_MAX - bias)
                snprintf(s->fields[virtual_field].comment, sizeof(s->fields[virtual_field].comment),
                         "Declared ELF virtual address; runtime segment start 0x%" PRIX64 " (load bias 0x%" PRIX64 ").", virtual_address + bias, bias);
            else
                snprintf(s->fields[virtual_field].comment, sizeof(s->fields[virtual_field].comment),
                         "Declared virtual address plus load bias overflows; no runtime address inferred.");
        }
        if (!have) unavailable = 1;
    }
    if (unavailable) xx_ps_warn(c, "Some remote ELF program-header entries are unavailable; file bytes were not substituted.");
    return c->status == XX_PROCESS_STRUCTS_OK;
}

static void ps_linux_loader(xx_ps_context *c, xx_io_device *io, int exe, const ps_linux_elf *elf, const ps_linux_aux *aux)
{
    unsigned ps = elf->ps;
    size_t i, phsize = ps == 8 ? 56 : 32, ehsize = ps == 8 ? 64 : 52;
    uint64_t bias = 0, header_va = 0, dynamic_va = 0, dynamic_size = 0, debug = 0, map, previous = 0;
    unsigned char ph[56], dyn[16], head[40], check[40];
    struct stat file;
    int have_bias = 0, have_phdr = 0, have_header = 0, changed_headers = 0;
    xx_process_structure *s;
    if (!io) xx_ps_warn(c, "Remote ELF/loader bytes are unavailable; known remote fields remain explicitly unavailable.");
    if (fstat(exe, &file) || file.st_size < 0 || elf->phentsize != phsize || !aux->phdr || !aux->phnum || aux->phent != phsize ||
        (elf->phnum != UINT16_MAX && aux->phnum != elf->phnum) || elf->phoff > (uint64_t)file.st_size || aux->phnum > ((uint64_t)file.st_size - elf->phoff) / phsize) {
        xx_ps_warn(c, "ELF program-header metadata is unsupported or unavailable; remote ELF layouts were not guessed.");
        return;
    }
    uint64_t program_size = aux->phnum * phsize;
    uint64_t address_maximum = ps == 8 ? UINT64_MAX : UINT32_MAX;
    if (aux->phdr > address_maximum || program_size - 1 > address_maximum - aux->phdr) {
        xx_ps_warn(c, "Kernel ELF program-header range exceeds the target address width.");
        return;
    }
    /* Native AUXV proves where the main program-header array resides. File
     * headers provide load-bias/segment extent metadata only; every displayed
     * Ehdr/Phdr/Dyn raw value is read from target memory, never substituted. */
    for (uint64_t index = 0; index < aux->phnum; ++index) {
        uint64_t at = elf->phoff + index * phsize;
        if (!ps_linux_pread(exe, at, ph, phsize)) {
            xx_ps_warn(c, "ELF program headers could not be read.");
            return;
        }
        uint64_t type = ps_number(ph, 4, elf->be);
        uint64_t off = ps_number(ph + (ps == 8 ? 8 : 4), ps, elf->be);
        uint64_t va = ps_number(ph + (ps == 8 ? 16 : 8), ps, elf->be);
        uint64_t file_size = ps_number(ph + (ps == 8 ? 32 : 16), ps, elf->be);
        uint64_t mem_size = ps_number(ph + (ps == 8 ? 40 : 20), ps, elf->be);
        if (type == 6 && off == elf->phoff && file_size >= program_size && mem_size >= program_size && aux->phdr >= va) {
            uint64_t candidate = aux->phdr - va;
            if (have_phdr && bias != candidate) {
                xx_ps_warn(c, "Conflicting ELF PT_PHDR load biases; loader layout was not guessed.");
                return;
            }
            bias = candidate;
            have_bias = have_phdr = 1;
        } else if (!have_phdr && type == 1 && elf->phoff >= off && elf->phoff - off <= file_size && program_size <= file_size - (elf->phoff - off) &&
                   elf->phoff - off <= mem_size && program_size <= mem_size - (elf->phoff - off) && va <= UINT64_MAX - (elf->phoff - off) &&
                   aux->phdr >= va + (elf->phoff - off)) {
            uint64_t candidate = aux->phdr - (va + (elf->phoff - off));
            if (have_bias && bias != candidate) {
                xx_ps_warn(c, "Conflicting ELF PT_LOAD load biases; loader layout was not guessed.");
                return;
            }
            bias = candidate;
            have_bias = 1;
        }
        if (type == 1 && !off && file_size >= ehsize && mem_size >= ehsize && file_size <= (uint64_t)file.st_size && !have_header) {
            header_va = va;
            have_header = 1;
        }
        if (type == 2) {
            if (dynamic_size || off > (uint64_t)file.st_size || file_size > (uint64_t)file.st_size - off || file_size > mem_size) {
                xx_ps_warn(c, "ELF PT_DYNAMIC is duplicated or exceeds its file/memory bounds.");
                return;
            }
            dynamic_va = va;
            dynamic_size = file_size;
        }
    }
    if (have_bias && have_header && header_va <= UINT64_MAX - bias && header_va + bias <= address_maximum &&
        (uint64_t)(ehsize - 1) <= address_maximum - (header_va + bias)) {
        if (!ps_linux_mapped_header(c, io, elf, header_va + bias, bias, aux->entry)) return;
    } else xx_ps_warn(c, "A mapped main ELF-header address could not be proven; no header address was guessed.");
    if (!ps_linux_program_records(c, io, exe, elf, aux, bias, have_bias, &changed_headers)) return;
    if (changed_headers) {
        xx_ps_warn(c, "Remote ELF program headers differ from the executable; dynamic addresses were not inferred from mismatched metadata.");
        return;
    }
    if (!have_bias || !dynamic_size || dynamic_va > UINT64_MAX - bias || (ps == 4 && dynamic_va + bias > UINT32_MAX)) {
        xx_ps_warn(c, "No usable ELF PT_DYNAMIC was found (static executable or unsupported layout).");
        return;
    }
    dynamic_va += bias;
    if (dynamic_size - 1 > address_maximum - dynamic_va) {
        xx_ps_warn(c, "ELF PT_DYNAMIC range exceeds the target address width.");
        return;
    }
    uint64_t entries = dynamic_size / (2 * ps);
    int terminated = 0, debug_seen = 0, debug_conflict = 0;
    for (uint64_t dynamic_index = 0; dynamic_index < entries; ++dynamic_index) {
        char name[96];
        uint64_t at;
        if (!ps_room(c)) {
            ps_omitted(c, "ELF dynamic entry omitted by structure limit.");
            return;
        }
        if (dynamic_index > (UINT64_MAX - dynamic_va) / (2 * ps)) {
            xx_ps_warn(c, "ELF dynamic table address overflow.");
            return;
        }
        at = dynamic_va + dynamic_index * 2 * ps;
        int have_entry = xx_ps_read(io, at, dyn, 2 * ps);
        uint64_t tag = have_entry ? ps_number(dyn, ps, elf->be) : 0;
        uint64_t value = have_entry ? ps_number(dyn + ps, ps, elf->be) : 0;
        xx_data_struct_record_property_t prop = have_entry ? ps_linux_dynamic_property(tag) : PS_N;
        const char *tag_name = have_entry ? ps_linux_dynamic_name(tag) : "unreadable";
        int known = have_entry && strcmp(tag_name, "DT_UNKNOWN");
        xx_data_struct_field_desc fields[] = {{L"d_tag", ps == 8 ? L"int64" : L"int32", 0, ps, PS_ID},
                                              {!known                                          ? L"d_un"
                                               : prop & XX_DATA_STRUCT_RECORD_PROPERTY_POINTER ? L"d_un.d_ptr"
                                                                                               : L"d_un.d_val",
                                               ps == 8 ? L"uint64" : L"uint32", ps, ps, prop}};
        /* Keep the established public row selector for the first DT_DEBUG. */
        if (have_entry && tag == 21 && !debug_seen) snprintf(name, sizeof(name), "DT_DEBUG");
        else snprintf(name, sizeof(name), "Elf%u_Dyn [%" PRIu64 "] %s", ps * 8, dynamic_index, tag_name);
        s = ps_record(c, name, ps == 8 ? "Elf64_Dyn in main image" : "Elf32_Dyn in main image", at, 1, 2 * ps, 0, fields, PS_COUNT(fields), have_entry ? dyn : NULL,
                      have_entry ? 2 * ps : 0, elf->be);
        if (!s) return;
        if (!have_entry) {
            xx_ps_warn(c, "A live ELF dynamic entry is unreadable; later entries were not guessed.");
            return;
        }
        if (s->field_count) snprintf(s->fields[0].comment, sizeof(s->fields[0].comment), "PT_DYNAMIC entry index %" PRIu64 " (%s).", dynamic_index, tag_name);
        if (s->field_count > 1) {
            if (!known)
                snprintf(s->fields[1].comment, sizeof(s->fields[1].comment), "Unclassified dynamic union bits; OS/processor-specific semantics are not inferred.");
            else if (tag == 21) snprintf(s->fields[1].comment, sizeof(s->fields[1].comment), "Runtime debugger rendezvous address written by the dynamic linker.");
            else if (prop & XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
                snprintf(s->fields[1].comment, sizeof(s->fields[1].comment),
                         "Raw in-memory program address; ABI/loader may adjust it. No address adjustment applied (image bias 0x%" PRIX64 ").", bias);
            else if (prop & XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
                snprintf(s->fields[1].comment, sizeof(s->fields[1].comment), "Offset into the dynamic string table; not a target memory address.");
        }
        if (!tag) {
            terminated = 1;
            break;
        }
        if (tag == 21) {
            if (debug_seen && debug != value) debug_conflict = 1;
            else {
                debug = value;
                debug_seen = 1;
            }
        }
    }
    if (!terminated) {
        xx_ps_warn(c, "ELF PT_DYNAMIC has no DT_NULL within its file-backed bounds.");
        return;
    }
    if (dynamic_size % (2 * ps)) xx_ps_warn(c, "ELF PT_DYNAMIC has trailing bytes outside complete dynamic entries.");
    if (debug_conflict) {
        xx_ps_warn(c, "ELF dynamic entries contain conflicting DT_DEBUG values; no rendezvous was followed.");
        return;
    }
    if (!debug) {
        xx_ps_warn(c, "No initialized DT_DEBUG rendezvous is available.");
        return;
    }
    if (!ps_room(c)) {
        ps_omitted(c, "Loader rendezvous omitted by structure limit.");
        return;
    }
    xx_data_struct_field_desc debug_fields[] = {{L"r_version", L"int32", 0, 4, PS_N},
                                                {L"r_map", L"pointer", ps, ps, PS_PTR},
                                                {L"r_brk", L"pointer", 2 * ps, ps, PS_PTR},
                                                {L"r_state", L"int32", 3 * ps, 4, PS_N},
                                                {L"r_ldbase", L"uintptr", 4 * ps, ps, PS_PTR}};
    int have = xx_ps_read(io, debug, head, 5 * ps);
    s = ps_record(c, "r_debug", ps == 8 ? "SVR4 r_debug64 public prefix" : "SVR4 r_debug32 public prefix", debug, 1, 5 * ps, 0, debug_fields, PS_COUNT(debug_fields),
                  have ? head : NULL, have ? 5 * ps : 0, elf->be);
    if (!have || !s) {
        xx_ps_warn(c, "Loader rendezvous is unreadable.");
        return;
    }
    uint64_t version = ps_number(head, 4, elf->be), state = ps_number(head + 3 * ps, 4, elf->be);
    map = ps_number(head + ps, ps, elf->be);
    if (!version || version > 2) {
        xx_ps_warn(c, "Unknown r_debug version; link_map was not followed.");
        return;
    }
    if (version == 2) xx_ps_warn(c, "Only the default loader namespace is collected; extra r_debug namespaces are omitted.");
    if (state) {
        xx_ps_warn(c, "Dynamic loader is changing its module list; unstable link_map was not followed.");
        return;
    }
    uint64_t *seen = NULL;
    size_t seen_capacity = 0;
    for (i = 0; map; ++i) {
        unsigned char entry[40];
        char name[96];
        xx_data_struct_field_desc row[] = {{L"l_addr", L"uintptr", 0, ps, PS_N},
                                           {L"l_name", L"pointer", ps, ps, PS_PTR},
                                           {L"l_ld", L"pointer", 2 * ps, ps, PS_PTR},
                                           {L"l_next", L"pointer", 3 * ps, ps, PS_PTR},
                                           {L"l_prev", L"pointer", 4 * ps, ps, PS_PTR}};
        size_t j;
        for (j = 0; j < i; ++j)
            if (seen[j] == map) break;
        if (j < i) {
            xx_ps_warn(c, "Loader link_map contains a cycle or changed during inspection.");
            break;
        }
        if (i >= c->limits.max_modules || !ps_room(c)) {
            ps_omitted(c, "Loader module/structure limit reached.");
            break;
        }
        if (i == seen_capacity) {
            size_t maximum = SIZE_MAX / sizeof(*seen);
            if (c->limits.max_modules < maximum) maximum = c->limits.max_modules;
            if (seen_capacity == maximum) {
                c->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
                break;
            }
            size_t capacity = seen_capacity ? (seen_capacity > maximum / 2 ? maximum : seen_capacity * 2) : (maximum < 64 ? maximum : 64);
            uint64_t *grown = (uint64_t *)realloc(seen, capacity * sizeof(*seen));
            if (!grown) {
                c->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
                break;
            }
            seen = grown;
            seen_capacity = capacity;
        }
        seen[i] = map;
        have = xx_ps_read(io, map, entry, 5 * ps);
        snprintf(name, sizeof(name), "link_map [%zu]", i);
        s = ps_record(c, name, ps == 8 ? "SVR4 link_map64 public prefix" : "SVR4 link_map32 public prefix", map, 1, 5 * ps, 0, row, PS_COUNT(row), have ? entry : NULL,
                      have ? 5 * ps : 0, elf->be);
        if (!have) {
            xx_ps_warn(c, "A loader link_map entry is unreadable.");
            break;
        }
        if (s && s->field_count > 1) ps_remote_string(io, ps_number(entry + ps, ps, elf->be), s->fields[1].comment, sizeof(s->fields[1].comment));
        if (ps_number(entry + 4 * ps, ps, elf->be) != previous) {
            xx_ps_warn(c, "Loader link_map back-link changed or is inconsistent.");
            break;
        }
        previous = map;
        map = ps_number(entry + 3 * ps, ps, elf->be);
    }
    free(seen);
    if (!xx_ps_read(io, debug, check, 5 * ps) || ps_number(check, 4, elf->be) != version || ps_number(check + ps, ps, elf->be) != ps_number(head + ps, ps, elf->be) ||
        ps_number(check + 3 * ps, 4, elf->be))
        xx_ps_warn(c, "Loader rendezvous changed during inspection; records may mix live generations.");
}

xx_process_structs_status xx_ps_collect(xx_ps_context *c, uint64_t requested, uint64_t expected)
{
    int proc = -1, exe = -1;
    char path[64];
    ps_linux_stat stat, final;
    ps_linux_elf elf;
    ps_linux_aux aux = {0};
    struct stat executable = {0}, current = {0};
    xx_io_device *io = NULL;
    xx_process_structs_status result = XX_PROCESS_STRUCTS_OK;
    if (!requested) requested = (uint64_t)getpid();
    if (requested > INT_MAX) return XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
    snprintf(path, sizeof(path), "/proc/%" PRIu64, requested);
    proc = open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (proc < 0) return ps_error(errno);
    if (!ps_linux_stat_read(proc, &stat) || stat.pid != requested) {
        result = ps_error(errno);
        goto done;
    }
    if (expected && stat.start != expected) {
        result = XX_PROCESS_STRUCTS_CHANGED;
        goto done;
    }
    c->snapshot->pid = requested;
    c->snapshot->start_time = stat.start;
    snprintf(c->snapshot->name, sizeof(c->snapshot->name), "%.*s",
             (int)(stat.length_field[1] < sizeof(c->snapshot->name) - 1 ? stat.length_field[1] : sizeof(c->snapshot->name) - 1), stat.text + stat.offset[1]);
    ssize_t path_length = readlinkat(proc, "exe", c->snapshot->path, sizeof(c->snapshot->path) - 1);
    if (path_length > 0) c->snapshot->path[path_length] = '\0';
    ps_linux_stat_record(c, &stat, 0);
    exe = ps_linux_executable(proc, &elf, &executable);
    if (exe >= 0) {
        ps_linux_arch(c->snapshot, &elf);
        if (c->status == XX_PROCESS_STRUCTS_OK) ps_linux_auxv(c, proc, &elf, &aux);
    } else {
        snprintf(c->snapshot->architecture, sizeof(c->snapshot->architecture), "unknown");
        xx_ps_warn(c, "Target executable ELF class/endianness is unavailable; AUXV and loader layouts were not guessed.");
    }
    if (c->status == XX_PROCESS_STRUCTS_OK) ps_linux_threads(c, proc);
    if (exe >= 0 && c->status == XX_PROCESS_STRUCTS_OK) {
        io = xx_io_process_open_ro(requested);
        if (!ps_linux_stat_read(proc, &final) || final.start != stat.start) {
            result = XX_PROCESS_STRUCTS_CHANGED;
            goto done;
        }
        ps_linux_loader(c, io, exe, &elf, &aux);
    }
    /* Re-open by PID as well as checking the pinned proc directory. An old
     * proc fd alone must not hide process exit followed by numeric PID reuse. */
    int reopened = open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (reopened < 0) {
        result = XX_PROCESS_STRUCTS_CHANGED;
        goto done;
    }
    int same = ps_linux_stat_read(reopened, &final) && final.pid == requested && final.start == stat.start;
    if (same && exe >= 0) same = fstatat(reopened, "exe", &current, 0) == 0 && current.st_ino == executable.st_ino && current.st_dev == executable.st_dev;
    close(reopened);
    if (!same) {
        result = XX_PROCESS_STRUCTS_CHANGED;
        goto done;
    }
    xx_ps_warn(c, "Live read-only snapshot; kernel query records have no target address. Private pthread/TCB layouts are not guessed.");
done:
    if (io) xx_io_process_close(io);
    if (exe >= 0) close(exe);
    if (proc >= 0) close(proc);
    return c->status == XX_PROCESS_STRUCTS_OK ? result : c->status;
}

#else
xx_process_structs_status xx_ps_collect(xx_ps_context *c, uint64_t pid, uint64_t expected)
{
    (void)c;
    (void)pid;
    (void)expected;
    return XX_PROCESS_STRUCTS_UNSUPPORTED;
}
#endif
#endif
