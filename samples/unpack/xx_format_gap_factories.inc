/* SPDX-License-Identifier: MIT */
static Abstractformat *mk_nsis(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_nsis_create(d,b); }
static void rm_nsis(void *p) { xx_nsis_free((xx_nsis *)p); }
static Abstractformat *mk_nowcompress(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_nowcompress_create(d,b); }
static void rm_nowcompress(void *p) { xx_nowcompress_free((xx_nowcompress *)p); }
static Abstractformat *mk_cpm_crunch(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_cpm_crunch_create(d,b); }
static void rm_cpm_crunch(void *p) { xx_cpm_crunch_free((xx_cpm_crunch *)p); }
static Abstractformat *mk_amplus(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_amplus_create(d,b); }
static void rm_amplus(void *p) { xx_amplus_free((xx_amplus *)p); }
static Abstractformat *mk_warp(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_warp_create(d,b); }
static void rm_warp(void *p) { xx_warp_free((xx_warp *)p); }
static Abstractformat *mk_lhwarp(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_lhwarp_create(d,b); }
static void rm_lhwarp(void *p) { xx_lhwarp_free((xx_lhwarp *)p); }
static Abstractformat *mk_compdisk(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_compdisk_create(d,b); }
static void rm_compdisk(void *p) { xx_compdisk_free((xx_compdisk *)p); }
static Abstractformat *mk_arc_cbm(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_arc_cbm_create(d,b); }
static void rm_arc_cbm(void *p) { xx_arc_cbm_free((xx_arc_cbm *)p); }
static Abstractformat *mk_crunchdisk(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_crunchdisk_create(d,b); }
static void rm_crunchdisk(void *p) { xx_crunchdisk_free((xx_crunchdisk *)p); }
static Abstractformat *mk_lhf(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_lhf_create(d,b); }
static void rm_lhf(void *p) { xx_lhf_free((xx_lhf *)p); }
static Abstractformat *mk_shrink_cdaf(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_shrink_cdaf_create(d,b); }
static void rm_shrink_cdaf(void *p) { xx_shrink_cdaf_free((xx_shrink_cdaf *)p); }
static Abstractformat *mk_spack(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_spack_create(d,b); }
static void rm_spack(void *p) { xx_spack_free((xx_spack *)p); }
static Abstractformat *mk_pcompress_pack(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_pcompress_pack_create(d,b); }
static void rm_pcompress_pack(void *p) { xx_pcompress_pack_free((xx_pcompress_pack *)p); }
static Abstractformat *mk_balz(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_balz_create(d,b); }
static void rm_balz(void *p) { xx_balz_free((xx_balz *)p); }
static Abstractformat *mk_quad(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_quad_create(d,b); }
static void rm_quad(void *p) { xx_quad_free((xx_quad *)p); }
static Abstractformat *mk_paq8(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_paq8_create(d,b); }
static void rm_paq8(void *p) { xx_paq8_free((xx_paq8 *)p); }
static Abstractformat *mk_lrzip(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_lrzip_create(d,b); }
static void rm_lrzip(void *p) { xx_lrzip_free((xx_lrzip *)p); }
static Abstractformat *mk_grzip(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_grzip_create(d,b); }
static void rm_grzip(void *p) { xx_grzip_free((xx_grzip *)p); }
static Abstractformat *mk_lpaq1(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_lpaq1_create(d,b); }
static void rm_lpaq1(void *p) { xx_lpaq1_free((xx_lpaq1 *)p); }
static Abstractformat *mk_lpaq5(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_lpaq5_create(d,b); }
static void rm_lpaq5(void *p) { xx_lpaq5_free((xx_lpaq5 *)p); }
static Abstractformat *mk_savage(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_savage_create(d,b); }
static void rm_savage(void *p) { xx_savage_free((xx_savage *)p); }
static Abstractformat *mk_mxm_simplearc(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_mxm_simplearc_create(d,b); }
static void rm_mxm_simplearc(void *p) { xx_mxm_simplearc_free((xx_mxm_simplearc *)p); }
static Abstractformat *mk_sds_sfx(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_sds_sfx_create(d,b); }
static void rm_sds_sfx(void *p) { xx_sds_sfx_free((xx_sds_sfx *)p); }
static Abstractformat *mk_lhpak_sfx(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_lhpak_sfx_create(d,b); }
static void rm_lhpak_sfx(void *p) { xx_lhpak_sfx_free((xx_lhpak_sfx *)p); }
static Abstractformat *mk_lhsfx(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_lhsfx_create(d,b); }
static void rm_lhsfx(void *p) { xx_lhsfx_free((xx_lhsfx *)p); }
static Abstractformat *mk_s_omni(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_s_omni_create(d,b); }
static void rm_s_omni(void *p) { xx_s_omni_free((xx_s_omni *)p); }
static Abstractformat *mk_applelink_pe(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_applelink_pe_create(d,b); }
static void rm_applelink_pe(void *p) { xx_applelink_pe_free((xx_applelink_pe *)p); }
static Abstractformat *mk_trackstar(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_trackstar_create(d,b); }
static void rm_trackstar(void *p) { xx_trackstar_free((xx_trackstar *)p); }
static Abstractformat *mk_gutenberg(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_gutenberg_create(d,b); }
static void rm_gutenberg(void *p) { xx_gutenberg_free((xx_gutenberg *)p); }
static Abstractformat *mk_apple_rdos(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_apple_rdos_create(d,b); }
static void rm_apple_rdos(void *p) { xx_apple_rdos_free((xx_apple_rdos *)p); }
static Abstractformat *mk_amdos(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_amdos_create(d,b); }
static void rm_amdos(void *p) { xx_amdos_free((xx_amdos *)p); }
static Abstractformat *mk_ozdos(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_ozdos_create(d,b); }
static void rm_ozdos(void *p) { xx_ozdos_free((xx_ozdos *)p); }
static Abstractformat *mk_unidos(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_unidos_create(d,b); }
static void rm_unidos(void *p) { xx_unidos_free((xx_unidos *)p); }
static Abstractformat *mk_cffa(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_cffa_create(d,b); }
static void rm_cffa(void *p) { xx_cffa_free((xx_cffa *)p); }
static Abstractformat *mk_apple_dos_hybrid(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_apple_dos_hybrid_create(d,b); }
static void rm_apple_dos_hybrid(void *p) { xx_apple_dos_hybrid_free((xx_apple_dos_hybrid *)p); }
static Abstractformat *mk_dos_master(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_dos_master_create(d,b); }
static void rm_dos_master(void *p) { xx_dos_master_free((xx_dos_master *)p); }
static Abstractformat *mk_focusdrive(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_focusdrive_create(d,b); }
static void rm_focusdrive(void *p) { xx_focusdrive_free((xx_focusdrive *)p); }
static Abstractformat *mk_microdrive(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_microdrive_create(d,b); }
static void rm_microdrive(void *p) { xx_microdrive_free((xx_microdrive *)p); }
static Abstractformat *mk_mac_ts(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_mac_ts_create(d,b); }
static void rm_mac_ts(void *p) { xx_mac_ts_free((xx_mac_ts *)p); }
static Abstractformat *mk_pascal_profile_manager(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_pascal_profile_manager_create(d,b); }
static void rm_pascal_profile_manager(void *p) { xx_pascal_profile_manager_free((xx_pascal_profile_manager *)p); }
static Abstractformat *mk_lisa_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_lisa_fs_create(d,b); }
static void rm_lisa_fs(void *p) { xx_lisa_fs_free((xx_lisa_fs *)p); }
static Abstractformat *mk_opera_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_opera_fs_create(d,b); }
static void rm_opera_fs(void *p) { xx_opera_fs_free((xx_opera_fs *)p); }
static Abstractformat *mk_cdi_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_cdi_fs_create(d,b); }
static void rm_cdi_fs(void *p) { xx_cdi_fs_free((xx_cdi_fs *)p); }
static Abstractformat *mk_bsd_disklabel(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_bsd_disklabel_create(d,b); }
static void rm_bsd_disklabel(void *p) { xx_bsd_disklabel_free((xx_bsd_disklabel *)p); }
static Abstractformat *mk_apple_cassette(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_apple_cassette_create(d,b); }
static void rm_apple_cassette(void *p) { xx_apple_cassette_free((xx_apple_cassette *)p); }
static Abstractformat *mk_lisa_blu(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_lisa_blu_create(d,b); }
static void rm_lisa_blu(void *p) { xx_lisa_blu_free((xx_lisa_blu *)p); }
static Abstractformat *mk_blindwrite4(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_blindwrite4_create(d,b); }
static void rm_blindwrite4(void *p) { xx_blindwrite4_free((xx_blindwrite4 *)p); }
static Abstractformat *mk_ciscopy(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_ciscopy_create(d,b); }
static void rm_ciscopy(void *p) { xx_ciscopy_free((xx_ciscopy *)p); }
static Abstractformat *mk_copytape(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_copytape_create(d,b); }
static void rm_copytape(void *p) { xx_copytape_free((xx_copytape *)p); }
static Abstractformat *mk_wc_disk_image(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_wc_disk_image_create(d,b); }
static void rm_wc_disk_image(void *p) { xx_wc_disk_image_free((xx_wc_disk_image *)p); }
static Abstractformat *mk_dri_diskcopy(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_dri_diskcopy_create(d,b); }
static void rm_dri_diskcopy(void *p) { xx_dri_diskcopy_free((xx_dri_diskcopy *)p); }
static Abstractformat *mk_maxi_disk(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_maxi_disk_create(d,b); }
static void rm_maxi_disk(void *p) { xx_maxi_disk_free((xx_maxi_disk *)p); }
static Abstractformat *mk_ray_dim(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_ray_dim_create(d,b); }
static void rm_ray_dim(void *p) { xx_ray_dim_free((xx_ray_dim *)p); }
static Abstractformat *mk_rs_ide(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_rs_ide_create(d,b); }
static void rm_rs_ide(void *p) { xx_rs_ide_free((xx_rs_ide *)p); }
static Abstractformat *mk_t98_hdd(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_t98_hdd_create(d,b); }
static void rm_t98_hdd(void *p) { xx_t98_hdd_free((xx_t98_hdd *)p); }
static Abstractformat *mk_pc_86f(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_pc_86f_create(d,b); }
static void rm_pc_86f(void *p) { xx_pc_86f_free((xx_pc_86f *)p); }
static Abstractformat *mk_fdx68_fdx(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_fdx68_fdx_create(d,b); }
static void rm_fdx68_fdx(void *p) { xx_fdx68_fdx_free((xx_fdx68_fdx *)p); }
static Abstractformat *mk_heathkit_h17(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_heathkit_h17_create(d,b); }
static void rm_heathkit_h17(void *p) { xx_heathkit_h17_free((xx_heathkit_h17 *)p); }
static Abstractformat *mk_bochs_growing(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_bochs_growing_create(d,b); }
static void rm_bochs_growing(void *p) { xx_bochs_growing_free((xx_bochs_growing *)p); }
static Abstractformat *mk_hxc_logic_analyzer(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_hxc_logic_analyzer_create(d,b); }
static void rm_hxc_logic_analyzer(void *p) { xx_hxc_logic_analyzer_free((xx_hxc_logic_analyzer *)p); }
static Abstractformat *mk_micral_n_raw(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_micral_n_raw_create(d,b); }
static void rm_micral_n_raw(void *p) { xx_micral_n_raw_free((xx_micral_n_raw *)p); }
static Abstractformat *mk_android_lp(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_android_lp_create(d,b); }
static void rm_android_lp(void *p) { xx_android_lp_free((xx_android_lp *)p); }
static Abstractformat *mk_lvm2(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_lvm2_create(d,b); }
static void rm_lvm2(void *p) { xx_lvm2_free((xx_lvm2 *)p); }
static Abstractformat *mk_coff_object(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_coff_object_create(d,b); }
static void rm_coff_object(void *p) { xx_coff_object_free((xx_coff_object *)p); }
static Abstractformat *mk_te_executable(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_te_executable_create(d,b); }
static void rm_te_executable(void *p) { xx_te_executable_free((xx_te_executable *)p); }
static Abstractformat *mk_atheos_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_atheos_fs_create(d,b); }
static void rm_atheos_fs(void *p) { xx_atheos_fs_free((xx_atheos_fs *)p); }
static Abstractformat *mk_beos_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_beos_fs_create(d,b); }
static void rm_beos_fs(void *p) { xx_beos_fs_free((xx_beos_fs *)p); }
static Abstractformat *mk_coherent_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_coherent_fs_create(d,b); }
static void rm_coherent_fs(void *p) { xx_coherent_fs_free((xx_coherent_fs *)p); }
static Abstractformat *mk_ods2_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_ods2_fs_create(d,b); }
static void rm_ods2_fs(void *p) { xx_ods2_fs_free((xx_ods2_fs *)p); }
static Abstractformat *mk_rt11_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_rt11_fs_create(d,b); }
static void rm_rt11_fs(void *p) { xx_rt11_fs_free((xx_rt11_fs *)p); }
static Abstractformat *mk_ecma67_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_ecma67_fs_create(d,b); }
static void rm_ecma67_fs(void *p) { xx_ecma67_fs_free((xx_ecma67_fs *)p); }
static Abstractformat *mk_f2fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_f2fs_create(d,b); }
static void rm_f2fs(void *p) { xx_f2fs_free((xx_f2fs *)p); }
static Abstractformat *mk_fossil_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_fossil_fs_create(d,b); }
static void rm_fossil_fs(void *p) { xx_fossil_fs_free((xx_fossil_fs *)p); }
static Abstractformat *mk_hammer_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_hammer_fs_create(d,b); }
static void rm_hammer_fs(void *p) { xx_hammer_fs_free((xx_hammer_fs *)p); }
static Abstractformat *mk_hpofs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_hpofs_create(d,b); }
static void rm_hpofs(void *p) { xx_hpofs_free((xx_hpofs *)p); }
static Abstractformat *mk_hp_lif(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_hp_lif_create(d,b); }
static void rm_hp_lif(void *p) { xx_hp_lif_free((xx_hp_lif *)p); }
static Abstractformat *mk_jfs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_jfs_create(d,b); }
static void rm_jfs(void *p) { xx_jfs_free((xx_jfs *)p); }
static Abstractformat *mk_linux_ext(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_linux_ext_create(d,b); }
static void rm_linux_ext(void *p) { xx_linux_ext_free((xx_linux_ext *)p); }
static Abstractformat *mk_locus_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_locus_fs_create(d,b); }
static void rm_locus_fs(void *p) { xx_locus_fs_free((xx_locus_fs *)p); }
static Abstractformat *mk_hpfs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_hpfs_create(d,b); }
static void rm_hpfs(void *p) { xx_hpfs_free((xx_hpfs *)p); }
static Abstractformat *mk_refs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_refs_create(d,b); }
static void rm_refs(void *p) { xx_refs_free((xx_refs *)p); }
static Abstractformat *mk_nilfs2(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_nilfs2_create(d,b); }
static void rm_nilfs2(void *p) { xx_nilfs2_free((xx_nilfs2 *)p); }
static Abstractformat *mk_amiga_pfs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_amiga_pfs_create(d,b); }
static void rm_amiga_pfs(void *p) { xx_amiga_pfs_free((xx_amiga_pfs *)p); }
static Abstractformat *mk_qnx4(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_qnx4_create(d,b); }
static void rm_qnx4(void *p) { xx_qnx4_free((xx_qnx4 *)p); }
static Abstractformat *mk_reiserfs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_reiserfs_create(d,b); }
static void rm_reiserfs(void *p) { xx_reiserfs_free((xx_reiserfs *)p); }
static Abstractformat *mk_sgi_efs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_sgi_efs_create(d,b); }
static void rm_sgi_efs(void *p) { xx_sgi_efs_free((xx_sgi_efs *)p); }
static Abstractformat *mk_amiga_sfs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_amiga_sfs_create(d,b); }
static void rm_amiga_sfs(void *p) { xx_amiga_sfs_free((xx_amiga_sfs *)p); }
static Abstractformat *mk_solar_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_solar_fs_create(d,b); }
static void rm_solar_fs(void *p) { xx_solar_fs_free((xx_solar_fs *)p); }
static Abstractformat *mk_unicos_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_unicos_fs_create(d,b); }
static void rm_unicos_fs(void *p) { xx_unicos_fs_free((xx_unicos_fs *)p); }
static Abstractformat *mk_sysv_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_sysv_fs_create(d,b); }
static void rm_sysv_fs(void *p) { xx_sysv_fs_free((xx_sysv_fs *)p); }
static Abstractformat *mk_unix_v7(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_unix_v7_create(d,b); }
static void rm_unix_v7(void *p) { xx_unix_v7_free((xx_unix_v7 *)p); }
static Abstractformat *mk_unix_bfs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_unix_bfs_create(d,b); }
static void rm_unix_bfs(void *p) { xx_unix_bfs_free((xx_unix_bfs *)p); }
static Abstractformat *mk_vxfs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_vxfs_create(d,b); }
static void rm_vxfs(void *p) { xx_vxfs_free((xx_vxfs *)p); }
static Abstractformat *mk_vmfs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_vmfs_create(d,b); }
static void rm_vmfs(void *p) { xx_vmfs_free((xx_vmfs *)p); }
static Abstractformat *mk_xenix_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_xenix_fs_create(d,b); }
static void rm_xenix_fs(void *p) { xx_xenix_fs_free((xx_xenix_fs *)p); }
static Abstractformat *mk_xia_fs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_xia_fs_create(d,b); }
static void rm_xia_fs(void *p) { xx_xia_fs_free((xx_xia_fs *)p); }
static Abstractformat *mk_xfs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_xfs_create(d,b); }
static void rm_xfs(void *p) { xx_xfs_free((xx_xfs *)p); }
static Abstractformat *mk_zfs(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_zfs_create(d,b); }
static void rm_zfs(void *p) { xx_zfs_free((xx_zfs *)p); }
static Abstractformat *mk_gfs2(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_gfs2_create(d,b); }
static void rm_gfs2(void *p) { xx_gfs2_free((xx_gfs2 *)p); }
static Abstractformat *mk_acorn_partitions(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_acorn_partitions_create(d,b); }
static void rm_acorn_partitions(void *p) { xx_acorn_partitions_free((xx_acorn_partitions *)p); }
static Abstractformat *mk_apricot_partitions(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_apricot_partitions_create(d,b); }
static void rm_apricot_partitions(void *p) { xx_apricot_partitions_free((xx_apricot_partitions *)p); }
static Abstractformat *mk_atari_ahdi(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_atari_ahdi_create(d,b); }
static void rm_atari_ahdi(void *p) { xx_atari_ahdi_free((xx_atari_ahdi *)p); }
static Abstractformat *mk_dec_disklabel(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_dec_disklabel_create(d,b); }
static void rm_dec_disklabel(void *p) { xx_dec_disklabel_free((xx_dec_disklabel *)p); }
static Abstractformat *mk_dragonfly_disklabel(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_dragonfly_disklabel_create(d,b); }
static void rm_dragonfly_disklabel(void *p) { xx_dragonfly_disklabel_free((xx_dragonfly_disklabel *)p); }
static Abstractformat *mk_human68k_partitions(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_human68k_partitions_create(d,b); }
static void rm_human68k_partitions(void *p) { xx_human68k_partitions_free((xx_human68k_partitions *)p); }
static Abstractformat *mk_minix_partitions(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_minix_partitions_create(d,b); }
static void rm_minix_partitions(void *p) { xx_minix_partitions_free((xx_minix_partitions *)p); }
static Abstractformat *mk_pc98_partitions(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_pc98_partitions_create(d,b); }
static void rm_pc98_partitions(void *p) { xx_pc98_partitions_free((xx_pc98_partitions *)p); }
static Abstractformat *mk_next_disklabel(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_next_disklabel_create(d,b); }
static void rm_next_disklabel(void *p) { xx_next_disklabel_free((xx_next_disklabel *)p); }
static Abstractformat *mk_plan9_partitions(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_plan9_partitions_create(d,b); }
static void rm_plan9_partitions(void *p) { xx_plan9_partitions_free((xx_plan9_partitions *)p); }
static Abstractformat *mk_rio_karma_partitions(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_rio_karma_partitions_create(d,b); }
static void rm_rio_karma_partitions(void *p) { xx_rio_karma_partitions_free((xx_rio_karma_partitions *)p); }
static Abstractformat *mk_sgi_volume_header(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_sgi_volume_header_create(d,b); }
static void rm_sgi_volume_header(void *p) { xx_sgi_volume_header_free((xx_sgi_volume_header *)p); }
static Abstractformat *mk_xbox360_layout(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_xbox360_layout_create(d,b); }
static void rm_xbox360_layout(void *p) { xx_xbox360_layout_free((xx_xbox360_layout *)p); }
static Abstractformat *mk_xenix_partitions(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_xenix_partitions_create(d,b); }
static void rm_xenix_partitions(void *p) { xx_xenix_partitions_free((xx_xenix_partitions *)p); }
static Abstractformat *mk_aff(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_aff_create(d,b); }
static void rm_aff(void *p) { xx_aff_free((xx_aff *)p); }
static Abstractformat *mk_md_raid(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_md_raid_create(d,b); }
static void rm_md_raid(void *p) { xx_md_raid_free((xx_md_raid *)p); }
static Abstractformat *mk_windows_ldm(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_windows_ldm_create(d,b); }
static void rm_windows_ldm(void *p) { xx_windows_ldm_free((xx_windows_ldm *)p); }
static Abstractformat *mk_parsec_dtc(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_parsec_dtc_create(d,b); }
static void rm_parsec_dtc(void *p) { xx_parsec_dtc_free((xx_parsec_dtc *)p); }
static Abstractformat *mk_parsec_dma(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_parsec_dma_create(d,b); }
static void rm_parsec_dma(void *p) { xx_parsec_dma_free((xx_parsec_dma *)p); }
static Abstractformat *mk_palladix_mus(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_palladix_mus_create(d,b); }
static void rm_palladix_mus(void *p) { xx_palladix_mus_free((xx_palladix_mus *)p); }
static Abstractformat *mk_palladix_snd(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_palladix_snd_create(d,b); }
static void rm_palladix_snd(void *p) { xx_palladix_snd_free((xx_palladix_snd *)p); }
static Abstractformat *mk_palladix_pma(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_palladix_pma_create(d,b); }
static void rm_palladix_pma(void *p) { xx_palladix_pma_free((xx_palladix_pma *)p); }
static Abstractformat *mk_mdh(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_mdh_create(d,b); }
static void rm_mdh(void *p) { xx_mdh_free((xx_mdh *)p); }
static Abstractformat *mk_sm8(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_sm8_create(d,b); }
static void rm_sm8(void *p) { xx_sm8_free((xx_sm8 *)p); }
static Abstractformat *mk_der(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_der_create(d,b); }
static void rm_der(void *p) { xx_der_free((xx_der *)p); }
static Abstractformat *mk_apks(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_apks_create(d,b); }
static void rm_apks(void *p) { xx_apks_free((xx_apks *)p); }
static Abstractformat *mk_text(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_text_create(d,b); }
static void rm_text(void *p) { xx_text_free((xx_text *)p); }
static Abstractformat *mk_machofat(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_machofat_create(d,b); }
static void rm_machofat(void *p) { xx_machofat_free((xx_machofat *)p); }
static Abstractformat *mk_cfbf(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_cfbf_create(d,b); }
static void rm_cfbf(void *p) { xx_cfbf_free((xx_cfbf *)p); }
static Abstractformat *mk_rzip(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_rzip_create(d,b); }
static void rm_rzip(void *p) { xx_rzip_free((xx_rzip *)p); }
static Abstractformat *mk_chz(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_chz_create(d,b); }
static void rm_chz(void *p) { xx_chz_free((xx_chz *)p); }
static Abstractformat *mk_ecm(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_ecm_create(d,b); }
static void rm_ecm(void *p) { xx_ecm_free((xx_ecm *)p); }
static Abstractformat *mk_he_tlkb(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_he_tlkb_create(d,b); }
static void rm_he_tlkb(void *p) { xx_he_tlkb_free((xx_he_tlkb *)p); }
static Abstractformat *mk_sar(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_sar_create(d,b); }
static void rm_sar(void *p) { xx_sar_free((xx_sar *)p); }
static Abstractformat *mk_is_stored(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_is_stored_create(d,b); }
static void rm_is_stored(void *p) { xx_is_stored_free((xx_is_stored *)p); }
static Abstractformat *mk_larc_pfx(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_larc_pfx_create(d,b); }
static void rm_larc_pfx(void *p) { xx_larc_pfx_free((xx_larc_pfx *)p); }
static Abstractformat *mk_wii_wad(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_wii_wad_create(d,b); }
static void rm_wii_wad(void *p) { xx_wii_wad_free((xx_wii_wad *)p); }
static Abstractformat *mk_binsh_sfx(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_binsh_sfx_create(d,b); }
static void rm_binsh_sfx(void *p) { xx_binsh_sfx_free((xx_binsh_sfx *)p); }
static Abstractformat *mk_instalit_data(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_instalit_data_create(d,b); }
static void rm_instalit_data(void *p) { xx_instalit_data_free((xx_instalit_data *)p); }
