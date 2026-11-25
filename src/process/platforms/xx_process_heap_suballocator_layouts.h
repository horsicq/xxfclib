/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/*
 * Original static target-layout metadata: native I386 Windows 10 22H2 (19045),
 * AMD64 Windows 10 22H2 and Windows 11 24H2 (26100), consulted 2026-10-20.
 * Only factual member names, offsets, sizes and bit ranges were transcribed.
 * No third-party declarations, implementation code or explanatory prose copied.
 * Include after the full SEGMENT_HEAP profiles and XX_PSW_* macros.
 *
 * Primary symbol-derived pages for each of the five top-level types:
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_HEAP_VS_SUBSEGMENT
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_HEAP_VS_SUBSEGMENT
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_HEAP_VS_SUBSEGMENT
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_HEAP_LFH_SUBSEGMENT
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_HEAP_LFH_SUBSEGMENT
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_HEAP_LFH_SUBSEGMENT
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_HEAP_LFH_BUCKET
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_HEAP_LFH_BUCKET
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_HEAP_LFH_BUCKET
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_HEAP_LFH_SUBSEGMENT_OWNER
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_HEAP_LFH_SUBSEGMENT_OWNER
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_HEAP_LFH_SUBSEGMENT_OWNER
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_HEAP_LFH_AFFINITY_SLOT
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_HEAP_LFH_AFFINITY_SLOT
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_HEAP_LFH_AFFINITY_SLOT
 *
 * Inline helper types were independently checked on the corresponding version
 * pages: _LIST_ENTRY, _SINGLE_LIST_ENTRY, _HEAP_LFH_SUBSEGMENT_DELAY_FREE,
 * _HEAP_LFH_SUBSEGMENT_ENCODED_OFFSETS, _HEAP_LFH_FAST_REF; and for 24H2:
 * _HEAP_LFH_SUBSEGMENT_STATE, _HEAP_LFH_BLOCK_LIST, _HEAP_LFH_BLOCK_SLIST,
 * _HEAP_LFH_COMMIT_UNIT_INFO, _HEAP_LFH_SUBSEGMENT_UCHAR_FIELDS,
 * _HEAP_LFH_PTRREF_LIST.
 *
 * Union aliases intentionally overlap. *Storage names describe physical parent
 * storage for named bitfields. Opaque offsets, tagged fast references and list
 * storage do not claim to be direct usable target pointers. Allocator-specific
 * Size/shift/offset values are scalar; byte units are not inferred.
 * BlockBitmap[1] describes the published fixed prefix, not an inferred dynamic
 * allocation bitmap extent. Selectors reject prefix/other/WOW64 parent arrays;
 * separately proven guest-module metadata selects WOW64 children elsewhere.
 * InterlockedShort/InterlockedLong preserve their signed target integer types;
 * their source volatile qualifier affects access, not the stored representation.
 */
#ifndef XX_PROCESS_HEAP_SUBALLOCATOR_LAYOUTS_H
#define XX_PROCESS_HEAP_SUBALLOCATOR_LAYOUTS_H

static const xx_data_struct_field_desc xx_pswhsa_vs32_19045[] = {
    XX_PSW_FIELD("ListEntry", "LIST_ENTRY32[1]", 0x0, 8, 0),
    XX_PSW_FIELD("ListEntry.Flink", "STORED_LINK32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry.Blink", "STORED_LINK32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitBitmap", "ULONGLONG", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitLock", "ULONG", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Size", "USHORT", 0x14, 2, 0),
    XX_PSW_FIELD("SignatureStorage", "USHORT", 0x16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Signature", "bitfield:0:15", 0x16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE),
    XX_PSW_FIELD("FullCommit", "bitfield:15:1", 0x16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsa_vs64_19045[] = {
    XX_PSW_FIELD("ListEntry", "LIST_ENTRY64[1]", 0x0, 16, 0),
    XX_PSW_FIELD("ListEntry.Flink", "STORED_LINK64", 0x0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry.Blink", "STORED_LINK64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitBitmap", "ULONGLONG", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitLock", "ULONGLONG", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Size", "USHORT", 0x20, 2, 0),
    XX_PSW_FIELD("SignatureStorage", "USHORT", 0x22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Signature", "bitfield:0:15", 0x22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE),
    XX_PSW_FIELD("FullCommit", "bitfield:15:1", 0x22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_subsegment32_19045[] = {
    XX_PSW_FIELD("ListEntry", "LIST_ENTRY32[1]", 0x0, 8, 0),
    XX_PSW_FIELD("ListEntry.Flink", "STORED_LINK32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry.Blink", "STORED_LINK32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Owner", "stored PHEAP_LFH_SUBSEGMENT_OWNER32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("DelayFree", "HEAP_LFH_SUBSEGMENT_DELAY_FREE32[1]", 0x8, 4, 0),
    XX_PSW_FIELD("DelayFree.DelayFree", "bitfield:0:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("DelayFree.Count", "bitfield:1:31", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("DelayFree.AllBits", "stored PVOID32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitLock", "ULONG", 0xc, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FreeCount", "USHORT", 0x10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("BlockCount", "USHORT", 0x12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("InterlockedShort", "int16", 0x10, 2, 0),
    XX_PSW_FIELD("InterlockedLong", "int32", 0x10, 4, 0),
    XX_PSW_FIELD("FreeHint", "USHORT", 0x14, 2, XX_PSW_ID),
    XX_PSW_FIELD("Location", "UCHAR", 0x16, 1, XX_PSW_ID),
    XX_PSW_FIELD("WitheldBlockCount", "UCHAR", 0x17, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("BlockOffsets", "opaque HEAP_LFH_SUBSEGMENT_ENCODED_OFFSETS[1]", 0x18, 4, 0),
    XX_PSW_FIELD("BlockOffsets.BlockSize", "opaque USHORT", 0x18, 2, 0),
    XX_PSW_FIELD("BlockOffsets.FirstBlockOffset", "opaque USHORT", 0x1a, 2, 0),
    XX_PSW_FIELD("BlockOffsets.EncodedData", "ULONG", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitUnitShift", "UCHAR", 0x1c, 1, 0),
    XX_PSW_FIELD("CommitUnitCount", "UCHAR", 0x1d, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CommitStateOffset", "USHORT", 0x1e, 2, 0),
    XX_PSW_FIELD("BlockBitmap", "ULONG[1]", 0x20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_subsegment64_19045[] = {
    XX_PSW_FIELD("ListEntry", "LIST_ENTRY64[1]", 0x0, 16, 0),
    XX_PSW_FIELD("ListEntry.Flink", "STORED_LINK64", 0x0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry.Blink", "STORED_LINK64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Owner", "stored PHEAP_LFH_SUBSEGMENT_OWNER64", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("DelayFree", "HEAP_LFH_SUBSEGMENT_DELAY_FREE64[1]", 0x10, 8, 0),
    XX_PSW_FIELD("DelayFree.DelayFree", "bitfield:0:1", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("DelayFree.Count", "bitfield:1:63", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("DelayFree.AllBits", "stored PVOID64", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitLock", "ULONGLONG", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FreeCount", "USHORT", 0x20, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("BlockCount", "USHORT", 0x22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("InterlockedShort", "int16", 0x20, 2, 0),
    XX_PSW_FIELD("InterlockedLong", "int32", 0x20, 4, 0),
    XX_PSW_FIELD("FreeHint", "USHORT", 0x24, 2, XX_PSW_ID),
    XX_PSW_FIELD("Location", "UCHAR", 0x26, 1, XX_PSW_ID),
    XX_PSW_FIELD("WitheldBlockCount", "UCHAR", 0x27, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("BlockOffsets", "opaque HEAP_LFH_SUBSEGMENT_ENCODED_OFFSETS[1]", 0x28, 4, 0),
    XX_PSW_FIELD("BlockOffsets.BlockSize", "opaque USHORT", 0x28, 2, 0),
    XX_PSW_FIELD("BlockOffsets.FirstBlockOffset", "opaque USHORT", 0x2a, 2, 0),
    XX_PSW_FIELD("BlockOffsets.EncodedData", "ULONG", 0x28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitUnitShift", "UCHAR", 0x2c, 1, 0),
    XX_PSW_FIELD("CommitUnitCount", "UCHAR", 0x2d, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CommitStateOffset", "USHORT", 0x2e, 2, 0),
    XX_PSW_FIELD("BlockBitmap", "ULONGLONG[1]", 0x30, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_subsegment64_26100[] = {
    XX_PSW_FIELD("ListEntry", "LIST_ENTRY64[1]", 0x0, 16, 0),
    XX_PSW_FIELD("ListEntry.Flink", "STORED_LINK64", 0x0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry.Blink", "STORED_LINK64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_STATE64[1]", 0x10, 8, 0),
    XX_PSW_FIELD("State.DelayFreeList", "stored USHORT reference", 0x10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.DelayFreeCount", "USHORT", 0x12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.Owner", "stored USHORT reference", 0x14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Location", "UCHAR", 0x16, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.DelayFreeFields", "int32", 0x10, 4, 0),
    XX_PSW_FIELD("State.OwnerLocation", "int32", 0x14, 4, 0),
    XX_PSW_FIELD("State.FreeList", "HEAP_LFH_BLOCK_LIST[1]", 0x10, 4, 0),
    XX_PSW_FIELD("State.FreeList.Next", "stored USHORT reference", 0x10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FreeList.Count", "USHORT", 0x12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.FreeList.SList", "HEAP_LFH_BLOCK_SLIST[1]", 0x10, 2, 0),
    XX_PSW_FIELD("State.FreeList.SList.Next", "stored USHORT reference", 0x10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FreeList.ListFields", "ULONG", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AllBits", "stored PVOID64", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("OwnerFreeListEntry", "SINGLE_LIST_ENTRY64[1]", 0x18, 8, 0),
    XX_PSW_FIELD("OwnerFreeListEntry.Next", "STORED_LINK64", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitStateOffset", "UCHAR", 0x18, 1, 0),
    XX_PSW_FIELD("Spare0Storage", "UCHAR", 0x19, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare0", "bitfield:0:4", 0x19, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("FreeCount", "USHORT", 0x20, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("BlockCount", "USHORT", 0x22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("FreeHint", "UCHAR", 0x24, 1, XX_PSW_ID),
    XX_PSW_FIELD("WitheldBlockCount", "UCHAR", 0x25, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CommitUnitShift", "UCHAR", 0x26, 1, 0),
    XX_PSW_FIELD("CommitUnitCount", "UCHAR", 0x27, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CommitUnitInfo", "HEAP_LFH_COMMIT_UNIT_INFO[1]", 0x26, 2, 0),
    XX_PSW_FIELD("CommitUnitInfo.CommitUnitShift", "UCHAR", 0x26, 1, 0),
    XX_PSW_FIELD("CommitUnitInfo.CommitUnitCount", "UCHAR", 0x27, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CommitUnitInfo.UShort", "USHORT", 0x26, 2, 0),
    XX_PSW_FIELD("BlockOffsets", "opaque HEAP_LFH_SUBSEGMENT_ENCODED_OFFSETS[1]", 0x28, 4, 0),
    XX_PSW_FIELD("BlockOffsets.BlockSize", "opaque USHORT", 0x28, 2, 0),
    XX_PSW_FIELD("BlockOffsets.FirstBlockOffset", "opaque USHORT", 0x2a, 2, 0),
    XX_PSW_FIELD("BlockOffsets.EncodedData", "ULONG", 0x28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BucketRef", "stored USHORT reference", 0x2c, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivateSlotMapRef", "stored USHORT reference", 0x2e, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("HighWatermarkBlockIndex", "USHORT", 0x30, 2, XX_PSW_ID),
    XX_PSW_FIELD("BitmapSearchWidth", "UCHAR", 0x32, 1, 0),
    XX_PSW_FIELD("UCharStorage", "UCHAR", 0x33, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivateFormat", "bitfield:0:1", 0x33, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare1", "bitfield:1:7", 0x33, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("UChar", "HEAP_LFH_SUBSEGMENT_UCHAR_FIELDS[1]", 0x33, 1, 0),
    XX_PSW_FIELD("UChar.PrivateFormat", "bitfield:0:1", 0x33, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("UChar.Spare1", "bitfield:1:7", 0x33, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("UChar.AllUCharBits", "UCHAR", 0x33, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare3", "ULONG", 0x34, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("CommitLock", "ULONGLONG", 0x38, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BlockBitmap", "ULONGLONG[1]", 0x40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_owner32_19045[] = {
    XX_PSW_FIELD("FlagsStorage", "UCHAR", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare0", "bitfield:1:7", 0x0, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("BucketIndex", "UCHAR", 0x1, 1, XX_PSW_ID),
    XX_PSW_FIELD("SlotCount", "UCHAR", 0x2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("SlotIndex", "UCHAR", 0x2, 1, XX_PSW_ID),
    XX_PSW_FIELD("Spare1", "UCHAR", 0x3, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("AvailableSubsegmentCount", "ULONG", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("Lock", "ULONG", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AvailableSubsegmentList", "LIST_ENTRY32[1]", 0xc, 8, 0),
    XX_PSW_FIELD("AvailableSubsegmentList.Flink", "STORED_LINK32", 0xc, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AvailableSubsegmentList.Blink", "STORED_LINK32", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullSubsegmentList", "LIST_ENTRY32[1]", 0x14, 8, 0),
    XX_PSW_FIELD("FullSubsegmentList.Flink", "STORED_LINK32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullSubsegmentList.Blink", "STORED_LINK32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_owner64_19045[] = {
    XX_PSW_FIELD("FlagsStorage", "UCHAR", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare0", "bitfield:1:7", 0x0, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("BucketIndex", "UCHAR", 0x1, 1, XX_PSW_ID),
    XX_PSW_FIELD("SlotCount", "UCHAR", 0x2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("SlotIndex", "UCHAR", 0x2, 1, XX_PSW_ID),
    XX_PSW_FIELD("Spare1", "UCHAR", 0x3, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("AvailableSubsegmentCount", "ULONGLONG", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("Lock", "ULONGLONG", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AvailableSubsegmentList", "LIST_ENTRY64[1]", 0x18, 16, 0),
    XX_PSW_FIELD("AvailableSubsegmentList.Flink", "STORED_LINK64", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AvailableSubsegmentList.Blink", "STORED_LINK64", 0x20, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullSubsegmentList", "LIST_ENTRY64[1]", 0x28, 16, 0),
    XX_PSW_FIELD("FullSubsegmentList.Flink", "STORED_LINK64", 0x28, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullSubsegmentList.Blink", "STORED_LINK64", 0x30, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_owner64_26100[] = {
    XX_PSW_FIELD("FlagsStorage", "UCHAR", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BucketIndex", "bitfield:1:7", 0x0, 1, XX_PSW_ID),
    XX_PSW_FIELD("SlotCount", "UCHAR", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("AvailableSubsegmentCount", "UCHAR", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("BucketRef", "stored USHORT reference", 0x2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivateSlotMapRef", "stored USHORT reference", 0x4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("HeatMapRef", "stored USHORT reference", 0x6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("OwnerFreeList", "SINGLE_LIST_ENTRY64[1]", 0x8, 8, 0),
    XX_PSW_FIELD("OwnerFreeList.Next", "STORED_LINK64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("SpareStorage", "USHORT", 0x8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare", "bitfield:0:12", 0x8, 2, XX_PSW_RESERVED),
    XX_PSW_FIELD("Lock", "ULONGLONG", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("SlotStandbyEntry", "SINGLE_LIST_ENTRY64[1]", 0x10, 8, 0),
    XX_PSW_FIELD("SlotStandbyEntry.Next", "STORED_LINK64", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivSlotListEntry", "HEAP_LFH_PTRREF_LIST[1]", 0x10, 4, 0),
    XX_PSW_FIELD("PrivSlotListEntry.Flink", "stored USHORT reference", 0x10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivSlotListEntry.Blink", "stored USHORT reference", 0x12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("OwnerThreadId", "ULONG", 0x14, 4, XX_PSW_ID),
    XX_PSW_FIELD("AvailableSubsegmentList", "LIST_ENTRY64[1]", 0x18, 16, 0),
    XX_PSW_FIELD("AvailableSubsegmentList.Flink", "STORED_LINK64", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AvailableSubsegmentList.Blink", "STORED_LINK64", 0x20, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullSubsegmentList", "LIST_ENTRY64[1]", 0x28, 16, 0),
    XX_PSW_FIELD("FullSubsegmentList.Flink", "STORED_LINK64", 0x28, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullSubsegmentList.Blink", "STORED_LINK64", 0x30, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_bucket32_19045[] = {
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_OWNER32[1]", 0x0, 28, 0),
    XX_PSW_FIELD("State.FlagsStorage", "UCHAR", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Spare0", "bitfield:1:7", 0x0, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.BucketIndex", "UCHAR", 0x1, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.SlotCount", "UCHAR", 0x2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.SlotIndex", "UCHAR", 0x2, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.Spare1", "UCHAR", 0x3, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.AvailableSubsegmentCount", "ULONG", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.Lock", "ULONG", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList", "LIST_ENTRY32[1]", 0xc, 8, 0),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Flink", "STORED_LINK32", 0xc, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Blink", "STORED_LINK32", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList", "LIST_ENTRY32[1]", 0x14, 8, 0),
    XX_PSW_FIELD("State.FullSubsegmentList.Flink", "STORED_LINK32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList.Blink", "STORED_LINK32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("TotalBlockCount", "ULONG", 0x1c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("TotalSubsegmentCount", "ULONG", 0x20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("ReciprocalBlockSize", "ULONG", 0x24, 4, 0),
    XX_PSW_FIELD("Shift", "UCHAR", 0x28, 1, 0),
    XX_PSW_FIELD("ContentionCount", "UCHAR", 0x29, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("AffinityMappingLock", "ULONG", 0x2c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ProcAffinityMapping", "PUCHAR32", 0x30, 4, XX_PSW_PTR),
    XX_PSW_FIELD("AffinitySlots", "PPHEAP_LFH_AFFINITY_SLOT32", 0x34, 4, XX_PSW_PTR),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_bucket64_19045[] = {
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_OWNER64[1]", 0x0, 56, 0),
    XX_PSW_FIELD("State.FlagsStorage", "UCHAR", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Spare0", "bitfield:1:7", 0x0, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.BucketIndex", "UCHAR", 0x1, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.SlotCount", "UCHAR", 0x2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.SlotIndex", "UCHAR", 0x2, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.Spare1", "UCHAR", 0x3, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.AvailableSubsegmentCount", "ULONGLONG", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.Lock", "ULONGLONG", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList", "LIST_ENTRY64[1]", 0x18, 16, 0),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Flink", "STORED_LINK64", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Blink", "STORED_LINK64", 0x20, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList", "LIST_ENTRY64[1]", 0x28, 16, 0),
    XX_PSW_FIELD("State.FullSubsegmentList.Flink", "STORED_LINK64", 0x28, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList.Blink", "STORED_LINK64", 0x30, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("TotalBlockCount", "ULONGLONG", 0x38, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("TotalSubsegmentCount", "ULONGLONG", 0x40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("ReciprocalBlockSize", "ULONG", 0x48, 4, 0),
    XX_PSW_FIELD("Shift", "UCHAR", 0x4c, 1, 0),
    XX_PSW_FIELD("ContentionCount", "UCHAR", 0x4d, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("AffinityMappingLock", "ULONGLONG", 0x50, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ProcAffinityMapping", "PUCHAR64", 0x58, 8, XX_PSW_PTR),
    XX_PSW_FIELD("AffinitySlots", "PPHEAP_LFH_AFFINITY_SLOT64", 0x60, 8, XX_PSW_PTR),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_bucket64_26100[] = {
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_OWNER64[1]", 0x0, 56, 0),
    XX_PSW_FIELD("State.FlagsStorage", "UCHAR", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.BucketIndex", "bitfield:1:7", 0x0, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.SlotCount", "UCHAR", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.AvailableSubsegmentCount", "UCHAR", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.BucketRef", "stored USHORT reference", 0x2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.PrivateSlotMapRef", "stored USHORT reference", 0x4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.HeatMapRef", "stored USHORT reference", 0x6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.OwnerFreeList", "SINGLE_LIST_ENTRY64[1]", 0x8, 8, 0),
    XX_PSW_FIELD("State.OwnerFreeList.Next", "STORED_LINK64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.SpareStorage", "USHORT", 0x8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Spare", "bitfield:0:12", 0x8, 2, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.Lock", "ULONGLONG", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.SlotStandbyEntry", "SINGLE_LIST_ENTRY64[1]", 0x10, 8, 0),
    XX_PSW_FIELD("State.SlotStandbyEntry.Next", "STORED_LINK64", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.PrivSlotListEntry", "HEAP_LFH_PTRREF_LIST[1]", 0x10, 4, 0),
    XX_PSW_FIELD("State.PrivSlotListEntry.Flink", "stored USHORT reference", 0x10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.PrivSlotListEntry.Blink", "stored USHORT reference", 0x12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.OwnerThreadId", "ULONG", 0x14, 4, XX_PSW_ID),
    XX_PSW_FIELD("State.AvailableSubsegmentList", "LIST_ENTRY64[1]", 0x18, 16, 0),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Flink", "STORED_LINK64", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Blink", "STORED_LINK64", 0x20, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList", "LIST_ENTRY64[1]", 0x28, 16, 0),
    XX_PSW_FIELD("State.FullSubsegmentList.Flink", "STORED_LINK64", 0x28, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList.Blink", "STORED_LINK64", 0x30, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("TotalBlockCount", "ULONGLONG", 0x38, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("TotalSubsegmentCount", "ULONGLONG", 0x40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("ReciprocalBlockSize", "ULONG", 0x48, 4, 0),
    XX_PSW_FIELD("HotHeatThreshold", "USHORT", 0x4c, 2, 0),
    XX_PSW_FIELD("PrivateHeatThreshold", "USHORT", 0x4e, 2, 0),
    XX_PSW_FIELD("PrivSlotListLock", "ULONGLONG", 0x50, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivSlotList", "HEAP_LFH_PTRREF_LIST[1]", 0x58, 4, 0),
    XX_PSW_FIELD("PrivSlotList.Flink", "stored USHORT reference", 0x58, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivSlotList.Blink", "stored USHORT reference", 0x5a, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CompactionNeeded", "UCHAR", 0x5c, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare", "UCHAR", 0x5d, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("AffinityMapInProgress", "UCHAR", 0x5e, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ContentionCount", "UCHAR", 0x5f, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("LastContentionTime", "ULONGLONG", 0x60, 8, 0),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_slot32_19045[] = {
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_OWNER32[1]", 0x0, 28, 0),
    XX_PSW_FIELD("State.FlagsStorage", "UCHAR", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Spare0", "bitfield:1:7", 0x0, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.BucketIndex", "UCHAR", 0x1, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.SlotCount", "UCHAR", 0x2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.SlotIndex", "UCHAR", 0x2, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.Spare1", "UCHAR", 0x3, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.AvailableSubsegmentCount", "ULONG", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.Lock", "ULONG", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList", "LIST_ENTRY32[1]", 0xc, 8, 0),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Flink", "STORED_LINK32", 0xc, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Blink", "STORED_LINK32", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList", "LIST_ENTRY32[1]", 0x14, 8, 0),
    XX_PSW_FIELD("State.FullSubsegmentList.Flink", "STORED_LINK32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList.Blink", "STORED_LINK32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment", "HEAP_LFH_FAST_REF32[1]", 0x1c, 4, 0),
    XX_PSW_FIELD("ActiveSubsegment.Target", "stored tagged PVOID32", 0x1c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment.Value", "ULONG", 0x1c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment.RefCount", "bitfield:0:12", 0x1c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_slot64_19045[] = {
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_OWNER64[1]", 0x0, 56, 0),
    XX_PSW_FIELD("State.FlagsStorage", "UCHAR", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Spare0", "bitfield:1:7", 0x0, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.BucketIndex", "UCHAR", 0x1, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.SlotCount", "UCHAR", 0x2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.SlotIndex", "UCHAR", 0x2, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.Spare1", "UCHAR", 0x3, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.AvailableSubsegmentCount", "ULONGLONG", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.Lock", "ULONGLONG", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList", "LIST_ENTRY64[1]", 0x18, 16, 0),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Flink", "STORED_LINK64", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Blink", "STORED_LINK64", 0x20, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList", "LIST_ENTRY64[1]", 0x28, 16, 0),
    XX_PSW_FIELD("State.FullSubsegmentList.Flink", "STORED_LINK64", 0x28, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList.Blink", "STORED_LINK64", 0x30, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment", "HEAP_LFH_FAST_REF64[1]", 0x38, 8, 0),
    XX_PSW_FIELD("ActiveSubsegment.Target", "stored tagged PVOID64", 0x38, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment.Value", "ULONGLONG", 0x38, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment.RefCount", "bitfield:0:12", 0x38, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
};

static const xx_data_struct_field_desc xx_pswhsa_lfh_slot64_26100[] = {
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_OWNER64[1]", 0x0, 56, 0),
    XX_PSW_FIELD("State.FlagsStorage", "UCHAR", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.BucketIndex", "bitfield:1:7", 0x0, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.SlotCount", "UCHAR", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.AvailableSubsegmentCount", "UCHAR", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.BucketRef", "stored USHORT reference", 0x2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.PrivateSlotMapRef", "stored USHORT reference", 0x4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.HeatMapRef", "stored USHORT reference", 0x6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.OwnerFreeList", "SINGLE_LIST_ENTRY64[1]", 0x8, 8, 0),
    XX_PSW_FIELD("State.OwnerFreeList.Next", "STORED_LINK64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.SpareStorage", "USHORT", 0x8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Spare", "bitfield:0:12", 0x8, 2, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.Lock", "ULONGLONG", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.SlotStandbyEntry", "SINGLE_LIST_ENTRY64[1]", 0x10, 8, 0),
    XX_PSW_FIELD("State.SlotStandbyEntry.Next", "STORED_LINK64", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.PrivSlotListEntry", "HEAP_LFH_PTRREF_LIST[1]", 0x10, 4, 0),
    XX_PSW_FIELD("State.PrivSlotListEntry.Flink", "stored USHORT reference", 0x10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.PrivSlotListEntry.Blink", "stored USHORT reference", 0x12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.OwnerThreadId", "ULONG", 0x14, 4, XX_PSW_ID),
    XX_PSW_FIELD("State.AvailableSubsegmentList", "LIST_ENTRY64[1]", 0x18, 16, 0),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Flink", "STORED_LINK64", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Blink", "STORED_LINK64", 0x20, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList", "LIST_ENTRY64[1]", 0x28, 16, 0),
    XX_PSW_FIELD("State.FullSubsegmentList.Flink", "STORED_LINK64", 0x28, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList.Blink", "STORED_LINK64", 0x30, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment", "HEAP_LFH_FAST_REF64[1]", 0x38, 8, 0),
    XX_PSW_FIELD("ActiveSubsegment.Target", "stored tagged PVOID64", 0x38, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment.Value", "ULONGLONG", 0x38, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment.RefCount", "bitfield:0:12", 0x38, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
};

static int xx_pswhsa_native_index(const xx_psw_nt_layout *parent, unsigned width)
{
    if (!parent) return -1;
    if (width == 4 && parent->fields == xx_pswh_segment32_19045) return 0;
    if (width == 8 && parent->fields == xx_pswh_segment64_19045) return 1;
    if (width == 8 && parent->fields == xx_pswh_segment64_26100) return 2;
    return -1;
}

static const xx_psw_nt_layout xx_pswhsa_vs_profiles[] = {
    {19045, 0x014c, "NT 10.0.19045 native I386 HEAP_VS_SUBSEGMENT symbol profile", xx_pswhsa_vs32_19045, XX_PSW_COUNT(xx_pswhsa_vs32_19045), 0x18},
    {19045, 0x8664, "NT 10.0.19045 native AMD64 HEAP_VS_SUBSEGMENT symbol profile", xx_pswhsa_vs64_19045, XX_PSW_COUNT(xx_pswhsa_vs64_19045), 0x28},
    {26100, 0x8664, "NT 10.0.26100 native AMD64 HEAP_VS_SUBSEGMENT symbol profile", xx_pswhsa_vs64_19045, XX_PSW_COUNT(xx_pswhsa_vs64_19045), 0x28},
};

static const xx_psw_nt_layout *xx_pswhsa_native_vs(const xx_psw_nt_layout *parent, unsigned width)
{
    int index = xx_pswhsa_native_index(parent, width);
    return index < 0 ? NULL : xx_pswhsa_vs_profiles + index;
}

static const xx_psw_nt_layout xx_pswhsa_lfh_subsegment_profiles[] = {
    {19045, 0x014c, "NT 10.0.19045 native I386 HEAP_LFH_SUBSEGMENT symbol profile", xx_pswhsa_lfh_subsegment32_19045, XX_PSW_COUNT(xx_pswhsa_lfh_subsegment32_19045),
     0x24},
    {19045, 0x8664, "NT 10.0.19045 native AMD64 HEAP_LFH_SUBSEGMENT symbol profile", xx_pswhsa_lfh_subsegment64_19045, XX_PSW_COUNT(xx_pswhsa_lfh_subsegment64_19045),
     0x38},
    {26100, 0x8664, "NT 10.0.26100 native AMD64 HEAP_LFH_SUBSEGMENT symbol profile", xx_pswhsa_lfh_subsegment64_26100, XX_PSW_COUNT(xx_pswhsa_lfh_subsegment64_26100),
     0x48},
};

static const xx_psw_nt_layout *xx_pswhsa_native_lfh_subsegment(const xx_psw_nt_layout *parent, unsigned width)
{
    int index = xx_pswhsa_native_index(parent, width);
    return index < 0 ? NULL : xx_pswhsa_lfh_subsegment_profiles + index;
}

static const xx_psw_nt_layout xx_pswhsa_lfh_bucket_profiles[] = {
    {19045, 0x014c, "NT 10.0.19045 native I386 HEAP_LFH_BUCKET symbol profile", xx_pswhsa_lfh_bucket32_19045, XX_PSW_COUNT(xx_pswhsa_lfh_bucket32_19045), 0x38},
    {19045, 0x8664, "NT 10.0.19045 native AMD64 HEAP_LFH_BUCKET symbol profile", xx_pswhsa_lfh_bucket64_19045, XX_PSW_COUNT(xx_pswhsa_lfh_bucket64_19045), 0x68},
    {26100, 0x8664, "NT 10.0.26100 native AMD64 HEAP_LFH_BUCKET symbol profile", xx_pswhsa_lfh_bucket64_26100, XX_PSW_COUNT(xx_pswhsa_lfh_bucket64_26100), 0x68},
};

static const xx_psw_nt_layout *xx_pswhsa_native_lfh_bucket(const xx_psw_nt_layout *parent, unsigned width)
{
    int index = xx_pswhsa_native_index(parent, width);
    return index < 0 ? NULL : xx_pswhsa_lfh_bucket_profiles + index;
}

static const xx_psw_nt_layout xx_pswhsa_lfh_slot_profiles[] = {
    {19045, 0x014c, "NT 10.0.19045 native I386 HEAP_LFH_AFFINITY_SLOT symbol profile", xx_pswhsa_lfh_slot32_19045, XX_PSW_COUNT(xx_pswhsa_lfh_slot32_19045), 0x20},
    {19045, 0x8664, "NT 10.0.19045 native AMD64 HEAP_LFH_AFFINITY_SLOT symbol profile", xx_pswhsa_lfh_slot64_19045, XX_PSW_COUNT(xx_pswhsa_lfh_slot64_19045), 0x40},
    {26100, 0x8664, "NT 10.0.26100 native AMD64 HEAP_LFH_AFFINITY_SLOT symbol profile", xx_pswhsa_lfh_slot64_26100, XX_PSW_COUNT(xx_pswhsa_lfh_slot64_26100), 0x40},
};

static const xx_psw_nt_layout *xx_pswhsa_native_lfh_slot(const xx_psw_nt_layout *parent, unsigned width)
{
    int index = xx_pswhsa_native_index(parent, width);
    return index < 0 ? NULL : xx_pswhsa_lfh_slot_profiles + index;
}

static const xx_psw_nt_layout xx_pswhsa_lfh_owner_profiles[] = {
    {19045, 0x014c, "NT 10.0.19045 native I386 HEAP_LFH_SUBSEGMENT_OWNER symbol profile", xx_pswhsa_lfh_owner32_19045, XX_PSW_COUNT(xx_pswhsa_lfh_owner32_19045), 0x1c},
    {19045, 0x8664, "NT 10.0.19045 native AMD64 HEAP_LFH_SUBSEGMENT_OWNER symbol profile", xx_pswhsa_lfh_owner64_19045, XX_PSW_COUNT(xx_pswhsa_lfh_owner64_19045), 0x38},
    {26100, 0x8664, "NT 10.0.26100 native AMD64 HEAP_LFH_SUBSEGMENT_OWNER symbol profile", xx_pswhsa_lfh_owner64_26100, XX_PSW_COUNT(xx_pswhsa_lfh_owner64_26100), 0x38},
};

static const xx_psw_nt_layout *xx_pswhsa_native_lfh_owner(const xx_psw_nt_layout *parent, unsigned width)
{
    int index = xx_pswhsa_native_index(parent, width);
    return index < 0 ? NULL : xx_pswhsa_lfh_owner_profiles + index;
}

#endif /* XX_PROCESS_HEAP_SUBALLOCATOR_LAYOUTS_H */
