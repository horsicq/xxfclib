/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_WOW64_HEAP_VS_CHUNK_LAYOUTS_H
#define XX_PROCESS_WOW64_HEAP_VS_CHUNK_LAYOUTS_H

/* Private factual WOW64 guest metadata from the exact Microsoft public NTDLL
 * PDBs below. Include after XX_PSW_*, xx_psw_nt_layout and the full WOW64
 * SEGMENT_HEAP profiles. No native structure is resized to infer a guest ABI.
 *
 * https://msdl.microsoft.com/download/symbols/wntdll.pdb/7C215E43AA974FFF5C173AB29D5A5EFB1/wntdll.pdb
 *   WOW64 NTDLL 10.0.19041.2965 on OS 10.0.19045; PE RSDS/DBI age1.
 * https://msdl.microsoft.com/download/symbols/wntdll.pdb/259160B822D032AC486248BAFFE15E011/wntdll.pdb
 *   WOW64 NTDLL 10.0.26100.1591 on OS 10.0.26100; PE RSDS/DBI age1.
 * https://github.com/microsoft/microsoft-pdb/blob/master/include/cvinfo.h
 * https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/shared/ntdef.h
 * The Microsoft SDK independently defines RTL_BALANCED_NODE parent mask3,
 * Red bit0 and Balance bits0..1 as overlapping views of UCHAR storage.
 *
 * All top-level and inline named union/structure/bitfield members are present.
 * Names stay exact for navigation; STORED_ENCODED container/storage types and
 * profile labels identify raw encoded aliases. The named bit views describe
 * bits of stored bytes, not decoded allocation size, status or usable offset.
 * No SIZE/POINTER/VIRTUAL_ADDRESS property is assigned, and no byte units,
 * header decryption, allocation graph or chunk stepping is inferred. Caller
 * presentation should retain this stored-data qualification on raw aliases.
 * BitfieldStorage rows explicitly expose unnamed physical storage units.
 *
 * Both guest profiles have an 8-byte CHUNK_HEADER and 16-byte FREE_HEADER.
 * FREE_HEADER.Node starts at+4 and overlaps Header.AllocatedChunkBits; its
 * ParentValue is tagged storage, not a directly followable parent address.
 * Type sizes do not prove where the first chunk follows a VS_SUBSEGMENT or
 * whether a particular chunk is allocated/free; discovery establishes those
 * facts separately before selecting a typed view. Selectors accept only the
 * exact previously identity-gated WOW64 parent arrays at target width 4.
 * All metadata strings are static. This header reads no target memory.
 */

static const xx_data_struct_field_desc xx_pswhvw_header32_19045[] = {
    XX_PSW_FIELD("Sizes", "STORED_ENCODED_HEAP_VS_CHUNK_HEADER_SIZE32_19045[1]", 0x0, 4, 0),
    XX_PSW_FIELD("Sizes.MemoryCost", "bitfield:0:1", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.UnsafeSize", "bitfield:1:15", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.UnsafePrevSize", "bitfield:16:15", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.Allocated", "bitfield:31:1", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.KeyUShort", "STORED_ENCODED_UINT16", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.KeyULong", "STORED_ENCODED_UINT32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.HeaderBits", "STORED_ENCODED_UINT32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("EncodedSegmentPageOffset", "bitfield:0:8", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("UnusedBytes", "bitfield:8:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("SkipDuringWalk", "bitfield:9:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare", "bitfield:10:22", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AllocatedChunkBits", "STORED_ENCODED_UINT32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhvw_free32_19045[] = {
    XX_PSW_FIELD("Header", "STORED_ENCODED_HEAP_VS_CHUNK_HEADER32_19045[1]", 0x0, 8, 0),
    XX_PSW_FIELD("Header.Sizes", "STORED_ENCODED_HEAP_VS_CHUNK_HEADER_SIZE32_19045[1]", 0x0, 4, 0),
    XX_PSW_FIELD("Header.Sizes.MemoryCost", "bitfield:0:1", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.UnsafeSize", "bitfield:1:15", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.UnsafePrevSize", "bitfield:16:15", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.Allocated", "bitfield:31:1", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.KeyUShort", "STORED_ENCODED_UINT16", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.KeyULong", "STORED_ENCODED_UINT32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.HeaderBits", "STORED_ENCODED_UINT32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.EncodedSegmentPageOffset", "bitfield:0:8", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.UnusedBytes", "bitfield:8:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.SkipDuringWalk", "bitfield:9:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Spare", "bitfield:10:22", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.AllocatedChunkBits", "STORED_ENCODED_UINT32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("OverlapsHeader", "STORED_OVERLAPPING_UINT32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node", "STORED_RTL_BALANCED_NODE32_19045[1]", 0x4, 12, 0),
    XX_PSW_FIELD("Node.BitfieldStorage", "STORED_UINT8", 0xC, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Children", "STORED_NODE_PTR32[2]", 0x4, 8, 0),
    XX_PSW_FIELD("Node.Children[0]", "STORED_NODE_PTR32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Children[1]", "STORED_NODE_PTR32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Left", "STORED_NODE_PTR32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Right", "STORED_NODE_PTR32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Red", "bitfield:0:1", 0xC, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Balance", "bitfield:0:2", 0xC, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.ParentValue", "STORED_TAGGED_UINT32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhvw_header32_26100[] = {
    XX_PSW_FIELD("Sizes", "STORED_ENCODED_HEAP_VS_CHUNK_HEADER_SIZE32_26100[1]", 0x0, 4, 0),
    XX_PSW_FIELD("Sizes.MemoryCost", "bitfield:0:1", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.UnsafeSize", "bitfield:1:15", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.UnsafePrevSize", "bitfield:16:15", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.Allocated", "bitfield:31:1", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.KeyUShort", "STORED_ENCODED_UINT16", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.KeyULong", "STORED_ENCODED_UINT32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Sizes.HeaderBits", "STORED_ENCODED_UINT32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("EncodedSegmentPageOffset", "bitfield:0:8", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("UnusedBytes", "bitfield:8:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("SkipDuringWalk", "bitfield:9:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare", "bitfield:10:22", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AllocatedChunkBits", "STORED_ENCODED_UINT32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhvw_free32_26100[] = {
    XX_PSW_FIELD("Header", "STORED_ENCODED_HEAP_VS_CHUNK_HEADER32_26100[1]", 0x0, 8, 0),
    XX_PSW_FIELD("Header.Sizes", "STORED_ENCODED_HEAP_VS_CHUNK_HEADER_SIZE32_26100[1]", 0x0, 4, 0),
    XX_PSW_FIELD("Header.Sizes.MemoryCost", "bitfield:0:1", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.UnsafeSize", "bitfield:1:15", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.UnsafePrevSize", "bitfield:16:15", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.Allocated", "bitfield:31:1", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.KeyUShort", "STORED_ENCODED_UINT16", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.KeyULong", "STORED_ENCODED_UINT32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Sizes.HeaderBits", "STORED_ENCODED_UINT32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.EncodedSegmentPageOffset", "bitfield:0:8", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.UnusedBytes", "bitfield:8:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.SkipDuringWalk", "bitfield:9:1", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.Spare", "bitfield:10:22", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Header.AllocatedChunkBits", "STORED_ENCODED_UINT32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("OverlapsHeader", "STORED_OVERLAPPING_UINT32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node", "STORED_RTL_BALANCED_NODE32_26100[1]", 0x4, 12, 0),
    XX_PSW_FIELD("Node.BitfieldStorage", "STORED_UINT8", 0xC, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Children", "STORED_NODE_PTR32[2]", 0x4, 8, 0),
    XX_PSW_FIELD("Node.Children[0]", "STORED_NODE_PTR32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Children[1]", "STORED_NODE_PTR32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Left", "STORED_NODE_PTR32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Right", "STORED_NODE_PTR32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Red", "bitfield:0:1", 0xC, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.Balance", "bitfield:0:2", 0xC, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Node.ParentValue", "STORED_TAGGED_UINT32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_psw_nt_layout xx_pswhvw_profiles_19045[] = {
    {19045, 0x014c, "WOW64 HEAP_VS_CHUNK_HEADER32; stored encoded aliases, not decoded", xx_pswhvw_header32_19045, XX_PSW_COUNT(xx_pswhvw_header32_19045), 0x8u},
    {19045, 0x014c, "WOW64 HEAP_VS_CHUNK_FREE_HEADER32; stored encoded aliases, not decoded", xx_pswhvw_free32_19045, XX_PSW_COUNT(xx_pswhvw_free32_19045), 0x10u},
};

static const xx_psw_nt_layout xx_pswhvw_profiles_26100[] = {
    {26100, 0x014c, "WOW64 HEAP_VS_CHUNK_HEADER32; stored encoded aliases, not decoded", xx_pswhvw_header32_26100, XX_PSW_COUNT(xx_pswhvw_header32_26100), 0x8u},
    {26100, 0x014c, "WOW64 HEAP_VS_CHUNK_FREE_HEADER32; stored encoded aliases, not decoded", xx_pswhvw_free32_26100, XX_PSW_COUNT(xx_pswhvw_free32_26100), 0x10u},
};

static const xx_psw_nt_layout *xx_pswhvw_profile(const xx_psw_nt_layout *parent, unsigned width, size_t kind)
{
    if (!parent || width != 4 || kind >= XX_PSW_COUNT(xx_pswhvw_profiles_19045)) return NULL;
    if (parent->fields == xx_pswh_wow_segment32_19045) return xx_pswhvw_profiles_19045 + kind;
    if (parent->fields == xx_pswh_wow_segment32_26100) return xx_pswhvw_profiles_26100 + kind;
    return NULL;
}
static const xx_psw_nt_layout *xx_pswhvc_wow_header(const xx_psw_nt_layout *parent, unsigned width)
{
    return xx_pswhvw_profile(parent, width, 0);
}
static const xx_psw_nt_layout *xx_pswhvc_wow_free(const xx_psw_nt_layout *parent, unsigned width)
{
    return xx_pswhvw_profile(parent, width, 1);
}

#endif /* XX_PROCESS_WOW64_HEAP_VS_CHUNK_LAYOUTS_H */
