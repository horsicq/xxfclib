/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/*
 * Original target-memory descriptor code, derived from public ABI facts.
 * Include after xx_psw_unicode and before xx_psw_loader in the Windows collector.
 *
 * Concrete symbol-derived process parameter layouts (consulted 2026-10-20):
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_RTL_USER_PROCESS_PARAMETERS
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_RTL_USER_PROCESS_PARAMETERS
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_RTL_USER_PROCESS_PARAMETERS
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_CURDIR
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_RTL_DRIVE_LETTER_CURDIR
 *
 * Fixed-width WOW64 declarations, field semantics and flag masks:
 * https://github.com/winsiderss/phnt/blob/master/ntrtl.h
 * https://github.com/winsiderss/phnt/blob/master/ntwow64.h
 * https://github.com/winsiderss/systeminformer/blob/master/phnt/include/ntwow64.h
 * https://github.com/winsiderss/phnt/blob/master/LICENSE (MIT)
 *
 * The 26100 WOW64 view is an explicitly labeled phnt32 prefix through
 * HeapMemoryTypeMask (Windows 11 22H2+), not a concrete full symbol profile.
 * Its fixed 32-bit ABI ends at 0x2c4. The newer, unversioned AttributeList is
 * deliberately excluded. Native x86 19045 and WOW64 share the 0x2c0 layout.
 * No 26100 native x86 or ARM64 private parameter profile is asserted here.
 * CurrentDirectories uses phnt's spelling; concrete symbols use CurrentDirectores.
 * Drive DosPath is ANSI STRING, while CURDIR DosPath is UNICODE_STRING.
 *
 * Published fallback definition:
 * https://learn.microsoft.com/windows/win32/api/winternl/ns-winternl-rtl_user_process_parameters
 * No third-party declarations, implementation code or prose comments are copied.
 */
#ifndef XX_PROCESS_PARAMS_LAYOUTS_H
#define XX_PROCESS_PARAMS_LAYOUTS_H

#define XX_PSWP_PARAMS32_19045_SIZE 0x2c0u
#define XX_PSWP_PARAMS64_19045_SIZE 0x440u
#define XX_PSWP_PARAMS32_26100_PREFIX_SIZE 0x2c4u
#define XX_PSWP_PARAMS64_26100_SIZE 0x448u

/* 32-bit build 19045: 87 descriptors, including nested string fields. */
static const xx_data_struct_field_desc xx_pswp_params32_19045[] = {
    XX_PSW_FIELD("MaximumLength", "ULONG", 0x0, 4, XX_PSW_SIZE),
    XX_PSW_FIELD("Length", "ULONG", 0x4, 4, XX_PSW_SIZE),
    XX_PSW_FIELD("Flags", "ULONG", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Normalized", "bitfield:0:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileUser", "bitfield:1:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileKernel", "bitfield:2:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileServer", "bitfield:3:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Reserve1MB", "bitfield:5:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Reserve16MB", "bitfield:6:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.CaseSensitive", "bitfield:7:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DisableHeapDecommit", "bitfield:8:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DllRedirectionLocal", "bitfield:12:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.AppManifestPresent", "bitfield:13:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ImageKeyMissing", "bitfield:14:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DevOverrideEnabled", "bitfield:15:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.OptInProcess", "bitfield:17:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.SessionOwner", "bitfield:18:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.HandleUserCallbackExceptions", "bitfield:19:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProtectedProcess", "bitfield:22:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.SecureProcess", "bitfield:31:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("DebugFlags", "ULONG", 0xc, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ConsoleHandle", "HANDLE32", 0x10, 4, XX_PSW_ID),
    XX_PSW_FIELD("ConsoleFlags", "ULONG", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("StandardInput", "HANDLE32", 0x18, 4, XX_PSW_ID),
    XX_PSW_FIELD("StandardOutput", "HANDLE32", 0x1c, 4, XX_PSW_ID),
    XX_PSW_FIELD("StandardError", "HANDLE32", 0x20, 4, XX_PSW_ID),
    XX_PSW_FIELD("CurrentDirectory", "CURDIR32[1]", 0x24, 12, 0),
    XX_PSW_FIELD("CurrentDirectory.DosPath", "UNICODE_STRING32[1]", 0x24, 8, 0),
    XX_PSW_FIELD("CurrentDirectory.DosPath.Length", "USHORT", 0x24, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CurrentDirectory.DosPath.MaximumLength", "USHORT", 0x26, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CurrentDirectory.DosPath.Buffer", "PWSTR32", 0x28, 4, XX_PSW_PTR),
    XX_PSW_FIELD("CurrentDirectory.Handle", "HANDLE32", 0x2c, 4, XX_PSW_ID),
    XX_PSW_FIELD("DllPath", "UNICODE_STRING32[1]", 0x30, 8, 0),
    XX_PSW_FIELD("DllPath.Length", "USHORT", 0x30, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DllPath.MaximumLength", "USHORT", 0x32, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DllPath.Buffer", "PWSTR32", 0x34, 4, XX_PSW_PTR),
    XX_PSW_FIELD("ImagePathName", "UNICODE_STRING32[1]", 0x38, 8, 0),
    XX_PSW_FIELD("ImagePathName.Length", "USHORT", 0x38, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ImagePathName.MaximumLength", "USHORT", 0x3a, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ImagePathName.Buffer", "PWSTR32", 0x3c, 4, XX_PSW_PTR),
    XX_PSW_FIELD("CommandLine", "UNICODE_STRING32[1]", 0x40, 8, 0),
    XX_PSW_FIELD("CommandLine.Length", "USHORT", 0x40, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CommandLine.MaximumLength", "USHORT", 0x42, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CommandLine.Buffer", "PWSTR32", 0x44, 4, XX_PSW_PTR),
    XX_PSW_FIELD("Environment", "PVOID32", 0x48, 4, XX_PSW_PTR),
    XX_PSW_FIELD("StartingX", "ULONG", 0x4c, 4, 0),
    XX_PSW_FIELD("StartingY", "ULONG", 0x50, 4, 0),
    XX_PSW_FIELD("CountX", "ULONG", 0x54, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountY", "ULONG", 0x58, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountCharsX", "ULONG", 0x5c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountCharsY", "ULONG", 0x60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("FillAttribute", "ULONG", 0x64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("WindowFlags", "ULONG", 0x68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ShowWindowFlags", "ULONG", 0x6c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("WindowTitle", "UNICODE_STRING32[1]", 0x70, 8, 0),
    XX_PSW_FIELD("WindowTitle.Length", "USHORT", 0x70, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("WindowTitle.MaximumLength", "USHORT", 0x72, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("WindowTitle.Buffer", "PWSTR32", 0x74, 4, XX_PSW_PTR),
    XX_PSW_FIELD("DesktopInfo", "UNICODE_STRING32[1]", 0x78, 8, 0),
    XX_PSW_FIELD("DesktopInfo.Length", "USHORT", 0x78, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DesktopInfo.MaximumLength", "USHORT", 0x7a, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DesktopInfo.Buffer", "PWSTR32", 0x7c, 4, XX_PSW_PTR),
    XX_PSW_FIELD("ShellInfo", "UNICODE_STRING32[1]", 0x80, 8, 0),
    XX_PSW_FIELD("ShellInfo.Length", "USHORT", 0x80, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ShellInfo.MaximumLength", "USHORT", 0x82, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ShellInfo.Buffer", "PWSTR32", 0x84, 4, XX_PSW_PTR),
    XX_PSW_FIELD("RuntimeData", "UNICODE_STRING32[1]", 0x88, 8, 0),
    XX_PSW_FIELD("RuntimeData.Length", "USHORT", 0x88, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RuntimeData.MaximumLength", "USHORT", 0x8a, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RuntimeData.Buffer", "PWSTR32", 0x8c, 4, XX_PSW_PTR),
    XX_PSW_FIELD("CurrentDirectories", "RTL_DRIVE_LETTER_CURDIR32[32]", 0x90, 512, 0),
    XX_PSW_FIELD("EnvironmentSize", "ULONG_PTR32", 0x290, 4, XX_PSW_SIZE),
    XX_PSW_FIELD("EnvironmentVersion", "ULONG_PTR32", 0x294, 4, 0),
    XX_PSW_FIELD("PackageDependencyData", "PVOID32", 0x298, 4, XX_PSW_PTR),
    XX_PSW_FIELD("ProcessGroupId", "ULONG", 0x29c, 4, XX_PSW_ID),
    XX_PSW_FIELD("LoaderThreads", "ULONG", 0x2a0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("RedirectionDllName", "UNICODE_STRING32[1]", 0x2a4, 8, 0),
    XX_PSW_FIELD("RedirectionDllName.Length", "USHORT", 0x2a4, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RedirectionDllName.MaximumLength", "USHORT", 0x2a6, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RedirectionDllName.Buffer", "PWSTR32", 0x2a8, 4, XX_PSW_PTR),
    XX_PSW_FIELD("HeapPartitionName", "UNICODE_STRING32[1]", 0x2ac, 8, 0),
    XX_PSW_FIELD("HeapPartitionName.Length", "USHORT", 0x2ac, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("HeapPartitionName.MaximumLength", "USHORT", 0x2ae, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("HeapPartitionName.Buffer", "PWSTR32", 0x2b0, 4, XX_PSW_PTR),
    XX_PSW_FIELD("DefaultThreadpoolCpuSetMasks", "PULONGLONG32", 0x2b4, 4, XX_PSW_PTR),
    XX_PSW_FIELD("DefaultThreadpoolCpuSetMaskCount", "ULONG", 0x2b8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("DefaultThreadpoolThreadMaximum", "ULONG", 0x2bc, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
};

/* 64-bit build 19045: 99 descriptors, including nested string fields. */
static const xx_data_struct_field_desc xx_pswp_params64_19045[] = {
    XX_PSW_FIELD("MaximumLength", "ULONG", 0x0, 4, XX_PSW_SIZE),
    XX_PSW_FIELD("Length", "ULONG", 0x4, 4, XX_PSW_SIZE),
    XX_PSW_FIELD("Flags", "ULONG", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Normalized", "bitfield:0:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileUser", "bitfield:1:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileKernel", "bitfield:2:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileServer", "bitfield:3:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Reserve1MB", "bitfield:5:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Reserve16MB", "bitfield:6:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.CaseSensitive", "bitfield:7:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DisableHeapDecommit", "bitfield:8:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DllRedirectionLocal", "bitfield:12:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.AppManifestPresent", "bitfield:13:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ImageKeyMissing", "bitfield:14:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DevOverrideEnabled", "bitfield:15:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.OptInProcess", "bitfield:17:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.SessionOwner", "bitfield:18:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.HandleUserCallbackExceptions", "bitfield:19:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProtectedProcess", "bitfield:22:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.SecureProcess", "bitfield:31:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("DebugFlags", "ULONG", 0xc, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ConsoleHandle", "HANDLE64", 0x10, 8, XX_PSW_ID),
    XX_PSW_FIELD("ConsoleFlags", "ULONG", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AlignmentPadding0", "UCHAR[4]", 0x1c, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("StandardInput", "HANDLE64", 0x20, 8, XX_PSW_ID),
    XX_PSW_FIELD("StandardOutput", "HANDLE64", 0x28, 8, XX_PSW_ID),
    XX_PSW_FIELD("StandardError", "HANDLE64", 0x30, 8, XX_PSW_ID),
    XX_PSW_FIELD("CurrentDirectory", "CURDIR64[1]", 0x38, 24, 0),
    XX_PSW_FIELD("CurrentDirectory.DosPath", "UNICODE_STRING64[1]", 0x38, 16, 0),
    XX_PSW_FIELD("CurrentDirectory.DosPath.Length", "USHORT", 0x38, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CurrentDirectory.DosPath.MaximumLength", "USHORT", 0x3a, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CurrentDirectory.DosPath.AlignmentPadding", "UCHAR[4]", 0x3c, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("CurrentDirectory.DosPath.Buffer", "PWSTR64", 0x40, 8, XX_PSW_PTR),
    XX_PSW_FIELD("CurrentDirectory.Handle", "HANDLE64", 0x48, 8, XX_PSW_ID),
    XX_PSW_FIELD("DllPath", "UNICODE_STRING64[1]", 0x50, 16, 0),
    XX_PSW_FIELD("DllPath.Length", "USHORT", 0x50, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DllPath.MaximumLength", "USHORT", 0x52, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DllPath.AlignmentPadding", "UCHAR[4]", 0x54, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("DllPath.Buffer", "PWSTR64", 0x58, 8, XX_PSW_PTR),
    XX_PSW_FIELD("ImagePathName", "UNICODE_STRING64[1]", 0x60, 16, 0),
    XX_PSW_FIELD("ImagePathName.Length", "USHORT", 0x60, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ImagePathName.MaximumLength", "USHORT", 0x62, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ImagePathName.AlignmentPadding", "UCHAR[4]", 0x64, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("ImagePathName.Buffer", "PWSTR64", 0x68, 8, XX_PSW_PTR),
    XX_PSW_FIELD("CommandLine", "UNICODE_STRING64[1]", 0x70, 16, 0),
    XX_PSW_FIELD("CommandLine.Length", "USHORT", 0x70, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CommandLine.MaximumLength", "USHORT", 0x72, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CommandLine.AlignmentPadding", "UCHAR[4]", 0x74, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("CommandLine.Buffer", "PWSTR64", 0x78, 8, XX_PSW_PTR),
    XX_PSW_FIELD("Environment", "PVOID64", 0x80, 8, XX_PSW_PTR),
    XX_PSW_FIELD("StartingX", "ULONG", 0x88, 4, 0),
    XX_PSW_FIELD("StartingY", "ULONG", 0x8c, 4, 0),
    XX_PSW_FIELD("CountX", "ULONG", 0x90, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountY", "ULONG", 0x94, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountCharsX", "ULONG", 0x98, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountCharsY", "ULONG", 0x9c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("FillAttribute", "ULONG", 0xa0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("WindowFlags", "ULONG", 0xa4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ShowWindowFlags", "ULONG", 0xa8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AlignmentPadding1", "UCHAR[4]", 0xac, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("WindowTitle", "UNICODE_STRING64[1]", 0xb0, 16, 0),
    XX_PSW_FIELD("WindowTitle.Length", "USHORT", 0xb0, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("WindowTitle.MaximumLength", "USHORT", 0xb2, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("WindowTitle.AlignmentPadding", "UCHAR[4]", 0xb4, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("WindowTitle.Buffer", "PWSTR64", 0xb8, 8, XX_PSW_PTR),
    XX_PSW_FIELD("DesktopInfo", "UNICODE_STRING64[1]", 0xc0, 16, 0),
    XX_PSW_FIELD("DesktopInfo.Length", "USHORT", 0xc0, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DesktopInfo.MaximumLength", "USHORT", 0xc2, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DesktopInfo.AlignmentPadding", "UCHAR[4]", 0xc4, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("DesktopInfo.Buffer", "PWSTR64", 0xc8, 8, XX_PSW_PTR),
    XX_PSW_FIELD("ShellInfo", "UNICODE_STRING64[1]", 0xd0, 16, 0),
    XX_PSW_FIELD("ShellInfo.Length", "USHORT", 0xd0, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ShellInfo.MaximumLength", "USHORT", 0xd2, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ShellInfo.AlignmentPadding", "UCHAR[4]", 0xd4, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("ShellInfo.Buffer", "PWSTR64", 0xd8, 8, XX_PSW_PTR),
    XX_PSW_FIELD("RuntimeData", "UNICODE_STRING64[1]", 0xe0, 16, 0),
    XX_PSW_FIELD("RuntimeData.Length", "USHORT", 0xe0, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RuntimeData.MaximumLength", "USHORT", 0xe2, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RuntimeData.AlignmentPadding", "UCHAR[4]", 0xe4, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("RuntimeData.Buffer", "PWSTR64", 0xe8, 8, XX_PSW_PTR),
    XX_PSW_FIELD("CurrentDirectories", "RTL_DRIVE_LETTER_CURDIR64[32]", 0xf0, 768, 0),
    XX_PSW_FIELD("EnvironmentSize", "ULONG_PTR64", 0x3f0, 8, XX_PSW_SIZE),
    XX_PSW_FIELD("EnvironmentVersion", "ULONG_PTR64", 0x3f8, 8, 0),
    XX_PSW_FIELD("PackageDependencyData", "PVOID64", 0x400, 8, XX_PSW_PTR),
    XX_PSW_FIELD("ProcessGroupId", "ULONG", 0x408, 4, XX_PSW_ID),
    XX_PSW_FIELD("LoaderThreads", "ULONG", 0x40c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("RedirectionDllName", "UNICODE_STRING64[1]", 0x410, 16, 0),
    XX_PSW_FIELD("RedirectionDllName.Length", "USHORT", 0x410, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RedirectionDllName.MaximumLength", "USHORT", 0x412, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RedirectionDllName.AlignmentPadding", "UCHAR[4]", 0x414, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("RedirectionDllName.Buffer", "PWSTR64", 0x418, 8, XX_PSW_PTR),
    XX_PSW_FIELD("HeapPartitionName", "UNICODE_STRING64[1]", 0x420, 16, 0),
    XX_PSW_FIELD("HeapPartitionName.Length", "USHORT", 0x420, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("HeapPartitionName.MaximumLength", "USHORT", 0x422, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("HeapPartitionName.AlignmentPadding", "UCHAR[4]", 0x424, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("HeapPartitionName.Buffer", "PWSTR64", 0x428, 8, XX_PSW_PTR),
    XX_PSW_FIELD("DefaultThreadpoolCpuSetMasks", "PULONGLONG64", 0x430, 8, XX_PSW_PTR),
    XX_PSW_FIELD("DefaultThreadpoolCpuSetMaskCount", "ULONG", 0x438, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("DefaultThreadpoolThreadMaximum", "ULONG", 0x43c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
};

/* 32-bit build 26100: 88 descriptors, including nested string fields. */
static const xx_data_struct_field_desc xx_pswp_params32_26100[] = {
    XX_PSW_FIELD("MaximumLength", "ULONG", 0x0, 4, XX_PSW_SIZE),
    XX_PSW_FIELD("Length", "ULONG", 0x4, 4, XX_PSW_SIZE),
    XX_PSW_FIELD("Flags", "ULONG", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Normalized", "bitfield:0:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileUser", "bitfield:1:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileKernel", "bitfield:2:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileServer", "bitfield:3:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Reserve1MB", "bitfield:5:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Reserve16MB", "bitfield:6:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.CaseSensitive", "bitfield:7:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DisableHeapDecommit", "bitfield:8:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DllRedirectionLocal", "bitfield:12:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.AppManifestPresent", "bitfield:13:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ImageKeyMissing", "bitfield:14:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DevOverrideEnabled", "bitfield:15:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.OptInProcess", "bitfield:17:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.SessionOwner", "bitfield:18:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.HandleUserCallbackExceptions", "bitfield:19:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProtectedProcess", "bitfield:22:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.SecureProcess", "bitfield:31:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("DebugFlags", "ULONG", 0xc, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ConsoleHandle", "HANDLE32", 0x10, 4, XX_PSW_ID),
    XX_PSW_FIELD("ConsoleFlags", "ULONG", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("StandardInput", "HANDLE32", 0x18, 4, XX_PSW_ID),
    XX_PSW_FIELD("StandardOutput", "HANDLE32", 0x1c, 4, XX_PSW_ID),
    XX_PSW_FIELD("StandardError", "HANDLE32", 0x20, 4, XX_PSW_ID),
    XX_PSW_FIELD("CurrentDirectory", "CURDIR32[1]", 0x24, 12, 0),
    XX_PSW_FIELD("CurrentDirectory.DosPath", "UNICODE_STRING32[1]", 0x24, 8, 0),
    XX_PSW_FIELD("CurrentDirectory.DosPath.Length", "USHORT", 0x24, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CurrentDirectory.DosPath.MaximumLength", "USHORT", 0x26, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CurrentDirectory.DosPath.Buffer", "PWSTR32", 0x28, 4, XX_PSW_PTR),
    XX_PSW_FIELD("CurrentDirectory.Handle", "HANDLE32", 0x2c, 4, XX_PSW_ID),
    XX_PSW_FIELD("DllPath", "UNICODE_STRING32[1]", 0x30, 8, 0),
    XX_PSW_FIELD("DllPath.Length", "USHORT", 0x30, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DllPath.MaximumLength", "USHORT", 0x32, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DllPath.Buffer", "PWSTR32", 0x34, 4, XX_PSW_PTR),
    XX_PSW_FIELD("ImagePathName", "UNICODE_STRING32[1]", 0x38, 8, 0),
    XX_PSW_FIELD("ImagePathName.Length", "USHORT", 0x38, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ImagePathName.MaximumLength", "USHORT", 0x3a, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ImagePathName.Buffer", "PWSTR32", 0x3c, 4, XX_PSW_PTR),
    XX_PSW_FIELD("CommandLine", "UNICODE_STRING32[1]", 0x40, 8, 0),
    XX_PSW_FIELD("CommandLine.Length", "USHORT", 0x40, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CommandLine.MaximumLength", "USHORT", 0x42, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CommandLine.Buffer", "PWSTR32", 0x44, 4, XX_PSW_PTR),
    XX_PSW_FIELD("Environment", "PVOID32", 0x48, 4, XX_PSW_PTR),
    XX_PSW_FIELD("StartingX", "ULONG", 0x4c, 4, 0),
    XX_PSW_FIELD("StartingY", "ULONG", 0x50, 4, 0),
    XX_PSW_FIELD("CountX", "ULONG", 0x54, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountY", "ULONG", 0x58, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountCharsX", "ULONG", 0x5c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountCharsY", "ULONG", 0x60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("FillAttribute", "ULONG", 0x64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("WindowFlags", "ULONG", 0x68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ShowWindowFlags", "ULONG", 0x6c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("WindowTitle", "UNICODE_STRING32[1]", 0x70, 8, 0),
    XX_PSW_FIELD("WindowTitle.Length", "USHORT", 0x70, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("WindowTitle.MaximumLength", "USHORT", 0x72, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("WindowTitle.Buffer", "PWSTR32", 0x74, 4, XX_PSW_PTR),
    XX_PSW_FIELD("DesktopInfo", "UNICODE_STRING32[1]", 0x78, 8, 0),
    XX_PSW_FIELD("DesktopInfo.Length", "USHORT", 0x78, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DesktopInfo.MaximumLength", "USHORT", 0x7a, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DesktopInfo.Buffer", "PWSTR32", 0x7c, 4, XX_PSW_PTR),
    XX_PSW_FIELD("ShellInfo", "UNICODE_STRING32[1]", 0x80, 8, 0),
    XX_PSW_FIELD("ShellInfo.Length", "USHORT", 0x80, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ShellInfo.MaximumLength", "USHORT", 0x82, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ShellInfo.Buffer", "PWSTR32", 0x84, 4, XX_PSW_PTR),
    XX_PSW_FIELD("RuntimeData", "UNICODE_STRING32[1]", 0x88, 8, 0),
    XX_PSW_FIELD("RuntimeData.Length", "USHORT", 0x88, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RuntimeData.MaximumLength", "USHORT", 0x8a, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RuntimeData.Buffer", "PWSTR32", 0x8c, 4, XX_PSW_PTR),
    XX_PSW_FIELD("CurrentDirectories", "RTL_DRIVE_LETTER_CURDIR32[32]", 0x90, 512, 0),
    XX_PSW_FIELD("EnvironmentSize", "ULONG_PTR32", 0x290, 4, XX_PSW_SIZE),
    XX_PSW_FIELD("EnvironmentVersion", "ULONG_PTR32", 0x294, 4, 0),
    XX_PSW_FIELD("PackageDependencyData", "PVOID32", 0x298, 4, XX_PSW_PTR),
    XX_PSW_FIELD("ProcessGroupId", "ULONG", 0x29c, 4, XX_PSW_ID),
    XX_PSW_FIELD("LoaderThreads", "ULONG", 0x2a0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("RedirectionDllName", "UNICODE_STRING32[1]", 0x2a4, 8, 0),
    XX_PSW_FIELD("RedirectionDllName.Length", "USHORT", 0x2a4, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RedirectionDllName.MaximumLength", "USHORT", 0x2a6, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RedirectionDllName.Buffer", "PWSTR32", 0x2a8, 4, XX_PSW_PTR),
    XX_PSW_FIELD("HeapPartitionName", "UNICODE_STRING32[1]", 0x2ac, 8, 0),
    XX_PSW_FIELD("HeapPartitionName.Length", "USHORT", 0x2ac, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("HeapPartitionName.MaximumLength", "USHORT", 0x2ae, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("HeapPartitionName.Buffer", "PWSTR32", 0x2b0, 4, XX_PSW_PTR),
    XX_PSW_FIELD("DefaultThreadpoolCpuSetMasks", "PULONGLONG32", 0x2b4, 4, XX_PSW_PTR),
    XX_PSW_FIELD("DefaultThreadpoolCpuSetMaskCount", "ULONG", 0x2b8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("DefaultThreadpoolThreadMaximum", "ULONG", 0x2bc, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("HeapMemoryTypeMask", "ULONG", 0x2c0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

/* 64-bit build 26100: 101 descriptors, including nested string fields. */
static const xx_data_struct_field_desc xx_pswp_params64_26100[] = {
    XX_PSW_FIELD("MaximumLength", "ULONG", 0x0, 4, XX_PSW_SIZE),
    XX_PSW_FIELD("Length", "ULONG", 0x4, 4, XX_PSW_SIZE),
    XX_PSW_FIELD("Flags", "ULONG", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Normalized", "bitfield:0:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileUser", "bitfield:1:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileKernel", "bitfield:2:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProfileServer", "bitfield:3:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Reserve1MB", "bitfield:5:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Reserve16MB", "bitfield:6:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.CaseSensitive", "bitfield:7:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DisableHeapDecommit", "bitfield:8:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DllRedirectionLocal", "bitfield:12:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.AppManifestPresent", "bitfield:13:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ImageKeyMissing", "bitfield:14:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.DevOverrideEnabled", "bitfield:15:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.OptInProcess", "bitfield:17:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.SessionOwner", "bitfield:18:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.HandleUserCallbackExceptions", "bitfield:19:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ProtectedProcess", "bitfield:22:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.SecureProcess", "bitfield:31:1", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("DebugFlags", "ULONG", 0xc, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ConsoleHandle", "HANDLE64", 0x10, 8, XX_PSW_ID),
    XX_PSW_FIELD("ConsoleFlags", "ULONG", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AlignmentPadding0", "UCHAR[4]", 0x1c, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("StandardInput", "HANDLE64", 0x20, 8, XX_PSW_ID),
    XX_PSW_FIELD("StandardOutput", "HANDLE64", 0x28, 8, XX_PSW_ID),
    XX_PSW_FIELD("StandardError", "HANDLE64", 0x30, 8, XX_PSW_ID),
    XX_PSW_FIELD("CurrentDirectory", "CURDIR64[1]", 0x38, 24, 0),
    XX_PSW_FIELD("CurrentDirectory.DosPath", "UNICODE_STRING64[1]", 0x38, 16, 0),
    XX_PSW_FIELD("CurrentDirectory.DosPath.Length", "USHORT", 0x38, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CurrentDirectory.DosPath.MaximumLength", "USHORT", 0x3a, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CurrentDirectory.DosPath.AlignmentPadding", "UCHAR[4]", 0x3c, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("CurrentDirectory.DosPath.Buffer", "PWSTR64", 0x40, 8, XX_PSW_PTR),
    XX_PSW_FIELD("CurrentDirectory.Handle", "HANDLE64", 0x48, 8, XX_PSW_ID),
    XX_PSW_FIELD("DllPath", "UNICODE_STRING64[1]", 0x50, 16, 0),
    XX_PSW_FIELD("DllPath.Length", "USHORT", 0x50, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DllPath.MaximumLength", "USHORT", 0x52, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DllPath.AlignmentPadding", "UCHAR[4]", 0x54, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("DllPath.Buffer", "PWSTR64", 0x58, 8, XX_PSW_PTR),
    XX_PSW_FIELD("ImagePathName", "UNICODE_STRING64[1]", 0x60, 16, 0),
    XX_PSW_FIELD("ImagePathName.Length", "USHORT", 0x60, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ImagePathName.MaximumLength", "USHORT", 0x62, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ImagePathName.AlignmentPadding", "UCHAR[4]", 0x64, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("ImagePathName.Buffer", "PWSTR64", 0x68, 8, XX_PSW_PTR),
    XX_PSW_FIELD("CommandLine", "UNICODE_STRING64[1]", 0x70, 16, 0),
    XX_PSW_FIELD("CommandLine.Length", "USHORT", 0x70, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CommandLine.MaximumLength", "USHORT", 0x72, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("CommandLine.AlignmentPadding", "UCHAR[4]", 0x74, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("CommandLine.Buffer", "PWSTR64", 0x78, 8, XX_PSW_PTR),
    XX_PSW_FIELD("Environment", "PVOID64", 0x80, 8, XX_PSW_PTR),
    XX_PSW_FIELD("StartingX", "ULONG", 0x88, 4, 0),
    XX_PSW_FIELD("StartingY", "ULONG", 0x8c, 4, 0),
    XX_PSW_FIELD("CountX", "ULONG", 0x90, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountY", "ULONG", 0x94, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountCharsX", "ULONG", 0x98, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("CountCharsY", "ULONG", 0x9c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("FillAttribute", "ULONG", 0xa0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("WindowFlags", "ULONG", 0xa4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("ShowWindowFlags", "ULONG", 0xa8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AlignmentPadding1", "UCHAR[4]", 0xac, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("WindowTitle", "UNICODE_STRING64[1]", 0xb0, 16, 0),
    XX_PSW_FIELD("WindowTitle.Length", "USHORT", 0xb0, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("WindowTitle.MaximumLength", "USHORT", 0xb2, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("WindowTitle.AlignmentPadding", "UCHAR[4]", 0xb4, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("WindowTitle.Buffer", "PWSTR64", 0xb8, 8, XX_PSW_PTR),
    XX_PSW_FIELD("DesktopInfo", "UNICODE_STRING64[1]", 0xc0, 16, 0),
    XX_PSW_FIELD("DesktopInfo.Length", "USHORT", 0xc0, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DesktopInfo.MaximumLength", "USHORT", 0xc2, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DesktopInfo.AlignmentPadding", "UCHAR[4]", 0xc4, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("DesktopInfo.Buffer", "PWSTR64", 0xc8, 8, XX_PSW_PTR),
    XX_PSW_FIELD("ShellInfo", "UNICODE_STRING64[1]", 0xd0, 16, 0),
    XX_PSW_FIELD("ShellInfo.Length", "USHORT", 0xd0, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ShellInfo.MaximumLength", "USHORT", 0xd2, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("ShellInfo.AlignmentPadding", "UCHAR[4]", 0xd4, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("ShellInfo.Buffer", "PWSTR64", 0xd8, 8, XX_PSW_PTR),
    XX_PSW_FIELD("RuntimeData", "UNICODE_STRING64[1]", 0xe0, 16, 0),
    XX_PSW_FIELD("RuntimeData.Length", "USHORT", 0xe0, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RuntimeData.MaximumLength", "USHORT", 0xe2, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RuntimeData.AlignmentPadding", "UCHAR[4]", 0xe4, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("RuntimeData.Buffer", "PWSTR64", 0xe8, 8, XX_PSW_PTR),
    XX_PSW_FIELD("CurrentDirectories", "RTL_DRIVE_LETTER_CURDIR64[32]", 0xf0, 768, 0),
    XX_PSW_FIELD("EnvironmentSize", "ULONG_PTR64", 0x3f0, 8, XX_PSW_SIZE),
    XX_PSW_FIELD("EnvironmentVersion", "ULONG_PTR64", 0x3f8, 8, 0),
    XX_PSW_FIELD("PackageDependencyData", "PVOID64", 0x400, 8, XX_PSW_PTR),
    XX_PSW_FIELD("ProcessGroupId", "ULONG", 0x408, 4, XX_PSW_ID),
    XX_PSW_FIELD("LoaderThreads", "ULONG", 0x40c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("RedirectionDllName", "UNICODE_STRING64[1]", 0x410, 16, 0),
    XX_PSW_FIELD("RedirectionDllName.Length", "USHORT", 0x410, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RedirectionDllName.MaximumLength", "USHORT", 0x412, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("RedirectionDllName.AlignmentPadding", "UCHAR[4]", 0x414, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("RedirectionDllName.Buffer", "PWSTR64", 0x418, 8, XX_PSW_PTR),
    XX_PSW_FIELD("HeapPartitionName", "UNICODE_STRING64[1]", 0x420, 16, 0),
    XX_PSW_FIELD("HeapPartitionName.Length", "USHORT", 0x420, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("HeapPartitionName.MaximumLength", "USHORT", 0x422, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("HeapPartitionName.AlignmentPadding", "UCHAR[4]", 0x424, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("HeapPartitionName.Buffer", "PWSTR64", 0x428, 8, XX_PSW_PTR),
    XX_PSW_FIELD("DefaultThreadpoolCpuSetMasks", "PULONGLONG64", 0x430, 8, XX_PSW_PTR),
    XX_PSW_FIELD("DefaultThreadpoolCpuSetMaskCount", "ULONG", 0x438, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("DefaultThreadpoolThreadMaximum", "ULONG", 0x43c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_PSW_FIELD("HeapMemoryTypeMask", "ULONG", 0x440, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AlignmentPadding2", "UCHAR[4]", 0x444, 4, XX_PSW_RESERVED),
};

static const xx_data_struct_field_desc xx_pswp_curdir32[] = {
    XX_PSW_FIELD("DosPath", "UNICODE_STRING32[1]", 0x0, 8, 0),
    XX_PSW_FIELD("DosPath.Length", "USHORT", 0x0, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DosPath.MaximumLength", "USHORT", 0x2, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DosPath.Buffer", "PWSTR32", 0x4, 4, XX_PSW_PTR),
    XX_PSW_FIELD("Handle", "HANDLE32", 0x8, 4, XX_PSW_ID),
};

static const xx_data_struct_field_desc xx_pswp_drive32[] = {
    XX_PSW_FIELD("Flags", "USHORT", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Valid", "bitfield:0:1", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Length", "USHORT", 0x2, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("TimeStamp", "ULONG", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    XX_PSW_FIELD("DosPath", "STRING32[1]", 0x8, 8, 0),
    XX_PSW_FIELD("DosPath.Length", "USHORT", 0x8, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DosPath.MaximumLength", "USHORT", 0xa, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DosPath.Buffer", "PCHAR32", 0xc, 4, XX_PSW_PTR),
};

static const xx_data_struct_field_desc xx_pswp_curdir64[] = {
    XX_PSW_FIELD("DosPath", "UNICODE_STRING64[1]", 0x0, 16, 0),           XX_PSW_FIELD("DosPath.Length", "USHORT", 0x0, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DosPath.MaximumLength", "USHORT", 0x2, 2, XX_PSW_SIZE), XX_PSW_FIELD("DosPath.AlignmentPadding", "UCHAR[4]", 0x4, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("DosPath.Buffer", "PWSTR64", 0x8, 8, XX_PSW_PTR),        XX_PSW_FIELD("Handle", "HANDLE64", 0x10, 8, XX_PSW_ID),
};

static const xx_data_struct_field_desc xx_pswp_drive64[] = {
    XX_PSW_FIELD("Flags", "USHORT", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.Valid", "bitfield:0:1", 0x0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Length", "USHORT", 0x2, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("TimeStamp", "ULONG", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    XX_PSW_FIELD("DosPath", "STRING64[1]", 0x8, 16, 0),
    XX_PSW_FIELD("DosPath.Length", "USHORT", 0x8, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DosPath.MaximumLength", "USHORT", 0xa, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("DosPath.AlignmentPadding", "UCHAR[4]", 0xc, 4, XX_PSW_RESERVED),
    XX_PSW_FIELD("DosPath.Buffer", "PCHAR64", 0x10, 8, XX_PSW_PTR),
};

typedef struct xx_pswp_string_member {
    const char *name;
    size_t offset32, offset64;
} xx_pswp_string_member;
static const xx_pswp_string_member xx_pswp_strings[] = {
    {"CurrentDirectory.DosPath", 0x24, 0x38},
    {"DllPath", 0x30, 0x50},
    {"ImagePathName", 0x38, 0x60},
    {"CommandLine", 0x40, 0x70},
    {"WindowTitle", 0x70, 0xb0},
    {"DesktopInfo", 0x78, 0xc0},
    {"ShellInfo", 0x80, 0xd0},
    {"RuntimeData", 0x88, 0xe0},
    {"RedirectionDllName", 0x2a4, 0x410},
    {"HeapPartitionName", 0x2ac, 0x420},
};

/* No drive letter is inferred from a slot: the ABI supplies 32 entries. */
static const char *const xx_pswp_drive_labels[32] = {
    "CurrentDirectories[0]",  "CurrentDirectories[1]",  "CurrentDirectories[2]",  "CurrentDirectories[3]",  "CurrentDirectories[4]",  "CurrentDirectories[5]",
    "CurrentDirectories[6]",  "CurrentDirectories[7]",  "CurrentDirectories[8]",  "CurrentDirectories[9]",  "CurrentDirectories[10]", "CurrentDirectories[11]",
    "CurrentDirectories[12]", "CurrentDirectories[13]", "CurrentDirectories[14]", "CurrentDirectories[15]", "CurrentDirectories[16]", "CurrentDirectories[17]",
    "CurrentDirectories[18]", "CurrentDirectories[19]", "CurrentDirectories[20]", "CurrentDirectories[21]", "CurrentDirectories[22]", "CurrentDirectories[23]",
    "CurrentDirectories[24]", "CurrentDirectories[25]", "CurrentDirectories[26]", "CurrentDirectories[27]", "CurrentDirectories[28]", "CurrentDirectories[29]",
    "CurrentDirectories[30]", "CurrentDirectories[31]",
};

/* Check both integer overflow and the target pointer width, including the last
 * addressed byte. The target address is never dereferenced by this process. */
static bool xx_pswp_span(uint64_t address, uint64_t offset, size_t size, unsigned width, uint64_t *result)
{
    uint64_t last;
    if (width != 4 && width != 8) return false;
    last = width == 4 ? UINT32_MAX : UINT64_MAX;
    if (address > last || offset > last - address) return false;
    address += offset;
    if (size && (uint64_t)(size - 1) > last - address) return false;
    if (result) *result = address;
    return true;
}

/* Call only when another real record remains to be represented. */
static bool xx_pswp_room(xx_ps_context *context)
{
    if (context->status != XX_PROCESS_STRUCTS_OK) return false;
    if (context->snapshot->structure_count < context->limits.max_structures) return true;
    context->snapshot->truncated = true;
    xx_psw_warn_once(context, "Process-parameter records were omitted by the structure limit.");
    return false;
}

/* Keep the complete verified schema but do not read outside the validated
 * Length/MaximumLength block. Short tails have unavailable values, not zeroes.
 * The largest supported object is the 0x448-byte AMD64 parameter structure. */
static xx_process_structure *xx_pswp_memory(xx_ps_context *context, xx_io_device *device, const char *name, const char *layout, uint64_t address, size_t extent,
                                            size_t readable, const xx_data_struct_field_desc *fields, size_t count)
{
    unsigned char bytes[XX_PSWP_PARAMS64_26100_SIZE] = {0};
    xx_process_structure *structure;
    bool complete, unreadable = false;
    if (!extent || extent > sizeof(bytes)) {
        context->status = XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
        return NULL;
    }
    if (readable > extent) readable = extent;
    structure = xx_ps_add(context, name, layout, address, true, extent, 0);
    if (!structure) return NULL;
    complete = readable && xx_psw_read_at(device, address, 0, bytes, readable);
    for (size_t i = 0; i < count; ++i) {
        const xx_data_struct_field_desc *field = fields + i;
        size_t offset, size;
        bool in_bounds, available;
        if (field->rel_offset < 0 || field->size < 0 || (uint64_t)field->rel_offset > extent || (uint64_t)field->size > extent - (size_t)field->rel_offset) {
            context->status = XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
            return NULL;
        }
        offset = (size_t)field->rel_offset;
        size = (size_t)field->size;
        in_bounds = offset <= readable && size <= readable - offset;
        available = in_bounds && (complete || xx_psw_read_at(device, address, offset, bytes + offset, size));
        if (in_bounds && !available) unreadable = true;
        if (!xx_ps_decode(context, structure, field, 1, available ? bytes : NULL, available ? offset + size : 0, false)) return NULL;
        if (!in_bounds && structure->field_count)
            (void)snprintf(structure->fields[structure->field_count - 1].comment, sizeof(structure->fields[structure->field_count - 1].comment),
                           "Outside the validated RTL_USER_PROCESS_PARAMETERS.Length block.");
    }
    xx_psw_bitfields(structure);
    if (unreadable) xx_psw_warn_once(context, "Some process-parameter fields were unreadable.");
    return structure;
}

static void xx_pswp_ansi_preview(xx_ps_context *context, xx_io_device *device, xx_process_structure *structure, uint64_t address, unsigned width, bool normalized)
{
    unsigned char header[24], bytes[40];
    uint64_t buffer;
    size_t length, maximum, preview, used = 0;
    char text[sizeof(bytes) * 4 + 1];
    xx_process_struct_field *field;
    if (!structure || !structure->field_count) return;
    field = structure->fields + structure->field_count - 1; /* DosPath.Buffer */
    /* Read validity and STRING metadata together. An unused slot may contain
     * stale length/pointer values, although its inline fields remain visible. */
    if (!xx_psw_read_at(device, address, 0, header, width == 8 ? 24 : 16)) return;
    if (!(xx_psw_le(header, 2) & 1)) {
        (void)snprintf(field->comment, sizeof(field->comment), "Drive slot is not marked valid; its stale STRING buffer was not followed.");
        return;
    }
    if (!normalized) {
        (void)snprintf(field->comment, sizeof(field->comment), "Unnormalized relative buffer value; it was not followed as an absolute address.");
        return;
    }
    length = (size_t)xx_psw_le(header + 8, 2);
    maximum = (size_t)xx_psw_le(header + 10, 2);
    buffer = xx_psw_le(header + (width == 8 ? 16 : 12), width);
    preview = length < sizeof(bytes) ? length : sizeof(bytes);
    if (length > maximum || (length && !buffer) || !xx_pswp_span(buffer, 0, preview, width, NULL)) {
        (void)snprintf(field->comment, sizeof(field->comment), "Inconsistent ANSI STRING length/buffer; text was not read.");
        xx_psw_warn_once(context, "An inconsistent drive STRING was not followed.");
        return;
    }
    if (preview && !xx_ps_read(device, buffer, bytes, preview)) {
        (void)snprintf(field->comment, sizeof(field->comment), "ANSI STRING buffer is unreadable.");
        xx_psw_warn_once(context, "A drive STRING buffer was unreadable.");
        return;
    }
    /* The target ANSI code page is not assumed to be UTF-8. Keep its bytes
     * visible with ASCII and hexadecimal escapes, without a locale guess. */
    for (size_t i = 0; i < preview; ++i) {
        unsigned char c = bytes[i];
        if (c >= 0x20 && c <= 0x7e && c != '\\') text[used++] = (char)c;
        else {
            static const char hex[] = "0123456789ABCDEF";
            text[used++] = '\\';
            text[used++] = 'x';
            text[used++] = hex[c >> 4];
            text[used++] = hex[c & 15];
        }
    }
    text[used] = 0;
    (void)snprintf(field->comment, sizeof(field->comment), "ANSI byte preview: %s", text);
}

static void xx_pswp_relative_buffers(xx_process_structure *structure)
{
    for (size_t i = 0; structure && i < structure->field_count; ++i) {
        xx_process_struct_field *field = structure->fields + i;
        const wchar_t *tail = NULL;
        for (const wchar_t *p = field->desc.name; p && *p; ++p)
            if (*p == L'.') tail = p;
        if (field->available && tail && wcscmp(tail, L".Buffer") == 0)
            (void)snprintf(field->comment, sizeof(field->comment), "Unnormalized relative buffer value; it was not followed as an absolute address.");
    }
}

static void xx_pswp_unicode_child(xx_ps_context *context, xx_io_device *device, const char *label, uint64_t address, size_t offset, size_t declared, unsigned width,
                                  bool normalized)
{
    size_t size = width == 8 ? 16 : 8;
    uint64_t child, buffer;
    unsigned char header[16];
    if (!normalized || offset > declared || size > declared - offset) return;
    if (!xx_pswp_span(address, offset, size, width, &child)) {
        xx_psw_warn_once(context, "An overflowing process-parameter child address was not followed.");
        return;
    }
    if (!xx_pswp_room(context)) return;
    if (xx_psw_read_at(device, child, 0, header, size)) {
        size_t length = (size_t)xx_psw_le(header, 2);
        size_t preview = length < 160 ? length : 160;
        buffer = xx_psw_le(header + (width == 8 ? 8 : 4), width);
        if (!xx_pswp_span(buffer, 0, preview, width, NULL)) {
            const xx_data_struct_field_desc *fields = width == 8 ? xx_psw_unicode64 : xx_psw_unicode32;
            xx_process_structure *structure;
            char name[96];
            (void)snprintf(name, sizeof(name), "UNICODE_STRING%u (%s)", width * 8, label);
            structure = xx_psw_memory(context, device, name, "Microsoft documented UNICODE_STRING", child, size, 0, fields, 3);
            if (structure && structure->field_count >= 3)
                (void)snprintf(structure->fields[2].comment, sizeof(structure->fields[2].comment),
                               "String preview exceeds the target address width; buffer was not followed.");
            xx_psw_warn_once(context, "An overflowing process-parameter string buffer was not followed.");
            return;
        }
    }
    xx_psw_unicode(context, device, label, child, width);
}

/* Full profiles are selected only through the existing exact version/machine
 * gate. The 26100 WOW64 case is intentionally labeled as a phnt32 prefix. */
static void xx_pswp_parameters(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, const xx_psw_target *target)
{
    const xx_psw_nt_layout *profile;
    const xx_data_struct_field_desc *fields, *curdir, *drive;
    xx_process_structure *structure;
    const char *layout;
    size_t count, extent, declared = 0, maximum = 0, readable;
    size_t curdir_offset, curdir_size, drive_offset, drive_size;
    unsigned char header[16];
    bool full = false, valid_header = false, normalized = false;
    uint64_t child;
    if (!context || context->status != XX_PROCESS_STRUCTS_OK || !address) return;
    if (width != 4 && width != 8) {
        xx_psw_warn_once(context, "An unsupported process-parameter pointer width was skipped.");
        return;
    }
    if (!xx_pswp_room(context)) return;
    profile = target ? xx_psw_profile(target, width, false) : NULL;
    fields = width == 8 ? xx_psw_params64 : xx_psw_params32;
    count = width == 8 ? XX_PSW_COUNT(xx_psw_params64) : XX_PSW_COUNT(xx_psw_params32);
    extent = width == 8 ? 128 : 72;
    layout = "Published SDK RTL_USER_PROCESS_PARAMETERS fallback";
    if (profile && profile->build == 19045) {
        fields = width == 8 ? xx_pswp_params64_19045 : xx_pswp_params32_19045;
        count = width == 8 ? XX_PSW_COUNT(xx_pswp_params64_19045) : XX_PSW_COUNT(xx_pswp_params32_19045);
        extent = width == 8 ? XX_PSWP_PARAMS64_19045_SIZE : XX_PSWP_PARAMS32_19045_SIZE;
        layout = width == 8 ? "NT 10.0.19045 AMD64 process parameters symbol profile" : "NT 10.0.19045 I386/WOW64 process parameters symbol profile";
        full = true;
    } else if (profile && profile->build == 26100 && (width == 8 || (target && target->native_machine == 0x8664))) {
        fields = width == 8 ? xx_pswp_params64_26100 : xx_pswp_params32_26100;
        count = width == 8 ? XX_PSW_COUNT(xx_pswp_params64_26100) : XX_PSW_COUNT(xx_pswp_params32_26100);
        extent = width == 8 ? XX_PSWP_PARAMS64_26100_SIZE : XX_PSWP_PARAMS32_26100_PREFIX_SIZE;
        layout = width == 8 ? "NT 10.0.26100 AMD64 process parameters symbol profile" : "NT 10.0.26100 WOW64 phnt32 parameters prefix (0x2c4)";
        full = true;
    }
    if (!xx_pswp_span(address, 0, extent, width, NULL)) {
        xx_psw_warn_once(context, "An overflowing process-parameter block address was skipped.");
        return;
    }
    if (xx_psw_read_at(device, address, 0, header, sizeof(header))) {
        maximum = (size_t)xx_psw_le(header, 4);
        declared = (size_t)xx_psw_le(header + 4, 4);
        valid_header =
            declared >= sizeof(header) && declared <= maximum && xx_pswp_span(address, 0, declared, width, NULL) && xx_pswp_span(address, 0, maximum, width, NULL);
        normalized = valid_header && (xx_psw_le(header + 8, 4) & 1) != 0;
    }
    readable = valid_header ? declared : sizeof(header);
    if (!valid_header) xx_psw_warn_once(context, "Process-parameter length header is unavailable or inconsistent; pointer children were not followed.");
    else if (!normalized) xx_psw_warn_once(context, "Process parameters are not normalized; relative string buffers were not followed.");
    if (valid_header && declared < extent) xx_psw_warn_once(context, "Process parameters are shorter than the selected schema; absent tail fields are unavailable.");
    structure =
        xx_pswp_memory(context, device, width == 8 ? "RTL_USER_PROCESS_PARAMETERS64" : "RTL_USER_PROCESS_PARAMETERS32", layout, address, extent, readable, fields, count);
    if (!structure) return;
    if (valid_header && !normalized) xx_pswp_relative_buffers(structure);
    if (!full) {
        xx_pswp_unicode_child(context, device, "ImagePathName", address, width == 8 ? 96 : 56, declared, width, normalized);
        xx_pswp_unicode_child(context, device, "CommandLine", address, width == 8 ? 112 : 64, declared, width, normalized);
        return;
    }
    if (!valid_header) return;
    curdir_offset = width == 8 ? 0x38 : 0x24;
    curdir_size = width == 8 ? 24 : 12;
    curdir = width == 8 ? xx_pswp_curdir64 : xx_pswp_curdir32;
    if (curdir_offset <= declared && curdir_size <= declared - curdir_offset && xx_pswp_span(address, curdir_offset, curdir_size, width, &child)) {
        if (!xx_pswp_room(context)) return;
        structure = xx_psw_memory(context, device, width == 8 ? "CURDIR64" : "CURDIR32", "Inline process CurrentDirectory", child, curdir_size, 0, curdir,
                                  width == 8 ? XX_PSW_COUNT(xx_pswp_curdir64) : XX_PSW_COUNT(xx_pswp_curdir32));
        if (!structure) return;
        if (!normalized) xx_pswp_relative_buffers(structure);
    }
    for (size_t i = 0; i < XX_PSW_COUNT(xx_pswp_strings); ++i) {
        const xx_pswp_string_member *member = xx_pswp_strings + i;
        xx_pswp_unicode_child(context, device, member->name, address, width == 8 ? member->offset64 : member->offset32, declared, width, normalized);
        if (context->status != XX_PROCESS_STRUCTS_OK) return;
    }
    drive_offset = width == 8 ? 0xf0 : 0x90;
    drive_size = width == 8 ? 24 : 16;
    drive = width == 8 ? xx_pswp_drive64 : xx_pswp_drive32;
    for (size_t i = 0; i < XX_PSW_COUNT(xx_pswp_drive_labels); ++i) {
        size_t offset = drive_offset + i * drive_size;
        char name[96];
        if (offset > declared || drive_size > declared - offset) break;
        if (!xx_pswp_span(address, offset, drive_size, width, &child)) {
            xx_psw_warn_once(context, "An overflowing drive-directory record address was skipped.");
            break;
        }
        if (!xx_pswp_room(context)) return;
        (void)snprintf(name, sizeof(name), "RTL_DRIVE_LETTER_CURDIR%u (%s)", width * 8, xx_pswp_drive_labels[i]);
        structure = xx_psw_memory(context, device, name, "Inline process drive directory; DosPath is an ANSI STRING", child, drive_size, 0, drive,
                                  width == 8 ? XX_PSW_COUNT(xx_pswp_drive64) : XX_PSW_COUNT(xx_pswp_drive32));
        if (!structure) return;
        xx_pswp_ansi_preview(context, device, structure, child, width, normalized);
    }
}

#endif /* XX_PROCESS_PARAMS_LAYOUTS_H */
