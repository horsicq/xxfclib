/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_WOW64_HEAP_SUBALLOCATOR_LAYOUTS_H
#define XX_PROCESS_WOW64_HEAP_SUBALLOCATOR_LAYOUTS_H

/* Private WOW64 guest suballocator ABI metadata. Include after XX_PSW_*,
 * xx_psw_nt_layout and the exact WOW64 SEGMENT_HEAP parent profiles.
 * Independently generated factual descriptors come directly from complete
 * Microsoft public 32-bit NTDLL CodeView type records, not native x86/x64
 * pointer-width substitution. Every named top-level and inline embedded
 * union/structure/bitfield view is retained. BitfieldStorage rows are explicit
 * metadata aliases for otherwise anonymous physical allocation units.
 *
 * Primary exact symbol sources:
 * https://msdl.microsoft.com/download/symbols/wntdll.pdb/7C215E43AA974FFF5C173AB29D5A5EFB1/wntdll.pdb
 *   WOW64 NTDLL 10.0.19041.2965 on OS 10.0.19045; PE RSDS/DBI age1.
 * https://msdl.microsoft.com/download/symbols/wntdll.pdb/259160B822D032AC486248BAFFE15E011/wntdll.pdb
 *   WOW64 NTDLL 10.0.26100.1591 on OS 10.0.26100; PE RSDS/DBI age1.
 * https://github.com/microsoft/microsoft-pdb/blob/master/include/cvinfo.h
 * The parent WOW64 SEGMENT_HEAP selector already requires the exact loaded
 * guest module GUID/age, OS version, I386 guest and AMD64 host. These selectors
 * accept only those exact selected parent field arrays at target width4.
 * Unknown identities, classification prefixes and native parents are rejected.
 *
 * These are fixed declared headers: BlockBitmap[1] is the complete PDB type's
 * fixed placeholder span, not a claim that the variable allocation bitmap ends
 * there. No dynamic payload extent or block graph is inferred. All array
 * spans retain their target byte size. Encoded offsets, tagged fast refs,
 * compact pointer refs, callback/lock state and list links remain stored data
 * without a followable POINTER property. In particular FAST_REF.RefCount uses
 * low12 bits, 26100 State.Owner is a USHORT ref, and modern buckets have no
 * AffinitySlots/ProcAffinityMapping members. No unproved decoding is performed.
 * Names/types are static literals. No target memory is read by this header.
 */

static const xx_data_struct_field_desc xx_pswhsw_vs32_19045[] = {
    XX_PSW_FIELD("BitfieldStorage", "uint16", 0x16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry", "LIST_ENTRY32_19045[1]", 0x0, 8, 0),
    XX_PSW_FIELD("ListEntry.Flink", "STORED_PVOID32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry.Blink", "STORED_PVOID32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitBitmap", "uint64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitLock", "uint32", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Size", "uint16", 0x14, 2, 0),
    XX_PSW_FIELD("Signature", "bitfield:0:15", 0x16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullCommit", "bitfield:15:1", 0x16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsw_lfh_subsegment32_19045[] = {
    XX_PSW_FIELD("ListEntry", "LIST_ENTRY32_19045[1]", 0x0, 8, 0),
    XX_PSW_FIELD("ListEntry.Flink", "STORED_PVOID32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry.Blink", "STORED_PVOID32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Owner", "STORED_PVOID32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("DelayFree", "HEAP_LFH_SUBSEGMENT_DELAY_FREE32_19045[1]", 0x8, 4, 0),
    XX_PSW_FIELD("DelayFree.DelayFree", "bitfield:0:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("DelayFree.Count", "bitfield:1:31", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("DelayFree.AllBits", "STORED_PVOID32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitLock", "uint32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FreeCount", "uint16", 0x10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("BlockCount", "uint16", 0x12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("InterlockedShort", "int16", 0x10, 2, 0),
    XX_PSW_FIELD("InterlockedLong", "int32", 0x10, 4, 0),
    XX_PSW_FIELD("FreeHint", "uint16", 0x14, 2, 0),
    XX_PSW_FIELD("Location", "uint8", 0x16, 1, XX_PSW_ID),
    XX_PSW_FIELD("WitheldBlockCount", "uint8", 0x17, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("BlockOffsets", "HEAP_LFH_SUBSEGMENT_ENCODED_OFFSETS32_19045[1]", 0x18, 4, 0),
    XX_PSW_FIELD("BlockOffsets.BlockSize", "uint16", 0x18, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BlockOffsets.FirstBlockOffset", "uint16", 0x1A, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BlockOffsets.EncodedData", "uint32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitUnitShift", "uint8", 0x1C, 1, 0),
    XX_PSW_FIELD("CommitUnitCount", "uint8", 0x1D, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CommitStateOffset", "uint16", 0x1E, 2, 0),
    XX_PSW_FIELD("BlockBitmap", "uint32[1]", 0x20, 4, 0),
    XX_PSW_FIELD("BlockBitmap[0]", "uint32", 0x20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsw_lfh_bucket32_19045[] = {
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_OWNER32_19045[1]", 0x0, 28, 0),
    XX_PSW_FIELD("State.BitfieldStorage", "uint8", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Spare0", "bitfield:1:7", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.BucketIndex", "uint8", 0x1, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.SlotCount", "uint8", 0x2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.SlotIndex", "uint8", 0x2, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.Spare1", "uint8", 0x3, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.AvailableSubsegmentCount", "uint32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.Lock", "uint32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList", "LIST_ENTRY32_19045[1]", 0xC, 8, 0),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Flink", "STORED_PVOID32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Blink", "STORED_PVOID32", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList", "LIST_ENTRY32_19045[1]", 0x14, 8, 0),
    XX_PSW_FIELD("State.FullSubsegmentList.Flink", "STORED_PVOID32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList.Blink", "STORED_PVOID32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("TotalBlockCount", "uint32", 0x1C, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("TotalSubsegmentCount", "uint32", 0x20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("ReciprocalBlockSize", "uint32", 0x24, 4, 0),
    XX_PSW_FIELD("Shift", "uint8", 0x28, 1, 0),
    XX_PSW_FIELD("ContentionCount", "uint8", 0x29, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("AffinityMappingLock", "uint32", 0x2C, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ProcAffinityMapping", "STORED_PVOID32", 0x30, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AffinitySlots", "STORED_PVOID32", 0x34, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsw_lfh_slot32_19045[] = {
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_OWNER32_19045[1]", 0x0, 28, 0),
    XX_PSW_FIELD("State.BitfieldStorage", "uint8", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Spare0", "bitfield:1:7", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.BucketIndex", "uint8", 0x1, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.SlotCount", "uint8", 0x2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.SlotIndex", "uint8", 0x2, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.Spare1", "uint8", 0x3, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("State.AvailableSubsegmentCount", "uint32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.Lock", "uint32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList", "LIST_ENTRY32_19045[1]", 0xC, 8, 0),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Flink", "STORED_PVOID32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Blink", "STORED_PVOID32", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList", "LIST_ENTRY32_19045[1]", 0x14, 8, 0),
    XX_PSW_FIELD("State.FullSubsegmentList.Flink", "STORED_PVOID32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList.Blink", "STORED_PVOID32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment", "HEAP_LFH_FAST_REF32_19045[1]", 0x1C, 4, 0),
    XX_PSW_FIELD("ActiveSubsegment.Target", "STORED_PVOID32", 0x1C, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment.Value", "uint32", 0x1C, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment.RefCount", "bitfield:0:12", 0x1C, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsw_lfh_owner32_19045[] = {
    XX_PSW_FIELD("BitfieldStorage", "uint8", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare0", "bitfield:1:7", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BucketIndex", "uint8", 0x1, 1, XX_PSW_ID),
    XX_PSW_FIELD("SlotCount", "uint8", 0x2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("SlotIndex", "uint8", 0x2, 1, XX_PSW_ID),
    XX_PSW_FIELD("Spare1", "uint8", 0x3, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("AvailableSubsegmentCount", "uint32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("Lock", "uint32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AvailableSubsegmentList", "LIST_ENTRY32_19045[1]", 0xC, 8, 0),
    XX_PSW_FIELD("AvailableSubsegmentList.Flink", "STORED_PVOID32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AvailableSubsegmentList.Blink", "STORED_PVOID32", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullSubsegmentList", "LIST_ENTRY32_19045[1]", 0x14, 8, 0),
    XX_PSW_FIELD("FullSubsegmentList.Flink", "STORED_PVOID32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullSubsegmentList.Blink", "STORED_PVOID32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsw_vs32_26100[] = {
    XX_PSW_FIELD("BitfieldStorage", "uint16", 0x16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry", "LIST_ENTRY32_26100[1]", 0x0, 8, 0),
    XX_PSW_FIELD("ListEntry.Flink", "STORED_PVOID32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry.Blink", "STORED_PVOID32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitBitmap", "uint64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitLock", "uint32", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Size", "uint16", 0x14, 2, 0),
    XX_PSW_FIELD("Signature", "bitfield:0:15", 0x16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullCommit", "bitfield:15:1", 0x16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsw_lfh_subsegment32_26100[] = {
    XX_PSW_FIELD("BitfieldStorage0", "uint8", 0x11, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BitfieldStorage1", "uint8", 0x27, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry", "LIST_ENTRY32_26100[1]", 0x0, 8, 0),
    XX_PSW_FIELD("ListEntry.Flink", "STORED_PVOID32", 0x0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ListEntry.Blink", "STORED_PVOID32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_STATE32_26100[1]", 0x8, 8, 0),
    XX_PSW_FIELD("State.DelayFreeList", "uint16", 0x8, 2, 0),
    XX_PSW_FIELD("State.DelayFreeCount", "uint16", 0xA, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.Owner", "uint16", 0xC, 2, 0),
    XX_PSW_FIELD("State.Location", "uint8", 0xE, 1, XX_PSW_ID),
    XX_PSW_FIELD("State.DelayFreeFields", "int32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.OwnerLocation", "int32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FreeList", "HEAP_LFH_BLOCK_LIST32_26100[1]", 0x8, 4, 0),
    XX_PSW_FIELD("State.FreeList.Next", "uint16", 0x8, 2, 0),
    XX_PSW_FIELD("State.FreeList.Count", "uint16", 0xA, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.FreeList.SList", "HEAP_LFH_BLOCK_SLIST32_26100[1]", 0x8, 2, 0),
    XX_PSW_FIELD("State.FreeList.SList.Next", "uint16", 0x8, 2, 0),
    XX_PSW_FIELD("State.FreeList.ListFields", "uint32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AllBits", "STORED_PVOID32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("OwnerFreeListEntry", "SINGLE_LIST_ENTRY32_26100[1]", 0x10, 4, 0),
    XX_PSW_FIELD("OwnerFreeListEntry.Next", "STORED_PVOID32", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("CommitStateOffset", "uint8", 0x10, 1, 0),
    XX_PSW_FIELD("Spare0", "bitfield:0:4", 0x11, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FreeCount", "uint16", 0x14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("BlockCount", "uint16", 0x16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("FreeHint", "uint8", 0x18, 1, 0),
    XX_PSW_FIELD("WitheldBlockCount", "uint8", 0x19, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CommitUnitShift", "uint8", 0x1A, 1, 0),
    XX_PSW_FIELD("CommitUnitCount", "uint8", 0x1B, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CommitUnitInfo", "HEAP_LFH_COMMIT_UNIT_INFO32_26100[1]", 0x1A, 2, 0),
    XX_PSW_FIELD("CommitUnitInfo.CommitUnitShift", "uint8", 0x1A, 1, 0),
    XX_PSW_FIELD("CommitUnitInfo.CommitUnitCount", "uint8", 0x1B, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CommitUnitInfo.UShort", "uint16", 0x1A, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BlockOffsets", "HEAP_LFH_SUBSEGMENT_ENCODED_OFFSETS32_26100[1]", 0x1C, 4, 0),
    XX_PSW_FIELD("BlockOffsets.BlockSize", "uint16", 0x1C, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BlockOffsets.FirstBlockOffset", "uint16", 0x1E, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BlockOffsets.EncodedData", "uint32", 0x1C, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BucketRef", "uint16", 0x20, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivateSlotMapRef", "uint16", 0x22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("HighWatermarkBlockIndex", "uint16", 0x24, 2, 0),
    XX_PSW_FIELD("BitmapSearchWidth", "uint8", 0x26, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivateFormat", "bitfield:0:1", 0x27, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare1", "bitfield:1:7", 0x27, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("UChar", "HEAP_LFH_SUBSEGMENT_UCHAR_FIELDS32_26100[1]", 0x27, 1, 0),
    XX_PSW_FIELD("UChar.PrivateFormat", "bitfield:0:1", 0x27, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("UChar.Spare1", "bitfield:1:7", 0x27, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("UChar.AllUCharBits", "uint8", 0x27, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare3", "uint32", 0x28, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("CommitLock", "uint32", 0x2C, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BlockBitmap", "uint32[1]", 0x30, 4, 0),
    XX_PSW_FIELD("BlockBitmap[0]", "uint32", 0x30, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsw_lfh_bucket32_26100[] = {
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_OWNER32_26100[1]", 0x0, 36, 0),
    XX_PSW_FIELD("State.BitfieldStorage0", "uint8", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.BitfieldStorage1", "uint16", 0x8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.BucketIndex", "bitfield:1:7", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.SlotCount", "uint8", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.AvailableSubsegmentCount", "uint8", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.BucketRef", "uint16", 0x2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.PrivateSlotMapRef", "uint16", 0x4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.HeatMapRef", "uint16", 0x6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.OwnerFreeList", "SINGLE_LIST_ENTRY32_26100[1]", 0x8, 4, 0),
    XX_PSW_FIELD("State.OwnerFreeList.Next", "STORED_PVOID32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Spare", "bitfield:0:12", 0x8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Lock", "uint32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.SlotStandbyEntry", "SINGLE_LIST_ENTRY32_26100[1]", 0xC, 4, 0),
    XX_PSW_FIELD("State.SlotStandbyEntry.Next", "STORED_PVOID32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.PrivSlotListEntry", "HEAP_LFH_PTRREF_LIST32_26100[1]", 0xC, 4, 0),
    XX_PSW_FIELD("State.PrivSlotListEntry.Flink", "uint16", 0xC, 2, 0),
    XX_PSW_FIELD("State.PrivSlotListEntry.Blink", "uint16", 0xE, 2, 0),
    XX_PSW_FIELD("State.OwnerThreadId", "uint32", 0x10, 4, XX_PSW_ID),
    XX_PSW_FIELD("State.AvailableSubsegmentList", "LIST_ENTRY32_26100[1]", 0x14, 8, 0),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Flink", "STORED_PVOID32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Blink", "STORED_PVOID32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList", "LIST_ENTRY32_26100[1]", 0x1C, 8, 0),
    XX_PSW_FIELD("State.FullSubsegmentList.Flink", "STORED_PVOID32", 0x1C, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList.Blink", "STORED_PVOID32", 0x20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("TotalBlockCount", "uint32", 0x24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("TotalSubsegmentCount", "uint32", 0x28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("ReciprocalBlockSize", "uint32", 0x2C, 4, 0),
    XX_PSW_FIELD("HotHeatThreshold", "uint16", 0x30, 2, 0),
    XX_PSW_FIELD("PrivateHeatThreshold", "uint16", 0x32, 2, 0),
    XX_PSW_FIELD("PrivSlotListLock", "uint32", 0x34, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivSlotList", "HEAP_LFH_PTRREF_LIST32_26100[1]", 0x38, 4, 0),
    XX_PSW_FIELD("PrivSlotList.Flink", "uint16", 0x38, 2, 0),
    XX_PSW_FIELD("PrivSlotList.Blink", "uint16", 0x3A, 2, 0),
    XX_PSW_FIELD("CompactionNeeded", "uint8", 0x3C, 1, 0),
    XX_PSW_FIELD("Spare", "uint8", 0x3D, 1, XX_PSW_RESERVED),
    XX_PSW_FIELD("AffinityMapInProgress", "uint8", 0x3E, 1, 0),
    XX_PSW_FIELD("ContentionCount", "uint8", 0x3F, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("LastContentionTime", "uint64", 0x40, 8, 0),
};

static const xx_data_struct_field_desc xx_pswhsw_lfh_slot32_26100[] = {
    XX_PSW_FIELD("State", "HEAP_LFH_SUBSEGMENT_OWNER32_26100[1]", 0x0, 36, 0),
    XX_PSW_FIELD("State.BitfieldStorage0", "uint8", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.BitfieldStorage1", "uint16", 0x8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.BucketIndex", "bitfield:1:7", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.SlotCount", "uint8", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.AvailableSubsegmentCount", "uint8", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("State.BucketRef", "uint16", 0x2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.PrivateSlotMapRef", "uint16", 0x4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.HeatMapRef", "uint16", 0x6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.OwnerFreeList", "SINGLE_LIST_ENTRY32_26100[1]", 0x8, 4, 0),
    XX_PSW_FIELD("State.OwnerFreeList.Next", "STORED_PVOID32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Spare", "bitfield:0:12", 0x8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.Lock", "uint32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.SlotStandbyEntry", "SINGLE_LIST_ENTRY32_26100[1]", 0xC, 4, 0),
    XX_PSW_FIELD("State.SlotStandbyEntry.Next", "STORED_PVOID32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.PrivSlotListEntry", "HEAP_LFH_PTRREF_LIST32_26100[1]", 0xC, 4, 0),
    XX_PSW_FIELD("State.PrivSlotListEntry.Flink", "uint16", 0xC, 2, 0),
    XX_PSW_FIELD("State.PrivSlotListEntry.Blink", "uint16", 0xE, 2, 0),
    XX_PSW_FIELD("State.OwnerThreadId", "uint32", 0x10, 4, XX_PSW_ID),
    XX_PSW_FIELD("State.AvailableSubsegmentList", "LIST_ENTRY32_26100[1]", 0x14, 8, 0),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Flink", "STORED_PVOID32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.AvailableSubsegmentList.Blink", "STORED_PVOID32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList", "LIST_ENTRY32_26100[1]", 0x1C, 8, 0),
    XX_PSW_FIELD("State.FullSubsegmentList.Flink", "STORED_PVOID32", 0x1C, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("State.FullSubsegmentList.Blink", "STORED_PVOID32", 0x20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment", "HEAP_LFH_FAST_REF32_26100[1]", 0x24, 4, 0),
    XX_PSW_FIELD("ActiveSubsegment.Target", "STORED_PVOID32", 0x24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment.Value", "uint32", 0x24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ActiveSubsegment.RefCount", "bitfield:0:12", 0x24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhsw_lfh_owner32_26100[] = {
    XX_PSW_FIELD("BitfieldStorage0", "uint8", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BitfieldStorage1", "uint16", 0x8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("IsBucket", "bitfield:0:1", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BucketIndex", "bitfield:1:7", 0x0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("SlotCount", "uint8", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("AvailableSubsegmentCount", "uint8", 0x1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("BucketRef", "uint16", 0x2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivateSlotMapRef", "uint16", 0x4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("HeatMapRef", "uint16", 0x6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("OwnerFreeList", "SINGLE_LIST_ENTRY32_26100[1]", 0x8, 4, 0),
    XX_PSW_FIELD("OwnerFreeList.Next", "STORED_PVOID32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Spare", "bitfield:0:12", 0x8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Lock", "uint32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("SlotStandbyEntry", "SINGLE_LIST_ENTRY32_26100[1]", 0xC, 4, 0),
    XX_PSW_FIELD("SlotStandbyEntry.Next", "STORED_PVOID32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("PrivSlotListEntry", "HEAP_LFH_PTRREF_LIST32_26100[1]", 0xC, 4, 0),
    XX_PSW_FIELD("PrivSlotListEntry.Flink", "uint16", 0xC, 2, 0),
    XX_PSW_FIELD("PrivSlotListEntry.Blink", "uint16", 0xE, 2, 0),
    XX_PSW_FIELD("OwnerThreadId", "uint32", 0x10, 4, XX_PSW_ID),
    XX_PSW_FIELD("AvailableSubsegmentList", "LIST_ENTRY32_26100[1]", 0x14, 8, 0),
    XX_PSW_FIELD("AvailableSubsegmentList.Flink", "STORED_PVOID32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AvailableSubsegmentList.Blink", "STORED_PVOID32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullSubsegmentList", "LIST_ENTRY32_26100[1]", 0x1C, 8, 0),
    XX_PSW_FIELD("FullSubsegmentList.Flink", "STORED_PVOID32", 0x1C, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("FullSubsegmentList.Blink", "STORED_PVOID32", 0x20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_psw_nt_layout xx_pswhsw_profiles_19045[] = {
    {19045, 0x014c, "WOW64 HEAP_VS_SUBSEGMENT32; exact selected NTDLL PDB profile", xx_pswhsw_vs32_19045, XX_PSW_COUNT(xx_pswhsw_vs32_19045), 0x18u},
    {19045, 0x014c, "WOW64 HEAP_LFH_SUBSEGMENT32; exact selected NTDLL PDB profile", xx_pswhsw_lfh_subsegment32_19045, XX_PSW_COUNT(xx_pswhsw_lfh_subsegment32_19045),
     0x24u},
    {19045, 0x014c, "WOW64 HEAP_LFH_BUCKET32; exact selected NTDLL PDB profile", xx_pswhsw_lfh_bucket32_19045, XX_PSW_COUNT(xx_pswhsw_lfh_bucket32_19045), 0x38u},
    {19045, 0x014c, "WOW64 HEAP_LFH_AFFINITY_SLOT32; exact selected NTDLL PDB profile", xx_pswhsw_lfh_slot32_19045, XX_PSW_COUNT(xx_pswhsw_lfh_slot32_19045), 0x20u},
    {19045, 0x014c, "WOW64 HEAP_LFH_SUBSEGMENT_OWNER32; exact selected NTDLL PDB profile", xx_pswhsw_lfh_owner32_19045, XX_PSW_COUNT(xx_pswhsw_lfh_owner32_19045), 0x1Cu},
};

static const xx_psw_nt_layout xx_pswhsw_profiles_26100[] = {
    {26100, 0x014c, "WOW64 HEAP_VS_SUBSEGMENT32; exact selected NTDLL PDB profile", xx_pswhsw_vs32_26100, XX_PSW_COUNT(xx_pswhsw_vs32_26100), 0x18u},
    {26100, 0x014c, "WOW64 HEAP_LFH_SUBSEGMENT32; exact selected NTDLL PDB profile", xx_pswhsw_lfh_subsegment32_26100, XX_PSW_COUNT(xx_pswhsw_lfh_subsegment32_26100),
     0x34u},
    {26100, 0x014c, "WOW64 HEAP_LFH_BUCKET32; exact selected NTDLL PDB profile", xx_pswhsw_lfh_bucket32_26100, XX_PSW_COUNT(xx_pswhsw_lfh_bucket32_26100), 0x48u},
    {26100, 0x014c, "WOW64 HEAP_LFH_AFFINITY_SLOT32; exact selected NTDLL PDB profile", xx_pswhsw_lfh_slot32_26100, XX_PSW_COUNT(xx_pswhsw_lfh_slot32_26100), 0x28u},
    {26100, 0x014c, "WOW64 HEAP_LFH_SUBSEGMENT_OWNER32; exact selected NTDLL PDB profile", xx_pswhsw_lfh_owner32_26100, XX_PSW_COUNT(xx_pswhsw_lfh_owner32_26100), 0x24u},
};

static const xx_psw_nt_layout *xx_pswhsw_profile(const xx_psw_nt_layout *parent, unsigned width, size_t kind)
{
    if (!parent || width != 4 || kind >= XX_PSW_COUNT(xx_pswhsw_profiles_19045)) return NULL;
    if (parent->fields == xx_pswh_wow_segment32_19045) return xx_pswhsw_profiles_19045 + kind;
    if (parent->fields == xx_pswh_wow_segment32_26100) return xx_pswhsw_profiles_26100 + kind;
    return NULL;
}

static const xx_psw_nt_layout *xx_pswhsa_wow_vs(const xx_psw_nt_layout *parent, unsigned width)
{
    return xx_pswhsw_profile(parent, width, 0);
}
static const xx_psw_nt_layout *xx_pswhsa_wow_lfh_subsegment(const xx_psw_nt_layout *parent, unsigned width)
{
    return xx_pswhsw_profile(parent, width, 1);
}
static const xx_psw_nt_layout *xx_pswhsa_wow_lfh_bucket(const xx_psw_nt_layout *parent, unsigned width)
{
    return xx_pswhsw_profile(parent, width, 2);
}
static const xx_psw_nt_layout *xx_pswhsa_wow_lfh_slot(const xx_psw_nt_layout *parent, unsigned width)
{
    return xx_pswhsw_profile(parent, width, 3);
}
static const xx_psw_nt_layout *xx_pswhsa_wow_lfh_owner(const xx_psw_nt_layout *parent, unsigned width)
{
    return xx_pswhsw_profile(parent, width, 4);
}

#endif /* XX_PROCESS_WOW64_HEAP_SUBALLOCATOR_LAYOUTS_H */
