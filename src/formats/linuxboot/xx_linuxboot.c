/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/linuxboot/xx_linuxboot.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_LINUXBOOT exists in the enum. */
#ifdef LINUXBOOT
#define XX_LINUXBOOT_FILE_TYPE XX_FILE_TYPE_LINUXBOOT
#else
#define XX_LINUXBOOT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/*
 * Provenance.  binwalk (src/signatures/linux.rs, linux_boot_image_*) matches
 * the sixteen bootsect.S bytes at offset 0 and then tests offset 514, a port
 * of the file(1)/binwalk-2 magic
 *
 *     0     string  \xb8\xc0\x07\x8e\xd8\xb8\x00\x90\x8e\xc0\xb9\x00\x01\x29\xf6\x29
 *     >514  string  !HdrS  {invalid}
 *
 * where "!HdrS" is libmagic's NEGATED string test: the match is invalid
 * unless "HdrS" is at 514.  The Rust port compares the five literal bytes
 * "!HdrS" instead, which no kernel contains (514 holds 'H'), so binwalk 3 as
 * shipped never reports this signature.  This reader implements the intended
 * test, "HdrS" at 514 (0x202, the boot protocol signature), and adds the
 * structural checks of Documentation/i386/boot.txt below.  binwalk reports
 * size 0 ("unknown, runs to the next signature or EOF"); this reader derives
 * the length from the header instead, as described at xx_linuxboot_parse.
 */

#define XX_LINUXBOOT_HEAD_READ 1024U /* boot sector + first setup sector */
#define XX_LINUXBOOT_SETUP_SECTS_OFFSET 0x1F1U
#define XX_LINUXBOOT_ROOT_FLAGS_OFFSET 0x1F2U
#define XX_LINUXBOOT_SYSSIZE_OFFSET 0x1F4U
#define XX_LINUXBOOT_SWAP_DEV_OFFSET 0x1F6U
#define XX_LINUXBOOT_RAM_SIZE_OFFSET 0x1F8U
#define XX_LINUXBOOT_VID_MODE_OFFSET 0x1FAU
#define XX_LINUXBOOT_ROOT_DEV_OFFSET 0x1FCU
#define XX_LINUXBOOT_BOOT_FLAG_OFFSET 0x1FEU
#define XX_LINUXBOOT_JUMP_OFFSET 0x200U
#define XX_LINUXBOOT_VERSION_OFFSET 0x206U
#define XX_LINUXBOOT_KERNEL_VERSION_OFFSET 0x20EU
#define XX_LINUXBOOT_LOADFLAGS_OFFSET 0x211U
#define XX_LINUXBOOT_CODE32_START_OFFSET 0x214U

#define XX_LINUXBOOT_DEFAULT_SETUP_SECTS 4U
/* The real-mode kernel (boot sector + setup) is loaded at 0x90000 and must
 * stay below the 0x9A000 heap/stack area of the old protocol: 64 sectors is
 * already above what any kernel of this era produced (build.c emits 4..~20). */
#define XX_LINUXBOOT_MAX_SETUP_SECTS 64U
#define XX_LINUXBOOT_LOADED_HIGH 0x01U
/* A zImage system is loaded at 0x10000 and must end below 0x90000. */
#define XX_LINUXBOOT_ZIMAGE_MAX_PARAS 0x8000U
/* Before protocol 2.04 syssize is 16 bits; bzImage systems past 1 MiB wrap.
 * build.c refused systems above 0x28000 paragraphs; three wraps is ample. */
#define XX_LINUXBOOT_WRAP_PARAS 0x10000U
#define XX_LINUXBOOT_MAX_WRAPS 3U
/* From 2.04 syssize is 32 bits; cap it at 1 GiB of system. */
#define XX_LINUXBOOT_MAX_PARAS32 0x4000000U
/* The piggy follows head.o and misc.o (the inflater), a few KiB of code. */
#define XX_LINUXBOOT_PIGGY_WINDOW 0x10000U
#define XX_LINUXBOOT_MIN_GZIP 18U
/* UTS_RELEASE, " (", user, "@", host, ") ", UTS_VERSION: well under 512. */
#define XX_LINUXBOOT_VERSION_SCAN 512U

typedef struct xx_linuxboot_info_s {
    uint16_t protocol_version;
    uint8_t setup_sects;
    uint8_t loadflags;
    uint16_t root_flags;
    uint16_t swap_dev;
    uint16_t ram_size;
    uint16_t vid_mode;
    uint16_t root_dev;
    uint16_t kernel_version_offset;
    uint32_t syssize;
    uint32_t code32_start;
    uint32_t system_paragraphs;
    int64_t setup_size;       /* bytes */
    int64_t system_size;      /* bytes inside the format */
    int64_t payload_offset;   /* relative to base, -1 if not located */
    int64_t payload_size;
    int64_t format_size;
    bool is_bzimage;
    bool size_exact;
    char kernel_version[XX_LINUXBOOT_KERNEL_VERSION_MAX];
} xx_linuxboot_info;

static void xx_linuxboot_vtable_destroy(Abstractformat *self);

static bool xx_linuxboot_read_at(xx_io_device *device, int64_t offset,
                                 void *data, size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* End of the setup header for each protocol minor; the jump at 0x200 must
 * land past it.  Later protocols have longer headers, 0x230 is a floor. */
static uint32_t xx_linuxboot_header_end(uint16_t version) {
    switch (version & 0xFFU) {
        case 0x00U: return 0x224U;
        case 0x01U: return 0x226U;
        case 0x02U: return 0x22CU;
        default: return 0x230U;
    }
}

/* The kernel_version string: NUL-terminated, human readable, inside setup.
 * boot.txt: "This value should be less than (0x200 * setup_sects)". */
static bool xx_linuxboot_read_version(xx_io_device *device, int64_t base,
                                      uint32_t string_offset,
                                      uint32_t setup_end,
                                      char *output, size_t output_size) {
    uint8_t scan[XX_LINUXBOOT_VERSION_SCAN];
    size_t limit;
    size_t index;
    if (!output || output_size == 0U) return false;
    output[0] = '\0';
    if (string_offset >= setup_end) return false;
    limit = setup_end - string_offset;
    if (limit > sizeof(scan)) limit = sizeof(scan);
    if (!xx_linuxboot_read_at(device, base + (int64_t)string_offset, scan,
                              limit)) {
        return false;
    }
    for (index = 0U; index < limit; ++index) {
        uint8_t c = scan[index];
        if (c == 0U) break;
        if (c != 0x09U && (c < 0x20U || c == 0x7FU)) return false;
    }
    if (index == 0U || index == limit) return false; /* empty or unterminated */
    {
        size_t copy = index < output_size - 1U ? index : output_size - 1U;
        size_t i;
        for (i = 0U; i < copy; ++i) {
            output[i] = (scan[i] >= 0x20U && scan[i] < 0x7FU) ? (char)scan[i]
                                                                : '?';
        }
        output[copy] = '\0';
    }
    return true;
}

/*
 * Size.  The image is 512 + setup_sects * 512 + L bytes, L being the system.
 * build.c before 2.6 writes L unpadded and stores syssize = ceil(L / 16), in
 * 16 bits below protocol 2.04 (boot.txt: it "cannot be trusted for the size
 * of a kernel if the LOAD_HIGH flag is set").  So:
 *
 *  1. Piggy.  The compressed kernel is linked last: bvmlinux = head.o misc.o
 *     piggy.o, and piggy's .data is { u32 input_len; gzip[input_len] }.  If a
 *     gzip member header preceded by its length is found in the first 64 KiB
 *     of the system and ceil((offset + length) / 16) equals syssize (or, for
 *     a pre-2.04 bzImage, syssize + k * 0x10000), the system ends exactly at
 *     the end of that gzip stream.  A consistent piggy running past EOF means
 *     the file is truncated.
 *  2. Otherwise the header alone: L = syssize * 16, clamped to EOF when the
 *     file ends inside the last paragraph (the unpadded tail).  A file ending
 *     earlier than that is truncated.  A pre-2.04 bzImage over 1 MiB
 *     without a locatable piggy is unwrapped only when EOF lands in its last
 *     paragraph; with trailing data it reports the stored (wrapped) size.
 */
static bool xx_linuxboot_parse(Abstractformat *self, xx_linuxboot_info *info,
                               xx_pd_struct *pd) {
    uint8_t head[XX_LINUXBOOT_HEAD_READ];
    uint8_t *window = NULL;
    int64_t total_size;
    int64_t available;
    uint32_t setup_sects;
    uint32_t setup_end;
    uint32_t header_end;
    uint32_t jump_target;
    uint32_t paragraphs;
    uint32_t wraps;
    uint32_t k_wrap;
    int64_t system_available;
    size_t window_size;
    size_t index;
    bool result = false;

    if (info) {
        xx_mem_zero(info, sizeof(*info));
        info->payload_offset = -1;
        info->format_size = -1;
    }
    if (!self || !self->device || !info || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) return false;
    available = total_size - self->base_address;
    /* Boot sector, at least one setup sector, at least one system byte. */
    if (available <= (int64_t)XX_LINUXBOOT_HEAD_READ ||
        !xx_linuxboot_read_at(self->device, self->base_address, head,
                              sizeof(head))) {
        return false;
    }
    if (xx_rt_memcmp(head, XX_LINUXBOOT_MAGIC, XX_LINUXBOOT_MAGIC_SIZE) != 0 ||
        head[XX_LINUXBOOT_BOOT_FLAG_OFFSET] != 0x55U ||
        head[XX_LINUXBOOT_BOOT_FLAG_OFFSET + 1U] != 0xAAU ||
        xx_rt_memcmp(head + XX_LINUXBOOT_HDRS_OFFSET, "HdrS", 4U) != 0) {
        return false;
    }

    info->protocol_version = xx_data_get_u16(head, sizeof(head),
                                             XX_LINUXBOOT_VERSION_OFFSET, false);
    if ((info->protocol_version >> 8U) != 2U) return false;
    header_end = xx_linuxboot_header_end(info->protocol_version);

    /* 0x200 is "jmp trampoline" over the header: a forward short jump.  A
     * near jump (E9 rel16) would overlap "HdrS" at 0x202, so only EB fits. */
    if (head[XX_LINUXBOOT_JUMP_OFFSET] != 0xEBU ||
        head[XX_LINUXBOOT_JUMP_OFFSET + 1U] >= 0x80U) {
        return false;
    }
    jump_target = XX_LINUXBOOT_JUMP_OFFSET + 2U +
                  (uint32_t)head[XX_LINUXBOOT_JUMP_OFFSET + 1U];
    if (jump_target < header_end) return false;

    info->setup_sects = head[XX_LINUXBOOT_SETUP_SECTS_OFFSET];
    setup_sects = info->setup_sects ? info->setup_sects
                                    : XX_LINUXBOOT_DEFAULT_SETUP_SECTS;
    if (setup_sects > XX_LINUXBOOT_MAX_SETUP_SECTS) return false;
    setup_end = XX_LINUXBOOT_SECTOR_SIZE * (1U + setup_sects);
    info->setup_size = (int64_t)setup_sects * XX_LINUXBOOT_SECTOR_SIZE;
    if (available <= (int64_t)setup_end) return false;

    info->root_flags = xx_data_get_u16(head, sizeof(head),
                                       XX_LINUXBOOT_ROOT_FLAGS_OFFSET, false);
    info->swap_dev = xx_data_get_u16(head, sizeof(head),
                                     XX_LINUXBOOT_SWAP_DEV_OFFSET, false);
    info->ram_size = xx_data_get_u16(head, sizeof(head),
                                     XX_LINUXBOOT_RAM_SIZE_OFFSET, false);
    info->vid_mode = xx_data_get_u16(head, sizeof(head),
                                     XX_LINUXBOOT_VID_MODE_OFFSET, false);
    info->root_dev = xx_data_get_u16(head, sizeof(head),
                                     XX_LINUXBOOT_ROOT_DEV_OFFSET, false);
    info->loadflags = head[XX_LINUXBOOT_LOADFLAGS_OFFSET];
    info->is_bzimage = (info->loadflags & XX_LINUXBOOT_LOADED_HIGH) != 0U;
    info->code32_start = xx_data_get_u32(head, sizeof(head),
                                         XX_LINUXBOOT_CODE32_START_OFFSET,
                                         false);
    if (info->protocol_version < 0x0204U) {
        info->syssize = xx_data_get_u16(head, sizeof(head),
                                        XX_LINUXBOOT_SYSSIZE_OFFSET, false);
        wraps = info->is_bzimage ? XX_LINUXBOOT_MAX_WRAPS : 0U;
    } else {
        info->syssize = xx_data_get_u32(head, sizeof(head),
                                        XX_LINUXBOOT_SYSSIZE_OFFSET, false);
        if (info->syssize > XX_LINUXBOOT_MAX_PARAS32) return false;
        wraps = 0U;
    }
    if (!info->is_bzimage && info->syssize > XX_LINUXBOOT_ZIMAGE_MAX_PARAS) {
        return false;
    }

    info->kernel_version_offset = xx_data_get_u16(
        head, sizeof(head), XX_LINUXBOOT_KERNEL_VERSION_OFFSET, false);
    if (info->kernel_version_offset != 0U &&
        !xx_linuxboot_read_version(
            self->device, self->base_address,
            XX_LINUXBOOT_JUMP_OFFSET + (uint32_t)info->kernel_version_offset,
            setup_end, info->kernel_version, sizeof(info->kernel_version))) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    /* Step 1: the piggy. */
    system_available = available - (int64_t)setup_end;
    window_size = system_available > (int64_t)XX_LINUXBOOT_PIGGY_WINDOW
                      ? (size_t)XX_LINUXBOOT_PIGGY_WINDOW
                      : (size_t)system_available;
    if (window_size >= 8U) {
        window = (uint8_t *)xx_mem_alloc(window_size);
        if (!window) return false;
        if (!xx_linuxboot_read_at(self->device,
                                  self->base_address + (int64_t)setup_end,
                                  window, window_size)) {
            goto done;
        }
        for (index = 4U; index + 4U <= window_size; ++index) {
            uint32_t length;
            uint64_t system_bytes;
            uint64_t needed;
            uint32_t k;
            if (window[index] != 0x1FU || window[index + 1U] != 0x8BU ||
                window[index + 2U] != 0x08U ||
                (window[index + 3U] & 0xE0U) != 0U) {
                continue;
            }
            length = xx_data_get_u32(window, window_size, index - 4U, false);
            if (length < XX_LINUXBOOT_MIN_GZIP) continue;
            system_bytes = (uint64_t)index + (uint64_t)length;
            needed = (system_bytes + 15U) / 16U;
            for (k = 0U; k <= wraps; ++k) {
                uint64_t candidate = (uint64_t)info->syssize +
                                     (uint64_t)k * XX_LINUXBOOT_WRAP_PARAS;
                if (candidate != needed) continue;
                if (system_bytes > (uint64_t)system_available) {
                    goto done; /* consistent piggy past EOF: truncated */
                }
                if (!info->is_bzimage &&
                    candidate > XX_LINUXBOOT_ZIMAGE_MAX_PARAS) {
                    goto done;
                }
                info->system_paragraphs = (uint32_t)candidate;
                info->system_size = (int64_t)system_bytes;
                info->payload_offset = (int64_t)setup_end + (int64_t)index;
                info->payload_size = (int64_t)length;
                info->size_exact = true;
                break;
            }
            if (info->payload_offset >= 0) break;
        }
    }

    /* Step 2: the header alone.  A wrapped pre-2.04 bzImage is recognised
     * only when EOF falls inside its last paragraph; otherwise the stored
     * value is taken as it stands. */
    if (info->payload_offset < 0) {
        int64_t declared;
        paragraphs = info->syssize;
        if (paragraphs == 0U) goto done;
        for (k_wrap = 1U; k_wrap <= wraps; ++k_wrap) {
            int64_t wrapped = ((int64_t)paragraphs +
                               (int64_t)k_wrap * XX_LINUXBOOT_WRAP_PARAS) * 16;
            if (system_available <= wrapped &&
                system_available + 15 >= wrapped) {
                paragraphs += k_wrap * XX_LINUXBOOT_WRAP_PARAS;
                break;
            }
        }
        declared = (int64_t)paragraphs * 16;
        if (system_available + 15 < declared) goto done; /* truncated */
        info->system_paragraphs = paragraphs;
        if (system_available <= declared) {
            info->system_size = system_available;
            info->size_exact = true;
        } else {
            info->system_size = declared;
            info->size_exact = false;
        }
    }
    info->format_size = (int64_t)setup_end + info->system_size;
    result = !(pd && xx_pd_is_stopped(pd));
done:
    if (window) xx_mem_free(window);
    if (!result) {
        info->payload_offset = -1;
        info->format_size = -1;
    }
    return result;
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_linuxboot_init(xx_linuxboot *image, xx_io_device *dev,
                       int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, dev, base_address);
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_LINUXBOOT_FILE_TYPE;
    image->format.format_type = XX_TYPE_BOOT;
    image->format.os = XX_OS_LINUX;
    image->format.arch = XX_ARCH_X86;
    image->format.is_executable = true;
    image->format.is_archive = false;
    xx_format_set_mime_type(&image->format, "application/x-linux-kernel");
    xx_format_set_extension(&image->format, "bin");
    image->format.check_is_valid = xx_linuxboot_check_is_valid;
    image->format.handle_base_info = xx_linuxboot_handle_base_info;
    image->format.get_format_size = xx_linuxboot_get_format_size;
    image->format.destroy = xx_linuxboot_vtable_destroy;
    image->setup_offset = -1;
    image->system_offset = -1;
    image->payload_offset = -1;
}

xx_linuxboot *xx_linuxboot_create(xx_io_device *dev, int64_t base_address) {
    xx_linuxboot *image = (xx_linuxboot *)xx_mem_alloc(sizeof(*image));
    if (image) xx_linuxboot_init(image, dev, base_address);
    return image;
}

void xx_linuxboot_destroy(xx_linuxboot *image) {
    if (!image) return;
    /* Nothing owned beyond the base parameters. */
    xx_format_cleanup_extra_parameters(&image->format);
}

static void xx_linuxboot_vtable_destroy(Abstractformat *self) {
    xx_linuxboot_destroy((xx_linuxboot *)self);
}

void xx_linuxboot_free(xx_linuxboot *image) {
    if (!image) return;
    xx_linuxboot_destroy(image);
    xx_mem_free(image);
}

bool xx_linuxboot_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_linuxboot_info info;
    return xx_linuxboot_parse(self, &info, pd);
}

static void xx_linuxboot_set_strings(Abstractformat *self,
                                     const xx_linuxboot_info *info) {
    char text[65]; /* a release (UTS_RELEASE) is at most 64 characters */
    size_t i = 0U;
    unsigned minor = (unsigned)(info->protocol_version & 0xFFU);
    /* Boot protocol as boot.txt writes it: "2.03". */
    text[i++] = (char)('0' + ((info->protocol_version >> 8U) & 0x0FU));
    text[i++] = '.';
    if (minor >= 100U) text[i++] = (char)('0' + minor / 100U);
    text[i++] = (char)('0' + (minor / 10U) % 10U);
    text[i++] = (char)('0' + minor % 10U);
    text[i] = '\0';
    xx_format_set_version(self, text);
    /* The kernel release is the first word of the version string. */
    for (i = 0U; i < sizeof(text) - 1U && info->kernel_version[i] &&
                 info->kernel_version[i] != ' ';
         ++i) {
        text[i] = info->kernel_version[i];
    }
    text[i] = '\0';
    xx_format_set_os_version(self, text);
}

bool xx_linuxboot_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_linuxboot *image = (xx_linuxboot *)self;
    xx_linuxboot_info info;
    int64_t total_size;
    int64_t end;
    if (!self) return false;
    if (!xx_linuxboot_parse(self, &info, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    image->protocol_version = info.protocol_version;
    image->setup_sects = info.setup_sects;
    image->loadflags = info.loadflags;
    image->root_flags = info.root_flags;
    image->swap_dev = info.swap_dev;
    image->ram_size = info.ram_size;
    image->vid_mode = info.vid_mode;
    image->root_dev = info.root_dev;
    image->kernel_version_offset = info.kernel_version_offset;
    image->syssize = info.syssize;
    image->code32_start = info.code32_start;
    image->system_paragraphs = info.system_paragraphs;
    image->setup_offset = self->base_address + XX_LINUXBOOT_SECTOR_SIZE;
    image->setup_size = info.setup_size;
    image->system_offset = image->setup_offset + info.setup_size;
    image->system_size = info.system_size;
    image->payload_offset = info.payload_offset >= 0
                                ? self->base_address + info.payload_offset
                                : -1;
    image->payload_size = info.payload_size;
    image->is_bzimage = info.is_bzimage;
    image->size_exact = info.size_exact;
    xx_rt_memcpy(image->kernel_version, info.kernel_version,
                 sizeof(image->kernel_version));
    xx_linuxboot_set_strings(self, &info);

    self->format_size = info.format_size;
    end = self->base_address + info.format_size;
    total_size = xx_io_total_size(self->device);
    if (total_size > end) {
        self->overlay_offset = end;
        self->overlay_size = total_size - end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_linuxboot_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

/* --- Getters --- */
uint16_t xx_linuxboot_get_protocol_version(const xx_linuxboot *image) {
    return image ? image->protocol_version : 0U;
}

const char *xx_linuxboot_get_kernel_version(const xx_linuxboot *image) {
    return image ? image->kernel_version : "";
}

int64_t xx_linuxboot_get_setup_size(const xx_linuxboot *image) {
    return image ? image->setup_size : 0;
}

int64_t xx_linuxboot_get_system_offset(const xx_linuxboot *image) {
    return image ? image->system_offset : -1;
}

int64_t xx_linuxboot_get_system_size(const xx_linuxboot *image) {
    return image ? image->system_size : 0;
}

int64_t xx_linuxboot_get_payload_offset(const xx_linuxboot *image) {
    return image ? image->payload_offset : -1;
}

bool xx_linuxboot_is_bzimage(const xx_linuxboot *image) {
    return image ? image->is_bzimage : false;
}
