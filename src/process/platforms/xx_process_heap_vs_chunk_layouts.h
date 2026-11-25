/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_HEAP_VS_CHUNK_LAYOUTS_H
#define XX_PROCESS_HEAP_VS_CHUNK_LAYOUTS_H

/*
 * Original native target metadata for Windows 10 22H2 native I386/AMD64 and
 * Windows 11 24H2 AMD64, consulted 2026-10-20. Include after the complete
 * native SEGMENT_HEAP profiles and XX_PSW_* descriptor macros.
 *
 * Primary symbol-derived offsets, extents, named fields and declared widths:
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_HEAP_VS_CHUNK_HEADER
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_HEAP_VS_CHUNK_HEADER_SIZE
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_HEAP_VS_CHUNK_FREE_HEADER
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_RTL_BALANCED_NODE
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_HEAP_VS_CHUNK_HEADER
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_HEAP_VS_CHUNK_HEADER_SIZE
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_HEAP_VS_CHUNK_FREE_HEADER
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_RTL_BALANCED_NODE
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_HEAP_VS_CHUNK_HEADER
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_HEAP_VS_CHUNK_HEADER_SIZE
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_HEAP_VS_CHUNK_FREE_HEADER
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_RTL_BALANCED_NODE
 *
 * Exact AMD64 bit positions are additionally read from complete Microsoft
 * public PDB type records selected by matching PE CodeView GUID/age1:
 * ntdll component 19041.2788 (OS family 19045):
 * https://msdl.microsoft.com/download/symbols/ntdll.pdb/76C7BBFD419865CA30108FE0023CF0401/ntdll.pdb
 * ntdll component 26100.1591 (OS family 26100):
 * https://msdl.microsoft.com/download/symbols/ntdll.pdb/219D0663E9DBAD581E64D9EC8618F5421/ntdll.pdb
 * Data-only inspection confirmed the same AMD64 chunk layouts in both PDBs.
 * These corroborate the existing native build profiles; selectors retain their
 * existing exact parent-array gates, not a new arbitrary module-family guess.
 * The 19041 symbol-server PE artifact is identified by machine,
 * timestamp/SizeOfImage and matching CodeView identity; its fetched SHA-256
 * differs from Winbindex's catalog hash, and both hashes are retained in the
 * accompanying source-data identity manifest. It is not claimed hash-equal.
 * PDB Info stream ages3/4 are distinct from their PE/DBI symbol-server age1.
 *
 * RTL_BALANCED_NODE Red and Balance overlap at bit0, rather than occupying
 * sequential bits. Both the PDBs and this primary source confirm the union:
 * https://raw.githubusercontent.com/winsiderss/systeminformer/master/phnt/include/phnt_ntdef.h
 * The separate allocated-header word's sequential bitfield positions follow
 * its explicit native symbol declaration and the documented Windows ABI:
 * https://learn.microsoft.com/cpp/c-language/c-bit-fields
 *
 * Native I386 HEADER_SIZE positions are not established by the reconstructed
 * symbol declaration: it reports widths1/15/15/1 but not their bit positions.
 * Preserve every name as a position-unverified opaque alias of its verified
 * 4-byte physical ULONG storage. No bit positions are borrowed from WOW64 or
 * AMD64. Exact scalar KeyUShort/KeyULong/HeaderBits spans remain available.
 *
 * All Sizes and EncodedSegmentPageOffset views are stored encoded overlays,
 * not decoded allocation sizes/offsets/status. Bitfield values, where proven,
 * are extracted only from stored bits; raw retains the complete physical word.
 * No descriptor has SIZE, POINTER or VIRTUAL_ADDRESS properties. Tree links
 * and ParentValue are stored data; traversal requires separate validation of
 * the selected context and tree's encoding state. ParentValue includes tags.
 * Free Header and Node intentionally overlap; both full union views are kept.
 * Children[2] retains its full span and named element aliases. *Storage rows
 * are explicit physical-storage aliases for named bitfields.
 * No chunk arithmetic, first-chunk placement or heap-key decoding is inferred.
 * No third-party declarations, implementation code or prose were copied.
 */

static const xx_data_struct_field_desc xx_pswhvn_header32_19045[] = {
    XX_PSW_FIELD("Sizes", "opaque encoded HEAP_VS_CHUNK_HEADER_SIZE32[1]", 0x0, 4, 0),
    XX_PSW_FIELD("Sizes.BitfieldStorageLow", "stored encoded ULONG", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.MemoryCost", "opaque encoded ULONG bitfield(width=1,position-unverified)", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.UnsafeSize", "opaque encoded ULONG bitfield(width=15,position-unverified)", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.UnsafePrevSize", "opaque encoded ULONG bitfield(width=15,position-unverified)", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.Allocated", "opaque encoded ULONG bitfield(width=1,position-unverified)", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.KeyUShort", "stored encoded USHORT", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.KeyULong", "stored encoded ULONG", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.HeaderBits", "stored encoded ULONG", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AllocatedChunkBits", "stored ULONG", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("EncodedSegmentPageOffset", "bitfield:0:8", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("UnusedBytes", "bitfield:8:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("SkipDuringWalk", "bitfield:9:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare", "bitfield:10:22", 0x4, 4, XX_PSW_RESERVED),
};

static const xx_data_struct_field_desc xx_pswhvn_header64[] = {
    XX_PSW_FIELD("Sizes", "opaque encoded HEAP_VS_CHUNK_HEADER_SIZE64[1]", 0x0, 8, 0),
    XX_PSW_FIELD("Sizes.BitfieldStorageLow", "stored encoded ULONG", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.BitfieldStorageHigh", "stored encoded ULONG", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.MemoryCost", "bitfield:0:16", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.UnsafeSize", "bitfield:16:16", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.UnsafePrevSize", "bitfield:0:16", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.Allocated", "bitfield:16:8", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.KeyUShort", "stored encoded USHORT", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.KeyULong", "stored encoded ULONG", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.HeaderBits", "stored encoded ULONGLONG", 0x0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AllocatedChunkBits", "stored ULONG", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("EncodedSegmentPageOffset", "bitfield:0:8", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("UnusedBytes", "bitfield:8:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("SkipDuringWalk", "bitfield:9:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare", "bitfield:10:22", 0x8, 4, XX_PSW_RESERVED),
};

static const xx_data_struct_field_desc xx_pswhvn_free32_19045[] = {
    XX_PSW_FIELD("Header", "opaque stored HEAP_VS_CHUNK_HEADER32[1]", 0x0, 8, 0),
    XX_PSW_FIELD("Header.Sizes", "opaque encoded HEAP_VS_CHUNK_HEADER_SIZE32[1]", 0x0, 4, 0),
    XX_PSW_FIELD("Header.Sizes.BitfieldStorageLow", "stored encoded ULONG", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.MemoryCost", "opaque encoded ULONG bitfield(width=1,position-unverified)", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.UnsafeSize", "opaque encoded ULONG bitfield(width=15,position-unverified)", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.UnsafePrevSize", "opaque encoded ULONG bitfield(width=15,position-unverified)", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.Allocated", "opaque encoded ULONG bitfield(width=1,position-unverified)", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.KeyUShort", "stored encoded USHORT", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.KeyULong", "stored encoded ULONG", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.HeaderBits", "stored encoded ULONG", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.AllocatedChunkBits", "stored ULONG", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.EncodedSegmentPageOffset", "bitfield:0:8", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.UnusedBytes", "bitfield:8:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.SkipDuringWalk", "bitfield:9:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Spare", "bitfield:10:22", 0x4, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("OverlapsHeader", "stored encoded ULONG", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node", "stored RTL_BALANCED_NODE32[1]", 0x4, 12, 0),
    XX_PSW_FIELD("Node.Children", "STORED_NODE32[2]", 0x4, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Children[0]", "stored PRTL_BALANCED_NODE32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Children[1]", "stored PRTL_BALANCED_NODE32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Left", "stored PRTL_BALANCED_NODE32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Right", "stored PRTL_BALANCED_NODE32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.BitfieldStorage", "UCHAR", 0xc, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Red", "bitfield:0:1", 0xc, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Balance", "bitfield:0:2", 0xc, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.ParentValue", "stored tagged ULONG_PTR32", 0xc, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhvn_free64[] = {
    XX_PSW_FIELD("Header", "opaque stored HEAP_VS_CHUNK_HEADER64[1]", 0x0, 16, 0),
    XX_PSW_FIELD("Header.Sizes", "opaque encoded HEAP_VS_CHUNK_HEADER_SIZE64[1]", 0x0, 8, 0),
    XX_PSW_FIELD("Header.Sizes.BitfieldStorageLow", "stored encoded ULONG", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.BitfieldStorageHigh", "stored encoded ULONG", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.MemoryCost", "bitfield:0:16", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.UnsafeSize", "bitfield:16:16", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.UnsafePrevSize", "bitfield:0:16", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.Allocated", "bitfield:16:8", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.KeyUShort", "stored encoded USHORT", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.KeyULong", "stored encoded ULONG", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.HeaderBits", "stored encoded ULONGLONG", 0x0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.AllocatedChunkBits", "stored ULONG", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.EncodedSegmentPageOffset", "bitfield:0:8", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.UnusedBytes", "bitfield:8:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.SkipDuringWalk", "bitfield:9:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Spare", "bitfield:10:22", 0x8, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("OverlapsHeader", "stored encoded ULONGLONG", 0x0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node", "stored RTL_BALANCED_NODE64[1]", 0x8, 24, 0),
    XX_PSW_FIELD("Node.Children", "STORED_NODE64[2]", 0x8, 16, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Children[0]", "stored PRTL_BALANCED_NODE64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Children[1]", "stored PRTL_BALANCED_NODE64", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Left", "stored PRTL_BALANCED_NODE64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Right", "stored PRTL_BALANCED_NODE64", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.BitfieldStorage", "UCHAR", 0x18, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Red", "bitfield:0:1", 0x18, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Balance", "bitfield:0:2", 0x18, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.ParentValue", "stored tagged ULONG_PTR64", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static int xx_pswhvn_index(const xx_psw_nt_layout *parent, unsigned width)
{
    if (!parent) return -1;
    if (width == 4 && parent->fields == xx_pswh_segment32_19045) return 0;
    if (width == 8 && parent->fields == xx_pswh_segment64_19045) return 1;
    if (width == 8 && parent->fields == xx_pswh_segment64_26100) return 2;
    return -1;
}

static const xx_psw_nt_layout xx_pswhvn_header_profiles[] = {
    {19045, 0x014c, "Native I386 HEAP_VS_CHUNK_HEADER; stored encoded overlays; HeaderSize bit positions unverified", xx_pswhvn_header32_19045,
     XX_PSW_COUNT(xx_pswhvn_header32_19045), 0x8},
    {19045, 0x8664, "Native AMD64 19045 HEAP_VS_CHUNK_HEADER; stored encoded overlays, not decoded allocation fields", xx_pswhvn_header64,
     XX_PSW_COUNT(xx_pswhvn_header64), 0x10},
    {26100, 0x8664, "Native AMD64 26100 HEAP_VS_CHUNK_HEADER; stored encoded overlays, not decoded allocation fields", xx_pswhvn_header64,
     XX_PSW_COUNT(xx_pswhvn_header64), 0x10}};

static const xx_psw_nt_layout xx_pswhvn_free_profiles[] = {
    {19045, 0x014c, "Native I386 HEAP_VS_CHUNK_FREE_HEADER; stored overlays; HeaderSize bit positions unverified", xx_pswhvn_free32_19045,
     XX_PSW_COUNT(xx_pswhvn_free32_19045), 0x10},
    {19045, 0x8664, "Native AMD64 19045 HEAP_VS_CHUNK_FREE_HEADER; stored encoded/tree overlays", xx_pswhvn_free64, XX_PSW_COUNT(xx_pswhvn_free64), 0x20},
    {26100, 0x8664, "Native AMD64 26100 HEAP_VS_CHUNK_FREE_HEADER; stored encoded/tree overlays", xx_pswhvn_free64, XX_PSW_COUNT(xx_pswhvn_free64), 0x20}};

static const xx_psw_nt_layout *xx_pswhvc_native_header(const xx_psw_nt_layout *parent, unsigned width)
{
    int index = xx_pswhvn_index(parent, width);
    return index < 0 ? NULL : xx_pswhvn_header_profiles + index;
}

static const xx_psw_nt_layout *xx_pswhvc_native_free(const xx_psw_nt_layout *parent, unsigned width)
{
    int index = xx_pswhvn_index(parent, width);
    return index < 0 ? NULL : xx_pswhvn_free_profiles + index;
}

#endif /* XX_PROCESS_HEAP_VS_CHUNK_LAYOUTS_H */
