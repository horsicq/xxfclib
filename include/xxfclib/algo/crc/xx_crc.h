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
 * @file xx_crc.h
 * @brief Comprehensive cyclic redundancy check (CRC) algorithms and parameterized engine.
 */

#ifndef XX_CRC_H
#define XX_CRC_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Standard predefined CRC algorithm identifiers.
 */
typedef enum xx_crc_type_e {
    XX_CRC_TYPE_UNKNOWN = 0,

    /* --- CRC-8 Algorithms --- */
    XX_CRC_TYPE_CRC8,               /**< CRC-8 / SMBUS (poly 0x07, init 0x00, refin=0, refout=0, xorout=0x00) */
    XX_CRC_TYPE_CRC8_CDMA2000,      /**< CRC-8 / CDMA2000 (poly 0x9B, init 0xFF, refin=0, refout=0, xorout=0x00) */
    XX_CRC_TYPE_CRC8_DARC,          /**< CRC-8 / DARC (poly 0x39, init 0x00, refin=1, refout=1, xorout=0x00) */
    XX_CRC_TYPE_CRC8_DVB_S2,        /**< CRC-8 / DVB-S2 (poly 0xD5, init 0x00, refin=0, refout=0, xorout=0x00) */
    XX_CRC_TYPE_CRC8_EBU,           /**< CRC-8 / EBU (poly 0x1D, init 0xFF, refin=1, refout=1, xorout=0x00) */
    XX_CRC_TYPE_CRC8_ICODE,         /**< CRC-8 / I-CODE (poly 0x1D, init 0xFD, refin=0, refout=0, xorout=0x00) */
    XX_CRC_TYPE_CRC8_ITU,           /**< CRC-8 / ITU (poly 0x07, init 0x00, refin=0, refout=0, xorout=0x55) */
    XX_CRC_TYPE_CRC8_MAXIM,         /**< CRC-8 / MAXIM / DALLAS 1-Wire (poly 0x31, init 0x00, refin=1, refout=1, xorout=0x00) */
    XX_CRC_TYPE_CRC8_ROHC,          /**< CRC-8 / ROHC (poly 0x07, init 0xFF, refin=1, refout=1, xorout=0x00) */
    XX_CRC_TYPE_CRC8_WCDMA,         /**< CRC-8 / WCDMA (poly 0x9B, init 0x00, refin=1, refout=1, xorout=0x00) */
    XX_CRC_TYPE_CRC8_BLUETOOTH,     /**< CRC-8 / BLUETOOTH (poly 0xA7, init 0x00, refin=1, refout=1, xorout=0x00) */
    XX_CRC_TYPE_CRC8_AUTOSAR,       /**< CRC-8 / AUTOSAR (poly 0x2F, init 0xFF, refin=0, refout=0, xorout=0xFF) */

    /* --- CRC-16 Algorithms --- */
    XX_CRC_TYPE_CRC16_ARC,          /**< CRC-16 / ARC / LHA (poly 0x8005, init 0x0000, refin=1, refout=1, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_CCITT_FALSE,  /**< CRC-16 / CCITT-FALSE (poly 0x1021, init 0xFFFF, refin=0, refout=0, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_AUG_CCITT,    /**< CRC-16 / AUG-CCITT / SPI (poly 0x1021, init 0x1D0F, refin=0, refout=0, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_BUYPASS,      /**< CRC-16 / BUYPASS (poly 0x8005, init 0x0000, refin=0, refout=0, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_CDMA2000,     /**< CRC-16 / CDMA2000 (poly 0xC867, init 0xFFFF, refin=0, refout=0, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_DDS_110,      /**< CRC-16 / DDS-110 (poly 0x8005, init 0x800D, refin=0, refout=0, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_DECT_R,       /**< CRC-16 / DECT-R (poly 0x0589, init 0x0000, refin=0, refout=0, xorout=0x0001) */
    XX_CRC_TYPE_CRC16_DECT_X,       /**< CRC-16 / DECT-X (poly 0x0589, init 0x0000, refin=0, refout=0, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_DNP,          /**< CRC-16 / DNP (poly 0x3D65, init 0x0000, refin=1, refout=1, xorout=0xFFFF) */
    XX_CRC_TYPE_CRC16_EN_13757,     /**< CRC-16 / EN-13757 (poly 0x3D65, init 0x0000, refin=0, refout=0, xorout=0xFFFF) */
    XX_CRC_TYPE_CRC16_GENIBUS,      /**< CRC-16 / GENIBUS (poly 0x1021, init 0xFFFF, refin=0, refout=0, xorout=0xFFFF) */
    XX_CRC_TYPE_CRC16_MAXIM,        /**< CRC-16 / MAXIM (poly 0x8005, init 0x0000, refin=1, refout=1, xorout=0xFFFF) */
    XX_CRC_TYPE_CRC16_MCRF4XX,      /**< CRC-16 / MCRF4XX (poly 0x1021, init 0xFFFF, refin=1, refout=1, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_MODBUS,       /**< CRC-16 / MODBUS (poly 0x8005, init 0xFFFF, refin=1, refout=1, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_RIELLO,       /**< CRC-16 / RIELLO (poly 0x1021, init 0xB2AA, refin=1, refout=1, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_T10_DIF,      /**< CRC-16 / T10-DIF (poly 0x8BB7, init 0x0000, refin=0, refout=0, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_TELEDISK,     /**< CRC-16 / TELEDISK (poly 0xA097, init 0x0000, refin=0, refout=0, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_TMS37157,     /**< CRC-16 / TMS37157 (poly 0x1021, init 0x89EC, refin=1, refout=1, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_USB,          /**< CRC-16 / USB (poly 0x8005, init 0xFFFF, refin=1, refout=1, xorout=0xFFFF) */
    XX_CRC_TYPE_CRC16_X25,          /**< CRC-16 / X-25 (poly 0x1021, init 0xFFFF, refin=1, refout=1, xorout=0xFFFF) */
    XX_CRC_TYPE_CRC16_XMODEM,       /**< CRC-16 / XMODEM (poly 0x1021, init 0x0000, refin=0, refout=0, xorout=0x0000) */
    XX_CRC_TYPE_CRC16_KERMIT,       /**< CRC-16 / KERMIT (poly 0x1021, init 0x0000, refin=1, refout=1, xorout=0x0000) */

    /* --- CRC-32 Algorithms --- */
    XX_CRC_TYPE_CRC32,              /**< CRC-32 / ISO-HDLC / PKZIP / Ethernet (poly 0x04C11DB7, init 0xFFFFFFFF, refin=1, refout=1, xorout=0xFFFFFFFF) */
    XX_CRC_TYPE_CRC32_BZIP2,        /**< CRC-32 / BZIP2 / AAL5 (poly 0x04C11DB7, init 0xFFFFFFFF, refin=0, refout=0, xorout=0xFFFFFFFF) */
    XX_CRC_TYPE_CRC32C,             /**< CRC-32C / Castagnoli (poly 0x1EDC6F41, init 0xFFFFFFFF, refin=1, refout=1, xorout=0xFFFFFFFF) */
    XX_CRC_TYPE_CRC32D,             /**< CRC-32D (poly 0xA833982B, init 0xFFFFFFFF, refin=1, refout=1, xorout=0xFFFFFFFF) */
    XX_CRC_TYPE_CRC32_MPEG2,        /**< CRC-32 / MPEG-2 (poly 0x04C11DB7, init 0xFFFFFFFF, refin=0, refout=0, xorout=0x00000000) */
    XX_CRC_TYPE_CRC32_POSIX,        /**< CRC-32 / POSIX / cksum (poly 0x04C11DB7, init 0x00000000, refin=0, refout=0, xorout=0xFFFFFFFF) */
    XX_CRC_TYPE_CRC32Q,             /**< CRC-32Q (poly 0x814141AB, init 0x00000000, refin=0, refout=0, xorout=0x00000000) */
    XX_CRC_TYPE_CRC32_JAMCRC,       /**< CRC-32 / JAMCRC (poly 0x04C11DB7, init 0xFFFFFFFF, refin=1, refout=1, xorout=0x00000000) */
    XX_CRC_TYPE_CRC32_XFER,         /**< CRC-32 / XFER (poly 0x000000AF, init 0x00000000, refin=0, refout=0, xorout=0x00000000) */

    /* --- CRC-64 Algorithms --- */
    XX_CRC_TYPE_CRC64_ECMA,         /**< CRC-64 / ECMA-182 (poly 0x42F0E1EBA9EA3693, init 0x0, refin=0, refout=0, xorout=0x0) */
    XX_CRC_TYPE_CRC64_GO_ISO,       /**< CRC-64 / GO-ISO (poly 0x1B, init 0xFFFFFFFFFFFFFFFF, refin=1, refout=1, xorout=0xFFFFFFFFFFFFFFFF) */
    XX_CRC_TYPE_CRC64_WE,           /**< CRC-64 / WE (poly 0x42F0E1EBA9EA3693, init 0xFFFFFFFFFFFFFFFF, refin=0, refout=0, xorout=0xFFFFFFFFFFFFFFFF) */
    XX_CRC_TYPE_CRC64_XZ,           /**< CRC-64 / XZ (poly 0x42F0E1EBA9EA3693, init 0xFFFFFFFFFFFFFFFF, refin=1, refout=1, xorout=0xFFFFFFFFFFFFFFFF) */

    XX_CRC_TYPE_CUSTOM,
    XX_CRC_TYPE_COUNT
} xx_crc_type_t;

/* Common algorithm aliases */
#define XX_CRC_TYPE_CRC8_SMBUS       XX_CRC_TYPE_CRC8
#define XX_CRC_TYPE_CRC16            XX_CRC_TYPE_CRC16_ARC
#define XX_CRC_TYPE_CRC32_ISO_HDLC   XX_CRC_TYPE_CRC32
#define XX_CRC_TYPE_CRC32_PKZIP      XX_CRC_TYPE_CRC32
#define XX_CRC_TYPE_CRC64            XX_CRC_TYPE_CRC64_XZ

/**
 * @brief Model definition representing an arbitrary parameterized CRC algorithm
 * conforming to the standard Williams / RevEng catalog specification.
 */
typedef struct xx_crc_model {
    uint8_t     width;      /**< CRC width in bits (8, 16, 32, 64) */
    uint64_t    poly;       /**< Generator polynomial */
    uint64_t    init;       /**< Initial shift register value */
    bool        refin;      /**< True if input bytes are reflected (LSB first) */
    bool        refout;     /**< True if output remainder is reflected before XOR */
    uint64_t    xorout;     /**< Final XOR mask applied to result */
    const char *name;       /**< Canonical name string */
} xx_crc_model;

/**
 * @brief Context structure for streaming / chunked CRC calculation.
 */
typedef struct xx_crc_context {
    xx_crc_model model;
    uint64_t     state;          /**< Running intermediate remainder register */
    uint64_t     table[256];     /**< Precalculated 256-entry lookup table */
    bool         initialized;
} xx_crc_context;

/* ========================================================================= */
/* --- Model Discovery & Introspection                                   --- */
/* ========================================================================= */

/**
 * @brief Retrieve model definition for a predefined CRC type.
 */
XXFC_API const xx_crc_model *xx_crc_get_model(xx_crc_type_t type);

/**
 * @brief Convert predefined CRC type to readable name string.
 */
XXFC_API const char *xx_crc_type_to_string(xx_crc_type_t type);

/**
 * @brief Find predefined CRC type by name (case-insensitive, ignores separators like '/', '-', '_').
 */
XXFC_API xx_crc_type_t xx_crc_string_to_type(const char *name);

/* ========================================================================= */
/* --- One-Shot Calculation API                                          --- */
/* ========================================================================= */

/**
 * @brief Calculate CRC-8 for given buffer and algorithm type.
 */
XXFC_API uint8_t xx_crc8(xx_crc_type_t type, const void *data, size_t size);

/**
 * @brief Calculate CRC-16 for given buffer and algorithm type.
 */
XXFC_API uint16_t xx_crc16(xx_crc_type_t type, const void *data, size_t size);

/**
 * @brief Calculate CRC-32 for given buffer and algorithm type.
 */
XXFC_API uint32_t xx_crc32(xx_crc_type_t type, const void *data, size_t size);

/**
 * @brief Calculate CRC-64 for given buffer and algorithm type.
 */
XXFC_API uint64_t xx_crc64(xx_crc_type_t type, const void *data, size_t size);

/**
 * @brief Calculate CRC for given buffer using an arbitrary custom model.
 */
XXFC_API uint64_t xx_crc_calculate(const xx_crc_model *model, const void *data, size_t size);

/**
 * @brief Calculate CRC for given buffer by predefined type, storing in 64-bit output pointer.
 */
XXFC_API bool xx_crc_calculate_by_type(xx_crc_type_t type, const void *data, size_t size, uint64_t *out_crc);

/* ========================================================================= */
/* --- Device Calculation API                                            --- */
/* ========================================================================= */

/**
 * @brief Compute CRC over a region of an xx_io_device.
 * @param dev Source I/O device.
 * @param offset Start offset (-1 for current seek position).
 * @param size Number of bytes to process (-1 for all remaining until EOF).
 * @param model Algorithm model definition.
 * @param pd Optional progress struct.
 * @param out_crc Output pointer for calculated checksum.
 * @return True on success, false on error.
 */
XXFC_API bool xx_crc_calculate_device(xx_io_device *dev, int64_t offset, int64_t size,
                                     const xx_crc_model *model, xx_pd_struct *pd, uint64_t *out_crc);

/**
 * @brief Compute CRC over a region of an xx_io_device using a predefined CRC type.
 */
XXFC_API bool xx_crc_calculate_device_by_type(xx_io_device *dev, int64_t offset, int64_t size,
                                             xx_crc_type_t type, xx_pd_struct *pd, uint64_t *out_crc);

/**
 * @brief Verify CRC checksum over a region of an xx_io_device.
 * Generic verification across any archive (ZIP, 7z, GZ, TAR, RAR) and any CRC type.
 * @param dev Device to read from.
 * @param offset Start byte offset (-1 to read from current position).
 * @param size Number of bytes to verify.
 * @param type Predefined CRC algorithm type (CRC-8, CRC-16, CRC-32, CRC-64).
 * @param expected_crc Expected checksum value.
 * @param pd Optional progress monitor.
 * @return True if read succeeded and computed CRC matches expected_crc.
 */
XXFC_API bool xx_crc_verify_device(xx_io_device *dev, int64_t offset, int64_t size,
                                   xx_crc_type_t type, uint64_t expected_crc,
                                   xx_pd_struct *pd);

/**
 * @brief Stream data between devices while computing and verifying CRC on-the-fly.
 * Generic across all archive containers and decompression streams.
 * @param src_dev Source device.
 * @param src_offset Start byte offset in src_dev (-1 to use current seek position).
 * @param size Number of bytes to stream.
 * @param dst_dev Destination device.
 * @param type Predefined CRC algorithm type.
 * @param expected_crc Expected checksum value.
 * @param out_match Optional output boolean set to true if CRC matches expected_crc.
 * @param pd Optional progress monitor.
 * @return True on success and CRC match, false on failure or CRC mismatch.
 */
XXFC_API bool xx_crc_copy_device_verify(xx_io_device *src_dev, int64_t src_offset, int64_t size,
                                        xx_io_device *dst_dev, xx_crc_type_t type,
                                        uint64_t expected_crc, bool *out_match, xx_pd_struct *pd);

/* ========================================================================= */
/* --- Streaming / Incremental Context API                               --- */
/* ========================================================================= */

/**
 * @brief Initialize a streaming CRC context using a model definition.
 */
XXFC_API bool xx_crc_context_init(xx_crc_context *ctx, const xx_crc_model *model);

/**
 * @brief Initialize a streaming CRC context using a predefined type.
 */
XXFC_API bool xx_crc_context_init_type(xx_crc_context *ctx, xx_crc_type_t type);

/**
 * @brief Feed a block of data into the active streaming CRC context.
 */
XXFC_API void xx_crc_context_update(xx_crc_context *ctx, const void *data, size_t size);

/**
 * @brief Finalize and return the calculated CRC checksum (does not mutate context state).
 */
XXFC_API uint64_t xx_crc_context_final(const xx_crc_context *ctx);

/**
 * @brief Reset streaming context accumulator back to its initial state.
 */
XXFC_API void xx_crc_context_reset(xx_crc_context *ctx);

/* ========================================================================= */
/* --- Fast Predefined Calculation Shortcuts                             --- */
/* ========================================================================= */

/**
 * @brief Calculate standard ISO-HDLC / PKZIP / Ethernet CRC-32 (0xEDB88320 reflected poly).
 * @param crc Running CRC (pass 0 for first chunk).
 */
XXFC_API uint32_t xx_crc32_calc(uint32_t crc, const void *data, size_t size);

/**
 * @brief Calculate Castagnoli CRC-32C.
 * @param crc Running CRC (pass 0 for first chunk).
 */
XXFC_API uint32_t xx_crc32c_calc(uint32_t crc, const void *data, size_t size);

/**
 * @brief Calculate CRC-16 / ARC (LHA/ARC).
 * @param crc Running CRC (pass 0 for first chunk).
 */
XXFC_API uint16_t xx_crc16_arc_calc(uint16_t crc, const void *data, size_t size);

/**
 * @brief Calculate CRC-16 / CCITT-FALSE.
 * @param crc Running CRC (pass 0xFFFF for first chunk).
 */
XXFC_API uint16_t xx_crc16_ccitt_calc(uint16_t crc, const void *data, size_t size);

/**
 * @brief Calculate CRC-16 / MODBUS.
 * @param crc Running CRC (pass 0xFFFF for first chunk).
 */
XXFC_API uint16_t xx_crc16_modbus_calc(uint16_t crc, const void *data, size_t size);

/**
 * @brief Calculate CRC-16 / XMODEM.
 * @param crc Running CRC (pass 0 for first chunk).
 */
XXFC_API uint16_t xx_crc16_xmodem_calc(uint16_t crc, const void *data, size_t size);

/**
 * @brief Calculate CRC-8 (SMBus standard).
 * @param crc Running CRC (pass 0 for first chunk).
 */
XXFC_API uint8_t xx_crc8_calc(uint8_t crc, const void *data, size_t size);

/**
 * @brief Calculate CRC-8 / MAXIM (Dallas 1-Wire).
 * @param crc Running CRC (pass 0 for first chunk).
 */
XXFC_API uint8_t xx_crc8_maxim_calc(uint8_t crc, const void *data, size_t size);

/**
 * @brief Calculate CRC-64 / XZ.
 * @param crc Running CRC (pass 0 for first chunk).
 */
XXFC_API uint64_t xx_crc64_xz_calc(uint64_t crc, const void *data, size_t size);

/**
 * @brief Calculate CRC-64 / ECMA-182.
 * @param crc Running CRC (pass 0 for first chunk).
 */
XXFC_API uint64_t xx_crc64_ecma_calc(uint64_t crc, const void *data, size_t size);

/* ========================================================================= */
/* --- Convenience Wrappers & Aliases                                    --- */
/* ========================================================================= */

static inline uint8_t XCrc_crc8(xx_crc_type_t type, const void *data, size_t size) {
    return xx_crc8(type, data, size);
}

static inline uint16_t XCrc_crc16(xx_crc_type_t type, const void *data, size_t size) {
    return xx_crc16(type, data, size);
}

static inline uint32_t XCrc_crc32(xx_crc_type_t type, const void *data, size_t size) {
    return xx_crc32(type, data, size);
}

static inline uint64_t XCrc_crc64(xx_crc_type_t type, const void *data, size_t size) {
    return xx_crc64(type, data, size);
}

static inline uint64_t XCrc_calculate(const xx_crc_model *model, const void *data, size_t size) {
    return xx_crc_calculate(model, data, size);
}

static inline uint32_t XCrc_crc32_calc(uint32_t crc, const void *data, size_t size) {
    return xx_crc32_calc(crc, data, size);
}

#ifdef __cplusplus
}
#endif

#endif /* XX_CRC_H */
