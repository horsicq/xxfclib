/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Reader includes, wrappers and table are generated. See README.md.
 *
 * One entry per reader the library ships. xxfclib constructs readers by
 * name and has no "open whatever this is" entry point, so a program that
 * wants to open an arbitrary file has to carry a table like this one.
 *
 * The file type each reader answers to is learned at run time rather than
 * written down here: a reader is created once against an empty device and
 * asked. A hard-coded name-to-enum mapping would be wrong for the readers
 * whose enumerator is spelled differently from their directory, and would
 * go stale as readers are added.
 */

#include "xxfc_readers.h"
#include <ctype.h>
#include <string.h>
#include <xxfclib/formats/mozilla_mar/xx_mozilla_mar.h>
#include <xxfclib/formats/westwood_pak/xx_westwood_pak.h>
#include <xxfclib/formats/fatx/xx_fatx.h>
#include <xxfclib/formats/soundfont2/xx_soundfont2.h>
#include <xxfclib/formats/ivf/xx_ivf.h>
#include <xxfclib/formats/windows_ani/xx_windows_ani.h>
#include <xxfclib/formats/interplay_acm/xx_interplay_acm.h>
#include <xxfclib/formats/cri_ahx/xx_cri_ahx.h>
#include <xxfclib/formats/adobe_director_cxt/xx_adobe_director_cxt.h>
#include <xxfclib/formats/olympus_dss/xx_olympus_dss.h>
#include <xxfclib/formats/ea_exa/xx_ea_exa.h>
#include <xxfclib/formats/audio_nitro_strm/xx_audio_nitro_strm.h>
#include <xxfclib/formats/audio_wwise_wem/xx_audio_wwise_wem.h>
#include <xxfclib/formats/audio_scumm_sou/xx_audio_scumm_sou.h>
#include <xxfclib/formats/audio_riff_ima/xx_audio_riff_ima.h>
#include <xxfclib/formats/hmi_midi/xx_hmi_midi.h>
#include <xxfclib/formats/ensoniq_paf/xx_ensoniq_paf.h>
#include <xxfclib/formats/abylight_strm/xx_abylight_strm.h>
#include <xxfclib/formats/lego_alp/xx_lego_alp.h>
#include <xxfclib/formats/audio_pvf/xx_audio_pvf.h>
#include <xxfclib/formats/audio_rifx_wave/xx_audio_rifx_wave.h>
#include <xxfclib/formats/audio_shockwave_swa/xx_audio_shockwave_swa.h>
#include "xxfclib/formats/apple_pascal/xx_apple_pascal.h"
#include "xxfclib/formats/dmk/xx_dmk.h"
#include "xxfclib/formats/mfs/xx_mfs.h"
#include "xxfclib/formats/hfs/xx_hfs.h"
#include "xxfclib/formats/catsystem_kif/xx_catsystem_kif.h"
#include "xxfclib/formats/malie_lib/xx_malie_lib.h"
#include "xxfclib/formats/nexas_pac/xx_nexas_pac.h"
#include "xxfclib/formats/nitroplus_npa/xx_nitroplus_npa.h"
#include "xxfclib/formats/cpm/xx_cpm.h"
#include "xxfclib/formats/cpm/xx_cpm_presets.h"
#include "xxfclib/formats/ufs1/xx_ufs1.h"
#include "xxfclib/formats/apple_dos33/xx_apple_dos33.h"
#include "xxfclib/formats/nitroplus_npk2/xx_nitroplus_npk2.h"
#include "xxfclib/formats/thomson_sap/xx_thomson_sap.h"
#include "xxfclib/formats/anex86_hdi/xx_anex86_hdi.h"
#include "xxfclib/formats/exfat/xx_exfat.h"
#include "xxfclib/formats/majiro/xx_majiro.h"
#include "xxfclib/formats/wux/xx_wux.h"
#include "xxfclib/formats/sdi/xx_sdi.h"
#include "xxfclib/formats/nhd/xx_nhd.h"
#include "xxfclib/formats/virtual98/xx_virtual98.h"
#include "xxfclib/formats/xva/xx_xva.h"
#include "xxfclib/formats/qlie_pack/xx_qlie_pack.h"
#include "xxfclib/formats/hfsplus/xx_hfsplus.h"
#include "xxfclib/formats/partimage/xx_partimage.h"
#include "xxfclib/formats/aaruformat/xx_aaruformat.h"
#include "xxfclib/formats/acorn_adfs/xx_acorn_adfs.h"
#include "xxfclib/formats/gdi/xx_gdi.h"
#include "xxfclib/formats/mds/xx_mds.h"
#include "xxfclib/formats/cbm_d64/xx_cbm_d64.h"
#include "xxfclib/formats/cbm_d71/xx_cbm_d71.h"
#include "xxfclib/formats/cbm_d81/xx_cbm_d81.h"
#include "xxfclib/formats/ccd/xx_ccd.h"
#include "xxfclib/formats/cdrdao_toc/xx_cdrdao_toc.h"
#include "xxfclib/formats/diskcopy42/xx_diskcopy42.h"
#include "xxfclib/formats/acorn_dfs/xx_acorn_dfs.h"
#include "xxfclib/formats/fdcopy_cfi/xx_fdcopy_cfi.h"
#include "xxfclib/formats/atari_dos2/xx_atari_dos2.h"
#include "xxfclib/formats/apridisk/xx_apridisk.h"
#include "xxfclib/formats/ti99_dsk/xx_ti99_dsk.h"
#include "xxfclib/formats/apple_dos32/xx_apple_dos32.h"
#include "xxfclib/formats/apple_dos33_32/xx_apple_dos33_32.h"
#include "xxfclib/formats/cbm_d8x/xx_cbm_d8x.h"
#include "xxfclib/formats/cbm_d67/xx_cbm_d67.h"
#include "xxfclib/formats/cbm_d90/xx_cbm_d90.h"
#include "xxfclib/formats/libdsk_extra/xx_libdsk_extra.h"
#include "xxfclib/formats/simh_disk/xx_simh_disk.h"
#include "xxfclib/formats/snatchit_cp2/xx_snatchit_cp2.h"
#include "xxfclib/formats/northstar_nsi/xx_northstar_nsi.h"
#include "xxfclib/formats/thomson_fd/xx_thomson_fd.h"
#include "xxfclib/formats/cmd_fd/xx_cmd_fd.h"
#include "xxfclib/formats/pce_pri/xx_pce_pri.h"
#include "xxfclib/formats/pce_pfi/xx_pce_pfi.h"
#include "xxfclib/formats/pce_pfdc/xx_pce_pfdc.h"
#include "xxfclib/formats/pce_pbi/xx_pce_pbi.h"
#include "xxfclib/formats/pce_pbit/xx_pce_pbit.h"
#include "xxfclib/formats/pce_tc/xx_pce_tc.h"
#include "xxfclib/formats/pce_anadisk/xx_pce_anadisk.h"
#include "xxfclib/formats/pce_xdf/xx_pce_xdf.h"
#include "xxfclib/formats/os9_rbf/xx_os9_rbf.h"
#include "xxfclib/formats/ldbs/xx_ldbs.h"
#include "xxfclib/formats/ldbst/xx_ldbst.h"
#include "xxfclib/formats/bytekiller/xx_bytekiller.h"

#include <xxfclib/formats/apple_disk_copy_6_ndif_image/xx_apple_disk_copy_6_ndif_image.h>
#include <xxfclib/formats/apple_sparse_bundle/xx_apple_sparse_bundle.h>
#include <xxfclib/formats/encrypted_apple_disk_image/xx_encrypted_apple_disk_image.h>
#include <xxfclib/formats/hxc_stream_hfe/xx_hxc_stream_hfe.h>
#include <xxfclib/formats/ms_dos_backup/xx_ms_dos_backup.h>
#include <xxfclib/formats/nec_pc_98_fdi/xx_nec_pc_98_fdi.h>
#include <xxfclib/formats/ns2/xx_ns2.h>
#include <xxfclib/formats/nsa/xx_nsa.h>
#include <xxfclib/formats/qcow1/xx_qcow1.h>
#include <xxfclib/formats/qnap_nas_firmware/xx_qnap_nas_firmware.h>
#include <xxfclib/formats/raw_deflate_compressed_data/xx_raw_deflate_compressed_data.h>
#include <xxfclib/formats/rdb/xx_rdb.h>
#include <xxfclib/formats/sfx_analogx_emucore_ffs/xx_sfx_analogx_emucore_ffs.h>
#include <xxfclib/formats/sfx_krzip/xx_sfx_krzip.h>
#include <xxfclib/formats/sfx_warpin_package/xx_sfx_warpin_package.h>
#include <xxfclib/formats/sfx_hci_instalit/xx_sfx_hci_instalit.h>
#include <xxfclib/formats/sfx_clickteam_multimedia_fusion/xx_sfx_clickteam_multimedia_fusion.h>
#include <xxfclib/formats/sfx_abbyy_fine_objects/xx_sfx_abbyy_fine_objects.h>
#include <xxfclib/formats/sfx_flashjester_jugglor/xx_sfx_flashjester_jugglor.h>
#include <xxfclib/formats/sfx_jgsoft_deploymaster_package/xx_sfx_jgsoft_deploymaster_package.h>
#include <xxfclib/formats/sfx_ardi_diskette_image/xx_sfx_ardi_diskette_image.h>
#include <xxfclib/formats/sfx_nullsoft_pimp/xx_sfx_nullsoft_pimp.h>
#include <xxfclib/formats/sfx_sydex_diskette_image/xx_sfx_sydex_diskette_image.h>
#include <xxfclib/formats/sfx_compaq_softpaq/xx_sfx_compaq_softpaq.h>
#include <xxfclib/formats/sfx_softpaq4/xx_sfx_softpaq4.h>
#include <xxfclib/formats/sfx_wasp_windows_auto/xx_sfx_wasp_windows_auto.h>
#include <xxfclib/formats/stuffit_split_file/xx_stuffit_split_file.h>
#include <xxfclib/formats/t98_next_nfd/xx_t98_next_nfd.h>
#include <xxfclib/formats/trs_80_jv1/xx_trs_80_jv1.h>
#include <xxfclib/formats/trs_80_jv3/xx_trs_80_jv3.h>
#include <xxfclib/formats/uharc/xx_uharc.h>
#include <xxfclib/formats/visionaire_studio_vis/xx_visionaire_studio_vis.h>
#include <xxfclib/formats/wise_installation_system/xx_wise_installation_system.h>
#include <xxfclib/formats/eschalon_setup_epsf/xx_eschalon_setup_epsf.h>
#include <xxfclib/formats/gentee_installer/xx_gentee_installer.h>
#include <xxfclib/formats/clickteam_install_creator/xx_clickteam_install_creator.h>
#include <xxfclib/formats/createinstall_instcrin_extractor/xx_createinstall_instcrin_extractor.h>
#include <xxfclib/formats/sfxstart/xx_sfxstart.h>
#include <xxfclib/formats/modbus_tcp/xx_modbus_tcp.h>
#include <xxfclib/formats/someip_message/xx_someip_message.h>
#include <xxfclib/formats/dds_rtps/xx_dds_rtps.h>
#include <xxfclib/formats/rip_message/xx_rip_message.h>
#include <xxfclib/formats/vrrp_message/xx_vrrp_message.h>
#include <xxfclib/formats/igmp_message/xx_igmp_message.h>
#include <xxfclib/formats/pim_message/xx_pim_message.h>
#include <xxfclib/formats/ldp_message/xx_ldp_message.h>
#include <xxfclib/formats/gre_packet/xx_gre_packet.h>
#include <xxfclib/formats/l2tp_packet/xx_l2tp_packet.h>
#include <xxfclib/formats/lldp_message/xx_lldp_message.h>
#include <xxfclib/formats/netflow_datagram/xx_netflow_datagram.h>
#include <xxfclib/formats/ntlm_message/xx_ntlm_message.h>
#include <xxfclib/formats/dcerpc_pdu/xx_dcerpc_pdu.h>
#include <xxfclib/formats/ethereum_rlp/xx_ethereum_rlp.h>
#include <xxfclib/formats/imagemagick_miff/xx_imagemagick_miff.h>
#include <xxfclib/formats/avs_image/xx_avs_image.h>
#include <xxfclib/formats/scanalytics_iplab/xx_scanalytics_iplab.h>
#include <xxfclib/formats/mtv_image/xx_mtv_image.h>
#include <xxfclib/formats/nokia_ota_bitmap/xx_nokia_ota_bitmap.h>
#include <xxfclib/formats/apple_pict/xx_apple_pict.h>
#include <xxfclib/formats/wordperfect_wpg/xx_wordperfect_wpg.h>
#include <xxfclib/formats/nasa_vicar/xx_nasa_vicar.h>
#include <xxfclib/formats/khoros_viff/xx_khoros_viff.h>
#include <xxfclib/formats/imagemagick_mvg/xx_imagemagick_mvg.h>
#include <xxfclib/formats/motif_uil/xx_motif_uil.h>
#include <xxfclib/formats/iges_model/xx_iges_model.h>
#include <xxfclib/formats/openusd_usda/xx_openusd_usda.h>
#include <xxfclib/formats/ufo_glif/xx_ufo_glif.h>
#include <xxfclib/formats/unifont_hex/xx_unifont_hex.h>
#include <xxfclib/formats/adlib_sop/xx_adlib_sop.h>
#include <xxfclib/formats/cudfm_cff/xx_cudfm_cff.h>
#include <xxfclib/formats/adlib_jbm/xx_adlib_jbm.h>
#include <xxfclib/formats/ceres_msc/xx_ceres_msc.h>
#include <xxfclib/formats/adlib_xsm/xx_adlib_xsm.h>
#include <xxfclib/formats/ken_ksm/xx_ken_ksm.h>
#include <xxfclib/formats/implay_music/xx_implay_music.h>
#include <xxfclib/formats/adlib_mtr/xx_adlib_mtr.h>
#include <xxfclib/formats/rdos_raw/xx_rdos_raw.h>
#include <xxfclib/formats/mad_tracker/xx_mad_tracker.h>
#include <xxfclib/formats/vasp_poscar/xx_vasp_poscar.h>
#include <xxfclib/formats/quantum_espresso_input/xx_quantum_espresso_input.h>
#include <xxfclib/formats/cp2k_input/xx_cp2k_input.h>
#include <xxfclib/formats/nwchem_input/xx_nwchem_input.h>
#include <xxfclib/formats/gamess_input/xx_gamess_input.h>
#include <xxfclib/formats/gaussian_input/xx_gaussian_input.h>
#include <xxfclib/formats/abinit_input/xx_abinit_input.h>
#include <xxfclib/formats/aims_geometry/xx_aims_geometry.h>
#include <xxfclib/formats/orca_input/xx_orca_input.h>
#include <xxfclib/formats/demon_input/xx_demon_input.h>
#include <xxfclib/formats/ethernet_frame/xx_ethernet_frame.h>
#include <xxfclib/formats/ip_packet/xx_ip_packet.h>
#include <xxfclib/formats/arp_packet/xx_arp_packet.h>
#include <xxfclib/formats/icmp_message/xx_icmp_message.h>
#include <xxfclib/formats/sip_message/xx_sip_message.h>
#include <xxfclib/formats/rtsp_message/xx_rtsp_message.h>
#include <xxfclib/formats/diameter_message/xx_diameter_message.h>
#include <xxfclib/formats/tacacs_packet/xx_tacacs_packet.h>
#include <xxfclib/formats/gtp_message/xx_gtp_message.h>
#include <xxfclib/formats/pfcp_message/xx_pfcp_message.h>
#include <xxfclib/formats/pptp_message/xx_pptp_message.h>
#include <xxfclib/formats/rsvp_message/xx_rsvp_message.h>
#include <xxfclib/formats/age_encrypted/xx_age_encrypted.h>
#include <xxfclib/formats/kerberos_ccache/xx_kerberos_ccache.h>
#include <xxfclib/formats/jose_jws/xx_jose_jws.h>
#include <xxfclib/formats/wbmp_image/xx_wbmp_image.h>
#include <xxfclib/formats/dec_sixel/xx_dec_sixel.h>
#include <xxfclib/formats/palm_bitmap/xx_palm_bitmap.h>
#include <xxfclib/formats/adobe_acv/xx_adobe_acv.h>
#include <xxfclib/formats/adobe_act/xx_adobe_act.h>
#include <xxfclib/formats/ogre_skeleton/xx_ogre_skeleton.h>
#include <xxfclib/formats/cal3d_skeleton/xx_cal3d_skeleton.h>
#include <xxfclib/formats/collada_dae/xx_collada_dae.h>
#include <xxfclib/formats/lightwave_scene/xx_lightwave_scene.h>
#include <xxfclib/formats/dsn6_density/xx_dsn6_density.h>
#include <xxfclib/formats/crystallography_mtz/xx_crystallography_mtz.h>
#include <xxfclib/formats/amira_mesh/xx_amira_mesh.h>
#include <xxfclib/formats/tetgen_mesh/xx_tetgen_mesh.h>
#include <xxfclib/formats/jedec_fuse/xx_jedec_fuse.h>
#include <xxfclib/formats/qchem_input/xx_qchem_input.h>
#include <xxfclib/formats/adlib_bam/xx_adlib_bam.h>
#include <xxfclib/formats/adlib_bmf/xx_adlib_bmf.h>
#include <xxfclib/formats/creative_cmf/xx_creative_cmf.h>
#include <xxfclib/formats/adlib_dfm/xx_adlib_dfm.h>
#include <xxfclib/formats/adlib_lds/xx_adlib_lds.h>
#include <xxfclib/formats/adlib_mkj/xx_adlib_mkj.h>
#include <xxfclib/formats/adlib_rol/xx_adlib_rol.h>
#include <xxfclib/formats/adlib_sa2/xx_adlib_sa2.h>
#include <xxfclib/formats/faust_fmc/xx_faust_fmc.h>
#include <xxfclib/formats/softstar_rix/xx_softstar_rix.h>
#include <xxfclib/formats/genomics_bed/xx_genomics_bed.h>
#include <xxfclib/formats/genomics_wiggle/xx_genomics_wiggle.h>
#include <xxfclib/formats/genomics_gtf/xx_genomics_gtf.h>
#include <xxfclib/formats/genomics_agp/xx_genomics_agp.h>
#include <xxfclib/formats/sequencing_abif/xx_sequencing_abif.h>
#include <xxfclib/formats/sequencing_scf/xx_sequencing_scf.h>
#include <xxfclib/formats/genomics_sff/xx_genomics_sff.h>
#include <xxfclib/formats/lut_spi1d/xx_lut_spi1d.h>
#include <xxfclib/formats/lut_spi3d/xx_lut_spi3d.h>
#include <xxfclib/formats/lut_cinespace_csp/xx_lut_cinespace_csp.h>
#include <xxfclib/formats/ntp_message/xx_ntp_message.h>
#include <xxfclib/formats/rtp_rtcp/xx_rtp_rtcp.h>
#include <xxfclib/formats/bgp_messages/xx_bgp_messages.h>
#include <xxfclib/formats/ospf_packet/xx_ospf_packet.h>
#include <xxfclib/formats/sctp_packet/xx_sctp_packet.h>
#include <xxfclib/formats/isakmp_message/xx_isakmp_message.h>
#include <xxfclib/formats/ssh_transport/xx_ssh_transport.h>
#include <xxfclib/formats/smtp_transcript/xx_smtp_transcript.h>
#include <xxfclib/formats/pkcs8_private_key/xx_pkcs8_private_key.h>
#include <xxfclib/formats/putty_ppk/xx_putty_ppk.h>
#include <xxfclib/formats/openssh_certificate/xx_openssh_certificate.h>
#include <xxfclib/formats/safetensors/xx_safetensors.h>
#include <xxfclib/formats/gguf/xx_gguf.h>
#include <xxfclib/formats/cdb_database/xx_cdb_database.h>
#include <xxfclib/formats/stomp_frames/xx_stomp_frames.h>
#include <xxfclib/formats/fontforge_sfd/xx_fontforge_sfd.h>
#include <xxfclib/formats/grub_pff2/xx_grub_pff2.h>
#include <xxfclib/formats/opengex_model/xx_opengex_model.h>
#include <xxfclib/formats/bvh_motion/xx_bvh_motion.h>
#include <xxfclib/formats/directx_x/xx_directx_x.h>
#include <xxfclib/formats/gts_surface/xx_gts_surface.h>
#include <xxfclib/formats/medit_mesh/xx_medit_mesh.h>
#include <xxfclib/formats/gocad_model/xx_gocad_model.h>
#include <xxfclib/formats/nastran_bulk/xx_nastran_bulk.h>
#include <xxfclib/formats/abaqus_input/xx_abaqus_input.h>
#include <xxfclib/formats/ensight_gold_geometry/xx_ensight_gold_geometry.h>
#include <xxfclib/formats/gmv_mesh/xx_gmv_mesh.h>
#include <xxfclib/formats/usgs_dem/xx_usgs_dem.h>
#include <xxfclib/formats/dted_elevation/xx_dted_elevation.h>
#include <xxfclib/formats/mapinfo_mif/xx_mapinfo_mif.h>
#include <xxfclib/formats/tracker_coconizer/xx_tracker_coconizer.h>
#include <xxfclib/formats/tracker_real/xx_tracker_real.h>
#include <xxfclib/formats/tracker_megatracker/xx_tracker_megatracker.h>
#include <xxfclib/formats/amos_music_bank/xx_amos_music_bank.h>
#include <xxfclib/formats/adlib_rad/xx_adlib_rad.h>
#include <xxfclib/formats/adlib_amd/xx_adlib_amd.h>
#include <xxfclib/formats/adlib_hsc/xx_adlib_hsc.h>
#include <xxfclib/formats/adlib_d00/xx_adlib_d00.h>
#include <xxfclib/formats/adlib_bnk/xx_adlib_bnk.h>
#include <xxfclib/formats/dosbox_dro/xx_dosbox_dro.h>
#include <xxfclib/formats/genomics_genbank/xx_genomics_genbank.h>
#include <xxfclib/formats/genomics_embl/xx_genomics_embl.h>
#include <xxfclib/formats/genomics_swissprot/xx_genomics_swissprot.h>
#include <xxfclib/formats/alignment_clustal/xx_alignment_clustal.h>
#include <xxfclib/formats/alignment_stockholm/xx_alignment_stockholm.h>
#include <xxfclib/formats/alignment_phylip/xx_alignment_phylip.h>
#include <xxfclib/formats/alignment_maf/xx_alignment_maf.h>
#include <xxfclib/formats/alignment_mauve/xx_alignment_mauve.h>
#include <xxfclib/formats/ucsc_nib/xx_ucsc_nib.h>
#include <xxfclib/formats/assembly_gfa/xx_assembly_gfa.h>
#include <xxfclib/formats/http1_message/xx_http1_message.h>
#include <xxfclib/formats/websocket_frames/xx_websocket_frames.h>
#include <xxfclib/formats/coap_message/xx_coap_message.h>
#include <xxfclib/formats/stun_message/xx_stun_message.h>
#include <xxfclib/formats/dhcp_message/xx_dhcp_message.h>
#include <xxfclib/formats/radius_packet/xx_radius_packet.h>
#include <xxfclib/formats/snmp_message/xx_snmp_message.h>
#include <xxfclib/formats/ldap_message/xx_ldap_message.h>
#include <xxfclib/formats/tls_records/xx_tls_records.h>
#include <xxfclib/formats/jks_keystore/xx_jks_keystore.h>
#include <xxfclib/formats/java_serialization/xx_java_serialization.h>
#include <xxfclib/formats/x509_crl/xx_x509_crl.h>
#include <xxfclib/formats/ocsp_response/xx_ocsp_response.h>
#include <xxfclib/formats/lmdb_data/xx_lmdb_data.h>
#include <xxfclib/formats/gdbm_dump/xx_gdbm_dump.h>
#include <xxfclib/formats/adobe_acb/xx_adobe_acb.h>
#include <xxfclib/formats/jasc_palette/xx_jasc_palette.h>
#include <xxfclib/formats/x11_xbm/xx_x11_xbm.h>
#include <xxfclib/formats/jpeg2000_pgx/xx_jpeg2000_pgx.h>
#include <xxfclib/formats/amiga_diskobject/xx_amiga_diskobject.h>
#include <xxfclib/formats/tex_vf/xx_tex_vf.h>
#include <xxfclib/formats/esri_ascii_grid/xx_esri_ascii_grid.h>
#include <xxfclib/formats/surfer_grid/xx_surfer_grid.h>
#include <xxfclib/formats/gxf_grid/xx_gxf_grid.h>
#include <xxfclib/formats/ogc_wkt/xx_ogc_wkt.h>
#include <xxfclib/formats/step_part21/xx_step_part21.h>
#include <xxfclib/formats/gerber_rs274x/xx_gerber_rs274x.h>
#include <xxfclib/formats/excellon_drill/xx_excellon_drill.h>
#include <xxfclib/formats/vrml_scene/xx_vrml_scene.h>
#include <xxfclib/formats/renderman_rib/xx_renderman_rib.h>
#include <xxfclib/formats/asylum_amf/xx_asylum_amf.h>
#include <xxfclib/formats/tracker_stx/xx_tracker_stx.h>
#include <xxfclib/formats/tracker_dtm/xx_tracker_dtm.h>
#include <xxfclib/formats/tracker_soundfx/xx_tracker_soundfx.h>
#include <xxfclib/formats/tracker_funk/xx_tracker_funk.h>
#include <xxfclib/formats/tracker_archimedes/xx_tracker_archimedes.h>
#include <xxfclib/formats/pce_psi/xx_pce_psi.h>
#include <xxfclib/formats/pc98_d88/xx_pc98_d88.h>
#include <xxfclib/formats/hxc_mfm/xx_hxc_mfm.h>
#include <xxfclib/formats/x68000_dim/xx_x68000_dim.h>
#include <xxfclib/formats/xamarin_compressed_assembly/xx_xamarin_compressed_assembly.h>
#include <xxfclib/formats/yaze_ydsk/xx_yaze_ydsk.h>
#include <xxfclib/formats/lammps_data/xx_lammps_data.h>
#include <xxfclib/formats/lammps_dump/xx_lammps_dump.h>
#include <xxfclib/formats/shelx_res/xx_shelx_res.h>
#include <xxfclib/formats/turbomole_coord/xx_turbomole_coord.h>
#include <xxfclib/formats/charmm_crd/xx_charmm_crd.h>
#include <xxfclib/formats/castep_cell/xx_castep_cell.h>
#include <xxfclib/formats/crystal_fort34/xx_crystal_fort34.h>
#include <xxfclib/formats/siesta_xv/xx_siesta_xv.h>
#include <xxfclib/formats/harwell_boeing/xx_harwell_boeing.h>
#include <xxfclib/formats/openfoam_points/xx_openfoam_points.h>
#include <xxfclib/formats/minecraft_nbt/xx_minecraft_nbt.h>
#include <xxfclib/formats/amazon_ion_binary/xx_amazon_ion_binary.h>
#include <xxfclib/formats/leveldb_log/xx_leveldb_log.h>
#include <xxfclib/formats/dns_message/xx_dns_message.h>
#include <xxfclib/formats/rocksdb_blob/xx_rocksdb_blob.h>
#include <xxfclib/formats/mongodb_wire/xx_mongodb_wire.h>
#include <xxfclib/formats/redis_resp/xx_redis_resp.h>
#include <xxfclib/formats/mqtt_packets/xx_mqtt_packets.h>
#include <xxfclib/formats/amqp_frames/xx_amqp_frames.h>
#include <xxfclib/formats/thrift_compact/xx_thrift_compact.h>
#include <xxfclib/formats/x509_certificate/xx_x509_certificate.h>
#include <xxfclib/formats/pkcs10_csr/xx_pkcs10_csr.h>
#include <xxfclib/formats/pkcs12_pfx/xx_pkcs12_pfx.h>
#include <xxfclib/formats/openssh_private_key/xx_openssh_private_key.h>
#include <xxfclib/formats/kerberos_keytab/xx_kerberos_keytab.h>
#include <xxfclib/formats/gimp_gpl/xx_gimp_gpl.h>
#include <xxfclib/formats/gimp_ggr/xx_gimp_ggr.h>
#include <xxfclib/formats/iridas_cube_lut/xx_iridas_cube_lut.h>
#include <xxfclib/formats/hpgl_plot/xx_hpgl_plot.h>
#include <xxfclib/formats/paintshop_psp/xx_paintshop_psp.h>
#include <xxfclib/formats/photoshop_pat/xx_photoshop_pat.h>
#include <xxfclib/formats/mmd_pmx/xx_mmd_pmx.h>
#include <xxfclib/formats/metasequoia_mqo/xx_metasequoia_mqo.h>
#include <xxfclib/formats/calma_gdsii/xx_calma_gdsii.h>
#include <xxfclib/formats/autodesk_ase/xx_autodesk_ase.h>
#include <xxfclib/formats/freesurfer_surface/xx_freesurfer_surface.h>
#include <xxfclib/formats/gmsh_msh/xx_gmsh_msh.h>
#include <xxfclib/formats/netgen_vol/xx_netgen_vol.h>
#include <xxfclib/formats/font_afm/xx_font_afm.h>
#include <xxfclib/formats/tiled_tmx/xx_tiled_tmx.h>
#include <xxfclib/formats/nintendo_sdat/xx_nintendo_sdat.h>
#include <xxfclib/formats/sony_vab/xx_sony_vab.h>
#include <xxfclib/formats/yamaha_ym/xx_yamaha_ym.h>
#include <xxfclib/formats/zx_ayemul/xx_zx_ayemul.h>
#include <xxfclib/formats/dragon_vdk/xx_dragon_vdk.h>
#include <xxfclib/formats/apple_a2r/xx_apple_a2r.h>
#include <xxfclib/formats/atari_atr/xx_atari_atr.h>
#include <xxfclib/formats/atari_pasti_stx/xx_atari_pasti_stx.h>
#include <xxfclib/formats/amiga_ipf/xx_amiga_ipf.h>
#include <xxfclib/formats/tracker_dtt/xx_tracker_dtt.h>
#include <xxfclib/formats/gaussian_cube/xx_gaussian_cube.h>
#include <xxfclib/formats/molecule_xyz/xx_molecule_xyz.h>
#include <xxfclib/formats/mdl_molfile/xx_mdl_molfile.h>
#include <xxfclib/formats/tripos_mol2/xx_tripos_mol2.h>
#include <xxfclib/formats/xcrysden_xsf/xx_xcrysden_xsf.h>
#include <xxfclib/formats/amber_prmtop/xx_amber_prmtop.h>
#include <xxfclib/formats/amber_restart/xx_amber_restart.h>
#include <xxfclib/formats/gaussian_fchk/xx_gaussian_fchk.h>
#include <xxfclib/formats/jcamp_dx/xx_jcamp_dx.h>
#include <xxfclib/formats/dl_poly_config/xx_dl_poly_config.h>
#include <xxfclib/formats/nix_nar/xx_nix_nar.h>
#include <xxfclib/formats/redis_rdb/xx_redis_rdb.h>
#include <xxfclib/formats/postgres_custom/xx_postgres_custom.h>
#include <xxfclib/formats/mysql_binlog/xx_mysql_binlog.h>
#include <xxfclib/formats/kafka_record_batch/xx_kafka_record_batch.h>
#include <xxfclib/formats/android_binary_xml/xx_android_binary_xml.h>
#include <xxfclib/formats/android_resources_arsc/xx_android_resources_arsc.h>
#include <xxfclib/formats/msgpack/xx_msgpack.h>
#include <xxfclib/formats/ubjson/xx_ubjson.h>
#include <xxfclib/formats/bittorrent_metainfo/xx_bittorrent_metainfo.h>
#include <xxfclib/formats/erlang_external_term/xx_erlang_external_term.h>
#include <xxfclib/formats/capnproto_message/xx_capnproto_message.h>
#include <xxfclib/formats/dbus_message/xx_dbus_message.h>
#include <xxfclib/formats/windows_shell_link/xx_windows_shell_link.h>
#include <xxfclib/formats/pkcs7_cms/xx_pkcs7_cms.h>
#include <xxfclib/formats/wavefront_obj/xx_wavefront_obj.h>
#include <xxfclib/formats/off_mesh/xx_off_mesh.h>
#include <xxfclib/formats/ac3d_model/xx_ac3d_model.h>
#include <xxfclib/formats/qubicle_qb/xx_qubicle_qb.h>
#include <xxfclib/formats/terragen_ter/xx_terragen_ter.h>
#include <xxfclib/formats/gimp_xcf/xx_gimp_xcf.h>
#include <xxfclib/formats/photoshop_abr/xx_photoshop_abr.h>
#include <xxfclib/formats/softimage_pic/xx_softimage_pic.h>
#include <xxfclib/formats/alias_pix/xx_alias_pix.h>
#include <xxfclib/formats/qt_qpicture/xx_qt_qpicture.h>
#include <xxfclib/formats/font_type1_pfb/xx_font_type1_pfb.h>
#include <xxfclib/formats/font_gem_fnt/xx_font_gem_fnt.h>
#include <xxfclib/formats/tex_gf/xx_tex_gf.h>
#include <xxfclib/formats/bpg_image/xx_bpg_image.h>
#include <xxfclib/formats/mng_animation/xx_mng_animation.h>
#include <xxfclib/formats/atari_7800_a78/xx_atari_7800_a78.h>
#include <xxfclib/formats/commodore_pc64/xx_commodore_pc64.h>
#include <xxfclib/formats/atari_cas/xx_atari_cas.h>
#include <xxfclib/formats/msx_cas/xx_msx_cas.h>
#include <xxfclib/formats/oric_tap/xx_oric_tap.h>
#include <xxfclib/formats/dragon_cas/xx_dragon_cas.h>
#include <xxfclib/formats/amiga_ahx/xx_amiga_ahx.h>
#include <xxfclib/formats/amstrad_cpc_sna/xx_amstrad_cpc_sna.h>
#include <xxfclib/formats/vtech_vz/xx_vtech_vz.h>
#include <xxfclib/formats/zx_hobeta/xx_zx_hobeta.h>
#include <xxfclib/formats/genomics_fasta/xx_genomics_fasta.h>
#include <xxfclib/formats/genomics_fastq/xx_genomics_fastq.h>
#include <xxfclib/formats/genomics_sam/xx_genomics_sam.h>
#include <xxfclib/formats/opendx_field/xx_opendx_field.h>
#include <xxfclib/formats/genomics_vcf/xx_genomics_vcf.h>
#include <xxfclib/formats/genomics_gff3/xx_genomics_gff3.h>
#include <xxfclib/formats/protein_pdb/xx_protein_pdb.h>
#include <xxfclib/formats/protein_mmcif/xx_protein_mmcif.h>
#include <xxfclib/formats/matrix_market/xx_matrix_market.h>
#include <xxfclib/formats/gromacs_gro/xx_gromacs_gro.h>
#include <xxfclib/formats/microsoft_msf/xx_microsoft_msf.h>
#include <xxfclib/formats/windows_registry_hive/xx_windows_registry_hive.h>
#include <xxfclib/formats/windows_evtx/xx_windows_evtx.h>
#include <xxfclib/formats/binary_plist/xx_binary_plist.h>
#include <xxfclib/formats/mongodb_bson/xx_mongodb_bson.h>
#include <xxfclib/formats/cbor/xx_cbor.h>
#include <xxfclib/formats/openzim/xx_openzim.h>
#include <xxfclib/formats/apache_orc/xx_apache_orc.h>
#include <xxfclib/formats/hadoop_sequencefile/xx_hadoop_sequencefile.h>
#include <xxfclib/formats/leveldb_sstable/xx_leveldb_sstable.h>
#include <xxfclib/formats/snappy_framed/xx_snappy_framed.h>
#include <xxfclib/formats/lzf_stream/xx_lzf_stream.h>
#include <xxfclib/formats/fastlz_sixpack/xx_fastlz_sixpack.h>
#include <xxfclib/formats/linux_btf/xx_linux_btf.h>
#include <xxfclib/formats/flatgeobuf/xx_flatgeobuf.h>
#include <xxfclib/formats/astc_texture/xx_astc_texture.h>
#include <xxfclib/formats/pkm_texture/xx_pkm_texture.h>
#include <xxfclib/formats/basis_texture/xx_basis_texture.h>
#include <xxfclib/formats/openctm_mesh/xx_openctm_mesh.h>
#include <xxfclib/formats/font_bdf/xx_font_bdf.h>
#include <xxfclib/formats/font_pcf/xx_font_pcf.h>
#include <xxfclib/formats/font_psf/xx_font_psf.h>
#include <xxfclib/formats/font_windows_fnt/xx_font_windows_fnt.h>
#include <xxfclib/formats/tex_tfm/xx_tex_tfm.h>
#include <xxfclib/formats/tex_pk/xx_tex_pk.h>
#include <xxfclib/formats/tex_dvi/xx_tex_dvi.h>
#include <xxfclib/formats/netpbm_pfm/xx_netpbm_pfm.h>
#include <xxfclib/formats/steinberg_vst3preset/xx_steinberg_vst3preset.h>
#include <xxfclib/formats/font_bmfont/xx_font_bmfont.h>
#include <xxfclib/formats/processing_vlw/xx_processing_vlw.h>
#include <xxfclib/formats/snes_spc/xx_snes_spc.h>
#include <xxfclib/formats/gameboy_gbs/xx_gameboy_gbs.h>
#include <xxfclib/formats/sega_sgc/xx_sega_sgc.h>
#include <xxfclib/formats/s98_log/xx_s98_log.h>
#include <xxfclib/formats/atari_sap/xx_atari_sap.h>
#include <xxfclib/formats/sc68_music/xx_sc68_music.h>
#include <xxfclib/formats/zx_spectrum_pzx/xx_zx_spectrum_pzx.h>
#include <xxfclib/formats/acorn_uef/xx_acorn_uef.h>
#include <xxfclib/formats/nintendo_unif/xx_nintendo_unif.h>
#include <xxfclib/formats/nintendo_fds/xx_nintendo_fds.h>
#include <xxfclib/formats/ucsc_bigwig/xx_ucsc_bigwig.h>
#include <xxfclib/formats/ucsc_bigbed/xx_ucsc_bigbed.h>
#include <xxfclib/formats/phylo_nexus/xx_phylo_nexus.h>
#include <xxfclib/formats/phylo_newick/xx_phylo_newick.h>
#include <xxfclib/formats/sqlite_rollback_journal/xx_sqlite_rollback_journal.h>
#include <xxfclib/formats/neuroscan_cnt/xx_neuroscan_cnt.h>
#include <xxfclib/formats/axona_tetrode/xx_axona_tetrode.h>
#include <xxfclib/formats/python_pickle/xx_python_pickle.h>
#include <xxfclib/formats/inivation_aedat/xx_inivation_aedat.h>
#include <xxfclib/formats/python_marshal/xx_python_marshal.h>
#include <xxfclib/formats/vice_x64/xx_vice_x64.h>
#include <xxfclib/formats/vice_snapshot/xx_vice_snapshot.h>
#include <xxfclib/formats/commodore_g64/xx_commodore_g64.h>
#include <xxfclib/formats/commodore_p64/xx_commodore_p64.h>
#include <xxfclib/formats/commodore_tap/xx_commodore_tap.h>
#include <xxfclib/formats/zx_spectrum_tzx/xx_zx_spectrum_tzx.h>
#include <xxfclib/formats/zx_spectrum_szx/xx_zx_spectrum_szx.h>
#include <xxfclib/formats/amstrad_cpc_dsk/xx_amstrad_cpc_dsk.h>
#include <xxfclib/formats/atari_st_msa/xx_atari_st_msa.h>
#include <xxfclib/formats/supercard_scp/xx_supercard_scp.h>
#include <xxfclib/formats/apple_woz/xx_apple_woz.h>
#include <xxfclib/formats/nintendo_nsf/xx_nintendo_nsf.h>
#include <xxfclib/formats/vgm_log/xx_vgm_log.h>
#include <xxfclib/formats/psid_sid/xx_psid_sid.h>
#include <xxfclib/formats/hes_sound/xx_hes_sound.h>
#include <xxfclib/formats/audio_dolby_ac3/xx_audio_dolby_ac3.h>
#include <xxfclib/formats/audio_mpeg_mp3/xx_audio_mpeg_mp3.h>
#include <xxfclib/formats/audio_aac_adts/xx_audio_aac_adts.h>
#include <xxfclib/formats/audio_monkeys_ape/xx_audio_monkeys_ape.h>
#include <xxfclib/formats/mpeg_transport_stream/xx_mpeg_transport_stream.h>
#include <xxfclib/formats/mpeg_program_stream/xx_mpeg_program_stream.h>
#include <xxfclib/formats/realmedia_rm/xx_realmedia_rm.h>
#include <xxfclib/formats/idtech_roq/xx_idtech_roq.h>
#include <xxfclib/formats/rad_bink/xx_rad_bink.h>
#include <xxfclib/formats/rad_smacker/xx_rad_smacker.h>
#include <xxfclib/formats/interplay_mve/xx_interplay_mve.h>
#include <xxfclib/formats/westwood_vqa/xx_westwood_vqa.h>
#include <xxfclib/formats/autodesk_flic/xx_autodesk_flic.h>
#include <xxfclib/formats/idtech_md5anim/xx_idtech_md5anim.h>
#include <xxfclib/formats/stereolithography_stl/xx_stereolithography_stl.h>
#include <xxfclib/formats/garmin_fit/xx_garmin_fit.h>
#include <xxfclib/formats/rosbag1/xx_rosbag1.h>
#include <xxfclib/formats/mcap/xx_mcap.h>
#include <xxfclib/formats/seismic_sac/xx_seismic_sac.h>
#include <xxfclib/formats/seismic_seg2/xx_seismic_seg2.h>
#include <xxfclib/formats/ucsc_twobit/xx_ucsc_twobit.h>
#include <xxfclib/formats/genomics_bgen/xx_genomics_bgen.h>
#include <xxfclib/formats/openephys_continuous/xx_openephys_continuous.h>
#include <xxfclib/formats/mountainsort_mda/xx_mountainsort_mda.h>
#include <xxfclib/formats/igor_ibw/xx_igor_ibw.h>
#include <xxfclib/formats/princeton_spe/xx_princeton_spe.h>
#include <xxfclib/formats/microscopy_spider/xx_microscopy_spider.h>
#include <xxfclib/formats/wmo_grib/xx_wmo_grib.h>
#include <xxfclib/formats/wmo_bufr/xx_wmo_bufr.h>
#include <xxfclib/formats/autocad_dxf/xx_autocad_dxf.h>
#include <xxfclib/formats/blackrock_nsx/xx_blackrock_nsx.h>
#include <xxfclib/formats/blackrock_nev/xx_blackrock_nev.h>
#include <xxfclib/formats/lecroy_trc/xx_lecroy_trc.h>
#include <xxfclib/formats/tektronix_isf/xx_tektronix_isf.h>
#include <xxfclib/formats/ircam_sdif/xx_ircam_sdif.h>
#include <xxfclib/formats/tracker_liquid/xx_tracker_liquid.h>
#include <xxfclib/formats/tracker_dmf/xx_tracker_dmf.h>
#include <xxfclib/formats/tracker_ptm/xx_tracker_ptm.h>
#include <xxfclib/formats/tracker_ams/xx_tracker_ams.h>
#include <xxfclib/formats/tracker_digi/xx_tracker_digi.h>
#include <xxfclib/formats/tracker_emod/xx_tracker_emod.h>
#include <xxfclib/formats/tracker_mt2/xx_tracker_mt2.h>
#include <xxfclib/formats/audio_dsf/xx_audio_dsf.h>
#include <xxfclib/formats/audio_dff/xx_audio_dff.h>
#include <xxfclib/formats/audio_wave64/xx_audio_wave64.h>
#include <xxfclib/formats/audio_adx/xx_audio_adx.h>
#include <xxfclib/formats/audio_ast/xx_audio_ast.h>
#include <xxfclib/formats/audio_hca/xx_audio_hca.h>
#include <xxfclib/formats/iff_8svx/xx_iff_8svx.h>
#include <xxfclib/formats/audio_wavpack/xx_audio_wavpack.h>
#include <xxfclib/formats/blender_blend/xx_blender_blend.h>
#include <xxfclib/formats/autodesk_fbx/xx_autodesk_fbx.h>
#include <xxfclib/formats/autodesk_3ds/xx_autodesk_3ds.h>
#include <xxfclib/formats/lightwave_lwo2/xx_lightwave_lwo2.h>
#include <xxfclib/formats/lightwave_mdd/xx_lightwave_mdd.h>
#include <xxfclib/formats/sony_psp_pbp/xx_sony_psp_pbp.h>
#include <xxfclib/formats/flash_video_flv/xx_flash_video_flv.h>
#include <xxfclib/formats/nintendo_n64_rom/xx_nintendo_n64_rom.h>
#include <xxfclib/formats/nintendo_gb_rom/xx_nintendo_gb_rom.h>
#include <xxfclib/formats/nintendo_gba_rom/xx_nintendo_gba_rom.h>
#include <xxfclib/formats/sega_megadrive_rom/xx_sega_megadrive_rom.h>
#include <xxfclib/formats/spring_s3o/xx_spring_s3o.h>
#include <xxfclib/formats/xna_xnb/xx_xna_xnb.h>
#include <xxfclib/formats/lua_bytecode51/xx_lua_bytecode51.h>
#include <xxfclib/formats/quake_md5mesh/xx_quake_md5mesh.h>
#include <xxfclib/formats/tracker_mod/xx_tracker_mod.h>
#include <xxfclib/formats/tracker_far/xx_tracker_far.h>
#include <xxfclib/formats/tracker_mdl/xx_tracker_mdl.h>
#include <xxfclib/formats/tracker_gdm/xx_tracker_gdm.h>
#include <xxfclib/formats/tracker_dbm/xx_tracker_dbm.h>
#include <xxfclib/formats/tracker_med/xx_tracker_med.h>
#include <xxfclib/formats/tracker_imf/xx_tracker_imf.h>
#include <xxfclib/formats/tracker_amf/xx_tracker_amf.h>
#include <xxfclib/formats/tracker_psm/xx_tracker_psm.h>
#include <xxfclib/formats/steinberg_fxb/xx_steinberg_fxb.h>
#include <xxfclib/formats/astronomy_ser/xx_astronomy_ser.h>
#include <xxfclib/formats/photontiming_ptu/xx_photontiming_ptu.h>
#include <xxfclib/formats/photontiming_phu/xx_photontiming_phu.h>
#include <xxfclib/formats/charmm_dcd/xx_charmm_dcd.h>
#include <xxfclib/formats/gromacs_trr/xx_gromacs_trr.h>
#include <xxfclib/formats/microscopy_ics/xx_microscopy_ics.h>
#include <xxfclib/formats/tecplot_plt/xx_tecplot_plt.h>
#include <xxfclib/formats/fujifilm_raf/xx_fujifilm_raf.h>
#include <xxfclib/formats/sigma_x3f/xx_sigma_x3f.h>
#include <xxfclib/formats/minolta_mrw/xx_minolta_mrw.h>
#include <xxfclib/formats/sfx_imp/xx_sfx_imp.h>
#include <xxfclib/formats/sfx_red/xx_sfx_red.h>
#include <xxfclib/formats/sfx_ha/xx_sfx_ha.h>
#include <xxfclib/formats/sfx_lzx/xx_sfx_lzx.h>
#include <xxfclib/formats/sfx_sqx/xx_sfx_sqx.h>
#include <xxfclib/formats/sfx_ain/xx_sfx_ain.h>
#include <xxfclib/formats/sfx_hap/xx_sfx_hap.h>
#include <xxfclib/formats/sfx_zoo/xx_sfx_zoo.h>
#include <xxfclib/formats/sfx_cazip/xx_sfx_cazip.h>
#include <xxfclib/formats/sfx_tgcf/xx_sfx_tgcf.h>
#include <xxfclib/formats/sfx_starkit/xx_sfx_starkit.h>
#include <xxfclib/formats/sfx_alz/xx_sfx_alz.h>
#include <xxfclib/formats/sfx_chm/xx_sfx_chm.h>
#include <xxfclib/formats/egg/xx_egg.h>
#include <xxfclib/formats/nufx/xx_nufx.h>
#include <xxfclib/formats/nintendo_dol/xx_nintendo_dol.h>
#include <xxfclib/formats/nintendo_j3d_bmd/xx_nintendo_j3d_bmd.h>
#include <xxfclib/formats/nintendo_j3d_btk/xx_nintendo_j3d_btk.h>
#include <xxfclib/formats/nintendo_brstm/xx_nintendo_brstm.h>
#include <xxfclib/formats/nintendo_brwav/xx_nintendo_brwav.h>
#include <xxfclib/formats/nintendo_brlyt/xx_nintendo_brlyt.h>
#include <xxfclib/formats/nintendo_brlan/xx_nintendo_brlan.h>
#include <xxfclib/formats/nintendo_bfsha/xx_nintendo_bfsha.h>
#include <xxfclib/formats/cri_usm/xx_cri_usm.h>
#include <xxfclib/formats/cri_utf/xx_cri_utf.h>
#include <xxfclib/formats/idtech_iqm/xx_idtech_iqm.h>
#include <xxfclib/formats/unreal_psk/xx_unreal_psk.h>
#include <xxfclib/formats/unreal_psa/xx_unreal_psa.h>
#include <xxfclib/formats/torque_dts/xx_torque_dts.h>
#include <xxfclib/formats/magicavoxel_vox/xx_magicavoxel_vox.h>
#include <xxfclib/formats/audio_au/xx_audio_au.h>
#include <xxfclib/formats/creative_voc/xx_creative_voc.h>
#include <xxfclib/formats/tracker_xm/xx_tracker_xm.h>
#include <xxfclib/formats/tracker_s3m/xx_tracker_s3m.h>
#include <xxfclib/formats/tracker_it/xx_tracker_it.h>
#include <xxfclib/formats/tracker_mtm/xx_tracker_mtm.h>
#include <xxfclib/formats/tracker_stm/xx_tracker_stm.h>
#include <xxfclib/formats/tracker_669/xx_tracker_669.h>
#include <xxfclib/formats/tracker_ult/xx_tracker_ult.h>
#include <xxfclib/formats/tracker_okt/xx_tracker_okt.h>
#include <xxfclib/formats/nifti2/xx_nifti2.h>
#include <xxfclib/formats/lidar_las/xx_lidar_las.h>
#include <xxfclib/formats/esri_shp/xx_esri_shp.h>
#include <xxfclib/formats/polygon_ply/xx_polygon_ply.h>
#include <xxfclib/formats/pointcloud_pcd/xx_pointcloud_pcd.h>
#include <xxfclib/formats/matlab_mat4/xx_matlab_mat4.h>
#include <xxfclib/formats/seismic_segy/xx_seismic_segy.h>
#include <xxfclib/formats/biomedical_bdf/xx_biomedical_bdf.h>
#include <xxfclib/formats/erlang_beam/xx_erlang_beam.h>
#include <xxfclib/formats/java_jmod/xx_java_jmod.h>
#include <xxfclib/formats/sfx_arcv2/xx_sfx_arcv2.h>
#include <xxfclib/formats/sfx_chz/xx_sfx_chz.h>
#include <xxfclib/formats/sfx_szdd/xx_sfx_szdd.h>
#include <xxfclib/formats/sfx_mpq/xx_sfx_mpq.h>
#include <xxfclib/formats/sfx_swag/xx_sfx_swag.h>
#include <xxfclib/formats/sfx_zpak/xx_sfx_zpak.h>
#include <xxfclib/formats/sfx_diskexpress/xx_sfx_diskexpress.h>
#include <xxfclib/formats/sfx_bzip2/xx_sfx_bzip2.h>
#include <xxfclib/formats/sfx_gzip/xx_sfx_gzip.h>
#include <xxfclib/formats/sfx_tar/xx_sfx_tar.h>
#include <xxfclib/formats/sfx_cab/xx_sfx_cab.h>
#include <xxfclib/formats/pmarc_sfx/xx_pmarc_sfx.h>
#include <xxfclib/formats/sfx_7zip/xx_sfx_7zip.h>
#include <xxfclib/formats/sfx_ace/xx_sfx_ace.h>
#include <xxfclib/formats/sfx_zipcentral/xx_sfx_zipcentral.h>
#include <xxfclib/formats/sony_psx_exe/xx_sony_psx_exe.h>
#include <xxfclib/formats/sony_psf/xx_sony_psf.h>
#include <xxfclib/formats/xbox_xdvdfs/xx_xbox_xdvdfs.h>
#include <xxfclib/formats/nintendo_wbfs/xx_nintendo_wbfs.h>
#include <xxfclib/formats/godot_ctex/xx_godot_ctex.h>
#include <xxfclib/formats/unity_serialized/xx_unity_serialized.h>
#include <xxfclib/formats/idtech_mdl/xx_idtech_mdl.h>
#include <xxfclib/formats/valve_studio_mdl/xx_valve_studio_mdl.h>
#include <xxfclib/formats/blitz3d_b3d/xx_blitz3d_b3d.h>
#include <xxfclib/formats/milkshape_ms3d/xx_milkshape_ms3d.h>
#include <xxfclib/formats/nintendo_bch/xx_nintendo_bch.h>
#include <xxfclib/formats/nintendo_cgfx/xx_nintendo_cgfx.h>
#include <xxfclib/formats/nintendo_byaml/xx_nintendo_byaml.h>
#include <xxfclib/formats/relic_chunky/xx_relic_chunky.h>
#include <xxfclib/formats/ogre_mesh/xx_ogre_mesh.h>
#include <xxfclib/formats/adobe_ase/xx_adobe_ase.h>
#include <xxfclib/formats/adobe_aco/xx_adobe_aco.h>
#include <xxfclib/formats/gimp_gbr/xx_gimp_gbr.h>
#include <xxfclib/formats/gimp_gih/xx_gimp_gih.h>
#include <xxfclib/formats/gimp_pat/xx_gimp_pat.h>
#include <xxfclib/formats/jbig2/xx_jbig2.h>
#include <xxfclib/formats/djvu/xx_djvu.h>
#include <xxfclib/formats/emf/xx_emf.h>
#include <xxfclib/formats/wmf/xx_wmf.h>
#include <xxfclib/formats/xfig/xx_xfig.h>
#include <xxfclib/formats/nifti1/xx_nifti1.h>
#include <xxfclib/formats/nrrd/xx_nrrd.h>
#include <xxfclib/formats/mrc/xx_mrc.h>
#include <xxfclib/formats/metaimage/xx_metaimage.h>
#include <xxfclib/formats/vtk_legacy/xx_vtk_legacy.h>
#include <xxfclib/formats/gipl/xx_gipl.h>
#include <xxfclib/formats/freesurfer_mgh/xx_freesurfer_mgh.h>
#include <xxfclib/formats/edf/xx_edf.h>
#include <xxfclib/formats/fcs/xx_fcs.h>
#include <xxfclib/formats/tensorflow_tfrecord/xx_tensorflow_tfrecord.h>
#include <xxfclib/formats/sfx_arc/xx_sfx_arc.h>
#include <xxfclib/formats/sfx_arj/xx_sfx_arj.h>
#include <xxfclib/formats/sfx_bsn/xx_sfx_bsn.h>
#include <xxfclib/formats/sfx_arq/xx_sfx_arq.h>
#include <xxfclib/formats/sfx_gxl/xx_sfx_gxl.h>
#include <xxfclib/formats/sfx_asymetrix/xx_sfx_asymetrix.h>
#include <xxfclib/formats/sfx_rta/xx_sfx_rta.h>
#include <xxfclib/formats/sfx_rtpatch/xx_sfx_rtpatch.h>
#include <xxfclib/formats/esp_archive/xx_esp_archive.h>
#include <xxfclib/formats/sfx_kwaj/xx_sfx_kwaj.h>
#include <xxfclib/formats/gemdos_lha/xx_gemdos_lha.h>
#include <xxfclib/formats/winimage_zip/xx_winimage_zip.h>
#include <xxfclib/formats/hp3000_wrq/xx_hp3000_wrq.h>
#include <xxfclib/formats/icu_data_package/xx_icu_data_package.h>
#include <xxfclib/formats/sfx_sqz/xx_sfx_sqz.h>
#include <xxfclib/formats/nintendo_bfstm/xx_nintendo_bfstm.h>
#include <xxfclib/formats/nintendo_bfwav/xx_nintendo_bfwav.h>
#include <xxfclib/formats/nintendo_bcwav/xx_nintendo_bcwav.h>
#include <xxfclib/formats/nintendo_bfres/xx_nintendo_bfres.h>
#include <xxfclib/formats/nintendo_bflyt/xx_nintendo_bflyt.h>
#include <xxfclib/formats/nintendo_bclyt/xx_nintendo_bclyt.h>
#include <xxfclib/formats/nintendo_bfnt/xx_nintendo_bfnt.h>
#include <xxfclib/formats/nintendo_bcfnt/xx_nintendo_bcfnt.h>
#include <xxfclib/formats/nintendo_3dsx/xx_nintendo_3dsx.h>
#include <xxfclib/formats/sony_tim2/xx_sony_tim2.h>
#include <xxfclib/formats/sony_pamf/xx_sony_pamf.h>
#include <xxfclib/formats/sega_gvr/xx_sega_gvr.h>
#include <xxfclib/formats/microsoft_xwb/xx_microsoft_xwb.h>
#include <xxfclib/formats/microsoft_xsb/xx_microsoft_xsb.h>
#include <xxfclib/formats/relic_sga/xx_relic_sga.h>
#include <xxfclib/formats/xpm/xx_xpm.h>
#include <xxfclib/formats/pcx/xx_pcx.h>
#include <xxfclib/formats/iff_ilbm/xx_iff_ilbm.h>
#include <xxfclib/formats/utah_rle/xx_utah_rle.h>
#include <xxfclib/formats/radiance_hdr/xx_radiance_hdr.h>
#include <xxfclib/formats/dpx/xx_dpx.h>
#include <xxfclib/formats/cineon/xx_cineon.h>
#include <xxfclib/formats/xwd/xx_xwd.h>
#include <xxfclib/formats/sgi_rgb/xx_sgi_rgb.h>
#include <xxfclib/formats/aseprite/xx_aseprite.h>
#include <xxfclib/formats/numpy_npy/xx_numpy_npy.h>
#include <xxfclib/formats/matlab_mat5/xx_matlab_mat5.h>
#include <xxfclib/formats/netcdf_classic/xx_netcdf_classic.h>
#include <xxfclib/formats/hdf4/xx_hdf4.h>
#include <xxfclib/formats/dbase_dbf/xx_dbase_dbf.h>
#include <xxfclib/formats/sas_xport/xx_sas_xport.h>
#include <xxfclib/formats/spss_sav/xx_spss_sav.h>
#include <xxfclib/formats/stata_dta/xx_stata_dta.h>
#include <xxfclib/formats/apache_arrow_file/xx_apache_arrow_file.h>
#include <xxfclib/formats/apache_parquet/xx_apache_parquet.h>
#include <xxfclib/formats/makeself/xx_makeself.h>
#include <xxfclib/formats/sun_java_binsh/xx_sun_java_binsh.h>
#include <xxfclib/formats/installanywhere_unix/xx_installanywhere_unix.h>
#include <xxfclib/formats/sfx_packagefortheweb/xx_sfx_packagefortheweb.h>
#include <xxfclib/formats/sfx_spis/xx_sfx_spis.h>
#include <xxfclib/formats/sfx_lha/xx_sfx_lha.h>
#include <xxfclib/formats/lmd_container/xx_lmd_container.h>
#include <xxfclib/formats/totalannihilation_hpi/xx_totalannihilation_hpi.h>
#include <xxfclib/formats/ravensoft_rff/xx_ravensoft_rff.h>
#include <xxfclib/formats/terminalreality_pod/xx_terminalreality_pod.h>
#include <xxfclib/formats/volition_vpp/xx_volition_vpp.h>
#include <xxfclib/formats/kirikiri_xp3/xx_kirikiri_xp3.h>
#include <xxfclib/formats/fromsoftware_binder/xx_fromsoftware_binder.h>
#include <xxfclib/formats/mythic_myp/xx_mythic_myp.h>
#include <xxfclib/formats/lithtech_rez/xx_lithtech_rez.h>
#include <xxfclib/formats/nintendo_ncch/xx_nintendo_ncch.h>
#include <xxfclib/formats/nintendo_ncsd/xx_nintendo_ncsd.h>
#include <xxfclib/formats/nintendo_cia/xx_nintendo_cia.h>
#include <xxfclib/formats/nintendo_nds/xx_nintendo_nds.h>
#include <xxfclib/formats/nintendo_gcm/xx_nintendo_gcm.h>
#include <xxfclib/formats/nintendo_tpl/xx_nintendo_tpl.h>
#include <xxfclib/formats/sony_tim/xx_sony_tim.h>
#include <xxfclib/formats/sony_vag/xx_sony_vag.h>
#include <xxfclib/formats/larian_lspk/xx_larian_lspk.h>
#include <xxfclib/formats/larian_lsf/xx_larian_lsf.h>
#include <xxfclib/formats/valve_hpak/xx_valve_hpak.h>
#include <xxfclib/formats/renpy_rpa/xx_renpy_rpa.h>
#include <xxfclib/formats/unreal_package/xx_unreal_package.h>
#include <xxfclib/formats/sega_pvr2/xx_sega_pvr2.h>
#include <xxfclib/formats/nintendo_bntx/xx_nintendo_bntx.h>
#include <xxfclib/formats/icns/xx_icns.h>
#include <xxfclib/formats/xcursor/xx_xcursor.h>
#include <xxfclib/formats/icc/xx_icc.h>
#include <xxfclib/formats/qoi/xx_qoi.h>
#include <xxfclib/formats/farbfeld/xx_farbfeld.h>
#include <xxfclib/formats/pnm/xx_pnm.h>
#include <xxfclib/formats/tga/xx_tga.h>
#include <xxfclib/formats/sun_raster/xx_sun_raster.h>
#include <xxfclib/formats/fits/xx_fits.h>
#include <xxfclib/formats/dicom/xx_dicom.h>
#include <xxfclib/formats/pcap/xx_pcap.h>
#include <xxfclib/formats/btsnoop/xx_btsnoop.h>
#include <xxfclib/formats/java_class/xx_java_class.h>
#include <xxfclib/formats/sfnt_collection/xx_sfnt_collection.h>
#include <xxfclib/formats/sqlite3/xx_sqlite3.h>
#include <xxfclib/formats/sqlite_wal/xx_sqlite_wal.h>
#include <xxfclib/formats/avro_object/xx_avro_object.h>
#include <xxfclib/formats/glb/xx_glb.h>
#include <xxfclib/formats/spirv/xx_spirv.h>
#include <xxfclib/formats/crx/xx_crx.h>
#include <xxfclib/formats/bethesda_bsa/xx_bethesda_bsa.h>
#include <xxfclib/formats/bethesda_ba2/xx_bethesda_ba2.h>
#include <xxfclib/formats/unityfs/xx_unityfs.h>
#include <xxfclib/formats/bioware_biff/xx_bioware_biff.h>
#include <xxfclib/formats/bioware_erf/xx_bioware_erf.h>
#include <xxfclib/formats/bioware_rim/xx_bioware_rim.h>
#include <xxfclib/formats/lucas_lab/xx_lucas_lab.h>
#include <xxfclib/formats/lucas_bun/xx_lucas_bun.h>
#include <xxfclib/formats/idtech_bsp/xx_idtech_bsp.h>
#include <xxfclib/formats/valve_bsp/xx_valve_bsp.h>
#include <xxfclib/formats/idtech_md2/xx_idtech_md2.h>
#include <xxfclib/formats/idtech_md3/xx_idtech_md3.h>
#include <xxfclib/formats/idtech_qvm/xx_idtech_qvm.h>
#include <xxfclib/formats/mohawk_mhk/xx_mohawk_mhk.h>
#include <xxfclib/formats/quake_sprite/xx_quake_sprite.h>
#include <xxfclib/formats/nintendo_narc/xx_nintendo_narc.h>
#include <xxfclib/formats/nintendo_sarc/xx_nintendo_sarc.h>
#include <xxfclib/formats/nintendo_pfs0/xx_nintendo_pfs0.h>
#include <xxfclib/formats/nintendo_hfs0/xx_nintendo_hfs0.h>
#include <xxfclib/formats/nintendo_brres/xx_nintendo_brres.h>
#include <xxfclib/formats/nintendo_bcstm/xx_nintendo_bcstm.h>
#include <xxfclib/formats/nintendo_bfsar/xx_nintendo_bfsar.h>
#include <xxfclib/formats/nintendo_bcsar/xx_nintendo_bcsar.h>
#include <xxfclib/formats/sony_psarc/xx_sony_psarc.h>
#include <xxfclib/formats/ktx/xx_ktx.h>
#include <xxfclib/formats/ktx2/xx_ktx2.h>
#include <xxfclib/formats/dds/xx_dds.h>
#include <xxfclib/formats/pvr/xx_pvr.h>
#include <xxfclib/formats/valve_vtf/xx_valve_vtf.h>
#include <xxfclib/formats/xbox_xbe/xx_xbox_xbe.h>
#include <xxfclib/formats/flac/xx_flac.h>
#include <xxfclib/formats/ogg/xx_ogg.h>
#include <xxfclib/formats/mp4/xx_mp4.h>
#include <xxfclib/formats/matroska/xx_matroska.h>
#include <xxfclib/formats/aiff/xx_aiff.h>
#include <xxfclib/formats/caf/xx_caf.h>
#include <xxfclib/formats/photoshop_psd/xx_photoshop_psd.h>
#include <xxfclib/formats/tiff/xx_tiff.h>
#include <xxfclib/formats/openexr/xx_openexr.h>
#include <xxfclib/formats/jpeg2000_jp2/xx_jpeg2000_jp2.h>
#include <xxfclib/formats/android_vendor_boot/xx_android_vendor_boot.h>
#include <xxfclib/formats/android_dtbo/xx_android_dtbo.h>
#include <xxfclib/formats/android_vbmeta/xx_android_vbmeta.h>
#include <xxfclib/formats/espressif_image/xx_espressif_image.h>
#include <xxfclib/formats/wasm/xx_wasm.h>
#include <xxfclib/formats/llvm_bitcode_wrapper/xx_llvm_bitcode_wrapper.h>
#include <xxfclib/formats/dotnet_metadata/xx_dotnet_metadata.h>
#include <xxfclib/formats/sfnt/xx_sfnt.h>
#include <xxfclib/formats/woff/xx_woff.h>
#include <xxfclib/formats/woff2/xx_woff2.h>
#include <xxfclib/formats/act_apricot_pc_xi_raw/xx_act_apricot_pc_xi_raw.h>
#include <xxfclib/formats/adam/xx_adam.h>
#include <xxfclib/formats/base16/xx_base16.h>
#include <xxfclib/formats/bondwell_2_disk/xx_bondwell_2_disk.h>
#include <xxfclib/formats/casio_fz_1_disk/xx_casio_fz_1_disk.h>
#include <xxfclib/formats/isz/xx_isz.h>
#include <xxfclib/formats/mame_floppy_image_mfi/xx_mame_floppy_image_mfi.h>
#include <xxfclib/formats/parallels_hdd/xx_parallels_hdd.h>
#include <xxfclib/formats/pc_magazine_flp/xx_pc_magazine_flp.h>
#include <xxfclib/formats/pchrom/xx_pchrom.h>
#include <xxfclib/formats/pem/xx_pem.h>
#include <xxfclib/formats/prodos/xx_prodos.h>
#include <xxfclib/formats/qemu_enhanced_disk/xx_qemu_enhanced_disk.h>
#include <xxfclib/formats/rawcd/xx_rawcd.h>
#include <xxfclib/formats/rsdos_fs/xx_rsdos_fs.h>
#include <xxfclib/formats/sar_ns/xx_sar_ns.h>
#include <xxfclib/formats/swf/xx_swf.h>
#include <xxfclib/formats/t64/xx_t64.h>
#include <xxfclib/formats/uue/xx_uue.h>
#include <xxfclib/formats/vdi/xx_vdi.h>
#include <xxfclib/formats/bmp/xx_bmp.h>
#include <xxfclib/formats/cfe/xx_cfe.h>
#include <xxfclib/formats/dxbc/xx_dxbc.h>
#include <xxfclib/formats/gif/xx_gif.h>
#include <xxfclib/formats/jpeg/xx_jpeg.h>
#include <xxfclib/formats/linuxarm64/xx_linuxarm64.h>
#include <xxfclib/formats/linuxboot/xx_linuxboot.h>
#include <xxfclib/formats/linuxzimage/xx_linuxzimage.h>
#include <xxfclib/formats/pcapng/xx_pcapng.h>
#include <xxfclib/formats/pjl/xx_pjl.h>
#include <xxfclib/formats/png/xx_png.h>
#include <xxfclib/formats/riff/xx_riff.h>
#include <xxfclib/formats/svg/xx_svg.h>
#include <xxfclib/formats/quake_pak/xx_quake_pak.h>
#include <xxfclib/formats/doom_wad/xx_doom_wad.h>
#include <xxfclib/formats/quake_wad2/xx_quake_wad2.h>
#include <xxfclib/formats/halflife_wad3/xx_halflife_wad3.h>
#include <xxfclib/formats/build_grp/xx_build_grp.h>
#include <xxfclib/formats/cri_afs/xx_cri_afs.h>
#include <xxfclib/formats/cri_awb/xx_cri_awb.h>
#include <xxfclib/formats/valve_vpk/xx_valve_vpk.h>
#include <xxfclib/formats/nintendo_u8/xx_nintendo_u8.h>
#include <xxfclib/formats/nintendo_rarc/xx_nintendo_rarc.h>
#include <xxfclib/formats/android_ab/xx_android_ab.h>
#include <xxfclib/formats/nes_rom/xx_nes_rom.h>
#include <xxfclib/formats/lynx_lnx/xx_lynx_lnx.h>
#include <xxfclib/formats/commodore_crt/xx_commodore_crt.h>
#include <xxfclib/formats/uf2/xx_uf2.h>
#include <xxfclib/formats/ico/xx_ico.h>
#include <xxfclib/formats/midi/xx_midi.h>
#include <xxfclib/formats/advanced_installer_bootstrapper/xx_advanced_installer_bootstrapper.h>
#include <xxfclib/formats/ardi_installer/xx_ardi_installer.h>
#include <xxfclib/formats/arni_installer_container/xx_arni_installer_container.h>
#include <xxfclib/formats/ej_technologies_install/xx_ej_technologies_install.h>
#include <xxfclib/formats/finstall/xx_finstall.h>
#include <xxfclib/formats/ghost_installer/xx_ghost_installer.h>
#include <xxfclib/formats/ibm_zpak_installer/xx_ibm_zpak_installer.h>
#include <xxfclib/formats/ifah_installer/xx_ifah_installer.h>
#include <xxfclib/formats/inno_setup/xx_inno_setup.h>
#include <xxfclib/formats/installer_vise_windows/xx_installer_vise_windows.h>
#include <xxfclib/formats/installshield_12_setup/xx_installshield_12_setup.h>
#include <xxfclib/formats/installshield_3/xx_installshield_3.h>
#include <xxfclib/formats/installshield_7_setup/xx_installshield_7_setup.h>
#include <xxfclib/formats/installshield_7_setup2/xx_installshield_7_setup2.h>
#include <xxfclib/formats/installshield_developer/xx_installshield_developer.h>
#include <xxfclib/formats/installshield_issetupstream/xx_installshield_issetupstream.h>
#include <xxfclib/formats/installshield_multiplatform/xx_installshield_multiplatform.h>
#include <xxfclib/formats/installshield_skin/xx_installshield_skin.h>
#include <xxfclib/formats/microfox_put/xx_microfox_put.h>
#include <xxfclib/formats/o_setup/xx_o_setup.h>
#include <xxfclib/formats/pc_install_setup/xx_pc_install_setup.h>
#include <xxfclib/formats/pyinstaller_one_executable/xx_pyinstaller_one_executable.h>
#include <xxfclib/formats/qsetup_installation_suite/xx_qsetup_installation_suite.h>
#include <xxfclib/formats/rtpatch_setup_data/xx_rtpatch_setup_data.h>
#include <xxfclib/formats/setup_factory/xx_setup_factory.h>
#include <xxfclib/formats/sfx_ebook_compiler_executables/xx_sfx_ebook_compiler_executables.h>
#include <xxfclib/formats/spoon_installer/xx_spoon_installer.h>
#include <xxfclib/formats/tarma_installer/xx_tarma_installer.h>
#include <xxfclib/formats/adf/xx_adf.h>
#include <xxfclib/formats/apm/xx_apm.h>
#include <xxfclib/formats/vhdx/xx_vhdx.h>
#include <xxfclib/formats/base64/xx_base64.h>
#include <xxfclib/formats/btoa/xx_btoa.h>
#include <xxfclib/formats/chd/xx_chd.h>
#include <xxfclib/formats/chm/xx_chm.h>
#include <xxfclib/formats/cloop/xx_cloop.h>
#include <xxfclib/formats/cue/xx_cue.h>
#include <xxfclib/formats/dahuazip/xx_dahuazip.h>
#include <xxfclib/formats/dmsfw/xx_dmsfw.h>
#include <xxfclib/formats/ewf/xx_ewf.h>
#include <xxfclib/formats/godot_engine_pck/xx_godot_engine_pck.h>
#include <xxfclib/formats/gpgsigned/xx_gpgsigned.h>
#include <xxfclib/formats/ihex/xx_ihex.h>
#include <xxfclib/formats/kwaj/xx_kwaj.h>
#include <xxfclib/formats/lbr/xx_lbr.h>
#include <xxfclib/formats/lzfsestream/xx_lzfsestream.h>
#include <xxfclib/formats/nrg/xx_nrg.h>
#include <xxfclib/formats/packit_mac/xx_packit_mac.h>
#include <xxfclib/formats/rpm/xx_rpm.h>
#include <xxfclib/formats/stuffit5/xx_stuffit5.h>
#include <xxfclib/rt/xx_rt.h>

#include <xxfclib/formats/7zip/xx_7zip.h>
#include <xxfclib/formats/ace/xx_ace.h>
#include <xxfclib/formats/agis/xx_agis.h>
#include <xxfclib/formats/aiaff/xx_aiaff.h>
#include <xxfclib/formats/ain/xx_ain.h>
#include <xxfclib/formats/aixbff/xx_aixbff.h>
#include <xxfclib/formats/aldus/xx_aldus.h>
#include <xxfclib/formats/alz/xx_alz.h>
#include <xxfclib/formats/amigahunk/xx_amigahunk.h>
#include <xxfclib/formats/amigalzx/xx_amigalzx.h>
#include <xxfclib/formats/ampk/xx_ampk.h>
#include <xxfclib/formats/androidboot/xx_androidboot.h>
#include <xxfclib/formats/aodos/xx_aodos.h>
#include <xxfclib/formats/ap4/xx_ap4.h>
#include <xxfclib/formats/apfs/xx_apfs.h>
#include <xxfclib/formats/apk/xx_apk.h>
#include <xxfclib/formats/applesingle/xx_applesingle.h>
#include <xxfclib/formats/apricot/xx_apricot.h>
#include <xxfclib/formats/ar/xx_ar.h>
#include <xxfclib/formats/arcadyan/xx_arcadyan.h>
#include <xxfclib/formats/arcfs/xx_arcfs.h>
#include <xxfclib/formats/arcv/xx_arcv.h>
#include <xxfclib/formats/arcv2/xx_arcv2.h>
#include <xxfclib/formats/arcv4/xx_arcv4.h>
#include <xxfclib/formats/arj/xx_arj.h>
#include <xxfclib/formats/arq/xx_arq.h>
#include <xxfclib/formats/artipack/xx_artipack.h>
#include <xxfclib/formats/arx/xx_arx.h>
#include <xxfclib/formats/asar/xx_asar.h>
#include <xxfclib/formats/ascend/xx_ascend.h>
#include <xxfclib/formats/ascendbackup/xx_ascendbackup.h>
#include <xxfclib/formats/ash0/xx_ash0.h>
#include <xxfclib/formats/asymetrix/xx_asymetrix.h>
#include <xxfclib/formats/atarist/xx_atarist.h>
#include <xxfclib/formats/autel/xx_autel.h>
#include <xxfclib/formats/bagf/xx_bagf.h>
#include <xxfclib/formats/battleisle/xx_battleisle.h>
#include <xxfclib/formats/bcm/xx_bcm.h>
#include <xxfclib/formats/bcw/xx_bcw.h>
#include <xxfclib/formats/beatthehouse/xx_beatthehouse.h>
#include <xxfclib/formats/beospkg/xx_beospkg.h>
#include <xxfclib/formats/bigaf/xx_bigaf.h>
#include <xxfclib/formats/bigf/xx_bigf.h>
#include <xxfclib/formats/ckp/xx_ckp.h>
#include <xxfclib/formats/edp/xx_edp.h>
#include <xxfclib/formats/parsec_rib/xx_parsec_rib.h>
#include <xxfclib/formats/parsec_archive/xx_parsec_archive.h>
#include <xxfclib/formats/parsec_pmm/xx_parsec_pmm.h>
#include <xxfclib/formats/ptero_bigf/xx_ptero_bigf.h>
#include <xxfclib/formats/rvz/xx_rvz.h>
#include <xxfclib/formats/binaryii/xx_binaryii.h>
#include <xxfclib/formats/binder/xx_binder.h>
#include <xxfclib/formats/binhdr/xx_binhdr.h>
#include <xxfclib/formats/binhex/xx_binhex.h>
#include <xxfclib/formats/bnd/xx_bnd.h>
#include <xxfclib/formats/boo/xx_boo.h>
#include <xxfclib/formats/borlandpack/xx_borlandpack.h>
#include <xxfclib/formats/brotli/xx_brotli.h>
#include <xxfclib/formats/bsn/xx_bsn.h>
#include <xxfclib/formats/btrfs/xx_btrfs.h>
#include <xxfclib/formats/bvrp/xx_bvrp.h>
#include <xxfclib/formats/bwcf/xx_bwcf.h>
#include <xxfclib/formats/bwf/xx_bwf.h>
#include <xxfclib/formats/bz2/xx_bz2.h>
#include <xxfclib/formats/c64wraptor/xx_c64wraptor.h>
#include <xxfclib/formats/cab/xx_cab.h>
#include <xxfclib/formats/cat/xx_cat.h>
#include <xxfclib/formats/cpoint/xx_cpoint.h>
#include <xxfclib/formats/cazip/xx_cazip.h>
#include <xxfclib/formats/cfl/xx_cfl.h>
#include <xxfclib/formats/chieflz/xx_chieflz.h>
#include <xxfclib/formats/chieflzmulti/xx_chieflzmulti.h>
#include <xxfclib/formats/chk/xx_chk.h>
#include <xxfclib/formats/ciso/xx_ciso.h>
#include <xxfclib/formats/claylz/xx_claylz.h>
#include <xxfclib/formats/clp/xx_clp.h>
#include <xxfclib/formats/cmp/xx_cmp.h>
#include <xxfclib/formats/com/xx_com.h>
#include <xxfclib/formats/compactpro/xx_compactpro.h>
#include <xxfclib/formats/compaqlzh/xx_compaqlzh.h>
#include <xxfclib/formats/copydisk/xx_copydisk.h>
#include <xxfclib/formats/copyqm/xx_copyqm.h>
#include <xxfclib/formats/copyqmexe/xx_copyqmexe.h>
#include <xxfclib/formats/corelltec/xx_corelltec.h>
#include <xxfclib/formats/cpio/xx_cpio.h>
#include <xxfclib/formats/cpx/xx_cpx.h>
#include <xxfclib/formats/cramfs/xx_cramfs.h>
#include <xxfclib/formats/cru/xx_cru.h>
#include <xxfclib/formats/csidos/xx_csidos.h>
#include <xxfclib/formats/csman/xx_csman.h>
#include <xxfclib/formats/dbz/xx_dbz.h>
#include <xxfclib/formats/dclft/xx_dclft.h>
#include <xxfclib/formats/dclraw/xx_dclraw.h>
#include <xxfclib/formats/debugscr/xx_debugscr.h>
#include <xxfclib/formats/dex/xx_dex.h>
#include <xxfclib/formats/diskdoubler/xx_diskdoubler.h>
#include <xxfclib/formats/diskdupe/xx_diskdupe.h>
#include <xxfclib/formats/diskexpress/xx_diskexpress.h>
#include <xxfclib/formats/diskjuggler/xx_diskjuggler.h>
#include <xxfclib/formats/dkbs/xx_dkbs.h>
#include <xxfclib/formats/dlink_tlv/xx_dlink_tlv.h>
#include <xxfclib/formats/dlke/xx_dlke.h>
#include <xxfclib/formats/dlob/xx_dlob.h>
#include <xxfclib/formats/dmapacked/xx_dmapacked.h>
#include <xxfclib/formats/dmg/xx_dmg.h>
#include <xxfclib/formats/dms/xx_dms.h>
#include <xxfclib/formats/dos16m/xx_dos16m.h>
#include <xxfclib/formats/dpk/xx_dpk.h>
#include <xxfclib/formats/dsl2/xx_dsl2.h>
#include <xxfclib/formats/dtb/xx_dtb.h>
#include <xxfclib/formats/dtpacked/xx_dtpacked.h>
#include <xxfclib/formats/ea/xx_ea.h>
#include <xxfclib/formats/ealib/xx_ealib.h>
#include <xxfclib/formats/elm/xx_elm.h>
#include <xxfclib/formats/earefpack/xx_earefpack.h>
#include <xxfclib/formats/ecmpacked/xx_ecmpacked.h>
#include <xxfclib/formats/ecos/xx_ecos.h>
#include <xxfclib/formats/edc/xx_edc.h>
#include <xxfclib/formats/edilzss/xx_edilzss.h>
#include <xxfclib/formats/elf/xx_elf.h>
#include <xxfclib/formats/emt/xx_emt.h>
#include <xxfclib/formats/encfw/xx_encfw.h>
#include <xxfclib/formats/encrpted_img/xx_encrpted_img.h>
#include <xxfclib/formats/ext/xx_ext.h>
#include <xxfclib/formats/erofs/xx_erofs.h>
#include <xxfclib/formats/fat/xx_fat.h>
#include <xxfclib/formats/fdi/xx_fdi.h>
#include <xxfclib/formats/finear/xx_finear.h>
#include <xxfclib/formats/fiz/xx_fiz.h>
#include <xxfclib/formats/fld/xx_fld.h>
#include <xxfclib/formats/fls/xx_fls.h>
#include <xxfclib/formats/fmc1/xx_fmc1.h>
#include <xxfclib/formats/fpak/xx_fpak.h>
#include <xxfclib/formats/freearc/xx_freearc.h>
#include <xxfclib/formats/frontpagetheme/xx_frontpagetheme.h>
#include <xxfclib/formats/ftcomp/xx_ftcomp.h>
#include <xxfclib/formats/gamos/xx_gamos.h>
#include <xxfclib/formats/gashuff/xx_gashuff.h>
#include <xxfclib/formats/genius/xx_genius.h>
#include <xxfclib/formats/gitobject/xx_gitobject.h>
#include <xxfclib/formats/gksetup/xx_gksetup.h>
#include <xxfclib/formats/glu/xx_glu.h>
#include <xxfclib/formats/gob/xx_gob.h>
#include <xxfclib/formats/gpfpack/xx_gpfpack.h>
#include <xxfclib/formats/gpt/xx_gpt.h>
#include <xxfclib/formats/grasp/xx_grasp.h>
#include <xxfclib/formats/gst/xx_gst.h>
#include <xxfclib/formats/gtu/xx_gtu.h>
#include <xxfclib/formats/gxl/xx_gxl.h>
#include <xxfclib/formats/gz/xx_gz.h>
#include <xxfclib/formats/ha/xx_ha.h>
#include <xxfclib/formats/hap/xx_hap.h>
#include <xxfclib/formats/hdcopy/xx_hdcopy.h>
#include <xxfclib/formats/hfe/xx_hfe.h>
#include <xxfclib/formats/hlb/xx_hlb.h>
#include <xxfclib/formats/hog/xx_hog.h>
#include <xxfclib/formats/hog2/xx_hog2.h>
#include <xxfclib/formats/huf/xx_huf.h>
#include <xxfclib/formats/hzl/xx_hzl.h>
#include <xxfclib/formats/ibmpack/xx_ibmpack.h>
#include <xxfclib/formats/ibmspack/xx_ibmspack.h>
#include <xxfclib/formats/ibmzpak/xx_ibmzpak.h>
#include <xxfclib/formats/igf1/xx_igf1.h>
#include <xxfclib/formats/igf2/xx_igf2.h>
#include <xxfclib/formats/imd/xx_imd.h>
#include <xxfclib/formats/imp/xx_imp.h>
#include <xxfclib/formats/infogramesft/xx_infogramesft.h>
#include <xxfclib/formats/inteduft/xx_inteduft.h>
#include <xxfclib/formats/ipa/xx_ipa.h>
#include <xxfclib/formats/irixsa/xx_irixsa.h>
#include <xxfclib/formats/irwinpac/xx_irwinpac.h>
#include <xxfclib/formats/is11/xx_is11.h>
#include <xxfclib/formats/is3/xx_is3.h>
#include <xxfclib/formats/is5/xx_is5.h>
#include <xxfclib/formats/is7inx/xx_is7inx.h>
#include <xxfclib/formats/iso9660/xx_iso9660.h>
#include <xxfclib/formats/ivt/xx_ivt.h>
#include <xxfclib/formats/ixa/xx_ixa.h>
#include <xxfclib/formats/mlb_ft/xx_mlb_ft.h>
#include <xxfclib/formats/fss/xx_fss.h>
#include <xxfclib/formats/epf/xx_epf.h>
#include <xxfclib/formats/dfc/xx_dfc.h>
#include <xxfclib/formats/sfx_rsfx/xx_sfx_rsfx.h>
#include <xxfclib/formats/ka/xx_ka.h>
#include <xxfclib/formats/nextstep_diskimage/xx_nextstep_diskimage.h>
#include <xxfclib/formats/dn/xx_dn.h>
#include <xxfclib/formats/insa/xx_insa.h>
#include <xxfclib/formats/sfx_vms_dcx/xx_sfx_vms_dcx.h>
#include <xxfclib/formats/oberon/xx_oberon.h>
#include <xxfclib/formats/ppd/xx_ppd.h>
#include <xxfclib/formats/sfx_ad01/xx_sfx_ad01.h>
#include <xxfclib/formats/sfx_nss/xx_sfx_nss.h>
#include <xxfclib/formats/solitaire_deluxe/xx_solitaire_deluxe.h>
#include <xxfclib/formats/thebat_msb/xx_thebat_msb.h>
#include <xxfclib/formats/sfx_localzip/xx_sfx_localzip.h>
#include <xxfclib/formats/izpack/xx_izpack.h>
#include <xxfclib/formats/jam/xx_jam.h>
#include <xxfclib/formats/jar/xx_jar.h>
#include <xxfclib/formats/jasc/xx_jasc.h>
#include <xxfclib/formats/jbf/xx_jbf.h>
#include <xxfclib/formats/jboot/xx_jboot.h>
#include <xxfclib/formats/jetbbs/xx_jetbbs.h>
#include <xxfclib/formats/jffs2/xx_jffs2.h>
#include <xxfclib/formats/jgpak/xx_jgpak.h>
#include <xxfclib/formats/jm93/xx_jm93.h>
#include <xxfclib/formats/kboom/xx_kboom.h>
#include <xxfclib/formats/kolibrikpack/xx_kolibrikpack.h>
#include <xxfclib/formats/kpck/xx_kpck.h>
#include <xxfclib/formats/krml/xx_krml.h>
#include <xxfclib/formats/lbrcobol/xx_lbrcobol.h>
#include <xxfclib/formats/le/xx_le.h>
#include <xxfclib/formats/lha/xx_lha.h>
#include <xxfclib/formats/lif/xx_lif.h>
#include <xxfclib/formats/lifkd/xx_lifkd.h>
#include <xxfclib/formats/lim/xx_lim.h>
#include <xxfclib/formats/lingvoarc/xx_lingvoarc.h>
#include <xxfclib/formats/lizard/xx_lizard.h>
#include <xxfclib/formats/lofi/xx_lofi.h>
#include <xxfclib/formats/logfs/xx_logfs.h>
#include <xxfclib/formats/logitechcompress/xx_logitechcompress.h>
#include <xxfclib/formats/lpaq8/xx_lpaq8.h>
#include <xxfclib/formats/lspack10/xx_lspack10.h>
#include <xxfclib/formats/lsz/xx_lsz.h>
#include <xxfclib/formats/luks/xx_luks.h>
#include <xxfclib/formats/lx/xx_lx.h>
#include <xxfclib/formats/lz4/xx_lz4.h>
#include <xxfclib/formats/lz4demo/xx_lz4demo.h>
#include <xxfclib/formats/lz5/xx_lz5.h>
#include <xxfclib/formats/lzdiet/xx_lzdiet.h>
#include <xxfclib/formats/lzhcxp/xx_lzhcxp.h>
#include <xxfclib/formats/lzip/xx_lzip.h>
#include <xxfclib/formats/lzk00/xx_lzk00.h>
#include <xxfclib/formats/lzma/xx_lzma.h>
#include <xxfclib/formats/lzop/xx_lzop.h>
#include <xxfclib/formats/lzpis2/xx_lzpis2.h>
#include <xxfclib/formats/lzv1/xx_lzv1.h>
#include <xxfclib/formats/lzw15v/xx_lzw15v.h>
#include <xxfclib/formats/lzwd/xx_lzwd.h>
#include <xxfclib/formats/macbinary/xx_macbinary.h>
#include <xxfclib/formats/macho/xx_macho.h>
#include <xxfclib/formats/marc/xx_marc.h>
#include <xxfclib/formats/mathcad/xx_mathcad.h>
#include <xxfclib/formats/matter_ota/xx_matter_ota.h>
#include <xxfclib/formats/mbr/xx_mbr.h>
#include <xxfclib/formats/mcc/xx_mcc.h>
#include <xxfclib/formats/mdcd/xx_mdcd.h>
#include <xxfclib/formats/megatechvol/xx_megatechvol.h>
#include <xxfclib/formats/mh01/xx_mh01.h>
#include <xxfclib/formats/mi10/xx_mi10.h>
#include <xxfclib/formats/minidump/xx_minidump.h>
#include <xxfclib/formats/miz/xx_miz.h>
#include <xxfclib/formats/mpq/xx_mpq.h>
#include <xxfclib/formats/mrnz/xx_mrnz.h>
#include <xxfclib/formats/mscompress/xx_mscompress.h>
#include <xxfclib/formats/msdos/xx_msdos.h>
#include <xxfclib/formats/mtree/xx_mtree.h>
#include <xxfclib/formats/mva/xx_mva.h>
#include <xxfclib/formats/mwave/xx_mwave.h>
#include <xxfclib/formats/mxs/xx_mxs.h>
#include <xxfclib/formats/ne/xx_ne.h>
#include <xxfclib/formats/netware2/xx_netware2.h>
#include <xxfclib/formats/netwarepacked/xx_netwarepacked.h>
#include <xxfclib/formats/nid/xx_nid.h>
#include <xxfclib/formats/notetab/xx_notetab.h>
#include <xxfclib/formats/npack/xx_npack.h>
#include <xxfclib/formats/npm/xx_npm.h>
#include <xxfclib/formats/ntfs/xx_ntfs.h>
#include <xxfclib/formats/opc/xx_opc.h>
#include <xxfclib/formats/oraclesqueeze/xx_oraclesqueeze.h>
#include <xxfclib/formats/packimg/xx_packimg.h>
#include <xxfclib/formats/packit/xx_packit.h>
#include <xxfclib/formats/pain/xx_pain.h>
#include <xxfclib/formats/pakleo/xx_pakleo.h>
#include <xxfclib/formats/panorama/xx_panorama.h>
#include <xxfclib/formats/paperport/xx_paperport.h>
#include <xxfclib/formats/pax/xx_pax.h>
#include <xxfclib/formats/pcinstall/xx_pcinstall.h>
#include <xxfclib/formats/pcommos2/xx_pcommos2.h>
#include <xxfclib/formats/pcsecure/xx_pcsecure.h>
#include <xxfclib/formats/pcxlib/xx_pcxlib.h>
#include <xxfclib/formats/pdb/xx_pdb.h>
#include <xxfclib/formats/pdp11ar/xx_pdp11ar.h>
#include <xxfclib/formats/pe/xx_pe.h>
#include <xxfclib/formats/pea/xx_pea.h>
#include <xxfclib/formats/perform/xx_perform.h>
#include <xxfclib/formats/phar/xx_phar.h>
#include <xxfclib/formats/pkt/xx_pkt.h>
#include <xxfclib/formats/pma/xx_pma.h>
#include <xxfclib/formats/pmdiskcopy/xx_pmdiskcopy.h>
#include <xxfclib/formats/povlablzh/xx_povlablzh.h>
#include <xxfclib/formats/powerarc/xx_powerarc.h>
#include <xxfclib/formats/powerboardbbs/xx_powerboardbbs.h>
#include <xxfclib/formats/pp20/xx_pp20.h>
#include <xxfclib/formats/psdc/xx_psdc.h>
#include <xxfclib/formats/psn/xx_psn.h>
#include <xxfclib/formats/pyz/xx_pyz.h>
#include <xxfclib/formats/qcow/xx_qcow.h>
#include <xxfclib/formats/qda/xx_qda.h>
#include <xxfclib/formats/qip1/xx_qip1.h>
#include <xxfclib/formats/qip2/xx_qip2.h>
#include <xxfclib/formats/qnx6/xx_qnx6.h>
#include <xxfclib/formats/qnxbase/xx_qnxbase.h>
#include <xxfclib/formats/qrst/xx_qrst.h>
#include <xxfclib/formats/qualitas/xx_qualitas.h>
#include <xxfclib/formats/quantum/xx_quantum.h>
#include <xxfclib/formats/quarterdeckqp/xx_quarterdeckqp.h>
#include <xxfclib/formats/rar/xx_rar.h>
#include <xxfclib/formats/rawstac/xx_rawstac.h>
#include <xxfclib/formats/rcf/xx_rcf.h>
#include <xxfclib/formats/recognita/xx_recognita.h>
#include <xxfclib/formats/red/xx_red.h>
#include <xxfclib/formats/res/xx_res.h>
#include <xxfclib/formats/resourcefork/xx_resourcefork.h>
#include <xxfclib/formats/rid/xx_rid.h>
#include <xxfclib/formats/riversoft/xx_riversoft.h>
#include <xxfclib/formats/rnc/xx_rnc.h>
#include <xxfclib/formats/rnca/xx_rnca.h>
#include <xxfclib/formats/romfs/xx_romfs.h>
#include <xxfclib/formats/rompaq/xx_rompaq.h>
#include <xxfclib/formats/rsc/xx_rsc.h>
#include <xxfclib/formats/rsvk/xx_rsvk.h>
#include <xxfclib/formats/rta/xx_rta.h>
#include <xxfclib/formats/rtk/xx_rtk.h>
#include <xxfclib/formats/rtpatch/xx_rtpatch.h>
#include <xxfclib/formats/sabdu/xx_sabdu.h>
#include <xxfclib/formats/saf/xx_saf.h>
#include <xxfclib/formats/savedskf/xx_savedskf.h>
#include <xxfclib/formats/scf/xx_scf.h>
#include <xxfclib/formats/sci/xx_sci.h>
#include <xxfclib/formats/scl/xx_scl.h>
#include <xxfclib/formats/sco/xx_sco.h>
#include <xxfclib/formats/seaarc/xx_seaarc.h>
#include <xxfclib/formats/seadata/xx_seadata.h>
#include <xxfclib/formats/seama/xx_seama.h>
#include <xxfclib/formats/secondnature/xx_secondnature.h>
#include <xxfclib/formats/settlersft/xx_settlersft.h>
#include <xxfclib/formats/sfpack/xx_sfpack.h>
#include <xxfclib/formats/shar/xx_shar.h>
#include <xxfclib/formats/shrinkwrap/xx_shrinkwrap.h>
#include <xxfclib/formats/shrs/xx_shrs.h>
#include <xxfclib/formats/silmarilsft/xx_silmarilsft.h>
#include <xxfclib/formats/sinner/xx_sinner.h>
#include <xxfclib/formats/sls/xx_sls.h>
#include <xxfclib/formats/smsipak/xx_smsipak.h>
#include <xxfclib/formats/softpaq2/xx_softpaq2.h>
#include <xxfclib/formats/softronics/xx_softronics.h>
#include <xxfclib/formats/solarispkg/xx_solarispkg.h>
#include <xxfclib/formats/sos/xx_sos.h>
#include <xxfclib/formats/sparse/xx_sparse.h>
#include <xxfclib/formats/spis/xx_spis.h>
#include <xxfclib/formats/sfx_sbx_extractor/xx_sfx_sbx_extractor.h>
#include <xxfclib/formats/spk/xx_spk.h>
#include <xxfclib/formats/sq/xx_sq.h>
#include <xxfclib/formats/squashfs/xx_squashfs.h>
#include <xxfclib/formats/squeeze1/xx_squeeze1.h>
#include <xxfclib/formats/squeeze2/xx_squeeze2.h>
#include <xxfclib/formats/sqx/xx_sqx.h>
#include <xxfclib/formats/sqz/xx_sqz.h>
#include <xxfclib/formats/srec/xx_srec.h>
#include <xxfclib/formats/ssm/xx_ssm.h>
#include <xxfclib/formats/stac/xx_stac.h>
#include <xxfclib/formats/starkit/xx_starkit.h>
#include <xxfclib/formats/stk/xx_stk.h>
#include <xxfclib/formats/stork/xx_stork.h>
#include <xxfclib/formats/stuffit/xx_stuffit.h>
#include <xxfclib/formats/stunts/xx_stunts.h>
#include <xxfclib/formats/stylus/xx_stylus.h>
#include <xxfclib/formats/sw/xx_sw.h>
#include <xxfclib/formats/swag/xx_swag.h>
#include <xxfclib/formats/swagpacket/xx_swagpacket.h>
#include <xxfclib/formats/tar/xx_tar.h>
#include <xxfclib/formats/tar_bz2/xx_tar_bz2.h>
#include <xxfclib/formats/tar_compress/xx_tar_compress.h>
#include <xxfclib/formats/tar_gz/xx_tar_gz.h>
#include <xxfclib/formats/tar_lz4/xx_tar_lz4.h>
#include <xxfclib/formats/tar_lzip/xx_tar_lzip.h>
#include <xxfclib/formats/tar_lzma/xx_tar_lzma.h>
#include <xxfclib/formats/tar_lzop/xx_tar_lzop.h>
#include <xxfclib/formats/tar_nextstep/xx_tar_nextstep.h>
#include <xxfclib/formats/tar_xz/xx_tar_xz.h>
#include <xxfclib/formats/tar_zstd/xx_tar_zstd.h>
#include <xxfclib/formats/tarx1/xx_tarx1.h>
#include <xxfclib/formats/tarx2/xx_tarx2.h>
#include <xxfclib/formats/teacy/xx_teacy.h>
#include <xxfclib/formats/teledisk/xx_teledisk.h>
#include <xxfclib/formats/terse/xx_terse.h>
#include <xxfclib/formats/tgcf/xx_tgcf.h>
#include <xxfclib/formats/ti99arc/xx_ti99arc.h>
#include <xxfclib/formats/tivoli/xx_tivoli.h>
#include <xxfclib/formats/tnef/xx_tnef.h>
#include <xxfclib/formats/topspeed/xx_topspeed.h>
#include <xxfclib/formats/tplink/xx_tplink.h>
#include <xxfclib/formats/tps/xx_tps.h>
#include <xxfclib/formats/tpwm/xx_tpwm.h>
#include <xxfclib/formats/trc/xx_trc.h>
#include <xxfclib/formats/trcpak/xx_trcpak.h>
#include <xxfclib/formats/trdos/xx_trdos.h>
#include <xxfclib/formats/trx/xx_trx.h>
#include <xxfclib/formats/twoimg/xx_twoimg.h>
#include <xxfclib/formats/twrx/xx_twrx.h>
#include <xxfclib/formats/tws/xx_tws.h>
#include <xxfclib/formats/ubi/xx_ubi.h>
#include <xxfclib/formats/ubifs/xx_ubifs.h>
#include <xxfclib/formats/uboot/xx_uboot.h>
#include <xxfclib/formats/udf/xx_udf.h>
#include <xxfclib/formats/uefi_capsule/xx_uefi_capsule.h>
#include <xxfclib/formats/uefi_fv/xx_uefi_fv.h>
#include <xxfclib/formats/uimage/xx_uimage.h>
#include <xxfclib/formats/ulead/xx_ulead.h>
#include <xxfclib/formats/unixcompact/xx_unixcompact.h>
#include <xxfclib/formats/unixcompress/xx_unixcompress.h>
#include <xxfclib/formats/unixpack/xx_unixpack.h>
#include <xxfclib/formats/vhddynamic/xx_vhddynamic.h>
#include <xxfclib/formats/vmarc/xx_vmarc.h>
#include <xxfclib/formats/vmdk/xx_vmdk.h>
#include <xxfclib/formats/vmsdb/xx_vmsdb.h>
#include <xxfclib/formats/vmspcsi/xx_vmspcsi.h>
#include <xxfclib/formats/vmssaveset/xx_vmssaveset.h>
#include <xxfclib/formats/volitionvpft/xx_volitionvpft.h>
#include <xxfclib/formats/vxworks/xx_vxworks.h>
#include <xxfclib/formats/warc/xx_warc.h>
#include <xxfclib/formats/wiilz77/xx_wiilz77.h>
#include <xxfclib/formats/wim/xx_wim.h>
#include <xxfclib/formats/wince/xx_wince.h>
#include <xxfclib/formats/winlink/xx_winlink.h>
#include <xxfclib/formats/wintermutedcp/xx_wintermutedcp.h>
#include <xxfclib/formats/wintersoft/xx_wintersoft.h>
#include <xxfclib/formats/wolfft/xx_wolfft.h>
#include <xxfclib/formats/wpk/xx_wpk.h>
#include <xxfclib/formats/wrzl/xx_wrzl.h>
#include <xxfclib/formats/xar/xx_xar.h>
#include <xxfclib/formats/xeditpack/xx_xeditpack.h>
#include <xxfclib/formats/xlas/xx_xlas.h>
#include <xxfclib/formats/xorarchive/xx_xorarchive.h>
#include <xxfclib/formats/xpak/xx_xpak.h>
#include <xxfclib/formats/xz/xx_xz.h>
#include <xxfclib/formats/yaffs/xx_yaffs.h>
#include <xxfclib/formats/zap/xx_zap.h>
#include <xxfclib/formats/zcmp/xx_zcmp.h>
#include <xxfclib/formats/zfsf/xx_zfsf.h>
#include <xxfclib/formats/zie/xx_zie.h>
#include <xxfclib/formats/zip/xx_zip.h>
#include <xxfclib/formats/zlib/xx_zlib.h>
#include <xxfclib/formats/zlwb/xx_zlwb.h>
#include <xxfclib/formats/zoo/xx_zoo.h>
#include <xxfclib/formats/zoom/xx_zoom.h>
#include <xxfclib/formats/zpak/xx_zpak.h>
#include <xxfclib/formats/zpaq/xx_zpaq.h>
#include <xxfclib/formats/zstd/xx_zstd.h>
#include <xxfclib/formats/ztc/xx_ztc.h>
#include <xxfclib/formats/zxzip/xx_zxzip.h>
#include <xxfclib/formats/zz/xx_zz.h>
#include <xxfclib/formats/zzz/xx_zzz.h>

static Abstractformat *mk_exfat(xx_io_device *d, int64_t b) {
    xx_exfat *r = xx_exfat_create(d, b);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_dmk(xx_io_device *d, int64_t b) {
    xx_dmk *r = xx_dmk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dmk(void *p) { xx_dmk_free((xx_dmk *)p); }
static Abstractformat *mk_mfs(xx_io_device *d, int64_t b) {
    xx_mfs *r = xx_mfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mfs(void *p) { xx_mfs_free((xx_mfs *)p); }
static Abstractformat *mk_hfs(xx_io_device *d, int64_t b) {
    xx_hfs *r = xx_hfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hfs(void *p) { xx_hfs_free((xx_hfs *)p); }
static Abstractformat *mk_catsystem_kif(xx_io_device *d, int64_t b) {
    xx_catsystem_kif *r = xx_catsystem_kif_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_catsystem_kif(void *p) { xx_catsystem_kif_free((xx_catsystem_kif *)p); }
static Abstractformat *mk_malie_lib(xx_io_device *d, int64_t b) {
    xx_malie_lib *r = xx_malie_lib_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_malie_lib(void *p) { xx_malie_lib_free((xx_malie_lib *)p); }
static Abstractformat *mk_nexas_pac(xx_io_device *d, int64_t b) {
    xx_nexas_pac *r = xx_nexas_pac_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nexas_pac(void *p) { xx_nexas_pac_free((xx_nexas_pac *)p); }
static Abstractformat *mk_nitroplus_npa(xx_io_device *d, int64_t b) {
    xx_nitroplus_npa *r = xx_nitroplus_npa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nitroplus_npa(void *p) { xx_nitroplus_npa_free((xx_nitroplus_npa *)p); }
static Abstractformat *mk_cpm(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cpm(void *p) { xx_cpm_free((xx_cpm *)p); }
static Abstractformat *mk_cpm_apple_do(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_APPLE_DO);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_apple_po(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_APPLE_PO);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_pcw180(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_PCW180);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_cpc_system(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_CPC_SYSTEM);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_cpc_data(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_CPC_DATA);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_cf2dd(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_CF2DD);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_alpha(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_ALPHA);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_sdcard(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_SDCARD);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_pc1_2m(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_PC1_2M);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_cpm86_144feat(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_CPM86_144FEAT);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_p112(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_P112);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_p112_old(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_P112_OLD);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_nigdos(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_NIGDOS);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_epsqx10(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_EPSQX10);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_ibm_8ss(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_IBM_8SS);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_electroglas(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_ELECTROGLAS);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cpm_ibmpc_514ds(xx_io_device *d, int64_t b) {
    xx_cpm *r = xx_cpm_create_preset(d, b, XX_CPM_PRESET_IBMPC_514DS);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_ufs1(xx_io_device *d, int64_t b) {
    xx_ufs1 *r = xx_ufs1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ufs1(void *p) { xx_ufs1_free((xx_ufs1 *)p); }
static Abstractformat *mk_xva(xx_io_device *d, int64_t b) {
    xx_xva *r = xx_xva_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xva(void *p) { xx_xva_free((xx_xva *)p); }
static Abstractformat *mk_qlie_pack(xx_io_device *d, int64_t b) {
    xx_qlie_pack *r = xx_qlie_pack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qlie_pack(void *p) { xx_qlie_pack_free((xx_qlie_pack *)p); }
static Abstractformat *mk_hfsplus(xx_io_device *d, int64_t b) {
    xx_hfsplus *r = xx_hfsplus_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hfsplus(void *p) { xx_hfsplus_free((xx_hfsplus *)p); }
static Abstractformat *mk_partimage(xx_io_device *d, int64_t b) {
    xx_partimage *r = xx_partimage_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_partimage(void *p) { xx_partimage_free((xx_partimage *)p); }
static Abstractformat *mk_aaruformat(xx_io_device *d, int64_t b) {
    xx_aaruformat *r = xx_aaruformat_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_aaruformat(void *p) { xx_aaruformat_free((xx_aaruformat *)p); }
static Abstractformat *mk_acorn_adfs(xx_io_device *d, int64_t b) {
    xx_acorn_adfs *r = xx_acorn_adfs_create(d, b);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_acorn_adfs_linear(xx_io_device *d, int64_t b) {
    xx_acorn_adfs *r = xx_acorn_adfs_create(d, b);
    if (r) r->order = XX_ACORN_ADFS_ORDER_LINEAR;
    return r ? &r->format : NULL;
}
static void rm_acorn_adfs(void *p) { xx_acorn_adfs_free((xx_acorn_adfs *)p); }
static Abstractformat *mk_apple_dos33(xx_io_device *d, int64_t b) {
    xx_apple_dos33 *r = xx_apple_dos33_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apple_dos33(void *p) { xx_apple_dos33_free((xx_apple_dos33 *)p); }
static Abstractformat *mk_apple_pascal(xx_io_device *d, int64_t b) {
    xx_apple_pascal *r = xx_apple_pascal_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apple_pascal(void *p) { xx_apple_pascal_free((xx_apple_pascal *)p); }
static Abstractformat *mk_nitroplus_npk2(xx_io_device *d, int64_t b) {
    xx_nitroplus_npk2 *r = xx_nitroplus_npk2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nitroplus_npk2(void *p) { xx_nitroplus_npk2_free((xx_nitroplus_npk2 *)p); }
static Abstractformat *mk_thomson_sap(xx_io_device *d, int64_t b) {
    xx_thomson_sap *r = xx_thomson_sap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_thomson_sap(void *p) { xx_thomson_sap_free((xx_thomson_sap *)p); }
static Abstractformat *mk_anex86_hdi(xx_io_device *d, int64_t b) {
    xx_anex86_hdi *r = xx_anex86_hdi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_anex86_hdi(void *p) { xx_anex86_hdi_free((xx_anex86_hdi *)p); }
static void rm_exfat(void *p) { xx_exfat_free((xx_exfat *)p); }
static Abstractformat *mk_majiro(xx_io_device *d, int64_t b) {
    xx_majiro *r = xx_majiro_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_majiro(void *p) { xx_majiro_free((xx_majiro *)p); }
static Abstractformat *mk_wux(xx_io_device *d, int64_t b) {
    xx_wux *r = xx_wux_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wux(void *p) { xx_wux_free((xx_wux *)p); }
static Abstractformat *mk_sdi(xx_io_device *d, int64_t b) {
    xx_sdi *r = xx_sdi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sdi(void *p) { xx_sdi_free((xx_sdi *)p); }
static Abstractformat *mk_nhd(xx_io_device *d, int64_t b) {
    xx_nhd *r = xx_nhd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nhd(void *p) { xx_nhd_free((xx_nhd *)p); }
static Abstractformat *mk_virtual98(xx_io_device *d, int64_t b) {
    xx_virtual98 *r = xx_virtual98_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_virtual98(void *p) { xx_virtual98_free((xx_virtual98 *)p); }

static Abstractformat *mk_7zip(xx_io_device *d, int64_t b) {
    xx_7zip *r = xx_7zip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_7zip(void *p) { xx_7zip_free((xx_7zip *)p); }
static Abstractformat *mk_ace(xx_io_device *d, int64_t b) {
    xx_ace *r = xx_ace_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ace(void *p) { xx_ace_free((xx_ace *)p); }
static Abstractformat *mk_agis(xx_io_device *d, int64_t b) {
    xx_agis *r = xx_agis_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_agis(void *p) { xx_agis_free((xx_agis *)p); }
static Abstractformat *mk_aiaff(xx_io_device *d, int64_t b) {
    xx_aiaff *r = xx_aiaff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_aiaff(void *p) { xx_aiaff_free((xx_aiaff *)p); }
static Abstractformat *mk_ain(xx_io_device *d, int64_t b) {
    xx_ain *r = xx_ain_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ain(void *p) { xx_ain_free((xx_ain *)p); }
static Abstractformat *mk_aixbff(xx_io_device *d, int64_t b) {
    xx_aixbff *r = xx_aixbff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_aixbff(void *p) { xx_aixbff_free((xx_aixbff *)p); }
static Abstractformat *mk_aldus(xx_io_device *d, int64_t b) {
    xx_aldus *r = xx_aldus_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_aldus(void *p) { xx_aldus_free((xx_aldus *)p); }
static Abstractformat *mk_alz(xx_io_device *d, int64_t b) {
    xx_alz *r = xx_alz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_alz(void *p) { xx_alz_free((xx_alz *)p); }
static Abstractformat *mk_amigahunk(xx_io_device *d, int64_t b) {
    xx_amigahunk *r = xx_amigahunk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amigahunk(void *p) { xx_amigahunk_free((xx_amigahunk *)p); }
static Abstractformat *mk_amigalzx(xx_io_device *d, int64_t b) {
    xx_amigalzx *r = xx_amigalzx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amigalzx(void *p) { xx_amigalzx_free((xx_amigalzx *)p); }
static Abstractformat *mk_ampk(xx_io_device *d, int64_t b) {
    xx_ampk *r = xx_ampk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ampk(void *p) { xx_ampk_free((xx_ampk *)p); }
static Abstractformat *mk_androidboot(xx_io_device *d, int64_t b) {
    xx_androidboot *r = xx_androidboot_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_androidboot(void *p) { xx_androidboot_free((xx_androidboot *)p); }
static Abstractformat *mk_aodos(xx_io_device *d, int64_t b) {
    xx_aodos *r = xx_aodos_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_aodos(void *p) { xx_aodos_free((xx_aodos *)p); }
static Abstractformat *mk_ap4(xx_io_device *d, int64_t b) {
    xx_ap4 *r = xx_ap4_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ap4(void *p) { xx_ap4_free((xx_ap4 *)p); }
static Abstractformat *mk_apfs(xx_io_device *d, int64_t b) {
    xx_apfs *r = xx_apfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apfs(void *p) { xx_apfs_free((xx_apfs *)p); }
static Abstractformat *mk_apk(xx_io_device *d, int64_t b) {
    xx_apk *r = xx_apk_create(d, b);
    return r ? &r->zip.format : NULL;
}
static void rm_apk(void *p) { xx_apk_free((xx_apk *)p); }
static Abstractformat *mk_apple_disk_copy_6_ndif_image(xx_io_device *d, int64_t b) {
    xx_apple_disk_copy_6_ndif_image *r = xx_apple_disk_copy_6_ndif_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apple_disk_copy_6_ndif_image(void *p) { xx_apple_disk_copy_6_ndif_image_free((xx_apple_disk_copy_6_ndif_image *)p); }
static Abstractformat *mk_apple_sparse_bundle(xx_io_device *d, int64_t b) {
    xx_apple_sparse_bundle *r = xx_apple_sparse_bundle_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apple_sparse_bundle(void *p) { xx_apple_sparse_bundle_free((xx_apple_sparse_bundle *)p); }
static Abstractformat *mk_applesingle(xx_io_device *d, int64_t b) {
    xx_applesingle *r = xx_applesingle_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_applesingle(void *p) { xx_applesingle_free((xx_applesingle *)p); }
static Abstractformat *mk_apricot(xx_io_device *d, int64_t b) {
    xx_apricot *r = xx_apricot_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apricot(void *p) { xx_apricot_free((xx_apricot *)p); }
static Abstractformat *mk_ar(xx_io_device *d, int64_t b) {
    xx_ar *r = xx_ar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ar(void *p) { xx_ar_free((xx_ar *)p); }
static Abstractformat *mk_arcadyan(xx_io_device *d, int64_t b) {
    xx_arcadyan *r = xx_arcadyan_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_arcadyan(void *p) { xx_arcadyan_free((xx_arcadyan *)p); }
static Abstractformat *mk_arcfs(xx_io_device *d, int64_t b) {
    xx_arcfs *r = xx_arcfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_arcfs(void *p) { xx_arcfs_free((xx_arcfs *)p); }
static Abstractformat *mk_arcv(xx_io_device *d, int64_t b) {
    xx_arcv *r = xx_arcv_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_arcv(void *p) { xx_arcv_free((xx_arcv *)p); }
static Abstractformat *mk_arcv2(xx_io_device *d, int64_t b) {
    xx_arcv2 *r = xx_arcv2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_arcv2(void *p) { xx_arcv2_free((xx_arcv2 *)p); }
static Abstractformat *mk_arcv4(xx_io_device *d, int64_t b) {
    xx_arcv4 *r = xx_arcv4_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_arcv4(void *p) { xx_arcv4_free((xx_arcv4 *)p); }
static Abstractformat *mk_arj(xx_io_device *d, int64_t b) {
    xx_arj *r = xx_arj_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_arj(void *p) { xx_arj_free((xx_arj *)p); }
static Abstractformat *mk_arq(xx_io_device *d, int64_t b) {
    xx_arq *r = xx_arq_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_arq(void *p) { xx_arq_free((xx_arq *)p); }
static Abstractformat *mk_artipack(xx_io_device *d, int64_t b) {
    xx_artipack *r = xx_artipack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_artipack(void *p) { xx_artipack_free((xx_artipack *)p); }
static Abstractformat *mk_arx(xx_io_device *d, int64_t b) {
    xx_arx *r = xx_arx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_arx(void *p) { xx_arx_free((xx_arx *)p); }
static Abstractformat *mk_asar(xx_io_device *d, int64_t b) {
    xx_asar *r = xx_asar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_asar(void *p) { xx_asar_free((xx_asar *)p); }
static Abstractformat *mk_ascend(xx_io_device *d, int64_t b) {
    xx_ascend *r = xx_ascend_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ascend(void *p) { xx_ascend_free((xx_ascend *)p); }
static Abstractformat *mk_ascendbackup(xx_io_device *d, int64_t b) {
    xx_ascendbackup *r = xx_ascendbackup_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ascendbackup(void *p) { xx_ascendbackup_free((xx_ascendbackup *)p); }
static Abstractformat *mk_ash0(xx_io_device *d, int64_t b) {
    xx_ash0 *r = xx_ash0_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ash0(void *p) { xx_ash0_free((xx_ash0 *)p); }
static Abstractformat *mk_asymetrix(xx_io_device *d, int64_t b) {
    xx_asymetrix *r = xx_asymetrix_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_asymetrix(void *p) { xx_asymetrix_free((xx_asymetrix *)p); }
static Abstractformat *mk_atarist(xx_io_device *d, int64_t b) {
    xx_atarist *r = xx_atarist_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_atarist(void *p) { xx_atarist_free((xx_atarist *)p); }
static Abstractformat *mk_autel(xx_io_device *d, int64_t b) {
    xx_autel *r = xx_autel_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_autel(void *p) { xx_autel_free((xx_autel *)p); }
static Abstractformat *mk_bagf(xx_io_device *d, int64_t b) {
    xx_bagf *r = xx_bagf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bagf(void *p) { xx_bagf_free((xx_bagf *)p); }
static Abstractformat *mk_battleisle(xx_io_device *d, int64_t b) {
    xx_battleisle *r = xx_battleisle_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_battleisle(void *p) { xx_battleisle_free((xx_battleisle *)p); }
static Abstractformat *mk_bcm(xx_io_device *d, int64_t b) {
    xx_bcm *r = xx_bcm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bcm(void *p) { xx_bcm_free((xx_bcm *)p); }
static Abstractformat *mk_bcw(xx_io_device *d, int64_t b) {
    xx_bcw *r = xx_bcw_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bcw(void *p) { xx_bcw_free((xx_bcw *)p); }
static Abstractformat *mk_beatthehouse(xx_io_device *d, int64_t b) {
    xx_beatthehouse *r = xx_beatthehouse_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_beatthehouse(void *p) { xx_beatthehouse_free((xx_beatthehouse *)p); }
static Abstractformat *mk_beospkg(xx_io_device *d, int64_t b) {
    xx_beospkg *r = xx_beospkg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_beospkg(void *p) { xx_beospkg_free((xx_beospkg *)p); }
static Abstractformat *mk_bigaf(xx_io_device *d, int64_t b) {
    xx_bigaf *r = xx_bigaf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bigaf(void *p) { xx_bigaf_free((xx_bigaf *)p); }
static Abstractformat *mk_bigf(xx_io_device *d, int64_t b) {
    xx_bigf *r = xx_bigf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bigf(void *p) { xx_bigf_free((xx_bigf *)p); }
static Abstractformat *mk_binaryii(xx_io_device *d, int64_t b) {
    xx_binaryii *r = xx_binaryii_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_binaryii(void *p) { xx_binaryii_free((xx_binaryii *)p); }
static Abstractformat *mk_binder(xx_io_device *d, int64_t b) {
    xx_binder *r = xx_binder_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_binder(void *p) { xx_binder_free((xx_binder *)p); }
static Abstractformat *mk_binhdr(xx_io_device *d, int64_t b) {
    xx_binhdr *r = xx_binhdr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_binhdr(void *p) { xx_binhdr_free((xx_binhdr *)p); }
static Abstractformat *mk_binhex(xx_io_device *d, int64_t b) {
    xx_binhex *r = xx_binhex_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_binhex(void *p) { xx_binhex_free((xx_binhex *)p); }
static Abstractformat *mk_bnd(xx_io_device *d, int64_t b) {
    xx_bnd *r = xx_bnd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bnd(void *p) { xx_bnd_free((xx_bnd *)p); }
static Abstractformat *mk_boo(xx_io_device *d, int64_t b) {
    xx_boo *r = xx_boo_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_boo(void *p) { xx_boo_free((xx_boo *)p); }
static Abstractformat *mk_borlandpack(xx_io_device *d, int64_t b) {
    xx_borlandpack *r = xx_borlandpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_borlandpack(void *p) { xx_borlandpack_free((xx_borlandpack *)p); }
static Abstractformat *mk_brotli(xx_io_device *d, int64_t b) {
    xx_brotli *r = xx_brotli_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_brotli(void *p) { xx_brotli_free((xx_brotli *)p); }
static Abstractformat *mk_bsn(xx_io_device *d, int64_t b) {
    xx_bsn *r = xx_bsn_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bsn(void *p) { xx_bsn_free((xx_bsn *)p); }
static Abstractformat *mk_btrfs(xx_io_device *d, int64_t b) {
    xx_btrfs *r = xx_btrfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_btrfs(void *p) { xx_btrfs_free((xx_btrfs *)p); }
static Abstractformat *mk_bvrp(xx_io_device *d, int64_t b) {
    xx_bvrp *r = xx_bvrp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bvrp(void *p) { xx_bvrp_free((xx_bvrp *)p); }
static Abstractformat *mk_bwcf(xx_io_device *d, int64_t b) {
    xx_bwcf *r = xx_bwcf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bwcf(void *p) { xx_bwcf_free((xx_bwcf *)p); }
static Abstractformat *mk_bwf(xx_io_device *d, int64_t b) {
    xx_bwf *r = xx_bwf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bwf(void *p) { xx_bwf_free((xx_bwf *)p); }
static Abstractformat *mk_bz2(xx_io_device *d, int64_t b) {
    xx_bz2 *r = xx_bz2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bz2(void *p) { xx_bz2_free((xx_bz2 *)p); }
static Abstractformat *mk_c64wraptor(xx_io_device *d, int64_t b) {
    xx_c64wraptor *r = xx_c64wraptor_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_c64wraptor(void *p) { xx_c64wraptor_free((xx_c64wraptor *)p); }
static Abstractformat *mk_cab(xx_io_device *d, int64_t b) {
    xx_cab *r = xx_cab_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cab(void *p) { xx_cab_free((xx_cab *)p); }
static Abstractformat *mk_cat(xx_io_device *d, int64_t b) {
    xx_cat *r = xx_cat_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cat(void *p) { xx_cat_free((xx_cat *)p); }
static Abstractformat *mk_cpoint(xx_io_device *d, int64_t b) {
    xx_cpoint *r = xx_cpoint_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cpoint(void *p) { xx_cpoint_free((xx_cpoint *)p); }
static Abstractformat *mk_cazip(xx_io_device *d, int64_t b) {
    xx_cazip *r = xx_cazip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cazip(void *p) { xx_cazip_free((xx_cazip *)p); }
static Abstractformat *mk_cfl(xx_io_device *d, int64_t b) {
    xx_cfl *r = xx_cfl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cfl(void *p) { xx_cfl_free((xx_cfl *)p); }
static Abstractformat *mk_chieflz(xx_io_device *d, int64_t b) {
    xx_chieflz *r = xx_chieflz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_chieflz(void *p) { xx_chieflz_free((xx_chieflz *)p); }
static Abstractformat *mk_chieflzmulti(xx_io_device *d, int64_t b) {
    xx_chieflzmulti *r = xx_chieflzmulti_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_chieflzmulti(void *p) { xx_chieflzmulti_free((xx_chieflzmulti *)p); }
static Abstractformat *mk_chk(xx_io_device *d, int64_t b) {
    xx_chk *r = xx_chk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_chk(void *p) { xx_chk_free((xx_chk *)p); }
static Abstractformat *mk_ciso(xx_io_device *d, int64_t b) {
    xx_ciso *r = xx_ciso_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ciso(void *p) { xx_ciso_free((xx_ciso *)p); }
static Abstractformat *mk_claylz(xx_io_device *d, int64_t b) {
    xx_claylz *r = xx_claylz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_claylz(void *p) { xx_claylz_free((xx_claylz *)p); }
static Abstractformat *mk_clp(xx_io_device *d, int64_t b) {
    xx_clp *r = xx_clp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_clp(void *p) { xx_clp_free((xx_clp *)p); }
static Abstractformat *mk_cmp(xx_io_device *d, int64_t b) {
    xx_cmp *r = xx_cmp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cmp(void *p) { xx_cmp_free((xx_cmp *)p); }
static Abstractformat *mk_com(xx_io_device *d, int64_t b) {
    xx_com *r = xx_com_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_com(void *p) { xx_com_free((xx_com *)p); }
static Abstractformat *mk_compactpro(xx_io_device *d, int64_t b) {
    xx_compactpro *r = xx_compactpro_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_compactpro(void *p) { xx_compactpro_free((xx_compactpro *)p); }
static Abstractformat *mk_compaqlzh(xx_io_device *d, int64_t b) {
    xx_compaqlzh *r = xx_compaqlzh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_compaqlzh(void *p) { xx_compaqlzh_free((xx_compaqlzh *)p); }
static Abstractformat *mk_copydisk(xx_io_device *d, int64_t b) {
    xx_copydisk *r = xx_copydisk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_copydisk(void *p) { xx_copydisk_free((xx_copydisk *)p); }
static Abstractformat *mk_copyqm(xx_io_device *d, int64_t b) {
    xx_copyqm *r = xx_copyqm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_copyqm(void *p) { xx_copyqm_free((xx_copyqm *)p); }
static Abstractformat *mk_copyqmexe(xx_io_device *d, int64_t b) {
    xx_copyqmexe *r = xx_copyqmexe_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_copyqmexe(void *p) { xx_copyqmexe_free((xx_copyqmexe *)p); }
static Abstractformat *mk_corelltec(xx_io_device *d, int64_t b) {
    xx_corelltec *r = xx_corelltec_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_corelltec(void *p) { xx_corelltec_free((xx_corelltec *)p); }
static Abstractformat *mk_cpio(xx_io_device *d, int64_t b) {
    xx_cpio *r = xx_cpio_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cpio(void *p) { xx_cpio_free((xx_cpio *)p); }
static Abstractformat *mk_cpx(xx_io_device *d, int64_t b) {
    xx_cpx *r = xx_cpx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cpx(void *p) { xx_cpx_free((xx_cpx *)p); }
static Abstractformat *mk_cramfs(xx_io_device *d, int64_t b) {
    xx_cramfs *r = xx_cramfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cramfs(void *p) { xx_cramfs_free((xx_cramfs *)p); }
static Abstractformat *mk_cru(xx_io_device *d, int64_t b) {
    xx_cru *r = xx_cru_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cru(void *p) { xx_cru_free((xx_cru *)p); }
static Abstractformat *mk_csidos(xx_io_device *d, int64_t b) {
    xx_csidos *r = xx_csidos_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_csidos(void *p) { xx_csidos_free((xx_csidos *)p); }
static Abstractformat *mk_csman(xx_io_device *d, int64_t b) {
    xx_csman *r = xx_csman_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_csman(void *p) { xx_csman_free((xx_csman *)p); }
static Abstractformat *mk_dbz(xx_io_device *d, int64_t b) {
    xx_dbz *r = xx_dbz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dbz(void *p) { xx_dbz_free((xx_dbz *)p); }
static Abstractformat *mk_dclft(xx_io_device *d, int64_t b) {
    xx_dclft *r = xx_dclft_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dclft(void *p) { xx_dclft_free((xx_dclft *)p); }
static Abstractformat *mk_dclraw(xx_io_device *d, int64_t b) {
    xx_dclraw *r = xx_dclraw_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dclraw(void *p) { xx_dclraw_free((xx_dclraw *)p); }
static Abstractformat *mk_debugscr(xx_io_device *d, int64_t b) {
    xx_debugscr *r = xx_debugscr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_debugscr(void *p) { xx_debugscr_free((xx_debugscr *)p); }
static Abstractformat *mk_dex(xx_io_device *d, int64_t b) {
    xx_dex *r = xx_dex_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dex(void *p) { xx_dex_free((xx_dex *)p); }
static Abstractformat *mk_diskdoubler(xx_io_device *d, int64_t b) {
    xx_diskdoubler *r = xx_diskdoubler_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_diskdoubler(void *p) { xx_diskdoubler_free((xx_diskdoubler *)p); }
static Abstractformat *mk_diskdupe(xx_io_device *d, int64_t b) {
    xx_diskdupe *r = xx_diskdupe_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_diskdupe(void *p) { xx_diskdupe_free((xx_diskdupe *)p); }
static Abstractformat *mk_diskexpress(xx_io_device *d, int64_t b) {
    xx_diskexpress *r = xx_diskexpress_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_diskexpress(void *p) { xx_diskexpress_free((xx_diskexpress *)p); }
static Abstractformat *mk_diskjuggler(xx_io_device *d, int64_t b) {
    xx_diskjuggler *r = xx_diskjuggler_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_diskjuggler(void *p) { xx_diskjuggler_free((xx_diskjuggler *)p); }
static Abstractformat *mk_dkbs(xx_io_device *d, int64_t b) {
    xx_dkbs *r = xx_dkbs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dkbs(void *p) { xx_dkbs_free((xx_dkbs *)p); }
static Abstractformat *mk_dlink_tlv(xx_io_device *d, int64_t b) {
    xx_dlink_tlv *r = xx_dlink_tlv_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dlink_tlv(void *p) { xx_dlink_tlv_free((xx_dlink_tlv *)p); }
static Abstractformat *mk_dlke(xx_io_device *d, int64_t b) {
    xx_dlke *r = xx_dlke_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dlke(void *p) { xx_dlke_free((xx_dlke *)p); }
static Abstractformat *mk_dlob(xx_io_device *d, int64_t b) {
    xx_dlob *r = xx_dlob_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dlob(void *p) { xx_dlob_free((xx_dlob *)p); }
static Abstractformat *mk_dmapacked(xx_io_device *d, int64_t b) {
    xx_dmapacked *r = xx_dmapacked_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dmapacked(void *p) { xx_dmapacked_free((xx_dmapacked *)p); }
static Abstractformat *mk_dmg(xx_io_device *d, int64_t b) {
    xx_dmg *r = xx_dmg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dmg(void *p) { xx_dmg_free((xx_dmg *)p); }
static Abstractformat *mk_dms(xx_io_device *d, int64_t b) {
    xx_dms *r = xx_dms_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dms(void *p) { xx_dms_free((xx_dms *)p); }
static Abstractformat *mk_dos16m(xx_io_device *d, int64_t b) {
    xx_dos16m *r = xx_dos16m_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dos16m(void *p) { xx_dos16m_free((xx_dos16m *)p); }
static Abstractformat *mk_dpk(xx_io_device *d, int64_t b) {
    xx_dpk *r = xx_dpk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dpk(void *p) { xx_dpk_free((xx_dpk *)p); }
static Abstractformat *mk_dsl2(xx_io_device *d, int64_t b) {
    xx_dsl2 *r = xx_dsl2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dsl2(void *p) { xx_dsl2_free((xx_dsl2 *)p); }
static Abstractformat *mk_dtb(xx_io_device *d, int64_t b) {
    xx_dtb *r = xx_dtb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dtb(void *p) { xx_dtb_free((xx_dtb *)p); }
static Abstractformat *mk_dtpacked(xx_io_device *d, int64_t b) {
    xx_dtpacked *r = xx_dtpacked_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dtpacked(void *p) { xx_dtpacked_free((xx_dtpacked *)p); }
static Abstractformat *mk_ea(xx_io_device *d, int64_t b) {
    xx_ea *r = xx_ea_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ea(void *p) { xx_ea_free((xx_ea *)p); }
static Abstractformat *mk_ealib(xx_io_device *d, int64_t b) {
    xx_ealib *r = xx_ealib_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ealib(void *p) { xx_ealib_free((xx_ealib *)p); }
static Abstractformat *mk_elm(xx_io_device *d, int64_t b) {
    xx_elm *r = xx_elm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_elm(void *p) { xx_elm_free((xx_elm *)p); }
static Abstractformat *mk_earefpack(xx_io_device *d, int64_t b) {
    xx_earefpack *r = xx_earefpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_earefpack(void *p) { xx_earefpack_free((xx_earefpack *)p); }
static Abstractformat *mk_ecmpacked(xx_io_device *d, int64_t b) {
    xx_ecmpacked *r = xx_ecmpacked_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ecmpacked(void *p) { xx_ecmpacked_free((xx_ecmpacked *)p); }
static Abstractformat *mk_ecos(xx_io_device *d, int64_t b) {
    xx_ecos *r = xx_ecos_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ecos(void *p) { xx_ecos_free((xx_ecos *)p); }
static Abstractformat *mk_edc(xx_io_device *d, int64_t b) {
    xx_edc *r = xx_edc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_edc(void *p) { xx_edc_free((xx_edc *)p); }
static Abstractformat *mk_edilzss(xx_io_device *d, int64_t b) {
    xx_edilzss *r = xx_edilzss_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_edilzss(void *p) { xx_edilzss_free((xx_edilzss *)p); }
static Abstractformat *mk_elf(xx_io_device *d, int64_t b) {
    xx_elf *r = xx_elf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_elf(void *p) { xx_elf_free((xx_elf *)p); }
static Abstractformat *mk_emt(xx_io_device *d, int64_t b) {
    xx_emt *r = xx_emt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_emt(void *p) { xx_emt_free((xx_emt *)p); }
static Abstractformat *mk_encfw(xx_io_device *d, int64_t b) {
    xx_encfw *r = xx_encfw_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_encfw(void *p) { xx_encfw_free((xx_encfw *)p); }
static Abstractformat *mk_encrpted_img(xx_io_device *d, int64_t b) {
    xx_encrpted_img *r = xx_encrpted_img_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_encrpted_img(void *p) { xx_encrpted_img_free((xx_encrpted_img *)p); }
static Abstractformat *mk_encrypted_apple_disk_image(xx_io_device *d, int64_t b) {
    xx_encrypted_apple_disk_image *r = xx_encrypted_apple_disk_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_encrypted_apple_disk_image(void *p) { xx_encrypted_apple_disk_image_free((xx_encrypted_apple_disk_image *)p); }
static Abstractformat *mk_ext(xx_io_device *d, int64_t b) {
    xx_ext *r = xx_ext_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ext(void *p) { xx_ext_free((xx_ext *)p); }
static Abstractformat *mk_erofs(xx_io_device *d, int64_t b) {
    xx_erofs *r = xx_erofs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_erofs(void *p) { xx_erofs_free((xx_erofs *)p); }
static Abstractformat *mk_fat(xx_io_device *d, int64_t b) {
    xx_fat *r = xx_fat_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fat(void *p) { xx_fat_free((xx_fat *)p); }
static Abstractformat *mk_fdi(xx_io_device *d, int64_t b) {
    xx_fdi *r = xx_fdi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fdi(void *p) { xx_fdi_free((xx_fdi *)p); }
static Abstractformat *mk_finear(xx_io_device *d, int64_t b) {
    xx_finear *r = xx_finear_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_finear(void *p) { xx_finear_free((xx_finear *)p); }
static Abstractformat *mk_fiz(xx_io_device *d, int64_t b) {
    xx_fiz *r = xx_fiz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fiz(void *p) { xx_fiz_free((xx_fiz *)p); }
static Abstractformat *mk_fld(xx_io_device *d, int64_t b) {
    xx_fld *r = xx_fld_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fld(void *p) { xx_fld_free((xx_fld *)p); }
static Abstractformat *mk_fls(xx_io_device *d, int64_t b) {
    xx_fls *r = xx_fls_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fls(void *p) { xx_fls_free((xx_fls *)p); }
static Abstractformat *mk_fmc1(xx_io_device *d, int64_t b) {
    xx_fmc1 *r = xx_fmc1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fmc1(void *p) { xx_fmc1_free((xx_fmc1 *)p); }
static Abstractformat *mk_fpak(xx_io_device *d, int64_t b) {
    xx_fpak *r = xx_fpak_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fpak(void *p) { xx_fpak_free((xx_fpak *)p); }
static Abstractformat *mk_freearc(xx_io_device *d, int64_t b) {
    xx_freearc *r = xx_freearc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_freearc(void *p) { xx_freearc_free((xx_freearc *)p); }
static Abstractformat *mk_frontpagetheme(xx_io_device *d, int64_t b) {
    xx_frontpagetheme *r = xx_frontpagetheme_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_frontpagetheme(void *p) { xx_frontpagetheme_free((xx_frontpagetheme *)p); }
static Abstractformat *mk_ftcomp(xx_io_device *d, int64_t b) {
    xx_ftcomp *r = xx_ftcomp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ftcomp(void *p) { xx_ftcomp_free((xx_ftcomp *)p); }
static Abstractformat *mk_gamos(xx_io_device *d, int64_t b) {
    xx_gamos *r = xx_gamos_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gamos(void *p) { xx_gamos_free((xx_gamos *)p); }
static Abstractformat *mk_gashuff(xx_io_device *d, int64_t b) {
    xx_gashuff *r = xx_gashuff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gashuff(void *p) { xx_gashuff_free((xx_gashuff *)p); }
static Abstractformat *mk_genius(xx_io_device *d, int64_t b) {
    xx_genius *r = xx_genius_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genius(void *p) { xx_genius_free((xx_genius *)p); }
static Abstractformat *mk_gitobject(xx_io_device *d, int64_t b) {
    xx_gitobject *r = xx_gitobject_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gitobject(void *p) { xx_gitobject_free((xx_gitobject *)p); }
static Abstractformat *mk_gksetup(xx_io_device *d, int64_t b) {
    xx_gksetup *r = xx_gksetup_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gksetup(void *p) { xx_gksetup_free((xx_gksetup *)p); }
static Abstractformat *mk_glu(xx_io_device *d, int64_t b) {
    xx_glu *r = xx_glu_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_glu(void *p) { xx_glu_free((xx_glu *)p); }
static Abstractformat *mk_gob(xx_io_device *d, int64_t b) {
    xx_gob *r = xx_gob_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gob(void *p) { xx_gob_free((xx_gob *)p); }
static Abstractformat *mk_gpfpack(xx_io_device *d, int64_t b) {
    xx_gpfpack *r = xx_gpfpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gpfpack(void *p) { xx_gpfpack_free((xx_gpfpack *)p); }
static Abstractformat *mk_gpt(xx_io_device *d, int64_t b) {
    xx_gpt *r = xx_gpt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gpt(void *p) { xx_gpt_free((xx_gpt *)p); }
static Abstractformat *mk_grasp(xx_io_device *d, int64_t b) {
    xx_grasp *r = xx_grasp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_grasp(void *p) { xx_grasp_free((xx_grasp *)p); }
static Abstractformat *mk_gst(xx_io_device *d, int64_t b) {
    xx_gst *r = xx_gst_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gst(void *p) { xx_gst_free((xx_gst *)p); }
static Abstractformat *mk_gtu(xx_io_device *d, int64_t b) {
    xx_gtu *r = xx_gtu_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gtu(void *p) { xx_gtu_free((xx_gtu *)p); }
static Abstractformat *mk_gxl(xx_io_device *d, int64_t b) {
    xx_gxl *r = xx_gxl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gxl(void *p) { xx_gxl_free((xx_gxl *)p); }
static Abstractformat *mk_gz(xx_io_device *d, int64_t b) {
    xx_gz *r = xx_gz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gz(void *p) { xx_gz_free((xx_gz *)p); }
static Abstractformat *mk_ha(xx_io_device *d, int64_t b) {
    xx_ha *r = xx_ha_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ha(void *p) { xx_ha_free((xx_ha *)p); }
static Abstractformat *mk_hap(xx_io_device *d, int64_t b) {
    xx_hap *r = xx_hap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hap(void *p) { xx_hap_free((xx_hap *)p); }
static Abstractformat *mk_hdcopy(xx_io_device *d, int64_t b) {
    xx_hdcopy *r = xx_hdcopy_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hdcopy(void *p) { xx_hdcopy_free((xx_hdcopy *)p); }
static Abstractformat *mk_hfe(xx_io_device *d, int64_t b) {
    xx_hfe *r = xx_hfe_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hfe(void *p) { xx_hfe_free((xx_hfe *)p); }
static Abstractformat *mk_hlb(xx_io_device *d, int64_t b) {
    xx_hlb *r = xx_hlb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hlb(void *p) { xx_hlb_free((xx_hlb *)p); }
static Abstractformat *mk_hog(xx_io_device *d, int64_t b) {
    xx_hog *r = xx_hog_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hog(void *p) { xx_hog_free((xx_hog *)p); }
static Abstractformat *mk_hog2(xx_io_device *d, int64_t b) {
    xx_hog2 *r = xx_hog2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hog2(void *p) { xx_hog2_free((xx_hog2 *)p); }
static Abstractformat *mk_huf(xx_io_device *d, int64_t b) {
    xx_huf *r = xx_huf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_huf(void *p) { xx_huf_free((xx_huf *)p); }
static Abstractformat *mk_hxc_stream_hfe(xx_io_device *d, int64_t b) {
    xx_hxc_stream_hfe *r = xx_hxc_stream_hfe_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hxc_stream_hfe(void *p) { xx_hxc_stream_hfe_free((xx_hxc_stream_hfe *)p); }
static Abstractformat *mk_hzl(xx_io_device *d, int64_t b) {
    xx_hzl *r = xx_hzl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hzl(void *p) { xx_hzl_free((xx_hzl *)p); }
static Abstractformat *mk_ibmpack(xx_io_device *d, int64_t b) {
    xx_ibmpack *r = xx_ibmpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ibmpack(void *p) { xx_ibmpack_free((xx_ibmpack *)p); }
static Abstractformat *mk_ibmspack(xx_io_device *d, int64_t b) {
    xx_ibmspack *r = xx_ibmspack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ibmspack(void *p) { xx_ibmspack_free((xx_ibmspack *)p); }
static Abstractformat *mk_ibmzpak(xx_io_device *d, int64_t b) {
    xx_ibmzpak *r = xx_ibmzpak_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ibmzpak(void *p) { xx_ibmzpak_free((xx_ibmzpak *)p); }
static Abstractformat *mk_igf1(xx_io_device *d, int64_t b) {
    xx_igf1 *r = xx_igf1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_igf1(void *p) { xx_igf1_free((xx_igf1 *)p); }
static Abstractformat *mk_igf2(xx_io_device *d, int64_t b) {
    xx_igf2 *r = xx_igf2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_igf2(void *p) { xx_igf2_free((xx_igf2 *)p); }
static Abstractformat *mk_imd(xx_io_device *d, int64_t b) {
    xx_imd *r = xx_imd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_imd(void *p) { xx_imd_free((xx_imd *)p); }
static Abstractformat *mk_imp(xx_io_device *d, int64_t b) {
    xx_imp *r = xx_imp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_imp(void *p) { xx_imp_free((xx_imp *)p); }
static Abstractformat *mk_infogramesft(xx_io_device *d, int64_t b) {
    xx_infogramesft *r = xx_infogramesft_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_infogramesft(void *p) { xx_infogramesft_free((xx_infogramesft *)p); }
static Abstractformat *mk_inteduft(xx_io_device *d, int64_t b) {
    xx_inteduft *r = xx_inteduft_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_inteduft(void *p) { xx_inteduft_free((xx_inteduft *)p); }
static Abstractformat *mk_ipa(xx_io_device *d, int64_t b) {
    xx_ipa *r = xx_ipa_create(d, b);
    return r ? &r->zip.format : NULL;
}
static void rm_ipa(void *p) { xx_ipa_free((xx_ipa *)p); }
static Abstractformat *mk_irixsa(xx_io_device *d, int64_t b) {
    xx_irixsa *r = xx_irixsa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_irixsa(void *p) { xx_irixsa_free((xx_irixsa *)p); }
static Abstractformat *mk_irwinpac(xx_io_device *d, int64_t b) {
    xx_irwinpac *r = xx_irwinpac_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_irwinpac(void *p) { xx_irwinpac_free((xx_irwinpac *)p); }
static Abstractformat *mk_is11(xx_io_device *d, int64_t b) {
    xx_is11 *r = xx_is11_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_is11(void *p) { xx_is11_free((xx_is11 *)p); }
static Abstractformat *mk_is3(xx_io_device *d, int64_t b) {
    xx_is3 *r = xx_is3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_is3(void *p) { xx_is3_free((xx_is3 *)p); }
static Abstractformat *mk_is5(xx_io_device *d, int64_t b) {
    xx_is5 *r = xx_is5_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_is5(void *p) { xx_is5_free((xx_is5 *)p); }
static Abstractformat *mk_is7inx(xx_io_device *d, int64_t b) {
    xx_is7inx *r = xx_is7inx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_is7inx(void *p) { xx_is7inx_free((xx_is7inx *)p); }
static Abstractformat *mk_iso9660(xx_io_device *d, int64_t b) {
    xx_iso9660 *r = xx_iso9660_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_iso9660(void *p) { xx_iso9660_free((xx_iso9660 *)p); }
static Abstractformat *mk_ivt(xx_io_device *d, int64_t b) {
    xx_ivt *r = xx_ivt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ivt(void *p) { xx_ivt_free((xx_ivt *)p); }
static Abstractformat *mk_ixa(xx_io_device *d, int64_t b) {
    xx_ixa *r = xx_ixa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ixa(void *p) { xx_ixa_free((xx_ixa *)p); }
static Abstractformat *mk_mlb_ft(xx_io_device *d, int64_t b) {
    xx_mlb_ft *r = xx_mlb_ft_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mlb_ft(void *p) { xx_mlb_ft_free((xx_mlb_ft *)p); }
static Abstractformat *mk_fss(xx_io_device *d, int64_t b) {
    xx_fss *r = xx_fss_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fss(void *p) { xx_fss_free((xx_fss *)p); }
static Abstractformat *mk_epf(xx_io_device *d, int64_t b) {
    xx_epf *r = xx_epf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_epf(void *p) { xx_epf_free((xx_epf *)p); }
static Abstractformat *mk_dfc(xx_io_device *d, int64_t b) {
    xx_dfc *r = xx_dfc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dfc(void *p) { xx_dfc_free((xx_dfc *)p); }
static Abstractformat *mk_sfx_rsfx(xx_io_device *d, int64_t b) {
    xx_sfx_rsfx *r = xx_sfx_rsfx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_rsfx(void *p) { xx_sfx_rsfx_free((xx_sfx_rsfx *)p); }
static Abstractformat *mk_ka(xx_io_device *d, int64_t b) {
    xx_ka *r = xx_ka_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ka(void *p) { xx_ka_free((xx_ka *)p); }
static Abstractformat *mk_nextstep_diskimage(xx_io_device *d, int64_t b) {
    xx_nextstep_diskimage *r = xx_nextstep_diskimage_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nextstep_diskimage(void *p) {
    xx_nextstep_diskimage_free((xx_nextstep_diskimage *)p);
}
static Abstractformat *mk_dn(xx_io_device *d, int64_t b) {
    xx_dn *r = xx_dn_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dn(void *p) { xx_dn_free((xx_dn *)p); }
static Abstractformat *mk_insa(xx_io_device *d, int64_t b) {
    xx_insa *r = xx_insa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_insa(void *p) { xx_insa_free((xx_insa *)p); }
static Abstractformat *mk_sfx_vms_dcx(xx_io_device *d, int64_t b) {
    xx_sfx_vms_dcx *r = xx_sfx_vms_dcx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_vms_dcx(void *p) {
    xx_sfx_vms_dcx_free((xx_sfx_vms_dcx *)p);
}
static Abstractformat *mk_oberon(xx_io_device *d, int64_t b) {
    xx_oberon *r = xx_oberon_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_oberon(void *p) { xx_oberon_free((xx_oberon *)p); }
static Abstractformat *mk_ppd(xx_io_device *d, int64_t b) {
    xx_ppd *r = xx_ppd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ppd(void *p) { xx_ppd_free((xx_ppd *)p); }
static Abstractformat *mk_sfx_ad01(xx_io_device *d, int64_t b) {
    xx_sfx_ad01 *r = xx_sfx_ad01_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_ad01(void *p) { xx_sfx_ad01_free((xx_sfx_ad01 *)p); }
static Abstractformat *mk_sfx_nss(xx_io_device *d, int64_t b) {
    xx_sfx_nss *r = xx_sfx_nss_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_nss(void *p) { xx_sfx_nss_free((xx_sfx_nss *)p); }
static Abstractformat *mk_solitaire_deluxe(xx_io_device *d, int64_t b) {
    xx_solitaire_deluxe *r = xx_solitaire_deluxe_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_solitaire_deluxe(void *p) {
    xx_solitaire_deluxe_free((xx_solitaire_deluxe *)p);
}
static Abstractformat *mk_thebat_msb(xx_io_device *d, int64_t b) {
    xx_thebat_msb *r = xx_thebat_msb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_thebat_msb(void *p) {
    xx_thebat_msb_free((xx_thebat_msb *)p);
}
static Abstractformat *mk_sfx_localzip(xx_io_device *d, int64_t b) {
    xx_sfx_localzip *r = xx_sfx_localzip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_localzip(void *p) { xx_sfx_localzip_free((xx_sfx_localzip *)p); }
static Abstractformat *mk_izpack(xx_io_device *d, int64_t b) {
    xx_izpack *r = xx_izpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_izpack(void *p) { xx_izpack_free((xx_izpack *)p); }
static Abstractformat *mk_jam(xx_io_device *d, int64_t b) {
    xx_jam *r = xx_jam_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jam(void *p) { xx_jam_free((xx_jam *)p); }
static Abstractformat *mk_jar(xx_io_device *d, int64_t b) {
    xx_jar *r = xx_jar_create(d, b);
    return r ? &r->zip.format : NULL;
}
static void rm_jar(void *p) { xx_jar_free((xx_jar *)p); }
static Abstractformat *mk_jasc(xx_io_device *d, int64_t b) {
    xx_jasc *r = xx_jasc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jasc(void *p) { xx_jasc_free((xx_jasc *)p); }
static Abstractformat *mk_jbf(xx_io_device *d, int64_t b) {
    xx_jbf *r = xx_jbf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jbf(void *p) { xx_jbf_free((xx_jbf *)p); }
static Abstractformat *mk_jboot(xx_io_device *d, int64_t b) {
    xx_jboot *r = xx_jboot_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jboot(void *p) { xx_jboot_free((xx_jboot *)p); }
static Abstractformat *mk_jetbbs(xx_io_device *d, int64_t b) {
    xx_jetbbs *r = xx_jetbbs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jetbbs(void *p) { xx_jetbbs_free((xx_jetbbs *)p); }
static Abstractformat *mk_jffs2(xx_io_device *d, int64_t b) {
    xx_jffs2 *r = xx_jffs2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jffs2(void *p) { xx_jffs2_free((xx_jffs2 *)p); }
static Abstractformat *mk_jgpak(xx_io_device *d, int64_t b) {
    xx_jgpak *r = xx_jgpak_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jgpak(void *p) { xx_jgpak_free((xx_jgpak *)p); }
static Abstractformat *mk_jm93(xx_io_device *d, int64_t b) {
    xx_jm93 *r = xx_jm93_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jm93(void *p) { xx_jm93_free((xx_jm93 *)p); }
static Abstractformat *mk_kboom(xx_io_device *d, int64_t b) {
    xx_kboom *r = xx_kboom_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_kboom(void *p) { xx_kboom_free((xx_kboom *)p); }
static Abstractformat *mk_kolibrikpack(xx_io_device *d, int64_t b) {
    xx_kolibrikpack *r = xx_kolibrikpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_kolibrikpack(void *p) { xx_kolibrikpack_free((xx_kolibrikpack *)p); }
static Abstractformat *mk_kpck(xx_io_device *d, int64_t b) {
    xx_kpck *r = xx_kpck_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_kpck(void *p) { xx_kpck_free((xx_kpck *)p); }
static Abstractformat *mk_krml(xx_io_device *d, int64_t b) {
    xx_krml *r = xx_krml_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_krml(void *p) { xx_krml_free((xx_krml *)p); }
static Abstractformat *mk_lbrcobol(xx_io_device *d, int64_t b) {
    xx_lbrcobol *r = xx_lbrcobol_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lbrcobol(void *p) { xx_lbrcobol_free((xx_lbrcobol *)p); }
static Abstractformat *mk_le(xx_io_device *d, int64_t b) {
    xx_le *r = xx_le_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_le(void *p) { xx_le_free((xx_le *)p); }
static Abstractformat *mk_lha(xx_io_device *d, int64_t b) {
    xx_lha *r = xx_lha_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lha(void *p) { xx_lha_free((xx_lha *)p); }
static Abstractformat *mk_lif(xx_io_device *d, int64_t b) {
    xx_lif *r = xx_lif_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lif(void *p) { xx_lif_free((xx_lif *)p); }
static Abstractformat *mk_lifkd(xx_io_device *d, int64_t b) {
    xx_lifkd *r = xx_lifkd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lifkd(void *p) { xx_lifkd_free((xx_lifkd *)p); }
static Abstractformat *mk_lim(xx_io_device *d, int64_t b) {
    xx_lim *r = xx_lim_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lim(void *p) { xx_lim_free((xx_lim *)p); }
static Abstractformat *mk_lingvoarc(xx_io_device *d, int64_t b) {
    xx_lingvoarc *r = xx_lingvoarc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lingvoarc(void *p) { xx_lingvoarc_free((xx_lingvoarc *)p); }
static Abstractformat *mk_lizard(xx_io_device *d, int64_t b) {
    xx_lizard *r = xx_lizard_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lizard(void *p) { xx_lizard_free((xx_lizard *)p); }
static Abstractformat *mk_lofi(xx_io_device *d, int64_t b) {
    xx_lofi *r = xx_lofi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lofi(void *p) { xx_lofi_free((xx_lofi *)p); }
static Abstractformat *mk_logfs(xx_io_device *d, int64_t b) {
    xx_logfs *r = xx_logfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_logfs(void *p) { xx_logfs_free((xx_logfs *)p); }
static Abstractformat *mk_logitechcompress(xx_io_device *d, int64_t b) {
    xx_logitechcompress *r = xx_logitechcompress_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_logitechcompress(void *p) { xx_logitechcompress_free((xx_logitechcompress *)p); }
static Abstractformat *mk_lpaq8(xx_io_device *d, int64_t b) {
    xx_lpaq8 *r = xx_lpaq8_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lpaq8(void *p) { xx_lpaq8_free((xx_lpaq8 *)p); }
static Abstractformat *mk_lspack10(xx_io_device *d, int64_t b) {
    xx_lspack10 *r = xx_lspack10_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lspack10(void *p) { xx_lspack10_free((xx_lspack10 *)p); }
static Abstractformat *mk_lsz(xx_io_device *d, int64_t b) {
    xx_lsz *r = xx_lsz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lsz(void *p) { xx_lsz_free((xx_lsz *)p); }
static Abstractformat *mk_luks(xx_io_device *d, int64_t b) {
    xx_luks *r = xx_luks_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_luks(void *p) { xx_luks_free((xx_luks *)p); }
static Abstractformat *mk_lx(xx_io_device *d, int64_t b) {
    xx_lx *r = xx_lx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lx(void *p) { xx_lx_free((xx_lx *)p); }
static Abstractformat *mk_lz4(xx_io_device *d, int64_t b) {
    xx_lz4 *r = xx_lz4_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lz4(void *p) { xx_lz4_free((xx_lz4 *)p); }
static Abstractformat *mk_lz4demo(xx_io_device *d, int64_t b) {
    xx_lz4demo *r = xx_lz4demo_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lz4demo(void *p) { xx_lz4demo_free((xx_lz4demo *)p); }
static Abstractformat *mk_lz5(xx_io_device *d, int64_t b) {
    xx_lz5 *r = xx_lz5_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lz5(void *p) { xx_lz5_free((xx_lz5 *)p); }
static Abstractformat *mk_lzdiet(xx_io_device *d, int64_t b) {
    xx_lzdiet *r = xx_lzdiet_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzdiet(void *p) { xx_lzdiet_free((xx_lzdiet *)p); }
static Abstractformat *mk_lzhcxp(xx_io_device *d, int64_t b) {
    xx_lzhcxp *r = xx_lzhcxp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzhcxp(void *p) { xx_lzhcxp_free((xx_lzhcxp *)p); }
static Abstractformat *mk_lzip(xx_io_device *d, int64_t b) {
    xx_lzip *r = xx_lzip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzip(void *p) { xx_lzip_free((xx_lzip *)p); }
static Abstractformat *mk_lzk00(xx_io_device *d, int64_t b) {
    xx_lzk00 *r = xx_lzk00_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzk00(void *p) { xx_lzk00_free((xx_lzk00 *)p); }
static Abstractformat *mk_lzma(xx_io_device *d, int64_t b) {
    xx_lzma *r = xx_lzma_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzma(void *p) { xx_lzma_free((xx_lzma *)p); }
static Abstractformat *mk_lzop(xx_io_device *d, int64_t b) {
    xx_lzop *r = xx_lzop_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzop(void *p) { xx_lzop_free((xx_lzop *)p); }
static Abstractformat *mk_lzpis2(xx_io_device *d, int64_t b) {
    xx_lzpis2 *r = xx_lzpis2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzpis2(void *p) { xx_lzpis2_free((xx_lzpis2 *)p); }
static Abstractformat *mk_lzv1(xx_io_device *d, int64_t b) {
    xx_lzv1 *r = xx_lzv1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzv1(void *p) { xx_lzv1_free((xx_lzv1 *)p); }
static Abstractformat *mk_lzw15v(xx_io_device *d, int64_t b) {
    xx_lzw15v *r = xx_lzw15v_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzw15v(void *p) { xx_lzw15v_free((xx_lzw15v *)p); }
static Abstractformat *mk_lzwd(xx_io_device *d, int64_t b) {
    xx_lzwd *r = xx_lzwd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzwd(void *p) { xx_lzwd_free((xx_lzwd *)p); }
static Abstractformat *mk_macbinary(xx_io_device *d, int64_t b) {
    xx_macbinary *r = xx_macbinary_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_macbinary(void *p) { xx_macbinary_free((xx_macbinary *)p); }
static Abstractformat *mk_macho(xx_io_device *d, int64_t b) {
    xx_macho *r = xx_macho_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_macho(void *p) { xx_macho_free((xx_macho *)p); }
static Abstractformat *mk_marc(xx_io_device *d, int64_t b) {
    xx_marc *r = xx_marc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_marc(void *p) { xx_marc_free((xx_marc *)p); }
static Abstractformat *mk_mathcad(xx_io_device *d, int64_t b) {
    xx_mathcad *r = xx_mathcad_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mathcad(void *p) { xx_mathcad_free((xx_mathcad *)p); }
static Abstractformat *mk_matter_ota(xx_io_device *d, int64_t b) {
    xx_matter_ota *r = xx_matter_ota_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_matter_ota(void *p) { xx_matter_ota_free((xx_matter_ota *)p); }
static Abstractformat *mk_mbr(xx_io_device *d, int64_t b) {
    xx_mbr *r = xx_mbr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mbr(void *p) { xx_mbr_free((xx_mbr *)p); }
static Abstractformat *mk_mcc(xx_io_device *d, int64_t b) {
    xx_mcc *r = xx_mcc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mcc(void *p) { xx_mcc_free((xx_mcc *)p); }
static Abstractformat *mk_mdcd(xx_io_device *d, int64_t b) {
    xx_mdcd *r = xx_mdcd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mdcd(void *p) { xx_mdcd_free((xx_mdcd *)p); }
static Abstractformat *mk_megatechvol(xx_io_device *d, int64_t b) {
    xx_megatechvol *r = xx_megatechvol_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_megatechvol(void *p) { xx_megatechvol_free((xx_megatechvol *)p); }
static Abstractformat *mk_mh01(xx_io_device *d, int64_t b) {
    xx_mh01 *r = xx_mh01_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mh01(void *p) { xx_mh01_free((xx_mh01 *)p); }
static Abstractformat *mk_mi10(xx_io_device *d, int64_t b) {
    xx_mi10 *r = xx_mi10_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mi10(void *p) { xx_mi10_free((xx_mi10 *)p); }
static Abstractformat *mk_minidump(xx_io_device *d, int64_t b) {
    xx_minidump *r = xx_minidump_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_minidump(void *p) { xx_minidump_free((xx_minidump *)p); }
static Abstractformat *mk_miz(xx_io_device *d, int64_t b) {
    xx_miz *r = xx_miz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_miz(void *p) { xx_miz_free((xx_miz *)p); }
static Abstractformat *mk_mpq(xx_io_device *d, int64_t b) {
    xx_mpq *r = xx_mpq_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mpq(void *p) { xx_mpq_free((xx_mpq *)p); }
static Abstractformat *mk_mrnz(xx_io_device *d, int64_t b) {
    xx_mrnz *r = xx_mrnz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mrnz(void *p) { xx_mrnz_free((xx_mrnz *)p); }
static Abstractformat *mk_ms_dos_backup(xx_io_device *d, int64_t b) {
    xx_ms_dos_backup *r = xx_ms_dos_backup_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ms_dos_backup(void *p) { xx_ms_dos_backup_free((xx_ms_dos_backup *)p); }
static Abstractformat *mk_mscompress(xx_io_device *d, int64_t b) {
    xx_mscompress *r = xx_mscompress_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mscompress(void *p) { xx_mscompress_free((xx_mscompress *)p); }
static Abstractformat *mk_msdos(xx_io_device *d, int64_t b) {
    xx_msdos *r = xx_msdos_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_msdos(void *p) { xx_msdos_free((xx_msdos *)p); }
static Abstractformat *mk_mtree(xx_io_device *d, int64_t b) {
    xx_mtree *r = xx_mtree_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mtree(void *p) { xx_mtree_free((xx_mtree *)p); }
static Abstractformat *mk_mva(xx_io_device *d, int64_t b) {
    xx_mva *r = xx_mva_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mva(void *p) { xx_mva_free((xx_mva *)p); }
static Abstractformat *mk_mwave(xx_io_device *d, int64_t b) {
    xx_mwave *r = xx_mwave_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mwave(void *p) { xx_mwave_free((xx_mwave *)p); }
static Abstractformat *mk_mxs(xx_io_device *d, int64_t b) {
    xx_mxs *r = xx_mxs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mxs(void *p) { xx_mxs_free((xx_mxs *)p); }
static Abstractformat *mk_ne(xx_io_device *d, int64_t b) {
    xx_ne *r = xx_ne_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ne(void *p) { xx_ne_free((xx_ne *)p); }
static Abstractformat *mk_nec_pc_98_fdi(xx_io_device *d, int64_t b) {
    xx_nec_pc_98_fdi *r = xx_nec_pc_98_fdi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nec_pc_98_fdi(void *p) { xx_nec_pc_98_fdi_free((xx_nec_pc_98_fdi *)p); }
static Abstractformat *mk_netware2(xx_io_device *d, int64_t b) {
    xx_netware2 *r = xx_netware2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_netware2(void *p) { xx_netware2_free((xx_netware2 *)p); }
static Abstractformat *mk_netwarepacked(xx_io_device *d, int64_t b) {
    xx_netwarepacked *r = xx_netwarepacked_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_netwarepacked(void *p) { xx_netwarepacked_free((xx_netwarepacked *)p); }
static Abstractformat *mk_nid(xx_io_device *d, int64_t b) {
    xx_nid *r = xx_nid_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nid(void *p) { xx_nid_free((xx_nid *)p); }
static Abstractformat *mk_notetab(xx_io_device *d, int64_t b) {
    xx_notetab *r = xx_notetab_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_notetab(void *p) { xx_notetab_free((xx_notetab *)p); }
static Abstractformat *mk_npack(xx_io_device *d, int64_t b) {
    xx_npack *r = xx_npack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_npack(void *p) { xx_npack_free((xx_npack *)p); }
static Abstractformat *mk_npm(xx_io_device *d, int64_t b) {
    xx_npm *r = xx_npm_create(d, b);
    return r ? &r->tar_gz.format : NULL;
}
static void rm_npm(void *p) { xx_npm_free((xx_npm *)p); }
static Abstractformat *mk_ns2(xx_io_device *d, int64_t b) {
    xx_ns2 *r = xx_ns2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ns2(void *p) { xx_ns2_free((xx_ns2 *)p); }
static Abstractformat *mk_nsa(xx_io_device *d, int64_t b) {
    xx_nsa *r = xx_nsa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nsa(void *p) { xx_nsa_free((xx_nsa *)p); }
static Abstractformat *mk_ntfs(xx_io_device *d, int64_t b) {
    xx_ntfs *r = xx_ntfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ntfs(void *p) { xx_ntfs_free((xx_ntfs *)p); }
static Abstractformat *mk_opc(xx_io_device *d, int64_t b) {
    xx_opc *r = xx_opc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_opc(void *p) { xx_opc_free((xx_opc *)p); }
static Abstractformat *mk_oraclesqueeze(xx_io_device *d, int64_t b) {
    xx_oraclesqueeze *r = xx_oraclesqueeze_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_oraclesqueeze(void *p) { xx_oraclesqueeze_free((xx_oraclesqueeze *)p); }
static Abstractformat *mk_packimg(xx_io_device *d, int64_t b) {
    xx_packimg *r = xx_packimg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_packimg(void *p) { xx_packimg_free((xx_packimg *)p); }
static Abstractformat *mk_packit(xx_io_device *d, int64_t b) {
    xx_packit *r = xx_packit_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_packit(void *p) { xx_packit_free((xx_packit *)p); }
static Abstractformat *mk_pain(xx_io_device *d, int64_t b) {
    xx_pain *r = xx_pain_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pain(void *p) { xx_pain_free((xx_pain *)p); }
static Abstractformat *mk_pakleo(xx_io_device *d, int64_t b) {
    xx_pakleo *r = xx_pakleo_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pakleo(void *p) { xx_pakleo_free((xx_pakleo *)p); }
static Abstractformat *mk_panorama(xx_io_device *d, int64_t b) {
    xx_panorama *r = xx_panorama_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_panorama(void *p) { xx_panorama_free((xx_panorama *)p); }
static Abstractformat *mk_paperport(xx_io_device *d, int64_t b) {
    xx_paperport *r = xx_paperport_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_paperport(void *p) { xx_paperport_free((xx_paperport *)p); }
static Abstractformat *mk_pax(xx_io_device *d, int64_t b) {
    xx_pax *r = xx_pax_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pax(void *p) { xx_pax_free((xx_pax *)p); }
static Abstractformat *mk_pcinstall(xx_io_device *d, int64_t b) {
    xx_pcinstall *r = xx_pcinstall_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pcinstall(void *p) { xx_pcinstall_free((xx_pcinstall *)p); }
static Abstractformat *mk_pcommos2(xx_io_device *d, int64_t b) {
    xx_pcommos2 *r = xx_pcommos2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pcommos2(void *p) { xx_pcommos2_free((xx_pcommos2 *)p); }
static Abstractformat *mk_pcsecure(xx_io_device *d, int64_t b) {
    xx_pcsecure *r = xx_pcsecure_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pcsecure(void *p) { xx_pcsecure_free((xx_pcsecure *)p); }
static Abstractformat *mk_pcxlib(xx_io_device *d, int64_t b) {
    xx_pcxlib *r = xx_pcxlib_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pcxlib(void *p) { xx_pcxlib_free((xx_pcxlib *)p); }
static Abstractformat *mk_pdb(xx_io_device *d, int64_t b) {
    xx_pdb *r = xx_pdb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pdb(void *p) { xx_pdb_free((xx_pdb *)p); }
static Abstractformat *mk_pdp11ar(xx_io_device *d, int64_t b) {
    xx_pdp11ar *r = xx_pdp11ar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pdp11ar(void *p) { xx_pdp11ar_free((xx_pdp11ar *)p); }
static Abstractformat *mk_pe(xx_io_device *d, int64_t b) {
    xx_pe *r = xx_pe_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pe(void *p) { xx_pe_free((xx_pe *)p); }
static Abstractformat *mk_pea(xx_io_device *d, int64_t b) {
    xx_pea *r = xx_pea_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pea(void *p) { xx_pea_free((xx_pea *)p); }
static Abstractformat *mk_perform(xx_io_device *d, int64_t b) {
    xx_perform *r = xx_perform_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_perform(void *p) { xx_perform_free((xx_perform *)p); }
static Abstractformat *mk_phar(xx_io_device *d, int64_t b) {
    xx_phar *r = xx_phar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_phar(void *p) { xx_phar_free((xx_phar *)p); }
static Abstractformat *mk_pkt(xx_io_device *d, int64_t b) {
    xx_pkt *r = xx_pkt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pkt(void *p) { xx_pkt_free((xx_pkt *)p); }
static Abstractformat *mk_pma(xx_io_device *d, int64_t b) {
    xx_pma *r = xx_pma_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pma(void *p) { xx_pma_free((xx_pma *)p); }
static Abstractformat *mk_pmdiskcopy(xx_io_device *d, int64_t b) {
    xx_pmdiskcopy *r = xx_pmdiskcopy_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pmdiskcopy(void *p) { xx_pmdiskcopy_free((xx_pmdiskcopy *)p); }
static Abstractformat *mk_povlablzh(xx_io_device *d, int64_t b) {
    xx_povlablzh *r = xx_povlablzh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_povlablzh(void *p) { xx_povlablzh_free((xx_povlablzh *)p); }
static Abstractformat *mk_powerarc(xx_io_device *d, int64_t b) {
    xx_powerarc *r = xx_powerarc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_powerarc(void *p) { xx_powerarc_free((xx_powerarc *)p); }
static Abstractformat *mk_powerboardbbs(xx_io_device *d, int64_t b) {
    xx_powerboardbbs *r = xx_powerboardbbs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_powerboardbbs(void *p) { xx_powerboardbbs_free((xx_powerboardbbs *)p); }
static Abstractformat *mk_pp20(xx_io_device *d, int64_t b) {
    xx_pp20 *r = xx_pp20_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pp20(void *p) { xx_pp20_free((xx_pp20 *)p); }
static Abstractformat *mk_psdc(xx_io_device *d, int64_t b) {
    xx_psdc *r = xx_psdc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_psdc(void *p) { xx_psdc_free((xx_psdc *)p); }
static Abstractformat *mk_psn(xx_io_device *d, int64_t b) {
    xx_psn *r = xx_psn_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_psn(void *p) { xx_psn_free((xx_psn *)p); }
static Abstractformat *mk_pyz(xx_io_device *d, int64_t b) {
    xx_pyz *r = xx_pyz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pyz(void *p) { xx_pyz_free((xx_pyz *)p); }
static Abstractformat *mk_qcow(xx_io_device *d, int64_t b) {
    xx_qcow *r = xx_qcow_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qcow(void *p) { xx_qcow_free((xx_qcow *)p); }
static Abstractformat *mk_qcow1(xx_io_device *d, int64_t b) {
    xx_qcow1 *r = xx_qcow1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qcow1(void *p) { xx_qcow1_free((xx_qcow1 *)p); }
static Abstractformat *mk_qda(xx_io_device *d, int64_t b) {
    xx_qda *r = xx_qda_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qda(void *p) { xx_qda_free((xx_qda *)p); }
static Abstractformat *mk_qip1(xx_io_device *d, int64_t b) {
    xx_qip1 *r = xx_qip1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qip1(void *p) { xx_qip1_free((xx_qip1 *)p); }
static Abstractformat *mk_qip2(xx_io_device *d, int64_t b) {
    xx_qip2 *r = xx_qip2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qip2(void *p) { xx_qip2_free((xx_qip2 *)p); }
static Abstractformat *mk_qnap_nas_firmware(xx_io_device *d, int64_t b) {
    xx_qnap_nas_firmware *r = xx_qnap_nas_firmware_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qnap_nas_firmware(void *p) { xx_qnap_nas_firmware_free((xx_qnap_nas_firmware *)p); }
static Abstractformat *mk_qnx6(xx_io_device *d, int64_t b) {
    xx_qnx6 *r = xx_qnx6_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qnx6(void *p) { xx_qnx6_free((xx_qnx6 *)p); }
static Abstractformat *mk_qnxbase(xx_io_device *d, int64_t b) {
    xx_qnxbase *r = xx_qnxbase_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qnxbase(void *p) { xx_qnxbase_free((xx_qnxbase *)p); }
static Abstractformat *mk_qrst(xx_io_device *d, int64_t b) {
    xx_qrst *r = xx_qrst_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qrst(void *p) { xx_qrst_free((xx_qrst *)p); }
static Abstractformat *mk_qualitas(xx_io_device *d, int64_t b) {
    xx_qualitas *r = xx_qualitas_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qualitas(void *p) { xx_qualitas_free((xx_qualitas *)p); }
static Abstractformat *mk_quantum(xx_io_device *d, int64_t b) {
    xx_quantum *r = xx_quantum_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_quantum(void *p) { xx_quantum_free((xx_quantum *)p); }
static Abstractformat *mk_quarterdeckqp(xx_io_device *d, int64_t b) {
    xx_quarterdeckqp *r = xx_quarterdeckqp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_quarterdeckqp(void *p) { xx_quarterdeckqp_free((xx_quarterdeckqp *)p); }
static Abstractformat *mk_rar(xx_io_device *d, int64_t b) {
    xx_rar *r = xx_rar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rar(void *p) { xx_rar_free((xx_rar *)p); }
static Abstractformat *mk_raw_deflate_compressed_data(xx_io_device *d, int64_t b) {
    xx_raw_deflate_compressed_data *r = xx_raw_deflate_compressed_data_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_raw_deflate_compressed_data(void *p) { xx_raw_deflate_compressed_data_free((xx_raw_deflate_compressed_data *)p); }
static Abstractformat *mk_rawstac(xx_io_device *d, int64_t b) {
    xx_rawstac *r = xx_rawstac_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rawstac(void *p) { xx_rawstac_free((xx_rawstac *)p); }
static Abstractformat *mk_rcf(xx_io_device *d, int64_t b) {
    xx_rcf *r = xx_rcf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rcf(void *p) { xx_rcf_free((xx_rcf *)p); }
static Abstractformat *mk_rdb(xx_io_device *d, int64_t b) {
    xx_rdb *r = xx_rdb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rdb(void *p) { xx_rdb_free((xx_rdb *)p); }
static Abstractformat *mk_recognita(xx_io_device *d, int64_t b) {
    xx_recognita *r = xx_recognita_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_recognita(void *p) { xx_recognita_free((xx_recognita *)p); }
static Abstractformat *mk_red(xx_io_device *d, int64_t b) {
    xx_red *r = xx_red_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_red(void *p) { xx_red_free((xx_red *)p); }
static Abstractformat *mk_res(xx_io_device *d, int64_t b) {
    xx_res *r = xx_res_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_res(void *p) { xx_res_free((xx_res *)p); }
static Abstractformat *mk_resourcefork(xx_io_device *d, int64_t b) {
    xx_resourcefork *r = xx_resourcefork_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_resourcefork(void *p) { xx_resourcefork_free((xx_resourcefork *)p); }
static Abstractformat *mk_rid(xx_io_device *d, int64_t b) {
    xx_rid *r = xx_rid_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rid(void *p) { xx_rid_free((xx_rid *)p); }
static Abstractformat *mk_riversoft(xx_io_device *d, int64_t b) {
    xx_riversoft *r = xx_riversoft_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_riversoft(void *p) { xx_riversoft_free((xx_riversoft *)p); }
static Abstractformat *mk_rnc(xx_io_device *d, int64_t b) {
    xx_rnc *r = xx_rnc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rnc(void *p) { xx_rnc_free((xx_rnc *)p); }
static Abstractformat *mk_rnca(xx_io_device *d, int64_t b) {
    xx_rnca *r = xx_rnca_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rnca(void *p) { xx_rnca_free((xx_rnca *)p); }
static Abstractformat *mk_romfs(xx_io_device *d, int64_t b) {
    xx_romfs *r = xx_romfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_romfs(void *p) { xx_romfs_free((xx_romfs *)p); }
static Abstractformat *mk_rompaq(xx_io_device *d, int64_t b) {
    xx_rompaq *r = xx_rompaq_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rompaq(void *p) { xx_rompaq_free((xx_rompaq *)p); }
static Abstractformat *mk_rsc(xx_io_device *d, int64_t b) {
    xx_rsc *r = xx_rsc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rsc(void *p) { xx_rsc_free((xx_rsc *)p); }
static Abstractformat *mk_rsvk(xx_io_device *d, int64_t b) {
    xx_rsvk *r = xx_rsvk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rsvk(void *p) { xx_rsvk_free((xx_rsvk *)p); }
static Abstractformat *mk_rta(xx_io_device *d, int64_t b) {
    xx_rta *r = xx_rta_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rta(void *p) { xx_rta_free((xx_rta *)p); }
static Abstractformat *mk_rtk(xx_io_device *d, int64_t b) {
    xx_rtk *r = xx_rtk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rtk(void *p) { xx_rtk_free((xx_rtk *)p); }
static Abstractformat *mk_rtpatch(xx_io_device *d, int64_t b) {
    xx_rtpatch *r = xx_rtpatch_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rtpatch(void *p) { xx_rtpatch_free((xx_rtpatch *)p); }
static Abstractformat *mk_sabdu(xx_io_device *d, int64_t b) {
    xx_sabdu *r = xx_sabdu_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sabdu(void *p) { xx_sabdu_free((xx_sabdu *)p); }
static Abstractformat *mk_saf(xx_io_device *d, int64_t b) {
    xx_saf *r = xx_saf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_saf(void *p) { xx_saf_free((xx_saf *)p); }
static Abstractformat *mk_savedskf(xx_io_device *d, int64_t b) {
    xx_savedskf *r = xx_savedskf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_savedskf(void *p) { xx_savedskf_free((xx_savedskf *)p); }
static Abstractformat *mk_scf(xx_io_device *d, int64_t b) {
    xx_scf *r = xx_scf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_scf(void *p) { xx_scf_free((xx_scf *)p); }
static Abstractformat *mk_sci(xx_io_device *d, int64_t b) {
    xx_sci *r = xx_sci_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sci(void *p) { xx_sci_free((xx_sci *)p); }
static Abstractformat *mk_scl(xx_io_device *d, int64_t b) {
    xx_scl *r = xx_scl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_scl(void *p) { xx_scl_free((xx_scl *)p); }
static Abstractformat *mk_sco(xx_io_device *d, int64_t b) {
    xx_sco *r = xx_sco_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sco(void *p) { xx_sco_free((xx_sco *)p); }
static Abstractformat *mk_seaarc(xx_io_device *d, int64_t b) {
    xx_seaarc *r = xx_seaarc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_seaarc(void *p) { xx_seaarc_free((xx_seaarc *)p); }
static Abstractformat *mk_seadata(xx_io_device *d, int64_t b) {
    xx_seadata *r = xx_seadata_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_seadata(void *p) { xx_seadata_free((xx_seadata *)p); }
static Abstractformat *mk_seama(xx_io_device *d, int64_t b) {
    xx_seama *r = xx_seama_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_seama(void *p) { xx_seama_free((xx_seama *)p); }
static Abstractformat *mk_secondnature(xx_io_device *d, int64_t b) {
    xx_secondnature *r = xx_secondnature_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_secondnature(void *p) { xx_secondnature_free((xx_secondnature *)p); }
static Abstractformat *mk_settlersft(xx_io_device *d, int64_t b) {
    xx_settlersft *r = xx_settlersft_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_settlersft(void *p) { xx_settlersft_free((xx_settlersft *)p); }
static Abstractformat *mk_sfpack(xx_io_device *d, int64_t b) {
    xx_sfpack *r = xx_sfpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfpack(void *p) { xx_sfpack_free((xx_sfpack *)p); }
static Abstractformat *mk_shar(xx_io_device *d, int64_t b) {
    xx_shar *r = xx_shar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_shar(void *p) { xx_shar_free((xx_shar *)p); }
static Abstractformat *mk_shrinkwrap(xx_io_device *d, int64_t b) {
    xx_shrinkwrap *r = xx_shrinkwrap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_shrinkwrap(void *p) { xx_shrinkwrap_free((xx_shrinkwrap *)p); }
static Abstractformat *mk_shrs(xx_io_device *d, int64_t b) {
    xx_shrs *r = xx_shrs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_shrs(void *p) { xx_shrs_free((xx_shrs *)p); }
static Abstractformat *mk_silmarilsft(xx_io_device *d, int64_t b) {
    xx_silmarilsft *r = xx_silmarilsft_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_silmarilsft(void *p) { xx_silmarilsft_free((xx_silmarilsft *)p); }
static Abstractformat *mk_sinner(xx_io_device *d, int64_t b) {
    xx_sinner *r = xx_sinner_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sinner(void *p) { xx_sinner_free((xx_sinner *)p); }
static Abstractformat *mk_sls(xx_io_device *d, int64_t b) {
    xx_sls *r = xx_sls_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sls(void *p) { xx_sls_free((xx_sls *)p); }
static Abstractformat *mk_smsipak(xx_io_device *d, int64_t b) {
    xx_smsipak *r = xx_smsipak_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_smsipak(void *p) { xx_smsipak_free((xx_smsipak *)p); }
static Abstractformat *mk_softpaq2(xx_io_device *d, int64_t b) {
    xx_softpaq2 *r = xx_softpaq2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_softpaq2(void *p) { xx_softpaq2_free((xx_softpaq2 *)p); }
static Abstractformat *mk_softronics(xx_io_device *d, int64_t b) {
    xx_softronics *r = xx_softronics_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_softronics(void *p) { xx_softronics_free((xx_softronics *)p); }
static Abstractformat *mk_solarispkg(xx_io_device *d, int64_t b) {
    xx_solarispkg *r = xx_solarispkg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_solarispkg(void *p) { xx_solarispkg_free((xx_solarispkg *)p); }
static Abstractformat *mk_sos(xx_io_device *d, int64_t b) {
    xx_sos *r = xx_sos_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sos(void *p) { xx_sos_free((xx_sos *)p); }
static Abstractformat *mk_sparse(xx_io_device *d, int64_t b) {
    xx_sparse *r = xx_sparse_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sparse(void *p) { xx_sparse_free((xx_sparse *)p); }
static Abstractformat *mk_spis(xx_io_device *d, int64_t b) {
    xx_spis *r = xx_spis_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_spis(void *p) { xx_spis_free((xx_spis *)p); }
static Abstractformat *mk_sfx_sbx_extractor(xx_io_device *d, int64_t b) {
    xx_sfx_sbx_extractor *r = xx_sfx_sbx_extractor_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_sbx_extractor(void *p) {
    xx_sfx_sbx_extractor_free((xx_sfx_sbx_extractor *)p);
}
static Abstractformat *mk_spk(xx_io_device *d, int64_t b) {
    xx_spk *r = xx_spk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_spk(void *p) { xx_spk_free((xx_spk *)p); }
static Abstractformat *mk_sq(xx_io_device *d, int64_t b) {
    xx_sq *r = xx_sq_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sq(void *p) { xx_sq_free((xx_sq *)p); }
static Abstractformat *mk_squashfs(xx_io_device *d, int64_t b) {
    xx_squashfs *r = xx_squashfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_squashfs(void *p) { xx_squashfs_free((xx_squashfs *)p); }
static Abstractformat *mk_squeeze1(xx_io_device *d, int64_t b) {
    xx_squeeze1 *r = xx_squeeze1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_squeeze1(void *p) { xx_squeeze1_free((xx_squeeze1 *)p); }
static Abstractformat *mk_squeeze2(xx_io_device *d, int64_t b) {
    xx_squeeze2 *r = xx_squeeze2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_squeeze2(void *p) { xx_squeeze2_free((xx_squeeze2 *)p); }
static Abstractformat *mk_sqx(xx_io_device *d, int64_t b) {
    xx_sqx *r = xx_sqx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sqx(void *p) { xx_sqx_free((xx_sqx *)p); }
static Abstractformat *mk_sqz(xx_io_device *d, int64_t b) {
    xx_sqz *r = xx_sqz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sqz(void *p) { xx_sqz_free((xx_sqz *)p); }
static Abstractformat *mk_srec(xx_io_device *d, int64_t b) {
    xx_srec *r = xx_srec_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_srec(void *p) { xx_srec_free((xx_srec *)p); }
static Abstractformat *mk_ssm(xx_io_device *d, int64_t b) {
    xx_ssm *r = xx_ssm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ssm(void *p) { xx_ssm_free((xx_ssm *)p); }
static Abstractformat *mk_stac(xx_io_device *d, int64_t b) {
    xx_stac *r = xx_stac_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stac(void *p) { xx_stac_free((xx_stac *)p); }
static Abstractformat *mk_starkit(xx_io_device *d, int64_t b) {
    xx_starkit *r = xx_starkit_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_starkit(void *p) { xx_starkit_free((xx_starkit *)p); }
static Abstractformat *mk_stk(xx_io_device *d, int64_t b) {
    xx_stk *r = xx_stk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stk(void *p) { xx_stk_free((xx_stk *)p); }
static Abstractformat *mk_stork(xx_io_device *d, int64_t b) {
    xx_stork *r = xx_stork_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stork(void *p) { xx_stork_free((xx_stork *)p); }
static Abstractformat *mk_stuffit(xx_io_device *d, int64_t b) {
    xx_stuffit *r = xx_stuffit_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stuffit(void *p) { xx_stuffit_free((xx_stuffit *)p); }
static Abstractformat *mk_stuffit_split_file(xx_io_device *d, int64_t b) {
    xx_stuffit_split_file *r = xx_stuffit_split_file_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stuffit_split_file(void *p) { xx_stuffit_split_file_free((xx_stuffit_split_file *)p); }
static Abstractformat *mk_stunts(xx_io_device *d, int64_t b) {
    xx_stunts *r = xx_stunts_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stunts(void *p) { xx_stunts_free((xx_stunts *)p); }
static Abstractformat *mk_stylus(xx_io_device *d, int64_t b) {
    xx_stylus *r = xx_stylus_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stylus(void *p) { xx_stylus_free((xx_stylus *)p); }
static Abstractformat *mk_sw(xx_io_device *d, int64_t b) {
    xx_sw *r = xx_sw_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sw(void *p) { xx_sw_free((xx_sw *)p); }
static Abstractformat *mk_swag(xx_io_device *d, int64_t b) {
    xx_swag *r = xx_swag_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_swag(void *p) { xx_swag_free((xx_swag *)p); }
static Abstractformat *mk_swagpacket(xx_io_device *d, int64_t b) {
    xx_swagpacket *r = xx_swagpacket_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_swagpacket(void *p) { xx_swagpacket_free((xx_swagpacket *)p); }
static Abstractformat *mk_t98_next_nfd(xx_io_device *d, int64_t b) {
    xx_t98_next_nfd *r = xx_t98_next_nfd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_t98_next_nfd(void *p) { xx_t98_next_nfd_free((xx_t98_next_nfd *)p); }
static Abstractformat *mk_tar(xx_io_device *d, int64_t b) {
    xx_tar *r = xx_tar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tar(void *p) { xx_tar_free((xx_tar *)p); }
static Abstractformat *mk_tar_bz2(xx_io_device *d, int64_t b) {
    xx_tar_bz2 *r = xx_tar_bz2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tar_bz2(void *p) { xx_tar_bz2_free((xx_tar_bz2 *)p); }
static Abstractformat *mk_tar_compress(xx_io_device *d, int64_t b) {
    xx_tar_compress *r = xx_tar_compress_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tar_compress(void *p) { xx_tar_compress_free((xx_tar_compress *)p); }
static Abstractformat *mk_tar_gz(xx_io_device *d, int64_t b) {
    xx_tar_gz *r = xx_tar_gz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tar_gz(void *p) { xx_tar_gz_free((xx_tar_gz *)p); }
static Abstractformat *mk_tar_lz4(xx_io_device *d, int64_t b) {
    xx_tar_lz4 *r = xx_tar_lz4_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tar_lz4(void *p) { xx_tar_lz4_free((xx_tar_lz4 *)p); }
static Abstractformat *mk_tar_lzip(xx_io_device *d, int64_t b) {
    xx_tar_lzip *r = xx_tar_lzip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tar_lzip(void *p) { xx_tar_lzip_free((xx_tar_lzip *)p); }
static Abstractformat *mk_tar_lzma(xx_io_device *d, int64_t b) {
    xx_tar_lzma *r = xx_tar_lzma_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tar_lzma(void *p) { xx_tar_lzma_free((xx_tar_lzma *)p); }
static Abstractformat *mk_tar_lzop(xx_io_device *d, int64_t b) {
    xx_tar_lzop *r = xx_tar_lzop_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tar_lzop(void *p) { xx_tar_lzop_free((xx_tar_lzop *)p); }
static Abstractformat *mk_tar_nextstep(xx_io_device *d, int64_t b) {
    xx_tar_nextstep *r = xx_tar_nextstep_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tar_nextstep(void *p) { xx_tar_nextstep_free((xx_tar_nextstep *)p); }
static Abstractformat *mk_tar_xz(xx_io_device *d, int64_t b) {
    xx_tar_xz *r = xx_tar_xz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tar_xz(void *p) { xx_tar_xz_free((xx_tar_xz *)p); }
static Abstractformat *mk_tar_zstd(xx_io_device *d, int64_t b) {
    xx_tar_zstd *r = xx_tar_zstd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tar_zstd(void *p) { xx_tar_zstd_free((xx_tar_zstd *)p); }
static Abstractformat *mk_tarx1(xx_io_device *d, int64_t b) {
    xx_tarx1 *r = xx_tarx1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tarx1(void *p) { xx_tarx1_free((xx_tarx1 *)p); }
static Abstractformat *mk_tarx2(xx_io_device *d, int64_t b) {
    xx_tarx2 *r = xx_tarx2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tarx2(void *p) { xx_tarx2_free((xx_tarx2 *)p); }
static Abstractformat *mk_teacy(xx_io_device *d, int64_t b) {
    xx_teacy *r = xx_teacy_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_teacy(void *p) { xx_teacy_free((xx_teacy *)p); }
static Abstractformat *mk_teledisk(xx_io_device *d, int64_t b) {
    xx_teledisk *r = xx_teledisk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_teledisk(void *p) { xx_teledisk_free((xx_teledisk *)p); }
static Abstractformat *mk_terse(xx_io_device *d, int64_t b) {
    xx_terse *r = xx_terse_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_terse(void *p) { xx_terse_free((xx_terse *)p); }
static Abstractformat *mk_tgcf(xx_io_device *d, int64_t b) {
    xx_tgcf *r = xx_tgcf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tgcf(void *p) { xx_tgcf_free((xx_tgcf *)p); }
static Abstractformat *mk_ti99arc(xx_io_device *d, int64_t b) {
    xx_ti99arc *r = xx_ti99arc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ti99arc(void *p) { xx_ti99arc_free((xx_ti99arc *)p); }
static Abstractformat *mk_tivoli(xx_io_device *d, int64_t b) {
    xx_tivoli *r = xx_tivoli_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tivoli(void *p) { xx_tivoli_free((xx_tivoli *)p); }
static Abstractformat *mk_tnef(xx_io_device *d, int64_t b) {
    xx_tnef *r = xx_tnef_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tnef(void *p) { xx_tnef_free((xx_tnef *)p); }
static Abstractformat *mk_topspeed(xx_io_device *d, int64_t b) {
    xx_topspeed *r = xx_topspeed_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_topspeed(void *p) { xx_topspeed_free((xx_topspeed *)p); }
static Abstractformat *mk_tplink(xx_io_device *d, int64_t b) {
    xx_tplink *r = xx_tplink_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tplink(void *p) { xx_tplink_free((xx_tplink *)p); }
static Abstractformat *mk_tps(xx_io_device *d, int64_t b) {
    xx_tps *r = xx_tps_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tps(void *p) { xx_tps_free((xx_tps *)p); }
static Abstractformat *mk_tpwm(xx_io_device *d, int64_t b) {
    xx_tpwm *r = xx_tpwm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tpwm(void *p) { xx_tpwm_free((xx_tpwm *)p); }
static Abstractformat *mk_trc(xx_io_device *d, int64_t b) {
    xx_trc *r = xx_trc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_trc(void *p) { xx_trc_free((xx_trc *)p); }
static Abstractformat *mk_trcpak(xx_io_device *d, int64_t b) {
    xx_trcpak *r = xx_trcpak_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_trcpak(void *p) { xx_trcpak_free((xx_trcpak *)p); }
static Abstractformat *mk_trdos(xx_io_device *d, int64_t b) {
    xx_trdos *r = xx_trdos_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_trdos(void *p) { xx_trdos_free((xx_trdos *)p); }
static Abstractformat *mk_trs_80_jv1(xx_io_device *d, int64_t b) {
    xx_trs_80_jv1 *r = xx_trs_80_jv1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_trs_80_jv1(void *p) { xx_trs_80_jv1_free((xx_trs_80_jv1 *)p); }
static Abstractformat *mk_trs_80_jv3(xx_io_device *d, int64_t b) {
    xx_trs_80_jv3 *r = xx_trs_80_jv3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_trs_80_jv3(void *p) { xx_trs_80_jv3_free((xx_trs_80_jv3 *)p); }
static Abstractformat *mk_trx(xx_io_device *d, int64_t b) {
    xx_trx *r = xx_trx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_trx(void *p) { xx_trx_free((xx_trx *)p); }
static Abstractformat *mk_twoimg(xx_io_device *d, int64_t b) {
    xx_twoimg *r = xx_twoimg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_twoimg(void *p) { xx_twoimg_free((xx_twoimg *)p); }
static Abstractformat *mk_twrx(xx_io_device *d, int64_t b) {
    xx_twrx *r = xx_twrx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_twrx(void *p) { xx_twrx_free((xx_twrx *)p); }
static Abstractformat *mk_tws(xx_io_device *d, int64_t b) {
    xx_tws *r = xx_tws_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tws(void *p) { xx_tws_free((xx_tws *)p); }
static Abstractformat *mk_ubi(xx_io_device *d, int64_t b) {
    xx_ubi *r = xx_ubi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ubi(void *p) { xx_ubi_free((xx_ubi *)p); }
static Abstractformat *mk_ubifs(xx_io_device *d, int64_t b) {
    xx_ubifs *r = xx_ubifs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ubifs(void *p) { xx_ubifs_free((xx_ubifs *)p); }
static Abstractformat *mk_uboot(xx_io_device *d, int64_t b) {
    xx_uboot *r = xx_uboot_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_uboot(void *p) { xx_uboot_free((xx_uboot *)p); }
static Abstractformat *mk_udf(xx_io_device *d, int64_t b) {
    xx_udf *r = xx_udf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_udf(void *p) { xx_udf_free((xx_udf *)p); }
static Abstractformat *mk_uefi_capsule(xx_io_device *d, int64_t b) {
    xx_uefi_capsule *r = xx_uefi_capsule_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_uefi_capsule(void *p) { xx_uefi_capsule_free((xx_uefi_capsule *)p); }
static Abstractformat *mk_uefi_fv(xx_io_device *d, int64_t b) {
    xx_uefi_fv *r = xx_uefi_fv_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_uefi_fv(void *p) { xx_uefi_fv_free((xx_uefi_fv *)p); }
static Abstractformat *mk_uharc(xx_io_device *d, int64_t b) {
    xx_uharc *r = xx_uharc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_uharc(void *p) { xx_uharc_free((xx_uharc *)p); }
static Abstractformat *mk_uimage(xx_io_device *d, int64_t b) {
    xx_uimage *r = xx_uimage_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_uimage(void *p) { xx_uimage_free((xx_uimage *)p); }
static Abstractformat *mk_ulead(xx_io_device *d, int64_t b) {
    xx_ulead *r = xx_ulead_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ulead(void *p) { xx_ulead_free((xx_ulead *)p); }
static Abstractformat *mk_unixcompact(xx_io_device *d, int64_t b) {
    xx_unixcompact *r = xx_unixcompact_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_unixcompact(void *p) { xx_unixcompact_free((xx_unixcompact *)p); }
static Abstractformat *mk_unixcompress(xx_io_device *d, int64_t b) {
    xx_unixcompress *r = xx_unixcompress_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_unixcompress(void *p) { xx_unixcompress_free((xx_unixcompress *)p); }
static Abstractformat *mk_unixpack(xx_io_device *d, int64_t b) {
    xx_unixpack *r = xx_unixpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_unixpack(void *p) { xx_unixpack_free((xx_unixpack *)p); }
static Abstractformat *mk_vhddynamic(xx_io_device *d, int64_t b) {
    xx_vhddynamic *r = xx_vhddynamic_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vhddynamic(void *p) { xx_vhddynamic_free((xx_vhddynamic *)p); }
static Abstractformat *mk_visionaire_studio_vis(xx_io_device *d, int64_t b) {
    xx_visionaire_studio_vis *r = xx_visionaire_studio_vis_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_visionaire_studio_vis(void *p) { xx_visionaire_studio_vis_free((xx_visionaire_studio_vis *)p); }
static Abstractformat *mk_vmarc(xx_io_device *d, int64_t b) {
    xx_vmarc *r = xx_vmarc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vmarc(void *p) { xx_vmarc_free((xx_vmarc *)p); }
static Abstractformat *mk_vmdk(xx_io_device *d, int64_t b) {
    xx_vmdk *r = xx_vmdk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vmdk(void *p) { xx_vmdk_free((xx_vmdk *)p); }
static Abstractformat *mk_vmsdb(xx_io_device *d, int64_t b) {
    xx_vmsdb *r = xx_vmsdb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vmsdb(void *p) { xx_vmsdb_free((xx_vmsdb *)p); }
static Abstractformat *mk_vmspcsi(xx_io_device *d, int64_t b) {
    xx_vmspcsi *r = xx_vmspcsi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vmspcsi(void *p) { xx_vmspcsi_free((xx_vmspcsi *)p); }
static Abstractformat *mk_vmssaveset(xx_io_device *d, int64_t b) {
    xx_vmssaveset *r = xx_vmssaveset_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vmssaveset(void *p) { xx_vmssaveset_free((xx_vmssaveset *)p); }
static Abstractformat *mk_volitionvpft(xx_io_device *d, int64_t b) {
    xx_volitionvpft *r = xx_volitionvpft_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_volitionvpft(void *p) { xx_volitionvpft_free((xx_volitionvpft *)p); }
static Abstractformat *mk_vxworks(xx_io_device *d, int64_t b) {
    xx_vxworks *r = xx_vxworks_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vxworks(void *p) { xx_vxworks_free((xx_vxworks *)p); }
static Abstractformat *mk_warc(xx_io_device *d, int64_t b) {
    xx_warc *r = xx_warc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_warc(void *p) { xx_warc_free((xx_warc *)p); }
static Abstractformat *mk_wiilz77(xx_io_device *d, int64_t b) {
    xx_wiilz77 *r = xx_wiilz77_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wiilz77(void *p) { xx_wiilz77_free((xx_wiilz77 *)p); }
static Abstractformat *mk_wim(xx_io_device *d, int64_t b) {
    xx_wim *r = xx_wim_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wim(void *p) { xx_wim_free((xx_wim *)p); }
static Abstractformat *mk_wince(xx_io_device *d, int64_t b) {
    xx_wince *r = xx_wince_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wince(void *p) { xx_wince_free((xx_wince *)p); }
static Abstractformat *mk_winlink(xx_io_device *d, int64_t b) {
    xx_winlink *r = xx_winlink_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_winlink(void *p) { xx_winlink_free((xx_winlink *)p); }
static Abstractformat *mk_wintermutedcp(xx_io_device *d, int64_t b) {
    xx_wintermutedcp *r = xx_wintermutedcp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wintermutedcp(void *p) { xx_wintermutedcp_free((xx_wintermutedcp *)p); }
static Abstractformat *mk_wintersoft(xx_io_device *d, int64_t b) {
    xx_wintersoft *r = xx_wintersoft_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wintersoft(void *p) { xx_wintersoft_free((xx_wintersoft *)p); }
static Abstractformat *mk_wolfft(xx_io_device *d, int64_t b) {
    xx_wolfft *r = xx_wolfft_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wolfft(void *p) { xx_wolfft_free((xx_wolfft *)p); }
static Abstractformat *mk_wpk(xx_io_device *d, int64_t b) {
    xx_wpk *r = xx_wpk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wpk(void *p) { xx_wpk_free((xx_wpk *)p); }
static Abstractformat *mk_wrzl(xx_io_device *d, int64_t b) {
    xx_wrzl *r = xx_wrzl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wrzl(void *p) { xx_wrzl_free((xx_wrzl *)p); }
static Abstractformat *mk_x68000_dim(xx_io_device *d, int64_t b) {
    xx_x68000_dim *r = xx_x68000_dim_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_x68000_dim(void *p) { xx_x68000_dim_free((xx_x68000_dim *)p); }
static Abstractformat *mk_xamarin_compressed_assembly(xx_io_device *d, int64_t b) {
    xx_xamarin_compressed_assembly *r = xx_xamarin_compressed_assembly_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xamarin_compressed_assembly(void *p) { xx_xamarin_compressed_assembly_free((xx_xamarin_compressed_assembly *)p); }
static Abstractformat *mk_xar(xx_io_device *d, int64_t b) {
    xx_xar *r = xx_xar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xar(void *p) { xx_xar_free((xx_xar *)p); }
static Abstractformat *mk_xeditpack(xx_io_device *d, int64_t b) {
    xx_xeditpack *r = xx_xeditpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xeditpack(void *p) { xx_xeditpack_free((xx_xeditpack *)p); }
static Abstractformat *mk_xlas(xx_io_device *d, int64_t b) {
    xx_xlas *r = xx_xlas_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xlas(void *p) { xx_xlas_free((xx_xlas *)p); }
static Abstractformat *mk_xorarchive(xx_io_device *d, int64_t b) {
    xx_xorarchive *r = xx_xorarchive_create(d, b);
    return r ? &r->container.format : NULL;
}
static void rm_xorarchive(void *p) { xx_xorarchive_free((xx_xorarchive *)p); }
static Abstractformat *mk_xpak(xx_io_device *d, int64_t b) {
    xx_xpak *r = xx_xpak_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xpak(void *p) { xx_xpak_free((xx_xpak *)p); }
static Abstractformat *mk_xz(xx_io_device *d, int64_t b) {
    xx_xz *r = xx_xz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xz(void *p) { xx_xz_free((xx_xz *)p); }
static Abstractformat *mk_yaffs(xx_io_device *d, int64_t b) {
    xx_yaffs *r = xx_yaffs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_yaffs(void *p) { xx_yaffs_free((xx_yaffs *)p); }
static Abstractformat *mk_zap(xx_io_device *d, int64_t b) {
    xx_zap *r = xx_zap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zap(void *p) { xx_zap_free((xx_zap *)p); }
static Abstractformat *mk_zcmp(xx_io_device *d, int64_t b) {
    xx_zcmp *r = xx_zcmp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zcmp(void *p) { xx_zcmp_free((xx_zcmp *)p); }
static Abstractformat *mk_zfsf(xx_io_device *d, int64_t b) {
    xx_zfsf *r = xx_zfsf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zfsf(void *p) { xx_zfsf_free((xx_zfsf *)p); }
static Abstractformat *mk_zie(xx_io_device *d, int64_t b) {
    xx_zie *r = xx_zie_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zie(void *p) { xx_zie_free((xx_zie *)p); }
static Abstractformat *mk_zip(xx_io_device *d, int64_t b) {
    xx_zip *r = xx_zip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zip(void *p) { xx_zip_free((xx_zip *)p); }
static Abstractformat *mk_zlib(xx_io_device *d, int64_t b) {
    xx_zlib *r = xx_zlib_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zlib(void *p) { xx_zlib_free((xx_zlib *)p); }
static Abstractformat *mk_zlwb(xx_io_device *d, int64_t b) {
    xx_zlwb *r = xx_zlwb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zlwb(void *p) { xx_zlwb_free((xx_zlwb *)p); }
static Abstractformat *mk_zoo(xx_io_device *d, int64_t b) {
    xx_zoo *r = xx_zoo_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zoo(void *p) { xx_zoo_free((xx_zoo *)p); }
static Abstractformat *mk_zoom(xx_io_device *d, int64_t b) {
    xx_zoom *r = xx_zoom_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zoom(void *p) { xx_zoom_free((xx_zoom *)p); }
static Abstractformat *mk_zpak(xx_io_device *d, int64_t b) {
    xx_zpak *r = xx_zpak_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zpak(void *p) { xx_zpak_free((xx_zpak *)p); }
static Abstractformat *mk_zpaq(xx_io_device *d, int64_t b) {
    xx_zpaq *r = xx_zpaq_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zpaq(void *p) { xx_zpaq_free((xx_zpaq *)p); }
static Abstractformat *mk_zstd(xx_io_device *d, int64_t b) {
    xx_zstd *r = xx_zstd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zstd(void *p) { xx_zstd_free((xx_zstd *)p); }
static Abstractformat *mk_ztc(xx_io_device *d, int64_t b) {
    xx_ztc *r = xx_ztc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ztc(void *p) { xx_ztc_free((xx_ztc *)p); }
static Abstractformat *mk_zxzip(xx_io_device *d, int64_t b) {
    xx_zxzip *r = xx_zxzip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zxzip(void *p) { xx_zxzip_free((xx_zxzip *)p); }
static Abstractformat *mk_zz(xx_io_device *d, int64_t b) {
    xx_zz *r = xx_zz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zz(void *p) { xx_zz_free((xx_zz *)p); }
static Abstractformat *mk_zzz(xx_io_device *d, int64_t b) {
    xx_zzz *r = xx_zzz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zzz(void *p) { xx_zzz_free((xx_zzz *)p); }

static Abstractformat *mk_sfx_analogx_emucore_ffs(xx_io_device *d, int64_t b) {
    xx_sfx_analogx_emucore_ffs *r = xx_sfx_analogx_emucore_ffs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_analogx_emucore_ffs(void *p) { xx_sfx_analogx_emucore_ffs_free((xx_sfx_analogx_emucore_ffs *)p); }
static Abstractformat *mk_sfx_krzip(xx_io_device *d, int64_t b) {
    xx_sfx_krzip *r = xx_sfx_krzip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_krzip(void *p) { xx_sfx_krzip_free((xx_sfx_krzip *)p); }
static Abstractformat *mk_sfx_warpin_package(xx_io_device *d, int64_t b) {
    xx_sfx_warpin_package *r = xx_sfx_warpin_package_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_warpin_package(void *p) { xx_sfx_warpin_package_free((xx_sfx_warpin_package *)p); }
static Abstractformat *mk_sfx_hci_instalit(xx_io_device *d, int64_t b) {
    xx_sfx_hci_instalit *r = xx_sfx_hci_instalit_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_hci_instalit(void *p) { xx_sfx_hci_instalit_free((xx_sfx_hci_instalit *)p); }
static Abstractformat *mk_sfx_clickteam_multimedia_fusion(xx_io_device *d, int64_t b) {
    xx_sfx_clickteam_multimedia_fusion *r = xx_sfx_clickteam_multimedia_fusion_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_clickteam_multimedia_fusion(void *p) { xx_sfx_clickteam_multimedia_fusion_free((xx_sfx_clickteam_multimedia_fusion *)p); }
static Abstractformat *mk_sfx_abbyy_fine_objects(xx_io_device *d, int64_t b) {
    xx_sfx_abbyy_fine_objects *r = xx_sfx_abbyy_fine_objects_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_abbyy_fine_objects(void *p) { xx_sfx_abbyy_fine_objects_free((xx_sfx_abbyy_fine_objects *)p); }
static Abstractformat *mk_sfx_flashjester_jugglor(xx_io_device *d, int64_t b) {
    xx_sfx_flashjester_jugglor *r = xx_sfx_flashjester_jugglor_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_flashjester_jugglor(void *p) { xx_sfx_flashjester_jugglor_free((xx_sfx_flashjester_jugglor *)p); }
static Abstractformat *mk_sfx_jgsoft_deploymaster_package(xx_io_device *d, int64_t b) {
    xx_sfx_jgsoft_deploymaster_package *r = xx_sfx_jgsoft_deploymaster_package_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_jgsoft_deploymaster_package(void *p) { xx_sfx_jgsoft_deploymaster_package_free((xx_sfx_jgsoft_deploymaster_package *)p); }
static Abstractformat *mk_sfx_ardi_diskette_image(xx_io_device *d, int64_t b) {
    xx_sfx_ardi_diskette_image *r = xx_sfx_ardi_diskette_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_ardi_diskette_image(void *p) { xx_sfx_ardi_diskette_image_free((xx_sfx_ardi_diskette_image *)p); }
static Abstractformat *mk_sfx_nullsoft_pimp(xx_io_device *d, int64_t b) {
    xx_sfx_nullsoft_pimp *r = xx_sfx_nullsoft_pimp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_nullsoft_pimp(void *p) { xx_sfx_nullsoft_pimp_free((xx_sfx_nullsoft_pimp *)p); }

static Abstractformat *mk_sfx_sydex_diskette_image(xx_io_device *d, int64_t b) {
    xx_sfx_sydex_diskette_image *r = xx_sfx_sydex_diskette_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_sydex_diskette_image(void *p) { xx_sfx_sydex_diskette_image_free((xx_sfx_sydex_diskette_image *)p); }

static Abstractformat *mk_sfx_compaq_softpaq(xx_io_device *d, int64_t b) {
    xx_sfx_compaq_softpaq *r = xx_sfx_compaq_softpaq_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_compaq_softpaq(void *p) { xx_sfx_compaq_softpaq_free((xx_sfx_compaq_softpaq *)p); }
static Abstractformat *mk_sfx_softpaq4(xx_io_device *d, int64_t b) {
    xx_sfx_softpaq4 *r = xx_sfx_softpaq4_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_softpaq4(void *p) { xx_sfx_softpaq4_free((xx_sfx_softpaq4 *)p); }

static Abstractformat *mk_sfx_wasp_windows_auto(xx_io_device *d, int64_t b) {
    xx_sfx_wasp_windows_auto *r = xx_sfx_wasp_windows_auto_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_wasp_windows_auto(void *p) { xx_sfx_wasp_windows_auto_free((xx_sfx_wasp_windows_auto *)p); }

static Abstractformat *mk_wise_installation_system(xx_io_device *d, int64_t b) {
    xx_wise_installation_system *r = xx_wise_installation_system_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wise_installation_system(void *p) { xx_wise_installation_system_free((xx_wise_installation_system *)p); }

static Abstractformat *mk_eschalon_setup_epsf(xx_io_device *d, int64_t b) {
    xx_eschalon_setup_epsf *r = xx_eschalon_setup_epsf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_eschalon_setup_epsf(void *p) { xx_eschalon_setup_epsf_free((xx_eschalon_setup_epsf *)p); }

static Abstractformat *mk_gentee_installer(xx_io_device *d, int64_t b) {
    xx_gentee_installer *r = xx_gentee_installer_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gentee_installer(void *p) { xx_gentee_installer_free((xx_gentee_installer *)p); }

static Abstractformat *mk_clickteam_install_creator(xx_io_device *d, int64_t b) {
    xx_clickteam_install_creator *r = xx_clickteam_install_creator_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_clickteam_install_creator(void *p) { xx_clickteam_install_creator_free((xx_clickteam_install_creator *)p); }

static Abstractformat *mk_createinstall_instcrin_extractor(xx_io_device *d, int64_t b) {
    xx_createinstall_instcrin_extractor *r = xx_createinstall_instcrin_extractor_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_createinstall_instcrin_extractor(void *p) { xx_createinstall_instcrin_extractor_free((xx_createinstall_instcrin_extractor *)p); }

static Abstractformat *mk_advanced_installer_bootstrapper(xx_io_device *d, int64_t b) {
    xx_advanced_installer_bootstrapper *r = xx_advanced_installer_bootstrapper_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_advanced_installer_bootstrapper(void *p) { xx_advanced_installer_bootstrapper_free((xx_advanced_installer_bootstrapper *)p); }
static Abstractformat *mk_ardi_installer(xx_io_device *d, int64_t b) {
    xx_ardi_installer *r = xx_ardi_installer_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ardi_installer(void *p) { xx_ardi_installer_free((xx_ardi_installer *)p); }
static Abstractformat *mk_arni_installer_container(xx_io_device *d, int64_t b) {
    xx_arni_installer_container *r = xx_arni_installer_container_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_arni_installer_container(void *p) { xx_arni_installer_container_free((xx_arni_installer_container *)p); }
static Abstractformat *mk_ej_technologies_install(xx_io_device *d, int64_t b) {
    xx_ej_technologies_install *r = xx_ej_technologies_install_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ej_technologies_install(void *p) { xx_ej_technologies_install_free((xx_ej_technologies_install *)p); }
static Abstractformat *mk_finstall(xx_io_device *d, int64_t b) {
    xx_finstall *r = xx_finstall_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_finstall(void *p) { xx_finstall_free((xx_finstall *)p); }
static Abstractformat *mk_ghost_installer(xx_io_device *d, int64_t b) {
    xx_ghost_installer *r = xx_ghost_installer_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ghost_installer(void *p) { xx_ghost_installer_free((xx_ghost_installer *)p); }
static Abstractformat *mk_ibm_zpak_installer(xx_io_device *d, int64_t b) {
    xx_ibm_zpak_installer *r = xx_ibm_zpak_installer_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ibm_zpak_installer(void *p) { xx_ibm_zpak_installer_free((xx_ibm_zpak_installer *)p); }
static Abstractformat *mk_ifah_installer(xx_io_device *d, int64_t b) {
    xx_ifah_installer *r = xx_ifah_installer_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ifah_installer(void *p) { xx_ifah_installer_free((xx_ifah_installer *)p); }
static Abstractformat *mk_inno_setup(xx_io_device *d, int64_t b) {
    xx_inno_setup *r = xx_inno_setup_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_inno_setup(void *p) { xx_inno_setup_free((xx_inno_setup *)p); }
static Abstractformat *mk_installer_vise_windows(xx_io_device *d, int64_t b) {
    xx_installer_vise_windows *r = xx_installer_vise_windows_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_installer_vise_windows(void *p) { xx_installer_vise_windows_free((xx_installer_vise_windows *)p); }
static Abstractformat *mk_installshield_12_setup(xx_io_device *d, int64_t b) {
    xx_installshield_12_setup *r = xx_installshield_12_setup_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_installshield_12_setup(void *p) { xx_installshield_12_setup_free((xx_installshield_12_setup *)p); }
static Abstractformat *mk_installshield_3(xx_io_device *d, int64_t b) {
    xx_installshield_3 *r = xx_installshield_3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_installshield_3(void *p) { xx_installshield_3_free((xx_installshield_3 *)p); }
static Abstractformat *mk_installshield_7_setup(xx_io_device *d, int64_t b) {
    xx_installshield_7_setup *r = xx_installshield_7_setup_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_installshield_7_setup(void *p) { xx_installshield_7_setup_free((xx_installshield_7_setup *)p); }
static Abstractformat *mk_installshield_7_setup2(xx_io_device *d, int64_t b) {
    xx_installshield_7_setup2 *r = xx_installshield_7_setup2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_installshield_7_setup2(void *p) { xx_installshield_7_setup2_free((xx_installshield_7_setup2 *)p); }
static Abstractformat *mk_installshield_developer(xx_io_device *d, int64_t b) {
    xx_installshield_developer *r = xx_installshield_developer_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_installshield_developer(void *p) { xx_installshield_developer_free((xx_installshield_developer *)p); }
static Abstractformat *mk_installshield_issetupstream(xx_io_device *d, int64_t b) {
    xx_installshield_issetupstream *r = xx_installshield_issetupstream_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_installshield_issetupstream(void *p) { xx_installshield_issetupstream_free((xx_installshield_issetupstream *)p); }
static Abstractformat *mk_installshield_multiplatform(xx_io_device *d, int64_t b) {
    xx_installshield_multiplatform *r = xx_installshield_multiplatform_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_installshield_multiplatform(void *p) { xx_installshield_multiplatform_free((xx_installshield_multiplatform *)p); }
static Abstractformat *mk_installshield_skin(xx_io_device *d, int64_t b) {
    xx_installshield_skin *r = xx_installshield_skin_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_installshield_skin(void *p) { xx_installshield_skin_free((xx_installshield_skin *)p); }
static Abstractformat *mk_microfox_put(xx_io_device *d, int64_t b) {
    xx_microfox_put *r = xx_microfox_put_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_microfox_put(void *p) { xx_microfox_put_free((xx_microfox_put *)p); }
static Abstractformat *mk_o_setup(xx_io_device *d, int64_t b) {
    xx_o_setup *r = xx_o_setup_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_o_setup(void *p) { xx_o_setup_free((xx_o_setup *)p); }
static Abstractformat *mk_pc_install_setup(xx_io_device *d, int64_t b) {
    xx_pc_install_setup *r = xx_pc_install_setup_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pc_install_setup(void *p) { xx_pc_install_setup_free((xx_pc_install_setup *)p); }
static Abstractformat *mk_pyinstaller_one_executable(xx_io_device *d, int64_t b) {
    xx_pyinstaller_one_executable *r = xx_pyinstaller_one_executable_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pyinstaller_one_executable(void *p) { xx_pyinstaller_one_executable_free((xx_pyinstaller_one_executable *)p); }
static Abstractformat *mk_qsetup_installation_suite(xx_io_device *d, int64_t b) {
    xx_qsetup_installation_suite *r = xx_qsetup_installation_suite_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qsetup_installation_suite(void *p) { xx_qsetup_installation_suite_free((xx_qsetup_installation_suite *)p); }
static Abstractformat *mk_rtpatch_setup_data(xx_io_device *d, int64_t b) {
    xx_rtpatch_setup_data *r = xx_rtpatch_setup_data_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rtpatch_setup_data(void *p) { xx_rtpatch_setup_data_free((xx_rtpatch_setup_data *)p); }
static Abstractformat *mk_setup_factory(xx_io_device *d, int64_t b) {
    xx_setup_factory *r = xx_setup_factory_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_setup_factory(void *p) { xx_setup_factory_free((xx_setup_factory *)p); }
static Abstractformat *mk_sfx_ebook_compiler_executables(xx_io_device *d, int64_t b) {
    xx_sfx_ebook_compiler_executables *r = xx_sfx_ebook_compiler_executables_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_ebook_compiler_executables(void *p) { xx_sfx_ebook_compiler_executables_free((xx_sfx_ebook_compiler_executables *)p); }
static Abstractformat *mk_spoon_installer(xx_io_device *d, int64_t b) {
    xx_spoon_installer *r = xx_spoon_installer_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_spoon_installer(void *p) { xx_spoon_installer_free((xx_spoon_installer *)p); }
static Abstractformat *mk_tarma_installer(xx_io_device *d, int64_t b) {
    xx_tarma_installer *r = xx_tarma_installer_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tarma_installer(void *p) { xx_tarma_installer_free((xx_tarma_installer *)p); }
static Abstractformat *mk_adf(xx_io_device *d, int64_t b) {
    xx_adf *r = xx_adf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adf(void *p) { xx_adf_free((xx_adf *)p); }
static Abstractformat *mk_apm(xx_io_device *d, int64_t b) {
    xx_apm *r = xx_apm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apm(void *p) { xx_apm_free((xx_apm *)p); }
static Abstractformat *mk_vhdx(xx_io_device *d, int64_t b) {
    xx_vhdx *r = xx_vhdx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vhdx(void *p) { xx_vhdx_free((xx_vhdx *)p); }
static Abstractformat *mk_base64(xx_io_device *d, int64_t b) {
    xx_base64 *r = xx_base64_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_base64(void *p) { xx_base64_free((xx_base64 *)p); }
static Abstractformat *mk_btoa(xx_io_device *d, int64_t b) {
    xx_btoa *r = xx_btoa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_btoa(void *p) { xx_btoa_free((xx_btoa *)p); }
static Abstractformat *mk_chd(xx_io_device *d, int64_t b) {
    xx_chd *r = xx_chd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_chd(void *p) { xx_chd_free((xx_chd *)p); }
static Abstractformat *mk_chm(xx_io_device *d, int64_t b) {
    xx_chm *r = xx_chm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_chm(void *p) { xx_chm_free((xx_chm *)p); }
static Abstractformat *mk_cloop(xx_io_device *d, int64_t b) {
    xx_cloop *r = xx_cloop_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cloop(void *p) { xx_cloop_free((xx_cloop *)p); }
static Abstractformat *mk_cue(xx_io_device *d, int64_t b) {
    xx_cue *r = xx_cue_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cue(void *p) { xx_cue_free((xx_cue *)p); }
static Abstractformat *mk_dahuazip(xx_io_device *d, int64_t b) {
    xx_dahuazip *r = xx_dahuazip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dahuazip(void *p) { xx_dahuazip_free((xx_dahuazip *)p); }
static Abstractformat *mk_dmsfw(xx_io_device *d, int64_t b) {
    xx_dmsfw *r = xx_dmsfw_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dmsfw(void *p) { xx_dmsfw_free((xx_dmsfw *)p); }
static Abstractformat *mk_ewf(xx_io_device *d, int64_t b) {
    xx_ewf *r = xx_ewf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ewf(void *p) { xx_ewf_free((xx_ewf *)p); }
static Abstractformat *mk_godot_engine_pck(xx_io_device *d, int64_t b) {
    xx_godot_engine_pck *r = xx_godot_engine_pck_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_godot_engine_pck(void *p) { xx_godot_engine_pck_free((xx_godot_engine_pck *)p); }
static Abstractformat *mk_gpgsigned(xx_io_device *d, int64_t b) {
    xx_gpgsigned *r = xx_gpgsigned_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gpgsigned(void *p) { xx_gpgsigned_free((xx_gpgsigned *)p); }
static Abstractformat *mk_ihex(xx_io_device *d, int64_t b) {
    xx_ihex *r = xx_ihex_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ihex(void *p) { xx_ihex_free((xx_ihex *)p); }
static Abstractformat *mk_kwaj(xx_io_device *d, int64_t b) {
    xx_kwaj *r = xx_kwaj_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_kwaj(void *p) { xx_kwaj_free((xx_kwaj *)p); }
static Abstractformat *mk_lbr(xx_io_device *d, int64_t b) {
    xx_lbr *r = xx_lbr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lbr(void *p) { xx_lbr_free((xx_lbr *)p); }
static Abstractformat *mk_lzfsestream(xx_io_device *d, int64_t b) {
    xx_lzfsestream *r = xx_lzfsestream_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzfsestream(void *p) { xx_lzfsestream_free((xx_lzfsestream *)p); }
static Abstractformat *mk_nrg(xx_io_device *d, int64_t b) {
    xx_nrg *r = xx_nrg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nrg(void *p) { xx_nrg_free((xx_nrg *)p); }
static Abstractformat *mk_packit_mac(xx_io_device *d, int64_t b) {
    xx_packit_mac *r = xx_packit_mac_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_packit_mac(void *p) { xx_packit_mac_free((xx_packit_mac *)p); }
static Abstractformat *mk_rpm(xx_io_device *d, int64_t b) {
    xx_rpm *r = xx_rpm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rpm(void *p) { xx_rpm_free((xx_rpm *)p); }
static Abstractformat *mk_stuffit5(xx_io_device *d, int64_t b) {
    xx_stuffit5 *r = xx_stuffit5_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stuffit5(void *p) { xx_stuffit5_free((xx_stuffit5 *)p); }
bool xxfc_is_incomplete(const xxfc_opened *opened) {
    if (!opened || !opened->format) return false;
    switch (opened->type) {
        case XX_FILE_TYPE_PRODOS: {
            const xx_prodos *r = (const xx_prodos *)opened->format;
            return r->truncated || r->damaged;
        }
        case XX_FILE_TYPE_ISZ:
            return ((const xx_isz *)opened->format)->multi_volume;
        case XX_FILE_TYPE_QEMU_ENHANCED_DISK: {
            const xx_qemu_enhanced_disk *r = (const xx_qemu_enhanced_disk *)opened->format;
            return r->has_backing_file || r->needs_check;
        }
        case XX_FILE_TYPE_VDI:
            return ((const xx_vdi *)opened->format)->image_type == 3 ||
                   ((const xx_vdi *)opened->format)->image_type == 4;
        case XX_FILE_TYPE_MAME_FLOPPY_IMAGE_MFI:
            return ((const xx_mame_floppy_image_mfi *)opened->format)->incomplete_tracks;
        case XX_FILE_TYPE_PARALLELS_HDD:
            return ((const xx_parallels_hdd *)opened->format)->truncated;
        case XX_FILE_TYPE_SWF:
            return !((const xx_swf *)opened->format)->movie_complete;
        case XX_FILE_TYPE_T64:
            return ((const xx_t64 *)opened->format)->truncated_entries != 0;
        case XX_FILE_TYPE_UUE:
            return !((const xx_uue *)opened->format)->is_terminated;
        case XX_FILE_TYPE_VALVE_VPK:
            return ((const xx_valve_vpk *)opened->format)->unavailable_members != 0 ||
                   ((const xx_valve_vpk *)opened->format)->unsupported_members != 0;
        case XX_FILE_TYPE_QUAKE_WAD2:
            return ((const xx_quake_wad2 *)opened->format)->unsupported_members != 0;
        case XX_FILE_TYPE_HALFLIFE_WAD3:
            return ((const xx_halflife_wad3 *)opened->format)->unsupported_members != 0;
        case XX_FILE_TYPE_GENTEE_INSTALLER:
            return !((const xx_gentee_installer *)opened->format)->complete;
        case XX_FILE_TYPE_EJ_TECHNOLOGIES_INSTALL:
            return ((const xx_ej_technologies_install *)opened->format)->truncated;
        case XX_FILE_TYPE_FINSTALL:
            return ((const xx_finstall *)opened->format)->truncated;
        case XX_FILE_TYPE_INSTALLSHIELD_12_SETUP:
            return ((const xx_installshield_12_setup *)opened->format)->truncated;
        case XX_FILE_TYPE_INSTALLSHIELD_DEVELOPER:
            return ((const xx_installshield_developer *)opened->format)->damaged;
        case XX_FILE_TYPE_INSTALLSHIELD_ISSETUPSTREAM:
            return ((const xx_installshield_issetupstream *)opened->format)->truncated;
        case XX_FILE_TYPE_SETUP_FACTORY:
            return ((const xx_setup_factory *)opened->format)->truncated;
        case XX_FILE_TYPE_RPM:
            return ((const xx_rpm *)opened->format)->truncated;
        case XX_FILE_TYPE_ADVANCED_INSTALLER_BOOTSTRAPPER:
            return ((const xx_advanced_installer_bootstrapper *)opened->format)->mode == 1;
        case XX_FILE_TYPE_INNO_SETUP:
            return ((const xx_inno_setup *)opened->format)->external_data;
        case XX_FILE_TYPE_QSETUP_INSTALLATION_SUITE:
            return !((const xx_qsetup_installation_suite *)opened->format)->complete;
        case XX_FILE_TYPE_RTPATCH_SETUP_DATA:
            return ((const xx_rtpatch_setup_data *)opened->format)->split;
        case XX_FILE_TYPE_TARMA_INSTALLER: {
            const xx_tarma_installer *r = (const xx_tarma_installer *)opened->format;
            return r->truncated || r->damaged;
        }
        case XX_FILE_TYPE_BASE64: {
            const xx_base64 *r = (const xx_base64 *)opened->format;
            return r->is_wrapped && !r->is_terminated;
        }
        case XX_FILE_TYPE_LBR:
            return ((const xx_lbr *)opened->format)->truncated_entries != 0;
        case XX_FILE_TYPE_PACKIT_MAC:
            return ((const xx_packit_mac *)opened->format)->has_incomplete_members;
        case XX_FILE_TYPE_IHEX:
            return ((const xx_ihex *)opened->format)->has_incomplete_records;
        case XX_FILE_TYPE_EWF:
            return !xx_ewf_is_complete((const xx_ewf *)opened->format);
        case XX_FILE_TYPE_VHDX:
            /* Differencing images require their parent; this reader fills
             * missing parent sectors with zero for recoverable local data. */
            return ((const xx_vhdx *)opened->format)->disk_type == 4;
        case XX_FILE_TYPE_APM: {
            const xx_apm *r = (const xx_apm *)opened->format;
            uint64_t i;
            if (r->entries_read < r->map_entries) return true;
            for (i = 0; i < xx_apm_get_number_of_members(r); ++i) {
                xx_apm_partition_info part;
                if (!xx_apm_get_partition_info(r, i, &part) || part.size < 0 ||
                    part.declared_size > (uint64_t)part.size) return true;
            }
            return false;
        }
        default: return false;
    }
}

void xxfc_attach_source_files(xxfc_opened *opened, const char *source_path) {
    if (!opened || !opened->format || !source_path) return;
    if (opened->type == XX_FILE_TYPE_CUE)
        (void)xx_cue_open_data_files((xx_cue *)opened->format, source_path);
    else if (opened->type == XX_FILE_TYPE_GDI)
        (void)xx_gdi_open_data_files((xx_gdi *)opened->format, source_path);
    else if (opened->type == XX_FILE_TYPE_MDS)
        (void)xx_mds_open_data_file((xx_mds *)opened->format, source_path);
    else if (opened->type == XX_FILE_TYPE_CCD)
        (void)xx_ccd_open_data_files((xx_ccd *)opened->format, source_path);
    else if (opened->type == XX_FILE_TYPE_CDRDAO_TOC)
        (void)xx_cdrdao_toc_open_data_files((xx_cdrdao_toc *)opened->format, source_path);
    else if (opened->reader_name && !xx_rt_strcmp(opened->reader_name, "vmdk"))
        (void)xx_vmdk_open_data_files((xx_vmdk *)opened->format, source_path);
}

static Abstractformat *mk_act_apricot_pc_xi_raw(xx_io_device *d, int64_t b) {
    xx_act_apricot_pc_xi_raw *r = xx_act_apricot_pc_xi_raw_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_act_apricot_pc_xi_raw(void *p) { xx_act_apricot_pc_xi_raw_free((xx_act_apricot_pc_xi_raw *)p); }
static Abstractformat *mk_adam(xx_io_device *d, int64_t b) {
    xx_adam *r = xx_adam_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adam(void *p) { xx_adam_free((xx_adam *)p); }
static Abstractformat *mk_base16(xx_io_device *d, int64_t b) {
    xx_base16 *r = xx_base16_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_base16(void *p) { xx_base16_free((xx_base16 *)p); }
static Abstractformat *mk_bondwell_2_disk(xx_io_device *d, int64_t b) {
    xx_bondwell_2_disk *r = xx_bondwell_2_disk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bondwell_2_disk(void *p) { xx_bondwell_2_disk_free((xx_bondwell_2_disk *)p); }
static Abstractformat *mk_casio_fz_1_disk(xx_io_device *d, int64_t b) {
    xx_casio_fz_1_disk *r = xx_casio_fz_1_disk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_casio_fz_1_disk(void *p) { xx_casio_fz_1_disk_free((xx_casio_fz_1_disk *)p); }
static Abstractformat *mk_isz(xx_io_device *d, int64_t b) {
    xx_isz *r = xx_isz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_isz(void *p) { xx_isz_free((xx_isz *)p); }
static Abstractformat *mk_mame_floppy_image_mfi(xx_io_device *d, int64_t b) {
    xx_mame_floppy_image_mfi *r = xx_mame_floppy_image_mfi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mame_floppy_image_mfi(void *p) { xx_mame_floppy_image_mfi_free((xx_mame_floppy_image_mfi *)p); }
static Abstractformat *mk_parallels_hdd(xx_io_device *d, int64_t b) {
    xx_parallels_hdd *r = xx_parallels_hdd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_parallels_hdd(void *p) { xx_parallels_hdd_free((xx_parallels_hdd *)p); }
static Abstractformat *mk_pc_magazine_flp(xx_io_device *d, int64_t b) {
    xx_pc_magazine_flp *r = xx_pc_magazine_flp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pc_magazine_flp(void *p) { xx_pc_magazine_flp_free((xx_pc_magazine_flp *)p); }
static Abstractformat *mk_pchrom(xx_io_device *d, int64_t b) {
    xx_pchrom *r = xx_pchrom_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pchrom(void *p) { xx_pchrom_free((xx_pchrom *)p); }
static Abstractformat *mk_pem(xx_io_device *d, int64_t b) {
    xx_pem *r = xx_pem_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pem(void *p) { xx_pem_free((xx_pem *)p); }
static Abstractformat *mk_prodos(xx_io_device *d, int64_t b) {
    xx_prodos *r = xx_prodos_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_prodos(void *p) { xx_prodos_free((xx_prodos *)p); }
static Abstractformat *mk_qemu_enhanced_disk(xx_io_device *d, int64_t b) {
    xx_qemu_enhanced_disk *r = xx_qemu_enhanced_disk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qemu_enhanced_disk(void *p) { xx_qemu_enhanced_disk_free((xx_qemu_enhanced_disk *)p); }
static Abstractformat *mk_rawcd(xx_io_device *d, int64_t b) {
    xx_rawcd *r = xx_rawcd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rawcd(void *p) { xx_rawcd_free((xx_rawcd *)p); }
static Abstractformat *mk_rsdos_fs(xx_io_device *d, int64_t b) {
    xx_rsdos_fs *r = xx_rsdos_fs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rsdos_fs(void *p) { xx_rsdos_fs_free((xx_rsdos_fs *)p); }
static Abstractformat *mk_sar_ns(xx_io_device *d, int64_t b) {
    xx_sar_ns *r = xx_sar_ns_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sar_ns(void *p) { xx_sar_ns_free((xx_sar_ns *)p); }
static Abstractformat *mk_swf(xx_io_device *d, int64_t b) {
    xx_swf *r = xx_swf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_swf(void *p) { xx_swf_free((xx_swf *)p); }
static Abstractformat *mk_t64(xx_io_device *d, int64_t b) {
    xx_t64 *r = xx_t64_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_t64(void *p) { xx_t64_free((xx_t64 *)p); }
static Abstractformat *mk_uue(xx_io_device *d, int64_t b) {
    xx_uue *r = xx_uue_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_uue(void *p) { xx_uue_free((xx_uue *)p); }
static Abstractformat *mk_vdi(xx_io_device *d, int64_t b) {
    xx_vdi *r = xx_vdi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vdi(void *p) { xx_vdi_free((xx_vdi *)p); }
static Abstractformat *mk_bmp(xx_io_device *d, int64_t b) {
    xx_bmp *r = xx_bmp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bmp(void *p) { xx_bmp_free((xx_bmp *)p); }
static Abstractformat *mk_cfe(xx_io_device *d, int64_t b) {
    xx_cfe *r = xx_cfe_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cfe(void *p) { xx_cfe_free((xx_cfe *)p); }
static Abstractformat *mk_dxbc(xx_io_device *d, int64_t b) {
    xx_dxbc *r = xx_dxbc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dxbc(void *p) { xx_dxbc_free((xx_dxbc *)p); }
static Abstractformat *mk_gif(xx_io_device *d, int64_t b) {
    xx_gif *r = xx_gif_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gif(void *p) { xx_gif_free((xx_gif *)p); }
static Abstractformat *mk_jpeg(xx_io_device *d, int64_t b) {
    xx_jpeg *r = xx_jpeg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jpeg(void *p) { xx_jpeg_free((xx_jpeg *)p); }
static Abstractformat *mk_linuxarm64(xx_io_device *d, int64_t b) {
    xx_linuxarm64 *r = xx_linuxarm64_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_linuxarm64(void *p) { xx_linuxarm64_free((xx_linuxarm64 *)p); }
static Abstractformat *mk_linuxboot(xx_io_device *d, int64_t b) {
    xx_linuxboot *r = xx_linuxboot_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_linuxboot(void *p) { xx_linuxboot_free((xx_linuxboot *)p); }
static Abstractformat *mk_linuxzimage(xx_io_device *d, int64_t b) {
    xx_linuxzimage *r = xx_linuxzimage_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_linuxzimage(void *p) { xx_linuxzimage_free((xx_linuxzimage *)p); }
static Abstractformat *mk_pcapng(xx_io_device *d, int64_t b) {
    xx_pcapng *r = xx_pcapng_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pcapng(void *p) { xx_pcapng_free((xx_pcapng *)p); }
static Abstractformat *mk_pjl(xx_io_device *d, int64_t b) {
    xx_pjl *r = xx_pjl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pjl(void *p) { xx_pjl_free((xx_pjl *)p); }
static Abstractformat *mk_png(xx_io_device *d, int64_t b) {
    xx_png *r = xx_png_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_png(void *p) { xx_png_free((xx_png *)p); }
static Abstractformat *mk_riff(xx_io_device *d, int64_t b) {
    xx_riff *r = xx_riff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_riff(void *p) { xx_riff_free((xx_riff *)p); }
static Abstractformat *mk_svg(xx_io_device *d, int64_t b) {
    xx_svg *r = xx_svg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_svg(void *p) { xx_svg_free((xx_svg *)p); }
static Abstractformat *mk_quake_pak(xx_io_device *d, int64_t b) {
    xx_quake_pak *r = xx_quake_pak_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_quake_pak(void *p) { xx_quake_pak_free((xx_quake_pak *)p); }
static Abstractformat *mk_doom_wad(xx_io_device *d, int64_t b) {
    xx_doom_wad *r = xx_doom_wad_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_doom_wad(void *p) { xx_doom_wad_free((xx_doom_wad *)p); }
static Abstractformat *mk_quake_wad2(xx_io_device *d, int64_t b) {
    xx_quake_wad2 *r = xx_quake_wad2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_quake_wad2(void *p) { xx_quake_wad2_free((xx_quake_wad2 *)p); }
static Abstractformat *mk_halflife_wad3(xx_io_device *d, int64_t b) {
    xx_halflife_wad3 *r = xx_halflife_wad3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_halflife_wad3(void *p) { xx_halflife_wad3_free((xx_halflife_wad3 *)p); }
static Abstractformat *mk_build_grp(xx_io_device *d, int64_t b) {
    xx_build_grp *r = xx_build_grp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_build_grp(void *p) { xx_build_grp_free((xx_build_grp *)p); }
static Abstractformat *mk_cri_afs(xx_io_device *d, int64_t b) {
    xx_cri_afs *r = xx_cri_afs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cri_afs(void *p) { xx_cri_afs_free((xx_cri_afs *)p); }
static Abstractformat *mk_cri_awb(xx_io_device *d, int64_t b) {
    xx_cri_awb *r = xx_cri_awb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cri_awb(void *p) { xx_cri_awb_free((xx_cri_awb *)p); }
static Abstractformat *mk_valve_vpk(xx_io_device *d, int64_t b) {
    xx_valve_vpk *r = xx_valve_vpk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_valve_vpk(void *p) { xx_valve_vpk_free((xx_valve_vpk *)p); }
static Abstractformat *mk_nintendo_u8(xx_io_device *d, int64_t b) {
    xx_nintendo_u8 *r = xx_nintendo_u8_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_u8(void *p) { xx_nintendo_u8_free((xx_nintendo_u8 *)p); }
static Abstractformat *mk_nintendo_rarc(xx_io_device *d, int64_t b) {
    xx_nintendo_rarc *r = xx_nintendo_rarc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_rarc(void *p) { xx_nintendo_rarc_free((xx_nintendo_rarc *)p); }
static Abstractformat *mk_android_ab(xx_io_device *d, int64_t b) {
    xx_android_ab *r = xx_android_ab_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_android_ab(void *p) { xx_android_ab_free((xx_android_ab *)p); }
static Abstractformat *mk_nes_rom(xx_io_device *d, int64_t b) {
    xx_nes_rom *r = xx_nes_rom_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nes_rom(void *p) { xx_nes_rom_free((xx_nes_rom *)p); }
static Abstractformat *mk_lynx_lnx(xx_io_device *d, int64_t b) {
    xx_lynx_lnx *r = xx_lynx_lnx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lynx_lnx(void *p) { xx_lynx_lnx_free((xx_lynx_lnx *)p); }
static Abstractformat *mk_commodore_crt(xx_io_device *d, int64_t b) {
    xx_commodore_crt *r = xx_commodore_crt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_commodore_crt(void *p) { xx_commodore_crt_free((xx_commodore_crt *)p); }
static Abstractformat *mk_uf2(xx_io_device *d, int64_t b) {
    xx_uf2 *r = xx_uf2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_uf2(void *p) { xx_uf2_free((xx_uf2 *)p); }
static Abstractformat *mk_ico(xx_io_device *d, int64_t b) {
    xx_ico *r = xx_ico_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ico(void *p) { xx_ico_free((xx_ico *)p); }
static Abstractformat *mk_midi(xx_io_device *d, int64_t b) {
    xx_midi *r = xx_midi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_midi(void *p) { xx_midi_free((xx_midi *)p); }
static Abstractformat *mk_bethesda_bsa(xx_io_device *d, int64_t b) {
    xx_bethesda_bsa *r = xx_bethesda_bsa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bethesda_bsa(void *p) { xx_bethesda_bsa_free((xx_bethesda_bsa *)p); }
static Abstractformat *mk_bethesda_ba2(xx_io_device *d, int64_t b) {
    xx_bethesda_ba2 *r = xx_bethesda_ba2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bethesda_ba2(void *p) { xx_bethesda_ba2_free((xx_bethesda_ba2 *)p); }
static Abstractformat *mk_unityfs(xx_io_device *d, int64_t b) {
    xx_unityfs *r = xx_unityfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_unityfs(void *p) { xx_unityfs_free((xx_unityfs *)p); }
static Abstractformat *mk_bioware_biff(xx_io_device *d, int64_t b) {
    xx_bioware_biff *r = xx_bioware_biff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bioware_biff(void *p) { xx_bioware_biff_free((xx_bioware_biff *)p); }
static Abstractformat *mk_bioware_erf(xx_io_device *d, int64_t b) {
    xx_bioware_erf *r = xx_bioware_erf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bioware_erf(void *p) { xx_bioware_erf_free((xx_bioware_erf *)p); }
static Abstractformat *mk_bioware_rim(xx_io_device *d, int64_t b) {
    xx_bioware_rim *r = xx_bioware_rim_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bioware_rim(void *p) { xx_bioware_rim_free((xx_bioware_rim *)p); }
static Abstractformat *mk_lucas_lab(xx_io_device *d, int64_t b) {
    xx_lucas_lab *r = xx_lucas_lab_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lucas_lab(void *p) { xx_lucas_lab_free((xx_lucas_lab *)p); }
static Abstractformat *mk_lucas_bun(xx_io_device *d, int64_t b) {
    xx_lucas_bun *r = xx_lucas_bun_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lucas_bun(void *p) { xx_lucas_bun_free((xx_lucas_bun *)p); }
static Abstractformat *mk_idtech_bsp(xx_io_device *d, int64_t b) {
    xx_idtech_bsp *r = xx_idtech_bsp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_idtech_bsp(void *p) { xx_idtech_bsp_free((xx_idtech_bsp *)p); }
static Abstractformat *mk_valve_bsp(xx_io_device *d, int64_t b) {
    xx_valve_bsp *r = xx_valve_bsp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_valve_bsp(void *p) { xx_valve_bsp_free((xx_valve_bsp *)p); }
static Abstractformat *mk_idtech_md2(xx_io_device *d, int64_t b) {
    xx_idtech_md2 *r = xx_idtech_md2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_idtech_md2(void *p) { xx_idtech_md2_free((xx_idtech_md2 *)p); }
static Abstractformat *mk_idtech_md3(xx_io_device *d, int64_t b) {
    xx_idtech_md3 *r = xx_idtech_md3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_idtech_md3(void *p) { xx_idtech_md3_free((xx_idtech_md3 *)p); }
static Abstractformat *mk_idtech_qvm(xx_io_device *d, int64_t b) {
    xx_idtech_qvm *r = xx_idtech_qvm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_idtech_qvm(void *p) { xx_idtech_qvm_free((xx_idtech_qvm *)p); }
static Abstractformat *mk_mohawk_mhk(xx_io_device *d, int64_t b) {
    xx_mohawk_mhk *r = xx_mohawk_mhk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mohawk_mhk(void *p) { xx_mohawk_mhk_free((xx_mohawk_mhk *)p); }
static Abstractformat *mk_quake_sprite(xx_io_device *d, int64_t b) {
    xx_quake_sprite *r = xx_quake_sprite_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_quake_sprite(void *p) { xx_quake_sprite_free((xx_quake_sprite *)p); }
static Abstractformat *mk_nintendo_narc(xx_io_device *d, int64_t b) {
    xx_nintendo_narc *r = xx_nintendo_narc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_narc(void *p) { xx_nintendo_narc_free((xx_nintendo_narc *)p); }
static Abstractformat *mk_nintendo_sarc(xx_io_device *d, int64_t b) {
    xx_nintendo_sarc *r = xx_nintendo_sarc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_sarc(void *p) { xx_nintendo_sarc_free((xx_nintendo_sarc *)p); }
static Abstractformat *mk_nintendo_pfs0(xx_io_device *d, int64_t b) {
    xx_nintendo_pfs0 *r = xx_nintendo_pfs0_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_pfs0(void *p) { xx_nintendo_pfs0_free((xx_nintendo_pfs0 *)p); }
static Abstractformat *mk_nintendo_hfs0(xx_io_device *d, int64_t b) {
    xx_nintendo_hfs0 *r = xx_nintendo_hfs0_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_hfs0(void *p) { xx_nintendo_hfs0_free((xx_nintendo_hfs0 *)p); }
static Abstractformat *mk_nintendo_brres(xx_io_device *d, int64_t b) {
    xx_nintendo_brres *r = xx_nintendo_brres_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_brres(void *p) { xx_nintendo_brres_free((xx_nintendo_brres *)p); }
static Abstractformat *mk_nintendo_bcstm(xx_io_device *d, int64_t b) {
    xx_nintendo_bcstm *r = xx_nintendo_bcstm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bcstm(void *p) { xx_nintendo_bcstm_free((xx_nintendo_bcstm *)p); }
static Abstractformat *mk_nintendo_bfsar(xx_io_device *d, int64_t b) {
    xx_nintendo_bfsar *r = xx_nintendo_bfsar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bfsar(void *p) { xx_nintendo_bfsar_free((xx_nintendo_bfsar *)p); }
static Abstractformat *mk_nintendo_bcsar(xx_io_device *d, int64_t b) {
    xx_nintendo_bcsar *r = xx_nintendo_bcsar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bcsar(void *p) { xx_nintendo_bcsar_free((xx_nintendo_bcsar *)p); }
static Abstractformat *mk_sony_psarc(xx_io_device *d, int64_t b) {
    xx_sony_psarc *r = xx_sony_psarc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sony_psarc(void *p) { xx_sony_psarc_free((xx_sony_psarc *)p); }
static Abstractformat *mk_ktx(xx_io_device *d, int64_t b) {
    xx_ktx *r = xx_ktx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ktx(void *p) { xx_ktx_free((xx_ktx *)p); }
static Abstractformat *mk_ktx2(xx_io_device *d, int64_t b) {
    xx_ktx2 *r = xx_ktx2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ktx2(void *p) { xx_ktx2_free((xx_ktx2 *)p); }
static Abstractformat *mk_dds(xx_io_device *d, int64_t b) {
    xx_dds *r = xx_dds_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dds(void *p) { xx_dds_free((xx_dds *)p); }
static Abstractformat *mk_pvr(xx_io_device *d, int64_t b) {
    xx_pvr *r = xx_pvr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pvr(void *p) { xx_pvr_free((xx_pvr *)p); }
static Abstractformat *mk_valve_vtf(xx_io_device *d, int64_t b) {
    xx_valve_vtf *r = xx_valve_vtf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_valve_vtf(void *p) { xx_valve_vtf_free((xx_valve_vtf *)p); }
static Abstractformat *mk_xbox_xbe(xx_io_device *d, int64_t b) {
    xx_xbox_xbe *r = xx_xbox_xbe_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xbox_xbe(void *p) { xx_xbox_xbe_free((xx_xbox_xbe *)p); }
static Abstractformat *mk_flac(xx_io_device *d, int64_t b) {
    xx_flac *r = xx_flac_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_flac(void *p) { xx_flac_free((xx_flac *)p); }
static Abstractformat *mk_ogg(xx_io_device *d, int64_t b) {
    xx_ogg *r = xx_ogg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ogg(void *p) { xx_ogg_free((xx_ogg *)p); }
static Abstractformat *mk_mp4(xx_io_device *d, int64_t b) {
    xx_mp4 *r = xx_mp4_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mp4(void *p) { xx_mp4_free((xx_mp4 *)p); }
static Abstractformat *mk_matroska(xx_io_device *d, int64_t b) {
    xx_matroska *r = xx_matroska_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_matroska(void *p) { xx_matroska_free((xx_matroska *)p); }
static Abstractformat *mk_aiff(xx_io_device *d, int64_t b) {
    xx_aiff *r = xx_aiff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_aiff(void *p) { xx_aiff_free((xx_aiff *)p); }
static Abstractformat *mk_caf(xx_io_device *d, int64_t b) {
    xx_caf *r = xx_caf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_caf(void *p) { xx_caf_free((xx_caf *)p); }
static Abstractformat *mk_photoshop_psd(xx_io_device *d, int64_t b) {
    xx_photoshop_psd *r = xx_photoshop_psd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_photoshop_psd(void *p) { xx_photoshop_psd_free((xx_photoshop_psd *)p); }
static Abstractformat *mk_tiff(xx_io_device *d, int64_t b) {
    xx_tiff *r = xx_tiff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tiff(void *p) { xx_tiff_free((xx_tiff *)p); }
static Abstractformat *mk_openexr(xx_io_device *d, int64_t b) {
    xx_openexr *r = xx_openexr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_openexr(void *p) { xx_openexr_free((xx_openexr *)p); }
static Abstractformat *mk_jpeg2000_jp2(xx_io_device *d, int64_t b) {
    xx_jpeg2000_jp2 *r = xx_jpeg2000_jp2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jpeg2000_jp2(void *p) { xx_jpeg2000_jp2_free((xx_jpeg2000_jp2 *)p); }
static Abstractformat *mk_android_vendor_boot(xx_io_device *d, int64_t b) {
    xx_android_vendor_boot *r = xx_android_vendor_boot_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_android_vendor_boot(void *p) { xx_android_vendor_boot_free((xx_android_vendor_boot *)p); }
static Abstractformat *mk_android_dtbo(xx_io_device *d, int64_t b) {
    xx_android_dtbo *r = xx_android_dtbo_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_android_dtbo(void *p) { xx_android_dtbo_free((xx_android_dtbo *)p); }
static Abstractformat *mk_android_vbmeta(xx_io_device *d, int64_t b) {
    xx_android_vbmeta *r = xx_android_vbmeta_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_android_vbmeta(void *p) { xx_android_vbmeta_free((xx_android_vbmeta *)p); }
static Abstractformat *mk_espressif_image(xx_io_device *d, int64_t b) {
    xx_espressif_image *r = xx_espressif_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_espressif_image(void *p) { xx_espressif_image_free((xx_espressif_image *)p); }
static Abstractformat *mk_wasm(xx_io_device *d, int64_t b) {
    xx_wasm *r = xx_wasm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wasm(void *p) { xx_wasm_free((xx_wasm *)p); }
static Abstractformat *mk_llvm_bitcode_wrapper(xx_io_device *d, int64_t b) {
    xx_llvm_bitcode_wrapper *r = xx_llvm_bitcode_wrapper_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_llvm_bitcode_wrapper(void *p) { xx_llvm_bitcode_wrapper_free((xx_llvm_bitcode_wrapper *)p); }
static Abstractformat *mk_dotnet_metadata(xx_io_device *d, int64_t b) {
    xx_dotnet_metadata *r = xx_dotnet_metadata_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dotnet_metadata(void *p) { xx_dotnet_metadata_free((xx_dotnet_metadata *)p); }
static Abstractformat *mk_sfnt(xx_io_device *d, int64_t b) {
    xx_sfnt *r = xx_sfnt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfnt(void *p) { xx_sfnt_free((xx_sfnt *)p); }
static Abstractformat *mk_woff(xx_io_device *d, int64_t b) {
    xx_woff *r = xx_woff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_woff(void *p) { xx_woff_free((xx_woff *)p); }
static Abstractformat *mk_woff2(xx_io_device *d, int64_t b) {
    xx_woff2 *r = xx_woff2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_woff2(void *p) { xx_woff2_free((xx_woff2 *)p); }
static Abstractformat *mk_makeself(xx_io_device *d, int64_t b) {
    xx_makeself *r = xx_makeself_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_makeself(void *p) { xx_makeself_free((xx_makeself *)p); }
static Abstractformat *mk_sun_java_binsh(xx_io_device *d, int64_t b) {
    xx_sun_java_binsh *r = xx_sun_java_binsh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sun_java_binsh(void *p) { xx_sun_java_binsh_free((xx_sun_java_binsh *)p); }
static Abstractformat *mk_installanywhere_unix(xx_io_device *d, int64_t b) {
    xx_installanywhere_unix *r = xx_installanywhere_unix_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_installanywhere_unix(void *p) { xx_installanywhere_unix_free((xx_installanywhere_unix *)p); }
static Abstractformat *mk_sfx_packagefortheweb(xx_io_device *d, int64_t b) {
    xx_sfx_packagefortheweb *r = xx_sfx_packagefortheweb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_packagefortheweb(void *p) { xx_sfx_packagefortheweb_free((xx_sfx_packagefortheweb *)p); }
static Abstractformat *mk_sfx_spis(xx_io_device *d, int64_t b) {
    xx_sfx_spis *r = xx_sfx_spis_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_spis(void *p) { xx_sfx_spis_free((xx_sfx_spis *)p); }
static Abstractformat *mk_sfx_lha(xx_io_device *d, int64_t b) {
    xx_sfx_lha *r = xx_sfx_lha_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_lha(void *p) { xx_sfx_lha_free((xx_sfx_lha *)p); }
static Abstractformat *mk_lmd_container(xx_io_device *d, int64_t b) {
    xx_lmd_container *r = xx_lmd_container_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lmd_container(void *p) { xx_lmd_container_free((xx_lmd_container *)p); }
static Abstractformat *mk_totalannihilation_hpi(xx_io_device *d, int64_t b) {
    xx_totalannihilation_hpi *r = xx_totalannihilation_hpi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_totalannihilation_hpi(void *p) { xx_totalannihilation_hpi_free((xx_totalannihilation_hpi *)p); }
static Abstractformat *mk_ravensoft_rff(xx_io_device *d, int64_t b) {
    xx_ravensoft_rff *r = xx_ravensoft_rff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ravensoft_rff(void *p) { xx_ravensoft_rff_free((xx_ravensoft_rff *)p); }
static Abstractformat *mk_terminalreality_pod(xx_io_device *d, int64_t b) {
    xx_terminalreality_pod *r = xx_terminalreality_pod_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_terminalreality_pod(void *p) { xx_terminalreality_pod_free((xx_terminalreality_pod *)p); }
static Abstractformat *mk_volition_vpp(xx_io_device *d, int64_t b) {
    xx_volition_vpp *r = xx_volition_vpp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_volition_vpp(void *p) { xx_volition_vpp_free((xx_volition_vpp *)p); }
static Abstractformat *mk_kirikiri_xp3(xx_io_device *d, int64_t b) {
    xx_kirikiri_xp3 *r = xx_kirikiri_xp3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_kirikiri_xp3(void *p) { xx_kirikiri_xp3_free((xx_kirikiri_xp3 *)p); }
static Abstractformat *mk_fromsoftware_binder(xx_io_device *d, int64_t b) {
    xx_fromsoftware_binder *r = xx_fromsoftware_binder_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fromsoftware_binder(void *p) { xx_fromsoftware_binder_free((xx_fromsoftware_binder *)p); }
static Abstractformat *mk_mythic_myp(xx_io_device *d, int64_t b) {
    xx_mythic_myp *r = xx_mythic_myp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mythic_myp(void *p) { xx_mythic_myp_free((xx_mythic_myp *)p); }
static Abstractformat *mk_lithtech_rez(xx_io_device *d, int64_t b) {
    xx_lithtech_rez *r = xx_lithtech_rez_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lithtech_rez(void *p) { xx_lithtech_rez_free((xx_lithtech_rez *)p); }
static Abstractformat *mk_nintendo_ncch(xx_io_device *d, int64_t b) {
    xx_nintendo_ncch *r = xx_nintendo_ncch_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_ncch(void *p) { xx_nintendo_ncch_free((xx_nintendo_ncch *)p); }
static Abstractformat *mk_nintendo_ncsd(xx_io_device *d, int64_t b) {
    xx_nintendo_ncsd *r = xx_nintendo_ncsd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_ncsd(void *p) { xx_nintendo_ncsd_free((xx_nintendo_ncsd *)p); }
static Abstractformat *mk_nintendo_cia(xx_io_device *d, int64_t b) {
    xx_nintendo_cia *r = xx_nintendo_cia_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_cia(void *p) { xx_nintendo_cia_free((xx_nintendo_cia *)p); }
static Abstractformat *mk_nintendo_nds(xx_io_device *d, int64_t b) {
    xx_nintendo_nds *r = xx_nintendo_nds_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_nds(void *p) { xx_nintendo_nds_free((xx_nintendo_nds *)p); }
static Abstractformat *mk_nintendo_gcm(xx_io_device *d, int64_t b) {
    xx_nintendo_gcm *r = xx_nintendo_gcm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_gcm(void *p) { xx_nintendo_gcm_free((xx_nintendo_gcm *)p); }
static Abstractformat *mk_nintendo_tpl(xx_io_device *d, int64_t b) {
    xx_nintendo_tpl *r = xx_nintendo_tpl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_tpl(void *p) { xx_nintendo_tpl_free((xx_nintendo_tpl *)p); }
static Abstractformat *mk_sony_tim(xx_io_device *d, int64_t b) {
    xx_sony_tim *r = xx_sony_tim_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sony_tim(void *p) { xx_sony_tim_free((xx_sony_tim *)p); }
static Abstractformat *mk_sony_vag(xx_io_device *d, int64_t b) {
    xx_sony_vag *r = xx_sony_vag_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sony_vag(void *p) { xx_sony_vag_free((xx_sony_vag *)p); }
static Abstractformat *mk_larian_lspk(xx_io_device *d, int64_t b) {
    xx_larian_lspk *r = xx_larian_lspk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_larian_lspk(void *p) { xx_larian_lspk_free((xx_larian_lspk *)p); }
static Abstractformat *mk_larian_lsf(xx_io_device *d, int64_t b) {
    xx_larian_lsf *r = xx_larian_lsf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_larian_lsf(void *p) { xx_larian_lsf_free((xx_larian_lsf *)p); }
static Abstractformat *mk_valve_hpak(xx_io_device *d, int64_t b) {
    xx_valve_hpak *r = xx_valve_hpak_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_valve_hpak(void *p) { xx_valve_hpak_free((xx_valve_hpak *)p); }
static Abstractformat *mk_renpy_rpa(xx_io_device *d, int64_t b) {
    xx_renpy_rpa *r = xx_renpy_rpa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_renpy_rpa(void *p) { xx_renpy_rpa_free((xx_renpy_rpa *)p); }
static Abstractformat *mk_unreal_package(xx_io_device *d, int64_t b) {
    xx_unreal_package *r = xx_unreal_package_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_unreal_package(void *p) { xx_unreal_package_free((xx_unreal_package *)p); }
static Abstractformat *mk_sega_pvr2(xx_io_device *d, int64_t b) {
    xx_sega_pvr2 *r = xx_sega_pvr2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sega_pvr2(void *p) { xx_sega_pvr2_free((xx_sega_pvr2 *)p); }
static Abstractformat *mk_nintendo_bntx(xx_io_device *d, int64_t b) {
    xx_nintendo_bntx *r = xx_nintendo_bntx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bntx(void *p) { xx_nintendo_bntx_free((xx_nintendo_bntx *)p); }
static Abstractformat *mk_icns(xx_io_device *d, int64_t b) {
    xx_icns *r = xx_icns_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_icns(void *p) { xx_icns_free((xx_icns *)p); }
static Abstractformat *mk_xcursor(xx_io_device *d, int64_t b) {
    xx_xcursor *r = xx_xcursor_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xcursor(void *p) { xx_xcursor_free((xx_xcursor *)p); }
static Abstractformat *mk_icc(xx_io_device *d, int64_t b) {
    xx_icc *r = xx_icc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_icc(void *p) { xx_icc_free((xx_icc *)p); }
static Abstractformat *mk_qoi(xx_io_device *d, int64_t b) {
    xx_qoi *r = xx_qoi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qoi(void *p) { xx_qoi_free((xx_qoi *)p); }
static Abstractformat *mk_farbfeld(xx_io_device *d, int64_t b) {
    xx_farbfeld *r = xx_farbfeld_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_farbfeld(void *p) { xx_farbfeld_free((xx_farbfeld *)p); }
static Abstractformat *mk_pnm(xx_io_device *d, int64_t b) {
    xx_pnm *r = xx_pnm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pnm(void *p) { xx_pnm_free((xx_pnm *)p); }
static Abstractformat *mk_tga(xx_io_device *d, int64_t b) {
    xx_tga *r = xx_tga_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tga(void *p) { xx_tga_free((xx_tga *)p); }
static Abstractformat *mk_sun_raster(xx_io_device *d, int64_t b) {
    xx_sun_raster *r = xx_sun_raster_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sun_raster(void *p) { xx_sun_raster_free((xx_sun_raster *)p); }
static Abstractformat *mk_fits(xx_io_device *d, int64_t b) {
    xx_fits *r = xx_fits_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fits(void *p) { xx_fits_free((xx_fits *)p); }
static Abstractformat *mk_dicom(xx_io_device *d, int64_t b) {
    xx_dicom *r = xx_dicom_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dicom(void *p) { xx_dicom_free((xx_dicom *)p); }
static Abstractformat *mk_pcap(xx_io_device *d, int64_t b) {
    xx_pcap *r = xx_pcap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pcap(void *p) { xx_pcap_free((xx_pcap *)p); }
static Abstractformat *mk_btsnoop(xx_io_device *d, int64_t b) {
    xx_btsnoop *r = xx_btsnoop_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_btsnoop(void *p) { xx_btsnoop_free((xx_btsnoop *)p); }
static Abstractformat *mk_java_class(xx_io_device *d, int64_t b) {
    xx_java_class *r = xx_java_class_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_java_class(void *p) { xx_java_class_free((xx_java_class *)p); }
static Abstractformat *mk_sfnt_collection(xx_io_device *d, int64_t b) {
    xx_sfnt_collection *r = xx_sfnt_collection_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfnt_collection(void *p) { xx_sfnt_collection_free((xx_sfnt_collection *)p); }
static Abstractformat *mk_sqlite3(xx_io_device *d, int64_t b) {
    xx_sqlite3 *r = xx_sqlite3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sqlite3(void *p) { xx_sqlite3_free((xx_sqlite3 *)p); }
static Abstractformat *mk_sqlite_wal(xx_io_device *d, int64_t b) {
    xx_sqlite_wal *r = xx_sqlite_wal_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sqlite_wal(void *p) { xx_sqlite_wal_free((xx_sqlite_wal *)p); }
static Abstractformat *mk_avro_object(xx_io_device *d, int64_t b) {
    xx_avro_object *r = xx_avro_object_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_avro_object(void *p) { xx_avro_object_free((xx_avro_object *)p); }
static Abstractformat *mk_glb(xx_io_device *d, int64_t b) {
    xx_glb *r = xx_glb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_glb(void *p) { xx_glb_free((xx_glb *)p); }
static Abstractformat *mk_spirv(xx_io_device *d, int64_t b) {
    xx_spirv *r = xx_spirv_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_spirv(void *p) { xx_spirv_free((xx_spirv *)p); }
static Abstractformat *mk_crx(xx_io_device *d, int64_t b) {
    xx_crx *r = xx_crx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_crx(void *p) { xx_crx_free((xx_crx *)p); }
static Abstractformat *mk_sfx_arc(xx_io_device *d, int64_t b) {
    xx_sfx_arc *r = xx_sfx_arc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_arc(void *p) { xx_sfx_arc_free((xx_sfx_arc *)p); }
static Abstractformat *mk_sfx_arj(xx_io_device *d, int64_t b) {
    xx_sfx_arj *r = xx_sfx_arj_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_arj(void *p) { xx_sfx_arj_free((xx_sfx_arj *)p); }
static Abstractformat *mk_sfx_bsn(xx_io_device *d, int64_t b) {
    xx_sfx_bsn *r = xx_sfx_bsn_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_bsn(void *p) { xx_sfx_bsn_free((xx_sfx_bsn *)p); }
static Abstractformat *mk_sfx_arq(xx_io_device *d, int64_t b) {
    xx_sfx_arq *r = xx_sfx_arq_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_arq(void *p) { xx_sfx_arq_free((xx_sfx_arq *)p); }
static Abstractformat *mk_sfx_gxl(xx_io_device *d, int64_t b) {
    xx_sfx_gxl *r = xx_sfx_gxl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_gxl(void *p) { xx_sfx_gxl_free((xx_sfx_gxl *)p); }
static Abstractformat *mk_sfx_asymetrix(xx_io_device *d, int64_t b) {
    xx_sfx_asymetrix *r = xx_sfx_asymetrix_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_asymetrix(void *p) { xx_sfx_asymetrix_free((xx_sfx_asymetrix *)p); }
static Abstractformat *mk_sfx_rta(xx_io_device *d, int64_t b) {
    xx_sfx_rta *r = xx_sfx_rta_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_rta(void *p) { xx_sfx_rta_free((xx_sfx_rta *)p); }
static Abstractformat *mk_sfx_rtpatch(xx_io_device *d, int64_t b) {
    xx_sfx_rtpatch *r = xx_sfx_rtpatch_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_rtpatch(void *p) { xx_sfx_rtpatch_free((xx_sfx_rtpatch *)p); }
static Abstractformat *mk_esp_archive(xx_io_device *d, int64_t b) {
    xx_esp_archive *r = xx_esp_archive_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_esp_archive(void *p) { xx_esp_archive_free((xx_esp_archive *)p); }
static Abstractformat *mk_sfx_kwaj(xx_io_device *d, int64_t b) {
    xx_sfx_kwaj *r = xx_sfx_kwaj_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_kwaj(void *p) { xx_sfx_kwaj_free((xx_sfx_kwaj *)p); }
static Abstractformat *mk_gemdos_lha(xx_io_device *d, int64_t b) {
    xx_gemdos_lha *r = xx_gemdos_lha_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gemdos_lha(void *p) { xx_gemdos_lha_free((xx_gemdos_lha *)p); }
static Abstractformat *mk_winimage_zip(xx_io_device *d, int64_t b) {
    xx_winimage_zip *r = xx_winimage_zip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_winimage_zip(void *p) { xx_winimage_zip_free((xx_winimage_zip *)p); }
static Abstractformat *mk_hp3000_wrq(xx_io_device *d, int64_t b) {
    xx_hp3000_wrq *r = xx_hp3000_wrq_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hp3000_wrq(void *p) { xx_hp3000_wrq_free((xx_hp3000_wrq *)p); }
static Abstractformat *mk_icu_data_package(xx_io_device *d, int64_t b) {
    xx_icu_data_package *r = xx_icu_data_package_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_icu_data_package(void *p) { xx_icu_data_package_free((xx_icu_data_package *)p); }
static Abstractformat *mk_sfx_sqz(xx_io_device *d, int64_t b) {
    xx_sfx_sqz *r = xx_sfx_sqz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_sqz(void *p) { xx_sfx_sqz_free((xx_sfx_sqz *)p); }
static Abstractformat *mk_nintendo_bfstm(xx_io_device *d, int64_t b) {
    xx_nintendo_bfstm *r = xx_nintendo_bfstm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bfstm(void *p) { xx_nintendo_bfstm_free((xx_nintendo_bfstm *)p); }
static Abstractformat *mk_nintendo_bfwav(xx_io_device *d, int64_t b) {
    xx_nintendo_bfwav *r = xx_nintendo_bfwav_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bfwav(void *p) { xx_nintendo_bfwav_free((xx_nintendo_bfwav *)p); }
static Abstractformat *mk_nintendo_bcwav(xx_io_device *d, int64_t b) {
    xx_nintendo_bcwav *r = xx_nintendo_bcwav_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bcwav(void *p) { xx_nintendo_bcwav_free((xx_nintendo_bcwav *)p); }
static Abstractformat *mk_nintendo_bfres(xx_io_device *d, int64_t b) {
    xx_nintendo_bfres *r = xx_nintendo_bfres_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bfres(void *p) { xx_nintendo_bfres_free((xx_nintendo_bfres *)p); }
static Abstractformat *mk_nintendo_bflyt(xx_io_device *d, int64_t b) {
    xx_nintendo_bflyt *r = xx_nintendo_bflyt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bflyt(void *p) { xx_nintendo_bflyt_free((xx_nintendo_bflyt *)p); }
static Abstractformat *mk_nintendo_bclyt(xx_io_device *d, int64_t b) {
    xx_nintendo_bclyt *r = xx_nintendo_bclyt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bclyt(void *p) { xx_nintendo_bclyt_free((xx_nintendo_bclyt *)p); }
static Abstractformat *mk_nintendo_bfnt(xx_io_device *d, int64_t b) {
    xx_nintendo_bfnt *r = xx_nintendo_bfnt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bfnt(void *p) { xx_nintendo_bfnt_free((xx_nintendo_bfnt *)p); }
static Abstractformat *mk_nintendo_bcfnt(xx_io_device *d, int64_t b) {
    xx_nintendo_bcfnt *r = xx_nintendo_bcfnt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bcfnt(void *p) { xx_nintendo_bcfnt_free((xx_nintendo_bcfnt *)p); }
static Abstractformat *mk_nintendo_3dsx(xx_io_device *d, int64_t b) {
    xx_nintendo_3dsx *r = xx_nintendo_3dsx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_3dsx(void *p) { xx_nintendo_3dsx_free((xx_nintendo_3dsx *)p); }
static Abstractformat *mk_sony_tim2(xx_io_device *d, int64_t b) {
    xx_sony_tim2 *r = xx_sony_tim2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sony_tim2(void *p) { xx_sony_tim2_free((xx_sony_tim2 *)p); }
static Abstractformat *mk_sony_pamf(xx_io_device *d, int64_t b) {
    xx_sony_pamf *r = xx_sony_pamf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sony_pamf(void *p) { xx_sony_pamf_free((xx_sony_pamf *)p); }
static Abstractformat *mk_sega_gvr(xx_io_device *d, int64_t b) {
    xx_sega_gvr *r = xx_sega_gvr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sega_gvr(void *p) { xx_sega_gvr_free((xx_sega_gvr *)p); }
static Abstractformat *mk_microsoft_xwb(xx_io_device *d, int64_t b) {
    xx_microsoft_xwb *r = xx_microsoft_xwb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_microsoft_xwb(void *p) { xx_microsoft_xwb_free((xx_microsoft_xwb *)p); }
static Abstractformat *mk_microsoft_xsb(xx_io_device *d, int64_t b) {
    xx_microsoft_xsb *r = xx_microsoft_xsb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_microsoft_xsb(void *p) { xx_microsoft_xsb_free((xx_microsoft_xsb *)p); }
static Abstractformat *mk_relic_sga(xx_io_device *d, int64_t b) {
    xx_relic_sga *r = xx_relic_sga_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_relic_sga(void *p) { xx_relic_sga_free((xx_relic_sga *)p); }
static Abstractformat *mk_xpm(xx_io_device *d, int64_t b) {
    xx_xpm *r = xx_xpm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xpm(void *p) { xx_xpm_free((xx_xpm *)p); }
static Abstractformat *mk_pcx(xx_io_device *d, int64_t b) {
    xx_pcx *r = xx_pcx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pcx(void *p) { xx_pcx_free((xx_pcx *)p); }
static Abstractformat *mk_iff_ilbm(xx_io_device *d, int64_t b) {
    xx_iff_ilbm *r = xx_iff_ilbm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_iff_ilbm(void *p) { xx_iff_ilbm_free((xx_iff_ilbm *)p); }
static Abstractformat *mk_utah_rle(xx_io_device *d, int64_t b) {
    xx_utah_rle *r = xx_utah_rle_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_utah_rle(void *p) { xx_utah_rle_free((xx_utah_rle *)p); }
static Abstractformat *mk_radiance_hdr(xx_io_device *d, int64_t b) {
    xx_radiance_hdr *r = xx_radiance_hdr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_radiance_hdr(void *p) { xx_radiance_hdr_free((xx_radiance_hdr *)p); }
static Abstractformat *mk_dpx(xx_io_device *d, int64_t b) {
    xx_dpx *r = xx_dpx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dpx(void *p) { xx_dpx_free((xx_dpx *)p); }
static Abstractformat *mk_cineon(xx_io_device *d, int64_t b) {
    xx_cineon *r = xx_cineon_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cineon(void *p) { xx_cineon_free((xx_cineon *)p); }
static Abstractformat *mk_xwd(xx_io_device *d, int64_t b) {
    xx_xwd *r = xx_xwd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xwd(void *p) { xx_xwd_free((xx_xwd *)p); }
static Abstractformat *mk_sgi_rgb(xx_io_device *d, int64_t b) {
    xx_sgi_rgb *r = xx_sgi_rgb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sgi_rgb(void *p) { xx_sgi_rgb_free((xx_sgi_rgb *)p); }
static Abstractformat *mk_aseprite(xx_io_device *d, int64_t b) {
    xx_aseprite *r = xx_aseprite_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_aseprite(void *p) { xx_aseprite_free((xx_aseprite *)p); }
static Abstractformat *mk_numpy_npy(xx_io_device *d, int64_t b) {
    xx_numpy_npy *r = xx_numpy_npy_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_numpy_npy(void *p) { xx_numpy_npy_free((xx_numpy_npy *)p); }
static Abstractformat *mk_matlab_mat5(xx_io_device *d, int64_t b) {
    xx_matlab_mat5 *r = xx_matlab_mat5_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_matlab_mat5(void *p) { xx_matlab_mat5_free((xx_matlab_mat5 *)p); }
static Abstractformat *mk_netcdf_classic(xx_io_device *d, int64_t b) {
    xx_netcdf_classic *r = xx_netcdf_classic_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_netcdf_classic(void *p) { xx_netcdf_classic_free((xx_netcdf_classic *)p); }
static Abstractformat *mk_hdf4(xx_io_device *d, int64_t b) {
    xx_hdf4 *r = xx_hdf4_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hdf4(void *p) { xx_hdf4_free((xx_hdf4 *)p); }
static Abstractformat *mk_dbase_dbf(xx_io_device *d, int64_t b) {
    xx_dbase_dbf *r = xx_dbase_dbf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dbase_dbf(void *p) { xx_dbase_dbf_free((xx_dbase_dbf *)p); }
static Abstractformat *mk_sas_xport(xx_io_device *d, int64_t b) {
    xx_sas_xport *r = xx_sas_xport_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sas_xport(void *p) { xx_sas_xport_free((xx_sas_xport *)p); }
static Abstractformat *mk_spss_sav(xx_io_device *d, int64_t b) {
    xx_spss_sav *r = xx_spss_sav_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_spss_sav(void *p) { xx_spss_sav_free((xx_spss_sav *)p); }
static Abstractformat *mk_stata_dta(xx_io_device *d, int64_t b) {
    xx_stata_dta *r = xx_stata_dta_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stata_dta(void *p) { xx_stata_dta_free((xx_stata_dta *)p); }
static Abstractformat *mk_apache_arrow_file(xx_io_device *d, int64_t b) {
    xx_apache_arrow_file *r = xx_apache_arrow_file_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apache_arrow_file(void *p) { xx_apache_arrow_file_free((xx_apache_arrow_file *)p); }
static Abstractformat *mk_apache_parquet(xx_io_device *d, int64_t b) {
    xx_apache_parquet *r = xx_apache_parquet_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apache_parquet(void *p) { xx_apache_parquet_free((xx_apache_parquet *)p); }
static Abstractformat *mk_sfx_arcv2(xx_io_device *d, int64_t b) {
    xx_sfx_arcv2 *r = xx_sfx_arcv2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_arcv2(void *p) { xx_sfx_arcv2_free((xx_sfx_arcv2 *)p); }
static Abstractformat *mk_sfx_chz(xx_io_device *d, int64_t b) {
    xx_sfx_chz *r = xx_sfx_chz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_chz(void *p) { xx_sfx_chz_free((xx_sfx_chz *)p); }
static Abstractformat *mk_sfx_szdd(xx_io_device *d, int64_t b) {
    xx_sfx_szdd *r = xx_sfx_szdd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_szdd(void *p) { xx_sfx_szdd_free((xx_sfx_szdd *)p); }
static Abstractformat *mk_sfx_mpq(xx_io_device *d, int64_t b) {
    xx_sfx_mpq *r = xx_sfx_mpq_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_mpq(void *p) { xx_sfx_mpq_free((xx_sfx_mpq *)p); }
static Abstractformat *mk_sfx_swag(xx_io_device *d, int64_t b) {
    xx_sfx_swag *r = xx_sfx_swag_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_swag(void *p) { xx_sfx_swag_free((xx_sfx_swag *)p); }
static Abstractformat *mk_sfx_zpak(xx_io_device *d, int64_t b) {
    xx_sfx_zpak *r = xx_sfx_zpak_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_zpak(void *p) { xx_sfx_zpak_free((xx_sfx_zpak *)p); }
static Abstractformat *mk_sfx_diskexpress(xx_io_device *d, int64_t b) {
    xx_sfx_diskexpress *r = xx_sfx_diskexpress_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_diskexpress(void *p) { xx_sfx_diskexpress_free((xx_sfx_diskexpress *)p); }
static Abstractformat *mk_sfx_bzip2(xx_io_device *d, int64_t b) {
    xx_sfx_bzip2 *r = xx_sfx_bzip2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_bzip2(void *p) { xx_sfx_bzip2_free((xx_sfx_bzip2 *)p); }
static Abstractformat *mk_sfx_gzip(xx_io_device *d, int64_t b) {
    xx_sfx_gzip *r = xx_sfx_gzip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_gzip(void *p) { xx_sfx_gzip_free((xx_sfx_gzip *)p); }
static Abstractformat *mk_sfx_tar(xx_io_device *d, int64_t b) {
    xx_sfx_tar *r = xx_sfx_tar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_tar(void *p) { xx_sfx_tar_free((xx_sfx_tar *)p); }
static Abstractformat *mk_sfx_cab(xx_io_device *d, int64_t b) {
    xx_sfx_cab *r = xx_sfx_cab_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_cab(void *p) { xx_sfx_cab_free((xx_sfx_cab *)p); }
static Abstractformat *mk_pmarc_sfx(xx_io_device *d, int64_t b) {
    xx_pmarc_sfx *r = xx_pmarc_sfx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pmarc_sfx(void *p) { xx_pmarc_sfx_free((xx_pmarc_sfx *)p); }
static Abstractformat *mk_sfx_7zip(xx_io_device *d, int64_t b) {
    xx_sfx_7zip *r = xx_sfx_7zip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_7zip(void *p) { xx_sfx_7zip_free((xx_sfx_7zip *)p); }
static Abstractformat *mk_sfx_ace(xx_io_device *d, int64_t b) {
    xx_sfx_ace *r = xx_sfx_ace_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_ace(void *p) { xx_sfx_ace_free((xx_sfx_ace *)p); }
static Abstractformat *mk_sfx_zipcentral(xx_io_device *d, int64_t b) {
    xx_sfx_zipcentral *r = xx_sfx_zipcentral_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_zipcentral(void *p) { xx_sfx_zipcentral_free((xx_sfx_zipcentral *)p); }
static Abstractformat *mk_sony_psx_exe(xx_io_device *d, int64_t b) {
    xx_sony_psx_exe *r = xx_sony_psx_exe_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sony_psx_exe(void *p) { xx_sony_psx_exe_free((xx_sony_psx_exe *)p); }
static Abstractformat *mk_sony_psf(xx_io_device *d, int64_t b) {
    xx_sony_psf *r = xx_sony_psf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sony_psf(void *p) { xx_sony_psf_free((xx_sony_psf *)p); }
static Abstractformat *mk_xbox_xdvdfs(xx_io_device *d, int64_t b) {
    xx_xbox_xdvdfs *r = xx_xbox_xdvdfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xbox_xdvdfs(void *p) { xx_xbox_xdvdfs_free((xx_xbox_xdvdfs *)p); }
static Abstractformat *mk_nintendo_wbfs(xx_io_device *d, int64_t b) {
    xx_nintendo_wbfs *r = xx_nintendo_wbfs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_wbfs(void *p) { xx_nintendo_wbfs_free((xx_nintendo_wbfs *)p); }
static Abstractformat *mk_godot_ctex(xx_io_device *d, int64_t b) {
    xx_godot_ctex *r = xx_godot_ctex_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_godot_ctex(void *p) { xx_godot_ctex_free((xx_godot_ctex *)p); }
static Abstractformat *mk_unity_serialized(xx_io_device *d, int64_t b) {
    xx_unity_serialized *r = xx_unity_serialized_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_unity_serialized(void *p) { xx_unity_serialized_free((xx_unity_serialized *)p); }
static Abstractformat *mk_idtech_mdl(xx_io_device *d, int64_t b) {
    xx_idtech_mdl *r = xx_idtech_mdl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_idtech_mdl(void *p) { xx_idtech_mdl_free((xx_idtech_mdl *)p); }
static Abstractformat *mk_valve_studio_mdl(xx_io_device *d, int64_t b) {
    xx_valve_studio_mdl *r = xx_valve_studio_mdl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_valve_studio_mdl(void *p) { xx_valve_studio_mdl_free((xx_valve_studio_mdl *)p); }
static Abstractformat *mk_blitz3d_b3d(xx_io_device *d, int64_t b) {
    xx_blitz3d_b3d *r = xx_blitz3d_b3d_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_blitz3d_b3d(void *p) { xx_blitz3d_b3d_free((xx_blitz3d_b3d *)p); }
static Abstractformat *mk_milkshape_ms3d(xx_io_device *d, int64_t b) {
    xx_milkshape_ms3d *r = xx_milkshape_ms3d_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_milkshape_ms3d(void *p) { xx_milkshape_ms3d_free((xx_milkshape_ms3d *)p); }
static Abstractformat *mk_nintendo_bch(xx_io_device *d, int64_t b) {
    xx_nintendo_bch *r = xx_nintendo_bch_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bch(void *p) { xx_nintendo_bch_free((xx_nintendo_bch *)p); }
static Abstractformat *mk_nintendo_cgfx(xx_io_device *d, int64_t b) {
    xx_nintendo_cgfx *r = xx_nintendo_cgfx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_cgfx(void *p) { xx_nintendo_cgfx_free((xx_nintendo_cgfx *)p); }
static Abstractformat *mk_nintendo_byaml(xx_io_device *d, int64_t b) {
    xx_nintendo_byaml *r = xx_nintendo_byaml_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_byaml(void *p) { xx_nintendo_byaml_free((xx_nintendo_byaml *)p); }
static Abstractformat *mk_relic_chunky(xx_io_device *d, int64_t b) {
    xx_relic_chunky *r = xx_relic_chunky_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_relic_chunky(void *p) { xx_relic_chunky_free((xx_relic_chunky *)p); }
static Abstractformat *mk_ogre_mesh(xx_io_device *d, int64_t b) {
    xx_ogre_mesh *r = xx_ogre_mesh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ogre_mesh(void *p) { xx_ogre_mesh_free((xx_ogre_mesh *)p); }
static Abstractformat *mk_adobe_ase(xx_io_device *d, int64_t b) {
    xx_adobe_ase *r = xx_adobe_ase_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adobe_ase(void *p) { xx_adobe_ase_free((xx_adobe_ase *)p); }
static Abstractformat *mk_adobe_aco(xx_io_device *d, int64_t b) {
    xx_adobe_aco *r = xx_adobe_aco_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adobe_aco(void *p) { xx_adobe_aco_free((xx_adobe_aco *)p); }
static Abstractformat *mk_gimp_gbr(xx_io_device *d, int64_t b) {
    xx_gimp_gbr *r = xx_gimp_gbr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gimp_gbr(void *p) { xx_gimp_gbr_free((xx_gimp_gbr *)p); }
static Abstractformat *mk_gimp_gih(xx_io_device *d, int64_t b) {
    xx_gimp_gih *r = xx_gimp_gih_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gimp_gih(void *p) { xx_gimp_gih_free((xx_gimp_gih *)p); }
static Abstractformat *mk_gimp_pat(xx_io_device *d, int64_t b) {
    xx_gimp_pat *r = xx_gimp_pat_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gimp_pat(void *p) { xx_gimp_pat_free((xx_gimp_pat *)p); }
static Abstractformat *mk_jbig2(xx_io_device *d, int64_t b) {
    xx_jbig2 *r = xx_jbig2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jbig2(void *p) { xx_jbig2_free((xx_jbig2 *)p); }
static Abstractformat *mk_djvu(xx_io_device *d, int64_t b) {
    xx_djvu *r = xx_djvu_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_djvu(void *p) { xx_djvu_free((xx_djvu *)p); }
static Abstractformat *mk_emf(xx_io_device *d, int64_t b) {
    xx_emf *r = xx_emf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_emf(void *p) { xx_emf_free((xx_emf *)p); }
static Abstractformat *mk_wmf(xx_io_device *d, int64_t b) {
    xx_wmf *r = xx_wmf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wmf(void *p) { xx_wmf_free((xx_wmf *)p); }
static Abstractformat *mk_xfig(xx_io_device *d, int64_t b) {
    xx_xfig *r = xx_xfig_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xfig(void *p) { xx_xfig_free((xx_xfig *)p); }
static Abstractformat *mk_nifti1(xx_io_device *d, int64_t b) {
    xx_nifti1 *r = xx_nifti1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nifti1(void *p) { xx_nifti1_free((xx_nifti1 *)p); }
static Abstractformat *mk_nrrd(xx_io_device *d, int64_t b) {
    xx_nrrd *r = xx_nrrd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nrrd(void *p) { xx_nrrd_free((xx_nrrd *)p); }
static Abstractformat *mk_mrc(xx_io_device *d, int64_t b) {
    xx_mrc *r = xx_mrc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mrc(void *p) { xx_mrc_free((xx_mrc *)p); }
static Abstractformat *mk_metaimage(xx_io_device *d, int64_t b) {
    xx_metaimage *r = xx_metaimage_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_metaimage(void *p) { xx_metaimage_free((xx_metaimage *)p); }
static Abstractformat *mk_vtk_legacy(xx_io_device *d, int64_t b) {
    xx_vtk_legacy *r = xx_vtk_legacy_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vtk_legacy(void *p) { xx_vtk_legacy_free((xx_vtk_legacy *)p); }
static Abstractformat *mk_gipl(xx_io_device *d, int64_t b) {
    xx_gipl *r = xx_gipl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gipl(void *p) { xx_gipl_free((xx_gipl *)p); }
static Abstractformat *mk_freesurfer_mgh(xx_io_device *d, int64_t b) {
    xx_freesurfer_mgh *r = xx_freesurfer_mgh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_freesurfer_mgh(void *p) { xx_freesurfer_mgh_free((xx_freesurfer_mgh *)p); }
static Abstractformat *mk_edf(xx_io_device *d, int64_t b) {
    xx_edf *r = xx_edf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_edf(void *p) { xx_edf_free((xx_edf *)p); }
static Abstractformat *mk_fcs(xx_io_device *d, int64_t b) {
    xx_fcs *r = xx_fcs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fcs(void *p) { xx_fcs_free((xx_fcs *)p); }
static Abstractformat *mk_tensorflow_tfrecord(xx_io_device *d, int64_t b) {
    xx_tensorflow_tfrecord *r = xx_tensorflow_tfrecord_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tensorflow_tfrecord(void *p) { xx_tensorflow_tfrecord_free((xx_tensorflow_tfrecord *)p); }
static Abstractformat *mk_sfx_imp(xx_io_device *d, int64_t b) {
    xx_sfx_imp *r = xx_sfx_imp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_imp(void *p) { xx_sfx_imp_free((xx_sfx_imp *)p); }
static Abstractformat *mk_sfx_red(xx_io_device *d, int64_t b) {
    xx_sfx_red *r = xx_sfx_red_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_red(void *p) { xx_sfx_red_free((xx_sfx_red *)p); }
static Abstractformat *mk_sfx_ha(xx_io_device *d, int64_t b) {
    xx_sfx_ha *r = xx_sfx_ha_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_ha(void *p) { xx_sfx_ha_free((xx_sfx_ha *)p); }
static Abstractformat *mk_sfx_lzx(xx_io_device *d, int64_t b) {
    xx_sfx_lzx *r = xx_sfx_lzx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_lzx(void *p) { xx_sfx_lzx_free((xx_sfx_lzx *)p); }
static Abstractformat *mk_sfx_sqx(xx_io_device *d, int64_t b) {
    xx_sfx_sqx *r = xx_sfx_sqx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_sqx(void *p) { xx_sfx_sqx_free((xx_sfx_sqx *)p); }
static Abstractformat *mk_sfx_ain(xx_io_device *d, int64_t b) {
    xx_sfx_ain *r = xx_sfx_ain_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_ain(void *p) { xx_sfx_ain_free((xx_sfx_ain *)p); }
static Abstractformat *mk_sfx_hap(xx_io_device *d, int64_t b) {
    xx_sfx_hap *r = xx_sfx_hap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_hap(void *p) { xx_sfx_hap_free((xx_sfx_hap *)p); }
static Abstractformat *mk_sfx_zoo(xx_io_device *d, int64_t b) {
    xx_sfx_zoo *r = xx_sfx_zoo_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_zoo(void *p) { xx_sfx_zoo_free((xx_sfx_zoo *)p); }
static Abstractformat *mk_sfx_cazip(xx_io_device *d, int64_t b) {
    xx_sfx_cazip *r = xx_sfx_cazip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_cazip(void *p) { xx_sfx_cazip_free((xx_sfx_cazip *)p); }
static Abstractformat *mk_sfx_tgcf(xx_io_device *d, int64_t b) {
    xx_sfx_tgcf *r = xx_sfx_tgcf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_tgcf(void *p) { xx_sfx_tgcf_free((xx_sfx_tgcf *)p); }
static Abstractformat *mk_sfx_starkit(xx_io_device *d, int64_t b) {
    xx_sfx_starkit *r = xx_sfx_starkit_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_starkit(void *p) { xx_sfx_starkit_free((xx_sfx_starkit *)p); }
static Abstractformat *mk_sfx_alz(xx_io_device *d, int64_t b) {
    xx_sfx_alz *r = xx_sfx_alz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_alz(void *p) { xx_sfx_alz_free((xx_sfx_alz *)p); }
static Abstractformat *mk_sfx_chm(xx_io_device *d, int64_t b) {
    xx_sfx_chm *r = xx_sfx_chm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfx_chm(void *p) { xx_sfx_chm_free((xx_sfx_chm *)p); }
static Abstractformat *mk_egg(xx_io_device *d, int64_t b) {
    xx_egg *r = xx_egg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_egg(void *p) { xx_egg_free((xx_egg *)p); }
static Abstractformat *mk_nufx(xx_io_device *d, int64_t b) {
    xx_nufx *r = xx_nufx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nufx(void *p) { xx_nufx_free((xx_nufx *)p); }
static Abstractformat *mk_nintendo_dol(xx_io_device *d, int64_t b) {
    xx_nintendo_dol *r = xx_nintendo_dol_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_dol(void *p) { xx_nintendo_dol_free((xx_nintendo_dol *)p); }
static Abstractformat *mk_nintendo_j3d_bmd(xx_io_device *d, int64_t b) {
    xx_nintendo_j3d_bmd *r = xx_nintendo_j3d_bmd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_j3d_bmd(void *p) { xx_nintendo_j3d_bmd_free((xx_nintendo_j3d_bmd *)p); }
static Abstractformat *mk_nintendo_j3d_btk(xx_io_device *d, int64_t b) {
    xx_nintendo_j3d_btk *r = xx_nintendo_j3d_btk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_j3d_btk(void *p) { xx_nintendo_j3d_btk_free((xx_nintendo_j3d_btk *)p); }
static Abstractformat *mk_nintendo_brstm(xx_io_device *d, int64_t b) {
    xx_nintendo_brstm *r = xx_nintendo_brstm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_brstm(void *p) { xx_nintendo_brstm_free((xx_nintendo_brstm *)p); }
static Abstractformat *mk_nintendo_brwav(xx_io_device *d, int64_t b) {
    xx_nintendo_brwav *r = xx_nintendo_brwav_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_brwav(void *p) { xx_nintendo_brwav_free((xx_nintendo_brwav *)p); }
static Abstractformat *mk_nintendo_brlyt(xx_io_device *d, int64_t b) {
    xx_nintendo_brlyt *r = xx_nintendo_brlyt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_brlyt(void *p) { xx_nintendo_brlyt_free((xx_nintendo_brlyt *)p); }
static Abstractformat *mk_nintendo_brlan(xx_io_device *d, int64_t b) {
    xx_nintendo_brlan *r = xx_nintendo_brlan_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_brlan(void *p) { xx_nintendo_brlan_free((xx_nintendo_brlan *)p); }
static Abstractformat *mk_nintendo_bfsha(xx_io_device *d, int64_t b) {
    xx_nintendo_bfsha *r = xx_nintendo_bfsha_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_bfsha(void *p) { xx_nintendo_bfsha_free((xx_nintendo_bfsha *)p); }
static Abstractformat *mk_cri_usm(xx_io_device *d, int64_t b) {
    xx_cri_usm *r = xx_cri_usm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cri_usm(void *p) { xx_cri_usm_free((xx_cri_usm *)p); }
static Abstractformat *mk_cri_utf(xx_io_device *d, int64_t b) {
    xx_cri_utf *r = xx_cri_utf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cri_utf(void *p) { xx_cri_utf_free((xx_cri_utf *)p); }
static Abstractformat *mk_idtech_iqm(xx_io_device *d, int64_t b) {
    xx_idtech_iqm *r = xx_idtech_iqm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_idtech_iqm(void *p) { xx_idtech_iqm_free((xx_idtech_iqm *)p); }
static Abstractformat *mk_unreal_psk(xx_io_device *d, int64_t b) {
    xx_unreal_psk *r = xx_unreal_psk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_unreal_psk(void *p) { xx_unreal_psk_free((xx_unreal_psk *)p); }
static Abstractformat *mk_unreal_psa(xx_io_device *d, int64_t b) {
    xx_unreal_psa *r = xx_unreal_psa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_unreal_psa(void *p) { xx_unreal_psa_free((xx_unreal_psa *)p); }
static Abstractformat *mk_torque_dts(xx_io_device *d, int64_t b) {
    xx_torque_dts *r = xx_torque_dts_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_torque_dts(void *p) { xx_torque_dts_free((xx_torque_dts *)p); }
static Abstractformat *mk_magicavoxel_vox(xx_io_device *d, int64_t b) {
    xx_magicavoxel_vox *r = xx_magicavoxel_vox_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_magicavoxel_vox(void *p) { xx_magicavoxel_vox_free((xx_magicavoxel_vox *)p); }
static Abstractformat *mk_audio_au(xx_io_device *d, int64_t b) {
    xx_audio_au *r = xx_audio_au_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_au(void *p) { xx_audio_au_free((xx_audio_au *)p); }
static Abstractformat *mk_creative_voc(xx_io_device *d, int64_t b) {
    xx_creative_voc *r = xx_creative_voc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_creative_voc(void *p) { xx_creative_voc_free((xx_creative_voc *)p); }
static Abstractformat *mk_tracker_xm(xx_io_device *d, int64_t b) {
    xx_tracker_xm *r = xx_tracker_xm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_xm(void *p) { xx_tracker_xm_free((xx_tracker_xm *)p); }
static Abstractformat *mk_tracker_s3m(xx_io_device *d, int64_t b) {
    xx_tracker_s3m *r = xx_tracker_s3m_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_s3m(void *p) { xx_tracker_s3m_free((xx_tracker_s3m *)p); }
static Abstractformat *mk_tracker_it(xx_io_device *d, int64_t b) {
    xx_tracker_it *r = xx_tracker_it_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_it(void *p) { xx_tracker_it_free((xx_tracker_it *)p); }
static Abstractformat *mk_tracker_mtm(xx_io_device *d, int64_t b) {
    xx_tracker_mtm *r = xx_tracker_mtm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_mtm(void *p) { xx_tracker_mtm_free((xx_tracker_mtm *)p); }
static Abstractformat *mk_tracker_stm(xx_io_device *d, int64_t b) {
    xx_tracker_stm *r = xx_tracker_stm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_stm(void *p) { xx_tracker_stm_free((xx_tracker_stm *)p); }
static Abstractformat *mk_tracker_669(xx_io_device *d, int64_t b) {
    xx_tracker_669 *r = xx_tracker_669_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_669(void *p) { xx_tracker_669_free((xx_tracker_669 *)p); }
static Abstractformat *mk_tracker_ult(xx_io_device *d, int64_t b) {
    xx_tracker_ult *r = xx_tracker_ult_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_ult(void *p) { xx_tracker_ult_free((xx_tracker_ult *)p); }
static Abstractformat *mk_tracker_okt(xx_io_device *d, int64_t b) {
    xx_tracker_okt *r = xx_tracker_okt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_okt(void *p) { xx_tracker_okt_free((xx_tracker_okt *)p); }
static Abstractformat *mk_nifti2(xx_io_device *d, int64_t b) {
    xx_nifti2 *r = xx_nifti2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nifti2(void *p) { xx_nifti2_free((xx_nifti2 *)p); }
static Abstractformat *mk_lidar_las(xx_io_device *d, int64_t b) {
    xx_lidar_las *r = xx_lidar_las_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lidar_las(void *p) { xx_lidar_las_free((xx_lidar_las *)p); }
static Abstractformat *mk_esri_shp(xx_io_device *d, int64_t b) {
    xx_esri_shp *r = xx_esri_shp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_esri_shp(void *p) { xx_esri_shp_free((xx_esri_shp *)p); }
static Abstractformat *mk_polygon_ply(xx_io_device *d, int64_t b) {
    xx_polygon_ply *r = xx_polygon_ply_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_polygon_ply(void *p) { xx_polygon_ply_free((xx_polygon_ply *)p); }
static Abstractformat *mk_pointcloud_pcd(xx_io_device *d, int64_t b) {
    xx_pointcloud_pcd *r = xx_pointcloud_pcd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pointcloud_pcd(void *p) { xx_pointcloud_pcd_free((xx_pointcloud_pcd *)p); }
static Abstractformat *mk_matlab_mat4(xx_io_device *d, int64_t b) {
    xx_matlab_mat4 *r = xx_matlab_mat4_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_matlab_mat4(void *p) { xx_matlab_mat4_free((xx_matlab_mat4 *)p); }
static Abstractformat *mk_seismic_segy(xx_io_device *d, int64_t b) {
    xx_seismic_segy *r = xx_seismic_segy_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_seismic_segy(void *p) { xx_seismic_segy_free((xx_seismic_segy *)p); }
static Abstractformat *mk_biomedical_bdf(xx_io_device *d, int64_t b) {
    xx_biomedical_bdf *r = xx_biomedical_bdf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_biomedical_bdf(void *p) { xx_biomedical_bdf_free((xx_biomedical_bdf *)p); }
static Abstractformat *mk_erlang_beam(xx_io_device *d, int64_t b) {
    xx_erlang_beam *r = xx_erlang_beam_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_erlang_beam(void *p) { xx_erlang_beam_free((xx_erlang_beam *)p); }
static Abstractformat *mk_java_jmod(xx_io_device *d, int64_t b) {
    xx_java_jmod *r = xx_java_jmod_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_java_jmod(void *p) { xx_java_jmod_free((xx_java_jmod *)p); }
static Abstractformat *mk_tracker_liquid(xx_io_device *d, int64_t b) {
    xx_tracker_liquid *r = xx_tracker_liquid_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_liquid(void *p) { xx_tracker_liquid_free((xx_tracker_liquid *)p); }
static Abstractformat *mk_tracker_dmf(xx_io_device *d, int64_t b) {
    xx_tracker_dmf *r = xx_tracker_dmf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_dmf(void *p) { xx_tracker_dmf_free((xx_tracker_dmf *)p); }
static Abstractformat *mk_tracker_ptm(xx_io_device *d, int64_t b) {
    xx_tracker_ptm *r = xx_tracker_ptm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_ptm(void *p) { xx_tracker_ptm_free((xx_tracker_ptm *)p); }
static Abstractformat *mk_tracker_ams(xx_io_device *d, int64_t b) {
    xx_tracker_ams *r = xx_tracker_ams_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_ams(void *p) { xx_tracker_ams_free((xx_tracker_ams *)p); }
static Abstractformat *mk_tracker_digi(xx_io_device *d, int64_t b) {
    xx_tracker_digi *r = xx_tracker_digi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_digi(void *p) { xx_tracker_digi_free((xx_tracker_digi *)p); }
static Abstractformat *mk_tracker_emod(xx_io_device *d, int64_t b) {
    xx_tracker_emod *r = xx_tracker_emod_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_emod(void *p) { xx_tracker_emod_free((xx_tracker_emod *)p); }
static Abstractformat *mk_tracker_mt2(xx_io_device *d, int64_t b) {
    xx_tracker_mt2 *r = xx_tracker_mt2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_mt2(void *p) { xx_tracker_mt2_free((xx_tracker_mt2 *)p); }
static Abstractformat *mk_audio_dsf(xx_io_device *d, int64_t b) {
    xx_audio_dsf *r = xx_audio_dsf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_dsf(void *p) { xx_audio_dsf_free((xx_audio_dsf *)p); }
static Abstractformat *mk_audio_dff(xx_io_device *d, int64_t b) {
    xx_audio_dff *r = xx_audio_dff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_dff(void *p) { xx_audio_dff_free((xx_audio_dff *)p); }
static Abstractformat *mk_audio_wave64(xx_io_device *d, int64_t b) {
    xx_audio_wave64 *r = xx_audio_wave64_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_wave64(void *p) { xx_audio_wave64_free((xx_audio_wave64 *)p); }
static Abstractformat *mk_audio_adx(xx_io_device *d, int64_t b) {
    xx_audio_adx *r = xx_audio_adx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_adx(void *p) { xx_audio_adx_free((xx_audio_adx *)p); }
static Abstractformat *mk_audio_ast(xx_io_device *d, int64_t b) {
    xx_audio_ast *r = xx_audio_ast_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_ast(void *p) { xx_audio_ast_free((xx_audio_ast *)p); }
static Abstractformat *mk_audio_hca(xx_io_device *d, int64_t b) {
    xx_audio_hca *r = xx_audio_hca_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_hca(void *p) { xx_audio_hca_free((xx_audio_hca *)p); }
static Abstractformat *mk_iff_8svx(xx_io_device *d, int64_t b) {
    xx_iff_8svx *r = xx_iff_8svx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_iff_8svx(void *p) { xx_iff_8svx_free((xx_iff_8svx *)p); }
static Abstractformat *mk_audio_wavpack(xx_io_device *d, int64_t b) {
    xx_audio_wavpack *r = xx_audio_wavpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_wavpack(void *p) { xx_audio_wavpack_free((xx_audio_wavpack *)p); }
static Abstractformat *mk_blender_blend(xx_io_device *d, int64_t b) {
    xx_blender_blend *r = xx_blender_blend_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_blender_blend(void *p) { xx_blender_blend_free((xx_blender_blend *)p); }
static Abstractformat *mk_autodesk_fbx(xx_io_device *d, int64_t b) {
    xx_autodesk_fbx *r = xx_autodesk_fbx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_autodesk_fbx(void *p) { xx_autodesk_fbx_free((xx_autodesk_fbx *)p); }
static Abstractformat *mk_autodesk_3ds(xx_io_device *d, int64_t b) {
    xx_autodesk_3ds *r = xx_autodesk_3ds_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_autodesk_3ds(void *p) { xx_autodesk_3ds_free((xx_autodesk_3ds *)p); }
static Abstractformat *mk_lightwave_lwo2(xx_io_device *d, int64_t b) {
    xx_lightwave_lwo2 *r = xx_lightwave_lwo2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lightwave_lwo2(void *p) { xx_lightwave_lwo2_free((xx_lightwave_lwo2 *)p); }
static Abstractformat *mk_lightwave_mdd(xx_io_device *d, int64_t b) {
    xx_lightwave_mdd *r = xx_lightwave_mdd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lightwave_mdd(void *p) { xx_lightwave_mdd_free((xx_lightwave_mdd *)p); }
static Abstractformat *mk_sony_psp_pbp(xx_io_device *d, int64_t b) {
    xx_sony_psp_pbp *r = xx_sony_psp_pbp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sony_psp_pbp(void *p) { xx_sony_psp_pbp_free((xx_sony_psp_pbp *)p); }
static Abstractformat *mk_flash_video_flv(xx_io_device *d, int64_t b) {
    xx_flash_video_flv *r = xx_flash_video_flv_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_flash_video_flv(void *p) { xx_flash_video_flv_free((xx_flash_video_flv *)p); }
static Abstractformat *mk_nintendo_n64_rom(xx_io_device *d, int64_t b) {
    xx_nintendo_n64_rom *r = xx_nintendo_n64_rom_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_n64_rom(void *p) { xx_nintendo_n64_rom_free((xx_nintendo_n64_rom *)p); }
static Abstractformat *mk_nintendo_gb_rom(xx_io_device *d, int64_t b) {
    xx_nintendo_gb_rom *r = xx_nintendo_gb_rom_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_gb_rom(void *p) { xx_nintendo_gb_rom_free((xx_nintendo_gb_rom *)p); }
static Abstractformat *mk_nintendo_gba_rom(xx_io_device *d, int64_t b) {
    xx_nintendo_gba_rom *r = xx_nintendo_gba_rom_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_gba_rom(void *p) { xx_nintendo_gba_rom_free((xx_nintendo_gba_rom *)p); }
static Abstractformat *mk_sega_megadrive_rom(xx_io_device *d, int64_t b) {
    xx_sega_megadrive_rom *r = xx_sega_megadrive_rom_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sega_megadrive_rom(void *p) { xx_sega_megadrive_rom_free((xx_sega_megadrive_rom *)p); }
static Abstractformat *mk_spring_s3o(xx_io_device *d, int64_t b) {
    xx_spring_s3o *r = xx_spring_s3o_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_spring_s3o(void *p) { xx_spring_s3o_free((xx_spring_s3o *)p); }
static Abstractformat *mk_xna_xnb(xx_io_device *d, int64_t b) {
    xx_xna_xnb *r = xx_xna_xnb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xna_xnb(void *p) { xx_xna_xnb_free((xx_xna_xnb *)p); }
static Abstractformat *mk_lua_bytecode51(xx_io_device *d, int64_t b) {
    xx_lua_bytecode51 *r = xx_lua_bytecode51_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lua_bytecode51(void *p) { xx_lua_bytecode51_free((xx_lua_bytecode51 *)p); }
static Abstractformat *mk_quake_md5mesh(xx_io_device *d, int64_t b) {
    xx_quake_md5mesh *r = xx_quake_md5mesh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_quake_md5mesh(void *p) { xx_quake_md5mesh_free((xx_quake_md5mesh *)p); }
static Abstractformat *mk_tracker_mod(xx_io_device *d, int64_t b) {
    xx_tracker_mod *r = xx_tracker_mod_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_mod(void *p) { xx_tracker_mod_free((xx_tracker_mod *)p); }
static Abstractformat *mk_tracker_far(xx_io_device *d, int64_t b) {
    xx_tracker_far *r = xx_tracker_far_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_far(void *p) { xx_tracker_far_free((xx_tracker_far *)p); }
static Abstractformat *mk_tracker_mdl(xx_io_device *d, int64_t b) {
    xx_tracker_mdl *r = xx_tracker_mdl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_mdl(void *p) { xx_tracker_mdl_free((xx_tracker_mdl *)p); }
static Abstractformat *mk_tracker_gdm(xx_io_device *d, int64_t b) {
    xx_tracker_gdm *r = xx_tracker_gdm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_gdm(void *p) { xx_tracker_gdm_free((xx_tracker_gdm *)p); }
static Abstractformat *mk_tracker_dbm(xx_io_device *d, int64_t b) {
    xx_tracker_dbm *r = xx_tracker_dbm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_dbm(void *p) { xx_tracker_dbm_free((xx_tracker_dbm *)p); }
static Abstractformat *mk_tracker_med(xx_io_device *d, int64_t b) {
    xx_tracker_med *r = xx_tracker_med_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_med(void *p) { xx_tracker_med_free((xx_tracker_med *)p); }
static Abstractformat *mk_tracker_imf(xx_io_device *d, int64_t b) {
    xx_tracker_imf *r = xx_tracker_imf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_imf(void *p) { xx_tracker_imf_free((xx_tracker_imf *)p); }
static Abstractformat *mk_tracker_amf(xx_io_device *d, int64_t b) {
    xx_tracker_amf *r = xx_tracker_amf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_amf(void *p) { xx_tracker_amf_free((xx_tracker_amf *)p); }
static Abstractformat *mk_tracker_psm(xx_io_device *d, int64_t b) {
    xx_tracker_psm *r = xx_tracker_psm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_psm(void *p) { xx_tracker_psm_free((xx_tracker_psm *)p); }
static Abstractformat *mk_steinberg_fxb(xx_io_device *d, int64_t b) {
    xx_steinberg_fxb *r = xx_steinberg_fxb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_steinberg_fxb(void *p) { xx_steinberg_fxb_free((xx_steinberg_fxb *)p); }
static Abstractformat *mk_astronomy_ser(xx_io_device *d, int64_t b) {
    xx_astronomy_ser *r = xx_astronomy_ser_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_astronomy_ser(void *p) { xx_astronomy_ser_free((xx_astronomy_ser *)p); }
static Abstractformat *mk_photontiming_ptu(xx_io_device *d, int64_t b) {
    xx_photontiming_ptu *r = xx_photontiming_ptu_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_photontiming_ptu(void *p) { xx_photontiming_ptu_free((xx_photontiming_ptu *)p); }
static Abstractformat *mk_photontiming_phu(xx_io_device *d, int64_t b) {
    xx_photontiming_phu *r = xx_photontiming_phu_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_photontiming_phu(void *p) { xx_photontiming_phu_free((xx_photontiming_phu *)p); }
static Abstractformat *mk_charmm_dcd(xx_io_device *d, int64_t b) {
    xx_charmm_dcd *r = xx_charmm_dcd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_charmm_dcd(void *p) { xx_charmm_dcd_free((xx_charmm_dcd *)p); }
static Abstractformat *mk_gromacs_trr(xx_io_device *d, int64_t b) {
    xx_gromacs_trr *r = xx_gromacs_trr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gromacs_trr(void *p) { xx_gromacs_trr_free((xx_gromacs_trr *)p); }
static Abstractformat *mk_microscopy_ics(xx_io_device *d, int64_t b) {
    xx_microscopy_ics *r = xx_microscopy_ics_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_microscopy_ics(void *p) { xx_microscopy_ics_free((xx_microscopy_ics *)p); }
static Abstractformat *mk_tecplot_plt(xx_io_device *d, int64_t b) {
    xx_tecplot_plt *r = xx_tecplot_plt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tecplot_plt(void *p) { xx_tecplot_plt_free((xx_tecplot_plt *)p); }
static Abstractformat *mk_fujifilm_raf(xx_io_device *d, int64_t b) {
    xx_fujifilm_raf *r = xx_fujifilm_raf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fujifilm_raf(void *p) { xx_fujifilm_raf_free((xx_fujifilm_raf *)p); }
static Abstractformat *mk_sigma_x3f(xx_io_device *d, int64_t b) {
    xx_sigma_x3f *r = xx_sigma_x3f_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sigma_x3f(void *p) { xx_sigma_x3f_free((xx_sigma_x3f *)p); }
static Abstractformat *mk_minolta_mrw(xx_io_device *d, int64_t b) {
    xx_minolta_mrw *r = xx_minolta_mrw_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_minolta_mrw(void *p) { xx_minolta_mrw_free((xx_minolta_mrw *)p); }
static Abstractformat *mk_vice_x64(xx_io_device *d, int64_t b) {
    xx_vice_x64 *r = xx_vice_x64_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vice_x64(void *p) { xx_vice_x64_free((xx_vice_x64 *)p); }
static Abstractformat *mk_vice_snapshot(xx_io_device *d, int64_t b) {
    xx_vice_snapshot *r = xx_vice_snapshot_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vice_snapshot(void *p) { xx_vice_snapshot_free((xx_vice_snapshot *)p); }
static Abstractformat *mk_commodore_g64(xx_io_device *d, int64_t b) {
    xx_commodore_g64 *r = xx_commodore_g64_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_commodore_g64(void *p) { xx_commodore_g64_free((xx_commodore_g64 *)p); }
static Abstractformat *mk_commodore_p64(xx_io_device *d, int64_t b) {
    xx_commodore_p64 *r = xx_commodore_p64_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_commodore_p64(void *p) { xx_commodore_p64_free((xx_commodore_p64 *)p); }
static Abstractformat *mk_commodore_tap(xx_io_device *d, int64_t b) {
    xx_commodore_tap *r = xx_commodore_tap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_commodore_tap(void *p) { xx_commodore_tap_free((xx_commodore_tap *)p); }
static Abstractformat *mk_zx_spectrum_tzx(xx_io_device *d, int64_t b) {
    xx_zx_spectrum_tzx *r = xx_zx_spectrum_tzx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zx_spectrum_tzx(void *p) { xx_zx_spectrum_tzx_free((xx_zx_spectrum_tzx *)p); }
static Abstractformat *mk_zx_spectrum_szx(xx_io_device *d, int64_t b) {
    xx_zx_spectrum_szx *r = xx_zx_spectrum_szx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zx_spectrum_szx(void *p) { xx_zx_spectrum_szx_free((xx_zx_spectrum_szx *)p); }
static Abstractformat *mk_amstrad_cpc_dsk(xx_io_device *d, int64_t b) {
    xx_amstrad_cpc_dsk *r = xx_amstrad_cpc_dsk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amstrad_cpc_dsk(void *p) { xx_amstrad_cpc_dsk_free((xx_amstrad_cpc_dsk *)p); }
static Abstractformat *mk_atari_st_msa(xx_io_device *d, int64_t b) {
    xx_atari_st_msa *r = xx_atari_st_msa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_atari_st_msa(void *p) { xx_atari_st_msa_free((xx_atari_st_msa *)p); }
static Abstractformat *mk_supercard_scp(xx_io_device *d, int64_t b) {
    xx_supercard_scp *r = xx_supercard_scp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_supercard_scp(void *p) { xx_supercard_scp_free((xx_supercard_scp *)p); }
static Abstractformat *mk_apple_woz(xx_io_device *d, int64_t b) {
    xx_apple_woz *r = xx_apple_woz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apple_woz(void *p) { xx_apple_woz_free((xx_apple_woz *)p); }
static Abstractformat *mk_nintendo_nsf(xx_io_device *d, int64_t b) {
    xx_nintendo_nsf *r = xx_nintendo_nsf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_nsf(void *p) { xx_nintendo_nsf_free((xx_nintendo_nsf *)p); }
static Abstractformat *mk_vgm_log(xx_io_device *d, int64_t b) {
    xx_vgm_log *r = xx_vgm_log_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vgm_log(void *p) { xx_vgm_log_free((xx_vgm_log *)p); }
static Abstractformat *mk_psid_sid(xx_io_device *d, int64_t b) {
    xx_psid_sid *r = xx_psid_sid_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_psid_sid(void *p) { xx_psid_sid_free((xx_psid_sid *)p); }
static Abstractformat *mk_hes_sound(xx_io_device *d, int64_t b) {
    xx_hes_sound *r = xx_hes_sound_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hes_sound(void *p) { xx_hes_sound_free((xx_hes_sound *)p); }
static Abstractformat *mk_audio_dolby_ac3(xx_io_device *d, int64_t b) {
    xx_audio_dolby_ac3 *r = xx_audio_dolby_ac3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_dolby_ac3(void *p) { xx_audio_dolby_ac3_free((xx_audio_dolby_ac3 *)p); }
static Abstractformat *mk_audio_mpeg_mp3(xx_io_device *d, int64_t b) {
    xx_audio_mpeg_mp3 *r = xx_audio_mpeg_mp3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_mpeg_mp3(void *p) { xx_audio_mpeg_mp3_free((xx_audio_mpeg_mp3 *)p); }
static Abstractformat *mk_audio_aac_adts(xx_io_device *d, int64_t b) {
    xx_audio_aac_adts *r = xx_audio_aac_adts_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_aac_adts(void *p) { xx_audio_aac_adts_free((xx_audio_aac_adts *)p); }
static Abstractformat *mk_audio_monkeys_ape(xx_io_device *d, int64_t b) {
    xx_audio_monkeys_ape *r = xx_audio_monkeys_ape_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_monkeys_ape(void *p) { xx_audio_monkeys_ape_free((xx_audio_monkeys_ape *)p); }
static Abstractformat *mk_mpeg_transport_stream(xx_io_device *d, int64_t b) {
    xx_mpeg_transport_stream *r = xx_mpeg_transport_stream_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mpeg_transport_stream(void *p) { xx_mpeg_transport_stream_free((xx_mpeg_transport_stream *)p); }
static Abstractformat *mk_mpeg_program_stream(xx_io_device *d, int64_t b) {
    xx_mpeg_program_stream *r = xx_mpeg_program_stream_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mpeg_program_stream(void *p) { xx_mpeg_program_stream_free((xx_mpeg_program_stream *)p); }
static Abstractformat *mk_realmedia_rm(xx_io_device *d, int64_t b) {
    xx_realmedia_rm *r = xx_realmedia_rm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_realmedia_rm(void *p) { xx_realmedia_rm_free((xx_realmedia_rm *)p); }
static Abstractformat *mk_idtech_roq(xx_io_device *d, int64_t b) {
    xx_idtech_roq *r = xx_idtech_roq_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_idtech_roq(void *p) { xx_idtech_roq_free((xx_idtech_roq *)p); }
static Abstractformat *mk_rad_bink(xx_io_device *d, int64_t b) {
    xx_rad_bink *r = xx_rad_bink_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rad_bink(void *p) { xx_rad_bink_free((xx_rad_bink *)p); }
static Abstractformat *mk_rad_smacker(xx_io_device *d, int64_t b) {
    xx_rad_smacker *r = xx_rad_smacker_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rad_smacker(void *p) { xx_rad_smacker_free((xx_rad_smacker *)p); }
static Abstractformat *mk_interplay_mve(xx_io_device *d, int64_t b) {
    xx_interplay_mve *r = xx_interplay_mve_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_interplay_mve(void *p) { xx_interplay_mve_free((xx_interplay_mve *)p); }
static Abstractformat *mk_westwood_vqa(xx_io_device *d, int64_t b) {
    xx_westwood_vqa *r = xx_westwood_vqa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_westwood_vqa(void *p) { xx_westwood_vqa_free((xx_westwood_vqa *)p); }
static Abstractformat *mk_autodesk_flic(xx_io_device *d, int64_t b) {
    xx_autodesk_flic *r = xx_autodesk_flic_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_autodesk_flic(void *p) { xx_autodesk_flic_free((xx_autodesk_flic *)p); }
static Abstractformat *mk_idtech_md5anim(xx_io_device *d, int64_t b) {
    xx_idtech_md5anim *r = xx_idtech_md5anim_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_idtech_md5anim(void *p) { xx_idtech_md5anim_free((xx_idtech_md5anim *)p); }
static Abstractformat *mk_stereolithography_stl(xx_io_device *d, int64_t b) {
    xx_stereolithography_stl *r = xx_stereolithography_stl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stereolithography_stl(void *p) { xx_stereolithography_stl_free((xx_stereolithography_stl *)p); }
static Abstractformat *mk_garmin_fit(xx_io_device *d, int64_t b) {
    xx_garmin_fit *r = xx_garmin_fit_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_garmin_fit(void *p) { xx_garmin_fit_free((xx_garmin_fit *)p); }
static Abstractformat *mk_rosbag1(xx_io_device *d, int64_t b) {
    xx_rosbag1 *r = xx_rosbag1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rosbag1(void *p) { xx_rosbag1_free((xx_rosbag1 *)p); }
static Abstractformat *mk_mcap(xx_io_device *d, int64_t b) {
    xx_mcap *r = xx_mcap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mcap(void *p) { xx_mcap_free((xx_mcap *)p); }
static Abstractformat *mk_seismic_sac(xx_io_device *d, int64_t b) {
    xx_seismic_sac *r = xx_seismic_sac_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_seismic_sac(void *p) { xx_seismic_sac_free((xx_seismic_sac *)p); }
static Abstractformat *mk_seismic_seg2(xx_io_device *d, int64_t b) {
    xx_seismic_seg2 *r = xx_seismic_seg2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_seismic_seg2(void *p) { xx_seismic_seg2_free((xx_seismic_seg2 *)p); }
static Abstractformat *mk_ucsc_twobit(xx_io_device *d, int64_t b) {
    xx_ucsc_twobit *r = xx_ucsc_twobit_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ucsc_twobit(void *p) { xx_ucsc_twobit_free((xx_ucsc_twobit *)p); }
static Abstractformat *mk_genomics_bgen(xx_io_device *d, int64_t b) {
    xx_genomics_bgen *r = xx_genomics_bgen_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_bgen(void *p) { xx_genomics_bgen_free((xx_genomics_bgen *)p); }
static Abstractformat *mk_openephys_continuous(xx_io_device *d, int64_t b) {
    xx_openephys_continuous *r = xx_openephys_continuous_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_openephys_continuous(void *p) { xx_openephys_continuous_free((xx_openephys_continuous *)p); }
static Abstractformat *mk_mountainsort_mda(xx_io_device *d, int64_t b) {
    xx_mountainsort_mda *r = xx_mountainsort_mda_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mountainsort_mda(void *p) { xx_mountainsort_mda_free((xx_mountainsort_mda *)p); }
static Abstractformat *mk_igor_ibw(xx_io_device *d, int64_t b) {
    xx_igor_ibw *r = xx_igor_ibw_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_igor_ibw(void *p) { xx_igor_ibw_free((xx_igor_ibw *)p); }
static Abstractformat *mk_princeton_spe(xx_io_device *d, int64_t b) {
    xx_princeton_spe *r = xx_princeton_spe_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_princeton_spe(void *p) { xx_princeton_spe_free((xx_princeton_spe *)p); }
static Abstractformat *mk_microscopy_spider(xx_io_device *d, int64_t b) {
    xx_microscopy_spider *r = xx_microscopy_spider_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_microscopy_spider(void *p) { xx_microscopy_spider_free((xx_microscopy_spider *)p); }
static Abstractformat *mk_wmo_grib(xx_io_device *d, int64_t b) {
    xx_wmo_grib *r = xx_wmo_grib_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wmo_grib(void *p) { xx_wmo_grib_free((xx_wmo_grib *)p); }
static Abstractformat *mk_wmo_bufr(xx_io_device *d, int64_t b) {
    xx_wmo_bufr *r = xx_wmo_bufr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wmo_bufr(void *p) { xx_wmo_bufr_free((xx_wmo_bufr *)p); }
static Abstractformat *mk_autocad_dxf(xx_io_device *d, int64_t b) {
    xx_autocad_dxf *r = xx_autocad_dxf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_autocad_dxf(void *p) { xx_autocad_dxf_free((xx_autocad_dxf *)p); }
static Abstractformat *mk_blackrock_nsx(xx_io_device *d, int64_t b) {
    xx_blackrock_nsx *r = xx_blackrock_nsx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_blackrock_nsx(void *p) { xx_blackrock_nsx_free((xx_blackrock_nsx *)p); }
static Abstractformat *mk_blackrock_nev(xx_io_device *d, int64_t b) {
    xx_blackrock_nev *r = xx_blackrock_nev_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_blackrock_nev(void *p) { xx_blackrock_nev_free((xx_blackrock_nev *)p); }
static Abstractformat *mk_lecroy_trc(xx_io_device *d, int64_t b) {
    xx_lecroy_trc *r = xx_lecroy_trc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lecroy_trc(void *p) { xx_lecroy_trc_free((xx_lecroy_trc *)p); }
static Abstractformat *mk_tektronix_isf(xx_io_device *d, int64_t b) {
    xx_tektronix_isf *r = xx_tektronix_isf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tektronix_isf(void *p) { xx_tektronix_isf_free((xx_tektronix_isf *)p); }
static Abstractformat *mk_ircam_sdif(xx_io_device *d, int64_t b) {
    xx_ircam_sdif *r = xx_ircam_sdif_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ircam_sdif(void *p) { xx_ircam_sdif_free((xx_ircam_sdif *)p); }
static Abstractformat *mk_microsoft_msf(xx_io_device *d, int64_t b) {
    xx_microsoft_msf *r = xx_microsoft_msf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_microsoft_msf(void *p) { xx_microsoft_msf_free((xx_microsoft_msf *)p); }
static Abstractformat *mk_windows_registry_hive(xx_io_device *d, int64_t b) {
    xx_windows_registry_hive *r = xx_windows_registry_hive_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_windows_registry_hive(void *p) { xx_windows_registry_hive_free((xx_windows_registry_hive *)p); }
static Abstractformat *mk_windows_evtx(xx_io_device *d, int64_t b) {
    xx_windows_evtx *r = xx_windows_evtx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_windows_evtx(void *p) { xx_windows_evtx_free((xx_windows_evtx *)p); }
static Abstractformat *mk_binary_plist(xx_io_device *d, int64_t b) {
    xx_binary_plist *r = xx_binary_plist_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_binary_plist(void *p) { xx_binary_plist_free((xx_binary_plist *)p); }
static Abstractformat *mk_mongodb_bson(xx_io_device *d, int64_t b) {
    xx_mongodb_bson *r = xx_mongodb_bson_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mongodb_bson(void *p) { xx_mongodb_bson_free((xx_mongodb_bson *)p); }
static Abstractformat *mk_cbor(xx_io_device *d, int64_t b) {
    xx_cbor *r = xx_cbor_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cbor(void *p) { xx_cbor_free((xx_cbor *)p); }
static Abstractformat *mk_openzim(xx_io_device *d, int64_t b) {
    xx_openzim *r = xx_openzim_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_openzim(void *p) { xx_openzim_free((xx_openzim *)p); }
static Abstractformat *mk_apache_orc(xx_io_device *d, int64_t b) {
    xx_apache_orc *r = xx_apache_orc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apache_orc(void *p) { xx_apache_orc_free((xx_apache_orc *)p); }
static Abstractformat *mk_hadoop_sequencefile(xx_io_device *d, int64_t b) {
    xx_hadoop_sequencefile *r = xx_hadoop_sequencefile_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hadoop_sequencefile(void *p) { xx_hadoop_sequencefile_free((xx_hadoop_sequencefile *)p); }
static Abstractformat *mk_leveldb_sstable(xx_io_device *d, int64_t b) {
    xx_leveldb_sstable *r = xx_leveldb_sstable_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_leveldb_sstable(void *p) { xx_leveldb_sstable_free((xx_leveldb_sstable *)p); }
static Abstractformat *mk_snappy_framed(xx_io_device *d, int64_t b) {
    xx_snappy_framed *r = xx_snappy_framed_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_snappy_framed(void *p) { xx_snappy_framed_free((xx_snappy_framed *)p); }
static Abstractformat *mk_lzf_stream(xx_io_device *d, int64_t b) {
    xx_lzf_stream *r = xx_lzf_stream_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzf_stream(void *p) { xx_lzf_stream_free((xx_lzf_stream *)p); }
static Abstractformat *mk_fastlz_sixpack(xx_io_device *d, int64_t b) {
    xx_fastlz_sixpack *r = xx_fastlz_sixpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fastlz_sixpack(void *p) { xx_fastlz_sixpack_free((xx_fastlz_sixpack *)p); }
static Abstractformat *mk_linux_btf(xx_io_device *d, int64_t b) {
    xx_linux_btf *r = xx_linux_btf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_linux_btf(void *p) { xx_linux_btf_free((xx_linux_btf *)p); }
static Abstractformat *mk_flatgeobuf(xx_io_device *d, int64_t b) {
    xx_flatgeobuf *r = xx_flatgeobuf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_flatgeobuf(void *p) { xx_flatgeobuf_free((xx_flatgeobuf *)p); }
static Abstractformat *mk_astc_texture(xx_io_device *d, int64_t b) {
    xx_astc_texture *r = xx_astc_texture_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_astc_texture(void *p) { xx_astc_texture_free((xx_astc_texture *)p); }
static Abstractformat *mk_pkm_texture(xx_io_device *d, int64_t b) {
    xx_pkm_texture *r = xx_pkm_texture_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pkm_texture(void *p) { xx_pkm_texture_free((xx_pkm_texture *)p); }
static Abstractformat *mk_basis_texture(xx_io_device *d, int64_t b) {
    xx_basis_texture *r = xx_basis_texture_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_basis_texture(void *p) { xx_basis_texture_free((xx_basis_texture *)p); }
static Abstractformat *mk_openctm_mesh(xx_io_device *d, int64_t b) {
    xx_openctm_mesh *r = xx_openctm_mesh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_openctm_mesh(void *p) { xx_openctm_mesh_free((xx_openctm_mesh *)p); }
static Abstractformat *mk_font_bdf(xx_io_device *d, int64_t b) {
    xx_font_bdf *r = xx_font_bdf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_font_bdf(void *p) { xx_font_bdf_free((xx_font_bdf *)p); }
static Abstractformat *mk_font_pcf(xx_io_device *d, int64_t b) {
    xx_font_pcf *r = xx_font_pcf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_font_pcf(void *p) { xx_font_pcf_free((xx_font_pcf *)p); }
static Abstractformat *mk_font_psf(xx_io_device *d, int64_t b) {
    xx_font_psf *r = xx_font_psf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_font_psf(void *p) { xx_font_psf_free((xx_font_psf *)p); }
static Abstractformat *mk_font_windows_fnt(xx_io_device *d, int64_t b) {
    xx_font_windows_fnt *r = xx_font_windows_fnt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_font_windows_fnt(void *p) { xx_font_windows_fnt_free((xx_font_windows_fnt *)p); }
static Abstractformat *mk_tex_tfm(xx_io_device *d, int64_t b) {
    xx_tex_tfm *r = xx_tex_tfm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tex_tfm(void *p) { xx_tex_tfm_free((xx_tex_tfm *)p); }
static Abstractformat *mk_tex_pk(xx_io_device *d, int64_t b) {
    xx_tex_pk *r = xx_tex_pk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tex_pk(void *p) { xx_tex_pk_free((xx_tex_pk *)p); }
static Abstractformat *mk_tex_dvi(xx_io_device *d, int64_t b) {
    xx_tex_dvi *r = xx_tex_dvi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tex_dvi(void *p) { xx_tex_dvi_free((xx_tex_dvi *)p); }
static Abstractformat *mk_netpbm_pfm(xx_io_device *d, int64_t b) {
    xx_netpbm_pfm *r = xx_netpbm_pfm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_netpbm_pfm(void *p) { xx_netpbm_pfm_free((xx_netpbm_pfm *)p); }
static Abstractformat *mk_steinberg_vst3preset(xx_io_device *d, int64_t b) {
    xx_steinberg_vst3preset *r = xx_steinberg_vst3preset_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_steinberg_vst3preset(void *p) { xx_steinberg_vst3preset_free((xx_steinberg_vst3preset *)p); }
static Abstractformat *mk_font_bmfont(xx_io_device *d, int64_t b) {
    xx_font_bmfont *r = xx_font_bmfont_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_font_bmfont(void *p) { xx_font_bmfont_free((xx_font_bmfont *)p); }
static Abstractformat *mk_processing_vlw(xx_io_device *d, int64_t b) {
    xx_processing_vlw *r = xx_processing_vlw_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_processing_vlw(void *p) { xx_processing_vlw_free((xx_processing_vlw *)p); }
static Abstractformat *mk_snes_spc(xx_io_device *d, int64_t b) {
    xx_snes_spc *r = xx_snes_spc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_snes_spc(void *p) { xx_snes_spc_free((xx_snes_spc *)p); }
static Abstractformat *mk_gameboy_gbs(xx_io_device *d, int64_t b) {
    xx_gameboy_gbs *r = xx_gameboy_gbs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gameboy_gbs(void *p) { xx_gameboy_gbs_free((xx_gameboy_gbs *)p); }
static Abstractformat *mk_sega_sgc(xx_io_device *d, int64_t b) {
    xx_sega_sgc *r = xx_sega_sgc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sega_sgc(void *p) { xx_sega_sgc_free((xx_sega_sgc *)p); }
static Abstractformat *mk_s98_log(xx_io_device *d, int64_t b) {
    xx_s98_log *r = xx_s98_log_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_s98_log(void *p) { xx_s98_log_free((xx_s98_log *)p); }
static Abstractformat *mk_atari_sap(xx_io_device *d, int64_t b) {
    xx_atari_sap *r = xx_atari_sap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_atari_sap(void *p) { xx_atari_sap_free((xx_atari_sap *)p); }
static Abstractformat *mk_sc68_music(xx_io_device *d, int64_t b) {
    xx_sc68_music *r = xx_sc68_music_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sc68_music(void *p) { xx_sc68_music_free((xx_sc68_music *)p); }
static Abstractformat *mk_zx_spectrum_pzx(xx_io_device *d, int64_t b) {
    xx_zx_spectrum_pzx *r = xx_zx_spectrum_pzx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zx_spectrum_pzx(void *p) { xx_zx_spectrum_pzx_free((xx_zx_spectrum_pzx *)p); }
static Abstractformat *mk_acorn_uef(xx_io_device *d, int64_t b) {
    xx_acorn_uef *r = xx_acorn_uef_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_acorn_uef(void *p) { xx_acorn_uef_free((xx_acorn_uef *)p); }
static Abstractformat *mk_nintendo_unif(xx_io_device *d, int64_t b) {
    xx_nintendo_unif *r = xx_nintendo_unif_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_unif(void *p) { xx_nintendo_unif_free((xx_nintendo_unif *)p); }
static Abstractformat *mk_nintendo_fds(xx_io_device *d, int64_t b) {
    xx_nintendo_fds *r = xx_nintendo_fds_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_fds(void *p) { xx_nintendo_fds_free((xx_nintendo_fds *)p); }
static Abstractformat *mk_ucsc_bigwig(xx_io_device *d, int64_t b) {
    xx_ucsc_bigwig *r = xx_ucsc_bigwig_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ucsc_bigwig(void *p) { xx_ucsc_bigwig_free((xx_ucsc_bigwig *)p); }
static Abstractformat *mk_ucsc_bigbed(xx_io_device *d, int64_t b) {
    xx_ucsc_bigbed *r = xx_ucsc_bigbed_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ucsc_bigbed(void *p) { xx_ucsc_bigbed_free((xx_ucsc_bigbed *)p); }
static Abstractformat *mk_phylo_nexus(xx_io_device *d, int64_t b) {
    xx_phylo_nexus *r = xx_phylo_nexus_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_phylo_nexus(void *p) { xx_phylo_nexus_free((xx_phylo_nexus *)p); }
static Abstractformat *mk_phylo_newick(xx_io_device *d, int64_t b) {
    xx_phylo_newick *r = xx_phylo_newick_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_phylo_newick(void *p) { xx_phylo_newick_free((xx_phylo_newick *)p); }
static Abstractformat *mk_sqlite_rollback_journal(xx_io_device *d, int64_t b) {
    xx_sqlite_rollback_journal *r = xx_sqlite_rollback_journal_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sqlite_rollback_journal(void *p) { xx_sqlite_rollback_journal_free((xx_sqlite_rollback_journal *)p); }
static Abstractformat *mk_neuroscan_cnt(xx_io_device *d, int64_t b) {
    xx_neuroscan_cnt *r = xx_neuroscan_cnt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_neuroscan_cnt(void *p) { xx_neuroscan_cnt_free((xx_neuroscan_cnt *)p); }
static Abstractformat *mk_axona_tetrode(xx_io_device *d, int64_t b) {
    xx_axona_tetrode *r = xx_axona_tetrode_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_axona_tetrode(void *p) { xx_axona_tetrode_free((xx_axona_tetrode *)p); }
static Abstractformat *mk_python_pickle(xx_io_device *d, int64_t b) {
    xx_python_pickle *r = xx_python_pickle_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_python_pickle(void *p) { xx_python_pickle_free((xx_python_pickle *)p); }
static Abstractformat *mk_inivation_aedat(xx_io_device *d, int64_t b) {
    xx_inivation_aedat *r = xx_inivation_aedat_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_inivation_aedat(void *p) { xx_inivation_aedat_free((xx_inivation_aedat *)p); }
static Abstractformat *mk_python_marshal(xx_io_device *d, int64_t b) {
    xx_python_marshal *r = xx_python_marshal_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_python_marshal(void *p) { xx_python_marshal_free((xx_python_marshal *)p); }
static Abstractformat *mk_nix_nar(xx_io_device *d, int64_t b) {
    xx_nix_nar *r = xx_nix_nar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nix_nar(void *p) { xx_nix_nar_free((xx_nix_nar *)p); }
static Abstractformat *mk_redis_rdb(xx_io_device *d, int64_t b) {
    xx_redis_rdb *r = xx_redis_rdb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_redis_rdb(void *p) { xx_redis_rdb_free((xx_redis_rdb *)p); }
static Abstractformat *mk_postgres_custom(xx_io_device *d, int64_t b) {
    xx_postgres_custom *r = xx_postgres_custom_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_postgres_custom(void *p) { xx_postgres_custom_free((xx_postgres_custom *)p); }
static Abstractformat *mk_mysql_binlog(xx_io_device *d, int64_t b) {
    xx_mysql_binlog *r = xx_mysql_binlog_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mysql_binlog(void *p) { xx_mysql_binlog_free((xx_mysql_binlog *)p); }
static Abstractformat *mk_kafka_record_batch(xx_io_device *d, int64_t b) {
    xx_kafka_record_batch *r = xx_kafka_record_batch_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_kafka_record_batch(void *p) { xx_kafka_record_batch_free((xx_kafka_record_batch *)p); }
static Abstractformat *mk_android_binary_xml(xx_io_device *d, int64_t b) {
    xx_android_binary_xml *r = xx_android_binary_xml_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_android_binary_xml(void *p) { xx_android_binary_xml_free((xx_android_binary_xml *)p); }
static Abstractformat *mk_android_resources_arsc(xx_io_device *d, int64_t b) {
    xx_android_resources_arsc *r = xx_android_resources_arsc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_android_resources_arsc(void *p) { xx_android_resources_arsc_free((xx_android_resources_arsc *)p); }
static Abstractformat *mk_msgpack(xx_io_device *d, int64_t b) {
    xx_msgpack *r = xx_msgpack_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_msgpack(void *p) { xx_msgpack_free((xx_msgpack *)p); }
static Abstractformat *mk_ubjson(xx_io_device *d, int64_t b) {
    xx_ubjson *r = xx_ubjson_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ubjson(void *p) { xx_ubjson_free((xx_ubjson *)p); }
static Abstractformat *mk_bittorrent_metainfo(xx_io_device *d, int64_t b) {
    xx_bittorrent_metainfo *r = xx_bittorrent_metainfo_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bittorrent_metainfo(void *p) { xx_bittorrent_metainfo_free((xx_bittorrent_metainfo *)p); }
static Abstractformat *mk_erlang_external_term(xx_io_device *d, int64_t b) {
    xx_erlang_external_term *r = xx_erlang_external_term_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_erlang_external_term(void *p) { xx_erlang_external_term_free((xx_erlang_external_term *)p); }
static Abstractformat *mk_capnproto_message(xx_io_device *d, int64_t b) {
    xx_capnproto_message *r = xx_capnproto_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_capnproto_message(void *p) { xx_capnproto_message_free((xx_capnproto_message *)p); }
static Abstractformat *mk_dbus_message(xx_io_device *d, int64_t b) {
    xx_dbus_message *r = xx_dbus_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dbus_message(void *p) { xx_dbus_message_free((xx_dbus_message *)p); }
static Abstractformat *mk_windows_shell_link(xx_io_device *d, int64_t b) {
    xx_windows_shell_link *r = xx_windows_shell_link_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_windows_shell_link(void *p) { xx_windows_shell_link_free((xx_windows_shell_link *)p); }
static Abstractformat *mk_pkcs7_cms(xx_io_device *d, int64_t b) {
    xx_pkcs7_cms *r = xx_pkcs7_cms_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pkcs7_cms(void *p) { xx_pkcs7_cms_free((xx_pkcs7_cms *)p); }
static Abstractformat *mk_wavefront_obj(xx_io_device *d, int64_t b) {
    xx_wavefront_obj *r = xx_wavefront_obj_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wavefront_obj(void *p) { xx_wavefront_obj_free((xx_wavefront_obj *)p); }
static Abstractformat *mk_off_mesh(xx_io_device *d, int64_t b) {
    xx_off_mesh *r = xx_off_mesh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_off_mesh(void *p) { xx_off_mesh_free((xx_off_mesh *)p); }
static Abstractformat *mk_ac3d_model(xx_io_device *d, int64_t b) {
    xx_ac3d_model *r = xx_ac3d_model_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ac3d_model(void *p) { xx_ac3d_model_free((xx_ac3d_model *)p); }
static Abstractformat *mk_qubicle_qb(xx_io_device *d, int64_t b) {
    xx_qubicle_qb *r = xx_qubicle_qb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qubicle_qb(void *p) { xx_qubicle_qb_free((xx_qubicle_qb *)p); }
static Abstractformat *mk_terragen_ter(xx_io_device *d, int64_t b) {
    xx_terragen_ter *r = xx_terragen_ter_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_terragen_ter(void *p) { xx_terragen_ter_free((xx_terragen_ter *)p); }
static Abstractformat *mk_gimp_xcf(xx_io_device *d, int64_t b) {
    xx_gimp_xcf *r = xx_gimp_xcf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gimp_xcf(void *p) { xx_gimp_xcf_free((xx_gimp_xcf *)p); }
static Abstractformat *mk_photoshop_abr(xx_io_device *d, int64_t b) {
    xx_photoshop_abr *r = xx_photoshop_abr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_photoshop_abr(void *p) { xx_photoshop_abr_free((xx_photoshop_abr *)p); }
static Abstractformat *mk_softimage_pic(xx_io_device *d, int64_t b) {
    xx_softimage_pic *r = xx_softimage_pic_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_softimage_pic(void *p) { xx_softimage_pic_free((xx_softimage_pic *)p); }
static Abstractformat *mk_alias_pix(xx_io_device *d, int64_t b) {
    xx_alias_pix *r = xx_alias_pix_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_alias_pix(void *p) { xx_alias_pix_free((xx_alias_pix *)p); }
static Abstractformat *mk_qt_qpicture(xx_io_device *d, int64_t b) {
    xx_qt_qpicture *r = xx_qt_qpicture_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qt_qpicture(void *p) { xx_qt_qpicture_free((xx_qt_qpicture *)p); }
static Abstractformat *mk_font_type1_pfb(xx_io_device *d, int64_t b) {
    xx_font_type1_pfb *r = xx_font_type1_pfb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_font_type1_pfb(void *p) { xx_font_type1_pfb_free((xx_font_type1_pfb *)p); }
static Abstractformat *mk_font_gem_fnt(xx_io_device *d, int64_t b) {
    xx_font_gem_fnt *r = xx_font_gem_fnt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_font_gem_fnt(void *p) { xx_font_gem_fnt_free((xx_font_gem_fnt *)p); }
static Abstractformat *mk_tex_gf(xx_io_device *d, int64_t b) {
    xx_tex_gf *r = xx_tex_gf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tex_gf(void *p) { xx_tex_gf_free((xx_tex_gf *)p); }
static Abstractformat *mk_bpg_image(xx_io_device *d, int64_t b) {
    xx_bpg_image *r = xx_bpg_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bpg_image(void *p) { xx_bpg_image_free((xx_bpg_image *)p); }
static Abstractformat *mk_mng_animation(xx_io_device *d, int64_t b) {
    xx_mng_animation *r = xx_mng_animation_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mng_animation(void *p) { xx_mng_animation_free((xx_mng_animation *)p); }
static Abstractformat *mk_atari_7800_a78(xx_io_device *d, int64_t b) {
    xx_atari_7800_a78 *r = xx_atari_7800_a78_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_atari_7800_a78(void *p) { xx_atari_7800_a78_free((xx_atari_7800_a78 *)p); }
static Abstractformat *mk_commodore_pc64(xx_io_device *d, int64_t b) {
    xx_commodore_pc64 *r = xx_commodore_pc64_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_commodore_pc64(void *p) { xx_commodore_pc64_free((xx_commodore_pc64 *)p); }
static Abstractformat *mk_atari_cas(xx_io_device *d, int64_t b) {
    xx_atari_cas *r = xx_atari_cas_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_atari_cas(void *p) { xx_atari_cas_free((xx_atari_cas *)p); }
static Abstractformat *mk_msx_cas(xx_io_device *d, int64_t b) {
    xx_msx_cas *r = xx_msx_cas_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_msx_cas(void *p) { xx_msx_cas_free((xx_msx_cas *)p); }
static Abstractformat *mk_oric_tap(xx_io_device *d, int64_t b) {
    xx_oric_tap *r = xx_oric_tap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_oric_tap(void *p) { xx_oric_tap_free((xx_oric_tap *)p); }
static Abstractformat *mk_dragon_cas(xx_io_device *d, int64_t b) {
    xx_dragon_cas *r = xx_dragon_cas_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dragon_cas(void *p) { xx_dragon_cas_free((xx_dragon_cas *)p); }
static Abstractformat *mk_amiga_ahx(xx_io_device *d, int64_t b) {
    xx_amiga_ahx *r = xx_amiga_ahx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amiga_ahx(void *p) { xx_amiga_ahx_free((xx_amiga_ahx *)p); }
static Abstractformat *mk_amstrad_cpc_sna(xx_io_device *d, int64_t b) {
    xx_amstrad_cpc_sna *r = xx_amstrad_cpc_sna_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amstrad_cpc_sna(void *p) { xx_amstrad_cpc_sna_free((xx_amstrad_cpc_sna *)p); }
static Abstractformat *mk_vtech_vz(xx_io_device *d, int64_t b) {
    xx_vtech_vz *r = xx_vtech_vz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vtech_vz(void *p) { xx_vtech_vz_free((xx_vtech_vz *)p); }
static Abstractformat *mk_zx_hobeta(xx_io_device *d, int64_t b) {
    xx_zx_hobeta *r = xx_zx_hobeta_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zx_hobeta(void *p) { xx_zx_hobeta_free((xx_zx_hobeta *)p); }
static Abstractformat *mk_genomics_fasta(xx_io_device *d, int64_t b) {
    xx_genomics_fasta *r = xx_genomics_fasta_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_fasta(void *p) { xx_genomics_fasta_free((xx_genomics_fasta *)p); }
static Abstractformat *mk_genomics_fastq(xx_io_device *d, int64_t b) {
    xx_genomics_fastq *r = xx_genomics_fastq_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_fastq(void *p) { xx_genomics_fastq_free((xx_genomics_fastq *)p); }
static Abstractformat *mk_genomics_sam(xx_io_device *d, int64_t b) {
    xx_genomics_sam *r = xx_genomics_sam_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_sam(void *p) { xx_genomics_sam_free((xx_genomics_sam *)p); }
static Abstractformat *mk_opendx_field(xx_io_device *d, int64_t b) {
    xx_opendx_field *r = xx_opendx_field_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_opendx_field(void *p) { xx_opendx_field_free((xx_opendx_field *)p); }
static Abstractformat *mk_genomics_vcf(xx_io_device *d, int64_t b) {
    xx_genomics_vcf *r = xx_genomics_vcf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_vcf(void *p) { xx_genomics_vcf_free((xx_genomics_vcf *)p); }
static Abstractformat *mk_genomics_gff3(xx_io_device *d, int64_t b) {
    xx_genomics_gff3 *r = xx_genomics_gff3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_gff3(void *p) { xx_genomics_gff3_free((xx_genomics_gff3 *)p); }
static Abstractformat *mk_protein_pdb(xx_io_device *d, int64_t b) {
    xx_protein_pdb *r = xx_protein_pdb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_protein_pdb(void *p) { xx_protein_pdb_free((xx_protein_pdb *)p); }
static Abstractformat *mk_protein_mmcif(xx_io_device *d, int64_t b) {
    xx_protein_mmcif *r = xx_protein_mmcif_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_protein_mmcif(void *p) { xx_protein_mmcif_free((xx_protein_mmcif *)p); }
static Abstractformat *mk_matrix_market(xx_io_device *d, int64_t b) {
    xx_matrix_market *r = xx_matrix_market_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_matrix_market(void *p) { xx_matrix_market_free((xx_matrix_market *)p); }
static Abstractformat *mk_gromacs_gro(xx_io_device *d, int64_t b) {
    xx_gromacs_gro *r = xx_gromacs_gro_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gromacs_gro(void *p) { xx_gromacs_gro_free((xx_gromacs_gro *)p); }
static Abstractformat *mk_minecraft_nbt(xx_io_device *d, int64_t b) {
    xx_minecraft_nbt *r = xx_minecraft_nbt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_minecraft_nbt(void *p) { xx_minecraft_nbt_free((xx_minecraft_nbt *)p); }
static Abstractformat *mk_amazon_ion_binary(xx_io_device *d, int64_t b) {
    xx_amazon_ion_binary *r = xx_amazon_ion_binary_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amazon_ion_binary(void *p) { xx_amazon_ion_binary_free((xx_amazon_ion_binary *)p); }
static Abstractformat *mk_leveldb_log(xx_io_device *d, int64_t b) {
    xx_leveldb_log *r = xx_leveldb_log_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_leveldb_log(void *p) { xx_leveldb_log_free((xx_leveldb_log *)p); }
static Abstractformat *mk_dns_message(xx_io_device *d, int64_t b) {
    xx_dns_message *r = xx_dns_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dns_message(void *p) { xx_dns_message_free((xx_dns_message *)p); }
static Abstractformat *mk_rocksdb_blob(xx_io_device *d, int64_t b) {
    xx_rocksdb_blob *r = xx_rocksdb_blob_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rocksdb_blob(void *p) { xx_rocksdb_blob_free((xx_rocksdb_blob *)p); }
static Abstractformat *mk_mongodb_wire(xx_io_device *d, int64_t b) {
    xx_mongodb_wire *r = xx_mongodb_wire_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mongodb_wire(void *p) { xx_mongodb_wire_free((xx_mongodb_wire *)p); }
static Abstractformat *mk_redis_resp(xx_io_device *d, int64_t b) {
    xx_redis_resp *r = xx_redis_resp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_redis_resp(void *p) { xx_redis_resp_free((xx_redis_resp *)p); }
static Abstractformat *mk_mqtt_packets(xx_io_device *d, int64_t b) {
    xx_mqtt_packets *r = xx_mqtt_packets_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mqtt_packets(void *p) { xx_mqtt_packets_free((xx_mqtt_packets *)p); }
static Abstractformat *mk_amqp_frames(xx_io_device *d, int64_t b) {
    xx_amqp_frames *r = xx_amqp_frames_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amqp_frames(void *p) { xx_amqp_frames_free((xx_amqp_frames *)p); }
static Abstractformat *mk_thrift_compact(xx_io_device *d, int64_t b) {
    xx_thrift_compact *r = xx_thrift_compact_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_thrift_compact(void *p) { xx_thrift_compact_free((xx_thrift_compact *)p); }
static Abstractformat *mk_x509_certificate(xx_io_device *d, int64_t b) {
    xx_x509_certificate *r = xx_x509_certificate_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_x509_certificate(void *p) { xx_x509_certificate_free((xx_x509_certificate *)p); }
static Abstractformat *mk_pkcs10_csr(xx_io_device *d, int64_t b) {
    xx_pkcs10_csr *r = xx_pkcs10_csr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pkcs10_csr(void *p) { xx_pkcs10_csr_free((xx_pkcs10_csr *)p); }
static Abstractformat *mk_pkcs12_pfx(xx_io_device *d, int64_t b) {
    xx_pkcs12_pfx *r = xx_pkcs12_pfx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pkcs12_pfx(void *p) { xx_pkcs12_pfx_free((xx_pkcs12_pfx *)p); }
static Abstractformat *mk_openssh_private_key(xx_io_device *d, int64_t b) {
    xx_openssh_private_key *r = xx_openssh_private_key_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_openssh_private_key(void *p) { xx_openssh_private_key_free((xx_openssh_private_key *)p); }
static Abstractformat *mk_kerberos_keytab(xx_io_device *d, int64_t b) {
    xx_kerberos_keytab *r = xx_kerberos_keytab_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_kerberos_keytab(void *p) { xx_kerberos_keytab_free((xx_kerberos_keytab *)p); }
static Abstractformat *mk_gimp_gpl(xx_io_device *d, int64_t b) {
    xx_gimp_gpl *r = xx_gimp_gpl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gimp_gpl(void *p) { xx_gimp_gpl_free((xx_gimp_gpl *)p); }
static Abstractformat *mk_gimp_ggr(xx_io_device *d, int64_t b) {
    xx_gimp_ggr *r = xx_gimp_ggr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gimp_ggr(void *p) { xx_gimp_ggr_free((xx_gimp_ggr *)p); }
static Abstractformat *mk_iridas_cube_lut(xx_io_device *d, int64_t b) {
    xx_iridas_cube_lut *r = xx_iridas_cube_lut_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_iridas_cube_lut(void *p) { xx_iridas_cube_lut_free((xx_iridas_cube_lut *)p); }
static Abstractformat *mk_hpgl_plot(xx_io_device *d, int64_t b) {
    xx_hpgl_plot *r = xx_hpgl_plot_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hpgl_plot(void *p) { xx_hpgl_plot_free((xx_hpgl_plot *)p); }
static Abstractformat *mk_paintshop_psp(xx_io_device *d, int64_t b) {
    xx_paintshop_psp *r = xx_paintshop_psp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_paintshop_psp(void *p) { xx_paintshop_psp_free((xx_paintshop_psp *)p); }
static Abstractformat *mk_photoshop_pat(xx_io_device *d, int64_t b) {
    xx_photoshop_pat *r = xx_photoshop_pat_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_photoshop_pat(void *p) { xx_photoshop_pat_free((xx_photoshop_pat *)p); }
static Abstractformat *mk_mmd_pmx(xx_io_device *d, int64_t b) {
    xx_mmd_pmx *r = xx_mmd_pmx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mmd_pmx(void *p) { xx_mmd_pmx_free((xx_mmd_pmx *)p); }
static Abstractformat *mk_metasequoia_mqo(xx_io_device *d, int64_t b) {
    xx_metasequoia_mqo *r = xx_metasequoia_mqo_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_metasequoia_mqo(void *p) { xx_metasequoia_mqo_free((xx_metasequoia_mqo *)p); }
static Abstractformat *mk_calma_gdsii(xx_io_device *d, int64_t b) {
    xx_calma_gdsii *r = xx_calma_gdsii_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_calma_gdsii(void *p) { xx_calma_gdsii_free((xx_calma_gdsii *)p); }
static Abstractformat *mk_autodesk_ase(xx_io_device *d, int64_t b) {
    xx_autodesk_ase *r = xx_autodesk_ase_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_autodesk_ase(void *p) { xx_autodesk_ase_free((xx_autodesk_ase *)p); }
static Abstractformat *mk_freesurfer_surface(xx_io_device *d, int64_t b) {
    xx_freesurfer_surface *r = xx_freesurfer_surface_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_freesurfer_surface(void *p) { xx_freesurfer_surface_free((xx_freesurfer_surface *)p); }
static Abstractformat *mk_gmsh_msh(xx_io_device *d, int64_t b) {
    xx_gmsh_msh *r = xx_gmsh_msh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gmsh_msh(void *p) { xx_gmsh_msh_free((xx_gmsh_msh *)p); }
static Abstractformat *mk_netgen_vol(xx_io_device *d, int64_t b) {
    xx_netgen_vol *r = xx_netgen_vol_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_netgen_vol(void *p) { xx_netgen_vol_free((xx_netgen_vol *)p); }
static Abstractformat *mk_font_afm(xx_io_device *d, int64_t b) {
    xx_font_afm *r = xx_font_afm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_font_afm(void *p) { xx_font_afm_free((xx_font_afm *)p); }
static Abstractformat *mk_tiled_tmx(xx_io_device *d, int64_t b) {
    xx_tiled_tmx *r = xx_tiled_tmx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tiled_tmx(void *p) { xx_tiled_tmx_free((xx_tiled_tmx *)p); }
static Abstractformat *mk_nintendo_sdat(xx_io_device *d, int64_t b) {
    xx_nintendo_sdat *r = xx_nintendo_sdat_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nintendo_sdat(void *p) { xx_nintendo_sdat_free((xx_nintendo_sdat *)p); }
static Abstractformat *mk_sony_vab(xx_io_device *d, int64_t b) {
    xx_sony_vab *r = xx_sony_vab_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sony_vab(void *p) { xx_sony_vab_free((xx_sony_vab *)p); }
static Abstractformat *mk_yamaha_ym(xx_io_device *d, int64_t b) {
    xx_yamaha_ym *r = xx_yamaha_ym_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_yamaha_ym(void *p) { xx_yamaha_ym_free((xx_yamaha_ym *)p); }
static Abstractformat *mk_zx_ayemul(xx_io_device *d, int64_t b) {
    xx_zx_ayemul *r = xx_zx_ayemul_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_zx_ayemul(void *p) { xx_zx_ayemul_free((xx_zx_ayemul *)p); }
static Abstractformat *mk_dragon_vdk(xx_io_device *d, int64_t b) {
    xx_dragon_vdk *r = xx_dragon_vdk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dragon_vdk(void *p) { xx_dragon_vdk_free((xx_dragon_vdk *)p); }
static Abstractformat *mk_apple_a2r(xx_io_device *d, int64_t b) {
    xx_apple_a2r *r = xx_apple_a2r_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apple_a2r(void *p) { xx_apple_a2r_free((xx_apple_a2r *)p); }
static Abstractformat *mk_atari_atr(xx_io_device *d, int64_t b) {
    xx_atari_atr *r = xx_atari_atr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_atari_atr(void *p) { xx_atari_atr_free((xx_atari_atr *)p); }
static Abstractformat *mk_atari_pasti_stx(xx_io_device *d, int64_t b) {
    xx_atari_pasti_stx *r = xx_atari_pasti_stx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_atari_pasti_stx(void *p) { xx_atari_pasti_stx_free((xx_atari_pasti_stx *)p); }
static Abstractformat *mk_amiga_ipf(xx_io_device *d, int64_t b) {
    xx_amiga_ipf *r = xx_amiga_ipf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amiga_ipf(void *p) { xx_amiga_ipf_free((xx_amiga_ipf *)p); }
static Abstractformat *mk_tracker_dtt(xx_io_device *d, int64_t b) {
    xx_tracker_dtt *r = xx_tracker_dtt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_dtt(void *p) { xx_tracker_dtt_free((xx_tracker_dtt *)p); }
static Abstractformat *mk_gaussian_cube(xx_io_device *d, int64_t b) {
    xx_gaussian_cube *r = xx_gaussian_cube_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gaussian_cube(void *p) { xx_gaussian_cube_free((xx_gaussian_cube *)p); }
static Abstractformat *mk_molecule_xyz(xx_io_device *d, int64_t b) {
    xx_molecule_xyz *r = xx_molecule_xyz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_molecule_xyz(void *p) { xx_molecule_xyz_free((xx_molecule_xyz *)p); }
static Abstractformat *mk_mdl_molfile(xx_io_device *d, int64_t b) {
    xx_mdl_molfile *r = xx_mdl_molfile_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mdl_molfile(void *p) { xx_mdl_molfile_free((xx_mdl_molfile *)p); }
static Abstractformat *mk_tripos_mol2(xx_io_device *d, int64_t b) {
    xx_tripos_mol2 *r = xx_tripos_mol2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tripos_mol2(void *p) { xx_tripos_mol2_free((xx_tripos_mol2 *)p); }
static Abstractformat *mk_xcrysden_xsf(xx_io_device *d, int64_t b) {
    xx_xcrysden_xsf *r = xx_xcrysden_xsf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xcrysden_xsf(void *p) { xx_xcrysden_xsf_free((xx_xcrysden_xsf *)p); }
static Abstractformat *mk_amber_prmtop(xx_io_device *d, int64_t b) {
    xx_amber_prmtop *r = xx_amber_prmtop_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amber_prmtop(void *p) { xx_amber_prmtop_free((xx_amber_prmtop *)p); }
static Abstractformat *mk_amber_restart(xx_io_device *d, int64_t b) {
    xx_amber_restart *r = xx_amber_restart_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amber_restart(void *p) { xx_amber_restart_free((xx_amber_restart *)p); }
static Abstractformat *mk_gaussian_fchk(xx_io_device *d, int64_t b) {
    xx_gaussian_fchk *r = xx_gaussian_fchk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gaussian_fchk(void *p) { xx_gaussian_fchk_free((xx_gaussian_fchk *)p); }
static Abstractformat *mk_jcamp_dx(xx_io_device *d, int64_t b) {
    xx_jcamp_dx *r = xx_jcamp_dx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jcamp_dx(void *p) { xx_jcamp_dx_free((xx_jcamp_dx *)p); }
static Abstractformat *mk_dl_poly_config(xx_io_device *d, int64_t b) {
    xx_dl_poly_config *r = xx_dl_poly_config_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dl_poly_config(void *p) { xx_dl_poly_config_free((xx_dl_poly_config *)p); }
static Abstractformat *mk_http1_message(xx_io_device *d, int64_t b) {
    xx_http1_message *r = xx_http1_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_http1_message(void *p) { xx_http1_message_free((xx_http1_message *)p); }
static Abstractformat *mk_websocket_frames(xx_io_device *d, int64_t b) {
    xx_websocket_frames *r = xx_websocket_frames_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_websocket_frames(void *p) { xx_websocket_frames_free((xx_websocket_frames *)p); }
static Abstractformat *mk_coap_message(xx_io_device *d, int64_t b) {
    xx_coap_message *r = xx_coap_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_coap_message(void *p) { xx_coap_message_free((xx_coap_message *)p); }
static Abstractformat *mk_stun_message(xx_io_device *d, int64_t b) {
    xx_stun_message *r = xx_stun_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stun_message(void *p) { xx_stun_message_free((xx_stun_message *)p); }
static Abstractformat *mk_dhcp_message(xx_io_device *d, int64_t b) {
    xx_dhcp_message *r = xx_dhcp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dhcp_message(void *p) { xx_dhcp_message_free((xx_dhcp_message *)p); }
static Abstractformat *mk_radius_packet(xx_io_device *d, int64_t b) {
    xx_radius_packet *r = xx_radius_packet_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_radius_packet(void *p) { xx_radius_packet_free((xx_radius_packet *)p); }
static Abstractformat *mk_snmp_message(xx_io_device *d, int64_t b) {
    xx_snmp_message *r = xx_snmp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_snmp_message(void *p) { xx_snmp_message_free((xx_snmp_message *)p); }
static Abstractformat *mk_ldap_message(xx_io_device *d, int64_t b) {
    xx_ldap_message *r = xx_ldap_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ldap_message(void *p) { xx_ldap_message_free((xx_ldap_message *)p); }
static Abstractformat *mk_tls_records(xx_io_device *d, int64_t b) {
    xx_tls_records *r = xx_tls_records_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tls_records(void *p) { xx_tls_records_free((xx_tls_records *)p); }
static Abstractformat *mk_jks_keystore(xx_io_device *d, int64_t b) {
    xx_jks_keystore *r = xx_jks_keystore_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jks_keystore(void *p) { xx_jks_keystore_free((xx_jks_keystore *)p); }
static Abstractformat *mk_java_serialization(xx_io_device *d, int64_t b) {
    xx_java_serialization *r = xx_java_serialization_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_java_serialization(void *p) { xx_java_serialization_free((xx_java_serialization *)p); }
static Abstractformat *mk_x509_crl(xx_io_device *d, int64_t b) {
    xx_x509_crl *r = xx_x509_crl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_x509_crl(void *p) { xx_x509_crl_free((xx_x509_crl *)p); }
static Abstractformat *mk_ocsp_response(xx_io_device *d, int64_t b) {
    xx_ocsp_response *r = xx_ocsp_response_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ocsp_response(void *p) { xx_ocsp_response_free((xx_ocsp_response *)p); }
static Abstractformat *mk_lmdb_data(xx_io_device *d, int64_t b) {
    xx_lmdb_data *r = xx_lmdb_data_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lmdb_data(void *p) { xx_lmdb_data_free((xx_lmdb_data *)p); }
static Abstractformat *mk_gdbm_dump(xx_io_device *d, int64_t b) {
    xx_gdbm_dump *r = xx_gdbm_dump_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gdbm_dump(void *p) { xx_gdbm_dump_free((xx_gdbm_dump *)p); }
static Abstractformat *mk_adobe_acb(xx_io_device *d, int64_t b) {
    xx_adobe_acb *r = xx_adobe_acb_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adobe_acb(void *p) { xx_adobe_acb_free((xx_adobe_acb *)p); }
static Abstractformat *mk_jasc_palette(xx_io_device *d, int64_t b) {
    xx_jasc_palette *r = xx_jasc_palette_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jasc_palette(void *p) { xx_jasc_palette_free((xx_jasc_palette *)p); }
static Abstractformat *mk_x11_xbm(xx_io_device *d, int64_t b) {
    xx_x11_xbm *r = xx_x11_xbm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_x11_xbm(void *p) { xx_x11_xbm_free((xx_x11_xbm *)p); }
static Abstractformat *mk_jpeg2000_pgx(xx_io_device *d, int64_t b) {
    xx_jpeg2000_pgx *r = xx_jpeg2000_pgx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jpeg2000_pgx(void *p) { xx_jpeg2000_pgx_free((xx_jpeg2000_pgx *)p); }
static Abstractformat *mk_amiga_diskobject(xx_io_device *d, int64_t b) {
    xx_amiga_diskobject *r = xx_amiga_diskobject_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amiga_diskobject(void *p) { xx_amiga_diskobject_free((xx_amiga_diskobject *)p); }
static Abstractformat *mk_tex_vf(xx_io_device *d, int64_t b) {
    xx_tex_vf *r = xx_tex_vf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tex_vf(void *p) { xx_tex_vf_free((xx_tex_vf *)p); }
static Abstractformat *mk_esri_ascii_grid(xx_io_device *d, int64_t b) {
    xx_esri_ascii_grid *r = xx_esri_ascii_grid_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_esri_ascii_grid(void *p) { xx_esri_ascii_grid_free((xx_esri_ascii_grid *)p); }
static Abstractformat *mk_surfer_grid(xx_io_device *d, int64_t b) {
    xx_surfer_grid *r = xx_surfer_grid_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_surfer_grid(void *p) { xx_surfer_grid_free((xx_surfer_grid *)p); }
static Abstractformat *mk_gxf_grid(xx_io_device *d, int64_t b) {
    xx_gxf_grid *r = xx_gxf_grid_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gxf_grid(void *p) { xx_gxf_grid_free((xx_gxf_grid *)p); }
static Abstractformat *mk_ogc_wkt(xx_io_device *d, int64_t b) {
    xx_ogc_wkt *r = xx_ogc_wkt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ogc_wkt(void *p) { xx_ogc_wkt_free((xx_ogc_wkt *)p); }
static Abstractformat *mk_step_part21(xx_io_device *d, int64_t b) {
    xx_step_part21 *r = xx_step_part21_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_step_part21(void *p) { xx_step_part21_free((xx_step_part21 *)p); }
static Abstractformat *mk_gerber_rs274x(xx_io_device *d, int64_t b) {
    xx_gerber_rs274x *r = xx_gerber_rs274x_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gerber_rs274x(void *p) { xx_gerber_rs274x_free((xx_gerber_rs274x *)p); }
static Abstractformat *mk_excellon_drill(xx_io_device *d, int64_t b) {
    xx_excellon_drill *r = xx_excellon_drill_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_excellon_drill(void *p) { xx_excellon_drill_free((xx_excellon_drill *)p); }
static Abstractformat *mk_vrml_scene(xx_io_device *d, int64_t b) {
    xx_vrml_scene *r = xx_vrml_scene_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vrml_scene(void *p) { xx_vrml_scene_free((xx_vrml_scene *)p); }
static Abstractformat *mk_renderman_rib(xx_io_device *d, int64_t b) {
    xx_renderman_rib *r = xx_renderman_rib_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_renderman_rib(void *p) { xx_renderman_rib_free((xx_renderman_rib *)p); }
static Abstractformat *mk_asylum_amf(xx_io_device *d, int64_t b) {
    xx_asylum_amf *r = xx_asylum_amf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_asylum_amf(void *p) { xx_asylum_amf_free((xx_asylum_amf *)p); }
static Abstractformat *mk_tracker_stx(xx_io_device *d, int64_t b) {
    xx_tracker_stx *r = xx_tracker_stx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_stx(void *p) { xx_tracker_stx_free((xx_tracker_stx *)p); }
static Abstractformat *mk_tracker_dtm(xx_io_device *d, int64_t b) {
    xx_tracker_dtm *r = xx_tracker_dtm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_dtm(void *p) { xx_tracker_dtm_free((xx_tracker_dtm *)p); }
static Abstractformat *mk_tracker_soundfx(xx_io_device *d, int64_t b) {
    xx_tracker_soundfx *r = xx_tracker_soundfx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_soundfx(void *p) { xx_tracker_soundfx_free((xx_tracker_soundfx *)p); }
static Abstractformat *mk_tracker_funk(xx_io_device *d, int64_t b) {
    xx_tracker_funk *r = xx_tracker_funk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_funk(void *p) { xx_tracker_funk_free((xx_tracker_funk *)p); }
static Abstractformat *mk_tracker_archimedes(xx_io_device *d, int64_t b) {
    xx_tracker_archimedes *r = xx_tracker_archimedes_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_archimedes(void *p) { xx_tracker_archimedes_free((xx_tracker_archimedes *)p); }
static Abstractformat *mk_pce_psi(xx_io_device *d, int64_t b) {
    xx_pce_psi *r = xx_pce_psi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pce_psi(void *p) { xx_pce_psi_free((xx_pce_psi *)p); }
static Abstractformat *mk_pc98_d88(xx_io_device *d, int64_t b) {
    xx_pc98_d88 *r = xx_pc98_d88_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pc98_d88(void *p) { xx_pc98_d88_free((xx_pc98_d88 *)p); }
static Abstractformat *mk_hxc_mfm(xx_io_device *d, int64_t b) {
    xx_hxc_mfm *r = xx_hxc_mfm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hxc_mfm(void *p) { xx_hxc_mfm_free((xx_hxc_mfm *)p); }
static Abstractformat *mk_yaze_ydsk(xx_io_device *d, int64_t b) {
    xx_yaze_ydsk *r = xx_yaze_ydsk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_yaze_ydsk(void *p) { xx_yaze_ydsk_free((xx_yaze_ydsk *)p); }
static Abstractformat *mk_lammps_data(xx_io_device *d, int64_t b) {
    xx_lammps_data *r = xx_lammps_data_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lammps_data(void *p) { xx_lammps_data_free((xx_lammps_data *)p); }
static Abstractformat *mk_lammps_dump(xx_io_device *d, int64_t b) {
    xx_lammps_dump *r = xx_lammps_dump_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lammps_dump(void *p) { xx_lammps_dump_free((xx_lammps_dump *)p); }
static Abstractformat *mk_shelx_res(xx_io_device *d, int64_t b) {
    xx_shelx_res *r = xx_shelx_res_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_shelx_res(void *p) { xx_shelx_res_free((xx_shelx_res *)p); }
static Abstractformat *mk_turbomole_coord(xx_io_device *d, int64_t b) {
    xx_turbomole_coord *r = xx_turbomole_coord_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_turbomole_coord(void *p) { xx_turbomole_coord_free((xx_turbomole_coord *)p); }
static Abstractformat *mk_charmm_crd(xx_io_device *d, int64_t b) {
    xx_charmm_crd *r = xx_charmm_crd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_charmm_crd(void *p) { xx_charmm_crd_free((xx_charmm_crd *)p); }
static Abstractformat *mk_castep_cell(xx_io_device *d, int64_t b) {
    xx_castep_cell *r = xx_castep_cell_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_castep_cell(void *p) { xx_castep_cell_free((xx_castep_cell *)p); }
static Abstractformat *mk_crystal_fort34(xx_io_device *d, int64_t b) {
    xx_crystal_fort34 *r = xx_crystal_fort34_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_crystal_fort34(void *p) { xx_crystal_fort34_free((xx_crystal_fort34 *)p); }
static Abstractformat *mk_siesta_xv(xx_io_device *d, int64_t b) {
    xx_siesta_xv *r = xx_siesta_xv_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_siesta_xv(void *p) { xx_siesta_xv_free((xx_siesta_xv *)p); }
static Abstractformat *mk_harwell_boeing(xx_io_device *d, int64_t b) {
    xx_harwell_boeing *r = xx_harwell_boeing_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_harwell_boeing(void *p) { xx_harwell_boeing_free((xx_harwell_boeing *)p); }
static Abstractformat *mk_openfoam_points(xx_io_device *d, int64_t b) {
    xx_openfoam_points *r = xx_openfoam_points_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_openfoam_points(void *p) { xx_openfoam_points_free((xx_openfoam_points *)p); }
static Abstractformat *mk_ntp_message(xx_io_device *d, int64_t b) {
    xx_ntp_message *r = xx_ntp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ntp_message(void *p) { xx_ntp_message_free((xx_ntp_message *)p); }
static Abstractformat *mk_rtp_rtcp(xx_io_device *d, int64_t b) {
    xx_rtp_rtcp *r = xx_rtp_rtcp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rtp_rtcp(void *p) { xx_rtp_rtcp_free((xx_rtp_rtcp *)p); }
static Abstractformat *mk_bgp_messages(xx_io_device *d, int64_t b) {
    xx_bgp_messages *r = xx_bgp_messages_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bgp_messages(void *p) { xx_bgp_messages_free((xx_bgp_messages *)p); }
static Abstractformat *mk_ospf_packet(xx_io_device *d, int64_t b) {
    xx_ospf_packet *r = xx_ospf_packet_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ospf_packet(void *p) { xx_ospf_packet_free((xx_ospf_packet *)p); }
static Abstractformat *mk_sctp_packet(xx_io_device *d, int64_t b) {
    xx_sctp_packet *r = xx_sctp_packet_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sctp_packet(void *p) { xx_sctp_packet_free((xx_sctp_packet *)p); }
static Abstractformat *mk_isakmp_message(xx_io_device *d, int64_t b) {
    xx_isakmp_message *r = xx_isakmp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_isakmp_message(void *p) { xx_isakmp_message_free((xx_isakmp_message *)p); }
static Abstractformat *mk_ssh_transport(xx_io_device *d, int64_t b) {
    xx_ssh_transport *r = xx_ssh_transport_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ssh_transport(void *p) { xx_ssh_transport_free((xx_ssh_transport *)p); }
static Abstractformat *mk_smtp_transcript(xx_io_device *d, int64_t b) {
    xx_smtp_transcript *r = xx_smtp_transcript_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_smtp_transcript(void *p) { xx_smtp_transcript_free((xx_smtp_transcript *)p); }
static Abstractformat *mk_pkcs8_private_key(xx_io_device *d, int64_t b) {
    xx_pkcs8_private_key *r = xx_pkcs8_private_key_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pkcs8_private_key(void *p) { xx_pkcs8_private_key_free((xx_pkcs8_private_key *)p); }
static Abstractformat *mk_putty_ppk(xx_io_device *d, int64_t b) {
    xx_putty_ppk *r = xx_putty_ppk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_putty_ppk(void *p) { xx_putty_ppk_free((xx_putty_ppk *)p); }
static Abstractformat *mk_openssh_certificate(xx_io_device *d, int64_t b) {
    xx_openssh_certificate *r = xx_openssh_certificate_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_openssh_certificate(void *p) { xx_openssh_certificate_free((xx_openssh_certificate *)p); }
static Abstractformat *mk_safetensors(xx_io_device *d, int64_t b) {
    xx_safetensors *r = xx_safetensors_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_safetensors(void *p) { xx_safetensors_free((xx_safetensors *)p); }
static Abstractformat *mk_gguf(xx_io_device *d, int64_t b) {
    xx_gguf *r = xx_gguf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gguf(void *p) { xx_gguf_free((xx_gguf *)p); }
static Abstractformat *mk_cdb_database(xx_io_device *d, int64_t b) {
    xx_cdb_database *r = xx_cdb_database_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cdb_database(void *p) { xx_cdb_database_free((xx_cdb_database *)p); }
static Abstractformat *mk_stomp_frames(xx_io_device *d, int64_t b) {
    xx_stomp_frames *r = xx_stomp_frames_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stomp_frames(void *p) { xx_stomp_frames_free((xx_stomp_frames *)p); }
static Abstractformat *mk_fontforge_sfd(xx_io_device *d, int64_t b) {
    xx_fontforge_sfd *r = xx_fontforge_sfd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fontforge_sfd(void *p) { xx_fontforge_sfd_free((xx_fontforge_sfd *)p); }
static Abstractformat *mk_grub_pff2(xx_io_device *d, int64_t b) {
    xx_grub_pff2 *r = xx_grub_pff2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_grub_pff2(void *p) { xx_grub_pff2_free((xx_grub_pff2 *)p); }
static Abstractformat *mk_opengex_model(xx_io_device *d, int64_t b) {
    xx_opengex_model *r = xx_opengex_model_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_opengex_model(void *p) { xx_opengex_model_free((xx_opengex_model *)p); }
static Abstractformat *mk_bvh_motion(xx_io_device *d, int64_t b) {
    xx_bvh_motion *r = xx_bvh_motion_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bvh_motion(void *p) { xx_bvh_motion_free((xx_bvh_motion *)p); }
static Abstractformat *mk_directx_x(xx_io_device *d, int64_t b) {
    xx_directx_x *r = xx_directx_x_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_directx_x(void *p) { xx_directx_x_free((xx_directx_x *)p); }
static Abstractformat *mk_gts_surface(xx_io_device *d, int64_t b) {
    xx_gts_surface *r = xx_gts_surface_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gts_surface(void *p) { xx_gts_surface_free((xx_gts_surface *)p); }
static Abstractformat *mk_medit_mesh(xx_io_device *d, int64_t b) {
    xx_medit_mesh *r = xx_medit_mesh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_medit_mesh(void *p) { xx_medit_mesh_free((xx_medit_mesh *)p); }
static Abstractformat *mk_gocad_model(xx_io_device *d, int64_t b) {
    xx_gocad_model *r = xx_gocad_model_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gocad_model(void *p) { xx_gocad_model_free((xx_gocad_model *)p); }
static Abstractformat *mk_nastran_bulk(xx_io_device *d, int64_t b) {
    xx_nastran_bulk *r = xx_nastran_bulk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nastran_bulk(void *p) { xx_nastran_bulk_free((xx_nastran_bulk *)p); }
static Abstractformat *mk_abaqus_input(xx_io_device *d, int64_t b) {
    xx_abaqus_input *r = xx_abaqus_input_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_abaqus_input(void *p) { xx_abaqus_input_free((xx_abaqus_input *)p); }
static Abstractformat *mk_ensight_gold_geometry(xx_io_device *d, int64_t b) {
    xx_ensight_gold_geometry *r = xx_ensight_gold_geometry_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ensight_gold_geometry(void *p) { xx_ensight_gold_geometry_free((xx_ensight_gold_geometry *)p); }
static Abstractformat *mk_gmv_mesh(xx_io_device *d, int64_t b) {
    xx_gmv_mesh *r = xx_gmv_mesh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gmv_mesh(void *p) { xx_gmv_mesh_free((xx_gmv_mesh *)p); }
static Abstractformat *mk_usgs_dem(xx_io_device *d, int64_t b) {
    xx_usgs_dem *r = xx_usgs_dem_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_usgs_dem(void *p) { xx_usgs_dem_free((xx_usgs_dem *)p); }
static Abstractformat *mk_dted_elevation(xx_io_device *d, int64_t b) {
    xx_dted_elevation *r = xx_dted_elevation_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dted_elevation(void *p) { xx_dted_elevation_free((xx_dted_elevation *)p); }
static Abstractformat *mk_mapinfo_mif(xx_io_device *d, int64_t b) {
    xx_mapinfo_mif *r = xx_mapinfo_mif_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mapinfo_mif(void *p) { xx_mapinfo_mif_free((xx_mapinfo_mif *)p); }
static Abstractformat *mk_tracker_coconizer(xx_io_device *d, int64_t b) {
    xx_tracker_coconizer *r = xx_tracker_coconizer_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_coconizer(void *p) { xx_tracker_coconizer_free((xx_tracker_coconizer *)p); }
static Abstractformat *mk_tracker_real(xx_io_device *d, int64_t b) {
    xx_tracker_real *r = xx_tracker_real_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_real(void *p) { xx_tracker_real_free((xx_tracker_real *)p); }
static Abstractformat *mk_tracker_megatracker(xx_io_device *d, int64_t b) {
    xx_tracker_megatracker *r = xx_tracker_megatracker_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tracker_megatracker(void *p) { xx_tracker_megatracker_free((xx_tracker_megatracker *)p); }
static Abstractformat *mk_amos_music_bank(xx_io_device *d, int64_t b) {
    xx_amos_music_bank *r = xx_amos_music_bank_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amos_music_bank(void *p) { xx_amos_music_bank_free((xx_amos_music_bank *)p); }
static Abstractformat *mk_adlib_rad(xx_io_device *d, int64_t b) {
    xx_adlib_rad *r = xx_adlib_rad_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_rad(void *p) { xx_adlib_rad_free((xx_adlib_rad *)p); }
static Abstractformat *mk_adlib_amd(xx_io_device *d, int64_t b) {
    xx_adlib_amd *r = xx_adlib_amd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_amd(void *p) { xx_adlib_amd_free((xx_adlib_amd *)p); }
static Abstractformat *mk_adlib_hsc(xx_io_device *d, int64_t b) {
    xx_adlib_hsc *r = xx_adlib_hsc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_hsc(void *p) { xx_adlib_hsc_free((xx_adlib_hsc *)p); }
static Abstractformat *mk_adlib_d00(xx_io_device *d, int64_t b) {
    xx_adlib_d00 *r = xx_adlib_d00_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_d00(void *p) { xx_adlib_d00_free((xx_adlib_d00 *)p); }
static Abstractformat *mk_adlib_bnk(xx_io_device *d, int64_t b) {
    xx_adlib_bnk *r = xx_adlib_bnk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_bnk(void *p) { xx_adlib_bnk_free((xx_adlib_bnk *)p); }
static Abstractformat *mk_dosbox_dro(xx_io_device *d, int64_t b) {
    xx_dosbox_dro *r = xx_dosbox_dro_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dosbox_dro(void *p) { xx_dosbox_dro_free((xx_dosbox_dro *)p); }
static Abstractformat *mk_genomics_genbank(xx_io_device *d, int64_t b) {
    xx_genomics_genbank *r = xx_genomics_genbank_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_genbank(void *p) { xx_genomics_genbank_free((xx_genomics_genbank *)p); }
static Abstractformat *mk_genomics_embl(xx_io_device *d, int64_t b) {
    xx_genomics_embl *r = xx_genomics_embl_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_embl(void *p) { xx_genomics_embl_free((xx_genomics_embl *)p); }
static Abstractformat *mk_genomics_swissprot(xx_io_device *d, int64_t b) {
    xx_genomics_swissprot *r = xx_genomics_swissprot_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_swissprot(void *p) { xx_genomics_swissprot_free((xx_genomics_swissprot *)p); }
static Abstractformat *mk_alignment_clustal(xx_io_device *d, int64_t b) {
    xx_alignment_clustal *r = xx_alignment_clustal_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_alignment_clustal(void *p) { xx_alignment_clustal_free((xx_alignment_clustal *)p); }
static Abstractformat *mk_alignment_stockholm(xx_io_device *d, int64_t b) {
    xx_alignment_stockholm *r = xx_alignment_stockholm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_alignment_stockholm(void *p) { xx_alignment_stockholm_free((xx_alignment_stockholm *)p); }
static Abstractformat *mk_alignment_phylip(xx_io_device *d, int64_t b) {
    xx_alignment_phylip *r = xx_alignment_phylip_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_alignment_phylip(void *p) { xx_alignment_phylip_free((xx_alignment_phylip *)p); }
static Abstractformat *mk_alignment_maf(xx_io_device *d, int64_t b) {
    xx_alignment_maf *r = xx_alignment_maf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_alignment_maf(void *p) { xx_alignment_maf_free((xx_alignment_maf *)p); }
static Abstractformat *mk_alignment_mauve(xx_io_device *d, int64_t b) {
    xx_alignment_mauve *r = xx_alignment_mauve_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_alignment_mauve(void *p) { xx_alignment_mauve_free((xx_alignment_mauve *)p); }
static Abstractformat *mk_ucsc_nib(xx_io_device *d, int64_t b) {
    xx_ucsc_nib *r = xx_ucsc_nib_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ucsc_nib(void *p) { xx_ucsc_nib_free((xx_ucsc_nib *)p); }
static Abstractformat *mk_assembly_gfa(xx_io_device *d, int64_t b) {
    xx_assembly_gfa *r = xx_assembly_gfa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_assembly_gfa(void *p) { xx_assembly_gfa_free((xx_assembly_gfa *)p); }
static Abstractformat *mk_ethernet_frame(xx_io_device *d, int64_t b) {
    xx_ethernet_frame *r = xx_ethernet_frame_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ethernet_frame(void *p) { xx_ethernet_frame_free((xx_ethernet_frame *)p); }
static Abstractformat *mk_ip_packet(xx_io_device *d, int64_t b) {
    xx_ip_packet *r = xx_ip_packet_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ip_packet(void *p) { xx_ip_packet_free((xx_ip_packet *)p); }
static Abstractformat *mk_arp_packet(xx_io_device *d, int64_t b) {
    xx_arp_packet *r = xx_arp_packet_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_arp_packet(void *p) { xx_arp_packet_free((xx_arp_packet *)p); }
static Abstractformat *mk_icmp_message(xx_io_device *d, int64_t b) {
    xx_icmp_message *r = xx_icmp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_icmp_message(void *p) { xx_icmp_message_free((xx_icmp_message *)p); }
static Abstractformat *mk_sip_message(xx_io_device *d, int64_t b) {
    xx_sip_message *r = xx_sip_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sip_message(void *p) { xx_sip_message_free((xx_sip_message *)p); }
static Abstractformat *mk_rtsp_message(xx_io_device *d, int64_t b) {
    xx_rtsp_message *r = xx_rtsp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rtsp_message(void *p) { xx_rtsp_message_free((xx_rtsp_message *)p); }
static Abstractformat *mk_diameter_message(xx_io_device *d, int64_t b) {
    xx_diameter_message *r = xx_diameter_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_diameter_message(void *p) { xx_diameter_message_free((xx_diameter_message *)p); }
static Abstractformat *mk_tacacs_packet(xx_io_device *d, int64_t b) {
    xx_tacacs_packet *r = xx_tacacs_packet_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tacacs_packet(void *p) { xx_tacacs_packet_free((xx_tacacs_packet *)p); }
static Abstractformat *mk_gtp_message(xx_io_device *d, int64_t b) {
    xx_gtp_message *r = xx_gtp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gtp_message(void *p) { xx_gtp_message_free((xx_gtp_message *)p); }
static Abstractformat *mk_pfcp_message(xx_io_device *d, int64_t b) {
    xx_pfcp_message *r = xx_pfcp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pfcp_message(void *p) { xx_pfcp_message_free((xx_pfcp_message *)p); }
static Abstractformat *mk_pptp_message(xx_io_device *d, int64_t b) {
    xx_pptp_message *r = xx_pptp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pptp_message(void *p) { xx_pptp_message_free((xx_pptp_message *)p); }
static Abstractformat *mk_rsvp_message(xx_io_device *d, int64_t b) {
    xx_rsvp_message *r = xx_rsvp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rsvp_message(void *p) { xx_rsvp_message_free((xx_rsvp_message *)p); }
static Abstractformat *mk_age_encrypted(xx_io_device *d, int64_t b) {
    xx_age_encrypted *r = xx_age_encrypted_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_age_encrypted(void *p) { xx_age_encrypted_free((xx_age_encrypted *)p); }
static Abstractformat *mk_kerberos_ccache(xx_io_device *d, int64_t b) {
    xx_kerberos_ccache *r = xx_kerberos_ccache_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_kerberos_ccache(void *p) { xx_kerberos_ccache_free((xx_kerberos_ccache *)p); }
static Abstractformat *mk_jose_jws(xx_io_device *d, int64_t b) {
    xx_jose_jws *r = xx_jose_jws_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jose_jws(void *p) { xx_jose_jws_free((xx_jose_jws *)p); }
static Abstractformat *mk_wbmp_image(xx_io_device *d, int64_t b) {
    xx_wbmp_image *r = xx_wbmp_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wbmp_image(void *p) { xx_wbmp_image_free((xx_wbmp_image *)p); }
static Abstractformat *mk_dec_sixel(xx_io_device *d, int64_t b) {
    xx_dec_sixel *r = xx_dec_sixel_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dec_sixel(void *p) { xx_dec_sixel_free((xx_dec_sixel *)p); }
static Abstractformat *mk_palm_bitmap(xx_io_device *d, int64_t b) {
    xx_palm_bitmap *r = xx_palm_bitmap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_palm_bitmap(void *p) { xx_palm_bitmap_free((xx_palm_bitmap *)p); }
static Abstractformat *mk_adobe_acv(xx_io_device *d, int64_t b) {
    xx_adobe_acv *r = xx_adobe_acv_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adobe_acv(void *p) { xx_adobe_acv_free((xx_adobe_acv *)p); }
static Abstractformat *mk_adobe_act(xx_io_device *d, int64_t b) {
    xx_adobe_act *r = xx_adobe_act_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adobe_act(void *p) { xx_adobe_act_free((xx_adobe_act *)p); }
static Abstractformat *mk_ogre_skeleton(xx_io_device *d, int64_t b) {
    xx_ogre_skeleton *r = xx_ogre_skeleton_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ogre_skeleton(void *p) { xx_ogre_skeleton_free((xx_ogre_skeleton *)p); }
static Abstractformat *mk_cal3d_skeleton(xx_io_device *d, int64_t b) {
    xx_cal3d_skeleton *r = xx_cal3d_skeleton_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cal3d_skeleton(void *p) { xx_cal3d_skeleton_free((xx_cal3d_skeleton *)p); }
static Abstractformat *mk_collada_dae(xx_io_device *d, int64_t b) {
    xx_collada_dae *r = xx_collada_dae_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_collada_dae(void *p) { xx_collada_dae_free((xx_collada_dae *)p); }
static Abstractformat *mk_lightwave_scene(xx_io_device *d, int64_t b) {
    xx_lightwave_scene *r = xx_lightwave_scene_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lightwave_scene(void *p) { xx_lightwave_scene_free((xx_lightwave_scene *)p); }
static Abstractformat *mk_dsn6_density(xx_io_device *d, int64_t b) {
    xx_dsn6_density *r = xx_dsn6_density_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dsn6_density(void *p) { xx_dsn6_density_free((xx_dsn6_density *)p); }
static Abstractformat *mk_crystallography_mtz(xx_io_device *d, int64_t b) {
    xx_crystallography_mtz *r = xx_crystallography_mtz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_crystallography_mtz(void *p) { xx_crystallography_mtz_free((xx_crystallography_mtz *)p); }
static Abstractformat *mk_amira_mesh(xx_io_device *d, int64_t b) {
    xx_amira_mesh *r = xx_amira_mesh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_amira_mesh(void *p) { xx_amira_mesh_free((xx_amira_mesh *)p); }
static Abstractformat *mk_tetgen_mesh(xx_io_device *d, int64_t b) {
    xx_tetgen_mesh *r = xx_tetgen_mesh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_tetgen_mesh(void *p) { xx_tetgen_mesh_free((xx_tetgen_mesh *)p); }
static Abstractformat *mk_jedec_fuse(xx_io_device *d, int64_t b) {
    xx_jedec_fuse *r = xx_jedec_fuse_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jedec_fuse(void *p) { xx_jedec_fuse_free((xx_jedec_fuse *)p); }
static Abstractformat *mk_qchem_input(xx_io_device *d, int64_t b) {
    xx_qchem_input *r = xx_qchem_input_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_qchem_input(void *p) { xx_qchem_input_free((xx_qchem_input *)p); }
static Abstractformat *mk_adlib_bam(xx_io_device *d, int64_t b) {
    xx_adlib_bam *r = xx_adlib_bam_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_bam(void *p) { xx_adlib_bam_free((xx_adlib_bam *)p); }
static Abstractformat *mk_adlib_bmf(xx_io_device *d, int64_t b) {
    xx_adlib_bmf *r = xx_adlib_bmf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_bmf(void *p) { xx_adlib_bmf_free((xx_adlib_bmf *)p); }
static Abstractformat *mk_creative_cmf(xx_io_device *d, int64_t b) {
    xx_creative_cmf *r = xx_creative_cmf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_creative_cmf(void *p) { xx_creative_cmf_free((xx_creative_cmf *)p); }
static Abstractformat *mk_adlib_dfm(xx_io_device *d, int64_t b) {
    xx_adlib_dfm *r = xx_adlib_dfm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_dfm(void *p) { xx_adlib_dfm_free((xx_adlib_dfm *)p); }
static Abstractformat *mk_adlib_lds(xx_io_device *d, int64_t b) {
    xx_adlib_lds *r = xx_adlib_lds_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_lds(void *p) { xx_adlib_lds_free((xx_adlib_lds *)p); }
static Abstractformat *mk_adlib_mkj(xx_io_device *d, int64_t b) {
    xx_adlib_mkj *r = xx_adlib_mkj_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_mkj(void *p) { xx_adlib_mkj_free((xx_adlib_mkj *)p); }
static Abstractformat *mk_adlib_rol(xx_io_device *d, int64_t b) {
    xx_adlib_rol *r = xx_adlib_rol_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_rol(void *p) { xx_adlib_rol_free((xx_adlib_rol *)p); }
static Abstractformat *mk_adlib_sa2(xx_io_device *d, int64_t b) {
    xx_adlib_sa2 *r = xx_adlib_sa2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_sa2(void *p) { xx_adlib_sa2_free((xx_adlib_sa2 *)p); }
static Abstractformat *mk_faust_fmc(xx_io_device *d, int64_t b) {
    xx_faust_fmc *r = xx_faust_fmc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_faust_fmc(void *p) { xx_faust_fmc_free((xx_faust_fmc *)p); }
static Abstractformat *mk_softstar_rix(xx_io_device *d, int64_t b) {
    xx_softstar_rix *r = xx_softstar_rix_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_softstar_rix(void *p) { xx_softstar_rix_free((xx_softstar_rix *)p); }
static Abstractformat *mk_genomics_bed(xx_io_device *d, int64_t b) {
    xx_genomics_bed *r = xx_genomics_bed_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_bed(void *p) { xx_genomics_bed_free((xx_genomics_bed *)p); }
static Abstractformat *mk_genomics_wiggle(xx_io_device *d, int64_t b) {
    xx_genomics_wiggle *r = xx_genomics_wiggle_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_wiggle(void *p) { xx_genomics_wiggle_free((xx_genomics_wiggle *)p); }
static Abstractformat *mk_genomics_gtf(xx_io_device *d, int64_t b) {
    xx_genomics_gtf *r = xx_genomics_gtf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_gtf(void *p) { xx_genomics_gtf_free((xx_genomics_gtf *)p); }
static Abstractformat *mk_genomics_agp(xx_io_device *d, int64_t b) {
    xx_genomics_agp *r = xx_genomics_agp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_agp(void *p) { xx_genomics_agp_free((xx_genomics_agp *)p); }
static Abstractformat *mk_sequencing_abif(xx_io_device *d, int64_t b) {
    xx_sequencing_abif *r = xx_sequencing_abif_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sequencing_abif(void *p) { xx_sequencing_abif_free((xx_sequencing_abif *)p); }
static Abstractformat *mk_sequencing_scf(xx_io_device *d, int64_t b) {
    xx_sequencing_scf *r = xx_sequencing_scf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sequencing_scf(void *p) { xx_sequencing_scf_free((xx_sequencing_scf *)p); }
static Abstractformat *mk_genomics_sff(xx_io_device *d, int64_t b) {
    xx_genomics_sff *r = xx_genomics_sff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_genomics_sff(void *p) { xx_genomics_sff_free((xx_genomics_sff *)p); }
static Abstractformat *mk_lut_spi1d(xx_io_device *d, int64_t b) {
    xx_lut_spi1d *r = xx_lut_spi1d_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lut_spi1d(void *p) { xx_lut_spi1d_free((xx_lut_spi1d *)p); }
static Abstractformat *mk_lut_spi3d(xx_io_device *d, int64_t b) {
    xx_lut_spi3d *r = xx_lut_spi3d_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lut_spi3d(void *p) { xx_lut_spi3d_free((xx_lut_spi3d *)p); }
static Abstractformat *mk_lut_cinespace_csp(xx_io_device *d, int64_t b) {
    xx_lut_cinespace_csp *r = xx_lut_cinespace_csp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lut_cinespace_csp(void *p) { xx_lut_cinespace_csp_free((xx_lut_cinespace_csp *)p); }
static Abstractformat *mk_modbus_tcp(xx_io_device *d, int64_t b) {
    xx_modbus_tcp *r = xx_modbus_tcp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_modbus_tcp(void *p) { xx_modbus_tcp_free((xx_modbus_tcp *)p); }
static Abstractformat *mk_someip_message(xx_io_device *d, int64_t b) {
    xx_someip_message *r = xx_someip_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_someip_message(void *p) { xx_someip_message_free((xx_someip_message *)p); }
static Abstractformat *mk_dds_rtps(xx_io_device *d, int64_t b) {
    xx_dds_rtps *r = xx_dds_rtps_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dds_rtps(void *p) { xx_dds_rtps_free((xx_dds_rtps *)p); }
static Abstractformat *mk_rip_message(xx_io_device *d, int64_t b) {
    xx_rip_message *r = xx_rip_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rip_message(void *p) { xx_rip_message_free((xx_rip_message *)p); }
static Abstractformat *mk_vrrp_message(xx_io_device *d, int64_t b) {
    xx_vrrp_message *r = xx_vrrp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vrrp_message(void *p) { xx_vrrp_message_free((xx_vrrp_message *)p); }
static Abstractformat *mk_igmp_message(xx_io_device *d, int64_t b) {
    xx_igmp_message *r = xx_igmp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_igmp_message(void *p) { xx_igmp_message_free((xx_igmp_message *)p); }
static Abstractformat *mk_pim_message(xx_io_device *d, int64_t b) {
    xx_pim_message *r = xx_pim_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pim_message(void *p) { xx_pim_message_free((xx_pim_message *)p); }
static Abstractformat *mk_ldp_message(xx_io_device *d, int64_t b) {
    xx_ldp_message *r = xx_ldp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ldp_message(void *p) { xx_ldp_message_free((xx_ldp_message *)p); }
static Abstractformat *mk_gre_packet(xx_io_device *d, int64_t b) {
    xx_gre_packet *r = xx_gre_packet_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gre_packet(void *p) { xx_gre_packet_free((xx_gre_packet *)p); }
static Abstractformat *mk_l2tp_packet(xx_io_device *d, int64_t b) {
    xx_l2tp_packet *r = xx_l2tp_packet_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_l2tp_packet(void *p) { xx_l2tp_packet_free((xx_l2tp_packet *)p); }
static Abstractformat *mk_lldp_message(xx_io_device *d, int64_t b) {
    xx_lldp_message *r = xx_lldp_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lldp_message(void *p) { xx_lldp_message_free((xx_lldp_message *)p); }
static Abstractformat *mk_netflow_datagram(xx_io_device *d, int64_t b) {
    xx_netflow_datagram *r = xx_netflow_datagram_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_netflow_datagram(void *p) { xx_netflow_datagram_free((xx_netflow_datagram *)p); }
static Abstractformat *mk_ntlm_message(xx_io_device *d, int64_t b) {
    xx_ntlm_message *r = xx_ntlm_message_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ntlm_message(void *p) { xx_ntlm_message_free((xx_ntlm_message *)p); }
static Abstractformat *mk_dcerpc_pdu(xx_io_device *d, int64_t b) {
    xx_dcerpc_pdu *r = xx_dcerpc_pdu_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dcerpc_pdu(void *p) { xx_dcerpc_pdu_free((xx_dcerpc_pdu *)p); }
static Abstractformat *mk_ethereum_rlp(xx_io_device *d, int64_t b) {
    xx_ethereum_rlp *r = xx_ethereum_rlp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ethereum_rlp(void *p) { xx_ethereum_rlp_free((xx_ethereum_rlp *)p); }
static Abstractformat *mk_imagemagick_miff(xx_io_device *d, int64_t b) {
    xx_imagemagick_miff *r = xx_imagemagick_miff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_imagemagick_miff(void *p) { xx_imagemagick_miff_free((xx_imagemagick_miff *)p); }
static Abstractformat *mk_avs_image(xx_io_device *d, int64_t b) {
    xx_avs_image *r = xx_avs_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_avs_image(void *p) { xx_avs_image_free((xx_avs_image *)p); }
static Abstractformat *mk_scanalytics_iplab(xx_io_device *d, int64_t b) {
    xx_scanalytics_iplab *r = xx_scanalytics_iplab_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_scanalytics_iplab(void *p) { xx_scanalytics_iplab_free((xx_scanalytics_iplab *)p); }
static Abstractformat *mk_mtv_image(xx_io_device *d, int64_t b) {
    xx_mtv_image *r = xx_mtv_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mtv_image(void *p) { xx_mtv_image_free((xx_mtv_image *)p); }
static Abstractformat *mk_nokia_ota_bitmap(xx_io_device *d, int64_t b) {
    xx_nokia_ota_bitmap *r = xx_nokia_ota_bitmap_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nokia_ota_bitmap(void *p) { xx_nokia_ota_bitmap_free((xx_nokia_ota_bitmap *)p); }
static Abstractformat *mk_apple_pict(xx_io_device *d, int64_t b) {
    xx_apple_pict *r = xx_apple_pict_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apple_pict(void *p) { xx_apple_pict_free((xx_apple_pict *)p); }
static Abstractformat *mk_wordperfect_wpg(xx_io_device *d, int64_t b) {
    xx_wordperfect_wpg *r = xx_wordperfect_wpg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_wordperfect_wpg(void *p) { xx_wordperfect_wpg_free((xx_wordperfect_wpg *)p); }
static Abstractformat *mk_nasa_vicar(xx_io_device *d, int64_t b) {
    xx_nasa_vicar *r = xx_nasa_vicar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nasa_vicar(void *p) { xx_nasa_vicar_free((xx_nasa_vicar *)p); }
static Abstractformat *mk_khoros_viff(xx_io_device *d, int64_t b) {
    xx_khoros_viff *r = xx_khoros_viff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_khoros_viff(void *p) { xx_khoros_viff_free((xx_khoros_viff *)p); }
static Abstractformat *mk_imagemagick_mvg(xx_io_device *d, int64_t b) {
    xx_imagemagick_mvg *r = xx_imagemagick_mvg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_imagemagick_mvg(void *p) { xx_imagemagick_mvg_free((xx_imagemagick_mvg *)p); }
static Abstractformat *mk_motif_uil(xx_io_device *d, int64_t b) {
    xx_motif_uil *r = xx_motif_uil_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_motif_uil(void *p) { xx_motif_uil_free((xx_motif_uil *)p); }
static Abstractformat *mk_iges_model(xx_io_device *d, int64_t b) {
    xx_iges_model *r = xx_iges_model_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_iges_model(void *p) { xx_iges_model_free((xx_iges_model *)p); }
static Abstractformat *mk_openusd_usda(xx_io_device *d, int64_t b) {
    xx_openusd_usda *r = xx_openusd_usda_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_openusd_usda(void *p) { xx_openusd_usda_free((xx_openusd_usda *)p); }
static Abstractformat *mk_ufo_glif(xx_io_device *d, int64_t b) {
    xx_ufo_glif *r = xx_ufo_glif_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ufo_glif(void *p) { xx_ufo_glif_free((xx_ufo_glif *)p); }
static Abstractformat *mk_unifont_hex(xx_io_device *d, int64_t b) {
    xx_unifont_hex *r = xx_unifont_hex_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_unifont_hex(void *p) { xx_unifont_hex_free((xx_unifont_hex *)p); }
static Abstractformat *mk_adlib_sop(xx_io_device *d, int64_t b) {
    xx_adlib_sop *r = xx_adlib_sop_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_sop(void *p) { xx_adlib_sop_free((xx_adlib_sop *)p); }
static Abstractformat *mk_cudfm_cff(xx_io_device *d, int64_t b) {
    xx_cudfm_cff *r = xx_cudfm_cff_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cudfm_cff(void *p) { xx_cudfm_cff_free((xx_cudfm_cff *)p); }
static Abstractformat *mk_adlib_jbm(xx_io_device *d, int64_t b) {
    xx_adlib_jbm *r = xx_adlib_jbm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_jbm(void *p) { xx_adlib_jbm_free((xx_adlib_jbm *)p); }
static Abstractformat *mk_ceres_msc(xx_io_device *d, int64_t b) {
    xx_ceres_msc *r = xx_ceres_msc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ceres_msc(void *p) { xx_ceres_msc_free((xx_ceres_msc *)p); }
static Abstractformat *mk_adlib_xsm(xx_io_device *d, int64_t b) {
    xx_adlib_xsm *r = xx_adlib_xsm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_xsm(void *p) { xx_adlib_xsm_free((xx_adlib_xsm *)p); }
static Abstractformat *mk_ken_ksm(xx_io_device *d, int64_t b) {
    xx_ken_ksm *r = xx_ken_ksm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ken_ksm(void *p) { xx_ken_ksm_free((xx_ken_ksm *)p); }
static Abstractformat *mk_implay_music(xx_io_device *d, int64_t b) {
    xx_implay_music *r = xx_implay_music_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_implay_music(void *p) { xx_implay_music_free((xx_implay_music *)p); }
static Abstractformat *mk_adlib_mtr(xx_io_device *d, int64_t b) {
    xx_adlib_mtr *r = xx_adlib_mtr_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adlib_mtr(void *p) { xx_adlib_mtr_free((xx_adlib_mtr *)p); }
static Abstractformat *mk_rdos_raw(xx_io_device *d, int64_t b) {
    xx_rdos_raw *r = xx_rdos_raw_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rdos_raw(void *p) { xx_rdos_raw_free((xx_rdos_raw *)p); }
static Abstractformat *mk_mad_tracker(xx_io_device *d, int64_t b) {
    xx_mad_tracker *r = xx_mad_tracker_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mad_tracker(void *p) { xx_mad_tracker_free((xx_mad_tracker *)p); }
static Abstractformat *mk_vasp_poscar(xx_io_device *d, int64_t b) {
    xx_vasp_poscar *r = xx_vasp_poscar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vasp_poscar(void *p) { xx_vasp_poscar_free((xx_vasp_poscar *)p); }
static Abstractformat *mk_quantum_espresso_input(xx_io_device *d, int64_t b) {
    xx_quantum_espresso_input *r = xx_quantum_espresso_input_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_quantum_espresso_input(void *p) { xx_quantum_espresso_input_free((xx_quantum_espresso_input *)p); }
static Abstractformat *mk_cp2k_input(xx_io_device *d, int64_t b) {
    xx_cp2k_input *r = xx_cp2k_input_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cp2k_input(void *p) { xx_cp2k_input_free((xx_cp2k_input *)p); }
static Abstractformat *mk_nwchem_input(xx_io_device *d, int64_t b) {
    xx_nwchem_input *r = xx_nwchem_input_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nwchem_input(void *p) { xx_nwchem_input_free((xx_nwchem_input *)p); }
static Abstractformat *mk_gamess_input(xx_io_device *d, int64_t b) {
    xx_gamess_input *r = xx_gamess_input_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gamess_input(void *p) { xx_gamess_input_free((xx_gamess_input *)p); }
static Abstractformat *mk_gaussian_input(xx_io_device *d, int64_t b) {
    xx_gaussian_input *r = xx_gaussian_input_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gaussian_input(void *p) { xx_gaussian_input_free((xx_gaussian_input *)p); }
static Abstractformat *mk_abinit_input(xx_io_device *d, int64_t b) {
    xx_abinit_input *r = xx_abinit_input_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_abinit_input(void *p) { xx_abinit_input_free((xx_abinit_input *)p); }
static Abstractformat *mk_aims_geometry(xx_io_device *d, int64_t b) {
    xx_aims_geometry *r = xx_aims_geometry_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_aims_geometry(void *p) { xx_aims_geometry_free((xx_aims_geometry *)p); }
static Abstractformat *mk_orca_input(xx_io_device *d, int64_t b) {
    xx_orca_input *r = xx_orca_input_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_orca_input(void *p) { xx_orca_input_free((xx_orca_input *)p); }
static Abstractformat *mk_demon_input(xx_io_device *d, int64_t b) {
    xx_demon_input *r = xx_demon_input_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_demon_input(void *p) { xx_demon_input_free((xx_demon_input *)p); }
static Abstractformat *mk_sfxstart(xx_io_device *d, int64_t b) {
    xx_sfxstart *r = xx_sfxstart_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfxstart(void *p) { xx_sfxstart_free((xx_sfxstart *)p); }


/* Additional native readers; prefer complete XP3/RPA readers for their canonical IDs. */
static Abstractformat *mk_acorn_atom_disk(xx_io_device *d, int64_t b) {
    xx_acorn_atom_disk *r = xx_acorn_atom_disk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_acorn_atom_disk(void *p) { xx_acorn_atom_disk_free((xx_acorn_atom_disk *)p); }
static Abstractformat *mk_apollo_afd(xx_io_device *d, int64_t b) {
    xx_apollo_afd *r = xx_apollo_afd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apollo_afd(void *p) { xx_apollo_afd_free((xx_apollo_afd *)p); }
static Abstractformat *mk_bga(xx_io_device *d, int64_t b) {
    xx_bga *r = xx_bga_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bga(void *p) { xx_bga_free((xx_bga *)p); }
static Abstractformat *mk_bgi(xx_io_device *d, int64_t b) {
    xx_bgi *r = xx_bgi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bgi(void *p) { xx_bgi_free((xx_bgi *)p); }
static Abstractformat *mk_bgi2(xx_io_device *d, int64_t b) {
    xx_bgi2 *r = xx_bgi2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bgi2(void *p) { xx_bgi2_free((xx_bgi2 *)p); }
static Abstractformat *mk_binscii(xx_io_device *d, int64_t b) {
    xx_binscii *r = xx_binscii_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_binscii(void *p) { xx_binscii_free((xx_binscii *)p); }
static Abstractformat *mk_blindwrite_5_6_image(xx_io_device *d, int64_t b) {
    xx_blindwrite_5_6_image *r = xx_blindwrite_5_6_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_blindwrite_5_6_image(void *p) { xx_blindwrite_5_6_image_free((xx_blindwrite_5_6_image *)p); }
static Abstractformat *mk_btrfs_stream(xx_io_device *d, int64_t b) {
    xx_btrfs_stream *r = xx_btrfs_stream_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_btrfs_stream(void *p) { xx_btrfs_stream_free((xx_btrfs_stream *)p); }
static Abstractformat *mk_camputers_lynx_ldf(xx_io_device *d, int64_t b) {
    xx_camputers_lynx_ldf *r = xx_camputers_lynx_ldf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_camputers_lynx_ldf(void *p) { xx_camputers_lynx_ldf_free((xx_camputers_lynx_ldf *)p); }
static Abstractformat *mk_cpk(xx_io_device *d, int64_t b) {
    xx_cpk *r = xx_cpk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cpk(void *p) { xx_cpk_free((xx_cpk *)p); }
static Abstractformat *mk_crt(xx_io_device *d, int64_t b) {
    xx_crt *r = xx_crt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_crt(void *p) { xx_crt_free((xx_crt *)p); }
static Abstractformat *mk_d_link_alpha_encimg_v2(xx_io_device *d, int64_t b) {
    xx_d_link_alpha_encimg_v2 *r = xx_d_link_alpha_encimg_v2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_d_link_alpha_encimg_v2(void *p) { xx_d_link_alpha_encimg_v2_free((xx_d_link_alpha_encimg_v2 *)p); }
static Abstractformat *mk_d_link_fpkg_cpkg(xx_io_device *d, int64_t b) {
    xx_d_link_fpkg_cpkg *r = xx_d_link_fpkg_cpkg_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_d_link_fpkg_cpkg(void *p) { xx_d_link_fpkg_cpkg_free((xx_d_link_fpkg_cpkg *)p); }
static Abstractformat *mk_daemon_tools_mdx(xx_io_device *d, int64_t b) {
    xx_daemon_tools_mdx *r = xx_daemon_tools_mdx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_daemon_tools_mdx(void *p) { xx_daemon_tools_mdx_free((xx_daemon_tools_mdx *)p); }
static Abstractformat *mk_dart(xx_io_device *d, int64_t b) {
    xx_dart *r = xx_dart_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dart(void *p) { xx_dart_free((xx_dart *)p); }
static Abstractformat *mk_ddd(xx_io_device *d, int64_t b) {
    xx_ddd *r = xx_ddd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ddd(void *p) { xx_ddd_free((xx_ddd *)p); }
static Abstractformat *mk_diet_compression(xx_io_device *d, int64_t b) {
    xx_diet_compression *r = xx_diet_compression_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_diet_compression(void *p) { xx_diet_compression_free((xx_diet_compression *)p); }
static Abstractformat *mk_dxa(xx_io_device *d, int64_t b) {
    xx_dxa *r = xx_dxa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_dxa(void *p) { xx_dxa_free((xx_dxa *)p); }
static Abstractformat *mk_ea_fsh(xx_io_device *d, int64_t b) {
    xx_ea_fsh *r = xx_ea_fsh_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ea_fsh(void *p) { xx_ea_fsh_free((xx_ea_fsh *)p); }
static Abstractformat *mk_ewf2_ex01(xx_io_device *d, int64_t b) {
    xx_ewf2_ex01 *r = xx_ewf2_ex01_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ewf2_ex01(void *p) { xx_ewf2_ex01_free((xx_ewf2_ex01 *)p); }
static Abstractformat *mk_ewf2_lx01(xx_io_device *d, int64_t b) {
    xx_ewf2_lx01 *r = xx_ewf2_lx01_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ewf2_lx01(void *p) { xx_ewf2_lx01_free((xx_ewf2_lx01 *)p); }
static Abstractformat *mk_ewf_l01(xx_io_device *d, int64_t b) {
    xx_ewf_l01 *r = xx_ewf_l01_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ewf_l01(void *p) { xx_ewf_l01_free((xx_ewf_l01 *)p); }
static Abstractformat *mk_fmod_sample_bank(xx_io_device *d, int64_t b) {
    xx_fmod_sample_bank *r = xx_fmod_sample_bank_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fmod_sample_bank(void *p) { xx_fmod_sample_bank_free((xx_fmod_sample_bank *)p); }
static Abstractformat *mk_gbi(xx_io_device *d, int64_t b) {
    xx_gbi *r = xx_gbi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gbi(void *p) { xx_gbi_free((xx_gbi *)p); }
static Abstractformat *mk_goldsrc_bsp(xx_io_device *d, int64_t b) {
    xx_goldsrc_bsp *r = xx_goldsrc_bsp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_goldsrc_bsp(void *p) { xx_goldsrc_bsp_free((xx_goldsrc_bsp *)p); }
static Abstractformat *mk_hsf(xx_io_device *d, int64_t b) {
    xx_hsf *r = xx_hsf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hsf(void *p) { xx_hsf_free((xx_hsf *)p); }
static Abstractformat *mk_htc_nbh_rom_image(xx_io_device *d, int64_t b) {
    xx_htc_nbh_rom_image *r = xx_htc_nbh_rom_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_htc_nbh_rom_image(void *p) { xx_htc_nbh_rom_image_free((xx_htc_nbh_rom_image *)p); }
static Abstractformat *mk_hxc_hfe_extended(xx_io_device *d, int64_t b) {
    xx_hxc_hfe_extended *r = xx_hxc_hfe_extended_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hxc_hfe_extended(void *p) { xx_hxc_hfe_extended_free((xx_hxc_hfe_extended *)p); }
static Abstractformat *mk_hxc_hfe_hddd_a2_variant(xx_io_device *d, int64_t b) {
    xx_hxc_hfe_hddd_a2_variant *r = xx_hxc_hfe_hddd_a2_variant_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hxc_hfe_hddd_a2_variant(void *p) { xx_hxc_hfe_hddd_a2_variant_free((xx_hxc_hfe_hddd_a2_variant *)p); }
static Abstractformat *mk_hxc_hfe_v3(xx_io_device *d, int64_t b) {
    xx_hxc_hfe_v3 *r = xx_hxc_hfe_v3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hxc_hfe_v3(void *p) { xx_hxc_hfe_v3_free((xx_hxc_hfe_v3 *)p); }
static Abstractformat *mk_hxs(xx_io_device *d, int64_t b) {
    xx_hxs *r = xx_hxs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hxs(void *p) { xx_hxs_free((xx_hxs *)p); }
static Abstractformat *mk_jffs2_old(xx_io_device *d, int64_t b) {
    xx_jffs2_old *r = xx_jffs2_old_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jffs2_old(void *p) { xx_jffs2_old_free((xx_jffs2_old *)p); }
static Abstractformat *mk_jvc(xx_io_device *d, int64_t b) {
    xx_jvc *r = xx_jvc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_jvc(void *p) { xx_jvc_free((xx_jvc *)p); }
static Abstractformat *mk_kgb_archiver(xx_io_device *d, int64_t b) {
    xx_kgb_archiver *r = xx_kgb_archiver_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_kgb_archiver(void *p) { xx_kgb_archiver_free((xx_kgb_archiver *)p); }
static Abstractformat *mk_kryoflux_stream(xx_io_device *d, int64_t b) {
    xx_kryoflux_stream *r = xx_kryoflux_stream_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_kryoflux_stream(void *p) { xx_kryoflux_stream_free((xx_kryoflux_stream *)p); }
static Abstractformat *mk_livemaker(xx_io_device *d, int64_t b) {
    xx_livemaker *r = xx_livemaker_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_livemaker(void *p) { xx_livemaker_free((xx_livemaker *)p); }
static Abstractformat *mk_lzma86(xx_io_device *d, int64_t b) {
    xx_lzma86 *r = xx_lzma86_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lzma86(void *p) { xx_lzma86_free((xx_lzma86 *)p); }
static Abstractformat *mk_maxis_far_archive(xx_io_device *d, int64_t b) {
    xx_maxis_far_archive *r = xx_maxis_far_archive_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_maxis_far_archive(void *p) { xx_maxis_far_archive_free((xx_maxis_far_archive *)p); }
static Abstractformat *mk_mgt(xx_io_device *d, int64_t b) {
    xx_mgt *r = xx_mgt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mgt(void *p) { xx_mgt_free((xx_mgt *)p); }
static Abstractformat *mk_minix(xx_io_device *d, int64_t b) {
    xx_minix *r = xx_minix_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_minix(void *p) { xx_minix_free((xx_minix *)p); }
static Abstractformat *mk_moof(xx_io_device *d, int64_t b) {
    xx_moof *r = xx_moof_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_moof(void *p) { xx_moof_free((xx_moof *)p); }
static Abstractformat *mk_ms_dos_backup2(xx_io_device *d, int64_t b) {
    xx_ms_dos_backup2 *r = xx_ms_dos_backup2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ms_dos_backup2(void *p) { xx_ms_dos_backup2_free((xx_ms_dos_backup2 *)p); }
static Abstractformat *mk_mub(xx_io_device *d, int64_t b) {
    xx_mub *r = xx_mub_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mub(void *p) { xx_mub_free((xx_mub *)p); }
static Abstractformat *mk_noa(xx_io_device *d, int64_t b) {
    xx_noa *r = xx_noa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_noa(void *p) { xx_noa_free((xx_noa *)p); }
static Abstractformat *mk_outlook_express_dbx_mailbox(xx_io_device *d, int64_t b) {
    xx_outlook_express_dbx_mailbox *r = xx_outlook_express_dbx_mailbox_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_outlook_express_dbx_mailbox(void *p) { xx_outlook_express_dbx_mailbox_free((xx_outlook_express_dbx_mailbox *)p); }
static Abstractformat *mk_partclone_image(xx_io_device *d, int64_t b) {
    xx_partclone_image *r = xx_partclone_image_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_partclone_image(void *p) { xx_partclone_image_free((xx_partclone_image *)p); }
static Abstractformat *mk_ppmd(xx_io_device *d, int64_t b) {
    xx_ppmd *r = xx_ppmd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ppmd(void *p) { xx_ppmd_free((xx_ppmd *)p); }
static Abstractformat *mk_quoted_printable_encoded_fil(xx_io_device *d, int64_t b) {
    xx_quoted_printable_encoded_fil *r = xx_quoted_printable_encoded_fil_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_quoted_printable_encoded_fil(void *p) { xx_quoted_printable_encoded_fil_free((xx_quoted_printable_encoded_fil *)p); }
static Abstractformat *mk_risc_os_sprite(xx_io_device *d, int64_t b) {
    xx_risc_os_sprite *r = xx_risc_os_sprite_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_risc_os_sprite(void *p) { xx_risc_os_sprite_free((xx_risc_os_sprite *)p); }
static Abstractformat *mk_rpa(xx_io_device *d, int64_t b) {
    xx_rpa *r = xx_rpa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rpa(void *p) { xx_rpa_free((xx_rpa *)p); }
static Abstractformat *mk_rpg_maker_rgssad(xx_io_device *d, int64_t b) {
    xx_rpg_maker_rgssad *r = xx_rpg_maker_rgssad_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rpg_maker_rgssad(void *p) { xx_rpg_maker_rgssad_free((xx_rpg_maker_rgssad *)p); }
static Abstractformat *mk_sfark_compressed_soundfont(xx_io_device *d, int64_t b) {
    xx_sfark_compressed_soundfont *r = xx_sfark_compressed_soundfont_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sfark_compressed_soundfont(void *p) { xx_sfark_compressed_soundfont_free((xx_sfark_compressed_soundfont *)p); }
static Abstractformat *mk_sis(xx_io_device *d, int64_t b) {
    xx_sis *r = xx_sis_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sis(void *p) { xx_sis_free((xx_sis *)p); }
static Abstractformat *mk_spectrum_udi(xx_io_device *d, int64_t b) {
    xx_spectrum_udi *r = xx_spectrum_udi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_spectrum_udi(void *p) { xx_spectrum_udi_free((xx_spectrum_udi *)p); }
static Abstractformat *mk_squashfs_sqlz(xx_io_device *d, int64_t b) {
    xx_squashfs_sqlz *r = xx_squashfs_sqlz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_squashfs_sqlz(void *p) { xx_squashfs_sqlz_free((xx_squashfs_sqlz *)p); }
static Abstractformat *mk_stos_memory_bank(xx_io_device *d, int64_t b) {
    xx_stos_memory_bank *r = xx_stos_memory_bank_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stos_memory_bank(void *p) { xx_stos_memory_bank_free((xx_stos_memory_bank *)p); }
static Abstractformat *mk_stuffitx(xx_io_device *d, int64_t b) {
    xx_stuffitx *r = xx_stuffitx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_stuffitx(void *p) { xx_stuffitx_free((xx_stuffitx *)p); }
static Abstractformat *mk_sufs(xx_io_device *d, int64_t b) {
    xx_sufs *r = xx_sufs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sufs(void *p) { xx_sufs_free((xx_sufs *)p); }
static Abstractformat *mk_sunvtoc(xx_io_device *d, int64_t b) {
    xx_sunvtoc *r = xx_sunvtoc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_sunvtoc(void *p) { xx_sunvtoc_free((xx_sunvtoc *)p); }
static Abstractformat *mk_telltale_ttarch(xx_io_device *d, int64_t b) {
    xx_telltale_ttarch *r = xx_telltale_ttarch_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_telltale_ttarch(void *p) { xx_telltale_ttarch_free((xx_telltale_ttarch *)p); }
static Abstractformat *mk_ufs2(xx_io_device *d, int64_t b) {
    xx_ufs2 *r = xx_ufs2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ufs2(void *p) { xx_ufs2_free((xx_ufs2 *)p); }
static Abstractformat *mk_uif(xx_io_device *d, int64_t b) {
    xx_uif *r = xx_uif_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_uif(void *p) { xx_uif_free((xx_uif *)p); }
static Abstractformat *mk_valve_gcf_cache(xx_io_device *d, int64_t b) {
    xx_valve_gcf_cache *r = xx_valve_gcf_cache_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_valve_gcf_cache(void *p) { xx_valve_gcf_cache_free((xx_valve_gcf_cache *)p); }
static Abstractformat *mk_valve_xzp(xx_io_device *d, int64_t b) {
    xx_valve_xzp *r = xx_valve_xzp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_valve_xzp(void *p) { xx_valve_xzp_free((xx_valve_xzp *)p); }
static Abstractformat *mk_vmdk_cowd_sparse(xx_io_device *d, int64_t b) {
    xx_vmdk_cowd_sparse *r = xx_vmdk_cowd_sparse_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vmdk_cowd_sparse(void *p) { xx_vmdk_cowd_sparse_free((xx_vmdk_cowd_sparse *)p); }
static Abstractformat *mk_vmdk_sesparse(xx_io_device *d, int64_t b) {
    xx_vmdk_sesparse *r = xx_vmdk_sesparse_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_vmdk_sesparse(void *p) { xx_vmdk_sesparse_free((xx_vmdk_sesparse *)p); }
static Abstractformat *mk_xiaomi_hdr1(xx_io_device *d, int64_t b) {
    xx_xiaomi_hdr1 *r = xx_xiaomi_hdr1_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xiaomi_hdr1(void *p) { xx_xiaomi_hdr1_free((xx_xiaomi_hdr1 *)p); }
static Abstractformat *mk_xiaomi_hdr2(xx_io_device *d, int64_t b) {
    xx_xiaomi_hdr2 *r = xx_xiaomi_hdr2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xiaomi_hdr2(void *p) { xx_xiaomi_hdr2_free((xx_xiaomi_hdr2 *)p); }
static Abstractformat *mk_xp3(xx_io_device *d, int64_t b) {
    xx_xp3 *r = xx_xp3_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xp3(void *p) { xx_xp3_free((xx_xp3 *)p); }
static Abstractformat *mk_xpk_compressed_file(xx_io_device *d, int64_t b) {
    xx_xpk_compressed_file *r = xx_xpk_compressed_file_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_xpk_compressed_file(void *p) { xx_xpk_compressed_file_free((xx_xpk_compressed_file *)p); }
static Abstractformat *mk_yenc_encoded_file(xx_io_device *d, int64_t b) {
    xx_yenc_encoded_file *r = xx_yenc_encoded_file_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_yenc_encoded_file(void *p) { xx_yenc_encoded_file_free((xx_yenc_encoded_file *)p); }
static Abstractformat *mk_ypf(xx_io_device *d, int64_t b) {
    xx_ypf *r = xx_ypf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ypf(void *p) { xx_ypf_free((xx_ypf *)p); }
static Abstractformat *mk_lpak(xx_io_device *d,int64_t b){xx_lpak *r=xx_lpak_create(d,b);return r?&r->format:NULL;}
static void rm_lpak(void *r){xx_lpak_free((xx_lpak *)r);}
static Abstractformat *mk_freeze(xx_io_device *d,int64_t b) { xx_freeze *r=xx_freeze_create(d,b);return r?&r->format:NULL; }
static void rm_freeze(void *r) {xx_freeze_free((xx_freeze *)r);}
static Abstractformat *mk_bzip1(xx_io_device *d,int64_t b) { xx_bzip1 *r=xx_bzip1_create(d,b);return r?&r->format:NULL; }
static void rm_bzip1(void *r) {xx_bzip1_free((xx_bzip1 *)r);}
static Abstractformat *mk_gdi(xx_io_device *d, int64_t b) {
    xx_gdi *r = xx_gdi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_gdi(void *r) { xx_gdi_free((xx_gdi *)r); }
static Abstractformat *mk_mds(xx_io_device *d, int64_t b) {
    xx_mds *r = xx_mds_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mds(void *r) { xx_mds_free((xx_mds *)r); }
static Abstractformat *mk_cbm_d64(xx_io_device *d, int64_t b) {
    xx_cbm_d64 *r = xx_cbm_d64_create(d, b);
    int64_t total = d ? xx_io_total_size(d) : -1;
    if (r && total >= b + 196608 && total <= b + 197376) {
        xx_cbm_d64_destroy(r);
        xx_cbm_d64_init_ex(r, d, b, XX_CBM_D64_SPEED40);
        if (!xx_cbm_d64_check_is_valid(&r->format, NULL)) {
            xx_cbm_d64_destroy(r);
            xx_cbm_d64_init_ex(r, d, b, XX_CBM_D64_DOLPHIN40);
        }
    }
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cbm_d64_speed40(xx_io_device *d, int64_t b) {
    xx_cbm_d64 *r = xx_cbm_d64_create(d, b);
    if (r) { xx_cbm_d64_destroy(r); xx_cbm_d64_init_ex(r, d, b, XX_CBM_D64_SPEED40); }
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cbm_d64_dolphin40(xx_io_device *d, int64_t b) {
    xx_cbm_d64 *r = xx_cbm_d64_create(d, b);
    if (r) { xx_cbm_d64_destroy(r); xx_cbm_d64_init_ex(r, d, b, XX_CBM_D64_DOLPHIN40); }
    return r ? &r->format : NULL;
}
static void rm_cbm_d64(void *r) { xx_cbm_d64_free((xx_cbm_d64 *)r); }
static Abstractformat *mk_cbm_d71(xx_io_device *d, int64_t b) {
    xx_cbm_d71 *r = xx_cbm_d71_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cbm_d71(void *r) { xx_cbm_d71_free((xx_cbm_d71 *)r); }
static Abstractformat *mk_cbm_d81(xx_io_device *d, int64_t b) {
    xx_cbm_d81 *r = xx_cbm_d81_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cbm_d81(void *r) { xx_cbm_d81_free((xx_cbm_d81 *)r); }
static Abstractformat *mk_ccd(xx_io_device *d, int64_t b) {
    xx_ccd *r = xx_ccd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ccd(void *r) { xx_ccd_free((xx_ccd *)r); }
static Abstractformat *mk_cdrdao_toc(xx_io_device *d, int64_t b) {
    xx_cdrdao_toc *r = xx_cdrdao_toc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cdrdao_toc(void *r) { xx_cdrdao_toc_free((xx_cdrdao_toc *)r); }
static Abstractformat *mk_diskcopy42(xx_io_device *d, int64_t b) {
    xx_diskcopy42 *r = xx_diskcopy42_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_diskcopy42(void *r) { xx_diskcopy42_free((xx_diskcopy42 *)r); }
static Abstractformat *mk_acorn_dfs(xx_io_device *d, int64_t b) {
    xx_acorn_dfs *r = xx_acorn_dfs_create(d, b);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_acorn_dfs_variant(xx_io_device *d, int64_t b,
                                             xx_acorn_dfs_variant variant) {
    xx_acorn_dfs *r = xx_acorn_dfs_create(d, b);
    if (r) {
        xx_acorn_dfs_destroy(r);
        xx_acorn_dfs_init_ex(r, d, b, variant);
    }
    return r ? &r->format : NULL;
}
static Abstractformat *mk_acorn_dfs_ssd40(xx_io_device *d, int64_t b) {
    return mk_acorn_dfs_variant(d, b, XX_ACORN_DFS_SSD40);
}
static Abstractformat *mk_acorn_dfs_ssd80(xx_io_device *d, int64_t b) {
    return mk_acorn_dfs_variant(d, b, XX_ACORN_DFS_SSD80);
}
static Abstractformat *mk_acorn_dfs_dsd40(xx_io_device *d, int64_t b) {
    return mk_acorn_dfs_variant(d, b, XX_ACORN_DFS_DSD40);
}
static Abstractformat *mk_acorn_dfs_dsd80(xx_io_device *d, int64_t b) {
    return mk_acorn_dfs_variant(d, b, XX_ACORN_DFS_DSD80);
}
static void rm_acorn_dfs(void *r) { xx_acorn_dfs_free((xx_acorn_dfs *)r); }
static Abstractformat *mk_fdcopy_cfi(xx_io_device *d, int64_t b) {
    xx_fdcopy_cfi *r = xx_fdcopy_cfi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fdcopy_cfi(void *r) { xx_fdcopy_cfi_free((xx_fdcopy_cfi *)r); }
static Abstractformat *mk_atari_dos2(xx_io_device *d, int64_t b) {
    xx_atari_dos2 *r = xx_atari_dos2_create(d, b);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_atari_dos2_variant(xx_io_device *d, int64_t b,
                                              xx_atari_dos2_variant variant) {
    xx_atari_dos2 *r = xx_atari_dos2_create(d, b);
    if (r) {
        xx_atari_dos2_destroy(r);
        xx_atari_dos2_init_ex(r, d, b, variant);
    }
    return r ? &r->format : NULL;
}
static Abstractformat *mk_atari_dos2_sd(xx_io_device *d, int64_t b) {
    return mk_atari_dos2_variant(d, b, XX_ATARI_DOS2_SD);
}
static Abstractformat *mk_atari_dos2_ed(xx_io_device *d, int64_t b) {
    return mk_atari_dos2_variant(d, b, XX_ATARI_DOS2_ED);
}
static Abstractformat *mk_atari_dos2_dd(xx_io_device *d, int64_t b) {
    return mk_atari_dos2_variant(d, b, XX_ATARI_DOS2_DD);
}
static void rm_atari_dos2(void *r) { xx_atari_dos2_free((xx_atari_dos2 *)r); }
static Abstractformat *mk_apridisk(xx_io_device *d, int64_t b) {
    xx_apridisk *r = xx_apridisk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_apridisk(void *r) { xx_apridisk_free((xx_apridisk *)r); }
static Abstractformat *mk_ti99_dsk(xx_io_device *d, int64_t b) {
    xx_ti99_dsk *r = xx_ti99_dsk_create(d, b);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_ti99_dsk_variant(xx_io_device *d, int64_t b,
                                            xx_ti99_dsk_variant variant) {
    xx_ti99_dsk *r = xx_ti99_dsk_create(d, b);
    if (r) {
        xx_ti99_dsk_destroy(r);
        xx_ti99_dsk_init_ex(r, d, b, variant);
    }
    return r ? &r->format : NULL;
}
static Abstractformat *mk_ti99_dsk_sssd(xx_io_device *d, int64_t b) {
    return mk_ti99_dsk_variant(d, b, XX_TI99_DSK_SS_SD);
}
static Abstractformat *mk_ti99_dsk_dssd(xx_io_device *d, int64_t b) {
    return mk_ti99_dsk_variant(d, b, XX_TI99_DSK_DS_SD);
}
static Abstractformat *mk_ti99_dsk_dsdd(xx_io_device *d, int64_t b) {
    return mk_ti99_dsk_variant(d, b, XX_TI99_DSK_DS_DD);
}
static void rm_ti99_dsk(void *r) { xx_ti99_dsk_free((xx_ti99_dsk *)r); }
static Abstractformat *mk_cbm_d8x_variant(xx_io_device *d, int64_t b,
                                            xx_cbm_d8x_variant variant) {
    xx_cbm_d8x *r = xx_cbm_d8x_create(d, b);
    if (r) {
        xx_cbm_d8x_destroy(r);
        xx_cbm_d8x_init_ex(r, d, b, variant);
    }
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cbm_d80(xx_io_device *d, int64_t b) {
    return mk_cbm_d8x_variant(d, b, XX_CBM_D8X_8050);
}
static Abstractformat *mk_cbm_d82(xx_io_device *d, int64_t b) {
    return mk_cbm_d8x_variant(d, b, XX_CBM_D8X_8250);
}
static Abstractformat *mk_cbm_d8x_auto(xx_io_device *d, int64_t b) {
    int64_t size = xx_io_total_size(d);
    return size >= b && size - b >= 1066496 ?
           mk_cbm_d82(d, b) : mk_cbm_d80(d, b);
}
static void rm_cbm_d8x(void *r) { xx_cbm_d8x_free((xx_cbm_d8x *)r); }
static Abstractformat *mk_cbm_d67(xx_io_device *d, int64_t b) {
    xx_cbm_d67 *r = xx_cbm_d67_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cbm_d67(void *r) { xx_cbm_d67_free((xx_cbm_d67 *)r); }
static Abstractformat *mk_cbm_d90(xx_io_device *d, int64_t b) {
    int64_t size = xx_io_total_size(d);
    xx_cbm_d90_variant variant = size >= b && size - b == 5013504 ? XX_CBM_D90_9060 : XX_CBM_D90_9090;
    xx_cbm_d90 *r = xx_cbm_d90_create_ex(d, b, variant);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cbm_d9060(xx_io_device *d, int64_t b) {
    xx_cbm_d90 *r = xx_cbm_d90_create_ex(d, b, XX_CBM_D90_9060);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cbm_d9090(xx_io_device *d, int64_t b) {
    xx_cbm_d90 *r = xx_cbm_d90_create_ex(d, b, XX_CBM_D90_9090);
    return r ? &r->format : NULL;
}
static void rm_cbm_d90(void *r) { xx_cbm_d90_free((xx_cbm_d90 *)r); }
static Abstractformat *mk_myz80(xx_io_device *d, int64_t b) {
    xx_myz80 *r = xx_myz80_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_myz80(void *r) { xx_myz80_free((xx_myz80 *)r); }
static Abstractformat *mk_nanowasp(xx_io_device *d, int64_t b) {
    xx_nanowasp *r = xx_nanowasp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_nanowasp(void *r) { xx_nanowasp_free((xx_nanowasp *)r); }
static Abstractformat *mk_gotek720(xx_io_device *d, int64_t b) {
    xx_gotek *r = xx_gotek_create(d, b, 720U);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_gotek1440(xx_io_device *d, int64_t b) {
    xx_gotek *r = xx_gotek_create(d, b, 1440U);
    return r ? &r->format : NULL;
}
static void rm_gotek(void *r) { xx_gotek_free((xx_gotek *)r); }
static Abstractformat *mk_simh_disk(xx_io_device *d, int64_t b) {
    xx_simh_disk *r = xx_simh_disk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_simh_disk(void *r) { xx_simh_disk_free((xx_simh_disk *)r); }
static Abstractformat *mk_snatchit_cp2(xx_io_device *d, int64_t b) {
    xx_snatchit_cp2 *r = xx_snatchit_cp2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_snatchit_cp2(void *r) {
    xx_snatchit_cp2_free((xx_snatchit_cp2 *)r);
}
static Abstractformat *mk_northstar_nsi(xx_io_device *d, int64_t b) {
    xx_northstar_nsi *r = xx_northstar_nsi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_northstar_nsi(void *r) {
    xx_northstar_nsi_free((xx_northstar_nsi *)r);
}
static Abstractformat *mk_thomson_fd(xx_io_device *d, int64_t b) {
    xx_thomson_fd *r = xx_thomson_fd_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_thomson_fd(void *r) {
    xx_thomson_fd_free((xx_thomson_fd *)r);
}
static Abstractformat *mk_cmd_d1m(xx_io_device *d, int64_t b) {
    xx_cmd_fd *r = xx_cmd_fd_create(d, b, 1U);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cmd_d2m(xx_io_device *d, int64_t b) {
    xx_cmd_fd *r = xx_cmd_fd_create(d, b, 2U);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_cmd_d4m(xx_io_device *d, int64_t b) {
    xx_cmd_fd *r = xx_cmd_fd_create(d, b, 4U);
    return r ? &r->format : NULL;
}
static void rm_cmd_fd(void *r) {
    xx_cmd_fd_free((xx_cmd_fd *)r);
}
static Abstractformat *mk_pce_pri(xx_io_device *d, int64_t b) {
    xx_pce_pri *r = xx_pce_pri_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pce_pri(void *r) {
    xx_pce_pri_free((xx_pce_pri *)r);
}
static Abstractformat *mk_pce_pfi(xx_io_device *d, int64_t b) {
    xx_pce_pfi *r = xx_pce_pfi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pce_pfi(void *r) {
    xx_pce_pfi_free((xx_pce_pfi *)r);
}
static Abstractformat *mk_pce_pfdc_v0(xx_io_device *d, int64_t b) {
    xx_pce_pfdc *r = xx_pce_pfdc_create(d, b, 0U);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_pce_pfdc_v1(xx_io_device *d, int64_t b) {
    xx_pce_pfdc *r = xx_pce_pfdc_create(d, b, 1U);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_pce_pfdc_v2(xx_io_device *d, int64_t b) {
    xx_pce_pfdc *r = xx_pce_pfdc_create(d, b, 2U);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_pce_pfdc_v4(xx_io_device *d, int64_t b) {
    xx_pce_pfdc *r = xx_pce_pfdc_create(d, b, 4U);
    return r ? &r->format : NULL;
}
static void rm_pce_pfdc(void *r) {
    xx_pce_pfdc_free((xx_pce_pfdc *)r);
}
static Abstractformat *mk_pce_pbi(xx_io_device *d, int64_t b) {
    xx_pce_pbi *r = xx_pce_pbi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pce_pbi(void *r) {
    xx_pce_pbi_free((xx_pce_pbi *)r);
}
static Abstractformat *mk_pce_pbit(xx_io_device *d, int64_t b) {
    xx_pce_pbit *r = xx_pce_pbit_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pce_pbit(void *r) {
    xx_pce_pbit_free((xx_pce_pbit *)r);
}
static Abstractformat *mk_pce_tc(xx_io_device *d, int64_t b) {
    xx_pce_tc *r = xx_pce_tc_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pce_tc(void *r) {
    xx_pce_tc_free((xx_pce_tc *)r);
}
static Abstractformat *mk_pce_anadisk(xx_io_device *d, int64_t b) {
    xx_pce_anadisk *r = xx_pce_anadisk_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pce_anadisk(void *r) {
    xx_pce_anadisk_free((xx_pce_anadisk *)r);
}
static Abstractformat *mk_pce_xdf(xx_io_device *d, int64_t b) {
    xx_pce_xdf *r = xx_pce_xdf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_pce_xdf(void *r) {
    xx_pce_xdf_free((xx_pce_xdf *)r);
}
static Abstractformat *mk_os9_rbf(xx_io_device *d, int64_t b) {
    xx_os9_rbf *r = xx_os9_rbf_create(d, b);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_os9_rbf_35ss_c1(xx_io_device *d, int64_t b) {
    xx_os9_rbf *r = xx_os9_rbf_create_ex(d, b, XX_OS9_RBF_35SS_C1);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_os9_rbf_40ds_c1(xx_io_device *d, int64_t b) {
    xx_os9_rbf *r = xx_os9_rbf_create_ex(d, b, XX_OS9_RBF_40DS_C1);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_os9_rbf_40ds_c2(xx_io_device *d, int64_t b) {
    xx_os9_rbf *r = xx_os9_rbf_create_ex(d, b, XX_OS9_RBF_40DS_C2);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_os9_rbf_80ds_c1(xx_io_device *d, int64_t b) {
    xx_os9_rbf *r = xx_os9_rbf_create_ex(d, b, XX_OS9_RBF_80DS_C1);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_os9_rbf_hd4096_c4(xx_io_device *d, int64_t b) {
    xx_os9_rbf *r = xx_os9_rbf_create_ex(d, b, XX_OS9_RBF_HD4096_C4);
    return r ? &r->format : NULL;
}
static void rm_os9_rbf(void *r) { xx_os9_rbf_free((xx_os9_rbf *)r); }
static Abstractformat *mk_ldbs(xx_io_device *d, int64_t b) {
    xx_ldbs *r = xx_ldbs_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ldbs(void *r) { xx_ldbs_free((xx_ldbs *)r); }
static Abstractformat *mk_ldbst(xx_io_device *d, int64_t b) {
    xx_ldbst *r = xx_ldbst_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ldbst(void *r) { xx_ldbst_free((xx_ldbst *)r); }
static Abstractformat *mk_apple_dos32(xx_io_device *d, int64_t b) {
    xx_apple_dos32 *r = xx_apple_dos32_create(d, b);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_apple_dos32_raw(xx_io_device *d, int64_t b) {
    xx_apple_dos32 *r = xx_apple_dos32_create(d, b);
    if (r) {
        xx_apple_dos32_destroy(r);
        xx_apple_dos32_init_ex(r, d, b, XX_APPLE_DOS32_MODE_RAW_SECTORS);
    }
    return r ? &r->format : NULL;
}
static void rm_apple_dos32(void *r) { xx_apple_dos32_free((xx_apple_dos32 *)r); }
static Abstractformat *mk_apple_dos33_32(xx_io_device *d, int64_t b) {
    xx_apple_dos33_32 *r = xx_apple_dos33_32_create(d, b);
    return r ? &r->format : NULL;
}
static Abstractformat *mk_apple_dos33_32_raw(xx_io_device *d, int64_t b) {
    xx_apple_dos33_32 *r = xx_apple_dos33_32_create(d, b);
    if (r) {
        xx_apple_dos33_32_destroy(r);
        xx_apple_dos33_32_init_ex(r, d, b, XX_APPLE_DOS33_32_MODE_RAW_SECTORS);
    }
    return r ? &r->format : NULL;
}
static void rm_apple_dos33_32(void *r) { xx_apple_dos33_32_free((xx_apple_dos33_32 *)r); }
static Abstractformat *mk_bytekiller(xx_io_device *d, int64_t b) {
    xx_bytekiller *r = xx_bytekiller_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_bytekiller(void *r) { xx_bytekiller_free((xx_bytekiller *)r); }
static Abstractformat *mk_mozilla_mar(xx_io_device *d, int64_t b) {
    xx_mozilla_mar *r = xx_mozilla_mar_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_mozilla_mar(void *p) { xx_mozilla_mar_free((xx_mozilla_mar *)p); }
static Abstractformat *mk_westwood_pak(xx_io_device *d, int64_t b) {
    xx_westwood_pak *r = xx_westwood_pak_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_westwood_pak(void *p) { xx_westwood_pak_free((xx_westwood_pak *)p); }
static Abstractformat *mk_fatx(xx_io_device *d, int64_t b) {
    xx_fatx *r = xx_fatx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_fatx(void *p) { xx_fatx_free((xx_fatx *)p); }
static Abstractformat *mk_soundfont2(xx_io_device *d, int64_t b) {
    xx_soundfont2 *r = xx_soundfont2_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_soundfont2(void *p) { xx_soundfont2_free((xx_soundfont2 *)p); }
static Abstractformat *mk_ivf(xx_io_device *d, int64_t b) {
    xx_ivf *r = xx_ivf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ivf(void *p) { xx_ivf_free((xx_ivf *)p); }
static Abstractformat *mk_windows_ani(xx_io_device *d, int64_t b) {
    xx_windows_ani *r = xx_windows_ani_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_windows_ani(void *p) { xx_windows_ani_free((xx_windows_ani *)p); }
static Abstractformat *mk_interplay_acm(xx_io_device *d, int64_t b) {
    xx_interplay_acm *r = xx_interplay_acm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_interplay_acm(void *p) { xx_interplay_acm_free((xx_interplay_acm *)p); }
static Abstractformat *mk_cri_ahx(xx_io_device *d, int64_t b) {
    xx_cri_ahx *r = xx_cri_ahx_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_cri_ahx(void *p) { xx_cri_ahx_free((xx_cri_ahx *)p); }
static Abstractformat *mk_adobe_director_cxt(xx_io_device *d, int64_t b) {
    xx_adobe_director_cxt *r = xx_adobe_director_cxt_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_adobe_director_cxt(void *p) { xx_adobe_director_cxt_free((xx_adobe_director_cxt *)p); }
static Abstractformat *mk_olympus_dss(xx_io_device *d, int64_t b) {
    xx_olympus_dss *r = xx_olympus_dss_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_olympus_dss(void *p) { xx_olympus_dss_free((xx_olympus_dss *)p); }
static Abstractformat *mk_ea_exa(xx_io_device *d, int64_t b) {
    xx_ea_exa *r = xx_ea_exa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ea_exa(void *p) { xx_ea_exa_free((xx_ea_exa *)p); }
static Abstractformat *mk_audio_nitro_strm(xx_io_device *d, int64_t b) {
    xx_audio_nitro_strm *r = xx_audio_nitro_strm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_nitro_strm(void *p) { xx_audio_nitro_strm_free((xx_audio_nitro_strm *)p); }
static Abstractformat *mk_audio_wwise_wem(xx_io_device *d, int64_t b) {
    xx_audio_wwise_wem *r = xx_audio_wwise_wem_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_wwise_wem(void *p) { xx_audio_wwise_wem_free((xx_audio_wwise_wem *)p); }
static Abstractformat *mk_audio_scumm_sou(xx_io_device *d, int64_t b) {
    xx_audio_scumm_sou *r = xx_audio_scumm_sou_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_scumm_sou(void *p) { xx_audio_scumm_sou_free((xx_audio_scumm_sou *)p); }
static Abstractformat *mk_audio_riff_ima(xx_io_device *d, int64_t b) {
    xx_audio_riff_ima *r = xx_audio_riff_ima_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_riff_ima(void *p) { xx_audio_riff_ima_free((xx_audio_riff_ima *)p); }
static Abstractformat *mk_hmi_midi(xx_io_device *d, int64_t b) {
    xx_hmi_midi *r = xx_hmi_midi_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_hmi_midi(void *p) { xx_hmi_midi_free((xx_hmi_midi *)p); }
static Abstractformat *mk_ensoniq_paf(xx_io_device *d, int64_t b) {
    xx_ensoniq_paf *r = xx_ensoniq_paf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ensoniq_paf(void *p) { xx_ensoniq_paf_free((xx_ensoniq_paf *)p); }
static Abstractformat *mk_abylight_strm(xx_io_device *d, int64_t b) {
    xx_abylight_strm *r = xx_abylight_strm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_abylight_strm(void *p) { xx_abylight_strm_free((xx_abylight_strm *)p); }
static Abstractformat *mk_lego_alp(xx_io_device *d, int64_t b) {
    xx_lego_alp *r = xx_lego_alp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_lego_alp(void *p) { xx_lego_alp_free((xx_lego_alp *)p); }
static Abstractformat *mk_audio_pvf(xx_io_device *d, int64_t b) {
    xx_audio_pvf *r = xx_audio_pvf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_pvf(void *p) { xx_audio_pvf_free((xx_audio_pvf *)p); }
static Abstractformat *mk_audio_rifx_wave(xx_io_device *d, int64_t b) {
    xx_audio_rifx_wave *r = xx_audio_rifx_wave_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_rifx_wave(void *p) { xx_audio_rifx_wave_free((xx_audio_rifx_wave *)p); }
static Abstractformat *mk_audio_shockwave_swa(xx_io_device *d, int64_t b) {
    xx_audio_shockwave_swa *r = xx_audio_shockwave_swa_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_audio_shockwave_swa(void *p) { xx_audio_shockwave_swa_free((xx_audio_shockwave_swa *)p); }
static Abstractformat *mk_ckp(xx_io_device *d, int64_t b) {
    xx_ckp *r = xx_ckp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ckp(void *p) { xx_ckp_free((xx_ckp *)p); }
static Abstractformat *mk_edp(xx_io_device *d, int64_t b) {
    xx_edp *r = xx_edp_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_edp(void *p) { xx_edp_free((xx_edp *)p); }
static Abstractformat *mk_parsec_rib(xx_io_device *d, int64_t b) {
    xx_parsec_rib *r = xx_parsec_rib_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_parsec_rib(void *p) { xx_parsec_rib_free((xx_parsec_rib *)p); }
static Abstractformat *mk_parsec_archive(xx_io_device *d, int64_t b) {
    xx_parsec_archive *r = xx_parsec_archive_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_parsec_archive(void *p) { xx_parsec_archive_free((xx_parsec_archive *)p); }
static Abstractformat *mk_parsec_pmm(xx_io_device *d, int64_t b) {
    xx_parsec_pmm *r = xx_parsec_pmm_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_parsec_pmm(void *p) { xx_parsec_pmm_free((xx_parsec_pmm *)p); }
static Abstractformat *mk_ptero_bigf(xx_io_device *d, int64_t b) {
    xx_ptero_bigf *r = xx_ptero_bigf_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ptero_bigf(void *p) { xx_ptero_bigf_free((xx_ptero_bigf *)p); }
static Abstractformat *mk_rvz(xx_io_device *d, int64_t b) {
    xx_rvz *r = xx_rvz_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_rvz(void *p) { xx_rvz_free((xx_rvz *)p); }

#ifndef XXFC_FORMATS_ONLY
#define XX_DIE_MUSIC_CLI_ROW(type, slug) \
    extern Abstractformat *xx_die_music_##slug##_create(xx_io_device *, int64_t); \
    extern void xx_die_music_##slug##_free(void *);
#include "../../src/formats/die_music/xx_die_music_cli_rows.inc"
#undef XX_DIE_MUSIC_CLI_ROW
#endif

static xxfc_reader_entry g_readers[] = {
    {"ckp", mk_ckp, rm_ckp, XX_FILE_TYPE_CKP},
    {"edp", mk_edp, rm_edp, XX_FILE_TYPE_EDP},
    {"parsec_rib", mk_parsec_rib, rm_parsec_rib, XX_FILE_TYPE_PARSEC_RIB},
    {"parsec_archive", mk_parsec_archive, rm_parsec_archive, XX_FILE_TYPE_PARSEC_ARCHIVE},
    {"parsec_pmm", mk_parsec_pmm, rm_parsec_pmm, XX_FILE_TYPE_PARSEC_PMM},
    {"ptero_bigf", mk_ptero_bigf, rm_ptero_bigf, XX_FILE_TYPE_PTERO_BIGF},
    {"rvz", mk_rvz, rm_rvz, XX_FILE_TYPE_RVZ},
    {"mozilla_mar", mk_mozilla_mar, rm_mozilla_mar, XX_FILE_TYPE_MOZILLA_MAR},
    {"westwood_pak", mk_westwood_pak, rm_westwood_pak, XX_FILE_TYPE_WESTWOOD_PAK},
    {"fatx", mk_fatx, rm_fatx, XX_FILE_TYPE_FATX},
    {"soundfont2", mk_soundfont2, rm_soundfont2, XX_FILE_TYPE_SOUNDFONT2},
    {"ivf", mk_ivf, rm_ivf, XX_FILE_TYPE_IVF},
    {"windows_ani", mk_windows_ani, rm_windows_ani, XX_FILE_TYPE_WINDOWS_ANI},
    {"interplay_acm", mk_interplay_acm, rm_interplay_acm, XX_FILE_TYPE_INTERPLAY_ACM},
    {"cri_ahx", mk_cri_ahx, rm_cri_ahx, XX_FILE_TYPE_CRI_AHX},
    {"adobe_director_cxt", mk_adobe_director_cxt, rm_adobe_director_cxt, XX_FILE_TYPE_ADOBE_DIRECTOR_CXT},
    {"olympus_dss", mk_olympus_dss, rm_olympus_dss, XX_FILE_TYPE_OLYMPUS_DSS},
    {"ea_exa", mk_ea_exa, rm_ea_exa, XX_FILE_TYPE_EA_EXA},
    {"audio_nitro_strm", mk_audio_nitro_strm, rm_audio_nitro_strm, XX_FILE_TYPE_AUDIO_NITRO_STRM},
    {"audio_wwise_wem", mk_audio_wwise_wem, rm_audio_wwise_wem, XX_FILE_TYPE_AUDIO_WWISE_WEM},
    {"audio_scumm_sou", mk_audio_scumm_sou, rm_audio_scumm_sou, XX_FILE_TYPE_AUDIO_SCUMM_SOU},
    {"audio_riff_ima", mk_audio_riff_ima, rm_audio_riff_ima, XX_FILE_TYPE_AUDIO_RIFF_IMA},
    {"hmi_midi", mk_hmi_midi, rm_hmi_midi, XX_FILE_TYPE_HMI_MIDI},
    {"ensoniq_paf", mk_ensoniq_paf, rm_ensoniq_paf, XX_FILE_TYPE_ENSONIQ_PAF},
    {"abylight_strm", mk_abylight_strm, rm_abylight_strm, XX_FILE_TYPE_ABYLIGHT_STRM},
    {"lego_alp", mk_lego_alp, rm_lego_alp, XX_FILE_TYPE_LEGO_ALP},
    {"audio_pvf", mk_audio_pvf, rm_audio_pvf, XX_FILE_TYPE_AUDIO_PVF},
    {"audio_rifx_wave", mk_audio_rifx_wave, rm_audio_rifx_wave, XX_FILE_TYPE_AUDIO_RIFX_WAVE},
    {"audio_shockwave_swa", mk_audio_shockwave_swa, rm_audio_shockwave_swa, XX_FILE_TYPE_AUDIO_SHOCKWAVE_SWA},
#ifndef XXFC_FORMATS_ONLY
#define XX_DIE_MUSIC_CLI_ROW(type, slug) \
    {"die_music_" #slug, xx_die_music_##slug##_create, \
     xx_die_music_##slug##_free, type},
#include "../../src/formats/die_music/xx_die_music_cli_rows.inc"
#undef XX_DIE_MUSIC_CLI_ROW
#endif
    {"gdi", mk_gdi, rm_gdi, XX_FILE_TYPE_GDI},
    {"mds", mk_mds, rm_mds, XX_FILE_TYPE_MDS},
    {"cbm_d64", mk_cbm_d64, rm_cbm_d64, XX_FILE_TYPE_CBM_D64},
    {"cbm_d64_speed40", mk_cbm_d64_speed40, rm_cbm_d64, XX_FILE_TYPE_CBM_D64},
    {"cbm_d64_dolphin40", mk_cbm_d64_dolphin40, rm_cbm_d64, XX_FILE_TYPE_CBM_D64},
    {"cbm_d71", mk_cbm_d71, rm_cbm_d71, XX_FILE_TYPE_CBM_D71},
    {"cbm_d81", mk_cbm_d81, rm_cbm_d81, XX_FILE_TYPE_CBM_D81},
    {"cbm_d8x", mk_cbm_d8x_auto, rm_cbm_d8x, XX_FILE_TYPE_CBM_D8X},
    {"cbm_d80", mk_cbm_d80, rm_cbm_d8x, XX_FILE_TYPE_CBM_D8X},
    {"cbm_d82", mk_cbm_d82, rm_cbm_d8x, XX_FILE_TYPE_CBM_D8X},
    {"cbm_d67", mk_cbm_d67, rm_cbm_d67, XX_FILE_TYPE_CBM_D67},
    {"cbm_d90", mk_cbm_d90, rm_cbm_d90, XX_FILE_TYPE_CBM_D90},
    {"cbm_d9060", mk_cbm_d9060, rm_cbm_d90, XX_FILE_TYPE_CBM_D90},
    {"cbm_d9090", mk_cbm_d9090, rm_cbm_d90, XX_FILE_TYPE_CBM_D90},
    {"myz80", mk_myz80, rm_myz80, XX_FILE_TYPE_MYZ80},
    {"nanowasp", mk_nanowasp, rm_nanowasp, XX_FILE_TYPE_NANOWASP},
    {"gotek720", mk_gotek720, rm_gotek, XX_FILE_TYPE_GOTEK},
    {"gotek1440", mk_gotek1440, rm_gotek, XX_FILE_TYPE_GOTEK},
    {"simh_disk", mk_simh_disk, rm_simh_disk, XX_FILE_TYPE_SIMH_DISK},
    {"snatchit_cp2", mk_snatchit_cp2, rm_snatchit_cp2, XX_FILE_TYPE_SNATCHIT_CP2},
    {"northstar_nsi", mk_northstar_nsi, rm_northstar_nsi, XX_FILE_TYPE_NORTHSTAR_NSI},
    {"thomson_fd", mk_thomson_fd, rm_thomson_fd, XX_FILE_TYPE_THOMSON_FD},
    {"cmd_d1m", mk_cmd_d1m, rm_cmd_fd, XX_FILE_TYPE_CMD_D1M},
    {"cmd_d2m", mk_cmd_d2m, rm_cmd_fd, XX_FILE_TYPE_CMD_D2M},
    {"cmd_d4m", mk_cmd_d4m, rm_cmd_fd, XX_FILE_TYPE_CMD_D4M},
    {"pce_pri", mk_pce_pri, rm_pce_pri, XX_FILE_TYPE_PCE_PRI},
    {"pce_pfi", mk_pce_pfi, rm_pce_pfi, XX_FILE_TYPE_PCE_PFI},
    {"pce_pfdc_v0", mk_pce_pfdc_v0, rm_pce_pfdc, XX_FILE_TYPE_PCE_PFDC_V0},
    {"pce_pfdc_v1", mk_pce_pfdc_v1, rm_pce_pfdc, XX_FILE_TYPE_PCE_PFDC_V1},
    {"pce_pfdc_v2", mk_pce_pfdc_v2, rm_pce_pfdc, XX_FILE_TYPE_PCE_PFDC_V2},
    {"pce_pfdc_v4", mk_pce_pfdc_v4, rm_pce_pfdc, XX_FILE_TYPE_PCE_PFDC_V4},
    {"pce_pbi", mk_pce_pbi, rm_pce_pbi, XX_FILE_TYPE_PCE_PBI},
    {"pce_pbit", mk_pce_pbit, rm_pce_pbit, XX_FILE_TYPE_PCE_PBIT},
    {"pce_tc", mk_pce_tc, rm_pce_tc, XX_FILE_TYPE_PCE_TC},
    {"pce_anadisk", mk_pce_anadisk, rm_pce_anadisk, XX_FILE_TYPE_PCE_ANADISK},
    {"pce_xdf", mk_pce_xdf, rm_pce_xdf, XX_FILE_TYPE_PCE_XDF},
    {"os9_rbf", mk_os9_rbf, rm_os9_rbf, XX_FILE_TYPE_OS9_RBF},
    {"os9_rbf_35ss_c1", mk_os9_rbf_35ss_c1, rm_os9_rbf, XX_FILE_TYPE_OS9_RBF},
    {"os9_rbf_40ds_c1", mk_os9_rbf_40ds_c1, rm_os9_rbf, XX_FILE_TYPE_OS9_RBF},
    {"os9_rbf_40ds_c2", mk_os9_rbf_40ds_c2, rm_os9_rbf, XX_FILE_TYPE_OS9_RBF},
    {"os9_rbf_80ds_c1", mk_os9_rbf_80ds_c1, rm_os9_rbf, XX_FILE_TYPE_OS9_RBF},
    {"os9_rbf_hd4096_c4", mk_os9_rbf_hd4096_c4, rm_os9_rbf, XX_FILE_TYPE_OS9_RBF},
    {"ldbs", mk_ldbs, rm_ldbs, XX_FILE_TYPE_LDBS},
    {"ldbst", mk_ldbst, rm_ldbst, XX_FILE_TYPE_LDBST},
    {"ccd", mk_ccd, rm_ccd, XX_FILE_TYPE_CCD},
    {"cdrdao_toc", mk_cdrdao_toc, rm_cdrdao_toc, XX_FILE_TYPE_CDRDAO_TOC},
    {"diskcopy42", mk_diskcopy42, rm_diskcopy42, XX_FILE_TYPE_DISKCOPY42},
    {"acorn_dfs", mk_acorn_dfs, rm_acorn_dfs, XX_FILE_TYPE_ACORN_DFS},
    {"acorn_dfs_ssd40", mk_acorn_dfs_ssd40, rm_acorn_dfs, XX_FILE_TYPE_ACORN_DFS},
    {"acorn_dfs_ssd80", mk_acorn_dfs_ssd80, rm_acorn_dfs, XX_FILE_TYPE_ACORN_DFS},
    {"acorn_dfs_dsd40", mk_acorn_dfs_dsd40, rm_acorn_dfs, XX_FILE_TYPE_ACORN_DFS},
    {"acorn_dfs_dsd80", mk_acorn_dfs_dsd80, rm_acorn_dfs, XX_FILE_TYPE_ACORN_DFS},
    {"fdcopy_cfi", mk_fdcopy_cfi, rm_fdcopy_cfi, XX_FILE_TYPE_FDCOPY_CFI},
    {"atari_dos2", mk_atari_dos2, rm_atari_dos2, XX_FILE_TYPE_ATARI_DOS2},
    {"atari_dos2_sd", mk_atari_dos2_sd, rm_atari_dos2, XX_FILE_TYPE_ATARI_DOS2},
    {"atari_dos2_ed", mk_atari_dos2_ed, rm_atari_dos2, XX_FILE_TYPE_ATARI_DOS2},
    {"atari_dos2_dd", mk_atari_dos2_dd, rm_atari_dos2, XX_FILE_TYPE_ATARI_DOS2},
    {"apridisk", mk_apridisk, rm_apridisk, XX_FILE_TYPE_APRIDISK},
    {"ti99_dsk", mk_ti99_dsk, rm_ti99_dsk, XX_FILE_TYPE_TI99_DSK},
    {"ti99_dsk_sssd", mk_ti99_dsk_sssd, rm_ti99_dsk, XX_FILE_TYPE_TI99_DSK},
    {"ti99_dsk_dssd", mk_ti99_dsk_dssd, rm_ti99_dsk, XX_FILE_TYPE_TI99_DSK},
    {"ti99_dsk_dsdd", mk_ti99_dsk_dsdd, rm_ti99_dsk, XX_FILE_TYPE_TI99_DSK},
    {"apple_dos32", mk_apple_dos32, rm_apple_dos32, XX_FILE_TYPE_APPLE_DOS32},
    {"apple_dos32_raw", mk_apple_dos32_raw, rm_apple_dos32, XX_FILE_TYPE_APPLE_DOS32},
    {"apple_dos33_32", mk_apple_dos33_32, rm_apple_dos33_32, XX_FILE_TYPE_APPLE_DOS33_32},
    {"apple_dos33_32_raw", mk_apple_dos33_32_raw, rm_apple_dos33_32, XX_FILE_TYPE_APPLE_DOS33_32},
    {"bytekiller", mk_bytekiller, rm_bytekiller, XX_FILE_TYPE_BYTEKILLER},
    {"apple_pascal",mk_apple_pascal,rm_apple_pascal,XX_FILE_TYPE_APPLE_PASCAL},
    {"dmk",mk_dmk,rm_dmk,XX_FILE_TYPE_DMK},
    {"mfs",mk_mfs,rm_mfs,XX_FILE_TYPE_MFS},
    {"hfs",mk_hfs,rm_hfs,XX_FILE_TYPE_HFS},
    {"catsystem_kif",mk_catsystem_kif,rm_catsystem_kif,XX_FILE_TYPE_CATSYSTEM_KIF},
    {"malie_lib",mk_malie_lib,rm_malie_lib,XX_FILE_TYPE_MALIE_LIB},
    {"nexas_pac",mk_nexas_pac,rm_nexas_pac,XX_FILE_TYPE_NEXAS_PAC},
    {"nitroplus_npa",mk_nitroplus_npa,rm_nitroplus_npa,XX_FILE_TYPE_NITROPLUS_NPA},
    {"cpm",mk_cpm,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_apple_do",mk_cpm_apple_do,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_apple_po",mk_cpm_apple_po,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_pcw180",mk_cpm_pcw180,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_cpc_system",mk_cpm_cpc_system,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_cpc_data",mk_cpm_cpc_data,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_cf2dd",mk_cpm_cf2dd,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_alpha",mk_cpm_alpha,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_sdcard",mk_cpm_sdcard,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_pc1_2m",mk_cpm_pc1_2m,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_cpm86_144feat",mk_cpm_cpm86_144feat,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_p112",mk_cpm_p112,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_p112_old",mk_cpm_p112_old,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_nigdos",mk_cpm_nigdos,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_epsqx10",mk_cpm_epsqx10,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_ibm_8ss",mk_cpm_ibm_8ss,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_electroglas",mk_cpm_electroglas,rm_cpm,XX_FILE_TYPE_CPM},
    {"cpm_ibmpc_514ds",mk_cpm_ibmpc_514ds,rm_cpm,XX_FILE_TYPE_CPM},
    {"ufs1",mk_ufs1,rm_ufs1,XX_FILE_TYPE_UFS1},
    {"xva",mk_xva,rm_xva,XX_FILE_TYPE_XVA},
    {"qlie_pack",mk_qlie_pack,rm_qlie_pack,XX_FILE_TYPE_QLIE_PACK},
    {"hfsplus",mk_hfsplus,rm_hfsplus,XX_FILE_TYPE_HFSPLUS},
    {"partimage",mk_partimage,rm_partimage,XX_FILE_TYPE_PARTIMAGE},
    {"aaruformat",mk_aaruformat,rm_aaruformat,XX_FILE_TYPE_AARUFORMAT},
    {"acorn_adfs",mk_acorn_adfs,rm_acorn_adfs,XX_FILE_TYPE_ADFS},
    {"acorn_adfs_linear",mk_acorn_adfs_linear,rm_acorn_adfs,XX_FILE_TYPE_ADFS},
    {"apple_dos33",mk_apple_dos33,rm_apple_dos33,XX_FILE_TYPE_APPLE_DOS33},
    {"nitroplus_npk2",mk_nitroplus_npk2,rm_nitroplus_npk2,XX_FILE_TYPE_NITROPLUS_NPK2},
    {"thomson_sap",mk_thomson_sap,rm_thomson_sap,XX_FILE_TYPE_THOMSON_SAP},
    {"anex86_hdi",mk_anex86_hdi,rm_anex86_hdi,XX_FILE_TYPE_ANEX86_HDI},
    {"exfat",mk_exfat,rm_exfat,XX_FILE_TYPE_EXFAT},
    {"majiro",mk_majiro,rm_majiro,XX_FILE_TYPE_MAJIRO},
    {"wux",mk_wux,rm_wux,XX_FILE_TYPE_WUX},
    {"sdi",mk_sdi,rm_sdi,XX_FILE_TYPE_SDI},
    {"nhd",mk_nhd,rm_nhd,XX_FILE_TYPE_NHD},
    {"virtual98",mk_virtual98,rm_virtual98,XX_FILE_TYPE_VIRTUAL98},
    {"lpak",mk_lpak,rm_lpak,XX_FILE_TYPE_LPAK},
    {"freeze",mk_freeze,rm_freeze,XX_FILE_TYPE_FREEZE},
    {"bzip1",mk_bzip1,rm_bzip1,XX_FILE_TYPE_BZIP1},
    { "acorn_atom_disk", mk_acorn_atom_disk, rm_acorn_atom_disk, XX_FILE_TYPE_ACORN_ATOM_DISK },
    { "apollo_afd", mk_apollo_afd, rm_apollo_afd, XX_FILE_TYPE_APOLLO_AFD },
    { "bga", mk_bga, rm_bga, XX_FILE_TYPE_BGA },
    { "bgi", mk_bgi, rm_bgi, XX_FILE_TYPE_BGI },
    { "bgi2", mk_bgi2, rm_bgi2, XX_FILE_TYPE_BGI2 },
    { "binscii", mk_binscii, rm_binscii, XX_FILE_TYPE_BINSCII },
    { "blindwrite_5_6_image", mk_blindwrite_5_6_image, rm_blindwrite_5_6_image, XX_FILE_TYPE_BLINDWRITE_5_6_IMAGE },
    { "btrfs_stream", mk_btrfs_stream, rm_btrfs_stream, XX_FILE_TYPE_BTRFS_STREAM },
    { "camputers_lynx_ldf", mk_camputers_lynx_ldf, rm_camputers_lynx_ldf, XX_FILE_TYPE_CAMPUTERS_LYNX_LDF },
    { "cpk", mk_cpk, rm_cpk, XX_FILE_TYPE_CPK },
    { "crt", mk_crt, rm_crt, XX_FILE_TYPE_CRT },
    { "d_link_alpha_encimg_v2", mk_d_link_alpha_encimg_v2, rm_d_link_alpha_encimg_v2, XX_FILE_TYPE_D_LINK_ALPHA_ENCIMG_V2 },
    { "d_link_fpkg_cpkg", mk_d_link_fpkg_cpkg, rm_d_link_fpkg_cpkg, XX_FILE_TYPE_D_LINK_FPKG_CPKG },
    { "daemon_tools_mdx", mk_daemon_tools_mdx, rm_daemon_tools_mdx, XX_FILE_TYPE_DAEMON_TOOLS_MDX },
    { "dart", mk_dart, rm_dart, XX_FILE_TYPE_DART },
    { "ddd", mk_ddd, rm_ddd, XX_FILE_TYPE_DDD },
    { "diet_compression", mk_diet_compression, rm_diet_compression, XX_FILE_TYPE_DIET_COMPRESSION },
    { "dxa", mk_dxa, rm_dxa, XX_FILE_TYPE_DXA },
    { "ea_fsh", mk_ea_fsh, rm_ea_fsh, XX_FILE_TYPE_EA_FSH },
    { "ewf2_ex01", mk_ewf2_ex01, rm_ewf2_ex01, XX_FILE_TYPE_EWF2_EX01 },
    { "ewf2_lx01", mk_ewf2_lx01, rm_ewf2_lx01, XX_FILE_TYPE_EWF2_LX01 },
    { "ewf_l01", mk_ewf_l01, rm_ewf_l01, XX_FILE_TYPE_EWF_L01 },
    { "fmod_sample_bank", mk_fmod_sample_bank, rm_fmod_sample_bank, XX_FILE_TYPE_FMOD_SAMPLE_BANK },
    { "gbi", mk_gbi, rm_gbi, XX_FILE_TYPE_GBI },
    { "goldsrc_bsp", mk_goldsrc_bsp, rm_goldsrc_bsp, XX_FILE_TYPE_GOLDSRC_BSP },
    { "hsf", mk_hsf, rm_hsf, XX_FILE_TYPE_HSF },
    { "htc_nbh_rom_image", mk_htc_nbh_rom_image, rm_htc_nbh_rom_image, XX_FILE_TYPE_HTC_NBH_ROM_IMAGE },
    { "hxc_hfe_extended", mk_hxc_hfe_extended, rm_hxc_hfe_extended, XX_FILE_TYPE_HXC_HFE_EXTENDED },
    { "hxc_hfe_hddd_a2_variant", mk_hxc_hfe_hddd_a2_variant, rm_hxc_hfe_hddd_a2_variant, XX_FILE_TYPE_HXC_HFE_HDDD_A2_VARIANT },
    { "hxc_hfe_v3", mk_hxc_hfe_v3, rm_hxc_hfe_v3, XX_FILE_TYPE_HXC_HFE_V3 },
    { "hxs", mk_hxs, rm_hxs, XX_FILE_TYPE_HXS },
    { "jffs2_old", mk_jffs2_old, rm_jffs2_old, XX_FILE_TYPE_JFFS2_OLD },
    { "jvc", mk_jvc, rm_jvc, XX_FILE_TYPE_JVC },
    { "kgb_archiver", mk_kgb_archiver, rm_kgb_archiver, XX_FILE_TYPE_KGB_ARCHIVER },
    { "kryoflux_stream", mk_kryoflux_stream, rm_kryoflux_stream, XX_FILE_TYPE_KRYOFLUX_STREAM },
    { "livemaker", mk_livemaker, rm_livemaker, XX_FILE_TYPE_LIVEMAKER },
    { "lzma86", mk_lzma86, rm_lzma86, XX_FILE_TYPE_LZMA86 },
    { "maxis_far_archive", mk_maxis_far_archive, rm_maxis_far_archive, XX_FILE_TYPE_MAXIS_FAR_ARCHIVE },
    { "mgt", mk_mgt, rm_mgt, XX_FILE_TYPE_MGT },
    { "minix", mk_minix, rm_minix, XX_FILE_TYPE_MINIX },
    { "moof", mk_moof, rm_moof, XX_FILE_TYPE_MOOF },
    { "ms_dos_backup2", mk_ms_dos_backup2, rm_ms_dos_backup2, XX_FILE_TYPE_MS_DOS_BACKUP2 },
    { "mub", mk_mub, rm_mub, XX_FILE_TYPE_MUB },
    { "noa", mk_noa, rm_noa, XX_FILE_TYPE_NOA },
    { "outlook_express_dbx_mailbox", mk_outlook_express_dbx_mailbox, rm_outlook_express_dbx_mailbox, XX_FILE_TYPE_OUTLOOK_EXPRESS_DBX_MAILBOX },
    { "partclone_image", mk_partclone_image, rm_partclone_image, XX_FILE_TYPE_PARTCLONE_IMAGE },
    { "ppmd", mk_ppmd, rm_ppmd, XX_FILE_TYPE_PPMD },
    { "quoted_printable_encoded_fil", mk_quoted_printable_encoded_fil, rm_quoted_printable_encoded_fil, XX_FILE_TYPE_QUOTED_PRINTABLE_ENCODED_FIL },
    { "risc_os_sprite", mk_risc_os_sprite, rm_risc_os_sprite, XX_FILE_TYPE_RISC_OS_SPRITE },
    { "rpa", mk_rpa, rm_rpa, XX_FILE_TYPE_RPA },
    { "rpg_maker_rgssad", mk_rpg_maker_rgssad, rm_rpg_maker_rgssad, XX_FILE_TYPE_RPG_MAKER_RGSSAD },
    { "sfark_compressed_soundfont", mk_sfark_compressed_soundfont, rm_sfark_compressed_soundfont, XX_FILE_TYPE_SFARK_COMPRESSED_SOUNDFONT },
    { "sis", mk_sis, rm_sis, XX_FILE_TYPE_SIS },
    { "spectrum_udi", mk_spectrum_udi, rm_spectrum_udi, XX_FILE_TYPE_SPECTRUM_UDI },
    { "squashfs_sqlz", mk_squashfs_sqlz, rm_squashfs_sqlz, XX_FILE_TYPE_SQUASHFS_SQLZ },
    { "stos_memory_bank", mk_stos_memory_bank, rm_stos_memory_bank, XX_FILE_TYPE_STOS_MEMORY_BANK },
    { "stuffitx", mk_stuffitx, rm_stuffitx, XX_FILE_TYPE_STUFFITX },
    { "sufs", mk_sufs, rm_sufs, XX_FILE_TYPE_SUFS },
    { "sunvtoc", mk_sunvtoc, rm_sunvtoc, XX_FILE_TYPE_SUNVTOC },
    { "telltale_ttarch", mk_telltale_ttarch, rm_telltale_ttarch, XX_FILE_TYPE_TELLTALE_TTARCH },
    { "ufs2", mk_ufs2, rm_ufs2, XX_FILE_TYPE_UFS2 },
    { "uif", mk_uif, rm_uif, XX_FILE_TYPE_UIF },
    { "valve_gcf_cache", mk_valve_gcf_cache, rm_valve_gcf_cache, XX_FILE_TYPE_VALVE_GCF_CACHE },
    { "valve_xzp", mk_valve_xzp, rm_valve_xzp, XX_FILE_TYPE_VALVE_XZP },
    { "vmdk_cowd_sparse", mk_vmdk_cowd_sparse, rm_vmdk_cowd_sparse, XX_FILE_TYPE_VMDK_COWD_SPARSE },
    { "vmdk_sesparse", mk_vmdk_sesparse, rm_vmdk_sesparse, XX_FILE_TYPE_VMDK_SESPARSE },
    { "xiaomi_hdr1", mk_xiaomi_hdr1, rm_xiaomi_hdr1, XX_FILE_TYPE_XIAOMI_HDR1 },
    { "xiaomi_hdr2", mk_xiaomi_hdr2, rm_xiaomi_hdr2, XX_FILE_TYPE_XIAOMI_HDR2 },
    { "xp3", mk_xp3, rm_xp3, XX_FILE_TYPE_XP3 },
    { "xpk_compressed_file", mk_xpk_compressed_file, rm_xpk_compressed_file, XX_FILE_TYPE_XPK_COMPRESSED_FILE },
    { "yenc_encoded_file", mk_yenc_encoded_file, rm_yenc_encoded_file, XX_FILE_TYPE_YENC_ENCODED_FILE },
    { "ypf", mk_ypf, rm_ypf, XX_FILE_TYPE_YPF },

    { "advanced_installer_bootstrapper", mk_advanced_installer_bootstrapper, rm_advanced_installer_bootstrapper, XX_FILE_TYPE_UNKNOWN },
    { "apple_disk_copy_6_ndif_image", mk_apple_disk_copy_6_ndif_image, rm_apple_disk_copy_6_ndif_image, XX_FILE_TYPE_UNKNOWN },
    { "apple_sparse_bundle", mk_apple_sparse_bundle, rm_apple_sparse_bundle, XX_FILE_TYPE_UNKNOWN },
    { "ardi_installer", mk_ardi_installer, rm_ardi_installer, XX_FILE_TYPE_UNKNOWN },
    { "arni_installer_container", mk_arni_installer_container, rm_arni_installer_container, XX_FILE_TYPE_UNKNOWN },
    { "ej_technologies_install", mk_ej_technologies_install, rm_ej_technologies_install, XX_FILE_TYPE_UNKNOWN },
    { "encrypted_apple_disk_image", mk_encrypted_apple_disk_image, rm_encrypted_apple_disk_image, XX_FILE_TYPE_UNKNOWN },
    { "finstall", mk_finstall, rm_finstall, XX_FILE_TYPE_UNKNOWN },
    { "ghost_installer", mk_ghost_installer, rm_ghost_installer, XX_FILE_TYPE_UNKNOWN },
    { "hxc_stream_hfe", mk_hxc_stream_hfe, rm_hxc_stream_hfe, XX_FILE_TYPE_UNKNOWN },
    { "ibm_zpak_installer", mk_ibm_zpak_installer, rm_ibm_zpak_installer, XX_FILE_TYPE_UNKNOWN },
    { "ifah_installer", mk_ifah_installer, rm_ifah_installer, XX_FILE_TYPE_UNKNOWN },
    { "inno_setup", mk_inno_setup, rm_inno_setup, XX_FILE_TYPE_UNKNOWN },
    { "installer_vise_windows", mk_installer_vise_windows, rm_installer_vise_windows, XX_FILE_TYPE_UNKNOWN },
    { "installshield_12_setup", mk_installshield_12_setup, rm_installshield_12_setup, XX_FILE_TYPE_UNKNOWN },
    { "installshield_3", mk_installshield_3, rm_installshield_3, XX_FILE_TYPE_UNKNOWN },
    { "installshield_7_setup", mk_installshield_7_setup, rm_installshield_7_setup, XX_FILE_TYPE_UNKNOWN },
    { "installshield_7_setup2", mk_installshield_7_setup2, rm_installshield_7_setup2, XX_FILE_TYPE_UNKNOWN },
    { "installshield_developer", mk_installshield_developer, rm_installshield_developer, XX_FILE_TYPE_UNKNOWN },
    { "installshield_issetupstream", mk_installshield_issetupstream, rm_installshield_issetupstream, XX_FILE_TYPE_UNKNOWN },
    { "installshield_multiplatform", mk_installshield_multiplatform, rm_installshield_multiplatform, XX_FILE_TYPE_UNKNOWN },
    { "installshield_skin", mk_installshield_skin, rm_installshield_skin, XX_FILE_TYPE_UNKNOWN },
    { "microfox_put", mk_microfox_put, rm_microfox_put, XX_FILE_TYPE_UNKNOWN },
    { "ms_dos_backup", mk_ms_dos_backup, rm_ms_dos_backup, XX_FILE_TYPE_UNKNOWN },
    { "nec_pc_98_fdi", mk_nec_pc_98_fdi, rm_nec_pc_98_fdi, XX_FILE_TYPE_UNKNOWN },
    { "ns2", mk_ns2, rm_ns2, XX_FILE_TYPE_UNKNOWN },
    { "nsa", mk_nsa, rm_nsa, XX_FILE_TYPE_UNKNOWN },
    { "o_setup", mk_o_setup, rm_o_setup, XX_FILE_TYPE_UNKNOWN },
    { "pc_install_setup", mk_pc_install_setup, rm_pc_install_setup, XX_FILE_TYPE_UNKNOWN },
    { "pyinstaller_one_executable", mk_pyinstaller_one_executable, rm_pyinstaller_one_executable, XX_FILE_TYPE_UNKNOWN },
    { "qcow1", mk_qcow1, rm_qcow1, XX_FILE_TYPE_UNKNOWN },
    { "qnap_nas_firmware", mk_qnap_nas_firmware, rm_qnap_nas_firmware, XX_FILE_TYPE_UNKNOWN },
    { "qsetup_installation_suite", mk_qsetup_installation_suite, rm_qsetup_installation_suite, XX_FILE_TYPE_UNKNOWN },
    { "raw_deflate_compressed_data", mk_raw_deflate_compressed_data, rm_raw_deflate_compressed_data, XX_FILE_TYPE_UNKNOWN },
    { "rdb", mk_rdb, rm_rdb, XX_FILE_TYPE_UNKNOWN },
    { "rtpatch_setup_data", mk_rtpatch_setup_data, rm_rtpatch_setup_data, XX_FILE_TYPE_UNKNOWN },
    { "setup_factory", mk_setup_factory, rm_setup_factory, XX_FILE_TYPE_UNKNOWN },
    { "sfx_ebook_compiler_executables", mk_sfx_ebook_compiler_executables, rm_sfx_ebook_compiler_executables, XX_FILE_TYPE_UNKNOWN },
    { "spoon_installer", mk_spoon_installer, rm_spoon_installer, XX_FILE_TYPE_UNKNOWN },
    { "stuffit_split_file", mk_stuffit_split_file, rm_stuffit_split_file, XX_FILE_TYPE_UNKNOWN },
    { "t98_next_nfd", mk_t98_next_nfd, rm_t98_next_nfd, XX_FILE_TYPE_UNKNOWN },
    { "tarma_installer", mk_tarma_installer, rm_tarma_installer, XX_FILE_TYPE_UNKNOWN },
    { "adf", mk_adf, rm_adf, XX_FILE_TYPE_UNKNOWN },
    { "apm", mk_apm, rm_apm, XX_FILE_TYPE_UNKNOWN },
    { "trs_80_jv1", mk_trs_80_jv1, rm_trs_80_jv1, XX_FILE_TYPE_UNKNOWN },
    { "trs_80_jv3", mk_trs_80_jv3, rm_trs_80_jv3, XX_FILE_TYPE_UNKNOWN },
    { "uharc", mk_uharc, rm_uharc, XX_FILE_TYPE_UNKNOWN },
    { "vhdx", mk_vhdx, rm_vhdx, XX_FILE_TYPE_UNKNOWN },
    { "base64", mk_base64, rm_base64, XX_FILE_TYPE_UNKNOWN },
    { "btoa", mk_btoa, rm_btoa, XX_FILE_TYPE_UNKNOWN },
    { "chd", mk_chd, rm_chd, XX_FILE_TYPE_UNKNOWN },
    { "chm", mk_chm, rm_chm, XX_FILE_TYPE_UNKNOWN },
    { "cloop", mk_cloop, rm_cloop, XX_FILE_TYPE_UNKNOWN },
    { "cue", mk_cue, rm_cue, XX_FILE_TYPE_UNKNOWN },
    { "dahuazip", mk_dahuazip, rm_dahuazip, XX_FILE_TYPE_UNKNOWN },
    { "dmsfw", mk_dmsfw, rm_dmsfw, XX_FILE_TYPE_UNKNOWN },
    { "ewf", mk_ewf, rm_ewf, XX_FILE_TYPE_UNKNOWN },
    { "godot_engine_pck", mk_godot_engine_pck, rm_godot_engine_pck, XX_FILE_TYPE_UNKNOWN },
    { "gpgsigned", mk_gpgsigned, rm_gpgsigned, XX_FILE_TYPE_UNKNOWN },
    { "ihex", mk_ihex, rm_ihex, XX_FILE_TYPE_UNKNOWN },
    { "kwaj", mk_kwaj, rm_kwaj, XX_FILE_TYPE_UNKNOWN },
    { "lbr", mk_lbr, rm_lbr, XX_FILE_TYPE_UNKNOWN },
    { "lzfsestream", mk_lzfsestream, rm_lzfsestream, XX_FILE_TYPE_UNKNOWN },
    { "nrg", mk_nrg, rm_nrg, XX_FILE_TYPE_UNKNOWN },
    { "packit_mac", mk_packit_mac, rm_packit_mac, XX_FILE_TYPE_UNKNOWN },
    { "rpm", mk_rpm, rm_rpm, XX_FILE_TYPE_UNKNOWN },
    { "stuffit5", mk_stuffit5, rm_stuffit5, XX_FILE_TYPE_UNKNOWN },
    { "act_apricot_pc_xi_raw", mk_act_apricot_pc_xi_raw, rm_act_apricot_pc_xi_raw, XX_FILE_TYPE_UNKNOWN },
    { "adam", mk_adam, rm_adam, XX_FILE_TYPE_UNKNOWN },
    { "base16", mk_base16, rm_base16, XX_FILE_TYPE_UNKNOWN },
    { "bondwell_2_disk", mk_bondwell_2_disk, rm_bondwell_2_disk, XX_FILE_TYPE_UNKNOWN },
    { "casio_fz_1_disk", mk_casio_fz_1_disk, rm_casio_fz_1_disk, XX_FILE_TYPE_UNKNOWN },
    { "isz", mk_isz, rm_isz, XX_FILE_TYPE_UNKNOWN },
    { "mame_floppy_image_mfi", mk_mame_floppy_image_mfi, rm_mame_floppy_image_mfi, XX_FILE_TYPE_UNKNOWN },
    { "parallels_hdd", mk_parallels_hdd, rm_parallels_hdd, XX_FILE_TYPE_UNKNOWN },
    { "pc_magazine_flp", mk_pc_magazine_flp, rm_pc_magazine_flp, XX_FILE_TYPE_UNKNOWN },
    { "pchrom", mk_pchrom, rm_pchrom, XX_FILE_TYPE_UNKNOWN },
    { "pem", mk_pem, rm_pem, XX_FILE_TYPE_UNKNOWN },
    { "prodos", mk_prodos, rm_prodos, XX_FILE_TYPE_UNKNOWN },
    { "qemu_enhanced_disk", mk_qemu_enhanced_disk, rm_qemu_enhanced_disk, XX_FILE_TYPE_UNKNOWN },
    { "rawcd", mk_rawcd, rm_rawcd, XX_FILE_TYPE_UNKNOWN },
    { "rsdos_fs", mk_rsdos_fs, rm_rsdos_fs, XX_FILE_TYPE_UNKNOWN },
    { "sar_ns", mk_sar_ns, rm_sar_ns, XX_FILE_TYPE_UNKNOWN },
    { "swf", mk_swf, rm_swf, XX_FILE_TYPE_UNKNOWN },
    { "t64", mk_t64, rm_t64, XX_FILE_TYPE_UNKNOWN },
    { "uue", mk_uue, rm_uue, XX_FILE_TYPE_UNKNOWN },
    { "vdi", mk_vdi, rm_vdi, XX_FILE_TYPE_UNKNOWN },
    { "bmp", mk_bmp, rm_bmp, XX_FILE_TYPE_UNKNOWN },
    { "cfe", mk_cfe, rm_cfe, XX_FILE_TYPE_UNKNOWN },
    { "dxbc", mk_dxbc, rm_dxbc, XX_FILE_TYPE_UNKNOWN },
    { "gif", mk_gif, rm_gif, XX_FILE_TYPE_UNKNOWN },
    { "jpeg", mk_jpeg, rm_jpeg, XX_FILE_TYPE_UNKNOWN },
    { "linuxarm64", mk_linuxarm64, rm_linuxarm64, XX_FILE_TYPE_UNKNOWN },
    { "linuxboot", mk_linuxboot, rm_linuxboot, XX_FILE_TYPE_UNKNOWN },
    { "linuxzimage", mk_linuxzimage, rm_linuxzimage, XX_FILE_TYPE_UNKNOWN },
    { "pcapng", mk_pcapng, rm_pcapng, XX_FILE_TYPE_UNKNOWN },
    { "pjl", mk_pjl, rm_pjl, XX_FILE_TYPE_UNKNOWN },
    { "png", mk_png, rm_png, XX_FILE_TYPE_UNKNOWN },
    { "riff", mk_riff, rm_riff, XX_FILE_TYPE_UNKNOWN },
    { "svg", mk_svg, rm_svg, XX_FILE_TYPE_UNKNOWN },
    { "quake_pak", mk_quake_pak, rm_quake_pak, XX_FILE_TYPE_UNKNOWN },
    { "doom_wad", mk_doom_wad, rm_doom_wad, XX_FILE_TYPE_UNKNOWN },
    { "quake_wad2", mk_quake_wad2, rm_quake_wad2, XX_FILE_TYPE_UNKNOWN },
    { "halflife_wad3", mk_halflife_wad3, rm_halflife_wad3, XX_FILE_TYPE_UNKNOWN },
    { "build_grp", mk_build_grp, rm_build_grp, XX_FILE_TYPE_UNKNOWN },
    { "cri_afs", mk_cri_afs, rm_cri_afs, XX_FILE_TYPE_UNKNOWN },
    { "cri_awb", mk_cri_awb, rm_cri_awb, XX_FILE_TYPE_UNKNOWN },
    { "valve_vpk", mk_valve_vpk, rm_valve_vpk, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_u8", mk_nintendo_u8, rm_nintendo_u8, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_rarc", mk_nintendo_rarc, rm_nintendo_rarc, XX_FILE_TYPE_UNKNOWN },
    { "android_ab", mk_android_ab, rm_android_ab, XX_FILE_TYPE_UNKNOWN },
    { "nes_rom", mk_nes_rom, rm_nes_rom, XX_FILE_TYPE_UNKNOWN },
    { "lynx_lnx", mk_lynx_lnx, rm_lynx_lnx, XX_FILE_TYPE_UNKNOWN },
    { "commodore_crt", mk_commodore_crt, rm_commodore_crt, XX_FILE_TYPE_UNKNOWN },
    { "uf2", mk_uf2, rm_uf2, XX_FILE_TYPE_UNKNOWN },
    { "ico", mk_ico, rm_ico, XX_FILE_TYPE_UNKNOWN },
    { "midi", mk_midi, rm_midi, XX_FILE_TYPE_UNKNOWN },
    { "bethesda_bsa", mk_bethesda_bsa, rm_bethesda_bsa, XX_FILE_TYPE_UNKNOWN },
    { "bethesda_ba2", mk_bethesda_ba2, rm_bethesda_ba2, XX_FILE_TYPE_UNKNOWN },
    { "unityfs", mk_unityfs, rm_unityfs, XX_FILE_TYPE_UNKNOWN },
    { "bioware_biff", mk_bioware_biff, rm_bioware_biff, XX_FILE_TYPE_UNKNOWN },
    { "bioware_erf", mk_bioware_erf, rm_bioware_erf, XX_FILE_TYPE_UNKNOWN },
    { "bioware_rim", mk_bioware_rim, rm_bioware_rim, XX_FILE_TYPE_UNKNOWN },
    { "lucas_lab", mk_lucas_lab, rm_lucas_lab, XX_FILE_TYPE_UNKNOWN },
    { "lucas_bun", mk_lucas_bun, rm_lucas_bun, XX_FILE_TYPE_UNKNOWN },
    { "idtech_bsp", mk_idtech_bsp, rm_idtech_bsp, XX_FILE_TYPE_UNKNOWN },
    { "valve_bsp", mk_valve_bsp, rm_valve_bsp, XX_FILE_TYPE_UNKNOWN },
    { "idtech_md2", mk_idtech_md2, rm_idtech_md2, XX_FILE_TYPE_UNKNOWN },
    { "idtech_md3", mk_idtech_md3, rm_idtech_md3, XX_FILE_TYPE_UNKNOWN },
    { "idtech_qvm", mk_idtech_qvm, rm_idtech_qvm, XX_FILE_TYPE_UNKNOWN },
    { "mohawk_mhk", mk_mohawk_mhk, rm_mohawk_mhk, XX_FILE_TYPE_UNKNOWN },
    { "quake_sprite", mk_quake_sprite, rm_quake_sprite, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_narc", mk_nintendo_narc, rm_nintendo_narc, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_sarc", mk_nintendo_sarc, rm_nintendo_sarc, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_pfs0", mk_nintendo_pfs0, rm_nintendo_pfs0, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_hfs0", mk_nintendo_hfs0, rm_nintendo_hfs0, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_brres", mk_nintendo_brres, rm_nintendo_brres, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bcstm", mk_nintendo_bcstm, rm_nintendo_bcstm, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bfsar", mk_nintendo_bfsar, rm_nintendo_bfsar, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bcsar", mk_nintendo_bcsar, rm_nintendo_bcsar, XX_FILE_TYPE_UNKNOWN },
    { "sony_psarc", mk_sony_psarc, rm_sony_psarc, XX_FILE_TYPE_UNKNOWN },
    { "ktx", mk_ktx, rm_ktx, XX_FILE_TYPE_UNKNOWN },
    { "ktx2", mk_ktx2, rm_ktx2, XX_FILE_TYPE_UNKNOWN },
    { "dds", mk_dds, rm_dds, XX_FILE_TYPE_UNKNOWN },
    { "pvr", mk_pvr, rm_pvr, XX_FILE_TYPE_UNKNOWN },
    { "valve_vtf", mk_valve_vtf, rm_valve_vtf, XX_FILE_TYPE_UNKNOWN },
    { "visionaire_studio_vis", mk_visionaire_studio_vis, rm_visionaire_studio_vis, XX_FILE_TYPE_UNKNOWN },
    { "x68000_dim", mk_x68000_dim, rm_x68000_dim, XX_FILE_TYPE_UNKNOWN },
    { "xamarin_compressed_assembly", mk_xamarin_compressed_assembly, rm_xamarin_compressed_assembly, XX_FILE_TYPE_UNKNOWN },
    { "xbox_xbe", mk_xbox_xbe, rm_xbox_xbe, XX_FILE_TYPE_UNKNOWN },
    { "flac", mk_flac, rm_flac, XX_FILE_TYPE_UNKNOWN },
    { "ogg", mk_ogg, rm_ogg, XX_FILE_TYPE_UNKNOWN },
    { "mp4", mk_mp4, rm_mp4, XX_FILE_TYPE_UNKNOWN },
    { "matroska", mk_matroska, rm_matroska, XX_FILE_TYPE_UNKNOWN },
    { "aiff", mk_aiff, rm_aiff, XX_FILE_TYPE_UNKNOWN },
    { "caf", mk_caf, rm_caf, XX_FILE_TYPE_UNKNOWN },
    { "photoshop_psd", mk_photoshop_psd, rm_photoshop_psd, XX_FILE_TYPE_UNKNOWN },
    { "tiff", mk_tiff, rm_tiff, XX_FILE_TYPE_UNKNOWN },
    { "openexr", mk_openexr, rm_openexr, XX_FILE_TYPE_UNKNOWN },
    { "jpeg2000_jp2", mk_jpeg2000_jp2, rm_jpeg2000_jp2, XX_FILE_TYPE_UNKNOWN },
    { "android_vendor_boot", mk_android_vendor_boot, rm_android_vendor_boot, XX_FILE_TYPE_UNKNOWN },
    { "android_dtbo", mk_android_dtbo, rm_android_dtbo, XX_FILE_TYPE_UNKNOWN },
    { "android_vbmeta", mk_android_vbmeta, rm_android_vbmeta, XX_FILE_TYPE_UNKNOWN },
    { "espressif_image", mk_espressif_image, rm_espressif_image, XX_FILE_TYPE_UNKNOWN },
    { "wasm", mk_wasm, rm_wasm, XX_FILE_TYPE_UNKNOWN },
    { "llvm_bitcode_wrapper", mk_llvm_bitcode_wrapper, rm_llvm_bitcode_wrapper, XX_FILE_TYPE_UNKNOWN },
    { "dotnet_metadata", mk_dotnet_metadata, rm_dotnet_metadata, XX_FILE_TYPE_UNKNOWN },
    { "sfnt", mk_sfnt, rm_sfnt, XX_FILE_TYPE_UNKNOWN },
    { "woff", mk_woff, rm_woff, XX_FILE_TYPE_UNKNOWN },
    { "woff2", mk_woff2, rm_woff2, XX_FILE_TYPE_UNKNOWN },
    { "makeself", mk_makeself, rm_makeself, XX_FILE_TYPE_UNKNOWN },
    { "sun_java_binsh", mk_sun_java_binsh, rm_sun_java_binsh, XX_FILE_TYPE_UNKNOWN },
    { "installanywhere_unix", mk_installanywhere_unix, rm_installanywhere_unix, XX_FILE_TYPE_UNKNOWN },
    { "sfx_packagefortheweb", mk_sfx_packagefortheweb, rm_sfx_packagefortheweb, XX_FILE_TYPE_UNKNOWN },
    { "sfx_spis", mk_sfx_spis, rm_sfx_spis, XX_FILE_TYPE_UNKNOWN },
    { "sfx_lha", mk_sfx_lha, rm_sfx_lha, XX_FILE_TYPE_UNKNOWN },
    { "lmd_container", mk_lmd_container, rm_lmd_container, XX_FILE_TYPE_UNKNOWN },
    { "totalannihilation_hpi", mk_totalannihilation_hpi, rm_totalannihilation_hpi, XX_FILE_TYPE_UNKNOWN },
    { "ravensoft_rff", mk_ravensoft_rff, rm_ravensoft_rff, XX_FILE_TYPE_UNKNOWN },
    { "terminalreality_pod", mk_terminalreality_pod, rm_terminalreality_pod, XX_FILE_TYPE_UNKNOWN },
    { "volition_vpp", mk_volition_vpp, rm_volition_vpp, XX_FILE_TYPE_UNKNOWN },
    { "kirikiri_xp3", mk_kirikiri_xp3, rm_kirikiri_xp3, XX_FILE_TYPE_UNKNOWN },
    { "fromsoftware_binder", mk_fromsoftware_binder, rm_fromsoftware_binder, XX_FILE_TYPE_UNKNOWN },
    { "mythic_myp", mk_mythic_myp, rm_mythic_myp, XX_FILE_TYPE_UNKNOWN },
    { "lithtech_rez", mk_lithtech_rez, rm_lithtech_rez, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_ncch", mk_nintendo_ncch, rm_nintendo_ncch, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_ncsd", mk_nintendo_ncsd, rm_nintendo_ncsd, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_cia", mk_nintendo_cia, rm_nintendo_cia, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_nds", mk_nintendo_nds, rm_nintendo_nds, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_gcm", mk_nintendo_gcm, rm_nintendo_gcm, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_tpl", mk_nintendo_tpl, rm_nintendo_tpl, XX_FILE_TYPE_UNKNOWN },
    { "sony_tim", mk_sony_tim, rm_sony_tim, XX_FILE_TYPE_UNKNOWN },
    { "sony_vag", mk_sony_vag, rm_sony_vag, XX_FILE_TYPE_UNKNOWN },
    { "larian_lspk", mk_larian_lspk, rm_larian_lspk, XX_FILE_TYPE_UNKNOWN },
    { "larian_lsf", mk_larian_lsf, rm_larian_lsf, XX_FILE_TYPE_UNKNOWN },
    { "valve_hpak", mk_valve_hpak, rm_valve_hpak, XX_FILE_TYPE_UNKNOWN },
    { "renpy_rpa", mk_renpy_rpa, rm_renpy_rpa, XX_FILE_TYPE_UNKNOWN },
    { "unreal_package", mk_unreal_package, rm_unreal_package, XX_FILE_TYPE_UNKNOWN },
    { "sega_pvr2", mk_sega_pvr2, rm_sega_pvr2, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bntx", mk_nintendo_bntx, rm_nintendo_bntx, XX_FILE_TYPE_UNKNOWN },
    { "icns", mk_icns, rm_icns, XX_FILE_TYPE_UNKNOWN },
    { "xcursor", mk_xcursor, rm_xcursor, XX_FILE_TYPE_UNKNOWN },
    { "icc", mk_icc, rm_icc, XX_FILE_TYPE_UNKNOWN },
    { "qoi", mk_qoi, rm_qoi, XX_FILE_TYPE_UNKNOWN },
    { "farbfeld", mk_farbfeld, rm_farbfeld, XX_FILE_TYPE_UNKNOWN },
    { "pnm", mk_pnm, rm_pnm, XX_FILE_TYPE_UNKNOWN },
    { "tga", mk_tga, rm_tga, XX_FILE_TYPE_UNKNOWN },
    { "sun_raster", mk_sun_raster, rm_sun_raster, XX_FILE_TYPE_UNKNOWN },
    { "fits", mk_fits, rm_fits, XX_FILE_TYPE_UNKNOWN },
    { "dicom", mk_dicom, rm_dicom, XX_FILE_TYPE_UNKNOWN },
    { "pcap", mk_pcap, rm_pcap, XX_FILE_TYPE_UNKNOWN },
    { "btsnoop", mk_btsnoop, rm_btsnoop, XX_FILE_TYPE_UNKNOWN },
    { "java_class", mk_java_class, rm_java_class, XX_FILE_TYPE_UNKNOWN },
    { "sfnt_collection", mk_sfnt_collection, rm_sfnt_collection, XX_FILE_TYPE_UNKNOWN },
    { "sqlite3", mk_sqlite3, rm_sqlite3, XX_FILE_TYPE_UNKNOWN },
    { "sqlite_wal", mk_sqlite_wal, rm_sqlite_wal, XX_FILE_TYPE_UNKNOWN },
    { "avro_object", mk_avro_object, rm_avro_object, XX_FILE_TYPE_UNKNOWN },
    { "glb", mk_glb, rm_glb, XX_FILE_TYPE_UNKNOWN },
    { "spirv", mk_spirv, rm_spirv, XX_FILE_TYPE_UNKNOWN },
    { "crx", mk_crx, rm_crx, XX_FILE_TYPE_UNKNOWN },
    { "sfx_arc", mk_sfx_arc, rm_sfx_arc, XX_FILE_TYPE_UNKNOWN },
    { "sfx_arj", mk_sfx_arj, rm_sfx_arj, XX_FILE_TYPE_UNKNOWN },
    { "sfx_bsn", mk_sfx_bsn, rm_sfx_bsn, XX_FILE_TYPE_UNKNOWN },
    { "sfx_arq", mk_sfx_arq, rm_sfx_arq, XX_FILE_TYPE_UNKNOWN },
    { "sfx_gxl", mk_sfx_gxl, rm_sfx_gxl, XX_FILE_TYPE_UNKNOWN },
    { "sfx_asymetrix", mk_sfx_asymetrix, rm_sfx_asymetrix, XX_FILE_TYPE_UNKNOWN },
    { "sfx_rta", mk_sfx_rta, rm_sfx_rta, XX_FILE_TYPE_UNKNOWN },
    { "sfx_rtpatch", mk_sfx_rtpatch, rm_sfx_rtpatch, XX_FILE_TYPE_UNKNOWN },
    { "esp_archive", mk_esp_archive, rm_esp_archive, XX_FILE_TYPE_UNKNOWN },
    { "sfx_kwaj", mk_sfx_kwaj, rm_sfx_kwaj, XX_FILE_TYPE_UNKNOWN },
    { "gemdos_lha", mk_gemdos_lha, rm_gemdos_lha, XX_FILE_TYPE_UNKNOWN },
    { "winimage_zip", mk_winimage_zip, rm_winimage_zip, XX_FILE_TYPE_UNKNOWN },
    { "hp3000_wrq", mk_hp3000_wrq, rm_hp3000_wrq, XX_FILE_TYPE_UNKNOWN },
    { "icu_data_package", mk_icu_data_package, rm_icu_data_package, XX_FILE_TYPE_UNKNOWN },
    { "sfx_sqz", mk_sfx_sqz, rm_sfx_sqz, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bfstm", mk_nintendo_bfstm, rm_nintendo_bfstm, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bfwav", mk_nintendo_bfwav, rm_nintendo_bfwav, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bcwav", mk_nintendo_bcwav, rm_nintendo_bcwav, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bfres", mk_nintendo_bfres, rm_nintendo_bfres, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bflyt", mk_nintendo_bflyt, rm_nintendo_bflyt, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bclyt", mk_nintendo_bclyt, rm_nintendo_bclyt, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bfnt", mk_nintendo_bfnt, rm_nintendo_bfnt, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bcfnt", mk_nintendo_bcfnt, rm_nintendo_bcfnt, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_3dsx", mk_nintendo_3dsx, rm_nintendo_3dsx, XX_FILE_TYPE_UNKNOWN },
    { "sony_tim2", mk_sony_tim2, rm_sony_tim2, XX_FILE_TYPE_UNKNOWN },
    { "sony_pamf", mk_sony_pamf, rm_sony_pamf, XX_FILE_TYPE_UNKNOWN },
    { "sega_gvr", mk_sega_gvr, rm_sega_gvr, XX_FILE_TYPE_UNKNOWN },
    { "microsoft_xwb", mk_microsoft_xwb, rm_microsoft_xwb, XX_FILE_TYPE_UNKNOWN },
    { "microsoft_xsb", mk_microsoft_xsb, rm_microsoft_xsb, XX_FILE_TYPE_UNKNOWN },
    { "relic_sga", mk_relic_sga, rm_relic_sga, XX_FILE_TYPE_UNKNOWN },
    { "xpm", mk_xpm, rm_xpm, XX_FILE_TYPE_UNKNOWN },
    { "pcx", mk_pcx, rm_pcx, XX_FILE_TYPE_UNKNOWN },
    { "iff_ilbm", mk_iff_ilbm, rm_iff_ilbm, XX_FILE_TYPE_UNKNOWN },
    { "utah_rle", mk_utah_rle, rm_utah_rle, XX_FILE_TYPE_UNKNOWN },
    { "radiance_hdr", mk_radiance_hdr, rm_radiance_hdr, XX_FILE_TYPE_UNKNOWN },
    { "dpx", mk_dpx, rm_dpx, XX_FILE_TYPE_UNKNOWN },
    { "cineon", mk_cineon, rm_cineon, XX_FILE_TYPE_UNKNOWN },
    { "xwd", mk_xwd, rm_xwd, XX_FILE_TYPE_UNKNOWN },
    { "sgi_rgb", mk_sgi_rgb, rm_sgi_rgb, XX_FILE_TYPE_UNKNOWN },
    { "aseprite", mk_aseprite, rm_aseprite, XX_FILE_TYPE_UNKNOWN },
    { "numpy_npy", mk_numpy_npy, rm_numpy_npy, XX_FILE_TYPE_UNKNOWN },
    { "matlab_mat5", mk_matlab_mat5, rm_matlab_mat5, XX_FILE_TYPE_UNKNOWN },
    { "netcdf_classic", mk_netcdf_classic, rm_netcdf_classic, XX_FILE_TYPE_UNKNOWN },
    { "hdf4", mk_hdf4, rm_hdf4, XX_FILE_TYPE_UNKNOWN },
    { "dbase_dbf", mk_dbase_dbf, rm_dbase_dbf, XX_FILE_TYPE_UNKNOWN },
    { "sas_xport", mk_sas_xport, rm_sas_xport, XX_FILE_TYPE_UNKNOWN },
    { "spss_sav", mk_spss_sav, rm_spss_sav, XX_FILE_TYPE_UNKNOWN },
    { "stata_dta", mk_stata_dta, rm_stata_dta, XX_FILE_TYPE_UNKNOWN },
    { "apache_arrow_file", mk_apache_arrow_file, rm_apache_arrow_file, XX_FILE_TYPE_UNKNOWN },
    { "apache_parquet", mk_apache_parquet, rm_apache_parquet, XX_FILE_TYPE_UNKNOWN },
    { "sfx_arcv2", mk_sfx_arcv2, rm_sfx_arcv2, XX_FILE_TYPE_UNKNOWN },
    { "sfx_chz", mk_sfx_chz, rm_sfx_chz, XX_FILE_TYPE_UNKNOWN },
    { "sfx_szdd", mk_sfx_szdd, rm_sfx_szdd, XX_FILE_TYPE_UNKNOWN },
    { "sfx_mpq", mk_sfx_mpq, rm_sfx_mpq, XX_FILE_TYPE_UNKNOWN },
    { "sfx_swag", mk_sfx_swag, rm_sfx_swag, XX_FILE_TYPE_UNKNOWN },
    { "sfx_zpak", mk_sfx_zpak, rm_sfx_zpak, XX_FILE_TYPE_UNKNOWN },
    { "sfx_diskexpress", mk_sfx_diskexpress, rm_sfx_diskexpress, XX_FILE_TYPE_UNKNOWN },
    { "sfx_bzip2", mk_sfx_bzip2, rm_sfx_bzip2, XX_FILE_TYPE_UNKNOWN },
    { "sfx_gzip", mk_sfx_gzip, rm_sfx_gzip, XX_FILE_TYPE_UNKNOWN },
    { "sfx_tar", mk_sfx_tar, rm_sfx_tar, XX_FILE_TYPE_UNKNOWN },
    { "sfx_cab", mk_sfx_cab, rm_sfx_cab, XX_FILE_TYPE_UNKNOWN },
    { "pmarc_sfx", mk_pmarc_sfx, rm_pmarc_sfx, XX_FILE_TYPE_UNKNOWN },
    { "sfx_7zip", mk_sfx_7zip, rm_sfx_7zip, XX_FILE_TYPE_UNKNOWN },
    { "sfx_ace", mk_sfx_ace, rm_sfx_ace, XX_FILE_TYPE_UNKNOWN },
    { "sfx_zipcentral", mk_sfx_zipcentral, rm_sfx_zipcentral, XX_FILE_TYPE_UNKNOWN },
    { "sony_psx_exe", mk_sony_psx_exe, rm_sony_psx_exe, XX_FILE_TYPE_UNKNOWN },
    { "sony_psf", mk_sony_psf, rm_sony_psf, XX_FILE_TYPE_UNKNOWN },
    { "xbox_xdvdfs", mk_xbox_xdvdfs, rm_xbox_xdvdfs, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_wbfs", mk_nintendo_wbfs, rm_nintendo_wbfs, XX_FILE_TYPE_UNKNOWN },
    { "godot_ctex", mk_godot_ctex, rm_godot_ctex, XX_FILE_TYPE_UNKNOWN },
    { "unity_serialized", mk_unity_serialized, rm_unity_serialized, XX_FILE_TYPE_UNKNOWN },
    { "idtech_mdl", mk_idtech_mdl, rm_idtech_mdl, XX_FILE_TYPE_UNKNOWN },
    { "valve_studio_mdl", mk_valve_studio_mdl, rm_valve_studio_mdl, XX_FILE_TYPE_UNKNOWN },
    { "blitz3d_b3d", mk_blitz3d_b3d, rm_blitz3d_b3d, XX_FILE_TYPE_UNKNOWN },
    { "milkshape_ms3d", mk_milkshape_ms3d, rm_milkshape_ms3d, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bch", mk_nintendo_bch, rm_nintendo_bch, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_cgfx", mk_nintendo_cgfx, rm_nintendo_cgfx, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_byaml", mk_nintendo_byaml, rm_nintendo_byaml, XX_FILE_TYPE_UNKNOWN },
    { "relic_chunky", mk_relic_chunky, rm_relic_chunky, XX_FILE_TYPE_UNKNOWN },
    { "ogre_mesh", mk_ogre_mesh, rm_ogre_mesh, XX_FILE_TYPE_UNKNOWN },
    { "adobe_ase", mk_adobe_ase, rm_adobe_ase, XX_FILE_TYPE_UNKNOWN },
    { "adobe_aco", mk_adobe_aco, rm_adobe_aco, XX_FILE_TYPE_UNKNOWN },
    { "gimp_gbr", mk_gimp_gbr, rm_gimp_gbr, XX_FILE_TYPE_UNKNOWN },
    { "gimp_gih", mk_gimp_gih, rm_gimp_gih, XX_FILE_TYPE_UNKNOWN },
    { "gimp_pat", mk_gimp_pat, rm_gimp_pat, XX_FILE_TYPE_UNKNOWN },
    { "jbig2", mk_jbig2, rm_jbig2, XX_FILE_TYPE_UNKNOWN },
    { "djvu", mk_djvu, rm_djvu, XX_FILE_TYPE_UNKNOWN },
    { "emf", mk_emf, rm_emf, XX_FILE_TYPE_UNKNOWN },
    { "wmf", mk_wmf, rm_wmf, XX_FILE_TYPE_UNKNOWN },
    { "xfig", mk_xfig, rm_xfig, XX_FILE_TYPE_UNKNOWN },
    { "nifti1", mk_nifti1, rm_nifti1, XX_FILE_TYPE_UNKNOWN },
    { "nrrd", mk_nrrd, rm_nrrd, XX_FILE_TYPE_UNKNOWN },
    { "mrc", mk_mrc, rm_mrc, XX_FILE_TYPE_UNKNOWN },
    { "metaimage", mk_metaimage, rm_metaimage, XX_FILE_TYPE_UNKNOWN },
    { "vtk_legacy", mk_vtk_legacy, rm_vtk_legacy, XX_FILE_TYPE_UNKNOWN },
    { "gipl", mk_gipl, rm_gipl, XX_FILE_TYPE_UNKNOWN },
    { "freesurfer_mgh", mk_freesurfer_mgh, rm_freesurfer_mgh, XX_FILE_TYPE_UNKNOWN },
    { "edf", mk_edf, rm_edf, XX_FILE_TYPE_UNKNOWN },
    { "fcs", mk_fcs, rm_fcs, XX_FILE_TYPE_UNKNOWN },
    { "tensorflow_tfrecord", mk_tensorflow_tfrecord, rm_tensorflow_tfrecord, XX_FILE_TYPE_UNKNOWN },
    { "sfx_imp", mk_sfx_imp, rm_sfx_imp, XX_FILE_TYPE_UNKNOWN },
    { "sfx_red", mk_sfx_red, rm_sfx_red, XX_FILE_TYPE_UNKNOWN },
    { "sfx_ha", mk_sfx_ha, rm_sfx_ha, XX_FILE_TYPE_UNKNOWN },
    { "sfx_lzx", mk_sfx_lzx, rm_sfx_lzx, XX_FILE_TYPE_UNKNOWN },
    { "sfx_sqx", mk_sfx_sqx, rm_sfx_sqx, XX_FILE_TYPE_UNKNOWN },
    { "sfx_ain", mk_sfx_ain, rm_sfx_ain, XX_FILE_TYPE_UNKNOWN },
    { "sfx_hap", mk_sfx_hap, rm_sfx_hap, XX_FILE_TYPE_UNKNOWN },
    { "sfx_zoo", mk_sfx_zoo, rm_sfx_zoo, XX_FILE_TYPE_UNKNOWN },
    { "sfx_cazip", mk_sfx_cazip, rm_sfx_cazip, XX_FILE_TYPE_UNKNOWN },
    { "sfx_tgcf", mk_sfx_tgcf, rm_sfx_tgcf, XX_FILE_TYPE_UNKNOWN },
    { "sfx_starkit", mk_sfx_starkit, rm_sfx_starkit, XX_FILE_TYPE_UNKNOWN },
    { "sfx_alz", mk_sfx_alz, rm_sfx_alz, XX_FILE_TYPE_UNKNOWN },
    { "sfx_chm", mk_sfx_chm, rm_sfx_chm, XX_FILE_TYPE_UNKNOWN },
    { "egg", mk_egg, rm_egg, XX_FILE_TYPE_UNKNOWN },
    { "nufx", mk_nufx, rm_nufx, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_dol", mk_nintendo_dol, rm_nintendo_dol, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_j3d_bmd", mk_nintendo_j3d_bmd, rm_nintendo_j3d_bmd, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_j3d_btk", mk_nintendo_j3d_btk, rm_nintendo_j3d_btk, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_brstm", mk_nintendo_brstm, rm_nintendo_brstm, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_brwav", mk_nintendo_brwav, rm_nintendo_brwav, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_brlyt", mk_nintendo_brlyt, rm_nintendo_brlyt, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_brlan", mk_nintendo_brlan, rm_nintendo_brlan, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_bfsha", mk_nintendo_bfsha, rm_nintendo_bfsha, XX_FILE_TYPE_UNKNOWN },
    { "cri_usm", mk_cri_usm, rm_cri_usm, XX_FILE_TYPE_UNKNOWN },
    { "cri_utf", mk_cri_utf, rm_cri_utf, XX_FILE_TYPE_UNKNOWN },
    { "idtech_iqm", mk_idtech_iqm, rm_idtech_iqm, XX_FILE_TYPE_UNKNOWN },
    { "unreal_psk", mk_unreal_psk, rm_unreal_psk, XX_FILE_TYPE_UNKNOWN },
    { "unreal_psa", mk_unreal_psa, rm_unreal_psa, XX_FILE_TYPE_UNKNOWN },
    { "torque_dts", mk_torque_dts, rm_torque_dts, XX_FILE_TYPE_UNKNOWN },
    { "magicavoxel_vox", mk_magicavoxel_vox, rm_magicavoxel_vox, XX_FILE_TYPE_UNKNOWN },
    { "audio_au", mk_audio_au, rm_audio_au, XX_FILE_TYPE_UNKNOWN },
    { "creative_voc", mk_creative_voc, rm_creative_voc, XX_FILE_TYPE_UNKNOWN },
    { "tracker_xm", mk_tracker_xm, rm_tracker_xm, XX_FILE_TYPE_UNKNOWN },
    { "tracker_s3m", mk_tracker_s3m, rm_tracker_s3m, XX_FILE_TYPE_UNKNOWN },
    { "tracker_it", mk_tracker_it, rm_tracker_it, XX_FILE_TYPE_UNKNOWN },
    { "tracker_mtm", mk_tracker_mtm, rm_tracker_mtm, XX_FILE_TYPE_UNKNOWN },
    { "tracker_stm", mk_tracker_stm, rm_tracker_stm, XX_FILE_TYPE_UNKNOWN },
    { "tracker_669", mk_tracker_669, rm_tracker_669, XX_FILE_TYPE_UNKNOWN },
    { "tracker_ult", mk_tracker_ult, rm_tracker_ult, XX_FILE_TYPE_UNKNOWN },
    { "tracker_okt", mk_tracker_okt, rm_tracker_okt, XX_FILE_TYPE_UNKNOWN },
    { "nifti2", mk_nifti2, rm_nifti2, XX_FILE_TYPE_UNKNOWN },
    { "lidar_las", mk_lidar_las, rm_lidar_las, XX_FILE_TYPE_UNKNOWN },
    { "esri_shp", mk_esri_shp, rm_esri_shp, XX_FILE_TYPE_UNKNOWN },
    { "polygon_ply", mk_polygon_ply, rm_polygon_ply, XX_FILE_TYPE_UNKNOWN },
    { "pointcloud_pcd", mk_pointcloud_pcd, rm_pointcloud_pcd, XX_FILE_TYPE_UNKNOWN },
    { "matlab_mat4", mk_matlab_mat4, rm_matlab_mat4, XX_FILE_TYPE_UNKNOWN },
    { "seismic_segy", mk_seismic_segy, rm_seismic_segy, XX_FILE_TYPE_UNKNOWN },
    { "biomedical_bdf", mk_biomedical_bdf, rm_biomedical_bdf, XX_FILE_TYPE_UNKNOWN },
    { "erlang_beam", mk_erlang_beam, rm_erlang_beam, XX_FILE_TYPE_UNKNOWN },
    { "java_jmod", mk_java_jmod, rm_java_jmod, XX_FILE_TYPE_UNKNOWN },
    { "tracker_liquid", mk_tracker_liquid, rm_tracker_liquid, XX_FILE_TYPE_UNKNOWN },
    { "tracker_dmf", mk_tracker_dmf, rm_tracker_dmf, XX_FILE_TYPE_UNKNOWN },
    { "tracker_ptm", mk_tracker_ptm, rm_tracker_ptm, XX_FILE_TYPE_UNKNOWN },
    { "tracker_ams", mk_tracker_ams, rm_tracker_ams, XX_FILE_TYPE_UNKNOWN },
    { "tracker_digi", mk_tracker_digi, rm_tracker_digi, XX_FILE_TYPE_UNKNOWN },
    { "tracker_emod", mk_tracker_emod, rm_tracker_emod, XX_FILE_TYPE_UNKNOWN },
    { "tracker_mt2", mk_tracker_mt2, rm_tracker_mt2, XX_FILE_TYPE_UNKNOWN },
    { "audio_dsf", mk_audio_dsf, rm_audio_dsf, XX_FILE_TYPE_UNKNOWN },
    { "audio_dff", mk_audio_dff, rm_audio_dff, XX_FILE_TYPE_UNKNOWN },
    { "audio_wave64", mk_audio_wave64, rm_audio_wave64, XX_FILE_TYPE_UNKNOWN },
    { "audio_adx", mk_audio_adx, rm_audio_adx, XX_FILE_TYPE_UNKNOWN },
    { "audio_ast", mk_audio_ast, rm_audio_ast, XX_FILE_TYPE_UNKNOWN },
    { "audio_hca", mk_audio_hca, rm_audio_hca, XX_FILE_TYPE_UNKNOWN },
    { "iff_8svx", mk_iff_8svx, rm_iff_8svx, XX_FILE_TYPE_UNKNOWN },
    { "audio_wavpack", mk_audio_wavpack, rm_audio_wavpack, XX_FILE_TYPE_UNKNOWN },
    { "blender_blend", mk_blender_blend, rm_blender_blend, XX_FILE_TYPE_UNKNOWN },
    { "autodesk_fbx", mk_autodesk_fbx, rm_autodesk_fbx, XX_FILE_TYPE_UNKNOWN },
    { "autodesk_3ds", mk_autodesk_3ds, rm_autodesk_3ds, XX_FILE_TYPE_UNKNOWN },
    { "lightwave_lwo2", mk_lightwave_lwo2, rm_lightwave_lwo2, XX_FILE_TYPE_UNKNOWN },
    { "lightwave_mdd", mk_lightwave_mdd, rm_lightwave_mdd, XX_FILE_TYPE_UNKNOWN },
    { "sony_psp_pbp", mk_sony_psp_pbp, rm_sony_psp_pbp, XX_FILE_TYPE_UNKNOWN },
    { "flash_video_flv", mk_flash_video_flv, rm_flash_video_flv, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_n64_rom", mk_nintendo_n64_rom, rm_nintendo_n64_rom, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_gb_rom", mk_nintendo_gb_rom, rm_nintendo_gb_rom, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_gba_rom", mk_nintendo_gba_rom, rm_nintendo_gba_rom, XX_FILE_TYPE_UNKNOWN },
    { "sega_megadrive_rom", mk_sega_megadrive_rom, rm_sega_megadrive_rom, XX_FILE_TYPE_UNKNOWN },
    { "spring_s3o", mk_spring_s3o, rm_spring_s3o, XX_FILE_TYPE_UNKNOWN },
    { "xna_xnb", mk_xna_xnb, rm_xna_xnb, XX_FILE_TYPE_UNKNOWN },
    { "lua_bytecode51", mk_lua_bytecode51, rm_lua_bytecode51, XX_FILE_TYPE_UNKNOWN },
    { "quake_md5mesh", mk_quake_md5mesh, rm_quake_md5mesh, XX_FILE_TYPE_UNKNOWN },
    { "tracker_mod", mk_tracker_mod, rm_tracker_mod, XX_FILE_TYPE_UNKNOWN },
    { "tracker_far", mk_tracker_far, rm_tracker_far, XX_FILE_TYPE_UNKNOWN },
    { "tracker_mdl", mk_tracker_mdl, rm_tracker_mdl, XX_FILE_TYPE_UNKNOWN },
    { "tracker_gdm", mk_tracker_gdm, rm_tracker_gdm, XX_FILE_TYPE_UNKNOWN },
    { "tracker_dbm", mk_tracker_dbm, rm_tracker_dbm, XX_FILE_TYPE_UNKNOWN },
    { "tracker_med", mk_tracker_med, rm_tracker_med, XX_FILE_TYPE_UNKNOWN },
    { "tracker_imf", mk_tracker_imf, rm_tracker_imf, XX_FILE_TYPE_UNKNOWN },
    { "tracker_amf", mk_tracker_amf, rm_tracker_amf, XX_FILE_TYPE_UNKNOWN },
    { "tracker_psm", mk_tracker_psm, rm_tracker_psm, XX_FILE_TYPE_UNKNOWN },
    { "steinberg_fxb", mk_steinberg_fxb, rm_steinberg_fxb, XX_FILE_TYPE_UNKNOWN },
    { "astronomy_ser", mk_astronomy_ser, rm_astronomy_ser, XX_FILE_TYPE_UNKNOWN },
    { "photontiming_ptu", mk_photontiming_ptu, rm_photontiming_ptu, XX_FILE_TYPE_UNKNOWN },
    { "photontiming_phu", mk_photontiming_phu, rm_photontiming_phu, XX_FILE_TYPE_UNKNOWN },
    { "charmm_dcd", mk_charmm_dcd, rm_charmm_dcd, XX_FILE_TYPE_UNKNOWN },
    { "gromacs_trr", mk_gromacs_trr, rm_gromacs_trr, XX_FILE_TYPE_UNKNOWN },
    { "microscopy_ics", mk_microscopy_ics, rm_microscopy_ics, XX_FILE_TYPE_UNKNOWN },
    { "tecplot_plt", mk_tecplot_plt, rm_tecplot_plt, XX_FILE_TYPE_UNKNOWN },
    { "fujifilm_raf", mk_fujifilm_raf, rm_fujifilm_raf, XX_FILE_TYPE_UNKNOWN },
    { "sigma_x3f", mk_sigma_x3f, rm_sigma_x3f, XX_FILE_TYPE_UNKNOWN },
    { "minolta_mrw", mk_minolta_mrw, rm_minolta_mrw, XX_FILE_TYPE_UNKNOWN },
    { "vice_x64", mk_vice_x64, rm_vice_x64, XX_FILE_TYPE_UNKNOWN },
    { "vice_snapshot", mk_vice_snapshot, rm_vice_snapshot, XX_FILE_TYPE_UNKNOWN },
    { "commodore_g64", mk_commodore_g64, rm_commodore_g64, XX_FILE_TYPE_UNKNOWN },
    { "commodore_p64", mk_commodore_p64, rm_commodore_p64, XX_FILE_TYPE_UNKNOWN },
    { "commodore_tap", mk_commodore_tap, rm_commodore_tap, XX_FILE_TYPE_UNKNOWN },
    { "zx_spectrum_tzx", mk_zx_spectrum_tzx, rm_zx_spectrum_tzx, XX_FILE_TYPE_UNKNOWN },
    { "zx_spectrum_szx", mk_zx_spectrum_szx, rm_zx_spectrum_szx, XX_FILE_TYPE_UNKNOWN },
    { "amstrad_cpc_dsk", mk_amstrad_cpc_dsk, rm_amstrad_cpc_dsk, XX_FILE_TYPE_UNKNOWN },
    { "atari_st_msa", mk_atari_st_msa, rm_atari_st_msa, XX_FILE_TYPE_UNKNOWN },
    { "supercard_scp", mk_supercard_scp, rm_supercard_scp, XX_FILE_TYPE_UNKNOWN },
    { "apple_woz", mk_apple_woz, rm_apple_woz, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_nsf", mk_nintendo_nsf, rm_nintendo_nsf, XX_FILE_TYPE_UNKNOWN },
    { "vgm_log", mk_vgm_log, rm_vgm_log, XX_FILE_TYPE_UNKNOWN },
    { "psid_sid", mk_psid_sid, rm_psid_sid, XX_FILE_TYPE_UNKNOWN },
    { "hes_sound", mk_hes_sound, rm_hes_sound, XX_FILE_TYPE_UNKNOWN },
    { "audio_dolby_ac3", mk_audio_dolby_ac3, rm_audio_dolby_ac3, XX_FILE_TYPE_UNKNOWN },
    { "audio_mpeg_mp3", mk_audio_mpeg_mp3, rm_audio_mpeg_mp3, XX_FILE_TYPE_UNKNOWN },
    { "audio_aac_adts", mk_audio_aac_adts, rm_audio_aac_adts, XX_FILE_TYPE_UNKNOWN },
    { "audio_monkeys_ape", mk_audio_monkeys_ape, rm_audio_monkeys_ape, XX_FILE_TYPE_UNKNOWN },
    { "mpeg_transport_stream", mk_mpeg_transport_stream, rm_mpeg_transport_stream, XX_FILE_TYPE_UNKNOWN },
    { "mpeg_program_stream", mk_mpeg_program_stream, rm_mpeg_program_stream, XX_FILE_TYPE_UNKNOWN },
    { "realmedia_rm", mk_realmedia_rm, rm_realmedia_rm, XX_FILE_TYPE_UNKNOWN },
    { "idtech_roq", mk_idtech_roq, rm_idtech_roq, XX_FILE_TYPE_UNKNOWN },
    { "rad_bink", mk_rad_bink, rm_rad_bink, XX_FILE_TYPE_UNKNOWN },
    { "rad_smacker", mk_rad_smacker, rm_rad_smacker, XX_FILE_TYPE_UNKNOWN },
    { "interplay_mve", mk_interplay_mve, rm_interplay_mve, XX_FILE_TYPE_UNKNOWN },
    { "westwood_vqa", mk_westwood_vqa, rm_westwood_vqa, XX_FILE_TYPE_UNKNOWN },
    { "autodesk_flic", mk_autodesk_flic, rm_autodesk_flic, XX_FILE_TYPE_UNKNOWN },
    { "idtech_md5anim", mk_idtech_md5anim, rm_idtech_md5anim, XX_FILE_TYPE_UNKNOWN },
    { "stereolithography_stl", mk_stereolithography_stl, rm_stereolithography_stl, XX_FILE_TYPE_UNKNOWN },
    { "garmin_fit", mk_garmin_fit, rm_garmin_fit, XX_FILE_TYPE_UNKNOWN },
    { "rosbag1", mk_rosbag1, rm_rosbag1, XX_FILE_TYPE_UNKNOWN },
    { "mcap", mk_mcap, rm_mcap, XX_FILE_TYPE_UNKNOWN },
    { "seismic_sac", mk_seismic_sac, rm_seismic_sac, XX_FILE_TYPE_UNKNOWN },
    { "seismic_seg2", mk_seismic_seg2, rm_seismic_seg2, XX_FILE_TYPE_UNKNOWN },
    { "ucsc_twobit", mk_ucsc_twobit, rm_ucsc_twobit, XX_FILE_TYPE_UNKNOWN },
    { "genomics_bgen", mk_genomics_bgen, rm_genomics_bgen, XX_FILE_TYPE_UNKNOWN },
    { "openephys_continuous", mk_openephys_continuous, rm_openephys_continuous, XX_FILE_TYPE_UNKNOWN },
    { "mountainsort_mda", mk_mountainsort_mda, rm_mountainsort_mda, XX_FILE_TYPE_UNKNOWN },
    { "igor_ibw", mk_igor_ibw, rm_igor_ibw, XX_FILE_TYPE_UNKNOWN },
    { "princeton_spe", mk_princeton_spe, rm_princeton_spe, XX_FILE_TYPE_UNKNOWN },
    { "microscopy_spider", mk_microscopy_spider, rm_microscopy_spider, XX_FILE_TYPE_UNKNOWN },
    { "wmo_grib", mk_wmo_grib, rm_wmo_grib, XX_FILE_TYPE_UNKNOWN },
    { "wmo_bufr", mk_wmo_bufr, rm_wmo_bufr, XX_FILE_TYPE_UNKNOWN },
    { "autocad_dxf", mk_autocad_dxf, rm_autocad_dxf, XX_FILE_TYPE_UNKNOWN },
    { "blackrock_nsx", mk_blackrock_nsx, rm_blackrock_nsx, XX_FILE_TYPE_UNKNOWN },
    { "blackrock_nev", mk_blackrock_nev, rm_blackrock_nev, XX_FILE_TYPE_UNKNOWN },
    { "lecroy_trc", mk_lecroy_trc, rm_lecroy_trc, XX_FILE_TYPE_UNKNOWN },
    { "tektronix_isf", mk_tektronix_isf, rm_tektronix_isf, XX_FILE_TYPE_UNKNOWN },
    { "ircam_sdif", mk_ircam_sdif, rm_ircam_sdif, XX_FILE_TYPE_UNKNOWN },
    { "microsoft_msf", mk_microsoft_msf, rm_microsoft_msf, XX_FILE_TYPE_UNKNOWN },
    { "windows_registry_hive", mk_windows_registry_hive, rm_windows_registry_hive, XX_FILE_TYPE_UNKNOWN },
    { "windows_evtx", mk_windows_evtx, rm_windows_evtx, XX_FILE_TYPE_UNKNOWN },
    { "binary_plist", mk_binary_plist, rm_binary_plist, XX_FILE_TYPE_UNKNOWN },
    { "mongodb_bson", mk_mongodb_bson, rm_mongodb_bson, XX_FILE_TYPE_UNKNOWN },
    { "cbor", mk_cbor, rm_cbor, XX_FILE_TYPE_UNKNOWN },
    { "openzim", mk_openzim, rm_openzim, XX_FILE_TYPE_UNKNOWN },
    { "apache_orc", mk_apache_orc, rm_apache_orc, XX_FILE_TYPE_UNKNOWN },
    { "hadoop_sequencefile", mk_hadoop_sequencefile, rm_hadoop_sequencefile, XX_FILE_TYPE_UNKNOWN },
    { "leveldb_sstable", mk_leveldb_sstable, rm_leveldb_sstable, XX_FILE_TYPE_UNKNOWN },
    { "snappy_framed", mk_snappy_framed, rm_snappy_framed, XX_FILE_TYPE_UNKNOWN },
    { "lzf_stream", mk_lzf_stream, rm_lzf_stream, XX_FILE_TYPE_UNKNOWN },
    { "fastlz_sixpack", mk_fastlz_sixpack, rm_fastlz_sixpack, XX_FILE_TYPE_UNKNOWN },
    { "linux_btf", mk_linux_btf, rm_linux_btf, XX_FILE_TYPE_UNKNOWN },
    { "flatgeobuf", mk_flatgeobuf, rm_flatgeobuf, XX_FILE_TYPE_UNKNOWN },
    { "astc_texture", mk_astc_texture, rm_astc_texture, XX_FILE_TYPE_UNKNOWN },
    { "pkm_texture", mk_pkm_texture, rm_pkm_texture, XX_FILE_TYPE_UNKNOWN },
    { "basis_texture", mk_basis_texture, rm_basis_texture, XX_FILE_TYPE_UNKNOWN },
    { "openctm_mesh", mk_openctm_mesh, rm_openctm_mesh, XX_FILE_TYPE_UNKNOWN },
    { "font_bdf", mk_font_bdf, rm_font_bdf, XX_FILE_TYPE_UNKNOWN },
    { "font_pcf", mk_font_pcf, rm_font_pcf, XX_FILE_TYPE_UNKNOWN },
    { "font_psf", mk_font_psf, rm_font_psf, XX_FILE_TYPE_UNKNOWN },
    { "font_windows_fnt", mk_font_windows_fnt, rm_font_windows_fnt, XX_FILE_TYPE_UNKNOWN },
    { "tex_tfm", mk_tex_tfm, rm_tex_tfm, XX_FILE_TYPE_UNKNOWN },
    { "tex_pk", mk_tex_pk, rm_tex_pk, XX_FILE_TYPE_UNKNOWN },
    { "tex_dvi", mk_tex_dvi, rm_tex_dvi, XX_FILE_TYPE_UNKNOWN },
    { "netpbm_pfm", mk_netpbm_pfm, rm_netpbm_pfm, XX_FILE_TYPE_UNKNOWN },
    { "steinberg_vst3preset", mk_steinberg_vst3preset, rm_steinberg_vst3preset, XX_FILE_TYPE_UNKNOWN },
    { "font_bmfont", mk_font_bmfont, rm_font_bmfont, XX_FILE_TYPE_UNKNOWN },
    { "processing_vlw", mk_processing_vlw, rm_processing_vlw, XX_FILE_TYPE_UNKNOWN },
    { "snes_spc", mk_snes_spc, rm_snes_spc, XX_FILE_TYPE_UNKNOWN },
    { "gameboy_gbs", mk_gameboy_gbs, rm_gameboy_gbs, XX_FILE_TYPE_UNKNOWN },
    { "sega_sgc", mk_sega_sgc, rm_sega_sgc, XX_FILE_TYPE_UNKNOWN },
    { "s98_log", mk_s98_log, rm_s98_log, XX_FILE_TYPE_UNKNOWN },
    { "atari_sap", mk_atari_sap, rm_atari_sap, XX_FILE_TYPE_UNKNOWN },
    { "sc68_music", mk_sc68_music, rm_sc68_music, XX_FILE_TYPE_UNKNOWN },
    { "zx_spectrum_pzx", mk_zx_spectrum_pzx, rm_zx_spectrum_pzx, XX_FILE_TYPE_UNKNOWN },
    { "acorn_uef", mk_acorn_uef, rm_acorn_uef, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_unif", mk_nintendo_unif, rm_nintendo_unif, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_fds", mk_nintendo_fds, rm_nintendo_fds, XX_FILE_TYPE_UNKNOWN },
    { "ucsc_bigwig", mk_ucsc_bigwig, rm_ucsc_bigwig, XX_FILE_TYPE_UNKNOWN },
    { "ucsc_bigbed", mk_ucsc_bigbed, rm_ucsc_bigbed, XX_FILE_TYPE_UNKNOWN },
    { "phylo_nexus", mk_phylo_nexus, rm_phylo_nexus, XX_FILE_TYPE_UNKNOWN },
    { "phylo_newick", mk_phylo_newick, rm_phylo_newick, XX_FILE_TYPE_UNKNOWN },
    { "sqlite_rollback_journal", mk_sqlite_rollback_journal, rm_sqlite_rollback_journal, XX_FILE_TYPE_UNKNOWN },
    { "neuroscan_cnt", mk_neuroscan_cnt, rm_neuroscan_cnt, XX_FILE_TYPE_UNKNOWN },
    { "axona_tetrode", mk_axona_tetrode, rm_axona_tetrode, XX_FILE_TYPE_UNKNOWN },
    { "python_pickle", mk_python_pickle, rm_python_pickle, XX_FILE_TYPE_UNKNOWN },
    { "inivation_aedat", mk_inivation_aedat, rm_inivation_aedat, XX_FILE_TYPE_UNKNOWN },
    { "python_marshal", mk_python_marshal, rm_python_marshal, XX_FILE_TYPE_UNKNOWN },
    { "nix_nar", mk_nix_nar, rm_nix_nar, XX_FILE_TYPE_UNKNOWN },
    { "redis_rdb", mk_redis_rdb, rm_redis_rdb, XX_FILE_TYPE_UNKNOWN },
    { "postgres_custom", mk_postgres_custom, rm_postgres_custom, XX_FILE_TYPE_UNKNOWN },
    { "mysql_binlog", mk_mysql_binlog, rm_mysql_binlog, XX_FILE_TYPE_UNKNOWN },
    { "kafka_record_batch", mk_kafka_record_batch, rm_kafka_record_batch, XX_FILE_TYPE_UNKNOWN },
    { "android_binary_xml", mk_android_binary_xml, rm_android_binary_xml, XX_FILE_TYPE_UNKNOWN },
    { "android_resources_arsc", mk_android_resources_arsc, rm_android_resources_arsc, XX_FILE_TYPE_UNKNOWN },
    { "msgpack", mk_msgpack, rm_msgpack, XX_FILE_TYPE_UNKNOWN },
    { "ubjson", mk_ubjson, rm_ubjson, XX_FILE_TYPE_UNKNOWN },
    { "bittorrent_metainfo", mk_bittorrent_metainfo, rm_bittorrent_metainfo, XX_FILE_TYPE_UNKNOWN },
    { "erlang_external_term", mk_erlang_external_term, rm_erlang_external_term, XX_FILE_TYPE_UNKNOWN },
    { "capnproto_message", mk_capnproto_message, rm_capnproto_message, XX_FILE_TYPE_UNKNOWN },
    { "dbus_message", mk_dbus_message, rm_dbus_message, XX_FILE_TYPE_UNKNOWN },
    { "windows_shell_link", mk_windows_shell_link, rm_windows_shell_link, XX_FILE_TYPE_UNKNOWN },
    { "pkcs7_cms", mk_pkcs7_cms, rm_pkcs7_cms, XX_FILE_TYPE_UNKNOWN },
    { "wavefront_obj", mk_wavefront_obj, rm_wavefront_obj, XX_FILE_TYPE_UNKNOWN },
    { "off_mesh", mk_off_mesh, rm_off_mesh, XX_FILE_TYPE_UNKNOWN },
    { "ac3d_model", mk_ac3d_model, rm_ac3d_model, XX_FILE_TYPE_UNKNOWN },
    { "qubicle_qb", mk_qubicle_qb, rm_qubicle_qb, XX_FILE_TYPE_UNKNOWN },
    { "terragen_ter", mk_terragen_ter, rm_terragen_ter, XX_FILE_TYPE_UNKNOWN },
    { "gimp_xcf", mk_gimp_xcf, rm_gimp_xcf, XX_FILE_TYPE_UNKNOWN },
    { "photoshop_abr", mk_photoshop_abr, rm_photoshop_abr, XX_FILE_TYPE_UNKNOWN },
    { "softimage_pic", mk_softimage_pic, rm_softimage_pic, XX_FILE_TYPE_UNKNOWN },
    { "alias_pix", mk_alias_pix, rm_alias_pix, XX_FILE_TYPE_UNKNOWN },
    { "qt_qpicture", mk_qt_qpicture, rm_qt_qpicture, XX_FILE_TYPE_UNKNOWN },
    { "font_type1_pfb", mk_font_type1_pfb, rm_font_type1_pfb, XX_FILE_TYPE_UNKNOWN },
    { "font_gem_fnt", mk_font_gem_fnt, rm_font_gem_fnt, XX_FILE_TYPE_UNKNOWN },
    { "tex_gf", mk_tex_gf, rm_tex_gf, XX_FILE_TYPE_UNKNOWN },
    { "bpg_image", mk_bpg_image, rm_bpg_image, XX_FILE_TYPE_UNKNOWN },
    { "mng_animation", mk_mng_animation, rm_mng_animation, XX_FILE_TYPE_UNKNOWN },
    { "atari_7800_a78", mk_atari_7800_a78, rm_atari_7800_a78, XX_FILE_TYPE_UNKNOWN },
    { "commodore_pc64", mk_commodore_pc64, rm_commodore_pc64, XX_FILE_TYPE_UNKNOWN },
    { "atari_cas", mk_atari_cas, rm_atari_cas, XX_FILE_TYPE_UNKNOWN },
    { "msx_cas", mk_msx_cas, rm_msx_cas, XX_FILE_TYPE_UNKNOWN },
    { "oric_tap", mk_oric_tap, rm_oric_tap, XX_FILE_TYPE_UNKNOWN },
    { "dragon_cas", mk_dragon_cas, rm_dragon_cas, XX_FILE_TYPE_UNKNOWN },
    { "amiga_ahx", mk_amiga_ahx, rm_amiga_ahx, XX_FILE_TYPE_UNKNOWN },
    { "amstrad_cpc_sna", mk_amstrad_cpc_sna, rm_amstrad_cpc_sna, XX_FILE_TYPE_UNKNOWN },
    { "vtech_vz", mk_vtech_vz, rm_vtech_vz, XX_FILE_TYPE_UNKNOWN },
    { "zx_hobeta", mk_zx_hobeta, rm_zx_hobeta, XX_FILE_TYPE_UNKNOWN },
    { "genomics_fasta", mk_genomics_fasta, rm_genomics_fasta, XX_FILE_TYPE_UNKNOWN },
    { "genomics_fastq", mk_genomics_fastq, rm_genomics_fastq, XX_FILE_TYPE_UNKNOWN },
    { "genomics_sam", mk_genomics_sam, rm_genomics_sam, XX_FILE_TYPE_UNKNOWN },
    { "opendx_field", mk_opendx_field, rm_opendx_field, XX_FILE_TYPE_UNKNOWN },
    { "genomics_vcf", mk_genomics_vcf, rm_genomics_vcf, XX_FILE_TYPE_UNKNOWN },
    { "genomics_gff3", mk_genomics_gff3, rm_genomics_gff3, XX_FILE_TYPE_UNKNOWN },
    { "protein_pdb", mk_protein_pdb, rm_protein_pdb, XX_FILE_TYPE_UNKNOWN },
    { "protein_mmcif", mk_protein_mmcif, rm_protein_mmcif, XX_FILE_TYPE_UNKNOWN },
    { "matrix_market", mk_matrix_market, rm_matrix_market, XX_FILE_TYPE_UNKNOWN },
    { "gromacs_gro", mk_gromacs_gro, rm_gromacs_gro, XX_FILE_TYPE_UNKNOWN },
    { "minecraft_nbt", mk_minecraft_nbt, rm_minecraft_nbt, XX_FILE_TYPE_UNKNOWN },
    { "amazon_ion_binary", mk_amazon_ion_binary, rm_amazon_ion_binary, XX_FILE_TYPE_UNKNOWN },
    { "leveldb_log", mk_leveldb_log, rm_leveldb_log, XX_FILE_TYPE_UNKNOWN },
    { "dns_message", mk_dns_message, rm_dns_message, XX_FILE_TYPE_UNKNOWN },
    { "rocksdb_blob", mk_rocksdb_blob, rm_rocksdb_blob, XX_FILE_TYPE_UNKNOWN },
    { "mongodb_wire", mk_mongodb_wire, rm_mongodb_wire, XX_FILE_TYPE_UNKNOWN },
    { "redis_resp", mk_redis_resp, rm_redis_resp, XX_FILE_TYPE_UNKNOWN },
    { "mqtt_packets", mk_mqtt_packets, rm_mqtt_packets, XX_FILE_TYPE_UNKNOWN },
    { "amqp_frames", mk_amqp_frames, rm_amqp_frames, XX_FILE_TYPE_UNKNOWN },
    { "thrift_compact", mk_thrift_compact, rm_thrift_compact, XX_FILE_TYPE_UNKNOWN },
    { "x509_certificate", mk_x509_certificate, rm_x509_certificate, XX_FILE_TYPE_UNKNOWN },
    { "pkcs10_csr", mk_pkcs10_csr, rm_pkcs10_csr, XX_FILE_TYPE_UNKNOWN },
    { "pkcs12_pfx", mk_pkcs12_pfx, rm_pkcs12_pfx, XX_FILE_TYPE_UNKNOWN },
    { "openssh_private_key", mk_openssh_private_key, rm_openssh_private_key, XX_FILE_TYPE_UNKNOWN },
    { "kerberos_keytab", mk_kerberos_keytab, rm_kerberos_keytab, XX_FILE_TYPE_UNKNOWN },
    { "gimp_gpl", mk_gimp_gpl, rm_gimp_gpl, XX_FILE_TYPE_UNKNOWN },
    { "gimp_ggr", mk_gimp_ggr, rm_gimp_ggr, XX_FILE_TYPE_UNKNOWN },
    { "iridas_cube_lut", mk_iridas_cube_lut, rm_iridas_cube_lut, XX_FILE_TYPE_UNKNOWN },
    { "hpgl_plot", mk_hpgl_plot, rm_hpgl_plot, XX_FILE_TYPE_UNKNOWN },
    { "paintshop_psp", mk_paintshop_psp, rm_paintshop_psp, XX_FILE_TYPE_UNKNOWN },
    { "photoshop_pat", mk_photoshop_pat, rm_photoshop_pat, XX_FILE_TYPE_UNKNOWN },
    { "mmd_pmx", mk_mmd_pmx, rm_mmd_pmx, XX_FILE_TYPE_UNKNOWN },
    { "metasequoia_mqo", mk_metasequoia_mqo, rm_metasequoia_mqo, XX_FILE_TYPE_UNKNOWN },
    { "calma_gdsii", mk_calma_gdsii, rm_calma_gdsii, XX_FILE_TYPE_UNKNOWN },
    { "autodesk_ase", mk_autodesk_ase, rm_autodesk_ase, XX_FILE_TYPE_UNKNOWN },
    { "freesurfer_surface", mk_freesurfer_surface, rm_freesurfer_surface, XX_FILE_TYPE_UNKNOWN },
    { "gmsh_msh", mk_gmsh_msh, rm_gmsh_msh, XX_FILE_TYPE_UNKNOWN },
    { "netgen_vol", mk_netgen_vol, rm_netgen_vol, XX_FILE_TYPE_UNKNOWN },
    { "font_afm", mk_font_afm, rm_font_afm, XX_FILE_TYPE_UNKNOWN },
    { "tiled_tmx", mk_tiled_tmx, rm_tiled_tmx, XX_FILE_TYPE_UNKNOWN },
    { "nintendo_sdat", mk_nintendo_sdat, rm_nintendo_sdat, XX_FILE_TYPE_UNKNOWN },
    { "sony_vab", mk_sony_vab, rm_sony_vab, XX_FILE_TYPE_UNKNOWN },
    { "yamaha_ym", mk_yamaha_ym, rm_yamaha_ym, XX_FILE_TYPE_UNKNOWN },
    { "zx_ayemul", mk_zx_ayemul, rm_zx_ayemul, XX_FILE_TYPE_UNKNOWN },
    { "dragon_vdk", mk_dragon_vdk, rm_dragon_vdk, XX_FILE_TYPE_UNKNOWN },
    { "apple_a2r", mk_apple_a2r, rm_apple_a2r, XX_FILE_TYPE_UNKNOWN },
    { "atari_atr", mk_atari_atr, rm_atari_atr, XX_FILE_TYPE_UNKNOWN },
    { "atari_pasti_stx", mk_atari_pasti_stx, rm_atari_pasti_stx, XX_FILE_TYPE_UNKNOWN },
    { "amiga_ipf", mk_amiga_ipf, rm_amiga_ipf, XX_FILE_TYPE_UNKNOWN },
    { "tracker_dtt", mk_tracker_dtt, rm_tracker_dtt, XX_FILE_TYPE_UNKNOWN },
    { "gaussian_cube", mk_gaussian_cube, rm_gaussian_cube, XX_FILE_TYPE_UNKNOWN },
    { "molecule_xyz", mk_molecule_xyz, rm_molecule_xyz, XX_FILE_TYPE_UNKNOWN },
    { "mdl_molfile", mk_mdl_molfile, rm_mdl_molfile, XX_FILE_TYPE_UNKNOWN },
    { "tripos_mol2", mk_tripos_mol2, rm_tripos_mol2, XX_FILE_TYPE_UNKNOWN },
    { "xcrysden_xsf", mk_xcrysden_xsf, rm_xcrysden_xsf, XX_FILE_TYPE_UNKNOWN },
    { "amber_prmtop", mk_amber_prmtop, rm_amber_prmtop, XX_FILE_TYPE_UNKNOWN },
    { "amber_restart", mk_amber_restart, rm_amber_restart, XX_FILE_TYPE_UNKNOWN },
    { "gaussian_fchk", mk_gaussian_fchk, rm_gaussian_fchk, XX_FILE_TYPE_UNKNOWN },
    { "jcamp_dx", mk_jcamp_dx, rm_jcamp_dx, XX_FILE_TYPE_UNKNOWN },
    { "dl_poly_config", mk_dl_poly_config, rm_dl_poly_config, XX_FILE_TYPE_UNKNOWN },
    { "http1_message", mk_http1_message, rm_http1_message, XX_FILE_TYPE_UNKNOWN },
    { "websocket_frames", mk_websocket_frames, rm_websocket_frames, XX_FILE_TYPE_UNKNOWN },
    { "coap_message", mk_coap_message, rm_coap_message, XX_FILE_TYPE_UNKNOWN },
    { "stun_message", mk_stun_message, rm_stun_message, XX_FILE_TYPE_UNKNOWN },
    { "dhcp_message", mk_dhcp_message, rm_dhcp_message, XX_FILE_TYPE_UNKNOWN },
    { "radius_packet", mk_radius_packet, rm_radius_packet, XX_FILE_TYPE_UNKNOWN },
    { "snmp_message", mk_snmp_message, rm_snmp_message, XX_FILE_TYPE_UNKNOWN },
    { "ldap_message", mk_ldap_message, rm_ldap_message, XX_FILE_TYPE_UNKNOWN },
    { "tls_records", mk_tls_records, rm_tls_records, XX_FILE_TYPE_UNKNOWN },
    { "jks_keystore", mk_jks_keystore, rm_jks_keystore, XX_FILE_TYPE_UNKNOWN },
    { "java_serialization", mk_java_serialization, rm_java_serialization, XX_FILE_TYPE_UNKNOWN },
    { "x509_crl", mk_x509_crl, rm_x509_crl, XX_FILE_TYPE_UNKNOWN },
    { "ocsp_response", mk_ocsp_response, rm_ocsp_response, XX_FILE_TYPE_UNKNOWN },
    { "lmdb_data", mk_lmdb_data, rm_lmdb_data, XX_FILE_TYPE_UNKNOWN },
    { "gdbm_dump", mk_gdbm_dump, rm_gdbm_dump, XX_FILE_TYPE_UNKNOWN },
    { "adobe_acb", mk_adobe_acb, rm_adobe_acb, XX_FILE_TYPE_UNKNOWN },
    { "jasc_palette", mk_jasc_palette, rm_jasc_palette, XX_FILE_TYPE_UNKNOWN },
    { "x11_xbm", mk_x11_xbm, rm_x11_xbm, XX_FILE_TYPE_UNKNOWN },
    { "jpeg2000_pgx", mk_jpeg2000_pgx, rm_jpeg2000_pgx, XX_FILE_TYPE_UNKNOWN },
    { "amiga_diskobject", mk_amiga_diskobject, rm_amiga_diskobject, XX_FILE_TYPE_UNKNOWN },
    { "tex_vf", mk_tex_vf, rm_tex_vf, XX_FILE_TYPE_UNKNOWN },
    { "esri_ascii_grid", mk_esri_ascii_grid, rm_esri_ascii_grid, XX_FILE_TYPE_UNKNOWN },
    { "surfer_grid", mk_surfer_grid, rm_surfer_grid, XX_FILE_TYPE_UNKNOWN },
    { "gxf_grid", mk_gxf_grid, rm_gxf_grid, XX_FILE_TYPE_UNKNOWN },
    { "ogc_wkt", mk_ogc_wkt, rm_ogc_wkt, XX_FILE_TYPE_UNKNOWN },
    { "step_part21", mk_step_part21, rm_step_part21, XX_FILE_TYPE_UNKNOWN },
    { "gerber_rs274x", mk_gerber_rs274x, rm_gerber_rs274x, XX_FILE_TYPE_UNKNOWN },
    { "excellon_drill", mk_excellon_drill, rm_excellon_drill, XX_FILE_TYPE_UNKNOWN },
    { "vrml_scene", mk_vrml_scene, rm_vrml_scene, XX_FILE_TYPE_UNKNOWN },
    { "renderman_rib", mk_renderman_rib, rm_renderman_rib, XX_FILE_TYPE_UNKNOWN },
    { "asylum_amf", mk_asylum_amf, rm_asylum_amf, XX_FILE_TYPE_UNKNOWN },
    { "tracker_stx", mk_tracker_stx, rm_tracker_stx, XX_FILE_TYPE_UNKNOWN },
    { "tracker_dtm", mk_tracker_dtm, rm_tracker_dtm, XX_FILE_TYPE_UNKNOWN },
    { "tracker_soundfx", mk_tracker_soundfx, rm_tracker_soundfx, XX_FILE_TYPE_UNKNOWN },
    { "tracker_funk", mk_tracker_funk, rm_tracker_funk, XX_FILE_TYPE_UNKNOWN },
    { "tracker_archimedes", mk_tracker_archimedes, rm_tracker_archimedes, XX_FILE_TYPE_UNKNOWN },
    { "pce_psi", mk_pce_psi, rm_pce_psi, XX_FILE_TYPE_UNKNOWN },
    { "pc98_d88", mk_pc98_d88, rm_pc98_d88, XX_FILE_TYPE_UNKNOWN },
    { "hxc_mfm", mk_hxc_mfm, rm_hxc_mfm, XX_FILE_TYPE_UNKNOWN },
    { "yaze_ydsk", mk_yaze_ydsk, rm_yaze_ydsk, XX_FILE_TYPE_UNKNOWN },
    { "lammps_data", mk_lammps_data, rm_lammps_data, XX_FILE_TYPE_UNKNOWN },
    { "lammps_dump", mk_lammps_dump, rm_lammps_dump, XX_FILE_TYPE_UNKNOWN },
    { "shelx_res", mk_shelx_res, rm_shelx_res, XX_FILE_TYPE_UNKNOWN },
    { "turbomole_coord", mk_turbomole_coord, rm_turbomole_coord, XX_FILE_TYPE_UNKNOWN },
    { "charmm_crd", mk_charmm_crd, rm_charmm_crd, XX_FILE_TYPE_UNKNOWN },
    { "castep_cell", mk_castep_cell, rm_castep_cell, XX_FILE_TYPE_UNKNOWN },
    { "crystal_fort34", mk_crystal_fort34, rm_crystal_fort34, XX_FILE_TYPE_UNKNOWN },
    { "siesta_xv", mk_siesta_xv, rm_siesta_xv, XX_FILE_TYPE_UNKNOWN },
    { "harwell_boeing", mk_harwell_boeing, rm_harwell_boeing, XX_FILE_TYPE_UNKNOWN },
    { "openfoam_points", mk_openfoam_points, rm_openfoam_points, XX_FILE_TYPE_UNKNOWN },
    { "ntp_message", mk_ntp_message, rm_ntp_message, XX_FILE_TYPE_UNKNOWN },
    { "rtp_rtcp", mk_rtp_rtcp, rm_rtp_rtcp, XX_FILE_TYPE_UNKNOWN },
    { "bgp_messages", mk_bgp_messages, rm_bgp_messages, XX_FILE_TYPE_UNKNOWN },
    { "ospf_packet", mk_ospf_packet, rm_ospf_packet, XX_FILE_TYPE_UNKNOWN },
    { "sctp_packet", mk_sctp_packet, rm_sctp_packet, XX_FILE_TYPE_UNKNOWN },
    { "isakmp_message", mk_isakmp_message, rm_isakmp_message, XX_FILE_TYPE_UNKNOWN },
    { "ssh_transport", mk_ssh_transport, rm_ssh_transport, XX_FILE_TYPE_UNKNOWN },
    { "smtp_transcript", mk_smtp_transcript, rm_smtp_transcript, XX_FILE_TYPE_UNKNOWN },
    { "pkcs8_private_key", mk_pkcs8_private_key, rm_pkcs8_private_key, XX_FILE_TYPE_UNKNOWN },
    { "putty_ppk", mk_putty_ppk, rm_putty_ppk, XX_FILE_TYPE_UNKNOWN },
    { "openssh_certificate", mk_openssh_certificate, rm_openssh_certificate, XX_FILE_TYPE_UNKNOWN },
    { "safetensors", mk_safetensors, rm_safetensors, XX_FILE_TYPE_UNKNOWN },
    { "gguf", mk_gguf, rm_gguf, XX_FILE_TYPE_UNKNOWN },
    { "cdb_database", mk_cdb_database, rm_cdb_database, XX_FILE_TYPE_UNKNOWN },
    { "stomp_frames", mk_stomp_frames, rm_stomp_frames, XX_FILE_TYPE_UNKNOWN },
    { "fontforge_sfd", mk_fontforge_sfd, rm_fontforge_sfd, XX_FILE_TYPE_UNKNOWN },
    { "grub_pff2", mk_grub_pff2, rm_grub_pff2, XX_FILE_TYPE_UNKNOWN },
    { "opengex_model", mk_opengex_model, rm_opengex_model, XX_FILE_TYPE_UNKNOWN },
    { "bvh_motion", mk_bvh_motion, rm_bvh_motion, XX_FILE_TYPE_UNKNOWN },
    { "directx_x", mk_directx_x, rm_directx_x, XX_FILE_TYPE_UNKNOWN },
    { "gts_surface", mk_gts_surface, rm_gts_surface, XX_FILE_TYPE_UNKNOWN },
    { "medit_mesh", mk_medit_mesh, rm_medit_mesh, XX_FILE_TYPE_UNKNOWN },
    { "gocad_model", mk_gocad_model, rm_gocad_model, XX_FILE_TYPE_UNKNOWN },
    { "nastran_bulk", mk_nastran_bulk, rm_nastran_bulk, XX_FILE_TYPE_UNKNOWN },
    { "abaqus_input", mk_abaqus_input, rm_abaqus_input, XX_FILE_TYPE_UNKNOWN },
    { "ensight_gold_geometry", mk_ensight_gold_geometry, rm_ensight_gold_geometry, XX_FILE_TYPE_UNKNOWN },
    { "gmv_mesh", mk_gmv_mesh, rm_gmv_mesh, XX_FILE_TYPE_UNKNOWN },
    { "usgs_dem", mk_usgs_dem, rm_usgs_dem, XX_FILE_TYPE_UNKNOWN },
    { "dted_elevation", mk_dted_elevation, rm_dted_elevation, XX_FILE_TYPE_UNKNOWN },
    { "mapinfo_mif", mk_mapinfo_mif, rm_mapinfo_mif, XX_FILE_TYPE_UNKNOWN },
    { "tracker_coconizer", mk_tracker_coconizer, rm_tracker_coconizer, XX_FILE_TYPE_UNKNOWN },
    { "tracker_real", mk_tracker_real, rm_tracker_real, XX_FILE_TYPE_UNKNOWN },
    { "tracker_megatracker", mk_tracker_megatracker, rm_tracker_megatracker, XX_FILE_TYPE_UNKNOWN },
    { "amos_music_bank", mk_amos_music_bank, rm_amos_music_bank, XX_FILE_TYPE_UNKNOWN },
    { "adlib_rad", mk_adlib_rad, rm_adlib_rad, XX_FILE_TYPE_UNKNOWN },
    { "adlib_amd", mk_adlib_amd, rm_adlib_amd, XX_FILE_TYPE_UNKNOWN },
    { "adlib_hsc", mk_adlib_hsc, rm_adlib_hsc, XX_FILE_TYPE_UNKNOWN },
    { "adlib_d00", mk_adlib_d00, rm_adlib_d00, XX_FILE_TYPE_UNKNOWN },
    { "adlib_bnk", mk_adlib_bnk, rm_adlib_bnk, XX_FILE_TYPE_UNKNOWN },
    { "dosbox_dro", mk_dosbox_dro, rm_dosbox_dro, XX_FILE_TYPE_UNKNOWN },
    { "genomics_genbank", mk_genomics_genbank, rm_genomics_genbank, XX_FILE_TYPE_UNKNOWN },
    { "genomics_embl", mk_genomics_embl, rm_genomics_embl, XX_FILE_TYPE_UNKNOWN },
    { "genomics_swissprot", mk_genomics_swissprot, rm_genomics_swissprot, XX_FILE_TYPE_UNKNOWN },
    { "alignment_clustal", mk_alignment_clustal, rm_alignment_clustal, XX_FILE_TYPE_UNKNOWN },
    { "alignment_stockholm", mk_alignment_stockholm, rm_alignment_stockholm, XX_FILE_TYPE_UNKNOWN },
    { "alignment_phylip", mk_alignment_phylip, rm_alignment_phylip, XX_FILE_TYPE_UNKNOWN },
    { "alignment_maf", mk_alignment_maf, rm_alignment_maf, XX_FILE_TYPE_UNKNOWN },
    { "alignment_mauve", mk_alignment_mauve, rm_alignment_mauve, XX_FILE_TYPE_UNKNOWN },
    { "ucsc_nib", mk_ucsc_nib, rm_ucsc_nib, XX_FILE_TYPE_UNKNOWN },
    { "assembly_gfa", mk_assembly_gfa, rm_assembly_gfa, XX_FILE_TYPE_UNKNOWN },
    { "ethernet_frame", mk_ethernet_frame, rm_ethernet_frame, XX_FILE_TYPE_UNKNOWN },
    { "ip_packet", mk_ip_packet, rm_ip_packet, XX_FILE_TYPE_UNKNOWN },
    { "arp_packet", mk_arp_packet, rm_arp_packet, XX_FILE_TYPE_UNKNOWN },
    { "icmp_message", mk_icmp_message, rm_icmp_message, XX_FILE_TYPE_UNKNOWN },
    { "sip_message", mk_sip_message, rm_sip_message, XX_FILE_TYPE_UNKNOWN },
    { "rtsp_message", mk_rtsp_message, rm_rtsp_message, XX_FILE_TYPE_UNKNOWN },
    { "diameter_message", mk_diameter_message, rm_diameter_message, XX_FILE_TYPE_UNKNOWN },
    { "tacacs_packet", mk_tacacs_packet, rm_tacacs_packet, XX_FILE_TYPE_UNKNOWN },
    { "gtp_message", mk_gtp_message, rm_gtp_message, XX_FILE_TYPE_UNKNOWN },
    { "pfcp_message", mk_pfcp_message, rm_pfcp_message, XX_FILE_TYPE_UNKNOWN },
    { "pptp_message", mk_pptp_message, rm_pptp_message, XX_FILE_TYPE_UNKNOWN },
    { "rsvp_message", mk_rsvp_message, rm_rsvp_message, XX_FILE_TYPE_UNKNOWN },
    { "age_encrypted", mk_age_encrypted, rm_age_encrypted, XX_FILE_TYPE_UNKNOWN },
    { "kerberos_ccache", mk_kerberos_ccache, rm_kerberos_ccache, XX_FILE_TYPE_UNKNOWN },
    { "jose_jws", mk_jose_jws, rm_jose_jws, XX_FILE_TYPE_UNKNOWN },
    { "wbmp_image", mk_wbmp_image, rm_wbmp_image, XX_FILE_TYPE_UNKNOWN },
    { "dec_sixel", mk_dec_sixel, rm_dec_sixel, XX_FILE_TYPE_UNKNOWN },
    { "palm_bitmap", mk_palm_bitmap, rm_palm_bitmap, XX_FILE_TYPE_UNKNOWN },
    { "adobe_acv", mk_adobe_acv, rm_adobe_acv, XX_FILE_TYPE_UNKNOWN },
    { "adobe_act", mk_adobe_act, rm_adobe_act, XX_FILE_TYPE_UNKNOWN },
    { "ogre_skeleton", mk_ogre_skeleton, rm_ogre_skeleton, XX_FILE_TYPE_UNKNOWN },
    { "cal3d_skeleton", mk_cal3d_skeleton, rm_cal3d_skeleton, XX_FILE_TYPE_UNKNOWN },
    { "collada_dae", mk_collada_dae, rm_collada_dae, XX_FILE_TYPE_UNKNOWN },
    { "lightwave_scene", mk_lightwave_scene, rm_lightwave_scene, XX_FILE_TYPE_UNKNOWN },
    { "dsn6_density", mk_dsn6_density, rm_dsn6_density, XX_FILE_TYPE_UNKNOWN },
    { "crystallography_mtz", mk_crystallography_mtz, rm_crystallography_mtz, XX_FILE_TYPE_UNKNOWN },
    { "amira_mesh", mk_amira_mesh, rm_amira_mesh, XX_FILE_TYPE_UNKNOWN },
    { "tetgen_mesh", mk_tetgen_mesh, rm_tetgen_mesh, XX_FILE_TYPE_UNKNOWN },
    { "jedec_fuse", mk_jedec_fuse, rm_jedec_fuse, XX_FILE_TYPE_UNKNOWN },
    { "qchem_input", mk_qchem_input, rm_qchem_input, XX_FILE_TYPE_UNKNOWN },
    { "adlib_bam", mk_adlib_bam, rm_adlib_bam, XX_FILE_TYPE_UNKNOWN },
    { "adlib_bmf", mk_adlib_bmf, rm_adlib_bmf, XX_FILE_TYPE_UNKNOWN },
    { "creative_cmf", mk_creative_cmf, rm_creative_cmf, XX_FILE_TYPE_UNKNOWN },
    { "adlib_dfm", mk_adlib_dfm, rm_adlib_dfm, XX_FILE_TYPE_UNKNOWN },
    { "adlib_lds", mk_adlib_lds, rm_adlib_lds, XX_FILE_TYPE_UNKNOWN },
    { "adlib_mkj", mk_adlib_mkj, rm_adlib_mkj, XX_FILE_TYPE_UNKNOWN },
    { "adlib_rol", mk_adlib_rol, rm_adlib_rol, XX_FILE_TYPE_UNKNOWN },
    { "adlib_sa2", mk_adlib_sa2, rm_adlib_sa2, XX_FILE_TYPE_UNKNOWN },
    { "faust_fmc", mk_faust_fmc, rm_faust_fmc, XX_FILE_TYPE_UNKNOWN },
    { "softstar_rix", mk_softstar_rix, rm_softstar_rix, XX_FILE_TYPE_UNKNOWN },
    { "genomics_bed", mk_genomics_bed, rm_genomics_bed, XX_FILE_TYPE_UNKNOWN },
    { "genomics_wiggle", mk_genomics_wiggle, rm_genomics_wiggle, XX_FILE_TYPE_UNKNOWN },
    { "genomics_gtf", mk_genomics_gtf, rm_genomics_gtf, XX_FILE_TYPE_UNKNOWN },
    { "genomics_agp", mk_genomics_agp, rm_genomics_agp, XX_FILE_TYPE_UNKNOWN },
    { "sequencing_abif", mk_sequencing_abif, rm_sequencing_abif, XX_FILE_TYPE_UNKNOWN },
    { "sequencing_scf", mk_sequencing_scf, rm_sequencing_scf, XX_FILE_TYPE_UNKNOWN },
    { "genomics_sff", mk_genomics_sff, rm_genomics_sff, XX_FILE_TYPE_UNKNOWN },
    { "lut_spi1d", mk_lut_spi1d, rm_lut_spi1d, XX_FILE_TYPE_UNKNOWN },
    { "lut_spi3d", mk_lut_spi3d, rm_lut_spi3d, XX_FILE_TYPE_UNKNOWN },
    { "lut_cinespace_csp", mk_lut_cinespace_csp, rm_lut_cinespace_csp, XX_FILE_TYPE_UNKNOWN },
    { "modbus_tcp", mk_modbus_tcp, rm_modbus_tcp, XX_FILE_TYPE_UNKNOWN },
    { "someip_message", mk_someip_message, rm_someip_message, XX_FILE_TYPE_UNKNOWN },
    { "dds_rtps", mk_dds_rtps, rm_dds_rtps, XX_FILE_TYPE_UNKNOWN },
    { "rip_message", mk_rip_message, rm_rip_message, XX_FILE_TYPE_UNKNOWN },
    { "vrrp_message", mk_vrrp_message, rm_vrrp_message, XX_FILE_TYPE_UNKNOWN },
    { "igmp_message", mk_igmp_message, rm_igmp_message, XX_FILE_TYPE_UNKNOWN },
    { "pim_message", mk_pim_message, rm_pim_message, XX_FILE_TYPE_UNKNOWN },
    { "ldp_message", mk_ldp_message, rm_ldp_message, XX_FILE_TYPE_UNKNOWN },
    { "gre_packet", mk_gre_packet, rm_gre_packet, XX_FILE_TYPE_UNKNOWN },
    { "l2tp_packet", mk_l2tp_packet, rm_l2tp_packet, XX_FILE_TYPE_UNKNOWN },
    { "lldp_message", mk_lldp_message, rm_lldp_message, XX_FILE_TYPE_UNKNOWN },
    { "netflow_datagram", mk_netflow_datagram, rm_netflow_datagram, XX_FILE_TYPE_UNKNOWN },
    { "ntlm_message", mk_ntlm_message, rm_ntlm_message, XX_FILE_TYPE_UNKNOWN },
    { "dcerpc_pdu", mk_dcerpc_pdu, rm_dcerpc_pdu, XX_FILE_TYPE_UNKNOWN },
    { "ethereum_rlp", mk_ethereum_rlp, rm_ethereum_rlp, XX_FILE_TYPE_UNKNOWN },
    { "imagemagick_miff", mk_imagemagick_miff, rm_imagemagick_miff, XX_FILE_TYPE_UNKNOWN },
    { "avs_image", mk_avs_image, rm_avs_image, XX_FILE_TYPE_UNKNOWN },
    { "scanalytics_iplab", mk_scanalytics_iplab, rm_scanalytics_iplab, XX_FILE_TYPE_UNKNOWN },
    { "mtv_image", mk_mtv_image, rm_mtv_image, XX_FILE_TYPE_UNKNOWN },
    { "nokia_ota_bitmap", mk_nokia_ota_bitmap, rm_nokia_ota_bitmap, XX_FILE_TYPE_UNKNOWN },
    { "apple_pict", mk_apple_pict, rm_apple_pict, XX_FILE_TYPE_UNKNOWN },
    { "wordperfect_wpg", mk_wordperfect_wpg, rm_wordperfect_wpg, XX_FILE_TYPE_UNKNOWN },
    { "nasa_vicar", mk_nasa_vicar, rm_nasa_vicar, XX_FILE_TYPE_UNKNOWN },
    { "khoros_viff", mk_khoros_viff, rm_khoros_viff, XX_FILE_TYPE_UNKNOWN },
    { "imagemagick_mvg", mk_imagemagick_mvg, rm_imagemagick_mvg, XX_FILE_TYPE_UNKNOWN },
    { "motif_uil", mk_motif_uil, rm_motif_uil, XX_FILE_TYPE_UNKNOWN },
    { "iges_model", mk_iges_model, rm_iges_model, XX_FILE_TYPE_UNKNOWN },
    { "openusd_usda", mk_openusd_usda, rm_openusd_usda, XX_FILE_TYPE_UNKNOWN },
    { "ufo_glif", mk_ufo_glif, rm_ufo_glif, XX_FILE_TYPE_UNKNOWN },
    { "unifont_hex", mk_unifont_hex, rm_unifont_hex, XX_FILE_TYPE_UNKNOWN },
    { "adlib_sop", mk_adlib_sop, rm_adlib_sop, XX_FILE_TYPE_UNKNOWN },
    { "cudfm_cff", mk_cudfm_cff, rm_cudfm_cff, XX_FILE_TYPE_UNKNOWN },
    { "adlib_jbm", mk_adlib_jbm, rm_adlib_jbm, XX_FILE_TYPE_UNKNOWN },
    { "ceres_msc", mk_ceres_msc, rm_ceres_msc, XX_FILE_TYPE_UNKNOWN },
    { "adlib_xsm", mk_adlib_xsm, rm_adlib_xsm, XX_FILE_TYPE_UNKNOWN },
    { "ken_ksm", mk_ken_ksm, rm_ken_ksm, XX_FILE_TYPE_UNKNOWN },
    { "implay_music", mk_implay_music, rm_implay_music, XX_FILE_TYPE_UNKNOWN },
    { "adlib_mtr", mk_adlib_mtr, rm_adlib_mtr, XX_FILE_TYPE_UNKNOWN },
    { "rdos_raw", mk_rdos_raw, rm_rdos_raw, XX_FILE_TYPE_UNKNOWN },
    { "mad_tracker", mk_mad_tracker, rm_mad_tracker, XX_FILE_TYPE_UNKNOWN },
    { "vasp_poscar", mk_vasp_poscar, rm_vasp_poscar, XX_FILE_TYPE_UNKNOWN },
    { "quantum_espresso_input", mk_quantum_espresso_input, rm_quantum_espresso_input, XX_FILE_TYPE_UNKNOWN },
    { "cp2k_input", mk_cp2k_input, rm_cp2k_input, XX_FILE_TYPE_UNKNOWN },
    { "nwchem_input", mk_nwchem_input, rm_nwchem_input, XX_FILE_TYPE_UNKNOWN },
    { "gamess_input", mk_gamess_input, rm_gamess_input, XX_FILE_TYPE_UNKNOWN },
    { "gaussian_input", mk_gaussian_input, rm_gaussian_input, XX_FILE_TYPE_UNKNOWN },
    { "abinit_input", mk_abinit_input, rm_abinit_input, XX_FILE_TYPE_UNKNOWN },
    { "aims_geometry", mk_aims_geometry, rm_aims_geometry, XX_FILE_TYPE_UNKNOWN },
    { "orca_input", mk_orca_input, rm_orca_input, XX_FILE_TYPE_UNKNOWN },
    { "demon_input", mk_demon_input, rm_demon_input, XX_FILE_TYPE_UNKNOWN },
    { "sfxstart", mk_sfxstart, rm_sfxstart, XX_FILE_TYPE_UNKNOWN },
    { "createinstall_instcrin_extractor", mk_createinstall_instcrin_extractor, rm_createinstall_instcrin_extractor, XX_FILE_TYPE_UNKNOWN },
    { "clickteam_install_creator", mk_clickteam_install_creator, rm_clickteam_install_creator, XX_FILE_TYPE_UNKNOWN },
    { "gentee_installer", mk_gentee_installer, rm_gentee_installer, XX_FILE_TYPE_UNKNOWN },
    { "eschalon_setup_epsf", mk_eschalon_setup_epsf, rm_eschalon_setup_epsf, XX_FILE_TYPE_UNKNOWN },
    { "wise_installation_system", mk_wise_installation_system, rm_wise_installation_system, XX_FILE_TYPE_UNKNOWN },
    { "sfx_wasp_windows_auto", mk_sfx_wasp_windows_auto, rm_sfx_wasp_windows_auto, XX_FILE_TYPE_UNKNOWN },
    { "sfx_compaq_softpaq", mk_sfx_compaq_softpaq, rm_sfx_compaq_softpaq, XX_FILE_TYPE_UNKNOWN },
    { "sfx_softpaq4", mk_sfx_softpaq4, rm_sfx_softpaq4, XX_FILE_TYPE_UNKNOWN },
    { "sfx_sydex_diskette_image", mk_sfx_sydex_diskette_image, rm_sfx_sydex_diskette_image, XX_FILE_TYPE_UNKNOWN },
    { "sfx_nullsoft_pimp", mk_sfx_nullsoft_pimp, rm_sfx_nullsoft_pimp, XX_FILE_TYPE_UNKNOWN },
    { "sfx_ardi_diskette_image", mk_sfx_ardi_diskette_image, rm_sfx_ardi_diskette_image, XX_FILE_TYPE_UNKNOWN },
    { "sfx_jgsoft_deploymaster_package", mk_sfx_jgsoft_deploymaster_package, rm_sfx_jgsoft_deploymaster_package, XX_FILE_TYPE_UNKNOWN },
    { "sfx_flashjester_jugglor", mk_sfx_flashjester_jugglor, rm_sfx_flashjester_jugglor, XX_FILE_TYPE_UNKNOWN },
    { "sfx_abbyy_fine_objects", mk_sfx_abbyy_fine_objects, rm_sfx_abbyy_fine_objects, XX_FILE_TYPE_UNKNOWN },
    { "sfx_clickteam_multimedia_fusion", mk_sfx_clickteam_multimedia_fusion, rm_sfx_clickteam_multimedia_fusion, XX_FILE_TYPE_UNKNOWN },
    { "sfx_hci_instalit", mk_sfx_hci_instalit, rm_sfx_hci_instalit, XX_FILE_TYPE_UNKNOWN },
    { "sfx_warpin_package", mk_sfx_warpin_package, rm_sfx_warpin_package, XX_FILE_TYPE_UNKNOWN },
    { "sfx_krzip", mk_sfx_krzip, rm_sfx_krzip, XX_FILE_TYPE_UNKNOWN },
    { "sfx_analogx_emucore_ffs", mk_sfx_analogx_emucore_ffs, rm_sfx_analogx_emucore_ffs, XX_FILE_TYPE_UNKNOWN },
    { "7zip", mk_7zip, rm_7zip, XX_FILE_TYPE_UNKNOWN },
    { "ace", mk_ace, rm_ace, XX_FILE_TYPE_UNKNOWN },
    { "agis", mk_agis, rm_agis, XX_FILE_TYPE_UNKNOWN },
    { "aiaff", mk_aiaff, rm_aiaff, XX_FILE_TYPE_UNKNOWN },
    { "ain", mk_ain, rm_ain, XX_FILE_TYPE_UNKNOWN },
    { "aixbff", mk_aixbff, rm_aixbff, XX_FILE_TYPE_UNKNOWN },
    { "aldus", mk_aldus, rm_aldus, XX_FILE_TYPE_UNKNOWN },
    { "alz", mk_alz, rm_alz, XX_FILE_TYPE_UNKNOWN },
    { "amigahunk", mk_amigahunk, rm_amigahunk, XX_FILE_TYPE_UNKNOWN },
    { "amigalzx", mk_amigalzx, rm_amigalzx, XX_FILE_TYPE_UNKNOWN },
    { "ampk", mk_ampk, rm_ampk, XX_FILE_TYPE_UNKNOWN },
    { "androidboot", mk_androidboot, rm_androidboot, XX_FILE_TYPE_UNKNOWN },
    { "aodos", mk_aodos, rm_aodos, XX_FILE_TYPE_UNKNOWN },
    { "ap4", mk_ap4, rm_ap4, XX_FILE_TYPE_UNKNOWN },
    { "apfs", mk_apfs, rm_apfs, XX_FILE_TYPE_UNKNOWN },
    { "apk", mk_apk, rm_apk, XX_FILE_TYPE_UNKNOWN },
    { "applesingle", mk_applesingle, rm_applesingle, XX_FILE_TYPE_UNKNOWN },
    { "apricot", mk_apricot, rm_apricot, XX_FILE_TYPE_UNKNOWN },
    { "ar", mk_ar, rm_ar, XX_FILE_TYPE_UNKNOWN },
    { "arcadyan", mk_arcadyan, rm_arcadyan, XX_FILE_TYPE_UNKNOWN },
    { "arcfs", mk_arcfs, rm_arcfs, XX_FILE_TYPE_UNKNOWN },
    { "arcv", mk_arcv, rm_arcv, XX_FILE_TYPE_UNKNOWN },
    { "arcv2", mk_arcv2, rm_arcv2, XX_FILE_TYPE_UNKNOWN },
    { "arcv4", mk_arcv4, rm_arcv4, XX_FILE_TYPE_UNKNOWN },
    { "arj", mk_arj, rm_arj, XX_FILE_TYPE_UNKNOWN },
    { "arq", mk_arq, rm_arq, XX_FILE_TYPE_UNKNOWN },
    { "artipack", mk_artipack, rm_artipack, XX_FILE_TYPE_UNKNOWN },
    { "arx", mk_arx, rm_arx, XX_FILE_TYPE_UNKNOWN },
    { "asar", mk_asar, rm_asar, XX_FILE_TYPE_UNKNOWN },
    { "ascend", mk_ascend, rm_ascend, XX_FILE_TYPE_UNKNOWN },
    { "ascendbackup", mk_ascendbackup, rm_ascendbackup, XX_FILE_TYPE_UNKNOWN },
    { "ash0", mk_ash0, rm_ash0, XX_FILE_TYPE_UNKNOWN },
    { "asymetrix", mk_asymetrix, rm_asymetrix, XX_FILE_TYPE_UNKNOWN },
    { "atarist", mk_atarist, rm_atarist, XX_FILE_TYPE_UNKNOWN },
    { "autel", mk_autel, rm_autel, XX_FILE_TYPE_UNKNOWN },
    { "bagf", mk_bagf, rm_bagf, XX_FILE_TYPE_UNKNOWN },
    { "battleisle", mk_battleisle, rm_battleisle, XX_FILE_TYPE_UNKNOWN },
    { "bcm", mk_bcm, rm_bcm, XX_FILE_TYPE_UNKNOWN },
    { "bcw", mk_bcw, rm_bcw, XX_FILE_TYPE_UNKNOWN },
    { "beatthehouse", mk_beatthehouse, rm_beatthehouse, XX_FILE_TYPE_UNKNOWN },
    { "beospkg", mk_beospkg, rm_beospkg, XX_FILE_TYPE_UNKNOWN },
    { "bigaf", mk_bigaf, rm_bigaf, XX_FILE_TYPE_UNKNOWN },
    { "bigf", mk_bigf, rm_bigf, XX_FILE_TYPE_UNKNOWN },
    { "binaryii", mk_binaryii, rm_binaryii, XX_FILE_TYPE_UNKNOWN },
    { "binder", mk_binder, rm_binder, XX_FILE_TYPE_UNKNOWN },
    { "binhdr", mk_binhdr, rm_binhdr, XX_FILE_TYPE_UNKNOWN },
    { "binhex", mk_binhex, rm_binhex, XX_FILE_TYPE_UNKNOWN },
    { "bnd", mk_bnd, rm_bnd, XX_FILE_TYPE_UNKNOWN },
    { "boo", mk_boo, rm_boo, XX_FILE_TYPE_UNKNOWN },
    { "borlandpack", mk_borlandpack, rm_borlandpack, XX_FILE_TYPE_UNKNOWN },
    { "brotli", mk_brotli, rm_brotli, XX_FILE_TYPE_UNKNOWN },
    { "bsn", mk_bsn, rm_bsn, XX_FILE_TYPE_UNKNOWN },
    { "btrfs", mk_btrfs, rm_btrfs, XX_FILE_TYPE_UNKNOWN },
    { "bvrp", mk_bvrp, rm_bvrp, XX_FILE_TYPE_UNKNOWN },
    { "bwcf", mk_bwcf, rm_bwcf, XX_FILE_TYPE_UNKNOWN },
    { "bwf", mk_bwf, rm_bwf, XX_FILE_TYPE_UNKNOWN },
    { "bz2", mk_bz2, rm_bz2, XX_FILE_TYPE_UNKNOWN },
    { "c64wraptor", mk_c64wraptor, rm_c64wraptor, XX_FILE_TYPE_UNKNOWN },
    { "cab", mk_cab, rm_cab, XX_FILE_TYPE_UNKNOWN },
    { "cat", mk_cat, rm_cat, XX_FILE_TYPE_UNKNOWN },
    { "cpoint", mk_cpoint, rm_cpoint, XX_FILE_TYPE_CPOINT },
    { "cazip", mk_cazip, rm_cazip, XX_FILE_TYPE_UNKNOWN },
    { "cfl", mk_cfl, rm_cfl, XX_FILE_TYPE_UNKNOWN },
    { "chieflz", mk_chieflz, rm_chieflz, XX_FILE_TYPE_UNKNOWN },
    { "chieflzmulti", mk_chieflzmulti, rm_chieflzmulti, XX_FILE_TYPE_UNKNOWN },
    { "chk", mk_chk, rm_chk, XX_FILE_TYPE_UNKNOWN },
    { "ciso", mk_ciso, rm_ciso, XX_FILE_TYPE_UNKNOWN },
    { "ciso2", mk_ciso, rm_ciso, XX_FILE_TYPE_CISO2 },
    { "ziso", mk_ciso, rm_ciso, XX_FILE_TYPE_ZISO },
    { "dax", mk_ciso, rm_ciso, XX_FILE_TYPE_DAX },
    { "claylz", mk_claylz, rm_claylz, XX_FILE_TYPE_UNKNOWN },
    { "clp", mk_clp, rm_clp, XX_FILE_TYPE_UNKNOWN },
    { "cmp", mk_cmp, rm_cmp, XX_FILE_TYPE_UNKNOWN },
    { "com", mk_com, rm_com, XX_FILE_TYPE_UNKNOWN },
    { "compactpro", mk_compactpro, rm_compactpro, XX_FILE_TYPE_UNKNOWN },
    { "compaqlzh", mk_compaqlzh, rm_compaqlzh, XX_FILE_TYPE_UNKNOWN },
    { "copydisk", mk_copydisk, rm_copydisk, XX_FILE_TYPE_UNKNOWN },
    { "copyqm", mk_copyqm, rm_copyqm, XX_FILE_TYPE_UNKNOWN },
    { "copyqmexe", mk_copyqmexe, rm_copyqmexe, XX_FILE_TYPE_UNKNOWN },
    { "corelltec", mk_corelltec, rm_corelltec, XX_FILE_TYPE_UNKNOWN },
    { "cpio", mk_cpio, rm_cpio, XX_FILE_TYPE_UNKNOWN },
    { "cpx", mk_cpx, rm_cpx, XX_FILE_TYPE_UNKNOWN },
    { "cramfs", mk_cramfs, rm_cramfs, XX_FILE_TYPE_UNKNOWN },
    { "cru", mk_cru, rm_cru, XX_FILE_TYPE_UNKNOWN },
    { "csidos", mk_csidos, rm_csidos, XX_FILE_TYPE_UNKNOWN },
    { "csman", mk_csman, rm_csman, XX_FILE_TYPE_UNKNOWN },
    { "dbz", mk_dbz, rm_dbz, XX_FILE_TYPE_UNKNOWN },
    { "dclft", mk_dclft, rm_dclft, XX_FILE_TYPE_UNKNOWN },
    { "dclraw", mk_dclraw, rm_dclraw, XX_FILE_TYPE_UNKNOWN },
    { "debugscr", mk_debugscr, rm_debugscr, XX_FILE_TYPE_UNKNOWN },
    { "dex", mk_dex, rm_dex, XX_FILE_TYPE_UNKNOWN },
    { "diskdoubler", mk_diskdoubler, rm_diskdoubler, XX_FILE_TYPE_UNKNOWN },
    { "diskdupe", mk_diskdupe, rm_diskdupe, XX_FILE_TYPE_UNKNOWN },
    { "diskexpress", mk_diskexpress, rm_diskexpress, XX_FILE_TYPE_UNKNOWN },
    { "diskjuggler", mk_diskjuggler, rm_diskjuggler, XX_FILE_TYPE_UNKNOWN },
    { "dkbs", mk_dkbs, rm_dkbs, XX_FILE_TYPE_UNKNOWN },
    { "dlink_tlv", mk_dlink_tlv, rm_dlink_tlv, XX_FILE_TYPE_UNKNOWN },
    { "dlke", mk_dlke, rm_dlke, XX_FILE_TYPE_UNKNOWN },
    { "dlob", mk_dlob, rm_dlob, XX_FILE_TYPE_UNKNOWN },
    { "dmapacked", mk_dmapacked, rm_dmapacked, XX_FILE_TYPE_UNKNOWN },
    { "dmg", mk_dmg, rm_dmg, XX_FILE_TYPE_UNKNOWN },
    { "dms", mk_dms, rm_dms, XX_FILE_TYPE_UNKNOWN },
    { "dos16m", mk_dos16m, rm_dos16m, XX_FILE_TYPE_UNKNOWN },
    { "dpk", mk_dpk, rm_dpk, XX_FILE_TYPE_UNKNOWN },
    { "dsl2", mk_dsl2, rm_dsl2, XX_FILE_TYPE_UNKNOWN },
    { "dtb", mk_dtb, rm_dtb, XX_FILE_TYPE_UNKNOWN },
    { "dtpacked", mk_dtpacked, rm_dtpacked, XX_FILE_TYPE_UNKNOWN },
    { "ea", mk_ea, rm_ea, XX_FILE_TYPE_UNKNOWN },
    { "ealib", mk_ealib, rm_ealib, XX_FILE_TYPE_UNKNOWN },
    { "elm", mk_elm, rm_elm, XX_FILE_TYPE_UNKNOWN },
    { "earefpack", mk_earefpack, rm_earefpack, XX_FILE_TYPE_UNKNOWN },
    { "ecmpacked", mk_ecmpacked, rm_ecmpacked, XX_FILE_TYPE_UNKNOWN },
    { "ecos", mk_ecos, rm_ecos, XX_FILE_TYPE_UNKNOWN },
    { "edc", mk_edc, rm_edc, XX_FILE_TYPE_UNKNOWN },
    { "edilzss", mk_edilzss, rm_edilzss, XX_FILE_TYPE_UNKNOWN },
    { "elf", mk_elf, rm_elf, XX_FILE_TYPE_UNKNOWN },
    { "emt", mk_emt, rm_emt, XX_FILE_TYPE_UNKNOWN },
    { "encfw", mk_encfw, rm_encfw, XX_FILE_TYPE_UNKNOWN },
    { "encrpted_img", mk_encrpted_img, rm_encrpted_img, XX_FILE_TYPE_UNKNOWN },
    { "ext", mk_ext, rm_ext, XX_FILE_TYPE_UNKNOWN },
    { "erofs", mk_erofs, rm_erofs, XX_FILE_TYPE_EROFS },
    { "fat", mk_fat, rm_fat, XX_FILE_TYPE_UNKNOWN },
    { "fdi", mk_fdi, rm_fdi, XX_FILE_TYPE_UNKNOWN },
    { "finear", mk_finear, rm_finear, XX_FILE_TYPE_UNKNOWN },
    { "fiz", mk_fiz, rm_fiz, XX_FILE_TYPE_UNKNOWN },
    { "fld", mk_fld, rm_fld, XX_FILE_TYPE_UNKNOWN },
    { "fls", mk_fls, rm_fls, XX_FILE_TYPE_UNKNOWN },
    { "fmc1", mk_fmc1, rm_fmc1, XX_FILE_TYPE_UNKNOWN },
    { "fpak", mk_fpak, rm_fpak, XX_FILE_TYPE_UNKNOWN },
    { "freearc", mk_freearc, rm_freearc, XX_FILE_TYPE_UNKNOWN },
    { "frontpagetheme", mk_frontpagetheme, rm_frontpagetheme, XX_FILE_TYPE_UNKNOWN },
    { "ftcomp", mk_ftcomp, rm_ftcomp, XX_FILE_TYPE_UNKNOWN },
    { "gamos", mk_gamos, rm_gamos, XX_FILE_TYPE_UNKNOWN },
    { "gashuff", mk_gashuff, rm_gashuff, XX_FILE_TYPE_UNKNOWN },
    { "genius", mk_genius, rm_genius, XX_FILE_TYPE_UNKNOWN },
    { "gitobject", mk_gitobject, rm_gitobject, XX_FILE_TYPE_UNKNOWN },
    { "gksetup", mk_gksetup, rm_gksetup, XX_FILE_TYPE_UNKNOWN },
    { "glu", mk_glu, rm_glu, XX_FILE_TYPE_UNKNOWN },
    { "gob", mk_gob, rm_gob, XX_FILE_TYPE_UNKNOWN },
    { "gpfpack", mk_gpfpack, rm_gpfpack, XX_FILE_TYPE_UNKNOWN },
    { "gpt", mk_gpt, rm_gpt, XX_FILE_TYPE_UNKNOWN },
    { "grasp", mk_grasp, rm_grasp, XX_FILE_TYPE_UNKNOWN },
    { "gst", mk_gst, rm_gst, XX_FILE_TYPE_UNKNOWN },
    { "gtu", mk_gtu, rm_gtu, XX_FILE_TYPE_UNKNOWN },
    { "gxl", mk_gxl, rm_gxl, XX_FILE_TYPE_UNKNOWN },
    { "gz", mk_gz, rm_gz, XX_FILE_TYPE_UNKNOWN },
    { "ha", mk_ha, rm_ha, XX_FILE_TYPE_UNKNOWN },
    { "hap", mk_hap, rm_hap, XX_FILE_TYPE_UNKNOWN },
    { "hdcopy", mk_hdcopy, rm_hdcopy, XX_FILE_TYPE_UNKNOWN },
    { "hfe", mk_hfe, rm_hfe, XX_FILE_TYPE_UNKNOWN },
    { "hlb", mk_hlb, rm_hlb, XX_FILE_TYPE_UNKNOWN },
    { "hog", mk_hog, rm_hog, XX_FILE_TYPE_UNKNOWN },
    { "hog2", mk_hog2, rm_hog2, XX_FILE_TYPE_UNKNOWN },
    { "huf", mk_huf, rm_huf, XX_FILE_TYPE_UNKNOWN },
    { "hzl", mk_hzl, rm_hzl, XX_FILE_TYPE_UNKNOWN },
    { "ibmpack", mk_ibmpack, rm_ibmpack, XX_FILE_TYPE_UNKNOWN },
    { "ibmspack", mk_ibmspack, rm_ibmspack, XX_FILE_TYPE_UNKNOWN },
    { "ibmzpak", mk_ibmzpak, rm_ibmzpak, XX_FILE_TYPE_UNKNOWN },
    { "igf1", mk_igf1, rm_igf1, XX_FILE_TYPE_UNKNOWN },
    { "igf2", mk_igf2, rm_igf2, XX_FILE_TYPE_UNKNOWN },
    { "imd", mk_imd, rm_imd, XX_FILE_TYPE_UNKNOWN },
    { "imp", mk_imp, rm_imp, XX_FILE_TYPE_UNKNOWN },
    { "infogramesft", mk_infogramesft, rm_infogramesft, XX_FILE_TYPE_UNKNOWN },
    { "inteduft", mk_inteduft, rm_inteduft, XX_FILE_TYPE_UNKNOWN },
    { "ipa", mk_ipa, rm_ipa, XX_FILE_TYPE_UNKNOWN },
    { "irixsa", mk_irixsa, rm_irixsa, XX_FILE_TYPE_UNKNOWN },
    { "irwinpac", mk_irwinpac, rm_irwinpac, XX_FILE_TYPE_UNKNOWN },
    { "is11", mk_is11, rm_is11, XX_FILE_TYPE_UNKNOWN },
    { "is3", mk_is3, rm_is3, XX_FILE_TYPE_UNKNOWN },
    { "is5", mk_is5, rm_is5, XX_FILE_TYPE_UNKNOWN },
    { "is7inx", mk_is7inx, rm_is7inx, XX_FILE_TYPE_UNKNOWN },
    { "iso9660", mk_iso9660, rm_iso9660, XX_FILE_TYPE_UNKNOWN },
    { "ivt", mk_ivt, rm_ivt, XX_FILE_TYPE_UNKNOWN },
    { "ixa", mk_ixa, rm_ixa, XX_FILE_TYPE_UNKNOWN },
    { "mlb_ft", mk_mlb_ft, rm_mlb_ft, XX_FILE_TYPE_UNKNOWN },
    { "fss", mk_fss, rm_fss, XX_FILE_TYPE_UNKNOWN },
    { "epf", mk_epf, rm_epf, XX_FILE_TYPE_UNKNOWN },
    { "dfc", mk_dfc, rm_dfc, XX_FILE_TYPE_UNKNOWN },
    { "sfx_rsfx", mk_sfx_rsfx, rm_sfx_rsfx, XX_FILE_TYPE_UNKNOWN },
    { "ka", mk_ka, rm_ka, XX_FILE_TYPE_UNKNOWN },
    { "nextstep_diskimage", mk_nextstep_diskimage, rm_nextstep_diskimage,
      XX_FILE_TYPE_UNKNOWN },
    { "dn", mk_dn, rm_dn, XX_FILE_TYPE_UNKNOWN },
    { "insa", mk_insa, rm_insa, XX_FILE_TYPE_UNKNOWN },
    { "sfx_vms_dcx", mk_sfx_vms_dcx, rm_sfx_vms_dcx,
      XX_FILE_TYPE_UNKNOWN },
    { "oberon", mk_oberon, rm_oberon, XX_FILE_TYPE_UNKNOWN },
    { "ppd", mk_ppd, rm_ppd, XX_FILE_TYPE_UNKNOWN },
    { "sfx_ad01", mk_sfx_ad01, rm_sfx_ad01, XX_FILE_TYPE_UNKNOWN },
    { "sfx_nss", mk_sfx_nss, rm_sfx_nss, XX_FILE_TYPE_UNKNOWN },
    { "solitaire_deluxe", mk_solitaire_deluxe,
      rm_solitaire_deluxe, XX_FILE_TYPE_UNKNOWN },
    { "thebat_msb", mk_thebat_msb, rm_thebat_msb, XX_FILE_TYPE_UNKNOWN },
    { "sfx_localzip", mk_sfx_localzip, rm_sfx_localzip, XX_FILE_TYPE_UNKNOWN },
    { "izpack", mk_izpack, rm_izpack, XX_FILE_TYPE_UNKNOWN },
    { "jam", mk_jam, rm_jam, XX_FILE_TYPE_UNKNOWN },
    { "jar", mk_jar, rm_jar, XX_FILE_TYPE_UNKNOWN },
    { "jasc", mk_jasc, rm_jasc, XX_FILE_TYPE_UNKNOWN },
    { "jbf", mk_jbf, rm_jbf, XX_FILE_TYPE_UNKNOWN },
    { "jboot", mk_jboot, rm_jboot, XX_FILE_TYPE_UNKNOWN },
    { "jetbbs", mk_jetbbs, rm_jetbbs, XX_FILE_TYPE_UNKNOWN },
    { "jffs2", mk_jffs2, rm_jffs2, XX_FILE_TYPE_UNKNOWN },
    { "jgpak", mk_jgpak, rm_jgpak, XX_FILE_TYPE_UNKNOWN },
    { "jm93", mk_jm93, rm_jm93, XX_FILE_TYPE_UNKNOWN },
    { "kboom", mk_kboom, rm_kboom, XX_FILE_TYPE_UNKNOWN },
    { "kolibrikpack", mk_kolibrikpack, rm_kolibrikpack, XX_FILE_TYPE_UNKNOWN },
    { "kpck", mk_kpck, rm_kpck, XX_FILE_TYPE_UNKNOWN },
    { "krml", mk_krml, rm_krml, XX_FILE_TYPE_UNKNOWN },
    { "lbrcobol", mk_lbrcobol, rm_lbrcobol, XX_FILE_TYPE_UNKNOWN },
    { "le", mk_le, rm_le, XX_FILE_TYPE_UNKNOWN },
    { "lha", mk_lha, rm_lha, XX_FILE_TYPE_UNKNOWN },
    { "lif", mk_lif, rm_lif, XX_FILE_TYPE_UNKNOWN },
    { "lifkd", mk_lifkd, rm_lifkd, XX_FILE_TYPE_UNKNOWN },
    { "lim", mk_lim, rm_lim, XX_FILE_TYPE_UNKNOWN },
    { "lingvoarc", mk_lingvoarc, rm_lingvoarc, XX_FILE_TYPE_UNKNOWN },
    { "lizard", mk_lizard, rm_lizard, XX_FILE_TYPE_UNKNOWN },
    { "lofi", mk_lofi, rm_lofi, XX_FILE_TYPE_UNKNOWN },
    { "logfs", mk_logfs, rm_logfs, XX_FILE_TYPE_UNKNOWN },
    { "logitechcompress", mk_logitechcompress, rm_logitechcompress, XX_FILE_TYPE_UNKNOWN },
    { "lpaq8", mk_lpaq8, rm_lpaq8, XX_FILE_TYPE_UNKNOWN },
    { "lspack10", mk_lspack10, rm_lspack10, XX_FILE_TYPE_UNKNOWN },
    { "lsz", mk_lsz, rm_lsz, XX_FILE_TYPE_UNKNOWN },
    { "luks", mk_luks, rm_luks, XX_FILE_TYPE_UNKNOWN },
    { "lx", mk_lx, rm_lx, XX_FILE_TYPE_UNKNOWN },
    { "lz4", mk_lz4, rm_lz4, XX_FILE_TYPE_UNKNOWN },
    { "lz4demo", mk_lz4demo, rm_lz4demo, XX_FILE_TYPE_UNKNOWN },
    { "lz5", mk_lz5, rm_lz5, XX_FILE_TYPE_UNKNOWN },
    { "lzdiet", mk_lzdiet, rm_lzdiet, XX_FILE_TYPE_UNKNOWN },
    { "lzhcxp", mk_lzhcxp, rm_lzhcxp, XX_FILE_TYPE_UNKNOWN },
    { "lzip", mk_lzip, rm_lzip, XX_FILE_TYPE_UNKNOWN },
    { "lzk00", mk_lzk00, rm_lzk00, XX_FILE_TYPE_UNKNOWN },
    { "lzma", mk_lzma, rm_lzma, XX_FILE_TYPE_UNKNOWN },
    { "lzop", mk_lzop, rm_lzop, XX_FILE_TYPE_UNKNOWN },
    { "lzpis2", mk_lzpis2, rm_lzpis2, XX_FILE_TYPE_UNKNOWN },
    { "lzv1", mk_lzv1, rm_lzv1, XX_FILE_TYPE_UNKNOWN },
    { "lzw15v", mk_lzw15v, rm_lzw15v, XX_FILE_TYPE_UNKNOWN },
    { "lzwd", mk_lzwd, rm_lzwd, XX_FILE_TYPE_UNKNOWN },
    { "macbinary", mk_macbinary, rm_macbinary, XX_FILE_TYPE_UNKNOWN },
    { "macho", mk_macho, rm_macho, XX_FILE_TYPE_UNKNOWN },
    { "marc", mk_marc, rm_marc, XX_FILE_TYPE_UNKNOWN },
    { "mathcad", mk_mathcad, rm_mathcad, XX_FILE_TYPE_UNKNOWN },
    { "matter_ota", mk_matter_ota, rm_matter_ota, XX_FILE_TYPE_UNKNOWN },
    { "mbr", mk_mbr, rm_mbr, XX_FILE_TYPE_UNKNOWN },
    { "mcc", mk_mcc, rm_mcc, XX_FILE_TYPE_UNKNOWN },
    { "mdcd", mk_mdcd, rm_mdcd, XX_FILE_TYPE_UNKNOWN },
    { "megatechvol", mk_megatechvol, rm_megatechvol, XX_FILE_TYPE_UNKNOWN },
    { "mh01", mk_mh01, rm_mh01, XX_FILE_TYPE_UNKNOWN },
    { "mi10", mk_mi10, rm_mi10, XX_FILE_TYPE_UNKNOWN },
    { "minidump", mk_minidump, rm_minidump, XX_FILE_TYPE_UNKNOWN },
    { "miz", mk_miz, rm_miz, XX_FILE_TYPE_UNKNOWN },
    { "mpq", mk_mpq, rm_mpq, XX_FILE_TYPE_UNKNOWN },
    { "mrnz", mk_mrnz, rm_mrnz, XX_FILE_TYPE_UNKNOWN },
    { "mscompress", mk_mscompress, rm_mscompress, XX_FILE_TYPE_UNKNOWN },
    { "msdos", mk_msdos, rm_msdos, XX_FILE_TYPE_UNKNOWN },
    { "mtree", mk_mtree, rm_mtree, XX_FILE_TYPE_UNKNOWN },
    { "mva", mk_mva, rm_mva, XX_FILE_TYPE_UNKNOWN },
    { "mwave", mk_mwave, rm_mwave, XX_FILE_TYPE_UNKNOWN },
    { "mxs", mk_mxs, rm_mxs, XX_FILE_TYPE_UNKNOWN },
    { "ne", mk_ne, rm_ne, XX_FILE_TYPE_UNKNOWN },
    { "netware2", mk_netware2, rm_netware2, XX_FILE_TYPE_UNKNOWN },
    { "netwarepacked", mk_netwarepacked, rm_netwarepacked, XX_FILE_TYPE_UNKNOWN },
    { "nid", mk_nid, rm_nid, XX_FILE_TYPE_UNKNOWN },
    { "notetab", mk_notetab, rm_notetab, XX_FILE_TYPE_UNKNOWN },
    { "npack", mk_npack, rm_npack, XX_FILE_TYPE_UNKNOWN },
    { "npm", mk_npm, rm_npm, XX_FILE_TYPE_UNKNOWN },
    { "ntfs", mk_ntfs, rm_ntfs, XX_FILE_TYPE_UNKNOWN },
    { "opc", mk_opc, rm_opc, XX_FILE_TYPE_UNKNOWN },
    { "oraclesqueeze", mk_oraclesqueeze, rm_oraclesqueeze, XX_FILE_TYPE_UNKNOWN },
    { "packimg", mk_packimg, rm_packimg, XX_FILE_TYPE_UNKNOWN },
    { "packit", mk_packit, rm_packit, XX_FILE_TYPE_UNKNOWN },
    { "pain", mk_pain, rm_pain, XX_FILE_TYPE_UNKNOWN },
    { "pakleo", mk_pakleo, rm_pakleo, XX_FILE_TYPE_UNKNOWN },
    { "panorama", mk_panorama, rm_panorama, XX_FILE_TYPE_UNKNOWN },
    { "paperport", mk_paperport, rm_paperport, XX_FILE_TYPE_UNKNOWN },
    { "pax", mk_pax, rm_pax, XX_FILE_TYPE_UNKNOWN },
    { "pcinstall", mk_pcinstall, rm_pcinstall, XX_FILE_TYPE_UNKNOWN },
    { "pcommos2", mk_pcommos2, rm_pcommos2, XX_FILE_TYPE_UNKNOWN },
    { "pcsecure", mk_pcsecure, rm_pcsecure, XX_FILE_TYPE_UNKNOWN },
    { "pcxlib", mk_pcxlib, rm_pcxlib, XX_FILE_TYPE_UNKNOWN },
    { "pdb", mk_pdb, rm_pdb, XX_FILE_TYPE_UNKNOWN },
    { "pdp11ar", mk_pdp11ar, rm_pdp11ar, XX_FILE_TYPE_UNKNOWN },
    { "pe", mk_pe, rm_pe, XX_FILE_TYPE_UNKNOWN },
    { "pea", mk_pea, rm_pea, XX_FILE_TYPE_UNKNOWN },
    { "perform", mk_perform, rm_perform, XX_FILE_TYPE_UNKNOWN },
    { "phar", mk_phar, rm_phar, XX_FILE_TYPE_UNKNOWN },
    { "pkt", mk_pkt, rm_pkt, XX_FILE_TYPE_UNKNOWN },
    { "pma", mk_pma, rm_pma, XX_FILE_TYPE_UNKNOWN },
    { "pmdiskcopy", mk_pmdiskcopy, rm_pmdiskcopy, XX_FILE_TYPE_UNKNOWN },
    { "povlablzh", mk_povlablzh, rm_povlablzh, XX_FILE_TYPE_UNKNOWN },
    { "powerarc", mk_powerarc, rm_powerarc, XX_FILE_TYPE_UNKNOWN },
    { "powerboardbbs", mk_powerboardbbs, rm_powerboardbbs, XX_FILE_TYPE_UNKNOWN },
    { "pp20", mk_pp20, rm_pp20, XX_FILE_TYPE_UNKNOWN },
    { "psdc", mk_psdc, rm_psdc, XX_FILE_TYPE_UNKNOWN },
    { "psn", mk_psn, rm_psn, XX_FILE_TYPE_UNKNOWN },
    { "pyz", mk_pyz, rm_pyz, XX_FILE_TYPE_UNKNOWN },
    { "qcow", mk_qcow, rm_qcow, XX_FILE_TYPE_UNKNOWN },
    { "qda", mk_qda, rm_qda, XX_FILE_TYPE_UNKNOWN },
    { "qip1", mk_qip1, rm_qip1, XX_FILE_TYPE_UNKNOWN },
    { "qip2", mk_qip2, rm_qip2, XX_FILE_TYPE_UNKNOWN },
    { "qnx6", mk_qnx6, rm_qnx6, XX_FILE_TYPE_UNKNOWN },
    { "qnxbase", mk_qnxbase, rm_qnxbase, XX_FILE_TYPE_UNKNOWN },
    { "qrst", mk_qrst, rm_qrst, XX_FILE_TYPE_UNKNOWN },
    { "qualitas", mk_qualitas, rm_qualitas, XX_FILE_TYPE_UNKNOWN },
    { "quantum", mk_quantum, rm_quantum, XX_FILE_TYPE_UNKNOWN },
    { "quarterdeckqp", mk_quarterdeckqp, rm_quarterdeckqp, XX_FILE_TYPE_UNKNOWN },
    { "rar", mk_rar, rm_rar, XX_FILE_TYPE_UNKNOWN },
    { "rawstac", mk_rawstac, rm_rawstac, XX_FILE_TYPE_UNKNOWN },
    { "rcf", mk_rcf, rm_rcf, XX_FILE_TYPE_UNKNOWN },
    { "recognita", mk_recognita, rm_recognita, XX_FILE_TYPE_UNKNOWN },
    { "red", mk_red, rm_red, XX_FILE_TYPE_UNKNOWN },
    { "res", mk_res, rm_res, XX_FILE_TYPE_UNKNOWN },
    { "resourcefork", mk_resourcefork, rm_resourcefork, XX_FILE_TYPE_UNKNOWN },
    { "rid", mk_rid, rm_rid, XX_FILE_TYPE_UNKNOWN },
    { "riversoft", mk_riversoft, rm_riversoft, XX_FILE_TYPE_UNKNOWN },
    { "rnc", mk_rnc, rm_rnc, XX_FILE_TYPE_UNKNOWN },
    { "rnca", mk_rnca, rm_rnca, XX_FILE_TYPE_UNKNOWN },
    { "romfs", mk_romfs, rm_romfs, XX_FILE_TYPE_UNKNOWN },
    { "rompaq", mk_rompaq, rm_rompaq, XX_FILE_TYPE_UNKNOWN },
    { "rsc", mk_rsc, rm_rsc, XX_FILE_TYPE_UNKNOWN },
    { "rsvk", mk_rsvk, rm_rsvk, XX_FILE_TYPE_UNKNOWN },
    { "rta", mk_rta, rm_rta, XX_FILE_TYPE_UNKNOWN },
    { "rtk", mk_rtk, rm_rtk, XX_FILE_TYPE_UNKNOWN },
    { "rtpatch", mk_rtpatch, rm_rtpatch, XX_FILE_TYPE_UNKNOWN },
    { "sabdu", mk_sabdu, rm_sabdu, XX_FILE_TYPE_UNKNOWN },
    { "saf", mk_saf, rm_saf, XX_FILE_TYPE_UNKNOWN },
    { "savedskf", mk_savedskf, rm_savedskf, XX_FILE_TYPE_UNKNOWN },
    { "scf", mk_scf, rm_scf, XX_FILE_TYPE_UNKNOWN },
    { "sci", mk_sci, rm_sci, XX_FILE_TYPE_UNKNOWN },
    { "scl", mk_scl, rm_scl, XX_FILE_TYPE_UNKNOWN },
    { "sco", mk_sco, rm_sco, XX_FILE_TYPE_UNKNOWN },
    { "seaarc", mk_seaarc, rm_seaarc, XX_FILE_TYPE_UNKNOWN },
    { "seadata", mk_seadata, rm_seadata, XX_FILE_TYPE_UNKNOWN },
    { "seama", mk_seama, rm_seama, XX_FILE_TYPE_UNKNOWN },
    { "secondnature", mk_secondnature, rm_secondnature, XX_FILE_TYPE_UNKNOWN },
    { "settlersft", mk_settlersft, rm_settlersft, XX_FILE_TYPE_UNKNOWN },
    { "sfpack", mk_sfpack, rm_sfpack, XX_FILE_TYPE_UNKNOWN },
    { "shar", mk_shar, rm_shar, XX_FILE_TYPE_UNKNOWN },
    { "shrinkwrap", mk_shrinkwrap, rm_shrinkwrap, XX_FILE_TYPE_UNKNOWN },
    { "shrs", mk_shrs, rm_shrs, XX_FILE_TYPE_UNKNOWN },
    { "silmarilsft", mk_silmarilsft, rm_silmarilsft, XX_FILE_TYPE_UNKNOWN },
    { "sinner", mk_sinner, rm_sinner, XX_FILE_TYPE_UNKNOWN },
    { "sls", mk_sls, rm_sls, XX_FILE_TYPE_UNKNOWN },
    { "smsipak", mk_smsipak, rm_smsipak, XX_FILE_TYPE_UNKNOWN },
    { "softpaq2", mk_softpaq2, rm_softpaq2, XX_FILE_TYPE_UNKNOWN },
    { "softronics", mk_softronics, rm_softronics, XX_FILE_TYPE_UNKNOWN },
    { "solarispkg", mk_solarispkg, rm_solarispkg, XX_FILE_TYPE_UNKNOWN },
    { "sos", mk_sos, rm_sos, XX_FILE_TYPE_UNKNOWN },
    { "sparse", mk_sparse, rm_sparse, XX_FILE_TYPE_UNKNOWN },
    { "spis", mk_spis, rm_spis, XX_FILE_TYPE_UNKNOWN },
    { "sfx_sbx_extractor", mk_sfx_sbx_extractor, rm_sfx_sbx_extractor, XX_FILE_TYPE_UNKNOWN },
    { "spk", mk_spk, rm_spk, XX_FILE_TYPE_UNKNOWN },
    { "sq", mk_sq, rm_sq, XX_FILE_TYPE_UNKNOWN },
    { "squashfs", mk_squashfs, rm_squashfs, XX_FILE_TYPE_UNKNOWN },
    { "squeeze1", mk_squeeze1, rm_squeeze1, XX_FILE_TYPE_UNKNOWN },
    { "squeeze2", mk_squeeze2, rm_squeeze2, XX_FILE_TYPE_UNKNOWN },
    { "sqx", mk_sqx, rm_sqx, XX_FILE_TYPE_UNKNOWN },
    { "sqz", mk_sqz, rm_sqz, XX_FILE_TYPE_UNKNOWN },
    { "srec", mk_srec, rm_srec, XX_FILE_TYPE_UNKNOWN },
    { "ssm", mk_ssm, rm_ssm, XX_FILE_TYPE_UNKNOWN },
    { "stac", mk_stac, rm_stac, XX_FILE_TYPE_UNKNOWN },
    { "starkit", mk_starkit, rm_starkit, XX_FILE_TYPE_UNKNOWN },
    { "stk", mk_stk, rm_stk, XX_FILE_TYPE_UNKNOWN },
    { "stork", mk_stork, rm_stork, XX_FILE_TYPE_UNKNOWN },
    { "stuffit", mk_stuffit, rm_stuffit, XX_FILE_TYPE_UNKNOWN },
    { "stunts", mk_stunts, rm_stunts, XX_FILE_TYPE_UNKNOWN },
    { "stylus", mk_stylus, rm_stylus, XX_FILE_TYPE_UNKNOWN },
    { "sw", mk_sw, rm_sw, XX_FILE_TYPE_UNKNOWN },
    { "swag", mk_swag, rm_swag, XX_FILE_TYPE_UNKNOWN },
    { "swagpacket", mk_swagpacket, rm_swagpacket, XX_FILE_TYPE_UNKNOWN },
    { "tar", mk_tar, rm_tar, XX_FILE_TYPE_UNKNOWN },
    { "tar_bz2", mk_tar_bz2, rm_tar_bz2, XX_FILE_TYPE_UNKNOWN },
    { "tar_compress", mk_tar_compress, rm_tar_compress, XX_FILE_TYPE_UNKNOWN },
    { "tar_gz", mk_tar_gz, rm_tar_gz, XX_FILE_TYPE_UNKNOWN },
    { "tar_lz4", mk_tar_lz4, rm_tar_lz4, XX_FILE_TYPE_UNKNOWN },
    { "tar_lzip", mk_tar_lzip, rm_tar_lzip, XX_FILE_TYPE_UNKNOWN },
    { "tar_lzma", mk_tar_lzma, rm_tar_lzma, XX_FILE_TYPE_UNKNOWN },
    { "tar_lzop", mk_tar_lzop, rm_tar_lzop, XX_FILE_TYPE_UNKNOWN },
    { "tar_nextstep", mk_tar_nextstep, rm_tar_nextstep, XX_FILE_TYPE_UNKNOWN },
    { "tar_xz", mk_tar_xz, rm_tar_xz, XX_FILE_TYPE_UNKNOWN },
    { "tar_zstd", mk_tar_zstd, rm_tar_zstd, XX_FILE_TYPE_UNKNOWN },
    { "tarx1", mk_tarx1, rm_tarx1, XX_FILE_TYPE_UNKNOWN },
    { "tarx2", mk_tarx2, rm_tarx2, XX_FILE_TYPE_UNKNOWN },
    { "teacy", mk_teacy, rm_teacy, XX_FILE_TYPE_UNKNOWN },
    { "teledisk", mk_teledisk, rm_teledisk, XX_FILE_TYPE_UNKNOWN },
    { "terse", mk_terse, rm_terse, XX_FILE_TYPE_UNKNOWN },
    { "tgcf", mk_tgcf, rm_tgcf, XX_FILE_TYPE_UNKNOWN },
    { "ti99arc", mk_ti99arc, rm_ti99arc, XX_FILE_TYPE_UNKNOWN },
    { "tivoli", mk_tivoli, rm_tivoli, XX_FILE_TYPE_UNKNOWN },
    { "tnef", mk_tnef, rm_tnef, XX_FILE_TYPE_UNKNOWN },
    { "topspeed", mk_topspeed, rm_topspeed, XX_FILE_TYPE_UNKNOWN },
    { "tplink", mk_tplink, rm_tplink, XX_FILE_TYPE_UNKNOWN },
    { "tps", mk_tps, rm_tps, XX_FILE_TYPE_UNKNOWN },
    { "tpwm", mk_tpwm, rm_tpwm, XX_FILE_TYPE_UNKNOWN },
    { "trc", mk_trc, rm_trc, XX_FILE_TYPE_UNKNOWN },
    { "trcpak", mk_trcpak, rm_trcpak, XX_FILE_TYPE_UNKNOWN },
    { "trdos", mk_trdos, rm_trdos, XX_FILE_TYPE_UNKNOWN },
    { "trx", mk_trx, rm_trx, XX_FILE_TYPE_UNKNOWN },
    { "twoimg", mk_twoimg, rm_twoimg, XX_FILE_TYPE_UNKNOWN },
    { "twrx", mk_twrx, rm_twrx, XX_FILE_TYPE_UNKNOWN },
    { "tws", mk_tws, rm_tws, XX_FILE_TYPE_UNKNOWN },
    { "ubi", mk_ubi, rm_ubi, XX_FILE_TYPE_UNKNOWN },
    { "ubifs", mk_ubifs, rm_ubifs, XX_FILE_TYPE_UNKNOWN },
    { "uboot", mk_uboot, rm_uboot, XX_FILE_TYPE_UNKNOWN },
    { "udf", mk_udf, rm_udf, XX_FILE_TYPE_UNKNOWN },
    { "uefi_capsule", mk_uefi_capsule, rm_uefi_capsule, XX_FILE_TYPE_UNKNOWN },
    { "uefi_fv", mk_uefi_fv, rm_uefi_fv, XX_FILE_TYPE_UNKNOWN },
    { "uimage", mk_uimage, rm_uimage, XX_FILE_TYPE_UNKNOWN },
    { "ulead", mk_ulead, rm_ulead, XX_FILE_TYPE_UNKNOWN },
    { "unixcompact", mk_unixcompact, rm_unixcompact, XX_FILE_TYPE_UNKNOWN },
    { "unixcompress", mk_unixcompress, rm_unixcompress, XX_FILE_TYPE_UNKNOWN },
    { "unixpack", mk_unixpack, rm_unixpack, XX_FILE_TYPE_UNKNOWN },
    { "vhddynamic", mk_vhddynamic, rm_vhddynamic, XX_FILE_TYPE_UNKNOWN },
    { "vmarc", mk_vmarc, rm_vmarc, XX_FILE_TYPE_UNKNOWN },
    { "vmdk", mk_vmdk, rm_vmdk, XX_FILE_TYPE_UNKNOWN },
    { "vmsdb", mk_vmsdb, rm_vmsdb, XX_FILE_TYPE_UNKNOWN },
    { "vmspcsi", mk_vmspcsi, rm_vmspcsi, XX_FILE_TYPE_UNKNOWN },
    { "vmssaveset", mk_vmssaveset, rm_vmssaveset, XX_FILE_TYPE_UNKNOWN },
    { "volitionvpft", mk_volitionvpft, rm_volitionvpft, XX_FILE_TYPE_UNKNOWN },
    { "vxworks", mk_vxworks, rm_vxworks, XX_FILE_TYPE_UNKNOWN },
    { "warc", mk_warc, rm_warc, XX_FILE_TYPE_UNKNOWN },
    { "wiilz77", mk_wiilz77, rm_wiilz77, XX_FILE_TYPE_UNKNOWN },
    { "wim", mk_wim, rm_wim, XX_FILE_TYPE_UNKNOWN },
    { "wince", mk_wince, rm_wince, XX_FILE_TYPE_UNKNOWN },
    { "winlink", mk_winlink, rm_winlink, XX_FILE_TYPE_UNKNOWN },
    { "wintermutedcp", mk_wintermutedcp, rm_wintermutedcp, XX_FILE_TYPE_UNKNOWN },
    { "wintersoft", mk_wintersoft, rm_wintersoft, XX_FILE_TYPE_UNKNOWN },
    { "wolfft", mk_wolfft, rm_wolfft, XX_FILE_TYPE_UNKNOWN },
    { "wpk", mk_wpk, rm_wpk, XX_FILE_TYPE_UNKNOWN },
    { "wrzl", mk_wrzl, rm_wrzl, XX_FILE_TYPE_UNKNOWN },
    { "xar", mk_xar, rm_xar, XX_FILE_TYPE_UNKNOWN },
    { "xeditpack", mk_xeditpack, rm_xeditpack, XX_FILE_TYPE_UNKNOWN },
    { "xlas", mk_xlas, rm_xlas, XX_FILE_TYPE_UNKNOWN },
    { "xorarchive", mk_xorarchive, rm_xorarchive, XX_FILE_TYPE_UNKNOWN },
    { "xpak", mk_xpak, rm_xpak, XX_FILE_TYPE_UNKNOWN },
    { "xz", mk_xz, rm_xz, XX_FILE_TYPE_UNKNOWN },
    { "yaffs", mk_yaffs, rm_yaffs, XX_FILE_TYPE_UNKNOWN },
    { "zap", mk_zap, rm_zap, XX_FILE_TYPE_UNKNOWN },
    { "zcmp", mk_zcmp, rm_zcmp, XX_FILE_TYPE_UNKNOWN },
    { "zfsf", mk_zfsf, rm_zfsf, XX_FILE_TYPE_UNKNOWN },
    { "zie", mk_zie, rm_zie, XX_FILE_TYPE_UNKNOWN },
    { "zip", mk_zip, rm_zip, XX_FILE_TYPE_UNKNOWN },
    { "zlib", mk_zlib, rm_zlib, XX_FILE_TYPE_UNKNOWN },
    { "zlwb", mk_zlwb, rm_zlwb, XX_FILE_TYPE_UNKNOWN },
    { "zoo", mk_zoo, rm_zoo, XX_FILE_TYPE_UNKNOWN },
    { "zoom", mk_zoom, rm_zoom, XX_FILE_TYPE_UNKNOWN },
    { "zpak", mk_zpak, rm_zpak, XX_FILE_TYPE_UNKNOWN },
    { "zpaq", mk_zpaq, rm_zpaq, XX_FILE_TYPE_UNKNOWN },
    { "zstd", mk_zstd, rm_zstd, XX_FILE_TYPE_UNKNOWN },
    { "ztc", mk_ztc, rm_ztc, XX_FILE_TYPE_UNKNOWN },
    { "zxzip", mk_zxzip, rm_zxzip, XX_FILE_TYPE_UNKNOWN },
    { "zz", mk_zz, rm_zz, XX_FILE_TYPE_UNKNOWN },
    { "zzz", mk_zzz, rm_zzz, XX_FILE_TYPE_UNKNOWN },
};

static int g_learned = 0;
static char g_reader_extensions[sizeof(g_readers) / sizeof(g_readers[0])][32];

size_t xxfc_reader_count(void) {
    return sizeof(g_readers) / sizeof(g_readers[0]);
}

xxfc_reader_entry *xxfc_reader_table(void) {
    static uint8_t probe[1] = { 0 };
    size_t i;

    if (g_learned) return g_readers;

    for (i = 0; i < xxfc_reader_count(); ++i) {
        xx_io_device *dev = xx_io_mem_open_ro(probe, sizeof(probe));
        Abstractformat *fmt;

        if (!dev) continue;
        fmt = g_readers[i].create(dev, 0);
        if (fmt) {
            const char *extension = xx_format_get_extension(fmt);
            if (g_readers[i].type == XX_FILE_TYPE_UNKNOWN)
                g_readers[i].type = xx_format_get_file_type(fmt);
            if (extension) {
                size_t length = strlen(extension);
                if (length > 0 && length < sizeof(g_reader_extensions[i]))
                    memcpy(g_reader_extensions[i], extension, length + 1);
            }
            g_readers[i].release(fmt);
        }
        xx_io_close(dev);
    }
    g_learned = 1;
    return g_readers;
}

bool xxfc_open(xxfc_opened *out, xx_io_device *device, int64_t base_address) {
    if (!out || !device) return false;
    return xxfc_open_type(out, device, base_address,
                          xx_format_get_file_type_device(device));
}

bool xxfc_open_type(xxfc_opened *out, xx_io_device *device,
                    int64_t base_address, xx_file_type_t type) {
    xxfc_reader_entry *table = xxfc_reader_table();
    size_t i;

    if (!out || !device) return false;
    out->format = NULL;
    out->release = NULL;
    out->reader_name = NULL;
    out->type = XX_FILE_TYPE_UNKNOWN;

    out->type = type;
    /* BINARY is the detector's way of saying "bytes, but nothing I know", so
     * it names no format to route to. It is also what the few readers that
     * decide their real type only after parsing (elf, macho, pe, atarist)
     * answer at construction -- so without this, every unrecognised file
     * would be handed to whichever of those came first in the table. */
    if (type == XX_FILE_TYPE_UNKNOWN || type == XX_FILE_TYPE_BINARY) {
        /* The broad detector reports Bullfrog's BULLFROG-prefixed RNC
         * streams as binary. The RNC reader verifies the packed CRC and
         * decodes the first complete stream before taking this route. */
        Abstractformat *candidate = mk_rnc(device, base_address);
        if (candidate && xx_format_is_valid(candidate, NULL)) {
            out->format = candidate;
            out->release = rm_rnc;
            out->reader_name = "rnc";
            out->type = XX_FILE_TYPE_RNC;
            return true;
        }
        if (candidate) rm_rnc(candidate);
        return false;
    }

    /* MCC registration bundles may contain an embedded PK signature in a
     * member payload.  That can make the generic detector report ZIP even
     * though the complete file is a valid MCC member chain. */
    if (type == XX_FILE_TYPE_ZIP) {
        Abstractformat *candidate = mk_mcc(device, base_address);
        if (candidate && xx_format_is_valid(candidate, NULL)) {
            out->format = candidate;
            out->release = rm_mcc;
            out->reader_name = "mcc";
            out->type = XX_FILE_TYPE_MCC;
            return true;
        }
        if (candidate) rm_mcc(candidate);

        /* RID installer payloads can contain a PK byte sequence that wins
         * the broad ZIP signature scan. A fully validated RID member/frame
         * chain identifies the actual outer archive. */
        candidate = mk_rid(device, base_address);
        if (candidate && xx_format_is_valid(candidate, NULL)) {
            out->format = candidate;
            out->release = rm_rid;
            out->reader_name = "rid";
            out->type = XX_FILE_TYPE_RID;
            return true;
        }
        if (candidate) rm_rid(candidate);
    }

    /* Executable detectors report the carrier before its ZIP overlay.  A
     * complete SFX ZIP parse is stronger evidence and exposes the members
     * directly; otherwise keep the ordinary executable reader. */
    if (type == XX_FILE_TYPE_NE || type == XX_FILE_TYPE_PE32 ||
        type == XX_FILE_TYPE_PE64) {
        Abstractformat *candidate = mk_sfx_zipcentral(device, base_address);
        if (candidate && xx_format_is_valid(candidate, NULL)) {
            out->format = candidate;
            out->release = rm_sfx_zipcentral;
            out->reader_name = "sfx_zipcentral";
            out->type = XX_FILE_TYPE_SFX_ZIPCENTRAL;
            return true;
        }
        if (candidate) rm_sfx_zipcentral(candidate);


        /* A PE installer may carry a complete CAB within its image rather
         * than in a ZIP-style overlay. The CAB reader validates the bounded
         * cabinet before it is preferred over the executable carrier. */
        candidate = mk_sfx_cab(device, base_address);
        if (candidate && xx_format_is_valid(candidate, NULL)) {
            out->format = candidate;
            out->release = rm_sfx_cab;
            out->reader_name = "sfx_cab";
            out->type = XX_FILE_TYPE_SFX_CAB;
            return true;
        }
        if (candidate) rm_sfx_cab(candidate);

        /* Some PE launchers carry an ACE resource even when the generic
         * detector reports only the executable carrier. The ACE wrapper
         * validates the bounded member chain before taking precedence. */
        candidate = mk_sfx_ace(device, base_address);
        if (candidate && xx_format_is_valid(candidate, NULL)) {
            out->format = candidate;
            out->release = rm_sfx_ace;
            out->reader_name = "sfx_ace";
            out->type = XX_FILE_TYPE_SFX_ACE;
            return true;
        }
        if (candidate) rm_sfx_ace(candidate);
    }

    /* The type detector may identify a GEMDOS carrier first, including its
     * wrapper reader that exposes only a nested payload.lzh.  Prefer direct
     * archive members whenever a complete LHA chain validates in its data
     * section; retain the carrier reader as a fallback. */
    if (type == XX_FILE_TYPE_ATARIST || type == XX_FILE_TYPE_GEMDOS_LHA) {
        Abstractformat *candidate = mk_sfx_lha(device, base_address);
        if (candidate && xx_format_is_valid(candidate, NULL)) {
            out->format = candidate;
            out->release = rm_sfx_lha;
            out->reader_name = "sfx_lha";
            out->type = XX_FILE_TYPE_SFX_LHA;
            return true;
        }
        if (candidate) rm_sfx_lha(candidate);
    }

    for (i = 0; i < xxfc_reader_count(); ++i) {
        if (table[i].type != type || type == XX_FILE_TYPE_BINARY) continue;
        out->format = table[i].create(device, base_address);
        if (!out->format) return false;
        out->release = table[i].release;
        out->reader_name = table[i].name;
        out->type = type;
        return true;
    }

    /* Detected, but nothing here reads it: a detect-only format, or one whose
     * reader names itself only after parsing. */
    return false;
}

static bool xxfc_extension_matches(const char *source_path,
                                   const char *extension) {
    const char *name = source_path;
    const char *cursor;
    size_t name_length, extension_length, i;
    if (!source_path || !extension || !extension[0]) return false;
    for (cursor = source_path; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\') name = cursor + 1;
    name_length = strlen(name);
    extension_length = strlen(extension);
    if (name_length <= extension_length ||
        name[name_length - extension_length - 1] != '.') return false;
    for (i = 0; i < extension_length; ++i) {
        if (tolower((unsigned char)name[name_length - extension_length + i]) !=
            tolower((unsigned char)extension[i])) return false;
    }
    return true;
}

bool xxfc_open_extension_fast(xxfc_opened *out, xx_io_device *device,
                              int64_t base_address, const char *source_path,
                              xx_pd_struct *pd) {
    xx_file_type_t hint = xx_format_get_file_type_extension(source_path);
    xxfc_reader_entry *table;
    int64_t original_position;
    size_t i;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->type = XX_FILE_TYPE_UNKNOWN;
    if (!device || hint == XX_FILE_TYPE_UNKNOWN ||
        (pd && xx_pd_is_stopped(pd))) return false;
    table = xxfc_reader_table();
    original_position = xx_io_tell(device);
    if (original_position < 0) original_position = 0;
    for (i = 0; i < xxfc_reader_count(); ++i) {
        if (table[i].type != hint) continue;
        out->format = table[i].create(device, base_address);
        if (!out->format) break;
        out->release = table[i].release;
        out->reader_name = table[i].name;
        out->type = hint;
        if (hint == XX_FILE_TYPE_PCE_ANADISK)
            xx_pce_anadisk_set_conservative_probe(
                (xx_pce_anadisk *)out->format, true);
        if (!xx_format_is_valid(out->format, pd) ||
            !xx_format_handle_base_info(out->format, pd) ||
            (pd && xx_pd_is_stopped(pd))) {
            xxfc_close(out);
        } else {
            xx_file_type_t actual = xx_format_get_file_type(out->format);
            if (actual != XX_FILE_TYPE_UNKNOWN && actual != XX_FILE_TYPE_BINARY)
                out->type = actual;
        }
        break;
    }
    (void)xx_io_seek64(device, original_position, SEEK_SET);
    return out->format != NULL;
}

bool xxfc_open_extension(xxfc_opened *out, xx_io_device *device,
                         int64_t base_address, const char *source_path,
                         xx_pd_struct *pd) {
    xxfc_reader_entry *table;
    xxfc_opened winner = {0};
    int64_t original_position;
    size_t i, longest = 0, suffix_length;
    bool ambiguous = false;

    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->type = XX_FILE_TYPE_UNKNOWN;
    if (!device || !source_path || xx_io_total_size(device) <= base_address)
        return false;
    table = xxfc_reader_table();
    original_position = xx_io_tell(device);
    if (original_position < 0) original_position = 0;

    /* A compound suffix such as tar.gz is more precise than gz. Readers
     * without a declared suffix can still be addressed by their table name. */
    for (i = 0; i < xxfc_reader_count(); ++i) {
        const char *extension = g_reader_extensions[i][0]
                                    ? g_reader_extensions[i] : table[i].name;
        size_t length = strlen(extension);
        if (length > longest && xxfc_extension_matches(source_path, extension))
            longest = length;
    }
    if (!longest) return false;

    for (suffix_length = longest; suffix_length > 0; --suffix_length) {
        for (i = 0; i < xxfc_reader_count(); ++i) {
            const char *extension = g_reader_extensions[i][0]
                                        ? g_reader_extensions[i] : table[i].name;
            xxfc_opened candidate = {0};
            xx_file_type_t type;
            if (strlen(extension) != suffix_length ||
                !xxfc_extension_matches(source_path, extension) ||
                (pd && xx_pd_is_stopped(pd))) continue;
            if (xx_io_seek64(device, base_address, SEEK_SET) != 0) continue;
            candidate.format = table[i].create(device, base_address);
            if (!candidate.format) continue;
            candidate.release = table[i].release;
            candidate.reader_name = table[i].name;
            if (strcmp(table[i].name, "pce_anadisk") == 0)
                xx_pce_anadisk_set_conservative_probe(
                    (xx_pce_anadisk *)candidate.format, true);
            if (!xx_format_is_valid(candidate.format, pd) ||
                !xx_format_handle_base_info(candidate.format, pd)) {
                xxfc_close(&candidate);
                continue;
            }
            type = xx_format_get_file_type(candidate.format);
            if (type == XX_FILE_TYPE_UNKNOWN || type == XX_FILE_TYPE_BINARY)
                type = table[i].type;
            if (type == XX_FILE_TYPE_UNKNOWN || type == XX_FILE_TYPE_BINARY) {
                xxfc_close(&candidate);
                continue;
            }
            candidate.type = type;
            if (winner.format) {
                if (winner.type != candidate.type) ambiguous = true;
                xxfc_close(&candidate);
            } else {
                winner = candidate;
            }
        }
        if (winner.format || ambiguous || (pd && xx_pd_is_stopped(pd))) break;
    }
    (void)xx_io_seek64(device, original_position, SEEK_SET);
    if (ambiguous || (pd && xx_pd_is_stopped(pd))) {
        xxfc_close(&winner);
        return false;
    }
    if (!winner.format) return false;
    *out = winner;
    return true;
}

bool xxfc_open_named(xxfc_opened *out, xx_io_device *device,
                     int64_t base_address, const char *name) {
    xxfc_reader_entry *table;
    size_t i;
    if (!out) return false;
    xx_rt_memset(out, 0, sizeof(*out));
    out->type = XX_FILE_TYPE_UNKNOWN;
    if (!device || !name || !name[0]) return false;
    if (xx_rt_strncmp(name, "cpm:", 4U) == 0) {
        xx_cpm_preset preset;
        xx_cpm *reader;
        if (!xx_cpm_preset_from_name(name + 4, &preset)) return false;
        reader = xx_cpm_create_preset(device, base_address, preset);
        if (!reader) return false;
        out->format = xx_cpm_to_format(reader);
        out->release = rm_cpm;
        out->reader_name = xx_cpm_preset_name(preset);
        out->type = XX_FILE_TYPE_CPM;
        return true;
    }
    table = xxfc_reader_table();
    for (i = 0; i < xxfc_reader_count(); ++i) {
        if (xx_rt_strcmp(name, table[i].name) != 0) continue;
        out->format = table[i].create(device, base_address);
        if (!out->format) return false;
        out->release = table[i].release;
        out->reader_name = table[i].name;
        out->type = table[i].type;
        return true;
    }
    return false;
}

void xxfc_close(xxfc_opened *opened) {
    if (!opened || !opened->format) return;
    opened->release(opened->format);
    opened->format = NULL;
    opened->release = NULL;
}

Abstractformat *xxfc_make_writer(const char *kind, xx_io_device *device,
                                 xxfc_release_fn *release) {
    static const struct {
        const char *kind;
        const char *reader;
    } writable[] = {
        { "tar",      "tar"      }, { "tar.gz",  "tar_gz"   },
        { "tar.bz2",  "tar_bz2"  }, { "tar.xz",  "tar_xz"   },
        { "tar.zst",  "tar_zstd" }, { "tar.lz4", "tar_lz4"  },
        { "zip",      "zip"      }, { "cpio",    "cpio"     },
    };
    xxfc_reader_entry *table = xxfc_reader_table();
    size_t i, j;

    if (!kind || !device || !release) return NULL;

    for (i = 0; i < sizeof(writable) / sizeof(writable[0]); ++i) {
        if (xx_rt_strcmp(kind, writable[i].kind) != 0) continue;
        for (j = 0; j < xxfc_reader_count(); ++j) {
            Abstractformat *fmt;
            if (xx_rt_strcmp(table[j].name, writable[i].reader) != 0) continue;
            fmt = table[j].create(device, 0);
            if (!fmt) return NULL;
            *release = table[j].release;
            return fmt;
        }
    }
    return NULL;
}
