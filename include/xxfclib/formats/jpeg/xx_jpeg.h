/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_jpeg.h @brief JPEG image (JFIF / Exif / bare DQT start),
 *  validated by walking its marker segments to the End Of Image marker. */

/* A port of binwalk's "JPEG image" signature: src/signatures/jpeg.rs (the
 * three magic variants) and src/extractors/jpeg.rs (get_jpeg_data_size, the
 * marker walk that finds the EOI and therefore the carve length).
 *
 * A JPEG stream is a sequence of markers, each 0xFF followed by an id byte.
 * Most markers carry a big-endian 16-bit length that counts itself and the
 * payload; SOI (D8), EOI (D9), RST0-7 (D0-D7) and TEM (01) do not.  A Start
 * Of Scan (DA) is followed by entropy-coded data that is not covered by its
 * length, in which a literal 0xFF is stuffed as FF 00 and restart markers
 * FF D0..FF D7 may appear; the scan ends at the first 0xFF followed by any
 * other byte.  The image ends right after FF D9.
 *
 * Only the three leads binwalk matches are accepted:
 *
 *   FF D8 FF E0 00 10 'J' 'F' 'I' 'F' 00   SOI + 16-byte JFIF APP0
 *   FF D8 FF E1                            SOI + APP1 (Exif)
 *   FF D8 FF DB                            SOI + DQT
 *
 * The component archive API publishes marker payloads and entropy-coded
 * scans separately. It does not decode the JPEG image.
 */

#ifndef XXFCLIB_FORMAT_JPEG_H
#define XXFCLIB_FORMAT_JPEG_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest of the three lead patterns (the JFIF one). */
#define XX_JPEG_MAX_MAGIC_SIZE 11U
/** Shortest stream that can validate: SOI, one segment whose length field
 *  covers only itself, EOI -- FF D8 FF E1 00 02 FF D9 (or FF DB).  The JFIF
 *  lead alone is already 11 bytes, so that variant starts at 22. */
#define XX_JPEG_MIN_SIZE 8

/** Which of binwalk's three magic variants the stream starts with. */
typedef enum xx_jpeg_variant_e {
    XX_JPEG_VARIANT_UNKNOWN = 0,
    XX_JPEG_VARIANT_JFIF = 1, /**< FF D8 FF E0 00 10 "JFIF" 00 */
    XX_JPEG_VARIANT_EXIF = 2, /**< FF D8 FF E1 */
    XX_JPEG_VARIANT_DQT = 3   /**< FF D8 FF DB */
} xx_jpeg_variant;

typedef struct xx_jpeg xx_jpeg;
typedef struct xx_jpeg xx_jpeg_t;
typedef struct xx_jpeg XJpegImage;

struct xx_jpeg {
    Abstractformat format;   /**< Base format structure (first member) */
    xx_jpeg_variant variant; /**< Lead pattern the stream matched */
    int64_t image_end;       /**< Absolute offset just past FF D9 */
    uint32_t marker_count;   /**< Markers walked, SOI and EOI included */
    uint32_t scan_count;     /**< Start Of Scan segments (progressive > 1) */
    uint8_t frame_marker;    /**< First SOFn id (C0 baseline, C2 progressive,
                                  ...), 0 when the stream has none */
    uint8_t precision;       /**< Sample precision from that SOF, bits */
    uint16_t height;         /**< Lines from that SOF (0 = defined by DNL) */
    uint16_t width;          /**< Samples per line from that SOF */
    uint8_t components;      /**< Component count from that SOF */
    void *analysis;          /**< Private inspection metadata; owned by reader */
};

/* Tolerant metadata inspection is separate from strict archive validation.
 * The device is borrowed; all const-char results remain owned by the reader.
 * Comments retain source bytes, stop at NUL, and omit CR/LF (100-byte limit).
 * DQT hashes cover concatenated quantization payloads, including the empty
 * digest when a marker walk does not reach EOI. */
XXFC_API bool xx_jpeg_analyze(xx_jpeg *jpeg, xx_pd_struct *pd);
XXFC_API const char *xx_jpeg_get_version(const xx_jpeg *jpeg);
XXFC_API const char *xx_jpeg_get_comment(const xx_jpeg *jpeg);
XXFC_API const char *xx_jpeg_get_dqt_md5(const xx_jpeg *jpeg);
XXFC_API const char *xx_jpeg_get_exif_camera_name(const xx_jpeg *jpeg);
XXFC_API int64_t xx_jpeg_get_exif_size(const xx_jpeg *jpeg);
XXFC_API bool xx_jpeg_is_chunk_present(const xx_jpeg *jpeg, uint8_t marker);

/* --- Constructors & Lifecycle --- */
XXFC_API void xx_jpeg_init(xx_jpeg *jpeg, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_jpeg *xx_jpeg_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_jpeg_destroy(xx_jpeg *jpeg);
XXFC_API void xx_jpeg_free(xx_jpeg *jpeg);

/* --- Format Implementation Callbacks --- */
XXFC_API bool xx_jpeg_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_jpeg_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_jpeg_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);

/* --- Prefilter helper --- */
/** True when @p data (the first @p size bytes of a candidate) starts with one
 *  of the three lead patterns.  Pure byte test, no I/O. */
XXFC_API bool xx_jpeg_check_magic(const uint8_t *data, size_t size);

/* --- Getters --- */
XXFC_API xx_jpeg_variant xx_jpeg_get_variant(const xx_jpeg *jpeg);
XXFC_API int64_t xx_jpeg_get_image_end(const xx_jpeg *jpeg);
XXFC_API uint32_t xx_jpeg_get_marker_count(const xx_jpeg *jpeg);
XXFC_API uint32_t xx_jpeg_get_scan_count(const xx_jpeg *jpeg);
XXFC_API uint8_t xx_jpeg_get_frame_marker(const xx_jpeg *jpeg);
XXFC_API bool xx_jpeg_is_progressive(const xx_jpeg *jpeg);
XXFC_API uint8_t xx_jpeg_get_precision(const xx_jpeg *jpeg);
XXFC_API uint16_t xx_jpeg_get_width(const xx_jpeg *jpeg);
XXFC_API uint16_t xx_jpeg_get_height(const xx_jpeg *jpeg);
XXFC_API uint8_t xx_jpeg_get_components(const xx_jpeg *jpeg);

/* Cast helpers */
static inline Abstractformat *xx_jpeg_to_format(xx_jpeg *jpeg) {
    return jpeg ? &jpeg->format : NULL;
}

static inline const Abstractformat *xx_jpeg_to_format_const(
    const xx_jpeg *jpeg) {
    return jpeg ? &jpeg->format : NULL;
}

/* User-facing aliases.  XJpegImage, not XJpeg: see the note at the top. */
static inline void XJpegImage_init(xx_jpeg *jpeg, xx_io_device *dev,
                                   int64_t base_address) {
    xx_jpeg_init(jpeg, dev, base_address);
}

static inline xx_jpeg *XJpegImage_create(xx_io_device *dev,
                                         int64_t base_address) {
    return xx_jpeg_create(dev, base_address);
}

static inline void XJpegImage_free(xx_jpeg *jpeg) { xx_jpeg_free(jpeg); }

static inline bool XJpegImage_is_valid(xx_jpeg *jpeg, xx_pd_struct *pd) {
    return jpeg ? xx_format_is_valid(&jpeg->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_jpeg_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_jpeg_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_jpeg_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_jpeg_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_jpeg_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_JPEG_H */
