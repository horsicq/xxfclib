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

/**
 * @file xx_dos16m.h
 * @brief DOS/16M and DOS/4G extender image readers.
 *
 * Both formats are a plain MS-DOS (MZ) loader stub whose DOS image ends at
 * `(e_cp - 1) * 512 + e_cblp`; immediately after that end sits a chain of
 * spliced Rational Systems `BW` (dos16m_exe_header) sub-images, optionally
 * interleaved with `MF` info blocks, and optionally terminated by a second
 * `MZ` payload. The two formats differ only in what that trailing `MZ`
 * payload turns out to be:
 *
 *   - no trailing MZ, or a trailing MZ whose subheader is `NE` -> DOS/16M
 *   - a trailing MZ whose subheader is `LE` or `LX`            -> DOS/4G
 *
 * Because the container walk is byte-for-byte identical, one module serves
 * both; `xx_dos16m_*` and `xx_dos4g_*` share the same state structure and
 * differ only in which resolved variant their `check_is_valid` accepts.
 */

#ifndef XXFCLIB_FORMAT_DOS16M_H
#define XXFCLIB_FORMAT_DOS16M_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations and type aliases */
typedef struct xx_dos16m xx_dos16m;
typedef struct xx_dos16m xx_dos16m_t;
typedef struct xx_dos16m XDos16m;

/* DOS/4G shares the structure; only the accepted variant differs. */
typedef struct xx_dos16m xx_dos4g;
typedef struct xx_dos16m xx_dos4g_t;
typedef struct xx_dos16m XDos4g;

/** Resolved extender variant of a `BW` chain. */
typedef enum xx_dos16m_variant_e {
    XX_DOS16M_VARIANT_NONE = 0,  /**< No `BW` header found; not an extender */
    XX_DOS16M_VARIANT_DOS16M = 1, /**< `BW` chain, bare or ending in `NE` */
    XX_DOS16M_VARIANT_DOS4G = 2   /**< `BW` chain ending in an `LE`/`LX` payload */
} xx_dos16m_variant_t;

/** Length in bytes of `dos16m_exe_header::EXP_path`. */
#define XX_DOS16M_EXP_PATH_SIZE 64U

/**
 * @brief DOS/16M - DOS/4G extender image.
 *
 * Inherits from Abstractformat by placing it as the first member. The header
 * fields below are those of the FIRST `BW` sub-image in the chain; the chain
 * as a whole is exposed through the memory map.
 */
struct xx_dos16m {
    Abstractformat format;         /**< Base format structure (first member) */

    /* --- Outer MS-DOS stub, the fields the chain start is derived from --- */
    uint16_t e_magic;              /**< Stub MZ signature (0x5A4D) */
    uint16_t e_cblp;               /**< Stub image length mod 512 */
    uint16_t e_cp;                 /**< Stub image length in 512-byte pages */

    /* --- First dos16m_exe_header ('BW') --- */
    uint16_t signature;            /**< `BW` signature (0x5742) */
    uint16_t last_page_bytes;      /**< Length of image mod 512 */
    uint16_t pages_in_file;        /**< Number of 512-byte pages */
    uint16_t reserved1;
    uint16_t reserved2;
    uint16_t min_alloc;            /**< Required memory, in KB */
    uint16_t max_alloc;            /**< Max KB (private allocation) */
    uint16_t stack_seg;            /**< Segment of stack */
    uint16_t stack_ptr;            /**< Initial SP value */
    uint16_t first_reloc_sel;      /**< Huge relocation list selector */
    uint16_t init_ip;              /**< Initial IP value */
    uint16_t code_seg;             /**< Segment of code */
    uint16_t runtime_gdt_size;     /**< Runtime GDT size in bytes */
    uint16_t MAKEPM_version;       /**< version * 100 */
    uint32_t next_header_pos;      /**< File position of next spliced .EXP */
    uint32_t cv_info_offset;       /**< Offset to start of debug info */
    uint16_t last_sel_used;        /**< Last selector value used */
    uint16_t pmem_alloc;           /**< Private extended memory KB if nonzero */
    uint16_t alloc_incr;           /**< Auto ExtReserve amount, in KB */
    uint16_t options;              /**< Runtime options */
    uint16_t trans_stack_sel;      /**< Selector of transparent stack */
    uint16_t exp_flags;            /**< ef_ constants */
    uint16_t program_size;         /**< Size of program in paragraphs */
    uint16_t gdtimage_size;        /**< Size of GDT in file, in bytes */
    uint16_t first_selector;       /**< gdt[first_sel] = gdtimage[0]; 0 => 0x80 */
    uint8_t  default_mem_strategy;
    uint16_t transfer_buffer_size; /**< Default in bytes; 0 => 8KB */
    char     EXP_path[XX_DOS16M_EXP_PATH_SIZE + 1]; /**< Original .EXP name */

    /* --- Derived container layout --- */
    int64_t  stub_size;            /**< (e_cp - 1) * 512 + e_cblp */
    int64_t  first_header_offset;  /**< Offset of the first `BW` header */
    int64_t  payload_offset;       /**< Offset of the trailing MZ, -1 if none */
    uint16_t payload_subsignature; /**< `NE`/`LE`/`LX` of the trailing MZ, 0 if none */
    uint32_t number_of_headers;    /**< Count of `BW` headers in the chain */
    bool     has_payload;          /**< True when a trailing MZ was reached */
    xx_dos16m_variant_t variant;   /**< Resolved DOS/16M vs DOS/4G */
};

/* ------------------------------------------------------------------ */
/* DOS/16M surface                                                     */
/* ------------------------------------------------------------------ */

/* --- Constructors & Lifecycle --- */
XXFC_API void xx_dos16m_init(xx_dos16m *dos16m, xx_io_device *dev, int64_t base_address);
XXFC_API xx_dos16m *xx_dos16m_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_dos16m_destroy(xx_dos16m *dos16m);
XXFC_API void xx_dos16m_free(xx_dos16m *dos16m);

/* --- Format Implementation Callbacks --- */
XXFC_API bool xx_dos16m_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dos16m_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_dos16m_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dos16m_get_memory_map(Abstractformat *self,
                                       xx_memory_map_mode_t mode,
                                       xx_memory_map *output,
                                       xx_pd_struct *pd);

/* --- Getters --- */
XXFC_API uint16_t xx_dos16m_get_e_magic(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_e_cblp(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_e_cp(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_signature(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_last_page_bytes(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_pages_in_file(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_reserved1(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_reserved2(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_min_alloc(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_max_alloc(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_stack_seg(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_stack_ptr(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_first_reloc_sel(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_init_ip(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_code_seg(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_runtime_gdt_size(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_MAKEPM_version(const xx_dos16m *dos16m);
XXFC_API uint32_t xx_dos16m_get_next_header_pos(const xx_dos16m *dos16m);
XXFC_API uint32_t xx_dos16m_get_cv_info_offset(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_last_sel_used(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_pmem_alloc(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_alloc_incr(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_options(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_trans_stack_sel(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_exp_flags(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_program_size(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_gdtimage_size(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_first_selector(const xx_dos16m *dos16m);
XXFC_API uint8_t xx_dos16m_get_default_mem_strategy(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_transfer_buffer_size(const xx_dos16m *dos16m);
XXFC_API const char *xx_dos16m_get_EXP_path(const xx_dos16m *dos16m);
XXFC_API int64_t xx_dos16m_get_stub_size(const xx_dos16m *dos16m);
XXFC_API int64_t xx_dos16m_get_first_header_offset(const xx_dos16m *dos16m);
XXFC_API int64_t xx_dos16m_get_payload_offset(const xx_dos16m *dos16m);
XXFC_API uint16_t xx_dos16m_get_payload_subsignature(const xx_dos16m *dos16m);
XXFC_API uint32_t xx_dos16m_get_number_of_headers(const xx_dos16m *dos16m);
XXFC_API bool xx_dos16m_has_payload(const xx_dos16m *dos16m);
XXFC_API xx_dos16m_variant_t xx_dos16m_get_variant(const xx_dos16m *dos16m);

/* ------------------------------------------------------------------ */
/* DOS/4G surface                                                      */
/* ------------------------------------------------------------------ */

/* --- Constructors & Lifecycle --- */
XXFC_API void xx_dos4g_init(xx_dos4g *dos4g, xx_io_device *dev, int64_t base_address);
XXFC_API xx_dos4g *xx_dos4g_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_dos4g_destroy(xx_dos4g *dos4g);
XXFC_API void xx_dos4g_free(xx_dos4g *dos4g);

/* --- Format Implementation Callbacks --- */
XXFC_API bool xx_dos4g_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dos4g_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_dos4g_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dos4g_get_memory_map(Abstractformat *self,
                                      xx_memory_map_mode_t mode,
                                      xx_memory_map *output,
                                      xx_pd_struct *pd);

/* --- Getters --- */
XXFC_API uint16_t xx_dos4g_get_e_magic(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_e_cblp(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_e_cp(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_signature(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_last_page_bytes(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_pages_in_file(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_reserved1(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_reserved2(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_min_alloc(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_max_alloc(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_stack_seg(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_stack_ptr(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_first_reloc_sel(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_init_ip(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_code_seg(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_runtime_gdt_size(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_MAKEPM_version(const xx_dos4g *dos4g);
XXFC_API uint32_t xx_dos4g_get_next_header_pos(const xx_dos4g *dos4g);
XXFC_API uint32_t xx_dos4g_get_cv_info_offset(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_last_sel_used(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_pmem_alloc(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_alloc_incr(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_options(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_trans_stack_sel(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_exp_flags(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_program_size(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_gdtimage_size(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_first_selector(const xx_dos4g *dos4g);
XXFC_API uint8_t xx_dos4g_get_default_mem_strategy(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_transfer_buffer_size(const xx_dos4g *dos4g);
XXFC_API const char *xx_dos4g_get_EXP_path(const xx_dos4g *dos4g);
XXFC_API int64_t xx_dos4g_get_stub_size(const xx_dos4g *dos4g);
XXFC_API int64_t xx_dos4g_get_first_header_offset(const xx_dos4g *dos4g);
XXFC_API int64_t xx_dos4g_get_payload_offset(const xx_dos4g *dos4g);
XXFC_API uint16_t xx_dos4g_get_payload_subsignature(const xx_dos4g *dos4g);
XXFC_API uint32_t xx_dos4g_get_number_of_headers(const xx_dos4g *dos4g);
XXFC_API bool xx_dos4g_has_payload(const xx_dos4g *dos4g);
XXFC_API xx_dos16m_variant_t xx_dos4g_get_variant(const xx_dos4g *dos4g);

/* Cast helpers */
static inline Abstractformat *xx_dos16m_to_format(xx_dos16m *dos16m) {
    return dos16m ? &dos16m->format : NULL;
}

static inline const Abstractformat *xx_dos16m_to_format_const(const xx_dos16m *dos16m) {
    return dos16m ? &dos16m->format : NULL;
}

static inline Abstractformat *xx_dos4g_to_format(xx_dos4g *dos4g) {
    return dos4g ? &dos4g->format : NULL;
}

static inline const Abstractformat *xx_dos4g_to_format_const(const xx_dos4g *dos4g) {
    return dos4g ? &dos4g->format : NULL;
}

/* User-facing aliases without xx_ prefix */
static inline void XDos16m_init(xx_dos16m *dos16m, xx_io_device *dev, int64_t base_address) {
    xx_dos16m_init(dos16m, dev, base_address);
}

static inline xx_dos16m *XDos16m_create(xx_io_device *dev, int64_t base_address) {
    return xx_dos16m_create(dev, base_address);
}

static inline void XDos16m_free(xx_dos16m *dos16m) {
    xx_dos16m_free(dos16m);
}

static inline bool XDos16m_check_is_valid(xx_dos16m *dos16m, xx_pd_struct *pd) {
    return dos16m ? xx_dos16m_check_is_valid(&dos16m->format, pd) : false;
}

static inline bool XDos16m_handle_base_info(xx_dos16m *dos16m, xx_pd_struct *pd) {
    return dos16m ? xx_dos16m_handle_base_info(&dos16m->format, pd) : false;
}

static inline void XDos4g_init(xx_dos4g *dos4g, xx_io_device *dev, int64_t base_address) {
    xx_dos4g_init(dos4g, dev, base_address);
}

static inline xx_dos4g *XDos4g_create(xx_io_device *dev, int64_t base_address) {
    return xx_dos4g_create(dev, base_address);
}

static inline void XDos4g_free(xx_dos4g *dos4g) {
    xx_dos4g_free(dos4g);
}

static inline bool XDos4g_check_is_valid(xx_dos4g *dos4g, xx_pd_struct *pd) {
    return dos4g ? xx_dos4g_check_is_valid(&dos4g->format, pd) : false;
}

static inline bool XDos4g_handle_base_info(xx_dos4g *dos4g, xx_pd_struct *pd) {
    return dos4g ? xx_dos4g_handle_base_info(&dos4g->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DOS16M_H */
