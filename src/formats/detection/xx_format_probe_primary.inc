/* SPDX-License-Identifier: MIT
 * Specific bounded signatures before broad container detection.
 * Ordered X-macro source: keep predicate and header boundaries in order.
 */

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(advanced_installer_bootstrapper, XX_FILE_TYPE_ADVANCED_INSTALLER_BOOTSTRAPPER,
    (is_mz))
XX_FORMAT_READER_PROBE(ardi_installer, XX_FILE_TYPE_ARDI_INSTALLER,
    (is_mz))
XX_FORMAT_READER_PROBE(arni_installer_container, XX_FILE_TYPE_ARNI_INSTALLER_CONTAINER,
    (is_mz))
XX_FORMAT_READER_PROBE(ej_technologies_install, XX_FILE_TYPE_EJ_TECHNOLOGIES_INSTALL,
    (is_mz))
XX_FORMAT_READER_PROBE(finstall, XX_FILE_TYPE_FINSTALL,
    (header_size >= 11U && header[0] == 0x01U && xx_rt_memcmp(header + 1U, "F Install ", 10U) == 0))
XX_FORMAT_READER_PROBE(ghost_installer, XX_FILE_TYPE_GHOST_INSTALLER,
    (is_mz || (header_size >= 8U && xx_rt_memcmp(header, "\xC0\xDE\xCE\xCB\x8D\x8D\x8D\x8D", 8U) == 0)))
XX_FORMAT_READER_PROBE(ibm_zpak_installer, XX_FILE_TYPE_IBM_ZPAK_INSTALLER,
    (is_mz))
XX_FORMAT_READER_PROBE(ifah_installer, XX_FILE_TYPE_IFAH_INSTALLER,
    (is_mz || (header_size >= 4U && xx_rt_memcmp(header, "IFAH", 4U) == 0)))
XX_FORMAT_READER_PROBE(inno_setup, XX_FILE_TYPE_INNO_SETUP,
    (is_mz))
XX_FORMAT_READER_PROBE(installer_vise_windows, XX_FILE_TYPE_INSTALLER_VISE_WINDOWS,
    (is_mz))
XX_FORMAT_READER_PROBE(installshield_12_setup, XX_FILE_TYPE_INSTALLSHIELD_12_SETUP,
    (is_mz))
XX_FORMAT_READER_PROBE(installshield_3, XX_FILE_TYPE_INSTALLSHIELD_3,
    (is_mz))
XX_FORMAT_READER_PROBE(installshield_7_setup, XX_FILE_TYPE_INSTALLSHIELD_7_SETUP,
    (is_mz))
XX_FORMAT_READER_PROBE(installshield_7_setup2, XX_FILE_TYPE_INSTALLSHIELD_7_SETUP2,
    (header_size >= 19U && header[0] >= 0x20U && header[0] != 0x7FU))
XX_FORMAT_READER_PROBE(installshield_developer, XX_FILE_TYPE_INSTALLSHIELD_DEVELOPER,
    (is_mz))
XX_FORMAT_READER_PROBE(installshield_issetupstream, XX_FILE_TYPE_INSTALLSHIELD_ISSETUPSTREAM,
    (is_mz || (header_size >= 14U && xx_rt_memcmp(header, "ISSetupStream", 13U) == 0 && header[13] == 0U)))
XX_FORMAT_READER_PROBE(installshield_multiplatform, XX_FILE_TYPE_INSTALLSHIELD_MULTIPLATFORM,
    (xx_installshield_multiplatform_has_footer(dev)))
XX_FORMAT_READER_PROBE(installshield_skin, XX_FILE_TYPE_INSTALLSHIELD_SKIN,
    (header_size >= 4U && ((((header[0] >> 4U) | ((header[0] & 0x0FU) << 4U)) ^ 0xA2U) >= 0x20U) && ((((header[0] >> 4U) | ((header[0] & 0x0FU) << 4U)) ^ 0xA2U) <= 0x7FU)))
XX_FORMAT_READER_PROBE(microfox_put, XX_FILE_TYPE_MICROFOX_PUT,
    (header_size >= 7U && header[2] == 0x2DU && header[3] == 0x6CU && header[4] == 0x5AU && (header[5] == 0x30U || header[5] == 0x31U || header[5] == 0x35U) && header[6] == 0x2DU))
XX_FORMAT_READER_PROBE(o_setup, XX_FILE_TYPE_O_SETUP,
    (is_mz))
XX_FORMAT_READER_PROBE(pc_install_setup, XX_FILE_TYPE_PC_INSTALL_SETUP,
    (is_mz))
XX_FORMAT_READER_PROBE(pyinstaller_one_executable, XX_FILE_TYPE_PYINSTALLER_ONE_EXECUTABLE,
    (is_mz || xx_pyinstaller_one_executable_has_tail_cookie(dev)))
XX_FORMAT_READER_PROBE(qsetup_installation_suite, XX_FILE_TYPE_QSETUP_INSTALLATION_SUITE,
    (is_mz))
XX_FORMAT_READER_PROBE(rtpatch_setup_data, XX_FILE_TYPE_RTPATCH_SETUP_DATA,
    (header_size >= 26U && header[14] >= 2U && header[14] <= 13U && header[15] == 0U && header_size >= 24U + header[14] && header[15U + header[14]] == 0U && header[16U + header[14]] == 0xB5U && header[17U + header[14]] == 0x9CU && header[18U + header[14]] <= 1U && header[19U + header[14]] == 0xFFU))
XX_FORMAT_READER_PROBE(setup_factory, XX_FILE_TYPE_SETUP_FACTORY,
    (is_mz))
XX_FORMAT_READER_PROBE(sfx_ebook_compiler_executables, XX_FILE_TYPE_SFX_EBOOK_COMPILER_EXECUTABLES,
    (is_mz))
XX_FORMAT_READER_PROBE(spoon_installer, XX_FILE_TYPE_SPOON_INSTALLER,
    (is_mz))
XX_FORMAT_READER_PROBE(tarma_installer, XX_FILE_TYPE_TARMA_INSTALLER,
    (is_mz))
XX_FORMAT_READER_PROBE(adf, XX_FILE_TYPE_ADF,
    (header_size >= 4U && ((header[0] == 0x44U && header[1] == 0x4FU && header[2] == 0x53U && header[3] <= 7U) || ((total_size == 901120 || total_size == 1802240) && header[0] == 0U && header[1] == 0U && header[2] == 0U && header[3] == 0U))))
XX_FORMAT_READER_PROBE(apm, XX_FILE_TYPE_APM,
    (header_size >= 4U && header[0] == 0x45U && header[1] == 0x52U && (header[2] == 2U || header[2] == 4U || header[2] == 8U || header[2] == 16U) && header[3] == 0U))
XX_FORMAT_READER_PROBE(vhdx, XX_FILE_TYPE_VHDX,
    (header_size >= 8U && xx_rt_memcmp(header, "vhdxfile", 8U) == 0))
XX_FORMAT_READER_PROBE(btoa, XX_FILE_TYPE_BTOA,
    ((header_size >= 2U && header[0] == 0x3CU && header[1] == 0x7EU) || (header_size >= 6U && xx_rt_memcmp(header, "xbtoa", 5U) == 0)))
XX_FORMAT_READER_PROBE(chd, XX_FILE_TYPE_CHD,
    (header_size >= 16U && xx_rt_memcmp(header, "MComprHD", 8U) == 0))
XX_FORMAT_READER_PROBE(chm, XX_FILE_TYPE_CHM,
    (header_size >= 4U && xx_rt_memcmp(header, "ITSF", 4U) == 0))
XX_FORMAT_READER_PROBE(cloop, XX_FILE_TYPE_CLOOP,
    (header_size >= 15U && xx_rt_memcmp(header, "#!/bin/sh\n#", 11U) == 0 && header[13] == '.' && header[14] == '0' && (((header[11] == 'V' || header[11] == 'v') && header[12] == '2') || ((header[11] == 'L' || header[11] == 'l') && header[12] == '3') || ((header[11] == 'Z' || header[11] == 'z') && header[12] == '4'))))
XX_FORMAT_READER_PROBE(cue, XX_FILE_TYPE_CUE,
    (xx_cue_test_magic(header, header_size)))
XX_FORMAT_READER_PROBE(dahuazip, XX_FILE_TYPE_DAHUA_ZIP,
    (header_size >= 4U && header[0] == 'D' && header[1] == 'H' && header[2] == 3U && header[3] == 4U))
XX_FORMAT_READER_PROBE(dmsfw, XX_FILE_TYPE_DMSFW,
    (header_size >= 8U && header[0] == 'M' && header[1] == 'G' && xx_rt_memcmp(header + 4U, "0><1", 4U) == 0))
XX_FORMAT_READER_PROBE(ewf, XX_FILE_TYPE_EWF,
    (header_size >= 8U && header[0] == 'E' && header[1] == 'V' && header[2] == 'F' && header[3] == 9U && header[4] == 13U && header[5] == 10U && header[6] == 255U && header[7] == 0U))
XX_FORMAT_READER_PROBE(godot_engine_pck, XX_FILE_TYPE_GODOT_ENGINE_PCK,
    (xx_godot_engine_pck_probe_device(dev)))
XX_FORMAT_READER_PROBE(gpgsigned, XX_FILE_TYPE_GPG_SIGNED,
    (header_size >= 2U && header[0] == 0xA3U && header[1] == 1U))
XX_FORMAT_READER_PROBE(ihex, XX_FILE_TYPE_IHEX,
    (xx_ihex_check_magic(magic, magic_size) || xx_ihex_probe_device(dev, 0)))
XX_FORMAT_READER_PROBE(kwaj, XX_FILE_TYPE_KWAJ,
    (header_size >= 8U && header[0] == 'K' && header[1] == 'W' && header[2] == 'A' && header[3] == 'J' && header[4] == 0x88U && header[5] == 0xF0U && header[6] == 0x27U && header[7] == 0xD1U))
XX_FORMAT_READER_PROBE(lbr, XX_FILE_TYPE_LBR,
    (header_size >= 16U && header[0] == 0U && xx_rt_memcmp(header + 1U, "           ", 11U) == 0 && header[12] == 0U && header[13] == 0U && (header[14] != 0U || header[15] != 0U)))
XX_FORMAT_READER_PROBE(lzfsestream, XX_FILE_TYPE_LZFSE,
    (header_size >= 4U && header[0] == 'b' && header[1] == 'v' && header[2] == 'x' && (header[3] == '-' || header[3] == '1' || header[3] == '2' || header[3] == 'n')))
XX_FORMAT_READER_PROBE(nrg, XX_FILE_TYPE_NRG,
    (xx_nrg_probe_device(dev)))
XX_FORMAT_READER_PROBE(packit_mac, XX_FILE_TYPE_PACKIT_MAC,
    (header_size >= 4U && header[0] == 'P' && header[1] == 'M' && header[2] == 'a' && (header[3] == 'g' || header[3] == '4')))
XX_FORMAT_READER_PROBE(rpm, XX_FILE_TYPE_RPM,
    (header_size >= 4U && header[0] == 0xEDU && header[1] == 0xABU && header[2] == 0xEEU && header[3] == 0xDBU))
XX_FORMAT_READER_PROBE(stuffit5, XX_FILE_TYPE_STUFFIT5,
    (header_size >= 16U && xx_rt_memcmp(header, "StuffIt (c)1997-", 16U) == 0))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(act_apricot_pc_xi_raw, XX_FILE_TYPE_ACT_APRICOT_PC_XI_RAW,
    (header_size >= 93U && total_size >= 143360 && header[0x0E] == 0 && header[0x0F] == 2 && header[0x50] == 0 && header[0x51] == 2 && header[0x52] != 0 && header[0x55] >= 1 && header[0x55] <= 2))
XX_FORMAT_READER_PROBE(adam, XX_FILE_TYPE_ADAM,
    (total_size >= 163840 && xx_adam_detection_hint(dev)))
XX_FORMAT_READER_PROBE(bondwell_2_disk, XX_FILE_TYPE_BONDWELL_2_DISK,
    (total_size == 348160 || total_size == 368640))
XX_FORMAT_READER_PROBE(casio_fz_1_disk, XX_FILE_TYPE_CASIO_FZ_1_DISK,
    (total_size == 1310720 && header_size >= 129U && header[12] == 0 && header[13] == 0 && header[14] == 2 && header[15] == 0 && header[28] == 0 && header[29] == 0 && header[30] == 0 && header[31] == 0 && (header[128] & 3U) == 3U))
XX_FORMAT_READER_PROBE(isz, XX_FILE_TYPE_ISZ,
    (magic_size >= 4U && magic[0] == 'I' && magic[1] == 's' && magic[2] == 'Z' && magic[3] == '!'))
XX_FORMAT_READER_PROBE(mame_floppy_image_mfi, XX_FILE_TYPE_MAME_FLOPPY_IMAGE_MFI,
    (magic_size >= 16U && (xx_rt_memcmp(magic, "MAMEFLOPPYIMAGE", 14U) == 0 || xx_rt_memcmp(magic, "MESSFLOPPYIMAGE", 14U) == 0)))
XX_FORMAT_READER_PROBE(parallels_hdd, XX_FILE_TYPE_PARALLELS_HDD,
    (magic_size >= 16U && (xx_rt_memcmp(magic, "WithoutFreeSpace", 16U) == 0 || xx_rt_memcmp(magic, "WithouFreSpacExt", 16U) == 0)))
XX_FORMAT_READER_PROBE(pc_magazine_flp, XX_FILE_TYPE_PC_MAGAZINE_FLP,
    (magic_size >= 3U && magic[0] == 'P' && magic[1] == 'C' && magic[2] == 'M'))
XX_FORMAT_READER_PROBE(pchrom, XX_FILE_TYPE_PCHROM,
    (header_size >= 20U && header[16] == 0x5A && header[17] == 0xA5 && header[18] == 0xF0 && header[19] == 0x0F))
XX_FORMAT_READER_PROBE(pem, XX_FILE_TYPE_PEM,
    (magic_size >= 11U && xx_rt_memcmp(magic, "-----BEGIN ", 11U) == 0))
XX_FORMAT_READER_PROBE(prodos, XX_FILE_TYPE_PRODOS,
    (total_size >= 1536 && xx_prodos_detection_hint(dev)))
XX_FORMAT_READER_PROBE(qemu_enhanced_disk, XX_FILE_TYPE_QEMU_ENHANCED_DISK,
    (magic_size >= 4U && magic[0] == 'Q' && magic[1] == 'E' && magic[2] == 'D' && magic[3] == 0))
XX_FORMAT_READER_PROBE(rawcd, XX_FILE_TYPE_RAWCD,
    (total_size >= 4672 && ((magic_size >= 16U && magic[0] == 0 && magic[1] == 0xFF && magic[10] == 0xFF && magic[11] == 0 && (magic[15] == 1 || magic[15] == 2)) || total_size % 2336 == 0 || total_size % 2352 == 0 || total_size % 2448 == 0)))
XX_FORMAT_READER_PROBE(rsdos_fs, XX_FILE_TYPE_RSDOS_FS,
    (total_size >= 82944 && total_size <= 2350336))
XX_FORMAT_READER_PROBE(swf, XX_FILE_TYPE_SWF,
    (magic_size >= 8U && (magic[0] == 'F' || magic[0] == 'C' || magic[0] == 'Z') && magic[1] == 'W' && magic[2] == 'S'))
XX_FORMAT_READER_PROBE(t64, XX_FILE_TYPE_T64,
    (magic_size >= 64U && magic[0] == 'C' && magic[1] == '6' && magic[2] == '4'))
XX_FORMAT_READER_PROBE(uue, XX_FILE_TYPE_UUE,
    (magic_size >= 9U && (xx_rt_memcmp(magic, "begin ", 6U) == 0 || (magic[0] == 0xEF && magic[1] == 0xBB && magic[2] == 0xBF && xx_rt_memcmp(magic + 3U, "begin ", 6U) == 0))))
XX_FORMAT_READER_PROBE(vdi, XX_FILE_TYPE_VDI,
    (header_size >= 72U && header[64] == 0x7F && header[65] == 0x10 && header[66] == 0xDA && header[67] == 0xBE))
XX_FORMAT_READER_PROBE(bmp, XX_FILE_TYPE_BMP,
    (header_size >= 2U && (xx_rt_memcmp(header + 0U, "\x42\x4D", 2U) == 0)))
XX_FORMAT_READER_PROBE(cfe, XX_FILE_TYPE_CFE,
    (header_size >= 36U && (xx_rt_memcmp(header + 28U, "\x43\x46\x45\x31\x43\x46\x45\x31", 8U) == 0)))
XX_FORMAT_READER_PROBE(dxbc, XX_FILE_TYPE_DXBC,
    (header_size >= 4U && (xx_rt_memcmp(header + 0U, "\x44\x58\x42\x43", 4U) == 0)))
XX_FORMAT_READER_PROBE(gif, XX_FILE_TYPE_GIF,
    (header_size >= 6U && (xx_rt_memcmp(header + 0U, "\x47\x49\x46\x38\x37\x61", 6U) == 0 || xx_rt_memcmp(header + 0U, "\x47\x49\x46\x38\x39\x61", 6U) == 0)))
XX_FORMAT_READER_PROBE(jpeg, XX_FILE_TYPE_JPEG,
    (xx_jpeg_check_magic(header, header_size)))
XX_FORMAT_READER_PROBE(linuxarm64, XX_FILE_TYPE_LINUX_ARM64,
    (header_size >= 60U && (xx_rt_memcmp(header + 56U, "\x41\x52\x4D\x64", 4U) == 0)))
XX_FORMAT_READER_PROBE(linuxboot, XX_FILE_TYPE_LINUXBOOT,
    (header_size >= 16U && (xx_rt_memcmp(header + 0U, "\xB8\xC0\x07\x8E\xD8\xB8\x00\x90\x8E\xC0\xB9\x00\x01\x29\xF6\x29", 16U) == 0)))
XX_FORMAT_READER_PROBE(linuxzimage, XX_FILE_TYPE_LINUX_ZIMAGE,
    (header_size >= 40U && (xx_rt_memcmp(header + 36U, "\x18\x28\x6F\x01", 4U) == 0 || xx_rt_memcmp(header + 36U, "\x01\x6F\x28\x18", 4U) == 0)))
XX_FORMAT_READER_PROBE(pcapng, XX_FILE_TYPE_PCAPNG,
    (header_size >= 4U && (xx_rt_memcmp(header + 0U, "\x0A\x0D\x0D\x0A", 4U) == 0)))
XX_FORMAT_READER_PROBE(pjl, XX_FILE_TYPE_PJL,
    (xx_pjl_check_magic(header, header_size)))
XX_FORMAT_READER_PROBE(png, XX_FILE_TYPE_PNG,
    (xx_png_check_magic(header, header_size)))
XX_FORMAT_READER_PROBE(svg, XX_FILE_TYPE_SVG,
    (xx_svg_check_magic(header, header_size)))
XX_FORMAT_READER_PROBE(quake_pak, XX_FILE_TYPE_QUAKE_PAK,
    (header_size >= 4U && ((header[0] == 0x50 && header[1] == 0x41 && header[2] == 0x43 && header[3] == 0x4B))))
XX_FORMAT_READER_PROBE(doom_wad, XX_FILE_TYPE_DOOM_WAD,
    (header_size >= 4U && ((header[0] == 0x49 && header[1] == 0x57 && header[2] == 0x41 && header[3] == 0x44) || (header[0] == 0x50 && header[1] == 0x57 && header[2] == 0x41 && header[3] == 0x44))))
XX_FORMAT_READER_PROBE(quake_wad2, XX_FILE_TYPE_QUAKE_WAD2,
    (header_size >= 4U && ((header[0] == 0x57 && header[1] == 0x41 && header[2] == 0x44 && header[3] == 0x32))))
XX_FORMAT_READER_PROBE(halflife_wad3, XX_FILE_TYPE_HALFLIFE_WAD3,
    (header_size >= 4U && ((header[0] == 0x57 && header[1] == 0x41 && header[2] == 0x44 && header[3] == 0x33))))
XX_FORMAT_READER_PROBE(build_grp, XX_FILE_TYPE_BUILD_GRP,
    (header_size >= 12U && ((header[0] == 0x4B && header[1] == 0x65 && header[2] == 0x6E && header[3] == 0x53 && header[4] == 0x69 && header[5] == 0x6C && header[6] == 0x76 && header[7] == 0x65 && header[8] == 0x72 && header[9] == 0x6D && header[10] == 0x61 && header[11] == 0x6E))))
XX_FORMAT_READER_PROBE(cri_afs, XX_FILE_TYPE_CRI_AFS,
    (header_size >= 4U && ((header[0] == 0x41 && header[1] == 0x46 && header[2] == 0x53 && header[3] == 0x00))))
XX_FORMAT_READER_PROBE(cri_awb, XX_FILE_TYPE_CRI_AWB,
    (header_size >= 4U && ((header[0] == 0x41 && header[1] == 0x46 && header[2] == 0x53 && header[3] == 0x32))))
XX_FORMAT_READER_PROBE(valve_vpk, XX_FILE_TYPE_VALVE_VPK,
    (header_size >= 4U && ((header[0] == 0x34 && header[1] == 0x12 && header[2] == 0xAA && header[3] == 0x55))))
XX_FORMAT_READER_PROBE(nintendo_u8, XX_FILE_TYPE_NINTENDO_U8,
    (header_size >= 4U && ((header[0] == 0x55 && header[1] == 0xAA && header[2] == 0x38 && header[3] == 0x2D) || (header[0] == 0x2D && header[1] == 0x38 && header[2] == 0xAA && header[3] == 0x55))))
XX_FORMAT_READER_PROBE(nintendo_rarc, XX_FILE_TYPE_NINTENDO_RARC,
    (header_size >= 4U && ((header[0] == 0x52 && header[1] == 0x41 && header[2] == 0x52 && header[3] == 0x43) || (header[0] == 0x43 && header[1] == 0x52 && header[2] == 0x41 && header[3] == 0x52))))
XX_FORMAT_READER_PROBE(android_ab, XX_FILE_TYPE_ANDROID_AB,
    (header_size >= 15U && xx_rt_memcmp(header, "\x41\x4e\x44\x52\x4f\x49\x44\x20\x42\x41\x43\x4b\x55\x50\x0a", 15U) == 0))
XX_FORMAT_READER_PROBE(nes_rom, XX_FILE_TYPE_NES_ROM,
    (header_size >= 4U && xx_rt_memcmp(header, "\x4e\x45\x53\x1a", 4U) == 0))
XX_FORMAT_READER_PROBE(lynx_lnx, XX_FILE_TYPE_LYNX_LNX,
    (header_size >= 4U && xx_rt_memcmp(header, "\x4c\x59\x4e\x58", 4U) == 0))
XX_FORMAT_READER_PROBE(commodore_crt, XX_FILE_TYPE_COMMODORE_CRT,
    (header_size >= 16U && xx_rt_memcmp(header, "\x43\x36\x34\x20\x43\x41\x52\x54\x52\x49\x44\x47\x45\x20\x20\x20", 16U) == 0))
XX_FORMAT_READER_PROBE(uf2, XX_FILE_TYPE_UF2,
    (header_size >= 8U && xx_rt_memcmp(header, "\x55\x46\x32\x0a\x57\x51\x5d\x9e", 8U) == 0))
XX_FORMAT_READER_PROBE(ico, XX_FILE_TYPE_ICO,
    (header_size >= 4U && xx_rt_memcmp(header, "\x00\x00\x01\x00", 4U) == 0))
XX_FORMAT_READER_PROBE(midi, XX_FILE_TYPE_MIDI,
    (header_size >= 4U && xx_rt_memcmp(header, "\x4d\x54\x68\x64", 4U) == 0))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(bethesda_bsa, XX_FILE_TYPE_BETHESDA_BSA,
    (header_size >= 4 && xx_rt_memcmp(header,"\x42\x53\x41\x00",4) == 0))
XX_FORMAT_READER_PROBE(bethesda_ba2, XX_FILE_TYPE_BETHESDA_BA2,
    (header_size >= 4 && xx_rt_memcmp(header,"\x42\x54\x44\x58",4) == 0))
XX_FORMAT_READER_PROBE(unityfs, XX_FILE_TYPE_UNITYFS,
    (header_size >= 8 && xx_rt_memcmp(header,"\x55\x6e\x69\x74\x79\x46\x53\x00",8) == 0))
XX_FORMAT_READER_PROBE(bioware_biff, XX_FILE_TYPE_BIOWARE_BIFF,
    (header_size >= 4 && xx_rt_memcmp(header,"\x42\x49\x46\x46",4) == 0))
XX_FORMAT_READER_PROBE(bioware_erf, XX_FILE_TYPE_BIOWARE_ERF,
    (header_size >= 4 && (xx_rt_memcmp(header,"ERF ",4)==0 || xx_rt_memcmp(header,"MOD ",4)==0 || xx_rt_memcmp(header,"HAK ",4)==0 || xx_rt_memcmp(header,"SAV ",4)==0)))
XX_FORMAT_READER_PROBE(bioware_rim, XX_FILE_TYPE_BIOWARE_RIM,
    (header_size >= 4 && xx_rt_memcmp(header,"\x52\x49\x4d\x20",4) == 0))
XX_FORMAT_READER_PROBE(lucas_lab, XX_FILE_TYPE_LUCAS_LAB,
    (header_size >= 4 && xx_rt_memcmp(header,"\x4c\x41\x42\x4e",4) == 0))
XX_FORMAT_READER_PROBE(lucas_bun, XX_FILE_TYPE_LUCAS_BUN,
    (header_size >= 4 && (xx_rt_memcmp(header,"LB83",4)==0 || xx_rt_memcmp(header,"LB23",4)==0)))
XX_FORMAT_READER_PROBE(idtech_bsp, XX_FILE_TYPE_IDTECH_BSP,
    (header_size >= 4 && xx_rt_memcmp(header,"\x49\x42\x53\x50",4) == 0))
XX_FORMAT_READER_PROBE(valve_bsp, XX_FILE_TYPE_VALVE_BSP,
    (header_size >= 4 && xx_rt_memcmp(header,"\x56\x42\x53\x50",4) == 0))
XX_FORMAT_READER_PROBE(idtech_md2, XX_FILE_TYPE_IDTECH_MD2,
    (header_size >= 4 && xx_rt_memcmp(header,"\x49\x44\x50\x32",4) == 0))
XX_FORMAT_READER_PROBE(idtech_md3, XX_FILE_TYPE_IDTECH_MD3,
    (header_size >= 4 && xx_rt_memcmp(header,"\x49\x44\x50\x33",4) == 0))
XX_FORMAT_READER_PROBE(idtech_qvm, XX_FILE_TYPE_IDTECH_QVM,
    (header_size >= 4 && xx_rt_memcmp(header,"\x44\x14\x72\x12",4) == 0))
XX_FORMAT_READER_PROBE(mohawk_mhk, XX_FILE_TYPE_MOHAWK_MHK,
    (header_size >= 4 && xx_rt_memcmp(header,"\x4d\x48\x57\x4b",4) == 0))
XX_FORMAT_READER_PROBE(quake_sprite, XX_FILE_TYPE_QUAKE_SPRITE,
    (header_size >= 4 && xx_rt_memcmp(header,"\x49\x44\x53\x50",4) == 0))
XX_FORMAT_READER_PROBE(nintendo_narc, XX_FILE_TYPE_NINTENDO_NARC,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(nintendo_sarc, XX_FILE_TYPE_NINTENDO_SARC,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(nintendo_pfs0, XX_FILE_TYPE_NINTENDO_PFS0,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(nintendo_hfs0, XX_FILE_TYPE_NINTENDO_HFS0,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(nintendo_brres, XX_FILE_TYPE_NINTENDO_BRRES,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(nintendo_bcstm, XX_FILE_TYPE_NINTENDO_BCSTM,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(nintendo_bfsar, XX_FILE_TYPE_NINTENDO_BFSAR,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(nintendo_bcsar, XX_FILE_TYPE_NINTENDO_BCSAR,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(sony_psarc, XX_FILE_TYPE_SONY_PSARC,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(ktx, XX_FILE_TYPE_KTX,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(ktx2, XX_FILE_TYPE_KTX2,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(dds, XX_FILE_TYPE_DDS,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(pvr, XX_FILE_TYPE_PVR,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(valve_vtf, XX_FILE_TYPE_VALVE_VTF,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(xbox_xbe, XX_FILE_TYPE_XBOX_XBE,
    (total_size >= 16))
XX_FORMAT_READER_PROBE(flac, XX_FILE_TYPE_FLAC,
    (magic_size >= 4 && !xx_rt_memcmp(magic,"fLaC",4)))
XX_FORMAT_READER_PROBE(ogg, XX_FILE_TYPE_OGG,
    (magic_size >= 5 && !xx_rt_memcmp(magic,"OggS",4) && magic[4] == 0))
XX_FORMAT_READER_PROBE(mp4, XX_FILE_TYPE_MP4,
    (magic_size >= 12 && !xx_rt_memcmp(magic+4,"ftyp",4)))
XX_FORMAT_READER_PROBE(matroska, XX_FILE_TYPE_MATROSKA,
    (magic_size >= 4 && magic[0]==0x1A && magic[1]==0x45 && magic[2]==0xDF && magic[3]==0xA3))
XX_FORMAT_READER_PROBE(aiff, XX_FILE_TYPE_AIFF,
    (magic_size >= 12 && !xx_rt_memcmp(magic,"FORM",4) && (!xx_rt_memcmp(magic+8,"AIFF",4) || !xx_rt_memcmp(magic+8,"AIFC",4))))
XX_FORMAT_READER_PROBE(caf, XX_FILE_TYPE_CAF,
    (magic_size >= 8 && !xx_rt_memcmp(magic,"caff",4) && magic[4]==0 && magic[5]==1))
XX_FORMAT_READER_PROBE(photoshop_psd, XX_FILE_TYPE_PHOTOSHOP_PSD,
    (magic_size >= 6 && !xx_rt_memcmp(magic,"8BPS",4) && magic[4]==0 && (magic[5]==1 || magic[5]==2)))
XX_FORMAT_READER_PROBE(tiff, XX_FILE_TYPE_TIFF,
    (magic_size >= 4 && ((magic[0]==0x49 && magic[1]==0x49 && (magic[2]==42 || magic[2]==43) && magic[3]==0) || (magic[0]==0x4D && magic[1]==0x4D && magic[2]==0 && (magic[3]==42 || magic[3]==43)))))
XX_FORMAT_READER_PROBE(openexr, XX_FILE_TYPE_OPENEXR,
    (magic_size >= 8 && magic[0]==0x76 && magic[1]==0x2F && magic[2]==0x31 && magic[3]==1 && magic[4]==2))
XX_FORMAT_READER_PROBE(jpeg2000_jp2, XX_FILE_TYPE_JPEG2000_JP2,
    (magic_size >= 12 && magic[0]==0 && magic[1]==0 && magic[2]==0 && magic[3]==12 && !xx_rt_memcmp(magic+4,"jP  ",4) && magic[8]==13 && magic[9]==10 && magic[10]==0x87 && magic[11]==10))
XX_FORMAT_READER_PROBE(android_vendor_boot, XX_FILE_TYPE_ANDROID_VENDOR_BOOT,
    (header_size >= 8 && header[0] == 86 && header[1] == 78 && header[2] == 68 && header[3] == 82 && header[4] == 66 && header[5] == 79 && header[6] == 79 && header[7] == 84))
XX_FORMAT_READER_PROBE(android_dtbo, XX_FILE_TYPE_ANDROID_DTBO,
    (header_size >= 4 && header[0] == 215 && header[1] == 183 && header[2] == 171 && header[3] == 30))
XX_FORMAT_READER_PROBE(android_vbmeta, XX_FILE_TYPE_ANDROID_VBMETA,
    (header_size >= 4 && header[0] == 65 && header[1] == 86 && header[2] == 66 && header[3] == 48))
XX_FORMAT_READER_PROBE(espressif_image, XX_FILE_TYPE_ESPRESSIF_IMAGE,
    (header_size >= 24 && header[0] == 0xe9 && header[1] > 0 && header[1] <= 16 && header[12] == 0 && header[13] == 0))
XX_FORMAT_READER_PROBE(wasm, XX_FILE_TYPE_WASM,
    (header_size >= 8 && header[0] == 0 && header[1] == 97 && header[2] == 115 && header[3] == 109 && header[4] == 1 && header[5] == 0 && header[6] == 0 && header[7] == 0))
XX_FORMAT_READER_PROBE(llvm_bitcode_wrapper, XX_FILE_TYPE_LLVM_BITCODE_WRAPPER,
    (header_size >= 4 && header[0] == 222 && header[1] == 192 && header[2] == 23 && header[3] == 11))
XX_FORMAT_READER_PROBE(dotnet_metadata, XX_FILE_TYPE_DOTNET_METADATA,
    (header_size >= 4 && header[0] == 66 && header[1] == 83 && header[2] == 74 && header[3] == 66))
XX_FORMAT_READER_PROBE(sfnt, XX_FILE_TYPE_SFNT,
    (header_size >= 12 && ((header[0] == 0 && header[1] == 1 && header[2] == 0 && header[3] == 0) || !xx_rt_memcmp(header,"OTTO",4) || !xx_rt_memcmp(header,"true",4) || !xx_rt_memcmp(header,"typ1",4))))
XX_FORMAT_READER_PROBE(woff, XX_FILE_TYPE_WOFF,
    (header_size >= 4 && header[0] == 119 && header[1] == 79 && header[2] == 70 && header[3] == 70))
XX_FORMAT_READER_PROBE(woff2, XX_FILE_TYPE_WOFF2,
    (header_size >= 4 && header[0] == 119 && header[1] == 79 && header[2] == 70 && header[3] == 50))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(makeself, XX_FILE_TYPE_MAKESELF,
    (header_size >= 9 && (xx_rt_memcmp(header+0,"\x23\x21\x2f\x62\x69\x6e\x2f\x73\x68",9) == 0)))
XX_FORMAT_READER_PROBE(sun_java_binsh, XX_FILE_TYPE_SUN_JAVA_BINSH,
    (header_size >= 9 && (xx_rt_memcmp(header+0,"\x23\x21\x2f\x62\x69\x6e\x2f\x73\x68",9) == 0)))
XX_FORMAT_READER_PROBE(installanywhere_unix, XX_FILE_TYPE_INSTALLANYWHERE_UNIX,
    (header_size >= 9 && (xx_rt_memcmp(header+0,"\x23\x21\x2f\x62\x69\x6e\x2f\x73\x68",9) == 0)))
XX_FORMAT_READER_PROBE(sfx_packagefortheweb, XX_FILE_TYPE_SFX_PACKAGEFORTHEWEB,
    (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0)))
XX_FORMAT_READER_PROBE(sfx_spis, XX_FILE_TYPE_SFX_SPIS,
    (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0)))
XX_FORMAT_READER_PROBE(sfx_lha, XX_FILE_TYPE_SFX_LHA,
    (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\xeb",1) == 0)))
XX_FORMAT_READER_PROBE(lmd_container, XX_FILE_TYPE_LMD_CONTAINER,
    (header_size >= 9 && (xx_rt_memcmp(header+0,"\x08\x4c\x4d\x44\x53\x4c\x4c\x33\x30",9) == 0 || xx_rt_memcmp(header+0,"\x08\x4c\x4d\x44\x42\x4d\x4c\x33\x30",9) == 0)))
XX_FORMAT_READER_PROBE(totalannihilation_hpi, XX_FILE_TYPE_TOTALANNIHILATION_HPI,
    (header_size >= 4 && (xx_rt_memcmp(header+0,"\x48\x41\x50\x49",4) == 0)))
XX_FORMAT_READER_PROBE(ravensoft_rff, XX_FILE_TYPE_RAVENSOFT_RFF,
    (header_size >= 4 && (xx_rt_memcmp(header+0,"\x52\x46\x46\x1a",4) == 0)))
XX_FORMAT_READER_PROBE(terminalreality_pod, XX_FILE_TYPE_TERMINALREALITY_POD,
    (header_size >= 4 && (xx_rt_memcmp(header+0,"\x50\x4f\x44\x32",4) == 0)))
XX_FORMAT_READER_PROBE(volition_vpp, XX_FILE_TYPE_VOLITION_VPP,
    (header_size >= 4 && (xx_rt_memcmp(header+0,"\xce\x0a\x89\x51",4) == 0 || xx_rt_memcmp(header+0,"\x51\x89\x0a\xce",4) == 0)))
XX_FORMAT_READER_PROBE(kirikiri_xp3, XX_FILE_TYPE_KIRIKIRI_XP3,
    (header_size >= 11 && (xx_rt_memcmp(header+0,"\x58\x50\x33\x0d\x0a\x20\x0a\x1a\x8b\x67\x01",11) == 0)))
XX_FORMAT_READER_PROBE(fromsoftware_binder, XX_FILE_TYPE_FROMSOFTWARE_BINDER,
    (header_size >= 4 && (xx_rt_memcmp(header+0,"\x42\x4e\x44\x33",4) == 0 || xx_rt_memcmp(header+0,"\x42\x4e\x44\x34",4) == 0)))
XX_FORMAT_READER_PROBE(mythic_myp, XX_FILE_TYPE_MYTHIC_MYP,
    (header_size >= 4 && (xx_rt_memcmp(header+0,"\x4d\x59\x50\x00",4) == 0)))
XX_FORMAT_READER_PROBE(lithtech_rez, XX_FILE_TYPE_LITHTECH_REZ,
    (header_size >= 18 && (xx_rt_memcmp(header+0,"\x0d\x0a\x52\x65\x7a\x4d\x67\x72\x20\x56\x65\x72\x73\x69\x6f\x6e\x20\x31",18) == 0)))
XX_FORMAT_READER_PROBE(nintendo_ncch, XX_FILE_TYPE_NINTENDO_NCCH,
    (total_size >= 512 && header_size >= 260U && header[256] == 0x4eU && header[257] == 0x43U && header[258] == 0x43U && header[259] == 0x48U))
XX_FORMAT_READER_PROBE(nintendo_ncsd, XX_FILE_TYPE_NINTENDO_NCSD,
    (total_size >= 512 && header_size >= 260U && header[256] == 0x4eU && header[257] == 0x43U && header[258] == 0x53U && header[259] == 0x44U))
XX_FORMAT_READER_PROBE(nintendo_cia, XX_FILE_TYPE_NINTENDO_CIA,
    (total_size >= 8224 && header_size >= 8U && header[0] == 0x20U && header[1] == 0x20U && header[2] == 0x00U && header[3] == 0x00U && header[4] == 0x00U && header[5] == 0x00U && header[6] == 0x00U && header[7] == 0x00U))
XX_FORMAT_READER_PROBE(nintendo_nds, XX_FILE_TYPE_NINTENDO_NDS,
    (total_size >= 512 && header_size >= 208U && header[192] == 0x24U && header[193] == 0xffU && header[194] == 0xaeU && header[195] == 0x51U && header[196] == 0x69U && header[197] == 0x9aU && header[198] == 0xa2U && header[199] == 0x21U && header[200] == 0x3dU && header[201] == 0x84U && header[202] == 0x82U && header[203] == 0x0aU && header[204] == 0x84U && header[205] == 0xe4U && header[206] == 0x09U && header[207] == 0xadU))
XX_FORMAT_READER_PROBE(nintendo_gcm, XX_FILE_TYPE_NINTENDO_GCM,
    (total_size >= 9312 && header_size >= 32U && header[28] == 0xc2U && header[29] == 0x33U && header[30] == 0x9fU && header[31] == 0x3dU))
XX_FORMAT_READER_PROBE(nintendo_tpl, XX_FILE_TYPE_NINTENDO_TPL,
    (total_size >= 12 && header_size >= 4U && header[0] == 0x00U && header[1] == 0x20U && header[2] == 0xafU && header[3] == 0x30U))
XX_FORMAT_READER_PROBE(sony_tim, XX_FILE_TYPE_SONY_TIM,
    (total_size >= 20 && header_size >= 4U && header[0] == 0x10U && header[1] == 0x00U && header[2] == 0x00U && header[3] == 0x00U))
XX_FORMAT_READER_PROBE(sony_vag, XX_FILE_TYPE_SONY_VAG,
    (total_size >= 48 && header_size >= 4U && header[0] == 0x56U && header[1] == 0x41U && header[2] == 0x47U && header[3] == 0x70U))
XX_FORMAT_READER_PROBE(larian_lspk, XX_FILE_TYPE_LARIAN_LSPK,
    (total_size >= 24 && header_size >= 4U && header[0] == 0x4cU && header[1] == 0x53U && header[2] == 0x50U && header[3] == 0x4bU))
XX_FORMAT_READER_PROBE(larian_lsf, XX_FILE_TYPE_LARIAN_LSF,
    (total_size >= 52 && header_size >= 4U && header[0] == 0x4cU && header[1] == 0x53U && header[2] == 0x4fU && header[3] == 0x46U))
XX_FORMAT_READER_PROBE(valve_hpak, XX_FILE_TYPE_VALVE_HPAK,
    (total_size >= 16 && header_size >= 4U && header[0] == 0x48U && header[1] == 0x50U && header[2] == 0x41U && header[3] == 0x4bU))
XX_FORMAT_READER_PROBE(renpy_rpa, XX_FILE_TYPE_RENPY_RPA,
    (total_size >= 34 && header_size >= 8U && header[0] == 0x52U && header[1] == 0x50U && header[2] == 0x41U && header[3] == 0x2dU && header[4] == 0x33U && header[5] == 0x2eU && header[6] == 0x30U && header[7] == 0x20U))
XX_FORMAT_READER_PROBE(unreal_package, XX_FILE_TYPE_UNREAL_PACKAGE,
    (total_size >= 44 && header_size >= 4U && header[0] == 0xc1U && header[1] == 0x83U && header[2] == 0x2aU && header[3] == 0x9eU))
XX_FORMAT_READER_PROBE(sega_pvr2, XX_FILE_TYPE_SEGA_PVR2,
    (total_size >= 16 && header_size >= 4U && header[0] == 0x50U && header[1] == 0x56U && header[2] == 0x52U && header[3] == 0x54U))
XX_FORMAT_READER_PROBE(nintendo_bntx, XX_FILE_TYPE_NINTENDO_BNTX,
    (total_size >= 88 && header_size >= 8U && header[0] == 0x42U && header[1] == 0x4eU && header[2] == 0x54U && header[3] == 0x58U && header[4] == 0x00U && header[5] == 0x00U && header[6] == 0x00U && header[7] == 0x00U))
XX_FORMAT_READER_PROBE(icns, XX_FILE_TYPE_ICNS,
    (header_size >= 8 && !xx_rt_memcmp(header,"icns",4)))
XX_FORMAT_READER_PROBE(xcursor, XX_FILE_TYPE_XCURSOR,
    (header_size >= 16 && !xx_rt_memcmp(header,"Xcur",4)))
XX_FORMAT_READER_PROBE(icc, XX_FILE_TYPE_ICC,
    (header_size >= 40 && !xx_rt_memcmp(header+36,"acsp",4) && (header[8]==2 || header[8]==4)))
XX_FORMAT_READER_PROBE(qoi, XX_FILE_TYPE_QOI,
    (header_size >= 14 && !xx_rt_memcmp(header,"qoif",4)))
XX_FORMAT_READER_PROBE(farbfeld, XX_FILE_TYPE_FARBFELD,
    (header_size >= 16 && !xx_rt_memcmp(header,"farbfeld",8)))
XX_FORMAT_READER_PROBE(pnm, XX_FILE_TYPE_PNM,
    (header_size >= 3 && header[0]==80 && header[1]>=49 && header[1]<=55 && (header[2]==9 || header[2]==11 || header[2]==12 || header[2]==10 || header[2]==13 || header[2]==32)))
XX_FORMAT_READER_PROBE(tga, XX_FILE_TYPE_TGA,
    (header_size >= 18 && (header[2]==1 || header[2]==2 || header[2]==3 || header[2]==9 || header[2]==10 || header[2]==11) && (header[12] || header[13]) && (header[14] || header[15]) && (header[16]==8 || header[16]==15 || header[16]==16 || header[16]==24 || header[16]==32) && !(header[17]&0xC0)))
XX_FORMAT_READER_PROBE(sun_raster, XX_FILE_TYPE_SUN_RASTER,
    (header_size >= 32 && header[0]==0x59 && header[1]==0xA6 && header[2]==0x6A && header[3]==0x95))
XX_FORMAT_READER_PROBE(fits, XX_FILE_TYPE_FITS,
    (header_size >= 30 && !xx_rt_memcmp(header,"SIMPLE  =",9) && header[29]==84))
XX_FORMAT_READER_PROBE(dicom, XX_FILE_TYPE_DICOM,
    (header_size >= 132 && !xx_rt_memcmp(header+128,"DICM",4)))
XX_FORMAT_READER_PROBE(pcap, XX_FILE_TYPE_PCAP,
    (((header_size >= 4 && header[0] == 212 && header[1] == 195 && header[2] == 178 && header[3] == 161) || (header_size >= 4 && header[0] == 161 && header[1] == 178 && header[2] == 195 && header[3] == 212) || (header_size >= 4 && header[0] == 77 && header[1] == 60 && header[2] == 178 && header[3] == 161) || (header_size >= 4 && header[0] == 161 && header[1] == 178 && header[2] == 60 && header[3] == 77))))
XX_FORMAT_READER_PROBE(btsnoop, XX_FILE_TYPE_BTSNOOP,
    (((header_size >= 8 && header[0] == 98 && header[1] == 116 && header[2] == 115 && header[3] == 110 && header[4] == 111 && header[5] == 111 && header[6] == 112 && header[7] == 0))))
XX_FORMAT_READER_PROBE(java_class, XX_FILE_TYPE_JAVA_CLASS,
    (((header_size >= 4 && header[0] == 202 && header[1] == 254 && header[2] == 186 && header[3] == 190))))
XX_FORMAT_READER_PROBE(sfnt_collection, XX_FILE_TYPE_SFNT_COLLECTION,
    (((header_size >= 4 && header[0] == 116 && header[1] == 116 && header[2] == 99 && header[3] == 102))))
XX_FORMAT_READER_PROBE(sqlite3, XX_FILE_TYPE_SQLITE3,
    (((header_size >= 16 && header[0] == 83 && header[1] == 81 && header[2] == 76 && header[3] == 105 && header[4] == 116 && header[5] == 101 && header[6] == 32 && header[7] == 102 && header[8] == 111 && header[9] == 114 && header[10] == 109 && header[11] == 97 && header[12] == 116 && header[13] == 32 && header[14] == 51 && header[15] == 0))))
XX_FORMAT_READER_PROBE(sqlite_wal, XX_FILE_TYPE_SQLITE_WAL,
    (((header_size >= 4 && header[0] == 55 && header[1] == 127 && header[2] == 6 && header[3] == 130) || (header_size >= 4 && header[0] == 55 && header[1] == 127 && header[2] == 6 && header[3] == 131))))
XX_FORMAT_READER_PROBE(avro_object, XX_FILE_TYPE_AVRO_OBJECT,
    (((header_size >= 4 && header[0] == 79 && header[1] == 98 && header[2] == 106 && header[3] == 1))))
XX_FORMAT_READER_PROBE(glb, XX_FILE_TYPE_GLB,
    (((header_size >= 4 && header[0] == 103 && header[1] == 108 && header[2] == 84 && header[3] == 70))))
XX_FORMAT_READER_PROBE(spirv, XX_FILE_TYPE_SPIRV,
    (((header_size >= 4 && header[0] == 3 && header[1] == 2 && header[2] == 35 && header[3] == 7) || (header_size >= 4 && header[0] == 7 && header[1] == 35 && header[2] == 2 && header[3] == 3))))
XX_FORMAT_READER_PROBE(crx, XX_FILE_TYPE_CRX,
    (((header_size >= 4 && header[0] == 67 && header[1] == 114 && header[2] == 50 && header[3] == 52))))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(sfx_arc, XX_FILE_TYPE_SFX_ARC,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_arj, XX_FILE_TYPE_SFX_ARJ,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_bsn, XX_FILE_TYPE_SFX_BSN,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_arq, XX_FILE_TYPE_SFX_ARQ,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_gxl, XX_FILE_TYPE_SFX_GXL,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_asymetrix, XX_FILE_TYPE_SFX_ASYMETRIX,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_rta, XX_FILE_TYPE_SFX_RTA,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_rtpatch, XX_FILE_TYPE_SFX_RTPATCH,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(esp_archive, XX_FILE_TYPE_ESP_ARCHIVE,
    (header_size >= 4 && (xx_rt_memcmp(header+0,"\x45\x53\x50\x3e",4) == 0 || xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0)))
XX_FORMAT_READER_PROBE(sfx_kwaj, XX_FILE_TYPE_SFX_KWAJ,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(gemdos_lha, XX_FILE_TYPE_GEMDOS_LHA,
    (header_size >= 2 && (xx_rt_memcmp(header+0,"\x60\x1a",2) == 0)))
XX_FORMAT_READER_PROBE(winimage_zip, XX_FILE_TYPE_WINIMAGE_ZIP,
    (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0)))
XX_FORMAT_READER_PROBE(hp3000_wrq, XX_FILE_TYPE_HP3000_WRQ,
    (header_size >= 8 && (xx_rt_memcmp(header+0,"\x52\x45\x43\x53\x49\x5a\x45\x3d",8) == 0)))
XX_FORMAT_READER_PROBE(icu_data_package, XX_FILE_TYPE_ICU_DATA_PACKAGE,
    (header_size >= 4 && (xx_rt_memcmp(header+2,"\xda\x27",2) == 0)))
XX_FORMAT_READER_PROBE(sfx_sqz, XX_FILE_TYPE_SFX_SQZ,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(nintendo_bfstm, XX_FILE_TYPE_NINTENDO_BFSTM,
    (total_size >= 64 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x53U && header[2] == 0x54U && header[3] == 0x4dU))
XX_FORMAT_READER_PROBE(nintendo_bfwav, XX_FILE_TYPE_NINTENDO_BFWAV,
    (total_size >= 64 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x57U && header[2] == 0x41U && header[3] == 0x56U))
XX_FORMAT_READER_PROBE(nintendo_bcwav, XX_FILE_TYPE_NINTENDO_BCWAV,
    (total_size >= 64 && header_size >= 4U && header[0] == 0x43U && header[1] == 0x57U && header[2] == 0x41U && header[3] == 0x56U))
XX_FORMAT_READER_PROBE(nintendo_bfres, XX_FILE_TYPE_NINTENDO_BFRES,
    (total_size >= 108 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x52U && header[2] == 0x45U && header[3] == 0x53U))
XX_FORMAT_READER_PROBE(nintendo_bflyt, XX_FILE_TYPE_NINTENDO_BFLYT,
    (total_size >= 20 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x4cU && header[2] == 0x59U && header[3] == 0x54U))
XX_FORMAT_READER_PROBE(nintendo_bclyt, XX_FILE_TYPE_NINTENDO_BCLYT,
    (total_size >= 20 && header_size >= 4U && header[0] == 0x43U && header[1] == 0x4cU && header[2] == 0x59U && header[3] == 0x54U))
XX_FORMAT_READER_PROBE(nintendo_bfnt, XX_FILE_TYPE_NINTENDO_BFNT,
    (total_size >= 20 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x46U && header[2] == 0x4eU && header[3] == 0x54U))
XX_FORMAT_READER_PROBE(nintendo_bcfnt, XX_FILE_TYPE_NINTENDO_BCFNT,
    (total_size >= 20 && header_size >= 4U && header[0] == 0x43U && header[1] == 0x46U && header[2] == 0x4eU && header[3] == 0x54U))
XX_FORMAT_READER_PROBE(nintendo_3dsx, XX_FILE_TYPE_NINTENDO_3DSX,
    (total_size >= 32 && header_size >= 4U && header[0] == 0x33U && header[1] == 0x44U && header[2] == 0x53U && header[3] == 0x58U))
XX_FORMAT_READER_PROBE(sony_tim2, XX_FILE_TYPE_SONY_TIM2,
    (total_size >= 16 && header_size >= 4U && header[0] == 0x54U && header[1] == 0x49U && header[2] == 0x4dU && header[3] == 0x32U))
XX_FORMAT_READER_PROBE(sony_pamf, XX_FILE_TYPE_SONY_PAMF,
    (total_size >= 184 && header_size >= 4U && header[0] == 0x50U && header[1] == 0x41U && header[2] == 0x4dU && header[3] == 0x46U))
XX_FORMAT_READER_PROBE(sega_gvr, XX_FILE_TYPE_SEGA_GVR,
    (total_size >= 16 && header_size >= 4U && header[0] == 0x47U && header[1] == 0x56U && header[2] == 0x52U && header[3] == 0x54U))
XX_FORMAT_READER_PROBE(microsoft_xwb, XX_FILE_TYPE_MICROSOFT_XWB,
    (total_size >= 52 && header_size >= 4U && header[0] == 0x57U && header[1] == 0x42U && header[2] == 0x4eU && header[3] == 0x44U))
XX_FORMAT_READER_PROBE(microsoft_xsb, XX_FILE_TYPE_MICROSOFT_XSB,
    (total_size >= 138 && header_size >= 4U && header[0] == 0x53U && header[1] == 0x44U && header[2] == 0x42U && header[3] == 0x4bU))
XX_FORMAT_READER_PROBE(relic_sga, XX_FILE_TYPE_RELIC_SGA,
    (total_size >= 152 && header_size >= 8U && header[0] == 0x5fU && header[1] == 0x41U && header[2] == 0x52U && header[3] == 0x43U && header[4] == 0x48U && header[5] == 0x49U && header[6] == 0x56U && header[7] == 0x45U))
XX_FORMAT_READER_PROBE(xpm, XX_FILE_TYPE_XPM,
    (header_size >= 9 && !xx_rt_memcmp(header,"/* XPM */",9)))
XX_FORMAT_READER_PROBE(pcx, XX_FILE_TYPE_PCX,
    (header_size >= 68 && header[0]==10 && (header[1]==0 || (header[1]>=2 && header[1]<=5)) && header[2]==1 && (header[3]==1 || header[3]==2 || header[3]==4 || header[3]==8)))
XX_FORMAT_READER_PROBE(iff_ilbm, XX_FILE_TYPE_IFF_ILBM,
    (header_size >= 12 && !xx_rt_memcmp(header,"FORM",4) && !xx_rt_memcmp(header+8,"ILBM",4)))
XX_FORMAT_READER_PROBE(utah_rle, XX_FILE_TYPE_UTAH_RLE,
    (header_size >= 16 && header[0]==0x52 && header[1]==0xCC && header[12]==8))
XX_FORMAT_READER_PROBE(radiance_hdr, XX_FILE_TYPE_RADIANCE_HDR,
    (header_size >= 7 && ((!xx_rt_memcmp(header,"#?RGBE",6) && (header[6]==10 || header[6]==13)) || (header_size>=11 && !xx_rt_memcmp(header,"#?RADIANCE",10) && (header[10]==10 || header[10]==13)))))
XX_FORMAT_READER_PROBE(dpx, XX_FILE_TYPE_DPX,
    (header_size >= 36 && (!xx_rt_memcmp(header,"SDPX",4) || !xx_rt_memcmp(header,"XPDS",4))))
XX_FORMAT_READER_PROBE(cineon, XX_FILE_TYPE_CINEON,
    (header_size >= 32 && ((header[0]==0x80 && header[1]==0x2A && header[2]==0x5F && header[3]==0xD7) || (header[0]==0xD7 && header[1]==0x5F && header[2]==0x2A && header[3]==0x80))))
XX_FORMAT_READER_PROBE(xwd, XX_FILE_TYPE_XWD,
    (header_size >= 100 && header[4]==0 && header[5]==0 && header[6]==0 && header[7]==7 && header[8]==0 && header[9]==0 && header[10]==0 && header[11]==2))
XX_FORMAT_READER_PROBE(sgi_rgb, XX_FILE_TYPE_SGI_RGB,
    (header_size >= 12 && header[0]==1 && header[1]==0xDA && header[2]<=1 && (header[3]==1 || header[3]==2)))
XX_FORMAT_READER_PROBE(aseprite, XX_FILE_TYPE_ASEPRITE,
    (header_size >= 128 && header[4]==0xE0 && header[5]==0xA5 && (header[12]==8 || header[12]==16 || header[12]==32) && header[13]==0))
XX_FORMAT_READER_PROBE(numpy_npy, XX_FILE_TYPE_NUMPY_NPY,
    (((header_size >= 6 && header[0] == 147 && header[1] == 78 && header[2] == 85 && header[3] == 77 && header[4] == 80 && header[5] == 89))))
XX_FORMAT_READER_PROBE(matlab_mat5, XX_FILE_TYPE_MATLAB_MAT5,
    (((header_size >= 128 && header[126] == 73 && header[127] == 77) || (header_size >= 128 && header[126] == 77 && header[127] == 73))))
XX_FORMAT_READER_PROBE(netcdf_classic, XX_FILE_TYPE_NETCDF_CLASSIC,
    (((header_size >= 3 && header[0] == 67 && header[1] == 68 && header[2] == 70))))
XX_FORMAT_READER_PROBE(hdf4, XX_FILE_TYPE_HDF4,
    (((header_size >= 4 && header[0] == 14 && header[1] == 3 && header[2] == 19 && header[3] == 1))))
XX_FORMAT_READER_PROBE(sas_xport, XX_FILE_TYPE_SAS_XPORT,
    (((header_size >= 48 && header[0] == 72 && header[1] == 69 && header[2] == 65 && header[3] == 68 && header[4] == 69 && header[5] == 82 && header[6] == 32 && header[7] == 82 && header[8] == 69 && header[9] == 67 && header[10] == 79 && header[11] == 82 && header[12] == 68 && header[13] == 42 && header[14] == 42 && header[15] == 42 && header[16] == 42 && header[17] == 42 && header[18] == 42 && header[19] == 42 && header[20] == 76 && header[21] == 73 && header[22] == 66 && header[23] == 82 && header[24] == 65 && header[25] == 82 && header[26] == 89 && header[27] == 32 && header[28] == 72 && header[29] == 69 && header[30] == 65 && header[31] == 68 && header[32] == 69 && header[33] == 82 && header[34] == 32 && header[35] == 82 && header[36] == 69 && header[37] == 67 && header[38] == 79 && header[39] == 82 && header[40] == 68 && header[41] == 33 && header[42] == 33 && header[43] == 33 && header[44] == 33 && header[45] == 33 && header[46] == 33 && header[47] == 33))))
XX_FORMAT_READER_PROBE(spss_sav, XX_FILE_TYPE_SPSS_SAV,
    (((header_size >= 4 && header[0] == 36 && header[1] == 70 && header[2] == 76 && header[3] == 50))))
XX_FORMAT_READER_PROBE(stata_dta, XX_FILE_TYPE_STATA_DTA,
    (((header_size >= 11 && header[0] == 60 && header[1] == 115 && header[2] == 116 && header[3] == 97 && header[4] == 116 && header[5] == 97 && header[6] == 95 && header[7] == 100 && header[8] == 116 && header[9] == 97 && header[10] == 62))))
XX_FORMAT_READER_PROBE(apache_arrow_file, XX_FILE_TYPE_APACHE_ARROW_FILE,
    (((header_size >= 6 && header[0] == 65 && header[1] == 82 && header[2] == 82 && header[3] == 79 && header[4] == 87 && header[5] == 49))))
XX_FORMAT_READER_PROBE(apache_parquet, XX_FILE_TYPE_APACHE_PARQUET,
    (((header_size >= 4 && header[0] == 80 && header[1] == 65 && header[2] == 82 && header[3] == 49))))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(sfx_arcv2, XX_FILE_TYPE_SFX_ARCV2,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_chz, XX_FILE_TYPE_SFX_CHZ,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_szdd, XX_FILE_TYPE_SFX_SZDD,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_mpq, XX_FILE_TYPE_SFX_MPQ,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_swag, XX_FILE_TYPE_SFX_SWAG,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_zpak, XX_FILE_TYPE_SFX_ZPAK,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_diskexpress, XX_FILE_TYPE_SFX_DISKEXPRESS,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_bzip2, XX_FILE_TYPE_SFX_BZIP2,
    (!is_mz && (header_size >= 4 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x7f\x45\x4c\x46",4) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_gzip, XX_FILE_TYPE_SFX_GZIP,
    (!is_mz && (header_size >= 4 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x7f\x45\x4c\x46",4) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_tar, XX_FILE_TYPE_SFX_TAR,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_cab, XX_FILE_TYPE_SFX_CAB,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(pmarc_sfx, XX_FILE_TYPE_PMARC_SFX,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x18\x0a",2) == 0 || xx_rt_memcmp(header+0,"\x18\x79",2) == 0 || xx_rt_memcmp(header+0,"\xeb\x18",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_7zip, XX_FILE_TYPE_SFX_7ZIP,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_ace, XX_FILE_TYPE_SFX_ACE,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_zipcentral, XX_FILE_TYPE_SFX_ZIPCENTRAL,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sony_psx_exe, XX_FILE_TYPE_SONY_PSX_EXE,
    (total_size >= 2048 && header_size >= 8U && header[0] == 0x50U && header[1] == 0x53U && header[2] == 0x2dU && header[3] == 0x58U && header[4] == 0x20U && header[5] == 0x45U && header[6] == 0x58U && header[7] == 0x45U))
XX_FORMAT_READER_PROBE(sony_psf, XX_FILE_TYPE_SONY_PSF,
    (total_size >= 16 && header_size >= 4U && header[0] == 0x50U && header[1] == 0x53U && header[2] == 0x46U && header[3] == 0x01U))
XX_FORMAT_READER_PROBE(nintendo_wbfs, XX_FILE_TYPE_NINTENDO_WBFS,
    (total_size >= 512 && header_size >= 4U && header[0] == 0x57U && header[1] == 0x42U && header[2] == 0x46U && header[3] == 0x53U))
XX_FORMAT_READER_PROBE(godot_ctex, XX_FILE_TYPE_GODOT_CTEX,
    (total_size >= 52 && header_size >= 4U && header[0] == 0x47U && header[1] == 0x53U && header[2] == 0x54U && header[3] == 0x32U))
XX_FORMAT_READER_PROBE(unity_serialized, XX_FILE_TYPE_UNITY_SERIALIZED,
    (total_size >= 48 && header_size >= 16U && header[0] == 0x00U && header[1] == 0x00U && header[2] == 0x00U && header[3] == 0x00U && header[4] == 0x00U && header[5] == 0x00U && header[6] == 0x00U && header[7] == 0x00U && header[8] == 0x00U && header[9] == 0x00U && header[10] == 0x00U && header[11] == 0x16U && header[12] == 0x00U && header[13] == 0x00U && header[14] == 0x00U && header[15] == 0x00U))
XX_FORMAT_READER_PROBE(idtech_mdl, XX_FILE_TYPE_IDTECH_MDL,
    (total_size >= 84 && header_size >= 4U && header[0] == 0x49U && header[1] == 0x44U && header[2] == 0x50U && header[3] == 0x4fU))
XX_FORMAT_READER_PROBE(valve_studio_mdl, XX_FILE_TYPE_VALVE_STUDIO_MDL,
    (total_size >= 244 && header_size >= 4U && header[0] == 0x49U && header[1] == 0x44U && header[2] == 0x53U && header[3] == 0x54U))
XX_FORMAT_READER_PROBE(blitz3d_b3d, XX_FILE_TYPE_BLITZ3D_B3D,
    (total_size >= 12 && header_size >= 4U && header[0] == 0x42U && header[1] == 0x42U && header[2] == 0x33U && header[3] == 0x44U))
XX_FORMAT_READER_PROBE(milkshape_ms3d, XX_FILE_TYPE_MILKSHAPE_MS3D,
    (total_size >= 16 && header_size >= 10U && header[0] == 0x4dU && header[1] == 0x53U && header[2] == 0x33U && header[3] == 0x44U && header[4] == 0x30U && header[5] == 0x30U && header[6] == 0x30U && header[7] == 0x30U && header[8] == 0x30U && header[9] == 0x30U))
XX_FORMAT_READER_PROBE(nintendo_bch, XX_FILE_TYPE_NINTENDO_BCH,
    (total_size >= 68 && header_size >= 4U && header[0] == 0x42U && header[1] == 0x43U && header[2] == 0x48U && header[3] == 0x00U))
XX_FORMAT_READER_PROBE(nintendo_cgfx, XX_FILE_TYPE_NINTENDO_CGFX,
    (total_size >= 156 && header_size >= 4U && header[0] == 0x43U && header[1] == 0x47U && header[2] == 0x46U && header[3] == 0x58U))
XX_FORMAT_READER_PROBE(nintendo_byaml, XX_FILE_TYPE_NINTENDO_BYAML,
    (total_size >= 16 && header_size >= 4 && ((header[0] == 0x59 && header[1] == 0x42 && header[2] == 2 && header[3] == 0) || (header[0] == 0x42 && header[1] == 0x59 && header[2] == 0 && header[3] == 2))))
XX_FORMAT_READER_PROBE(relic_chunky, XX_FILE_TYPE_RELIC_CHUNKY,
    (total_size >= 36 && header_size >= 16U && header[0] == 0x52U && header[1] == 0x65U && header[2] == 0x6cU && header[3] == 0x69U && header[4] == 0x63U && header[5] == 0x20U && header[6] == 0x43U && header[7] == 0x68U && header[8] == 0x75U && header[9] == 0x6eU && header[10] == 0x6bU && header[11] == 0x79U && header[12] == 0x0dU && header[13] == 0x0aU && header[14] == 0x1aU && header[15] == 0x00U))
XX_FORMAT_READER_PROBE(ogre_mesh, XX_FILE_TYPE_OGRE_MESH,
    (total_size >= 33 && header_size >= 26U && header[0] == 0x00U && header[1] == 0x10U && header[2] == 0x5bU && header[3] == 0x4dU && header[4] == 0x65U && header[5] == 0x73U && header[6] == 0x68U && header[7] == 0x53U && header[8] == 0x65U && header[9] == 0x72U && header[10] == 0x69U && header[11] == 0x61U && header[12] == 0x6cU && header[13] == 0x69U && header[14] == 0x7aU && header[15] == 0x65U && header[16] == 0x72U && header[17] == 0x5fU && header[18] == 0x76U && header[19] == 0x31U && header[20] == 0x2eU && header[21] == 0x31U && header[22] == 0x30U && header[23] == 0x30U && header[24] == 0x5dU && header[25] == 0x0aU))
XX_FORMAT_READER_PROBE(adobe_ase, XX_FILE_TYPE_ADOBE_ASE,
    (header_size>=12 && !xx_rt_memcmp(header,"ASEF\0\1\0\0",8)))
XX_FORMAT_READER_PROBE(adobe_aco, XX_FILE_TYPE_ADOBE_ACO,
    (header_size>=14 && header[0]==0 && header[1]==1 && (header[2] || header[3]) && header[2]<=16 && header[4]==0 && (header[5]<=2 || header[5]==7 || header[5]==8)))
XX_FORMAT_READER_PROBE(gimp_gbr, XX_FILE_TYPE_GIMP_GBR,
    (header_size>=28 && header[4]==0 && header[5]==0 && header[6]==0 && header[7]==2 && !xx_rt_memcmp(header+20,"GIMP",4)))
XX_FORMAT_READER_PROBE(gimp_gih, XX_FILE_TYPE_GIMP_GIH,
    (xx_gimp_gih_probe_header(header,header_size)))
XX_FORMAT_READER_PROBE(gimp_pat, XX_FILE_TYPE_GIMP_PAT,
    (header_size>=24 && header[4]==0 && header[5]==0 && header[6]==0 && header[7]==1 && !xx_rt_memcmp(header+20,"GPAT",4)))
XX_FORMAT_READER_PROBE(jbig2, XX_FILE_TYPE_JBIG2,
    (header_size>=9 && !xx_rt_memcmp(header,"\x97JB2\r\n\x1A\n",8)))
XX_FORMAT_READER_PROBE(djvu, XX_FILE_TYPE_DJVU,
    (header_size>=16 && !xx_rt_memcmp(header,"AT&TFORM",8) && !xx_rt_memcmp(header+12,"DJVU",4)))
XX_FORMAT_READER_PROBE(emf, XX_FILE_TYPE_EMF,
    (header_size>=88 && header[0]==1 && header[1]==0 && header[2]==0 && header[3]==0 && !xx_rt_memcmp(header+40," EMF",4)))
XX_FORMAT_READER_PROBE(wmf, XX_FILE_TYPE_WMF,
    (header_size>=40 && header[0]==0xD7 && header[1]==0xCD && header[2]==0xC6 && header[3]==0x9A))
XX_FORMAT_READER_PROBE(xfig, XX_FILE_TYPE_XFIG,
    (header_size>=9 && !xx_rt_memcmp(header,"#FIG 3.2",8) && (header[8]==10 || header[8]==13)))
XX_FORMAT_READER_PROBE(nifti1, XX_FILE_TYPE_NIFTI1,
    (((header_size >= 348 && header[344] == 110 && header[345] == 43 && header[346] == 49 && header[347] == 0))))
XX_FORMAT_READER_PROBE(nrrd, XX_FILE_TYPE_NRRD,
    (((header_size >= 7 && header[0] == 78 && header[1] == 82 && header[2] == 82 && header[3] == 68 && header[4] == 48 && header[5] == 48 && header[6] == 48))))
XX_FORMAT_READER_PROBE(mrc, XX_FILE_TYPE_MRC,
    (((header_size >= 212 && header[208] == 77 && header[209] == 65 && header[210] == 80 && header[211] == 32))))
XX_FORMAT_READER_PROBE(metaimage, XX_FILE_TYPE_METAIMAGE,
    (((header_size >= 10 && header[0] == 79 && header[1] == 98 && header[2] == 106 && header[3] == 101 && header[4] == 99 && header[5] == 116 && header[6] == 84 && header[7] == 121 && header[8] == 112 && header[9] == 101))))
XX_FORMAT_READER_PROBE(vtk_legacy, XX_FILE_TYPE_VTK_LEGACY,
    (((header_size >= 23 && header[0] == 35 && header[1] == 32 && header[2] == 118 && header[3] == 116 && header[4] == 107 && header[5] == 32 && header[6] == 68 && header[7] == 97 && header[8] == 116 && header[9] == 97 && header[10] == 70 && header[11] == 105 && header[12] == 108 && header[13] == 101 && header[14] == 32 && header[15] == 86 && header[16] == 101 && header[17] == 114 && header[18] == 115 && header[19] == 105 && header[20] == 111 && header[21] == 110 && header[22] == 32))))
XX_FORMAT_READER_PROBE(gipl, XX_FILE_TYPE_GIPL,
    (((header_size >= 256 && header[252] == 239 && header[253] == 255 && header[254] == 233 && header[255] == 176) || (header_size >= 256 && header[252] == 176 && header[253] == 233 && header[254] == 255 && header[255] == 239) || (header_size >= 256 && header[252] == 42 && header[253] == 227 && header[254] == 137 && header[255] == 184) || (header_size >= 256 && header[252] == 184 && header[253] == 137 && header[254] == 227 && header[255] == 42))))
XX_FORMAT_READER_PROBE(edf, XX_FILE_TYPE_EDF,
    (((header_size >= 8 && header[0] == 48 && header[1] == 32 && header[2] == 32 && header[3] == 32 && header[4] == 32 && header[5] == 32 && header[6] == 32 && header[7] == 32))))
XX_FORMAT_READER_PROBE(fcs, XX_FILE_TYPE_FCS,
    (((header_size >= 6 && header[0] == 70 && header[1] == 67 && header[2] == 83 && header[3] == 51 && header[4] == 46 && header[5] == 48) || (header_size >= 6 && header[0] == 70 && header[1] == 67 && header[2] == 83 && header[3] == 51 && header[4] == 46 && header[5] == 49))))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(sfx_imp, XX_FILE_TYPE_SFX_IMP,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_red, XX_FILE_TYPE_SFX_RED,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_ha, XX_FILE_TYPE_SFX_HA,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_lzx, XX_FILE_TYPE_SFX_LZX,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_sqx, XX_FILE_TYPE_SFX_SQX,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_ain, XX_FILE_TYPE_SFX_AIN,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_hap, XX_FILE_TYPE_SFX_HAP,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_zoo, XX_FILE_TYPE_SFX_ZOO,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_cazip, XX_FILE_TYPE_SFX_CAZIP,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_tgcf, XX_FILE_TYPE_SFX_TGCF,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_starkit, XX_FILE_TYPE_SFX_STARKIT,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_alz, XX_FILE_TYPE_SFX_ALZ,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_chm, XX_FILE_TYPE_SFX_CHM,
    (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(egg, XX_FILE_TYPE_EGG,
    (header_size >= 4 && (xx_rt_memcmp(header+0,"\x45\x47\x47\x41",4) == 0)))
XX_FORMAT_READER_PROBE(nufx, XX_FILE_TYPE_NUFX,
    (header_size >= 6 && (xx_rt_memcmp(header+0,"\x4e\xf5\x46\xe9\x6c\xe5",6) == 0)))
XX_FORMAT_READER_PROBE(nintendo_dol, XX_FILE_TYPE_NINTENDO_DOL,
    (total_size >= 256 && header_size >= 4U && header[0] == 0x00U && header[1] == 0x00U && header[2] == 0x01U && header[3] == 0x00U))
XX_FORMAT_READER_PROBE(nintendo_j3d_bmd, XX_FILE_TYPE_NINTENDO_J3D_BMD,
    (total_size >= 32 && header_size >= 8U && header[0] == 0x4aU && header[1] == 0x33U && header[2] == 0x44U && header[3] == 0x32U && header[4] == 0x62U && header[5] == 0x6dU && header[6] == 0x64U && header[7] == 0x33U))
XX_FORMAT_READER_PROBE(nintendo_j3d_btk, XX_FILE_TYPE_NINTENDO_J3D_BTK,
    (total_size >= 32 && header_size >= 8U && header[0] == 0x4aU && header[1] == 0x33U && header[2] == 0x44U && header[3] == 0x31U && header[4] == 0x62U && header[5] == 0x74U && header[6] == 0x6bU && header[7] == 0x31U))
XX_FORMAT_READER_PROBE(nintendo_brstm, XX_FILE_TYPE_NINTENDO_BRSTM,
    (total_size >= 64 && header_size >= 4U && header[0] == 0x52U && header[1] == 0x53U && header[2] == 0x54U && header[3] == 0x4dU))
XX_FORMAT_READER_PROBE(nintendo_brwav, XX_FILE_TYPE_NINTENDO_BRWAV,
    (total_size >= 32 && header_size >= 4U && header[0] == 0x52U && header[1] == 0x57U && header[2] == 0x41U && header[3] == 0x56U))
XX_FORMAT_READER_PROBE(nintendo_brlyt, XX_FILE_TYPE_NINTENDO_BRLYT,
    (total_size >= 16 && header_size >= 4U && header[0] == 0x52U && header[1] == 0x4cU && header[2] == 0x59U && header[3] == 0x54U))
XX_FORMAT_READER_PROBE(nintendo_brlan, XX_FILE_TYPE_NINTENDO_BRLAN,
    (total_size >= 16 && header_size >= 4U && header[0] == 0x52U && header[1] == 0x4cU && header[2] == 0x41U && header[3] == 0x4eU))
XX_FORMAT_READER_PROBE(nintendo_bfsha, XX_FILE_TYPE_NINTENDO_BFSHA,
    (total_size >= 56 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x53U && header[2] == 0x48U && header[3] == 0x41U))
XX_FORMAT_READER_PROBE(cri_usm, XX_FILE_TYPE_CRI_USM,
    (total_size >= 32 && header_size >= 4U && header[0] == 0x43U && header[1] == 0x52U && header[2] == 0x49U && header[3] == 0x44U))
XX_FORMAT_READER_PROBE(cri_utf, XX_FILE_TYPE_CRI_UTF,
    (total_size >= 32 && header_size >= 4U && header[0] == 0x40U && header[1] == 0x55U && header[2] == 0x54U && header[3] == 0x46U))
XX_FORMAT_READER_PROBE(idtech_iqm, XX_FILE_TYPE_IDTECH_IQM,
    (total_size >= 124 && header_size >= 16U && header[0] == 0x49U && header[1] == 0x4eU && header[2] == 0x54U && header[3] == 0x45U && header[4] == 0x52U && header[5] == 0x51U && header[6] == 0x55U && header[7] == 0x41U && header[8] == 0x4bU && header[9] == 0x45U && header[10] == 0x4dU && header[11] == 0x4fU && header[12] == 0x44U && header[13] == 0x45U && header[14] == 0x4cU && header[15] == 0x00U))
XX_FORMAT_READER_PROBE(unreal_psk, XX_FILE_TYPE_UNREAL_PSK,
    (total_size >= 32 && header_size >= 8U && header[0] == 0x41U && header[1] == 0x43U && header[2] == 0x54U && header[3] == 0x52U && header[4] == 0x48U && header[5] == 0x45U && header[6] == 0x41U && header[7] == 0x44U))
XX_FORMAT_READER_PROBE(unreal_psa, XX_FILE_TYPE_UNREAL_PSA,
    (total_size >= 32 && header_size >= 8U && header[0] == 0x41U && header[1] == 0x4eU && header[2] == 0x49U && header[3] == 0x4dU && header[4] == 0x48U && header[5] == 0x45U && header[6] == 0x41U && header[7] == 0x44U))
XX_FORMAT_READER_PROBE(torque_dts, XX_FILE_TYPE_TORQUE_DTS,
    (total_size >= 16 && header_size >= 2U && header[0] == 0x18U && header[1] == 0x00U))
XX_FORMAT_READER_PROBE(magicavoxel_vox, XX_FILE_TYPE_MAGICAVOXEL_VOX,
    (total_size >= 20 && header_size >= 4U && header[0] == 0x56U && header[1] == 0x4fU && header[2] == 0x58U && header[3] == 0x20U))
XX_FORMAT_READER_PROBE(audio_au, XX_FILE_TYPE_AUDIO_AU,
    (header_size>=24 && (!xx_rt_memcmp(header,".snd",4) ||
                            !xx_rt_memcmp(header,"dns.",4) ||
                            !xx_rt_memcmp(header,"\0ds.",4) ||
                            !xx_rt_memcmp(header,".sd\0",4))))
XX_FORMAT_READER_PROBE(creative_voc, XX_FILE_TYPE_CREATIVE_VOC,
    (header_size>=26 && !xx_rt_memcmp(header,"Creative Voice File\x1a",20)))
XX_FORMAT_READER_PROBE(tracker_xm, XX_FILE_TYPE_TRACKER_XM,
    (header_size>=80 && !xx_rt_memcmp(header,"Extended Module: ",17) && header[37]==0x1a && header[58]==4 && header[59]==1))
XX_FORMAT_READER_PROBE(tracker_s3m, XX_FILE_TYPE_TRACKER_S3M,
    (header_size>=96 && header[28]==0x1a && header[29]==16 && !xx_rt_memcmp(header+44,"SCRM",4)))
XX_FORMAT_READER_PROBE(tracker_it, XX_FILE_TYPE_TRACKER_IT,
    (header_size>=192 && !xx_rt_memcmp(header,"IMPM",4)))
XX_FORMAT_READER_PROBE(tracker_mtm, XX_FILE_TYPE_TRACKER_MTM,
    (header_size>=66 && !xx_rt_memcmp(header,"MTM\x10",4)))
XX_FORMAT_READER_PROBE(tracker_stm, XX_FILE_TYPE_TRACKER_STM,
    (header_size>=48 && !xx_rt_memcmp(header+20,"!Scream!",8) && header[28]==0x1a && header[29]==2 && header[30]==2 && header[31]==21))
XX_FORMAT_READER_PROBE(tracker_669, XX_FILE_TYPE_TRACKER_669,
    (header_size>=497 && ((header[0]==105 && header[1]==102) || (header[0]==74 && header[1]==78)) && header[110]<=64 && header[111]>0 && header[111]<=128))
XX_FORMAT_READER_PROBE(tracker_ult, XX_FILE_TYPE_TRACKER_ULT,
    (header_size>=48 && !xx_rt_memcmp(header,"MAS_UTrack_V004",15)))
XX_FORMAT_READER_PROBE(tracker_okt, XX_FILE_TYPE_TRACKER_OKT,
    (header_size>=16 && !xx_rt_memcmp(header,"OKTASONG",8)))
XX_FORMAT_READER_PROBE(nifti2, XX_FILE_TYPE_NIFTI2,
    (((header_size >= 12 && header[4] == 110 && header[5] == 43 && header[6] == 50 && header[7] == 0 && header[8] == 13 && header[9] == 10 && header[10] == 26 && header[11] == 10))))
XX_FORMAT_READER_PROBE(lidar_las, XX_FILE_TYPE_LIDAR_LAS,
    (((header_size >= 4 && header[0] == 76 && header[1] == 65 && header[2] == 83 && header[3] == 70))))
XX_FORMAT_READER_PROBE(esri_shp, XX_FILE_TYPE_ESRI_SHP,
    (((header_size >= 4 && header[0] == 0 && header[1] == 0 && header[2] == 39 && header[3] == 10))))
XX_FORMAT_READER_PROBE(polygon_ply, XX_FILE_TYPE_POLYGON_PLY,
    (((header_size >= 4 && header[0] == 112 && header[1] == 108 && header[2] == 121 && header[3] == 10) || (header_size >= 5 && header[0] == 112 && header[1] == 108 && header[2] == 121 && header[3] == 13 && header[4] == 10))))
XX_FORMAT_READER_PROBE(pointcloud_pcd, XX_FILE_TYPE_POINTCLOUD_PCD,
    (((header_size >= 6 && header[0] == 35 && header[1] == 32 && header[2] == 46 && header[3] == 80 && header[4] == 67 && header[5] == 68) || (header_size >= 8 && header[0] == 86 && header[1] == 69 && header[2] == 82 && header[3] == 83 && header[4] == 73 && header[5] == 79 && header[6] == 78 && header[7] == 32))))
XX_FORMAT_READER_PROBE(biomedical_bdf, XX_FILE_TYPE_BIOMEDICAL_BDF,
    (((header_size >= 8 && header[0] == 255 && header[1] == 66 && header[2] == 73 && header[3] == 79 && header[4] == 83 && header[5] == 69 && header[6] == 77 && header[7] == 73))))
XX_FORMAT_READER_PROBE(erlang_beam, XX_FILE_TYPE_ERLANG_BEAM,
    (((header_size >= 4 && header[0] == 70 && header[1] == 79 && header[2] == 82 && header[3] == 49))))
XX_FORMAT_READER_PROBE(java_jmod, XX_FILE_TYPE_JAVA_JMOD,
    (((header_size >= 4 && header[0] == 74 && header[1] == 77 && header[2] == 1 && header[3] == 0))))

XX_FORMAT_PROBE_READ(1084U)
XX_FORMAT_READER_PROBE(tracker_liquid, XX_FILE_TYPE_TRACKER_LIQUID,
    (header_size >= 14 && xx_rt_memcmp(header,"Liquid Module:",14)==0))
XX_FORMAT_READER_PROBE(tracker_dmf, XX_FILE_TYPE_TRACKER_DMF,
    (header_size >= 4 && xx_rt_memcmp(header+0,"\x44\x44\x4d\x46",4)==0))
XX_FORMAT_READER_PROBE(tracker_ptm, XX_FILE_TYPE_TRACKER_PTM,
    (header_size >= 48 && xx_rt_memcmp(header+44,"\x50\x54\x4d\x46",4)==0))
XX_FORMAT_READER_PROBE(tracker_ams, XX_FILE_TYPE_TRACKER_AMS,
    (header_size >= 7 && xx_rt_memcmp(header+0,"\x45\x78\x74\x72\x65\x6d\x65",7)==0))
XX_FORMAT_READER_PROBE(tracker_digi, XX_FILE_TYPE_TRACKER_DIGI,
    (header_size >= 20 && xx_rt_memcmp(header+0,"\x44\x49\x47\x49\x20\x42\x6f\x6f\x73\x74\x65\x72\x20\x6d\x6f\x64\x75\x6c\x65\x00",20)==0))
XX_FORMAT_READER_PROBE(tracker_emod, XX_FILE_TYPE_TRACKER_EMOD,
    (header_size >= 12 && xx_rt_memcmp(header+0,"\x46\x4f\x52\x4d",4)==0 && xx_rt_memcmp(header+8,"\x45\x4d\x4f\x44",4)==0))
XX_FORMAT_READER_PROBE(tracker_mt2, XX_FILE_TYPE_TRACKER_MT2,
    (header_size >= 4 && xx_rt_memcmp(header+0,"\x4d\x54\x32\x30",4)==0))
XX_FORMAT_READER_PROBE(audio_dsf, XX_FILE_TYPE_AUDIO_DSF,
    (header_size >= 4 && xx_rt_memcmp(header+0,"\x44\x53\x44\x20",4)==0))
XX_FORMAT_READER_PROBE(audio_dff, XX_FILE_TYPE_AUDIO_DFF,
    (header_size >= 16 && xx_rt_memcmp(header+0,"\x46\x52\x4d\x38",4)==0 && xx_rt_memcmp(header+12,"\x44\x53\x44\x20",4)==0))
XX_FORMAT_READER_PROBE(audio_wave64, XX_FILE_TYPE_AUDIO_WAVE64,
    (header_size >= 16 && xx_rt_memcmp(header+0,"\x72\x69\x66\x66\x2e\x91\xcf\x11\xa5\xd6\x28\xdb\x04\xc1\x00\x00",16)==0))
XX_FORMAT_READER_PROBE(audio_adx, XX_FILE_TYPE_AUDIO_ADX,
    (header_size >= 2 && xx_rt_memcmp(header+0,"\x80\x00",2)==0))
XX_FORMAT_READER_PROBE(audio_ast, XX_FILE_TYPE_AUDIO_AST,
    (header_size >= 4 && xx_rt_memcmp(header+0,"\x53\x54\x52\x4d",4)==0))
XX_FORMAT_READER_PROBE(audio_hca, XX_FILE_TYPE_AUDIO_HCA,
    (header_size >= 4 && xx_rt_memcmp(header+0,"\x48\x43\x41\x00",4)==0))
XX_FORMAT_READER_PROBE(iff_8svx, XX_FILE_TYPE_IFF_8SVX,
    (header_size >= 12 && xx_rt_memcmp(header+0,"\x46\x4f\x52\x4d",4)==0 && xx_rt_memcmp(header+8,"\x38\x53\x56\x58",4)==0))
XX_FORMAT_READER_PROBE(audio_wavpack, XX_FILE_TYPE_AUDIO_WAVPACK,
    (header_size >= 4 && xx_rt_memcmp(header+0,"\x77\x76\x70\x6b",4)==0))
XX_FORMAT_READER_PROBE(blender_blend, XX_FILE_TYPE_BLENDER_BLEND,
    (total_size >= 12 && header_size >= 7U && header[0] == 0x42U && header[1] == 0x4cU && header[2] == 0x45U && header[3] == 0x4eU && header[4] == 0x44U && header[5] == 0x45U && header[6] == 0x52U))
XX_FORMAT_READER_PROBE(autodesk_fbx, XX_FILE_TYPE_AUTODESK_FBX,
    (total_size >= 27 && header_size >= 23U && header[0] == 0x4bU && header[1] == 0x61U && header[2] == 0x79U && header[3] == 0x64U && header[4] == 0x61U && header[5] == 0x72U && header[6] == 0x61U && header[7] == 0x20U && header[8] == 0x46U && header[9] == 0x42U && header[10] == 0x58U && header[11] == 0x20U && header[12] == 0x42U && header[13] == 0x69U && header[14] == 0x6eU && header[15] == 0x61U && header[16] == 0x72U && header[17] == 0x79U && header[18] == 0x20U && header[19] == 0x20U && header[20] == 0x00U && header[21] == 0x1aU && header[22] == 0x00U))
XX_FORMAT_READER_PROBE(autodesk_3ds, XX_FILE_TYPE_AUTODESK_3DS,
    (total_size >= 6 && header_size >= 2U && header[0] == 0x4dU && header[1] == 0x4dU))
XX_FORMAT_READER_PROBE(lightwave_lwo2, XX_FILE_TYPE_LIGHTWAVE_LWO2,
    (total_size >= 12 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x4fU && header[2] == 0x52U && header[3] == 0x4dU))
XX_FORMAT_READER_PROBE(sony_psp_pbp, XX_FILE_TYPE_SONY_PSP_PBP,
    (total_size >= 40 && header_size >= 4U && header[0] == 0x00U && header[1] == 0x50U && header[2] == 0x42U && header[3] == 0x50U))
XX_FORMAT_READER_PROBE(flash_video_flv, XX_FILE_TYPE_FLASH_VIDEO_FLV,
    (total_size >= 9 && header_size >= 3U && header[0] == 0x46U && header[1] == 0x4cU && header[2] == 0x56U))
XX_FORMAT_READER_PROBE(nintendo_n64_rom, XX_FILE_TYPE_NINTENDO_N64_ROM,
    (total_size >= 4096 && header_size >= 4U && header[0] == 0x80U && header[1] == 0x37U && header[2] == 0x12U && header[3] == 0x40U))
XX_FORMAT_READER_PROBE(nintendo_gb_rom, XX_FILE_TYPE_NINTENDO_GB_ROM,
    (total_size >= 336 && header_size >= 268U && header[260] == 0xceU && header[261] == 0xedU && header[262] == 0x66U && header[263] == 0x66U && header[264] == 0xccU && header[265] == 0x0dU && header[266] == 0x00U && header[267] == 0x0bU))
XX_FORMAT_READER_PROBE(nintendo_gba_rom, XX_FILE_TYPE_NINTENDO_GBA_ROM,
    (total_size >= 192 && header_size >= 12U && header[4] == 0x24U && header[5] == 0xffU && header[6] == 0xaeU && header[7] == 0x51U && header[8] == 0x69U && header[9] == 0x9aU && header[10] == 0xa2U && header[11] == 0x21U))
XX_FORMAT_READER_PROBE(sega_megadrive_rom, XX_FILE_TYPE_SEGA_MEGADRIVE_ROM,
    (total_size >= 512 && header_size >= 260U && header[256] == 0x53U && header[257] == 0x45U && header[258] == 0x47U && header[259] == 0x41U))
XX_FORMAT_READER_PROBE(spring_s3o, XX_FILE_TYPE_SPRING_S3O,
    (total_size >= 52 && header_size >= 12U && header[0] == 0x53U && header[1] == 0x70U && header[2] == 0x72U && header[3] == 0x69U && header[4] == 0x6eU && header[5] == 0x67U && header[6] == 0x20U && header[7] == 0x75U && header[8] == 0x6eU && header[9] == 0x69U && header[10] == 0x74U && header[11] == 0x00U))
XX_FORMAT_READER_PROBE(xna_xnb, XX_FILE_TYPE_XNA_XNB,
    (total_size >= 10 && header_size >= 3U && header[0] == 0x58U && header[1] == 0x4eU && header[2] == 0x42U))
XX_FORMAT_READER_PROBE(lua_bytecode51, XX_FILE_TYPE_LUA_BYTECODE51,
    (total_size >= 12 && header_size >= 5U && header[0] == 0x1bU && header[1] == 0x4cU && header[2] == 0x75U && header[3] == 0x61U && header[4] == 0x51U))
XX_FORMAT_READER_PROBE(quake_md5mesh, XX_FILE_TYPE_QUAKE_MD5MESH,
    (total_size >= 10 && header_size >= 10U && header[0] == 0x4dU && header[1] == 0x44U && header[2] == 0x35U && header[3] == 0x56U && header[4] == 0x65U && header[5] == 0x72U && header[6] == 0x73U && header[7] == 0x69U && header[8] == 0x6fU && header[9] == 0x6eU))
XX_FORMAT_READER_PROBE(tracker_mod, XX_FILE_TYPE_TRACKER_MOD,
    ((header_size>=1084 && header[1080]==0x4d && header[1081]==0x2e && header[1082]==0x4b && header[1083]==0x2e) || (header_size>=1084 && header[1080]==0x4d && header[1081]==0x21 && header[1082]==0x4b && header[1083]==0x21) || (header_size>=1084 && header[1080]==0x34 && header[1081]==0x43 && header[1082]==0x48 && header[1083]==0x4e) || (header_size>=1084 && header[1080]==0x46 && header[1081]==0x4c && header[1082]==0x54 && header[1083]==0x34)))
XX_FORMAT_READER_PROBE(tracker_far, XX_FILE_TYPE_TRACKER_FAR,
    ((header_size>=4 && header[0]==0x46 && header[1]==0x41 && header[2]==0x52 && header[3]==0xfe)))
XX_FORMAT_READER_PROBE(tracker_mdl, XX_FILE_TYPE_TRACKER_MDL,
    ((header_size>=4 && header[0]==0x44 && header[1]==0x4d && header[2]==0x44 && header[3]==0x4c)))
XX_FORMAT_READER_PROBE(tracker_gdm, XX_FILE_TYPE_TRACKER_GDM,
    ((header_size>=4 && header[0]==0x47 && header[1]==0x44 && header[2]==0x4d && header[3]==0xfe) && (header_size>=75 && header[71]==0x47 && header[72]==0x4d && header[73]==0x46 && header[74]==0x53)))
XX_FORMAT_READER_PROBE(tracker_dbm, XX_FILE_TYPE_TRACKER_DBM,
    ((header_size>=4 && header[0]==0x44 && header[1]==0x42 && header[2]==0x4d && header[3]==0x30)))
XX_FORMAT_READER_PROBE(tracker_med, XX_FILE_TYPE_TRACKER_MED,
    ((header_size>=4 && header[0]==0x4d && header[1]==0x4d && header[2]==0x44 && header[3]==0x30) || (header_size>=4 && header[0]==0x4d && header[1]==0x4d && header[2]==0x44 && header[3]==0x31)))
XX_FORMAT_READER_PROBE(tracker_imf, XX_FILE_TYPE_TRACKER_IMF,
    ((header_size>=64 && header[60]==0x49 && header[61]==0x4d && header[62]==0x31 && header[63]==0x30)))
XX_FORMAT_READER_PROBE(tracker_amf, XX_FILE_TYPE_TRACKER_AMF,
    ((header_size>=4 && header[0]==0x41 && header[1]==0x4d && header[2]==0x46 && header[3]==0x0e)))
XX_FORMAT_READER_PROBE(tracker_psm, XX_FILE_TYPE_TRACKER_PSM,
    ((header_size>=4 && header[0]==0x50 && header[1]==0x53 && header[2]==0x4d && header[3]==0x20)))
XX_FORMAT_READER_PROBE(steinberg_fxb, XX_FILE_TYPE_STEINBERG_FXB,
    ((header_size>=4 && header[0]==0x43 && header[1]==0x63 && header[2]==0x6e && header[3]==0x4b)))
XX_FORMAT_READER_PROBE(astronomy_ser, XX_FILE_TYPE_ASTRONOMY_SER,
    (((header_size >= 14 && header[0] == 76 && header[1] == 85 && header[2] == 67 && header[3] == 65 && header[4] == 77 && header[5] == 45 && header[6] == 82 && header[7] == 69 && header[8] == 67 && header[9] == 79 && header[10] == 82 && header[11] == 68 && header[12] == 69 && header[13] == 82))))
XX_FORMAT_READER_PROBE(photontiming_ptu, XX_FILE_TYPE_PHOTONTIMING_PTU,
    (((header_size >= 8 && header[0] == 80 && header[1] == 81 && header[2] == 84 && header[3] == 84 && header[4] == 84 && header[5] == 82 && header[6] == 0 && header[7] == 0))))
XX_FORMAT_READER_PROBE(photontiming_phu, XX_FILE_TYPE_PHOTONTIMING_PHU,
    (((header_size >= 8 && header[0] == 80 && header[1] == 81 && header[2] == 72 && header[3] == 73 && header[4] == 83 && header[5] == 84 && header[6] == 79 && header[7] == 0))))
XX_FORMAT_READER_PROBE(charmm_dcd, XX_FILE_TYPE_CHARMM_DCD,
    (((header_size >= 8 && header[0] == 84 && header[1] == 0 && header[2] == 0 && header[3] == 0 && header[4] == 67 && header[5] == 79 && header[6] == 82 && header[7] == 68) || (header_size >= 8 && header[0] == 0 && header[1] == 0 && header[2] == 0 && header[3] == 84 && header[4] == 67 && header[5] == 79 && header[6] == 82 && header[7] == 68))))
XX_FORMAT_READER_PROBE(gromacs_trr, XX_FILE_TYPE_GROMACS_TRR,
    (((header_size >= 4 && header[0] == 0 && header[1] == 0 && header[2] == 7 && header[3] == 201))))
XX_FORMAT_READER_PROBE(microscopy_ics, XX_FILE_TYPE_MICROSCOPY_ICS,
    (((header_size >= 17 && header[0] == 9 && header[1] == 10 && header[2] == 105 && header[3] == 99 && header[4] == 115 && header[5] == 95 && header[6] == 118 && header[7] == 101 && header[8] == 114 && header[9] == 115 && header[10] == 105 && header[11] == 111 && header[12] == 110 && header[13] == 9 && header[14] == 50 && header[15] == 46 && header[16] == 48))))
XX_FORMAT_READER_PROBE(tecplot_plt, XX_FILE_TYPE_TECPLOT_PLT,
    (((header_size >= 8 && header[0] == 35 && header[1] == 33 && header[2] == 84 && header[3] == 68 && header[4] == 86 && header[5] == 49 && header[6] == 49 && header[7] == 50))))
XX_FORMAT_READER_PROBE(fujifilm_raf, XX_FILE_TYPE_FUJIFILM_RAF,
    (((header_size >= 16 && header[0] == 70 && header[1] == 85 && header[2] == 74 && header[3] == 73 && header[4] == 70 && header[5] == 73 && header[6] == 76 && header[7] == 77 && header[8] == 67 && header[9] == 67 && header[10] == 68 && header[11] == 45 && header[12] == 82 && header[13] == 65 && header[14] == 87 && header[15] == 32))))
XX_FORMAT_READER_PROBE(sigma_x3f, XX_FILE_TYPE_SIGMA_X3F,
    (((header_size >= 4 && header[0] == 70 && header[1] == 79 && header[2] == 86 && header[3] == 98))))
XX_FORMAT_READER_PROBE(minolta_mrw, XX_FILE_TYPE_MINOLTA_MRW,
    (((header_size >= 4 && header[0] == 0 && header[1] == 77 && header[2] == 82 && header[3] == 77))))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(vice_x64, XX_FILE_TYPE_VICE_X64,
    (header_size >= 4 && (!xx_rt_memcmp(header, "\x43\x15\x41\x64", 4))))
XX_FORMAT_READER_PROBE(vice_snapshot, XX_FILE_TYPE_VICE_SNAPSHOT,
    (header_size >= 19 && (!xx_rt_memcmp(header, "\x56\x49\x43\x45\x20\x53\x6e\x61\x70\x73\x68\x6f\x74\x20\x46\x69\x6c\x65\x1a", 19))))
XX_FORMAT_READER_PROBE(commodore_g64, XX_FILE_TYPE_COMMODORE_G64,
    (header_size >= 8 && (!xx_rt_memcmp(header, "\x47\x43\x52\x2d\x31\x35\x34\x31", 8))))
XX_FORMAT_READER_PROBE(commodore_p64, XX_FILE_TYPE_COMMODORE_P64,
    (header_size >= 8 && (!xx_rt_memcmp(header, "\x50\x36\x34\x2d\x31\x35\x34\x31", 8))))
XX_FORMAT_READER_PROBE(commodore_tap, XX_FILE_TYPE_COMMODORE_TAP,
    (header_size >= 12 && (!xx_rt_memcmp(header, "\x43\x36\x34\x2d\x54\x41\x50\x45\x2d\x52\x41\x57", 12))))
XX_FORMAT_READER_PROBE(zx_spectrum_tzx, XX_FILE_TYPE_ZX_SPECTRUM_TZX,
    (header_size >= 8 && (!xx_rt_memcmp(header, "\x5a\x58\x54\x61\x70\x65\x21\x1a", 8))))
XX_FORMAT_READER_PROBE(zx_spectrum_szx, XX_FILE_TYPE_ZX_SPECTRUM_SZX,
    (header_size >= 4 && (!xx_rt_memcmp(header, "\x5a\x58\x53\x54", 4))))
XX_FORMAT_READER_PROBE(amstrad_cpc_dsk, XX_FILE_TYPE_AMSTRAD_CPC_DSK,
    (header_size >= 34 && (!xx_rt_memcmp(header, "\x4d\x56\x20\x2d\x20\x43\x50\x43\x45\x4d\x55\x20\x44\x69\x73\x6b\x2d\x46\x69\x6c\x65\x0d\x0a\x44\x69\x73\x6b\x2d\x49\x6e\x66\x6f\x0d\x0a", 34) || !xx_rt_memcmp(header, "\x45\x58\x54\x45\x4e\x44\x45\x44\x20\x43\x50\x43\x20\x44\x53\x4b\x20\x46\x69\x6c\x65\x0d\x0a\x44\x69\x73\x6b\x2d\x49\x6e\x66\x6f\x0d\x0a", 34))))
XX_FORMAT_READER_PROBE(atari_st_msa, XX_FILE_TYPE_ATARI_ST_MSA,
    (header_size >= 2 && (!xx_rt_memcmp(header, "\x0e\x0f", 2))))
XX_FORMAT_READER_PROBE(supercard_scp, XX_FILE_TYPE_SUPERCARD_SCP,
    (header_size >= 3 && (!xx_rt_memcmp(header, "\x53\x43\x50", 3))))
XX_FORMAT_READER_PROBE(apple_woz, XX_FILE_TYPE_APPLE_WOZ,
    (header_size >= 8 && (!xx_rt_memcmp(header, "\x57\x4f\x5a\x31\xff\x0a\x0d\x0a", 8) || !xx_rt_memcmp(header, "\x57\x4f\x5a\x32\xff\x0a\x0d\x0a", 8))))
XX_FORMAT_READER_PROBE(nintendo_nsf, XX_FILE_TYPE_NINTENDO_NSF,
    (header_size >= 5 && (!xx_rt_memcmp(header, "\x4e\x45\x53\x4d\x1a", 5))))
XX_FORMAT_READER_PROBE(vgm_log, XX_FILE_TYPE_VGM_LOG,
    (header_size >= 4 && (!xx_rt_memcmp(header, "\x56\x67\x6d\x20", 4))))
XX_FORMAT_READER_PROBE(psid_sid, XX_FILE_TYPE_PSID_SID,
    (header_size >= 4 && (!xx_rt_memcmp(header, "\x50\x53\x49\x44", 4) || !xx_rt_memcmp(header, "\x52\x53\x49\x44", 4))))
XX_FORMAT_READER_PROBE(hes_sound, XX_FILE_TYPE_HES_SOUND,
    (header_size >= 4 && (!xx_rt_memcmp(header, "\x48\x45\x53\x4d", 4))))
XX_FORMAT_READER_PROBE(audio_dolby_ac3, XX_FILE_TYPE_AUDIO_DOLBY_AC3,
    (total_size >= 128 && header_size >= 2U && ((header[0] == 0x0bU && header[1] == 0x77U))))
XX_FORMAT_READER_PROBE(audio_mpeg_mp3, XX_FILE_TYPE_AUDIO_MPEG_MP3,
    (total_size >= 21 && header_size >= 3U && ((header[0] == 0x49U && header[1] == 0x44U && header[2] == 0x33U) || (header[0] == 0xffU && header[1] == 0xfbU))))
XX_FORMAT_READER_PROBE(audio_aac_adts, XX_FILE_TYPE_AUDIO_AAC_ADTS,
    (total_size >= 8 && header_size >= 2U && ((header[0] == 0xffU && header[1] == 0xf1U))))
XX_FORMAT_READER_PROBE(audio_monkeys_ape, XX_FILE_TYPE_AUDIO_MONKEYS_APE,
    (total_size >= 84 && header_size >= 4U && ((header[0] == 0x4dU && header[1] == 0x41U && header[2] == 0x43U && header[3] == 0x20U))))
XX_FORMAT_READER_PROBE(mpeg_transport_stream, XX_FILE_TYPE_MPEG_TRANSPORT_STREAM,
    (total_size >= 564 && header_size >= 1U && ((header[0] == 0x47U))))
XX_FORMAT_READER_PROBE(mpeg_program_stream, XX_FILE_TYPE_MPEG_PROGRAM_STREAM,
    (total_size >= 18 && header_size >= 4U && ((header[0] == 0x00U && header[1] == 0x00U && header[2] == 0x01U && header[3] == 0xbaU))))
XX_FORMAT_READER_PROBE(realmedia_rm, XX_FILE_TYPE_REALMEDIA_RM,
    (total_size >= 50 && header_size >= 4U && ((header[0] == 0x2eU && header[1] == 0x52U && header[2] == 0x4dU && header[3] == 0x46U))))
XX_FORMAT_READER_PROBE(idtech_roq, XX_FILE_TYPE_IDTECH_ROQ,
    (total_size >= 24 && header_size >= 6U && ((header[0] == 0x84U && header[1] == 0x10U && header[2] == 0xffU && header[3] == 0xffU && header[4] == 0xffU && header[5] == 0xffU))))
XX_FORMAT_READER_PROBE(rad_bink, XX_FILE_TYPE_RAD_BINK,
    (total_size >= 48 && header_size >= 3U && ((header[0] == 0x42U && header[1] == 0x49U && header[2] == 0x4bU))))
XX_FORMAT_READER_PROBE(rad_smacker, XX_FILE_TYPE_RAD_SMACKER,
    (total_size >= 109 && header_size >= 4U && ((header[0] == 0x53U && header[1] == 0x4dU && header[2] == 0x4bU && header[3] == 0x32U) || (header[0] == 0x53U && header[1] == 0x4dU && header[2] == 0x4bU && header[3] == 0x34U))))
XX_FORMAT_READER_PROBE(interplay_mve, XX_FILE_TYPE_INTERPLAY_MVE,
    (total_size >= 30 && header_size >= 19U && ((header[0] == 0x49U && header[1] == 0x6eU && header[2] == 0x74U && header[3] == 0x65U && header[4] == 0x72U && header[5] == 0x70U && header[6] == 0x6cU && header[7] == 0x61U && header[8] == 0x79U && header[9] == 0x20U && header[10] == 0x4dU && header[11] == 0x56U && header[12] == 0x45U && header[13] == 0x20U && header[14] == 0x46U && header[15] == 0x69U && header[16] == 0x6cU && header[17] == 0x65U && header[18] == 0x1aU))))
XX_FORMAT_READER_PROBE(westwood_vqa, XX_FILE_TYPE_WESTWOOD_VQA,
    (total_size >= 62 && header_size >= 12U && ((header[8] == 0x57U && header[9] == 0x56U && header[10] == 0x51U && header[11] == 0x41U))))
XX_FORMAT_READER_PROBE(autodesk_flic, XX_FILE_TYPE_AUTODESK_FLIC,
    (total_size >= 150 && header_size >= 6U && ((header[4] == 0x11U && header[5] == 0xafU) || (header[4] == 0x12U && header[5] == 0xafU))))
XX_FORMAT_READER_PROBE(idtech_md5anim, XX_FILE_TYPE_IDTECH_MD5ANIM,
    (total_size >= 10 && header_size >= 10U && ((header[0] == 0x4dU && header[1] == 0x44U && header[2] == 0x35U && header[3] == 0x56U && header[4] == 0x65U && header[5] == 0x72U && header[6] == 0x73U && header[7] == 0x69U && header[8] == 0x6fU && header[9] == 0x6eU))))
XX_FORMAT_READER_PROBE(garmin_fit, XX_FILE_TYPE_GARMIN_FIT,
    (((header_size >= 12 && header[8] == 46 && header[9] == 70 && header[10] == 73 && header[11] == 84))))
XX_FORMAT_READER_PROBE(rosbag1, XX_FILE_TYPE_ROSBAG1,
    (((header_size >= 13 && header[0] == 35 && header[1] == 82 && header[2] == 79 && header[3] == 83 && header[4] == 66 && header[5] == 65 && header[6] == 71 && header[7] == 32 && header[8] == 86 && header[9] == 50 && header[10] == 46 && header[11] == 48 && header[12] == 10))))
XX_FORMAT_READER_PROBE(mcap, XX_FILE_TYPE_MCAP,
    (((header_size >= 8 && header[0] == 137 && header[1] == 77 && header[2] == 67 && header[3] == 65 && header[4] == 80 && header[5] == 48 && header[6] == 13 && header[7] == 10))))
XX_FORMAT_READER_PROBE(seismic_seg2, XX_FILE_TYPE_SEISMIC_SEG2,
    (((header_size >= 2 && header[0] == 85 && header[1] == 58) || (header_size >= 2 && header[0] == 58 && header[1] == 85))))
XX_FORMAT_READER_PROBE(ucsc_twobit, XX_FILE_TYPE_UCSC_TWOBIT,
    (((header_size >= 4 && header[0] == 26 && header[1] == 65 && header[2] == 39 && header[3] == 67) || (header_size >= 4 && header[0] == 67 && header[1] == 39 && header[2] == 65 && header[3] == 26))))
XX_FORMAT_READER_PROBE(genomics_bgen, XX_FILE_TYPE_GENOMICS_BGEN,
    (((header_size >= 20 && header[16] == 98 && header[17] == 103 && header[18] == 101 && header[19] == 110))))
XX_FORMAT_READER_PROBE(openephys_continuous, XX_FILE_TYPE_OPENEPHYS_CONTINUOUS,
    (((header_size >= 7 && header[0] == 104 && header[1] == 101 && header[2] == 97 && header[3] == 100 && header[4] == 101 && header[5] == 114 && header[6] == 46))))
XX_FORMAT_READER_PROBE(igor_ibw, XX_FILE_TYPE_IGOR_IBW,
    (((header_size >= 2 && header[0] == 5 && header[1] == 0) || (header_size >= 2 && header[0] == 0 && header[1] == 5))))
XX_FORMAT_READER_PROBE(princeton_spe, XX_FILE_TYPE_PRINCETON_SPE,
    (((header_size >= 4100 && header[4098] == 85 && header[4099] == 85))))
XX_FORMAT_READER_PROBE(wmo_grib, XX_FILE_TYPE_WMO_GRIB,
    (((header_size >= 4 && header[0] == 71 && header[1] == 82 && header[2] == 73 && header[3] == 66))))
XX_FORMAT_READER_PROBE(wmo_bufr, XX_FILE_TYPE_WMO_BUFR,
    (((header_size >= 4 && header[0] == 66 && header[1] == 85 && header[2] == 70 && header[3] == 82))))
XX_FORMAT_READER_PROBE(autocad_dxf, XX_FILE_TYPE_AUTOCAD_DXF,
    (((header_size >= 22 && header[0] == 65 && header[1] == 117 && header[2] == 116 && header[3] == 111 && header[4] == 67 && header[5] == 65 && header[6] == 68 && header[7] == 32 && header[8] == 66 && header[9] == 105 && header[10] == 110 && header[11] == 97 && header[12] == 114 && header[13] == 121 && header[14] == 32 && header[15] == 68 && header[16] == 88 && header[17] == 70 && header[18] == 13 && header[19] == 10 && header[20] == 26 && header[21] == 0))))
XX_FORMAT_READER_PROBE(blackrock_nsx, XX_FILE_TYPE_BLACKROCK_NSX,
    (((header_size >= 8 && header[0] == 78 && header[1] == 69 && header[2] == 85 && header[3] == 82 && header[4] == 65 && header[5] == 76 && header[6] == 67 && header[7] == 68))))
XX_FORMAT_READER_PROBE(blackrock_nev, XX_FILE_TYPE_BLACKROCK_NEV,
    (((header_size >= 8 && header[0] == 78 && header[1] == 69 && header[2] == 85 && header[3] == 82 && header[4] == 65 && header[5] == 76 && header[6] == 69 && header[7] == 86))))
XX_FORMAT_READER_PROBE(lecroy_trc, XX_FILE_TYPE_LECROY_TRC,
    (((header_size >= 8 && header[0] == 87 && header[1] == 65 && header[2] == 86 && header[3] == 69 && header[4] == 68 && header[5] == 69 && header[6] == 83 && header[7] == 67))))
XX_FORMAT_READER_PROBE(tektronix_isf, XX_FILE_TYPE_TEKTRONIX_ISF,
    (((header_size >= 8 && header[0] == 58 && header[1] == 87 && header[2] == 70 && header[3] == 77 && header[4] == 80 && header[5] == 82 && header[6] == 69 && header[7] == 58))))
XX_FORMAT_READER_PROBE(ircam_sdif, XX_FILE_TYPE_IRCAM_SDIF,
    (((header_size >= 4 && header[0] == 83 && header[1] == 68 && header[2] == 73 && header[3] == 70))))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(microsoft_msf, XX_FILE_TYPE_MICROSOFT_MSF,
    (header_size >= 32 && !xx_rt_memcmp(header, "\x4d\x69\x63\x72\x6f\x73\x6f\x66\x74\x20\x43\x2f\x43\x2b\x2b\x20\x4d\x53\x46\x20\x37\x2e\x30\x30\x0d\x0a\x1a\x44\x53\x00\x00\x00", 32)))
XX_FORMAT_READER_PROBE(windows_registry_hive, XX_FILE_TYPE_WINDOWS_REGISTRY_HIVE,
    (header_size >= 4 && !xx_rt_memcmp(header, "\x72\x65\x67\x66", 4)))
XX_FORMAT_READER_PROBE(windows_evtx, XX_FILE_TYPE_WINDOWS_EVTX,
    (header_size >= 8 && !xx_rt_memcmp(header, "\x45\x6c\x66\x46\x69\x6c\x65\x00", 8)))
XX_FORMAT_READER_PROBE(binary_plist, XX_FILE_TYPE_BINARY_PLIST,
    (header_size >= 8 && !xx_rt_memcmp(header, "\x62\x70\x6c\x69\x73\x74\x30\x30", 8)))
XX_FORMAT_READER_PROBE(cbor, XX_FILE_TYPE_CBOR,
    (header_size >= 3 && !xx_rt_memcmp(header, "\xd9\xd9\xf7", 3)))
XX_FORMAT_READER_PROBE(openzim, XX_FILE_TYPE_OPENZIM,
    (header_size >= 4 && !xx_rt_memcmp(header, "\x5a\x49\x4d\x04", 4)))
XX_FORMAT_READER_PROBE(apache_orc, XX_FILE_TYPE_APACHE_ORC,
    (header_size >= 3 && !xx_rt_memcmp(header, "\x4f\x52\x43", 3)))
XX_FORMAT_READER_PROBE(hadoop_sequencefile, XX_FILE_TYPE_HADOOP_SEQUENCEFILE,
    (header_size >= 4 && !xx_rt_memcmp(header, "\x53\x45\x51\x06", 4)))
XX_FORMAT_READER_PROBE(snappy_framed, XX_FILE_TYPE_SNAPPY_FRAMED,
    (header_size >= 10 && !xx_rt_memcmp(header, "\xff\x06\x00\x00\x73\x4e\x61\x50\x70\x59", 10)))
XX_FORMAT_READER_PROBE(lzf_stream, XX_FILE_TYPE_LZF_STREAM,
    (header_size >= 2 && !xx_rt_memcmp(header, "\x5a\x56", 2)))
XX_FORMAT_READER_PROBE(fastlz_sixpack, XX_FILE_TYPE_FASTLZ_SIXPACK,
    (header_size >= 8 && !xx_rt_memcmp(header, "\x89\x36\x50\x4b\x0d\x0a\x1a\x0a", 8)))
XX_FORMAT_READER_PROBE(linux_btf, XX_FILE_TYPE_LINUX_BTF,
    (header_size >= 4 && !xx_rt_memcmp(header, "\x9f\xeb\x01\x00", 4)))
XX_FORMAT_READER_PROBE(flatgeobuf, XX_FILE_TYPE_FLATGEOBUF,
    (header_size >= 8 && !xx_rt_memcmp(header, "\x66\x67\x62\x03\x66\x67\x62\x00", 8)))
XX_FORMAT_READER_PROBE(astc_texture, XX_FILE_TYPE_ASTC_TEXTURE,
    (total_size >= 32 && header_size >= 4U && ((header[0] == 0x13U && header[1] == 0xabU && header[2] == 0xa1U && header[3] == 0x5cU))))
XX_FORMAT_READER_PROBE(pkm_texture, XX_FILE_TYPE_PKM_TEXTURE,
    (total_size >= 24 && header_size >= 6U && ((header[0] == 0x50U && header[1] == 0x4bU && header[2] == 0x4dU && header[3] == 0x20U && header[4] == 0x31U && header[5] == 0x30U) || (header[0] == 0x50U && header[1] == 0x4bU && header[2] == 0x4dU && header[3] == 0x20U && header[4] == 0x32U && header[5] == 0x30U))))
XX_FORMAT_READER_PROBE(basis_texture, XX_FILE_TYPE_BASIS_TEXTURE,
    (total_size >= 100 && header_size >= 2U && ((header[0] == 0x73U && header[1] == 0x42U))))
XX_FORMAT_READER_PROBE(openctm_mesh, XX_FILE_TYPE_OPENCTM_MESH,
    (total_size >= 56 && header_size >= 4U && ((header[0] == 0x4fU && header[1] == 0x43U && header[2] == 0x54U && header[3] == 0x4dU))))
XX_FORMAT_READER_PROBE(font_bdf, XX_FILE_TYPE_FONT_BDF,
    (total_size >= 70 && header_size >= 10U && ((header[0] == 0x53U && header[1] == 0x54U && header[2] == 0x41U && header[3] == 0x52U && header[4] == 0x54U && header[5] == 0x46U && header[6] == 0x4fU && header[7] == 0x4eU && header[8] == 0x54U && header[9] == 0x20U))))
XX_FORMAT_READER_PROBE(font_pcf, XX_FILE_TYPE_FONT_PCF,
    (total_size >= 68 && header_size >= 4U && ((header[0] == 0x01U && header[1] == 0x66U && header[2] == 0x63U && header[3] == 0x70U))))
XX_FORMAT_READER_PROBE(font_psf, XX_FILE_TYPE_FONT_PSF,
    (total_size >= 33 && header_size >= 4U && ((header[0] == 0x36U && header[1] == 0x04U) || (header[0] == 0x72U && header[1] == 0xb5U && header[2] == 0x4aU && header[3] == 0x86U))))
XX_FORMAT_READER_PROBE(font_windows_fnt, XX_FILE_TYPE_FONT_WINDOWS_FNT,
    (total_size >= 123 && header_size >= 2U && ((header[0] == 0x00U && header[1] == 0x02U) || (header[0] == 0x00U && header[1] == 0x03U))))
XX_FORMAT_READER_PROBE(tex_pk, XX_FILE_TYPE_TEX_PK,
    (total_size >= 24 && header_size >= 2U && ((header[0] == 0xf7U && header[1] == 0x59U))))
XX_FORMAT_READER_PROBE(tex_dvi, XX_FILE_TYPE_TEX_DVI,
    (total_size >= 54 && header_size >= 2U && ((header[0] == 0xf7U && header[1] == 0x02U))))
XX_FORMAT_READER_PROBE(netpbm_pfm, XX_FILE_TYPE_NETPBM_PFM,
    (total_size >= 14 && header_size >= 3U && ((header[0] == 0x50U && header[1] == 0x46U && header[2] == 0x0aU) || (header[0] == 0x50U && header[1] == 0x66U && header[2] == 0x0aU))))
XX_FORMAT_READER_PROBE(steinberg_vst3preset, XX_FILE_TYPE_STEINBERG_VST3PRESET,
    (total_size >= 76 && header_size >= 4U && ((header[0] == 0x56U && header[1] == 0x53U && header[2] == 0x54U && header[3] == 0x33U))))
XX_FORMAT_READER_PROBE(font_bmfont, XX_FILE_TYPE_FONT_BMFONT,
    (total_size >= 79 && header_size >= 4U && ((header[0] == 0x42U && header[1] == 0x4dU && header[2] == 0x46U && header[3] == 0x03U))))
XX_FORMAT_READER_PROBE(snes_spc, XX_FILE_TYPE_SNES_SPC,
    (header_size >= 33 && xx_rt_memcmp(header, "\x53\x4e\x45\x53\x2d\x53\x50\x43\x37\x30\x30\x20\x53\x6f\x75\x6e\x64\x20\x46\x69\x6c\x65\x20\x44\x61\x74\x61\x20\x76\x30\x2e\x33\x30", 33) == 0))
XX_FORMAT_READER_PROBE(gameboy_gbs, XX_FILE_TYPE_GAMEBOY_GBS,
    (header_size >= 4 && xx_rt_memcmp(header, "\x47\x42\x53\x01", 4) == 0))
XX_FORMAT_READER_PROBE(sega_sgc, XX_FILE_TYPE_SEGA_SGC,
    (header_size >= 5 && xx_rt_memcmp(header, "\x53\x47\x43\x1a\x01", 5) == 0))
XX_FORMAT_READER_PROBE(s98_log, XX_FILE_TYPE_S98_LOG,
    (header_size >= 4 && xx_rt_memcmp(header, "\x53\x39\x38\x33", 4) == 0))
XX_FORMAT_READER_PROBE(atari_sap, XX_FILE_TYPE_ATARI_SAP,
    (header_size >= 5 && xx_rt_memcmp(header, "\x53\x41\x50\x0d\x0a", 5) == 0))
XX_FORMAT_READER_PROBE(sc68_music, XX_FILE_TYPE_SC68_MUSIC,
    (header_size >= 56 && xx_rt_memcmp(header, "\x53\x43\x36\x38\x20\x4d\x75\x73\x69\x63\x2d\x66\x69\x6c\x65\x20\x2f\x20\x28\x63\x29\x20\x28\x42\x65\x4e\x29\x6a\x61\x6d\x69\x6e\x20\x47\x65\x72\x61\x72\x64\x20\x2f\x20\x53\x61\x73\x48\x69\x70\x41\x2d\x44\x65\x76\x20\x20\x00", 56) == 0))
XX_FORMAT_READER_PROBE(zx_spectrum_pzx, XX_FILE_TYPE_ZX_SPECTRUM_PZX,
    (header_size >= 4 && xx_rt_memcmp(header, "\x50\x5a\x58\x54", 4) == 0))
XX_FORMAT_READER_PROBE(acorn_uef, XX_FILE_TYPE_ACORN_UEF,
    (header_size >= 10 && xx_rt_memcmp(header, "\x55\x45\x46\x20\x46\x69\x6c\x65\x21\x00", 10) == 0))
XX_FORMAT_READER_PROBE(nintendo_unif, XX_FILE_TYPE_NINTENDO_UNIF,
    (header_size >= 4 && xx_rt_memcmp(header, "\x55\x4e\x49\x46", 4) == 0))
XX_FORMAT_READER_PROBE(nintendo_fds, XX_FILE_TYPE_NINTENDO_FDS,
    (header_size >= 4 && xx_rt_memcmp(header, "\x46\x44\x53\x1a", 4) == 0))
XX_FORMAT_READER_PROBE(ucsc_bigwig, XX_FILE_TYPE_UCSC_BIGWIG,
    (((header_size >= 4 && header[0] == 38 && header[1] == 252 && header[2] == 143 && header[3] == 136) || (header_size >= 4 && header[0] == 136 && header[1] == 143 && header[2] == 252 && header[3] == 38))))
XX_FORMAT_READER_PROBE(ucsc_bigbed, XX_FILE_TYPE_UCSC_BIGBED,
    (((header_size >= 4 && header[0] == 235 && header[1] == 242 && header[2] == 137 && header[3] == 135) || (header_size >= 4 && header[0] == 135 && header[1] == 137 && header[2] == 242 && header[3] == 235))))
XX_FORMAT_READER_PROBE(phylo_nexus, XX_FILE_TYPE_PHYLO_NEXUS,
    (((header_size >= 6 && header[0] == 35 && header[1] == 78 && header[2] == 69 && header[3] == 88 && header[4] == 85 && header[5] == 83) || (header_size >= 6 && header[0] == 35 && header[1] == 110 && header[2] == 101 && header[3] == 120 && header[4] == 117 && header[5] == 115))))
XX_FORMAT_READER_PROBE(sqlite_rollback_journal, XX_FILE_TYPE_SQLITE_ROLLBACK_JOURNAL,
    (((header_size >= 8 && header[0] == 217 && header[1] == 213 && header[2] == 5 && header[3] == 249 && header[4] == 32 && header[5] == 161 && header[6] == 99 && header[7] == 215))))
XX_FORMAT_READER_PROBE(neuroscan_cnt, XX_FILE_TYPE_NEUROSCAN_CNT,
    (((header_size >= 11 && header[0] == 86 && header[1] == 101 && header[2] == 114 && header[3] == 115 && header[4] == 105 && header[5] == 111 && header[6] == 110 && header[7] == 32 && header[8] == 51 && header[9] == 46 && header[10] == 48))))
XX_FORMAT_READER_PROBE(axona_tetrode, XX_FILE_TYPE_AXONA_TETRODE,
    (((header_size >= 11 && header[0] == 116 && header[1] == 114 && header[2] == 105 && header[3] == 97 && header[4] == 108 && header[5] == 95 && header[6] == 100 && header[7] == 97 && header[8] == 116 && header[9] == 101 && header[10] == 32))))
XX_FORMAT_READER_PROBE(python_pickle, XX_FILE_TYPE_PYTHON_PICKLE,
    (((header_size >= 2 && header[0] == 128 && header[1] == 2) || (header_size >= 2 && header[0] == 128 && header[1] == 3) || (header_size >= 2 && header[0] == 128 && header[1] == 4) || (header_size >= 2 && header[0] == 128 && header[1] == 5))))
XX_FORMAT_READER_PROBE(inivation_aedat, XX_FILE_TYPE_INIVATION_AEDAT,
    (((header_size >= 14 && header[0] == 35 && header[1] == 33 && header[2] == 65 && header[3] == 69 && header[4] == 82 && header[5] == 45 && header[6] == 68 && header[7] == 65 && header[8] == 84 && header[9] == 50 && header[10] == 46 && header[11] == 48 && header[12] == 13 && header[13] == 10) || (header_size >= 13 && header[0] == 35 && header[1] == 33 && header[2] == 65 && header[3] == 69 && header[4] == 82 && header[5] == 45 && header[6] == 68 && header[7] == 65 && header[8] == 84 && header[9] == 50 && header[10] == 46 && header[11] == 48 && header[12] == 10))))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(nix_nar, XX_FILE_TYPE_NIX_NAR,
    (header_size >= 21 && !xx_rt_memcmp(header, "\x0d\x00\x00\x00\x00\x00\x00\x00\x6e\x69\x78\x2d\x61\x72\x63\x68\x69\x76\x65\x2d\x31", 21)))
XX_FORMAT_READER_PROBE(redis_rdb, XX_FILE_TYPE_REDIS_RDB,
    (header_size >= 5 && !xx_rt_memcmp(header, "\x52\x45\x44\x49\x53", 5)))
XX_FORMAT_READER_PROBE(postgres_custom, XX_FILE_TYPE_POSTGRES_CUSTOM,
    (header_size >= 5 && !xx_rt_memcmp(header, "\x50\x47\x44\x4d\x50", 5)))
XX_FORMAT_READER_PROBE(mysql_binlog, XX_FILE_TYPE_MYSQL_BINLOG,
    (header_size >= 4 && !xx_rt_memcmp(header, "\xfe\x62\x69\x6e", 4)))
XX_FORMAT_READER_PROBE(android_binary_xml, XX_FILE_TYPE_ANDROID_BINARY_XML,
    (header_size >= 4 && !xx_rt_memcmp(header, "\x03\x00\x08\x00", 4)))
XX_FORMAT_READER_PROBE(android_resources_arsc, XX_FILE_TYPE_ANDROID_RESOURCES_ARSC,
    (header_size >= 4 && !xx_rt_memcmp(header, "\x02\x00\x0c\x00", 4)))
XX_FORMAT_READER_PROBE(erlang_external_term, XX_FILE_TYPE_ERLANG_EXTERNAL_TERM,
    (header_size >= 1 && !xx_rt_memcmp(header, "\x83", 1)))
XX_FORMAT_READER_PROBE(windows_shell_link, XX_FILE_TYPE_WINDOWS_SHELL_LINK,
    (header_size >= 20 && !xx_rt_memcmp(header, "\x4c\x00\x00\x00\x01\x14\x02\x00\x00\x00\x00\x00\xc0\x00\x00\x00\x00\x00\x00\x46", 20)))
XX_FORMAT_READER_PROBE(off_mesh, XX_FILE_TYPE_OFF_MESH,
    (total_size >= 30 && header_size >= 3U && ((header[0]==0x4fU && header[1]==0x46U && header[2]==0x46U))))
XX_FORMAT_READER_PROBE(ac3d_model, XX_FILE_TYPE_AC3D_MODEL,
    (total_size >= 45 && header_size >= 5U && ((header[0]==0x41U && header[1]==0x43U && header[2]==0x33U && header[3]==0x44U && header[4]==0x62U))))
XX_FORMAT_READER_PROBE(qubicle_qb, XX_FILE_TYPE_QUBICLE_QB,
    (total_size >= 54 && header_size >= 4U && ((header[0]==0x01U && header[1]==0x01U && header[2]==0x00U && header[3]==0x00U))))
XX_FORMAT_READER_PROBE(terragen_ter, XX_FILE_TYPE_TERRAGEN_TER,
    (total_size >= 40 && header_size >= 16U && ((header[0]==0x54U && header[1]==0x45U && header[2]==0x52U && header[3]==0x52U && header[4]==0x41U && header[5]==0x47U && header[6]==0x45U && header[7]==0x4eU && header[8]==0x54U && header[9]==0x45U && header[10]==0x52U && header[11]==0x52U && header[12]==0x41U && header[13]==0x49U && header[14]==0x4eU && header[15]==0x20U))))
XX_FORMAT_READER_PROBE(gimp_xcf, XX_FILE_TYPE_GIMP_XCF,
    (total_size >= 50 && header_size >= 9U && ((header[0]==0x67U && header[1]==0x69U && header[2]==0x6dU && header[3]==0x70U && header[4]==0x20U && header[5]==0x78U && header[6]==0x63U && header[7]==0x66U && header[8]==0x20U))))
XX_FORMAT_READER_PROBE(photoshop_abr, XX_FILE_TYPE_PHOTOSHOP_ABR,
    (total_size >= 42 && header_size >= 2U && ((header[0]==0x00U && header[1]==0x01U) || (header[0]==0x00U && header[1]==0x02U))))
XX_FORMAT_READER_PROBE(softimage_pic, XX_FILE_TYPE_SOFTIMAGE_PIC,
    (total_size >= 108 && header_size >= 4U && ((header[0]==0x53U && header[1]==0x80U && header[2]==0xf6U && header[3]==0x34U))))
XX_FORMAT_READER_PROBE(qt_qpicture, XX_FILE_TYPE_QT_QPICTURE,
    (total_size >= 34 && header_size >= 4U && ((header[0]==0x51U && header[1]==0x50U && header[2]==0x49U && header[3]==0x43U))))
XX_FORMAT_READER_PROBE(font_type1_pfb, XX_FILE_TYPE_FONT_TYPE1_PFB,
    (total_size >= 30 && header_size >= 2U && ((header[0]==0x80U && header[1]==0x01U))))
XX_FORMAT_READER_PROBE(tex_gf, XX_FILE_TYPE_TEX_GF,
    (total_size >= 55 && header_size >= 2U && ((header[0]==0xf7U && header[1]==0x83U))))
XX_FORMAT_READER_PROBE(bpg_image, XX_FILE_TYPE_BPG_IMAGE,
    (total_size >= 12 && header_size >= 4U && ((header[0]==0x42U && header[1]==0x50U && header[2]==0x47U && header[3]==0xfbU))))
XX_FORMAT_READER_PROBE(mng_animation, XX_FILE_TYPE_MNG_ANIMATION,
    (total_size >= 70 && header_size >= 8U && ((header[0]==0x8aU && header[1]==0x4dU && header[2]==0x4eU && header[3]==0x47U && header[4]==0x0dU && header[5]==0x0aU && header[6]==0x1aU && header[7]==0x0aU))))
XX_FORMAT_READER_PROBE(atari_7800_a78, XX_FILE_TYPE_ATARI_7800_A78,
    (header_size>=128 && !xx_rt_memcmp(header+1,"ATARI7800",9) && !xx_rt_memcmp(header+100,"ACTUAL CART DATA STARTS HERE",28)))
XX_FORMAT_READER_PROBE(commodore_pc64, XX_FILE_TYPE_COMMODORE_PC64,
    (header_size>=26 && !xx_rt_memcmp(header,"C64File",8)))
XX_FORMAT_READER_PROBE(atari_cas, XX_FILE_TYPE_ATARI_CAS,
    (header_size>=8 && !xx_rt_memcmp(header,"FUJI",4)))
XX_FORMAT_READER_PROBE(msx_cas, XX_FILE_TYPE_MSX_CAS,
    (header_size>=32 && !xx_rt_memcmp(header,"\x1f\xa6\xde\xba\xcc\x13\x7d\x74",8)))
XX_FORMAT_READER_PROBE(oric_tap, XX_FILE_TYPE_ORIC_TAP,
    (header_size>=14 && header[0]==0x16 && header[1]==0x16 && header[2]==0x16))
XX_FORMAT_READER_PROBE(dragon_cas, XX_FILE_TYPE_DRAGON_CAS,
    (header_size>=21 && header[0]==0x55))
XX_FORMAT_READER_PROBE(amiga_ahx, XX_FILE_TYPE_AMIGA_AHX,
    (header_size>=14 && !xx_rt_memcmp(header,"THX",3) && header[3]<=1))
XX_FORMAT_READER_PROBE(amstrad_cpc_sna, XX_FILE_TYPE_AMSTRAD_CPC_SNA,
    (header_size>=256 && !xx_rt_memcmp(header,"MV - SNA",8)))
XX_FORMAT_READER_PROBE(vtech_vz, XX_FILE_TYPE_VTECH_VZ,
    (header_size>=24 && !xx_rt_memcmp(header,"VZF",3) && (header[3]=='0' || header[3]=='1')))
XX_FORMAT_READER_PROBE(zx_hobeta, XX_FILE_TYPE_ZX_HOBETA,
    (header_size>=17 && header[13]==0 && header[14]!=0 && (header[8]=='B' || header[8]=='C' || header[8]=='D')))

XX_FORMAT_PROBE_READ_EXACT(4100U)
XX_FORMAT_READER_PROBE(amazon_ion_binary, XX_FILE_TYPE_AMAZON_ION_BINARY,
    (header_size >= 4 && !xx_rt_memcmp(header,"\xe0\x01\x00\xea",4)))
XX_FORMAT_READER_PROBE(rocksdb_blob, XX_FILE_TYPE_ROCKSDB_BLOB,
    (header_size >= 8 && !xx_rt_memcmp(header,"\x37\x8f\x24\x00\x01\x00\x00\x00",8)))
XX_FORMAT_READER_PROBE(amqp_frames, XX_FILE_TYPE_AMQP_FRAMES,
    (header_size >= 8 && !xx_rt_memcmp(header,"\x41\x4d\x51\x50\x00\x00\x09\x01",8)))
XX_FORMAT_READER_PROBE(openssh_private_key, XX_FILE_TYPE_OPENSSH_PRIVATE_KEY,
    (header_size >= 15 && !xx_rt_memcmp(header,"\x6f\x70\x65\x6e\x73\x73\x68\x2d\x6b\x65\x79\x2d\x76\x31\x00",15)))
XX_FORMAT_READER_PROBE(kerberos_keytab, XX_FILE_TYPE_KERBEROS_KEYTAB,
    (header_size >= 2 && !xx_rt_memcmp(header,"\x05\x02",2)))
XX_FORMAT_READER_PROBE(gimp_gpl, XX_FILE_TYPE_GIMP_GPL,
    (total_size >= 12 && header_size >= 12U && !xx_rt_memcmp(header,"\x47\x49\x4d\x50\x20\x50\x61\x6c\x65\x74\x74\x65",12U)))
XX_FORMAT_READER_PROBE(gimp_ggr, XX_FILE_TYPE_GIMP_GGR,
    (total_size >= 13 && header_size >= 13U && !xx_rt_memcmp(header,"\x47\x49\x4d\x50\x20\x47\x72\x61\x64\x69\x65\x6e\x74",13U)))
XX_FORMAT_READER_PROBE(paintshop_psp, XX_FILE_TYPE_PAINTSHOP_PSP,
    (total_size >= 36 && header_size >= 27U && !xx_rt_memcmp(header,"\x50\x61\x69\x6e\x74\x20\x53\x68\x6f\x70\x20\x50\x72\x6f\x20\x49\x6d\x61\x67\x65\x20\x46\x69\x6c\x65\x0a\x1a",27U)))
XX_FORMAT_READER_PROBE(photoshop_pat, XX_FILE_TYPE_PHOTOSHOP_PAT,
    (total_size >= 12 && header_size >= 4U && !xx_rt_memcmp(header,"\x38\x42\x50\x54",4U)))
XX_FORMAT_READER_PROBE(mmd_pmx, XX_FILE_TYPE_MMD_PMX,
    (total_size >= 17 && header_size >= 4U && !xx_rt_memcmp(header,"\x50\x4d\x58\x20",4U)))
XX_FORMAT_READER_PROBE(metasequoia_mqo, XX_FILE_TYPE_METASEQUOIA_MQO,
    (total_size >= 20 && header_size >= 20U && !xx_rt_memcmp(header,"\x4d\x65\x74\x61\x73\x65\x71\x75\x6f\x69\x61\x20\x44\x6f\x63\x75\x6d\x65\x6e\x74",20U)))
XX_FORMAT_READER_PROBE(calma_gdsii, XX_FILE_TYPE_CALMA_GDSII,
    (total_size >= 6 && header_size >= 4U && !xx_rt_memcmp(header,"\x00\x06\x00\x02",4U)))
XX_FORMAT_READER_PROBE(autodesk_ase, XX_FILE_TYPE_AUTODESK_ASE,
    (total_size >= 20 && header_size >= 19U && !xx_rt_memcmp(header,"\x2a\x33\x44\x53\x4d\x41\x58\x5f\x41\x53\x43\x49\x49\x45\x58\x50\x4f\x52\x54",19U)))
XX_FORMAT_READER_PROBE(freesurfer_surface, XX_FILE_TYPE_FREESURFER_SURFACE,
    (total_size >= 12 && header_size >= 3U && !xx_rt_memcmp(header,"\xff\xff\xfe",3U)))
XX_FORMAT_READER_PROBE(gmsh_msh, XX_FILE_TYPE_GMSH_MSH,
    (total_size >= 12 && header_size >= 11U && !xx_rt_memcmp(header,"\x24\x4d\x65\x73\x68\x46\x6f\x72\x6d\x61\x74",11U)))
XX_FORMAT_READER_PROBE(netgen_vol, XX_FILE_TYPE_NETGEN_VOL,
    (total_size >= 8 && header_size >= 6U && !xx_rt_memcmp(header,"\x6d\x65\x73\x68\x33\x64",6U)))
XX_FORMAT_READER_PROBE(font_afm, XX_FILE_TYPE_FONT_AFM,
    (total_size >= 16 && header_size >= 16U && !xx_rt_memcmp(header,"\x53\x74\x61\x72\x74\x46\x6f\x6e\x74\x4d\x65\x74\x72\x69\x63\x73",16U)))
XX_FORMAT_READER_PROBE(nintendo_sdat, XX_FILE_TYPE_NINTENDO_SDAT,
    (header_size>=6 && !xx_rt_memcmp(header,"\x53\x44\x41\x54\xff\xfe",6)))
XX_FORMAT_READER_PROBE(sony_vab, XX_FILE_TYPE_SONY_VAB,
    (header_size>=4 && !xx_rt_memcmp(header,"\x70\x42\x41\x56",4)))
XX_FORMAT_READER_PROBE(yamaha_ym, XX_FILE_TYPE_YAMAHA_YM,
    (header_size>=12 && (!xx_rt_memcmp(header,"YM5!LeOnArD!",12) || !xx_rt_memcmp(header,"YM6!LeOnArD!",12))))
XX_FORMAT_READER_PROBE(zx_ayemul, XX_FILE_TYPE_ZX_AYEMUL,
    (header_size>=8 && !xx_rt_memcmp(header,"\x5a\x58\x41\x59\x45\x4d\x55\x4c",8)))
XX_FORMAT_READER_PROBE(dragon_vdk, XX_FILE_TYPE_DRAGON_VDK,
    (header_size>=2 && !xx_rt_memcmp(header,"\x64\x6b",2)))
XX_FORMAT_READER_PROBE(apple_a2r, XX_FILE_TYPE_APPLE_A2R,
    (header_size>=8 &&
        (!xx_rt_memcmp(header,"\x41\x32\x52\x32\xff\x0a\x0d\x0a",8) ||
         !xx_rt_memcmp(header,"\x41\x32\x52\x33\xff\x0a\x0d\x0a",8))))
XX_FORMAT_READER_PROBE(atari_atr, XX_FILE_TYPE_ATARI_ATR,
    (header_size>=2 && !xx_rt_memcmp(header,"\x96\x02",2)))
XX_FORMAT_READER_PROBE(atari_pasti_stx, XX_FILE_TYPE_ATARI_PASTI_STX,
    (header_size>=4 && !xx_rt_memcmp(header,"\x52\x53\x59\x00",4)))
XX_FORMAT_READER_PROBE(amiga_ipf, XX_FILE_TYPE_AMIGA_IPF,
    (header_size>=4 && !xx_rt_memcmp(header,"\x43\x41\x50\x53",4)))
XX_FORMAT_READER_PROBE(tracker_dtt, XX_FILE_TYPE_TRACKER_DTT,
    (header_size>=4 && !xx_rt_memcmp(header,"\x44\x73\x6b\x54",4)))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(jks_keystore, XX_FILE_TYPE_JKS_KEYSTORE,
    (header_size >= 8 && !xx_rt_memcmp(header,"\xfe\xed\xfe\xed\x00\x00\x00\x02",8)))
XX_FORMAT_READER_PROBE(java_serialization, XX_FILE_TYPE_JAVA_SERIALIZATION,
    (header_size >= 4 && !xx_rt_memcmp(header,"\xac\xed\x00\x05",4)))
XX_FORMAT_READER_PROBE(gdbm_dump, XX_FILE_TYPE_GDBM_DUMP,
    (header_size >= 28 && !xx_rt_memcmp(header,"\x23\x20\x47\x44\x42\x4d\x20\x64\x75\x6d\x70\x20\x66\x69\x6c\x65\x20\x63\x72\x65\x61\x74\x65\x64\x20\x62\x79\x20",28)))
XX_FORMAT_READER_PROBE(adobe_acb, XX_FILE_TYPE_ADOBE_ACB,
    (total_size >= 32 && header_size >= 4U && !xx_rt_memcmp(header,"\x38\x42\x43\x42",4U)))
XX_FORMAT_READER_PROBE(jasc_palette, XX_FILE_TYPE_JASC_PALETTE,
    (total_size >= 16 && header_size >= 8U && !xx_rt_memcmp(header,"\x4a\x41\x53\x43\x2d\x50\x41\x4c",8U)))
XX_FORMAT_READER_PROBE(jpeg2000_pgx, XX_FILE_TYPE_JPEG2000_PGX,
    (total_size >= 14 && header_size >= 3U && !xx_rt_memcmp(header,"\x50\x47\x20",3U)))
XX_FORMAT_READER_PROBE(amiga_diskobject, XX_FILE_TYPE_AMIGA_DISKOBJECT,
    (total_size >= 78 && header_size >= 4U && !xx_rt_memcmp(header,"\xe3\x10\x00\x01",4U)))
XX_FORMAT_READER_PROBE(tex_vf, XX_FILE_TYPE_TEX_VF,
    (total_size >= 12 && header_size >= 2U && !xx_rt_memcmp(header,"\xf7\xca",2U)))
XX_FORMAT_READER_PROBE(surfer_grid, XX_FILE_TYPE_SURFER_GRID,
    (total_size >= 24 && header_size >= 4U && !xx_rt_memcmp(header,"\x44\x53\x41\x41",4U)))
XX_FORMAT_READER_PROBE(step_part21, XX_FILE_TYPE_STEP_PART21,
    (total_size >= 24 && header_size >= 13U && !xx_rt_memcmp(header,"\x49\x53\x4f\x2d\x31\x30\x33\x30\x33\x2d\x32\x31\x3b",13U)))
XX_FORMAT_READER_PROBE(excellon_drill, XX_FILE_TYPE_EXCELLON_DRILL,
    (total_size >= 16 && header_size >= 3U && !xx_rt_memcmp(header,"\x4d\x34\x38",3U)))
XX_FORMAT_READER_PROBE(vrml_scene, XX_FILE_TYPE_VRML_SCENE,
    (total_size >= 24 && header_size >= 15U && !xx_rt_memcmp(header,"\x23\x56\x52\x4d\x4c\x20\x56\x32\x2e\x30\x20\x75\x74\x66\x38",15U)))
XX_FORMAT_READER_PROBE(asylum_amf, XX_FILE_TYPE_ASYLUM_AMF,
    ((header_size >= 24 && xx_rt_memcmp(header + 0, "\x41\x53\x59\x4c\x55\x4d\x20\x4d\x75\x73\x69\x63\x20\x46\x6f\x72\x6d\x61\x74\x20\x56\x31\x2e\x30", 24) == 0)))
XX_FORMAT_READER_PROBE(tracker_stx, XX_FILE_TYPE_TRACKER_STX,
    (((header_size >= 28 && xx_rt_memcmp(header + 20, "\x21\x53\x63\x72\x65\x61\x6d\x21", 8) == 0) || (header_size >= 28 && xx_rt_memcmp(header + 20, "\x42\x4d\x4f\x44\x32\x53\x54\x4d", 8) == 0)) && header_size >= 64 && xx_rt_memcmp(header + 60, "SCRM", 4) == 0))
XX_FORMAT_READER_PROBE(tracker_dtm, XX_FILE_TYPE_TRACKER_DTM,
    ((header_size >= 4 && xx_rt_memcmp(header + 0, "\x44\x2e\x54\x2e", 4) == 0)))
XX_FORMAT_READER_PROBE(tracker_soundfx, XX_FILE_TYPE_TRACKER_SOUNDFX,
    ((header_size >= 64 && xx_rt_memcmp(header + 60, "\x53\x4f\x4e\x47", 4) == 0) || (header_size >= 128 && xx_rt_memcmp(header + 124, "\x53\x4f\x4e\x47", 4) == 0)))
XX_FORMAT_READER_PROBE(tracker_funk, XX_FILE_TYPE_TRACKER_FUNK,
    ((header_size >= 4 && xx_rt_memcmp(header + 0, "\x46\x75\x6e\x6b", 4) == 0)))
XX_FORMAT_READER_PROBE(tracker_archimedes, XX_FILE_TYPE_TRACKER_ARCHIMEDES,
    ((header_size >= 4 && xx_rt_memcmp(header + 0, "\x4d\x55\x53\x58", 4) == 0)))
XX_FORMAT_READER_PROBE(pce_psi, XX_FILE_TYPE_PCE_PSI,
    ((header_size >= 4 && xx_rt_memcmp(header + 0, "\x50\x53\x49\x20", 4) == 0)))
XX_FORMAT_READER_PROBE(pc98_d88, XX_FILE_TYPE_PC98_D88,
    (header_size >= 32 && header[16] == 0 && header[17] == 0 && (header[26] == 0 || header[26] == 16) && (header[27] == 0 || header[27] == 16 || header[27] == 32)))
XX_FORMAT_READER_PROBE(hxc_mfm, XX_FILE_TYPE_HXC_MFM,
    ((header_size >= 7 && xx_rt_memcmp(header + 0, "\x48\x58\x43\x4d\x46\x4d\x00", 7) == 0)))
XX_FORMAT_READER_PROBE(yaze_ydsk, XX_FILE_TYPE_YAZE_YDSK,
    ((header_size >= 10 && xx_rt_memcmp(header + 0, "\x3c\x43\x50\x4d\x5f\x44\x69\x73\x6b\x3e", 10) == 0)))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(bgp_messages, XX_FILE_TYPE_BGP_MESSAGES,
    (header_size >= 29 && header[0] == 255 && header[15] == 255 && header[18] == 1))
XX_FORMAT_READER_PROBE(ssh_transport, XX_FILE_TYPE_SSH_TRANSPORT,
    (header_size >= 32 && !xx_rt_memcmp(header,"SSH-2.0-",8)))
XX_FORMAT_READER_PROBE(putty_ppk, XX_FILE_TYPE_PUTTY_PPK,
    (header_size >= 32 && !xx_rt_memcmp(header,"PuTTY-User-Key-File-",20)))
XX_FORMAT_READER_PROBE(gguf, XX_FILE_TYPE_GGUF,
    (header_size >= 24 && !xx_rt_memcmp(header,"GGUF",4)))
XX_FORMAT_READER_PROBE(stomp_frames, XX_FILE_TYPE_STOMP_FRAMES,
    (header_size >= 32 && (!xx_rt_memcmp(header,"STOMP",5) || !xx_rt_memcmp(header,"CONNECT",7))))
XX_FORMAT_READER_PROBE(fontforge_sfd, XX_FILE_TYPE_FONTFORGE_SFD,
    (total_size >= 24 && header_size >= 13U && !xx_rt_memcmp(header,"\x53\x70\x6c\x69\x6e\x65\x46\x6f\x6e\x74\x44\x42\x3a",13U)))
XX_FORMAT_READER_PROBE(grub_pff2, XX_FILE_TYPE_GRUB_PFF2,
    (total_size >= 32 && header_size >= 12U && !xx_rt_memcmp(header,"\x46\x49\x4c\x45\x00\x00\x00\x04\x50\x46\x46\x32",12U)))
XX_FORMAT_READER_PROBE(bvh_motion, XX_FILE_TYPE_BVH_MOTION,
    (total_size >= 24 && header_size >= 9U && !xx_rt_memcmp(header,"\x48\x49\x45\x52\x41\x52\x43\x48\x59",9U)))
XX_FORMAT_READER_PROBE(directx_x, XX_FILE_TYPE_DIRECTX_X,
    (total_size >= 32 && header_size >= 4U && !xx_rt_memcmp(header,"\x78\x6f\x66\x20",4U)))
XX_FORMAT_READER_PROBE(medit_mesh, XX_FILE_TYPE_MEDIT_MESH,
    (total_size >= 24 && header_size >= 20U && !xx_rt_memcmp(header,"\x4d\x65\x73\x68\x56\x65\x72\x73\x69\x6f\x6e\x46\x6f\x72\x6d\x61\x74\x74\x65\x64",20U)))
XX_FORMAT_READER_PROBE(gocad_model, XX_FILE_TYPE_GOCAD_MODEL,
    (total_size >= 24 && header_size >= 6U && !xx_rt_memcmp(header,"\x47\x4f\x43\x41\x44\x20",6U)))
XX_FORMAT_READER_PROBE(ensight_gold_geometry, XX_FILE_TYPE_ENSIGHT_GOLD_GEOMETRY,
    (total_size >= 400 && header_size >= 8U && !xx_rt_memcmp(header,"\x43\x20\x42\x69\x6e\x61\x72\x79",8U)))
XX_FORMAT_READER_PROBE(gmv_mesh, XX_FILE_TYPE_GMV_MESH,
    (total_size >= 24 && header_size >= 8U && !xx_rt_memcmp(header,"\x67\x6d\x76\x69\x6e\x70\x75\x74",8U)))
XX_FORMAT_READER_PROBE(dted_elevation, XX_FILE_TYPE_DTED_ELEVATION,
    (total_size >= 3428 && header_size >= 4U && !xx_rt_memcmp(header,"\x55\x48\x4c\x31",4U)))
XX_FORMAT_READER_PROBE(mapinfo_mif, XX_FILE_TYPE_MAPINFO_MIF,
    (total_size >= 24 && header_size >= 8U && !xx_rt_memcmp(header,"\x56\x65\x72\x73\x69\x6f\x6e\x20",8U)))
XX_FORMAT_READER_PROBE(tracker_coconizer, XX_FILE_TYPE_TRACKER_COCONIZER,
    (header_size >= 32 && (header[0] == 0x84 || header[0] == 0x88)))
XX_FORMAT_READER_PROBE(tracker_real, XX_FILE_TYPE_TRACKER_REAL,
    ((header_size >= 5 && header[0] == 0x52 && header[1] == 0x54 && header[2] == 0x4d && header[3] == 0x4d && header[4] == 0x20)))
XX_FORMAT_READER_PROBE(tracker_megatracker, XX_FILE_TYPE_TRACKER_MEGATRACKER,
    (header_size >= 8 && header[0] == 0x4d && header[1] == 0x47 && header[2] == 0x54 && header[3] == 0x11 && header[4] == 0xbd && header[5] == 0x4d && header[6] == 0x43 && header[7] == 0x53))
XX_FORMAT_READER_PROBE(amos_music_bank, XX_FILE_TYPE_AMOS_MUSIC_BANK,
    (header_size >= 20 && header[0] == 0x41 && header[1] == 0x6d && header[2] == 0x42 && header[3] == 0x6b && header[4] == 0 && header[5] == 3 && header[12] == 0x4d && header[13] == 0x75 && header[14] == 0x73 && header[15] == 0x69 && header[16] == 0x63 && header[17] == 0x20 && header[18] == 0x20 && header[19] == 0x20))
XX_FORMAT_READER_PROBE(adlib_rad, XX_FILE_TYPE_ADLIB_RAD,
    ((header_size >= 16 && header[0] == 0x52 && header[1] == 0x41 && header[2] == 0x44 && header[3] == 0x20 && header[4] == 0x62 && header[5] == 0x79 && header[6] == 0x20 && header[7] == 0x52 && header[8] == 0x45 && header[9] == 0x41 && header[10] == 0x4c && header[11] == 0x69 && header[12] == 0x54 && header[13] == 0x59 && header[14] == 0x21 && header[15] == 0x21)))
XX_FORMAT_READER_PROBE(adlib_amd, XX_FILE_TYPE_ADLIB_AMD,
    ((header_size >= 1071 && header[1062] == 0x3c && header[1063] == 0x6f && header[1064] == 0xef && header[1065] == 0x51 && header[1066] == 0x55 && header[1067] == 0xee && header[1068] == 0x52 && header[1069] == 0x6f && header[1070] == 0x52) || (header_size >= 1071 && header[1062] == 0x4d && header[1063] == 0x61 && header[1064] == 0x44 && header[1065] == 0x6f && header[1066] == 0x4b && header[1067] == 0x61 && header[1068] == 0x4e && header[1069] == 0x39 && header[1070] == 0x36)))
XX_FORMAT_READER_PROBE(adlib_hsc, XX_FILE_TYPE_ADLIB_HSC,
    (header_size >= 12 && header[8] <= 15 && header[9] <= 3 && header[10] <= 3))
XX_FORMAT_READER_PROBE(adlib_d00, XX_FILE_TYPE_ADLIB_D00,
    (header_size >= 11 && header[0] == 0x4a && header[1] == 0x43 && header[2] == 0x48 && header[3] == 0x26 && header[4] == 0x02 && header[5] == 0x66 && header[6] == 0 && header[7] == 4 && header[8] != 0 && header[9] != 0 && header[10] == 0))
XX_FORMAT_READER_PROBE(adlib_bnk, XX_FILE_TYPE_ADLIB_BNK,
    ((header_size >= 8 && header[2] == 0x41 && header[3] == 0x44 && header[4] == 0x4c && header[5] == 0x49 && header[6] == 0x42 && header[7] == 0x2d)))
XX_FORMAT_READER_PROBE(dosbox_dro, XX_FILE_TYPE_DOSBOX_DRO,
    ((header_size >= 8 && header[0] == 0x44 && header[1] == 0x42 && header[2] == 0x52 && header[3] == 0x41 && header[4] == 0x57 && header[5] == 0x4f && header[6] == 0x50 && header[7] == 0x4c)))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(sip_message, XX_FILE_TYPE_SIP_MESSAGE,
    (header_size >= 32 && (!xx_rt_memcmp(header,"MESSAGE ",8) || !xx_rt_memcmp(header,"OPTIONS ",8))))
XX_FORMAT_READER_PROBE(rtsp_message, XX_FILE_TYPE_RTSP_MESSAGE,
    (header_size >= 32 && (!xx_rt_memcmp(header,"OPTIONS ",8) || !xx_rt_memcmp(header,"DESCRIBE ",9))))
XX_FORMAT_READER_PROBE(age_encrypted, XX_FILE_TYPE_AGE_ENCRYPTED,
    (header_size >= 32 && header[21] == 10 && !xx_rt_memcmp(header,"age-encryption.org/v1",21)))
XX_FORMAT_READER_PROBE(dec_sixel, XX_FILE_TYPE_DEC_SIXEL,
    (total_size >= 12 && header_size >= 2U && !xx_rt_memcmp(header,"\x1b\x50",2U)))
XX_FORMAT_READER_PROBE(ogre_skeleton, XX_FILE_TYPE_OGRE_SKELETON,
    (total_size >= 24 && header_size >= 2U && !xx_rt_memcmp(header,"\x00\x10",2U)))
XX_FORMAT_READER_PROBE(cal3d_skeleton, XX_FILE_TYPE_CAL3D_SKELETON,
    (total_size >= 12 && header_size >= 4U && !xx_rt_memcmp(header,"\x43\x53\x46\x00",4U)))
XX_FORMAT_READER_PROBE(lightwave_scene, XX_FILE_TYPE_LIGHTWAVE_SCENE,
    (total_size >= 8 && header_size >= 4U && !xx_rt_memcmp(header,"\x4c\x57\x53\x43",4U)))
XX_FORMAT_READER_PROBE(crystallography_mtz, XX_FILE_TYPE_CRYSTALLOGRAPHY_MTZ,
    (total_size >= 80 && header_size >= 4U && !xx_rt_memcmp(header,"\x4d\x54\x5a\x20",4U)))
XX_FORMAT_READER_PROBE(amira_mesh, XX_FILE_TYPE_AMIRA_MESH,
    (total_size >= 64 && header_size >= 12U && !xx_rt_memcmp(header,"\x23\x20\x41\x6d\x69\x72\x61\x4d\x65\x73\x68\x20",12U)))
XX_FORMAT_READER_PROBE(jedec_fuse, XX_FILE_TYPE_JEDEC_FUSE,
    (total_size >= 16 && header_size >= 1U && !xx_rt_memcmp(header,"\x02",1U)))
XX_FORMAT_READER_PROBE(adlib_bam, XX_FILE_TYPE_ADLIB_BAM,
    (header_size >= 4 && !xx_rt_memcmp(header, "\x43\x42\x4d\x46", 4)))
XX_FORMAT_READER_PROBE(adlib_bmf, XX_FILE_TYPE_ADLIB_BMF,
    (header_size >= 6 && !xx_rt_memcmp(header, "\x42\x4d\x46\x31\x2e\x31", 6)))
XX_FORMAT_READER_PROBE(creative_cmf, XX_FILE_TYPE_CREATIVE_CMF,
    (header_size >= 4 && !xx_rt_memcmp(header, "\x43\x54\x4d\x46", 4)))
XX_FORMAT_READER_PROBE(adlib_dfm, XX_FILE_TYPE_ADLIB_DFM,
    (header_size >= 4 && !xx_rt_memcmp(header, "\x44\x46\x4d\x1a", 4)))
XX_FORMAT_READER_PROBE(adlib_mkj, XX_FILE_TYPE_ADLIB_MKJ,
    (header_size >= 6 && !xx_rt_memcmp(header, "\x4d\x4b\x4a\x61\x6d\x7a", 6)))
XX_FORMAT_READER_PROBE(adlib_sa2, XX_FILE_TYPE_ADLIB_SA2,
    (header_size >= 5 && !xx_rt_memcmp(header, "\x53\x41\x64\x54\x09", 5)))
XX_FORMAT_READER_PROBE(faust_fmc, XX_FILE_TYPE_FAUST_FMC,
    (header_size >= 4 && !xx_rt_memcmp(header, "\x46\x4d\x43\x21", 4)))
XX_FORMAT_READER_PROBE(softstar_rix, XX_FILE_TYPE_SOFTSTAR_RIX,
    (header_size >= 2 && !xx_rt_memcmp(header, "\xaa\x55", 2)))
XX_FORMAT_READER_PROBE(sequencing_abif, XX_FILE_TYPE_SEQUENCING_ABIF,
    (((header_size >= 4 && header[0] == 65 && header[1] == 66 && header[2] == 73 && header[3] == 70))))
XX_FORMAT_READER_PROBE(sequencing_scf, XX_FILE_TYPE_SEQUENCING_SCF,
    (((header_size >= 4 && header[0] == 46 && header[1] == 115 && header[2] == 99 && header[3] == 102))))
XX_FORMAT_READER_PROBE(genomics_sff, XX_FILE_TYPE_GENOMICS_SFF,
    (((header_size >= 4 && header[0] == 46 && header[1] == 115 && header[2] == 102 && header[3] == 102))))
XX_FORMAT_READER_PROBE(lut_spi1d, XX_FILE_TYPE_LUT_SPI1D,
    (((header_size >= 9 && header[0] == 86 && header[1] == 101 && header[2] == 114 && header[3] == 115 && header[4] == 105 && header[5] == 111 && header[6] == 110 && header[7] == 32 && header[8] == 49))))
XX_FORMAT_READER_PROBE(lut_spi3d, XX_FILE_TYPE_LUT_SPI3D,
    (((header_size >= 10 && header[0] == 83 && header[1] == 80 && header[2] == 73 && header[3] == 76 && header[4] == 85 && header[5] == 84 && header[6] == 32 && header[7] == 49 && header[8] == 46 && header[9] == 48))))
XX_FORMAT_READER_PROBE(lut_cinespace_csp, XX_FILE_TYPE_LUT_CINESPACE_CSP,
    (((header_size >= 10 && header[0] == 67 && header[1] == 83 && header[2] == 80 && header[3] == 76 && header[4] == 85 && header[5] == 84 && header[6] == 86 && header[7] == 49 && header[8] == 48 && header[9] == 48))))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(dds_rtps, XX_FILE_TYPE_DDS_RTPS,
    (header_size >= 24 && !xx_rt_memcmp(header,"RTPS",4)))
XX_FORMAT_READER_PROBE(ntlm_message, XX_FILE_TYPE_NTLM_MESSAGE,
    (header_size >= 32 && !xx_rt_memcmp(header,"NTLMSSP",7) && header[7] == 0))
XX_FORMAT_READER_PROBE(imagemagick_miff, XX_FILE_TYPE_IMAGEMAGICK_MIFF,
    (total_size >= 32 && header_size >= 14U && !xx_rt_memcmp(header,"\x69\x64\x3d\x49\x6d\x61\x67\x65\x4d\x61\x67\x69\x63\x6b",14U)))
XX_FORMAT_READER_PROBE(scanalytics_iplab, XX_FILE_TYPE_SCANALYTICS_IPLAB,
    (total_size >= 53 && header_size >= 16 && ((!xx_rt_memcmp(header,"iiii",4))||(!xx_rt_memcmp(header,"mmmm",4))) && !xx_rt_memcmp(header+8,"100fdata",8)))
XX_FORMAT_READER_PROBE(wordperfect_wpg, XX_FILE_TYPE_WORDPERFECT_WPG,
    (total_size >= 20 && header_size >= 4U && !xx_rt_memcmp(header,"\xff\x57\x50\x43",4U)))
XX_FORMAT_READER_PROBE(nasa_vicar, XX_FILE_TYPE_NASA_VICAR,
    (total_size >= 64 && header_size >= 8U && !xx_rt_memcmp(header,"\x4c\x42\x4c\x53\x49\x5a\x45\x3d",8U)))
XX_FORMAT_READER_PROBE(khoros_viff, XX_FILE_TYPE_KHOROS_VIFF,
    (total_size >= 1025 && header_size >= 8 && header[0]==171 && header[1]==1 && header[2]==1 && header[3]==3 && (header[4]==2||header[4]==4||header[4]==8) && header[5]==0 && header[6]==0 && header[7]==0))
XX_FORMAT_READER_PROBE(imagemagick_mvg, XX_FILE_TYPE_IMAGEMAGICK_MVG,
    (total_size >= 32 && header_size >= 20U && !xx_rt_memcmp(header,"\x70\x75\x73\x68\x20\x67\x72\x61\x70\x68\x69\x63\x2d\x63\x6f\x6e\x74\x65\x78\x74",20U)))
XX_FORMAT_READER_PROBE(motif_uil, XX_FILE_TYPE_MOTIF_UIL,
    (total_size >= 32 && header_size >= 9U && !xx_rt_memcmp(header,"\x2f\x2a\x20\x55\x49\x4c\x20\x2a\x2f",9U)))
XX_FORMAT_READER_PROBE(openusd_usda, XX_FILE_TYPE_OPENUSD_USDA,
    (total_size >= 16 && header_size >= 9U && !xx_rt_memcmp(header,"\x23\x75\x73\x64\x61\x20\x31\x2e\x30",9U)))
XX_FORMAT_READER_PROBE(adlib_sop, XX_FILE_TYPE_ADLIB_SOP,
    ((header_size>=7 && header[0]==115 && header[1]==111 && header[2]==112 && header[3]==101 && header[4]==112 && header[5]==111 && header[6]==115)))
XX_FORMAT_READER_PROBE(cudfm_cff, XX_FILE_TYPE_CUDFM_CFF,
    ((header_size>=16 && header[0]==60 && header[1]==67 && header[2]==85 && header[3]==68 && header[4]==45 && header[5]==70 && header[6]==77 && header[7]==45 && header[8]==70 && header[9]==105 && header[10]==108 && header[11]==101 && header[12]==62 && header[13]==26 && header[14]==222 && header[15]==224)))
XX_FORMAT_READER_PROBE(adlib_jbm, XX_FILE_TYPE_ADLIB_JBM,
    ((header_size>=32 && header[0]==2 && header[1]==0)))
XX_FORMAT_READER_PROBE(ceres_msc, XX_FILE_TYPE_CERES_MSC,
    ((header_size>=16 && header[0]==67 && header[1]==101 && header[2]==114 && header[3]==101 && header[4]==115 && header[5]==32 && header[6]==19 && header[7]==32 && header[8]==77 && header[9]==83 && header[10]==67 && header[11]==112 && header[12]==108 && header[13]==97 && header[14]==121 && header[15]==32)))
XX_FORMAT_READER_PROBE(adlib_xsm, XX_FILE_TYPE_ADLIB_XSM,
    ((header_size>=6 && header[0]==111 && header[1]==102 && header[2]==84 && header[3]==65 && header[4]==90 && header[5]==33)))
XX_FORMAT_READER_PROBE(adlib_mtr, XX_FILE_TYPE_ADLIB_MTR,
    ((header_size>=10 && header[0]==77 && header[1]==84 && header[2]==82 && header[3]==65 && header[4]==67 && header[5]==75 && header[6]==32 && header[7]==78 && header[8]==67 && header[9]==32)))
XX_FORMAT_READER_PROBE(rdos_raw, XX_FILE_TYPE_RDOS_RAW,
    ((header_size>=8 && header[0]==82 && header[1]==65 && header[2]==87 && header[3]==65 && header[4]==68 && header[5]==65 && header[6]==84 && header[7]==65)))
XX_FORMAT_READER_PROBE(mad_tracker, XX_FILE_TYPE_MAD_TRACKER,
    ((header_size>=4 && header[0]==77 && header[1]==65 && header[2]==68 && header[3]==43)))
