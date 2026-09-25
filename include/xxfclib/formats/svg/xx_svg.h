/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_svg.h @brief SVG image: validation and exact size. */

/* An SVG image is an XML document whose root element is <svg>.  There is no
 * length field anywhere: the image ends where its root element closes.  The
 * reader follows binwalk's src/signatures/svg.rs, src/structures/svg.rs and
 * src/extractors/svg.rs, whose carve length is the contract.
 *
 * binwalk's rule, applied from an "<svg " (with the space) match:
 *
 *  - Every later occurrence of "<svg " or "</svg>" in the data is a tag,
 *    including ones inside comments, CDATA or attribute values, because
 *    binwalk finds them by plain substring search.  A tag runs from its "<"
 *    to the first ">" after it and must be valid UTF-8 (overlong forms,
 *    surrogates and code points above U+10FFFF are not); a tag with no ">"
 *    or with invalid UTF-8 ends the walk with no result.
 *  - A tag containing xmlns="http://www.w3.org/2000/svg" (exactly so, with
 *    double quotes) is a "head" tag.  A second head tag ends the walk with
 *    no result.
 *  - "<svg " opens, "</svg>" closes.  When exactly one head tag has been
 *    seen and every opened tag is closed, the image ends right after that
 *    "</svg>".
 *
 * This reader decides at the base address, so it also has to decide what
 * may come before the root.  It accepts exactly an XML prolog: an optional
 * UTF-8 byte order mark, then any mix of XML white space (space, tab, CR,
 * LF), processing instructions (<?xml ...?> and friends), comments
 * (<!-- -->) and at most one document type declaration whose name is svg
 * (<!DOCTYPE svg ...>, quoted literals and an internal [subset] included),
 * all within the first XX_SVG_MAX_PROLOG_SIZE bytes, and then the bytes
 * "<svg ".  Anything else -- another root element, text, a root written
 * "<svg" + newline, which binwalk never matches -- is not an SVG here, so
 * generic XML and HTML are not claimed.  The reported size runs from the
 * base address (prolog included) through the matching "</svg>"; that is
 * the root's offset plus binwalk's carve length at the root.  Everything
 * after it, usually a final newline, is overlay.
 *
 * Deliberate, named deviations, both stricter than binwalk:
 *  - When the root element closes without a head tag having been seen, the
 *    image is rejected.  binwalk keeps scanning past the root's end there
 *    and would decrement its tag counter below zero on the next "</svg>".
 *  - The format may not exceed XX_SVG_MAX_SIZE bytes (binwalk has no cap);
 *    the scan never reads past that bound.
 *
 * NOT an archive: binwalk's extractor carves the image itself
 * ("image.svg") and declines even that at offset 0.
 */

#ifndef XXFCLIB_FORMAT_SVG_H
#define XXFCLIB_FORMAT_SVG_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** binwalk's magic and open tag, with its trailing space. */
#define XX_SVG_OPEN_TAG "<svg "
#define XX_SVG_OPEN_TAG_SIZE 5U
#define XX_SVG_CLOSE_TAG "</svg>"
#define XX_SVG_CLOSE_TAG_SIZE 6U
/** What makes a tag the head tag (binwalk SVG_HEAD_MAGIC). */
#define XX_SVG_HEAD_MAGIC "xmlns=\"http://www.w3.org/2000/svg\""
#define XX_SVG_HEAD_MAGIC_SIZE 34U
/** Smallest possible image: "<svg " + head + "</svg>", where the close
 *  tag's '>' also ends the root tag. */
#define XX_SVG_MIN_SIZE 45
/** The root must start within this many bytes of the base address. */
#define XX_SVG_MAX_PROLOG_SIZE (1024 * 1024)
/** Largest image accepted; binwalk has no cap, this only bounds the scan. */
#define XX_SVG_MAX_SIZE (256 * 1024 * 1024)

typedef struct xx_svg xx_svg;
typedef struct xx_svg xx_svg_t;
typedef struct xx_svg XSvg;

struct xx_svg {
    Abstractformat format;
    int64_t root_offset;       /**< Absolute offset of the root "<svg ". */
    int64_t head_offset;       /**< Absolute offset of the head tag. */
    int64_t end_offset;        /**< One past the matching "</svg>". */
    uint64_t number_of_svg_tags; /**< "<svg " open tags, root included. */
    uint32_t max_depth;        /**< Deepest svg-in-svg nesting, root = 1. */
    uint32_t number_of_prolog_comments;
    uint32_t number_of_prolog_pis;
    bool has_bom;              /**< UTF-8 byte order mark at the base. */
    bool has_xml_declaration;  /**< The prolog holds "<?xml" + space. */
    bool has_doctype;          /**< The prolog holds <!DOCTYPE svg ...>. */
};

XXFC_API void xx_svg_init(xx_svg *svg, xx_io_device *dev, int64_t base_address);
XXFC_API xx_svg *xx_svg_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_svg_destroy(xx_svg *svg);
XXFC_API void xx_svg_free(xx_svg *svg);

XXFC_API bool xx_svg_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_svg_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_svg_get_format_size(Abstractformat *self, xx_pd_struct *pd);

/** Size of the detector's magic window. */
#define XX_SVG_MAGIC_WINDOW 64U

/** Prefilter over the detector's first bytes: after an optional UTF-8 BOM
 *  and white space, the window must hold "<svg ", "<?", "<!--" or
 *  "<!DOCTYPE" -- the prolog's first token.  A full window that ends
 *  before the token is complete passes.  Necessary, not sufficient: every
 *  image the probe accepts passes, and the probe decides. */
XXFC_API bool xx_svg_check_magic(const uint8_t *magic, size_t magic_size);

XXFC_API int64_t xx_svg_get_root_offset(const xx_svg *svg);
XXFC_API int64_t xx_svg_get_head_offset(const xx_svg *svg);
XXFC_API int64_t xx_svg_get_end_offset(const xx_svg *svg);
XXFC_API uint64_t xx_svg_get_number_of_svg_tags(const xx_svg *svg);
XXFC_API uint32_t xx_svg_get_max_depth(const xx_svg *svg);
XXFC_API bool xx_svg_has_xml_declaration(const xx_svg *svg);
XXFC_API bool xx_svg_has_doctype(const xx_svg *svg);

static inline Abstractformat *xx_svg_to_format(xx_svg *svg) {
    return svg ? &svg->format : NULL;
}
static inline void XSvg_init(xx_svg *svg, xx_io_device *dev,
                             int64_t base_address) {
    xx_svg_init(svg, dev, base_address);
}
static inline xx_svg *XSvg_create(xx_io_device *dev, int64_t base_address) {
    return xx_svg_create(dev, base_address);
}
static inline void XSvg_free(xx_svg *svg) { xx_svg_free(svg); }
static inline bool XSvg_is_valid(xx_svg *svg, xx_pd_struct *pd) {
    return svg ? xx_format_is_valid(&svg->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SVG_H */
