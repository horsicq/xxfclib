/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft PDB constants, generated from _mylibs/XPDB/xpdb_def.h
 * (XPDB_DEF::NAME -> XX_MS_PDB_NAME). Layout reference:
 * https://github.com/Microsoft/microsoft-pdb
 */
#ifndef XX_MICROSOFT_PDB_DEF_H
#define XX_MICROSOFT_PDB_DEF_H

/* MSF magic strings (including the terminating padding bytes) */
#define XX_MS_PDB_MAGIC_SIZE_PDB7 32 /* "Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0" */
#define XX_MS_PDB_MAGIC_SIZE_PDB2 44 /* "Microsoft C/C++ program database 2.00\r\n\x1aJG\0\0" */

/* Well known stream indexes */
#define XX_MS_PDB_STREAM_ROOT 0 /* Old MSF directory */
#define XX_MS_PDB_STREAM_PDB 1  /* PDB info stream */
#define XX_MS_PDB_STREAM_TPI 2  /* Type info stream */
#define XX_MS_PDB_STREAM_DBI 3  /* Debug info stream */
#define XX_MS_PDB_STREAM_IPI 4  /* Id info stream */

#define XX_MS_PDB_INVALID_STREAM_SIZE 0xFFFFFFFF
#define XX_MS_PDB_INVALID_STREAM_INDEX 0xFFFF

/* PDB info stream (stream 1) versions */
/* PDB_IMPL_VERSION */
#define XX_MS_PDB_PDB_IMPL_VC2 19941610
#define XX_MS_PDB_PDB_IMPL_VC4 19950623
#define XX_MS_PDB_PDB_IMPL_VC41 19950814
#define XX_MS_PDB_PDB_IMPL_VC50 19960307
#define XX_MS_PDB_PDB_IMPL_VC98 19970604
#define XX_MS_PDB_PDB_IMPL_VC70DEP 19990604
#define XX_MS_PDB_PDB_IMPL_VC70 20000404
#define XX_MS_PDB_PDB_IMPL_VC80 20030901
#define XX_MS_PDB_PDB_IMPL_VC110 20091201
#define XX_MS_PDB_PDB_IMPL_VC140 20140508

/* Feature codes stored at the end of the PDB info stream */
/* PDB_FEATURE */
#define XX_MS_PDB_PDB_FEATURE_VC110 20091201
#define XX_MS_PDB_PDB_FEATURE_VC140 20140508
#define XX_MS_PDB_PDB_FEATURE_NOTYPEMERGE 0x4D544F4E      /* "NOTM" */
#define XX_MS_PDB_PDB_FEATURE_MINIMALDEBUGINFO 0x494E494D /* "MINI" */

/* TPI/IPI stream versions */
/* TPI_VERSION */
#define XX_MS_PDB_TPI_V40 19950410
#define XX_MS_PDB_TPI_V41 19951122
#define XX_MS_PDB_TPI_V50 19961031
#define XX_MS_PDB_TPI_V70 19990903
#define XX_MS_PDB_TPI_V80 20040203

/* DBI stream versions */
/* DBI_VERSION */
#define XX_MS_PDB_DBI_V41 930803
#define XX_MS_PDB_DBI_V50 19960307
#define XX_MS_PDB_DBI_V60 19970606
#define XX_MS_PDB_DBI_V70 19990903
#define XX_MS_PDB_DBI_V110 20091201

/* Section contribution substream versions */
/* DBI_SECCONTRIB_VERSION */
#define XX_MS_PDB_DBI_SECCONTRIB_VER60 0xEFFE0000 + 19970605
#define XX_MS_PDB_DBI_SECCONTRIB_V2 0xEFFE0000 + 20140516

/* DBI header flags */
/* DBI_FLAG */
#define XX_MS_PDB_DBI_FLAG_INCREMENTAL 0x0001
#define XX_MS_PDB_DBI_FLAG_STRIPPED 0x0002
#define XX_MS_PDB_DBI_FLAG_CTYPES 0x0004

/* Indexes inside the optional debug header substream of the DBI stream */
/* DBG_INDEX */
#define XX_MS_PDB_DBG_INDEX_FPO 0
#define XX_MS_PDB_DBG_INDEX_EXCEPTION 1
#define XX_MS_PDB_DBG_INDEX_FIXUP 2
#define XX_MS_PDB_DBG_INDEX_OMAPTOSRC 3
#define XX_MS_PDB_DBG_INDEX_OMAPFROMSRC 4
#define XX_MS_PDB_DBG_INDEX_SECTIONHDR 5
#define XX_MS_PDB_DBG_INDEX_TOKENRIDMAP 6
#define XX_MS_PDB_DBG_INDEX_XDATA 7
#define XX_MS_PDB_DBG_INDEX_PDATA 8
#define XX_MS_PDB_DBG_INDEX_NEWFPO 9
#define XX_MS_PDB_DBG_INDEX_SECTIONHDRORIG 10
#define XX_MS_PDB_DBG_INDEX_MAX 11

/* Signature of the /names string table stream */
#define XX_MS_PDB_NAMES_SIGNATURE 0xEFFEEFFE

/* CodeView symbol record kinds (subset, SYM_ENUM_e) */
/* SYM_KIND */
#define XX_MS_PDB_S_COMPILE 0x0001
#define XX_MS_PDB_S_END 0x0006
#define XX_MS_PDB_S_OBJNAME 0x1101
#define XX_MS_PDB_S_THUNK32 0x1102
#define XX_MS_PDB_S_BLOCK32 0x1103
#define XX_MS_PDB_S_LABEL32 0x1105
#define XX_MS_PDB_S_REGISTER 0x1106
#define XX_MS_PDB_S_CONSTANT 0x1107
#define XX_MS_PDB_S_UDT 0x1108
#define XX_MS_PDB_S_LDATA32 0x110C
#define XX_MS_PDB_S_GDATA32 0x110D
#define XX_MS_PDB_S_PUB32 0x110E
#define XX_MS_PDB_S_LPROC32 0x110F
#define XX_MS_PDB_S_GPROC32 0x1110
#define XX_MS_PDB_S_REGREL32 0x1111
#define XX_MS_PDB_S_LTHREAD32 0x1112
#define XX_MS_PDB_S_GTHREAD32 0x1113
#define XX_MS_PDB_S_PROCREF 0x1125
#define XX_MS_PDB_S_DATAREF 0x1126
#define XX_MS_PDB_S_LPROCREF 0x1127
#define XX_MS_PDB_S_TRAMPOLINE 0x112C
#define XX_MS_PDB_S_SEPCODE 0x1132
#define XX_MS_PDB_S_LOCAL 0x113E
#define XX_MS_PDB_S_DEFRANGE 0x113F
#define XX_MS_PDB_S_SECTION 0x1136
#define XX_MS_PDB_S_COFFGROUP 0x1137
#define XX_MS_PDB_S_EXPORT 0x1138
#define XX_MS_PDB_S_CALLSITEINFO 0x1139
#define XX_MS_PDB_S_FRAMECOOKIE 0x113A
#define XX_MS_PDB_S_COMPILE3 0x113C
#define XX_MS_PDB_S_ENVBLOCK 0x113D
#define XX_MS_PDB_S_FRAMEPROC 0x1012
#define XX_MS_PDB_S_CALLEES 0x115A
#define XX_MS_PDB_S_CALLERS 0x115B
#define XX_MS_PDB_S_HEAPALLOCSITE 0x115E
#define XX_MS_PDB_S_FILESTATIC 0x1153
#define XX_MS_PDB_S_LPROC32_ID 0x1146
#define XX_MS_PDB_S_GPROC32_ID 0x1147
#define XX_MS_PDB_S_BUILDINFO 0x114C
#define XX_MS_PDB_S_INLINESITE 0x114D
#define XX_MS_PDB_S_INLINESITE_END 0x114E
#define XX_MS_PDB_S_PROC_ID_END 0x114F
#define XX_MS_PDB_S_UNAMESPACE 0x1124
#define XX_MS_PDB_S_ANNOTATION 0x1019

/* CodeView public symbol flags (CV_PUBSYMFLAGS_e) */
/* PUB_FLAG */
#define XX_MS_PDB_PUB_FLAG_CODE 0x00000001
#define XX_MS_PDB_PUB_FLAG_FUNCTION 0x00000002
#define XX_MS_PDB_PUB_FLAG_MANAGED 0x00000004
#define XX_MS_PDB_PUB_FLAG_MSIL 0x00000008

/* CodeView type record kinds (subset, LEAF_ENUM_e) */
/* LEAF_KIND */
#define XX_MS_PDB_LF_MODIFIER 0x1001
#define XX_MS_PDB_LF_POINTER 0x1002
#define XX_MS_PDB_LF_PROCEDURE 0x1008
#define XX_MS_PDB_LF_MFUNCTION 0x1009
#define XX_MS_PDB_LF_ARGLIST 0x1201
#define XX_MS_PDB_LF_FIELDLIST 0x1203
#define XX_MS_PDB_LF_BITFIELD 0x1205
#define XX_MS_PDB_LF_METHODLIST 0x1206
#define XX_MS_PDB_LF_BCLASS 0x1400
#define XX_MS_PDB_LF_VBCLASS 0x1401
#define XX_MS_PDB_LF_IVBCLASS 0x1402
#define XX_MS_PDB_LF_INDEX 0x1404
#define XX_MS_PDB_LF_VFUNCTAB 0x1409
#define XX_MS_PDB_LF_ENUMERATE 0x1502
#define XX_MS_PDB_LF_ARRAY 0x1503
#define XX_MS_PDB_LF_CLASS 0x1504
#define XX_MS_PDB_LF_STRUCTURE 0x1505
#define XX_MS_PDB_LF_UNION 0x1506
#define XX_MS_PDB_LF_ENUM 0x1507
#define XX_MS_PDB_LF_MEMBER 0x150D
#define XX_MS_PDB_LF_STMEMBER 0x150E
#define XX_MS_PDB_LF_METHOD 0x150F
#define XX_MS_PDB_LF_NESTTYPE 0x1510
#define XX_MS_PDB_LF_ONEMETHOD 0x1511
#define XX_MS_PDB_LF_VFTABLE 0x151D
#define XX_MS_PDB_LF_VTSHAPE 0x000A
#define XX_MS_PDB_LF_TYPESERVER2 0x1515
#define XX_MS_PDB_LF_INTERFACE 0x1519
#define XX_MS_PDB_LF_FUNC_ID 0x1601
#define XX_MS_PDB_LF_MFUNC_ID 0x1602
#define XX_MS_PDB_LF_BUILDINFO 0x1603
#define XX_MS_PDB_LF_SUBSTR_LIST 0x1604
#define XX_MS_PDB_LF_STRING_ID 0x1605
#define XX_MS_PDB_LF_UDT_SRC_LINE 0x1606
#define XX_MS_PDB_LF_UDT_MOD_SRC_LINE 0x1607
/* Newer UDT records (VS 2019 and later). The property field is 4 bytes and */
/* there is no member count, so the layout differs from LF_CLASS/STRUCTURE. */
#define XX_MS_PDB_LF_CLASS2 0x1608
#define XX_MS_PDB_LF_STRUCTURE2 0x1609
#define XX_MS_PDB_LF_UNION2 0x160A
#define XX_MS_PDB_LF_INTERFACE2 0x160B

/* Legacy type record kinds of the "program database 2.00" (JG) era. They are */
/* decoded only for XX_MS_PDB_TYPE_PDB2 files, where every record is */
/* normalised to its current counterpart when the types are loaded: _16T = */
/* 16 bit type indices (C7 .. VC 4.x), _ST = length prefixed name with 32 bit */
/* type indices (VC 5 .. VC 7.0). */
/* LEAF_KIND_LEGACY */
#define XX_MS_PDB_LF_MODIFIER_16T 0x0001
#define XX_MS_PDB_LF_POINTER_16T 0x0002
#define XX_MS_PDB_LF_ARRAY_16T 0x0003
#define XX_MS_PDB_LF_CLASS_16T 0x0004
#define XX_MS_PDB_LF_STRUCTURE_16T 0x0005
#define XX_MS_PDB_LF_UNION_16T 0x0006
#define XX_MS_PDB_LF_ENUM_16T 0x0007
#define XX_MS_PDB_LF_PROCEDURE_16T 0x0008
#define XX_MS_PDB_LF_MFUNCTION_16T 0x0009
#define XX_MS_PDB_LF_ARGLIST_16T 0x0201
#define XX_MS_PDB_LF_FIELDLIST_16T 0x0204
#define XX_MS_PDB_LF_BITFIELD_16T 0x0206
#define XX_MS_PDB_LF_METHODLIST_16T 0x0207
#define XX_MS_PDB_LF_BCLASS_16T 0x0400
#define XX_MS_PDB_LF_VBCLASS_16T 0x0401
#define XX_MS_PDB_LF_IVBCLASS_16T 0x0402
#define XX_MS_PDB_LF_ENUMERATE_ST 0x0403
#define XX_MS_PDB_LF_FRIENDFCN_16T 0x0404
#define XX_MS_PDB_LF_INDEX_16T 0x0405
#define XX_MS_PDB_LF_MEMBER_16T 0x0406
#define XX_MS_PDB_LF_STMEMBER_16T 0x0407
#define XX_MS_PDB_LF_METHOD_16T 0x0408
#define XX_MS_PDB_LF_NESTTYPE_16T 0x0409
#define XX_MS_PDB_LF_VFUNCTAB_16T 0x040A
#define XX_MS_PDB_LF_FRIENDCLS_16T 0x040B
#define XX_MS_PDB_LF_ONEMETHOD_16T 0x040C
#define XX_MS_PDB_LF_VFUNCOFF_16T 0x040D
#define XX_MS_PDB_LF_ARRAY_ST 0x1003
#define XX_MS_PDB_LF_CLASS_ST 0x1004
#define XX_MS_PDB_LF_STRUCTURE_ST 0x1005
#define XX_MS_PDB_LF_UNION_ST 0x1006
#define XX_MS_PDB_LF_ENUM_ST 0x1007
#define XX_MS_PDB_LF_FRIENDFCN_ST 0x1403
#define XX_MS_PDB_LF_MEMBER_ST 0x1405
#define XX_MS_PDB_LF_STMEMBER_ST 0x1406
#define XX_MS_PDB_LF_METHOD_ST 0x1407
#define XX_MS_PDB_LF_NESTTYPE_ST 0x1408
#define XX_MS_PDB_LF_FRIENDCLS 0x140A
#define XX_MS_PDB_LF_ONEMETHOD_ST 0x140B
#define XX_MS_PDB_LF_VFUNCOFF 0x140C
#define XX_MS_PDB_LF_NESTTYPEEX_ST 0x140D
#define XX_MS_PDB_LF_MEMBERMODIFY_ST 0x140E

/* Legacy symbol record kinds, normalised the same way (JG files only) */
/* SYM_KIND_LEGACY */
#define XX_MS_PDB_S_CONSTANT_16T 0x0003
#define XX_MS_PDB_S_UDT_16T 0x0004
#define XX_MS_PDB_S_OBJNAME_ST 0x0009
#define XX_MS_PDB_S_LDATA32_16T 0x0201
#define XX_MS_PDB_S_GDATA32_16T 0x0202
#define XX_MS_PDB_S_PUB32_16T 0x0203
#define XX_MS_PDB_S_LPROC32_16T 0x0204
#define XX_MS_PDB_S_GPROC32_16T 0x0205
#define XX_MS_PDB_S_THUNK32_ST 0x0206
#define XX_MS_PDB_S_LABEL32_ST 0x0209
#define XX_MS_PDB_S_LTHREAD32_16T 0x020D
#define XX_MS_PDB_S_GTHREAD32_16T 0x020E
#define XX_MS_PDB_S_PROCREF_ST 0x0400
#define XX_MS_PDB_S_DATAREF_ST 0x0401
#define XX_MS_PDB_S_LPROCREF_ST 0x0403
#define XX_MS_PDB_S_CONSTANT_ST 0x1002
#define XX_MS_PDB_S_UDT_ST 0x1003
#define XX_MS_PDB_S_LDATA32_ST 0x1007
#define XX_MS_PDB_S_GDATA32_ST 0x1008
#define XX_MS_PDB_S_PUB32_ST 0x1009
#define XX_MS_PDB_S_LPROC32_ST 0x100A
#define XX_MS_PDB_S_GPROC32_ST 0x100B
#define XX_MS_PDB_S_LTHREAD32_ST 0x100E
#define XX_MS_PDB_S_GTHREAD32_ST 0x100F

/* Numeric leaves used inside type records */
/* LEAF_NUMERIC */
#define XX_MS_PDB_LF_NUMERIC 0x8000
#define XX_MS_PDB_LF_CHAR 0x8000
#define XX_MS_PDB_LF_SHORT 0x8001
#define XX_MS_PDB_LF_USHORT 0x8002
#define XX_MS_PDB_LF_LONG 0x8003
#define XX_MS_PDB_LF_ULONG 0x8004
#define XX_MS_PDB_LF_REAL32 0x8005
#define XX_MS_PDB_LF_REAL64 0x8006
#define XX_MS_PDB_LF_REAL80 0x8007
#define XX_MS_PDB_LF_REAL128 0x8008
#define XX_MS_PDB_LF_QUADWORD 0x8009
#define XX_MS_PDB_LF_UQUADWORD 0x800A

/* First type index used by the TPI stream. Everything below it is a primitive */
/* type encoded in the index itself. */
#define XX_MS_PDB_FIRST_TYPE_INDEX 0x1000

/* Primitive type indexes (T_*), see cvinfo.h */
/* PRIMITIVE_TYPE */
#define XX_MS_PDB_T_NOTYPE 0x0000
#define XX_MS_PDB_T_ABS 0x0001
#define XX_MS_PDB_T_SEGMENT 0x0002
#define XX_MS_PDB_T_VOID 0x0003
#define XX_MS_PDB_T_CURRENCY 0x0004
#define XX_MS_PDB_T_NBASICSTR 0x0005
#define XX_MS_PDB_T_FBASICSTR 0x0006
#define XX_MS_PDB_T_NOTTRANS 0x0007
#define XX_MS_PDB_T_HRESULT 0x0008
#define XX_MS_PDB_T_CHAR 0x0010
#define XX_MS_PDB_T_SHORT 0x0011
#define XX_MS_PDB_T_LONG 0x0012
#define XX_MS_PDB_T_QUAD 0x0013
#define XX_MS_PDB_T_OCT 0x0014
#define XX_MS_PDB_T_UCHAR 0x0020
#define XX_MS_PDB_T_USHORT 0x0021
#define XX_MS_PDB_T_ULONG 0x0022
#define XX_MS_PDB_T_UQUAD 0x0023
#define XX_MS_PDB_T_UOCT 0x0024
#define XX_MS_PDB_T_BOOL08 0x0030
#define XX_MS_PDB_T_BOOL16 0x0031
#define XX_MS_PDB_T_BOOL32 0x0032
#define XX_MS_PDB_T_BOOL64 0x0033
#define XX_MS_PDB_T_REAL32 0x0040
#define XX_MS_PDB_T_REAL64 0x0041
#define XX_MS_PDB_T_REAL80 0x0042
#define XX_MS_PDB_T_REAL128 0x0043
#define XX_MS_PDB_T_REAL48 0x0044
#define XX_MS_PDB_T_REAL32PP 0x0045
#define XX_MS_PDB_T_REAL16 0x0046
#define XX_MS_PDB_T_CPLX32 0x0050
#define XX_MS_PDB_T_CPLX64 0x0051
#define XX_MS_PDB_T_CPLX80 0x0052
#define XX_MS_PDB_T_CPLX128 0x0053
#define XX_MS_PDB_T_BIT 0x0060
#define XX_MS_PDB_T_PASCHAR 0x0061
#define XX_MS_PDB_T_BOOL32FF 0x0062
#define XX_MS_PDB_T_INT1 0x0068
#define XX_MS_PDB_T_UINT1 0x0069
#define XX_MS_PDB_T_RCHAR 0x0070
#define XX_MS_PDB_T_WCHAR 0x0071
#define XX_MS_PDB_T_INT2 0x0072
#define XX_MS_PDB_T_UINT2 0x0073
#define XX_MS_PDB_T_INT4 0x0074
#define XX_MS_PDB_T_UINT4 0x0075
#define XX_MS_PDB_T_INT8 0x0076
#define XX_MS_PDB_T_UINT8 0x0077
#define XX_MS_PDB_T_INT16 0x0078
#define XX_MS_PDB_T_UINT16 0x0079
#define XX_MS_PDB_T_CHAR16 0x007A
#define XX_MS_PDB_T_CHAR32 0x007B
#define XX_MS_PDB_T_CHAR8 0x007C

/* Pointer mode bits of a primitive type index (bits 8..10) */
/* PRIMITIVE_PTRMODE */
#define XX_MS_PDB_PRIMITIVE_PTR_NEAR 0x0100
#define XX_MS_PDB_PRIMITIVE_PTR_FAR 0x0200
#define XX_MS_PDB_PRIMITIVE_PTR_HUGE 0x0300
#define XX_MS_PDB_PRIMITIVE_PTR_NEAR32 0x0400
#define XX_MS_PDB_PRIMITIVE_PTR_FAR32 0x0500
#define XX_MS_PDB_PRIMITIVE_PTR_NEAR64 0x0600

#define XX_MS_PDB_PRIMITIVE_MODE_MASK 0x0700
#define XX_MS_PDB_PRIMITIVE_TYPE_MASK 0x00FF

/* DIA BasicType values. XPDB reports these so that consumers can share the */
/* naming tables used by the DIA based readers. */
/* BASIC_TYPE */
#define XX_MS_PDB_btNoType 0
#define XX_MS_PDB_btVoid 1
#define XX_MS_PDB_btChar 2
#define XX_MS_PDB_btWChar 3
#define XX_MS_PDB_btInt 6
#define XX_MS_PDB_btUInt 7
#define XX_MS_PDB_btFloat 8
#define XX_MS_PDB_btBCD 9
#define XX_MS_PDB_btBool 10
#define XX_MS_PDB_btLong 13
#define XX_MS_PDB_btULong 14
#define XX_MS_PDB_btCurrency 25
#define XX_MS_PDB_btDate 26
#define XX_MS_PDB_btVariant 27
#define XX_MS_PDB_btComplex 28
#define XX_MS_PDB_btBit 29
#define XX_MS_PDB_btBSTR 30
#define XX_MS_PDB_btHresult 31
#define XX_MS_PDB_btChar16 32
#define XX_MS_PDB_btChar32 33
#define XX_MS_PDB_btChar8 34

/* UDT kinds, the values match DIA's UdtKind */
/* UDT_KIND */
#define XX_MS_PDB_UDT_KIND_STRUCT 0
#define XX_MS_PDB_UDT_KIND_CLASS 1
#define XX_MS_PDB_UDT_KIND_UNION 2
#define XX_MS_PDB_UDT_KIND_INTERFACE 3

/* Property bits of an UDT record (CV_prop_t) */
/* UDT_PROPERTY */
#define XX_MS_PDB_UDT_PROPERTY_PACKED 0x0001
#define XX_MS_PDB_UDT_PROPERTY_CTOR 0x0002
#define XX_MS_PDB_UDT_PROPERTY_OVLOPS 0x0004
#define XX_MS_PDB_UDT_PROPERTY_ISNESTED 0x0008
#define XX_MS_PDB_UDT_PROPERTY_CNESTED 0x0010
#define XX_MS_PDB_UDT_PROPERTY_OPASSIGN 0x0020
#define XX_MS_PDB_UDT_PROPERTY_OPCAST 0x0040
#define XX_MS_PDB_UDT_PROPERTY_FWDREF 0x0080
#define XX_MS_PDB_UDT_PROPERTY_SCOPED 0x0100
#define XX_MS_PDB_UDT_PROPERTY_HASUNIQUENAME 0x0200
#define XX_MS_PDB_UDT_PROPERTY_SEALED 0x0400
#define XX_MS_PDB_UDT_PROPERTY_INTRINSIC 0x2000

/* Pointer attribute bits (LF_POINTER) */
/* PTR_ATTR */
#define XX_MS_PDB_PTR_ATTR_KIND_MASK 0x0000001F
#define XX_MS_PDB_PTR_ATTR_MODE_MASK 0x000000E0
#define XX_MS_PDB_PTR_ATTR_MODE_SHIFT 5
#define XX_MS_PDB_PTR_ATTR_FLAT32 0x00000100
#define XX_MS_PDB_PTR_ATTR_VOLATILE 0x00000200
#define XX_MS_PDB_PTR_ATTR_CONST 0x00000400
#define XX_MS_PDB_PTR_ATTR_UNALIGNED 0x00000800
#define XX_MS_PDB_PTR_ATTR_RESTRICT 0x00001000
#define XX_MS_PDB_PTR_ATTR_SIZE_MASK 0x0007E000
#define XX_MS_PDB_PTR_ATTR_SIZE_SHIFT 13

/* Pointer kinds (CV_ptrtype_e), the low bits of the pointer attributes */
/* CV_PTRTYPE */
#define XX_MS_PDB_CV_PTR_NEAR 0x00
#define XX_MS_PDB_CV_PTR_FAR 0x01
#define XX_MS_PDB_CV_PTR_HUGE 0x02
#define XX_MS_PDB_CV_PTR_BASE_SEG 0x03
#define XX_MS_PDB_CV_PTR_BASE_VAL 0x04
#define XX_MS_PDB_CV_PTR_BASE_SEGVAL 0x05
#define XX_MS_PDB_CV_PTR_BASE_ADDR 0x06
#define XX_MS_PDB_CV_PTR_BASE_SEGADDR 0x07
#define XX_MS_PDB_CV_PTR_BASE_TYPE 0x08
#define XX_MS_PDB_CV_PTR_BASE_SELF 0x09
#define XX_MS_PDB_CV_PTR_NEAR32 0x0A
#define XX_MS_PDB_CV_PTR_FAR32 0x0B
#define XX_MS_PDB_CV_PTR_64 0x0C

/* PTR_MODE */
#define XX_MS_PDB_PTR_MODE_POINTER 0
#define XX_MS_PDB_PTR_MODE_LVALUE_REFERENCE 1
#define XX_MS_PDB_PTR_MODE_MEMBER_DATA_POINTER 2
#define XX_MS_PDB_PTR_MODE_MEMBER_FUNCTION_POINTER 3
#define XX_MS_PDB_PTR_MODE_RVALUE_REFERENCE 4

/* Modifier attribute bits (LF_MODIFIER) */
/* MOD_ATTR */
#define XX_MS_PDB_MOD_ATTR_CONST 0x0001
#define XX_MS_PDB_MOD_ATTR_VOLATILE 0x0002
#define XX_MS_PDB_MOD_ATTR_UNALIGNED 0x0004

/* Field attribute bits (CV_fldattr_t) */
/* FLD_ATTR */
#define XX_MS_PDB_FLD_ATTR_ACCESS_MASK 0x0003
#define XX_MS_PDB_FLD_ATTR_MPROP_MASK 0x001C
#define XX_MS_PDB_FLD_ATTR_MPROP_SHIFT 2

/* FLD_ACCESS */
#define XX_MS_PDB_FLD_ACCESS_NONE 0
#define XX_MS_PDB_FLD_ACCESS_PRIVATE 1
#define XX_MS_PDB_FLD_ACCESS_PROTECTED 2
#define XX_MS_PDB_FLD_ACCESS_PUBLIC 3

/* FLD_MPROP */
#define XX_MS_PDB_FLD_MPROP_VANILLA 0
#define XX_MS_PDB_FLD_MPROP_VIRTUAL 1
#define XX_MS_PDB_FLD_MPROP_STATIC 2
#define XX_MS_PDB_FLD_MPROP_FRIEND 3
#define XX_MS_PDB_FLD_MPROP_INTRO 4
#define XX_MS_PDB_FLD_MPROP_PUREVIRT 5
#define XX_MS_PDB_FLD_MPROP_PUREINTRO 6

/* Padding leaves inside a field list */
#define XX_MS_PDB_LF_PAD_MASK 0xF0

/* MSF 7.00 super block, 56 bytes */

/* MSF 2.00 header, 56 bytes */

/* PDB info stream header, 28 bytes (the GUID is present for VC70 and above) */

/* TPI/IPI stream header, 56 bytes */

/* DBI stream header, 64 bytes */

/* Section contribution entry, 28 bytes (32 bytes for the V2 substream) */

/* Fixed part of a module info record, 64 bytes, followed by two ASCIIZ names */

#define XX_MS_PDB_MODINFO_HEADER_SIZE 64

/* Section map entry, 20 bytes */

/* Publics stream header, 28 bytes */

/* Global symbol index hash header, 16 bytes */

#define XX_MS_PDB_GSI_HASH_SIGNATURE 0xFFFFFFFF
#define XX_MS_PDB_GSI_HASH_VERSION_V70 0xF12F091A

/* COFF section header stored inside the "section headers" debug stream, 40 bytes */

#define XX_MS_PDB_IMAGE_SECTION_HEADER_SIZE 40

/* /names stream header, 12 bytes */

/* Portable PDB: an ECMA-335 metadata file, not an MSF container. The debug */
/* information lives in metadata tables instead of CodeView records. */
/* https://github.com/dotnet/runtime/blob/main/docs/design/specs/PortablePdb-Metadata.md */
#define XX_MS_PDB_PORTABLE_SIGNATURE 0x424A5342 /* "BSJB" */
#define XX_MS_PDB_PORTABLE_VERSION_OFFSET 16

/* Heap size flags of the table stream header */
/* PORTABLE_HEAPSIZE */
#define XX_MS_PDB_PORTABLE_HEAPSIZE_STRINGS 0x01 /* #Strings uses 4 byte indexes */
#define XX_MS_PDB_PORTABLE_HEAPSIZE_GUID 0x02
#define XX_MS_PDB_PORTABLE_HEAPSIZE_BLOB 0x04

/* Metadata table indexes. Everything below TABLE_DOCUMENT belongs to the type */
/* system and is only present in a combined(assembly + debug) metadata file. */
/* PORTABLE_TABLE */
#define XX_MS_PDB_TABLE_MODULE 0x00
#define XX_MS_PDB_TABLE_TYPEREF 0x01
#define XX_MS_PDB_TABLE_TYPEDEF 0x02
#define XX_MS_PDB_TABLE_FIELD 0x04
#define XX_MS_PDB_TABLE_METHODDEF 0x06
#define XX_MS_PDB_TABLE_PARAM 0x08
#define XX_MS_PDB_TABLE_DOCUMENT 0x30
#define XX_MS_PDB_TABLE_METHODDEBUGINFORMATION 0x31
#define XX_MS_PDB_TABLE_LOCALSCOPE 0x32
#define XX_MS_PDB_TABLE_LOCALVARIABLE 0x33
#define XX_MS_PDB_TABLE_LOCALCONSTANT 0x34
#define XX_MS_PDB_TABLE_IMPORTSCOPE 0x35
#define XX_MS_PDB_TABLE_STATEMACHINEMETHOD 0x36
#define XX_MS_PDB_TABLE_CUSTOMDEBUGINFORMATION 0x37
#define XX_MS_PDB_TABLE_MAX 0x40

/* Well known stream names of a portable PDB */

#endif
