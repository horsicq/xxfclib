/* xx_microsoft_pdb_debug.h - debug information of a Microsoft PDB beyond types:
 * C13 line tables and source file checksums, the /names string table, compiler
 * and build information per module, procedure details (parameters, locals,
 * frame, inline sites), embedded sources, source server / SourceLink data,
 * FPO / frame data and OMAP.
 *
 * Everything here is built on the public xx_microsoft_pdb.h API (streams,
 * modules, section headers, IPI ids), so it works on any xx_ms_pdb opened with
 * xx_ms_pdb_open. MSF 7.00 ("DS") files only unless a function says otherwise;
 * JG files and portable PDBs give empty results.
 *
 * Ownership: every result struct owns all of its memory (strings included) and
 * is released with its *_free function, which also accepts a zeroed struct and
 * leaves the struct zeroed. A getter returns false only for a bad argument or
 * a failed allocation; "nothing there" (no such stream, no C13 data, a broken
 * record) is true with empty results. Malformed data is cut at the first bad
 * record, never read out of bounds.
 *
 * Addresses: "rva" fields are relative virtual addresses computed from
 * segment:offset with the PDB's section headers (DBI optional debug stream 5,
 * or 10 + OMAP when the PDB has OMAP: then the rva is already mapped to the
 * image); XX_MS_PDB_NO_RVA when it cannot be computed.
 */
#ifndef XX_MICROSOFT_PDB_DEBUG_H
#define XX_MICROSOFT_PDB_DEBUG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_MS_PDB_NO_RVA ((uint64_t)-1)

/* ======================================================================
 * Part A: /names + address mapping (xx_microsoft_pdb_names.c), C13 lines
 * (xx_microsoft_pdb_lines.c)
 * ====================================================================== */

/* The /names string table (named stream "/names": u32 magic 0xEFFEEFFE,
 * u32 hash version, u32 buffer size, buffer). Loaded once per handle. */
typedef struct xx_ms_pdb_names xx_ms_pdb_names; /* opaque */

XXFC_API xx_ms_pdb_names *xx_ms_pdb_names_open(xx_ms_pdb *pdb); /* NULL: no /names */
XXFC_API void xx_ms_pdb_names_close(xx_ms_pdb_names *names);
/* The NUL-terminated string at that buffer offset, NULL when out of range.
 * Valid until xx_ms_pdb_names_close. */
XXFC_API const char *xx_ms_pdb_names_get(const xx_ms_pdb_names *names, uint32_t offset);

/* segment:offset (segment 1 based) -> rva. Uses the section headers and, when
 * the DBI optional header has OMAP-from-source (stream index 4) and original
 * section headers (index 10), maps through OMAP as MSDIA does. The mapper is
 * built once and kept; it is independent of the xx_ms_pdb's own caches. */
typedef struct xx_ms_pdb_addr_map xx_ms_pdb_addr_map; /* opaque */
XXFC_API xx_ms_pdb_addr_map *xx_ms_pdb_addr_map_open(xx_ms_pdb *pdb); /* never NULL unless OOM */
XXFC_API void xx_ms_pdb_addr_map_close(xx_ms_pdb_addr_map *map);
XXFC_API uint64_t xx_ms_pdb_addr_map_rva(const xx_ms_pdb_addr_map *map, uint16_t segment, uint32_t offset);
XXFC_API bool xx_ms_pdb_addr_map_has_omap(const xx_ms_pdb_addr_map *map);
/* A pre-OMAP ("source") rva -> image rva through OMAP-from-source (identity
 * without OMAP; XX_MS_PDB_NO_RVA when OMAP maps it to 0). */
XXFC_API uint64_t xx_ms_pdb_addr_map_omap(const xx_ms_pdb_addr_map *map, uint32_t rva);

#define XX_MS_PDB_CHECKSUM_NONE   0
#define XX_MS_PDB_CHECKSUM_MD5    1
#define XX_MS_PDB_CHECKSUM_SHA1   2
#define XX_MS_PDB_CHECKSUM_SHA256 3

/* One entry of a module's DEBUG_S_FILECHKSMS (0xF4) subsection. */
typedef struct xx_ms_pdb_source_file {
    uint32_t checksum_offset;  /* offset of the entry inside the subsection: the
                                  "file id" the line blocks and inlinee lines use */
    const char *name;          /* from /names, "" when missing */
    uint32_t name_offset;      /* into /names */
    uint8_t checksum_kind;     /* XX_MS_PDB_CHECKSUM_* (other values kept as is) */
    uint8_t checksum_size;     /* bytes stored, at most 64 are kept */
    uint8_t checksum[64];
} xx_ms_pdb_source_file;

/* One line entry of a DEBUG_S_LINES (0xF2) block. */
typedef struct xx_ms_pdb_line {
    uint16_t segment;
    uint32_t offset;           /* offCon + the entry's offset */
    uint64_t rva;
    uint32_t length;           /* up to the next entry of the same block, the
                                  last one up to offCon + cbCon */
    uint32_t line;             /* linenumStart (24 bits); 0xFEEFEE / 0xF00F00
                                  are "hidden" lines and kept as is */
    uint32_t line_end;         /* line + deltaLineEnd (7 bits) */
    uint16_t column;           /* only with CV_LINES_HAVE_COLUMNS, else 0 */
    uint16_t column_end;
    bool is_statement;         /* fStatement bit */
    int32_t file;              /* index into xx_ms_pdb_module_lines.files, -1 if
                                  the block's file id matches no entry */
} xx_ms_pdb_line;

/* One entry of a DEBUG_S_INLINEELINES (0xF6) subsection. */
typedef struct xx_ms_pdb_inlinee_line {
    uint32_t inlinee;          /* IPI id (LF_FUNC_ID / LF_MFUNC_ID) */
    int32_t file;              /* index into files, -1 if unknown */
    uint32_t line;             /* line the inlinee starts at */
    size_t extra_file_count;   /* CV_INLINEE_SOURCE_LINE_SIGNATURE_EX only */
    int32_t *extra_files;      /* indexes into files */
} xx_ms_pdb_inlinee_line;

/* A subsection of the module's C13 data that this module does not decode
 * (frame data 0xF5, cross scope 0xF7/0xF8, IL lines 0xF9, ...). */
typedef struct xx_ms_pdb_subsection {
    uint32_t kind;             /* incl. the 0x80000000 "ignore" bit */
    uint32_t offset;           /* of the payload inside the module stream */
    uint32_t size;
} xx_ms_pdb_subsection;

typedef struct xx_ms_pdb_module_lines {
    int32_t module_index;
    xx_ms_pdb_source_file *files;     size_t file_count;
    xx_ms_pdb_line *lines;            size_t line_count;   /* stream order */
    xx_ms_pdb_inlinee_line *inlinees; size_t inlinee_count;
    xx_ms_pdb_subsection *others;     size_t other_count;
    void *internal_;                  /* owned allocations */
} xx_ms_pdb_module_lines;

/* The C13 data of one module (module stream: u32 signature 4, symbols
 * (sym_byte_size - 4), C11 lines (c11_byte_size, ignored), C13 subsections
 * (c13_byte_size): {u32 kind, u32 size, payload, pad to 4}). names / map may
 * be NULL (then names are "" / rvas XX_MS_PDB_NO_RVA). */
XXFC_API bool xx_ms_pdb_get_module_lines(xx_ms_pdb *pdb, int32_t module_index,
                                         const xx_ms_pdb_names *names,
                                         const xx_ms_pdb_addr_map *map,
                                         xx_ms_pdb_module_lines *out);
XXFC_API void xx_ms_pdb_module_lines_free(xx_ms_pdb_module_lines *lines);

XXFC_API const char *xx_ms_pdb_checksum_kind_to_string(uint8_t kind); /* "None", "MD5", "SHA1", "SHA256"; "Unknown" */
XXFC_API const char *xx_ms_pdb_debug_subsection_to_string(uint32_t kind); /* "DEBUG_S_LINES", ... ; "Unknown" */

/* ======================================================================
 * Part B (xx_microsoft_pdb_syms.c): compiler/build info, procedure details
 * ====================================================================== */

typedef struct xx_ms_pdb_env_pair { const char *key; const char *value; } xx_ms_pdb_env_pair;

/* What a module's symbol stream says about how it was built. */
typedef struct xx_ms_pdb_compile_info {
    int32_t module_index;
    bool has_compile;          /* an S_COMPILE / S_COMPILE2(_ST) / S_COMPILE3 was found */
    uint16_t compile_kind;     /* that symbol kind */
    uint32_t language;         /* CV_CFL_LANG: 0 C, 1 C++, 2 Fortran, 3 MASM, 4 Pascal,
                                  5 Basic, 6 COBOL, 7 LINK, 8 CVTRES, 9 CVTPGD,
                                  10 C#, 11 VB, 12 ILASM, 13 Java, 14 JScript,
                                  15 MSIL, 16 HLSL, 17 ObjC, 18 ObjC++, 19 Swift,
                                  20 ALIASOBJ, 21 Rust, 22 Go; 'D' 0x44 D, ... */
    uint32_t flags;            /* the flag bits after the language byte, as stored
                                  (S_COMPILE3: 24 bits; S_COMPILE2: 24 bits) */
    bool ec, no_dbg_info, ltcg, no_data_align, managed_present, security_checks,
         hot_patch, cvtcil, msil_module, sdl, pgo, exp_module; /* decoded flag bits */
    uint16_t machine;          /* CV_CPU_TYPE_e */
    uint16_t fe_major, fe_minor, fe_build, fe_qfe;  /* front end (qfe: COMPILE3 only) */
    uint16_t be_major, be_minor, be_build, be_qfe;  /* back end */
    const char *version;       /* the version string ("Microsoft (R) Optimizing Compiler") */
    const char *extra_strings; /* S_COMPILE2: the extra NUL-separated strings joined
                                  with ' ', else "" */
    bool has_objname;
    uint32_t object_signature; /* S_OBJNAME */
    const char *object_name;   /* S_OBJNAME, else "" */
    xx_ms_pdb_env_pair *env;   size_t env_count;  /* S_ENVBLOCK pairs (cwd, cl, cmd, src, pdb, ...) */
    bool has_build_info;       /* S_BUILDINFO -> IPI LF_BUILDINFO */
    uint32_t build_info_id;
    /* LF_BUILDINFO args in order: 0 current directory, 1 build tool,
       2 source file, 3 pdb file, 4 command line; each an LF_STRING_ID whose
       LF_SUBSTR_LIST parts (if any) are prepended (substrings first, then the
       string itself, all concatenated without separator, as MSDIA/llvm do). */
    const char *build_cwd, *build_tool, *build_source, *build_pdb, *build_command_line;
    void *internal_;
} xx_ms_pdb_compile_info;

XXFC_API bool xx_ms_pdb_get_module_compile_info(xx_ms_pdb *pdb, int32_t module_index,
                                                xx_ms_pdb_compile_info *out);
XXFC_API void xx_ms_pdb_compile_info_free(xx_ms_pdb_compile_info *info);
XXFC_API const char *xx_ms_pdb_language_to_string(uint32_t language); /* "C", "C++", "MASM", ... ; "Unknown" */
XXFC_API const char *xx_ms_pdb_cpu_to_string(uint16_t cpu);           /* CV_CPU_TYPE_e: "x64", "Pentium III", "ARM64", ...; "Unknown" */
XXFC_API const char *xx_ms_pdb_register_to_string(uint16_t cpu, uint16_t reg); /* CV_HREG_e for x86 / x64 / ARM / Thumb / ARM64: "EBP", "RSP", "R0", ...; cpu = a CV_CPU_TYPE_e or a PE machine (0x1C0/0x1C2/0x1C4 ARM, 0xAA64/0xA641/0xA64E ARM64, else x86/x64); "Unknown" */

/* A parameter or local of a procedure. */
#define XX_MS_PDB_LOC_REGREL   1  /* S_REGREL32: [register + offset] */
#define XX_MS_PDB_LOC_BPREL    2  /* S_BPREL32: [frame + offset] */
#define XX_MS_PDB_LOC_REGISTER 3  /* S_REGISTER: in a register */
#define XX_MS_PDB_LOC_LOCAL    4  /* S_LOCAL (+ S_DEFRANGE_*): see def_ranges */
#define XX_MS_PDB_LOC_STATIC   5  /* S_LDATA32 / S_GDATA32 / S_LTHREAD32 inside the proc */
#define XX_MS_PDB_LOC_CONSTANT 6  /* S_CONSTANT inside the proc */

typedef struct xx_ms_pdb_def_range {
    uint16_t kind;             /* S_DEFRANGE_* symbol kind */
    uint16_t reg;              /* register (REGISTER / REGISTER_REL / SUBFIELD_REGISTER) or 0 */
    int32_t offset;            /* FRAMEPOINTER_REL(_FULL_SCOPE) / REGISTER_REL offset */
    uint64_t rva_start;        /* range start (XX_MS_PDB_NO_RVA for FULL_SCOPE) */
    uint32_t length;           /* range length (0 for FULL_SCOPE) */
    size_t gap_count;          /* gaps are not kept, only counted */
} xx_ms_pdb_def_range;

typedef struct xx_ms_pdb_local {
    uint16_t symbol_kind;
    uint8_t location;          /* XX_MS_PDB_LOC_* */
    const char *name;
    uint32_t type_index;
    bool is_param;             /* S_LOCAL: the fIsParam flag. S_REGREL32 / S_BPREL32 /
                                  S_REGISTER: true for the first N of them at depth 0,
                                  N = parameter count of the proc type (LF_PROCEDURE
                                  parmcount; LF_MFUNCTION parmcount + 1 when its this
                                  type is not 0) */
    uint16_t local_flags;      /* S_LOCAL CV_LVARFLAGS, else 0 */
    uint16_t reg;              /* REGREL / REGISTER register */
    int32_t offset;            /* REGREL / BPREL offset */
    uint64_t rva;              /* STATIC: address */
    int64_t value;             /* CONSTANT */
    int32_t depth;             /* 0 = proc scope, 1 = inside the first S_BLOCK32, ... */
    xx_ms_pdb_def_range *ranges; size_t range_count; /* LOCAL only */
} xx_ms_pdb_local;

/* A code range of an inline site, decoded from the binary annotations. */
typedef struct xx_ms_pdb_inline_range {
    uint32_t code_offset;      /* from the start of the outermost procedure */
    uint64_t rva;
    uint32_t length;
    int32_t line_offset;       /* line = inlinee start line (DEBUG_S_INLINEELINES) + this */
    uint32_t file_id;          /* checksum offset set by ChangeFile, UINT32_MAX = the
                                  inlinee's own file from DEBUG_S_INLINEELINES */
} xx_ms_pdb_inline_range;

typedef struct xx_ms_pdb_inline_site {
    uint32_t inlinee;          /* IPI id */
    const char *name;          /* LF_FUNC_ID / LF_MFUNC_ID name, "" if unknown */
    int32_t parent;            /* index of the enclosing inline site in the same proc, -1 */
    int32_t depth;             /* nesting of inline sites, 0 = directly in the proc */
    xx_ms_pdb_inline_range *ranges; size_t range_count;
} xx_ms_pdb_inline_site;

typedef struct xx_ms_pdb_proc_detail {
    uint16_t kind;             /* S_GPROC32 / S_LPROC32 / *_ID / S_GPROC32_DPC ... */
    const char *name;
    uint32_t type_index;       /* TPI type (for *_ID kinds the id was resolved to the type
                                  through LF_FUNC_ID / LF_MFUNC_ID) */
    uint32_t id_index;         /* *_ID kinds: the IPI id, else 0 */
    uint16_t segment;
    uint32_t offset;
    uint64_t rva;
    uint32_t code_size;
    uint32_t debug_start, debug_end;
    uint8_t proc_flags;        /* CV_PROCFLAGS */
    int64_t record_offset;     /* in the module stream */
    bool has_frame;            /* S_FRAMEPROC */
    uint32_t frame_size, frame_padding, frame_padding_offset, saved_regs_size;
    uint32_t frame_flags;
    uint16_t frame_local_base_reg, frame_param_base_reg; /* decoded from frame_flags bits 14-17, CV_AllReg encodings per machine (x64: 0 none,1 RSP,2 RBP,3 R13; x86: 1 VFRAME,2 EBP,3 EBX) */
    xx_ms_pdb_local *locals;   size_t local_count;
    xx_ms_pdb_inline_site *inlines; size_t inline_count;
    size_t block_count;        /* S_BLOCK32 count */
    size_t label_count;        /* S_LABEL32 count */
    bool has_callsite_info;
} xx_ms_pdb_proc_detail;

typedef struct xx_ms_pdb_thunk {
    const char *name;
    uint16_t segment; uint32_t offset; uint64_t rva;
    uint16_t length;
    uint8_t ordinal;           /* THUNK_ORDINAL */
} xx_ms_pdb_thunk;

typedef struct xx_ms_pdb_module_procs {
    int32_t module_index;
    xx_ms_pdb_proc_detail *procs; size_t proc_count;  /* stream order */
    xx_ms_pdb_thunk *thunks;      size_t thunk_count;
    void *internal_;
} xx_ms_pdb_module_procs;

/* Walks the module symbol stream once. Nested S_GPROC32 inside a proc (rare)
 * is reported as its own proc. Ends: S_END / S_PROC_ID_END / S_INLINESITE_END. */
XXFC_API bool xx_ms_pdb_get_module_procs(xx_ms_pdb *pdb, int32_t module_index,
                                         const xx_ms_pdb_addr_map *map,
                                         xx_ms_pdb_module_procs *out);
XXFC_API void xx_ms_pdb_module_procs_free(xx_ms_pdb_module_procs *procs);

/* ======================================================================
 * Part C (xx_microsoft_pdb_srcs.c): embedded sources, srcsrv, SourceLink,
 * FPO / frame data, OMAP
 * ====================================================================== */

/* /src/headerblock (SrcHeaderBlock {u32 version 19980827, u32 size, u64 FILETIME,
 * u32 age, u8 pad[44]} then a name->entry hash map in the named stream map layout
 * whose values are offsets of SrcHeaderBlockEntry {u32 size, u32 version, u32 crc,
 * u32 file_size, u32 file_ni, u32 obj_ni, u32 vfile_ni, u8 compression,
 * u8 is_virtual, u16 pad, u8 reserved[8]}); each file is the named stream
 * "/src/files/<virtual name, lower case>". */
typedef struct xx_ms_pdb_embedded_source {
    const char *file_name;     /* file_ni in /names */
    const char *object_name;   /* obj_ni */
    const char *virtual_name;  /* vfile_ni */
    const char *stream_name;   /* "/src/files/..." */
    int32_t stream_index;      /* -1 when that stream is missing */
    int64_t stream_size;
    uint32_t file_size;
    uint32_t crc;
    uint8_t compression;       /* 0 none; anything else: content is returned raw */
    bool is_virtual;
} xx_ms_pdb_embedded_source;

typedef struct xx_ms_pdb_embedded_source_list {
    xx_ms_pdb_embedded_source *items; size_t count;
    bool has_header_block;
    uint32_t header_version;
    uint64_t header_filetime;
    uint32_t header_age;
    void *internal_;
} xx_ms_pdb_embedded_source_list;

XXFC_API bool xx_ms_pdb_get_embedded_sources(xx_ms_pdb *pdb, const xx_ms_pdb_names *names,
                                             xx_ms_pdb_embedded_source_list *out);
XXFC_API void xx_ms_pdb_embedded_source_list_free(xx_ms_pdb_embedded_source_list *list);
/* Also lists natvis and other "/src/files/" or "/natvis/" named streams that have
 * no header block entry, with only stream_name / stream_index / stream_size set. */

/* Whole content of a named stream (srcsrv, sourcelink, ...), NUL appended.
 * false when missing; *data freed with xx_mem_free. */
XXFC_API bool xx_ms_pdb_read_named_stream(xx_ms_pdb *pdb, const char *name, uint8_t **data, size_t *size);

/* The source server stream "srcsrv" (text, INI-like sections SRCSRV: ini /
 * variables / source files / end). */
typedef struct xx_ms_pdb_srcsrv_file {
    const char *line;          /* the raw "path*var2*var3..." line */
    const char *path;          /* var1 */
    size_t var_count; const char **vars; /* var1..varN as split by '*' */
    const char *target;        /* SRCSRVTRG expanded (%varN%, %targ%, %fnbksl%(..),
                                  %fnfile%(..), %fnvar%(..), variables of the
                                  variables section; unknown ones kept verbatim) */
    const char *command;       /* SRCSRVCMD expanded the same way, "" if none */
} xx_ms_pdb_srcsrv_file;

typedef struct xx_ms_pdb_srcsrv {
    bool is_valid;
    xx_ms_pdb_env_pair *ini;       size_t ini_count;
    xx_ms_pdb_env_pair *variables; size_t variable_count;
    xx_ms_pdb_srcsrv_file *files;  size_t file_count;
    void *internal_;
} xx_ms_pdb_srcsrv;

XXFC_API bool xx_ms_pdb_parse_srcsrv(const char *text, size_t size, xx_ms_pdb_srcsrv *out);
XXFC_API void xx_ms_pdb_srcsrv_free(xx_ms_pdb_srcsrv *srcsrv);

/* "sourcelink" (and "sourcelink$1", ...) streams: JSON {"documents": {pattern: url}}.
 * Only the document map is decoded; patterns may end in '*' which matches the rest. */
typedef struct xx_ms_pdb_sourcelink {
    xx_ms_pdb_env_pair *documents; size_t document_count; /* key = pattern, value = url */
    size_t stream_count;
    void *internal_;
} xx_ms_pdb_sourcelink;
XXFC_API bool xx_ms_pdb_get_sourcelink(xx_ms_pdb *pdb, xx_ms_pdb_sourcelink *out);
XXFC_API void xx_ms_pdb_sourcelink_free(xx_ms_pdb_sourcelink *sl);
/* path -> url with the first matching pattern (case-insensitive, '\\' and '/'
 * equal, the '*' part substituted with '\\' turned into '/'); NULL if none.
 * Returned string freed with xx_mem_free. */
XXFC_API char *xx_ms_pdb_sourcelink_map(const xx_ms_pdb_sourcelink *sl, const char *path);

/* FPO_DATA (DBI optional debug stream 0, 16 bytes each). */
typedef struct xx_ms_pdb_fpo {
    uint32_t rva_start, proc_size, locals_dwords;
    uint16_t params_dwords;
    uint8_t prolog_size, saved_regs, has_seh, use_bp, frame_type;
} xx_ms_pdb_fpo;

/* FRAMEDATA (DBI optional debug stream 9, 32 bytes each; when the stream size
 * is not a multiple of 32 the first u32 is a relocation pointer and skipped). */
typedef struct xx_ms_pdb_frame_data {
    uint32_t rva_start, code_size, locals_size, params_size, max_stack_size;
    uint32_t frame_func;       /* offset in /names of the program string */
    const char *program;       /* that string, "" if none */
    uint16_t prolog_size, saved_regs_size;
    bool has_seh, has_eh, is_function_start;
} xx_ms_pdb_frame_data;

typedef struct xx_ms_pdb_frame_info {
    xx_ms_pdb_fpo *fpo;               size_t fpo_count;
    xx_ms_pdb_frame_data *frames;     size_t frame_count;
    void *internal_;
} xx_ms_pdb_frame_info;

/* rvas as stored (pre-OMAP when the PDB has OMAP; map them with
 * xx_ms_pdb_addr_map_omap). */
XXFC_API bool xx_ms_pdb_get_frame_info(xx_ms_pdb *pdb, const xx_ms_pdb_names *names,
                                       xx_ms_pdb_frame_info *out);
XXFC_API void xx_ms_pdb_frame_info_free(xx_ms_pdb_frame_info *info);

/* ======================================================================
 * Part E: generic decoding of everything: stream walker xx_ms_pdb_dump
 * (xx_microsoft_pdb_dump.c), record decoders (xx_microsoft_pdb_decode.c)
 * ====================================================================== */

/* A dump is a stream of events: BEGIN opens a named object (a stream, a
 * record, a field list member, an array), FIELD is one named value inside the
 * innermost open object, END closes it. Every BEGIN has its END, also when the
 * data is cut (then a FIELD "error" with a string value says why first). */
#define XX_MS_PDB_EV_BEGIN 1
#define XX_MS_PDB_EV_FIELD 2
#define XX_MS_PDB_EV_END   3

#define XX_MS_PDB_FV_NONE     0  /* BEGIN / END */
#define XX_MS_PDB_FV_UINT     1  /* u: decimal */
#define XX_MS_PDB_FV_INT      2  /* i */
#define XX_MS_PDB_FV_HEX      3  /* u: shown as hex (offsets, rvas, flags, signatures) */
#define XX_MS_PDB_FV_STRING   4  /* s (UTF-8 as stored, NUL-terminated) */
#define XX_MS_PDB_FV_BOOL     5  /* u 0 / 1 */
#define XX_MS_PDB_FV_TYPE     6  /* u: a TPI type index; s: its short text when known ("int", "0x1003") */
#define XX_MS_PDB_FV_ID       7  /* u: an IPI id index; s like TYPE */
#define XX_MS_PDB_FV_ENUM     8  /* u: raw value; s: its name ("S_GPROC32", "LF_CLASS", "RBP", "C++", ...) */
#define XX_MS_PDB_FV_BYTES    9  /* bytes / size (GUIDs as s instead; raw blobs at most 4096 bytes, size
                                    is the full length, bytes the first min(size, 4096)) */
#define XX_MS_PDB_FV_FLAGS   10  /* u: raw bits; s: the set flag names joined with " | " */
#define XX_MS_PDB_FV_GUID    11  /* s: "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" */
#define XX_MS_PDB_FV_FLOAT   12  /* d */

typedef struct xx_ms_pdb_dump_event {
    uint8_t event;          /* XX_MS_PDB_EV_* */
    uint8_t value_kind;     /* XX_MS_PDB_FV_* */
    int32_t depth;          /* nesting depth of the event (BEGIN at d -> its fields at d + 1) */
    const char *name;       /* object / field name, never NULL: snake_case ("type_index", "name") */
    uint64_t u;
    int64_t i;
    double d;
    const char *s;          /* never NULL ("" when unused) */
    const uint8_t *bytes;
    size_t size;
    int64_t offset;         /* BEGIN of a record: its offset in the stream, else -1 */
    int32_t stream;         /* BEGIN: stream index the object lives in, else -1 */
} xx_ms_pdb_dump_event;

/* Return false to stop the dump. Strings and bytes are valid during the call only. */
typedef bool (*xx_ms_pdb_dump_fn)(void *user, const xx_ms_pdb_dump_event *event);

/* What to dump (any combination); XX_MS_PDB_DUMP_ALL = everything. */
#define XX_MS_PDB_DUMP_MSF          0x00000001u /* superblock, stream directory (sizes, page counts) */
#define XX_MS_PDB_DUMP_INFO         0x00000002u /* PDB info stream: version, signature, age, guid,
                                                   named stream map, feature codes */
#define XX_MS_PDB_DUMP_NAMES        0x00000004u /* /names: header, every string with its offset, hash buckets count */
#define XX_MS_PDB_DUMP_TPI          0x00000008u /* TPI header + every type record, all fields, field lists
                                                   member by member, method lists, arg lists, vftable shapes ... */
#define XX_MS_PDB_DUMP_IPI          0x00000010u /* IPI header + every id record */
#define XX_MS_PDB_DUMP_TPI_HASH     0x00000020u /* TPI/IPI hash stream: hash values, index offsets, hash adjusters */
#define XX_MS_PDB_DUMP_DBI          0x00000040u /* DBI header, module infos, section contributions, section map,
                                                   file info (source files per module), type server map,
                                                   EC names, optional debug header stream list */
#define XX_MS_PDB_DUMP_DBG_STREAMS  0x00000080u /* each optional debug stream: section headers (both),
                                                   FPO, frame data, OMAP to / from, fixups, pdata / xdata
                                                   entries, token rid map, exception data */
#define XX_MS_PDB_DUMP_MODULES      0x00000100u /* every module stream: all symbol records, every C13
                                                   subsection (lines, checksums, inlinee lines, frame data,
                                                   cross scope imports / exports, IL lines, md token maps,
                                                   merged assembly input, COFF symbol rvas, string table),
                                                   C11 lines, global refs */
#define XX_MS_PDB_DUMP_SYMBOLS      0x00000200u /* the global symbol record stream, every record */
#define XX_MS_PDB_DUMP_GLOBALS      0x00000400u /* GSI hash (globals stream): header, hash records, buckets */
#define XX_MS_PDB_DUMP_PUBLICS      0x00000800u /* PSGSI: header, hash, address map, thunk map, section map */
#define XX_MS_PDB_DUMP_OTHER        0x00001000u /* other named streams: /LinkInfo, /TMCache, /UDTSRCLINEUNDONE,
                                                   /src/headerblock, sourcelink, srcsrv, natvis ... (text as
                                                   STRING, binary as BYTES, known layouts decoded) */
#define XX_MS_PDB_DUMP_ALL          0x00001FFFu

/* Walks the PDB (MSF 7.00; JG files: what the base API can read; portable PDBs: nothing).
 * names may be NULL. Returns false only for a bad argument or when fn stopped it. */
XXFC_API bool xx_ms_pdb_dump(xx_ms_pdb *pdb, uint32_t what, const xx_ms_pdb_names *names,
                             xx_ms_pdb_dump_fn fn, void *user);

/* Decoders of single records (payload = after the u16 length + u16 kind header). They emit the record's
 * fields (no BEGIN / END for the record itself, nested objects get their own BEGIN / END) at depth.
 * Every CodeView symbol kind of cvinfo.h (S_*, incl. ST and 16t forms) and every leaf kind (LF_*, incl.
 * field list members, numeric leaves, 16t forms) is known by name; kinds without a decoder emit a BYTES
 * field "data". false = unknown kind (still emitted as bytes). */
XXFC_API bool xx_ms_pdb_decode_symbol(uint16_t kind, const uint8_t *payload, size_t size, uint16_t machine,
                                      int32_t depth, xx_ms_pdb_dump_fn fn, void *user);
XXFC_API bool xx_ms_pdb_decode_leaf(uint16_t kind, const uint8_t *payload, size_t size,
                                    int32_t depth, xx_ms_pdb_dump_fn fn, void *user);
XXFC_API const char *xx_ms_pdb_dump_section_name(uint32_t what_bit); /* "msf", "info", "names", ... */

/* A portable (ECMA-335 "BSJB") PDB, everything in it with the same events
 * (xx_microsoft_pdb_portable_dump.c): metadata root, stream headers, #Pdb
 * stream, #~ table header, every row of every present table with every
 * column decoded (Document: name, hash algorithm GUID name, hash, language;
 * MethodDebugInformation: sequence points decoded, document; LocalScope,
 * LocalVariable, LocalConstant (signature blob decoded: type + value),
 * ImportScope (imports blob decoded: kind, alias, target namespace /
 * assembly ref / type), StateMachineMethod, CustomDebugInformation (kind GUID
 * named: SourceLink as text, EmbeddedSource inflated when compressed (first
 * 64 KiB as STRING when text), CompilationOptions / CompilationMetadataReferences
 * decoded, others as BYTES), and the referenced type-system tables' row
 * counts), the #Strings, #US, #Blob and #GUID heaps (sizes; #GUID entries).
 * false = not a portable PDB or fn stopped it. */
XXFC_API bool xx_ms_pdb_portable_dump(xx_io_device *device, xx_ms_pdb_dump_fn fn, void *user);
/* The embedded source of each document (CustomDebugInformation EmbeddedSource,
 * inflated): calls fn(user, document name, data, size) per file; false = not
 * portable. fn returns false to stop. */
typedef bool (*xx_ms_pdb_portable_source_fn)(void *user, const char *name, const uint8_t *data, size_t size);
XXFC_API bool xx_ms_pdb_portable_embedded_sources(xx_io_device *device, xx_ms_pdb_portable_source_fn fn, void *user);

#ifdef __cplusplus
}
#endif

#endif
