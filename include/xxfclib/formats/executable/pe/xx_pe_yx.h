/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* YARA-X-exact PE helpers.
 *
 * The regular readers in xx_pe.h / xx_pe_inspect.h follow the Windows loader
 * or flat-inspection semantics that DIE and the script API rely on. The
 * functions in this header instead reproduce, bit for bit, the parsing
 * behavior of YARA-X 1.21.0's PE module (and of the xxyara port of it):
 * RVA<->file-offset conversion, the pe.calculate_checksum quirk, the Rich
 * signature view (including the decrypted clear data), standard and delay
 * import walking with the published ordinal-name tables, export enumeration
 * with forwarder detection, the MD5 imphash with Unicode lowercasing, the
 * resource tree and VS_VERSIONINFO string walkers, and Authenticode signature
 * extraction over the PE security directory.
 *
 * These functions never modify their input and only read inside the supplied
 * buffer. Name and DLL bytes returned by the visitors borrow the input and
 * remain valid for its lifetime. Signature results own their strings; release
 * them with xx_pe_yx_signature_result_destroy. */

#ifndef XXFCLIB_PE_YX_H
#define XXFCLIB_PE_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Strict UTF-8 validation over a byte range (no NUL requirement), shared by
 * the YARA-X-exact PE parsers and Authenticode name formatting. */
XXFC_API bool xx_pe_yx_utf8_valid(const uint8_t *text, size_t length);

/** Maps an RVA to a file offset with the YARA-X PE parser semantics: the
 * section with the highest start RVA containing the RVA wins, the raw offset
 * is aligned down by min(file_alignment, 0x200) (and to 0x200 when
 * section_alignment >= 0x1000), and RVAs below the lowest section RVA pass
 * through. Returns false when the RVA is not backed by raw data or the input
 * is not a PE. */
XXFC_API bool xx_pe_yx_rva_to_offset(const uint8_t *data, size_t size,
                                     uint64_t rva, uint64_t *value);

/** Inverse of xx_pe_yx_rva_to_offset with the YARA-X bounds checks. Sections
 * without file data do not participate; an offset below the lowest mapped
 * raw offset passes through. Returns false when the offset is not inside a
 * section's raw data. */
XXFC_API bool xx_pe_yx_offset_to_rva(const uint8_t *data, size_t size,
                                     uint64_t offset, uint64_t *value);

/** The pe.calculate_checksum value: the YARA-X 1.21.0 checksum iterator
 * consumes 32-bit words (a trailing 16-bit word when only two bytes remain)
 * with carry propagation; an odd trailing ASCII digit contributes its numeric
 * value while other trailing bytes are left unread. The stored CheckSum
 * header field is then subtracted (with the reference's borrow quirk), the
 * sum is folded to 16 bits and the file length added. Unlike xx_pe_checksum,
 * the CheckSum field itself is included in the iterated data. Returns false
 * when the CheckSum field is undefined for this input. */
XXFC_API bool xx_pe_yx_calculate_checksum(const uint8_t *data, size_t size,
                                          uint64_t *value);

/** Located Rich signature. `offset`/`length` describe the encrypted blob in
 * the file (starting at the xored "DanS" marker); the clear payload has the
 * same length once decrypted with `key`. */
typedef struct {
    bool present;
    size_t offset;
    size_t length;
    uint32_t key;
} xx_pe_yx_rich_info;

/** Locates the Rich signature. Returns false when the input is not a PE or
 * no well-formed DanS..Rich pair exists; `info` is unchanged then. */
XXFC_API bool xx_pe_yx_rich_read(const uint8_t *data, size_t size,
                                 xx_pe_yx_rich_info *info);

/** Number of tool records, matching the reference module: 1 when the Rich
 * signature is absent, otherwise (length - 16) / 8, or 0 for a truncated
 * signature shorter than 16 bytes. Always returns true. */
XXFC_API bool xx_pe_yx_rich_tool_count(const uint8_t *data, size_t size,
                                       uint64_t *count);

/** One tool record field; `member` is "toolid", "version" or "times".
 * Returns false when the index is out of range or the member is unknown. */
XXFC_API bool xx_pe_yx_rich_tool_field(const uint8_t *data, size_t size,
                                       uint64_t index, const char *member,
                                       uint64_t *value);

/** Allocates and decrypts the DanS..Rich payload (allocated with malloc; the
 * caller frees with free). Returns false when no Rich signature exists. */
XXFC_API bool xx_pe_yx_rich_clear_data(const uint8_t *data, size_t size,
                                       uint8_t **bytes, size_t *length);

/** Import walk totals; undefined (false) on non-PE data. */
typedef struct {
    uint64_t standard_libraries;
    uint64_t standard_functions;
    uint64_t delayed_libraries;
    uint64_t delayed_functions;
} xx_pe_yx_import_stats;

/** Called for every successfully parsed import. `function` names an ordinal
 * fallback ("ordN" or a published ordinal name) when `by_ordinal` is true.
 * `rva` is the IAT slot address. All name bytes borrow the input. */
typedef void (*xx_pe_yx_import_visitor)(void *opaque,
                                        const uint8_t *dll,
                                        size_t dll_length,
                                        const uint8_t *function,
                                        size_t function_length,
                                        bool by_ordinal, uint16_t ordinal,
                                        uint32_t rva);

/** Like xx_pe_yx_import_visitor, but also receives the descriptor index,
 * preserving separate library records for adjacent identical DLL names. */
typedef void (*xx_pe_yx_import_descriptor_visitor)(
    void *opaque, size_t descriptor_index, const uint8_t *dll,
    size_t dll_length, const uint8_t *function, size_t function_length,
    bool by_ordinal, uint16_t ordinal, uint32_t rva);

/** Fills `out` by walking the standard (directory 1) and delay-load
 * (directory 13) import descriptors with the reference budgets. Returns
 * false on non-PE data. */
XXFC_API bool xx_pe_yx_get_import_stats(const uint8_t *data, size_t size,
                                        xx_pe_yx_import_stats *out);

/** Walks parsed imports in file order; `flags` bit 0 selects standard
 * imports, bit 1 delay imports. A NULL visitor walks nothing. */
XXFC_API void xx_pe_yx_visit_imports(const uint8_t *data, size_t size,
                                     unsigned flags,
                                     xx_pe_yx_import_visitor visitor,
                                     void *opaque);

/** Descriptor-indexed variant of xx_pe_yx_visit_imports. */
XXFC_API void xx_pe_yx_visit_import_descriptors(
    const uint8_t *data, size_t size, unsigned flags,
    xx_pe_yx_import_descriptor_visitor visitor, void *opaque);

/** Resolves an import ordinal for oleaut32.dll, wsock32.dll and ws2_32.dll
 * using YARA-X's published tables; writes "ordN" into `fallback` otherwise
 * and returns it. `fallback` must hold 16 bytes. */
XXFC_API const char *xx_pe_yx_lookup_ordinal_name(const uint8_t *dll,
                                                  size_t dll_length,
                                                  uint16_t ordinal,
                                                  char fallback[16]);

/** One export address table slot. `name` is NULL for unnamed slots; a slot
 * inside the export directory range exposes `forward_name` instead of an
 * offset. All bytes borrow the input. */
typedef struct {
    const uint8_t *name;
    size_t name_length;
    uint32_t ordinal;
    uint32_t rva;
    bool has_offset;
    uint32_t offset;
    const uint8_t *forward_name;
    size_t forward_name_length;
} xx_pe_yx_export;

typedef void (*xx_pe_yx_export_visitor)(void *opaque,
                                        const xx_pe_yx_export *entry);

/** The reference module's scalar export fields. Returns false only when the
 * input is not a PE. `*present` reports whether the export directory was
 * located; when absent, `*function_count` is 0 and `*timestamp` is
 * meaningless. */
XXFC_API bool xx_pe_yx_export_info(const uint8_t *data, size_t size,
                                   bool *present, uint64_t *function_count,
                                   uint64_t *timestamp);

/** The export directory's DLL name: a validated UTF-8 string borrowing the
 * input. Returns false when the directory or its name cannot be read. */
XXFC_API bool xx_pe_yx_export_dll_name(const uint8_t *data, size_t size,
                                       const uint8_t **bytes, size_t *length);

/** Visits every export address table slot in order, applying the reference
 * name aliasing (first ordinal-table occurrence wins) and forwarder
 * detection. `poll`/`poll_opaque` optionally abort the walk: returning true
 * from `poll` stops visiting and makes this function return false. Without a
 * poll callback the walk always returns true. */
XXFC_API bool xx_pe_yx_visit_exports(const uint8_t *data, size_t size,
                                     xx_pe_yx_export_visitor visitor,
                                     void *opaque,
                                     bool (*poll)(void *poll_opaque),
                                     void *poll_opaque);

/** Finds an export by name (case-insensitive ASCII) or slot ordinal.
 * `*found`/`*index` are always written; the index is the EAT slot. */
XXFC_API bool xx_pe_yx_exports(const uint8_t *data, size_t size,
                               const char *name, size_t name_length,
                               bool by_ordinal, uint64_t ordinal,
                               bool *found, uint64_t *index);

/** ----------------------------------------------------------- Imphash -- */

/** The pe.imphash() value: an MD5 over "dll.function" pairs in import order,
 * comma-separated. Library and function names are lowercased with Rust's
 * str::to_lowercase semantics (Unicode 17 per-code-point mapping plus the
 * Final_Sigma context rule); a trailing ".dll"/".sys"/".ocx" extension is
 * stripped from the library name. Standard imports only. `out` must hold 33
 * bytes. Returns false on non-PE data or allocation failure. */
XXFC_API bool xx_pe_yx_imphash(const uint8_t *data, size_t size, char out[33]);

/** --------------------------------------------------------- Resources -- */

typedef enum {
    XX_PE_YX_RESOURCE_UNKNOWN,
    XX_PE_YX_RESOURCE_NUMERIC,
    XX_PE_YX_RESOURCE_NAMED
} xx_pe_yx_resource_id_kind;

/** A resource identifier at one tree level. Named identifiers are UTF-16LE
 * bytes borrowing the input. */
typedef struct {
    xx_pe_yx_resource_id_kind kind;
    uint32_t value;
    const uint8_t *name;
    size_t name_length;
} xx_pe_yx_resource_id;

/** One resource leaf in breadth-first tree order. */
typedef struct {
    uint32_t length;
    uint32_t rva;
    bool has_offset;
    uint32_t offset;
    xx_pe_yx_resource_id type;
    xx_pe_yx_resource_id id;
    xx_pe_yx_resource_id language;
} xx_pe_yx_resource;

typedef struct {
    bool present; /* true only when at least one resource was parsed */
    uint32_t timestamp;
    uint16_t major_version;
    uint16_t minor_version;
    uint64_t count;
} xx_pe_yx_resource_directory;

typedef void (*xx_pe_yx_resource_visitor)(void *opaque,
                                          const xx_pe_yx_resource *resource);

/** Walks the resource tree in the reference parser's breadth-first order
 * (type, name, language levels), applying its limits: 65536 queued nodes and
 * leaves, 32768 entries per directory, named IDs below 1000 UTF-16 units,
 * language IDs at most 0xfffff and leaf sizes below 0x3fffffff. A valid leaf
 * is retained even when its data RVA cannot be mapped. `result` receives the
 * directory header of the first level and the leaf count. `poll`/`poll_opaque`
 * optionally abort the walk: returning true from `poll` stops walking and
 * makes this function return false. Without a poll callback the walk always
 * returns true (also for non-PE input, which yields an empty directory). */
XXFC_API bool xx_pe_yx_visit_resources(const uint8_t *data, size_t size,
                                       xx_pe_yx_resource_directory *result,
                                       xx_pe_yx_resource_visitor visitor,
                                       void *opaque,
                                       bool (*poll)(void *poll_opaque),
                                       void *poll_opaque);

/** ------------------------------------------------------ Version info -- */

/** Called for every StringFileInfo string in document order. Keys and values
 * are decoded UTF-8; the pointers are valid only during the callback. */
typedef void (*xx_pe_yx_version_visitor)(void *opaque,
                                         const char *key, size_t key_length,
                                         const char *value,
                                         size_t value_length);

/** Parses one VS_VERSIONINFO structure at `offset`, visiting its StringFileInfo
 * strings; returns the number of strings. The reference accepts either child
 * order (VarFileInfo, StringFileInfo), each at most once, skips the 52-byte
 * fixed file info value, and treats a missing UTF-16 terminator as the end of
 * the record. `poll` aborts parsing like in xx_pe_yx_visit_resources. */
XXFC_API uint64_t xx_pe_yx_version_walk_at(const uint8_t *data, size_t size,
                                           size_t offset,
                                           xx_pe_yx_version_visitor visitor,
                                           void *opaque,
                                           bool (*poll)(void *poll_opaque),
                                           void *poll_opaque);

/** Walks the resource tree and parses every RT_VERSION resource
 * (type numeric 16 with a mappable data offset). Returns false when the
 * resource walk aborts through `poll`. */
XXFC_API bool xx_pe_yx_visit_version_info(const uint8_t *data, size_t size,
                                          xx_pe_yx_version_visitor visitor,
                                          void *opaque,
                                          bool (*poll)(void *poll_opaque),
                                          void *poll_opaque);

/** -------------------------------------------------------- Signatures -- */

/** One X.509 certificate of a signature, with the reference module's field
 * rendering: slash-separated issuer/subject names, lowercase hex SHA-1
 * thumbprint, algorithm display name and OID, colon-separated serial, and
 * 1-based version. All strings are owned. */
typedef struct {
    char *issuer;
    size_t issuer_length;
    char *subject;
    size_t subject_length;
    char *thumbprint;
    char *algorithm;
    char *algorithm_oid;
    char *serial;
    int64_t version;
    int64_t not_before;
    int64_t not_after;
} xx_pe_yx_certificate;

typedef struct {
    bool verified;
    bool has_sign_time;
    int64_t sign_time;
    char *digest;
    char *digest_alg;
    size_t chain_count;
    size_t *chain_indices;
} xx_pe_yx_countersignature;

typedef struct {
    bool verified;
    char *digest_alg;
    char *digest;
    char *file_digest;
    char *program_name;
    char *more_info;
    char *signer_digest;
    char *signer_digest_alg;
    size_t signer_chain_count;
    size_t *signer_chain_indices;
    size_t certificate_count;
    xx_pe_yx_certificate *certificates;
    size_t countersignature_count;
    xx_pe_yx_countersignature *countersignatures;
} xx_pe_yx_signature;

typedef struct {
    size_t count;
    xx_pe_yx_signature *items;
} xx_pe_yx_signature_result;

/** Hasher callback used for non-PE Authenticode inputs (the msi module): the
 * parser feeds the signed bytes to `update`, which hashes them with the
 * signature's digest algorithm. */
typedef void (*xx_pe_yx_authenticode_update_fn)(void *state,
                                                const uint8_t *data,
                                                size_t size);
typedef void (*xx_pe_yx_authenticode_hasher_fn)(void *opaque,
                                                xx_pe_yx_authenticode_update_fn update,
                                                void *state);

/** Parses the Authenticode PKCS#7 signatures of the PE security directory:
 * iterates the WIN_CERTIFICATE records (length-prefixed, 8-byte aligned) and
 * renders every nested signature with the reference policy (serial-only
 * signer selection, prefix certificate sets, PKCS#9/RFC3161 countersignatures
 * and nested signatures). All strings and arrays in `result` are owned and
 * released by xx_pe_yx_signature_result_destroy. Non-PE input and a missing
 * certificate table leave `result` empty. Returns false only for invalid
 * arguments or allocation failure (`result` destroyed then). */
XXFC_API bool xx_pe_yx_parse_signatures(const uint8_t *data, size_t size,
                                        xx_pe_yx_signature_result *result);

/** Parses the Authenticode signatures of one DER PKCS#7 SignedData blob, such
 * as an MSI DigitalSignature stream; each file_digest is computed with
 * `hasher` instead of the PE byte ranges. A blob that is not a valid
 * signature leaves `result` empty. */
XXFC_API bool xx_pe_yx_parse_blob(const uint8_t *blob, size_t blob_size,
                                  xx_pe_yx_authenticode_hasher_fn hasher,
                                  void *hasher_opaque,
                                  xx_pe_yx_signature_result *result);

/** Releases every signature, certificate and countersignature owned by
 * `result` and zeroes it. */
XXFC_API void xx_pe_yx_signature_result_destroy(xx_pe_yx_signature_result *result);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_PE_YX_H */
