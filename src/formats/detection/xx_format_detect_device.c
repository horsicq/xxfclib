/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xx_format.h"
#include "../xx_format_detect_internal.h"
#include "xxfclib/formats/mozilla_mar/xx_mozilla_mar.h"
#include "xxfclib/formats/westwood_pak/xx_westwood_pak.h"
#include "xxfclib/formats/fatx/xx_fatx.h"
#include "xxfclib/formats/soundfont2/xx_soundfont2.h"
#include "xxfclib/formats/ivf/xx_ivf.h"
#include "xxfclib/formats/windows_ani/xx_windows_ani.h"
#include "xxfclib/formats/interplay_acm/xx_interplay_acm.h"
#include "xxfclib/formats/cri_ahx/xx_cri_ahx.h"
#include "xxfclib/formats/adobe_director_cxt/xx_adobe_director_cxt.h"
#include "xxfclib/formats/olympus_dss/xx_olympus_dss.h"
#include "xxfclib/formats/ea_exa/xx_ea_exa.h"
#include "xxfclib/formats/audio_nitro_strm/xx_audio_nitro_strm.h"
#include "xxfclib/formats/audio_wwise_wem/xx_audio_wwise_wem.h"
#include "xxfclib/formats/audio_scumm_sou/xx_audio_scumm_sou.h"
#include "xxfclib/formats/audio_riff_ima/xx_audio_riff_ima.h"
#include "xxfclib/formats/hmi_midi/xx_hmi_midi.h"
#include "xxfclib/formats/ensoniq_paf/xx_ensoniq_paf.h"
#include "xxfclib/formats/abylight_strm/xx_abylight_strm.h"
#include "xxfclib/formats/lego_alp/xx_lego_alp.h"
#include "xxfclib/formats/audio_pvf/xx_audio_pvf.h"
#include "xxfclib/formats/audio_rifx_wave/xx_audio_rifx_wave.h"
#include "xxfclib/formats/audio_shockwave_swa/xx_audio_shockwave_swa.h"
#include "xxfclib/formats/die_music/xx_die_music.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#ifdef XXFC_FORMAT_DETECTION_LZMA_XZ_ONLY
#include "xxfclib/algo/lzma_alone/xx_lzma_alone.h"
#include "xxfclib/formats/lzma/xx_lzma.h"
#include "xxfclib/formats/xz/xx_xz.h"
#else
#include "xx_format_additional.h"
#include "xxfclib/formats/ue2_documents/xx_ue2_documents.h"
#include "xxfclib/formats/ue2_games/xx_ue2_games.h"
#include "xxfclib/formats/chromium_pak/xx_chromium_pak.h"
#include "xxfclib/formats/windows_thumbnail_cache/xx_windows_thumbnail_cache.h"
#include "xxfclib/formats/enigma_virtual_box/xx_enigma_virtual_box.h"
#include "xxfclib/formats/bitrock/xx_bitrock.h"
#include "xxfclib/formats/smart_install_maker/xx_smart_install_maker.h"
#include "xxfclib/formats/upx_engine/xx_upx_engine.h"
#include "xxfclib/formats/molebox/xx_molebox.h"
#include "xxfclib/formats/superdat/xx_superdat.h"
#include "xxfclib/formats/excelsior/xx_excelsior.h"
#include "xxfclib/formats/fead/xx_fead.h"
#include "xxfclib/formats/bz2/xx_bz2.h"
#include "xxfclib/formats/gz/xx_gz.h"
#include "xxfclib/formats/jar/xx_jar.h"
#include "xxfclib/formats/apk/xx_apk.h"
#include "xxfclib/formats/ipa/xx_ipa.h"
#include "xxfclib/formats/npm/xx_npm.h"
#include "xxfclib/formats/xz/xx_xz.h"
#include "xxfclib/formats/tar_lz4/xx_tar_lz4.h"
#include "xxfclib/formats/lz4/xx_lz4.h"
#include "xxfclib/formats/lz5/xx_lz5.h"
#include "xxfclib/formats/lizard/xx_lizard.h"
#include "xxfclib/formats/brotli/xx_brotli.h"
#include "xxfclib/formats/unixpack/xx_unixpack.h"
#include "xxfclib/formats/zlib/xx_zlib.h"
#include "xxfclib/formats/unixcompress/xx_unixcompress.h"
#include "xxfclib/formats/gitobject/xx_gitobject.h"
#include "xxfclib/formats/mscompress/xx_mscompress.h"
#include "xxfclib/formats/ash0/xx_ash0.h"
#include "xxfclib/formats/wiilz77/xx_wiilz77.h"
#include "xxfclib/formats/lzv1/xx_lzv1.h"
#include "xxfclib/formats/oraclesqueeze/xx_oraclesqueeze.h"
#include "xxfclib/formats/softronics/xx_softronics.h"
#include "xxfclib/formats/logitechcompress/xx_logitechcompress.h"
#include "xxfclib/formats/dmapacked/xx_dmapacked.h"
#include "xxfclib/formats/gashuff/xx_gashuff.h"
#include "xxfclib/formats/huf/xx_huf.h"
#include "xxfclib/formats/lzdiet/xx_lzdiet.h"
#include "xxfclib/formats/lzpis2/xx_lzpis2.h"
#include "xxfclib/formats/zie/xx_zie.h"
#include "xxfclib/formats/xeditpack/xx_xeditpack.h"
#include "xxfclib/formats/wpk/xx_wpk.h"
#include "xxfclib/formats/wintersoft/xx_wintersoft.h"
#include "xxfclib/formats/vmarc/xx_vmarc.h"
#include "xxfclib/formats/tivoli/xx_tivoli.h"
#include "xxfclib/formats/ti99arc/xx_ti99arc.h"
#include "xxfclib/formats/stylus/xx_stylus.h"
#include "xxfclib/formats/rtpatch/xx_rtpatch.h"
#include "xxfclib/formats/rta/xx_rta.h"
#include "xxfclib/formats/rompaq/xx_rompaq.h"
#include "xxfclib/formats/rid/xx_rid.h"
#include "xxfclib/formats/qnxbase/xx_qnxbase.h"
#include "xxfclib/formats/qda/xx_qda.h"
#include "xxfclib/formats/pkt/xx_pkt.h"
#include "xxfclib/formats/lofi/xx_lofi.h"
#include "xxfclib/formats/lim/xx_lim.h"
#include "xxfclib/formats/kolibrikpack/xx_kolibrikpack.h"
#include "xxfclib/formats/ivt/xx_ivt.h"
#include "xxfclib/formats/irwinpac/xx_irwinpac.h"
#include "xxfclib/formats/hap/xx_hap.h"
#include "xxfclib/formats/compactpro/xx_compactpro.h"
#include "xxfclib/formats/imp/xx_imp.h"
#include "xxfclib/formats/sqx/xx_sqx.h"
#include "xxfclib/formats/zoo/xx_zoo.h"
#include "xxfclib/formats/terse/xx_terse.h"
#include "xxfclib/formats/stk/xx_stk.h"
#include "xxfclib/formats/pcommos2/xx_pcommos2.h"
#include "xxfclib/formats/jbf/xx_jbf.h"
#include "xxfclib/formats/ibmspack/xx_ibmspack.h"
#include "xxfclib/formats/gtu/xx_gtu.h"
#include "xxfclib/formats/glu/xx_glu.h"
#include "xxfclib/formats/ztc/xx_ztc.h"
#include "xxfclib/formats/netwarepacked/xx_netwarepacked.h"
#include "xxfclib/formats/zpak/xx_zpak.h"
#include "xxfclib/formats/zcmp/xx_zcmp.h"
#include "xxfclib/formats/scl/xx_scl.h"
#include "xxfclib/formats/pakleo/xx_pakleo.h"
#include "xxfclib/formats/npack/xx_npack.h"
#include "xxfclib/formats/mi10/xx_mi10.h"
#include "xxfclib/formats/lzwd/xx_lzwd.h"
#include "xxfclib/formats/lzhcxp/xx_lzhcxp.h"
#include "xxfclib/formats/kboom/xx_kboom.h"
#include "xxfclib/formats/hzl/xx_hzl.h"
#include "xxfclib/formats/ha/xx_ha.h"
#include "xxfclib/formats/genius/xx_genius.h"
#include "xxfclib/formats/fls/xx_fls.h"
#include "xxfclib/formats/earefpack/xx_earefpack.h"
#include "xxfclib/formats/ealib/xx_ealib.h"
#include "xxfclib/formats/elm/xx_elm.h"
#include "xxfclib/formats/ea/xx_ea.h"
#include "xxfclib/formats/diskdoubler/xx_diskdoubler.h"
#include "xxfclib/formats/cmp/xx_cmp.h"
#include "xxfclib/formats/clp/xx_clp.h"
#include "xxfclib/formats/chieflzmulti/xx_chieflzmulti.h"
#include "xxfclib/formats/chieflz/xx_chieflz.h"
#include "xxfclib/formats/bwcf/xx_bwcf.h"
#include "xxfclib/formats/asymetrix/xx_asymetrix.h"
#include "xxfclib/formats/seaarc/xx_seaarc.h"
#include "xxfclib/formats/amigalzx/xx_amigalzx.h"
#include "xxfclib/formats/spis/xx_spis.h"
#include "xxfclib/formats/lha/xx_lha.h"
#include "xxfclib/formats/xar/xx_xar.h"
#include "xxfclib/formats/fmc1/xx_fmc1.h"
#include "xxfclib/formats/pyz/xx_pyz.h"
#include "xxfclib/formats/ascendbackup/xx_ascendbackup.h"
#include "xxfclib/formats/stork/xx_stork.h"
#include "xxfclib/formats/zap/xx_zap.h"
#include "xxfclib/formats/bwf/xx_bwf.h"
#include "xxfclib/formats/qualitas/xx_qualitas.h"
#include "xxfclib/formats/jetbbs/xx_jetbbs.h"
#include "xxfclib/formats/ecmpacked/xx_ecmpacked.h"
#include "xxfclib/formats/borlandpack/xx_borlandpack.h"
#include "xxfclib/formats/jgpak/xx_jgpak.h"
#include "xxfclib/formats/zzz/xx_zzz.h"
#include "xxfclib/formats/zz/xx_zz.h"
#include "xxfclib/formats/zlwb/xx_zlwb.h"
#include "xxfclib/formats/trc/xx_trc.h"
#include "xxfclib/formats/tgcf/xx_tgcf.h"
#include "xxfclib/formats/swag/xx_swag.h"
#include "xxfclib/formats/riversoft/xx_riversoft.h"
#include "xxfclib/formats/rcf/xx_rcf.h"
#include "xxfclib/formats/quarterdeckqp/xx_quarterdeckqp.h"
#include "xxfclib/formats/qip1/xx_qip1.h"
#include "xxfclib/formats/powerarc/xx_powerarc.h"
#include "xxfclib/formats/povlablzh/xx_povlablzh.h"
#include "xxfclib/formats/mva/xx_mva.h"
#include "xxfclib/formats/miz/xx_miz.h"
#include "xxfclib/formats/lsz/xx_lsz.h"
#include "xxfclib/formats/jm93/xx_jm93.h"
#include "xxfclib/formats/inteduft/xx_inteduft.h"
#include "xxfclib/formats/igf2/xx_igf2.h"
#include "xxfclib/formats/igf1/xx_igf1.h"
#include "xxfclib/formats/ibmzpak/xx_ibmzpak.h"
#include "xxfclib/formats/fld/xx_fld.h"
#include "xxfclib/formats/fiz/xx_fiz.h"
#include "xxfclib/formats/dtpacked/xx_dtpacked.h"
#include "xxfclib/formats/dsl2/xx_dsl2.h"
#include "xxfclib/formats/dpk/xx_dpk.h"
#include "xxfclib/formats/cfl/xx_cfl.h"
#include "xxfclib/formats/trcpak/xx_trcpak.h"
#include "xxfclib/formats/swagpacket/xx_swagpacket.h"
#include "xxfclib/formats/sw/xx_sw.h"
#include "xxfclib/formats/sos/xx_sos.h"
#include "xxfclib/formats/secondnature/xx_secondnature.h"
#include "xxfclib/formats/seadata/xx_seadata.h"
#include "xxfclib/formats/sci/xx_sci.h"
#include "xxfclib/formats/powerboardbbs/xx_powerboardbbs.h"
#include "xxfclib/formats/minidump/xx_minidump.h"
#include "xxfclib/formats/lbrcobol/xx_lbrcobol.h"
#include "xxfclib/formats/krml/xx_krml.h"
#include "xxfclib/formats/jam/xx_jam.h"
#include "xxfclib/formats/irixsa/xx_irixsa.h"
#include "xxfclib/formats/hlb/xx_hlb.h"
#include "xxfclib/formats/frontpagetheme/xx_frontpagetheme.h"
#include "xxfclib/formats/cru/xx_cru.h"
#include "xxfclib/formats/bigaf/xx_bigaf.h"
#include "xxfclib/formats/tws/xx_tws.h"
#include "xxfclib/formats/packit/xx_packit.h"
#include "xxfclib/formats/zfsf/xx_zfsf.h"
#include "xxfclib/formats/marc/xx_marc.h"
#include "xxfclib/formats/bigf/xx_bigf.h"
#include "xxfclib/formats/ckp/xx_ckp.h"
#include "xxfclib/formats/edp/xx_edp.h"
#include "xxfclib/formats/sfx_inftool/xx_sfx_inftool.h"
#include "xxfclib/formats/parsec_rib/xx_parsec_rib.h"
#include "xxfclib/formats/parsec_archive/xx_parsec_archive.h"
#include "xxfclib/formats/parsec_pmm/xx_parsec_pmm.h"
#include "xxfclib/formats/ptero_bigf/xx_ptero_bigf.h"
#include "xxfclib/formats/rvz/xx_rvz.h"
#include "xxfclib/formats/ascend/xx_ascend.h"
#include "xxfclib/formats/asar/xx_asar.h"
#include "xxfclib/formats/arq/xx_arq.h"
#include "xxfclib/formats/ap4/xx_ap4.h"
#include "xxfclib/formats/freearc/xx_freearc.h"
#include "xxfclib/formats/zpaq/xx_zpaq.h"
#include "xxfclib/formats/pea/xx_pea.h"
#include "xxfclib/formats/lpaq8/xx_lpaq8.h"
#include "xxfclib/formats/bcm/xx_bcm.h"
#include "xxfclib/formats/tar_zstd/xx_tar_zstd.h"
#include "xxfclib/formats/zstd/xx_zstd.h"
#include "xxfclib/formats/cpio/xx_cpio.h"
#include "xxfclib/formats/mtree/xx_mtree.h"
#include "xxfclib/formats/tar_nextstep/xx_tar_nextstep.h"
#include "xxfclib/formats/tar_compress/xx_tar_compress.h"
#include "xxfclib/algo/compress/xx_compress.h"
#include "xxfclib/formats/tar_lzip/xx_tar_lzip.h"
#include "xxfclib/algo/lzip/xx_lzip.h"
#include "xxfclib/formats/lzip/xx_lzip.h"
#include "xxfclib/formats/tar_lzma/xx_tar_lzma.h"
#include "xxfclib/algo/lzma_alone/xx_lzma_alone.h"
#include "xxfclib/formats/lzma/xx_lzma.h"
#include "xxfclib/formats/tar_lzop/xx_tar_lzop.h"
#include "xxfclib/algo/lzop/xx_lzop.h"
#include "xxfclib/formats/tarx1/xx_tarx1.h"
#include "xxfclib/algo/tarx/xx_tarx.h"
#include "xxfclib/formats/tarx2/xx_tarx2.h"
#include "xxfclib/algo/tarx/xx_tarx2.h"
#include "xxfclib/formats/iso9660/xx_iso9660.h"
#include "xxfclib/formats/trx/xx_trx.h"
#include "xxfclib/formats/seama/xx_seama.h"
#include "xxfclib/formats/chk/xx_chk.h"
#include "xxfclib/formats/packimg/xx_packimg.h"
#include "xxfclib/formats/dlob/xx_dlob.h"
#include "xxfclib/formats/wince/xx_wince.h"
#include "xxfclib/formats/binhdr/xx_binhdr.h"
#include "xxfclib/formats/rtk/xx_rtk.h"
#include "xxfclib/formats/csman/xx_csman.h"
#include "xxfclib/formats/vxworks/xx_vxworks.h"
#include "xxfclib/formats/uefi_fv/xx_uefi_fv.h"
#include "xxfclib/formats/uefi_capsule/xx_uefi_capsule.h"
#include "xxfclib/formats/qcow/xx_qcow.h"
#include "xxfclib/formats/qnx6/xx_qnx6.h"
#include "xxfclib/formats/luks/xx_luks.h"
#include "xxfclib/formats/apfs/xx_apfs.h"
#include "xxfclib/formats/btrfs/xx_btrfs.h"
#include "xxfclib/formats/logfs/xx_logfs.h"
#include "xxfclib/formats/dmg/xx_dmg.h"
#include "xxfclib/formats/dms/xx_dms.h"
#include "xxfclib/formats/xamarin_compressed_assembly/xx_xamarin_compressed_assembly.h"
#include "xxfclib/formats/x68000_dim/xx_x68000_dim.h"
#include "xxfclib/formats/visionaire_studio_vis/xx_visionaire_studio_vis.h"
#include "xxfclib/formats/uharc/xx_uharc.h"
#include "xxfclib/formats/trs_80_jv3/xx_trs_80_jv3.h"
#include "xxfclib/formats/trs_80_jv1/xx_trs_80_jv1.h"
#include "xxfclib/formats/t98_next_nfd/xx_t98_next_nfd.h"
#include "xxfclib/formats/stuffit_split_file/xx_stuffit_split_file.h"
#include "xxfclib/formats/rdb/xx_rdb.h"
#include "xxfclib/formats/qnap_nas_firmware/xx_qnap_nas_firmware.h"
#include "xxfclib/formats/qcow1/xx_qcow1.h"
#include "xxfclib/formats/nsa/xx_nsa.h"
#include "xxfclib/formats/ns2/xx_ns2.h"
#include "xxfclib/formats/nec_pc_98_fdi/xx_nec_pc_98_fdi.h"
#include "xxfclib/formats/ms_dos_backup/xx_ms_dos_backup.h"
#include "xxfclib/formats/hxc_stream_hfe/xx_hxc_stream_hfe.h"
#include "xxfclib/formats/encrypted_apple_disk_image/xx_encrypted_apple_disk_image.h"
#include "xxfclib/formats/apple_sparse_bundle/xx_apple_sparse_bundle.h"
#include "xxfclib/formats/apple_disk_copy_6_ndif_image/xx_apple_disk_copy_6_ndif_image.h"
#include "xxfclib/formats/raw_deflate_compressed_data/xx_raw_deflate_compressed_data.h"
#include "xxfclib/formats/lzop/xx_lzop.h"
#include "xxfclib/formats/srec/xx_srec.h"
#include "xxfclib/formats/dclraw/xx_dclraw.h"
#include "xxfclib/formats/xpak/xx_xpak.h"
#include "xxfclib/formats/pdb/xx_pdb.h"
#include "xxfclib/formats/infogramesft/xx_infogramesft.h"
#include "xxfclib/formats/uboot/xx_uboot.h"
#include "xxfclib/formats/twrx/xx_twrx.h"
#include "xxfclib/formats/tplink/xx_tplink.h"
#include "xxfclib/formats/silmarilsft/xx_silmarilsft.h"
#include "xxfclib/formats/shrs/xx_shrs.h"
#include "xxfclib/formats/mh01/xx_mh01.h"
#include "xxfclib/formats/matter_ota/xx_matter_ota.h"
#include "xxfclib/formats/lz4demo/xx_lz4demo.h"
#include "xxfclib/formats/lingvoarc/xx_lingvoarc.h"
#include "xxfclib/formats/jboot/xx_jboot.h"
#include "xxfclib/formats/encrpted_img/xx_encrpted_img.h"
#include "xxfclib/formats/encfw/xx_encfw.h"
#include "xxfclib/formats/ecos/xx_ecos.h"
#include "xxfclib/formats/dlke/xx_dlke.h"
#include "xxfclib/formats/dlink_tlv/xx_dlink_tlv.h"
#include "xxfclib/formats/dkbs/xx_dkbs.h"
#include "xxfclib/formats/autel/xx_autel.h"
#include "xxfclib/formats/arcadyan/xx_arcadyan.h"
#include "xxfclib/formats/androidboot/xx_androidboot.h"
#include "xxfclib/formats/rawstac/xx_rawstac.h"
#include "xxfclib/formats/rtpatch/xx_rtpatch.h"
#include "xxfclib/formats/softronics/xx_softronics.h"
#include "xxfclib/formats/vmssaveset/xx_vmssaveset.h"
#include "xxfclib/formats/boo/xx_boo.h"
#include "xxfclib/formats/wolfft/xx_wolfft.h"
#include "xxfclib/formats/settlersft/xx_settlersft.h"
#include "xxfclib/formats/teacy/xx_teacy.h"
#include "xxfclib/formats/rsc/xx_rsc.h"
#include "xxfclib/formats/res/xx_res.h"
#include "xxfclib/formats/bsn/xx_bsn.h"
#include "xxfclib/formats/wintermutedcp/xx_wintermutedcp.h"
#include "xxfclib/formats/volitionvpft/xx_volitionvpft.h"
#include "xxfclib/formats/agis/xx_agis.h"
#include "xxfclib/formats/hog/xx_hog.h"
#include "xxfclib/formats/lzpis2/xx_lzpis2.h"
#include "xxfclib/formats/rnca/xx_rnca.h"
#include "xxfclib/formats/paperport/xx_paperport.h"
#include "xxfclib/formats/starkit/xx_starkit.h"
#include "xxfclib/formats/lspack10/xx_lspack10.h"
#include "xxfclib/formats/ixa/xx_ixa.h"
#include "xxfclib/formats/mlb_ft/xx_mlb_ft.h"
#include "xxfclib/formats/fss/xx_fss.h"
#include "xxfclib/formats/epf/xx_epf.h"
#include "xxfclib/formats/dfc/xx_dfc.h"
#include "xxfclib/formats/ppd/xx_ppd.h"
#include "xxfclib/formats/sfx_rsfx/xx_sfx_rsfx.h"
#include "xxfclib/formats/sfx_softpaq4/xx_sfx_softpaq4.h"
#include "xxfclib/formats/sfx_ad01/xx_sfx_ad01.h"
#include "xxfclib/formats/sfx_nss/xx_sfx_nss.h"
#include "xxfclib/formats/ka/xx_ka.h"
#include "xxfclib/formats/dn/xx_dn.h"
#include "xxfclib/formats/insa/xx_insa.h"
#include "xxfclib/formats/thebat_msb/xx_thebat_msb.h"
#include "xxfclib/formats/sfx_localzip/xx_sfx_localzip.h"
#include "xxfclib/formats/lif/xx_lif.h"
#include "xxfclib/formats/qip2/xx_qip2.h"
#include "xxfclib/formats/emt/xx_emt.h"
#include "xxfclib/formats/bagf/xx_bagf.h"
#include "xxfclib/formats/wrzl/xx_wrzl.h"
#include "xxfclib/formats/spk/xx_spk.h"
#include "xxfclib/formats/stac/xx_stac.h"
#include "xxfclib/formats/dbz/xx_dbz.h"
#include "xxfclib/formats/squeeze2/xx_squeeze2.h"
#include "xxfclib/formats/sq/xx_sq.h"
#include "xxfclib/formats/phar/xx_phar.h"
#include "xxfclib/formats/mpq/xx_mpq.h"
#include "xxfclib/formats/sabdu/xx_sabdu.h"
#include "xxfclib/formats/apricot/xx_apricot.h"
#include "xxfclib/formats/hdcopy/xx_hdcopy.h"
#include "xxfclib/formats/copydisk/xx_copydisk.h"
#include "xxfclib/formats/ciso/xx_ciso.h"
#include "xxfclib/formats/vmdk/xx_vmdk.h"
#include "xxfclib/formats/vhddynamic/xx_vhddynamic.h"
#include "xxfclib/formats/wim/xx_wim.h"
#include "xxfclib/formats/softpaq2/xx_softpaq2.h"
#include "xxfclib/formats/aiaff/xx_aiaff.h"
#include "xxfclib/formats/gxl/xx_gxl.h"
#include "xxfclib/formats/shrinkwrap/xx_shrinkwrap.h"
#include "xxfclib/formats/red/xx_red.h"
#include "xxfclib/formats/diskexpress/xx_diskexpress.h"
#include "xxfclib/formats/cpx/xx_cpx.h"
#include "xxfclib/formats/smsipak/xx_smsipak.h"
#include "xxfclib/formats/bnd/xx_bnd.h"
#include "xxfclib/formats/cat/xx_cat.h"
#include "xxfclib/formats/csidos/xx_csidos.h"
#include "xxfclib/formats/binder/xx_binder.h"
#include "xxfclib/formats/jasc/xx_jasc.h"
#include "xxfclib/formats/recognita/xx_recognita.h"
#include "xxfclib/formats/scf/xx_scf.h"
#include "xxfclib/formats/bcw/xx_bcw.h"
#include "xxfclib/formats/bvrp/xx_bvrp.h"
#include "xxfclib/formats/ssm/xx_ssm.h"
#include "xxfclib/formats/mdcd/xx_mdcd.h"
#include "xxfclib/formats/xlas/xx_xlas.h"
#include "xxfclib/formats/pain/xx_pain.h"
#include "xxfclib/formats/qrst/xx_qrst.h"
#include "xxfclib/formats/opc/xx_opc.h"
#include "xxfclib/formats/tnef/xx_tnef.h"
#include "xxfclib/formats/mcc/xx_mcc.h"
#include "xxfclib/formats/notetab/xx_notetab.h"
#include "xxfclib/formats/stunts/xx_stunts.h"
#include "xxfclib/formats/megatechvol/xx_megatechvol.h"
#include "xxfclib/formats/grasp/xx_grasp.h"
#include "xxfclib/formats/psn/xx_psn.h"
#include "xxfclib/formats/sinner/xx_sinner.h"
#include "xxfclib/formats/hog2/xx_hog2.h"
#include "xxfclib/formats/pcxlib/xx_pcxlib.h"
#include "xxfclib/formats/vmsdb/xx_vmsdb.h"
#include "xxfclib/formats/vmspcsi/xx_vmspcsi.h"
#include "xxfclib/formats/beospkg/xx_beospkg.h"
#include "xxfclib/formats/solarispkg/xx_solarispkg.h"
#include "xxfclib/formats/pax/xx_pax.h"
#include "xxfclib/formats/copyqmexe/xx_copyqmexe.h"
#include "xxfclib/formats/diskjuggler/xx_diskjuggler.h"
#include "xxfclib/formats/pmdiskcopy/xx_pmdiskcopy.h"
#include "xxfclib/formats/diskdupe/xx_diskdupe.h"
#include "xxfclib/formats/imd/xx_imd.h"
#include "xxfclib/formats/twoimg/xx_twoimg.h"
#include "xxfclib/formats/fdi/xx_fdi.h"
#include "xxfclib/formats/hfe/xx_hfe.h"
#include "xxfclib/formats/teledisk/xx_teledisk.h"
#include "xxfclib/formats/copyqm/xx_copyqm.h"
#include "xxfclib/formats/pcinstall/xx_pcinstall.h"
#include "xxfclib/formats/gksetup/xx_gksetup.h"
#include "xxfclib/formats/is11/xx_is11.h"
#include "xxfclib/formats/izpack/xx_izpack.h"
#include "xxfclib/formats/squeeze1/xx_squeeze1.h"
#include "xxfclib/formats/trdos/xx_trdos.h"
#include "xxfclib/formats/lifkd/xx_lifkd.h"
#include "xxfclib/formats/arcv/xx_arcv.h"
#include "xxfclib/formats/cpoint/xx_cpoint.h"
#include "xxfclib/formats/compaqlzh/xx_compaqlzh.h"
#include "xxfclib/formats/lzk00/xx_lzk00.h"
#include "xxfclib/formats/pma/xx_pma.h"
#include "xxfclib/formats/binhex/xx_binhex.h"
#include "xxfclib/formats/binaryii/xx_binaryii.h"
#include "xxfclib/formats/stuffit/xx_stuffit.h"
#include "xxfclib/formats/dclft/xx_dclft.h"
#include "xxfclib/formats/debugscr/xx_debugscr.h"
#include "xxfclib/formats/gob/xx_gob.h"
#include "xxfclib/formats/savedskf/xx_savedskf.h"
#include "xxfclib/formats/edilzss/xx_edilzss.h"
#include "xxfclib/formats/is7inx/xx_is7inx.h"
#include "xxfclib/formats/is5/xx_is5.h"
#include "xxfclib/formats/is3/xx_is3.h"
#include "xxfclib/formats/psdc/xx_psdc.h"
#include "xxfclib/formats/unixcompact/xx_unixcompact.h"
#include "xxfclib/formats/sco/xx_sco.h"
#include "xxfclib/formats/gpfpack/xx_gpfpack.h"
#include "xxfclib/formats/ftcomp/xx_ftcomp.h"
#include "xxfclib/formats/winlink/xx_winlink.h"
#include "xxfclib/formats/gst/xx_gst.h"
#include "xxfclib/formats/finear/xx_finear.h"
#include "xxfclib/formats/mwave/xx_mwave.h"
#include "xxfclib/formats/xorarchive/xx_xorarchive.h"
#include "xxfclib/formats/mxs/xx_mxs.h"
#include "xxfclib/formats/edc/xx_edc.h"
#include "xxfclib/formats/mrnz/xx_mrnz.h"
#include "xxfclib/formats/tpwm/xx_tpwm.h"
#include "xxfclib/formats/cazip/xx_cazip.h"
#include "xxfclib/formats/ibmpack/xx_ibmpack.h"
#include "xxfclib/formats/rnc/xx_rnc.h"
#include "xxfclib/formats/shar/xx_shar.h"
#include "xxfclib/formats/netware2/xx_netware2.h"
#include "xxfclib/formats/mathcad/xx_mathcad.h"
#include "xxfclib/formats/perform/xx_perform.h"
#include "xxfclib/formats/battleisle/xx_battleisle.h"
#include "xxfclib/formats/kpck/xx_kpck.h"
#include "xxfclib/formats/beatthehouse/xx_beatthehouse.h"
#include "xxfclib/formats/pp20/xx_pp20.h"
#include "xxfclib/formats/macbinary/xx_macbinary.h"
#include "xxfclib/formats/applesingle/xx_applesingle.h"
#include "xxfclib/formats/resourcefork/xx_resourcefork.h"
#include "xxfclib/formats/cramfs/xx_cramfs.h"
#include "xxfclib/formats/jffs2/xx_jffs2.h"
#include "xxfclib/formats/yaffs/xx_yaffs.h"
#include "xxfclib/formats/ubi/xx_ubi.h"
#include "xxfclib/formats/ubifs/xx_ubifs.h"
#include "xxfclib/formats/ext/xx_ext.h"
#include "xxfclib/formats/fat/xx_fat.h"
#include "xxfclib/formats/mbr/xx_mbr.h"
#include "xxfclib/formats/gpt/xx_gpt.h"
#include "xxfclib/formats/sparse/xx_sparse.h"
#include "xxfclib/formats/uimage/xx_uimage.h"
#include "xxfclib/formats/dtb/xx_dtb.h"
#include "xxfclib/formats/squashfs/xx_squashfs.h"
#include "xxfclib/formats/ntfs/xx_ntfs.h"
#include "xxfclib/formats/udf/xx_udf.h"
#include "xxfclib/formats/romfs/xx_romfs.h"
#include "xxfclib/formats/sqz/xx_sqz.h"
#include "xxfclib/formats/topspeed/xx_topspeed.h"
#include "xxfclib/formats/tps/xx_tps.h"
#include "xxfclib/formats/ulead/xx_ulead.h"
#include "xxfclib/formats/quantum/xx_quantum.h"
#include "xxfclib/formats/zxzip/xx_zxzip.h"
#include "xxfclib/formats/zoom/xx_zoom.h"
#include "xxfclib/formats/sfpack/xx_sfpack.h"
#include "xxfclib/formats/claylz/xx_claylz.h"
#include "xxfclib/formats/c64wraptor/xx_c64wraptor.h"
#include "xxfclib/formats/corelltec/xx_corelltec.h"
#include "xxfclib/formats/pcsecure/xx_pcsecure.h"
#include "xxfclib/formats/rsvk/xx_rsvk.h"
#include "xxfclib/formats/lzw15v/xx_lzw15v.h"
#include "xxfclib/formats/saf/xx_saf.h"
#include "xxfclib/formats/sls/xx_sls.h"
#include "xxfclib/formats/nid/xx_nid.h"
#include "xxfclib/formats/gamos/xx_gamos.h"
#include "xxfclib/formats/panorama/xx_panorama.h"
#include "xxfclib/formats/fpak/xx_fpak.h"
#include "xxfclib/formats/ace/xx_ace.h"
#include "xxfclib/formats/ain/xx_ain.h"
#include "xxfclib/formats/aldus/xx_aldus.h"
#include "xxfclib/formats/alz/xx_alz.h"
#include "xxfclib/formats/ampk/xx_ampk.h"
#include "xxfclib/formats/aodos/xx_aodos.h"
#include "xxfclib/formats/arcfs/xx_arcfs.h"
#include "xxfclib/formats/pdp11ar/xx_pdp11ar.h"
#include "xxfclib/formats/artipack/xx_artipack.h"
#include "xxfclib/formats/arcv2/xx_arcv2.h"
#include "xxfclib/formats/arcv4/xx_arcv4.h"
#include "xxfclib/formats/warc/xx_warc.h"
#include "xxfclib/formats/arj/xx_arj.h"
#include "xxfclib/formats/cab/xx_cab.h"
#include "xxfclib/formats/aixbff/xx_aixbff.h"
#include "xxfclib/formats/arx/xx_arx.h"
#include "xxfclib/formats/sfx_analogx_emucore_ffs/xx_sfx_analogx_emucore_ffs.h"
#include "xxfclib/formats/sfx_krzip/xx_sfx_krzip.h"
#include "xxfclib/formats/sfx_warpin_package/xx_sfx_warpin_package.h"
#include "xxfclib/formats/sfx_hci_instalit/xx_sfx_hci_instalit.h"
#include "xxfclib/formats/sfx_clickteam_multimedia_fusion/xx_sfx_clickteam_multimedia_fusion.h"
#include "xxfclib/formats/sfx_abbyy_fine_objects/xx_sfx_abbyy_fine_objects.h"
#include "xxfclib/formats/sfx_flashjester_jugglor/xx_sfx_flashjester_jugglor.h"
#include "xxfclib/formats/sfx_jgsoft_deploymaster_package/xx_sfx_jgsoft_deploymaster_package.h"
#include "xxfclib/formats/sfx_ardi_diskette_image/xx_sfx_ardi_diskette_image.h"
#include "xxfclib/formats/sfx_nullsoft_pimp/xx_sfx_nullsoft_pimp.h"
#include "xxfclib/formats/sfx_sydex_diskette_image/xx_sfx_sydex_diskette_image.h"
#include "xxfclib/formats/sfx_compaq_softpaq/xx_sfx_compaq_softpaq.h"
#include "xxfclib/formats/sfx_wasp_windows_auto/xx_sfx_wasp_windows_auto.h"
#include "xxfclib/formats/wise_installation_system/xx_wise_installation_system.h"
#include "xxfclib/formats/eschalon_setup_epsf/xx_eschalon_setup_epsf.h"
#include "xxfclib/formats/gentee_installer/xx_gentee_installer.h"
#include "xxfclib/formats/clickteam_install_creator/xx_clickteam_install_creator.h"
#include "xxfclib/formats/createinstall_instcrin_extractor/xx_createinstall_instcrin_extractor.h"
#include "xxfclib/formats/sfxstart/xx_sfxstart.h"
#include "xxfclib/formats/modbus_tcp/xx_modbus_tcp.h"
#include "xxfclib/formats/someip_message/xx_someip_message.h"
#include "xxfclib/formats/dds_rtps/xx_dds_rtps.h"
#include "xxfclib/formats/rip_message/xx_rip_message.h"
#include "xxfclib/formats/vrrp_message/xx_vrrp_message.h"
#include "xxfclib/formats/igmp_message/xx_igmp_message.h"
#include "xxfclib/formats/pim_message/xx_pim_message.h"
#include "xxfclib/formats/ldp_message/xx_ldp_message.h"
#include "xxfclib/formats/gre_packet/xx_gre_packet.h"
#include "xxfclib/formats/l2tp_packet/xx_l2tp_packet.h"
#include "xxfclib/formats/lldp_message/xx_lldp_message.h"
#include "xxfclib/formats/netflow_datagram/xx_netflow_datagram.h"
#include "xxfclib/formats/ntlm_message/xx_ntlm_message.h"
#include "xxfclib/formats/dcerpc_pdu/xx_dcerpc_pdu.h"
#include "xxfclib/formats/ethereum_rlp/xx_ethereum_rlp.h"
#include "xxfclib/formats/imagemagick_miff/xx_imagemagick_miff.h"
#include "xxfclib/formats/avs_image/xx_avs_image.h"
#include "xxfclib/formats/scanalytics_iplab/xx_scanalytics_iplab.h"
#include "xxfclib/formats/mtv_image/xx_mtv_image.h"
#include "xxfclib/formats/nokia_ota_bitmap/xx_nokia_ota_bitmap.h"
#include "xxfclib/formats/apple_pict/xx_apple_pict.h"
#include "xxfclib/formats/wordperfect_wpg/xx_wordperfect_wpg.h"
#include "xxfclib/formats/nasa_vicar/xx_nasa_vicar.h"
#include "xxfclib/formats/khoros_viff/xx_khoros_viff.h"
#include "xxfclib/formats/imagemagick_mvg/xx_imagemagick_mvg.h"
#include "xxfclib/formats/motif_uil/xx_motif_uil.h"
#include "xxfclib/formats/iges_model/xx_iges_model.h"
#include "xxfclib/formats/openusd_usda/xx_openusd_usda.h"
#include "xxfclib/formats/ufo_glif/xx_ufo_glif.h"
#include "xxfclib/formats/unifont_hex/xx_unifont_hex.h"
#include "xxfclib/formats/adlib_sop/xx_adlib_sop.h"
#include "xxfclib/formats/cudfm_cff/xx_cudfm_cff.h"
#include "xxfclib/formats/adlib_jbm/xx_adlib_jbm.h"
#include "xxfclib/formats/ceres_msc/xx_ceres_msc.h"
#include "xxfclib/formats/adlib_xsm/xx_adlib_xsm.h"
#include "xxfclib/formats/ken_ksm/xx_ken_ksm.h"
#include "xxfclib/formats/implay_music/xx_implay_music.h"
#include "xxfclib/formats/adlib_mtr/xx_adlib_mtr.h"
#include "xxfclib/formats/rdos_raw/xx_rdos_raw.h"
#include "xxfclib/formats/mad_tracker/xx_mad_tracker.h"
#include "xxfclib/formats/vasp_poscar/xx_vasp_poscar.h"
#include "xxfclib/formats/quantum_espresso_input/xx_quantum_espresso_input.h"
#include "xxfclib/formats/cp2k_input/xx_cp2k_input.h"
#include "xxfclib/formats/nwchem_input/xx_nwchem_input.h"
#include "xxfclib/formats/gamess_input/xx_gamess_input.h"
#include "xxfclib/formats/gaussian_input/xx_gaussian_input.h"
#include "xxfclib/formats/abinit_input/xx_abinit_input.h"
#include "xxfclib/formats/aims_geometry/xx_aims_geometry.h"
#include "xxfclib/formats/orca_input/xx_orca_input.h"
#include "xxfclib/formats/demon_input/xx_demon_input.h"
#include "xxfclib/formats/ethernet_frame/xx_ethernet_frame.h"
#include "xxfclib/formats/ip_packet/xx_ip_packet.h"
#include "xxfclib/formats/arp_packet/xx_arp_packet.h"
#include "xxfclib/formats/icmp_message/xx_icmp_message.h"
#include "xxfclib/formats/sip_message/xx_sip_message.h"
#include "xxfclib/formats/rtsp_message/xx_rtsp_message.h"
#include "xxfclib/formats/diameter_message/xx_diameter_message.h"
#include "xxfclib/formats/tacacs_packet/xx_tacacs_packet.h"
#include "xxfclib/formats/gtp_message/xx_gtp_message.h"
#include "xxfclib/formats/pfcp_message/xx_pfcp_message.h"
#include "xxfclib/formats/pptp_message/xx_pptp_message.h"
#include "xxfclib/formats/rsvp_message/xx_rsvp_message.h"
#include "xxfclib/formats/age_encrypted/xx_age_encrypted.h"
#include "xxfclib/formats/kerberos_ccache/xx_kerberos_ccache.h"
#include "xxfclib/formats/jose_jws/xx_jose_jws.h"
#include "xxfclib/formats/wbmp_image/xx_wbmp_image.h"
#include "xxfclib/formats/dec_sixel/xx_dec_sixel.h"
#include "xxfclib/formats/palm_bitmap/xx_palm_bitmap.h"
#include "xxfclib/formats/adobe_acv/xx_adobe_acv.h"
#include "xxfclib/formats/adobe_act/xx_adobe_act.h"
#include "xxfclib/formats/ogre_skeleton/xx_ogre_skeleton.h"
#include "xxfclib/formats/cal3d_skeleton/xx_cal3d_skeleton.h"
#include "xxfclib/formats/collada_dae/xx_collada_dae.h"
#include "xxfclib/formats/lightwave_scene/xx_lightwave_scene.h"
#include "xxfclib/formats/dsn6_density/xx_dsn6_density.h"
#include "xxfclib/formats/crystallography_mtz/xx_crystallography_mtz.h"
#include "xxfclib/formats/amira_mesh/xx_amira_mesh.h"
#include "xxfclib/formats/tetgen_mesh/xx_tetgen_mesh.h"
#include "xxfclib/formats/jedec_fuse/xx_jedec_fuse.h"
#include "xxfclib/formats/qchem_input/xx_qchem_input.h"
#include "xxfclib/formats/adlib_bam/xx_adlib_bam.h"
#include "xxfclib/formats/adlib_bmf/xx_adlib_bmf.h"
#include "xxfclib/formats/creative_cmf/xx_creative_cmf.h"
#include "xxfclib/formats/adlib_dfm/xx_adlib_dfm.h"
#include "xxfclib/formats/adlib_lds/xx_adlib_lds.h"
#include "xxfclib/formats/adlib_mkj/xx_adlib_mkj.h"
#include "xxfclib/formats/adlib_rol/xx_adlib_rol.h"
#include "xxfclib/formats/adlib_sa2/xx_adlib_sa2.h"
#include "xxfclib/formats/faust_fmc/xx_faust_fmc.h"
#include "xxfclib/formats/softstar_rix/xx_softstar_rix.h"
#include "xxfclib/formats/genomics_bed/xx_genomics_bed.h"
#include "xxfclib/formats/genomics_wiggle/xx_genomics_wiggle.h"
#include "xxfclib/formats/genomics_gtf/xx_genomics_gtf.h"
#include "xxfclib/formats/genomics_agp/xx_genomics_agp.h"
#include "xxfclib/formats/sequencing_abif/xx_sequencing_abif.h"
#include "xxfclib/formats/sequencing_scf/xx_sequencing_scf.h"
#include "xxfclib/formats/genomics_sff/xx_genomics_sff.h"
#include "xxfclib/formats/lut_spi1d/xx_lut_spi1d.h"
#include "xxfclib/formats/lut_spi3d/xx_lut_spi3d.h"
#include "xxfclib/formats/lut_cinespace_csp/xx_lut_cinespace_csp.h"
#include "xxfclib/formats/ntp_message/xx_ntp_message.h"
#include "xxfclib/formats/rtp_rtcp/xx_rtp_rtcp.h"
#include "xxfclib/formats/bgp_messages/xx_bgp_messages.h"
#include "xxfclib/formats/ospf_packet/xx_ospf_packet.h"
#include "xxfclib/formats/sctp_packet/xx_sctp_packet.h"
#include "xxfclib/formats/isakmp_message/xx_isakmp_message.h"
#include "xxfclib/formats/ssh_transport/xx_ssh_transport.h"
#include "xxfclib/formats/smtp_transcript/xx_smtp_transcript.h"
#include "xxfclib/formats/pkcs8_private_key/xx_pkcs8_private_key.h"
#include "xxfclib/formats/putty_ppk/xx_putty_ppk.h"
#include "xxfclib/formats/openssh_certificate/xx_openssh_certificate.h"
#include "xxfclib/formats/safetensors/xx_safetensors.h"
#include "xxfclib/formats/gguf/xx_gguf.h"
#include "xxfclib/formats/cdb_database/xx_cdb_database.h"
#include "xxfclib/formats/stomp_frames/xx_stomp_frames.h"
#include "xxfclib/formats/fontforge_sfd/xx_fontforge_sfd.h"
#include "xxfclib/formats/grub_pff2/xx_grub_pff2.h"
#include "xxfclib/formats/opengex_model/xx_opengex_model.h"
#include "xxfclib/formats/bvh_motion/xx_bvh_motion.h"
#include "xxfclib/formats/directx_x/xx_directx_x.h"
#include "xxfclib/formats/gts_surface/xx_gts_surface.h"
#include "xxfclib/formats/medit_mesh/xx_medit_mesh.h"
#include "xxfclib/formats/gocad_model/xx_gocad_model.h"
#include "xxfclib/formats/nastran_bulk/xx_nastran_bulk.h"
#include "xxfclib/formats/abaqus_input/xx_abaqus_input.h"
#include "xxfclib/formats/ensight_gold_geometry/xx_ensight_gold_geometry.h"
#include "xxfclib/formats/gmv_mesh/xx_gmv_mesh.h"
#include "xxfclib/formats/usgs_dem/xx_usgs_dem.h"
#include "xxfclib/formats/dted_elevation/xx_dted_elevation.h"
#include "xxfclib/formats/mapinfo_mif/xx_mapinfo_mif.h"
#include "xxfclib/formats/tracker_coconizer/xx_tracker_coconizer.h"
#include "xxfclib/formats/tracker_real/xx_tracker_real.h"
#include "xxfclib/formats/tracker_megatracker/xx_tracker_megatracker.h"
#include "xxfclib/formats/amos_music_bank/xx_amos_music_bank.h"
#include "xxfclib/formats/adlib_rad/xx_adlib_rad.h"
#include "xxfclib/formats/adlib_amd/xx_adlib_amd.h"
#include "xxfclib/formats/adlib_hsc/xx_adlib_hsc.h"
#include "xxfclib/formats/adlib_d00/xx_adlib_d00.h"
#include "xxfclib/formats/adlib_bnk/xx_adlib_bnk.h"
#include "xxfclib/formats/dosbox_dro/xx_dosbox_dro.h"
#include "xxfclib/formats/genomics_genbank/xx_genomics_genbank.h"
#include "xxfclib/formats/genomics_embl/xx_genomics_embl.h"
#include "xxfclib/formats/genomics_swissprot/xx_genomics_swissprot.h"
#include "xxfclib/formats/alignment_clustal/xx_alignment_clustal.h"
#include "xxfclib/formats/alignment_stockholm/xx_alignment_stockholm.h"
#include "xxfclib/formats/alignment_phylip/xx_alignment_phylip.h"
#include "xxfclib/formats/alignment_maf/xx_alignment_maf.h"
#include "xxfclib/formats/alignment_mauve/xx_alignment_mauve.h"
#include "xxfclib/formats/ucsc_nib/xx_ucsc_nib.h"
#include "xxfclib/formats/assembly_gfa/xx_assembly_gfa.h"
#include "xxfclib/formats/http1_message/xx_http1_message.h"
#include "xxfclib/formats/websocket_frames/xx_websocket_frames.h"
#include "xxfclib/formats/coap_message/xx_coap_message.h"
#include "xxfclib/formats/stun_message/xx_stun_message.h"
#include "xxfclib/formats/dhcp_message/xx_dhcp_message.h"
#include "xxfclib/formats/radius_packet/xx_radius_packet.h"
#include "xxfclib/formats/snmp_message/xx_snmp_message.h"
#include "xxfclib/formats/ldap_message/xx_ldap_message.h"
#include "xxfclib/formats/tls_records/xx_tls_records.h"
#include "xxfclib/formats/jks_keystore/xx_jks_keystore.h"
#include "xxfclib/formats/java_serialization/xx_java_serialization.h"
#include "xxfclib/formats/x509_crl/xx_x509_crl.h"
#include "xxfclib/formats/ocsp_response/xx_ocsp_response.h"
#include "xxfclib/formats/lmdb_data/xx_lmdb_data.h"
#include "xxfclib/formats/gdbm_dump/xx_gdbm_dump.h"
#include "xxfclib/formats/adobe_acb/xx_adobe_acb.h"
#include "xxfclib/formats/jasc_palette/xx_jasc_palette.h"
#include "xxfclib/formats/x11_xbm/xx_x11_xbm.h"
#include "xxfclib/formats/jpeg2000_pgx/xx_jpeg2000_pgx.h"
#include "xxfclib/formats/amiga_diskobject/xx_amiga_diskobject.h"
#include "xxfclib/formats/tex_vf/xx_tex_vf.h"
#include "xxfclib/formats/esri_ascii_grid/xx_esri_ascii_grid.h"
#include "xxfclib/formats/surfer_grid/xx_surfer_grid.h"
#include "xxfclib/formats/gxf_grid/xx_gxf_grid.h"
#include "xxfclib/formats/ogc_wkt/xx_ogc_wkt.h"
#include "xxfclib/formats/step_part21/xx_step_part21.h"
#include "xxfclib/formats/gerber_rs274x/xx_gerber_rs274x.h"
#include "xxfclib/formats/excellon_drill/xx_excellon_drill.h"
#include "xxfclib/formats/vrml_scene/xx_vrml_scene.h"
#include "xxfclib/formats/renderman_rib/xx_renderman_rib.h"
#include "xxfclib/formats/asylum_amf/xx_asylum_amf.h"
#include "xxfclib/formats/tracker_stx/xx_tracker_stx.h"
#include "xxfclib/formats/tracker_dtm/xx_tracker_dtm.h"
#include "xxfclib/formats/tracker_soundfx/xx_tracker_soundfx.h"
#include "xxfclib/formats/tracker_funk/xx_tracker_funk.h"
#include "xxfclib/formats/tracker_archimedes/xx_tracker_archimedes.h"
#include "xxfclib/formats/pce_psi/xx_pce_psi.h"
#include "xxfclib/formats/pc98_d88/xx_pc98_d88.h"
#include "xxfclib/formats/hxc_mfm/xx_hxc_mfm.h"
#include "xxfclib/formats/amiga_ext_adf/xx_amiga_ext_adf.h"
#include "xxfclib/formats/amiga_old_ext_adf/xx_amiga_old_ext_adf.h"
#include "xxfclib/formats/atari_dim/xx_atari_dim.h"
#include "xxfclib/formats/atari_stt/xx_atari_stt.h"
#include "xxfclib/formats/atari_stw/xx_atari_stw.h"
#include "xxfclib/formats/discferret_dfi/xx_discferret_dfi.h"
#include "xxfclib/formats/hxc_afi/xx_hxc_afi.h"
#include "xxfclib/formats/hxc_qd/xx_hxc_qd.h"
#include "xxfclib/formats/hxc_stream/xx_hxc_stream.h"
#include "xxfclib/formats/svd/xx_svd.h"
#include "xxfclib/formats/sdu/xx_sdu.h"
#include "xxfclib/formats/fei/xx_fei.h"
#include "xxfclib/formats/oric_dsk/xx_oric_dsk.h"
#include "xxfclib/formats/ensoniq_gkh/xx_ensoniq_gkh.h"
#include "xxfclib/formats/ensoniq_ede/xx_ensoniq_ede.h"
#include "xxfclib/formats/samcoupe_sad/xx_samcoupe_sad.h"
#include "xxfclib/formats/apple_nib/xx_apple_nib.h"
#include "xxfclib/formats/ti99_pc99/xx_ti99_pc99.h"
#include "xxfclib/formats/emax_disk/xx_emax_disk.h"
#include "xxfclib/formats/emulatorii_eii/xx_emulatorii_eii.h"
#include "xxfclib/formats/casio_fzf/xx_casio_fzf.h"
#include "xxfclib/formats/vtr_disk/xx_vtr_disk.h"
#include "xxfclib/formats/speccydos_sdd/xx_speccydos_sdd.h"
#include "xxfclib/formats/hxc_raw_floppy/xx_hxc_raw_floppy.h"
#include "xxfclib/formats/hxc_xml_disk_layout/xx_hxc_xml_disk_layout.h"

#include "xxfclib/formats/yaze_ydsk/xx_yaze_ydsk.h"
#include "xxfclib/formats/lammps_data/xx_lammps_data.h"
#include "xxfclib/formats/lammps_dump/xx_lammps_dump.h"
#include "xxfclib/formats/shelx_res/xx_shelx_res.h"
#include "xxfclib/formats/turbomole_coord/xx_turbomole_coord.h"
#include "xxfclib/formats/charmm_crd/xx_charmm_crd.h"
#include "xxfclib/formats/castep_cell/xx_castep_cell.h"
#include "xxfclib/formats/crystal_fort34/xx_crystal_fort34.h"
#include "xxfclib/formats/siesta_xv/xx_siesta_xv.h"
#include "xxfclib/formats/harwell_boeing/xx_harwell_boeing.h"
#include "xxfclib/formats/openfoam_points/xx_openfoam_points.h"
#include "xxfclib/formats/minecraft_nbt/xx_minecraft_nbt.h"
#include "xxfclib/formats/amazon_ion_binary/xx_amazon_ion_binary.h"
#include "xxfclib/formats/leveldb_log/xx_leveldb_log.h"
#include "xxfclib/formats/dns_message/xx_dns_message.h"
#include "xxfclib/formats/rocksdb_blob/xx_rocksdb_blob.h"
#include "xxfclib/formats/mongodb_wire/xx_mongodb_wire.h"
#include "xxfclib/formats/redis_resp/xx_redis_resp.h"
#include "xxfclib/formats/mqtt_packets/xx_mqtt_packets.h"
#include "xxfclib/formats/amqp_frames/xx_amqp_frames.h"
#include "xxfclib/formats/thrift_compact/xx_thrift_compact.h"
#include "xxfclib/formats/x509_certificate/xx_x509_certificate.h"
#include "xxfclib/formats/pkcs10_csr/xx_pkcs10_csr.h"
#include "xxfclib/formats/pkcs12_pfx/xx_pkcs12_pfx.h"
#include "xxfclib/formats/openssh_private_key/xx_openssh_private_key.h"
#include "xxfclib/formats/kerberos_keytab/xx_kerberos_keytab.h"
#include "xxfclib/formats/gimp_gpl/xx_gimp_gpl.h"
#include "xxfclib/formats/gimp_ggr/xx_gimp_ggr.h"
#include "xxfclib/formats/iridas_cube_lut/xx_iridas_cube_lut.h"
#include "xxfclib/formats/hpgl_plot/xx_hpgl_plot.h"
#include "xxfclib/formats/paintshop_psp/xx_paintshop_psp.h"
#include "xxfclib/formats/photoshop_pat/xx_photoshop_pat.h"
#include "xxfclib/formats/mmd_pmx/xx_mmd_pmx.h"
#include "xxfclib/formats/metasequoia_mqo/xx_metasequoia_mqo.h"
#include "xxfclib/formats/calma_gdsii/xx_calma_gdsii.h"
#include "xxfclib/formats/autodesk_ase/xx_autodesk_ase.h"
#include "xxfclib/formats/freesurfer_surface/xx_freesurfer_surface.h"
#include "xxfclib/formats/gmsh_msh/xx_gmsh_msh.h"
#include "xxfclib/formats/netgen_vol/xx_netgen_vol.h"
#include "xxfclib/formats/font_afm/xx_font_afm.h"
#include "xxfclib/formats/tiled_tmx/xx_tiled_tmx.h"
#include "xxfclib/formats/nintendo_sdat/xx_nintendo_sdat.h"
#include "xxfclib/formats/sony_vab/xx_sony_vab.h"
#include "xxfclib/formats/yamaha_ym/xx_yamaha_ym.h"
#include "xxfclib/formats/zx_ayemul/xx_zx_ayemul.h"
#include "xxfclib/formats/dragon_vdk/xx_dragon_vdk.h"
#include "xxfclib/formats/apple_a2r/xx_apple_a2r.h"
#include "xxfclib/formats/atari_atr/xx_atari_atr.h"
#include "xxfclib/formats/atari_pasti_stx/xx_atari_pasti_stx.h"
#include "xxfclib/formats/amiga_ipf/xx_amiga_ipf.h"
#include "xxfclib/formats/tracker_dtt/xx_tracker_dtt.h"
#include "xxfclib/formats/gaussian_cube/xx_gaussian_cube.h"
#include "xxfclib/formats/molecule_xyz/xx_molecule_xyz.h"
#include "xxfclib/formats/mdl_molfile/xx_mdl_molfile.h"
#include "xxfclib/formats/tripos_mol2/xx_tripos_mol2.h"
#include "xxfclib/formats/xcrysden_xsf/xx_xcrysden_xsf.h"
#include "xxfclib/formats/amber_prmtop/xx_amber_prmtop.h"
#include "xxfclib/formats/amber_restart/xx_amber_restart.h"
#include "xxfclib/formats/gaussian_fchk/xx_gaussian_fchk.h"
#include "xxfclib/formats/jcamp_dx/xx_jcamp_dx.h"
#include "xxfclib/formats/dl_poly_config/xx_dl_poly_config.h"
#include "xxfclib/formats/nix_nar/xx_nix_nar.h"
#include "xxfclib/formats/redis_rdb/xx_redis_rdb.h"
#include "xxfclib/formats/postgres_custom/xx_postgres_custom.h"
#include "xxfclib/formats/mysql_binlog/xx_mysql_binlog.h"
#include "xxfclib/formats/kafka_record_batch/xx_kafka_record_batch.h"
#include "xxfclib/formats/android_binary_xml/xx_android_binary_xml.h"
#include "xxfclib/formats/android_resources_arsc/xx_android_resources_arsc.h"
#include "xxfclib/formats/msgpack/xx_msgpack.h"
#include "xxfclib/formats/ubjson/xx_ubjson.h"
#include "xxfclib/formats/bittorrent_metainfo/xx_bittorrent_metainfo.h"
#include "xxfclib/formats/erlang_external_term/xx_erlang_external_term.h"
#include "xxfclib/formats/capnproto_message/xx_capnproto_message.h"
#include "xxfclib/formats/dbus_message/xx_dbus_message.h"
#include "xxfclib/formats/windows_shell_link/xx_windows_shell_link.h"
#include "xxfclib/formats/pkcs7_cms/xx_pkcs7_cms.h"
#include "xxfclib/formats/wavefront_obj/xx_wavefront_obj.h"
#include "xxfclib/formats/off_mesh/xx_off_mesh.h"
#include "xxfclib/formats/ac3d_model/xx_ac3d_model.h"
#include "xxfclib/formats/qubicle_qb/xx_qubicle_qb.h"
#include "xxfclib/formats/terragen_ter/xx_terragen_ter.h"
#include "xxfclib/formats/gimp_xcf/xx_gimp_xcf.h"
#include "xxfclib/formats/photoshop_abr/xx_photoshop_abr.h"
#include "xxfclib/formats/softimage_pic/xx_softimage_pic.h"
#include "xxfclib/formats/alias_pix/xx_alias_pix.h"
#include "xxfclib/formats/qt_qpicture/xx_qt_qpicture.h"
#include "xxfclib/formats/font_type1_pfb/xx_font_type1_pfb.h"
#include "xxfclib/formats/font_gem_fnt/xx_font_gem_fnt.h"
#include "xxfclib/formats/tex_gf/xx_tex_gf.h"
#include "xxfclib/formats/bpg_image/xx_bpg_image.h"
#include "xxfclib/formats/mng_animation/xx_mng_animation.h"
#include "xxfclib/formats/atari_7800_a78/xx_atari_7800_a78.h"
#include "xxfclib/formats/commodore_pc64/xx_commodore_pc64.h"
#include "xxfclib/formats/atari_cas/xx_atari_cas.h"
#include "xxfclib/formats/msx_cas/xx_msx_cas.h"
#include "xxfclib/formats/oric_tap/xx_oric_tap.h"
#include "xxfclib/formats/dragon_cas/xx_dragon_cas.h"
#include "xxfclib/formats/amiga_ahx/xx_amiga_ahx.h"
#include "xxfclib/formats/amstrad_cpc_sna/xx_amstrad_cpc_sna.h"
#include "xxfclib/formats/vtech_vz/xx_vtech_vz.h"
#include "xxfclib/formats/zx_hobeta/xx_zx_hobeta.h"
#include "xxfclib/formats/genomics_fasta/xx_genomics_fasta.h"
#include "xxfclib/formats/genomics_fastq/xx_genomics_fastq.h"
#include "xxfclib/formats/genomics_sam/xx_genomics_sam.h"
#include "xxfclib/formats/opendx_field/xx_opendx_field.h"
#include "xxfclib/formats/genomics_vcf/xx_genomics_vcf.h"
#include "xxfclib/formats/genomics_gff3/xx_genomics_gff3.h"
#include "xxfclib/formats/protein_pdb/xx_protein_pdb.h"
#include "xxfclib/formats/protein_mmcif/xx_protein_mmcif.h"
#include "xxfclib/formats/matrix_market/xx_matrix_market.h"
#include "xxfclib/formats/gromacs_gro/xx_gromacs_gro.h"
#include "xxfclib/formats/microsoft_msf/xx_microsoft_msf.h"
#include "xxfclib/formats/windows_registry_hive/xx_windows_registry_hive.h"
#include "xxfclib/formats/windows_evtx/xx_windows_evtx.h"
#include "xxfclib/formats/binary_plist/xx_binary_plist.h"
#include "xxfclib/formats/mongodb_bson/xx_mongodb_bson.h"
#include "xxfclib/formats/cbor/xx_cbor.h"
#include "xxfclib/formats/openzim/xx_openzim.h"
#include "xxfclib/formats/apache_orc/xx_apache_orc.h"
#include "xxfclib/formats/hadoop_sequencefile/xx_hadoop_sequencefile.h"
#include "xxfclib/formats/leveldb_sstable/xx_leveldb_sstable.h"
#include "xxfclib/formats/snappy_framed/xx_snappy_framed.h"
#include "xxfclib/formats/lzf_stream/xx_lzf_stream.h"
#include "xxfclib/formats/fastlz_sixpack/xx_fastlz_sixpack.h"
#include "xxfclib/formats/linux_btf/xx_linux_btf.h"
#include "xxfclib/formats/flatgeobuf/xx_flatgeobuf.h"
#include "xxfclib/formats/astc_texture/xx_astc_texture.h"
#include "xxfclib/formats/pkm_texture/xx_pkm_texture.h"
#include "xxfclib/formats/basis_texture/xx_basis_texture.h"
#include "xxfclib/formats/openctm_mesh/xx_openctm_mesh.h"
#include "xxfclib/formats/font_bdf/xx_font_bdf.h"
#include "xxfclib/formats/font_pcf/xx_font_pcf.h"
#include "xxfclib/formats/font_psf/xx_font_psf.h"
#include "xxfclib/formats/font_windows_fnt/xx_font_windows_fnt.h"
#include "xxfclib/formats/tex_tfm/xx_tex_tfm.h"
#include "xxfclib/formats/tex_pk/xx_tex_pk.h"
#include "xxfclib/formats/tex_dvi/xx_tex_dvi.h"
#include "xxfclib/formats/netpbm_pfm/xx_netpbm_pfm.h"
#include "xxfclib/formats/steinberg_vst3preset/xx_steinberg_vst3preset.h"
#include "xxfclib/formats/font_bmfont/xx_font_bmfont.h"
#include "xxfclib/formats/processing_vlw/xx_processing_vlw.h"
#include "xxfclib/formats/snes_spc/xx_snes_spc.h"
#include "xxfclib/formats/gameboy_gbs/xx_gameboy_gbs.h"
#include "xxfclib/formats/sega_sgc/xx_sega_sgc.h"
#include "xxfclib/formats/s98_log/xx_s98_log.h"
#include "xxfclib/formats/atari_sap/xx_atari_sap.h"
#include "xxfclib/formats/sc68_music/xx_sc68_music.h"
#include "xxfclib/formats/zx_spectrum_pzx/xx_zx_spectrum_pzx.h"
#include "xxfclib/formats/acorn_uef/xx_acorn_uef.h"
#include "xxfclib/formats/nintendo_unif/xx_nintendo_unif.h"
#include "xxfclib/formats/nintendo_fds/xx_nintendo_fds.h"
#include "xxfclib/formats/ucsc_bigwig/xx_ucsc_bigwig.h"
#include "xxfclib/formats/ucsc_bigbed/xx_ucsc_bigbed.h"
#include "xxfclib/formats/phylo_nexus/xx_phylo_nexus.h"
#include "xxfclib/formats/phylo_newick/xx_phylo_newick.h"
#include "xxfclib/formats/sqlite_rollback_journal/xx_sqlite_rollback_journal.h"
#include "xxfclib/formats/neuroscan_cnt/xx_neuroscan_cnt.h"
#include "xxfclib/formats/axona_tetrode/xx_axona_tetrode.h"
#include "xxfclib/formats/python_pickle/xx_python_pickle.h"
#include "xxfclib/formats/inivation_aedat/xx_inivation_aedat.h"
#include "xxfclib/formats/python_marshal/xx_python_marshal.h"
#include "xxfclib/formats/pyc/xx_pyc.h"
#include "xxfclib/formats/vice_x64/xx_vice_x64.h"
#include "xxfclib/formats/vice_snapshot/xx_vice_snapshot.h"
#include "xxfclib/formats/commodore_g64/xx_commodore_g64.h"
#include "xxfclib/formats/commodore_p64/xx_commodore_p64.h"
#include "xxfclib/formats/commodore_tap/xx_commodore_tap.h"
#include "xxfclib/formats/zx_spectrum_tzx/xx_zx_spectrum_tzx.h"
#include "xxfclib/formats/zx_spectrum_szx/xx_zx_spectrum_szx.h"
#include "xxfclib/formats/amstrad_cpc_dsk/xx_amstrad_cpc_dsk.h"
#include "xxfclib/formats/atari_st_msa/xx_atari_st_msa.h"
#include "xxfclib/formats/supercard_scp/xx_supercard_scp.h"
#include "xxfclib/formats/apple_woz/xx_apple_woz.h"
#include "xxfclib/formats/nintendo_nsf/xx_nintendo_nsf.h"
#include "xxfclib/formats/vgm_log/xx_vgm_log.h"
#include "xxfclib/formats/psid_sid/xx_psid_sid.h"
#include "xxfclib/formats/hes_sound/xx_hes_sound.h"
#include "xxfclib/formats/audio_dolby_ac3/xx_audio_dolby_ac3.h"
#include "xxfclib/formats/audio_mpeg_mp3/xx_audio_mpeg_mp3.h"
#include "xxfclib/formats/audio_aac_adts/xx_audio_aac_adts.h"
#include "xxfclib/formats/audio_monkeys_ape/xx_audio_monkeys_ape.h"
#include "xxfclib/formats/mpeg_transport_stream/xx_mpeg_transport_stream.h"
#include "xxfclib/formats/mpeg_program_stream/xx_mpeg_program_stream.h"
#include "xxfclib/formats/realmedia_rm/xx_realmedia_rm.h"
#include "xxfclib/formats/idtech_roq/xx_idtech_roq.h"
#include "xxfclib/formats/rad_bink/xx_rad_bink.h"
#include "xxfclib/formats/rad_smacker/xx_rad_smacker.h"
#include "xxfclib/formats/interplay_mve/xx_interplay_mve.h"
#include "xxfclib/formats/westwood_vqa/xx_westwood_vqa.h"
#include "xxfclib/formats/autodesk_flic/xx_autodesk_flic.h"
#include "xxfclib/formats/idtech_md5anim/xx_idtech_md5anim.h"
#include "xxfclib/formats/stereolithography_stl/xx_stereolithography_stl.h"
#include "xxfclib/formats/garmin_fit/xx_garmin_fit.h"
#include "xxfclib/formats/rosbag1/xx_rosbag1.h"
#include "xxfclib/formats/mcap/xx_mcap.h"
#include "xxfclib/formats/seismic_sac/xx_seismic_sac.h"
#include "xxfclib/formats/seismic_seg2/xx_seismic_seg2.h"
#include "xxfclib/formats/ucsc_twobit/xx_ucsc_twobit.h"
#include "xxfclib/formats/genomics_bgen/xx_genomics_bgen.h"
#include "xxfclib/formats/openephys_continuous/xx_openephys_continuous.h"
#include "xxfclib/formats/mountainsort_mda/xx_mountainsort_mda.h"
#include "xxfclib/formats/igor_ibw/xx_igor_ibw.h"
#include "xxfclib/formats/princeton_spe/xx_princeton_spe.h"
#include "xxfclib/formats/microscopy_spider/xx_microscopy_spider.h"
#include "xxfclib/formats/wmo_grib/xx_wmo_grib.h"
#include "xxfclib/formats/wmo_bufr/xx_wmo_bufr.h"
#include "xxfclib/formats/autocad_dxf/xx_autocad_dxf.h"
#include "xxfclib/formats/blackrock_nsx/xx_blackrock_nsx.h"
#include "xxfclib/formats/blackrock_nev/xx_blackrock_nev.h"
#include "xxfclib/formats/lecroy_trc/xx_lecroy_trc.h"
#include "xxfclib/formats/tektronix_isf/xx_tektronix_isf.h"
#include "xxfclib/formats/ircam_sdif/xx_ircam_sdif.h"
#include "xxfclib/formats/tracker_liquid/xx_tracker_liquid.h"
#include "xxfclib/formats/tracker_dmf/xx_tracker_dmf.h"
#include "xxfclib/formats/tracker_ptm/xx_tracker_ptm.h"
#include "xxfclib/formats/tracker_ams/xx_tracker_ams.h"
#include "xxfclib/formats/tracker_digi/xx_tracker_digi.h"
#include "xxfclib/formats/tracker_emod/xx_tracker_emod.h"
#include "xxfclib/formats/tracker_mt2/xx_tracker_mt2.h"
#include "xxfclib/formats/audio_dsf/xx_audio_dsf.h"
#include "xxfclib/formats/audio_dff/xx_audio_dff.h"
#include "xxfclib/formats/audio_wave64/xx_audio_wave64.h"
#include "xxfclib/formats/audio_adx/xx_audio_adx.h"
#include "xxfclib/formats/audio_ast/xx_audio_ast.h"
#include "xxfclib/formats/audio_hca/xx_audio_hca.h"
#include "xxfclib/formats/iff_8svx/xx_iff_8svx.h"
#include "xxfclib/formats/audio_wavpack/xx_audio_wavpack.h"
#include "xxfclib/formats/blender_blend/xx_blender_blend.h"
#include "xxfclib/formats/autodesk_fbx/xx_autodesk_fbx.h"
#include "xxfclib/formats/autodesk_3ds/xx_autodesk_3ds.h"
#include "xxfclib/formats/lightwave_lwo2/xx_lightwave_lwo2.h"
#include "xxfclib/formats/lightwave_mdd/xx_lightwave_mdd.h"
#include "xxfclib/formats/sony_psp_pbp/xx_sony_psp_pbp.h"
#include "xxfclib/formats/flash_video_flv/xx_flash_video_flv.h"
#include "xxfclib/formats/nintendo_n64_rom/xx_nintendo_n64_rom.h"
#include "xxfclib/formats/nintendo_gb_rom/xx_nintendo_gb_rom.h"
#include "xxfclib/formats/nintendo_gba_rom/xx_nintendo_gba_rom.h"
#include "xxfclib/formats/sega_megadrive_rom/xx_sega_megadrive_rom.h"
#include "xxfclib/formats/spring_s3o/xx_spring_s3o.h"
#include "xxfclib/formats/xna_xnb/xx_xna_xnb.h"
#include "xxfclib/formats/lua_bytecode51/xx_lua_bytecode51.h"
#include "xxfclib/formats/quake_md5mesh/xx_quake_md5mesh.h"
#include "xxfclib/formats/tracker_mod/xx_tracker_mod.h"
#include "xxfclib/formats/tracker_far/xx_tracker_far.h"
#include "xxfclib/formats/tracker_mdl/xx_tracker_mdl.h"
#include "xxfclib/formats/tracker_gdm/xx_tracker_gdm.h"
#include "xxfclib/formats/tracker_dbm/xx_tracker_dbm.h"
#include "xxfclib/formats/tracker_med/xx_tracker_med.h"
#include "xxfclib/formats/tracker_imf/xx_tracker_imf.h"
#include "xxfclib/formats/tracker_amf/xx_tracker_amf.h"
#include "xxfclib/formats/tracker_psm/xx_tracker_psm.h"
#include "xxfclib/formats/steinberg_fxb/xx_steinberg_fxb.h"
#include "xxfclib/formats/astronomy_ser/xx_astronomy_ser.h"
#include "xxfclib/formats/photontiming_ptu/xx_photontiming_ptu.h"
#include "xxfclib/formats/photontiming_phu/xx_photontiming_phu.h"
#include "xxfclib/formats/charmm_dcd/xx_charmm_dcd.h"
#include "xxfclib/formats/gromacs_trr/xx_gromacs_trr.h"
#include "xxfclib/formats/microscopy_ics/xx_microscopy_ics.h"
#include "xxfclib/formats/tecplot_plt/xx_tecplot_plt.h"
#include "xxfclib/formats/fujifilm_raf/xx_fujifilm_raf.h"
#include "xxfclib/formats/sigma_x3f/xx_sigma_x3f.h"
#include "xxfclib/formats/minolta_mrw/xx_minolta_mrw.h"
#include "xxfclib/formats/sfx_imp/xx_sfx_imp.h"
#include "xxfclib/formats/sfx_red/xx_sfx_red.h"
#include "xxfclib/formats/sfx_ha/xx_sfx_ha.h"
#include "xxfclib/formats/sfx_lzx/xx_sfx_lzx.h"
#include "xxfclib/formats/sfx_sqx/xx_sfx_sqx.h"
#include "xxfclib/formats/sfx_ain/xx_sfx_ain.h"
#include "xxfclib/formats/sfx_hap/xx_sfx_hap.h"
#include "xxfclib/formats/sfx_zoo/xx_sfx_zoo.h"
#include "xxfclib/formats/sfx_cazip/xx_sfx_cazip.h"
#include "xxfclib/formats/sfx_tgcf/xx_sfx_tgcf.h"
#include "xxfclib/formats/sfx_starkit/xx_sfx_starkit.h"
#include "xxfclib/formats/sfx_alz/xx_sfx_alz.h"
#include "xxfclib/formats/sfx_chm/xx_sfx_chm.h"
#include "xxfclib/formats/egg/xx_egg.h"
#include "xxfclib/formats/nufx/xx_nufx.h"
#include "xxfclib/formats/nintendo_dol/xx_nintendo_dol.h"
#include "xxfclib/formats/nintendo_j3d_bmd/xx_nintendo_j3d_bmd.h"
#include "xxfclib/formats/nintendo_j3d_btk/xx_nintendo_j3d_btk.h"
#include "xxfclib/formats/nintendo_brstm/xx_nintendo_brstm.h"
#include "xxfclib/formats/nintendo_brwav/xx_nintendo_brwav.h"
#include "xxfclib/formats/nintendo_brlyt/xx_nintendo_brlyt.h"
#include "xxfclib/formats/nintendo_brlan/xx_nintendo_brlan.h"
#include "xxfclib/formats/nintendo_bfsha/xx_nintendo_bfsha.h"
#include "xxfclib/formats/cri_usm/xx_cri_usm.h"
#include "xxfclib/formats/cri_utf/xx_cri_utf.h"
#include "xxfclib/formats/idtech_iqm/xx_idtech_iqm.h"
#include "xxfclib/formats/unreal_psk/xx_unreal_psk.h"
#include "xxfclib/formats/unreal_psa/xx_unreal_psa.h"
#include "xxfclib/formats/torque_dts/xx_torque_dts.h"
#include "xxfclib/formats/magicavoxel_vox/xx_magicavoxel_vox.h"
#include "xxfclib/formats/audio_au/xx_audio_au.h"
#include "xxfclib/formats/creative_voc/xx_creative_voc.h"
#include "xxfclib/formats/tracker_xm/xx_tracker_xm.h"
#include "xxfclib/formats/tracker_s3m/xx_tracker_s3m.h"
#include "xxfclib/formats/tracker_it/xx_tracker_it.h"
#include "xxfclib/formats/tracker_mtm/xx_tracker_mtm.h"
#include "xxfclib/formats/tracker_stm/xx_tracker_stm.h"
#include "xxfclib/formats/tracker_669/xx_tracker_669.h"
#include "xxfclib/formats/tracker_ult/xx_tracker_ult.h"
#include "xxfclib/formats/tracker_okt/xx_tracker_okt.h"
#include "xxfclib/formats/nifti2/xx_nifti2.h"
#include "xxfclib/formats/lidar_las/xx_lidar_las.h"
#include "xxfclib/formats/esri_shp/xx_esri_shp.h"
#include "xxfclib/formats/polygon_ply/xx_polygon_ply.h"
#include "xxfclib/formats/pointcloud_pcd/xx_pointcloud_pcd.h"
#include "xxfclib/formats/matlab_mat4/xx_matlab_mat4.h"
#include "xxfclib/formats/seismic_segy/xx_seismic_segy.h"
#include "xxfclib/formats/biomedical_bdf/xx_biomedical_bdf.h"
#include "xxfclib/formats/erlang_beam/xx_erlang_beam.h"
#include "xxfclib/formats/java_jmod/xx_java_jmod.h"
#include "xxfclib/formats/sfx_arcv2/xx_sfx_arcv2.h"
#include "xxfclib/formats/sfx_chz/xx_sfx_chz.h"
#include "xxfclib/formats/sfx_szdd/xx_sfx_szdd.h"
#include "xxfclib/formats/sfx_mpq/xx_sfx_mpq.h"
#include "xxfclib/formats/sfx_swag/xx_sfx_swag.h"
#include "xxfclib/formats/sfx_zpak/xx_sfx_zpak.h"
#include "xxfclib/formats/sfx_diskexpress/xx_sfx_diskexpress.h"
#include "xxfclib/formats/sfx_bzip2/xx_sfx_bzip2.h"
#include "xxfclib/formats/sfx_gzip/xx_sfx_gzip.h"
#include "xxfclib/formats/sfx_tar/xx_sfx_tar.h"
#include "xxfclib/formats/sfx_cab/xx_sfx_cab.h"
#include "xxfclib/formats/pmarc_sfx/xx_pmarc_sfx.h"
#include "xxfclib/formats/sfx_7zip/xx_sfx_7zip.h"
#include "xxfclib/formats/sfx_ace/xx_sfx_ace.h"
#include "xxfclib/formats/sfx_zipcentral/xx_sfx_zipcentral.h"
#include "xxfclib/formats/sony_psx_exe/xx_sony_psx_exe.h"
#include "xxfclib/formats/sony_psf/xx_sony_psf.h"
#include "xxfclib/formats/xbox_xdvdfs/xx_xbox_xdvdfs.h"
#include "xxfclib/formats/nintendo_wbfs/xx_nintendo_wbfs.h"
#include "xxfclib/formats/godot_ctex/xx_godot_ctex.h"
#include "xxfclib/formats/unity_serialized/xx_unity_serialized.h"
#include "xxfclib/formats/idtech_mdl/xx_idtech_mdl.h"
#include "xxfclib/formats/valve_studio_mdl/xx_valve_studio_mdl.h"
#include "xxfclib/formats/blitz3d_b3d/xx_blitz3d_b3d.h"
#include "xxfclib/formats/milkshape_ms3d/xx_milkshape_ms3d.h"
#include "xxfclib/formats/nintendo_bch/xx_nintendo_bch.h"
#include "xxfclib/formats/nintendo_cgfx/xx_nintendo_cgfx.h"
#include "xxfclib/formats/nintendo_byaml/xx_nintendo_byaml.h"
#include "xxfclib/formats/relic_chunky/xx_relic_chunky.h"
#include "xxfclib/formats/ogre_mesh/xx_ogre_mesh.h"
#include "xxfclib/formats/adobe_ase/xx_adobe_ase.h"
#include "xxfclib/formats/adobe_aco/xx_adobe_aco.h"
#include "xxfclib/formats/gimp_gbr/xx_gimp_gbr.h"
#include "xxfclib/formats/gimp_gih/xx_gimp_gih.h"
#include "xxfclib/formats/gimp_pat/xx_gimp_pat.h"
#include "xxfclib/formats/jbig2/xx_jbig2.h"
#include "xxfclib/formats/djvu/xx_djvu.h"
#include "xxfclib/formats/emf/xx_emf.h"
#include "xxfclib/formats/wmf/xx_wmf.h"
#include "xxfclib/formats/xfig/xx_xfig.h"
#include "xxfclib/formats/nifti1/xx_nifti1.h"
#include "xxfclib/formats/nrrd/xx_nrrd.h"
#include "xxfclib/formats/mrc/xx_mrc.h"
#include "xxfclib/formats/metaimage/xx_metaimage.h"
#include "xxfclib/formats/vtk_legacy/xx_vtk_legacy.h"
#include "xxfclib/formats/gipl/xx_gipl.h"
#include "xxfclib/formats/freesurfer_mgh/xx_freesurfer_mgh.h"
#include "xxfclib/formats/edf/xx_edf.h"
#include "xxfclib/formats/fcs/xx_fcs.h"
#include "xxfclib/formats/tensorflow_tfrecord/xx_tensorflow_tfrecord.h"
#include "xxfclib/formats/sfx_arc/xx_sfx_arc.h"
#include "xxfclib/formats/sfx_arj/xx_sfx_arj.h"
#include "xxfclib/formats/sfx_bsn/xx_sfx_bsn.h"
#include "xxfclib/formats/sfx_arq/xx_sfx_arq.h"
#include "xxfclib/formats/sfx_gxl/xx_sfx_gxl.h"
#include "xxfclib/formats/sfx_asymetrix/xx_sfx_asymetrix.h"
#include "xxfclib/formats/sfx_rta/xx_sfx_rta.h"
#include "xxfclib/formats/sfx_rtpatch/xx_sfx_rtpatch.h"
#include "xxfclib/formats/esp_archive/xx_esp_archive.h"
#include "xxfclib/formats/sfx_kwaj/xx_sfx_kwaj.h"
#include "xxfclib/formats/gemdos_lha/xx_gemdos_lha.h"
#include "xxfclib/formats/winimage_zip/xx_winimage_zip.h"
#include "xxfclib/formats/hp3000_wrq/xx_hp3000_wrq.h"
#include "xxfclib/formats/icu_data_package/xx_icu_data_package.h"
#include "xxfclib/formats/sfx_sqz/xx_sfx_sqz.h"
#include "xxfclib/formats/nintendo_bfstm/xx_nintendo_bfstm.h"
#include "xxfclib/formats/nintendo_bfwav/xx_nintendo_bfwav.h"
#include "xxfclib/formats/nintendo_bcwav/xx_nintendo_bcwav.h"
#include "xxfclib/formats/nintendo_bfres/xx_nintendo_bfres.h"
#include "xxfclib/formats/nintendo_bflyt/xx_nintendo_bflyt.h"
#include "xxfclib/formats/nintendo_bclyt/xx_nintendo_bclyt.h"
#include "xxfclib/formats/nintendo_bfnt/xx_nintendo_bfnt.h"
#include "xxfclib/formats/nintendo_bcfnt/xx_nintendo_bcfnt.h"
#include "xxfclib/formats/nintendo_3dsx/xx_nintendo_3dsx.h"
#include "xxfclib/formats/sony_tim2/xx_sony_tim2.h"
#include "xxfclib/formats/sony_pamf/xx_sony_pamf.h"
#include "xxfclib/formats/sega_gvr/xx_sega_gvr.h"
#include "xxfclib/formats/microsoft_xwb/xx_microsoft_xwb.h"
#include "xxfclib/formats/microsoft_xsb/xx_microsoft_xsb.h"
#include "xxfclib/formats/relic_sga/xx_relic_sga.h"
#include "xxfclib/formats/xpm/xx_xpm.h"
#include "xxfclib/formats/pcx/xx_pcx.h"
#include "xxfclib/formats/iff_ilbm/xx_iff_ilbm.h"
#include "xxfclib/formats/utah_rle/xx_utah_rle.h"
#include "xxfclib/formats/radiance_hdr/xx_radiance_hdr.h"
#include "xxfclib/formats/dpx/xx_dpx.h"
#include "xxfclib/formats/cineon/xx_cineon.h"
#include "xxfclib/formats/xwd/xx_xwd.h"
#include "xxfclib/formats/sgi_rgb/xx_sgi_rgb.h"
#include "xxfclib/formats/aseprite/xx_aseprite.h"
#include "xxfclib/formats/numpy_npy/xx_numpy_npy.h"
#include "xxfclib/formats/matlab_mat5/xx_matlab_mat5.h"
#include "xxfclib/formats/netcdf_classic/xx_netcdf_classic.h"
#include "xxfclib/formats/hdf4/xx_hdf4.h"
#include "xxfclib/formats/dbase_dbf/xx_dbase_dbf.h"
#include "xxfclib/formats/sas_xport/xx_sas_xport.h"
#include "xxfclib/formats/spss_sav/xx_spss_sav.h"
#include "xxfclib/formats/stata_dta/xx_stata_dta.h"
#include "xxfclib/formats/apache_arrow_file/xx_apache_arrow_file.h"
#include "xxfclib/formats/apache_parquet/xx_apache_parquet.h"
#include "xxfclib/formats/makeself/xx_makeself.h"
#include "xxfclib/formats/sun_java_binsh/xx_sun_java_binsh.h"
#include "xxfclib/formats/installanywhere_unix/xx_installanywhere_unix.h"
#include "xxfclib/formats/sfx_packagefortheweb/xx_sfx_packagefortheweb.h"
#include "xxfclib/formats/sfx_spis/xx_sfx_spis.h"
#include "xxfclib/formats/sfx_lha/xx_sfx_lha.h"
#include "xxfclib/formats/lmd_container/xx_lmd_container.h"
#include "xxfclib/formats/totalannihilation_hpi/xx_totalannihilation_hpi.h"
#include "xxfclib/formats/ravensoft_rff/xx_ravensoft_rff.h"
#include "xxfclib/formats/terminalreality_pod/xx_terminalreality_pod.h"
#include "xxfclib/formats/volition_vpp/xx_volition_vpp.h"
#include "xxfclib/formats/kirikiri_xp3/xx_kirikiri_xp3.h"
#include "xxfclib/formats/fromsoftware_binder/xx_fromsoftware_binder.h"
#include "xxfclib/formats/mythic_myp/xx_mythic_myp.h"
#include "xxfclib/formats/lithtech_rez/xx_lithtech_rez.h"
#include "xxfclib/formats/nintendo_ncch/xx_nintendo_ncch.h"
#include "xxfclib/formats/nintendo_ncsd/xx_nintendo_ncsd.h"
#include "xxfclib/formats/nintendo_cia/xx_nintendo_cia.h"
#include "xxfclib/formats/nintendo_nds/xx_nintendo_nds.h"
#include "xxfclib/formats/nintendo_gcm/xx_nintendo_gcm.h"
#include "xxfclib/formats/nintendo_tpl/xx_nintendo_tpl.h"
#include "xxfclib/formats/sony_tim/xx_sony_tim.h"
#include "xxfclib/formats/sony_vag/xx_sony_vag.h"
#include "xxfclib/formats/larian_lspk/xx_larian_lspk.h"
#include "xxfclib/formats/larian_lsf/xx_larian_lsf.h"
#include "xxfclib/formats/valve_hpak/xx_valve_hpak.h"
#include "xxfclib/formats/renpy_rpa/xx_renpy_rpa.h"
#include "xxfclib/formats/unreal_package/xx_unreal_package.h"
#include "xxfclib/formats/sega_pvr2/xx_sega_pvr2.h"
#include "xxfclib/formats/nintendo_bntx/xx_nintendo_bntx.h"
#include "xxfclib/formats/icns/xx_icns.h"
#include "xxfclib/formats/xcursor/xx_xcursor.h"
#include "xxfclib/formats/icc/xx_icc.h"
#include "xxfclib/formats/qoi/xx_qoi.h"
#include "xxfclib/formats/farbfeld/xx_farbfeld.h"
#include "xxfclib/formats/pnm/xx_pnm.h"
#include "xxfclib/formats/tga/xx_tga.h"
#include "xxfclib/formats/sun_raster/xx_sun_raster.h"
#include "xxfclib/formats/fits/xx_fits.h"
#include "xxfclib/formats/dicom/xx_dicom.h"
#include "xxfclib/formats/pcap/xx_pcap.h"
#include "xxfclib/formats/btsnoop/xx_btsnoop.h"
#include "xxfclib/formats/java_class/xx_java_class.h"
#include "xxfclib/formats/sfnt_collection/xx_sfnt_collection.h"
#include "xxfclib/formats/sqlite3/xx_sqlite3.h"
#include "xxfclib/formats/sqlite_wal/xx_sqlite_wal.h"
#include "xxfclib/formats/avro_object/xx_avro_object.h"
#include "xxfclib/formats/glb/xx_glb.h"
#include "xxfclib/formats/spirv/xx_spirv.h"
#include "xxfclib/formats/crx/xx_crx.h"
#include "xxfclib/formats/bethesda_bsa/xx_bethesda_bsa.h"
#include "xxfclib/formats/bethesda_ba2/xx_bethesda_ba2.h"
#include "xxfclib/formats/unityfs/xx_unityfs.h"
#include "xxfclib/formats/bioware_biff/xx_bioware_biff.h"
#include "xxfclib/formats/bioware_erf/xx_bioware_erf.h"
#include "xxfclib/formats/bioware_rim/xx_bioware_rim.h"
#include "xxfclib/formats/lucas_lab/xx_lucas_lab.h"
#include "xxfclib/formats/lucas_bun/xx_lucas_bun.h"
#include "xxfclib/formats/idtech_bsp/xx_idtech_bsp.h"
#include "xxfclib/formats/valve_bsp/xx_valve_bsp.h"
#include "xxfclib/formats/idtech_md2/xx_idtech_md2.h"
#include "xxfclib/formats/idtech_md3/xx_idtech_md3.h"
#include "xxfclib/formats/idtech_qvm/xx_idtech_qvm.h"
#include "xxfclib/formats/mohawk_mhk/xx_mohawk_mhk.h"
#include "xxfclib/formats/quake_sprite/xx_quake_sprite.h"
#include "xxfclib/formats/nintendo_narc/xx_nintendo_narc.h"
#include "xxfclib/formats/nintendo_sarc/xx_nintendo_sarc.h"
#include "xxfclib/formats/nintendo_pfs0/xx_nintendo_pfs0.h"
#include "xxfclib/formats/nintendo_hfs0/xx_nintendo_hfs0.h"
#include "xxfclib/formats/nintendo_brres/xx_nintendo_brres.h"
#include "xxfclib/formats/nintendo_bcstm/xx_nintendo_bcstm.h"
#include "xxfclib/formats/nintendo_bfsar/xx_nintendo_bfsar.h"
#include "xxfclib/formats/nintendo_bcsar/xx_nintendo_bcsar.h"
#include "xxfclib/formats/sony_psarc/xx_sony_psarc.h"
#include "xxfclib/formats/ktx/xx_ktx.h"
#include "xxfclib/formats/ktx2/xx_ktx2.h"
#include "xxfclib/formats/dds/xx_dds.h"
#include "xxfclib/formats/pvr/xx_pvr.h"
#include "xxfclib/formats/valve_vtf/xx_valve_vtf.h"
#include "xxfclib/formats/xbox_xbe/xx_xbox_xbe.h"
#include "xxfclib/formats/flac/xx_flac.h"
#include "xxfclib/formats/ogg/xx_ogg.h"
#include "xxfclib/formats/mp4/xx_mp4.h"
#include "xxfclib/formats/matroska/xx_matroska.h"
#include "xxfclib/formats/aiff/xx_aiff.h"
#include "xxfclib/formats/caf/xx_caf.h"
#include "xxfclib/formats/photoshop_psd/xx_photoshop_psd.h"
#include "xxfclib/formats/tiff/xx_tiff.h"
#include "xxfclib/formats/openexr/xx_openexr.h"
#include "xxfclib/formats/jpeg2000_jp2/xx_jpeg2000_jp2.h"
#include "xxfclib/formats/android_vendor_boot/xx_android_vendor_boot.h"
#include "xxfclib/formats/android_dtbo/xx_android_dtbo.h"
#include "xxfclib/formats/android_vbmeta/xx_android_vbmeta.h"
#include "xxfclib/formats/espressif_image/xx_espressif_image.h"
#include "xxfclib/formats/wasm/xx_wasm.h"
#include "xxfclib/formats/llvm_bitcode_wrapper/xx_llvm_bitcode_wrapper.h"
#include "xxfclib/formats/dotnet_metadata/xx_dotnet_metadata.h"
#include "xxfclib/formats/sfnt/xx_sfnt.h"
#include "xxfclib/formats/woff/xx_woff.h"
#include "xxfclib/formats/woff2/xx_woff2.h"
#include "xxfclib/formats/act_apricot_pc_xi_raw/xx_act_apricot_pc_xi_raw.h"
#include "xxfclib/formats/adam/xx_adam.h"
#include "xxfclib/formats/base16/xx_base16.h"
#include "xxfclib/formats/bondwell_2_disk/xx_bondwell_2_disk.h"
#include "xxfclib/formats/casio_fz_1_disk/xx_casio_fz_1_disk.h"
#include "xxfclib/formats/isz/xx_isz.h"
#include "xxfclib/formats/mame_floppy_image_mfi/xx_mame_floppy_image_mfi.h"
#include "xxfclib/formats/parallels_hdd/xx_parallels_hdd.h"
#include "xxfclib/formats/pc_magazine_flp/xx_pc_magazine_flp.h"
#include "xxfclib/formats/pchrom/xx_pchrom.h"
#include "xxfclib/formats/pem/xx_pem.h"
#include "xxfclib/formats/prodos/xx_prodos.h"
#include "xxfclib/formats/qemu_enhanced_disk/xx_qemu_enhanced_disk.h"
#include "xxfclib/formats/rawcd/xx_rawcd.h"
#include "xxfclib/formats/rsdos_fs/xx_rsdos_fs.h"
#include "xxfclib/formats/sar_ns/xx_sar_ns.h"
#include "xxfclib/formats/swf/xx_swf.h"
#include "xxfclib/formats/t64/xx_t64.h"
#include "xxfclib/formats/uue/xx_uue.h"
#include "xxfclib/formats/vdi/xx_vdi.h"
#include "xxfclib/formats/bmp/xx_bmp.h"
#include "xxfclib/formats/cfe/xx_cfe.h"
#include "xxfclib/formats/dxbc/xx_dxbc.h"
#include "xxfclib/formats/gif/xx_gif.h"
#include "xxfclib/formats/jpeg/xx_jpeg.h"
#include "xxfclib/formats/linuxarm64/xx_linuxarm64.h"
#include "xxfclib/formats/linuxboot/xx_linuxboot.h"
#include "xxfclib/formats/linuxzimage/xx_linuxzimage.h"
#include "xxfclib/formats/pcapng/xx_pcapng.h"
#include "xxfclib/formats/pjl/xx_pjl.h"
#include "xxfclib/formats/png/xx_png.h"
#include "xxfclib/formats/riff/xx_riff.h"
#include "xxfclib/formats/svg/xx_svg.h"
#include "xxfclib/formats/quake_pak/xx_quake_pak.h"
#include "xxfclib/formats/doom_wad/xx_doom_wad.h"
#include "xxfclib/formats/quake_wad2/xx_quake_wad2.h"
#include "xxfclib/formats/halflife_wad3/xx_halflife_wad3.h"
#include "xxfclib/formats/build_grp/xx_build_grp.h"
#include "xxfclib/formats/cri_afs/xx_cri_afs.h"
#include "xxfclib/formats/cri_awb/xx_cri_awb.h"
#include "xxfclib/formats/valve_vpk/xx_valve_vpk.h"
#include "xxfclib/formats/nintendo_u8/xx_nintendo_u8.h"
#include "xxfclib/formats/nintendo_rarc/xx_nintendo_rarc.h"
#include "xxfclib/formats/android_ab/xx_android_ab.h"
#include "xxfclib/formats/nes_rom/xx_nes_rom.h"
#include "xxfclib/formats/lynx_lnx/xx_lynx_lnx.h"
#include "xxfclib/formats/commodore_crt/xx_commodore_crt.h"
#include "xxfclib/formats/uf2/xx_uf2.h"
#include "xxfclib/formats/ico/xx_ico.h"
#include "xxfclib/formats/midi/xx_midi.h"
#include "xxfclib/formats/advanced_installer_bootstrapper/xx_advanced_installer_bootstrapper.h"
#include "xxfclib/formats/ardi_installer/xx_ardi_installer.h"
#include "xxfclib/formats/arni_installer_container/xx_arni_installer_container.h"
#include "xxfclib/formats/ej_technologies_install/xx_ej_technologies_install.h"
#include "xxfclib/formats/finstall/xx_finstall.h"
#include "xxfclib/formats/ghost_installer/xx_ghost_installer.h"
#include "xxfclib/formats/ibm_zpak_installer/xx_ibm_zpak_installer.h"
#include "xxfclib/formats/ifah_installer/xx_ifah_installer.h"
#include "xxfclib/formats/inno_setup/xx_inno_setup.h"
#include "xxfclib/formats/installer_vise_windows/xx_installer_vise_windows.h"
#include "xxfclib/formats/installshield_12_setup/xx_installshield_12_setup.h"
#include "xxfclib/formats/installshield_3/xx_installshield_3.h"
#include "xxfclib/formats/installshield_7_setup/xx_installshield_7_setup.h"
#include "xxfclib/formats/installshield_7_setup2/xx_installshield_7_setup2.h"
#include "xxfclib/formats/installshield_developer/xx_installshield_developer.h"
#include "xxfclib/formats/installshield_issetupstream/xx_installshield_issetupstream.h"
#include "xxfclib/formats/installshield_multiplatform/xx_installshield_multiplatform.h"
#include "xxfclib/formats/installshield_skin/xx_installshield_skin.h"
#include "xxfclib/formats/microfox_put/xx_microfox_put.h"
#include "xxfclib/formats/o_setup/xx_o_setup.h"
#include "xxfclib/formats/pc_install_setup/xx_pc_install_setup.h"
#include "xxfclib/formats/pyinstaller_one_executable/xx_pyinstaller_one_executable.h"
#include "xxfclib/formats/qsetup_installation_suite/xx_qsetup_installation_suite.h"
#include "xxfclib/formats/rtpatch_setup_data/xx_rtpatch_setup_data.h"
#include "xxfclib/formats/setup_factory/xx_setup_factory.h"
#include "xxfclib/formats/sfx_ebook_compiler_executables/xx_sfx_ebook_compiler_executables.h"
#include "xxfclib/formats/spoon_installer/xx_spoon_installer.h"
#include "xxfclib/formats/tarma_installer/xx_tarma_installer.h"
#include "xxfclib/formats/adf/xx_adf.h"
#include "xxfclib/formats/apm/xx_apm.h"
#include "xxfclib/formats/vhdx/xx_vhdx.h"
#include "xxfclib/formats/base64/xx_base64.h"
#include "xxfclib/formats/btoa/xx_btoa.h"
#include "xxfclib/formats/chd/xx_chd.h"
#include "xxfclib/formats/chm/xx_chm.h"
#include "xxfclib/formats/cloop/xx_cloop.h"
#include "xxfclib/formats/cue/xx_cue.h"
#include "xxfclib/formats/dahuazip/xx_dahuazip.h"
#include "xxfclib/formats/dmsfw/xx_dmsfw.h"
#include "xxfclib/formats/ewf/xx_ewf.h"
#include "xxfclib/formats/godot_engine_pck/xx_godot_engine_pck.h"
#include "xxfclib/formats/gpgsigned/xx_gpgsigned.h"
#include "xxfclib/formats/ihex/xx_ihex.h"
#include "xxfclib/formats/kwaj/xx_kwaj.h"
#include "xxfclib/formats/lbr/xx_lbr.h"
#include "xxfclib/formats/lzfsestream/xx_lzfsestream.h"
#include "xxfclib/formats/nrg/xx_nrg.h"
#include "xxfclib/formats/packit_mac/xx_packit_mac.h"
#include "xxfclib/formats/rpm/xx_rpm.h"
#include "xxfclib/formats/stuffit5/xx_stuffit5.h"
#include "xxfclib/formats/msdos/xx_msdos.h"
#include "xxfclib/formats/sfx_sbx_extractor/xx_sfx_sbx_extractor.h"
#include "xxfclib/formats/com/xx_com.h"
#include "xxfclib/formats/dos16m/xx_dos16m.h"
#include "xxfclib/formats/atarist/xx_atarist.h"
#include "xxfclib/formats/amigahunk/xx_amigahunk.h"
#include "xxfclib/formats/pe/xx_pe.h"
#include "xxfclib/formats/dotnet/xx_dotnet.h"
#include "xxfclib/formats/elf/xx_elf.h"
#include "xxfclib/formats/macho/xx_macho.h"
#include "xxfclib/formats/ne/xx_ne.h"
#include "xxfclib/formats/le/xx_le.h"
#include "xxfclib/formats/lx/xx_lx.h"
#include "xxfclib/formats/dex/xx_dex.h"
#endif
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef XXFC_FORMAT_DETECTION_LZMA_XZ_ONLY
static bool xx_format_tar_header_is_valid(const uint8_t header[512]) {
    uint64_t stored = 0;
    uint64_t checksum = 0;
    int64_t signed_checksum = 0;
    bool saw_digit = false;

    if (!header || header[0] == 0) {
        return false;
    }

    for (size_t i = 0; i < 8; ++i) {
        uint8_t c = header[148 + i];
        if (c == 0 || c == ' ') {
            if (saw_digit) {
                break;
            }
            continue;
        }
        if (c < '0' || c > '7') {
            return false;
        }
        saw_digit = true;
        stored = (stored << 3) | (uint64_t)(c - '0');
    }
    if (!saw_digit) {
        return false;
    }

    for (size_t i = 0; i < 512; ++i) {
        if (i >= 148 && i < 156) {
            checksum += 0x20U;
            signed_checksum += 0x20;
        } else {
            checksum += header[i];
            signed_checksum += (int8_t)header[i];
        }
    }
    return checksum == stored ||
           (signed_checksum >= 0 && (uint64_t)signed_checksum == stored);
}

typedef struct xx_format_prefix_sink_s {
    uint8_t data[512];
    size_t size;
} xx_format_prefix_sink;

static ssize_t xx_format_prefix_write(xx_io_device *device,
                                      const void *data, size_t size) {
    xx_format_prefix_sink *sink =
        device ? (xx_format_prefix_sink *)device->priv : NULL;
    size_t available;
    size_t amount;
    if (!sink || (!data && size != 0U)) return -1;
    available = sizeof(sink->data) - sink->size;
    amount = size < available ? size : available;
    if (amount != 0U) {
        xx_mem_copy(sink->data + sink->size, data, amount);
        sink->size += amount;
    }
    return amount == size ? (ssize_t)size : -1;
}

/* Keeps each reader probe in its own stack frame (see the probes used
 * inline by the detector below). */
#if defined(_MSC_VER)
#define XX_FORMAT_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define XX_FORMAT_NOINLINE __attribute__((noinline))
#else
#define XX_FORMAT_NOINLINE
#endif

#include "xx_format_reader_probe.h"

static bool xx_format_is_tar_gz_device(xx_io_device *device) {
    xx_format_prefix_sink prefix;
    xx_io_device sink;
    xx_gz gz;
    bool parsed;
    if (!device) return false;
    xx_mem_zero(&prefix, sizeof(prefix));
    xx_mem_zero(&sink, sizeof(sink));
    sink.write = xx_format_prefix_write;
    sink.priv = &prefix;
    xx_gz_init(&gz, device, 0);
    parsed = xx_gz_handle_base_info(&gz.format, NULL);
    if (parsed) {
        (void)xx_gz_unpack_to_device(&gz, &sink, NULL);
    }
    xx_gz_destroy(&gz);
    return prefix.size == sizeof(prefix.data) &&
           xx_format_tar_header_is_valid(prefix.data);
}

static bool xx_format_is_tar_bz2_device(xx_io_device *device) {
    xx_format_prefix_sink prefix;
    xx_io_device sink;
    xx_bz2 bz2;
    bool parsed;
    if (!device) return false;
    xx_mem_zero(&prefix, sizeof(prefix));
    xx_mem_zero(&sink, sizeof(sink));
    sink.write = xx_format_prefix_write;
    sink.priv = &prefix;
    xx_bz2_init(&bz2, device, 0);
    parsed = xx_bz2_handle_base_info(&bz2.format, NULL);
    if (parsed) {
        (void)xx_bz2_unpack_to_device(&bz2, &sink, NULL);
    }
    xx_bz2_destroy(&bz2);
    return prefix.size == sizeof(prefix.data) &&
           xx_format_tar_header_is_valid(prefix.data);
}

static bool xx_format_is_tar_xz_device(xx_io_device *device) {
    xx_format_prefix_sink prefix;
    xx_io_device sink;
    xx_xz xz;
    bool parsed;
    if (!device) return false;
    xx_mem_zero(&prefix, sizeof(prefix));
    xx_mem_zero(&sink, sizeof(sink));
    sink.write = xx_format_prefix_write;
    sink.priv = &prefix;
    xx_xz_init(&xz, device, 0);
    parsed = xx_xz_handle_base_info(&xz.format, NULL) &&
             xx_xz_can_extract(&xz);
    if (parsed) {
        (void)xx_xz_unpack_to_device(&xz, &sink, NULL);
    }
    xx_xz_destroy(&xz);
    return prefix.size == sizeof(prefix.data) &&
           xx_format_tar_header_is_valid(prefix.data);
}

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tar_lz4_device,
    xx_tar_lz4, device, tar_lz4, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lz4_device,
    xx_lz4, device, lz4, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lz5_device,
    xx_lz5, device, lz5, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lizard_device,
    xx_lizard, device, lizard, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

/* Raw Brotli streams intentionally have no universal signature.  Only the
 * independent-frame wrapper emitted by the 7-Zip Brotli codec is safe to
 * recognise automatically; raw streams remain available through xx_brotli. */
XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_brotli_device,
    xx_brotli, device, brotli, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_unixpack_device,
    xx_unixpack, device, unixpack, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zlib_device,
    xx_zlib, device, zlib, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_unixcompress_device,
    xx_unixcompress, device, unixcompress, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_gitobject_device,
    xx_gitobject, device, gitobject, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mscompress_device,
    xx_mscompress, device, mscompress, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ash0_device,
    xx_ash0, device, ash0, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_wiilz77_device,
    xx_wiilz77, device, wiilz77, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lzv1_device,
    xx_lzv1, device, lzv1, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_oraclesqueeze_device,
    xx_oraclesqueeze, device, oraclesqueeze, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_softronics_device,
    xx_softronics, device, softronics, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

static bool xx_format_is_logitechcompress_device(xx_io_device *device) {
    xx_logitechcompress logitechcompress;
    bool result;
    if (!device) return false;
    xx_logitechcompress_init(&logitechcompress, device, 0);
    result = xx_logitechcompress_handle_base_info(&logitechcompress.format,
                                                   NULL);
    xx_logitechcompress_destroy(&logitechcompress);
    return result;
}

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dmapacked_device,
    xx_dmapacked, device, dmapacked, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_gashuff_device,
    xx_gashuff, device, gashuff, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_huf_device,
    xx_huf, device, huf, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lzdiet_device,
    xx_lzdiet, device, lzdiet, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_bcm_device,
    xx_bcm, device, bcm, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lpaq8_device,
    xx_lpaq8, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pea_device,
    xx_pea, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zpaq_device,
    xx_zpaq, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_freearc_device,
    xx_freearc, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ap4_device,
    xx_ap4, device, value, result, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_arq_device,
    xx_arq, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_asar_device,
    xx_asar, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

/*
 * Ascend has no magic: its first twelve bytes are a date. A full decode is
 * needed to be sure, so this cheap bounds check keeps that off the hot path --
 * the date fields plus the two DCL header bytes reject almost everything
 * before anything is read into memory.
 */
static bool xx_format_ascend_prefilter(const uint8_t *magic,
                                       size_t magic_size) {
    uint16_t year, month, day, hour, minute, second;

    if (magic_size < 14U) return false;
    year = (uint16_t)(magic[0] | (magic[1] << 8));
    month = (uint16_t)(magic[2] | (magic[3] << 8));
    day = (uint16_t)(magic[4] | (magic[5] << 8));
    hour = (uint16_t)(magic[6] | (magic[7] << 8));
    minute = (uint16_t)(magic[8] | (magic[9] << 8));
    second = (uint16_t)(magic[10] | (magic[11] << 8));
    return year >= 1980U && year <= 2100U && month >= 1U && month <= 12U &&
           day >= 1U && day <= 31U && hour <= 23U && minute <= 59U &&
           second <= 59U &&
           magic[12] <= 1U && magic[13] >= 4U && magic[13] <= 6U;
}

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ascend_device,
    xx_ascend, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_bigf_device,
    xx_bigf, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_marc_device,
    xx_marc, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zfsf_device,
    xx_zfsf, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_packit_device,
    xx_packit, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tws_device,
    xx_tws, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_bigaf_device,
    xx_bigaf, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_cru_device,
    xx_cru, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_frontpagetheme_device,
    xx_frontpagetheme, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_hlb_device,
    xx_hlb, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_irixsa_device,
    xx_irixsa, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_jam_device,
    xx_jam, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_krml_device,
    xx_krml, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lbrcobol_device,
    xx_lbrcobol, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_minidump_device,
    xx_minidump, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_powerboardbbs_device,
    xx_powerboardbbs, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sci_device,
    xx_sci, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_seadata_device,
    xx_seadata, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_secondnature_device,
    xx_secondnature, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sos_device,
    xx_sos, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sw_device,
    xx_sw, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_swagpacket_device,
    xx_swagpacket, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_trcpak_device,
    xx_trcpak, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_cfl_device,
    xx_cfl, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dpk_device,
    xx_dpk, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dsl2_device,
    xx_dsl2, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dtpacked_device,
    xx_dtpacked, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_fiz_device,
    xx_fiz, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_fld_device,
    xx_fld, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ibmzpak_device,
    xx_ibmzpak, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_igf1_device,
    xx_igf1, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_igf2_device,
    xx_igf2, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_inteduft_device,
    xx_inteduft, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_jm93_device,
    xx_jm93, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lsz_device,
    xx_lsz, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_miz_device,
    xx_miz, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mva_device,
    xx_mva, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_povlablzh_device,
    xx_povlablzh, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_powerarc_device,
    xx_powerarc, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_qip1_device,
    xx_qip1, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_quarterdeckqp_device,
    xx_quarterdeckqp, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_rcf_device,
    xx_rcf, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_riversoft_device,
    xx_riversoft, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_swag_device,
    xx_swag, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tgcf_device,
    xx_tgcf, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_trc_device,
    xx_trc, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zlwb_device,
    xx_zlwb, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zz_device,
    xx_zz, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zzz_device,
    xx_zzz, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_jgpak_device,
    xx_jgpak, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_borlandpack_device,
    xx_borlandpack, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ecmpacked_device,
    xx_ecmpacked, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_jetbbs_device,
    xx_jetbbs, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_qualitas_device,
    xx_qualitas, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_bwf_device,
    xx_bwf, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zap_device,
    xx_zap, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_stork_device,
    xx_stork, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ascendbackup_device,
    xx_ascendbackup, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pyz_device,
    xx_pyz, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_fmc1_device,
    xx_fmc1, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_xar_device,
    xx_xar, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lha_device,
    xx_lha, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_spis_device,
    xx_spis, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_amigalzx_device,
    xx_amigalzx, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_seaarc_device,
    xx_seaarc, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_asymetrix_device,
    xx_asymetrix, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_bwcf_device,
    xx_bwcf, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_chieflz_device,
    xx_chieflz, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_chieflzmulti_device,
    xx_chieflzmulti, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_clp_device,
    xx_clp, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_cmp_device,
    xx_cmp, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_diskdoubler_device,
    xx_diskdoubler, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ea_device,
    xx_ea, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ealib_device,
    xx_ealib, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_earefpack_device,
    xx_earefpack, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_fls_device,
    xx_fls, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_genius_device,
    xx_genius, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ha_device,
    xx_ha, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_hzl_device,
    xx_hzl, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_kboom_device,
    xx_kboom, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lzhcxp_device,
    xx_lzhcxp, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lzwd_device,
    xx_lzwd, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mi10_device,
    xx_mi10, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_npack_device,
    xx_npack, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pakleo_device,
    xx_pakleo, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_scl_device,
    xx_scl, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zcmp_device,
    xx_zcmp, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zpak_device,
    xx_zpak, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_netwarepacked_device,
    xx_netwarepacked, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ztc_device,
    xx_ztc, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_glu_device,
    xx_glu, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_gtu_device,
    xx_gtu, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ibmspack_device,
    xx_ibmspack, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_jbf_device,
    xx_jbf, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pcommos2_device,
    xx_pcommos2, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_stk_device,
    xx_stk, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_terse_device,
    xx_terse, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zoo_device,
    xx_zoo, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sqx_device,
    xx_sqx, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_imp_device,
    xx_imp, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_compactpro_device,
    xx_compactpro, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_hap_device,
    xx_hap, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_irwinpac_device,
    xx_irwinpac, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ivt_device,
    xx_ivt, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_kolibrikpack_device,
    xx_kolibrikpack, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lim_device,
    xx_lim, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lofi_device,
    xx_lofi, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pkt_device,
    xx_pkt, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_qda_device,
    xx_qda, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_qnxbase_device,
    xx_qnxbase, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_rid_device,
    xx_rid, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_rompaq_device,
    xx_rompaq, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_rta_device,
    xx_rta, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_rtpatch_device,
    xx_rtpatch, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_stylus_device,
    xx_stylus, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ti99arc_device,
    xx_ti99arc, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tivoli_device,
    xx_tivoli, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_vmarc_device,
    xx_vmarc, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_wintersoft_device,
    xx_wintersoft, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_wpk_device,
    xx_wpk, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_xeditpack_device,
    xx_xeditpack, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zie_device,
    xx_zie, device, value, result, XX_FORMAT_ALLOW_NULL, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lzpis2_device,
    xx_lzpis2, device, lzpis2, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tar_zstd_device,
    xx_tar_zstd, device, tar_zstd, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zstd_device,
    xx_zstd, device, zstd, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tar_nextstep_device,
    xx_tar_nextstep, device, tar_nextstep, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tar_compress_device,
    xx_tar_compress, device, tar_compress, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tar_lzip_device,
    xx_tar_lzip, device, tar_lzip, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lzip_device,
    xx_lzip, device, lzip, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tar_lzma_device,
    xx_tar_lzma, device, tar_lzma, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lzma_device,
    xx_lzma, device, lzma, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tar_lzop_device,
    xx_tar_lzop, device, tar_lzop, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tarx1_device,
    xx_tarx1, device, tarx1, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tarx2_device,
    xx_tarx2, device, tarx2, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_squashfs_device,
    xx_squashfs, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ntfs_device,
    xx_ntfs, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_udf_device,
    xx_udf, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_romfs_device,
    xx_romfs, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sqz_device,
    xx_sqz, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_topspeed_device,
    xx_topspeed, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tps_device,
    xx_tps, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ulead_device,
    xx_ulead, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_quantum_device,
    xx_quantum, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zxzip_device,
    xx_zxzip, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_zoom_device,
    xx_zoom, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sfpack_device,
    xx_sfpack, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_claylz_device,
    xx_claylz, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_c64wraptor_device,
    xx_c64wraptor, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_corelltec_device,
    xx_corelltec, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pcsecure_device,
    xx_pcsecure, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_rsvk_device,
    xx_rsvk, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lzw15v_device,
    xx_lzw15v, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_saf_device,
    xx_saf, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sls_device,
    xx_sls, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_nid_device,
    xx_nid, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_gamos_device,
    xx_gamos, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_panorama_device,
    xx_panorama, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_fpak_device,
    xx_fpak, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_cramfs_device,
    xx_cramfs, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_jffs2_device,
    xx_jffs2, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

static XXFC_MAYBE_UNUSED bool xx_format_is_yaffs_device(xx_io_device *device) {
    xx_yaffs value;
    bool result;
    if (!device) return false;
    xx_yaffs_init(&value, device, 0);
    result = xx_yaffs_handle_base_info(&value.format, NULL);
    xx_yaffs_destroy(&value);
    return result;
}

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ubi_device,
    xx_ubi, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ubifs_device,
    xx_ubifs, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ext_device,
    xx_ext, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_fat_device,
    xx_fat, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

static bool xx_format_is_mbr_device(xx_io_device *device) {
    xx_mbr value;
    bool result;
    if (!device) return false;
    xx_mbr_init(&value, device, 0);
    result = xx_mbr_handle_base_info(&value.format, NULL);
    /* A protective MBR (a single 0xEE entry) is the cover page of a GPT
     * disk, not a partition table in its own right. Declining it here means
     * GPT still wins even if the dispatch order is later disturbed. */
    if (result && xx_mbr_is_protective(&value)) result = false;
    xx_mbr_destroy(&value);
    return result;
}

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_gpt_device,
    xx_gpt, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sparse_device,
    xx_sparse, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_uimage_device,
    xx_uimage, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dtb_device,
    xx_dtb, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_trx_device,
    xx_trx, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_seama_device,
    xx_seama, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_chk_device,
    xx_chk, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_packimg_device,
    xx_packimg, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dlob_device,
    xx_dlob, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_wince_device,
    xx_wince, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_binhdr_device,
    xx_binhdr, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_rtk_device,
    xx_rtk, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_csman_device,
    xx_csman, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

static XXFC_MAYBE_UNUSED bool xx_format_is_vxworks_device(xx_io_device *device) {
    xx_vxworks value;
    bool result;
    if (!device) return false;
    xx_vxworks_init(&value, device, 0);
    result = xx_vxworks_handle_base_info(&value.format, NULL);
    xx_vxworks_destroy(&value);
    return result;
}

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_uefi_fv_device,
    xx_uefi_fv, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_uefi_capsule_device,
    xx_uefi_capsule, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_qcow_device,
    xx_qcow, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_qnx6_device,
    xx_qnx6, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_luks_device,
    xx_luks, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_apfs_device,
    xx_apfs, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_btrfs_device,
    xx_btrfs, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_logfs_device,
    xx_logfs, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

/* DMS carries a four-byte magic, so the probe is only reached when those
 * bytes already matched; it confirms the rest of the header parses. */
/* A resource fork has no magic -- its header is four plausible offsets --
 * so nothing but the full structural walk can confirm one. Late dispatch,
 * and cheaper than the whole-stream decoders it runs before. */
XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_resourcefork_device,
    xx_resourcefork, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_applesingle_device,
    xx_applesingle, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_macbinary_device,
    xx_macbinary, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pp20_device,
    xx_pp20, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_beatthehouse_device,
    xx_beatthehouse, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_kpck_device,
    xx_kpck, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_battleisle_device,
    xx_battleisle, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_perform_device,
    xx_perform, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mathcad_device,
    xx_mathcad, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_netware2_device,
    xx_netware2, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_shar_device,
    xx_shar, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_rnc_device,
    xx_rnc, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ibmpack_device,
    xx_ibmpack, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_cazip_device,
    xx_cazip, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tpwm_device,
    xx_tpwm, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mrnz_device,
    xx_mrnz, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_edc_device,
    xx_edc, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mxs_device,
    xx_mxs, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

/* This reader wraps whichever container the mask was hiding, so its first
 * member is a union of the delegates rather than an Abstractformat of its
 * own -- the vtable lives one level in, at .container.format. */
static bool xx_format_is_xorarchive_device(xx_io_device *device) {
    xx_xorarchive value;
    bool result;
    if (!device) return false;
    xx_xorarchive_init(&value, device, 0);
    result = xx_xorarchive_check_is_valid(&value.container.format, NULL);
    xx_xorarchive_destroy(&value);
    return result;
}

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mwave_device,
    xx_mwave, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_finear_device,
    xx_finear, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_gst_device,
    xx_gst, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_winlink_device,
    xx_winlink, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ftcomp_device,
    xx_ftcomp, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_gpfpack_device,
    xx_gpfpack, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sco_device,
    xx_sco, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_unixcompact_device,
    xx_unixcompact, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_psdc_device,
    xx_psdc, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_is3_device,
    xx_is3, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_is5_device,
    xx_is5, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_is7inx_device,
    xx_is7inx, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_edilzss_device,
    xx_edilzss, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_savedskf_device,
    xx_savedskf, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_gob_device,
    xx_gob, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_debugscr_device,
    xx_debugscr, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dclft_device,
    xx_dclft, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_stuffit_device,
    xx_stuffit, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_binaryii_device,
    xx_binaryii, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_binhex_device,
    xx_binhex, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pma_device,
    xx_pma, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lzk00_device,
    xx_lzk00, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_compaqlzh_device,
    xx_compaqlzh, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_arcv_device,
    xx_arcv, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lifkd_device,
    xx_lifkd, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_trdos_device,
    xx_trdos, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_squeeze1_device,
    xx_squeeze1, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_izpack_device,
    xx_izpack, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_is11_device,
    xx_is11, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_gksetup_device,
    xx_gksetup, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pcinstall_device,
    xx_pcinstall, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_copyqm_device,
    xx_copyqm, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_teledisk_device,
    xx_teledisk, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_hfe_device,
    xx_hfe, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_fdi_device,
    xx_fdi, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_twoimg_device,
    xx_twoimg, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_imd_device,
    xx_imd, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_diskdupe_device,
    xx_diskdupe, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pmdiskcopy_device,
    xx_pmdiskcopy, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_diskjuggler_device,
    xx_diskjuggler, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_copyqmexe_device,
    xx_copyqmexe, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pax_device,
    xx_pax, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

static bool xx_format_is_solarispkg_device(xx_io_device *device) {
    xx_solarispkg value;
    bool result;
    if (!device) return false;
    xx_solarispkg_init(&value, device, 0);
    /* The package reader intentionally accepts a recoverable prefix.  For
     * this early detector priority, require that the parsed stream reaches
     * EOF so a broken package cannot mask a ZIP in its remaining bytes. */
    result = xx_solarispkg_handle_base_info(&value.format, NULL) &&
             value.format.format_size == xx_io_total_size(device);
    xx_solarispkg_destroy(&value);
    return result;
}

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_cpoint_device,
    xx_cpoint, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

static bool xx_format_is_elm_device(xx_io_device *device) {
    xx_elm value;
    bool result;

    xx_elm_init(&value, device, 0);
    result = xx_elm_is_css_trailer_variant(&value.format, NULL);
    xx_elm_destroy(&value);
    return result;
}

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_beospkg_device,
    xx_beospkg, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_vmspcsi_device,
    xx_vmspcsi, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_vmsdb_device,
    xx_vmsdb, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pcxlib_device,
    xx_pcxlib, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_hog2_device,
    xx_hog2, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sinner_device,
    xx_sinner, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_psn_device,
    xx_psn, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_grasp_device,
    xx_grasp, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_megatechvol_device,
    xx_megatechvol, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_stunts_device,
    xx_stunts, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_notetab_device,
    xx_notetab, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mcc_device,
    xx_mcc, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tnef_device,
    xx_tnef, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_opc_device,
    xx_opc, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_qrst_device,
    xx_qrst, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pain_device,
    xx_pain, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_xlas_device,
    xx_xlas, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mdcd_device,
    xx_mdcd, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ssm_device,
    xx_ssm, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_bvrp_device,
    xx_bvrp, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_bcw_device,
    xx_bcw, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_scf_device,
    xx_scf, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_recognita_device,
    xx_recognita, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_jasc_device,
    xx_jasc, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_binder_device,
    xx_binder, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_csidos_device,
    xx_csidos, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_cat_device,
    xx_cat, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_bnd_device,
    xx_bnd, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_smsipak_device,
    xx_smsipak, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_cpx_device,
    xx_cpx, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_diskexpress_device,
    xx_diskexpress, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_red_device,
    xx_red, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_shrinkwrap_device,
    xx_shrinkwrap, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_gxl_device,
    xx_gxl, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_aiaff_device,
    xx_aiaff, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_softpaq2_device,
    xx_softpaq2, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_wim_device,
    xx_wim, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_vhddynamic_device,
    xx_vhddynamic, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_vmdk_device,
    xx_vmdk, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ciso_device,
    xx_ciso, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_copydisk_device,
    xx_copydisk, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_hdcopy_device,
    xx_hdcopy, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_apricot_device,
    xx_apricot, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sabdu_device,
    xx_sabdu, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mpq_device,
    xx_mpq, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_phar_device,
    xx_phar, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sq_device,
    xx_sq, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_squeeze2_device,
    xx_squeeze2, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dbz_device,
    xx_dbz, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_stac_device,
    xx_stac, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_spk_device,
    xx_spk, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_wrzl_device,
    xx_wrzl, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_bagf_device,
    xx_bagf, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_emt_device,
    xx_emt, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_qip2_device,
    xx_qip2, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lif_device,
    xx_lif, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ixa_device,
    xx_ixa, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lspack10_device,
    xx_lspack10, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_starkit_device,
    xx_starkit, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_paperport_device,
    xx_paperport, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_rnca_device,
    xx_rnca, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_hog_device,
    xx_hog, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_agis_device,
    xx_agis, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_volitionvpft_device,
    xx_volitionvpft, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_wintermutedcp_device,
    xx_wintermutedcp, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_bsn_device,
    xx_bsn, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_res_device,
    xx_res, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_rsc_device,
    xx_rsc, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_teacy_device,
    xx_teacy, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_settlersft_device,
    xx_settlersft, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_wolfft_device,
    xx_wolfft, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_boo_device,
    xx_boo, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_vmssaveset_device,
    xx_vmssaveset, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_rawstac_device,
    xx_rawstac, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_androidboot_device,
    xx_androidboot, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_arcadyan_device,
    xx_arcadyan, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_autel_device,
    xx_autel, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dkbs_device,
    xx_dkbs, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dlink_tlv_device,
    xx_dlink_tlv, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dlke_device,
    xx_dlke, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ecos_device,
    xx_ecos, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_encfw_device,
    xx_encfw, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_encrpted_img_device,
    xx_encrpted_img, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_jboot_device,
    xx_jboot, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lingvoarc_device,
    xx_lingvoarc, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lz4demo_device,
    xx_lz4demo, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_matter_ota_device,
    xx_matter_ota, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mh01_device,
    xx_mh01, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_shrs_device,
    xx_shrs, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_silmarilsft_device,
    xx_silmarilsft, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_tplink_device,
    xx_tplink, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_twrx_device,
    xx_twrx, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_uboot_device,
    xx_uboot, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_infogramesft_device,
    xx_infogramesft, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_pdb_device,
    xx_pdb, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_xpak_device,
    xx_xpak, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dclraw_device,
    xx_dclraw, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_srec_device,
    xx_srec, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_lzop_device,
    xx_lzop, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_raw_deflate_compressed_data_device,
    xx_raw_deflate_compressed_data, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_apple_disk_copy_6_ndif_image_device,
    xx_apple_disk_copy_6_ndif_image, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_apple_sparse_bundle_device,
    xx_apple_sparse_bundle, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_encrypted_apple_disk_image_device,
    xx_encrypted_apple_disk_image, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_hxc_stream_hfe_device,
    xx_hxc_stream_hfe, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_ms_dos_backup_device,
    xx_ms_dos_backup, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_nec_pc_98_fdi_device,
    xx_nec_pc_98_fdi, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_ns2_device,
    xx_ns2, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_nsa_device,
    xx_nsa, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_qcow1_device,
    xx_qcow1, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_qnap_nas_firmware_device,
    xx_qnap_nas_firmware, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_rdb_device,
    xx_rdb, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_stuffit_split_file_device,
    xx_stuffit_split_file, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_t98_next_nfd_device,
    xx_t98_next_nfd, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_trs_80_jv1_device,
    xx_trs_80_jv1, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_trs_80_jv3_device,
    xx_trs_80_jv3, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_uharc_device,
    xx_uharc, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_visionaire_studio_vis_device,
    xx_visionaire_studio_vis, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_x68000_dim_device,
    xx_x68000_dim, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_is_xamarin_compressed_assembly_device,
    xx_xamarin_compressed_assembly, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dms_device,
    xx_dms, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dmg_device,
    xx_dmg, device, value, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_iso9660_device,
    xx_iso9660, device, iso, result, XX_FORMAT_REQUIRE_DEVICE, handle_base_info)

#endif /* full format-detection helpers */

#ifndef XXFC_FORMAT_DETECTION_LZMA_XZ_ONLY
/* Reader probes used inline by the detector. Each lives in its own
 * non-inlined frame: the reader structs are large, and the detector
 * must not hold dozens of them on one stack frame while a nested
 * probe (tar.zst, for example) decodes with a big stack buffer. */

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_installer_vise_windows,
    xx_installer_vise_windows, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_installshield_multiplatform,
    xx_installshield_multiplatform, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_advanced_installer_bootstrapper,
    xx_advanced_installer_bootstrapper, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_ifah_installer,
    xx_ifah_installer, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_installshield_7_setup,
    xx_installshield_7_setup, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_nullsoft_pimp,
    xx_sfx_nullsoft_pimp, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_sydex_diskette_image,
    xx_sfx_sydex_diskette_image, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_compaq_softpaq,
    xx_sfx_compaq_softpaq, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_wasp_windows_auto,
    xx_sfx_wasp_windows_auto, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_wise_installation_system,
    xx_wise_installation_system, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_eschalon_setup_epsf,
    xx_eschalon_setup_epsf, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_gentee_installer,
    xx_gentee_installer, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_clickteam_install_creator,
    xx_clickteam_install_creator, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_createinstall_instcrin_extractor,
    xx_createinstall_instcrin_extractor, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfxstart,
    xx_sfxstart, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_analogx_emucore_ffs,
    xx_sfx_analogx_emucore_ffs, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_krzip,
    xx_sfx_krzip, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_warpin_package,
    xx_sfx_warpin_package, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_hci_instalit,
    xx_sfx_hci_instalit, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_clickteam_multimedia_fusion,
    xx_sfx_clickteam_multimedia_fusion, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_abbyy_fine_objects,
    xx_sfx_abbyy_fine_objects, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_flashjester_jugglor,
    xx_sfx_flashjester_jugglor, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_jgsoft_deploymaster_package,
    xx_sfx_jgsoft_deploymaster_package, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_ardi_diskette_image,
    xx_sfx_ardi_diskette_image, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_arni_installer_container,
    xx_arni_installer_container, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_ej_technologies_install,
    xx_ej_technologies_install, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_installshield_3,
    xx_installshield_3, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_installshield_developer,
    xx_installshield_developer, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_ardi_installer,
    xx_ardi_installer, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_installshield_12_setup,
    xx_installshield_12_setup, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_qsetup_installation_suite,
    xx_qsetup_installation_suite, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_setup_factory,
    xx_setup_factory, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_tarma_installer,
    xx_tarma_installer, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_kwaj,
    xx_kwaj, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_rpm,
    xx_rpm, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_t64,
    xx_t64, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_pc_magazine_flp,
    xx_pc_magazine_flp, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_vdi,
    xx_vdi, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_cue,
    xx_cue, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

static XX_FORMAT_NOINLINE bool xx_format_probe_macbinary_verified(xx_io_device *dev) {
    xx_macbinary reader;
    bool valid;
    xx_macbinary_init(&reader, dev, 0);
    valid = xx_macbinary_check_is_valid_verified(&reader.format, NULL);
    xx_macbinary_destroy(&reader);
    return valid;
}

#endif /* full local format-detection probes */

static bool xx_format_read_probe_exact(xx_io_device *device, uint8_t *buffer, size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t got = xx_io_read(device, buffer + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

#ifndef XXFC_FORMAT_DETECTION_LZMA_XZ_ONLY
XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_softpaq4,
    xx_sfx_softpaq4, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_NOINLINE, xx_format_probe_sfx_inftool,
    xx_sfx_inftool, dev, reader, valid, XX_FORMAT_ALLOW_NULL, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_mlb_ft_device,
    xx_mlb_ft, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_fss_device,
    xx_fss, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_epf_device,
    xx_epf, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dfc_device,
    xx_dfc, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ppd_device,
    xx_ppd, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_ka_device,
    xx_ka, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_dn_device,
    xx_dn, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_insa_device,
    xx_insa, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_thebat_msb_device,
    xx_thebat_msb, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sfx_localzip_device,
    xx_sfx_localzip, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_sun_java_binsh_device,
    xx_sun_java_binsh, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

XX_FORMAT_READER_ADAPTER(XX_FORMAT_INLINE_ALLOWED, xx_format_is_installanywhere_unix_device,
    xx_installanywhere_unix, device, value, result, XX_FORMAT_REQUIRE_DEVICE, check_is_valid)

xx_file_type_t xx_format_gap_detect(xx_io_device *device);

#include "xx_format_probe_registry.h"

static XX_FORMAT_NOINLINE xx_file_type_t xx_format_get_unpacked_file_type_device(xx_io_device *dev) {
#else
xx_file_type_t xx_format_get_file_type_device(xx_io_device *dev) {
#endif
#ifdef XXFC_FORMAT_DETECTION_LZMA_XZ_ONLY
    /* A codec-only build must not pull in every format reader through the
     * generic detector. Keep cursor preservation and real LZMA validation. */
    static const uint8_t xz_magic[6] = {0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00};
    uint8_t header[13] = {0};
    xx_file_type_t type = XX_FILE_TYPE_BINARY;
    int64_t total_size;
    int64_t original_position;
    size_t header_size;

    if (!dev) return XX_FILE_TYPE_UNKNOWN;
    total_size = xx_io_total_size(dev);
    if (total_size <= 0) return XX_FILE_TYPE_UNKNOWN;
    original_position = xx_io_tell(dev);
    if (original_position < 0) original_position = 0;
    header_size = total_size < (int64_t)sizeof(header)
                      ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) == 0 &&
        xx_format_read_probe_exact(dev, header, header_size)) {
        if (header_size >= sizeof(xz_magic) &&
            xx_rt_memcmp(header, xz_magic, sizeof(xz_magic)) == 0) {
            type = XX_FILE_TYPE_XZ;
        } else if (xx_lzma_alone_has_header(header, header_size)) {
            xx_lzma lzma;
            xx_lzma_init(&lzma, dev, 0);
            if (xx_lzma_handle_base_info(&lzma.format, NULL))
                type = XX_FILE_TYPE_LZMA;
            xx_lzma_destroy(&lzma);
        }
    }
    (void)xx_io_seek64(dev, original_position, SEEK_SET);
    return type;
#else
    if (!dev) {
        return XX_FILE_TYPE_UNKNOWN;
    }

    int64_t total_size = xx_io_total_size(dev);
    if (total_size <= 0) {
        return XX_FILE_TYPE_UNKNOWN;
    }

    if (total_size < 4) {
        return XX_FILE_TYPE_BINARY;
    }

    /* Preserve the caller's 64-bit cursor. Devices without tell support keep
     * the historical zero-position fallback. */
    int64_t orig_pos = xx_io_tell(dev);
    if (orig_pos < 0) orig_pos = 0;

    /* Check magic at offset 0 */
    /* Some transport headers are substantially longer than the traditional
     * eight-byte signature window (for example Softronics v2.00's banner). */
    uint8_t magic[64] = {0};
    size_t magic_size = total_size < (int64_t)sizeof(magic)
                            ? (size_t)total_size
                            : sizeof(magic);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_BINARY;
    }
    if (!xx_format_read_probe_exact(dev, magic, magic_size)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_BINARY;
    }

    {
        xx_file_type_t signature_type = xx_format_detect_signature_readers(dev, NULL);
        if(signature_type != XX_FILE_TYPE_UNKNOWN) {(void)xx_io_seek64(dev,orig_pos,SEEK_SET);return signature_type;}
        xx_file_type_t document=xx_ue2_documents_detect_device(dev);
        (void)xx_io_seek64(dev,orig_pos,SEEK_SET);
        if(document!=XX_FILE_TYPE_UNKNOWN) return document;
        document=xx_ue2_games_detect_device(dev,NULL);
        (void)xx_io_seek64(dev,orig_pos,SEEK_SET);
        if(document!=XX_FILE_TYPE_UNKNOWN) return document;
        if(magic_size>=4 && (xx_rt_memcmp(magic,"CMMM",4)==0 ||
           (magic[0]>=4 && magic[0]<=5 && !magic[1] && !magic[2] && !magic[3]))) {
            Abstractformat *candidate=magic[0]=='C'?(Abstractformat *)xx_windows_thumbnail_cache_create(dev,0):(Abstractformat *)xx_chromium_pak_create(dev,0);
            bool valid=candidate && candidate->check_is_valid(candidate,NULL);
            document=candidate?candidate->file_type:XX_FILE_TYPE_UNKNOWN;
            if(magic[0]=='C')xx_windows_thumbnail_cache_free((xx_windows_thumbnail_cache *)candidate);else xx_chromium_pak_free((xx_chromium_pak *)candidate);
            (void)xx_io_seek64(dev,orig_pos,SEEK_SET);
            if(valid)return document;
        }
        if(magic_size>=16 && !xx_rt_memcmp(magic,"\x30\x26\xb2\x75\x8e\x66\xcf\x11\xa6\xd9\x00\xaa\x00\x62\xce\x6c",16))return XX_FILE_TYPE_ASF;
        if(magic_size>=8 && !xx_rt_memcmp(magic+4,"\x57\x90\x75\x36",4))return XX_FILE_TYPE_AUDIBLE_AA;
        if(magic_size>=12 && !xx_rt_memcmp(magic,"ITOLITLS",8) && magic[8]==1 && !magic[9] && !magic[10] && !magic[11])return XX_FILE_TYPE_MICROSOFT_LIT;
        if(xx_pyc_check_magic(magic, magic_size))return XX_FILE_TYPE_PYC;
        if(magic_size>=8 && !xx_rt_memcmp(magic,"%PDF-",5) &&
           magic[5]>='0' && magic[5]<='9' && magic[6]=='.' &&
           magic[7]>='0' && magic[7]<='9')return XX_FILE_TYPE_PDF;
        if(magic_size>=4&&!xx_rt_memcmp(magic,"DGCA",4)&&xx_io_total_size(dev)>=32)return XX_FILE_TYPE_DGCA;
        {
            xx_file_type_t installer=xx_excelsior_detect_device(dev,NULL);
            (void)xx_io_seek64(dev,orig_pos,SEEK_SET);
            if(installer!=XX_FILE_TYPE_UNKNOWN)return installer;
        }
        {
            xx_file_type_t installer=xx_fead_detect_device(dev,NULL);
            (void)xx_io_seek64(dev,orig_pos,SEEK_SET);
            if(installer!=XX_FILE_TYPE_UNKNOWN)return installer;
        }
        if(xx_bitrock_has_candidate_device(dev,0)) {
            xx_bitrock *r=xx_bitrock_create(dev,0);
            bool valid=r && r->format.check_is_valid(&r->format,NULL);
            xx_bitrock_free(r);
            (void)xx_io_seek64(dev,orig_pos,SEEK_SET);
            if(valid)return XX_FILE_TYPE_BITROCK;
        }
        if(xx_smart_install_maker_has_candidate_device(dev,0)) {
            xx_smart_install_maker *r=xx_smart_install_maker_create(dev,0);
            bool valid=r && ((Abstractformat *)r)->check_is_valid((Abstractformat *)r,NULL);
            xx_smart_install_maker_free(r);
            (void)xx_io_seek64(dev,orig_pos,SEEK_SET);
            if(valid)return XX_FILE_TYPE_SMART_INSTALL_MAKER;
        }
        if(xx_superdat_has_candidate_device(dev,0)) {
            xx_superdat *r=xx_superdat_create(dev,0);
            bool valid=r && r->format.check_is_valid(&r->format,NULL);
            xx_superdat_free(r);
            (void)xx_io_seek64(dev,orig_pos,SEEK_SET);
            if(valid)return XX_FILE_TYPE_SUPERDAT;
        }
        if(xx_molebox_has_candidate_device(dev,0)) {
            xx_molebox *r=xx_molebox_create(dev,0);
            bool valid=r && r->format.check_is_valid(&r->format,NULL);
            xx_molebox_free(r);
            (void)xx_io_seek64(dev,orig_pos,SEEK_SET);
            if(valid)return XX_FILE_TYPE_MOLEBOX;
        }
        if((magic_size>=2 && magic[0]=='M' && magic[1]=='Z') ||
           (magic_size>=4 && !xx_rt_memcmp(magic,"EVB\0",4))) {
            xx_enigma_virtual_box *r=xx_enigma_virtual_box_create(dev,0);
            bool valid=r && r->format.check_is_valid(&r->format,NULL);xx_enigma_virtual_box_free(r);
            (void)xx_io_seek64(dev,orig_pos,SEEK_SET);if(valid)return XX_FILE_TYPE_ENIGMA_VIRTUAL_BOX;
        }
        xx_file_type_t added=xx_format_gap_detect(dev);
        (void)xx_io_seek64(dev,orig_pos,SEEK_SET);
        if(added!=XX_FILE_TYPE_UNKNOWN) return added;
    }

    if (magic_size >= 4U && xx_rt_memcmp(magic, "RVZ\x01", 4U) == 0) {
        xx_rvz reader;
        bool valid;
        xx_rvz_init(&reader, dev, 0);
        valid = xx_rvz_check_is_valid(&reader.format, NULL);
        xx_rvz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RVZ;
    }

    if (magic_size >= 14U && magic[4] == 0U && magic[5] == 1U) {
        if (xx_rt_memcmp(magic, ".CKP", 4U) == 0) {
            xx_ckp reader;
            bool valid;
            xx_ckp_init(&reader, dev, 0);
            valid = xx_ckp_check_is_valid(&reader.format, NULL);
            xx_ckp_destroy(&reader);
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            if (valid) return XX_FILE_TYPE_CKP;
        } else if (xx_rt_memcmp(magic, ".EDP", 4U) == 0) {
            xx_edp reader;
            bool valid;
            xx_edp_init(&reader, dev, 0);
            valid = xx_edp_check_is_valid(&reader.format, NULL);
            xx_edp_destroy(&reader);
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            if (valid) return XX_FILE_TYPE_EDP;
        }
    }

    /* BIGF/ZBL is a Ptero-Engine container, unrelated to EA BIGF/BIG4.
     * Validate its full index before the generic BIGF probe below. */
    if (magic_size >= 64U && xx_rt_memcmp(magic, "BIGF", 4U) == 0 &&
        xx_rt_memcmp(magic + 5, "ZBL", 3U) == 0) {
        xx_ptero_bigf reader;
        bool valid;
        xx_ptero_bigf_init(&reader, dev, 0);
        valid = xx_ptero_bigf_check_is_valid(&reader.format, NULL);
        xx_ptero_bigf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PTERO_BIGF;
    }

    if (magic_size >= 8U && xx_rt_memcmp(magic, "RIB\0", 4U) == 0) {
        xx_parsec_rib reader;
        bool valid;
        xx_parsec_rib_init(&reader, dev, 0);
        valid = reader.format.check_is_valid(&reader.format, NULL);
        xx_parsec_rib_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PARSEC_RIB;
    }
    if (magic_size >= 16U &&
        xx_rt_memcmp(magic, "MTCVTS PSM 2.00", 16U) == 0) {
        xx_parsec_pmm reader;
        bool valid;
        xx_parsec_pmm_init(&reader, dev, 0);
        valid = reader.format.check_is_valid(&reader.format, NULL);
        xx_parsec_pmm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PARSEC_PMM;
    }

    /* A BACKUP save set can contain PK bytes in a member payload.  Its
     * complete block chain is stronger evidence than the embedded ZIP
     * signature, so validate the tightly prefixed save-set header first. */
    if (magic_size >= 36U && magic[0] == 0U && magic[1] == 1U &&
        magic[2] == 0U &&
        (magic[3] == 4U || magic[3] == 8U || magic[3] == 16U) &&
        magic[4] == 1U && magic[5] == 0U &&
        magic[6] >= 1U && magic[6] <= 2U && magic[7] == 0U &&
        magic[32] == 1U && magic[33] == 1U &&
        magic[34] == 1U && magic[35] == 0U) {
        bool valid = xx_format_is_vmssaveset_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VMSSAVESET;
    }
    /* A Solaris package may contain ZIP members.  Its exact outer banner
     * and validated package stream take precedence over an embedded ZIP. */
    if (magic_size >= 21U &&
        xx_rt_memcmp(magic, "# PaCkAgE DaTaStReAm\n", 21U) == 0) {
        bool valid = xx_format_is_solarispkg_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SOLARISPKG;
    }
    /* A GEMDOS self-extractor can carry ZIP bytes inside its LHA payload.
     * Validate the executable's declared outer LHA stream before considering
     * any embedded archive signature. */
    if (magic_size >= 28U && magic[0] == 0x60U && magic[1] == 0x1aU) {
        xx_sfx_lha value;
        bool valid;
        xx_sfx_lha_init(&value, dev, 0);
        valid = xx_sfx_lha_check_is_valid(&value.format, NULL);
        xx_sfx_lha_destroy(&value);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_LHA;
    }

    /* These signatured containers have independent, bounded validators. */
    if (magic_size >= 4U && xx_rt_memcmp(magic, "MAR1", 4U) == 0) {
        xx_mozilla_mar reader;
        bool valid;
        xx_mozilla_mar_init(&reader, dev, 0);
        valid = xx_mozilla_mar_check_is_valid(&reader.format, NULL);
        xx_mozilla_mar_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MOZILLA_MAR;
    }
    if (magic_size >= 4U && xx_rt_memcmp(magic, "FATX", 4U) == 0) {
        xx_fatx reader;
        bool valid;
        xx_fatx_init(&reader, dev, 0);
        valid = xx_fatx_check_is_valid(&reader.format, NULL);
        xx_fatx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FATX;
    }
    if (magic_size >= 12U && xx_rt_memcmp(magic, "RIFF", 4U) == 0 &&
        xx_rt_memcmp(magic + 8U, "sfbk", 4U) == 0) {
        xx_soundfont2 reader;
        bool valid;
        xx_soundfont2_init(&reader, dev, 0);
        valid = xx_soundfont2_check_is_valid(&reader.format, NULL);
        xx_soundfont2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SOUNDFONT2;
    }
    if (magic_size >= 4U && xx_rt_memcmp(magic, "DKIF", 4U) == 0) {
        xx_ivf reader;
        bool valid;
        xx_ivf_init(&reader, dev, 0);
        valid = xx_ivf_check_is_valid(&reader.format, NULL);
        xx_ivf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IVF;
    }
    if (magic_size >= 12U && xx_rt_memcmp(magic, "RIFF", 4U) == 0 &&
        xx_rt_memcmp(magic + 8U, "ACON", 4U) == 0) {
        xx_windows_ani reader;
        bool valid;
        xx_windows_ani_init(&reader, dev, 0);
        valid = xx_windows_ani_check_is_valid(&reader.format, NULL);
        xx_windows_ani_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WINDOWS_ANI;
    }

    /* Distinct HxC image containers require framing validation. Headerless
     * sector profiles are explicitly selected; size is not an identity. */
#define XX_PROBE_HXC(stem, id) do { \
        xx_##stem reader; bool valid; \
        xx_##stem##_init(&reader, dev, 0); \
        valid=xx_format_is_valid(&reader.format, NULL); \
        xx_##stem##_destroy(&reader); \
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET); \
        if(valid) return XX_FILE_TYPE_##id; \
    } while(0)
    if(magic_size>=8U && !xx_rt_memcmp(magic,"UAE-1ADF",8U)) XX_PROBE_HXC(amiga_ext_adf,AMIGA_EXT_ADF);
    if(magic_size>=8U && !xx_rt_memcmp(magic,"UAE--ADF",8U)) XX_PROBE_HXC(amiga_old_ext_adf,AMIGA_OLD_EXT_ADF);
    if(magic_size>=8U && (!xx_rt_memcmp(magic,"A2R2",4U) || !xx_rt_memcmp(magic,"A2R3",4U))) XX_PROBE_HXC(apple_a2r,APPLE_A2R);
    if(magic_size>=2U && !xx_rt_memcmp(magic,"BB",2U)) XX_PROBE_HXC(atari_dim,ATARI_DIM);
    if(magic_size>=4U && !xx_rt_memcmp(magic,"STEM",4U)) XX_PROBE_HXC(atari_stt,ATARI_STT);
    if(magic_size>=4U && !xx_rt_memcmp(magic,"STW\0",4U)) XX_PROBE_HXC(atari_stw,ATARI_STW);
    if(magic_size>=4U && (!xx_rt_memcmp(magic,"DFER",4U) || !xx_rt_memcmp(magic,"DFE2",4U))) XX_PROBE_HXC(discferret_dfi,DISCFERRET_DFI);
    if(magic_size>=14U && !xx_rt_memcmp(magic,"AFI_FLOPPY_IMG",14U)) XX_PROBE_HXC(hxc_afi,HXC_AFI);
    if(magic_size>=8U && !xx_rt_memcmp(magic,"HXCQDDRV",8U)) XX_PROBE_HXC(hxc_qd,HXC_QD);
    if(magic_size>=4U && !xx_rt_memcmp(magic,"CHKH",4U)) XX_PROBE_HXC(hxc_stream,HXC_STREAM);
    if(magic_size>=4U && (!xx_rt_memcmp(magic,"1.2\n",4U) || !xx_rt_memcmp(magic,"1.5\n",4U) ||
        !xx_rt_memcmp(magic,"2.0\n",4U) || !xx_rt_memcmp(magic,"1.2\r",4U) ||
        !xx_rt_memcmp(magic,"1.5\r",4U) || !xx_rt_memcmp(magic,"2.0\r",4U))) XX_PROBE_HXC(svd,SVD);
    if(magic_size>=20U && !xx_rt_memcmp(magic,"SAB Diskette Utility",20U)) XX_PROBE_HXC(sdu,SDU);
    if(magic_size>=8U && (!xx_rt_memcmp(magic,"MFM_DISK",8U) || !xx_rt_memcmp(magic,"ORICDISK",8U))) XX_PROBE_HXC(oric_dsk,ORIC_DSK);
    if(magic_size>=6U && !xx_rt_memcmp(magic,"TDDFI\1",6U)) XX_PROBE_HXC(ensoniq_gkh,ENSONIQ_GKH);
    if(magic_size>=2U && magic[0]==13U && magic[1]==10U) XX_PROBE_HXC(ensoniq_ede,ENSONIQ_EDE);
    if(magic_size>=18U && !xx_rt_memcmp(magic,"Aley's disk backup",18U)) XX_PROBE_HXC(samcoupe_sad,SAMCOUPE_SAD);
    if(magic_size>=13U && !xx_rt_memcmp(magic,"emaxutil v1.1",13U)) XX_PROBE_HXC(emax_disk,EMAX_DISK);
    if(magic_size>=7U && !xx_rt_memcmp(magic,"VTrucco",7U)) XX_PROBE_HXC(vtr_disk,VTR_DISK);
    if(magic_size>=5U && !xx_rt_memcmp(magic,"TRKY2",5U)) XX_PROBE_HXC(speccydos_sdd,SPECCYDOS_SDD);
    if((total_size==35*6656 || total_size==40*6656) && (magic[0]&0x80U)) XX_PROBE_HXC(apple_nib,APPLE_NIB);
    if(total_size==40*3253 || total_size==80*3253 || total_size==40*6872 || total_size==80*6872) XX_PROBE_HXC(ti99_pc99,TI99_PC99);
    if(total_size>=25000 && total_size<=8500000 && total_size%25000==0) XX_PROBE_HXC(fei,FEI);
    {
        size_t i=0;
        if(magic_size>=3U && magic[0]==0xefU && magic[1]==0xbbU && magic[2]==0xbfU) i=3;
        while(i<magic_size && (magic[i]==' ' || magic[i]=='\t' || magic[i]=='\r' || magic[i]=='\n')) ++i;
        if(i<magic_size && magic[i]=='<') XX_PROBE_HXC(hxc_xml_disk_layout,HXC_XML_DISK_LAYOUT);
    }
#undef XX_PROBE_HXC

    /* Validate each distinct DIE audio container before the broad detector.
     * Prefixes keep these bounded native probes off unrelated files. */
#define XX_PROBE_DIE_AUDIO(stem, id) do { \
        xx_##stem reader; \
        bool valid; \
        xx_##stem##_init(&reader, dev, 0); \
        valid = xx_##stem##_check_is_valid(&reader.format, NULL); \
        xx_##stem##_destroy(&reader); \
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET); \
        if (valid) return XX_FILE_TYPE_##id; \
    } while (0)
    if (magic_size >= 4U && magic[0] == 0x97U && magic[1] == 0x28U &&
        magic[2] == 0x03U && magic[3] == 0x01U)
        XX_PROBE_DIE_AUDIO(interplay_acm, INTERPLAY_ACM);
    if (magic_size >= 24U && magic[0] == 0x80U && magic[1] == 0x00U)
        XX_PROBE_DIE_AUDIO(cri_ahx, CRI_AHX);
    if (magic_size >= 12U &&
        (!xx_rt_memcmp(magic, "RIFX", 4U) || !xx_rt_memcmp(magic, "XFIR", 4U)))
        XX_PROBE_DIE_AUDIO(adobe_director_cxt, ADOBE_DIRECTOR_CXT);
    if (magic_size >= 4U && (magic[0] == 2U || magic[0] == 3U) &&
        (!xx_rt_memcmp(magic + 1U, "dss", 3U) ||
         !xx_rt_memcmp(magic + 1U, "ds2", 3U) ||
         !xx_rt_memcmp(magic + 1U, "enc", 3U)))
        XX_PROBE_DIE_AUDIO(olympus_dss, OLYMPUS_DSS);
    if (magic_size >= 4U && !xx_rt_memcmp(magic, "SCHl", 4U))
        XX_PROBE_DIE_AUDIO(ea_exa, EA_EXA);
    if (magic_size >= 8U && !xx_rt_memcmp(magic, "STRM", 4U) &&
        magic[4] == 0xe8U && magic[5] == 0x03U &&
        magic[6] == 0U && magic[7] == 0U)
        XX_PROBE_DIE_AUDIO(abylight_strm, ABYLIGHT_STRM);
    if (magic_size >= 4U && !xx_rt_memcmp(magic, "STRM", 4U))
        XX_PROBE_DIE_AUDIO(audio_nitro_strm, AUDIO_NITRO_STRM);
    if (magic_size >= 12U && !xx_rt_memcmp(magic, "RIFF", 4U) &&
        !xx_rt_memcmp(magic + 8U, "IMA ", 4U))
        XX_PROBE_DIE_AUDIO(audio_riff_ima, AUDIO_RIFF_IMA);
    if (magic_size >= 12U &&
        (!xx_rt_memcmp(magic, "RIFF", 4U) || !xx_rt_memcmp(magic, "RIFX", 4U)) &&
        (!xx_rt_memcmp(magic + 8U, "WAVE", 4U) || !xx_rt_memcmp(magic + 8U, "XWMA", 4U)))
        XX_PROBE_DIE_AUDIO(audio_wwise_wem, AUDIO_WWISE_WEM);
    if (magic_size >= 12U && !xx_rt_memcmp(magic, "RIFX", 4U) &&
        !xx_rt_memcmp(magic + 8U, "WAVE", 4U))
        XX_PROBE_DIE_AUDIO(audio_rifx_wave, AUDIO_RIFX_WAVE);
    if (magic_size >= 4U && !xx_rt_memcmp(magic, "SOU ", 4U))
        XX_PROBE_DIE_AUDIO(audio_scumm_sou, AUDIO_SCUMM_SOU);
    if (magic_size >= 18U && !xx_rt_memcmp(magic, "HMI-MIDISONG061595", 18U))
        XX_PROBE_DIE_AUDIO(hmi_midi, HMI_MIDI);
    if (magic_size >= 4U &&
        (!xx_rt_memcmp(magic, " paf", 4U) || !xx_rt_memcmp(magic, "fap ", 4U)))
        XX_PROBE_DIE_AUDIO(ensoniq_paf, ENSONIQ_PAF);
    if (magic_size >= 4U && !xx_rt_memcmp(magic, "ALP ", 4U))
        XX_PROBE_DIE_AUDIO(lego_alp, LEGO_ALP);
    if (magic_size >= 4U && !xx_rt_memcmp(magic, "PVF", 3U))
        XX_PROBE_DIE_AUDIO(audio_pvf, AUDIO_PVF);
    if (magic_size >= 8U && magic[0] == 0U && magic[1] == 0U &&
        magic[2] >= 1U && magic[4] == 0U && magic[5] == 0U &&
        magic[6] == 0U && magic[7] == 3U)
        XX_PROBE_DIE_AUDIO(audio_shockwave_swa, AUDIO_SHOCKWAVE_SWA);
#undef XX_PROBE_DIE_AUDIO

    {
        xx_file_type_t additional = xx_format_detect_additional(dev);
        if (additional != XX_FILE_TYPE_UNKNOWN) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return additional;
        }
    }

    bool is_zip = false;
    bool is_zip64 = false;
    bool is_7zip = magic_size >= 6 &&
                   magic[0] == 0x37 && magic[1] == 0x7A &&
                   magic[2] == 0xBC && magic[3] == 0xAF &&
                   magic[4] == 0x27 && magic[5] == 0x1C;
    bool is_rar = magic_size >= 7 &&
                  magic[0] == 0x52 && magic[1] == 0x61 &&
                  magic[2] == 0x72 && magic[3] == 0x21 &&
                  magic[4] == 0x1A && magic[5] == 0x07 &&
                  (magic[6] == 0x00 ||
                   (magic_size >= 8 && magic[6] == 0x01 && magic[7] == 0x00));
    bool is_ar = magic_size >= 8 &&
                 (xx_rt_memcmp(magic, "!<arch>\n", 8) == 0 ||
                  xx_rt_memcmp(magic, "!<thin>\n", 8) == 0);
    bool is_bz2 = magic_size >= 4 &&
                  magic[0] == 'B' && magic[1] == 'Z' && magic[2] == 'h' &&
                  magic[3] >= '1' && magic[3] <= '9';
    bool is_gz = magic_size >= 3 &&
                 magic[0] == 0x1F && magic[1] == 0x8B && magic[2] == 0x08;
    bool is_xz = magic_size >= 6 &&
                  magic[0] == 0xFD && magic[1] == 0x37 && magic[2] == 0x7A &&
                  magic[3] == 0x58 && magic[4] == 0x5A && magic[5] == 0x00;
    bool is_lz4 = magic_size >= 4 &&
                  ((magic[0] == 0x04 && magic[1] == 0x22 &&
                    magic[2] == 0x4D && magic[3] == 0x18) ||
                   (magic[0] >= 0x50 && magic[0] <= 0x5F &&
                    magic[1] == 0x2A && magic[2] == 0x4D &&
                    magic[3] == 0x18));
    bool is_lz5 = magic_size >= 4 &&
                  ((magic[0] == 0x05 && magic[1] == 0x22 &&
                    magic[2] == 0x4D && magic[3] == 0x18) ||
                   (magic[0] >= 0x50 && magic[0] <= 0x5F &&
                    magic[1] == 0x2A && magic[2] == 0x4D &&
                    magic[3] == 0x18));
    bool is_lizard = magic_size >= 4 &&
                     ((magic[0] == 0x06 && magic[1] == 0x22 &&
                       magic[2] == 0x4D && magic[3] == 0x18) ||
                      (magic[0] >= 0x50 && magic[0] <= 0x5F &&
                       magic[1] == 0x2A && magic[2] == 0x4D &&
                       magic[3] == 0x18));
    bool is_brotli_mt = magic_size >= 16 &&
                        magic[0] == 0x50 && magic[1] == 0x2A &&
                        magic[2] == 0x4D && magic[3] == 0x18 &&
                        magic[4] == 0x08 && magic[5] == 0x00 &&
                        magic[6] == 0x00 && magic[7] == 0x00 &&
                        magic[12] == 'B' && magic[13] == 'R';
    bool is_zstd = magic_size >= 4 &&
                   ((magic[0] == 0x28 && magic[1] == 0xB5 &&
                     magic[2] == 0x2F && magic[3] == 0xFD) ||
                    (magic[0] >= 0x50 && magic[0] <= 0x5F &&
                     magic[1] == 0x2A && magic[2] == 0x4D &&
                     magic[3] == 0x18));
    bool is_mz = magic_size >= 2 && magic[0] == 'M' && magic[1] == 'Z';
    bool is_elf = magic_size >= 5 && magic[0] == 0x7fU &&
                  magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F' &&
                  (magic[4] == XX_ELF_CLASS_32 ||
                   magic[4] == XX_ELF_CLASS_64);
    bool is_macho = magic_size >= 4 &&
                    ((magic[0] == 0xceU && magic[1] == 0xfaU &&
                      magic[2] == 0xedU && magic[3] == 0xfeU) ||
                     (magic[0] == 0xcfU && magic[1] == 0xfaU &&
                      magic[2] == 0xedU && magic[3] == 0xfeU) ||
                     (magic[0] == 0xfeU && magic[1] == 0xedU &&
                      magic[2] == 0xfaU && magic[3] == 0xceU) ||
                     (magic[0] == 0xfeU && magic[1] == 0xedU &&
                      magic[2] == 0xfaU && magic[3] == 0xcfU));
    bool is_dex = magic_size >= XX_DEX_MAGIC_SIZE &&
                  xx_rt_memcmp(magic, "dex\n", 4U) == 0 && magic[7] == 0U;
    bool is_iso9660 = false;
    bool is_ace = false;
    bool is_ain = magic_size >= 1 && magic[0] == '!';
    bool is_aldus = magic_size >= 8 &&
                    (xx_rt_memcmp(magic, "ALDUS LZ", 8U) == 0 ||
                     xx_rt_memcmp(magic, "ALDUS PK", 8U) == 0 ||
                     xx_rt_memcmp(magic, "ADOBE LZ", 8U) == 0);
    bool is_alz = magic_size >= 4 && xx_rt_memcmp(magic, "ALZ\1", 4U) == 0;
    bool is_ampk = magic_size >= 4 && xx_rt_memcmp(magic, "AMPK", 4U) == 0;
    bool is_aodos = magic_size >= 4U &&
                    ((magic[0] == 0xa0U && magic[1] == 0U &&
                      (magic[2] == 0x16U || magic[2] == 0x20U ||
                       magic[2] == 0x22U) && magic[3] == 1U) ||
                     (magic[0] == 0U && magic[1] == 0U &&
                      magic[2] == 0U && magic[3] == 0U));
    bool is_arcfs = magic_size >= 8 && xx_rt_memcmp(magic, "Archive\0", 8U) == 0;
    bool is_pdp11ar = magic_size >= 2 && magic[0] == 0x65U && magic[1] == 0xffU;
    bool is_artipack = magic_size >= 8 && xx_rt_memcmp(magic, "ARTIPACK", 8U) == 0;
    bool is_arcv2 = magic_size >= 8 && xx_rt_memcmp(magic, "ARCV", 4U) == 0 &&
                    magic[4] == 0U && magic[5] == 2U &&
                    magic[6] == 14U && magic[7] == 0U;
    bool is_arcv4 = magic_size >= 6 && xx_rt_memcmp(magic, "ARCV", 4U) == 0 &&
                    magic[4] == 0U && magic[5] == 4U;
    /* 0x60 0xea alone is a weak signature, so also require the little-endian
     * basic-header size that follows it to be in the range xx_arj_parse()
     * accepts. The header body is CRC32-verified afterwards. */
    bool is_arj = magic_size >= 4 && magic[0] == 0x60U && magic[1] == 0xeaU &&
                  (uint16_t)(magic[2] | ((uint16_t)magic[3] << 8U)) >= 30U &&
                  (uint16_t)(magic[2] | ((uint16_t)magic[3] << 8U)) <= 2600U;
    bool is_cab = magic_size >= 4 && xx_rt_memcmp(magic, "MSCF", 4U) == 0;
    /* XX_BFF_VOLUME_MAGIC is 0xea6b0009 read little-endian, so the bytes on
     * disk are 09 00 6b ea. */
    bool is_aixbff = magic_size >= 4 && magic[0] == 0x09U && magic[1] == 0x00U &&
                     magic[2] == 0x6bU && magic[3] == 0xeaU;
    /* ARX has no signature at offset 0: byte 0 is the first header's size - 2
     * and byte 1 its checksum. What is fixed is the LHA-style method stamp at
     * bytes 2..6 and the zero at byte 7, both of which xx_arx_parse() requires. */
    bool is_arx = magic_size >= 8 && magic[0] >= 22U && magic[2] == '-' &&
                  magic[3] == 'l' && (magic[4] == 'h' || magic[4] == 'z') &&
                  magic[5] >= '0' && magic[5] <= '9' && magic[6] == '-' &&
                  magic[7] == 0U;
    bool is_warc = magic_size >= 7 && xx_rt_memcmp(magic, "WARC/", 5U) == 0 &&
                   (magic[5] == '1' || magic[5] == '0') && magic[6] == '.';
    bool is_cpio = (magic_size >= 6 &&
                    (xx_rt_memcmp(magic, "070701", 6U) == 0 ||
                     xx_rt_memcmp(magic, "070702", 6U) == 0 ||
                     xx_rt_memcmp(magic, "070707", 6U) == 0 ||
                     xx_rt_memcmp(magic, "070727", 6U) == 0)) ||
                   (magic_size >= 2 &&
                    ((magic[0] == 0xc7U && magic[1] == 0x71U) ||
                     (magic[0] == 0x71U && magic[1] == 0xc7U))) ||
                   /* Solaris ships its cpio archives inside a block-compressed
                    * wrapper whose own header is 0x19 0x9E 'T' 'L'; the cpio
                    * stream only appears after inflating it. The reader knows
                    * that container, so the prefilter has to let it through. */
                   (magic_size >= 4 &&
                    magic[0] == 0x19U && magic[1] == 0x9eU &&
                    magic[2] == 'T' && magic[3] == 'L');
    bool is_mtree = magic_size >= 6 && xx_rt_memcmp(magic, "#mtree", 6U) == 0;
    bool is_compress = magic_size >= 3 &&
                       magic[0] == XX_COMPRESS_MAGIC0 &&
                       magic[1] == XX_COMPRESS_MAGIC1 &&
                       (magic[2] & UINT8_C(0x60)) == 0U &&
                       (magic[2] & UINT8_C(0x1f)) >= 9U &&
                       (magic[2] & UINT8_C(0x1f)) <= 16U;
    bool is_unixpack = magic_size >= 6 && magic[0] == UINT8_C(0x1f) &&
                       (magic[1] == UINT8_C(0x1e) ||
                        magic[1] == UINT8_C(0x1f));
    bool is_zlib = magic_size >= 6 && (magic[0] & UINT8_C(0x0f)) == 8U &&
                   (magic[0] >> 4U) <= 7U &&
                   ((((uint16_t)magic[0] << 8U) | magic[1]) % 31U) == 0U &&
                   (magic[1] & UINT8_C(0x20)) == 0U;
    bool is_mscompress =
        (magic_size >= 8 && xx_rt_memcmp(magic, "SZDD\x88\xf0\x27\x33", 8U) == 0) ||
        (magic_size >= 7 && xx_rt_memcmp(magic, "SZ \x88\xf0\x27\x33", 7U) == 0);
    bool is_ash0 = magic_size >= 4 && xx_rt_memcmp(magic, "ASH0", 4U) == 0;
    bool is_wiilz77 =
        (magic_size >= 5 && xx_rt_memcmp(magic, "LZ77", 4U) == 0 &&
         (magic[4] == 0x10U || magic[4] == 0x11U)) ||
        (magic_size >= 1 && (magic[0] == 0x10U || magic[0] == 0x11U)) ||
        (magic_size >= 4 && xx_rt_memcmp(magic, "IMD5", 4U) == 0);
    bool is_lzv1 = magic_size >= 10 && xx_rt_memcmp(magic, "LZV1", 4U) == 0 &&
                   magic[4] == 0x5dU && magic[5] == 0x19U &&
                   magic[6] == 0x01U && magic[7] == 0xadU &&
                   magic[8] == 0x00U && magic[9] == 0x00U;
    bool is_oraclesqueeze = magic_size >= 2 && magic[0] == 0x76U &&
                            magic[1] == 0xffU;
    bool is_softronics =
        magic_size >= 42 && magic[1] == 0U &&
        xx_rt_memcmp(magic + 2U, "Softronics Compressed File\0Version 2.00\0",
               40U) == 0;
    bool is_logitechcompress = magic_size >= 10 && magic[0] == 0xdaU &&
                               magic[1] == 0xfaU && magic[8] <= 1U &&
                               magic[9] >= 4U && magic[9] <= 6U;
    bool is_dmapacked = magic_size >= 34 && magic[0] == 'd' &&
                        magic[1] == 'm' && magic[2] == 0x10U &&
                        magic[3] == 0x11U &&
                        xx_rt_memcmp(magic + 0x1aU, "PAKPAK", 6U) == 0 &&
                        magic[0x20U] == 0U && magic[0x21U] == 0x2aU;
    bool is_huf = magic_size >= 2 && magic[0] == 0xbdU && magic[1] == 0x01U;
    bool is_lzdiet = magic_size >= 6 && xx_rt_memcmp(magic, "lZdIeT", 6U) == 0;
    bool is_lzpis2 = magic_size >= 6 && xx_rt_memcmp(magic, "LZPIS2", 6U) == 0;
    bool is_zie = total_size >= 0x118 && magic_size >= 4U && xx_rt_memcmp(magic, "PIT2", 4U) == 0;
    bool is_xeditpack = magic_size >= 16 && magic[0x0] == 0x00U && magic[0x1] == 0x01U && magic[0x2] == 0x40U &&
                        (magic[3] == 0xc6U || magic[3] == 0xe5U);
    bool is_wpk = magic_size >= 0x0c &&
                  ((magic[0x0] == 0x03U && magic[0x1] == 0x24U && magic[0x2] == 0x01U && magic[0x3] == 0x01U) ||
                   (magic[0x0] == 0x03U && magic[0x1] == 0x24U && magic[0x2] == 0x33U && magic[0x3] == 0x01U));
    bool is_wintersoft = magic_size >= 16 && xx_rt_memcmp(magic, "**++", 4U) == 0 &&
                         (xx_rt_memcmp(magic + 0x4U, "LZW ", 4U) == 0 ||
                          xx_rt_memcmp(magic + 0x4U, "HUFF", 4U) == 0);
    bool is_vmarc = magic_size >= 16 &&
                    magic[0x0] == 0x7aU && magic[0x1] == 0xc3U && magic[0x2] == 0xc6U && magic[0x3] == 0xc6U && magic[0x4] == 0x40U && magic[0x5] == 0x40U && magic[0x6] == 0x40U && magic[0x7] == 0x40U && magic[0x8] == 0x01U;
    /* 12, not 0x50: the prefilter window is 64 bytes, so a 0x50 test could
     * never be true and Tivoli was unreachable. The banner it matches is
     * fully inside the first twelve bytes. */
    bool is_tivoli = magic_size >= 12U && xx_rt_memcmp(magic, "    79 TFPB-", 12U) == 0;
    /* A minimum FILE size, not a magic-window size: the window is 64
     * bytes, so asking for 0x80 of it could never be true and TI99ARC
     * was unreachable. Same correction applies to the four below. */
    bool is_ti99arc = total_size >= 0x80;
    bool is_stylus = magic_size >= 0x10 && xx_rt_memcmp(magic, "DP", 2U) == 0 &&
                     magic[0x2] == 0x1aU && magic[0x3] == 0x07U;
    bool is_rtpatch = magic_size >= 0x1a && xx_rt_memcmp(magic, "K*", 2U) == 0;
    bool is_rta = magic_size >= 8 && xx_rt_memcmp(magic, "KJd", 3U) == 0 && magic[3] == 0U;
    bool is_rompaq = total_size >= 0x4a && magic_size >= 0x14 && magic[0x13] == 0U;
    bool is_rid = magic_size >= 0x2b && magic[0x2] == 0x00U && magic[0x3] == 0x00U && magic[0x4] == 0x00U && magic[0x5] == 0x00U && magic[0x6] == 0x00U && magic[0x7] == 0x00U && magic[0x8] == 0x00U && magic[0x9] == 0x00U;
    bool is_qnxbase = magic_size >= 16 && magic[0x0] == 0xebU && magic[0x1] == 0x4cU && magic[0x2] == 0x44U && magic[0x3] == 0x44U && magic[0x4] == 0x44U && magic[0x5] == 0x44U;
    bool is_qda = magic_size >= 0x10 && xx_rt_memcmp(magic + 0x4U, "QDA0", 4U) == 0 && magic[1] == 0U;
    bool is_pkt = magic_size >= 0x3a && magic[0x12] == 0x02U && magic[0x13] == 0x00U;
    bool is_lofi = magic_size >= 0x24 &&
        ((xx_rt_memcmp(magic, "lzma\0", 5U) == 0) ||
         (xx_rt_memcmp(magic, "gzip\0", 5U) == 0) ||
         (xx_rt_memcmp(magic, "gzip-6\0", 7U) == 0) ||
         (xx_rt_memcmp(magic, "gzip-9\0", 7U) == 0));
    bool is_lim = magic_size >= 8 && xx_rt_memcmp(magic, "LM", 2U) == 0 &&
                  magic[0x2] == 0x1aU && magic[0x3] == 0x08U && magic[4] == 0U;
    bool is_kolibrikpack = magic_size >= 12 && xx_rt_memcmp(magic, "KPCK", 4U) == 0;
    bool is_ivt = magic_size >= 8 && magic[0x0] == 0x3fU && magic[0x1] == 0x5fU && magic[0x2] == 0x04U && magic[0x3] == 0x01U;
    bool is_irwinpac = magic_size >= 20 && xx_rt_memcmp(magic, "IrwinPac", 8U) == 0 &&
                       magic[8] == 20U && magic[9] == 0U;
    bool is_hap = magic_size >= 15 && magic[0x0] == 0x91U && magic[0x1] == 0x33U && magic[0x2] == 0x48U && magic[0x3] == 0x46U &&
                  magic[0x4] == 0x00U && magic[0x5] == 0x00U && magic[0x6] == 0x00U && magic[0x7] == 0x00U;
    bool is_compactpro = magic_size >= 8 && magic[0] == 1U;
    bool is_imp = magic_size >= 42 && xx_rt_memcmp(magic, "IMP\n", 4U) == 0;
    bool is_sqx = magic_size >= 25 && magic[2] == 'R' && xx_rt_memcmp(magic + 0x7U, "-sqx-", 5U) == 0;
    bool is_zoo = magic_size >= 0x20 && magic[0x14] == 0xdcU && magic[0x15] == 0xa7U && magic[0x16] == 0xc4U && magic[0x17] == 0xfdU;
    bool is_trx = magic_size >= 4U && magic[0]==0x48U && magic[1]==0x44U && magic[2]==0x52U && magic[3]==0x30U;
    bool is_seama = magic_size >= 4U && magic[0]==0x5EU && magic[1]==0xA3U && magic[2]==0xA4U && magic[3]==0x17U;
    /* DLOB is not a distinct container: it is a SEAMA chain whose first
     * entity has size 0 (metadata only, carrying the board signature).
     * Same magic, so the zero size at +8 is the only discriminator, and
     * DLOB must be tried before SEAMA -- SEAMA accepts both shapes. */
    bool is_dlob = magic_size >= 12U && magic[0]==0x5EU && magic[1]==0xA3U &&
                   magic[2]==0xA4U && magic[3]==0x17U &&
                   magic[8]==0U && magic[9]==0U && magic[10]==0U && magic[11]==0U;
    bool is_chk = magic_size >= 4U && magic[0]==0x2AU && magic[1]==0x23U && magic[2]==0x24U && magic[3]==0x5EU;
    bool is_packimg = magic_size >= 12U && xx_rt_memcmp(magic, "--PaCkImGs--", 12U) == 0;
    /* "B000FF
" -- spelled as bytes because the trailing newline has no
     * business being an escape inside a source string. */
    bool is_wince = magic_size >= 7U && magic[0]==0x42U && magic[1]==0x30U &&
                    magic[2]==0x30U && magic[3]==0x30U && magic[4]==0x46U &&
                    magic[5]==0x46U && magic[6]==0x0AU;
    bool is_rtk = magic_size >= 4U && xx_rt_memcmp(magic, "RTK0", 4U) == 0;
    bool is_binhdr = magic_size >= 18U && xx_rt_memcmp(magic + 14U, "U2ND", 4U) == 0;
    bool is_qcow = magic_size >= 8U && magic[0]==0x51U && magic[1]==0x46U && magic[2]==0x49U &&
        magic[3]==0xFBU && magic[4]==0U && magic[5]==0U && magic[6]==0U &&
        (magic[7]==2U || magic[7]==3U);
    bool is_luks = magic_size >= 8U && magic[0]==0x4CU && magic[1]==0x55U && magic[2]==0x4BU &&
        magic[3]==0x53U && magic[4]==0xBAU && magic[5]==0xBEU &&
        magic[6]==0U && (magic[7]==1U || magic[7]==2U);
    bool is_apfs = magic_size >= 36U && magic[32]==0x4EU && magic[33]==0x58U && magic[34]==0x53U && magic[35]==0x42U;
    bool is_logfs = magic_size >= 32U && magic[24]==0x7AU && magic[25]==0x3AU && magic[26]==0x8EU && magic[27]==0x5CU;
    bool is_uefi_fv = magic_size >= 44U && magic[40]==0x5FU && magic[41]==0x46U && magic[42]==0x56U && magic[43]==0x48U;
    /* Past the 64-byte window or magicless: qnx6's superblock is at
     * 0x2000, btrfs's at 0x10000, dmg's koly trailer in the LAST 512
     * bytes, csman/dlob/vxworks/uefi_capsule need structural probes. */
    bool is_qnx6 = total_size > 0x3000;
    bool is_btrfs = total_size > 0x11000;
    bool is_dmg = total_size >= 512;
    bool is_applesingle = (magic_size >= 4U && magic[0] == 0x00U && magic[1] == 0x05U && magic[2] == 0x16U && (magic[3] == 0x00U || magic[3] == 0x07U));
    bool is_pp20 = (magic_size >= 8U && (xx_rt_memcmp(magic, "PP20", 4U) == 0 || xx_rt_memcmp(magic, "PP11", 4U) == 0 || xx_rt_memcmp(magic, "PPLS", 4U) == 0 || xx_rt_memcmp(magic, "PX20", 4U) == 0 || xx_rt_memcmp(magic, "PPBK", 4U) == 0));
    bool is_beatthehouse = (magic_size >= 8U && magic[0] == 'P' && magic[1] == 'A' && magic[2] == 'K' && magic[3] >= 'A' && magic[3] <= 'Z');
    bool is_kpck = (magic_size >= 12U && xx_rt_memcmp(magic, "KPCK", 4U) == 0);
    bool is_perform = (magic_size >= 34U && xx_rt_memcmp(magic, "PerFORM compressed database 1.00 ", 34U) == 0);
    bool is_mathcad = (magic_size >= 15U && xx_rt_memcmp(magic, ".MCDCOMPRESSION", 15U) == 0);
    bool is_netware2 = (magic_size >= 20U && magic[0] == 0x23U && magic[1] == 0U && magic[2] == 0U && magic[3] == 0U && magic[4] == 0x10U && xx_rt_memcmp(magic + 5, "NetWareFileInfo", 15U) == 0);
    bool is_shar = ((magic_size >= 9U && xx_rt_memcmp(magic, "#!/bin/sh", 9U) == 0) || (magic_size >= 10U && xx_rt_memcmp(magic, "#! /bin/sh", 10U) == 0) || (magic_size >= 25U && xx_rt_memcmp(magic, "# This is a shell archive", 25U) == 0));
    bool is_shell_wrapper = (magic_size >= 9U &&
                             xx_rt_memcmp(magic, "#!/bin/sh", 9U) == 0) ||
                            (magic_size >= 11U &&
                             xx_rt_memcmp(magic, "#!/bin/bash", 11U) == 0);
    bool is_rnc = (magic_size >= 18U && magic[0] == 'R' && magic[1] == 'N' && magic[2] == 'C' && (magic[3] == 1U || magic[3] == 2U));
    bool is_ibmpack = (magic_size >= 4U && magic[0] == 0xa5U && magic[1] == 0x96U && ((magic[2] == 0xfeU && magic[3] == 0xffU) || (magic[2] == 0xffU && magic[3] == 0xffU) || (magic[2] == 0x14U && magic[3] == 0x0aU) || (magic[2] == 0x00U && magic[3] == 0x14U)));
    bool is_cazip = ((magic_size >= 10U && magic[0] == 0x0dU && magic[1] == 0x0aU && magic[2] == 0x1aU && xx_rt_memcmp(magic + 3, "CAZIP", 5U) == 0) || (magic_size >= 6U && xx_rt_memcmp(magic, "CAZIP", 5U) == 0 && magic[5] == 0x04U));
    bool is_tpwm = (magic_size >= 8U && magic[0] == 'T' && magic[1] == 'P' && magic[2] == 'W' && magic[3] == 'M');
    bool is_mrnz = (magic_size >= 12U && magic[0] == 'M' && magic[1] == 'R' && magic[2] == 'N' && magic[3] == 'Z' && magic[4] == 0x88U && magic[5] == 0xf0U && magic[6] == 0x27U && magic[7] == 0x33U);
    bool is_edc = (magic_size >= 12U && xx_rt_memcmp(magic, " EDC Packed ", 12U) == 0);
    bool is_xorarchive = xx_xorarchive_test_magic(magic, magic_size);
    bool is_mwave = (magic_size >= 24U && magic[0] == 0x1fU && magic[1] == 0x9dU && magic[6] == 0x20U && magic[7] == 0x00U && magic[20] == 0U && magic[21] == 0U && magic[22] == 0U && (magic[23] & 0x60U) == 0U && (magic[23] & 0x1fU) >= 9U && (magic[23] & 0x1fU) <= 16U);
    bool is_finear = (magic_size >= 17U && xx_rt_memcmp(magic, "FINEAR", 6U) == 0 && magic[6] == 0xddU && magic[7] == 0x88U && magic[8] == 0xddU);
    bool is_gst = (magic_size >= 32U &&
                   ((magic[0] == 0xe9U && magic[1] == 0xc8U) ||
                    (magic[0] == 0xeaU && magic[1] == 0xc9U)) &&
                   (magic[7] == 0x00U || magic[7] == 0x01U));
    bool is_winlink = (magic_size >= 24U && magic[0] == 0x02U && magic[1] == 0x00U && magic[2] == 0x00U && magic[20] == 0xFFU && magic[21] == 0xFFU && magic[22] == 0xFFU && magic[23] == 0xFFU);
    bool is_ftcomp = (magic_size >= 31U && magic[0] == 0xA5U && magic[1] == 0x96U && magic[2] == 0xFDU && magic[3] == 0xFFU && xx_rt_memcmp(magic + 24, "FTCOMP", 6U) == 0);
    bool is_gpfpack = (magic_size >= 14U && magic[0] == 0xC0U && magic[1] == 0U && magic[2] == 0U && magic[3] == 0U && xx_rt_memcmp(magic + 4, "GPFPACK", 7U) == 0 && magic[12] == 0x01U && magic[13] == 0x00U);
    bool is_sco = (magic_size >= 4U && magic[0] == 0x1fU && magic[1] == 0xa0U);
    bool is_unixcompact = (magic_size >= 2U && magic[0] == 0xffU && magic[1] == 0x1fU);
    bool is_is3 = (magic_size >= 8U && ((magic[0] == 0x13U && magic[1] == 0x5DU && magic[2] == 0x65U && magic[3] == 0x8CU) || (magic[0] == 0x2AU && magic[1] == 0xABU && magic[2] == 0x79U && magic[3] == 0xD8U && magic[4] == 0x00U && magic[5] == 0x01U && magic[6] == 0x00U && magic[7] == 0x00U)));
    bool is_is5 = (magic_size >= 4U && xx_rt_memcmp(magic, "ISc(", 4U) == 0);
    bool is_is7inx = (magic_size >= 4U && magic[0] == 0x74U && magic[1] == 0xC4U && magic[2] == 0x2CU && magic[3] == 0x84U);
    bool is_edilzss = (magic_size >= 8U && xx_rt_memcmp(magic, "EDILZSS", 7U) == 0 && (magic[7] == '1' || magic[7] == '2'));
    bool is_savedskf = (magic_size >= 2U && magic[0] == 0xAAU && (magic[1] == 0x58U || magic[1] == 0x59U || magic[1] == 0x5AU));
    bool is_gob = (magic_size >= 4U && xx_rt_memcmp(magic, "GOB", 3U) == 0 && (magic[3] == 0x0AU || magic[3] == ' '));
    bool is_debugscr = (magic_size >= 3U && ((magic[0] == 'N' || magic[0] == 'n') || ((magic[0] == ' ' || magic[0] == 0x09U) && (magic[1] == 'N' || magic[1] == 'n' || magic[2] == 'N' || magic[2] == 'n'))));
    bool is_stuffit = (magic_size >= 14U && xx_rt_memcmp(magic + 10, "rLau", 4U) == 0 && (xx_rt_memcmp(magic, "SIT!", 4U) == 0 || xx_rt_memcmp(magic, "ST46", 4U) == 0 || xx_rt_memcmp(magic, "ST50", 4U) == 0 || xx_rt_memcmp(magic, "ST60", 4U) == 0 || xx_rt_memcmp(magic, "ST65", 4U) == 0 || xx_rt_memcmp(magic, "STin", 4U) == 0 || xx_rt_memcmp(magic, "STi2", 4U) == 0 || xx_rt_memcmp(magic, "STi3", 4U) == 0 || xx_rt_memcmp(magic, "STi4", 4U) == 0));
    bool is_binaryii = (magic_size >= 0x13U && magic[0] == 0x0AU && magic[1] == 0x47U && magic[2] == 0x4CU && magic[0x12] == 0x02U);
    bool is_binhex = (magic_size >= 40U && xx_rt_memcmp(magic, "(This file must be converted with BinHex", 40U) == 0);
    bool is_pma = (magic_size >= 22U && magic[2] == '-' && magic[3] == 'p' && magic[4] == 'm' && magic[5] >= '0' && magic[5] <= '2' && magic[6] == '-' && magic[20] == 0U);
    bool is_lzk00 = (magic_size >= 9U && xx_rt_memcmp(magic, "LZK00", 5U) == 0 && magic[5] == 0U && magic[6] == 0U && magic[7] == 0U && magic[8] == 0U);
    bool is_compaqlzh = (magic_size >= 29U && xx_rt_memcmp(magic, "CPQ_LZH", 7U) == 0);
    bool is_arcv = (magic_size >= 6U && magic[0] == 'A' && magic[1] == 'R' && magic[2] == 'C' && magic[3] == 'V' &&
                    (magic[4] == 0x00U || magic[4] == 0x10U) && magic[5] == 0x01U);
    bool is_cpoint = magic_size >= 13U && magic[0] == 0x7cU &&
                     magic[1] == 0U && magic[2] == 0U && magic[3] == 0U &&
                     magic[4] == 0U;
    bool is_izpack = (magic_size >= 41U && magic[0] == 0xACU && magic[1] == 0xEDU && magic[2] == 0x00U && magic[3] == 0x05U && magic[4] == 0x77U && magic[5] == 0x04U && magic[10] == 0x73U && magic[11] == 0x72U && magic[12] == 0x00U && magic[13] == 0x1BU && xx_rt_memcmp(magic + 14, "com.izforge.izpack.PackFile", 27U) == 0);
    bool is_is11 = (magic_size >= 8U && magic[0] == 0x65U && magic[1] == 0x5DU && magic[2] == 0x13U && magic[3] == 0x8CU && magic[4] == 0x08U && magic[5] == 0x01U && (magic[6] == 0x01U || magic[6] == 0x03U) && magic[7] == 0x00U);
    bool is_gksetup = (magic_size >= 39U && xx_rt_memcmp(magic, "This is a binary data file. Keep out !\x1A", 39U) == 0);
    bool is_copyqm = (magic_size >= 3U && magic[0] == 'C' && magic[1] == 'Q' && magic[2] == 0x14U);
    bool is_teledisk = (magic_size >= 5U && ((magic[0] == 'T' && magic[1] == 'D') || (magic[0] == 't' && magic[1] == 'd')) && magic[2] == 0U && magic[4] >= 10U && magic[4] <= 21U);
    bool is_hfe = (magic_size >= 9U && xx_rt_memcmp(magic, "HXCPICFE", 8U) == 0 && magic[8] == 0U);
    bool is_fdi = (magic_size >= 4U && xx_rt_memcmp(magic, "FDI", 3U) == 0 && magic[3] == 0U);
    bool is_twoimg = (magic_size >= 4U && xx_rt_memcmp(magic, "2IMG", 4U) == 0);
    bool is_imd = (magic_size >= 4U && xx_rt_memcmp(magic, "IMD ", 4U) == 0);
    bool is_diskdupe = (magic_size >= 21U && xx_rt_memcmp(magic, "MSD Image Version 1 \x1A", 21U) == 0);
    bool is_pmdiskcopy = (magic_size >= 11U && xx_rt_memcmp(magic, "PM Diskcopy", 11U) == 0);
    bool is_pax = (magic_size >= 38U && xx_rt_memcmp(magic, "LZF0", 4U) == 0);
    bool is_beospkg = (magic_size >= 8U && magic[0] == 0x41U && magic[1] == 0x6CU && magic[2] == 0x42U && magic[3] == 0x1AU && magic[4] == 0xFFU && magic[5] == 0x0AU && magic[6] == 0x0DU && magic[7] == 0x00U);
    bool is_vmspcsi = (magic_size >= 32U && xx_rt_memcmp(magic, "OpenVMS DCX PCSI Compressed File", 32U) == 0);
    bool is_vmsdb = (magic_size >= 12U && magic[0] == 0xffU && magic[1] == 0xffU && magic[2] == 0x74U && magic[3] == 0x80U && magic[4] == 0xa0U && magic[5] == 0x80U && magic[6] == 0x80U && magic[7] == 0x01U && magic[8] == 0x01U && magic[9] == 0x81U && magic[10] == 0x01U && magic[11] == 0x00U);
    bool is_pcxlib = (magic_size >= 7U && xx_rt_memcmp(magic, "pcxLib", 6U) == 0 && magic[6] == 0U);
    bool is_hog2 = (magic_size >= 4U && xx_rt_memcmp(magic, "HOG2", 4U) == 0);
    bool is_sinner = (magic_size >= 10U && xx_rt_memcmp(magic, "|CCTfs2.0|", 10U) == 0);
    bool is_psn = (magic_size >= 53U && xx_rt_memcmp(magic, "PSNcompress-Copyright", 21U) == 0);
    bool is_notetab = (magic_size >= 5U && magic[0] == '=' && magic[1] == ' ' && magic[2] == 'V' && (magic[3] == '4' || magic[3] == '5') && magic[4] == ' ');
    bool is_mcc = (magic_size >= 4U && xx_rt_memcmp(magic, "MCC", 3U) == 0 && (magic[3] == 0U || magic[3] == 1U));
    bool is_tnef = (magic_size >= 4U && magic[0] == 0x78U && magic[1] == 0x9FU && magic[2] == 0x3EU && magic[3] == 0x22U);
    bool is_opc = (magic_size >= 8U && xx_rt_memcmp(magic, "OS2POINT", 8U) == 0);
    bool is_qrst = (magic_size >= 8U && xx_rt_memcmp(magic, "QRST", 4U) == 0 && magic[4] == 0U && magic[5] == 0U && magic[6] == 0x80U && magic[7] == 0x3FU);
    bool is_pain = (magic_size >= 8U && xx_rt_memcmp(magic, "CRDATA00", 8U) == 0);
    bool is_xlas = (magic_size >= 4U && xx_rt_memcmp(magic, "XLAS", 4U) == 0);
    bool is_mdcd = (magic_size >= 6U && xx_rt_memcmp(magic, "MDmd", 4U) == 0 && magic[5] == 1U);
    bool is_ssm = (magic_size >= 4U && xx_rt_memcmp(magic, "SSM", 3U) == 0 && magic[3] == 0U);
    bool is_bvrp = (magic_size >= 23U && xx_rt_memcmp(magic, "PAC - ", 6U) == 0 &&
                    (xx_rt_memcmp(magic + 10, "BVRP Software", 13U) == 0 ||
                     (magic[6] == 0xa9U && magic[7] == ' ' &&
                      xx_rt_memcmp(magic + 8, "BVRP Software", 13U) == 0)));
    bool is_bcw = (magic_size >= 5U && magic[0] == 0x0aU && magic[1] == 0x14U && magic[2] == 0x1eU && magic[3] == 0x28U && (magic[4] == 1U || magic[4] == 2U));
    bool is_scf = (magic_size >= 4U && magic[0] == 4U && magic[1] == 0U && magic[2] == 0U && magic[3] == 0U);
    bool is_recognita = (magic_size >= 27U && magic[25] == 0x00U && magic[26] == 0x06U);
    bool is_jasc = (magic_size >= 17U && magic[16] != 0U && magic[0] == (uint8_t)(magic[16] + 15U));
    bool is_smsipak = (magic_size >= 10U && xx_rt_memcmp(magic, "SMSIPAK ", 8U) == 0 && magic[8] == 0x07U && magic[9] == 0x1aU);
    bool is_cpx = (magic_size >= 6U && magic[1] == 0x16U && magic[2] == 0x27U && magic[3] == 0x93U && (magic[0] == 0x28U || magic[0] == 0x2aU || magic[0] == 0x2cU));
    bool is_diskexpress = (magic_size >= 15U && magic[0] == 'A' && magic[1] == 'S' && ((magic[2] == 1U && (magic[3] == 1U || magic[3] == 4U)) || (magic[2] == 2U && (magic[3] == 0U || magic[3] == 30U))) && (magic[4] == 0x20U || magic[4] == 'A' || magic[4] == 'a') && magic[5] >= 3U && magic[5] <= 7U);
    bool is_red = (magic_size >= 4U && magic[0] == 'R' && magic[1] == 'R' && magic[2] == 1U && magic[3] >= 39U);
    bool is_gxl = (magic_size >= 54U && magic[0] == 0x01U && magic[1] == 0xCAU && xx_rt_memcmp(magic + 2, "Copyri", 6U) == 0 && magic[52] == 100U && magic[53] == 0U);
    bool is_aiaff = (magic_size >= 8U && xx_rt_memcmp(magic, "<aiaff>", 7U) == 0 && magic[7] == 0x0AU);
    bool is_softpaq2 = (magic_size >= 2U && magic[0] == 'M' && magic[1] == 'Z');
    bool is_wim = (magic_size >= 8U && xx_rt_memcmp(magic, "MSWIM", 5U) == 0);
    bool is_vhddynamic = total_size >= 1023 && (total_size % 512 == 0 || total_size % 512 == 511);
    bool is_vmdk = (magic_size >= 4U && xx_rt_memcmp(magic, "KDMV", 4U) == 0);
    bool is_ciso = (magic_size >= 4U &&
                    (xx_rt_memcmp(magic, "CISO", 4U) == 0 ||
                     xx_rt_memcmp(magic, "ZISO", 4U) == 0 ||
                     xx_rt_memcmp(magic, "DAX\0", 4U) == 0));
    bool is_copydisk = (magic_size >= 10U && xx_rt_memcmp(magic, "COPYDISK", 8U) == 0);
    bool is_hdcopy = (magic_size >= 16U && magic[0] == 0xffU && magic[1] == 0x18U);
    bool is_apricot = (magic_size >= 22U && xx_rt_memcmp(magic, "ACT Apricot disk image", 22U) == 0);
    bool is_sabdu = (magic_size >= 26U && xx_rt_memcmp(magic, "SAB Diskette Utility", 20U) == 0);
    bool is_mpq = (magic_size >= 4U && magic[0] == 'M' && magic[1] == 'P' && magic[2] == 'Q' && magic[3] == 0x1aU);
    bool is_phar = (magic_size >= 5U && xx_rt_memcmp(magic, "<?php", 5U) == 0);
    bool is_sq = (magic_size >= 4U && magic[0] == 0x53U && magic[1] == 0x51U && magic[2] == 0xacU && magic[3] == 0xaeU);
    bool is_squeeze2 = (magic_size >= 2U && magic[0] == 0xfaU && magic[1] == 0xffU);
    bool is_dbz = (magic_size >= 27U && xx_rt_memcmp(magic, "!<man database compressed>", 26U) == 0);
    bool is_stac = (magic_size >= 6U && xx_rt_memcmp(magic, "sTaC", 4U) == 0);
    bool is_spk = (magic_size >= 0x2aU && magic[0] == 0x1aU && (((magic[1] >= 0x81U) && (magic[1] <= 0x89U)) || magic[1] == 0xffU));
    bool is_wrzl = (magic_size >= 12U && xx_rt_memcmp(magic, "WRZL", 4U) == 0);
    bool is_bagf = (magic_size >= 12U && xx_rt_memcmp(magic, "BAGF", 4U) == 0 && magic[4] == 2U && magic[5] == 0U);
    bool is_emt = (magic_size >= 6U && magic[0] == 0x5cU && magic[2] == 0x7aU && magic[3] == 0xc5U && magic[4] == 0xd4U && magic[5] == 0xe3U);
    bool is_qip2 = (magic_size >= 4U && magic[0] == 'Q' && magic[1] == 'P');
    bool is_lif = (magic_size >= XX_LIF_HEADER_SIZE && magic[0] == 0x44U &&
                   (magic[1] == 0x43U || magic[1] == 0x4cU) &&
                   magic[2] == XX_LIF_VERSION && magic[3] == 0U &&
                   (magic[4] != 0U || magic[5] != 0U) &&
                   xx_data_get_u32(magic + 0x15, 4, 0, false) == (uint64_t)total_size &&
                   xx_data_get_u32(magic + 0x19, 4, 0, false) == XX_LIF_METHOD &&
                   xx_data_get_u32(magic + 0x1d, 4, 0, false) ==
                       (uint64_t)(total_size - XX_LIF_HEADER_SIZE) &&
                   xx_data_get_u32(magic + 0x21, 4, 0, false) == 0U);
    bool is_ixa = (magic_size >= 48U && xx_rt_memcmp(magic, "IXALANCE", 8U) == 0);
    bool is_mlb_ft = (magic_size >= 4U && magic[2] == 6U && magic[3] == 0U);
    bool is_fss = (magic_size >= 5U && xx_rt_memcmp(magic, "SSBOB", 5U) == 0);
    bool is_epf = (magic_size >= 11U && xx_rt_memcmp(magic, "EPFS", 4U) == 0);
    bool is_ka = (magic_size >= 11U &&
                  xx_rt_memcmp(magic, "KA Archive\0", 11U) == 0);
    bool is_dn = (magic_size >= 4U && magic[0] == 0x84U &&
                  magic[1] == 0x8dU && magic[2] == 0x01U && magic[3] == 0x02U);
    bool is_insa = (magic_size >= 6U && total_size >= 7 &&
                    total_size <= 16 * 1024 * 1024 &&
                    magic[0] == 1U && magic[1] == 0U &&
                    (magic[2] | magic[3] | magic[4] | magic[5]) != 0U &&
                    magic[5] <= 4U);
    bool is_dfc = (magic_size >= 35U && magic[4] >= 1U && magic[4] <= 12U &&
                   ((uint32_t)magic[17] | ((uint32_t)magic[18] << 8U) |
                    ((uint32_t)magic[19] << 16U) | ((uint32_t)magic[20] << 24U)) ==
                   (((uint32_t)magic[0] | ((uint32_t)magic[1] << 8U)) * 35U + 4U));
    bool is_ppd = (magic_size >= 9U && total_size >= 22 &&
                   total_size <= 512 * 1024 * 1024 &&
                   ((uint32_t)magic[0] | ((uint32_t)magic[1] << 8U) |
                    ((uint32_t)magic[2] << 16U) |
                    ((uint32_t)magic[3] << 24U)) >= 1U &&
                   ((uint32_t)magic[0] | ((uint32_t)magic[1] << 8U) |
                    ((uint32_t)magic[2] << 16U) |
                    ((uint32_t)magic[3] << 24U)) <= 4096U &&
                   magic[5] == 0U && magic[6] == 0U && magic[7] == 0U &&
                   magic[4] >= 2U && magic[4] <= 95U &&
                   magic[8] >= 0x20U && magic[8] <= 0x7eU);
    bool is_thebat_msb = (magic_size >= 12U &&
                         magic[0] == 0x40U && magic[1] == 0U &&
                         magic[2] == 0U && magic[3] == 0U &&
                         magic[4] == 0x40U && magic[5] == 0U &&
                         magic[6] == 0U && magic[7] == 0U &&
                         magic[8] == 0xffU && magic[9] == 0xffU &&
                         magic[10] == 0xffU && magic[11] == 0xffU);
    bool is_lspack10 = (magic_size >= 40U && magic[0] == 'F' && magic[1] == 'L' && magic[2] == 0x03U);
    bool is_starkit = (magic_size >= 8U && magic[0] == 'J' && magic[1] == 'L' && magic[2] == 0x1aU && magic[3] == 0x00U);
    bool is_paperport = (magic_size >= 6U && magic[0] == 'V' && magic[1] == 'i' && magic[2] == 'G');
    bool is_rnca = (magic_size >= 12U && xx_rt_memcmp(magic, "RNCA", 4U) == 0);
    bool is_hog = (magic_size >= 3U && xx_rt_memcmp(magic, "DHF", 3U) == 0);
    bool is_agis = (magic_size >= 19U && xx_rt_memcmp(magic, "AGIS", 4U) == 0 && magic[4] == 0x10U);
    bool is_volitionvpft = (magic_size >= 16U && xx_rt_memcmp(magic, "VPVP", 4U) == 0 && magic[4] == 2U && magic[5] == 0U && magic[6] == 0U && magic[7] == 0U);
    bool is_wintermutedcp = (magic_size >= 12U && magic[0] == 0xdeU && magic[1] == 0xadU && magic[2] == 0xc0U && magic[3] == 0xdeU);
    bool is_bsn = (magic_size >= 6U && magic[0] == 0xffU && magic[1] == 'B' && magic[2] == 'S');
    bool is_androidboot = magic_size >= 8U && xx_rt_memcmp(magic, "ANDROID!", 8U) == 0;
    bool is_autel = magic_size >= 32 && xx_rt_memcmp(magic, "ECC0101\0", 8U) == 0 && magic[12] == 0x20U && magic[13] == 0U && magic[14] == 0U && magic[15] == 0U && xx_rt_memcmp(magic + 16U, "Copyright Autel\0", 16U) == 0;
    bool is_dkbs = magic_size >= 13U && total_size > 0xA0 && xx_rt_memcmp(magic + 7U, "_dkbs_", 6U) == 0;
    bool is_dlink_tlv = magic_size >= 0x25U && total_size > 0x74 && magic[0]==0x64U && magic[1]==0x80U && magic[2]==0x19U && magic[3]==0x40U && magic[4] >= 0x20U && magic[4] <= 0x7EU && magic[0x24] >= 0x20U && magic[0x24] <= 0x7EU;
    bool is_dlke = magic_size >= 64U && (xx_rt_memcmp(magic, "DLK6E8202001", 12U) == 0 || xx_rt_memcmp(magic, "DLK6E6110002", 12U) == 0);
    bool is_ecos = magic_size >= 8 && ((magic[0] == 0x40U && magic[1] == 0x1AU && magic[2] == 0x68U && magic[3] == 0x00U) || (magic[0] == 0x00U && magic[1] == 0x68U && magic[2] == 0x1AU && magic[3] == 0x40U));
    bool is_encrpted_img = magic_size >= 17U && xx_rt_memcmp(magic, "encrpted_img", 12U) == 0;
    bool is_jboot = ((magic_size >= 40 && magic[0] == 0x24U && magic[1] == 0x21U && magic[2] <= 3U && magic[3] == 2U && magic[36] == 40U && magic[37] == 0U) || (magic_size >= 16 && magic[1] == 4U && magic[2] == 0x24U && magic[3] == 0x2BU && (magic[0] == 4U || magic[0] == 0xFFU)) || (magic_size >= 64 && total_size >= 80 && (magic[20] | magic[21] | magic[22] | magic[23] | magic[24] | magic[25] | magic[27]) == 0U && magic[26] == 1U && (magic[48] | magic[49] | magic[50] | magic[51] | magic[52] | magic[53] | magic[54] | magic[55] | magic[56] | magic[57] | magic[58] | magic[59] | magic[60] | magic[61] | magic[62] | magic[63]) == 0U));
    bool is_lingvoarc = ((magic_size >= 18 && xx_rt_memcmp(magic, "lingvoArc", 9) == 0 && (magic[9] == '1' || magic[9] == '2') && magic[10] == 0x00 && magic[11] == 0xFD && magic[12] == 0x00 && magic[13] == 0xDF && magic[14] == 0x00 && magic[15] == 0xFF && (magic[16] != 0 || magic[17] != 0)) || (magic_size >= 26 && xx_rt_memcmp(magic, "LingvoArch", 10) == 0 && magic[10] == 0x01 && magic[11] == 0x00 && magic[12] == 0xF0 && magic[13] == 0x1F && magic[14] == 0x00 && magic[15] == 0x01 && magic[16] == 0x40 && magic[17] == 0x00 && magic[18] == 0x47 && magic[19] == 0x01 && magic[20] == 0x47 && magic[21] == 0x01 && magic[22] == 0x1F && magic[23] == 0x83 && magic[24] == 0x41 && magic[25] == 0x01));
    bool is_lz4demo = magic_size >= 4 && magic[0] == 0x02 && magic[1] == 0x21 && magic[2] == 0x4C && magic[3] == 0x18;
    bool is_matter_ota = magic_size >= 17U && magic[0] == 0x1EU && magic[1] == 0xF1U && magic[2] == 0xEEU && magic[3] == 0x1BU && magic[16] == 0x15U;
    bool is_mh01 = magic_size >= 32U && xx_rt_memcmp(magic, "MH01", 4U) == 0 && xx_rt_memcmp(magic + 16U, "MH01", 4U) == 0;
    bool is_shrs = magic_size >= 12U && xx_rt_memcmp(magic, "SHRS", 4U) == 0;
    bool is_silmarilsft = magic_size >= 15U && ((magic[4] == 0x01U && magic[5] == 0x00U && (magic[0] & 0x07U) == 0x06U && (magic[3] == 0x81U || (magic[3] == 0xa1U && magic[6] == 0x0bU && magic[7] == 0x09U))) || (magic[4] == 0x00U && magic[5] == 0x01U && (magic[3] & 0x07U) == 0x06U && (magic[0] == 0x81U || (magic[0] == 0xa1U && magic[6] == 0x0bU && magic[7] == 0x09U))));
    bool is_tplink = magic_size >= 24U && (xx_rt_memcmp(magic + 4U, "TP-LINK Technologies", 20U) == 0 || (magic[0] == 0x00U && magic[1] == 0x14U && magic[2] == 0x2FU && magic[3] == 0xC0U && xx_rt_memcmp(magic + 20U, "IMG0", 4U) == 0));
    bool is_twrx = magic_size >= 30U && xx_rt_memcmp(magic, "TWRX", 4U) == 0 && magic[4] == 0U && magic[5] == 1U && magic[6] == 0U && magic[7] == 0U && magic[0x1aU] != 0U && magic[0x1bU] == 0U && magic[0x1cU] == 0U && magic[0x1dU] == 0U;
    bool is_infogramesft = xx_infogramesft_test_magic(magic, magic_size, total_size);
    bool is_xpak = magic_size >= 36 && xx_rt_memcmp(magic, "XPAK", 4U) == 0 && magic[0x1A] == 0xFFU && magic[0x1B] == 0xFEU;
    bool is_srec = magic_size >= 10U && magic[0] == 0x53U && xx_srec_check_magic(magic, magic_size);
    bool is_apple_disk_copy_6_ndif_image = magic_size >= 3 && total_size >= 174 && magic[0] == 0x00U && magic[1] >= 1U && magic[1] <= 63U && magic[2] >= 0x20U;
    bool is_apple_sparse_bundle = (total_size >= 64 && total_size <= 65536 && magic_size >= 16 && (xx_rt_memcmp(magic, "<?xml", 5) == 0 || xx_rt_memcmp(magic, "\xEF\xBB\xBF<?xml", 8) == 0 || xx_rt_memcmp(magic, "<!DOCTYPE plist", 15) == 0 || xx_rt_memcmp(magic, "<plist", 6) == 0));
    bool is_encrypted_apple_disk_image = magic_size >= 12U && xx_rt_memcmp(magic, "encrcdsa", 8U) == 0 && magic[8] == 0U && magic[9] == 0U && magic[10] == 0U && magic[11] == 2U;
    bool is_hxc_stream_hfe = (magic_size >= 16U && xx_rt_memcmp(magic, "HxC_Stream_Image", 16U) == 0);
    bool is_ms_dos_backup = magic_size >= 7 && total_size >= 128 && (magic[0] == 0x00 || magic[0] == 0xFF) && magic[1] != 0 && magic[2] == 0 && magic[3] == 0 && magic[4] == 0 && (magic[5] == 0x5C || magic[5] == 0x2F) && magic[6] >= 0x20;
    bool is_nec_pc_98_fdi = (magic_size >= 32U && magic[0] == 0U && magic[1] == 0U && magic[2] == 0U && magic[3] == 0U && (magic[8] | magic[9] | magic[10]) != 0U && magic[10] <= 1U && magic[11] == 0U && magic[15] == 0U && magic[18] == 0U && magic[19] == 0U && ((magic[16] == 0x80U && magic[17] == 0U) || (magic[16] == 0U && (magic[17] == 1U || magic[17] == 2U || magic[17] == 4U || magic[17] == 8U || magic[17] == 16U || magic[17] == 32U || magic[17] == 64U))) && magic[20] != 0U && magic[21] == 0U && magic[22] == 0U && magic[23] == 0U && (magic[24] == 1U || magic[24] == 2U) && magic[25] == 0U && magic[26] == 0U && magic[27] == 0U && magic[28] != 0U && magic[29] == 0U && magic[30] == 0U && magic[31] == 0U);
    bool is_qcow1 = magic_size >= 48U && magic[0]==0x51U && magic[1]==0x46U && magic[2]==0x49U && magic[3]==0xFBU && magic[4]==0U && magic[5]==0U && magic[6]==0U && magic[7]==1U;
    bool is_qnap_nas_firmware = magic_size >= 4U && magic[0] == 0xF5U && magic[1] == 0x7BU && magic[2] == 0x47U && magic[3] == 0x03U;
    bool is_stuffit_split_file = (magic_size >= 5 && magic[0] == 0xB0 && magic[1] == 0x56 && magic[2] == 0x00 && magic[3] != 0 && magic[4] >= 1 && magic[4] <= 63 && total_size >= 100);
    bool is_t98_next_nfd = (magic_size >= 15U && magic[0] == 0x54U && magic[1] == 0x39U && magic[2] == 0x38U && magic[3] == 0x46U && magic[4] == 0x44U && magic[5] == 0x44U && magic[6] == 0x49U && magic[7] == 0x4DU && magic[8] == 0x41U && magic[9] == 0x47U && magic[10] == 0x45U && magic[11] == 0x2EU && magic[12] == 0x52U && (magic[13] == 0x30U || magic[13] == 0x31U) && magic[14] == 0U);
    bool is_uharc = magic_size >= 16 && magic[0] == 0x55 && magic[1] == 0x48 && magic[2] == 0x41 && magic[3] >= 0x01 && magic[3] <= 0x06;
    bool is_visionaire_studio_vis = magic_size >= 8 && magic[0] == 0x56 && magic[1] == 0x49 && magic[2] == 0x53 && magic[3] == 0x33;
    bool is_xamarin_compressed_assembly = magic_size >= 13 && magic[0] == 0x58 && magic[1] == 0x41 && magic[2] == 0x4C && magic[3] == 0x5A && (magic[8] | magic[9] | magic[10] | magic[11]) != 0 && magic[11] < 0x10 && total_size >= 13;
    bool is_dms = magic_size >= 4U && magic[0]==0x44U && magic[1]==0x4DU &&
        magic[2]==0x53U && magic[3]==0x21U;   /* "DMS!" */
    bool is_csman = magic_size >= 2U &&
        ((magic[0]==0x43U && magic[1]==0x53U) || (magic[0]==0x53U && magic[1]==0x43U));
    bool is_uefi_capsule = total_size >= 32;
    bool is_cramfs = magic_size >= 4U &&
        ((magic[0]==0x45U&&magic[1]==0x3DU&&magic[2]==0xCDU&&magic[3]==0x28U) ||
         (magic[0]==0x28U&&magic[1]==0xCDU&&magic[2]==0x3DU&&magic[3]==0x45U));
    bool is_ubi = magic_size >= 4U && magic[0]==0x55U && magic[1]==0x42U && magic[2]==0x49U && magic[3]==0x23U;
    bool is_ubifs = magic_size >= 21U && magic[0]==0x31U && magic[1]==0x18U &&
        magic[2]==0x10U && magic[3]==0x06U && magic[20]==0x06U;
    bool is_sparse = magic_size >= 4U && magic[0]==0x3AU && magic[1]==0xFFU && magic[2]==0x26U && magic[3]==0xEDU;
    bool is_uimage = magic_size >= 4U && magic[0]==0x27U && magic[1]==0x05U && magic[2]==0x19U && magic[3]==0x56U;
    bool is_dtb = magic_size >= 4U && magic[0]==0xD0U && magic[1]==0x0DU && magic[2]==0xFEU && magic[3]==0xEDU;
    bool is_jffs2 = magic_size >= 4U &&
        ((magic[0]==0x85U && magic[1]==0x19U) || (magic[0]==0x19U && magic[1]==0x85U));
    bool is_fat = magic_size >= 22U &&
        ((magic[0]==0xEBU && magic[2]==0x90U) || magic[0]==0xE9U) &&
        ((uint16_t)(magic[14] | (magic[15] << 8)) != 0U) &&
        (magic[16]==1U || magic[16]==2U) &&
        (magic[21]==0xF0U || magic[21]>=0xF8U);
    /* Magic past the 64-byte window: ext's superblock is at 1024, GPT's
     * header at 512, MBR's signature at 510. All three are device probes. */
    bool is_ext = total_size > 2048;
    bool is_gpt = total_size > 1024;
    bool is_mbr = total_size >= 512;
    bool is_squashfs = magic_size >= 32U &&
        ((magic[0]==0x68U&&magic[1]==0x73U&&magic[2]==0x71U&&magic[3]==0x73U) ||
         (magic[0]==0x73U&&magic[1]==0x71U&&magic[2]==0x73U&&magic[3]==0x68U) ||
         (magic[0]==0x68U&&magic[1]==0x73U&&magic[2]==0x71U&&magic[3]==0x74U) ||
         (magic[0]==0x73U&&magic[1]==0x68U&&magic[2]==0x73U&&magic[3]==0x71U));
    bool is_ntfs = magic_size >= 11U && xx_rt_memcmp(magic + 3U, "NTFS    ", 8U) == 0;
    bool is_romfs = magic_size >= 8U && xx_rt_memcmp(magic, "-rom1fs-", 8U) == 0;
    bool is_sqz = magic_size >= 5U && xx_rt_memcmp(magic, "HLSQZ", 5U) == 0;
    bool is_tps = magic_size >= 5U && magic[0]==0x54U && magic[1]==0x50U &&
                  magic[2]==0x53U && magic[3]==0x1AU && magic[4]==0x02U;
    bool is_ulead = magic_size >= 12U && xx_rt_memcmp(magic, "U_LEAD CORP.", 12U) == 0;
    bool is_quantum = magic_size >= 3U && magic[0]==0x44U && magic[1]==0x53U && magic[2]==0x00U;
    bool is_zxzip = magic_size >= 11U && magic[8]==0x5AU && magic[9]==0x49U && magic[10]==0x50U;
    bool is_zoom = magic_size >= 7U && xx_rt_memcmp(magic, "ZOM5", 4U) == 0 && magic[6]==0x05U;
    bool is_sfpack = magic_size >= 6U && xx_rt_memcmp(magic, "SFPK", 4U) == 0 && magic[4]==0x00U && magic[5]==0x01U;
    bool is_claylz = magic_size >= 4U && xx_rt_memcmp(magic, "Clay", 4U) == 0;
    bool is_c64wraptor = magic_size >= 4U && magic[0]==0xFFU && magic[1]==0x42U && magic[2]==0x4CU && magic[3]==0xFFU;
    bool is_corelltec = magic_size >= 9U && xx_rt_memcmp(magic, "LTEC", 4U) == 0 && magic[8]==0x00U;
    bool is_pcsecure = magic_size >= 4U &&
        (xx_rt_memcmp(magic, "PCT5", 4U) == 0 || xx_rt_memcmp(magic, "PCT6", 4U) == 0 ||
         xx_rt_memcmp(magic, "PCT7", 4U) == 0 || xx_rt_memcmp(magic, "AfoS", 4U) == 0);
    bool is_rsvk = magic_size >= 8U &&
        (xx_rt_memcmp(magic, "RSVKDATA", 8U) == 0 || xx_rt_memcmp(magic, "DLIBDATA", 8U) == 0);
    bool is_saf = magic_size >= 8U && xx_rt_memcmp(magic, "SAF, (c)", 8U) == 0;
    bool is_sls = magic_size >= 9U && magic[0]==0x1FU && magic[1]==0x53U &&
                  magic[2]==0x2FU && magic[3]==0x4CU && magic[4]==0x3FU &&
                  magic[5]==0x53U && magic[6]==0x4FU && magic[7]==0x41U &&
                  magic[8]==0x5FU;
    bool is_nid = magic_size >= 4U && magic[0]==0x4EU && magic[1]==0x49U && magic[2]==0x15U && magic[3]==0x01U;
    bool is_gamos = magic_size >= 18U && magic[0]==0x1AU && xx_rt_memcmp(magic + 1U, "GAMOS PACKED FILE", 17U) == 0;
    bool is_fpak = magic_size >= 4U &&
        (xx_rt_memcmp(magic, "FPAK", 4U) == 0 || xx_rt_memcmp(magic, "FPAC", 4U) == 0);
    /* UDF's recognition sequence lives at offset 32768, far past the
     * 64-byte magic window, so it is probed on the device like ISO9660. */
    bool is_udf = xx_udf_device_has_recognition_sequence(dev, 0);
    bool is_terse = magic_size >= 16;
    bool is_stk = magic_size >= 16;
    bool is_pcommos2 = magic_size >= 16;
    bool is_jbf = magic_size >= 16 && magic[0x0] == 0xe4U && magic[0x1] == 0x63U && magic[0x2] == 0x31U && magic[0x3] == 0x30U && magic[0x4] == 0xb3U && magic[0x5] == 0x70U && magic[0x6] == 0xb4U && magic[0x7] == 0x5cU;
    bool is_ibmspack = magic_size >= 8 && magic[0] == 0x53U;
    bool is_gtu = magic_size >= 16;
    bool is_glu = magic_size >= 16;
    bool is_ztc = magic_size >= 0x16 && magic[0x0] == 0xd6U && magic[0x1] == 0xbbU && magic[0x2] == 0xabU && magic[0x3] == 0x01U;
    bool is_netwarepacked = magic_size >= 0x20 && xx_rt_memcmp(magic, "Packed File ", 12U) == 0 && magic[24] == 0x1aU;
    bool is_zpak = magic_size >= 6 && (xx_rt_memcmp(magic, "zpak", 4U) == 0 ||
                        xx_rt_memcmp(magic, "zpk2", 4U) == 0);
    bool is_zcmp = magic_size >= 0x28 && magic[0x0] == 0x00U && magic[0x1] == 0x00U && magic[0x2] == 0x00U && magic[0x3] == 0x00U &&
                   xx_rt_memcmp(magic + 0x4U, "Zcmp", 4U) == 0;
    bool is_scl = magic_size >= 0x17 && xx_rt_memcmp(magic, "SINCLAIR", 8U) == 0 && magic[8] != 0U;
    bool is_pakleo = magic_size >= 0x3f &&
                     xx_rt_memcmp(magic, "LEOLZW - (c) Leonardus Leonardi 1993", 36U) == 0;
    bool is_npack = magic_size >= 8 && xx_rt_memcmp(magic, "MSTSM", 5U) == 0;
    bool is_mi10 = magic_size >= 16 && xx_rt_memcmp(magic, "MI10", 4U) == 0;
    bool is_lzwd = magic_size >= 11 && magic[0x4] == 0xfcU && magic[0x5] == 0x4cU && magic[0x6] == 0x5aU && magic[0x7] == 0x57U;
    bool is_lzhcxp = magic_size >= 8 && xx_rt_memcmp(magic, "LZ", 2U) == 0 && magic[2] != 0U &&
                     magic[3] == 0U;
    bool is_kboom = magic_size >= 8 && magic[0x0] == 0xa8U && magic[0x1] == 0x4dU && magic[0x2] == 0x50U && magic[0x3] == 0xa8U;
    bool is_hzl = magic_size >= 12 && xx_rt_memcmp(magic, "!HZL", 4U) == 0 && magic[8] == '.';
    bool is_ha = magic_size >= 0x15 && xx_rt_memcmp(magic, "HA", 2U) == 0;
    bool is_genius = magic_size >= 0x20 && xx_rt_memcmp(magic, "GENIUS LIBRARY", 14U) == 0 &&
                     magic[0x0e] == 0U;
    bool is_fls = magic_size >= 0x2e && magic[0x2] == 0xfeU && magic[0x3] == 0x00U;
    bool is_earefpack = magic_size >= 6 && (magic[0] & 0x7eU) == 0x10U &&
                        magic[1] == 0xfbU;
    bool is_ealib = magic_size >= 0x14 && xx_rt_memcmp(magic, "EALIB", 5U) == 0;
    bool is_elm = magic_size >= 12 && magic[0] >= '0' && magic[0] <= '9' &&
                  magic[1] == '.' && magic[2] >= '0' && magic[2] <= '9' &&
                  magic[3] == '.';
    bool is_ea = magic_size >= 0x30 && magic[0] == 0x1aU && xx_rt_memcmp(magic + 0x1U, "EA", 2U) == 0;
    bool is_diskdoubler = magic_size >= 4U && ((total_size >= 0x54 && magic[0x0] == 0xabU && magic[0x1] == 0xcdU && magic[0x2] == 0x00U && magic[0x3] == 0x54U) || (total_size >= 0x44 && magic_size >= 6U && magic[0x0] == 0x44U && magic[0x1] == 0x44U && magic[0x2] == 0x41U && magic[0x3] == 0x32U && magic[0x4] == 0x00U && magic[0x5] == 0x3eU) || (total_size >= 0x4e && magic[0x0] == 0x44U && magic[0x1] == 0x44U && magic[0x2] == 0x41U && magic[0x3] == 0x52U));
    bool is_cmp = magic_size >= 0x3d && magic[0x0] == 0x7fU && magic[0x1] == 0x00U;
    bool is_clp = total_size >= 0x5d && magic_size >= 2U && magic[1] == 0xc3U &&
                  (magic[0] == 0x50U || magic[0] == 0x51U);
    bool is_chieflzmulti = magic_size >= 0x2b && magic[0] == 0x0cU &&
                           magic[0x1] == 0x04U && magic[0x2] == 0x0dU &&
                           xx_rt_memcmp(magic + 0x3U, "ChfLZ_2", 7U) == 0 &&
                           magic[0xa] == 0x05U && magic[0xb] == 0x06U && magic[0xc] == 0x04U;
    bool is_chieflz = magic_size >= 0x20 && magic[0] == 8U && xx_rt_memcmp(magic + 0x1U, "aChiefM#", 8U) == 0;
    bool is_bwcf = total_size >= 0x56 && magic_size >= 5U && xx_rt_memcmp(magic, "BWCF", 4U) == 0 &&
                   (magic[4] == 1U || magic[4] == 2U);
    bool is_asymetrix = magic_size >= 0x2c && magic[0x0] == 0x60U && magic[0x1] == 0x22U && magic[0x2] == 0x13U && magic[0x3] == 0x63U;
    bool is_seaarc = magic_size >= 29 && magic[0] == 0x1aU &&
                     ((magic[1] >= 1U && magic[1] <= 11U) ||
                      magic[1] == 0x7fU);
    bool is_amigalzx = magic_size >= 41 && xx_rt_memcmp(magic, "LZX", 3U) == 0;
    bool is_spis = magic_size >= 16 && xx_rt_memcmp(magic, "SPIS\x1a", 5U) == 0;
    bool is_lha = magic_size >= 22 && magic[2] == '-' && magic[6] == '-' &&
                  ((magic[3] == 'l' &&
                    (magic[4] == 'h' || magic[4] == 'z')) ||
                   (magic[3] == 'p' && magic[4] == 'm'));
    bool is_xar = magic_size >= 28 && xx_rt_memcmp(magic, "xar!", 4U) == 0;
    bool is_fmc1 = magic_size >= 24 && xx_rt_memcmp(magic, "FMC1", 4U) == 0;
    bool is_pyz = magic_size >= 12 && xx_rt_memcmp(magic, "PYZ", 3U) == 0 && magic[3] == 0U;
    bool is_ascendbackup = magic_size >= 0x12 && magic[1] == 0U && magic[0] >= 1U &&
                           magic[0] <= 12U;
    bool is_stork = magic_size >= 0x12 && magic[0] >= 1U && magic[0] <= 12U &&
                    magic[0x11] == '$';
    bool is_zap = magic_size >= 0x15 && magic[0] >= 1U && magic[0] <= 12U;
    bool is_bwf = magic_size >= 22 && magic[0] == 0x01U;
    bool is_qualitas = magic_size >= 14 && magic[4] == 0x0eU && magic[5] == 0x00U;
    bool is_jetbbs = magic_size >= 22 && xx_rt_memcmp(magic + 0x2U, "-mg", 3U) == 0 &&
                     (magic[5] == '0' || magic[5] == '4' || magic[5] == '5') &&
                     magic[6] == '-';
    bool is_ecmpacked = magic_size >= 38 && xx_rt_memcmp(magic, "ECM", 3U) == 0 && magic[3] == 0U;
    bool is_borlandpack = magic_size >= 36 && xx_rt_memcmp(magic, "This is a packed file.", 22U) == 0 &&
                          magic[0x16] == 0x1aU;
    bool is_jgpak = magic_size >= 16 && xx_rt_memcmp(magic, "JGPAK", 5U) == 0 && magic[5] == 0U &&
                    magic[6] == 1U;
    bool is_zzz = magic_size >= 0x18 && xx_rt_memcmp(magic, "ZZZ", 3U) == 0;
    bool is_zz = magic_size >= 0x12 && magic[0x0] == 0x5aU && magic[0x1] == 0x5aU && magic[0x2] == 0x02U && magic[0x3] == 0x00U;
    bool is_zlwb = magic_size >= 0x1e && xx_rt_memcmp(magic, "ZLWB", 4U) == 0 && magic[4] == 0x1aU;
    bool is_trc = magic_size >= 12 && magic[0x0] == 0xb0U && magic[0x1] == 0xb1U && magic[0x2] == 0xb2U &&
                  xx_rt_memcmp(magic + 0x3U, "TRCZip", 6U) == 0 &&
                  magic[0x9] == 0xb2U && magic[0xa] == 0xb1U && magic[0xb] == 0xb0U;
    bool is_tgcf = magic_size >= 0x1c && xx_rt_memcmp(magic, "TGCF", 4U) == 0;
    bool is_swag = magic_size >= 22 && xx_rt_memcmp(magic + 0x2U, "-sw1-", 5U) == 0;
    bool is_riversoft = magic_size >= 32 && xx_rt_memcmp(magic, "RiverSoft Data Library\x1a", 23U) == 0;
    bool is_rcf = magic_size >= 12 && magic[0x0] == 0x03U && magic[0x1] == 0xf7U && magic[0x2] == 0xe8U && magic[0x3] == 0xebU && magic[0x4] == 0x03U &&
                  xx_rt_memcmp(magic + 0x5U, "1.0", 3U) == 0 && magic[8] == 0U &&
                  magic[9] == 0U;
    bool is_quarterdeckqp = magic_size >= 8 && xx_rt_memcmp(magic, "QP", 2U) == 0;
    bool is_qip1 = magic_size >= 0x20 && xx_rt_memcmp(magic, "QD", 2U) == 0 && magic[2] == 0U &&
                   magic[3] == 0U;
    bool is_powerarc = magic_size >= 22 && xx_rt_memcmp(magic, "BZIP0001", 8U) == 0 &&
                       xx_rt_memcmp(magic + 0x8U, "BZh", 3U) == 0;
    bool is_povlablzh = magic_size >= 22 && (xx_rt_memcmp(magic + 0x2U, "-ARS-", 5U) == 0 ||
                         xx_rt_memcmp(magic + 0x2U, "-ARA-", 5U) == 0);
    bool is_mva = magic_size >= 8 && xx_rt_memcmp(magic, "mflh", 4U) == 0 &&
                  magic[0x4] == 0x01U && magic[0x5] == 0x00U && magic[0x6] == 0x00U && magic[0x7] == 0x00U;
    bool is_miz = magic_size >= 14 && xx_rt_memcmp(magic, "DKCL", 4U) == 0;
    bool is_lsz = magic_size >= 6 && magic[0x0] == 0x37U && magic[0x1] == 0xf0U && magic[0x2] == 0xffU && magic[0x3] == 0xffU && magic[0x4] == 0x00U && magic[0x5] == 0x03U;
    bool is_jm93 = magic_size >= 5 && xx_rt_memcmp(magic, "JM93", 4U) == 0 && magic[4] == 0U;
    bool is_inteduft = magic_size >= 6 && magic[0x0] == 0x7cU && magic[0x1] == 0x2eU && magic[0x2] == 0x07U && magic[0x3] == 0x04U;
    bool is_igf2 = magic_size >= 0x28 && magic[0x0] == 0x24U && magic[0x1] == 0x13U;
    bool is_igf1 = magic_size >= 0x38 && magic[0x0] == 0xdbU && magic[0x1] == 0xecU;
    bool is_ibmzpak = magic_size >= 8 && xx_rt_memcmp(magic, "-ZPAK", 5U) == 0 &&
                      magic[0x5] == 0x00U && magic[0x6] == 0x01U && magic[0x7] == 0x00U;
    bool is_fld = magic_size >= 27 && magic[0] == 0x0cU && magic[0x1a] == '$';
    bool is_fiz = magic_size >= 20 && xx_rt_memcmp(magic, "FIZ\x1a", 4U) == 0;
    bool is_dtpacked = magic_size >= 41 && xx_rt_memcmp(magic, "DT", 2U) == 0 &&
                       magic[0x2] == 0x02U && magic[0x3] == 0x00U && magic[0x4] == 0x01U && magic[0x5] == 0x00U;
    bool is_dsl2 = magic_size >= 20 && xx_rt_memcmp(magic, "DS'L install 2.0", 16U) == 0;
    bool is_dpk = magic_size >= 16 && xx_rt_memcmp(magic, "DPK4", 4U) == 0;
    bool is_cfl = magic_size >= 20 && xx_rt_memcmp(magic, "CFL3", 4U) == 0;
    bool is_trcpak = magic_size >= 28 && xx_rt_memcmp(magic, "TRCPAK", 7U) == 0;
    bool is_swagpacket = magic_size >= 48 && xx_rt_memcmp(magic, "SWAGOLX.EXE (c) 1993 GDSOFT  ALL RIGHTS RESERVED", 48U) == 0;
    bool is_sw = magic_size >= 15 && xx_rt_memcmp(magic, "im001V", 6U) == 0;
    bool is_sos = magic_size >= 20 && xx_rt_memcmp(magic, "DOS", 3U) == 0 &&
                  xx_rt_memcmp(magic + 0x10U, "SOS1", 4U) == 0;
    bool is_secondnature = magic_size >= 32 &&
                           ((magic[0] == 'S' && magic[1] == 'e') ||
                            ((uint8_t)~magic[0] == 'S' &&
                             (uint8_t)~magic[1] == 'e'));
    bool is_seadata = magic_size >= 8 && magic[0] == 0x43U && magic[1] == 0x34U &&
                      magic[2] == 0x21U && magic[3] == 0x12U;
    bool is_sci = magic_size >= 46 && xx_rt_memcmp(magic, "SCI", 3U) == 0 &&
                  (magic[3] == '1' || magic[3] == '2');
    /* Headerless: the only fixed-offset gate is the record lead byte,
     * and the smallest valid archive is a single 7-byte record. */
    bool is_powerboardbbs = magic_size >= 7 &&
                            ((magic[0] >= 1U && magic[0] <= 8U) ||
                             (magic[0] >= 11U && magic[0] <= 18U));
    bool is_minidump = magic_size >= 32 && xx_rt_memcmp(magic, "MDMP", 4U) == 0;
    bool is_lbrcobol = magic_size >= 34 && xx_rt_memcmp(magic, "Micro Focus COBOL Library File", 30U) == 0;
    bool is_krml = magic_size >= 6 && xx_rt_memcmp(magic, "KRML", 4U) == 0;
    bool is_jam = magic_size >= 7 && xx_rt_memcmp(magic, "JAM", 3U) == 0;
    bool is_irixsa = magic_size >= 8 && magic[0] == 0xacU && magic[1] == 0xedU &&
                     magic[2] == 0x12U && magic[3] == 0x34U;
    bool is_hlb = magic_size >= 8 && magic[0] == 0xd2U && magic[1] == 0x04U;
    bool is_frontpagetheme = magic_size >= 4 && magic[0] >= '0' && magic[0] <= '9' &&
                             (magic[1] == '.' ||
                              (magic[1] >= '0' && magic[1] <= '9'));
    bool is_cru = magic_size >= 13 && xx_rt_memcmp(magic, "CRUSH v1", 8U) == 0 && magic[8] == '.';
    bool is_bigaf = magic_size >= 8 && xx_rt_memcmp(magic, "<bigaf>\n", 8U) == 0;
    /* TWS has no signature at all. The cheap gate is the header record's
     * three fixed fields; everything past that is the device probe's job. */
    bool is_tws = magic_size >= 17 && magic[0] >= 1U && magic[0] <= 12U &&
                  magic[13] == 1U && magic[14] == 0U && magic[15] == 0U &&
                  magic[16] == 0U;
    bool is_packit = magic_size >= 16 &&
                     xx_rt_memcmp(magic, "PACKIT by MJP\r\n\x1a",
                                  16U) == 0;
    bool is_zfsf = magic_size >= 4 && magic[0] == 'Z' && magic[1] == 'F' && magic[2] == 'S' && magic[3] == 'F';
    bool is_marc = magic_size >= 8 && magic[0] == 'M' && magic[1] == 'A' && magic[2] == 'R' && magic[3] == 'C';
    bool is_bigf = magic_size >= 4 && magic[0] == 'B' && magic[1] == 'I' && magic[2] == 'G' && (magic[3] == 'F' || magic[3] == '4');
    bool is_ascend = xx_format_ascend_prefilter(magic, magic_size);
    bool is_asar = magic_size >= 16 && magic[0] == 0x04U && magic[1] == 0x00U && magic[2] == 0x00U && magic[3] == 0x00U;
    /* 67 57 04 01 (member) or 67 57 04 02 (wrapping prelude), LE. */
    bool is_arq = magic_size >= 4 && magic[0] == 0x67U &&
                  magic[1] == 0x57U && magic[2] == 0x04U &&
                  (magic[3] == 0x01U || magic[3] == 0x02U);
    bool is_freearc = magic_size >= 12 && magic[0] == 'A' && magic[1] == 'r' && magic[2] == 'C' && magic[3] == 0x01U && magic[8] == 'A' && magic[9] == 'r' && magic[10] == 'C' && magic[11] == 0x01U;
    bool is_zpaq = magic_size >= 3 && ((magic[0] == 'z' && magic[1] == 'P' && magic[2] == 'Q') || (magic_size >= 16 && magic[0] == 0x37U && magic[1] == 0x6BU && magic[13] == 'z' && magic[14] == 'P' && magic[15] == 'Q'));
    bool is_pea = magic_size >= 4 && magic[0] == 0xEAU && magic[1] == 0x01U && magic[2] <= 6U;
    bool is_lpaq8 = magic_size >= 4 && magic[0] == 'p' && magic[1] == 'Q' &&
                    magic[2] == 0x08U && magic[3] >= '0' && magic[3] <= '9';
    bool is_bcm = magic_size >= 4 && xx_rt_memcmp(magic, "BCM", 3U) == 0 &&
                  (magic[3] == '!' || magic[3] == '1');
    bool is_lzip = xx_lzip_has_header(magic, magic_size);
    bool is_lzma_alone = xx_lzma_alone_has_header(magic, magic_size);
    bool is_lzop = xx_lzop_has_header(magic, magic_size);
    bool is_tarx1 = xx_tarx1_has_header(magic, magic_size);
    bool is_tarx2 = xx_tarx2_has_header(magic, magic_size);
    bool is_tar = false;

    if (total_size >= 512 && xx_io_seek64(dev, 0, SEEK_SET) == 0) {
        uint8_t tar_header[512];
        if (xx_io_read(dev, tar_header, sizeof(tar_header)) ==
                (ssize_t)sizeof(tar_header)) {
            is_tar = xx_format_tar_header_is_valid(tar_header);
        }
    }
    if (total_size >= (int64_t)17 * 2048 &&
        xx_io_seek64(dev, (int64_t)16 * 2048, SEEK_SET) == 0) {
        uint8_t volume_descriptor[7];
        if (xx_io_read(dev, volume_descriptor, sizeof(volume_descriptor)) ==
                (ssize_t)sizeof(volume_descriptor) &&
            (volume_descriptor[0] == 0U || volume_descriptor[0] == 1U ||
             volume_descriptor[0] == 2U) &&
            xx_rt_memcmp(volume_descriptor + 1U, "CD001", 5U) == 0 &&
            volume_descriptor[6] == 1U) {
            is_iso9660 = true;
        }
    }
    if (total_size >= 14 && xx_io_seek64(dev, 7, SEEK_SET) == 0) {
        uint8_t ace_magic[7];
        is_ace = xx_io_read(dev, ace_magic, sizeof(ace_magic)) ==
                     (ssize_t)sizeof(ace_magic) &&
                 xx_rt_memcmp(ace_magic, "**ACE**", sizeof(ace_magic)) == 0;
    }

    if (magic[0] == 'P' && magic[1] == 'K') {
        if ((magic[2] == 0x03 && magic[3] == 0x04) ||
            (magic[2] == 0x05 && magic[3] == 0x06) ||
            (magic[2] == 0x07 && magic[3] == 0x08)) {
            is_zip = true;
        }
    }

    /* Scan tail of device for EOCD / ZIP64 records */
    size_t scan_size = (total_size > 65557) ? 65557 : (size_t)total_size;
    int64_t scan_offset = total_size - (int64_t)scan_size;
    if (scan_offset < 0) {
        scan_offset = 0;
    }

    /* Allocate buffer for scanning */
    uint8_t *scan_buf = (uint8_t*)xx_mem_alloc(scan_size);
    if (scan_buf) {
        if (xx_io_seek64(dev, scan_offset, SEEK_SET) == 0) {
            ssize_t bytes_read = xx_io_read(dev, scan_buf, scan_size);
            if (bytes_read >= 4) {
                for (ssize_t i = 0; i <= bytes_read - 4; ++i) {
                    if (scan_buf[i] == 'P' && scan_buf[i + 1] == 'K') {
                        uint8_t b2 = scan_buf[i + 2];
                        uint8_t b3 = scan_buf[i + 3];

                        /* PK\x05\x06 : End of central directory record */
                        if (b2 == 0x05 && b3 == 0x06) {
                            is_zip = true;
                            /* If standard EOCD has 0xFFFF/0xFFFFFFFF fields, check for ZIP64 */
                            if (i + 20 <= bytes_read) {
                                uint16_t disk_num = (uint16_t)(scan_buf[i + 4] | (scan_buf[i + 5] << 8));
                                uint16_t cd_disk  = (uint16_t)(scan_buf[i + 6] | (scan_buf[i + 7] << 8));
                                uint16_t cd_rec   = (uint16_t)(scan_buf[i + 8] | (scan_buf[i + 9] << 8));
                                uint16_t cd_total = (uint16_t)(scan_buf[i + 10] | (scan_buf[i + 11] << 8));
                                if (disk_num == 0xFFFF || cd_disk == 0xFFFF ||
                                    cd_rec == 0xFFFF || cd_total == 0xFFFF) {
                                    is_zip64 = true;
                                }
                            }
                        }
                        /* PK\x06\x07 : ZIP64 end of central directory locator */
                        else if (b2 == 0x06 && b3 == 0x07) {
                            is_zip64 = true;
                            is_zip = true;
                        }
                        /* PK\x06\x06 : ZIP64 end of central directory record */
                        else if (b2 == 0x06 && b3 == 0x06) {
                            is_zip64 = true;
                            is_zip = true;
                        }
                    }
                }
            }
        }
        xx_mem_free(scan_buf);
    }

    /* Restore position */
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);

    if (is_vhddynamic) {
        if (xx_format_is_vhddynamic_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_VHDDYNAMIC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* Installer VISE for Windows (installer_vise_windows). */
    if ((magic_size >= 2U && magic[0] == 'M' && magic[1] == 'Z')) {
        bool valid = xx_format_probe_installer_vise_windows(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLER_VISE_WINDOWS;
    }

    const xx_format_probe_context registered_probes = {
        dev, total_size, orig_pos, is_mz, magic, magic_size
    };
    {
        xx_file_type_t registered = xx_format_probe_registered_primary(&registered_probes);
        if (registered != XX_FILE_TYPE_UNKNOWN) return registered;
    }

    if (is_ms_dos_backup) {
        if (xx_format_is_ms_dos_backup_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MS_DOS_BACKUP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* InstallShield MultiPlatform (installshield_multiplatform). */
    if (((magic_size >= 2 && magic[0] == 0x4DU && magic[1] == 0x5AU) || (magic_size >= 4 && magic[0] == 0x7FU && magic[1] == 0x45U && magic[2] == 0x4CU && magic[3] == 0x46U) || (magic_size >= 2 && magic[0] == 0x01U && (magic[1] == 0xDFU || magic[1] == 0xF7U)) || (magic_size >= 4 && magic[0] == 0x02U && (magic[1] == 0x0BU || magic[1] == 0x10U || magic[1] == 0x14U) && magic[2] == 0x01U && (magic[3] == 0x07U || magic[3] == 0x08U || magic[3] == 0x0BU)))) {
        bool valid = xx_format_probe_installshield_multiplatform(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_MULTIPLATFORM;
    }

    if (is_elf) {
        xx_elf elf;
        xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
        xx_elf_init(&elf, dev, 0);
        if (xx_elf_handle_base_info(&elf.format, NULL))
            type = elf.format.file_type;
        xx_elf_destroy(&elf);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (type == XX_FILE_TYPE_ELF32 || type == XX_FILE_TYPE_ELF64)
            return type;
    }

    if (is_macho) {
        xx_macho macho;
        xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
        xx_macho_init(&macho, dev, 0);
        if (xx_macho_handle_base_info(&macho.format, NULL))
            type = macho.format.file_type;
        xx_macho_destroy(&macho);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (type == XX_FILE_TYPE_MACHO32 || type == XX_FILE_TYPE_MACHO64)
            return type;
    }

    if (is_dex) {
        xx_dex dex;
        bool valid;
        xx_dex_init(&dex, dev, 0);
        valid = xx_dex_check_is_valid(&dex.format, NULL);
        xx_dex_destroy(&dex);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DEX;
    }

    if (1) {
        xx_amigahunk probe_amigahunk;
        bool valid_amigahunk;
        xx_amigahunk_init(&probe_amigahunk, dev, 0);
        valid_amigahunk = xx_amigahunk_check_is_valid(&probe_amigahunk.format, NULL);
        xx_amigahunk_destroy(&probe_amigahunk);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_amigahunk) return XX_FILE_TYPE_AMIGAHUNK;
    }
    if (1) {
        xx_atarist probe_atarist;
        bool valid_atarist;
        xx_atarist_init(&probe_atarist, dev, 0);
        valid_atarist = xx_atarist_check_is_valid(&probe_atarist.format, NULL);
        xx_atarist_destroy(&probe_atarist);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_atarist) return XX_FILE_TYPE_ATARIST;
    }
    /* Advanced Installer bootstrapper: MZ carrier with an embedded package table. */
    if (is_mz) {
        bool valid = xx_format_probe_advanced_installer_bootstrapper(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADVANCED_INSTALLER_BOOTSTRAPPER;
    }

    /* IFAH installer: MZ carrier or a bare "IFAH"...+17 "IFFH" package. */
    if (is_mz || (magic_size >= 21 && magic[0] == 'I' && magic[1] == 'F' && magic[2] == 'A' && magic[3] == 'H' && magic[17] == 'I' && magic[18] == 'F' && magic[19] == 'F' && magic[20] == 'H')) {
        bool valid = xx_format_probe_ifah_installer(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IFAH_INSTALLER;
    }

    /* InstallShield All-in-One setup (IS 7-12) (installshield_7_setup). */
    if (magic_size >= 2 && magic[0] == 'M' && magic[1] == 'Z') {
        bool valid = xx_format_probe_installshield_7_setup(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_7_SETUP;
    }

    /* A SoftPaq is an MZ executable carrying a "[FIT]" payload locator, so
     * it has to be asked before the generic MZ/PE identification below --
     * which would otherwise claim it as a plain DOS or PE binary. The probe
     * scans for the locator, so it runs only on files that are already MZ
     * and that nothing with a real signature has claimed. */
    /* Same reason as SoftPaq below: the CopyQM tools carry their help text
     * in a "TX" overlay behind an ordinary MZ image, so the generic MZ
     * identification would claim them first. */
    /* Installer payloads must be recognised before their generic executable
     * carrier. FFS and WarpIN also have standalone signatures; Instalit volumes
     * have a validated footer rather than a fixed prefix. Each probe restores
     * the caller's device cursor and releases all temporary parser state. */
    if (is_mz) {
        bool valid = xx_format_probe_sfx_nullsoft_pimp(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_NULLSOFT_PIMP;
    }

    if (is_mz) {
        bool valid = xx_format_probe_sfx_sydex_diskette_image(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_SYDEX_DISKETTE_IMAGE;
    }

    if (is_mz) {
        bool valid = xx_format_probe_sfx_compaq_softpaq(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_COMPAQ_SOFTPAQ;
    }

    if (is_mz) {
        bool valid = xx_format_probe_sfx_softpaq4(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_SOFTPAQ4;
    }

    if (is_mz) {
        bool valid = xx_format_probe_sfx_wasp_windows_auto(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_WASP_WINDOWS_AUTO;
    }

    if (is_mz) {
        bool valid = xx_format_probe_wise_installation_system(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WISE_INSTALLATION_SYSTEM;
    }

    if (is_mz) {
        bool valid = xx_format_probe_eschalon_setup_epsf(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ESCHALON_SETUP_EPSF;
    }

    if (is_mz) {
        bool valid = xx_format_probe_gentee_installer(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENTEE_INSTALLER;
    }

    if (is_mz) {
        bool valid = xx_format_probe_clickteam_install_creator(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CLICKTEAM_INSTALL_CREATOR;
    }

    if (is_mz) {
        bool valid = xx_format_probe_createinstall_instcrin_extractor(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CREATEINSTALL_INSTCRIN_EXTRACTOR;
    }

    if (is_mz) {
        bool valid = xx_format_probe_sfxstart(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFXSTART;
    }
    if (is_mz || (magic_size >= 4 && xx_rt_memcmp(magic, "FFS!", 4) == 0)) {
        bool valid = xx_format_probe_sfx_analogx_emucore_ffs(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ANALOGX_EMUCORE_FFS;
    }
    if (is_mz) {
        bool valid = xx_format_probe_sfx_krzip(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_KRZIP;
    }
    if (magic_size >= 4 && magic[0] == 0x77 && magic[1] == 0x04 && magic[2] == 0x02 && magic[3] == 0xbe) {
        bool valid = xx_format_probe_sfx_warpin_package(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_WARPIN_PACKAGE;
    }
    {
        bool valid = xx_format_probe_sfx_hci_instalit(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_HCI_INSTALIT;
    }
    if (is_mz) {
        bool valid = xx_format_probe_sfx_clickteam_multimedia_fusion(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_CLICKTEAM_MULTIMEDIA_FUSION;
    }
    if (is_mz) {
        bool valid = xx_format_probe_sfx_abbyy_fine_objects(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ABBYY_FINE_OBJECTS;
    }
    if (is_mz) {
        bool valid = xx_format_probe_sfx_inftool(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_INFTOOL;
    }
    if (is_mz) {
        bool valid = xx_format_probe_sfx_flashjester_jugglor(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_FLASHJESTER_JUGGLOR;
    }
    if (is_mz) {
        bool valid = xx_format_probe_sfx_jgsoft_deploymaster_package(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_JGSOFT_DEPLOYMASTER_PACKAGE;
    }
    if (is_mz) {
        bool valid = xx_format_probe_sfx_ardi_diskette_image(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ARDI_DISKETTE_IMAGE;
    }
    if (is_mz) {
        xx_sfx_sbx_extractor sbx;
        bool valid_sbx;
        xx_sfx_sbx_extractor_init(&sbx, dev, 0);
        valid_sbx = xx_sfx_sbx_extractor_check_is_valid(&sbx.format, NULL);
        xx_sfx_sbx_extractor_destroy(&sbx);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_sbx) return XX_FILE_TYPE_SFX_SBX_EXTRACTOR;
        if (xx_format_is_copyqmexe_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_COPYQMEXE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* ARNI installer container behind an MZ stub. */
    if (is_mz) {
        bool valid = xx_format_probe_arni_installer_container(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ARNI_INSTALLER_CONTAINER;
    }

    /* ej-technologies install4j / exe4j launcher. */
    if (is_mz) {
        bool valid = xx_format_probe_ej_technologies_install(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_EJ_TECHNOLOGIES_INSTALL;
    }

    /* InstallShield 3.x/5.x SFX (installshield_3). */
    if ((magic_size >= 2 && magic[0] == 0x4D && magic[1] == 0x5A)) {
        bool valid = xx_format_probe_installshield_3(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_3;
    }

    /* InstallShield Developer 7 Setup Launcher (installshield_developer). */
    if (magic_size >= 2 && magic[0] == 0x4D && magic[1] == 0x5A) {
        bool valid = xx_format_probe_installshield_developer(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_DEVELOPER;
    }

    /* ARDI installer (ardi_installer). */
    if ((magic_size >= 2 && magic[0] == 0x4D && magic[1] == 0x5A && total_size >= 134)) {
        bool valid = xx_format_probe_ardi_installer(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ARDI_INSTALLER;
    }

    /* InstallShield 12-2012 Setup (installshield_12_setup). */
    if ((magic_size >= 2U && magic[0] == 0x4DU && magic[1] == 0x5AU)) {
        bool valid = xx_format_probe_installshield_12_setup(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_12_SETUP;
    }

    /* QSetup Installation Suite (qsetup_installation_suite). */
    if (magic_size >= 2U && magic[0] == 'M' && magic[1] == 'Z') {
        bool valid = xx_format_probe_qsetup_installation_suite(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QSETUP_INSTALLATION_SUITE;
    }

    /* Setup Factory (setup_factory). */
    if (magic_size >= 2 && magic[0] == 0x4D && magic[1] == 0x5A) {
        bool valid = xx_format_probe_setup_factory(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SETUP_FACTORY;
    }

    /* Tarma Installer (tarma_installer). */
    if ((magic_size >= 2U && magic[0] == 'M' && magic[1] == 'Z')) {
        bool valid = xx_format_probe_tarma_installer(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TARMA_INSTALLER;
    }

    if (is_softpaq2) {
        if (xx_format_is_softpaq2_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SOFTPAQ2;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* A PE resource or image may carry a complete CAB. Check its bounded
     * cabinet graph before the broad SFX scans below revisit the same image. */
    if (is_mz) {
        xx_sfx_cab cabinet;
        bool valid_cabinet;
        xx_sfx_cab_init(&cabinet, dev, 0);
        valid_cabinet = xx_sfx_cab_check_is_valid(&cabinet.format, NULL);
        xx_sfx_cab_destroy(&cabinet);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_cabinet) return XX_FILE_TYPE_SFX_CAB;
    }
    {
        xx_file_type_t registered = xx_format_probe_registered_carriers(&registered_probes);
        if (registered != XX_FILE_TYPE_UNKNOWN) return registered;
    }

    if (is_pmdiskcopy && xx_format_is_pmdiskcopy_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_PMDISKCOPY;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);

    if (is_mz) {
        xx_sfx_spis spis_carrier;
        xx_sfx_rtpatch rtpatch_carrier;
        xx_sfx_rsfx rsfx_carrier;
        xx_sfx_ad01 ad01_carrier;
        xx_sfx_nss nss_carrier;
        bool valid_carrier;
        if (magic_size >= 32U && xx_rt_memcmp(magic + 28U, "RSFX", 4U) == 0) {
            xx_sfx_rsfx_init(&rsfx_carrier, dev, 0);
            valid_carrier = xx_sfx_rsfx_check_is_valid(&rsfx_carrier.format, NULL);
            xx_sfx_rsfx_destroy(&rsfx_carrier);
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            if (valid_carrier) return XX_FILE_TYPE_SFX_RSFX;
        }
        xx_sfx_ad01_init(&ad01_carrier, dev, 0);
        valid_carrier = xx_sfx_ad01_check_is_valid(&ad01_carrier.format, NULL);
        xx_sfx_ad01_destroy(&ad01_carrier);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_carrier) return XX_FILE_TYPE_SFX_AD01;
        xx_sfx_nss_init(&nss_carrier, dev, 0);
        valid_carrier = xx_sfx_nss_check_is_valid(&nss_carrier.format, NULL);
        xx_sfx_nss_destroy(&nss_carrier);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_carrier) return XX_FILE_TYPE_SFX_NSS;
        xx_sfx_spis_init(&spis_carrier, dev, 0);
        valid_carrier = xx_sfx_spis_check_is_valid(&spis_carrier.format, NULL);
        xx_sfx_spis_destroy(&spis_carrier);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_carrier) return XX_FILE_TYPE_SFX_SPIS;
        xx_sfx_rtpatch_init(&rtpatch_carrier, dev, 0);
        valid_carrier = xx_sfx_rtpatch_check_is_valid(&rtpatch_carrier.format, NULL);
        xx_sfx_rtpatch_destroy(&rtpatch_carrier);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_carrier) return XX_FILE_TYPE_SFX_RTPATCH;
        if (xx_format_is_sfx_localzip_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SFX_LOCALZIP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        xx_pe pe;
        xx_ne ne;
        xx_le le;
        xx_lx lx;
        xx_msdos msdos;
        xx_file_type_t pe_type = XX_FILE_TYPE_UNKNOWN;
        bool valid_ne;
        bool valid_le;
        bool valid_lx;
        bool valid_msdos;
        xx_pe_init(&pe, dev, 0);
        if (xx_pe_check_is_valid(&pe.format, NULL)) {
            uint32_t nt_offset = xx_io_get_u32(dev, 0x3c, false);
            uint16_t optional_magic = xx_io_get_u16(
                dev, (int64_t)nt_offset + 24, false);
            if (optional_magic == XX_PE_MAGIC_32) pe_type = XX_FILE_TYPE_PE32;
            else if (optional_magic == XX_PE_MAGIC_64) pe_type = XX_FILE_TYPE_PE64;
        }
        xx_pe_destroy(&pe);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (pe_type != XX_FILE_TYPE_UNKNOWN) {
            xx_dotnet managed;
            bool valid_managed;
            xx_dotnet_init(&managed, dev, 0);
            valid_managed = xx_dotnet_check_is_valid(&managed.pe.format, NULL);
            xx_dotnet_destroy(&managed);
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return valid_managed ? XX_FILE_TYPE_DOTNET : pe_type;
        }
        xx_ne_init(&ne, dev, 0);
        valid_ne = xx_ne_check_is_valid(&ne.format, NULL);
        xx_ne_destroy(&ne);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_ne) return XX_FILE_TYPE_NE;
        xx_le_init(&le, dev, 0);
        valid_le = xx_le_check_is_valid(&le.format, NULL);
        xx_le_destroy(&le);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_le) return XX_FILE_TYPE_LE;
        xx_lx_init(&lx, dev, 0);
        valid_lx = xx_lx_check_is_valid(&lx.format, NULL);
        xx_lx_destroy(&lx);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_lx) return XX_FILE_TYPE_LX;
        if (1) {
            xx_dos16m probe_dos16m;
            bool valid_dos16m;
            xx_dos16m_init(&probe_dos16m, dev, 0);
            valid_dos16m = xx_dos16m_check_is_valid(&probe_dos16m.format, NULL);
            xx_dos16m_destroy(&probe_dos16m);
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            if (valid_dos16m) return XX_FILE_TYPE_DOS16M;
        }
        if (1) {
            xx_dos16m probe_dos4g;
            bool valid_dos4g;
            xx_dos4g_init(&probe_dos4g, dev, 0);
            valid_dos4g = xx_dos4g_check_is_valid(&probe_dos4g.format, NULL);
            xx_dos4g_destroy(&probe_dos4g);
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            if (valid_dos4g) return XX_FILE_TYPE_DOS4G;
        }
        xx_msdos_init(&msdos, dev, 0);
        valid_msdos = xx_msdos_check_is_valid(&msdos.format, NULL);
        xx_msdos_destroy(&msdos);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_msdos) return XX_FILE_TYPE_MSDOS;
    }

    if (is_encrpted_img) {
        if (xx_format_is_encrpted_img_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ENCRPTED_IMG;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_matter_ota) {
        if (xx_format_is_matter_ota_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MATTER_OTA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_7zip) {
        return XX_FILE_TYPE_7ZIP;
    }
    if (is_rar) {
        return XX_FILE_TYPE_RAR;
    }
    if (is_ar) {
        return XX_FILE_TYPE_AR;
    }
    if (is_mscompress) {
        if (xx_format_is_mscompress_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MS_COMPRESS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* MS COMPRESS KWAJ (kwaj). */
    if (magic_size >= 14 && xx_rt_memcmp(magic, "KWAJ\x88\xf0\x27\xd1", 8U) == 0) {
        bool valid = xx_format_probe_kwaj(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_KWAJ;
    }

    if (is_ash0) {
        if (xx_format_is_ash0_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ASH0;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_wiilz77) {
        if (xx_format_is_wiilz77_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_WII_LZ77;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lzv1) {
        if (xx_format_is_lzv1_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZV1;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_oraclesqueeze) {
        /* Classic CP/M Greenlaw SQ shares this exact 0x76 0xFF prefilter with
         * Oracle Squeeze -- a different format entirely. The stricter test
         * runs first; each refuses the other's layout at parse time. */
        if (xx_format_is_squeeze1_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SQUEEZE1;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_oraclesqueeze) {
        if (xx_format_is_oraclesqueeze_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ORACLE_SQUEEZE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_softronics) {
        if (xx_format_is_softronics_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SOFTRONICS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_autel) {
        if (xx_format_is_autel_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_AUTEL;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_visionaire_studio_vis) {
        if (xx_format_is_visionaire_studio_vis_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_VISIONAIRE_STUDIO_VIS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_xpak) {
        if (xx_format_is_xpak_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_XPAK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_logitechcompress) {
        if (xx_format_is_logitechcompress_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LOGITECH_COMPRESS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_dmapacked) {
        if (xx_format_is_dmapacked_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DMA_PACKED;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_huf) {
        if (xx_format_is_huf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_HUF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lzdiet) {
        if (xx_format_is_lzdiet_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZDIET;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zie) {
        if (xx_format_is_zie_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZIE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_xeditpack) {
        if (xx_format_is_xeditpack_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_XEDITPACK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_wpk) {
        if (xx_format_is_wpk_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_WPK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_wintersoft) {
        if (xx_format_is_wintersoft_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_WINTERSOFT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_vmarc) {
        if (xx_format_is_vmarc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_VMARC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_tivoli) {
        if (xx_format_is_tivoli_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TIVOLI;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_stylus) {
        if (xx_format_is_stylus_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_STYLUS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_rtpatch) {
        if (xx_format_is_rtpatch_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_RTPATCH;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_rta) {
        if (xx_format_is_rta_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_RTA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_qnxbase) {
        if (xx_format_is_qnxbase_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QNXBASE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_qda) {
        if (xx_format_is_qda_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QDA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lofi) {
        if (xx_format_is_lofi_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LOFI;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lim) {
        if (xx_format_is_lim_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LIM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_kolibrikpack) {
        if (xx_format_is_kolibrikpack_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_KOLIBRIKPACK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ivt) {
        if (xx_format_is_ivt_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IVT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_irwinpac) {
        if (xx_format_is_irwinpac_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IRWINPAC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_hap) {
        if (xx_format_is_hap_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_HAP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_imp) {
        if (xx_format_is_imp_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IMP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sqx) {
        if (xx_format_is_sqx_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SQX;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zoo) {
        if (xx_format_is_zoo_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZOO;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_jbf) {
        if (xx_format_is_jbf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_JBF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ztc) {
        if (xx_format_is_ztc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZTC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_netwarepacked) {
        if (xx_format_is_netwarepacked_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_NETWAREPACKED;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zpak) {
        if (xx_format_is_zpak_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZPAK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zcmp) {
        if (xx_format_is_zcmp_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZCMP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_scl) {
        if (xx_format_is_scl_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SCL;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pakleo) {
        if (xx_format_is_pakleo_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PAKLEO;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_npack) {
        if (xx_format_is_npack_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_NPACK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_mi10) {
        if (xx_format_is_mi10_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MI10;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lzwd) {
        if (xx_format_is_lzwd_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZWD;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lzhcxp) {
        if (xx_format_is_lzhcxp_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZHCXP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_kboom) {
        if (xx_format_is_kboom_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_KBOOM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_hzl) {
        if (xx_format_is_hzl_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_HZL;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ha) {
        if (xx_format_is_ha_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_HA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_genius) {
        if (xx_format_is_genius_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_GENIUS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_fls) {
        if (xx_format_is_fls_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FLS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_earefpack) {
        if (xx_format_is_earefpack_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_EAREFPACK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ealib) {
        if (xx_format_is_ealib_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_EALIB;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_elm) {
        /* These readers share a payload layout.  The FrontPage reader
         * requires the declared chain to land exactly on EOF, while ELM
         * additionally accepts a bounded, unlisted CSS trailer.  Prefer
         * the narrower match for exact-ended themes. */
        if (xx_format_is_frontpagetheme_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FRONTPAGETHEME;
        }
        if (xx_format_is_elm_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ELM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ea) {
        if (xx_format_is_ea_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_EA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_diskdoubler) {
        if (xx_format_is_diskdoubler_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DISKDOUBLER;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_cmp) {
        if (xx_format_is_cmp_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CMP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_clp) {
        if (xx_format_is_clp_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CLP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_chieflzmulti) {
        if (xx_format_is_chieflzmulti_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CHIEFLZMULTI;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_chieflz) {
        if (xx_format_is_chieflz_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CHIEFLZ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_bwcf) {
        if (xx_format_is_bwcf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BWCF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_asymetrix) {
        if (xx_format_is_asymetrix_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ASYMETRIX;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_amigalzx) {
        if (xx_format_is_amigalzx_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_AMIGALZX;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_spis) {
        if (xx_format_is_spis_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SPIS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lha) {
        if (xx_format_is_lha_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LHA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_xar) {
        if (xx_format_is_xar_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_XAR;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_fmc1) {
        if (xx_format_is_fmc1_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FMC1;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pyz) {
        if (xx_format_is_pyz_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PYZ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_jetbbs) {
        if (xx_format_is_jetbbs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_JETBBS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ecmpacked) {
        if (xx_format_is_ecmpacked_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ECMPACKED;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_borlandpack) {
        if (xx_format_is_borlandpack_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BORLANDPACK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_jgpak) {
        if (xx_format_is_jgpak_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_JGPAK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zzz) {
        if (xx_format_is_zzz_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZZZ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zz) {
        if (xx_format_is_zz_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZZ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zlwb) {
        if (xx_format_is_zlwb_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZLWB;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_trc) {
        if (xx_format_is_trc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TRC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_tgcf) {
        if (xx_format_is_tgcf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TGCF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_swag) {
        if (xx_format_is_swag_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SWAG;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_riversoft) {
        if (xx_format_is_riversoft_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_RIVERSOFT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_rcf) {
        if (xx_format_is_rcf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_RCF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_quarterdeckqp) {
        if (xx_format_is_quarterdeckqp_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QUARTERDECKQP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_qip1) {
        if (xx_format_is_qip1_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QIP1;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_powerarc) {
        if (xx_format_is_powerarc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_POWERARC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_povlablzh) {
        if (xx_format_is_povlablzh_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_POVLABLZH;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_mva) {
        if (xx_format_is_mva_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MVA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_miz) {
        if (xx_format_is_miz_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MIZ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lsz) {
        if (xx_format_is_lsz_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LSZ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_jm93) {
        if (xx_format_is_jm93_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_JM93;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_inteduft) {
        if (xx_format_is_inteduft_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_INTEDUFT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_igf2) {
        if (xx_format_is_igf2_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IGF2;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_igf1) {
        if (xx_format_is_igf1_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IGF1;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ibmzpak) {
        if (xx_format_is_ibmzpak_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IBMZPAK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_fiz) {
        if (xx_format_is_fiz_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FIZ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_dtpacked) {
        if (xx_format_is_dtpacked_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DTPACKED;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_dsl2) {
        if (xx_format_is_dsl2_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DSL2;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_dpk) {
        if (xx_format_is_dpk_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DPK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_cfl) {
        if (xx_format_is_cfl_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CFL;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_trcpak) {
        if (xx_format_is_trcpak_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TRCPAK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_swagpacket) {
        if (xx_format_is_swagpacket_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SWAGPACKET;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sw) {
        if (xx_format_is_sw_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SW;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sos) {
        if (xx_format_is_sos_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SOS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_secondnature) {
        if (xx_format_is_secondnature_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SECONDNATURE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_seadata) {
        if (xx_format_is_seadata_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SEADATA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sci) {
        if (xx_format_is_sci_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SCI;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_powerboardbbs) {
        if (xx_format_is_powerboardbbs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_POWERBOARDBBS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_minidump) {
        if (xx_format_is_minidump_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MINIDUMP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lbrcobol) {
        if (xx_format_is_lbrcobol_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LBRCOBOL;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_krml) {
        if (xx_format_is_krml_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_KRML;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_jam) {
        if (xx_format_is_jam_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_JAM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_irixsa) {
        if (xx_format_is_irixsa_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IRIXSA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_hlb) {
        if (xx_format_is_hlb_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_HLB;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_frontpagetheme) {
        if (xx_format_is_frontpagetheme_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FRONTPAGETHEME;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_cru) {
        if (xx_format_is_cru_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CRU;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_bigaf) {
        if (xx_format_is_bigaf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BIGAF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_packit) {
        if (xx_format_is_packit_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PACKIT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zfsf) {
        if (xx_format_is_zfsf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZFSF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_marc) {
        if (xx_format_is_marc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MARC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_bigf) {
        if (xx_format_is_bigf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BIGF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ascend) {
        if (xx_format_is_ascend_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ASCEND;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_asar) {
        if (xx_format_is_asar_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ASAR;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_arq) {
        if (xx_format_is_arq_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ARQ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_freearc) {
        if (xx_format_is_freearc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FREEARC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zpaq) {
        if (xx_format_is_zpaq_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZPAQ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pea) {
        if (xx_format_is_pea_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PEA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lpaq8) {
        if (xx_format_is_lpaq8_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LPAQ8;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_uharc) {
        if (xx_format_is_uharc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_UHARC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_bcm) {
        if (xx_format_is_bcm_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BCM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_bz2) {
        bool is_tar_bz2 = xx_format_is_tar_bz2_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_tar_bz2) return XX_FILE_TYPE_TAR_BZ2;
        return XX_FILE_TYPE_BZ2;
    }
    if (is_gz) {
        bool is_tar_gz = xx_format_is_tar_gz_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_tar_gz) {
            xx_npm npm;
            bool is_npm;
            xx_npm_init(&npm, dev, 0);
            is_npm = xx_npm_check_is_valid(&npm.tar_gz.format, NULL);
            xx_npm_destroy(&npm);
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            if (is_npm) return XX_FILE_TYPE_NPM;
            return XX_FILE_TYPE_TAR_GZ;
        }
        return XX_FILE_TYPE_GZ;
    }
    if (is_xz) {
        bool is_tar_xz = xx_format_is_tar_xz_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_tar_xz) return XX_FILE_TYPE_TAR_XZ;
        return XX_FILE_TYPE_XZ;
    }
    if (is_lz4) {
        bool is_tar_lz4 = xx_format_is_tar_lz4_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_tar_lz4) return XX_FILE_TYPE_TAR_LZ4;
        if (xx_format_is_lz4_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZ4;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lz4demo) {
        if (xx_format_is_lz4demo_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZ4DEMO;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_xamarin_compressed_assembly) {
        if (xx_format_is_xamarin_compressed_assembly_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_XAMARIN_COMPRESSED_ASSEMBLY;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lz5) {
        if (xx_format_is_lz5_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZ5;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lizard) {
        if (xx_format_is_lizard_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LIZARD;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_brotli_mt) {
        if (xx_format_is_brotli_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BROTLI;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zstd) {
        bool is_tar_zstd = xx_format_is_tar_zstd_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_tar_zstd) return XX_FILE_TYPE_TAR_ZSTD;
        if (xx_format_is_zstd_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZSTD;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* Before ISO9660 on purpose: a UDF bridge disc carries a real
     * ISO9660 PVD at sector 16, so testing ISO9660 first would claim
     * every bridge disc. A plain ISO9660 image fails the UDF probe
     * (which is a full volume parse) and falls through unharmed. */
    if (is_udf) {
        if (xx_format_is_udf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_UDF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_iso9660) {
        bool valid_iso = xx_format_is_iso9660_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_iso) return XX_FILE_TYPE_ISO9660;
    }
    if (is_ace) {
        xx_ace ace;
        bool valid_ace;
        xx_ace_init(&ace, dev, 0);
        valid_ace = xx_ace_check_is_valid(&ace.format, NULL);
        xx_ace_destroy(&ace);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_ace) return XX_FILE_TYPE_ACE;
    }
    /* CAB and BFF carry exact four-byte signatures, so they sit above the
     * weaker single-byte probes below and above the LZMA-alone check, which
     * would otherwise decode the whole file before giving up on them. */
    if (is_cab) {
        xx_cab cab;
        bool valid_cab;
        xx_cab_init(&cab, dev, 0);
        valid_cab = xx_cab_check_is_valid(&cab.format, NULL);
        xx_cab_destroy(&cab);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_cab) return XX_FILE_TYPE_CAB;
    }
    if (is_aixbff) {
        xx_aixbff aixbff;
        bool valid_aixbff;
        xx_aixbff_init(&aixbff, dev, 0);
        valid_aixbff = xx_aixbff_check_is_valid(&aixbff.format, NULL);
        xx_aixbff_destroy(&aixbff);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_aixbff) return XX_FILE_TYPE_AIXBFF;
    }
    if (is_ain) {
        xx_ain ain;
        bool valid_ain;
        xx_ain_init(&ain, dev, 0);
        valid_ain = xx_ain_check_is_valid(&ain.format, NULL);
        xx_ain_destroy(&ain);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_ain) return XX_FILE_TYPE_AIN;
    }
    if (is_aldus) {
        xx_aldus aldus;
        bool valid_aldus;
        xx_aldus_init(&aldus, dev, 0);
        valid_aldus = xx_aldus_check_is_valid(&aldus.format, NULL);
        xx_aldus_destroy(&aldus);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_aldus) return XX_FILE_TYPE_ALDUS;
    }
    if (is_alz) {
        xx_alz alz;
        bool valid_alz;
        xx_alz_init(&alz, dev, 0);
        valid_alz = xx_alz_check_is_valid(&alz.format, NULL);
        xx_alz_destroy(&alz);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_alz) return XX_FILE_TYPE_ALZ;
    }
    if (is_ampk) {
        xx_ampk ampk;
        bool valid_ampk;
        xx_ampk_init(&ampk, dev, 0);
        valid_ampk = xx_ampk_check_is_valid(&ampk.format, NULL);
        xx_ampk_destroy(&ampk);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_ampk) return XX_FILE_TYPE_AMPK;
    }
    if (is_aodos) {
        xx_aodos aodos;
        bool valid_aodos;
        xx_aodos_init(&aodos, dev, 0);
        valid_aodos = xx_aodos_check_is_valid(&aodos.format, NULL);
        xx_aodos_destroy(&aodos);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_aodos) return XX_FILE_TYPE_AODOS;
    }
    if (is_arcfs) {
        xx_arcfs arcfs;
        bool valid_arcfs;
        xx_arcfs_init(&arcfs, dev, 0);
        valid_arcfs = xx_arcfs_check_is_valid(&arcfs.format, NULL);
        xx_arcfs_destroy(&arcfs);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_arcfs) return XX_FILE_TYPE_ARCFS;
    }
    if (is_pdp11ar) {
        xx_pdp11ar pdp11ar;
        bool valid_pdp11ar;
        xx_pdp11ar_init(&pdp11ar, dev, 0);
        valid_pdp11ar = xx_pdp11ar_check_is_valid(&pdp11ar.format, NULL);
        xx_pdp11ar_destroy(&pdp11ar);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_pdp11ar) return XX_FILE_TYPE_PDP11AR;
    }
    if (is_artipack) {
        xx_artipack artipack;
        bool valid_artipack;
        xx_artipack_init(&artipack, dev, 0);
        valid_artipack = xx_artipack_check_is_valid(&artipack.format, NULL);
        xx_artipack_destroy(&artipack);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_artipack) return XX_FILE_TYPE_ARTIPACK;
    }
    if (is_arcv2) {
        xx_arcv2 arcv2;
        bool valid_arcv2;
        xx_arcv2_init(&arcv2, dev, 0);
        valid_arcv2 = xx_arcv2_check_is_valid(&arcv2.format, NULL);
        xx_arcv2_destroy(&arcv2);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_arcv2) return XX_FILE_TYPE_ARCV2;
    }
    if (is_arcv4) {
        xx_arcv4 arcv4;
        bool valid_arcv4;
        xx_arcv4_init(&arcv4, dev, 0);
        valid_arcv4 = xx_arcv4_check_is_valid(&arcv4.format, NULL);
        xx_arcv4_destroy(&arcv4);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_arcv4) return XX_FILE_TYPE_ARCV4;
    }
    if (is_arj) {
        xx_arj arj;
        bool valid_arj;
        xx_arj_init(&arj, dev, 0);
        valid_arj = xx_arj_check_is_valid(&arj.format, NULL);
        xx_arj_destroy(&arj);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_arj) return XX_FILE_TYPE_ARJ;
    }
    if (is_warc) {
        xx_warc warc;
        bool valid_warc;
        xx_warc_init(&warc, dev, 0);
        valid_warc = xx_warc_check_is_valid(&warc.format, NULL);
        xx_warc_destroy(&warc);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_warc) return XX_FILE_TYPE_WARC;
    }
    /* RPM (rpm). */
    if (magic_size >= 8 && magic[0] == 0xEDU && magic[1] == 0xABU && magic[2] == 0xEEU && magic[3] == 0xDBU && (magic[4] == 3U || magic[4] == 4U) && magic[6] == 0U && magic[7] <= 1U && total_size >= 112) {
        bool valid = xx_format_probe_rpm(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RPM;
    }

    if (is_cpio) {
        xx_cpio cpio;
        bool valid_cpio;
        xx_cpio_init(&cpio, dev, 0);
        valid_cpio = xx_cpio_check_is_valid(&cpio.format, NULL);
        xx_cpio_destroy(&cpio);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_cpio) return XX_FILE_TYPE_CPIO;
    }
    if (is_mtree) {
        xx_mtree mtree;
        bool valid_mtree;
        xx_mtree_init(&mtree, dev, 0);
        valid_mtree = xx_mtree_check_is_valid(&mtree.format, NULL);
        xx_mtree_destroy(&mtree);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_mtree) return XX_FILE_TYPE_MTREE;
    }
    if (is_compress) {
        bool is_tar_compress = xx_format_is_tar_compress_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_tar_compress) return XX_FILE_TYPE_TAR_COMPRESS;
        if (xx_format_is_unixcompress_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_UNIX_COMPRESS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_unixpack) {
        if (xx_format_is_unixpack_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_UNIX_PACK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zlib) {
        /* A UDIF data fork may begin with a zlib-compressed run. Its koly
         * trailer establishes the disk image before classifying bare zlib. */
        if (is_dmg && xx_format_is_dmg_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DMG;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (xx_format_is_gitobject_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_GIT_OBJECT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (xx_format_is_zlib_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZLIB;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lzip) {
        bool is_tar_lzip = xx_format_is_tar_lzip_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_tar_lzip) return XX_FILE_TYPE_TAR_LZIP;
        if (xx_format_is_lzip_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZIP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lzma_alone) {
        bool is_tar_lzma = xx_format_is_tar_lzma_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_tar_lzma) return XX_FILE_TYPE_TAR_LZMA;
        if (xx_format_is_lzma_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZMA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lzop) {
        bool is_tar_lzop = xx_format_is_tar_lzop_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_tar_lzop) return XX_FILE_TYPE_TAR_LZOP;
    }
    if (is_lzop) {
        if (xx_format_is_lzop_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZOP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_tarx1) {
        bool valid_tarx1 = xx_format_is_tarx1_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_tarx1) return XX_FILE_TYPE_TARX1;
    }
    if (is_tarx2) {
        bool valid_tarx2 = xx_format_is_tarx2_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_tarx2) return XX_FILE_TYPE_TARX2;
    }
    /* ARX last of the new readers: its first two bytes are a header size and a
     * checksum rather than a signature, so it must not get to arbitrate ahead
     * of any format that does have one. It still has to precede the NeXTSTEP
     * and ZIP tail probes below, which are structural and would claim it. */
    if (is_arx) {
        xx_arx arx;
        bool valid_arx;
        xx_arx_init(&arx, dev, 0);
        valid_arx = xx_arx_check_is_valid(&arx.format, NULL);
        xx_arx_destroy(&arx);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_arx) return XX_FILE_TYPE_ARX;
    }
    if (!is_tar && total_size >= 512) {
        bool is_tar_nextstep = xx_format_is_tar_nextstep_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_tar_nextstep) return XX_FILE_TYPE_TAR_NEXTSTEP;
    }
    if (is_tar) {
        return XX_FILE_TYPE_TAR;
    }
    if (is_dkbs) {
        if (xx_format_is_dkbs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DKBS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* An IzPack pack is a Java serialization stream that happens to
     * carry ZIP members further in, so the ZIP probe claims it
     * first and then fails. IzPack has the stronger evidence -- a
     * 41-byte fixed prefix ending in the literal class name -- so
     * it is asked first. Narrowing ZIP instead would risk every
     * real ZIP, including the ZIP64 forms this corpus has none of.
     */
    if (is_izpack) {
        if (xx_format_is_izpack_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IZPACK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* KDC LIF: a bare chain of ASCII-hex member headers. A stored member may
     * be a PKZIP self-extractor whose EOCD ends the file, so LIF is asked
     * before the ZIP tail-scan verdict. */
    if (magic_size >= 54 && xx_lifkd_is_member_header(magic, magic_size)) {
        if (xx_format_is_lifkd_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LIFKD;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    if (is_apple_disk_copy_6_ndif_image) {
        if (xx_format_is_apple_disk_copy_6_ndif_image_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_APPLE_DISK_COPY_6_NDIF_IMAGE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* MacBinary II/III whose header CRC-16 verifies. Ahead of the ZIP
     * verdict, whose tail scan would otherwise claim a MacBinary-wrapped
     * ZIP; MacBinary I and stale-CRC headers stay in the late chain. */
    if (total_size >= 128 && magic[0] == 0x00U && magic[1] >= 1U &&
        magic[1] <= 63U && magic[2] >= 0x20U) {
        bool is_macbinary = xx_format_probe_macbinary_verified(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_macbinary) return XX_FILE_TYPE_MACBINARY;
    }

    /* NID volumes may contain a ZIP member near the end.  The ZIP tail scan
     * sees that nested EOCD, so prefer a structurally valid NI container. */
    if (is_nid) {
        if (xx_format_is_nid_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_NID;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* A ROMFS can likewise contain a ZIP file whose EOCD is visible in the
     * tail scan.  Its leading magic and validated filesystem take priority. */
    if (is_romfs) {
        if (xx_format_is_romfs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ROMFS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    if (is_cpoint) {
        if (xx_format_is_cpoint_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CPOINT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    /* These self-extracting shell scripts can contain a valid ZIP trailer.
     * Let their structurally validated wrapper readers claim the payload
     * before the generic ZIP tail scan chooses a nested stream. */
    if (is_shell_wrapper) {
        if (xx_format_is_sun_java_binsh_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SUN_JAVA_BINSH;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (xx_format_is_installanywhere_unix_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_INSTALLANYWHERE_UNIX;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    if (is_zip) {
        xx_apk apk;
        xx_ipa ipa;
        xx_jar jar;
        bool is_apk;
        bool is_ipa;
        bool is_jar;

        /* APKs and IPAs may carry a Java manifest, so the more specific
         * mobile package identities must be tested before JAR. */
        xx_apk_init(&apk, dev, 0);
        is_apk = xx_apk_check_is_valid(&apk.zip.format, NULL);
        xx_apk_destroy(&apk);
        if (is_apk) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_APK;
        }

        xx_ipa_init(&ipa, dev, 0);
        is_ipa = xx_ipa_check_is_valid(&ipa.zip.format, NULL);
        xx_ipa_destroy(&ipa);
        if (is_ipa) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IPA;
        }

        xx_jar_init(&jar, dev, 0);
        is_jar = xx_jar_check_is_valid(&jar.zip.format, NULL);
        xx_jar_destroy(&jar);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (is_jar) {
            return XX_FILE_TYPE_JAR;
        }
        return is_zip64 ? XX_FILE_TYPE_ZIP64 : XX_FILE_TYPE_ZIP;
    }

    /* GAS Huffman has no magic.  It is intentionally last: only a complete
     * native structural parse and bounded decode may claim an otherwise
     * unrecognized file. */
    if (xx_format_is_gashuff_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_GAS_HUFF;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);

    if (is_seaarc) {
        if (xx_format_is_seaarc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SEAARC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    if (is_compactpro) {
        if (xx_format_is_compactpro_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_COMPACTPRO;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pkt) {
        if (xx_format_is_pkt_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PKT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_rid) {
        if (xx_format_is_rid_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_RID;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_rompaq) {
        if (xx_format_is_rompaq_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ROMPAQ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ti99arc) {
        if (xx_format_is_ti99arc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TI99ARC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    if (is_dlke) {
        if (xx_format_is_dlke_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DLKE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* No signature at all: GTU keys on its own file size, STK on a
     * plausible member count, TERSE and the rest on a trial decode.
     * They must not get first refusal on a file another format can
     * name outright. */
    if (is_glu) {
        if (xx_format_is_glu_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_GLU;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_gtu) {
        if (xx_format_is_gtu_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_GTU;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ibmspack) {
        if (xx_format_is_ibmspack_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IBMSPACK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pcommos2) {
        if (xx_format_is_pcommos2_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PCOMMOS2;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_stk) {
        if (xx_format_is_stk_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_STK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_terse) {
        if (xx_format_is_terse_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TERSE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    /* Newly added readers. Placed after every pre-existing format so a
     * weak new magic (Quantum's two-byte "DS", ZXZIP's "ZIP" at +8) can never
     * take a file that an established reader would have claimed. */
    if (is_squashfs) {
        if (xx_format_is_squashfs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SQUASHFS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ntfs) {
        if (xx_format_is_ntfs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_NTFS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sqz) {
        if (xx_format_is_sqz_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SQZ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_tps) {
        if (xx_format_is_tps_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TPS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ulead) {
        if (xx_format_is_ulead_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ULEAD;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_quantum) {
        if (xx_format_is_quantum_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QUANTUM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zxzip) {
        if (xx_format_is_zxzip_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZXZIP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zoom) {
        if (xx_format_is_zoom_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZOOM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sfpack) {
        if (xx_format_is_sfpack_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SFPACK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_claylz) {
        if (xx_format_is_claylz_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CLAYLZ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_c64wraptor) {
        if (xx_format_is_c64wraptor_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_C64WRAPTOR;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* T64 (t64). */
    if ((magic_size >= 64U && magic[0] == 0x43U && magic[1] == 0x36U && magic[2] == 0x34U && ((magic[3] == 0x20U && (magic[4] | 0x20U) == 0x74U && (magic[5] | 0x20U) == 0x61U && (magic[6] | 0x20U) == 0x70U && (magic[7] | 0x20U) == 0x65U && magic[8] == 0x20U) || (magic[3] == 0x53U && magic[4] == 0x20U && (magic[5] | 0x20U) == 0x74U && (magic[6] | 0x20U) == 0x61U && (magic[7] | 0x20U) == 0x70U && (magic[8] | 0x20U) == 0x65U && magic[9] == 0x20U)) && (magic[0x22] != 0U || magic[0x23] != 0U))) {
        bool valid = xx_format_probe_t64(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_T64;
    }

    if (is_corelltec) {
        if (xx_format_is_corelltec_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CORELLTEC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pcsecure) {
        if (xx_format_is_pcsecure_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PCSECURE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_rsvk) {
        if (xx_format_is_rsvk_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_RSVK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_saf) {
        if (xx_format_is_saf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SAF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sls) {
        if (xx_format_is_sls_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SLS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_gamos) {
        if (xx_format_is_gamos_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_GAMOS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_fpak) {
        if (xx_format_is_fpak_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FPAK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    /* Wave-1 firmware/filesystem readers. Order is load-bearing:
     * GPT first -- "EFI PART" plus a verified header CRC32 is the
     * strongest signal here, and running it first stops a GPT disk
     * being claimed by MBR through its protective entry. MBR goes
     * last and declines protective tables, so GPT still wins even if
     * this order is later disturbed. NTFS and FAT are more specific
     * than MBR and precede it. */
    if (is_gpt) {
        if (xx_format_is_gpt_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_GPT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_cramfs) {
        if (xx_format_is_cramfs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CRAMFS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ubi) {
        if (xx_format_is_ubi_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_UBI;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ubifs) {
        if (xx_format_is_ubifs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_UBIFS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sparse) {
        if (xx_format_is_sparse_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SPARSE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_androidboot) {
        if (xx_format_is_androidboot_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ANDROIDBOOT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_uimage) {
        if (xx_format_is_uimage_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_UIMAGE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_dtb) {
        if (xx_format_is_dtb_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DTB;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_jffs2) {
        if (xx_format_is_jffs2_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_JFFS2;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ext) {
        if (xx_format_is_ext_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_EXT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_fat) {
        if (xx_format_is_fat_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FAT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_mbr) {
        if (xx_format_is_mbr_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MBR;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    /* Wave-2 firmware/filesystem readers. Two orderings are
     * load-bearing: DLOB before SEAMA, because a DLOB *is* a SEAMA
     * chain whose first entity has size 0 and SEAMA accepts both
     * shapes; and the UEFI capsule before the firmware volume,
     * because a capsule commonly wraps an FV at a non-zero offset. */
    if (is_trx) {
        if (xx_format_is_trx_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TRX;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_qnap_nas_firmware) {
        if (xx_format_is_qnap_nas_firmware_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QNAP_NAS_FIRMWARE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_tplink) {
        if (xx_format_is_tplink_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TPLINK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_chk) {
        if (xx_format_is_chk_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CHK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_packimg) {
        if (xx_format_is_packimg_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PACKIMG;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_wince) {
        if (xx_format_is_wince_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_WINCE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_rtk) {
        if (xx_format_is_rtk_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_RTK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_binhdr) {
        if (xx_format_is_binhdr_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BINHDR;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_qcow) {
        if (xx_format_is_qcow_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QCOW;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_qcow1) {
        if (xx_format_is_qcow1_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QCOW1;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_luks) {
        if (xx_format_is_luks_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LUKS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_encrypted_apple_disk_image) {
        if (xx_format_is_encrypted_apple_disk_image_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ENCRYPTED_APPLE_DISK_IMAGE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_apfs) {
        if (xx_format_is_apfs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_APFS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_logfs) {
        if (xx_format_is_logfs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LOGFS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_dlob) {
        if (xx_format_is_dlob_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DLOB;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_seama) {
        if (xx_format_is_seama_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SEAMA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_dlink_tlv) {
        if (xx_format_is_dlink_tlv_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DLINK_TLV;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_mh01) {
        if (xx_format_is_mh01_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MH01;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_infogramesft) {
        if (xx_format_is_infogramesft_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_INFOGRAMESFT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* LIF's member name overlaps the fields that an unknown-GUID capsule
     * treats as its header size and flags. Its exact container extents and
     * complete LZD stream are stronger evidence than that capsule fallback. */
    if (is_lif) {
        bool valid = xx_format_is_lif_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LIF;
    }
    if (is_uefi_capsule) {
        if (xx_format_is_uefi_capsule_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_UEFI_CAPSULE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_uefi_fv) {
        if (xx_format_is_uefi_fv_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_UEFI_FV;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_qnx6) {
        if (xx_format_is_qnx6_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QNX6;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_btrfs) {
        if (xx_format_is_btrfs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BTRFS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_dmg) {
        if (xx_format_is_dmg_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DMG;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_applesingle) {
        if (xx_format_is_applesingle_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_APPLESINGLE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pp20) {
        if (xx_format_is_pp20_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PP20;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_beatthehouse) {
        if (xx_format_is_beatthehouse_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BEATTHEHOUSE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_kpck) {
        if (xx_format_is_kpck_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_KPCK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_perform) {
        if (xx_format_is_perform_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PERFORM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_mathcad) {
        if (xx_format_is_mathcad_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MATHCAD;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_netware2) {
        if (xx_format_is_netware2_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_NETWARE2;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_shar) {
        xx_makeself makeself;
        bool valid_makeself;
        xx_makeself_init(&makeself, dev, 0);
        valid_makeself = xx_makeself_check_is_valid(&makeself.format, NULL);
        xx_makeself_destroy(&makeself);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid_makeself) return XX_FILE_TYPE_MAKESELF;
        if (xx_format_is_shar_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SHAR;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_rnc) {
        if (xx_format_is_rnc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_RNC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ibmpack) {
        if (xx_format_is_ibmpack_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IBMPACK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_cazip) {
        if (xx_format_is_cazip_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CAZIP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_tpwm) {
        if (xx_format_is_tpwm_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TPWM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_mrnz) {
        if (xx_format_is_mrnz_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MRNZ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_edc) {
        if (xx_format_is_edc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_EDC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_xorarchive) {
        if (xx_format_is_xorarchive_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_XORARCHIVE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_mwave) {
        if (xx_format_is_mwave_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MWAVE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_finear) {
        if (xx_format_is_finear_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FINEAR;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lingvoarc) {
        if (xx_format_is_lingvoarc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LINGVOARC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_gst) {
        if (xx_format_is_gst_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_GST;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_winlink) {
        if (xx_format_is_winlink_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_WINLINK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ftcomp) {
        if (xx_format_is_ftcomp_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FTCOMP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_gpfpack) {
        if (xx_format_is_gpfpack_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_GPFPACK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sco) {
        if (xx_format_is_sco_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SCO;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_unixcompact) {
        if (xx_format_is_unixcompact_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_UNIX_COMPACT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_is3) {
        if (xx_format_is_is3_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IS3;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_is5) {
        if (xx_format_is_is5_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IS5;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_is7inx) {
        if (xx_format_is_is7inx_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IS7INX;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_edilzss) {
        if (xx_format_is_edilzss_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_EDILZSS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_savedskf) {
        if (xx_format_is_savedskf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SAVEDSKF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_gob) {
        if (xx_format_is_gob_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_GOB;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_debugscr) {
        if (xx_format_is_debugscr_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DEBUGSCR;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_stuffit) {
        if (xx_format_is_stuffit_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_STUFFIT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_stuffit_split_file) {
        if (xx_format_is_stuffit_split_file_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_STUFFIT_SPLIT_FILE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_binaryii) {
        if (xx_format_is_binaryii_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BINARYII;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_binhex) {
        if (xx_format_is_binhex_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BINHEX;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pma) {
        if (xx_format_is_pma_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PMA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lzk00) {
        if (xx_format_is_lzk00_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZK00;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_compaqlzh) {
        if (xx_format_is_compaqlzh_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_COMPAQLZH;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_arcv) {
        if (xx_format_is_arcv_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ARCV;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_is11) {
        if (xx_format_is_is11_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IS11;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_gksetup) {
        if (xx_format_is_gksetup_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_GKSETUP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_copyqm) {
        if (xx_format_is_copyqm_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_COPYQM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_teledisk) {
        if (xx_format_is_teledisk_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TELEDISK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_hfe) {
        if (xx_format_is_hfe_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_HFE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_hxc_stream_hfe) {
        if (xx_format_is_hxc_stream_hfe_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_HXC_STREAM_HFE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_fdi) {
        if (xx_format_is_fdi_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FDI;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_twoimg) {
        if (xx_format_is_twoimg_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TWOIMG;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_nec_pc_98_fdi) {
        if (xx_format_is_nec_pc_98_fdi_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_NEC_PC_98_FDI;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_t98_next_nfd) {
        if (xx_format_is_t98_next_nfd_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_T98_NEXT_NFD;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* PC Magazine FLP (pc_magazine_flp). */
    if ((magic_size >= 13U && magic[0] == 'P' && magic[1] == 'C' && magic[2] == 'M' && (magic[5] == 1U || magic[5] == 2U) && magic[6] == 0U && magic[7] != 0U && magic[8] == 0U && magic[9] != 0U && magic[10] == 0U)) {
        bool valid = xx_format_probe_pc_magazine_flp(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PC_MAGAZINE_FLP;
    }

    if (is_imd) {
        if (xx_format_is_imd_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IMD;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_diskdupe) {
        if (xx_format_is_diskdupe_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DISKDUPE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pmdiskcopy) {
        if (xx_format_is_pmdiskcopy_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PMDISKCOPY;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pax) {
        if (xx_format_is_pax_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PAX;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_beospkg) {
        if (xx_format_is_beospkg_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BEOSPKG;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_vmspcsi) {
        if (xx_format_is_vmspcsi_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_VMSPCSI;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_vmsdb) {
        if (xx_format_is_vmsdb_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_VMSDB;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pcxlib) {
        if (xx_format_is_pcxlib_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PCXLIB;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_hog2) {
        if (xx_format_is_hog2_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_HOG2;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sinner) {
        if (xx_format_is_sinner_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SINNER;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_psn) {
        if (xx_format_is_psn_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PSN;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_notetab) {
        if (xx_format_is_notetab_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_NOTETAB;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_mcc) {
        if (xx_format_is_mcc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MCC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_tnef) {
        if (xx_format_is_tnef_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TNEF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_opc) {
        if (xx_format_is_opc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_OPC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_qrst) {
        if (xx_format_is_qrst_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QRST;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_pain) {
        if (xx_format_is_pain_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PAIN;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_xlas) {
        if (xx_format_is_xlas_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_XLAS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_mdcd) {
        if (xx_format_is_mdcd_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MDCD;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ssm) {
        if (xx_format_is_ssm_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SSM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_bvrp) {
        if (xx_format_is_bvrp_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BVRP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_bcw) {
        if (xx_format_is_bcw_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BCW;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_scf) {
        if (xx_format_is_scf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SCF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_recognita) {
        if (xx_format_is_recognita_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_RECOGNITA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_jasc) {
        if (xx_format_is_jasc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_JASC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_smsipak) {
        if (xx_format_is_smsipak_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SMSIPAK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_cpx) {
        if (xx_format_is_cpx_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CPX;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_diskexpress) {
        if (xx_format_is_diskexpress_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DISKEXPRESS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_red) {
        if (xx_format_is_red_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_RED;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_gxl) {
        if (xx_format_is_gxl_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_GXL;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_aiaff) {
        if (xx_format_is_aiaff_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_AIAFF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_wim) {
        if (xx_format_is_wim_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_WIM;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_vmdk) {
        if (xx_format_is_vmdk_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_VMDK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* VDI (vdi). */
    if (magic_size >= 64U && total_size >= 456 && magic[0] == 0x3CU && magic[1] == 0x3CU && magic[2] == 0x3CU && magic[3] == 0x20U) {
        bool valid = xx_format_probe_vdi(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VDI;
    }

    if (is_apple_sparse_bundle) {
        if (xx_format_is_apple_sparse_bundle_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_APPLE_SPARSE_BUNDLE;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ciso) {
        if (xx_format_is_ciso_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return xx_rt_memcmp(magic, "DAX\0", 4U) == 0 ? XX_FILE_TYPE_DAX :
                   xx_rt_memcmp(magic, "ZISO", 4U) == 0 ? XX_FILE_TYPE_ZISO :
                   magic_size >= 21U && magic[20] == 2U ? XX_FILE_TYPE_CISO2 :
                   XX_FILE_TYPE_CISO;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_copydisk) {
        if (xx_format_is_copydisk_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_COPYDISK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_hdcopy) {
        if (xx_format_is_hdcopy_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_HDCOPY;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_apricot) {
        if (xx_format_is_apricot_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_APRICOT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sabdu) {
        if (xx_format_is_sabdu_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SABDU;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_mpq) {
        if (xx_format_is_mpq_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MPQ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_phar) {
        if (xx_format_is_phar_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PHAR;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_sq) {
        if (xx_format_is_sq_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SQ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_squeeze2) {
        if (xx_format_is_squeeze2_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SQUEEZE2;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_dbz) {
        if (xx_format_is_dbz_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DBZ;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_stac) {
        if (xx_format_is_stac_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_STAC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_spk) {
        if (xx_format_is_spk_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SPK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_wrzl) {
        if (xx_format_is_wrzl_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_WRZL;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_bagf) {
        if (xx_format_is_bagf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BAGF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_emt) {
        if (xx_format_is_emt_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_EMT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_qip2) {
        if (xx_format_is_qip2_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QIP2;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ixa) {
        if (xx_format_is_ixa_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_IXA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_mlb_ft) {
        if (xx_format_is_mlb_ft_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_MLB_FT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_fss) {
        if (xx_format_is_fss_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FSS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_epf) {
        if (xx_format_is_epf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_EPF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ka) {
        if (xx_format_is_ka_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_KA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_dn) {
        if (xx_format_is_dn_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DN;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_insa) {
        if (xx_format_is_insa_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_INSA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_dfc) {
        if (xx_format_is_dfc_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DFC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ppd) {
        if (xx_format_is_ppd_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PPD;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_thebat_msb) {
        if (xx_format_is_thebat_msb_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_THEBAT_MSB;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lspack10) {
        if (xx_format_is_lspack10_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LSPACK10;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_starkit) {
        if (xx_format_is_starkit_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_STARKIT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_paperport) {
        if (xx_format_is_paperport_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_PAPERPORT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_rnca) {
        if (xx_format_is_rnca_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_RNCA;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_lzpis2) {
        if (xx_format_is_lzpis2_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LZPIS2;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_hog) {
        if (xx_format_is_hog_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_HOG;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_agis) {
        if (xx_format_is_agis_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_AGIS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_volitionvpft) {
        if (xx_format_is_volitionvpft_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_VOLITIONVPFT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_wintermutedcp) {
        if (xx_format_is_wintermutedcp_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_WINTERMUTEDCP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_bsn) {
        if (xx_format_is_bsn_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BSN;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_shrs) {
        if (xx_format_is_shrs_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SHRS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_srec) {
        if (xx_format_is_srec_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SREC;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* CUE sheet (cue). */
    if (xx_cue_test_magic(magic, magic_size)) {
        bool valid = xx_format_probe_cue(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CUE;
    }

    if (is_dms) {
        if (xx_format_is_dms_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_DMS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_csman) {
        if (xx_format_is_csman_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_CSMAN;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    if (is_ecos) {
        if (xx_format_is_ecos_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ECOS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_jboot) {
        if (xx_format_is_jboot_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_JBOOT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* These carry no signature either -- a record tag byte, a name
     * length, a single constant field. Their prefilters cannot
     * identify anything on their own, and several of their probes
     * trial-decode a stream, so they go after every format that can
     * name itself. */
    if (is_fld) {
        if (xx_format_is_fld_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_FLD;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_qualitas) {
        if (xx_format_is_qualitas_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_QUALITAS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_bwf) {
        if (xx_format_is_bwf_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_BWF;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_zap) {
        if (xx_format_is_zap_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ZAP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_stork) {
        if (xx_format_is_stork_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_STORK;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    if (is_ascendbackup) {
        if (xx_format_is_ascendbackup_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_ASCENDBACKUP;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    if (is_twrx) {
        if (xx_format_is_twrx_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TWRX;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* TWS carries no signature, only three fixed header fields, so it goes
     * after every format that can identify itself by magic. */
    if (is_tws) {
        if (xx_format_is_tws_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_TWS;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }

    if (is_silmarilsft) {
        if (xx_format_is_silmarilsft_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_SILMARILSFT;
        }
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    }
    /* AP4 has no magic either, and its table of contents can sit anywhere in
     * the first 64 KiB, so no fixed-offset prefilter is possible -- the gate
     * is the whole table chain plus the MPEG classification of the first
     * non-zero member.  Last, for the same reason as GAS Huffman above, and
     * after it because its scan is the more expensive of the two. */
    if (xx_format_is_ap4_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_AP4;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);

    /* Magicless formats, ordered most constrained first. Each is decided by
     * decoding its whole stream, so a false positive costs a wrong answer
     * rather than a crash -- and reaching here at all means every format
     * that announces itself has already declined. */
    if (xx_format_is_resourcefork_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_RESOURCEFORK;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_macbinary_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_MACBINARY;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_battleisle_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_BATTLEISLE;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_mxs_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_MXS;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_psdc_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_PSDC;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_dclft_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_DCLFT;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_trdos_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_TRDOS;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_pcinstall_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_PCINSTALL;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_diskjuggler_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_DISKJUGGLER;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_grasp_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_GRASP;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_megatechvol_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_MEGATECHVOL;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_stunts_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_STUNTS;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_binder_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_BINDER;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_csidos_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_CSIDOS;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_cat_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_CAT;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_bnd_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_BND;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_shrinkwrap_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_SHRINKWRAP;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_res_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_RES;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_rsc_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_RSC;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_teacy_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_TEACY;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_settlersft_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_SETTLERSFT;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_wolfft_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_WOLFFT;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_boo_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_BOO;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_vmssaveset_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_VMSSAVESET;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_rawstac_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_RAWSTAC;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_topspeed_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_TOPSPEED;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_panorama_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_PANORAMA;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_lzw15v_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_RAW_LZW15V;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_arcadyan_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_ARCADYAN;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_encfw_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_ENCFW;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_uboot_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_UBOOT_ENV;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_pdb_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_PDB;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_dclraw_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_DCLRAW;
    }
    {
        xx_file_type_t registered = xx_format_probe_registered_fallback(&registered_probes);
        if (registered != XX_FILE_TYPE_UNKNOWN) return registered;
    }

    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_raw_deflate_compressed_data_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_RAW_DEFLATE_COMPRESSED_DATA;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_ns2_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_NS2;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_nsa_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_NSA;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_rdb_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_RDB;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_trs_80_jv1_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_TRS_80_JV1;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_trs_80_jv3_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_TRS_80_JV3;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (xx_format_is_x68000_dim_device(dev)) {
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        return XX_FILE_TYPE_X68000_DIM;
    }
    /* Parsec DAT has no magic. Its complete offset/size table, zero
     * sentinel, contiguous RIB/SM8 members, and inner RIB size bounds
     * must all validate before it is identified. */
    if (magic_size >= 4U) {
        uint32_t table_size = (uint32_t)magic[0] |
            ((uint32_t)magic[1] << 8U) |
            ((uint32_t)magic[2] << 16U) |
            ((uint32_t)magic[3] << 24U);
        if (table_size >= 12U && (table_size - 4U) % 8U == 0U &&
            (uint64_t)table_size <= (uint64_t)total_size) {
        xx_parsec_archive reader;
        bool valid;
        xx_parsec_archive_init(&reader, dev, 0);
        valid = reader.format.check_is_valid(&reader.format, NULL);
        xx_parsec_archive_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PARSEC_ARCHIVE;
        }
    }
    /* Westwood PAK has no magic: its exact directory boundary and entry
     * offsets must validate before this final fallback can identify it. */
    {
        xx_westwood_pak reader;
        bool valid;
        xx_westwood_pak_init(&reader, dev, 0);
        valid = xx_westwood_pak_check_is_valid(&reader.format, NULL);
        xx_westwood_pak_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WESTWOOD_PAK;
    }
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
#ifndef XXFC_FORMATS_ONLY
    {
        xx_file_type_t music_type = xx_die_music_detect_device(dev);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (music_type != XX_FILE_TYPE_UNKNOWN) return music_type;
    }
#endif
    return XX_FILE_TYPE_BINARY;
#endif /* XXFC_FORMAT_DETECTION_LZMA_XZ_ONLY */
}

#ifndef XXFC_FORMAT_DETECTION_LZMA_XZ_ONLY
XX_FORMAT_NOINLINE xx_file_type_t xx_format_get_file_type_device(xx_io_device *dev) {
    int64_t size;
    xx_file_type_t packed;
    if (!dev) return XX_FILE_TYPE_UNKNOWN;
    size = xx_io_total_size(dev);
    if (size <= 0) return XX_FILE_TYPE_UNKNOWN;
    if (size < 4) return XX_FILE_TYPE_BINARY;
    /* Run the bounded helper before the broad detector allocates its legacy
     * reader frame. Both functions stay separate even under link-time inlining.
     * Damaged packed carriers retain their type and cannot expose raw sections. */
    if (xx_upx_has_marker_device(dev, NULL)) {
        packed = xx_upx_detect_device(dev, NULL);
        return packed != XX_FILE_TYPE_UNKNOWN ? packed : XX_FILE_TYPE_UPX;
    }
    return xx_format_get_unpacked_file_type_device(dev);
}
#endif
