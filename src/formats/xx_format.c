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

static void xx_format_extra_parameter_free_elem(void *element);

static void xx_format_invalidate_password_state(Abstractformat *format) {
    if (!format) {
        return;
    }
    /* Header-encrypted formats must get another preparation/base-info pass
     * after their format-wide password changes. Concrete split handlers are
     * responsible for rebuilding any password-derived private view. */
    format->split_format_handled = false;
    format->base_info_handled = false;
    format->is_valid = false;
    xx_format_invalidate_memory_map(format);
}

void xx_format_init(Abstractformat *fmt, xx_io_device *dev, int64_t base_address) {
    if (!fmt) {
        return;
    }
    xx_mem_zero(fmt, sizeof(Abstractformat));
    fmt->device = dev;
    fmt->base_address = base_address;
    fmt->is_mapped = false;
    fmt->base_info_handled = false;
    fmt->is_valid = false;
    fmt->format_size = -1;
    fmt->overlay_offset = -1;
    fmt->overlay_size = 0;
    fmt->endian = XX_ENDIAN_UNKNOWN;
    fmt->file_type = XX_FILE_TYPE_UNKNOWN;
    fmt->os = XX_OS_UNKNOWN;
    fmt->format_type = XX_FORMAT_TYPE_UNKNOWN;
    fmt->arch = XX_ARCH_UNKNOWN;
    fmt->is_executable = false;
    fmt->is_archive = false;
    fmt->is_signed = false;
    fmt->is_crypted = false;
    fmt->number_of_imports = 0;
    fmt->number_of_exports = 0;
    fmt->number_of_resources = 0;
    fmt->number_of_metadata = 0;
    fmt->number_of_archive_records = 0;
    fmt->module_address = XX_INVALID_ADDRESS;
    xx_memory_map_init(&fmt->memory_map);
    xx_list_init(&fmt->list_extra_parameters, sizeof(xx_meta),
                 xx_format_extra_parameter_free_elem);
}

void xx_format_invalidate_memory_map(Abstractformat *format) {
    bool was_handling;
    if (!format) return;
    was_handling = format->memory_map_handling;
    xx_memory_map_cleanup(&format->memory_map);
    format->memory_map_handled = false;
    /* Do not erase the recursion guard if an invalidating setter is called
     * from inside a concrete producer. */
    format->memory_map_handling = was_handling;
    format->memory_map_requested_mode = XX_MEMORY_MAP_MODE_UNKNOWN;
}

static bool xx_format_get_default_memory_map(Abstractformat *format,
                                             xx_memory_map_mode_t mode,
                                             xx_memory_map *output) {
    xx_memory_record record;
    int64_t total;
    int64_t size;
    if (!format || !format->device || !output || format->base_address < 0)
        return false;
    if (mode != XX_MEMORY_MAP_MODE_UNKNOWN &&
        mode != XX_MEMORY_MAP_MODE_REGIONS)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    output->binary_offset = format->base_address;
    output->module_address = format->module_address != XX_INVALID_ADDRESS
                                 ? format->module_address
                                 : 0U;
    output->is_image = format->is_mapped;
    output->binary_size = size;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = XX_MEMORY_MAP_MODE_REGIONS;
    if (size == 0) return true;
    xx_mem_zero(&record, sizeof(record));
    record.offset = format->base_address;
    record.address = output->module_address;
    record.size = size;
    record.file_part = XX_FILE_PART_REGION;
    record.file_part_number = 0;
    (void)xx_rt_snprintf(record.name, sizeof(record.name), "%s", "Binary");
    return xx_memory_map_add_record(output, &record);
}

bool xx_format_handle_memory_map(Abstractformat *format,
                                 xx_memory_map_mode_t mode,
                                 xx_pd_struct *pd) {
    xx_memory_map result;
    bool built;
    if (!format || xx_pd_is_stopped(pd)) return false;
    if (!xx_format_handle_split_format(format, pd)) return false;
    if (format->memory_map_handled &&
        (format->memory_map_requested_mode == mode ||
         (mode != XX_MEMORY_MAP_MODE_UNKNOWN &&
          format->memory_map.mode == mode)))
        return true;
    if (format->memory_map_handling) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG,
                        "Recursive memory-map construction");
        return false;
    }
    if (!format->base_info_handled && format->handle_base_info &&
        !xx_format_handle_base_info(format, pd))
        return false;
    /* A concrete base-info handler may construct the default map because it
     * needs that same map for parsing address-based tables (PE does this). */
    if (format->memory_map_handled &&
        (format->memory_map_requested_mode == mode ||
         (mode != XX_MEMORY_MAP_MODE_UNKNOWN &&
          format->memory_map.mode == mode)))
        return true;
    format->memory_map_handling = true;
    xx_memory_map_init(&result);
    built = format->get_memory_map
                ? format->get_memory_map(format, mode, &result, pd)
                : xx_format_get_default_memory_map(format, mode, &result);
    if (built && result.mode == XX_MEMORY_MAP_MODE_UNKNOWN &&
        mode != XX_MEMORY_MAP_MODE_UNKNOWN)
        result.mode = mode;
    if (built) built = xx_memory_map_finalize(&result);
    if (!built || xx_pd_is_stopped(pd)) {
        xx_memory_map_cleanup(&result);
        format->memory_map_handling = false;
        return false;
    }
    xx_memory_map_cleanup(&format->memory_map);
    format->memory_map = result;
    format->memory_map_handled = true;
    format->memory_map_handling = false;
    format->memory_map_requested_mode = mode;
    return true;
}

const xx_memory_map *xx_format_get_memory_map(Abstractformat *format,
                                               xx_memory_map_mode_t mode,
                                               xx_pd_struct *pd) {
    return xx_format_handle_memory_map(format, mode, pd)
               ? &format->memory_map
               : NULL;
}

uint64_t xx_format_offset_to_address(Abstractformat *format, int64_t offset,
                                     xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_offset_to_address(map, offset)
               : XX_INVALID_ADDRESS;
}

int64_t xx_format_address_to_offset(Abstractformat *format, uint64_t address,
                                    xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_address_to_offset(map, address) : -1;
}

uint64_t xx_format_offset_to_rel_address(Abstractformat *format,
                                          int64_t offset, xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_offset_to_relative_address(map, offset)
               : XX_INVALID_ADDRESS;
}

int64_t xx_format_rel_address_to_offset(Abstractformat *format,
                                        int64_t relative_address,
                                        xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_relative_address_to_offset(map,
                                                           relative_address)
               : -1;
}

uint64_t xx_format_rel_address_to_address(Abstractformat *format,
                                           int64_t relative_address,
                                           xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_relative_address_to_address(map,
                                                            relative_address)
               : XX_INVALID_ADDRESS;
}

int64_t xx_format_address_to_rel_address(Abstractformat *format,
                                          uint64_t address,
                                          xx_pd_struct *pd) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    return map ? xx_memory_map_address_to_relative_address(map, address) : -1;
}

bool xx_format_handle_split_format(Abstractformat *format, xx_pd_struct *pd) {
    bool handled;
    if (!format) {
        return false;
    }
    /* Keep formats without a split handler entirely on their legacy path. */
    if (!format->handle_split_format) {
        return true;
    }
    if (xx_pd_is_stopped(pd)) {
        return false;
    }
    if (format->split_format_handling) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Recursive split-format preparation");
        return false;
    }
    if (format->split_format_handled) {
        return true;
    }
    format->split_format_handling = true;
    handled = format->handle_split_format(format, pd);
    format->split_format_handling = false;
    if (!handled || xx_pd_is_stopped(pd)) {
        return false;
    }
    format->split_format_handled = true;
    return true;
}

static void xx_format_secure_clear_parameter(xx_meta *meta) {
    xx_var *value;
    if (!meta || meta->meta_id != XX_META_ID_OPT_PASSWORD) {
        return;
    }
    value = &meta->var;
    if (!value->is_allocated) {
        return;
    }
    if (value->type == XX_VAR_TYPE_STRING && value->val.str.ptr) {
        xx_mem_zero(value->val.str.ptr, value->val.str.len);
    } else if (value->type == XX_VAR_TYPE_WSTRING && value->val.wstr.ptr) {
        xx_mem_zero(value->val.wstr.ptr,
                    value->val.wstr.len * sizeof(wchar_t));
    } else if (value->type == XX_VAR_TYPE_BYTES && value->val.bytes.data) {
        xx_mem_zero(value->val.bytes.data, value->val.bytes.size);
    }
}

static void xx_format_extra_parameter_free_elem(void *element) {
    xx_meta *meta = (xx_meta *)element;
    if (!meta) {
        return;
    }
    xx_format_secure_clear_parameter(meta);
    xx_meta_cleanup(meta);
}

static bool xx_format_copy_parameter_value(xx_var *destination,
                                           const xx_var *source) {
    if (!destination || !source) {
        return false;
    }
    switch ((xx_var_type_t)source->type) {
        case XX_VAR_TYPE_STRING_VIEW: {
            char *copy;
            if ((!source->val.str.ptr && source->val.str.len != 0U) ||
                source->val.str.len == SIZE_MAX) {
                return false;
            }
            copy = (char *)xx_mem_alloc(source->val.str.len + 1U);
            if (!copy) {
                return false;
            }
            if (source->val.str.len != 0U) {
                xx_mem_copy(copy, source->val.str.ptr, source->val.str.len);
            }
            copy[source->val.str.len] = '\0';
            return xx_var_set_str_take(destination, copy,
                                       source->val.str.len);
        }
        case XX_VAR_TYPE_WSTRING_VIEW: {
            wchar_t *copy;
            if ((!source->val.wstr.ptr && source->val.wstr.len != 0U) ||
                source->val.wstr.len >
                    (SIZE_MAX / sizeof(wchar_t)) - 1U) {
                return false;
            }
            copy = (wchar_t *)xx_mem_alloc(
                (source->val.wstr.len + 1U) * sizeof(wchar_t));
            if (!copy) {
                return false;
            }
            if (source->val.wstr.len != 0U) {
                xx_mem_copy(copy, source->val.wstr.ptr,
                            source->val.wstr.len * sizeof(wchar_t));
            }
            copy[source->val.wstr.len] = L'\0';
            return xx_var_set_wstr_take(destination, copy,
                                        source->val.wstr.len);
        }
        case XX_VAR_TYPE_BYTES_VIEW:
            return xx_var_set_bytes(destination, source->val.bytes.data,
                                    source->val.bytes.size);
        default:
            return xx_var_copy(destination, source);
    }
}

const xx_var *xx_format_find_extra_parameter(const Abstractformat *format,
                                              uint32_t meta_id) {
    size_t i;
    if (!format) {
        return NULL;
    }
    for (i = 0; i < format->list_extra_parameters.count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at(
            &format->list_extra_parameters, i);
        if (meta && meta->meta_id == meta_id) {
            return &meta->var;
        }
    }
    return NULL;
}

const xx_var *xx_format_resolve_extra_parameter(
    const Abstractformat *format, const xx_list_s *operation_parameters,
    uint32_t meta_id) {
    size_t i;
    if (operation_parameters) {
        for (i = 0; i < operation_parameters->count; ++i) {
            const xx_meta *meta = (const xx_meta *)xx_list_at(
                operation_parameters, i);
            if (meta && meta->meta_id == meta_id) {
                return &meta->var;
            }
        }
    }
    return xx_format_find_extra_parameter(format, meta_id);
}

bool xx_format_set_extra_parameter(Abstractformat *format, uint32_t meta_id,
                                   const xx_var *value) {
    xx_var replacement;
    size_t i;
    if (!format) {
        return false;
    }
    if (!value) {
        (void)xx_format_remove_extra_parameter(format, meta_id);
        return true;
    }
    if (format->list_extra_parameters.elem_size == 0U &&
        !xx_list_init(&format->list_extra_parameters, sizeof(xx_meta),
                      xx_format_extra_parameter_free_elem)) {
        return false;
    }
    format->list_extra_parameters.elem_free =
        xx_format_extra_parameter_free_elem;
    xx_var_init(&replacement);
    if (!xx_format_copy_parameter_value(&replacement, value)) {
        return false;
    }
    for (i = 0; i < format->list_extra_parameters.count; ++i) {
        xx_meta *meta = (xx_meta *)xx_list_at(
            &format->list_extra_parameters, i);
        if (meta && meta->meta_id == meta_id) {
            xx_format_secure_clear_parameter(meta);
            xx_var_cleanup(&meta->var);
            meta->var = replacement;
            if (meta_id == XX_META_ID_OPT_PASSWORD) {
                xx_format_invalidate_password_state(format);
            }
            return true;
        }
    }
    {
        xx_meta meta;
        xx_meta_init(&meta, meta_id);
        meta.var = replacement;
        if (!xx_list_append(&format->list_extra_parameters, &meta)) {
            xx_format_extra_parameter_free_elem(&meta);
            return false;
        }
    }
    if (meta_id == XX_META_ID_OPT_PASSWORD) {
        xx_format_invalidate_password_state(format);
    }
    return true;
}

bool xx_format_remove_extra_parameter(Abstractformat *format,
                                      uint32_t meta_id) {
    size_t i;
    bool removed = false;
    if (!format) {
        return false;
    }
    format->list_extra_parameters.elem_free =
        xx_format_extra_parameter_free_elem;
    i = format->list_extra_parameters.count;
    while (i != 0U) {
        xx_meta *meta;
        --i;
        meta = (xx_meta *)xx_list_at(&format->list_extra_parameters, i);
        if (meta && meta->meta_id == meta_id &&
            xx_list_remove_at(&format->list_extra_parameters, i)) {
            removed = true;
        }
    }
    if (removed && meta_id == XX_META_ID_OPT_PASSWORD) {
        xx_format_invalidate_password_state(format);
    }
    return removed;
}

void xx_format_cleanup_extra_parameters(Abstractformat *format) {
    if (!format) {
        return;
    }
    format->list_extra_parameters.elem_free =
        xx_format_extra_parameter_free_elem;
    xx_list_cleanup(&format->list_extra_parameters);
    xx_format_invalidate_memory_map(format);
}

bool xx_format_set_password(Abstractformat *format,
                            const char *password_utf8) {
    xx_meta sensitive;
    xx_var value;
    bool result;
    if (!format) {
        return false;
    }
    if (!password_utf8) {
        (void)xx_format_remove_extra_parameter(format,
                                               XX_META_ID_OPT_PASSWORD);
        return true;
    }
    xx_var_init(&value);
    if (!xx_var_set_str(&value, password_utf8)) {
        return false;
    }
    result = xx_format_set_extra_parameter(format,
                                           XX_META_ID_OPT_PASSWORD, &value);
    sensitive.meta_id = XX_META_ID_OPT_PASSWORD;
    sensitive.var = value;
    xx_format_secure_clear_parameter(&sensitive);
    xx_var_cleanup(&value);
    return result;
}

const char *xx_format_get_password(const Abstractformat *format) {
    const xx_var *value = xx_format_find_extra_parameter(
        format, XX_META_ID_OPT_PASSWORD);
    return value ? xx_var_get_str(value) : NULL;
}

xx_format_type_t xx_format_get_type(Abstractformat *f) {
    if (!f) {
        return XX_TYPE_UNKNOWN;
    }
    if (f->get_type) {
        return f->get_type(f);
    }
    return f->format_type;
}

void xx_format_set_type(Abstractformat *f, xx_format_type_t type) {
    if (f) {
        if (f->format_type != type)
            xx_format_invalidate_memory_map(f);
        f->format_type = type;
    }
}

xx_endian_t xx_format_get_endian(Abstractformat *f) {
    if (!f) {
        return XX_ENDIAN_UNKNOWN;
    }
    if (f->get_endian) {
        return f->get_endian(f);
    }
    return f->endian;
}

void xx_format_set_endian(Abstractformat *f, xx_endian_t endian) {
    if (f) {
        if (f->endian != endian)
            xx_format_invalidate_memory_map(f);
        f->endian = endian;
    }
}

xx_os_t xx_format_get_os(Abstractformat *f) {
    if (!f) {
        return XX_OS_UNKNOWN;
    }
    if (f->get_os) {
        return f->get_os(f);
    }
    return f->os;
}

void xx_format_set_os(Abstractformat *f, xx_os_t os) {
    if (f) {
        f->os = os;
    }
}

xx_arch_t xx_format_get_arch(Abstractformat *f) {
    if (!f) {
        return XX_ARCH_UNKNOWN;
    }
    if (f->get_arch) {
        return f->get_arch(f);
    }
    return f->arch;
}

void xx_format_set_arch(Abstractformat *f, xx_arch_t arch) {
    if (f) {
        if (f->arch != arch)
            xx_format_invalidate_memory_map(f);
        f->arch = arch;
    }
}

const char *xx_type_to_string(xx_format_type_t type) {
    switch (type) {
        case XX_TYPE_CONSOLE_APPLICATION: return "Console application";
        case XX_TYPE_GUI_APPLICATION:     return "GUI application";
        case XX_TYPE_LIBRARY:             return "Library";
        case XX_TYPE_DRIVER:              return "Driver";
        case XX_TYPE_STATIC_LIBRARY:      return "Static library";
        case XX_TYPE_SERVICE:             return "Service";
        case XX_TYPE_DAEMON:              return "Daemon";
        case XX_TYPE_BOOT:                return "Boot";
        case XX_TYPE_FIRMWARE:            return "Firmware";
        case XX_TYPE_OBJECT:              return "Object";
        case XX_TYPE_PACKAGE:             return "Package";
        case XX_TYPE_RAW:                 return "Raw";
        case XX_TYPE_ARCHIVE:             return "Archive";
        default:                          return "Unknown";
    }
}

const char *xx_arch_to_string(xx_arch_t arch) {
    switch (arch) {
        case XX_ARCH_X86_64:  return "x86_64";
        case XX_ARCH_X86:     return "x86";
        case XX_ARCH_X86_16:  return "x86_16";
        case XX_ARCH_ARM:     return "arm";
        case XX_ARCH_ARM64:   return "arm64";
        case XX_ARCH_MIPS:    return "mips";
        case XX_ARCH_MIPS64:  return "mips64";
        case XX_ARCH_PPC:     return "ppc";
        case XX_ARCH_PPC64:   return "ppc64";
        case XX_ARCH_RISCV:   return "riscv";
        case XX_ARCH_RISCV64: return "riscv64";
        case XX_ARCH_SPARC:   return "sparc";
        case XX_ARCH_SPARC64: return "sparc64";
        case XX_ARCH_M68K:    return "m68k";
        case XX_ARCH_AVR:     return "avr";
        case XX_ARCH_SH:      return "sh";
        case XX_ARCH_WASM:    return "wasm";
        case XX_ARCH_JVM:     return "jvm";
        case XX_ARCH_DOTNET:  return "cil";
        case XX_ARCH_DALVIK:  return "dalvik";
        case XX_ARCH_GENERIC: return "Generic";
        default:              return "Unknown";
    }
}

const char *xx_os_to_string(xx_os_t os) {
    switch (os) {
        case XX_OS_WINDOWS: return "Windows";
        case XX_OS_LINUX:   return "Linux";
        case XX_OS_MACOS:   return "macOS";
        case XX_OS_UNIX:    return "Unix";
        case XX_OS_DOS:     return "DOS";
        case XX_OS_FREEBSD: return "FreeBSD";
        case XX_OS_ANDROID: return "Android";
        case XX_OS_IOS:     return "iOS";
        case XX_OS_OS2:     return "OS/2";
        case XX_OS_GENERIC: return "Generic";
        default:            return "Unknown";
    }
}


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

static bool xx_format_is_tar_lz4_device(xx_io_device *device) {
    xx_tar_lz4 tar_lz4;
    bool result;
    if (!device) return false;
    xx_tar_lz4_init(&tar_lz4, device, 0);
    result = xx_tar_lz4_handle_base_info(&tar_lz4.format, NULL);
    xx_tar_lz4_destroy(&tar_lz4);
    return result;
}

static bool xx_format_is_lz4_device(xx_io_device *device) {
    xx_lz4 lz4;
    bool result;
    if (!device) return false;
    xx_lz4_init(&lz4, device, 0);
    result = xx_lz4_handle_base_info(&lz4.format, NULL);
    xx_lz4_destroy(&lz4);
    return result;
}

static bool xx_format_is_lz5_device(xx_io_device *device) {
    xx_lz5 lz5;
    bool result;
    if (!device) return false;
    xx_lz5_init(&lz5, device, 0);
    result = xx_lz5_handle_base_info(&lz5.format, NULL);
    xx_lz5_destroy(&lz5);
    return result;
}

static bool xx_format_is_lizard_device(xx_io_device *device) {
    xx_lizard lizard;
    bool result;
    if (!device) return false;
    xx_lizard_init(&lizard, device, 0);
    result = xx_lizard_handle_base_info(&lizard.format, NULL);
    xx_lizard_destroy(&lizard);
    return result;
}

/* Raw Brotli streams intentionally have no universal signature.  Only the
 * independent-frame wrapper emitted by the 7-Zip Brotli codec is safe to
 * recognise automatically; raw streams remain available through xx_brotli. */
static bool xx_format_is_brotli_device(xx_io_device *device) {
    xx_brotli brotli;
    bool result;
    if (!device) return false;
    xx_brotli_init(&brotli, device, 0);
    result = xx_brotli_handle_base_info(&brotli.format, NULL);
    xx_brotli_destroy(&brotli);
    return result;
}

static bool xx_format_is_unixpack_device(xx_io_device *device) {
    xx_unixpack unixpack;
    bool result;
    if (!device) return false;
    xx_unixpack_init(&unixpack, device, 0);
    result = xx_unixpack_handle_base_info(&unixpack.format, NULL);
    xx_unixpack_destroy(&unixpack);
    return result;
}

static bool xx_format_is_zlib_device(xx_io_device *device) {
    xx_zlib zlib;
    bool result;
    if (!device) return false;
    xx_zlib_init(&zlib, device, 0);
    result = xx_zlib_handle_base_info(&zlib.format, NULL);
    xx_zlib_destroy(&zlib);
    return result;
}

static bool xx_format_is_unixcompress_device(xx_io_device *device) {
    xx_unixcompress unixcompress;
    bool result;
    if (!device) return false;
    xx_unixcompress_init(&unixcompress, device, 0);
    result = xx_unixcompress_handle_base_info(&unixcompress.format, NULL);
    xx_unixcompress_destroy(&unixcompress);
    return result;
}

static bool xx_format_is_gitobject_device(xx_io_device *device) {
    xx_gitobject gitobject;
    bool result;
    if (!device) return false;
    xx_gitobject_init(&gitobject, device, 0);
    result = xx_gitobject_handle_base_info(&gitobject.format, NULL);
    xx_gitobject_destroy(&gitobject);
    return result;
}

static bool xx_format_is_mscompress_device(xx_io_device *device) {
    xx_mscompress mscompress;
    bool result;
    if (!device) return false;
    xx_mscompress_init(&mscompress, device, 0);
    result = xx_mscompress_handle_base_info(&mscompress.format, NULL);
    xx_mscompress_destroy(&mscompress);
    return result;
}

static bool xx_format_is_ash0_device(xx_io_device *device) {
    xx_ash0 ash0;
    bool result;
    if (!device) return false;
    xx_ash0_init(&ash0, device, 0);
    result = xx_ash0_handle_base_info(&ash0.format, NULL);
    xx_ash0_destroy(&ash0);
    return result;
}

static bool xx_format_is_wiilz77_device(xx_io_device *device) {
    xx_wiilz77 wiilz77;
    bool result;
    if (!device) return false;
    xx_wiilz77_init(&wiilz77, device, 0);
    result = xx_wiilz77_handle_base_info(&wiilz77.format, NULL);
    xx_wiilz77_destroy(&wiilz77);
    return result;
}

static bool xx_format_is_lzv1_device(xx_io_device *device) {
    xx_lzv1 lzv1;
    bool result;
    if (!device) return false;
    xx_lzv1_init(&lzv1, device, 0);
    result = xx_lzv1_handle_base_info(&lzv1.format, NULL);
    xx_lzv1_destroy(&lzv1);
    return result;
}

static bool xx_format_is_oraclesqueeze_device(xx_io_device *device) {
    xx_oraclesqueeze oraclesqueeze;
    bool result;
    if (!device) return false;
    xx_oraclesqueeze_init(&oraclesqueeze, device, 0);
    result = xx_oraclesqueeze_handle_base_info(&oraclesqueeze.format, NULL);
    xx_oraclesqueeze_destroy(&oraclesqueeze);
    return result;
}

static bool xx_format_is_softronics_device(xx_io_device *device) {
    xx_softronics softronics;
    bool result;
    if (!device) return false;
    xx_softronics_init(&softronics, device, 0);
    result = xx_softronics_handle_base_info(&softronics.format, NULL);
    xx_softronics_destroy(&softronics);
    return result;
}

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

static bool xx_format_is_dmapacked_device(xx_io_device *device) {
    xx_dmapacked dmapacked;
    bool result;
    if (!device) return false;
    xx_dmapacked_init(&dmapacked, device, 0);
    result = xx_dmapacked_handle_base_info(&dmapacked.format, NULL);
    xx_dmapacked_destroy(&dmapacked);
    return result;
}

static bool xx_format_is_gashuff_device(xx_io_device *device) {
    xx_gashuff gashuff;
    bool result;
    if (!device) return false;
    xx_gashuff_init(&gashuff, device, 0);
    result = xx_gashuff_handle_base_info(&gashuff.format, NULL);
    xx_gashuff_destroy(&gashuff);
    return result;
}

static bool xx_format_is_huf_device(xx_io_device *device) {
    xx_huf huf;
    bool result;
    if (!device) return false;
    xx_huf_init(&huf, device, 0);
    result = xx_huf_handle_base_info(&huf.format, NULL);
    xx_huf_destroy(&huf);
    return result;
}

static bool xx_format_is_lzdiet_device(xx_io_device *device) {
    xx_lzdiet lzdiet;
    bool result;
    if (!device) return false;
    xx_lzdiet_init(&lzdiet, device, 0);
    result = xx_lzdiet_handle_base_info(&lzdiet.format, NULL);
    xx_lzdiet_destroy(&lzdiet);
    return result;
}

static bool xx_format_is_bcm_device(xx_io_device *device) {
    xx_bcm bcm;
    bool result;

    xx_bcm_init(&bcm, device, 0);
    result = xx_bcm_handle_base_info(&bcm.format, NULL);
    xx_bcm_destroy(&bcm);
    return result;
}

static bool xx_format_is_lpaq8_device(xx_io_device *device) {
    xx_lpaq8 value;
    bool result;

    xx_lpaq8_init(&value, device, 0);
    result = xx_lpaq8_handle_base_info(&value.format, NULL);
    xx_lpaq8_destroy(&value);
    return result;
}

static bool xx_format_is_pea_device(xx_io_device *device) {
    xx_pea value;
    bool result;

    xx_pea_init(&value, device, 0);
    result = xx_pea_handle_base_info(&value.format, NULL);
    xx_pea_destroy(&value);
    return result;
}

static bool xx_format_is_zpaq_device(xx_io_device *device) {
    xx_zpaq value;
    bool result;

    xx_zpaq_init(&value, device, 0);
    result = xx_zpaq_handle_base_info(&value.format, NULL);
    xx_zpaq_destroy(&value);
    return result;
}

static bool xx_format_is_freearc_device(xx_io_device *device) {
    xx_freearc value;
    bool result;

    xx_freearc_init(&value, device, 0);
    result = xx_freearc_handle_base_info(&value.format, NULL);
    xx_freearc_destroy(&value);
    return result;
}

static bool xx_format_is_ap4_device(xx_io_device *device) {
    xx_ap4 value;
    bool result;

    xx_ap4_init(&value, device, 0);
    result = xx_ap4_check_is_valid(&value.format, NULL);
    xx_ap4_destroy(&value);
    return result;
}

static bool xx_format_is_arq_device(xx_io_device *device) {
    xx_arq value;
    bool result;

    xx_arq_init(&value, device, 0);
    result = xx_arq_handle_base_info(&value.format, NULL);
    xx_arq_destroy(&value);
    return result;
}

static bool xx_format_is_asar_device(xx_io_device *device) {
    xx_asar value;
    bool result;

    xx_asar_init(&value, device, 0);
    result = xx_asar_handle_base_info(&value.format, NULL);
    xx_asar_destroy(&value);
    return result;
}

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

static bool xx_format_is_ascend_device(xx_io_device *device) {
    xx_ascend value;
    bool result;

    xx_ascend_init(&value, device, 0);
    result = xx_ascend_handle_base_info(&value.format, NULL);
    xx_ascend_destroy(&value);
    return result;
}

static bool xx_format_is_bigf_device(xx_io_device *device) {
    xx_bigf value;
    bool result;

    xx_bigf_init(&value, device, 0);
    result = xx_bigf_handle_base_info(&value.format, NULL);
    xx_bigf_destroy(&value);
    return result;
}

static bool xx_format_is_marc_device(xx_io_device *device) {
    xx_marc value;
    bool result;

    xx_marc_init(&value, device, 0);
    result = xx_marc_handle_base_info(&value.format, NULL);
    xx_marc_destroy(&value);
    return result;
}

static bool xx_format_is_zfsf_device(xx_io_device *device) {
    xx_zfsf value;
    bool result;

    xx_zfsf_init(&value, device, 0);
    result = xx_zfsf_handle_base_info(&value.format, NULL);
    xx_zfsf_destroy(&value);
    return result;
}

static bool xx_format_is_packit_device(xx_io_device *device) {
    xx_packit value;
    bool result;

    xx_packit_init(&value, device, 0);
    result = xx_packit_handle_base_info(&value.format, NULL);
    xx_packit_destroy(&value);
    return result;
}

static bool xx_format_is_tws_device(xx_io_device *device) {
    xx_tws value;
    bool result;

    xx_tws_init(&value, device, 0);
    result = xx_tws_handle_base_info(&value.format, NULL);
    xx_tws_destroy(&value);
    return result;
}

static bool xx_format_is_bigaf_device(xx_io_device *device) {
    xx_bigaf value;
    bool result;

    xx_bigaf_init(&value, device, 0);
    result = xx_bigaf_handle_base_info(&value.format, NULL);
    xx_bigaf_destroy(&value);
    return result;
}

static bool xx_format_is_cru_device(xx_io_device *device) {
    xx_cru value;
    bool result;

    xx_cru_init(&value, device, 0);
    result = xx_cru_handle_base_info(&value.format, NULL);
    xx_cru_destroy(&value);
    return result;
}

static bool xx_format_is_frontpagetheme_device(xx_io_device *device) {
    xx_frontpagetheme value;
    bool result;

    xx_frontpagetheme_init(&value, device, 0);
    result = xx_frontpagetheme_handle_base_info(&value.format, NULL);
    xx_frontpagetheme_destroy(&value);
    return result;
}

static bool xx_format_is_hlb_device(xx_io_device *device) {
    xx_hlb value;
    bool result;

    xx_hlb_init(&value, device, 0);
    result = xx_hlb_handle_base_info(&value.format, NULL);
    xx_hlb_destroy(&value);
    return result;
}

static bool xx_format_is_irixsa_device(xx_io_device *device) {
    xx_irixsa value;
    bool result;

    xx_irixsa_init(&value, device, 0);
    result = xx_irixsa_handle_base_info(&value.format, NULL);
    xx_irixsa_destroy(&value);
    return result;
}

static bool xx_format_is_jam_device(xx_io_device *device) {
    xx_jam value;
    bool result;

    xx_jam_init(&value, device, 0);
    result = xx_jam_handle_base_info(&value.format, NULL);
    xx_jam_destroy(&value);
    return result;
}

static bool xx_format_is_krml_device(xx_io_device *device) {
    xx_krml value;
    bool result;

    xx_krml_init(&value, device, 0);
    result = xx_krml_handle_base_info(&value.format, NULL);
    xx_krml_destroy(&value);
    return result;
}

static bool xx_format_is_lbrcobol_device(xx_io_device *device) {
    xx_lbrcobol value;
    bool result;

    xx_lbrcobol_init(&value, device, 0);
    result = xx_lbrcobol_handle_base_info(&value.format, NULL);
    xx_lbrcobol_destroy(&value);
    return result;
}

static bool xx_format_is_minidump_device(xx_io_device *device) {
    xx_minidump value;
    bool result;

    xx_minidump_init(&value, device, 0);
    result = xx_minidump_handle_base_info(&value.format, NULL);
    xx_minidump_destroy(&value);
    return result;
}

static bool xx_format_is_powerboardbbs_device(xx_io_device *device) {
    xx_powerboardbbs value;
    bool result;

    xx_powerboardbbs_init(&value, device, 0);
    result = xx_powerboardbbs_handle_base_info(&value.format, NULL);
    xx_powerboardbbs_destroy(&value);
    return result;
}

static bool xx_format_is_sci_device(xx_io_device *device) {
    xx_sci value;
    bool result;

    xx_sci_init(&value, device, 0);
    result = xx_sci_handle_base_info(&value.format, NULL);
    xx_sci_destroy(&value);
    return result;
}

static bool xx_format_is_seadata_device(xx_io_device *device) {
    xx_seadata value;
    bool result;

    xx_seadata_init(&value, device, 0);
    result = xx_seadata_handle_base_info(&value.format, NULL);
    xx_seadata_destroy(&value);
    return result;
}

static bool xx_format_is_secondnature_device(xx_io_device *device) {
    xx_secondnature value;
    bool result;

    xx_secondnature_init(&value, device, 0);
    result = xx_secondnature_handle_base_info(&value.format, NULL);
    xx_secondnature_destroy(&value);
    return result;
}

static bool xx_format_is_sos_device(xx_io_device *device) {
    xx_sos value;
    bool result;

    xx_sos_init(&value, device, 0);
    result = xx_sos_handle_base_info(&value.format, NULL);
    xx_sos_destroy(&value);
    return result;
}

static bool xx_format_is_sw_device(xx_io_device *device) {
    xx_sw value;
    bool result;

    xx_sw_init(&value, device, 0);
    result = xx_sw_handle_base_info(&value.format, NULL);
    xx_sw_destroy(&value);
    return result;
}

static bool xx_format_is_swagpacket_device(xx_io_device *device) {
    xx_swagpacket value;
    bool result;

    xx_swagpacket_init(&value, device, 0);
    result = xx_swagpacket_handle_base_info(&value.format, NULL);
    xx_swagpacket_destroy(&value);
    return result;
}

static bool xx_format_is_trcpak_device(xx_io_device *device) {
    xx_trcpak value;
    bool result;

    xx_trcpak_init(&value, device, 0);
    result = xx_trcpak_handle_base_info(&value.format, NULL);
    xx_trcpak_destroy(&value);
    return result;
}

static bool xx_format_is_cfl_device(xx_io_device *device) {
    xx_cfl value;
    bool result;

    xx_cfl_init(&value, device, 0);
    result = xx_cfl_handle_base_info(&value.format, NULL);
    xx_cfl_destroy(&value);
    return result;
}

static bool xx_format_is_dpk_device(xx_io_device *device) {
    xx_dpk value;
    bool result;

    xx_dpk_init(&value, device, 0);
    result = xx_dpk_handle_base_info(&value.format, NULL);
    xx_dpk_destroy(&value);
    return result;
}

static bool xx_format_is_dsl2_device(xx_io_device *device) {
    xx_dsl2 value;
    bool result;

    xx_dsl2_init(&value, device, 0);
    result = xx_dsl2_handle_base_info(&value.format, NULL);
    xx_dsl2_destroy(&value);
    return result;
}

static bool xx_format_is_dtpacked_device(xx_io_device *device) {
    xx_dtpacked value;
    bool result;

    xx_dtpacked_init(&value, device, 0);
    result = xx_dtpacked_handle_base_info(&value.format, NULL);
    xx_dtpacked_destroy(&value);
    return result;
}

static bool xx_format_is_fiz_device(xx_io_device *device) {
    xx_fiz value;
    bool result;

    xx_fiz_init(&value, device, 0);
    result = xx_fiz_handle_base_info(&value.format, NULL);
    xx_fiz_destroy(&value);
    return result;
}

static bool xx_format_is_fld_device(xx_io_device *device) {
    xx_fld value;
    bool result;

    xx_fld_init(&value, device, 0);
    result = xx_fld_handle_base_info(&value.format, NULL);
    xx_fld_destroy(&value);
    return result;
}

static bool xx_format_is_ibmzpak_device(xx_io_device *device) {
    xx_ibmzpak value;
    bool result;

    xx_ibmzpak_init(&value, device, 0);
    result = xx_ibmzpak_handle_base_info(&value.format, NULL);
    xx_ibmzpak_destroy(&value);
    return result;
}

static bool xx_format_is_igf1_device(xx_io_device *device) {
    xx_igf1 value;
    bool result;

    xx_igf1_init(&value, device, 0);
    result = xx_igf1_handle_base_info(&value.format, NULL);
    xx_igf1_destroy(&value);
    return result;
}

static bool xx_format_is_igf2_device(xx_io_device *device) {
    xx_igf2 value;
    bool result;

    xx_igf2_init(&value, device, 0);
    result = xx_igf2_handle_base_info(&value.format, NULL);
    xx_igf2_destroy(&value);
    return result;
}

static bool xx_format_is_inteduft_device(xx_io_device *device) {
    xx_inteduft value;
    bool result;

    xx_inteduft_init(&value, device, 0);
    result = xx_inteduft_handle_base_info(&value.format, NULL);
    xx_inteduft_destroy(&value);
    return result;
}

static bool xx_format_is_jm93_device(xx_io_device *device) {
    xx_jm93 value;
    bool result;

    xx_jm93_init(&value, device, 0);
    result = xx_jm93_handle_base_info(&value.format, NULL);
    xx_jm93_destroy(&value);
    return result;
}

static bool xx_format_is_lsz_device(xx_io_device *device) {
    xx_lsz value;
    bool result;

    xx_lsz_init(&value, device, 0);
    result = xx_lsz_handle_base_info(&value.format, NULL);
    xx_lsz_destroy(&value);
    return result;
}

static bool xx_format_is_miz_device(xx_io_device *device) {
    xx_miz value;
    bool result;

    xx_miz_init(&value, device, 0);
    result = xx_miz_handle_base_info(&value.format, NULL);
    xx_miz_destroy(&value);
    return result;
}

static bool xx_format_is_mva_device(xx_io_device *device) {
    xx_mva value;
    bool result;

    xx_mva_init(&value, device, 0);
    result = xx_mva_handle_base_info(&value.format, NULL);
    xx_mva_destroy(&value);
    return result;
}

static bool xx_format_is_povlablzh_device(xx_io_device *device) {
    xx_povlablzh value;
    bool result;

    xx_povlablzh_init(&value, device, 0);
    result = xx_povlablzh_handle_base_info(&value.format, NULL);
    xx_povlablzh_destroy(&value);
    return result;
}

static bool xx_format_is_powerarc_device(xx_io_device *device) {
    xx_powerarc value;
    bool result;

    xx_powerarc_init(&value, device, 0);
    result = xx_powerarc_handle_base_info(&value.format, NULL);
    xx_powerarc_destroy(&value);
    return result;
}

static bool xx_format_is_qip1_device(xx_io_device *device) {
    xx_qip1 value;
    bool result;

    xx_qip1_init(&value, device, 0);
    result = xx_qip1_handle_base_info(&value.format, NULL);
    xx_qip1_destroy(&value);
    return result;
}

static bool xx_format_is_quarterdeckqp_device(xx_io_device *device) {
    xx_quarterdeckqp value;
    bool result;

    xx_quarterdeckqp_init(&value, device, 0);
    result = xx_quarterdeckqp_handle_base_info(&value.format, NULL);
    xx_quarterdeckqp_destroy(&value);
    return result;
}

static bool xx_format_is_rcf_device(xx_io_device *device) {
    xx_rcf value;
    bool result;

    xx_rcf_init(&value, device, 0);
    result = xx_rcf_handle_base_info(&value.format, NULL);
    xx_rcf_destroy(&value);
    return result;
}

static bool xx_format_is_riversoft_device(xx_io_device *device) {
    xx_riversoft value;
    bool result;

    xx_riversoft_init(&value, device, 0);
    result = xx_riversoft_handle_base_info(&value.format, NULL);
    xx_riversoft_destroy(&value);
    return result;
}

static bool xx_format_is_swag_device(xx_io_device *device) {
    xx_swag value;
    bool result;

    xx_swag_init(&value, device, 0);
    result = xx_swag_handle_base_info(&value.format, NULL);
    xx_swag_destroy(&value);
    return result;
}

static bool xx_format_is_tgcf_device(xx_io_device *device) {
    xx_tgcf value;
    bool result;

    xx_tgcf_init(&value, device, 0);
    result = xx_tgcf_handle_base_info(&value.format, NULL);
    xx_tgcf_destroy(&value);
    return result;
}

static bool xx_format_is_trc_device(xx_io_device *device) {
    xx_trc value;
    bool result;

    xx_trc_init(&value, device, 0);
    result = xx_trc_handle_base_info(&value.format, NULL);
    xx_trc_destroy(&value);
    return result;
}

static bool xx_format_is_zlwb_device(xx_io_device *device) {
    xx_zlwb value;
    bool result;

    xx_zlwb_init(&value, device, 0);
    result = xx_zlwb_handle_base_info(&value.format, NULL);
    xx_zlwb_destroy(&value);
    return result;
}

static bool xx_format_is_zz_device(xx_io_device *device) {
    xx_zz value;
    bool result;

    xx_zz_init(&value, device, 0);
    result = xx_zz_handle_base_info(&value.format, NULL);
    xx_zz_destroy(&value);
    return result;
}

static bool xx_format_is_zzz_device(xx_io_device *device) {
    xx_zzz value;
    bool result;

    xx_zzz_init(&value, device, 0);
    result = xx_zzz_handle_base_info(&value.format, NULL);
    xx_zzz_destroy(&value);
    return result;
}

static bool xx_format_is_jgpak_device(xx_io_device *device) {
    xx_jgpak value;
    bool result;

    xx_jgpak_init(&value, device, 0);
    result = xx_jgpak_handle_base_info(&value.format, NULL);
    xx_jgpak_destroy(&value);
    return result;
}

static bool xx_format_is_borlandpack_device(xx_io_device *device) {
    xx_borlandpack value;
    bool result;

    xx_borlandpack_init(&value, device, 0);
    result = xx_borlandpack_handle_base_info(&value.format, NULL);
    xx_borlandpack_destroy(&value);
    return result;
}

static bool xx_format_is_ecmpacked_device(xx_io_device *device) {
    xx_ecmpacked value;
    bool result;

    xx_ecmpacked_init(&value, device, 0);
    result = xx_ecmpacked_handle_base_info(&value.format, NULL);
    xx_ecmpacked_destroy(&value);
    return result;
}

static bool xx_format_is_jetbbs_device(xx_io_device *device) {
    xx_jetbbs value;
    bool result;

    xx_jetbbs_init(&value, device, 0);
    result = xx_jetbbs_handle_base_info(&value.format, NULL);
    xx_jetbbs_destroy(&value);
    return result;
}

static bool xx_format_is_qualitas_device(xx_io_device *device) {
    xx_qualitas value;
    bool result;

    xx_qualitas_init(&value, device, 0);
    result = xx_qualitas_handle_base_info(&value.format, NULL);
    xx_qualitas_destroy(&value);
    return result;
}

static bool xx_format_is_bwf_device(xx_io_device *device) {
    xx_bwf value;
    bool result;

    xx_bwf_init(&value, device, 0);
    result = xx_bwf_handle_base_info(&value.format, NULL);
    xx_bwf_destroy(&value);
    return result;
}

static bool xx_format_is_zap_device(xx_io_device *device) {
    xx_zap value;
    bool result;

    xx_zap_init(&value, device, 0);
    result = xx_zap_handle_base_info(&value.format, NULL);
    xx_zap_destroy(&value);
    return result;
}

static bool xx_format_is_stork_device(xx_io_device *device) {
    xx_stork value;
    bool result;

    xx_stork_init(&value, device, 0);
    result = xx_stork_handle_base_info(&value.format, NULL);
    xx_stork_destroy(&value);
    return result;
}

static bool xx_format_is_ascendbackup_device(xx_io_device *device) {
    xx_ascendbackup value;
    bool result;

    xx_ascendbackup_init(&value, device, 0);
    result = xx_ascendbackup_handle_base_info(&value.format, NULL);
    xx_ascendbackup_destroy(&value);
    return result;
}

static bool xx_format_is_pyz_device(xx_io_device *device) {
    xx_pyz value;
    bool result;

    xx_pyz_init(&value, device, 0);
    result = xx_pyz_handle_base_info(&value.format, NULL);
    xx_pyz_destroy(&value);
    return result;
}

static bool xx_format_is_fmc1_device(xx_io_device *device) {
    xx_fmc1 value;
    bool result;

    xx_fmc1_init(&value, device, 0);
    result = xx_fmc1_handle_base_info(&value.format, NULL);
    xx_fmc1_destroy(&value);
    return result;
}

static bool xx_format_is_xar_device(xx_io_device *device) {
    xx_xar value;
    bool result;

    xx_xar_init(&value, device, 0);
    result = xx_xar_handle_base_info(&value.format, NULL);
    xx_xar_destroy(&value);
    return result;
}

static bool xx_format_is_lha_device(xx_io_device *device) {
    xx_lha value;
    bool result;

    xx_lha_init(&value, device, 0);
    result = xx_lha_handle_base_info(&value.format, NULL);
    xx_lha_destroy(&value);
    return result;
}

static bool xx_format_is_spis_device(xx_io_device *device) {
    xx_spis value;
    bool result;

    xx_spis_init(&value, device, 0);
    result = xx_spis_handle_base_info(&value.format, NULL);
    xx_spis_destroy(&value);
    return result;
}

static bool xx_format_is_amigalzx_device(xx_io_device *device) {
    xx_amigalzx value;
    bool result;

    xx_amigalzx_init(&value, device, 0);
    result = xx_amigalzx_handle_base_info(&value.format, NULL);
    xx_amigalzx_destroy(&value);
    return result;
}

static bool xx_format_is_seaarc_device(xx_io_device *device) {
    xx_seaarc value;
    bool result;

    xx_seaarc_init(&value, device, 0);
    result = xx_seaarc_handle_base_info(&value.format, NULL);
    xx_seaarc_destroy(&value);
    return result;
}

static bool xx_format_is_asymetrix_device(xx_io_device *device) {
    xx_asymetrix value;
    bool result;

    xx_asymetrix_init(&value, device, 0);
    result = xx_asymetrix_handle_base_info(&value.format, NULL);
    xx_asymetrix_destroy(&value);
    return result;
}

static bool xx_format_is_bwcf_device(xx_io_device *device) {
    xx_bwcf value;
    bool result;

    xx_bwcf_init(&value, device, 0);
    result = xx_bwcf_handle_base_info(&value.format, NULL);
    xx_bwcf_destroy(&value);
    return result;
}

static bool xx_format_is_chieflz_device(xx_io_device *device) {
    xx_chieflz value;
    bool result;

    xx_chieflz_init(&value, device, 0);
    result = xx_chieflz_handle_base_info(&value.format, NULL);
    xx_chieflz_destroy(&value);
    return result;
}

static bool xx_format_is_chieflzmulti_device(xx_io_device *device) {
    xx_chieflzmulti value;
    bool result;

    xx_chieflzmulti_init(&value, device, 0);
    result = xx_chieflzmulti_handle_base_info(&value.format, NULL);
    xx_chieflzmulti_destroy(&value);
    return result;
}

static bool xx_format_is_clp_device(xx_io_device *device) {
    xx_clp value;
    bool result;

    xx_clp_init(&value, device, 0);
    result = xx_clp_handle_base_info(&value.format, NULL);
    xx_clp_destroy(&value);
    return result;
}

static bool xx_format_is_cmp_device(xx_io_device *device) {
    xx_cmp value;
    bool result;

    xx_cmp_init(&value, device, 0);
    result = xx_cmp_handle_base_info(&value.format, NULL);
    xx_cmp_destroy(&value);
    return result;
}

static bool xx_format_is_diskdoubler_device(xx_io_device *device) {
    xx_diskdoubler value;
    bool result;

    xx_diskdoubler_init(&value, device, 0);
    result = xx_diskdoubler_handle_base_info(&value.format, NULL);
    xx_diskdoubler_destroy(&value);
    return result;
}

static bool xx_format_is_ea_device(xx_io_device *device) {
    xx_ea value;
    bool result;

    xx_ea_init(&value, device, 0);
    result = xx_ea_handle_base_info(&value.format, NULL);
    xx_ea_destroy(&value);
    return result;
}

static bool xx_format_is_ealib_device(xx_io_device *device) {
    xx_ealib value;
    bool result;

    xx_ealib_init(&value, device, 0);
    result = xx_ealib_handle_base_info(&value.format, NULL);
    xx_ealib_destroy(&value);
    return result;
}

static bool xx_format_is_earefpack_device(xx_io_device *device) {
    xx_earefpack value;
    bool result;

    xx_earefpack_init(&value, device, 0);
    result = xx_earefpack_handle_base_info(&value.format, NULL);
    xx_earefpack_destroy(&value);
    return result;
}

static bool xx_format_is_fls_device(xx_io_device *device) {
    xx_fls value;
    bool result;

    xx_fls_init(&value, device, 0);
    result = xx_fls_handle_base_info(&value.format, NULL);
    xx_fls_destroy(&value);
    return result;
}

static bool xx_format_is_genius_device(xx_io_device *device) {
    xx_genius value;
    bool result;

    xx_genius_init(&value, device, 0);
    result = xx_genius_handle_base_info(&value.format, NULL);
    xx_genius_destroy(&value);
    return result;
}

static bool xx_format_is_ha_device(xx_io_device *device) {
    xx_ha value;
    bool result;

    xx_ha_init(&value, device, 0);
    result = xx_ha_handle_base_info(&value.format, NULL);
    xx_ha_destroy(&value);
    return result;
}

static bool xx_format_is_hzl_device(xx_io_device *device) {
    xx_hzl value;
    bool result;

    xx_hzl_init(&value, device, 0);
    result = xx_hzl_handle_base_info(&value.format, NULL);
    xx_hzl_destroy(&value);
    return result;
}

static bool xx_format_is_kboom_device(xx_io_device *device) {
    xx_kboom value;
    bool result;

    xx_kboom_init(&value, device, 0);
    result = xx_kboom_handle_base_info(&value.format, NULL);
    xx_kboom_destroy(&value);
    return result;
}

static bool xx_format_is_lzhcxp_device(xx_io_device *device) {
    xx_lzhcxp value;
    bool result;

    xx_lzhcxp_init(&value, device, 0);
    result = xx_lzhcxp_handle_base_info(&value.format, NULL);
    xx_lzhcxp_destroy(&value);
    return result;
}

static bool xx_format_is_lzwd_device(xx_io_device *device) {
    xx_lzwd value;
    bool result;

    xx_lzwd_init(&value, device, 0);
    result = xx_lzwd_handle_base_info(&value.format, NULL);
    xx_lzwd_destroy(&value);
    return result;
}

static bool xx_format_is_mi10_device(xx_io_device *device) {
    xx_mi10 value;
    bool result;

    xx_mi10_init(&value, device, 0);
    result = xx_mi10_handle_base_info(&value.format, NULL);
    xx_mi10_destroy(&value);
    return result;
}

static bool xx_format_is_npack_device(xx_io_device *device) {
    xx_npack value;
    bool result;

    xx_npack_init(&value, device, 0);
    result = xx_npack_handle_base_info(&value.format, NULL);
    xx_npack_destroy(&value);
    return result;
}

static bool xx_format_is_pakleo_device(xx_io_device *device) {
    xx_pakleo value;
    bool result;

    xx_pakleo_init(&value, device, 0);
    result = xx_pakleo_handle_base_info(&value.format, NULL);
    xx_pakleo_destroy(&value);
    return result;
}

static bool xx_format_is_scl_device(xx_io_device *device) {
    xx_scl value;
    bool result;

    xx_scl_init(&value, device, 0);
    result = xx_scl_handle_base_info(&value.format, NULL);
    xx_scl_destroy(&value);
    return result;
}

static bool xx_format_is_zcmp_device(xx_io_device *device) {
    xx_zcmp value;
    bool result;

    xx_zcmp_init(&value, device, 0);
    result = xx_zcmp_handle_base_info(&value.format, NULL);
    xx_zcmp_destroy(&value);
    return result;
}

static bool xx_format_is_zpak_device(xx_io_device *device) {
    xx_zpak value;
    bool result;

    xx_zpak_init(&value, device, 0);
    result = xx_zpak_handle_base_info(&value.format, NULL);
    xx_zpak_destroy(&value);
    return result;
}

static bool xx_format_is_netwarepacked_device(xx_io_device *device) {
    xx_netwarepacked value;
    bool result;

    xx_netwarepacked_init(&value, device, 0);
    result = xx_netwarepacked_handle_base_info(&value.format, NULL);
    xx_netwarepacked_destroy(&value);
    return result;
}

static bool xx_format_is_ztc_device(xx_io_device *device) {
    xx_ztc value;
    bool result;

    xx_ztc_init(&value, device, 0);
    result = xx_ztc_handle_base_info(&value.format, NULL);
    xx_ztc_destroy(&value);
    return result;
}

static bool xx_format_is_glu_device(xx_io_device *device) {
    xx_glu value;
    bool result;

    xx_glu_init(&value, device, 0);
    result = xx_glu_handle_base_info(&value.format, NULL);
    xx_glu_destroy(&value);
    return result;
}

static bool xx_format_is_gtu_device(xx_io_device *device) {
    xx_gtu value;
    bool result;

    xx_gtu_init(&value, device, 0);
    result = xx_gtu_handle_base_info(&value.format, NULL);
    xx_gtu_destroy(&value);
    return result;
}

static bool xx_format_is_ibmspack_device(xx_io_device *device) {
    xx_ibmspack value;
    bool result;

    xx_ibmspack_init(&value, device, 0);
    result = xx_ibmspack_handle_base_info(&value.format, NULL);
    xx_ibmspack_destroy(&value);
    return result;
}

static bool xx_format_is_jbf_device(xx_io_device *device) {
    xx_jbf value;
    bool result;

    xx_jbf_init(&value, device, 0);
    result = xx_jbf_handle_base_info(&value.format, NULL);
    xx_jbf_destroy(&value);
    return result;
}

static bool xx_format_is_pcommos2_device(xx_io_device *device) {
    xx_pcommos2 value;
    bool result;

    xx_pcommos2_init(&value, device, 0);
    result = xx_pcommos2_handle_base_info(&value.format, NULL);
    xx_pcommos2_destroy(&value);
    return result;
}

static bool xx_format_is_stk_device(xx_io_device *device) {
    xx_stk value;
    bool result;

    xx_stk_init(&value, device, 0);
    result = xx_stk_handle_base_info(&value.format, NULL);
    xx_stk_destroy(&value);
    return result;
}

static bool xx_format_is_terse_device(xx_io_device *device) {
    xx_terse value;
    bool result;

    xx_terse_init(&value, device, 0);
    result = xx_terse_handle_base_info(&value.format, NULL);
    xx_terse_destroy(&value);
    return result;
}

static bool xx_format_is_zoo_device(xx_io_device *device) {
    xx_zoo value;
    bool result;

    xx_zoo_init(&value, device, 0);
    result = xx_zoo_handle_base_info(&value.format, NULL);
    xx_zoo_destroy(&value);
    return result;
}

static bool xx_format_is_sqx_device(xx_io_device *device) {
    xx_sqx value;
    bool result;

    xx_sqx_init(&value, device, 0);
    result = xx_sqx_handle_base_info(&value.format, NULL);
    xx_sqx_destroy(&value);
    return result;
}

static bool xx_format_is_imp_device(xx_io_device *device) {
    xx_imp value;
    bool result;

    xx_imp_init(&value, device, 0);
    result = xx_imp_handle_base_info(&value.format, NULL);
    xx_imp_destroy(&value);
    return result;
}

static bool xx_format_is_compactpro_device(xx_io_device *device) {
    xx_compactpro value;
    bool result;

    xx_compactpro_init(&value, device, 0);
    result = xx_compactpro_handle_base_info(&value.format, NULL);
    xx_compactpro_destroy(&value);
    return result;
}

static bool xx_format_is_hap_device(xx_io_device *device) {
    xx_hap value;
    bool result;

    xx_hap_init(&value, device, 0);
    result = xx_hap_handle_base_info(&value.format, NULL);
    xx_hap_destroy(&value);
    return result;
}

static bool xx_format_is_irwinpac_device(xx_io_device *device) {
    xx_irwinpac value;
    bool result;

    xx_irwinpac_init(&value, device, 0);
    result = xx_irwinpac_handle_base_info(&value.format, NULL);
    xx_irwinpac_destroy(&value);
    return result;
}

static bool xx_format_is_ivt_device(xx_io_device *device) {
    xx_ivt value;
    bool result;

    xx_ivt_init(&value, device, 0);
    result = xx_ivt_handle_base_info(&value.format, NULL);
    xx_ivt_destroy(&value);
    return result;
}

static bool xx_format_is_kolibrikpack_device(xx_io_device *device) {
    xx_kolibrikpack value;
    bool result;

    xx_kolibrikpack_init(&value, device, 0);
    result = xx_kolibrikpack_handle_base_info(&value.format, NULL);
    xx_kolibrikpack_destroy(&value);
    return result;
}

static bool xx_format_is_lim_device(xx_io_device *device) {
    xx_lim value;
    bool result;

    xx_lim_init(&value, device, 0);
    result = xx_lim_handle_base_info(&value.format, NULL);
    xx_lim_destroy(&value);
    return result;
}

static bool xx_format_is_lofi_device(xx_io_device *device) {
    xx_lofi value;
    bool result;

    xx_lofi_init(&value, device, 0);
    result = xx_lofi_handle_base_info(&value.format, NULL);
    xx_lofi_destroy(&value);
    return result;
}

static bool xx_format_is_pkt_device(xx_io_device *device) {
    xx_pkt value;
    bool result;

    xx_pkt_init(&value, device, 0);
    result = xx_pkt_handle_base_info(&value.format, NULL);
    xx_pkt_destroy(&value);
    return result;
}

static bool xx_format_is_qda_device(xx_io_device *device) {
    xx_qda value;
    bool result;

    xx_qda_init(&value, device, 0);
    result = xx_qda_handle_base_info(&value.format, NULL);
    xx_qda_destroy(&value);
    return result;
}

static bool xx_format_is_qnxbase_device(xx_io_device *device) {
    xx_qnxbase value;
    bool result;

    xx_qnxbase_init(&value, device, 0);
    result = xx_qnxbase_handle_base_info(&value.format, NULL);
    xx_qnxbase_destroy(&value);
    return result;
}

static bool xx_format_is_rid_device(xx_io_device *device) {
    xx_rid value;
    bool result;

    xx_rid_init(&value, device, 0);
    result = xx_rid_handle_base_info(&value.format, NULL);
    xx_rid_destroy(&value);
    return result;
}

static bool xx_format_is_rompaq_device(xx_io_device *device) {
    xx_rompaq value;
    bool result;

    xx_rompaq_init(&value, device, 0);
    result = xx_rompaq_handle_base_info(&value.format, NULL);
    xx_rompaq_destroy(&value);
    return result;
}

static bool xx_format_is_rta_device(xx_io_device *device) {
    xx_rta value;
    bool result;

    xx_rta_init(&value, device, 0);
    result = xx_rta_handle_base_info(&value.format, NULL);
    xx_rta_destroy(&value);
    return result;
}

static bool xx_format_is_rtpatch_device(xx_io_device *device) {
    xx_rtpatch value;
    bool result;

    xx_rtpatch_init(&value, device, 0);
    result = xx_rtpatch_handle_base_info(&value.format, NULL);
    xx_rtpatch_destroy(&value);
    return result;
}

static bool xx_format_is_stylus_device(xx_io_device *device) {
    xx_stylus value;
    bool result;

    xx_stylus_init(&value, device, 0);
    result = xx_stylus_handle_base_info(&value.format, NULL);
    xx_stylus_destroy(&value);
    return result;
}

static bool xx_format_is_ti99arc_device(xx_io_device *device) {
    xx_ti99arc value;
    bool result;

    xx_ti99arc_init(&value, device, 0);
    result = xx_ti99arc_handle_base_info(&value.format, NULL);
    xx_ti99arc_destroy(&value);
    return result;
}

static bool xx_format_is_tivoli_device(xx_io_device *device) {
    xx_tivoli value;
    bool result;

    xx_tivoli_init(&value, device, 0);
    result = xx_tivoli_handle_base_info(&value.format, NULL);
    xx_tivoli_destroy(&value);
    return result;
}

static bool xx_format_is_vmarc_device(xx_io_device *device) {
    xx_vmarc value;
    bool result;

    xx_vmarc_init(&value, device, 0);
    result = xx_vmarc_handle_base_info(&value.format, NULL);
    xx_vmarc_destroy(&value);
    return result;
}

static bool xx_format_is_wintersoft_device(xx_io_device *device) {
    xx_wintersoft value;
    bool result;

    xx_wintersoft_init(&value, device, 0);
    result = xx_wintersoft_handle_base_info(&value.format, NULL);
    xx_wintersoft_destroy(&value);
    return result;
}

static bool xx_format_is_wpk_device(xx_io_device *device) {
    xx_wpk value;
    bool result;

    xx_wpk_init(&value, device, 0);
    result = xx_wpk_handle_base_info(&value.format, NULL);
    xx_wpk_destroy(&value);
    return result;
}

static bool xx_format_is_xeditpack_device(xx_io_device *device) {
    xx_xeditpack value;
    bool result;

    xx_xeditpack_init(&value, device, 0);
    result = xx_xeditpack_handle_base_info(&value.format, NULL);
    xx_xeditpack_destroy(&value);
    return result;
}

static bool xx_format_is_zie_device(xx_io_device *device) {
    xx_zie value;
    bool result;

    xx_zie_init(&value, device, 0);
    result = xx_zie_handle_base_info(&value.format, NULL);
    xx_zie_destroy(&value);
    return result;
}

static bool xx_format_is_lzpis2_device(xx_io_device *device) {
    xx_lzpis2 lzpis2;
    bool result;
    if (!device) return false;
    xx_lzpis2_init(&lzpis2, device, 0);
    result = xx_lzpis2_handle_base_info(&lzpis2.format, NULL);
    xx_lzpis2_destroy(&lzpis2);
    return result;
}

static bool xx_format_is_tar_zstd_device(xx_io_device *device) {
    xx_tar_zstd tar_zstd;
    bool result;
    if (!device) return false;
    xx_tar_zstd_init(&tar_zstd, device, 0);
    result = xx_tar_zstd_handle_base_info(&tar_zstd.format, NULL);
    xx_tar_zstd_destroy(&tar_zstd);
    return result;
}

static bool xx_format_is_zstd_device(xx_io_device *device) {
    xx_zstd zstd;
    bool result;
    if (!device) return false;
    xx_zstd_init(&zstd, device, 0);
    result = xx_zstd_handle_base_info(&zstd.format, NULL);
    xx_zstd_destroy(&zstd);
    return result;
}

static bool xx_format_is_tar_nextstep_device(xx_io_device *device) {
    xx_tar_nextstep tar_nextstep;
    bool result;
    if (!device) return false;
    xx_tar_nextstep_init(&tar_nextstep, device, 0);
    result = xx_tar_nextstep_check_is_valid(&tar_nextstep.format, NULL);
    xx_tar_nextstep_destroy(&tar_nextstep);
    return result;
}

static bool xx_format_is_tar_compress_device(xx_io_device *device) {
    xx_tar_compress tar_compress;
    bool result;
    if (!device) return false;
    xx_tar_compress_init(&tar_compress, device, 0);
    result = xx_tar_compress_handle_base_info(&tar_compress.format, NULL);
    xx_tar_compress_destroy(&tar_compress);
    return result;
}

static bool xx_format_is_tar_lzip_device(xx_io_device *device) {
    xx_tar_lzip tar_lzip;
    bool result;
    if (!device) return false;
    xx_tar_lzip_init(&tar_lzip, device, 0);
    result = xx_tar_lzip_handle_base_info(&tar_lzip.format, NULL);
    xx_tar_lzip_destroy(&tar_lzip);
    return result;
}

static bool xx_format_is_lzip_device(xx_io_device *device) {
    xx_lzip lzip;
    bool result;
    if (!device) return false;
    xx_lzip_init(&lzip, device, 0);
    result = xx_lzip_handle_base_info(&lzip.format, NULL);
    xx_lzip_destroy(&lzip);
    return result;
}

static bool xx_format_is_tar_lzma_device(xx_io_device *device) {
    xx_tar_lzma tar_lzma;
    bool result;
    if (!device) return false;
    xx_tar_lzma_init(&tar_lzma, device, 0);
    result = xx_tar_lzma_handle_base_info(&tar_lzma.format, NULL);
    xx_tar_lzma_destroy(&tar_lzma);
    return result;
}

static bool xx_format_is_lzma_device(xx_io_device *device) {
    xx_lzma lzma;
    bool result;
    if (!device) return false;
    xx_lzma_init(&lzma, device, 0);
    result = xx_lzma_handle_base_info(&lzma.format, NULL);
    xx_lzma_destroy(&lzma);
    return result;
}

static bool xx_format_is_tar_lzop_device(xx_io_device *device) {
    xx_tar_lzop tar_lzop;
    bool result;
    if (!device) return false;
    xx_tar_lzop_init(&tar_lzop, device, 0);
    result = xx_tar_lzop_handle_base_info(&tar_lzop.format, NULL);
    xx_tar_lzop_destroy(&tar_lzop);
    return result;
}

static bool xx_format_is_tarx1_device(xx_io_device *device) {
    xx_tarx1 tarx1;
    bool result;
    if (!device) return false;
    xx_tarx1_init(&tarx1, device, 0);
    result = xx_tarx1_handle_base_info(&tarx1.format, NULL);
    xx_tarx1_destroy(&tarx1);
    return result;
}

static bool xx_format_is_tarx2_device(xx_io_device *device) {
    xx_tarx2 tarx2;
    bool result;
    if (!device) return false;
    xx_tarx2_init(&tarx2, device, 0);
    result = xx_tarx2_handle_base_info(&tarx2.format, NULL);
    xx_tarx2_destroy(&tarx2);
    return result;
}

static bool xx_format_is_squashfs_device(xx_io_device *device) {
    xx_squashfs value;
    bool result;
    if (!device) return false;
    xx_squashfs_init(&value, device, 0);
    result = xx_squashfs_handle_base_info(&value.format, NULL);
    xx_squashfs_destroy(&value);
    return result;
}

static bool xx_format_is_ntfs_device(xx_io_device *device) {
    xx_ntfs value;
    bool result;
    if (!device) return false;
    xx_ntfs_init(&value, device, 0);
    result = xx_ntfs_handle_base_info(&value.format, NULL);
    xx_ntfs_destroy(&value);
    return result;
}

static bool xx_format_is_udf_device(xx_io_device *device) {
    xx_udf value;
    bool result;
    if (!device) return false;
    xx_udf_init(&value, device, 0);
    result = xx_udf_handle_base_info(&value.format, NULL);
    xx_udf_destroy(&value);
    return result;
}

static bool xx_format_is_romfs_device(xx_io_device *device) {
    xx_romfs value;
    bool result;
    if (!device) return false;
    xx_romfs_init(&value, device, 0);
    result = xx_romfs_handle_base_info(&value.format, NULL);
    xx_romfs_destroy(&value);
    return result;
}

static bool xx_format_is_sqz_device(xx_io_device *device) {
    xx_sqz value;
    bool result;
    if (!device) return false;
    xx_sqz_init(&value, device, 0);
    result = xx_sqz_handle_base_info(&value.format, NULL);
    xx_sqz_destroy(&value);
    return result;
}

static bool xx_format_is_topspeed_device(xx_io_device *device) {
    xx_topspeed value;
    bool result;
    if (!device) return false;
    xx_topspeed_init(&value, device, 0);
    result = xx_topspeed_handle_base_info(&value.format, NULL);
    xx_topspeed_destroy(&value);
    return result;
}

static bool xx_format_is_tps_device(xx_io_device *device) {
    xx_tps value;
    bool result;
    if (!device) return false;
    xx_tps_init(&value, device, 0);
    result = xx_tps_handle_base_info(&value.format, NULL);
    xx_tps_destroy(&value);
    return result;
}

static bool xx_format_is_ulead_device(xx_io_device *device) {
    xx_ulead value;
    bool result;
    if (!device) return false;
    xx_ulead_init(&value, device, 0);
    result = xx_ulead_handle_base_info(&value.format, NULL);
    xx_ulead_destroy(&value);
    return result;
}

static bool xx_format_is_quantum_device(xx_io_device *device) {
    xx_quantum value;
    bool result;
    if (!device) return false;
    xx_quantum_init(&value, device, 0);
    result = xx_quantum_handle_base_info(&value.format, NULL);
    xx_quantum_destroy(&value);
    return result;
}

static bool xx_format_is_zxzip_device(xx_io_device *device) {
    xx_zxzip value;
    bool result;
    if (!device) return false;
    xx_zxzip_init(&value, device, 0);
    result = xx_zxzip_handle_base_info(&value.format, NULL);
    xx_zxzip_destroy(&value);
    return result;
}

static bool xx_format_is_zoom_device(xx_io_device *device) {
    xx_zoom value;
    bool result;
    if (!device) return false;
    xx_zoom_init(&value, device, 0);
    result = xx_zoom_handle_base_info(&value.format, NULL);
    xx_zoom_destroy(&value);
    return result;
}

static bool xx_format_is_sfpack_device(xx_io_device *device) {
    xx_sfpack value;
    bool result;
    if (!device) return false;
    xx_sfpack_init(&value, device, 0);
    result = xx_sfpack_handle_base_info(&value.format, NULL);
    xx_sfpack_destroy(&value);
    return result;
}

static bool xx_format_is_claylz_device(xx_io_device *device) {
    xx_claylz value;
    bool result;
    if (!device) return false;
    xx_claylz_init(&value, device, 0);
    result = xx_claylz_handle_base_info(&value.format, NULL);
    xx_claylz_destroy(&value);
    return result;
}

static bool xx_format_is_c64wraptor_device(xx_io_device *device) {
    xx_c64wraptor value;
    bool result;
    if (!device) return false;
    xx_c64wraptor_init(&value, device, 0);
    result = xx_c64wraptor_handle_base_info(&value.format, NULL);
    xx_c64wraptor_destroy(&value);
    return result;
}

static bool xx_format_is_corelltec_device(xx_io_device *device) {
    xx_corelltec value;
    bool result;
    if (!device) return false;
    xx_corelltec_init(&value, device, 0);
    result = xx_corelltec_handle_base_info(&value.format, NULL);
    xx_corelltec_destroy(&value);
    return result;
}

static bool xx_format_is_pcsecure_device(xx_io_device *device) {
    xx_pcsecure value;
    bool result;
    if (!device) return false;
    xx_pcsecure_init(&value, device, 0);
    result = xx_pcsecure_handle_base_info(&value.format, NULL);
    xx_pcsecure_destroy(&value);
    return result;
}

static bool xx_format_is_rsvk_device(xx_io_device *device) {
    xx_rsvk value;
    bool result;
    if (!device) return false;
    xx_rsvk_init(&value, device, 0);
    result = xx_rsvk_handle_base_info(&value.format, NULL);
    xx_rsvk_destroy(&value);
    return result;
}

static bool xx_format_is_lzw15v_device(xx_io_device *device) {
    xx_lzw15v value;
    bool result;
    if (!device) return false;
    xx_lzw15v_init(&value, device, 0);
    result = xx_lzw15v_handle_base_info(&value.format, NULL);
    xx_lzw15v_destroy(&value);
    return result;
}

static bool xx_format_is_saf_device(xx_io_device *device) {
    xx_saf value;
    bool result;
    if (!device) return false;
    xx_saf_init(&value, device, 0);
    result = xx_saf_handle_base_info(&value.format, NULL);
    xx_saf_destroy(&value);
    return result;
}

static bool xx_format_is_sls_device(xx_io_device *device) {
    xx_sls value;
    bool result;
    if (!device) return false;
    xx_sls_init(&value, device, 0);
    result = xx_sls_handle_base_info(&value.format, NULL);
    xx_sls_destroy(&value);
    return result;
}

static bool xx_format_is_nid_device(xx_io_device *device) {
    xx_nid value;
    bool result;
    if (!device) return false;
    xx_nid_init(&value, device, 0);
    result = xx_nid_handle_base_info(&value.format, NULL);
    xx_nid_destroy(&value);
    return result;
}

static bool xx_format_is_gamos_device(xx_io_device *device) {
    xx_gamos value;
    bool result;
    if (!device) return false;
    xx_gamos_init(&value, device, 0);
    result = xx_gamos_handle_base_info(&value.format, NULL);
    xx_gamos_destroy(&value);
    return result;
}

static bool xx_format_is_panorama_device(xx_io_device *device) {
    xx_panorama value;
    bool result;
    if (!device) return false;
    xx_panorama_init(&value, device, 0);
    result = xx_panorama_handle_base_info(&value.format, NULL);
    xx_panorama_destroy(&value);
    return result;
}

static bool xx_format_is_fpak_device(xx_io_device *device) {
    xx_fpak value;
    bool result;
    if (!device) return false;
    xx_fpak_init(&value, device, 0);
    result = xx_fpak_handle_base_info(&value.format, NULL);
    xx_fpak_destroy(&value);
    return result;
}

static bool xx_format_is_cramfs_device(xx_io_device *device) {
    xx_cramfs value;
    bool result;
    if (!device) return false;
    xx_cramfs_init(&value, device, 0);
    result = xx_cramfs_handle_base_info(&value.format, NULL);
    xx_cramfs_destroy(&value);
    return result;
}

static bool xx_format_is_jffs2_device(xx_io_device *device) {
    xx_jffs2 value;
    bool result;
    if (!device) return false;
    xx_jffs2_init(&value, device, 0);
    result = xx_jffs2_handle_base_info(&value.format, NULL);
    xx_jffs2_destroy(&value);
    return result;
}

static bool xx_format_is_yaffs_device(xx_io_device *device) {
    xx_yaffs value;
    bool result;
    if (!device) return false;
    xx_yaffs_init(&value, device, 0);
    result = xx_yaffs_handle_base_info(&value.format, NULL);
    xx_yaffs_destroy(&value);
    return result;
}

static bool xx_format_is_ubi_device(xx_io_device *device) {
    xx_ubi value;
    bool result;
    if (!device) return false;
    xx_ubi_init(&value, device, 0);
    result = xx_ubi_handle_base_info(&value.format, NULL);
    xx_ubi_destroy(&value);
    return result;
}

static bool xx_format_is_ubifs_device(xx_io_device *device) {
    xx_ubifs value;
    bool result;
    if (!device) return false;
    xx_ubifs_init(&value, device, 0);
    result = xx_ubifs_handle_base_info(&value.format, NULL);
    xx_ubifs_destroy(&value);
    return result;
}

static bool xx_format_is_ext_device(xx_io_device *device) {
    xx_ext value;
    bool result;
    if (!device) return false;
    xx_ext_init(&value, device, 0);
    result = xx_ext_handle_base_info(&value.format, NULL);
    xx_ext_destroy(&value);
    return result;
}

static bool xx_format_is_fat_device(xx_io_device *device) {
    xx_fat value;
    bool result;
    if (!device) return false;
    xx_fat_init(&value, device, 0);
    result = xx_fat_handle_base_info(&value.format, NULL);
    xx_fat_destroy(&value);
    return result;
}

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

static bool xx_format_is_gpt_device(xx_io_device *device) {
    xx_gpt value;
    bool result;
    if (!device) return false;
    xx_gpt_init(&value, device, 0);
    result = xx_gpt_handle_base_info(&value.format, NULL);
    xx_gpt_destroy(&value);
    return result;
}

static bool xx_format_is_sparse_device(xx_io_device *device) {
    xx_sparse value;
    bool result;
    if (!device) return false;
    xx_sparse_init(&value, device, 0);
    result = xx_sparse_handle_base_info(&value.format, NULL);
    xx_sparse_destroy(&value);
    return result;
}

static bool xx_format_is_uimage_device(xx_io_device *device) {
    xx_uimage value;
    bool result;
    if (!device) return false;
    xx_uimage_init(&value, device, 0);
    result = xx_uimage_handle_base_info(&value.format, NULL);
    xx_uimage_destroy(&value);
    return result;
}

static bool xx_format_is_dtb_device(xx_io_device *device) {
    xx_dtb value;
    bool result;
    if (!device) return false;
    xx_dtb_init(&value, device, 0);
    result = xx_dtb_handle_base_info(&value.format, NULL);
    xx_dtb_destroy(&value);
    return result;
}

static bool xx_format_is_trx_device(xx_io_device *device) {
    xx_trx value;
    bool result;
    if (!device) return false;
    xx_trx_init(&value, device, 0);
    result = xx_trx_handle_base_info(&value.format, NULL);
    xx_trx_destroy(&value);
    return result;
}

static bool xx_format_is_seama_device(xx_io_device *device) {
    xx_seama value;
    bool result;
    if (!device) return false;
    xx_seama_init(&value, device, 0);
    result = xx_seama_handle_base_info(&value.format, NULL);
    xx_seama_destroy(&value);
    return result;
}

static bool xx_format_is_chk_device(xx_io_device *device) {
    xx_chk value;
    bool result;
    if (!device) return false;
    xx_chk_init(&value, device, 0);
    result = xx_chk_handle_base_info(&value.format, NULL);
    xx_chk_destroy(&value);
    return result;
}

static bool xx_format_is_packimg_device(xx_io_device *device) {
    xx_packimg value;
    bool result;
    if (!device) return false;
    xx_packimg_init(&value, device, 0);
    result = xx_packimg_handle_base_info(&value.format, NULL);
    xx_packimg_destroy(&value);
    return result;
}

static bool xx_format_is_dlob_device(xx_io_device *device) {
    xx_dlob value;
    bool result;
    if (!device) return false;
    xx_dlob_init(&value, device, 0);
    result = xx_dlob_handle_base_info(&value.format, NULL);
    xx_dlob_destroy(&value);
    return result;
}

static bool xx_format_is_wince_device(xx_io_device *device) {
    xx_wince value;
    bool result;
    if (!device) return false;
    xx_wince_init(&value, device, 0);
    result = xx_wince_handle_base_info(&value.format, NULL);
    xx_wince_destroy(&value);
    return result;
}

static bool xx_format_is_binhdr_device(xx_io_device *device) {
    xx_binhdr value;
    bool result;
    if (!device) return false;
    xx_binhdr_init(&value, device, 0);
    result = xx_binhdr_handle_base_info(&value.format, NULL);
    xx_binhdr_destroy(&value);
    return result;
}

static bool xx_format_is_rtk_device(xx_io_device *device) {
    xx_rtk value;
    bool result;
    if (!device) return false;
    xx_rtk_init(&value, device, 0);
    result = xx_rtk_handle_base_info(&value.format, NULL);
    xx_rtk_destroy(&value);
    return result;
}

static bool xx_format_is_csman_device(xx_io_device *device) {
    xx_csman value;
    bool result;
    if (!device) return false;
    xx_csman_init(&value, device, 0);
    result = xx_csman_handle_base_info(&value.format, NULL);
    xx_csman_destroy(&value);
    return result;
}

static bool xx_format_is_vxworks_device(xx_io_device *device) {
    xx_vxworks value;
    bool result;
    if (!device) return false;
    xx_vxworks_init(&value, device, 0);
    result = xx_vxworks_handle_base_info(&value.format, NULL);
    xx_vxworks_destroy(&value);
    return result;
}

static bool xx_format_is_uefi_fv_device(xx_io_device *device) {
    xx_uefi_fv value;
    bool result;
    if (!device) return false;
    xx_uefi_fv_init(&value, device, 0);
    result = xx_uefi_fv_handle_base_info(&value.format, NULL);
    xx_uefi_fv_destroy(&value);
    return result;
}

static bool xx_format_is_uefi_capsule_device(xx_io_device *device) {
    xx_uefi_capsule value;
    bool result;
    if (!device) return false;
    xx_uefi_capsule_init(&value, device, 0);
    result = xx_uefi_capsule_handle_base_info(&value.format, NULL);
    xx_uefi_capsule_destroy(&value);
    return result;
}

static bool xx_format_is_qcow_device(xx_io_device *device) {
    xx_qcow value;
    bool result;
    if (!device) return false;
    xx_qcow_init(&value, device, 0);
    result = xx_qcow_handle_base_info(&value.format, NULL);
    xx_qcow_destroy(&value);
    return result;
}

static bool xx_format_is_qnx6_device(xx_io_device *device) {
    xx_qnx6 value;
    bool result;
    if (!device) return false;
    xx_qnx6_init(&value, device, 0);
    result = xx_qnx6_handle_base_info(&value.format, NULL);
    xx_qnx6_destroy(&value);
    return result;
}

static bool xx_format_is_luks_device(xx_io_device *device) {
    xx_luks value;
    bool result;
    if (!device) return false;
    xx_luks_init(&value, device, 0);
    result = xx_luks_handle_base_info(&value.format, NULL);
    xx_luks_destroy(&value);
    return result;
}

static bool xx_format_is_apfs_device(xx_io_device *device) {
    xx_apfs value;
    bool result;
    if (!device) return false;
    xx_apfs_init(&value, device, 0);
    result = xx_apfs_handle_base_info(&value.format, NULL);
    xx_apfs_destroy(&value);
    return result;
}

static bool xx_format_is_btrfs_device(xx_io_device *device) {
    xx_btrfs value;
    bool result;
    if (!device) return false;
    xx_btrfs_init(&value, device, 0);
    result = xx_btrfs_handle_base_info(&value.format, NULL);
    xx_btrfs_destroy(&value);
    return result;
}

static bool xx_format_is_logfs_device(xx_io_device *device) {
    xx_logfs value;
    bool result;
    if (!device) return false;
    xx_logfs_init(&value, device, 0);
    result = xx_logfs_handle_base_info(&value.format, NULL);
    xx_logfs_destroy(&value);
    return result;
}

/* DMS carries a four-byte magic, so the probe is only reached when those
 * bytes already matched; it confirms the rest of the header parses. */
/* A resource fork has no magic -- its header is four plausible offsets --
 * so nothing but the full structural walk can confirm one. Late dispatch,
 * and cheaper than the whole-stream decoders it runs before. */
static bool xx_format_is_resourcefork_device(xx_io_device *device) {
    xx_resourcefork value;
    bool result;
    if (!device) return false;
    xx_resourcefork_init(&value, device, 0);
    result = xx_resourcefork_check_is_valid(&value.format, NULL);
    xx_resourcefork_destroy(&value);
    return result;
}

static bool xx_format_is_applesingle_device(xx_io_device *device) {
    xx_applesingle value;
    bool result;
    if (!device) return false;
    xx_applesingle_init(&value, device, 0);
    result = xx_applesingle_check_is_valid(&value.format, NULL);
    xx_applesingle_destroy(&value);
    return result;
}

static bool xx_format_is_macbinary_device(xx_io_device *device) {
    xx_macbinary value;
    bool result;
    if (!device) return false;
    xx_macbinary_init(&value, device, 0);
    result = xx_macbinary_check_is_valid(&value.format, NULL);
    xx_macbinary_destroy(&value);
    return result;
}

static bool xx_format_is_pp20_device(xx_io_device *device) {
    xx_pp20 value;
    bool result;
    if (!device) return false;
    xx_pp20_init(&value, device, 0);
    result = xx_pp20_check_is_valid(&value.format, NULL);
    xx_pp20_destroy(&value);
    return result;
}

static bool xx_format_is_beatthehouse_device(xx_io_device *device) {
    xx_beatthehouse value;
    bool result;
    if (!device) return false;
    xx_beatthehouse_init(&value, device, 0);
    result = xx_beatthehouse_check_is_valid(&value.format, NULL);
    xx_beatthehouse_destroy(&value);
    return result;
}

static bool xx_format_is_kpck_device(xx_io_device *device) {
    xx_kpck value;
    bool result;
    if (!device) return false;
    xx_kpck_init(&value, device, 0);
    result = xx_kpck_check_is_valid(&value.format, NULL);
    xx_kpck_destroy(&value);
    return result;
}

static bool xx_format_is_battleisle_device(xx_io_device *device) {
    xx_battleisle value;
    bool result;
    if (!device) return false;
    xx_battleisle_init(&value, device, 0);
    result = xx_battleisle_check_is_valid(&value.format, NULL);
    xx_battleisle_destroy(&value);
    return result;
}

static bool xx_format_is_perform_device(xx_io_device *device) {
    xx_perform value;
    bool result;
    if (!device) return false;
    xx_perform_init(&value, device, 0);
    result = xx_perform_check_is_valid(&value.format, NULL);
    xx_perform_destroy(&value);
    return result;
}

static bool xx_format_is_mathcad_device(xx_io_device *device) {
    xx_mathcad value;
    bool result;
    if (!device) return false;
    xx_mathcad_init(&value, device, 0);
    result = xx_mathcad_check_is_valid(&value.format, NULL);
    xx_mathcad_destroy(&value);
    return result;
}

static bool xx_format_is_netware2_device(xx_io_device *device) {
    xx_netware2 value;
    bool result;
    if (!device) return false;
    xx_netware2_init(&value, device, 0);
    result = xx_netware2_check_is_valid(&value.format, NULL);
    xx_netware2_destroy(&value);
    return result;
}

static bool xx_format_is_shar_device(xx_io_device *device) {
    xx_shar value;
    bool result;
    if (!device) return false;
    xx_shar_init(&value, device, 0);
    result = xx_shar_check_is_valid(&value.format, NULL);
    xx_shar_destroy(&value);
    return result;
}

static bool xx_format_is_rnc_device(xx_io_device *device) {
    xx_rnc value;
    bool result;
    if (!device) return false;
    xx_rnc_init(&value, device, 0);
    result = xx_rnc_check_is_valid(&value.format, NULL);
    xx_rnc_destroy(&value);
    return result;
}

static bool xx_format_is_ibmpack_device(xx_io_device *device) {
    xx_ibmpack value;
    bool result;
    if (!device) return false;
    xx_ibmpack_init(&value, device, 0);
    result = xx_ibmpack_check_is_valid(&value.format, NULL);
    xx_ibmpack_destroy(&value);
    return result;
}

static bool xx_format_is_cazip_device(xx_io_device *device) {
    xx_cazip value;
    bool result;
    if (!device) return false;
    xx_cazip_init(&value, device, 0);
    result = xx_cazip_check_is_valid(&value.format, NULL);
    xx_cazip_destroy(&value);
    return result;
}

static bool xx_format_is_tpwm_device(xx_io_device *device) {
    xx_tpwm value;
    bool result;
    if (!device) return false;
    xx_tpwm_init(&value, device, 0);
    result = xx_tpwm_check_is_valid(&value.format, NULL);
    xx_tpwm_destroy(&value);
    return result;
}

static bool xx_format_is_mrnz_device(xx_io_device *device) {
    xx_mrnz value;
    bool result;
    if (!device) return false;
    xx_mrnz_init(&value, device, 0);
    result = xx_mrnz_check_is_valid(&value.format, NULL);
    xx_mrnz_destroy(&value);
    return result;
}

static bool xx_format_is_edc_device(xx_io_device *device) {
    xx_edc value;
    bool result;
    if (!device) return false;
    xx_edc_init(&value, device, 0);
    result = xx_edc_check_is_valid(&value.format, NULL);
    xx_edc_destroy(&value);
    return result;
}

static bool xx_format_is_mxs_device(xx_io_device *device) {
    xx_mxs value;
    bool result;
    if (!device) return false;
    xx_mxs_init(&value, device, 0);
    result = xx_mxs_check_is_valid(&value.format, NULL);
    xx_mxs_destroy(&value);
    return result;
}

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

static bool xx_format_is_mwave_device(xx_io_device *device) {
    xx_mwave value;
    bool result;
    if (!device) return false;
    xx_mwave_init(&value, device, 0);
    result = xx_mwave_check_is_valid(&value.format, NULL);
    xx_mwave_destroy(&value);
    return result;
}

static bool xx_format_is_finear_device(xx_io_device *device) {
    xx_finear value;
    bool result;
    if (!device) return false;
    xx_finear_init(&value, device, 0);
    result = xx_finear_check_is_valid(&value.format, NULL);
    xx_finear_destroy(&value);
    return result;
}

static bool xx_format_is_gst_device(xx_io_device *device) {
    xx_gst value;
    bool result;
    if (!device) return false;
    xx_gst_init(&value, device, 0);
    result = xx_gst_check_is_valid(&value.format, NULL);
    xx_gst_destroy(&value);
    return result;
}

static bool xx_format_is_winlink_device(xx_io_device *device) {
    xx_winlink value;
    bool result;
    if (!device) return false;
    xx_winlink_init(&value, device, 0);
    result = xx_winlink_check_is_valid(&value.format, NULL);
    xx_winlink_destroy(&value);
    return result;
}

static bool xx_format_is_ftcomp_device(xx_io_device *device) {
    xx_ftcomp value;
    bool result;
    if (!device) return false;
    xx_ftcomp_init(&value, device, 0);
    result = xx_ftcomp_check_is_valid(&value.format, NULL);
    xx_ftcomp_destroy(&value);
    return result;
}

static bool xx_format_is_gpfpack_device(xx_io_device *device) {
    xx_gpfpack value;
    bool result;
    if (!device) return false;
    xx_gpfpack_init(&value, device, 0);
    result = xx_gpfpack_check_is_valid(&value.format, NULL);
    xx_gpfpack_destroy(&value);
    return result;
}

static bool xx_format_is_sco_device(xx_io_device *device) {
    xx_sco value;
    bool result;
    if (!device) return false;
    xx_sco_init(&value, device, 0);
    result = xx_sco_check_is_valid(&value.format, NULL);
    xx_sco_destroy(&value);
    return result;
}

static bool xx_format_is_unixcompact_device(xx_io_device *device) {
    xx_unixcompact value;
    bool result;
    if (!device) return false;
    xx_unixcompact_init(&value, device, 0);
    result = xx_unixcompact_check_is_valid(&value.format, NULL);
    xx_unixcompact_destroy(&value);
    return result;
}

static bool xx_format_is_psdc_device(xx_io_device *device) {
    xx_psdc value;
    bool result;
    if (!device) return false;
    xx_psdc_init(&value, device, 0);
    result = xx_psdc_check_is_valid(&value.format, NULL);
    xx_psdc_destroy(&value);
    return result;
}

static bool xx_format_is_is3_device(xx_io_device *device) {
    xx_is3 value;
    bool result;
    if (!device) return false;
    xx_is3_init(&value, device, 0);
    result = xx_is3_check_is_valid(&value.format, NULL);
    xx_is3_destroy(&value);
    return result;
}

static bool xx_format_is_is5_device(xx_io_device *device) {
    xx_is5 value;
    bool result;
    if (!device) return false;
    xx_is5_init(&value, device, 0);
    result = xx_is5_check_is_valid(&value.format, NULL);
    xx_is5_destroy(&value);
    return result;
}

static bool xx_format_is_is7inx_device(xx_io_device *device) {
    xx_is7inx value;
    bool result;
    if (!device) return false;
    xx_is7inx_init(&value, device, 0);
    result = xx_is7inx_check_is_valid(&value.format, NULL);
    xx_is7inx_destroy(&value);
    return result;
}

static bool xx_format_is_edilzss_device(xx_io_device *device) {
    xx_edilzss value;
    bool result;
    if (!device) return false;
    xx_edilzss_init(&value, device, 0);
    result = xx_edilzss_check_is_valid(&value.format, NULL);
    xx_edilzss_destroy(&value);
    return result;
}

static bool xx_format_is_savedskf_device(xx_io_device *device) {
    xx_savedskf value;
    bool result;
    if (!device) return false;
    xx_savedskf_init(&value, device, 0);
    result = xx_savedskf_check_is_valid(&value.format, NULL);
    xx_savedskf_destroy(&value);
    return result;
}

static bool xx_format_is_gob_device(xx_io_device *device) {
    xx_gob value;
    bool result;
    if (!device) return false;
    xx_gob_init(&value, device, 0);
    result = xx_gob_check_is_valid(&value.format, NULL);
    xx_gob_destroy(&value);
    return result;
}

static bool xx_format_is_debugscr_device(xx_io_device *device) {
    xx_debugscr value;
    bool result;
    if (!device) return false;
    xx_debugscr_init(&value, device, 0);
    result = xx_debugscr_check_is_valid(&value.format, NULL);
    xx_debugscr_destroy(&value);
    return result;
}

static bool xx_format_is_dclft_device(xx_io_device *device) {
    xx_dclft value;
    bool result;
    if (!device) return false;
    xx_dclft_init(&value, device, 0);
    result = xx_dclft_check_is_valid(&value.format, NULL);
    xx_dclft_destroy(&value);
    return result;
}

static bool xx_format_is_stuffit_device(xx_io_device *device) {
    xx_stuffit value;
    bool result;
    if (!device) return false;
    xx_stuffit_init(&value, device, 0);
    result = xx_stuffit_check_is_valid(&value.format, NULL);
    xx_stuffit_destroy(&value);
    return result;
}

static bool xx_format_is_binaryii_device(xx_io_device *device) {
    xx_binaryii value;
    bool result;
    if (!device) return false;
    xx_binaryii_init(&value, device, 0);
    result = xx_binaryii_check_is_valid(&value.format, NULL);
    xx_binaryii_destroy(&value);
    return result;
}

static bool xx_format_is_binhex_device(xx_io_device *device) {
    xx_binhex value;
    bool result;
    if (!device) return false;
    xx_binhex_init(&value, device, 0);
    result = xx_binhex_check_is_valid(&value.format, NULL);
    xx_binhex_destroy(&value);
    return result;
}

static bool xx_format_is_pma_device(xx_io_device *device) {
    xx_pma value;
    bool result;
    if (!device) return false;
    xx_pma_init(&value, device, 0);
    result = xx_pma_check_is_valid(&value.format, NULL);
    xx_pma_destroy(&value);
    return result;
}

static bool xx_format_is_lzk00_device(xx_io_device *device) {
    xx_lzk00 value;
    bool result;
    if (!device) return false;
    xx_lzk00_init(&value, device, 0);
    result = xx_lzk00_check_is_valid(&value.format, NULL);
    xx_lzk00_destroy(&value);
    return result;
}

static bool xx_format_is_compaqlzh_device(xx_io_device *device) {
    xx_compaqlzh value;
    bool result;
    if (!device) return false;
    xx_compaqlzh_init(&value, device, 0);
    result = xx_compaqlzh_check_is_valid(&value.format, NULL);
    xx_compaqlzh_destroy(&value);
    return result;
}

static bool xx_format_is_arcv_device(xx_io_device *device) {
    xx_arcv value;
    bool result;
    if (!device) return false;
    xx_arcv_init(&value, device, 0);
    result = xx_arcv_check_is_valid(&value.format, NULL);
    xx_arcv_destroy(&value);
    return result;
}

static bool xx_format_is_lifkd_device(xx_io_device *device) {
    xx_lifkd value;
    bool result;
    if (!device) return false;
    xx_lifkd_init(&value, device, 0);
    result = xx_lifkd_check_is_valid(&value.format, NULL);
    xx_lifkd_destroy(&value);
    return result;
}

static bool xx_format_is_trdos_device(xx_io_device *device) {
    xx_trdos value;
    bool result;
    if (!device) return false;
    xx_trdos_init(&value, device, 0);
    result = xx_trdos_check_is_valid(&value.format, NULL);
    xx_trdos_destroy(&value);
    return result;
}

static bool xx_format_is_squeeze1_device(xx_io_device *device) {
    xx_squeeze1 value;
    bool result;
    if (!device) return false;
    xx_squeeze1_init(&value, device, 0);
    result = xx_squeeze1_check_is_valid(&value.format, NULL);
    xx_squeeze1_destroy(&value);
    return result;
}

static bool xx_format_is_izpack_device(xx_io_device *device) {
    xx_izpack value;
    bool result;
    if (!device) return false;
    xx_izpack_init(&value, device, 0);
    result = xx_izpack_check_is_valid(&value.format, NULL);
    xx_izpack_destroy(&value);
    return result;
}

static bool xx_format_is_is11_device(xx_io_device *device) {
    xx_is11 value;
    bool result;
    if (!device) return false;
    xx_is11_init(&value, device, 0);
    result = xx_is11_check_is_valid(&value.format, NULL);
    xx_is11_destroy(&value);
    return result;
}

static bool xx_format_is_gksetup_device(xx_io_device *device) {
    xx_gksetup value;
    bool result;
    if (!device) return false;
    xx_gksetup_init(&value, device, 0);
    result = xx_gksetup_check_is_valid(&value.format, NULL);
    xx_gksetup_destroy(&value);
    return result;
}

static bool xx_format_is_pcinstall_device(xx_io_device *device) {
    xx_pcinstall value;
    bool result;
    if (!device) return false;
    xx_pcinstall_init(&value, device, 0);
    result = xx_pcinstall_check_is_valid(&value.format, NULL);
    xx_pcinstall_destroy(&value);
    return result;
}

static bool xx_format_is_copyqm_device(xx_io_device *device) {
    xx_copyqm value;
    bool result;
    if (!device) return false;
    xx_copyqm_init(&value, device, 0);
    result = xx_copyqm_check_is_valid(&value.format, NULL);
    xx_copyqm_destroy(&value);
    return result;
}

static bool xx_format_is_teledisk_device(xx_io_device *device) {
    xx_teledisk value;
    bool result;
    if (!device) return false;
    xx_teledisk_init(&value, device, 0);
    result = xx_teledisk_check_is_valid(&value.format, NULL);
    xx_teledisk_destroy(&value);
    return result;
}

static bool xx_format_is_hfe_device(xx_io_device *device) {
    xx_hfe value;
    bool result;
    if (!device) return false;
    xx_hfe_init(&value, device, 0);
    result = xx_hfe_check_is_valid(&value.format, NULL);
    xx_hfe_destroy(&value);
    return result;
}

static bool xx_format_is_fdi_device(xx_io_device *device) {
    xx_fdi value;
    bool result;
    if (!device) return false;
    xx_fdi_init(&value, device, 0);
    result = xx_fdi_check_is_valid(&value.format, NULL);
    xx_fdi_destroy(&value);
    return result;
}

static bool xx_format_is_twoimg_device(xx_io_device *device) {
    xx_twoimg value;
    bool result;
    if (!device) return false;
    xx_twoimg_init(&value, device, 0);
    result = xx_twoimg_check_is_valid(&value.format, NULL);
    xx_twoimg_destroy(&value);
    return result;
}

static bool xx_format_is_imd_device(xx_io_device *device) {
    xx_imd value;
    bool result;
    if (!device) return false;
    xx_imd_init(&value, device, 0);
    result = xx_imd_check_is_valid(&value.format, NULL);
    xx_imd_destroy(&value);
    return result;
}

static bool xx_format_is_diskdupe_device(xx_io_device *device) {
    xx_diskdupe value;
    bool result;
    if (!device) return false;
    xx_diskdupe_init(&value, device, 0);
    result = xx_diskdupe_check_is_valid(&value.format, NULL);
    xx_diskdupe_destroy(&value);
    return result;
}

static bool xx_format_is_pmdiskcopy_device(xx_io_device *device) {
    xx_pmdiskcopy value;
    bool result;
    if (!device) return false;
    xx_pmdiskcopy_init(&value, device, 0);
    result = xx_pmdiskcopy_check_is_valid(&value.format, NULL);
    xx_pmdiskcopy_destroy(&value);
    return result;
}

static bool xx_format_is_diskjuggler_device(xx_io_device *device) {
    xx_diskjuggler value;
    bool result;
    if (!device) return false;
    xx_diskjuggler_init(&value, device, 0);
    result = xx_diskjuggler_check_is_valid(&value.format, NULL);
    xx_diskjuggler_destroy(&value);
    return result;
}

static bool xx_format_is_copyqmexe_device(xx_io_device *device) {
    xx_copyqmexe value;
    bool result;
    if (!device) return false;
    xx_copyqmexe_init(&value, device, 0);
    result = xx_copyqmexe_check_is_valid(&value.format, NULL);
    xx_copyqmexe_destroy(&value);
    return result;
}

static bool xx_format_is_pax_device(xx_io_device *device) {
    xx_pax value;
    bool result;
    if (!device) return false;
    xx_pax_init(&value, device, 0);
    result = xx_pax_check_is_valid(&value.format, NULL);
    xx_pax_destroy(&value);
    return result;
}

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

static bool xx_format_is_cpoint_device(xx_io_device *device) {
    xx_cpoint value;
    bool result;
    if (!device) return false;
    xx_cpoint_init(&value, device, 0);
    result = xx_cpoint_check_is_valid(&value.format, NULL);
    xx_cpoint_destroy(&value);
    return result;
}

static bool xx_format_is_elm_device(xx_io_device *device) {
    xx_elm value;
    bool result;

    xx_elm_init(&value, device, 0);
    result = xx_elm_is_css_trailer_variant(&value.format, NULL);
    xx_elm_destroy(&value);
    return result;
}

static bool xx_format_is_beospkg_device(xx_io_device *device) {
    xx_beospkg value;
    bool result;
    if (!device) return false;
    xx_beospkg_init(&value, device, 0);
    result = xx_beospkg_check_is_valid(&value.format, NULL);
    xx_beospkg_destroy(&value);
    return result;
}

static bool xx_format_is_vmspcsi_device(xx_io_device *device) {
    xx_vmspcsi value;
    bool result;
    if (!device) return false;
    xx_vmspcsi_init(&value, device, 0);
    result = xx_vmspcsi_check_is_valid(&value.format, NULL);
    xx_vmspcsi_destroy(&value);
    return result;
}

static bool xx_format_is_vmsdb_device(xx_io_device *device) {
    xx_vmsdb value;
    bool result;
    if (!device) return false;
    xx_vmsdb_init(&value, device, 0);
    result = xx_vmsdb_check_is_valid(&value.format, NULL);
    xx_vmsdb_destroy(&value);
    return result;
}

static bool xx_format_is_pcxlib_device(xx_io_device *device) {
    xx_pcxlib value;
    bool result;
    if (!device) return false;
    xx_pcxlib_init(&value, device, 0);
    result = xx_pcxlib_check_is_valid(&value.format, NULL);
    xx_pcxlib_destroy(&value);
    return result;
}

static bool xx_format_is_hog2_device(xx_io_device *device) {
    xx_hog2 value;
    bool result;
    if (!device) return false;
    xx_hog2_init(&value, device, 0);
    result = xx_hog2_check_is_valid(&value.format, NULL);
    xx_hog2_destroy(&value);
    return result;
}

static bool xx_format_is_sinner_device(xx_io_device *device) {
    xx_sinner value;
    bool result;
    if (!device) return false;
    xx_sinner_init(&value, device, 0);
    result = xx_sinner_check_is_valid(&value.format, NULL);
    xx_sinner_destroy(&value);
    return result;
}

static bool xx_format_is_psn_device(xx_io_device *device) {
    xx_psn value;
    bool result;
    if (!device) return false;
    xx_psn_init(&value, device, 0);
    result = xx_psn_check_is_valid(&value.format, NULL);
    xx_psn_destroy(&value);
    return result;
}

static bool xx_format_is_grasp_device(xx_io_device *device) {
    xx_grasp value;
    bool result;
    if (!device) return false;
    xx_grasp_init(&value, device, 0);
    result = xx_grasp_check_is_valid(&value.format, NULL);
    xx_grasp_destroy(&value);
    return result;
}

static bool xx_format_is_megatechvol_device(xx_io_device *device) {
    xx_megatechvol value;
    bool result;
    if (!device) return false;
    xx_megatechvol_init(&value, device, 0);
    result = xx_megatechvol_check_is_valid(&value.format, NULL);
    xx_megatechvol_destroy(&value);
    return result;
}

static bool xx_format_is_stunts_device(xx_io_device *device) {
    xx_stunts value;
    bool result;
    if (!device) return false;
    xx_stunts_init(&value, device, 0);
    result = xx_stunts_check_is_valid(&value.format, NULL);
    xx_stunts_destroy(&value);
    return result;
}

static bool xx_format_is_notetab_device(xx_io_device *device) {
    xx_notetab value;
    bool result;
    if (!device) return false;
    xx_notetab_init(&value, device, 0);
    result = xx_notetab_check_is_valid(&value.format, NULL);
    xx_notetab_destroy(&value);
    return result;
}

static bool xx_format_is_mcc_device(xx_io_device *device) {
    xx_mcc value;
    bool result;
    if (!device) return false;
    xx_mcc_init(&value, device, 0);
    result = xx_mcc_check_is_valid(&value.format, NULL);
    xx_mcc_destroy(&value);
    return result;
}

static bool xx_format_is_tnef_device(xx_io_device *device) {
    xx_tnef value;
    bool result;
    if (!device) return false;
    xx_tnef_init(&value, device, 0);
    result = xx_tnef_check_is_valid(&value.format, NULL);
    xx_tnef_destroy(&value);
    return result;
}

static bool xx_format_is_opc_device(xx_io_device *device) {
    xx_opc value;
    bool result;
    if (!device) return false;
    xx_opc_init(&value, device, 0);
    result = xx_opc_check_is_valid(&value.format, NULL);
    xx_opc_destroy(&value);
    return result;
}

static bool xx_format_is_qrst_device(xx_io_device *device) {
    xx_qrst value;
    bool result;
    if (!device) return false;
    xx_qrst_init(&value, device, 0);
    result = xx_qrst_check_is_valid(&value.format, NULL);
    xx_qrst_destroy(&value);
    return result;
}

static bool xx_format_is_pain_device(xx_io_device *device) {
    xx_pain value;
    bool result;
    if (!device) return false;
    xx_pain_init(&value, device, 0);
    result = xx_pain_check_is_valid(&value.format, NULL);
    xx_pain_destroy(&value);
    return result;
}

static bool xx_format_is_xlas_device(xx_io_device *device) {
    xx_xlas value;
    bool result;
    if (!device) return false;
    xx_xlas_init(&value, device, 0);
    result = xx_xlas_check_is_valid(&value.format, NULL);
    xx_xlas_destroy(&value);
    return result;
}

static bool xx_format_is_mdcd_device(xx_io_device *device) {
    xx_mdcd value;
    bool result;
    if (!device) return false;
    xx_mdcd_init(&value, device, 0);
    result = xx_mdcd_check_is_valid(&value.format, NULL);
    xx_mdcd_destroy(&value);
    return result;
}

static bool xx_format_is_ssm_device(xx_io_device *device) {
    xx_ssm value;
    bool result;
    if (!device) return false;
    xx_ssm_init(&value, device, 0);
    result = xx_ssm_check_is_valid(&value.format, NULL);
    xx_ssm_destroy(&value);
    return result;
}

static bool xx_format_is_bvrp_device(xx_io_device *device) {
    xx_bvrp value;
    bool result;
    if (!device) return false;
    xx_bvrp_init(&value, device, 0);
    result = xx_bvrp_check_is_valid(&value.format, NULL);
    xx_bvrp_destroy(&value);
    return result;
}

static bool xx_format_is_bcw_device(xx_io_device *device) {
    xx_bcw value;
    bool result;
    if (!device) return false;
    xx_bcw_init(&value, device, 0);
    result = xx_bcw_check_is_valid(&value.format, NULL);
    xx_bcw_destroy(&value);
    return result;
}

static bool xx_format_is_scf_device(xx_io_device *device) {
    xx_scf value;
    bool result;
    if (!device) return false;
    xx_scf_init(&value, device, 0);
    result = xx_scf_check_is_valid(&value.format, NULL);
    xx_scf_destroy(&value);
    return result;
}

static bool xx_format_is_recognita_device(xx_io_device *device) {
    xx_recognita value;
    bool result;
    if (!device) return false;
    xx_recognita_init(&value, device, 0);
    result = xx_recognita_check_is_valid(&value.format, NULL);
    xx_recognita_destroy(&value);
    return result;
}

static bool xx_format_is_jasc_device(xx_io_device *device) {
    xx_jasc value;
    bool result;
    if (!device) return false;
    xx_jasc_init(&value, device, 0);
    result = xx_jasc_check_is_valid(&value.format, NULL);
    xx_jasc_destroy(&value);
    return result;
}

static bool xx_format_is_binder_device(xx_io_device *device) {
    xx_binder value;
    bool result;
    if (!device) return false;
    xx_binder_init(&value, device, 0);
    result = xx_binder_check_is_valid(&value.format, NULL);
    xx_binder_destroy(&value);
    return result;
}

static bool xx_format_is_csidos_device(xx_io_device *device) {
    xx_csidos value;
    bool result;
    if (!device) return false;
    xx_csidos_init(&value, device, 0);
    result = xx_csidos_check_is_valid(&value.format, NULL);
    xx_csidos_destroy(&value);
    return result;
}

static bool xx_format_is_cat_device(xx_io_device *device) {
    xx_cat value;
    bool result;
    if (!device) return false;
    xx_cat_init(&value, device, 0);
    result = xx_cat_check_is_valid(&value.format, NULL);
    xx_cat_destroy(&value);
    return result;
}

static bool xx_format_is_bnd_device(xx_io_device *device) {
    xx_bnd value;
    bool result;
    if (!device) return false;
    xx_bnd_init(&value, device, 0);
    result = xx_bnd_check_is_valid(&value.format, NULL);
    xx_bnd_destroy(&value);
    return result;
}

static bool xx_format_is_smsipak_device(xx_io_device *device) {
    xx_smsipak value;
    bool result;
    if (!device) return false;
    xx_smsipak_init(&value, device, 0);
    result = xx_smsipak_check_is_valid(&value.format, NULL);
    xx_smsipak_destroy(&value);
    return result;
}

static bool xx_format_is_cpx_device(xx_io_device *device) {
    xx_cpx value;
    bool result;
    if (!device) return false;
    xx_cpx_init(&value, device, 0);
    result = xx_cpx_check_is_valid(&value.format, NULL);
    xx_cpx_destroy(&value);
    return result;
}

static bool xx_format_is_diskexpress_device(xx_io_device *device) {
    xx_diskexpress value;
    bool result;
    if (!device) return false;
    xx_diskexpress_init(&value, device, 0);
    result = xx_diskexpress_check_is_valid(&value.format, NULL);
    xx_diskexpress_destroy(&value);
    return result;
}

static bool xx_format_is_red_device(xx_io_device *device) {
    xx_red value;
    bool result;
    if (!device) return false;
    xx_red_init(&value, device, 0);
    result = xx_red_check_is_valid(&value.format, NULL);
    xx_red_destroy(&value);
    return result;
}

static bool xx_format_is_shrinkwrap_device(xx_io_device *device) {
    xx_shrinkwrap value;
    bool result;
    if (!device) return false;
    xx_shrinkwrap_init(&value, device, 0);
    result = xx_shrinkwrap_check_is_valid(&value.format, NULL);
    xx_shrinkwrap_destroy(&value);
    return result;
}

static bool xx_format_is_gxl_device(xx_io_device *device) {
    xx_gxl value;
    bool result;
    if (!device) return false;
    xx_gxl_init(&value, device, 0);
    result = xx_gxl_check_is_valid(&value.format, NULL);
    xx_gxl_destroy(&value);
    return result;
}

static bool xx_format_is_aiaff_device(xx_io_device *device) {
    xx_aiaff value;
    bool result;
    if (!device) return false;
    xx_aiaff_init(&value, device, 0);
    result = xx_aiaff_check_is_valid(&value.format, NULL);
    xx_aiaff_destroy(&value);
    return result;
}

static bool xx_format_is_softpaq2_device(xx_io_device *device) {
    xx_softpaq2 value;
    bool result;
    if (!device) return false;
    xx_softpaq2_init(&value, device, 0);
    result = xx_softpaq2_check_is_valid(&value.format, NULL);
    xx_softpaq2_destroy(&value);
    return result;
}

static bool xx_format_is_wim_device(xx_io_device *device) {
    xx_wim value;
    bool result;
    if (!device) return false;
    xx_wim_init(&value, device, 0);
    result = xx_wim_check_is_valid(&value.format, NULL);
    xx_wim_destroy(&value);
    return result;
}

static bool xx_format_is_vhddynamic_device(xx_io_device *device) {
    xx_vhddynamic value;
    bool result;
    if (!device) return false;
    xx_vhddynamic_init(&value, device, 0);
    result = xx_vhddynamic_check_is_valid(&value.format, NULL);
    xx_vhddynamic_destroy(&value);
    return result;
}

static bool xx_format_is_vmdk_device(xx_io_device *device) {
    xx_vmdk value;
    bool result;
    if (!device) return false;
    xx_vmdk_init(&value, device, 0);
    result = xx_vmdk_check_is_valid(&value.format, NULL);
    xx_vmdk_destroy(&value);
    return result;
}

static bool xx_format_is_ciso_device(xx_io_device *device) {
    xx_ciso value;
    bool result;
    if (!device) return false;
    xx_ciso_init(&value, device, 0);
    result = xx_ciso_check_is_valid(&value.format, NULL);
    xx_ciso_destroy(&value);
    return result;
}

static bool xx_format_is_copydisk_device(xx_io_device *device) {
    xx_copydisk value;
    bool result;
    if (!device) return false;
    xx_copydisk_init(&value, device, 0);
    result = xx_copydisk_check_is_valid(&value.format, NULL);
    xx_copydisk_destroy(&value);
    return result;
}

static bool xx_format_is_hdcopy_device(xx_io_device *device) {
    xx_hdcopy value;
    bool result;
    if (!device) return false;
    xx_hdcopy_init(&value, device, 0);
    result = xx_hdcopy_check_is_valid(&value.format, NULL);
    xx_hdcopy_destroy(&value);
    return result;
}

static bool xx_format_is_apricot_device(xx_io_device *device) {
    xx_apricot value;
    bool result;
    if (!device) return false;
    xx_apricot_init(&value, device, 0);
    result = xx_apricot_check_is_valid(&value.format, NULL);
    xx_apricot_destroy(&value);
    return result;
}

static bool xx_format_is_sabdu_device(xx_io_device *device) {
    xx_sabdu value;
    bool result;
    if (!device) return false;
    xx_sabdu_init(&value, device, 0);
    result = xx_sabdu_check_is_valid(&value.format, NULL);
    xx_sabdu_destroy(&value);
    return result;
}

static bool xx_format_is_mpq_device(xx_io_device *device) {
    xx_mpq value;
    bool result;
    if (!device) return false;
    xx_mpq_init(&value, device, 0);
    result = xx_mpq_check_is_valid(&value.format, NULL);
    xx_mpq_destroy(&value);
    return result;
}

static bool xx_format_is_phar_device(xx_io_device *device) {
    xx_phar value;
    bool result;
    if (!device) return false;
    xx_phar_init(&value, device, 0);
    result = xx_phar_check_is_valid(&value.format, NULL);
    xx_phar_destroy(&value);
    return result;
}

static bool xx_format_is_sq_device(xx_io_device *device) {
    xx_sq value;
    bool result;
    if (!device) return false;
    xx_sq_init(&value, device, 0);
    result = xx_sq_check_is_valid(&value.format, NULL);
    xx_sq_destroy(&value);
    return result;
}

static bool xx_format_is_squeeze2_device(xx_io_device *device) {
    xx_squeeze2 value;
    bool result;
    if (!device) return false;
    xx_squeeze2_init(&value, device, 0);
    result = xx_squeeze2_check_is_valid(&value.format, NULL);
    xx_squeeze2_destroy(&value);
    return result;
}

static bool xx_format_is_dbz_device(xx_io_device *device) {
    xx_dbz value;
    bool result;
    if (!device) return false;
    xx_dbz_init(&value, device, 0);
    result = xx_dbz_check_is_valid(&value.format, NULL);
    xx_dbz_destroy(&value);
    return result;
}

static bool xx_format_is_stac_device(xx_io_device *device) {
    xx_stac value;
    bool result;
    if (!device) return false;
    xx_stac_init(&value, device, 0);
    result = xx_stac_check_is_valid(&value.format, NULL);
    xx_stac_destroy(&value);
    return result;
}

static bool xx_format_is_spk_device(xx_io_device *device) {
    xx_spk value;
    bool result;
    if (!device) return false;
    xx_spk_init(&value, device, 0);
    result = xx_spk_check_is_valid(&value.format, NULL);
    xx_spk_destroy(&value);
    return result;
}

static bool xx_format_is_wrzl_device(xx_io_device *device) {
    xx_wrzl value;
    bool result;
    if (!device) return false;
    xx_wrzl_init(&value, device, 0);
    result = xx_wrzl_check_is_valid(&value.format, NULL);
    xx_wrzl_destroy(&value);
    return result;
}

static bool xx_format_is_bagf_device(xx_io_device *device) {
    xx_bagf value;
    bool result;
    if (!device) return false;
    xx_bagf_init(&value, device, 0);
    result = xx_bagf_check_is_valid(&value.format, NULL);
    xx_bagf_destroy(&value);
    return result;
}

static bool xx_format_is_emt_device(xx_io_device *device) {
    xx_emt value;
    bool result;
    if (!device) return false;
    xx_emt_init(&value, device, 0);
    result = xx_emt_check_is_valid(&value.format, NULL);
    xx_emt_destroy(&value);
    return result;
}

static bool xx_format_is_qip2_device(xx_io_device *device) {
    xx_qip2 value;
    bool result;
    if (!device) return false;
    xx_qip2_init(&value, device, 0);
    result = xx_qip2_check_is_valid(&value.format, NULL);
    xx_qip2_destroy(&value);
    return result;
}

static bool xx_format_is_lif_device(xx_io_device *device) {
    xx_lif value;
    bool result;
    if (!device) return false;
    xx_lif_init(&value, device, 0);
    result = xx_lif_check_is_valid(&value.format, NULL);
    xx_lif_destroy(&value);
    return result;
}

static bool xx_format_is_ixa_device(xx_io_device *device) {
    xx_ixa value;
    bool result;
    if (!device) return false;
    xx_ixa_init(&value, device, 0);
    result = xx_ixa_check_is_valid(&value.format, NULL);
    xx_ixa_destroy(&value);
    return result;
}

static bool xx_format_is_lspack10_device(xx_io_device *device) {
    xx_lspack10 value;
    bool result;
    if (!device) return false;
    xx_lspack10_init(&value, device, 0);
    result = xx_lspack10_check_is_valid(&value.format, NULL);
    xx_lspack10_destroy(&value);
    return result;
}

static bool xx_format_is_starkit_device(xx_io_device *device) {
    xx_starkit value;
    bool result;
    if (!device) return false;
    xx_starkit_init(&value, device, 0);
    result = xx_starkit_check_is_valid(&value.format, NULL);
    xx_starkit_destroy(&value);
    return result;
}

static bool xx_format_is_paperport_device(xx_io_device *device) {
    xx_paperport value;
    bool result;
    if (!device) return false;
    xx_paperport_init(&value, device, 0);
    result = xx_paperport_check_is_valid(&value.format, NULL);
    xx_paperport_destroy(&value);
    return result;
}

static bool xx_format_is_rnca_device(xx_io_device *device) {
    xx_rnca value;
    bool result;
    if (!device) return false;
    xx_rnca_init(&value, device, 0);
    result = xx_rnca_check_is_valid(&value.format, NULL);
    xx_rnca_destroy(&value);
    return result;
}

static bool xx_format_is_hog_device(xx_io_device *device) {
    xx_hog value;
    bool result;
    if (!device) return false;
    xx_hog_init(&value, device, 0);
    result = xx_hog_check_is_valid(&value.format, NULL);
    xx_hog_destroy(&value);
    return result;
}

static bool xx_format_is_agis_device(xx_io_device *device) {
    xx_agis value;
    bool result;
    if (!device) return false;
    xx_agis_init(&value, device, 0);
    result = xx_agis_check_is_valid(&value.format, NULL);
    xx_agis_destroy(&value);
    return result;
}

static bool xx_format_is_volitionvpft_device(xx_io_device *device) {
    xx_volitionvpft value;
    bool result;
    if (!device) return false;
    xx_volitionvpft_init(&value, device, 0);
    result = xx_volitionvpft_check_is_valid(&value.format, NULL);
    xx_volitionvpft_destroy(&value);
    return result;
}

static bool xx_format_is_wintermutedcp_device(xx_io_device *device) {
    xx_wintermutedcp value;
    bool result;
    if (!device) return false;
    xx_wintermutedcp_init(&value, device, 0);
    result = xx_wintermutedcp_check_is_valid(&value.format, NULL);
    xx_wintermutedcp_destroy(&value);
    return result;
}

static bool xx_format_is_bsn_device(xx_io_device *device) {
    xx_bsn value;
    bool result;
    if (!device) return false;
    xx_bsn_init(&value, device, 0);
    result = xx_bsn_check_is_valid(&value.format, NULL);
    xx_bsn_destroy(&value);
    return result;
}

static bool xx_format_is_res_device(xx_io_device *device) {
    xx_res value;
    bool result;
    if (!device) return false;
    xx_res_init(&value, device, 0);
    result = xx_res_check_is_valid(&value.format, NULL);
    xx_res_destroy(&value);
    return result;
}

static bool xx_format_is_rsc_device(xx_io_device *device) {
    xx_rsc value;
    bool result;
    if (!device) return false;
    xx_rsc_init(&value, device, 0);
    result = xx_rsc_check_is_valid(&value.format, NULL);
    xx_rsc_destroy(&value);
    return result;
}

static bool xx_format_is_teacy_device(xx_io_device *device) {
    xx_teacy value;
    bool result;
    if (!device) return false;
    xx_teacy_init(&value, device, 0);
    result = xx_teacy_check_is_valid(&value.format, NULL);
    xx_teacy_destroy(&value);
    return result;
}

static bool xx_format_is_settlersft_device(xx_io_device *device) {
    xx_settlersft value;
    bool result;
    if (!device) return false;
    xx_settlersft_init(&value, device, 0);
    result = xx_settlersft_check_is_valid(&value.format, NULL);
    xx_settlersft_destroy(&value);
    return result;
}

static bool xx_format_is_wolfft_device(xx_io_device *device) {
    xx_wolfft value;
    bool result;
    if (!device) return false;
    xx_wolfft_init(&value, device, 0);
    result = xx_wolfft_check_is_valid(&value.format, NULL);
    xx_wolfft_destroy(&value);
    return result;
}

static bool xx_format_is_boo_device(xx_io_device *device) {
    xx_boo value;
    bool result;
    if (!device) return false;
    xx_boo_init(&value, device, 0);
    result = xx_boo_check_is_valid(&value.format, NULL);
    xx_boo_destroy(&value);
    return result;
}

static bool xx_format_is_vmssaveset_device(xx_io_device *device) {
    xx_vmssaveset value;
    bool result;
    if (!device) return false;
    xx_vmssaveset_init(&value, device, 0);
    result = xx_vmssaveset_check_is_valid(&value.format, NULL);
    xx_vmssaveset_destroy(&value);
    return result;
}

static bool xx_format_is_rawstac_device(xx_io_device *device) {
    xx_rawstac value;
    bool result;
    if (!device) return false;
    xx_rawstac_init(&value, device, 0);
    result = xx_rawstac_check_is_valid(&value.format, NULL);
    xx_rawstac_destroy(&value);
    return result;
}

static bool xx_format_is_androidboot_device(xx_io_device *device) {
    xx_androidboot value;
    bool result;
    if (!device) return false;
    xx_androidboot_init(&value, device, 0);
    result = xx_androidboot_check_is_valid(&value.format, NULL);
    xx_androidboot_destroy(&value);
    return result;
}

static bool xx_format_is_arcadyan_device(xx_io_device *device) {
    xx_arcadyan value;
    bool result;
    if (!device) return false;
    xx_arcadyan_init(&value, device, 0);
    result = xx_arcadyan_check_is_valid(&value.format, NULL);
    xx_arcadyan_destroy(&value);
    return result;
}

static bool xx_format_is_autel_device(xx_io_device *device) {
    xx_autel value;
    bool result;
    if (!device) return false;
    xx_autel_init(&value, device, 0);
    result = xx_autel_check_is_valid(&value.format, NULL);
    xx_autel_destroy(&value);
    return result;
}

static bool xx_format_is_dkbs_device(xx_io_device *device) {
    xx_dkbs value;
    bool result;
    if (!device) return false;
    xx_dkbs_init(&value, device, 0);
    result = xx_dkbs_check_is_valid(&value.format, NULL);
    xx_dkbs_destroy(&value);
    return result;
}

static bool xx_format_is_dlink_tlv_device(xx_io_device *device) {
    xx_dlink_tlv value;
    bool result;
    if (!device) return false;
    xx_dlink_tlv_init(&value, device, 0);
    result = xx_dlink_tlv_check_is_valid(&value.format, NULL);
    xx_dlink_tlv_destroy(&value);
    return result;
}

static bool xx_format_is_dlke_device(xx_io_device *device) {
    xx_dlke value;
    bool result;
    if (!device) return false;
    xx_dlke_init(&value, device, 0);
    result = xx_dlke_check_is_valid(&value.format, NULL);
    xx_dlke_destroy(&value);
    return result;
}

static bool xx_format_is_ecos_device(xx_io_device *device) {
    xx_ecos value;
    bool result;
    if (!device) return false;
    xx_ecos_init(&value, device, 0);
    result = xx_ecos_check_is_valid(&value.format, NULL);
    xx_ecos_destroy(&value);
    return result;
}

static bool xx_format_is_encfw_device(xx_io_device *device) {
    xx_encfw value;
    bool result;
    if (!device) return false;
    xx_encfw_init(&value, device, 0);
    result = xx_encfw_check_is_valid(&value.format, NULL);
    xx_encfw_destroy(&value);
    return result;
}

static bool xx_format_is_encrpted_img_device(xx_io_device *device) {
    xx_encrpted_img value;
    bool result;
    if (!device) return false;
    xx_encrpted_img_init(&value, device, 0);
    result = xx_encrpted_img_check_is_valid(&value.format, NULL);
    xx_encrpted_img_destroy(&value);
    return result;
}

static bool xx_format_is_jboot_device(xx_io_device *device) {
    xx_jboot value;
    bool result;
    if (!device) return false;
    xx_jboot_init(&value, device, 0);
    result = xx_jboot_check_is_valid(&value.format, NULL);
    xx_jboot_destroy(&value);
    return result;
}

static bool xx_format_is_lingvoarc_device(xx_io_device *device) {
    xx_lingvoarc value;
    bool result;
    if (!device) return false;
    xx_lingvoarc_init(&value, device, 0);
    result = xx_lingvoarc_check_is_valid(&value.format, NULL);
    xx_lingvoarc_destroy(&value);
    return result;
}

static bool xx_format_is_lz4demo_device(xx_io_device *device) {
    xx_lz4demo value;
    bool result;
    if (!device) return false;
    xx_lz4demo_init(&value, device, 0);
    result = xx_lz4demo_check_is_valid(&value.format, NULL);
    xx_lz4demo_destroy(&value);
    return result;
}

static bool xx_format_is_matter_ota_device(xx_io_device *device) {
    xx_matter_ota value;
    bool result;
    if (!device) return false;
    xx_matter_ota_init(&value, device, 0);
    result = xx_matter_ota_check_is_valid(&value.format, NULL);
    xx_matter_ota_destroy(&value);
    return result;
}

static bool xx_format_is_mh01_device(xx_io_device *device) {
    xx_mh01 value;
    bool result;
    if (!device) return false;
    xx_mh01_init(&value, device, 0);
    result = xx_mh01_check_is_valid(&value.format, NULL);
    xx_mh01_destroy(&value);
    return result;
}

static bool xx_format_is_shrs_device(xx_io_device *device) {
    xx_shrs value;
    bool result;
    if (!device) return false;
    xx_shrs_init(&value, device, 0);
    result = xx_shrs_check_is_valid(&value.format, NULL);
    xx_shrs_destroy(&value);
    return result;
}

static bool xx_format_is_silmarilsft_device(xx_io_device *device) {
    xx_silmarilsft value;
    bool result;
    if (!device) return false;
    xx_silmarilsft_init(&value, device, 0);
    result = xx_silmarilsft_check_is_valid(&value.format, NULL);
    xx_silmarilsft_destroy(&value);
    return result;
}

static bool xx_format_is_tplink_device(xx_io_device *device) {
    xx_tplink value;
    bool result;
    if (!device) return false;
    xx_tplink_init(&value, device, 0);
    result = xx_tplink_check_is_valid(&value.format, NULL);
    xx_tplink_destroy(&value);
    return result;
}

static bool xx_format_is_twrx_device(xx_io_device *device) {
    xx_twrx value;
    bool result;
    if (!device) return false;
    xx_twrx_init(&value, device, 0);
    result = xx_twrx_check_is_valid(&value.format, NULL);
    xx_twrx_destroy(&value);
    return result;
}

static bool xx_format_is_uboot_device(xx_io_device *device) {
    xx_uboot value;
    bool result;
    if (!device) return false;
    xx_uboot_init(&value, device, 0);
    result = xx_uboot_check_is_valid(&value.format, NULL);
    xx_uboot_destroy(&value);
    return result;
}

static bool xx_format_is_infogramesft_device(xx_io_device *device) {
    xx_infogramesft value;
    bool result;
    if (!device) return false;
    xx_infogramesft_init(&value, device, 0);
    result = xx_infogramesft_check_is_valid(&value.format, NULL);
    xx_infogramesft_destroy(&value);
    return result;
}

static bool xx_format_is_pdb_device(xx_io_device *device) {
    xx_pdb value;
    bool result;
    if (!device) return false;
    xx_pdb_init(&value, device, 0);
    result = xx_pdb_check_is_valid(&value.format, NULL);
    xx_pdb_destroy(&value);
    return result;
}

static bool xx_format_is_xpak_device(xx_io_device *device) {
    xx_xpak value;
    bool result;
    if (!device) return false;
    xx_xpak_init(&value, device, 0);
    result = xx_xpak_check_is_valid(&value.format, NULL);
    xx_xpak_destroy(&value);
    return result;
}

static bool xx_format_is_dclraw_device(xx_io_device *device) {
    xx_dclraw value;
    bool result;
    if (!device) return false;
    xx_dclraw_init(&value, device, 0);
    result = xx_dclraw_check_is_valid(&value.format, NULL);
    xx_dclraw_destroy(&value);
    return result;
}

static bool xx_format_is_srec_device(xx_io_device *device) {
    xx_srec value;
    bool result;
    if (!device) return false;
    xx_srec_init(&value, device, 0);
    result = xx_srec_check_is_valid(&value.format, NULL);
    xx_srec_destroy(&value);
    return result;
}

static bool xx_format_is_lzop_device(xx_io_device *device) {
    xx_lzop value;
    bool result;
    if (!device) return false;
    xx_lzop_init(&value, device, 0);
    result = xx_lzop_check_is_valid(&value.format, NULL);
    xx_lzop_destroy(&value);
    return result;
}

static bool xx_format_is_raw_deflate_compressed_data_device(xx_io_device *device) {
    xx_raw_deflate_compressed_data value;
    bool result;
    if (!device) return false;
    xx_raw_deflate_compressed_data_init(&value, device, 0);
    result = xx_raw_deflate_compressed_data_check_is_valid(&value.format, NULL);
    xx_raw_deflate_compressed_data_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_apple_disk_copy_6_ndif_image_device(xx_io_device *device) {
    xx_apple_disk_copy_6_ndif_image value;
    bool result;
    if (!device) return false;
    xx_apple_disk_copy_6_ndif_image_init(&value, device, 0);
    result = xx_apple_disk_copy_6_ndif_image_check_is_valid(&value.format, NULL);
    xx_apple_disk_copy_6_ndif_image_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_apple_sparse_bundle_device(xx_io_device *device) {
    xx_apple_sparse_bundle value;
    bool result;
    if (!device) return false;
    xx_apple_sparse_bundle_init(&value, device, 0);
    result = xx_apple_sparse_bundle_check_is_valid(&value.format, NULL);
    xx_apple_sparse_bundle_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_encrypted_apple_disk_image_device(xx_io_device *device) {
    xx_encrypted_apple_disk_image value;
    bool result;
    if (!device) return false;
    xx_encrypted_apple_disk_image_init(&value, device, 0);
    result = xx_encrypted_apple_disk_image_check_is_valid(&value.format, NULL);
    xx_encrypted_apple_disk_image_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_hxc_stream_hfe_device(xx_io_device *device) {
    xx_hxc_stream_hfe value;
    bool result;
    if (!device) return false;
    xx_hxc_stream_hfe_init(&value, device, 0);
    result = xx_hxc_stream_hfe_check_is_valid(&value.format, NULL);
    xx_hxc_stream_hfe_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_ms_dos_backup_device(xx_io_device *device) {
    xx_ms_dos_backup value;
    bool result;
    if (!device) return false;
    xx_ms_dos_backup_init(&value, device, 0);
    result = xx_ms_dos_backup_check_is_valid(&value.format, NULL);
    xx_ms_dos_backup_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_nec_pc_98_fdi_device(xx_io_device *device) {
    xx_nec_pc_98_fdi value;
    bool result;
    if (!device) return false;
    xx_nec_pc_98_fdi_init(&value, device, 0);
    result = xx_nec_pc_98_fdi_check_is_valid(&value.format, NULL);
    xx_nec_pc_98_fdi_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_ns2_device(xx_io_device *device) {
    xx_ns2 value;
    bool result;
    if (!device) return false;
    xx_ns2_init(&value, device, 0);
    result = xx_ns2_check_is_valid(&value.format, NULL);
    xx_ns2_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_nsa_device(xx_io_device *device) {
    xx_nsa value;
    bool result;
    if (!device) return false;
    xx_nsa_init(&value, device, 0);
    result = xx_nsa_check_is_valid(&value.format, NULL);
    xx_nsa_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_qcow1_device(xx_io_device *device) {
    xx_qcow1 value;
    bool result;
    if (!device) return false;
    xx_qcow1_init(&value, device, 0);
    result = xx_qcow1_check_is_valid(&value.format, NULL);
    xx_qcow1_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_qnap_nas_firmware_device(xx_io_device *device) {
    xx_qnap_nas_firmware value;
    bool result;
    if (!device) return false;
    xx_qnap_nas_firmware_init(&value, device, 0);
    result = xx_qnap_nas_firmware_check_is_valid(&value.format, NULL);
    xx_qnap_nas_firmware_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_rdb_device(xx_io_device *device) {
    xx_rdb value;
    bool result;
    if (!device) return false;
    xx_rdb_init(&value, device, 0);
    result = xx_rdb_check_is_valid(&value.format, NULL);
    xx_rdb_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_stuffit_split_file_device(xx_io_device *device) {
    xx_stuffit_split_file value;
    bool result;
    if (!device) return false;
    xx_stuffit_split_file_init(&value, device, 0);
    result = xx_stuffit_split_file_check_is_valid(&value.format, NULL);
    xx_stuffit_split_file_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_t98_next_nfd_device(xx_io_device *device) {
    xx_t98_next_nfd value;
    bool result;
    if (!device) return false;
    xx_t98_next_nfd_init(&value, device, 0);
    result = xx_t98_next_nfd_check_is_valid(&value.format, NULL);
    xx_t98_next_nfd_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_trs_80_jv1_device(xx_io_device *device) {
    xx_trs_80_jv1 value;
    bool result;
    if (!device) return false;
    xx_trs_80_jv1_init(&value, device, 0);
    result = xx_trs_80_jv1_check_is_valid(&value.format, NULL);
    xx_trs_80_jv1_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_trs_80_jv3_device(xx_io_device *device) {
    xx_trs_80_jv3 value;
    bool result;
    if (!device) return false;
    xx_trs_80_jv3_init(&value, device, 0);
    result = xx_trs_80_jv3_check_is_valid(&value.format, NULL);
    xx_trs_80_jv3_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_uharc_device(xx_io_device *device) {
    xx_uharc value;
    bool result;
    if (!device) return false;
    xx_uharc_init(&value, device, 0);
    result = xx_uharc_handle_base_info(&value.format, NULL);
    xx_uharc_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_visionaire_studio_vis_device(xx_io_device *device) {
    xx_visionaire_studio_vis value;
    bool result;
    if (!device) return false;
    xx_visionaire_studio_vis_init(&value, device, 0);
    result = xx_visionaire_studio_vis_check_is_valid(&value.format, NULL);
    xx_visionaire_studio_vis_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_x68000_dim_device(xx_io_device *device) {
    xx_x68000_dim value;
    bool result;
    if (!device) return false;
    xx_x68000_dim_init(&value, device, 0);
    result = xx_x68000_dim_check_is_valid(&value.format, NULL);
    xx_x68000_dim_destroy(&value);
    return result;
}

static XX_FORMAT_NOINLINE bool xx_format_is_xamarin_compressed_assembly_device(xx_io_device *device) {
    xx_xamarin_compressed_assembly value;
    bool result;
    if (!device) return false;
    xx_xamarin_compressed_assembly_init(&value, device, 0);
    result = xx_xamarin_compressed_assembly_check_is_valid(&value.format, NULL);
    xx_xamarin_compressed_assembly_destroy(&value);
    return result;
}

static bool xx_format_is_dms_device(xx_io_device *device) {
    xx_dms value;
    bool result;
    if (!device) return false;
    xx_dms_init(&value, device, 0);
    result = xx_dms_check_is_valid(&value.format, NULL);
    xx_dms_destroy(&value);
    return result;
}

static bool xx_format_is_dmg_device(xx_io_device *device) {
    xx_dmg value;
    bool result;
    if (!device) return false;
    xx_dmg_init(&value, device, 0);
    result = xx_dmg_handle_base_info(&value.format, NULL);
    xx_dmg_destroy(&value);
    return result;
}

static bool xx_format_is_iso9660_device(xx_io_device *device) {
    xx_iso9660 iso;
    bool result;
    if (!device) return false;
    xx_iso9660_init(&iso, device, 0);
    result = xx_iso9660_handle_base_info(&iso.format, NULL);
    xx_iso9660_destroy(&iso);
    return result;
}

#endif /* full format-detection helpers */

xx_file_type_t xx_format_get_parent_file_type(xx_file_type_t type) {
    switch (type) {
        /* ZIP containers. */
        case XX_FILE_TYPE_ZIP64:
        case XX_FILE_TYPE_JAR:
        case XX_FILE_TYPE_APK:
        case XX_FILE_TYPE_IPA:
            return XX_FILE_TYPE_ZIP;
        /* An npm package is a tar.gz with a package/ root, and a tar.gz is a
         * gzip stream, so this walks two levels. */
        case XX_FILE_TYPE_NPM:
            return XX_FILE_TYPE_TAR_GZ;
        case XX_FILE_TYPE_TAR_GZ:
            return XX_FILE_TYPE_GZ;
        case XX_FILE_TYPE_TAR_BZ2:
            return XX_FILE_TYPE_BZ2;
        case XX_FILE_TYPE_TAR_XZ:
            return XX_FILE_TYPE_XZ;
        /* The remaining compressed-tar variants have no standalone type for
         * their outer stream, so they hang directly off BINARY. */
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_NE:
        case XX_FILE_TYPE_LE:
        case XX_FILE_TYPE_LX:
            return XX_FILE_TYPE_MSDOS;
        /* Nothing is more generic than a binary, so this ends the chain. */
        case XX_FILE_TYPE_UNKNOWN:
        case XX_FILE_TYPE_BINARY:
            return XX_FILE_TYPE_UNKNOWN;
        default:
            return XX_FILE_TYPE_BINARY;
    }
}

size_t xx_format_get_file_type_chain(xx_file_type_t type,
                                     xx_file_type_t *types, size_t capacity) {
    xx_file_type_t stack[XX_FILE_TYPE_CHAIN_MAX];
    size_t count = 0U;
    xx_file_type_t current = type;

    if (type == XX_FILE_TYPE_UNKNOWN) {
        return 0U;
    }
    /* Walk from the most specific type towards BINARY, then reverse, so the
     * caller sees the chain outermost-container first. The bound also stops a
     * malformed parent table from looping forever. */
    while (current != XX_FILE_TYPE_UNKNOWN && count < XX_FILE_TYPE_CHAIN_MAX) {
        stack[count++] = current;
        current = xx_format_get_parent_file_type(current);
    }
    if (!types || count > capacity) {
        return count;
    }
    {
        size_t index;
        for (index = 0U; index < count; ++index) {
            types[index] = stack[count - 1U - index];
        }
    }
    return count;
}

xx_list_t *xx_format_get_file_types_device(xx_io_device *dev) {
    xx_file_type_t chain[XX_FILE_TYPE_CHAIN_MAX];
    xx_list_t *list = xx_list_create(sizeof(xx_file_type_t), NULL);
    size_t count;
    size_t index;

    if (!list) {
        return NULL;
    }
    count = xx_format_get_file_type_chain(xx_format_get_file_type_device(dev),
                                          chain, XX_FILE_TYPE_CHAIN_MAX);
    for (index = 0U; index < count; ++index) {
        if (!xx_list_append(list, &chain[index])) {
            xx_list_destroy(list);
            return NULL;
        }
    }
    return list;
}

#ifndef XXFC_FORMAT_DETECTION_LZMA_XZ_ONLY
/* Reader probes used inline by the detector. Each lives in its own
 * non-inlined frame: the reader structs are large, and the detector
 * must not hold dozens of them on one stack frame while a nested
 * probe (tar.zst, for example) decodes with a big stack buffer. */

static XX_FORMAT_NOINLINE bool xx_format_probe_installer_vise_windows(xx_io_device *dev) {
    xx_installer_vise_windows reader;
    bool valid;
    xx_installer_vise_windows_init(&reader, dev, 0);
    valid = xx_installer_vise_windows_check_is_valid(&reader.format, NULL);
    xx_installer_vise_windows_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_installshield_multiplatform(xx_io_device *dev) {
    xx_installshield_multiplatform reader;
    bool valid;
    xx_installshield_multiplatform_init(&reader, dev, 0);
    valid = xx_installshield_multiplatform_check_is_valid(&reader.format, NULL);
    xx_installshield_multiplatform_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_advanced_installer_bootstrapper(xx_io_device *dev) {
    xx_advanced_installer_bootstrapper reader;
    bool valid;
    xx_advanced_installer_bootstrapper_init(&reader, dev, 0);
    valid = xx_advanced_installer_bootstrapper_check_is_valid(&reader.format, NULL);
    xx_advanced_installer_bootstrapper_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_ifah_installer(xx_io_device *dev) {
    xx_ifah_installer reader;
    bool valid;
    xx_ifah_installer_init(&reader, dev, 0);
    valid = xx_ifah_installer_check_is_valid(&reader.format, NULL);
    xx_ifah_installer_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_installshield_7_setup(xx_io_device *dev) {
    xx_installshield_7_setup reader;
    bool valid;
    xx_installshield_7_setup_init(&reader, dev, 0);
    valid = xx_installshield_7_setup_check_is_valid(&reader.format, NULL);
    xx_installshield_7_setup_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_nullsoft_pimp(xx_io_device *dev) {
    xx_sfx_nullsoft_pimp reader;
    bool valid;
    xx_sfx_nullsoft_pimp_init(&reader, dev, 0);
    valid = xx_sfx_nullsoft_pimp_check_is_valid(&reader.format, NULL);
    xx_sfx_nullsoft_pimp_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_sydex_diskette_image(xx_io_device *dev) {
    xx_sfx_sydex_diskette_image reader;
    bool valid;
    xx_sfx_sydex_diskette_image_init(&reader, dev, 0);
    valid = xx_sfx_sydex_diskette_image_check_is_valid(&reader.format, NULL);
    xx_sfx_sydex_diskette_image_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_compaq_softpaq(xx_io_device *dev) {
    xx_sfx_compaq_softpaq reader;
    bool valid;
    xx_sfx_compaq_softpaq_init(&reader, dev, 0);
    valid = xx_sfx_compaq_softpaq_check_is_valid(&reader.format, NULL);
    xx_sfx_compaq_softpaq_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_wasp_windows_auto(xx_io_device *dev) {
    xx_sfx_wasp_windows_auto reader;
    bool valid;
    xx_sfx_wasp_windows_auto_init(&reader, dev, 0);
    valid = xx_sfx_wasp_windows_auto_check_is_valid(&reader.format, NULL);
    xx_sfx_wasp_windows_auto_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_wise_installation_system(xx_io_device *dev) {
    xx_wise_installation_system reader;
    bool valid;
    xx_wise_installation_system_init(&reader, dev, 0);
    valid = xx_wise_installation_system_check_is_valid(&reader.format, NULL);
    xx_wise_installation_system_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_eschalon_setup_epsf(xx_io_device *dev) {
    xx_eschalon_setup_epsf reader;
    bool valid;
    xx_eschalon_setup_epsf_init(&reader, dev, 0);
    valid = xx_eschalon_setup_epsf_check_is_valid(&reader.format, NULL);
    xx_eschalon_setup_epsf_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_gentee_installer(xx_io_device *dev) {
    xx_gentee_installer reader;
    bool valid;
    xx_gentee_installer_init(&reader, dev, 0);
    valid = xx_gentee_installer_check_is_valid(&reader.format, NULL);
    xx_gentee_installer_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_clickteam_install_creator(xx_io_device *dev) {
    xx_clickteam_install_creator reader;
    bool valid;
    xx_clickteam_install_creator_init(&reader, dev, 0);
    valid = xx_clickteam_install_creator_check_is_valid(&reader.format, NULL);
    xx_clickteam_install_creator_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_createinstall_instcrin_extractor(xx_io_device *dev) {
    xx_createinstall_instcrin_extractor reader;
    bool valid;
    xx_createinstall_instcrin_extractor_init(&reader, dev, 0);
    valid = xx_createinstall_instcrin_extractor_check_is_valid(&reader.format, NULL);
    xx_createinstall_instcrin_extractor_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfxstart(xx_io_device *dev) {
    xx_sfxstart reader;
    bool valid;
    xx_sfxstart_init(&reader, dev, 0);
    valid = xx_sfxstart_check_is_valid(&reader.format, NULL);
    xx_sfxstart_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_analogx_emucore_ffs(xx_io_device *dev) {
    xx_sfx_analogx_emucore_ffs reader;
    bool valid;
    xx_sfx_analogx_emucore_ffs_init(&reader, dev, 0);
    valid = xx_sfx_analogx_emucore_ffs_check_is_valid(&reader.format, NULL);
    xx_sfx_analogx_emucore_ffs_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_krzip(xx_io_device *dev) {
    xx_sfx_krzip reader;
    bool valid;
    xx_sfx_krzip_init(&reader, dev, 0);
    valid = xx_sfx_krzip_check_is_valid(&reader.format, NULL);
    xx_sfx_krzip_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_warpin_package(xx_io_device *dev) {
    xx_sfx_warpin_package reader;
    bool valid;
    xx_sfx_warpin_package_init(&reader, dev, 0);
    valid = xx_sfx_warpin_package_check_is_valid(&reader.format, NULL);
    xx_sfx_warpin_package_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_hci_instalit(xx_io_device *dev) {
    xx_sfx_hci_instalit reader;
    bool valid;
    xx_sfx_hci_instalit_init(&reader, dev, 0);
    valid = xx_sfx_hci_instalit_check_is_valid(&reader.format, NULL);
    xx_sfx_hci_instalit_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_clickteam_multimedia_fusion(xx_io_device *dev) {
    xx_sfx_clickteam_multimedia_fusion reader;
    bool valid;
    xx_sfx_clickteam_multimedia_fusion_init(&reader, dev, 0);
    valid = xx_sfx_clickteam_multimedia_fusion_check_is_valid(&reader.format, NULL);
    xx_sfx_clickteam_multimedia_fusion_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_abbyy_fine_objects(xx_io_device *dev) {
    xx_sfx_abbyy_fine_objects reader;
    bool valid;
    xx_sfx_abbyy_fine_objects_init(&reader, dev, 0);
    valid = xx_sfx_abbyy_fine_objects_check_is_valid(&reader.format, NULL);
    xx_sfx_abbyy_fine_objects_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_flashjester_jugglor(xx_io_device *dev) {
    xx_sfx_flashjester_jugglor reader;
    bool valid;
    xx_sfx_flashjester_jugglor_init(&reader, dev, 0);
    valid = xx_sfx_flashjester_jugglor_check_is_valid(&reader.format, NULL);
    xx_sfx_flashjester_jugglor_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_jgsoft_deploymaster_package(xx_io_device *dev) {
    xx_sfx_jgsoft_deploymaster_package reader;
    bool valid;
    xx_sfx_jgsoft_deploymaster_package_init(&reader, dev, 0);
    valid = xx_sfx_jgsoft_deploymaster_package_check_is_valid(&reader.format, NULL);
    xx_sfx_jgsoft_deploymaster_package_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_ardi_diskette_image(xx_io_device *dev) {
    xx_sfx_ardi_diskette_image reader;
    bool valid;
    xx_sfx_ardi_diskette_image_init(&reader, dev, 0);
    valid = xx_sfx_ardi_diskette_image_check_is_valid(&reader.format, NULL);
    xx_sfx_ardi_diskette_image_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_arni_installer_container(xx_io_device *dev) {
    xx_arni_installer_container reader;
    bool valid;
    xx_arni_installer_container_init(&reader, dev, 0);
    valid = xx_arni_installer_container_check_is_valid(&reader.format, NULL);
    xx_arni_installer_container_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_ej_technologies_install(xx_io_device *dev) {
    xx_ej_technologies_install reader;
    bool valid;
    xx_ej_technologies_install_init(&reader, dev, 0);
    valid = xx_ej_technologies_install_check_is_valid(&reader.format, NULL);
    xx_ej_technologies_install_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_installshield_3(xx_io_device *dev) {
    xx_installshield_3 reader;
    bool valid;
    xx_installshield_3_init(&reader, dev, 0);
    valid = xx_installshield_3_check_is_valid(&reader.format, NULL);
    xx_installshield_3_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_installshield_developer(xx_io_device *dev) {
    xx_installshield_developer reader;
    bool valid;
    xx_installshield_developer_init(&reader, dev, 0);
    valid = xx_installshield_developer_check_is_valid(&reader.format, NULL);
    xx_installshield_developer_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_ardi_installer(xx_io_device *dev) {
    xx_ardi_installer reader;
    bool valid;
    xx_ardi_installer_init(&reader, dev, 0);
    valid = xx_ardi_installer_check_is_valid(&reader.format, NULL);
    xx_ardi_installer_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_installshield_12_setup(xx_io_device *dev) {
    xx_installshield_12_setup reader;
    bool valid;
    xx_installshield_12_setup_init(&reader, dev, 0);
    valid = xx_installshield_12_setup_check_is_valid(&reader.format, NULL);
    xx_installshield_12_setup_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_qsetup_installation_suite(xx_io_device *dev) {
    xx_qsetup_installation_suite reader;
    bool valid;
    xx_qsetup_installation_suite_init(&reader, dev, 0);
    valid = xx_qsetup_installation_suite_check_is_valid(&reader.format, NULL);
    xx_qsetup_installation_suite_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_setup_factory(xx_io_device *dev) {
    xx_setup_factory reader;
    bool valid;
    xx_setup_factory_init(&reader, dev, 0);
    valid = xx_setup_factory_check_is_valid(&reader.format, NULL);
    xx_setup_factory_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_tarma_installer(xx_io_device *dev) {
    xx_tarma_installer reader;
    bool valid;
    xx_tarma_installer_init(&reader, dev, 0);
    valid = xx_tarma_installer_check_is_valid(&reader.format, NULL);
    xx_tarma_installer_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_kwaj(xx_io_device *dev) {
    xx_kwaj reader;
    bool valid;
    xx_kwaj_init(&reader, dev, 0);
    valid = xx_kwaj_check_is_valid(&reader.format, NULL);
    xx_kwaj_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_rpm(xx_io_device *dev) {
    xx_rpm reader;
    bool valid;
    xx_rpm_init(&reader, dev, 0);
    valid = xx_rpm_check_is_valid(&reader.format, NULL);
    xx_rpm_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_t64(xx_io_device *dev) {
    xx_t64 reader;
    bool valid;
    xx_t64_init(&reader, dev, 0);
    valid = xx_t64_check_is_valid(&reader.format, NULL);
    xx_t64_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_pc_magazine_flp(xx_io_device *dev) {
    xx_pc_magazine_flp reader;
    bool valid;
    xx_pc_magazine_flp_init(&reader, dev, 0);
    valid = xx_pc_magazine_flp_check_is_valid(&reader.format, NULL);
    xx_pc_magazine_flp_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_vdi(xx_io_device *dev) {
    xx_vdi reader;
    bool valid;
    xx_vdi_init(&reader, dev, 0);
    valid = xx_vdi_check_is_valid(&reader.format, NULL);
    xx_vdi_destroy(&reader);
    return valid;
}

static XX_FORMAT_NOINLINE bool xx_format_probe_cue(xx_io_device *dev) {
    xx_cue reader;
    bool valid;
    xx_cue_init(&reader, dev, 0);
    valid = xx_cue_check_is_valid(&reader.format, NULL);
    xx_cue_destroy(&reader);
    return valid;
}

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

static XX_FORMAT_NOINLINE bool xx_format_probe_sfx_softpaq4(xx_io_device *dev) {
    xx_sfx_softpaq4 reader;
    bool valid;
    xx_sfx_softpaq4_init(&reader, dev, 0);
    valid = xx_sfx_softpaq4_check_is_valid(&reader.format, NULL);
    xx_sfx_softpaq4_destroy(&reader);
    return valid;
}

static bool xx_format_is_mlb_ft_device(xx_io_device *device) {
    xx_mlb_ft value;
    bool result;
    if (!device) return false;
    xx_mlb_ft_init(&value, device, 0);
    result = xx_mlb_ft_check_is_valid(&value.format, NULL);
    xx_mlb_ft_destroy(&value);
    return result;
}

static bool xx_format_is_fss_device(xx_io_device *device) {
    xx_fss value;
    bool result;
    if (!device) return false;
    xx_fss_init(&value, device, 0);
    result = xx_fss_check_is_valid(&value.format, NULL);
    xx_fss_destroy(&value);
    return result;
}

static bool xx_format_is_epf_device(xx_io_device *device) {
    xx_epf value;
    bool result;
    if (!device) return false;
    xx_epf_init(&value, device, 0);
    result = xx_epf_check_is_valid(&value.format, NULL);
    xx_epf_destroy(&value);
    return result;
}

static bool xx_format_is_dfc_device(xx_io_device *device) {
    xx_dfc value;
    bool result;
    if (!device) return false;
    xx_dfc_init(&value, device, 0);
    result = xx_dfc_check_is_valid(&value.format, NULL);
    xx_dfc_destroy(&value);
    return result;
}

static bool xx_format_is_ppd_device(xx_io_device *device) {
    xx_ppd value;
    bool result;
    if (!device) return false;
    xx_ppd_init(&value, device, 0);
    result = xx_ppd_check_is_valid(&value.format, NULL);
    xx_ppd_destroy(&value);
    return result;
}

static bool xx_format_is_ka_device(xx_io_device *device) {
    xx_ka value;
    bool result;
    if (!device) return false;
    xx_ka_init(&value, device, 0);
    result = xx_ka_check_is_valid(&value.format, NULL);
    xx_ka_destroy(&value);
    return result;
}

static bool xx_format_is_dn_device(xx_io_device *device) {
    xx_dn value;
    bool result;
    if (!device) return false;
    xx_dn_init(&value, device, 0);
    result = xx_dn_check_is_valid(&value.format, NULL);
    xx_dn_destroy(&value);
    return result;
}

static bool xx_format_is_insa_device(xx_io_device *device) {
    xx_insa value;
    bool result;
    if (!device) return false;
    xx_insa_init(&value, device, 0);
    result = xx_insa_check_is_valid(&value.format, NULL);
    xx_insa_destroy(&value);
    return result;
}

static bool xx_format_is_thebat_msb_device(xx_io_device *device) {
    xx_thebat_msb value;
    bool result;
    if (!device) return false;
    xx_thebat_msb_init(&value, device, 0);
    result = xx_thebat_msb_check_is_valid(&value.format, NULL);
    xx_thebat_msb_destroy(&value);
    return result;
}

static bool xx_format_is_sfx_localzip_device(xx_io_device *device) {
    xx_sfx_localzip value;
    bool result;
    if (!device) return false;
    xx_sfx_localzip_init(&value, device, 0);
    result = xx_sfx_localzip_check_is_valid(&value.format, NULL);
    xx_sfx_localzip_destroy(&value);
    return result;
}

static bool xx_format_is_sun_java_binsh_device(xx_io_device *device) {
    xx_sun_java_binsh value;
    bool result;
    if (!device) return false;
    xx_sun_java_binsh_init(&value, device, 0);
    result = xx_sun_java_binsh_check_is_valid(&value.format, NULL);
    xx_sun_java_binsh_destroy(&value);
    return result;
}

static bool xx_format_is_installanywhere_unix_device(xx_io_device *device) {
    xx_installanywhere_unix value;
    bool result;
    if (!device) return false;
    xx_installanywhere_unix_init(&value, device, 0);
    result = xx_installanywhere_unix_check_is_valid(&value.format, NULL);
    xx_installanywhere_unix_destroy(&value);
    return result;
}

xx_file_type_t xx_format_get_file_type_device(xx_io_device *dev) {
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
                    * wrapper whose own header is "TL"; the cpio
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
                  (magic[0x0] == 0x03U && magic[0x1] == 0x24U && magic[0x2] == 0x01U && magic[0x3] == 0x01U ||
                   magic[0x0] == 0x03U && magic[0x1] == 0x24U && magic[0x2] == 0x33U && magic[0x3] == 0x01U);
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
    bool is_lofi = magic_size >= 0x24 && xx_rt_memcmp(magic, "lzma", 4U) == 0 && magic[4] == 0U;
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
    bool is_gst = (magic_size >= 32U && magic[0] == 0xe9U && magic[1] == 0xc8U && (magic[7] == 0x00U || magic[7] == 0x01U));
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
    bool is_lif = (magic_size >= 0x25U && magic[0] == 0x44U && (magic[1] == 0x43U || magic[1] == 0x4cU) && magic[2] == 2U && magic[3] == 0U);
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
                  magic[3] >= '1' && magic[3] <= '9';
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

    #include "xx_format_registered_fifty.inc"
    #include "xx_format_registered_second_fifty.inc"
    #include "xx_format_registered_third_fifty.inc"
    #include "xx_format_registered_fourth_fifty.inc"
    #include "xx_format_registered_fifth_fifty.inc"
    #include "xx_format_registered_sixth_fifty.inc"
    #include "xx_format_registered_seventh_fifty.inc"
    #include "xx_format_registered_eighth_fifty.inc"
    #include "xx_format_registered_ninth_fifty.inc"
    #include "xx_format_registered_tenth_fifty.inc"
    #include "xx_format_registered_eleventh_fifty.inc"
    #include "xx_format_registered_twelfth_fifty.inc"
    #include "xx_format_registered_thirteenth_fifty.inc"
    #include "xx_format_registered_fourteenth_fifty.inc"
    #include "xx_format_registered_fifteenth_fifty.inc"
    #include "xx_format_registered_sixteenth_fifty.inc"

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
    #include "xx_format_registered_fifth_fifty_carriers.inc"

    #include "xx_format_registered_sixth_fifty_carriers.inc"

    #include "xx_format_registered_seventh_fifty_carriers.inc"

    #include "xx_format_registered_eighth_fifty_carriers.inc"

    #include "xx_format_registered_ninth_fifty_carriers.inc"

    #include "xx_format_registered_tenth_fifty_carriers.inc"

    #include "xx_format_registered_eleventh_fifty_carriers.inc"

    #include "xx_format_registered_twelfth_fifty_carriers.inc"

    #include "xx_format_registered_thirteenth_fifty_carriers.inc"

    #include "xx_format_registered_fourteenth_fifty_carriers.inc"

    #include "xx_format_registered_fifteenth_fifty_carriers.inc"

    #include "xx_format_registered_sixteenth_fifty_carriers.inc"

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
        if (pe_type != XX_FILE_TYPE_UNKNOWN) return pe_type;
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
    if (is_lif) {
        if (xx_format_is_lif_device(dev)) {
            (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
            return XX_FILE_TYPE_LIF;
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
    #include "xx_format_registered_fourteenth_fifty_late.inc"
    #include "xx_format_registered_thirteenth_fifty_late.inc"
    #include "xx_format_registered_twelfth_fifty_late.inc"
    #include "xx_format_registered_eleventh_fifty_late.inc"
    #include "xx_format_registered_tenth_fifty_late.inc"
    #include "xx_format_registered_ninth_fifty_late.inc"
    #include "xx_format_registered_eighth_fifty_late.inc"
    #include "xx_format_registered_seventh_fifty_late.inc"
    #include "xx_format_registered_sixth_fifty_late.inc"
    #include "xx_format_registered_fifth_fifty_late.inc"
    #include "xx_format_registered_fourth_fifty_late.inc"
    #include "xx_format_registered_third_fifty_late.inc"
    #include "xx_format_registered_second_fifty_late.inc"
    #include "xx_format_registered_fifty_late.inc"
    #include "xx_format_registered_fifteenth_fifty_late.inc"
    #include "xx_format_registered_sixteenth_fifty_late.inc"

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

xx_file_type_t xx_format_get_file_type(Abstractformat *fmt) {
    if (!fmt) {
        return XX_FILE_TYPE_UNKNOWN;
    }
    if (fmt->get_file_type) {
        return fmt->get_file_type(fmt);
    }
    if (fmt->file_type != XX_FILE_TYPE_UNKNOWN) {
        return fmt->file_type;
    }
    if (fmt->device) {
        xx_format_set_file_type(fmt,
                                xx_format_get_file_type_device(fmt->device));
        return fmt->file_type;
    }
    return XX_FILE_TYPE_UNKNOWN;
}

Abstractformat *xx_format_create(xx_io_device *dev, int64_t base_address) {
    Abstractformat *fmt = (Abstractformat *)xx_mem_alloc(sizeof(Abstractformat));
    if (!fmt) {
        return NULL;
    }
    xx_format_init(fmt, dev, base_address);
    return fmt;
}

void xx_format_free(Abstractformat *fmt) {
    if (!fmt) {
        return;
    }
    xx_format_destroy(fmt);
    xx_mem_free(fmt);
}

/* ========================================================================= */
/* --- Metadata & Archive Record Lifecycle                               --- */
/* ========================================================================= */

void xx_meta_init(xx_meta *meta, uint32_t meta_id) {
    if (!meta) {
        return;
    }
    meta->meta_id = meta_id;
    xx_var_init(&meta->var);
}

void xx_meta_cleanup(xx_meta *meta) {
    if (!meta) {
        return;
    }
    /* Password metadata also appears in per-operation option lists, not only
       in the format-wide extra-parameter list.  Clear owned password storage
       before releasing it on every cleanup path. */
    xx_format_secure_clear_parameter(meta);
    xx_var_cleanup(&meta->var);
    meta->meta_id = 0;
}

void xx_meta_free_elem(void *element) {
    if (element) {
        xx_meta_cleanup((xx_meta *)element);
    }
}

void xx_archive_record_init(xx_archive_record *rec) {
    if (!rec) {
        return;
    }
    xx_mem_zero(rec, sizeof(xx_archive_record));
    rec->header_offset = -1;
    rec->data_offset = -1;
    xx_list_init(&rec->list_meta, sizeof(xx_meta), xx_meta_free_elem);
}

void xx_archive_record_cleanup(xx_archive_record *rec) {
    if (!rec) {
        return;
    }
    xx_list_cleanup(&rec->list_meta);
    xx_mem_zero(rec, sizeof(xx_archive_record));
    rec->header_offset = -1;
    rec->data_offset = -1;
}

void xx_archive_record_free_elem(void *element) {
    if (element) {
        xx_archive_record_cleanup((xx_archive_record *)element);
    }
}

bool xx_archive_record_add_meta(xx_archive_record *rec, uint32_t meta_id, const xx_var *var) {
    if (!rec) {
        return false;
    }
    xx_meta item;
    xx_meta_init(&item, meta_id);
    if (var) {
        if (!xx_var_copy(&item.var, var)) {
            xx_meta_cleanup(&item);
            return false;
        }
    }
    if (!xx_list_append(&rec->list_meta, &item)) {
        xx_meta_cleanup(&item);
        return false;
    }
    return true;
}

bool xx_archive_record_set_meta(xx_archive_record *rec, uint32_t meta_id, const xx_var *var) {
    if (!rec) {
        return false;
    }
    size_t count = rec->list_meta.count;
    for (size_t i = 0; i < count; ++i) {
        xx_meta *m = (xx_meta *)xx_list_at(&rec->list_meta, i);
        if (m && m->meta_id == meta_id) {
            return xx_var_copy(&m->var, var);
        }
    }
    return xx_archive_record_add_meta(rec, meta_id, var);
}

bool xx_archive_record_add_meta_str(xx_archive_record *rec, uint32_t meta_id, const char *str) {
    if (!rec || !str) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    if (!xx_var_set_str(&v, str)) {
        return false;
    }
    bool ok = xx_archive_record_add_meta(rec, meta_id, &v);
    xx_var_cleanup(&v);
    return ok;
}

bool xx_archive_record_add_meta_wstr(xx_archive_record *rec, uint32_t meta_id, const wchar_t *wstr) {
    if (!rec || !wstr) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    if (!xx_var_set_wstr(&v, wstr)) {
        return false;
    }
    bool ok = xx_archive_record_add_meta(rec, meta_id, &v);
    xx_var_cleanup(&v);
    return ok;
}

bool xx_archive_record_add_meta_i64(xx_archive_record *rec, uint32_t meta_id, int64_t val) {
    if (!rec) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    xx_var_set_i64(&v, val);
    return xx_archive_record_add_meta(rec, meta_id, &v);
}

bool xx_archive_record_add_meta_u64(xx_archive_record *rec, uint32_t meta_id, uint64_t val) {
    if (!rec) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    xx_var_set_u64(&v, val);
    return xx_archive_record_add_meta(rec, meta_id, &v);
}

bool xx_archive_record_set_meta_str(xx_archive_record *rec, uint32_t meta_id, const char *str) {
    if (!rec || !str) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    if (!xx_var_set_str(&v, str)) {
        return false;
    }
    bool ok = xx_archive_record_set_meta(rec, meta_id, &v);
    xx_var_cleanup(&v);
    return ok;
}

bool xx_archive_record_set_meta_wstr(xx_archive_record *rec, uint32_t meta_id, const wchar_t *wstr) {
    if (!rec || !wstr) {
        return false;
    }
    xx_var v;
    xx_var_init(&v);
    if (!xx_var_set_wstr(&v, wstr)) {
        return false;
    }
    bool ok = xx_archive_record_set_meta(rec, meta_id, &v);
    xx_var_cleanup(&v);
    return ok;
}

bool xx_archive_record_set_meta_i64(xx_archive_record *rec, uint32_t meta_id, int64_t val) {
    if (!rec) return false;
    xx_var v;
    xx_var_init(&v);
    xx_var_set_i64(&v, val);
    return xx_archive_record_set_meta(rec, meta_id, &v);
}

bool xx_archive_record_set_meta_u64(xx_archive_record *rec, uint32_t meta_id, uint64_t val) {
    if (!rec) return false;
    xx_var v;
    xx_var_init(&v);
    xx_var_set_u64(&v, val);
    return xx_archive_record_set_meta(rec, meta_id, &v);
}

bool xx_archive_record_set_meta_bool(xx_archive_record *rec, uint32_t meta_id, bool val) {
    if (!rec) return false;
    xx_var v;
    xx_var_init(&v);
    xx_var_set_bool(&v, val);
    return xx_archive_record_set_meta(rec, meta_id, &v);
}

const xx_var* xx_archive_record_find_meta(const xx_archive_record *rec, uint32_t meta_id) {
    if (!rec) {
        return NULL;
    }
    size_t count = rec->list_meta.count;
    for (size_t i = 0; i < count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at(&rec->list_meta, i);
        if (m && m->meta_id == meta_id) {
            return &m->var;
        }
    }
    return NULL;
}

const char* xx_archive_record_get_meta_str(const xx_archive_record *rec, uint32_t meta_id) {
    const xx_var *v = xx_archive_record_find_meta(rec, meta_id);
    return v ? xx_var_get_str(v) : NULL;
}

const wchar_t* xx_archive_record_get_meta_wstr(const xx_archive_record *rec, uint32_t meta_id) {
    const xx_var *v = xx_archive_record_find_meta(rec, meta_id);
    return v ? xx_var_get_wstr(v) : NULL;
}

int64_t xx_archive_record_get_meta_i64(const xx_archive_record *rec, uint32_t meta_id, int64_t default_val) {
    const xx_var *v = xx_archive_record_find_meta(rec, meta_id);
    return v ? xx_var_get_i64(v) : default_val;
}

uint64_t xx_archive_record_get_meta_u64(const xx_archive_record *rec, uint32_t meta_id, uint64_t default_val) {
    const xx_var *v = xx_archive_record_find_meta(rec, meta_id);
    return v ? xx_var_get_u64(v) : default_val;
}

bool xx_archive_record_get_meta_bool(const xx_archive_record *rec, uint32_t meta_id, bool default_val) {
    const xx_var *v = xx_archive_record_find_meta(rec, meta_id);
    return v ? xx_var_get_bool(v) : default_val;
}

/* ========================================================================= */
/* --- Archive Record Stream Reading Operations                          --- */
/* ========================================================================= */

void xx_archive_record_state_init(xx_archive_record_state *state, Abstractformat *fmt) {
    if (!state) {
        return;
    }
    xx_mem_zero(state, sizeof(xx_archive_record_state));
    state->format = fmt;
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    state->current_index = -1;
    state->total_records = -1;
    xx_list_init(&state->options, sizeof(xx_meta), xx_meta_free_elem);
}

void xx_archive_record_state_cleanup(xx_archive_record_state *state) {
    if (!state) {
        return;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_list_cleanup(&state->options);
    if (state->free_internal && state->internal_state) {
        state->free_internal(state->internal_state);
        state->internal_state = NULL;
    }
    xx_mem_zero(state, sizeof(xx_archive_record_state));
    state->current_index = -1;
    state->total_records = -1;
}

void xx_archive_record_state_free(xx_archive_record_state *state) {
    if (!state) {
        return;
    }
    xx_archive_record_state_cleanup(state);
    xx_mem_free(state);
}

xx_archive_record_state *xx_format_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    if (!xx_format_handle_split_format(f, pd)) {
        return NULL;
    }
    if (!f->base_info_handled) {
        xx_format_handle_base_info(f, pd);
    }
    if (f->create_archive_records_reading) {
        return (f->create_archive_records_reading)(f, options, pd);
    }
    return NULL;
}

const xx_archive_record *xx_format_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state) {
    if (!state) {
        return NULL;
    }
    if (f && f->get_current_archive_record) {
        return (f->get_current_archive_record)(f, state);
    }
    return state->has_record ? &state->current_record : NULL;
}

bool xx_format_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!state || !state->has_record) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->unpack_current_archive_record) {
        return (f->unpack_current_archive_record)(f, state, pd);
    }
    return false;
}

bool xx_format_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!state) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->archive_record_move_to_next) {
        return (f->archive_record_move_to_next)(f, state, pd);
    }
    return false;
}

void xx_format_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state) {
    if (!state) {
        return;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->free_archive_records_reading) {
        (f->free_archive_records_reading)(f, state);
    } else {
        xx_archive_record_state_free(state);
    }
}

/* ========================================================================= */
/* --- Archive Record Stream Writing / Packing State & Operations        --- */
/* ========================================================================= */

void xx_archive_write_state_init(xx_archive_write_state *state, Abstractformat *fmt) {
    if (!state) {
        return;
    }
    xx_mem_zero(state, sizeof(xx_archive_write_state));
    state->format = fmt;
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    state->current_index = -1;
    state->total_records = -1;
    xx_list_init(&state->options, sizeof(xx_meta), xx_meta_free_elem);
}

void xx_archive_write_state_cleanup(xx_archive_write_state *state) {
    if (!state) {
        return;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_list_cleanup(&state->options);
    if (state->free_internal && state->internal_state) {
        state->free_internal(state->internal_state);
        state->internal_state = NULL;
    }
    xx_mem_zero(state, sizeof(xx_archive_write_state));
    state->current_index = -1;
    state->total_records = -1;
}

void xx_archive_write_state_free(xx_archive_write_state *state) {
    if (!state) {
        return;
    }
    xx_archive_write_state_cleanup(state);
    xx_mem_free(state);
}

xx_archive_write_state *xx_format_create_archive_records_writing(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    if (!f) {
        return NULL;
    }
    if (f->create_archive_records_writing) {
        return (f->create_archive_records_writing)(f, options, pd);
    }
    return NULL;
}

bool xx_format_pack_archive_record(Abstractformat *f, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd) {
    if (!state) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->pack_archive_record) {
        return (f->pack_archive_record)(f, state, record, source_dev, pd);
    }
    return false;
}

bool xx_format_finalize_archive_records_writing(Abstractformat *f, xx_archive_write_state *state, xx_pd_struct *pd) {
    if (!state) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->finalize_archive_records_writing) {
        return (f->finalize_archive_records_writing)(f, state, pd);
    }
    return false;
}

void xx_format_free_archive_records_writing(Abstractformat *f, xx_archive_write_state *state) {
    if (!state) {
        return;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->free_archive_records_writing) {
        (f->free_archive_records_writing)(f, state);
    } else {
        xx_archive_write_state_free(state);
    }
}

/* ========================================================================= */
/* --- Data Struct Id <-> String Conversion & Stream Reading Operations  --- */
/* ========================================================================= */

const char *xx_format_data_struct_id_to_string(Abstractformat *f, uint32_t id) {
    if (id == XX_DATA_STRUCT_ID_RAW_DATA) {
        return "RAW_DATA";
    }
    if (f && f->data_struct_id_to_string) {
        return (f->data_struct_id_to_string)(f, id);
    }
    return "UNKNOWN";
}

uint32_t xx_format_data_struct_string_to_id(Abstractformat *f, const char *name) {
    if (f && f->data_struct_string_to_id) {
        return (f->data_struct_string_to_id)(f, name);
    }
    return 0;
}

const char *xx_data_struct_type_to_string(xx_data_struct_type_t type) {
    switch (type) {
        case XX_DATA_STRUCT_TYPE_STRUCT:    return "struct";
        case XX_DATA_STRUCT_TYPE_ENTRY:     return "entry";
        case XX_DATA_STRUCT_TYPE_FOOTER:    return "footer";
        case XX_DATA_STRUCT_TYPE_LOCATOR:   return "locator";
        case XX_DATA_STRUCT_TYPE_RAW_DATA:  return "raw_data";
        default:                            return "unknown";
    }
}

const char *xx_format_file_type_to_string(xx_file_type_t type) {
    switch (type) {
        case XX_FILE_TYPE_BZIP1: return "BZIP1";
        case XX_FILE_TYPE_FREEZE: return "Freeze";
        case XX_FILE_TYPE_LPAK: return "LPAK";
        case XX_FILE_TYPE_EXFAT: return "exFAT";
        case XX_FILE_TYPE_MAJIRO: return "Majiro";
        case XX_FILE_TYPE_WUX: return "WUX";
        case XX_FILE_TYPE_SDI: return "SDI";
        case XX_FILE_TYPE_NHD: return "NHD";
        case XX_FILE_TYPE_VIRTUAL98: return "Virtual98";
        case XX_FILE_TYPE_ANEX86_HDI: return "Anex86 HDI";
        case XX_FILE_TYPE_THOMSON_SAP: return "Thomson SAP";
        case XX_FILE_TYPE_NITROPLUS_NPK2: return "Nitroplus NPK2";
        case XX_FILE_TYPE_APPLE_PASCAL: return "Apple Pascal";
        case XX_FILE_TYPE_DMK: return "DMK";
        case XX_FILE_TYPE_MFS: return "Macintosh MFS";
        case XX_FILE_TYPE_HFS: return "Macintosh HFS";
        case XX_FILE_TYPE_CATSYSTEM_KIF: return "CatSystem KIF";
        case XX_FILE_TYPE_MALIE_LIB: return "Malie LIB";
        case XX_FILE_TYPE_NEXAS_PAC: return "NeXAS PAC";
        case XX_FILE_TYPE_NITROPLUS_NPA: return "Nitroplus NPA";
        case XX_FILE_TYPE_UFS1: return "BSD UFS1";
        case XX_FILE_TYPE_CPM: return "CP/M";
        case XX_FILE_TYPE_XVA: return "Xen XVA";
        case XX_FILE_TYPE_QLIE_PACK: return "Qlie PACK";
        case XX_FILE_TYPE_HFSPLUS: return "HFS+/HFSX";
        case XX_FILE_TYPE_PARTIMAGE: return "Partimage";
        case XX_FILE_TYPE_AARUFORMAT: return "AaruFormat";
        case XX_FILE_TYPE_ADFS: return "Acorn ADFS";
        case XX_FILE_TYPE_GDI: return "Dreamcast GDI";
        case XX_FILE_TYPE_MDS: return "Alcohol 120% MDS/MDF";
        case XX_FILE_TYPE_CBM_D64: return "Commodore D64";
        case XX_FILE_TYPE_CBM_D71: return "Commodore D71";
        case XX_FILE_TYPE_CBM_D81: return "Commodore D81";
        case XX_FILE_TYPE_CCD: return "CloneCD CCD/IMG/SUB";
        case XX_FILE_TYPE_CDRDAO_TOC: return "CDRDAO TOC";
        case XX_FILE_TYPE_DISKCOPY42: return "Apple DiskCopy 4.2";
        case XX_FILE_TYPE_ACORN_DFS: return "Acorn DFS";
        case XX_FILE_TYPE_FDCOPY_CFI: return "FDCOPY CFI";
        case XX_FILE_TYPE_ATARI_DOS2: return "Atari DOS 2.x filesystem";
        case XX_FILE_TYPE_APRIDISK: return "ApriDisk";
        case XX_FILE_TYPE_TI99_DSK: return "TI-99/4A DSK filesystem";
        case XX_FILE_TYPE_APPLE_DOS32: return "Apple DOS 3.2";
        case XX_FILE_TYPE_APPLE_DOS33_32: return "Apple DOS 3.3 32-sector";
        case XX_FILE_TYPE_CBM_D8X: return "Commodore 8050/8250 D80/D82";
        case XX_FILE_TYPE_CBM_D67: return "Commodore 2040 DOS 1 D67";
        case XX_FILE_TYPE_CBM_D90: return "Commodore D9060/D9090 D90";
        case XX_FILE_TYPE_SIMH_DISK: return "SIMH CP/M disk image";
        case XX_FILE_TYPE_SNATCHIT_CP2: return "Snatch-it CP2 disk image";
        case XX_FILE_TYPE_NORTHSTAR_NSI: return "North Star NSI disk image";
        case XX_FILE_TYPE_THOMSON_FD: return "Thomson FD disk image";
        case XX_FILE_TYPE_CMD_D1M: return "Commodore CMD D1M disk image";
        case XX_FILE_TYPE_CMD_D2M: return "Commodore CMD D2M disk image";
        case XX_FILE_TYPE_CMD_D4M: return "Commodore CMD D4M disk image";
        case XX_FILE_TYPE_PCE_PRI: return "PCE PRI bitstream disk image";
        case XX_FILE_TYPE_PCE_PFI: return "PCE PFI flux disk image";
        case XX_FILE_TYPE_PCE_PFDC_V0: return "PCE PFDC v0 sector disk image";
        case XX_FILE_TYPE_PCE_PFDC_V1: return "PCE PFDC v1 sector disk image";
        case XX_FILE_TYPE_PCE_PFDC_V2: return "PCE PFDC v2 sector disk image";
        case XX_FILE_TYPE_PCE_PFDC_V4: return "PCE PFDC v4 sector disk image";
        case XX_FILE_TYPE_PCE_PBI: return "PCE PBI block disk image";
        case XX_FILE_TYPE_PCE_PBIT: return "PCE PBIT bitstream disk image";
        case XX_FILE_TYPE_PCE_TC: return "PCE TransCopy TC bitstream disk image";
        case XX_FILE_TYPE_PCE_ANADISK: return "AnaDisk sector disk image";
        case XX_FILE_TYPE_PCE_XDF: return "PCE XDF 1.84 MB sector disk image";
        case XX_FILE_TYPE_EROFS: return "EROFS filesystem image";
        case XX_FILE_TYPE_OS9_RBF: return "Microware OS-9 RBF filesystem";
        case XX_FILE_TYPE_LDBS: return "LibDsk LDBS disk image";
        case XX_FILE_TYPE_LDBST: return "LibDsk LDBST text disk image";
        case XX_FILE_TYPE_BYTEKILLER: return "ByteKiller";
        case XX_FILE_TYPE_APPLE_DOS33: return "Apple DOS3.3";
        case XX_FILE_TYPE_ACORN_ATOM_DISK: return "acorn_atom_disk";
        case XX_FILE_TYPE_APOLLO_AFD: return "apollo_afd";
        case XX_FILE_TYPE_BGA: return "bga";
        case XX_FILE_TYPE_BGI: return "bgi";
        case XX_FILE_TYPE_BGI2: return "bgi2";
        case XX_FILE_TYPE_BINSCII: return "binscii";
        case XX_FILE_TYPE_BLINDWRITE_5_6_IMAGE: return "blindwrite_5_6_image";
        case XX_FILE_TYPE_BTRFS_STREAM: return "btrfs_stream";
        case XX_FILE_TYPE_CAMPUTERS_LYNX_LDF: return "camputers_lynx_ldf";
        case XX_FILE_TYPE_CPK: return "cpk";
        case XX_FILE_TYPE_CRT: return "crt";
        case XX_FILE_TYPE_D_LINK_ALPHA_ENCIMG_V2: return "d_link_alpha_encimg_v2";
        case XX_FILE_TYPE_D_LINK_FPKG_CPKG: return "d_link_fpkg_cpkg";
        case XX_FILE_TYPE_DAEMON_TOOLS_MDX: return "daemon_tools_mdx";
        case XX_FILE_TYPE_DART: return "dart";
        case XX_FILE_TYPE_DDD: return "ddd";
        case XX_FILE_TYPE_DIET_COMPRESSION: return "diet_compression";
        case XX_FILE_TYPE_DXA: return "dxa";
        case XX_FILE_TYPE_EA_FSH: return "ea_fsh";
        case XX_FILE_TYPE_EWF2_EX01: return "ewf2_ex01";
        case XX_FILE_TYPE_EWF2_LX01: return "ewf2_lx01";
        case XX_FILE_TYPE_EWF_L01: return "ewf_l01";
        case XX_FILE_TYPE_FMOD_SAMPLE_BANK: return "fmod_sample_bank";
        case XX_FILE_TYPE_GBI: return "gbi";
        case XX_FILE_TYPE_GOLDSRC_BSP: return "goldsrc_bsp";
        case XX_FILE_TYPE_HSF: return "hsf";
        case XX_FILE_TYPE_HTC_NBH_ROM_IMAGE: return "htc_nbh_rom_image";
        case XX_FILE_TYPE_HXC_HFE_EXTENDED: return "hxc_hfe_extended";
        case XX_FILE_TYPE_HXC_HFE_HDDD_A2_VARIANT: return "hxc_hfe_hddd_a2_variant";
        case XX_FILE_TYPE_HXC_HFE_V3: return "hxc_hfe_v3";
        case XX_FILE_TYPE_HXS: return "hxs";
        case XX_FILE_TYPE_JFFS2_OLD: return "jffs2_old";
        case XX_FILE_TYPE_JVC: return "jvc";
        case XX_FILE_TYPE_KGB_ARCHIVER: return "kgb_archiver";
        case XX_FILE_TYPE_KRYOFLUX_STREAM: return "kryoflux_stream";
        case XX_FILE_TYPE_LIVEMAKER: return "livemaker";
        case XX_FILE_TYPE_LZMA86: return "lzma86";
        case XX_FILE_TYPE_MAXIS_FAR_ARCHIVE: return "maxis_far_archive";
        case XX_FILE_TYPE_MGT: return "mgt";
        case XX_FILE_TYPE_MINIX: return "minix";
        case XX_FILE_TYPE_MOOF: return "moof";
        case XX_FILE_TYPE_MS_DOS_BACKUP2: return "ms_dos_backup2";
        case XX_FILE_TYPE_MUB: return "mub";
        case XX_FILE_TYPE_NOA: return "noa";
        case XX_FILE_TYPE_OUTLOOK_EXPRESS_DBX_MAILBOX: return "outlook_express_dbx_mailbox";
        case XX_FILE_TYPE_PARTCLONE_IMAGE: return "partclone_image";
        case XX_FILE_TYPE_PPMD: return "ppmd";
        case XX_FILE_TYPE_QUOTED_PRINTABLE_ENCODED_FIL: return "quoted_printable_encoded_fil";
        case XX_FILE_TYPE_RISC_OS_SPRITE: return "risc_os_sprite";
        case XX_FILE_TYPE_RPG_MAKER_RGSSAD: return "rpg_maker_rgssad";
        case XX_FILE_TYPE_SFARK_COMPRESSED_SOUNDFONT: return "sfark_compressed_soundfont";
        case XX_FILE_TYPE_SIS: return "sis";
        case XX_FILE_TYPE_SPECTRUM_UDI: return "spectrum_udi";
        case XX_FILE_TYPE_SQUASHFS_SQLZ: return "squashfs_sqlz";
        case XX_FILE_TYPE_STOS_MEMORY_BANK: return "stos_memory_bank";
        case XX_FILE_TYPE_STUFFITX: return "stuffitx";
        case XX_FILE_TYPE_SUFS: return "sufs";
        case XX_FILE_TYPE_SUNVTOC: return "sunvtoc";
        case XX_FILE_TYPE_TELLTALE_TTARCH: return "telltale_ttarch";
        case XX_FILE_TYPE_UFS2: return "ufs2";
        case XX_FILE_TYPE_UIF: return "uif";
        case XX_FILE_TYPE_VALVE_GCF_CACHE: return "valve_gcf_cache";
        case XX_FILE_TYPE_VALVE_XZP: return "valve_xzp";
        case XX_FILE_TYPE_VMDK_COWD_SPARSE: return "vmdk_cowd_sparse";
        case XX_FILE_TYPE_VMDK_SESPARSE: return "vmdk_sesparse";
        case XX_FILE_TYPE_XIAOMI_HDR1: return "xiaomi_hdr1";
        case XX_FILE_TYPE_XIAOMI_HDR2: return "xiaomi_hdr2";
        case XX_FILE_TYPE_XPK_COMPRESSED_FILE: return "xpk_compressed_file";
        case XX_FILE_TYPE_YENC_ENCODED_FILE: return "yenc_encoded_file";
        case XX_FILE_TYPE_YPF: return "ypf";

        case XX_FILE_TYPE_ZIP:    return "ZIP";
        case XX_FILE_TYPE_ZIP64:  return "ZIP64";
        case XX_FILE_TYPE_7ZIP:   return "7ZIP";
        case XX_FILE_TYPE_RAR:    return "RAR";
        case XX_FILE_TYPE_AR:     return "AR";
        case XX_FILE_TYPE_BZ2:    return "BZ2";
        case XX_FILE_TYPE_GZ:     return "GZ";
        case XX_FILE_TYPE_XZ:     return "XZ";
        case XX_FILE_TYPE_TAR:    return "TAR";
        case XX_FILE_TYPE_JAR:    return "JAR";
        case XX_FILE_TYPE_APK:    return "APK";
        case XX_FILE_TYPE_IPA:    return "IPA";
        case XX_FILE_TYPE_NPM:    return "NPM";
        case XX_FILE_TYPE_TAR_GZ: return "TAR.GZ";
        case XX_FILE_TYPE_TAR_BZ2:return "TAR.BZ2";
        case XX_FILE_TYPE_TAR_XZ: return "TAR.XZ";
        case XX_FILE_TYPE_TAR_ZSTD: return "TAR.ZST";
        case XX_FILE_TYPE_ZSTD: return "ZSTD";
        case XX_FILE_TYPE_CPIO: return "CPIO";
        case XX_FILE_TYPE_MTREE: return "MTREE";
        case XX_FILE_TYPE_TAR_NEXTSTEP: return "TAR NEXTSTEP";
        case XX_FILE_TYPE_TAR_COMPRESS: return "TAR.Z";
        case XX_FILE_TYPE_UNIX_PACK: return "UNIX PACK";
        case XX_FILE_TYPE_ZLIB: return "ZLIB";
        case XX_FILE_TYPE_UNIX_COMPRESS: return "UNIX COMPRESS";
        case XX_FILE_TYPE_GIT_OBJECT: return "GIT OBJECT";
        case XX_FILE_TYPE_MS_COMPRESS: return "MS COMPRESS";
        case XX_FILE_TYPE_ASH0: return "ASH0";
        case XX_FILE_TYPE_WII_LZ77: return "WII LZ77";
        case XX_FILE_TYPE_LZV1: return "LZV1";
        case XX_FILE_TYPE_ORACLE_SQUEEZE: return "ORACLE SQUEEZE";
        case XX_FILE_TYPE_SOFTRONICS: return "SOFTRONICS";
        case XX_FILE_TYPE_LOGITECH_COMPRESS: return "LOGITECH COMPRESS";
        case XX_FILE_TYPE_DMA_PACKED: return "DMA PACKED";
        case XX_FILE_TYPE_GAS_HUFF: return "GAS HUFF";
        case XX_FILE_TYPE_HUF: return "HUF";
        case XX_FILE_TYPE_LZDIET: return "LZDIET";
        case XX_FILE_TYPE_ZIE: return "ZIE";
        case XX_FILE_TYPE_XEDITPACK: return "XEDITPACK";
        case XX_FILE_TYPE_WPK: return "WPK";
        case XX_FILE_TYPE_WINTERSOFT: return "WINTERSOFT";
        case XX_FILE_TYPE_VMARC: return "VMARC";
        case XX_FILE_TYPE_TIVOLI: return "TIVOLI";
        case XX_FILE_TYPE_TI99ARC: return "TI99ARC";
        case XX_FILE_TYPE_STYLUS: return "STYLUS";
        case XX_FILE_TYPE_RTPATCH: return "RTPATCH";
        case XX_FILE_TYPE_RTA: return "RTA";
        case XX_FILE_TYPE_ROMPAQ: return "ROMPAQ";
        case XX_FILE_TYPE_RID: return "RID";
        case XX_FILE_TYPE_QNXBASE: return "QNXBASE";
        case XX_FILE_TYPE_QDA: return "QDA";
        case XX_FILE_TYPE_PKT: return "PKT";
        case XX_FILE_TYPE_LOFI: return "LOFI";
        case XX_FILE_TYPE_LIM: return "LIM";
        case XX_FILE_TYPE_KOLIBRIKPACK: return "KOLIBRIKPACK";
        case XX_FILE_TYPE_IVT: return "IVT";
        case XX_FILE_TYPE_IRWINPAC: return "IRWINPAC";
        case XX_FILE_TYPE_HAP: return "HAP";
        case XX_FILE_TYPE_COMPACTPRO: return "COMPACTPRO";
        case XX_FILE_TYPE_IMP: return "IMP";
        case XX_FILE_TYPE_SQX: return "SQX";
        case XX_FILE_TYPE_ZOO: return "ZOO";
        case XX_FILE_TYPE_TERSE: return "TERSE";
        case XX_FILE_TYPE_STK: return "STK";
        case XX_FILE_TYPE_PCOMMOS2: return "PCOMMOS2";
        case XX_FILE_TYPE_JBF: return "JBF";
        case XX_FILE_TYPE_IBMSPACK: return "IBMSPACK";
        case XX_FILE_TYPE_GTU: return "GTU";
        case XX_FILE_TYPE_GLU: return "GLU";
        case XX_FILE_TYPE_ZTC: return "ZTC";
        case XX_FILE_TYPE_NETWAREPACKED: return "NETWAREPACKED";
        case XX_FILE_TYPE_ZPAK: return "ZPAK";
        case XX_FILE_TYPE_ZCMP: return "ZCMP";
        case XX_FILE_TYPE_SCL: return "SCL";
        case XX_FILE_TYPE_PAKLEO: return "PAKLEO";
        case XX_FILE_TYPE_NPACK: return "NPACK";
        case XX_FILE_TYPE_MI10: return "MI10";
        case XX_FILE_TYPE_LZWD: return "LZWD";
        case XX_FILE_TYPE_LZHCXP: return "LZHCXP";
        case XX_FILE_TYPE_KBOOM: return "KBOOM";
        case XX_FILE_TYPE_HZL: return "HZL";
        case XX_FILE_TYPE_HA: return "HA";
        case XX_FILE_TYPE_GENIUS: return "GENIUS";
        case XX_FILE_TYPE_FLS: return "FLS";
        case XX_FILE_TYPE_EAREFPACK: return "EAREFPACK";
        case XX_FILE_TYPE_EALIB: return "EALIB";
        case XX_FILE_TYPE_ELM: return "ELM";
        case XX_FILE_TYPE_EA: return "EA";
        case XX_FILE_TYPE_DISKDOUBLER: return "DISKDOUBLER";
        case XX_FILE_TYPE_CMP: return "CMP";
        case XX_FILE_TYPE_CLP: return "CLP";
        case XX_FILE_TYPE_CHIEFLZMULTI: return "CHIEFLZMULTI";
        case XX_FILE_TYPE_CHIEFLZ: return "CHIEFLZ";
        case XX_FILE_TYPE_BWCF: return "BWCF";
        case XX_FILE_TYPE_ASYMETRIX: return "ASYMETRIX";
        case XX_FILE_TYPE_SEAARC: return "SEAARC";
        case XX_FILE_TYPE_AMIGAHUNK: return "AMIGAHUNK";
        case XX_FILE_TYPE_ATARIST: return "ATARIST";
        case XX_FILE_TYPE_DOS16M: return "DOS16M";
        case XX_FILE_TYPE_DOS4G: return "DOS4G";
        case XX_FILE_TYPE_COM: return "COM";
        case XX_FILE_TYPE_AMIGALZX: return "AMIGALZX";
        case XX_FILE_TYPE_SPIS: return "SPIS";
        case XX_FILE_TYPE_LHA: return "LHA";
        case XX_FILE_TYPE_XAR: return "XAR";
        case XX_FILE_TYPE_FMC1: return "FMC1";
        case XX_FILE_TYPE_PYZ: return "PYZ";
        case XX_FILE_TYPE_ASCENDBACKUP: return "ASCENDBACKUP";
        case XX_FILE_TYPE_STORK: return "STORK";
        case XX_FILE_TYPE_ZAP: return "ZAP";
        case XX_FILE_TYPE_BWF: return "BWF";
        case XX_FILE_TYPE_QUALITAS: return "QUALITAS";
        case XX_FILE_TYPE_JETBBS: return "JETBBS";
        case XX_FILE_TYPE_ECMPACKED: return "ECMPACKED";
        case XX_FILE_TYPE_BORLANDPACK: return "BORLANDPACK";
        case XX_FILE_TYPE_JGPAK: return "JGPAK";
        case XX_FILE_TYPE_ZZZ: return "ZZZ";
        case XX_FILE_TYPE_ZZ: return "ZZ";
        case XX_FILE_TYPE_ZLWB: return "ZLWB";
        case XX_FILE_TYPE_TRC: return "TRC";
        case XX_FILE_TYPE_TGCF: return "TGCF";
        case XX_FILE_TYPE_SWAG: return "SWAG";
        case XX_FILE_TYPE_RIVERSOFT: return "RIVERSOFT";
        case XX_FILE_TYPE_RCF: return "RCF";
        case XX_FILE_TYPE_QUARTERDECKQP: return "QUARTERDECKQP";
        case XX_FILE_TYPE_QIP1: return "QIP1";
        case XX_FILE_TYPE_POWERARC: return "POWERARC";
        case XX_FILE_TYPE_POVLABLZH: return "POVLABLZH";
        case XX_FILE_TYPE_MVA: return "MVA";
        case XX_FILE_TYPE_MIZ: return "MIZ";
        case XX_FILE_TYPE_LSZ: return "LSZ";
        case XX_FILE_TYPE_JM93: return "JM93";
        case XX_FILE_TYPE_INTEDUFT: return "INTEDUFT";
        case XX_FILE_TYPE_IGF2: return "IGF2";
        case XX_FILE_TYPE_IGF1: return "IGF1";
        case XX_FILE_TYPE_IBMZPAK: return "IBMZPAK";
        case XX_FILE_TYPE_FLD: return "FLD";
        case XX_FILE_TYPE_FIZ: return "FIZ";
        case XX_FILE_TYPE_DTPACKED: return "DTPACKED";
        case XX_FILE_TYPE_DSL2: return "DSL2";
        case XX_FILE_TYPE_DPK: return "DPK";
        case XX_FILE_TYPE_CFL: return "CFL";
        case XX_FILE_TYPE_TRCPAK: return "TRCPAK";
        case XX_FILE_TYPE_SWAGPACKET: return "SWAGPACKET";
        case XX_FILE_TYPE_SW: return "SW";
        case XX_FILE_TYPE_SOS: return "SOS";
        case XX_FILE_TYPE_SECONDNATURE: return "SECONDNATURE";
        case XX_FILE_TYPE_SEADATA: return "SEADATA";
        case XX_FILE_TYPE_SCI: return "SCI";
        case XX_FILE_TYPE_POWERBOARDBBS: return "POWERBOARDBBS";
        case XX_FILE_TYPE_MINIDUMP: return "MINIDUMP";
        case XX_FILE_TYPE_LBRCOBOL: return "LBRCOBOL";
        case XX_FILE_TYPE_KRML: return "KRML";
        case XX_FILE_TYPE_JAM: return "JAM";
        case XX_FILE_TYPE_IRIXSA: return "IRIXSA";
        case XX_FILE_TYPE_HLB: return "HLB";
        case XX_FILE_TYPE_FRONTPAGETHEME: return "FRONTPAGETHEME";
        case XX_FILE_TYPE_CRU: return "CRU";
        case XX_FILE_TYPE_BIGAF: return "BIGAF";
        case XX_FILE_TYPE_TWS: return "TWS";
        case XX_FILE_TYPE_PACKIT: return "PACKIT";
        case XX_FILE_TYPE_ZFSF: return "ZFSF";
        case XX_FILE_TYPE_MARC: return "MARC";
        case XX_FILE_TYPE_BIGF: return "BIGF";
        case XX_FILE_TYPE_ASCEND: return "ASCEND";
        case XX_FILE_TYPE_ASAR: return "ASAR";
        case XX_FILE_TYPE_ARQ: return "ARQ";
        case XX_FILE_TYPE_AP4: return "AP4";
        case XX_FILE_TYPE_FREEARC: return "FREEARC";
        case XX_FILE_TYPE_ZPAQ: return "ZPAQ";
        case XX_FILE_TYPE_PEA: return "PEA";
        case XX_FILE_TYPE_LPAQ8: return "LPAQ8";
        case XX_FILE_TYPE_BCM: return "BCM";
        case XX_FILE_TYPE_TAR_LZIP: return "TAR.LZ";
        case XX_FILE_TYPE_LZIP: return "LZIP";
        case XX_FILE_TYPE_TAR_LZMA: return "TAR.LZMA";
        case XX_FILE_TYPE_LZMA: return "LZMA";
        case XX_FILE_TYPE_TAR_LZOP: return "TAR.LZO";
        case XX_FILE_TYPE_TARX1: return "TARX1";
        case XX_FILE_TYPE_TARX2: return "TARX2";
        case XX_FILE_TYPE_ACE: return "ACE";
        case XX_FILE_TYPE_SFX_NULLSOFT_PIMP: return "Nullsoft PiMP";
        case XX_FILE_TYPE_SFX_SYDEX_DISKETTE_IMAGE: return "Sydex diskette image";
        case XX_FILE_TYPE_SFX_COMPAQ_SOFTPAQ: return "Compaq SoftPaq v1";
        case XX_FILE_TYPE_SFX_WASP_WINDOWS_AUTO: return "WASP installer";
        case XX_FILE_TYPE_WISE_INSTALLATION_SYSTEM: return "Wise Installation System";
        case XX_FILE_TYPE_ESCHALON_SETUP_EPSF: return "Eschalon EPSF";
        case XX_FILE_TYPE_GENTEE_INSTALLER: return "Gentee installer";
        case XX_FILE_TYPE_CLICKTEAM_INSTALL_CREATOR: return "Clickteam Install Creator";
        case XX_FILE_TYPE_CREATEINSTALL_INSTCRIN_EXTRACTOR: return "CreateInstall";
        case XX_FILE_TYPE_SFXSTART: return "SFXSTART";
        case XX_FILE_TYPE_MODBUS_TCP: return "modbus tcp";
        case XX_FILE_TYPE_SOMEIP_MESSAGE: return "someip message";
        case XX_FILE_TYPE_DDS_RTPS: return "dds rtps";
        case XX_FILE_TYPE_RIP_MESSAGE: return "rip message";
        case XX_FILE_TYPE_VRRP_MESSAGE: return "vrrp message";
        case XX_FILE_TYPE_IGMP_MESSAGE: return "igmp message";
        case XX_FILE_TYPE_PIM_MESSAGE: return "pim message";
        case XX_FILE_TYPE_LDP_MESSAGE: return "ldp message";
        case XX_FILE_TYPE_GRE_PACKET: return "gre packet";
        case XX_FILE_TYPE_L2TP_PACKET: return "l2tp packet";
        case XX_FILE_TYPE_LLDP_MESSAGE: return "lldp message";
        case XX_FILE_TYPE_NETFLOW_DATAGRAM: return "netflow datagram";
        case XX_FILE_TYPE_NTLM_MESSAGE: return "ntlm message";
        case XX_FILE_TYPE_DCERPC_PDU: return "dcerpc pdu";
        case XX_FILE_TYPE_ETHEREUM_RLP: return "ethereum rlp";
        case XX_FILE_TYPE_IMAGEMAGICK_MIFF: return "imagemagick miff";
        case XX_FILE_TYPE_AVS_IMAGE: return "avs image";
        case XX_FILE_TYPE_SCANALYTICS_IPLAB: return "scanalytics iplab";
        case XX_FILE_TYPE_MTV_IMAGE: return "mtv image";
        case XX_FILE_TYPE_NOKIA_OTA_BITMAP: return "nokia ota bitmap";
        case XX_FILE_TYPE_APPLE_PICT: return "apple pict";
        case XX_FILE_TYPE_WORDPERFECT_WPG: return "wordperfect wpg";
        case XX_FILE_TYPE_NASA_VICAR: return "nasa vicar";
        case XX_FILE_TYPE_KHOROS_VIFF: return "khoros viff";
        case XX_FILE_TYPE_IMAGEMAGICK_MVG: return "imagemagick mvg";
        case XX_FILE_TYPE_MOTIF_UIL: return "motif uil";
        case XX_FILE_TYPE_IGES_MODEL: return "iges model";
        case XX_FILE_TYPE_OPENUSD_USDA: return "openusd usda";
        case XX_FILE_TYPE_UFO_GLIF: return "ufo glif";
        case XX_FILE_TYPE_UNIFONT_HEX: return "unifont hex";
        case XX_FILE_TYPE_ADLIB_SOP: return "adlib sop";
        case XX_FILE_TYPE_CUDFM_CFF: return "cudfm cff";
        case XX_FILE_TYPE_ADLIB_JBM: return "adlib jbm";
        case XX_FILE_TYPE_CERES_MSC: return "ceres msc";
        case XX_FILE_TYPE_ADLIB_XSM: return "adlib xsm";
        case XX_FILE_TYPE_KEN_KSM: return "ken ksm";
        case XX_FILE_TYPE_IMPLAY_MUSIC: return "implay music";
        case XX_FILE_TYPE_ADLIB_MTR: return "adlib mtr";
        case XX_FILE_TYPE_RDOS_RAW: return "rdos raw";
        case XX_FILE_TYPE_MAD_TRACKER: return "mad tracker";
        case XX_FILE_TYPE_VASP_POSCAR: return "vasp poscar";
        case XX_FILE_TYPE_QUANTUM_ESPRESSO_INPUT: return "quantum espresso input";
        case XX_FILE_TYPE_CP2K_INPUT: return "cp2k input";
        case XX_FILE_TYPE_NWCHEM_INPUT: return "nwchem input";
        case XX_FILE_TYPE_GAMESS_INPUT: return "gamess input";
        case XX_FILE_TYPE_GAUSSIAN_INPUT: return "gaussian input";
        case XX_FILE_TYPE_ABINIT_INPUT: return "abinit input";
        case XX_FILE_TYPE_AIMS_GEOMETRY: return "aims geometry";
        case XX_FILE_TYPE_ORCA_INPUT: return "orca input";
        case XX_FILE_TYPE_DEMON_INPUT: return "demon input";
        case XX_FILE_TYPE_ETHERNET_FRAME: return "ethernet frame";
        case XX_FILE_TYPE_IP_PACKET: return "ip packet";
        case XX_FILE_TYPE_ARP_PACKET: return "arp packet";
        case XX_FILE_TYPE_ICMP_MESSAGE: return "icmp message";
        case XX_FILE_TYPE_SIP_MESSAGE: return "sip message";
        case XX_FILE_TYPE_RTSP_MESSAGE: return "rtsp message";
        case XX_FILE_TYPE_DIAMETER_MESSAGE: return "diameter message";
        case XX_FILE_TYPE_TACACS_PACKET: return "tacacs packet";
        case XX_FILE_TYPE_GTP_MESSAGE: return "gtp message";
        case XX_FILE_TYPE_PFCP_MESSAGE: return "pfcp message";
        case XX_FILE_TYPE_PPTP_MESSAGE: return "pptp message";
        case XX_FILE_TYPE_RSVP_MESSAGE: return "rsvp message";
        case XX_FILE_TYPE_AGE_ENCRYPTED: return "age encrypted";
        case XX_FILE_TYPE_KERBEROS_CCACHE: return "kerberos ccache";
        case XX_FILE_TYPE_JOSE_JWS: return "jose jws";
        case XX_FILE_TYPE_WBMP_IMAGE: return "wbmp image";
        case XX_FILE_TYPE_DEC_SIXEL: return "dec sixel";
        case XX_FILE_TYPE_PALM_BITMAP: return "palm bitmap";
        case XX_FILE_TYPE_ADOBE_ACV: return "adobe acv";
        case XX_FILE_TYPE_ADOBE_ACT: return "adobe act";
        case XX_FILE_TYPE_OGRE_SKELETON: return "ogre skeleton";
        case XX_FILE_TYPE_CAL3D_SKELETON: return "cal3d skeleton";
        case XX_FILE_TYPE_COLLADA_DAE: return "collada dae";
        case XX_FILE_TYPE_LIGHTWAVE_SCENE: return "lightwave scene";
        case XX_FILE_TYPE_DSN6_DENSITY: return "dsn6 density";
        case XX_FILE_TYPE_CRYSTALLOGRAPHY_MTZ: return "crystallography mtz";
        case XX_FILE_TYPE_AMIRA_MESH: return "amira mesh";
        case XX_FILE_TYPE_TETGEN_MESH: return "tetgen mesh";
        case XX_FILE_TYPE_JEDEC_FUSE: return "jedec fuse";
        case XX_FILE_TYPE_QCHEM_INPUT: return "qchem input";
        case XX_FILE_TYPE_ADLIB_BAM: return "adlib bam";
        case XX_FILE_TYPE_ADLIB_BMF: return "adlib bmf";
        case XX_FILE_TYPE_CREATIVE_CMF: return "creative cmf";
        case XX_FILE_TYPE_ADLIB_DFM: return "adlib dfm";
        case XX_FILE_TYPE_ADLIB_LDS: return "adlib lds";
        case XX_FILE_TYPE_ADLIB_MKJ: return "adlib mkj";
        case XX_FILE_TYPE_ADLIB_ROL: return "adlib rol";
        case XX_FILE_TYPE_ADLIB_SA2: return "adlib sa2";
        case XX_FILE_TYPE_FAUST_FMC: return "faust fmc";
        case XX_FILE_TYPE_SOFTSTAR_RIX: return "softstar rix";
        case XX_FILE_TYPE_GENOMICS_BED: return "genomics bed";
        case XX_FILE_TYPE_GENOMICS_WIGGLE: return "genomics wiggle";
        case XX_FILE_TYPE_GENOMICS_GTF: return "genomics gtf";
        case XX_FILE_TYPE_GENOMICS_AGP: return "genomics agp";
        case XX_FILE_TYPE_SEQUENCING_ABIF: return "sequencing abif";
        case XX_FILE_TYPE_SEQUENCING_SCF: return "sequencing scf";
        case XX_FILE_TYPE_GENOMICS_SFF: return "genomics sff";
        case XX_FILE_TYPE_LUT_SPI1D: return "lut spi1d";
        case XX_FILE_TYPE_LUT_SPI3D: return "lut spi3d";
        case XX_FILE_TYPE_LUT_CINESPACE_CSP: return "lut cinespace csp";
        case XX_FILE_TYPE_NTP_MESSAGE: return "ntp message";
        case XX_FILE_TYPE_RTP_RTCP: return "rtp rtcp";
        case XX_FILE_TYPE_BGP_MESSAGES: return "bgp messages";
        case XX_FILE_TYPE_OSPF_PACKET: return "ospf packet";
        case XX_FILE_TYPE_SCTP_PACKET: return "sctp packet";
        case XX_FILE_TYPE_ISAKMP_MESSAGE: return "isakmp message";
        case XX_FILE_TYPE_SSH_TRANSPORT: return "ssh transport";
        case XX_FILE_TYPE_SMTP_TRANSCRIPT: return "smtp transcript";
        case XX_FILE_TYPE_PKCS8_PRIVATE_KEY: return "pkcs8 private key";
        case XX_FILE_TYPE_PUTTY_PPK: return "putty ppk";
        case XX_FILE_TYPE_OPENSSH_CERTIFICATE: return "openssh certificate";
        case XX_FILE_TYPE_SAFETENSORS: return "safetensors";
        case XX_FILE_TYPE_GGUF: return "gguf";
        case XX_FILE_TYPE_CDB_DATABASE: return "cdb database";
        case XX_FILE_TYPE_STOMP_FRAMES: return "stomp frames";
        case XX_FILE_TYPE_FONTFORGE_SFD: return "fontforge sfd";
        case XX_FILE_TYPE_GRUB_PFF2: return "grub pff2";
        case XX_FILE_TYPE_OPENGEX_MODEL: return "opengex model";
        case XX_FILE_TYPE_BVH_MOTION: return "bvh motion";
        case XX_FILE_TYPE_DIRECTX_X: return "directx x";
        case XX_FILE_TYPE_GTS_SURFACE: return "gts surface";
        case XX_FILE_TYPE_MEDIT_MESH: return "medit mesh";
        case XX_FILE_TYPE_GOCAD_MODEL: return "gocad model";
        case XX_FILE_TYPE_NASTRAN_BULK: return "nastran bulk";
        case XX_FILE_TYPE_ABAQUS_INPUT: return "abaqus input";
        case XX_FILE_TYPE_ENSIGHT_GOLD_GEOMETRY: return "ensight gold geometry";
        case XX_FILE_TYPE_GMV_MESH: return "gmv mesh";
        case XX_FILE_TYPE_USGS_DEM: return "usgs dem";
        case XX_FILE_TYPE_DTED_ELEVATION: return "dted elevation";
        case XX_FILE_TYPE_MAPINFO_MIF: return "mapinfo mif";
        case XX_FILE_TYPE_TRACKER_COCONIZER: return "tracker coconizer";
        case XX_FILE_TYPE_TRACKER_REAL: return "tracker real";
        case XX_FILE_TYPE_TRACKER_MEGATRACKER: return "tracker megatracker";
        case XX_FILE_TYPE_AMOS_MUSIC_BANK: return "amos music bank";
        case XX_FILE_TYPE_ADLIB_RAD: return "adlib rad";
        case XX_FILE_TYPE_ADLIB_AMD: return "adlib amd";
        case XX_FILE_TYPE_ADLIB_HSC: return "adlib hsc";
        case XX_FILE_TYPE_ADLIB_D00: return "adlib d00";
        case XX_FILE_TYPE_ADLIB_BNK: return "adlib bnk";
        case XX_FILE_TYPE_DOSBOX_DRO: return "dosbox dro";
        case XX_FILE_TYPE_GENOMICS_GENBANK: return "genomics genbank";
        case XX_FILE_TYPE_GENOMICS_EMBL: return "genomics embl";
        case XX_FILE_TYPE_GENOMICS_SWISSPROT: return "genomics swissprot";
        case XX_FILE_TYPE_ALIGNMENT_CLUSTAL: return "alignment clustal";
        case XX_FILE_TYPE_ALIGNMENT_STOCKHOLM: return "alignment stockholm";
        case XX_FILE_TYPE_ALIGNMENT_PHYLIP: return "alignment phylip";
        case XX_FILE_TYPE_ALIGNMENT_MAF: return "alignment maf";
        case XX_FILE_TYPE_ALIGNMENT_MAUVE: return "alignment mauve";
        case XX_FILE_TYPE_UCSC_NIB: return "ucsc nib";
        case XX_FILE_TYPE_ASSEMBLY_GFA: return "assembly gfa";
        case XX_FILE_TYPE_HTTP1_MESSAGE: return "http1 message";
        case XX_FILE_TYPE_WEBSOCKET_FRAMES: return "websocket frames";
        case XX_FILE_TYPE_COAP_MESSAGE: return "coap message";
        case XX_FILE_TYPE_STUN_MESSAGE: return "stun message";
        case XX_FILE_TYPE_DHCP_MESSAGE: return "dhcp message";
        case XX_FILE_TYPE_RADIUS_PACKET: return "radius packet";
        case XX_FILE_TYPE_SNMP_MESSAGE: return "snmp message";
        case XX_FILE_TYPE_LDAP_MESSAGE: return "ldap message";
        case XX_FILE_TYPE_TLS_RECORDS: return "tls records";
        case XX_FILE_TYPE_JKS_KEYSTORE: return "jks keystore";
        case XX_FILE_TYPE_JAVA_SERIALIZATION: return "java serialization";
        case XX_FILE_TYPE_X509_CRL: return "x509 crl";
        case XX_FILE_TYPE_OCSP_RESPONSE: return "ocsp response";
        case XX_FILE_TYPE_LMDB_DATA: return "lmdb data";
        case XX_FILE_TYPE_GDBM_DUMP: return "gdbm dump";
        case XX_FILE_TYPE_ADOBE_ACB: return "adobe acb";
        case XX_FILE_TYPE_JASC_PALETTE: return "jasc palette";
        case XX_FILE_TYPE_X11_XBM: return "x11 xbm";
        case XX_FILE_TYPE_JPEG2000_PGX: return "jpeg2000 pgx";
        case XX_FILE_TYPE_AMIGA_DISKOBJECT: return "amiga diskobject";
        case XX_FILE_TYPE_TEX_VF: return "tex vf";
        case XX_FILE_TYPE_ESRI_ASCII_GRID: return "esri ascii grid";
        case XX_FILE_TYPE_SURFER_GRID: return "surfer grid";
        case XX_FILE_TYPE_GXF_GRID: return "gxf grid";
        case XX_FILE_TYPE_OGC_WKT: return "ogc wkt";
        case XX_FILE_TYPE_STEP_PART21: return "step part21";
        case XX_FILE_TYPE_GERBER_RS274X: return "gerber rs274x";
        case XX_FILE_TYPE_EXCELLON_DRILL: return "excellon drill";
        case XX_FILE_TYPE_VRML_SCENE: return "vrml scene";
        case XX_FILE_TYPE_RENDERMAN_RIB: return "renderman rib";
        case XX_FILE_TYPE_ASYLUM_AMF: return "asylum amf";
        case XX_FILE_TYPE_TRACKER_STX: return "tracker stx";
        case XX_FILE_TYPE_TRACKER_DTM: return "tracker dtm";
        case XX_FILE_TYPE_TRACKER_SOUNDFX: return "tracker soundfx";
        case XX_FILE_TYPE_TRACKER_FUNK: return "tracker funk";
        case XX_FILE_TYPE_TRACKER_ARCHIMEDES: return "tracker archimedes";
        case XX_FILE_TYPE_PCE_PSI: return "pce psi";
        case XX_FILE_TYPE_PC98_D88: return "pc98 d88";
        case XX_FILE_TYPE_HXC_MFM: return "hxc mfm";
        case XX_FILE_TYPE_YAZE_YDSK: return "yaze ydsk";
        case XX_FILE_TYPE_LAMMPS_DATA: return "lammps data";
        case XX_FILE_TYPE_LAMMPS_DUMP: return "lammps dump";
        case XX_FILE_TYPE_SHELX_RES: return "shelx res";
        case XX_FILE_TYPE_TURBOMOLE_COORD: return "turbomole coord";
        case XX_FILE_TYPE_CHARMM_CRD: return "charmm crd";
        case XX_FILE_TYPE_CASTEP_CELL: return "castep cell";
        case XX_FILE_TYPE_CRYSTAL_FORT34: return "crystal fort34";
        case XX_FILE_TYPE_SIESTA_XV: return "siesta xv";
        case XX_FILE_TYPE_HARWELL_BOEING: return "harwell boeing";
        case XX_FILE_TYPE_OPENFOAM_POINTS: return "openfoam points";
        case XX_FILE_TYPE_MINECRAFT_NBT: return "minecraft nbt";
        case XX_FILE_TYPE_AMAZON_ION_BINARY: return "amazon ion binary";
        case XX_FILE_TYPE_LEVELDB_LOG: return "leveldb log";
        case XX_FILE_TYPE_DNS_MESSAGE: return "dns message";
        case XX_FILE_TYPE_ROCKSDB_BLOB: return "rocksdb blob";
        case XX_FILE_TYPE_MONGODB_WIRE: return "mongodb wire";
        case XX_FILE_TYPE_REDIS_RESP: return "redis resp";
        case XX_FILE_TYPE_MQTT_PACKETS: return "mqtt packets";
        case XX_FILE_TYPE_AMQP_FRAMES: return "amqp frames";
        case XX_FILE_TYPE_THRIFT_COMPACT: return "thrift compact";
        case XX_FILE_TYPE_X509_CERTIFICATE: return "x509 certificate";
        case XX_FILE_TYPE_PKCS10_CSR: return "pkcs10 csr";
        case XX_FILE_TYPE_PKCS12_PFX: return "pkcs12 pfx";
        case XX_FILE_TYPE_OPENSSH_PRIVATE_KEY: return "openssh private key";
        case XX_FILE_TYPE_KERBEROS_KEYTAB: return "kerberos keytab";
        case XX_FILE_TYPE_GIMP_GPL: return "gimp gpl";
        case XX_FILE_TYPE_GIMP_GGR: return "gimp ggr";
        case XX_FILE_TYPE_IRIDAS_CUBE_LUT: return "iridas cube lut";
        case XX_FILE_TYPE_HPGL_PLOT: return "hpgl plot";
        case XX_FILE_TYPE_PAINTSHOP_PSP: return "paintshop psp";
        case XX_FILE_TYPE_PHOTOSHOP_PAT: return "photoshop pat";
        case XX_FILE_TYPE_MMD_PMX: return "mmd pmx";
        case XX_FILE_TYPE_METASEQUOIA_MQO: return "metasequoia mqo";
        case XX_FILE_TYPE_CALMA_GDSII: return "calma gdsii";
        case XX_FILE_TYPE_AUTODESK_ASE: return "autodesk ase";
        case XX_FILE_TYPE_FREESURFER_SURFACE: return "freesurfer surface";
        case XX_FILE_TYPE_GMSH_MSH: return "gmsh msh";
        case XX_FILE_TYPE_NETGEN_VOL: return "netgen vol";
        case XX_FILE_TYPE_FONT_AFM: return "font afm";
        case XX_FILE_TYPE_TILED_TMX: return "tiled tmx";
        case XX_FILE_TYPE_NINTENDO_SDAT: return "nintendo sdat";
        case XX_FILE_TYPE_SONY_VAB: return "sony vab";
        case XX_FILE_TYPE_YAMAHA_YM: return "yamaha ym";
        case XX_FILE_TYPE_ZX_AYEMUL: return "zx ayemul";
        case XX_FILE_TYPE_DRAGON_VDK: return "dragon vdk";
        case XX_FILE_TYPE_APPLE_A2R: return "apple a2r";
        case XX_FILE_TYPE_ATARI_ATR: return "atari atr";
        case XX_FILE_TYPE_ATARI_PASTI_STX: return "atari pasti stx";
        case XX_FILE_TYPE_AMIGA_IPF: return "amiga ipf";
        case XX_FILE_TYPE_TRACKER_DTT: return "tracker dtt";
        case XX_FILE_TYPE_GAUSSIAN_CUBE: return "gaussian cube";
        case XX_FILE_TYPE_MOLECULE_XYZ: return "molecule xyz";
        case XX_FILE_TYPE_MDL_MOLFILE: return "mdl molfile";
        case XX_FILE_TYPE_TRIPOS_MOL2: return "tripos mol2";
        case XX_FILE_TYPE_XCRYSDEN_XSF: return "xcrysden xsf";
        case XX_FILE_TYPE_AMBER_PRMTOP: return "amber prmtop";
        case XX_FILE_TYPE_AMBER_RESTART: return "amber restart";
        case XX_FILE_TYPE_GAUSSIAN_FCHK: return "gaussian fchk";
        case XX_FILE_TYPE_JCAMP_DX: return "jcamp dx";
        case XX_FILE_TYPE_DL_POLY_CONFIG: return "dl poly config";
        case XX_FILE_TYPE_NIX_NAR: return "nix nar";
        case XX_FILE_TYPE_REDIS_RDB: return "redis rdb";
        case XX_FILE_TYPE_POSTGRES_CUSTOM: return "postgres custom";
        case XX_FILE_TYPE_MYSQL_BINLOG: return "mysql binlog";
        case XX_FILE_TYPE_KAFKA_RECORD_BATCH: return "kafka record batch";
        case XX_FILE_TYPE_ANDROID_BINARY_XML: return "android binary xml";
        case XX_FILE_TYPE_ANDROID_RESOURCES_ARSC: return "android resources arsc";
        case XX_FILE_TYPE_MSGPACK: return "msgpack";
        case XX_FILE_TYPE_UBJSON: return "ubjson";
        case XX_FILE_TYPE_BITTORRENT_METAINFO: return "bittorrent metainfo";
        case XX_FILE_TYPE_ERLANG_EXTERNAL_TERM: return "erlang external term";
        case XX_FILE_TYPE_CAPNPROTO_MESSAGE: return "capnproto message";
        case XX_FILE_TYPE_DBUS_MESSAGE: return "dbus message";
        case XX_FILE_TYPE_WINDOWS_SHELL_LINK: return "windows shell link";
        case XX_FILE_TYPE_PKCS7_CMS: return "pkcs7 cms";
        case XX_FILE_TYPE_WAVEFRONT_OBJ: return "wavefront obj";
        case XX_FILE_TYPE_OFF_MESH: return "off mesh";
        case XX_FILE_TYPE_AC3D_MODEL: return "ac3d model";
        case XX_FILE_TYPE_QUBICLE_QB: return "qubicle qb";
        case XX_FILE_TYPE_TERRAGEN_TER: return "terragen ter";
        case XX_FILE_TYPE_GIMP_XCF: return "gimp xcf";
        case XX_FILE_TYPE_PHOTOSHOP_ABR: return "photoshop abr";
        case XX_FILE_TYPE_SOFTIMAGE_PIC: return "softimage pic";
        case XX_FILE_TYPE_ALIAS_PIX: return "alias pix";
        case XX_FILE_TYPE_QT_QPICTURE: return "qt qpicture";
        case XX_FILE_TYPE_FONT_TYPE1_PFB: return "font type1 pfb";
        case XX_FILE_TYPE_FONT_GEM_FNT: return "font gem fnt";
        case XX_FILE_TYPE_TEX_GF: return "tex gf";
        case XX_FILE_TYPE_BPG_IMAGE: return "bpg image";
        case XX_FILE_TYPE_MNG_ANIMATION: return "mng animation";
        case XX_FILE_TYPE_ATARI_7800_A78: return "atari 7800 a78";
        case XX_FILE_TYPE_COMMODORE_PC64: return "commodore pc64";
        case XX_FILE_TYPE_ATARI_CAS: return "atari cas";
        case XX_FILE_TYPE_MSX_CAS: return "msx cas";
        case XX_FILE_TYPE_ORIC_TAP: return "oric tap";
        case XX_FILE_TYPE_DRAGON_CAS: return "dragon cas";
        case XX_FILE_TYPE_AMIGA_AHX: return "amiga ahx";
        case XX_FILE_TYPE_AMSTRAD_CPC_SNA: return "amstrad cpc sna";
        case XX_FILE_TYPE_VTECH_VZ: return "vtech vz";
        case XX_FILE_TYPE_ZX_HOBETA: return "zx hobeta";
        case XX_FILE_TYPE_GENOMICS_FASTA: return "genomics fasta";
        case XX_FILE_TYPE_GENOMICS_FASTQ: return "genomics fastq";
        case XX_FILE_TYPE_GENOMICS_SAM: return "genomics sam";
        case XX_FILE_TYPE_OPENDX_FIELD: return "opendx field";
        case XX_FILE_TYPE_GENOMICS_VCF: return "genomics vcf";
        case XX_FILE_TYPE_GENOMICS_GFF3: return "genomics gff3";
        case XX_FILE_TYPE_PROTEIN_PDB: return "protein pdb";
        case XX_FILE_TYPE_PROTEIN_MMCIF: return "protein mmcif";
        case XX_FILE_TYPE_MATRIX_MARKET: return "matrix market";
        case XX_FILE_TYPE_GROMACS_GRO: return "gromacs gro";
        case XX_FILE_TYPE_MICROSOFT_MSF: return "microsoft msf";
        case XX_FILE_TYPE_WINDOWS_REGISTRY_HIVE: return "windows registry hive";
        case XX_FILE_TYPE_WINDOWS_EVTX: return "windows evtx";
        case XX_FILE_TYPE_BINARY_PLIST: return "binary plist";
        case XX_FILE_TYPE_MONGODB_BSON: return "mongodb bson";
        case XX_FILE_TYPE_CBOR: return "cbor";
        case XX_FILE_TYPE_OPENZIM: return "openzim";
        case XX_FILE_TYPE_APACHE_ORC: return "apache orc";
        case XX_FILE_TYPE_HADOOP_SEQUENCEFILE: return "hadoop sequencefile";
        case XX_FILE_TYPE_LEVELDB_SSTABLE: return "leveldb sstable";
        case XX_FILE_TYPE_SNAPPY_FRAMED: return "snappy framed";
        case XX_FILE_TYPE_LZF_STREAM: return "lzf stream";
        case XX_FILE_TYPE_FASTLZ_SIXPACK: return "fastlz sixpack";
        case XX_FILE_TYPE_LINUX_BTF: return "linux btf";
        case XX_FILE_TYPE_FLATGEOBUF: return "flatgeobuf";
        case XX_FILE_TYPE_ASTC_TEXTURE: return "astc texture";
        case XX_FILE_TYPE_PKM_TEXTURE: return "pkm texture";
        case XX_FILE_TYPE_BASIS_TEXTURE: return "basis texture";
        case XX_FILE_TYPE_OPENCTM_MESH: return "openctm mesh";
        case XX_FILE_TYPE_FONT_BDF: return "font bdf";
        case XX_FILE_TYPE_FONT_PCF: return "font pcf";
        case XX_FILE_TYPE_FONT_PSF: return "font psf";
        case XX_FILE_TYPE_FONT_WINDOWS_FNT: return "font windows fnt";
        case XX_FILE_TYPE_TEX_TFM: return "tex tfm";
        case XX_FILE_TYPE_TEX_PK: return "tex pk";
        case XX_FILE_TYPE_TEX_DVI: return "tex dvi";
        case XX_FILE_TYPE_NETPBM_PFM: return "netpbm pfm";
        case XX_FILE_TYPE_STEINBERG_VST3PRESET: return "steinberg vst3preset";
        case XX_FILE_TYPE_FONT_BMFONT: return "font bmfont";
        case XX_FILE_TYPE_PROCESSING_VLW: return "processing vlw";
        case XX_FILE_TYPE_SNES_SPC: return "snes spc";
        case XX_FILE_TYPE_GAMEBOY_GBS: return "gameboy gbs";
        case XX_FILE_TYPE_SEGA_SGC: return "sega sgc";
        case XX_FILE_TYPE_S98_LOG: return "s98 log";
        case XX_FILE_TYPE_ATARI_SAP: return "atari sap";
        case XX_FILE_TYPE_SC68_MUSIC: return "sc68 music";
        case XX_FILE_TYPE_ZX_SPECTRUM_PZX: return "zx spectrum pzx";
        case XX_FILE_TYPE_ACORN_UEF: return "acorn uef";
        case XX_FILE_TYPE_NINTENDO_UNIF: return "nintendo unif";
        case XX_FILE_TYPE_NINTENDO_FDS: return "nintendo fds";
        case XX_FILE_TYPE_UCSC_BIGWIG: return "ucsc bigwig";
        case XX_FILE_TYPE_UCSC_BIGBED: return "ucsc bigbed";
        case XX_FILE_TYPE_PHYLO_NEXUS: return "phylo nexus";
        case XX_FILE_TYPE_PHYLO_NEWICK: return "phylo newick";
        case XX_FILE_TYPE_SQLITE_ROLLBACK_JOURNAL: return "sqlite rollback journal";
        case XX_FILE_TYPE_NEUROSCAN_CNT: return "neuroscan cnt";
        case XX_FILE_TYPE_AXONA_TETRODE: return "axona tetrode";
        case XX_FILE_TYPE_PYTHON_PICKLE: return "python pickle";
        case XX_FILE_TYPE_INIVATION_AEDAT: return "inivation aedat";
        case XX_FILE_TYPE_PYTHON_MARSHAL: return "python marshal";
        case XX_FILE_TYPE_VICE_X64: return "vice x64";
        case XX_FILE_TYPE_VICE_SNAPSHOT: return "vice snapshot";
        case XX_FILE_TYPE_COMMODORE_G64: return "commodore g64";
        case XX_FILE_TYPE_COMMODORE_P64: return "commodore p64";
        case XX_FILE_TYPE_COMMODORE_TAP: return "commodore tap";
        case XX_FILE_TYPE_ZX_SPECTRUM_TZX: return "zx spectrum tzx";
        case XX_FILE_TYPE_ZX_SPECTRUM_SZX: return "zx spectrum szx";
        case XX_FILE_TYPE_AMSTRAD_CPC_DSK: return "amstrad cpc dsk";
        case XX_FILE_TYPE_ATARI_ST_MSA: return "atari st msa";
        case XX_FILE_TYPE_SUPERCARD_SCP: return "supercard scp";
        case XX_FILE_TYPE_APPLE_WOZ: return "apple woz";
        case XX_FILE_TYPE_NINTENDO_NSF: return "nintendo nsf";
        case XX_FILE_TYPE_VGM_LOG: return "vgm log";
        case XX_FILE_TYPE_PSID_SID: return "psid sid";
        case XX_FILE_TYPE_HES_SOUND: return "hes sound";
        case XX_FILE_TYPE_AUDIO_DOLBY_AC3: return "audio dolby ac3";
        case XX_FILE_TYPE_AUDIO_MPEG_MP3: return "audio mpeg mp3";
        case XX_FILE_TYPE_AUDIO_AAC_ADTS: return "audio aac adts";
        case XX_FILE_TYPE_AUDIO_MONKEYS_APE: return "audio monkeys ape";
        case XX_FILE_TYPE_MPEG_TRANSPORT_STREAM: return "mpeg transport stream";
        case XX_FILE_TYPE_MPEG_PROGRAM_STREAM: return "mpeg program stream";
        case XX_FILE_TYPE_REALMEDIA_RM: return "realmedia rm";
        case XX_FILE_TYPE_IDTECH_ROQ: return "idtech roq";
        case XX_FILE_TYPE_RAD_BINK: return "rad bink";
        case XX_FILE_TYPE_RAD_SMACKER: return "rad smacker";
        case XX_FILE_TYPE_INTERPLAY_MVE: return "interplay mve";
        case XX_FILE_TYPE_WESTWOOD_VQA: return "westwood vqa";
        case XX_FILE_TYPE_AUTODESK_FLIC: return "autodesk flic";
        case XX_FILE_TYPE_IDTECH_MD5ANIM: return "idtech md5anim";
        case XX_FILE_TYPE_STEREOLITHOGRAPHY_STL: return "stereolithography stl";
        case XX_FILE_TYPE_GARMIN_FIT: return "garmin fit";
        case XX_FILE_TYPE_ROSBAG1: return "rosbag1";
        case XX_FILE_TYPE_MCAP: return "mcap";
        case XX_FILE_TYPE_SEISMIC_SAC: return "seismic sac";
        case XX_FILE_TYPE_SEISMIC_SEG2: return "seismic seg2";
        case XX_FILE_TYPE_UCSC_TWOBIT: return "ucsc twobit";
        case XX_FILE_TYPE_GENOMICS_BGEN: return "genomics bgen";
        case XX_FILE_TYPE_OPENEPHYS_CONTINUOUS: return "openephys continuous";
        case XX_FILE_TYPE_MOUNTAINSORT_MDA: return "mountainsort mda";
        case XX_FILE_TYPE_IGOR_IBW: return "igor ibw";
        case XX_FILE_TYPE_PRINCETON_SPE: return "princeton spe";
        case XX_FILE_TYPE_MICROSCOPY_SPIDER: return "microscopy spider";
        case XX_FILE_TYPE_WMO_GRIB: return "wmo grib";
        case XX_FILE_TYPE_WMO_BUFR: return "wmo bufr";
        case XX_FILE_TYPE_AUTOCAD_DXF: return "autocad dxf";
        case XX_FILE_TYPE_BLACKROCK_NSX: return "blackrock nsx";
        case XX_FILE_TYPE_BLACKROCK_NEV: return "blackrock nev";
        case XX_FILE_TYPE_LECROY_TRC: return "lecroy trc";
        case XX_FILE_TYPE_TEKTRONIX_ISF: return "tektronix isf";
        case XX_FILE_TYPE_IRCAM_SDIF: return "ircam sdif";
        case XX_FILE_TYPE_TRACKER_LIQUID: return "tracker dsm";
        case XX_FILE_TYPE_TRACKER_DMF: return "tracker dmf";
        case XX_FILE_TYPE_TRACKER_PTM: return "tracker ptm";
        case XX_FILE_TYPE_TRACKER_AMS: return "tracker ams";
        case XX_FILE_TYPE_TRACKER_DIGI: return "tracker digi";
        case XX_FILE_TYPE_TRACKER_EMOD: return "tracker emod";
        case XX_FILE_TYPE_TRACKER_MT2: return "tracker mt2";
        case XX_FILE_TYPE_AUDIO_DSF: return "audio dsf";
        case XX_FILE_TYPE_AUDIO_DFF: return "audio dff";
        case XX_FILE_TYPE_AUDIO_WAVE64: return "audio wave64";
        case XX_FILE_TYPE_AUDIO_ADX: return "audio adx";
        case XX_FILE_TYPE_AUDIO_AST: return "audio ast";
        case XX_FILE_TYPE_AUDIO_HCA: return "audio hca";
        case XX_FILE_TYPE_IFF_8SVX: return "iff 8svx";
        case XX_FILE_TYPE_AUDIO_WAVPACK: return "audio wavpack";
        case XX_FILE_TYPE_BLENDER_BLEND: return "blender blend";
        case XX_FILE_TYPE_AUTODESK_FBX: return "autodesk fbx";
        case XX_FILE_TYPE_AUTODESK_3DS: return "autodesk 3ds";
        case XX_FILE_TYPE_LIGHTWAVE_LWO2: return "lightwave lwo2";
        case XX_FILE_TYPE_LIGHTWAVE_MDD: return "lightwave mdd";
        case XX_FILE_TYPE_SONY_PSP_PBP: return "sony psp pbp";
        case XX_FILE_TYPE_FLASH_VIDEO_FLV: return "flash video flv";
        case XX_FILE_TYPE_NINTENDO_N64_ROM: return "nintendo n64 rom";
        case XX_FILE_TYPE_NINTENDO_GB_ROM: return "nintendo gb rom";
        case XX_FILE_TYPE_NINTENDO_GBA_ROM: return "nintendo gba rom";
        case XX_FILE_TYPE_SEGA_MEGADRIVE_ROM: return "sega megadrive rom";
        case XX_FILE_TYPE_SPRING_S3O: return "spring s3o";
        case XX_FILE_TYPE_XNA_XNB: return "xna xnb";
        case XX_FILE_TYPE_LUA_BYTECODE51: return "lua bytecode51";
        case XX_FILE_TYPE_QUAKE_MD5MESH: return "quake md5mesh";
        case XX_FILE_TYPE_TRACKER_MOD: return "tracker mod";
        case XX_FILE_TYPE_TRACKER_FAR: return "tracker far";
        case XX_FILE_TYPE_TRACKER_MDL: return "tracker mdl";
        case XX_FILE_TYPE_TRACKER_GDM: return "tracker gdm";
        case XX_FILE_TYPE_TRACKER_DBM: return "tracker dbm";
        case XX_FILE_TYPE_TRACKER_MED: return "tracker med";
        case XX_FILE_TYPE_TRACKER_IMF: return "tracker imf";
        case XX_FILE_TYPE_TRACKER_AMF: return "tracker amf";
        case XX_FILE_TYPE_TRACKER_PSM: return "tracker psm";
        case XX_FILE_TYPE_STEINBERG_FXB: return "steinberg fxb";
        case XX_FILE_TYPE_ASTRONOMY_SER: return "astronomy ser";
        case XX_FILE_TYPE_PHOTONTIMING_PTU: return "photontiming ptu";
        case XX_FILE_TYPE_PHOTONTIMING_PHU: return "photontiming phu";
        case XX_FILE_TYPE_CHARMM_DCD: return "charmm dcd";
        case XX_FILE_TYPE_GROMACS_TRR: return "gromacs trr";
        case XX_FILE_TYPE_MICROSCOPY_ICS: return "microscopy ics";
        case XX_FILE_TYPE_TECPLOT_PLT: return "tecplot plt";
        case XX_FILE_TYPE_FUJIFILM_RAF: return "fujifilm raf";
        case XX_FILE_TYPE_SIGMA_X3F: return "sigma x3f";
        case XX_FILE_TYPE_MINOLTA_MRW: return "minolta mrw";
        case XX_FILE_TYPE_SFX_IMP: return "sfx imp";
        case XX_FILE_TYPE_SFX_RED: return "sfx red";
        case XX_FILE_TYPE_SFX_HA: return "sfx ha";
        case XX_FILE_TYPE_SFX_LZX: return "sfx lzx";
        case XX_FILE_TYPE_SFX_SQX: return "sfx sqx";
        case XX_FILE_TYPE_SFX_AIN: return "sfx ain";
        case XX_FILE_TYPE_SFX_HAP: return "sfx hap";
        case XX_FILE_TYPE_SFX_ZOO: return "sfx zoo";
        case XX_FILE_TYPE_SFX_CAZIP: return "sfx cazip";
        case XX_FILE_TYPE_SFX_TGCF: return "sfx tgcf";
        case XX_FILE_TYPE_SFX_STARKIT: return "sfx starkit";
        case XX_FILE_TYPE_SFX_ALZ: return "sfx alz";
        case XX_FILE_TYPE_SFX_CHM: return "sfx chm";
        case XX_FILE_TYPE_EGG: return "egg";
        case XX_FILE_TYPE_NUFX: return "nufx";
        case XX_FILE_TYPE_NINTENDO_DOL: return "nintendo dol";
        case XX_FILE_TYPE_NINTENDO_J3D_BMD: return "nintendo j3d bmd";
        case XX_FILE_TYPE_NINTENDO_J3D_BTK: return "nintendo j3d btk";
        case XX_FILE_TYPE_NINTENDO_BRSTM: return "nintendo brstm";
        case XX_FILE_TYPE_NINTENDO_BRWAV: return "nintendo brwav";
        case XX_FILE_TYPE_NINTENDO_BRLYT: return "nintendo brlyt";
        case XX_FILE_TYPE_NINTENDO_BRLAN: return "nintendo brlan";
        case XX_FILE_TYPE_NINTENDO_BFSHA: return "nintendo bfsha";
        case XX_FILE_TYPE_CRI_USM: return "cri usm";
        case XX_FILE_TYPE_CRI_UTF: return "cri utf";
        case XX_FILE_TYPE_IDTECH_IQM: return "idtech iqm";
        case XX_FILE_TYPE_UNREAL_PSK: return "unreal psk";
        case XX_FILE_TYPE_UNREAL_PSA: return "unreal psa";
        case XX_FILE_TYPE_TORQUE_DTS: return "torque dts";
        case XX_FILE_TYPE_MAGICAVOXEL_VOX: return "magicavoxel vox";
        case XX_FILE_TYPE_AUDIO_AU: return "audio au";
        case XX_FILE_TYPE_CREATIVE_VOC: return "creative voc";
        case XX_FILE_TYPE_TRACKER_XM: return "tracker xm";
        case XX_FILE_TYPE_TRACKER_S3M: return "tracker s3m";
        case XX_FILE_TYPE_TRACKER_IT: return "tracker it";
        case XX_FILE_TYPE_TRACKER_MTM: return "tracker mtm";
        case XX_FILE_TYPE_TRACKER_STM: return "tracker stm";
        case XX_FILE_TYPE_TRACKER_669: return "tracker 669";
        case XX_FILE_TYPE_TRACKER_ULT: return "tracker ult";
        case XX_FILE_TYPE_TRACKER_OKT: return "tracker okt";
        case XX_FILE_TYPE_NIFTI2: return "nifti2";
        case XX_FILE_TYPE_LIDAR_LAS: return "lidar las";
        case XX_FILE_TYPE_ESRI_SHP: return "esri shp";
        case XX_FILE_TYPE_POLYGON_PLY: return "polygon ply";
        case XX_FILE_TYPE_POINTCLOUD_PCD: return "pointcloud pcd";
        case XX_FILE_TYPE_MATLAB_MAT4: return "matlab mat4";
        case XX_FILE_TYPE_SEISMIC_SEGY: return "seismic segy";
        case XX_FILE_TYPE_BIOMEDICAL_BDF: return "biomedical bdf";
        case XX_FILE_TYPE_ERLANG_BEAM: return "erlang beam";
        case XX_FILE_TYPE_JAVA_JMOD: return "java jmod";
        case XX_FILE_TYPE_SFX_ARCV2: return "sfx arcv2";
        case XX_FILE_TYPE_SFX_CHZ: return "sfx chz";
        case XX_FILE_TYPE_SFX_SZDD: return "sfx szdd";
        case XX_FILE_TYPE_SFX_MPQ: return "sfx mpq";
        case XX_FILE_TYPE_SFX_SWAG: return "sfx swag";
        case XX_FILE_TYPE_SFX_ZPAK: return "sfx zpak";
        case XX_FILE_TYPE_SFX_DISKEXPRESS: return "sfx diskexpress";
        case XX_FILE_TYPE_SFX_BZIP2: return "sfx bzip2";
        case XX_FILE_TYPE_SFX_GZIP: return "sfx gzip";
        case XX_FILE_TYPE_SFX_TAR: return "sfx tar";
        case XX_FILE_TYPE_SFX_CAB: return "sfx cab";
        case XX_FILE_TYPE_PMARC_SFX: return "pmarc sfx";
        case XX_FILE_TYPE_SFX_7ZIP: return "sfx 7zip";
        case XX_FILE_TYPE_SFX_ACE: return "sfx ace";
        case XX_FILE_TYPE_SFX_ZIPCENTRAL: return "sfx zipcentral";
        case XX_FILE_TYPE_SONY_PSX_EXE: return "sony psx exe";
        case XX_FILE_TYPE_SONY_PSF: return "sony psf";
        case XX_FILE_TYPE_XBOX_XDVDFS: return "xbox xdvdfs";
        case XX_FILE_TYPE_NINTENDO_WBFS: return "nintendo wbfs";
        case XX_FILE_TYPE_GODOT_CTEX: return "godot ctex";
        case XX_FILE_TYPE_UNITY_SERIALIZED: return "unity serialized";
        case XX_FILE_TYPE_IDTECH_MDL: return "idtech mdl";
        case XX_FILE_TYPE_VALVE_STUDIO_MDL: return "valve studio mdl";
        case XX_FILE_TYPE_BLITZ3D_B3D: return "blitz3d b3d";
        case XX_FILE_TYPE_MILKSHAPE_MS3D: return "milkshape ms3d";
        case XX_FILE_TYPE_NINTENDO_BCH: return "nintendo bch";
        case XX_FILE_TYPE_NINTENDO_CGFX: return "nintendo cgfx";
        case XX_FILE_TYPE_NINTENDO_BYAML: return "nintendo byaml";
        case XX_FILE_TYPE_RELIC_CHUNKY: return "relic chunky";
        case XX_FILE_TYPE_OGRE_MESH: return "ogre mesh";
        case XX_FILE_TYPE_ADOBE_ASE: return "adobe ase";
        case XX_FILE_TYPE_ADOBE_ACO: return "adobe aco";
        case XX_FILE_TYPE_GIMP_GBR: return "gimp gbr";
        case XX_FILE_TYPE_GIMP_GIH: return "gimp gih";
        case XX_FILE_TYPE_GIMP_PAT: return "gimp pat";
        case XX_FILE_TYPE_JBIG2: return "jbig2";
        case XX_FILE_TYPE_DJVU: return "djvu";
        case XX_FILE_TYPE_EMF: return "emf";
        case XX_FILE_TYPE_WMF: return "wmf";
        case XX_FILE_TYPE_XFIG: return "xfig";
        case XX_FILE_TYPE_NIFTI1: return "nifti1";
        case XX_FILE_TYPE_NRRD: return "nrrd";
        case XX_FILE_TYPE_MRC: return "mrc";
        case XX_FILE_TYPE_METAIMAGE: return "metaimage";
        case XX_FILE_TYPE_VTK_LEGACY: return "vtk legacy";
        case XX_FILE_TYPE_GIPL: return "gipl";
        case XX_FILE_TYPE_FREESURFER_MGH: return "freesurfer mgh";
        case XX_FILE_TYPE_EDF: return "edf";
        case XX_FILE_TYPE_FCS: return "fcs";
        case XX_FILE_TYPE_TENSORFLOW_TFRECORD: return "tensorflow tfrecord";
        case XX_FILE_TYPE_SFX_ARC: return "sfx arc";
        case XX_FILE_TYPE_SFX_ARJ: return "sfx arj";
        case XX_FILE_TYPE_SFX_BSN: return "sfx bsn";
        case XX_FILE_TYPE_SFX_ARQ: return "sfx arq";
        case XX_FILE_TYPE_SFX_GXL: return "sfx gxl";
        case XX_FILE_TYPE_SFX_ASYMETRIX: return "sfx asymetrix";
        case XX_FILE_TYPE_SFX_RTA: return "sfx rta";
        case XX_FILE_TYPE_SFX_RTPATCH: return "sfx rtpatch";
        case XX_FILE_TYPE_ESP_ARCHIVE: return "esp archive";
        case XX_FILE_TYPE_SFX_KWAJ: return "sfx kwaj";
        case XX_FILE_TYPE_GEMDOS_LHA: return "gemdos lha";
        case XX_FILE_TYPE_WINIMAGE_ZIP: return "winimage zip";
        case XX_FILE_TYPE_HP3000_WRQ: return "hp3000 wrq";
        case XX_FILE_TYPE_ICU_DATA_PACKAGE: return "icu data package";
        case XX_FILE_TYPE_SFX_SQZ: return "sfx sqz";
        case XX_FILE_TYPE_NINTENDO_BFSTM: return "nintendo bfstm";
        case XX_FILE_TYPE_NINTENDO_BFWAV: return "nintendo bfwav";
        case XX_FILE_TYPE_NINTENDO_BCWAV: return "nintendo bcwav";
        case XX_FILE_TYPE_NINTENDO_BFRES: return "nintendo bfres";
        case XX_FILE_TYPE_NINTENDO_BFLYT: return "nintendo bflyt";
        case XX_FILE_TYPE_NINTENDO_BCLYT: return "nintendo bclyt";
        case XX_FILE_TYPE_NINTENDO_BFNT: return "nintendo bfnt";
        case XX_FILE_TYPE_NINTENDO_BCFNT: return "nintendo bcfnt";
        case XX_FILE_TYPE_NINTENDO_3DSX: return "nintendo 3dsx";
        case XX_FILE_TYPE_SONY_TIM2: return "sony tim2";
        case XX_FILE_TYPE_SONY_PAMF: return "sony pamf";
        case XX_FILE_TYPE_SEGA_GVR: return "sega gvr";
        case XX_FILE_TYPE_MICROSOFT_XWB: return "microsoft xwb";
        case XX_FILE_TYPE_MICROSOFT_XSB: return "microsoft xsb";
        case XX_FILE_TYPE_RELIC_SGA: return "relic sga";
        case XX_FILE_TYPE_XPM: return "xpm";
        case XX_FILE_TYPE_PCX: return "pcx";
        case XX_FILE_TYPE_IFF_ILBM: return "iff ilbm";
        case XX_FILE_TYPE_UTAH_RLE: return "utah rle";
        case XX_FILE_TYPE_RADIANCE_HDR: return "radiance hdr";
        case XX_FILE_TYPE_DPX: return "dpx";
        case XX_FILE_TYPE_CINEON: return "cineon";
        case XX_FILE_TYPE_XWD: return "xwd";
        case XX_FILE_TYPE_SGI_RGB: return "sgi rgb";
        case XX_FILE_TYPE_ASEPRITE: return "aseprite";
        case XX_FILE_TYPE_NUMPY_NPY: return "numpy npy";
        case XX_FILE_TYPE_MATLAB_MAT5: return "matlab mat5";
        case XX_FILE_TYPE_NETCDF_CLASSIC: return "netcdf classic";
        case XX_FILE_TYPE_HDF4: return "hdf4";
        case XX_FILE_TYPE_DBASE_DBF: return "dbase dbf";
        case XX_FILE_TYPE_SAS_XPORT: return "sas xport";
        case XX_FILE_TYPE_SPSS_SAV: return "spss sav";
        case XX_FILE_TYPE_STATA_DTA: return "stata dta";
        case XX_FILE_TYPE_APACHE_ARROW_FILE: return "apache arrow file";
        case XX_FILE_TYPE_APACHE_PARQUET: return "apache parquet";
        case XX_FILE_TYPE_MAKESELF: return "makeself";
        case XX_FILE_TYPE_SUN_JAVA_BINSH: return "sun java binsh";
        case XX_FILE_TYPE_INSTALLANYWHERE_UNIX: return "installanywhere unix";
        case XX_FILE_TYPE_SFX_PACKAGEFORTHEWEB: return "sfx packagefortheweb";
        case XX_FILE_TYPE_SFX_SPIS: return "sfx spis";
        case XX_FILE_TYPE_SFX_LHA: return "sfx lha";
        case XX_FILE_TYPE_LMD_CONTAINER: return "lmd container";
        case XX_FILE_TYPE_TOTALANNIHILATION_HPI: return "totalannihilation hpi";
        case XX_FILE_TYPE_RAVENSOFT_RFF: return "ravensoft rff";
        case XX_FILE_TYPE_TERMINALREALITY_POD: return "terminalreality pod";
        case XX_FILE_TYPE_VOLITION_VPP: return "volition vpp";
        case XX_FILE_TYPE_KIRIKIRI_XP3: return "kirikiri xp3";
        case XX_FILE_TYPE_FROMSOFTWARE_BINDER: return "fromsoftware binder";
        case XX_FILE_TYPE_MYTHIC_MYP: return "mythic myp";
        case XX_FILE_TYPE_LITHTECH_REZ: return "lithtech rez";
        case XX_FILE_TYPE_NINTENDO_NCCH: return "nintendo ncch";
        case XX_FILE_TYPE_NINTENDO_NCSD: return "nintendo ncsd";
        case XX_FILE_TYPE_NINTENDO_CIA: return "nintendo cia";
        case XX_FILE_TYPE_NINTENDO_NDS: return "nintendo nds";
        case XX_FILE_TYPE_NINTENDO_GCM: return "nintendo gcm";
        case XX_FILE_TYPE_NINTENDO_TPL: return "nintendo tpl";
        case XX_FILE_TYPE_SONY_TIM: return "sony tim";
        case XX_FILE_TYPE_SONY_VAG: return "sony vag";
        case XX_FILE_TYPE_LARIAN_LSPK: return "larian lspk";
        case XX_FILE_TYPE_LARIAN_LSF: return "larian lsf";
        case XX_FILE_TYPE_VALVE_HPAK: return "valve hpak";
        case XX_FILE_TYPE_RENPY_RPA: return "renpy rpa";
        case XX_FILE_TYPE_UNREAL_PACKAGE: return "unreal package";
        case XX_FILE_TYPE_SEGA_PVR2: return "sega pvr2";
        case XX_FILE_TYPE_NINTENDO_BNTX: return "nintendo bntx";
        case XX_FILE_TYPE_ICNS: return "icns";
        case XX_FILE_TYPE_XCURSOR: return "xcursor";
        case XX_FILE_TYPE_ICC: return "icc";
        case XX_FILE_TYPE_QOI: return "qoi";
        case XX_FILE_TYPE_FARBFELD: return "farbfeld";
        case XX_FILE_TYPE_PNM: return "pnm";
        case XX_FILE_TYPE_TGA: return "tga";
        case XX_FILE_TYPE_SUN_RASTER: return "sun raster";
        case XX_FILE_TYPE_FITS: return "fits";
        case XX_FILE_TYPE_DICOM: return "dicom";
        case XX_FILE_TYPE_PCAP: return "pcap";
        case XX_FILE_TYPE_BTSNOOP: return "btsnoop";
        case XX_FILE_TYPE_JAVA_CLASS: return "java class";
        case XX_FILE_TYPE_SFNT_COLLECTION: return "sfnt collection";
        case XX_FILE_TYPE_SQLITE3: return "sqlite3";
        case XX_FILE_TYPE_SQLITE_WAL: return "sqlite wal";
        case XX_FILE_TYPE_AVRO_OBJECT: return "avro object";
        case XX_FILE_TYPE_GLB: return "glb";
        case XX_FILE_TYPE_SPIRV: return "spirv";
        case XX_FILE_TYPE_CRX: return "crx";
        case XX_FILE_TYPE_BETHESDA_BSA: return "bethesda bsa";
        case XX_FILE_TYPE_BETHESDA_BA2: return "bethesda ba2";
        case XX_FILE_TYPE_UNITYFS: return "unityfs";
        case XX_FILE_TYPE_BIOWARE_BIFF: return "bioware biff";
        case XX_FILE_TYPE_BIOWARE_ERF: return "bioware erf";
        case XX_FILE_TYPE_BIOWARE_RIM: return "bioware rim";
        case XX_FILE_TYPE_LUCAS_LAB: return "lucas lab";
        case XX_FILE_TYPE_LUCAS_BUN: return "lucas bun";
        case XX_FILE_TYPE_IDTECH_BSP: return "idtech bsp";
        case XX_FILE_TYPE_VALVE_BSP: return "valve bsp";
        case XX_FILE_TYPE_IDTECH_MD2: return "idtech md2";
        case XX_FILE_TYPE_IDTECH_MD3: return "idtech md3";
        case XX_FILE_TYPE_IDTECH_QVM: return "idtech qvm";
        case XX_FILE_TYPE_MOHAWK_MHK: return "mohawk mhk";
        case XX_FILE_TYPE_QUAKE_SPRITE: return "quake sprite";
        case XX_FILE_TYPE_NINTENDO_NARC: return "nintendo narc";
        case XX_FILE_TYPE_NINTENDO_SARC: return "nintendo sarc";
        case XX_FILE_TYPE_NINTENDO_PFS0: return "nintendo pfs0";
        case XX_FILE_TYPE_NINTENDO_HFS0: return "nintendo hfs0";
        case XX_FILE_TYPE_NINTENDO_BRRES: return "nintendo brres";
        case XX_FILE_TYPE_NINTENDO_BCSTM: return "nintendo bcstm";
        case XX_FILE_TYPE_NINTENDO_BFSAR: return "nintendo bfsar";
        case XX_FILE_TYPE_NINTENDO_BCSAR: return "nintendo bcsar";
        case XX_FILE_TYPE_SONY_PSARC: return "sony psarc";
        case XX_FILE_TYPE_KTX: return "ktx";
        case XX_FILE_TYPE_KTX2: return "ktx2";
        case XX_FILE_TYPE_DDS: return "dds";
        case XX_FILE_TYPE_PVR: return "pvr";
        case XX_FILE_TYPE_VALVE_VTF: return "valve vtf";
        case XX_FILE_TYPE_XBOX_XBE: return "xbox xbe";
        case XX_FILE_TYPE_FLAC: return "flac";
        case XX_FILE_TYPE_OGG: return "ogg";
        case XX_FILE_TYPE_MP4: return "mp4";
        case XX_FILE_TYPE_MATROSKA: return "matroska";
        case XX_FILE_TYPE_AIFF: return "aiff";
        case XX_FILE_TYPE_CAF: return "caf";
        case XX_FILE_TYPE_PHOTOSHOP_PSD: return "photoshop psd";
        case XX_FILE_TYPE_TIFF: return "tiff";
        case XX_FILE_TYPE_OPENEXR: return "openexr";
        case XX_FILE_TYPE_JPEG2000_JP2: return "jpeg2000 jp2";
        case XX_FILE_TYPE_ANDROID_VENDOR_BOOT: return "android vendor boot";
        case XX_FILE_TYPE_ANDROID_DTBO: return "android dtbo";
        case XX_FILE_TYPE_ANDROID_VBMETA: return "android vbmeta";
        case XX_FILE_TYPE_ESPRESSIF_IMAGE: return "espressif image";
        case XX_FILE_TYPE_WASM: return "wasm";
        case XX_FILE_TYPE_LLVM_BITCODE_WRAPPER: return "llvm bitcode wrapper";
        case XX_FILE_TYPE_DOTNET_METADATA: return "dotnet metadata";
        case XX_FILE_TYPE_SFNT: return "sfnt";
        case XX_FILE_TYPE_WOFF: return "woff";
        case XX_FILE_TYPE_WOFF2: return "woff2";
        case XX_FILE_TYPE_ACT_APRICOT_PC_XI_RAW: return "act apricot pc xi raw";
        case XX_FILE_TYPE_ADAM: return "adam";
        case XX_FILE_TYPE_BASE16: return "base16";
        case XX_FILE_TYPE_BONDWELL_2_DISK: return "bondwell 2 disk";
        case XX_FILE_TYPE_CASIO_FZ_1_DISK: return "casio fz 1 disk";
        case XX_FILE_TYPE_ISZ: return "isz";
        case XX_FILE_TYPE_MAME_FLOPPY_IMAGE_MFI: return "mame floppy image mfi";
        case XX_FILE_TYPE_PARALLELS_HDD: return "parallels hdd";
        case XX_FILE_TYPE_PC_MAGAZINE_FLP: return "pc magazine flp";
        case XX_FILE_TYPE_PCHROM: return "pchrom";
        case XX_FILE_TYPE_PEM: return "pem";
        case XX_FILE_TYPE_PRODOS: return "prodos";
        case XX_FILE_TYPE_QEMU_ENHANCED_DISK: return "qemu enhanced disk";
        case XX_FILE_TYPE_RAWCD: return "rawcd";
        case XX_FILE_TYPE_RSDOS_FS: return "rsdos fs";
        case XX_FILE_TYPE_SAR_NS: return "sar ns";
        case XX_FILE_TYPE_SWF: return "swf";
        case XX_FILE_TYPE_T64: return "t64";
        case XX_FILE_TYPE_UUE: return "uue";
        case XX_FILE_TYPE_VDI: return "vdi";
        case XX_FILE_TYPE_BMP: return "bmp";
        case XX_FILE_TYPE_CFE: return "cfe";
        case XX_FILE_TYPE_DXBC: return "dxbc";
        case XX_FILE_TYPE_GIF: return "gif";
        case XX_FILE_TYPE_JPEG: return "jpeg";
        case XX_FILE_TYPE_LINUX_ARM64: return "linuxarm64";
        case XX_FILE_TYPE_LINUXBOOT: return "linuxboot";
        case XX_FILE_TYPE_LINUX_ZIMAGE: return "linuxzimage";
        case XX_FILE_TYPE_PCAPNG: return "pcapng";
        case XX_FILE_TYPE_PJL: return "pjl";
        case XX_FILE_TYPE_PNG: return "png";
        case XX_FILE_TYPE_RIFF: return "riff";
        case XX_FILE_TYPE_SVG: return "svg";
        case XX_FILE_TYPE_QUAKE_PAK: return "quake pak";
        case XX_FILE_TYPE_MOZILLA_MAR: return "Mozilla MAR";
        case XX_FILE_TYPE_WESTWOOD_PAK: return "Westwood PAK";
        case XX_FILE_TYPE_FATX: return "Xbox FATX filesystem";
        case XX_FILE_TYPE_SOUNDFONT2: return "SoundFont 2";
        case XX_FILE_TYPE_IVF: return "IVF video";
        case XX_FILE_TYPE_WINDOWS_ANI: return "Windows animated cursor (ANI)";
        case XX_FILE_TYPE_INTERPLAY_ACM: return "Interplay ACM";
        case XX_FILE_TYPE_CRI_AHX: return "CRI AHX";
        case XX_FILE_TYPE_ADOBE_DIRECTOR_CXT: return "Adobe Director CXT";
        case XX_FILE_TYPE_OLYMPUS_DSS: return "Olympus DSS/DS2";
        case XX_FILE_TYPE_EA_EXA: return "EA EXA/SCHl audio";
        case XX_FILE_TYPE_AUDIO_NITRO_STRM: return "Nintendo DS STRM audio";
        case XX_FILE_TYPE_AUDIO_WWISE_WEM: return "Wwise WEM audio";
        case XX_FILE_TYPE_AUDIO_SCUMM_SOU: return "SCUMM SOU voice";
        case XX_FILE_TYPE_AUDIO_RIFF_IMA: return "RIFF IMA audio";
        case XX_FILE_TYPE_HMI_MIDI: return "HMI MIDI song";
        case XX_FILE_TYPE_ENSONIQ_PAF: return "Ensoniq PAF audio";
        case XX_FILE_TYPE_ABYLIGHT_STRM: return "Abylight 3DS STRM audio";
        case XX_FILE_TYPE_LEGO_ALP: return "LEGO Racers ALP/TUN audio";
        case XX_FILE_TYPE_AUDIO_PVF: return "Portable Voice Format audio";
        case XX_FILE_TYPE_AUDIO_RIFX_WAVE: return "RIFX big-endian WAVE audio";
        case XX_FILE_TYPE_AUDIO_SHOCKWAVE_SWA: return "Shockwave Audio (SWA)";
        case XX_FILE_TYPE_CKP: return "CKP game archive";
        case XX_FILE_TYPE_EDP: return "EdgeDataPak archive";
        case XX_FILE_TYPE_PARSEC_RIB: return "Parsec RIB compressed resource";
        case XX_FILE_TYPE_PARSEC_ARCHIVE: return "Parsec resource archive";
        case XX_FILE_TYPE_PARSEC_PMM: return "Parsec PSM 2.00 music module";
        case XX_FILE_TYPE_MACHOFAT: return "MACHOFAT";
        case XX_FILE_TYPE_CFBF: return "CFBF";
        case XX_FILE_TYPE_PDF: return "PDF";
        case XX_FILE_TYPE_PTERO_BIGF: return "Ptero-Engine BIGF/ZBL archive";
        case XX_FILE_TYPE_RVZ: return "Dolphin RVZ GameCube image";
#define XX_DIE_MUSIC_TYPE_NAME(type, name) case type: return name;
#include "die_music/xx_die_music_type_names.inc"
#undef XX_DIE_MUSIC_TYPE_NAME
        case XX_FILE_TYPE_DOOM_WAD: return "doom wad";
        case XX_FILE_TYPE_QUAKE_WAD2: return "quake wad2";
        case XX_FILE_TYPE_HALFLIFE_WAD3: return "halflife wad3";
        case XX_FILE_TYPE_BUILD_GRP: return "build grp";
        case XX_FILE_TYPE_CRI_AFS: return "cri afs";
        case XX_FILE_TYPE_CRI_AWB: return "cri awb";
        case XX_FILE_TYPE_VALVE_VPK: return "valve vpk";
        case XX_FILE_TYPE_NINTENDO_U8: return "nintendo u8";
        case XX_FILE_TYPE_NINTENDO_RARC: return "nintendo rarc";
        case XX_FILE_TYPE_ANDROID_AB: return "android ab";
        case XX_FILE_TYPE_NES_ROM: return "nes rom";
        case XX_FILE_TYPE_LYNX_LNX: return "lynx lnx";
        case XX_FILE_TYPE_COMMODORE_CRT: return "commodore crt";
        case XX_FILE_TYPE_UF2: return "uf2";
        case XX_FILE_TYPE_ICO: return "ico";
        case XX_FILE_TYPE_MIDI: return "midi";
        case XX_FILE_TYPE_ADVANCED_INSTALLER_BOOTSTRAPPER: return "Advanced Installer bootstrapper";
        case XX_FILE_TYPE_ARDI_INSTALLER: return "ARDI OS/2 installer";
        case XX_FILE_TYPE_ARNI_INSTALLER_CONTAINER: return "ARNI installer container";
        case XX_FILE_TYPE_EJ_TECHNOLOGIES_INSTALL: return "ej-technologies install4j / exe4j";
        case XX_FILE_TYPE_FINSTALL: return "F Install disk data";
        case XX_FILE_TYPE_GHOST_INSTALLER: return "Ghost Installer";
        case XX_FILE_TYPE_IBM_ZPAK_INSTALLER: return "IBM ZPAK installer";
        case XX_FILE_TYPE_IFAH_INSTALLER: return "IFAH installer package";
        case XX_FILE_TYPE_INNO_SETUP: return "Inno Setup";
        case XX_FILE_TYPE_INSTALLER_VISE_WINDOWS: return "Installer VISE for Windows";
        case XX_FILE_TYPE_INSTALLSHIELD_12_SETUP: return "InstallShield 12-2012 Setup";
        case XX_FILE_TYPE_INSTALLSHIELD_3: return "InstallShield 3.x/5.x SFX";
        case XX_FILE_TYPE_INSTALLSHIELD_7_SETUP: return "InstallShield 7 All-in-One Setup";
        case XX_FILE_TYPE_INSTALLSHIELD_7_SETUP2: return "InstallShield 7 setup.boot";
        case XX_FILE_TYPE_INSTALLSHIELD_DEVELOPER: return "InstallShield Developer 7";
        case XX_FILE_TYPE_INSTALLSHIELD_ISSETUPSTREAM: return "InstallShield ISSetupStream";
        case XX_FILE_TYPE_INSTALLSHIELD_MULTIPLATFORM: return "InstallShield MultiPlatform";
        case XX_FILE_TYPE_INSTALLSHIELD_SKIN: return "InstallShield skin";
        case XX_FILE_TYPE_MICROFOX_PUT: return "MicroFox PUT";
        case XX_FILE_TYPE_O_SETUP: return "O'Setup";
        case XX_FILE_TYPE_PC_INSTALL_SETUP: return "PC-Install";
        case XX_FILE_TYPE_PYINSTALLER_ONE_EXECUTABLE: return "PyInstaller CArchive";
        case XX_FILE_TYPE_QSETUP_INSTALLATION_SUITE: return "QSetup";
        case XX_FILE_TYPE_RTPATCH_SETUP_DATA: return "RTPatch Setup volume";
        case XX_FILE_TYPE_SETUP_FACTORY: return "Setup Factory";
        case XX_FILE_TYPE_SFX_EBOOK_COMPILER_EXECUTABLES: return "eBook Creator / SBook Builder";
        case XX_FILE_TYPE_SPOON_INSTALLER: return "Spoon Installer";
        case XX_FILE_TYPE_TARMA_INSTALLER: return "Tarma Installer";
        case XX_FILE_TYPE_ADF: return "Amiga ADF";
        case XX_FILE_TYPE_APM: return "Apple Partition Map";
        case XX_FILE_TYPE_VHDX: return "VHDX";
        case XX_FILE_TYPE_BASE64: return "Base64";
        case XX_FILE_TYPE_BTOA: return "btoa / Ascii85";
        case XX_FILE_TYPE_CHD: return "MAME CHD";
        case XX_FILE_TYPE_CHM: return "Microsoft CHM";
        case XX_FILE_TYPE_CLOOP: return "cloop / geom_uzip";
        case XX_FILE_TYPE_CUE: return "CDRWIN CUE sheet";
        case XX_FILE_TYPE_DAHUA_ZIP: return "Dahua ZIP firmware";
        case XX_FILE_TYPE_DMSFW: return "DMS swapped firmware";
        case XX_FILE_TYPE_EWF: return "Expert Witness / EnCase EWF v1";
        case XX_FILE_TYPE_GODOT_ENGINE_PCK: return "Godot PCK resource pack";
        case XX_FILE_TYPE_GPG_SIGNED: return "OpenPGP ZIP-compressed packet";
        case XX_FILE_TYPE_IHEX: return "Intel HEX image";
        case XX_FILE_TYPE_KWAJ: return "Microsoft KWAJ stream";
        case XX_FILE_TYPE_LBR: return "CP/M LU LBR library";
        case XX_FILE_TYPE_LZFSE: return "LZFSE / LZVN stream";
        case XX_FILE_TYPE_NRG: return "Nero NRG disc image";
        case XX_FILE_TYPE_PACKIT_MAC: return "Macintosh PackIt";
        case XX_FILE_TYPE_RPM: return "RPM package";
        case XX_FILE_TYPE_STUFFIT5: return "StuffIt 5 archive";
        case XX_FILE_TYPE_MSDOS: return "MSDOS";
        case XX_FILE_TYPE_SFX_ANALOGX_EMUCORE_FFS: return "AnalogX FFS";
        case XX_FILE_TYPE_SFX_KRZIP: return "KRZIP self-extractor";
        case XX_FILE_TYPE_SFX_WARPIN_PACKAGE: return "WarpIN package";
        case XX_FILE_TYPE_SFX_HCI_INSTALIT: return "HCI Instalit";
        case XX_FILE_TYPE_SFX_CLICKTEAM_MULTIMEDIA_FUSION: return "Clickteam Multimedia Fusion";
        case XX_FILE_TYPE_SFX_ABBYY_FINE_OBJECTS: return "ABBYY FineObjects";
        case XX_FILE_TYPE_SFX_FLASHJESTER_JUGGLOR: return "FlashJester Jugglor";
        case XX_FILE_TYPE_SFX_JGSOFT_DEPLOYMASTER_PACKAGE: return "JGsoft DeployMaster";
        case XX_FILE_TYPE_SFX_ARDI_DISKETTE_IMAGE: return "ARDI diskette image";
        case XX_FILE_TYPE_SFX_SBX_EXTRACTOR: return "SBX self-extractor";
        case XX_FILE_TYPE_PE32: return "PE32";
        case XX_FILE_TYPE_PE64: return "PE64";
        case XX_FILE_TYPE_BINARY: return "BINARY";
        case XX_FILE_TYPE_AIN: return "AIN";
        case XX_FILE_TYPE_ALDUS: return "ALDUS";
        case XX_FILE_TYPE_ALZ: return "ALZ";
        case XX_FILE_TYPE_AMPK: return "AMPK";
        case XX_FILE_TYPE_AODOS: return "AODOS";
        case XX_FILE_TYPE_ARCFS: return "ARCFS";
        case XX_FILE_TYPE_PDP11AR: return "PDP11AR";
        case XX_FILE_TYPE_ARTIPACK: return "ARTIPACK";
        case XX_FILE_TYPE_ARCV2: return "ARCV2";
        case XX_FILE_TYPE_ARCV4: return "ARCV4";
        case XX_FILE_TYPE_WARC: return "WARC";
        case XX_FILE_TYPE_ISO9660: return "ISO9660";
        case XX_FILE_TYPE_TRX: return "TRX";
        case XX_FILE_TYPE_SEAMA: return "SEAMA";
        case XX_FILE_TYPE_CHK: return "CHK";
        case XX_FILE_TYPE_PACKIMG: return "PACKIMG";
        case XX_FILE_TYPE_DLOB: return "DLOB";
        case XX_FILE_TYPE_WINCE: return "Windows CE binary image";
        case XX_FILE_TYPE_BINHDR: return "BIN firmware header";
        case XX_FILE_TYPE_RTK: return "RTK firmware header";
        case XX_FILE_TYPE_CSMAN: return "CSman DAT file";
        case XX_FILE_TYPE_VXWORKS: return "VxWorks symbol table";
        case XX_FILE_TYPE_UEFI_FV: return "UEFI firmware volume";
        case XX_FILE_TYPE_UEFI_CAPSULE: return "UEFI capsule";
        case XX_FILE_TYPE_QCOW: return "QCOW";
        case XX_FILE_TYPE_QNX6: return "QNX6";
        case XX_FILE_TYPE_LUKS: return "LUKS";
        case XX_FILE_TYPE_APFS: return "APFS";
        case XX_FILE_TYPE_BTRFS: return "Btrfs";
        case XX_FILE_TYPE_LOGFS: return "LogFS";
        case XX_FILE_TYPE_DMG: return "DMG";
        case XX_FILE_TYPE_DMS: return "DMS";
        case XX_FILE_TYPE_XAMARIN_COMPRESSED_ASSEMBLY: return "Xamarin compressed assembly (XALZ)";
        case XX_FILE_TYPE_X68000_DIM: return "X68000 DIM floppy image";
        case XX_FILE_TYPE_VISIONAIRE_STUDIO_VIS: return "Visionaire Studio VIS";
        case XX_FILE_TYPE_UHARC: return "UHARC";
        case XX_FILE_TYPE_TRS_80_JV3: return "TRS-80 JV3";
        case XX_FILE_TYPE_TRS_80_JV1: return "TRS-80 JV1";
        case XX_FILE_TYPE_T98_NEXT_NFD: return "T98-Next NFD";
        case XX_FILE_TYPE_STUFFIT_SPLIT_FILE: return "StuffIt split file";
        case XX_FILE_TYPE_RDB: return "Amiga Rigid Disk Block (RDB) hard disk";
        case XX_FILE_TYPE_QNAP_NAS_FIRMWARE: return "QNAP NAS firmware";
        case XX_FILE_TYPE_QCOW1: return "QCOW1";
        case XX_FILE_TYPE_NSA: return "NScripter NSA archive";
        case XX_FILE_TYPE_NS2: return "NScripter NS2 archive";
        case XX_FILE_TYPE_NEC_PC_98_FDI: return "NEC PC-98 FDI";
        case XX_FILE_TYPE_MS_DOS_BACKUP: return "MS-DOS BACKUP (v2.0-3.2)";
        case XX_FILE_TYPE_HXC_STREAM_HFE: return "HxC Stream HFE";
        case XX_FILE_TYPE_ENCRYPTED_APPLE_DISK_IMAGE: return "Encrypted Apple disk image";
        case XX_FILE_TYPE_APPLE_SPARSE_BUNDLE: return "Apple sparse bundle";
        case XX_FILE_TYPE_APPLE_DISK_COPY_6_NDIF_IMAGE: return "Apple Disk Copy 6 NDIF image";
        case XX_FILE_TYPE_RAW_DEFLATE_COMPRESSED_DATA: return "Raw Deflate";
        case XX_FILE_TYPE_LZOP: return "LZOP";
        case XX_FILE_TYPE_SREC: return "Motorola S-record";
        case XX_FILE_TYPE_DCLRAW: return "DCLMultiStream";
        case XX_FILE_TYPE_XPAK: return "XPAK";
        case XX_FILE_TYPE_PDB: return "Palm PDB";
        case XX_FILE_TYPE_INFOGRAMESFT: return "Infogrames PAK";
        case XX_FILE_TYPE_UBOOT_ENV: return "U-Boot environment";
        case XX_FILE_TYPE_TWRX: return "TWRX";
        case XX_FILE_TYPE_TPLINK: return "TP-Link firmware";
        case XX_FILE_TYPE_SILMARILSFT: return "Silmarils";
        case XX_FILE_TYPE_SHRS: return "D-Link SHRS";
        case XX_FILE_TYPE_MH01: return "D-Link MH01";
        case XX_FILE_TYPE_MATTER_OTA: return "Matter OTA image";
        case XX_FILE_TYPE_LZ4DEMO: return "LZ4Demo";
        case XX_FILE_TYPE_LINGVOARC: return "LingvoArc";
        case XX_FILE_TYPE_JBOOT: return "JBOOT";
        case XX_FILE_TYPE_ENCRPTED_IMG: return "D-Link encrpted_img";
        case XX_FILE_TYPE_ENCFW: return "D-Link encfw encrypted firmware";
        case XX_FILE_TYPE_ECOS: return "eCos kernel";
        case XX_FILE_TYPE_DLKE: return "DLKE";
        case XX_FILE_TYPE_DLINK_TLV: return "D-Link TLV firmware";
        case XX_FILE_TYPE_DKBS: return "DKBS";
        case XX_FILE_TYPE_AUTEL: return "AUTEL";
        case XX_FILE_TYPE_ARCADYAN: return "Arcadyan obfuscated LZMA";
        case XX_FILE_TYPE_ANDROIDBOOT: return "Android boot image";
        case XX_FILE_TYPE_RAWSTAC: return "RawStac";
        case XX_FILE_TYPE_VMSSAVESET: return "VMSSaveset";
        case XX_FILE_TYPE_BOO: return "BOO";
        case XX_FILE_TYPE_WOLFFT: return "Wolf";
        case XX_FILE_TYPE_SETTLERSFT: return "Settlers";
        case XX_FILE_TYPE_TEACY: return "Teacy";
        case XX_FILE_TYPE_RSC: return "RSC";
        case XX_FILE_TYPE_RES: return "RES";
        case XX_FILE_TYPE_BSN: return "BSN";
        case XX_FILE_TYPE_WINTERMUTEDCP: return "WintermuteDCP";
        case XX_FILE_TYPE_VOLITIONVPFT: return "VolitionVP";
        case XX_FILE_TYPE_AGIS: return "AGIS";
        case XX_FILE_TYPE_HOG: return "HOG";
        case XX_FILE_TYPE_LZPIS2: return "LZPIS2";
        case XX_FILE_TYPE_RNCA: return "RNCA";
        case XX_FILE_TYPE_PAPERPORT: return "PaperPort";
        case XX_FILE_TYPE_STARKIT: return "Starkit";
        case XX_FILE_TYPE_LSPACK10: return "LSPack10";
        case XX_FILE_TYPE_IXA: return "IXA";
        case XX_FILE_TYPE_MLB_FT: return "MLB_FT";
        case XX_FILE_TYPE_FSS: return "FSS";
        case XX_FILE_TYPE_EPF: return "EPF";
        case XX_FILE_TYPE_DFC: return "DFC";
        case XX_FILE_TYPE_PPD: return "PPD";
        case XX_FILE_TYPE_SFX_AD01: return "SFX AD01";
        case XX_FILE_TYPE_SFX_NSS: return "SFX NSS";
        case XX_FILE_TYPE_SFX_RSFX: return "SFX RSFX";
        case XX_FILE_TYPE_SFX_SOFTPAQ4: return "Compaq SoftPaq 4";
        case XX_FILE_TYPE_KA: return "KA";
        case XX_FILE_TYPE_NEXTSTEP_DISKIMAGE: return "NeXTSTEP disk image";
        case XX_FILE_TYPE_DN: return "DN";
        case XX_FILE_TYPE_INSA: return "INSA";
        case XX_FILE_TYPE_SFX_VMS_DCX: return "SFX VMS DCX";
        case XX_FILE_TYPE_OBERON: return "Oberon";
        case XX_FILE_TYPE_SOLITAIRE_DELUXE: return "Solitaire Deluxe";
        case XX_FILE_TYPE_THEBAT_MSB: return "The Bat! MSB";
        case XX_FILE_TYPE_SFX_LOCALZIP: return "SFX embedded archive";
        case XX_FILE_TYPE_LIF: return "LIF";
        case XX_FILE_TYPE_QIP2: return "QIP2";
        case XX_FILE_TYPE_EMT: return "EMT";
        case XX_FILE_TYPE_BAGF: return "BAGF";
        case XX_FILE_TYPE_WRZL: return "WRZL";
        case XX_FILE_TYPE_SPK: return "SPK";
        case XX_FILE_TYPE_STAC: return "Stac";
        case XX_FILE_TYPE_DBZ: return "DBZ";
        case XX_FILE_TYPE_SQUEEZE2: return "Squeeze2";
        case XX_FILE_TYPE_SQ: return "SQ";
        case XX_FILE_TYPE_PHAR: return "Phar";
        case XX_FILE_TYPE_MPQ: return "MPQ";
        case XX_FILE_TYPE_SABDU: return "SABDU";
        case XX_FILE_TYPE_APRICOT: return "Apricot";
        case XX_FILE_TYPE_HDCOPY: return "HDCopy";
        case XX_FILE_TYPE_COPYDISK: return "CopyDisk";
        case XX_FILE_TYPE_CISO: return "CISO";
        case XX_FILE_TYPE_CISO2: return "CISO v2";
        case XX_FILE_TYPE_ZISO: return "ZISO";
        case XX_FILE_TYPE_DAX: return "DAX compressed ISO";
        case XX_FILE_TYPE_MYZ80: return "MYZ80 disk image";
        case XX_FILE_TYPE_NANOWASP: return "NanoWasp Microbee disk image";
        case XX_FILE_TYPE_GOTEK: return "Gotek disk collection";
        case XX_FILE_TYPE_VMDK: return "VMDK";
        case XX_FILE_TYPE_VHDDYNAMIC: return "VHDDynamic";
        case XX_FILE_TYPE_WIM: return "WIM";
        case XX_FILE_TYPE_SOFTPAQ2: return "SoftPaq2";
        case XX_FILE_TYPE_AIAFF: return "AIAFF";
        case XX_FILE_TYPE_GXL: return "GXL";
        case XX_FILE_TYPE_SHRINKWRAP: return "ShrinkWrap";
        case XX_FILE_TYPE_RED: return "RED";
        case XX_FILE_TYPE_DISKEXPRESS: return "DiskExpress";
        case XX_FILE_TYPE_CPX: return "CPX";
        case XX_FILE_TYPE_CPX4: return "CPX4";
        case XX_FILE_TYPE_SMSIPAK: return "SMSIPAK";
        case XX_FILE_TYPE_BND: return "BND";
        case XX_FILE_TYPE_CAT: return "CAT";
        case XX_FILE_TYPE_CSIDOS: return "CSIDOS";
        case XX_FILE_TYPE_BINDER: return "Binder";
        case XX_FILE_TYPE_JASC: return "JASC";
        case XX_FILE_TYPE_RECOGNITA: return "Recognita";
        case XX_FILE_TYPE_SCF: return "SCF";
        case XX_FILE_TYPE_BCW: return "BCW";
        case XX_FILE_TYPE_BVRP: return "BVRP";
        case XX_FILE_TYPE_SSM: return "SSM";
        case XX_FILE_TYPE_MDCD: return "MDCD";
        case XX_FILE_TYPE_XLAS: return "XLAS";
        case XX_FILE_TYPE_PAIN: return "PAIN";
        case XX_FILE_TYPE_QRST: return "QRST";
        case XX_FILE_TYPE_OPC: return "OPC";
        case XX_FILE_TYPE_TNEF: return "TNEF";
        case XX_FILE_TYPE_MCC: return "MCC";
        case XX_FILE_TYPE_NOTETAB: return "NoteTab";
        case XX_FILE_TYPE_STUNTS: return "Stunts";
        case XX_FILE_TYPE_MEGATECHVOL: return "MegatechVol";
        case XX_FILE_TYPE_GRASP: return "GRASP";
        case XX_FILE_TYPE_PSN: return "PSNCompress";
        case XX_FILE_TYPE_SINNER: return "Sinner";
        case XX_FILE_TYPE_HOG2: return "HOG2";
        case XX_FILE_TYPE_PCXLIB: return "PCXLib";
        case XX_FILE_TYPE_VMSDB: return "VMSDatabase";
        case XX_FILE_TYPE_VMSPCSI: return "VMSPCSI";
        case XX_FILE_TYPE_BEOSPKG: return "BeOSPackage";
        case XX_FILE_TYPE_SOLARISPKG: return "SolarisPackage";
        case XX_FILE_TYPE_PAX: return "PAX";
        case XX_FILE_TYPE_COPYQMEXE: return "CopyQMOverlay";
        case XX_FILE_TYPE_DISKJUGGLER: return "DiskJuggler";
        case XX_FILE_TYPE_PMDISKCOPY: return "PMDiskcopy";
        case XX_FILE_TYPE_DISKDUPE: return "DiskDupe";
        case XX_FILE_TYPE_IMD: return "IMD";
        case XX_FILE_TYPE_TWOIMG: return "2IMG";
        case XX_FILE_TYPE_FDI: return "FDI";
        case XX_FILE_TYPE_HFE: return "HFE";
        case XX_FILE_TYPE_TELEDISK: return "TeleDisk";
        case XX_FILE_TYPE_COPYQM: return "CopyQM";
        case XX_FILE_TYPE_PCINSTALL: return "PCInstall";
        case XX_FILE_TYPE_GKSETUP: return "GkSetup";
        case XX_FILE_TYPE_IS11: return "IS11";
        case XX_FILE_TYPE_IZPACK: return "IzPack";
        case XX_FILE_TYPE_SQUEEZE1: return "Squeeze1";
        case XX_FILE_TYPE_TRDOS: return "TRDOS";
        case XX_FILE_TYPE_LIFKD: return "LIFKD";
        case XX_FILE_TYPE_ARCV: return "ARCV";
        case XX_FILE_TYPE_CPOINT: return "CPOINT";
        case XX_FILE_TYPE_COMPAQLZH: return "CompaqLZH";
        case XX_FILE_TYPE_LZK00: return "LZK00";
        case XX_FILE_TYPE_PMA: return "PMA";
        case XX_FILE_TYPE_BINHEX: return "BinHex";
        case XX_FILE_TYPE_BINARYII: return "BinaryII";
        case XX_FILE_TYPE_STUFFIT: return "StuffIt";
        case XX_FILE_TYPE_DCLFT: return "DCLStream";
        case XX_FILE_TYPE_DEBUGSCR: return "DebugScript";
        case XX_FILE_TYPE_GOB: return "GOB";
        case XX_FILE_TYPE_SAVEDSKF: return "SaveDskF";
        case XX_FILE_TYPE_EDILZSS: return "EDILZSS";
        case XX_FILE_TYPE_IS7INX: return "IS7INX";
        case XX_FILE_TYPE_IS5: return "IS5";
        case XX_FILE_TYPE_IS3: return "IS3";
        case XX_FILE_TYPE_PSDC: return "PSDC";
        case XX_FILE_TYPE_UNIX_COMPACT: return "UnixCompact";
        case XX_FILE_TYPE_SCO: return "SCO";
        case XX_FILE_TYPE_GPFPACK: return "GPFPACK";
        case XX_FILE_TYPE_FTCOMP: return "FTCOMP";
        case XX_FILE_TYPE_WINLINK: return "WinLink";
        case XX_FILE_TYPE_GST: return "GST";
        case XX_FILE_TYPE_FINEAR: return "FinEAR";
        case XX_FILE_TYPE_MWAVE: return "Mwave";
        case XX_FILE_TYPE_XORARCHIVE: return "XORArchive";
        case XX_FILE_TYPE_MXS: return "MXS";
        case XX_FILE_TYPE_EDC: return "EDC";
        case XX_FILE_TYPE_MRNZ: return "MRNZ";
        case XX_FILE_TYPE_TPWM: return "TPWM";
        case XX_FILE_TYPE_CAZIP: return "CAZIP";
        case XX_FILE_TYPE_IBMPACK: return "IBMPACK";
        case XX_FILE_TYPE_RNC: return "RNC";
        case XX_FILE_TYPE_SHAR: return "SHAR";
        case XX_FILE_TYPE_NETWARE2: return "NetWare2";
        case XX_FILE_TYPE_MATHCAD: return "MathCAD";
        case XX_FILE_TYPE_PERFORM: return "PerFORM";
        case XX_FILE_TYPE_BATTLEISLE: return "BattleIsle";
        case XX_FILE_TYPE_KPCK: return "KPCK";
        case XX_FILE_TYPE_BEATTHEHOUSE: return "BeatTheHouse";
        case XX_FILE_TYPE_PP20: return "PP20";
        case XX_FILE_TYPE_MACBINARY: return "MacBinary";
        case XX_FILE_TYPE_APPLESINGLE: return "AppleSingle";
        case XX_FILE_TYPE_RESOURCEFORK: return "ResourceFork";
        case XX_FILE_TYPE_CRAMFS: return "CRAMFS";
        case XX_FILE_TYPE_JFFS2: return "JFFS2";
        case XX_FILE_TYPE_YAFFS: return "YAFFS";
        case XX_FILE_TYPE_UBI: return "UBI";
        case XX_FILE_TYPE_UBIFS: return "UBIFS";
        case XX_FILE_TYPE_EXT: return "ext2/3/4";
        case XX_FILE_TYPE_FAT: return "FAT";
        case XX_FILE_TYPE_MBR: return "MBR";
        case XX_FILE_TYPE_GPT: return "GPT";
        case XX_FILE_TYPE_SPARSE: return "Android sparse image";
        case XX_FILE_TYPE_UIMAGE: return "U-Boot uImage";
        case XX_FILE_TYPE_DTB: return "Device Tree Blob";
        case XX_FILE_TYPE_SQUASHFS: return "SquashFS";
        case XX_FILE_TYPE_NTFS: return "NTFS";
        case XX_FILE_TYPE_UDF: return "UDF";
        case XX_FILE_TYPE_ROMFS: return "ROMFS";
        case XX_FILE_TYPE_SQZ: return "SQZ";
        case XX_FILE_TYPE_TOPSPEED: return "TopSpeed";
        case XX_FILE_TYPE_TPS: return "TPS";
        case XX_FILE_TYPE_ULEAD: return "ULEAD";
        case XX_FILE_TYPE_QUANTUM: return "Quantum archive";
        case XX_FILE_TYPE_ZXZIP: return "ZXZIP";
        case XX_FILE_TYPE_ZOOM: return "Zoom disk image";
        case XX_FILE_TYPE_SFPACK: return "SFPack";
        case XX_FILE_TYPE_CLAYLZ: return "CLAYLZ";
        case XX_FILE_TYPE_C64WRAPTOR: return "C64WRAPTOR";
        case XX_FILE_TYPE_CORELLTEC: return "CORELLTEC";
        case XX_FILE_TYPE_PCSECURE: return "PCSECURE";
        case XX_FILE_TYPE_RSVK: return "RSVK";
        case XX_FILE_TYPE_RAW_LZW15V: return "RAW_LZW15V";
        case XX_FILE_TYPE_SAF: return "SAF";
        case XX_FILE_TYPE_SLS: return "SLS";
        case XX_FILE_TYPE_NID: return "NID";
        case XX_FILE_TYPE_GAMOS: return "GAMOS";
        case XX_FILE_TYPE_PANORAMA: return "PANORAMA";
        case XX_FILE_TYPE_FPAK: return "FPAK";
        case XX_FILE_TYPE_TAR_LZ4: return "TAR.LZ4";
        case XX_FILE_TYPE_LZ4: return "LZ4";
        case XX_FILE_TYPE_LZ5: return "LZ5";
        case XX_FILE_TYPE_LIZARD: return "LIZARD";
        case XX_FILE_TYPE_BROTLI: return "BROTLI";
        case XX_FILE_TYPE_ARJ: return "ARJ";
        case XX_FILE_TYPE_CAB: return "CAB";
        case XX_FILE_TYPE_AIXBFF: return "BFF";
        case XX_FILE_TYPE_ARX: return "ARX";
        case XX_FILE_TYPE_ELF32: return "ELF32";
        case XX_FILE_TYPE_ELF64: return "ELF64";
        case XX_FILE_TYPE_MACHO32: return "MACH-O32";
        case XX_FILE_TYPE_MACHO64: return "MACH-O64";
        case XX_FILE_TYPE_NE: return "NE";
        case XX_FILE_TYPE_LE: return "LE";
        case XX_FILE_TYPE_LX: return "LX";
        case XX_FILE_TYPE_DEX: return "DEX";
        default:                 return "UNKNOWN";
    }
}

static const char *xx_format_get_short_name(Abstractformat *f) {
    return f ? xx_format_file_type_to_string(f->file_type) : "UNKNOWN";
}

wchar_t *xx_format_data_struct_to_string(Abstractformat *f, const xx_data_struct *ds) {
    if (!ds) {
        return NULL;
    }

    const char *id_name = xx_format_data_struct_id_to_string(f, ds->id);
    const char *type_name = xx_data_struct_type_to_string(ds->type);

    char buf[256];
    /* Global ids (e.g. RAW_DATA) are format-independent, so no "<FORMAT>::" prefix is added */
    if (ds->id == XX_DATA_STRUCT_ID_RAW_DATA) {
        xx_rt_snprintf(buf, sizeof(buf), "%s?offset=%lld&entry_size=%lld&total_size=%lld&count=%llu&type=%s",
                 id_name, (long long)ds->offset, (long long)ds->entry_size, (long long)ds->total_size,
                 (unsigned long long)ds->count, type_name);
    } else {
        const char *format_name = xx_format_get_short_name(f);
        xx_rt_snprintf(buf, sizeof(buf), "%s::%s?offset=%lld&entry_size=%lld&total_size=%lld&count=%llu&type=%s",
                 format_name, id_name, (long long)ds->offset, (long long)ds->entry_size, (long long)ds->total_size,
                 (unsigned long long)ds->count, type_name);
    }

    return xx_str_ansi_to_unicode(buf);
}

void xx_data_struct_state_init(xx_data_struct_state *state, Abstractformat *fmt) {
    if (!state) {
        return;
    }
    xx_mem_zero(state, sizeof(xx_data_struct_state));
    state->format = fmt;
    state->has_struct = false;
    state->current_index = -1;
    state->total_structs = -1;
}

void xx_data_struct_state_cleanup(xx_data_struct_state *state) {
    if (!state) {
        return;
    }
    if (state->free_internal && state->internal_state) {
        state->free_internal(state->internal_state);
        state->internal_state = NULL;
    }
    xx_mem_zero(state, sizeof(xx_data_struct_state));
    state->current_index = -1;
    state->total_structs = -1;
}

void xx_data_struct_state_free(xx_data_struct_state *state) {
    if (!state) {
        return;
    }
    xx_data_struct_state_cleanup(state);
    xx_mem_free(state);
}

xx_data_struct_state *xx_format_create_data_structs_reading(Abstractformat *f, xx_pd_struct *pd) {
    if (!xx_format_handle_split_format(f, pd)) {
        return NULL;
    }
    if (!f->base_info_handled) {
        xx_format_handle_base_info(f, pd);
    }
    if (f->create_data_structs_reading) {
        return (f->create_data_structs_reading)(f, pd);
    }
    return NULL;
}

const xx_data_struct *xx_format_get_current_data_struct(Abstractformat *f, xx_data_struct_state *state) {
    if (!state) {
        return NULL;
    }
    if (f && f->get_current_data_struct) {
        return (f->get_current_data_struct)(f, state);
    }
    return state->has_struct ? &state->current_struct : NULL;
}

bool xx_format_data_struct_move_to_next(Abstractformat *f, xx_data_struct_state *state, xx_pd_struct *pd) {
    if (!state) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->data_struct_move_to_next) {
        return (f->data_struct_move_to_next)(f, state, pd);
    }
    return false;
}

void xx_format_free_data_structs_reading(Abstractformat *f, xx_data_struct_state *state) {
    if (!state) {
        return;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->free_data_structs_reading) {
        (f->free_data_structs_reading)(f, state);
    } else {
        xx_data_struct_state_free(state);
    }
}

/* ========================================================================= */
/* --- Data Struct Record Lifecycle                                       --- */
/* ========================================================================= */

void xx_data_struct_record_init(xx_data_struct_record *rec) {
    if (!rec) {
        return;
    }
    xx_mem_zero(rec, sizeof(xx_data_struct_record));
    rec->offset = -1;
    rec->size = -1;
    rec->property = XX_DATA_STRUCT_RECORD_PROPERTY_NONE;
    xx_var_init(&rec->value);
}

void xx_data_struct_record_cleanup(xx_data_struct_record *rec) {
    if (!rec) {
        return;
    }
    if (rec->name) {
        xx_str_wfree(rec->name);
    }
    if (rec->type) {
        xx_str_wfree(rec->type);
    }
    if (rec->display_value) {
        xx_str_wfree(rec->display_value);
    }
    xx_var_cleanup(&rec->value);
    xx_mem_zero(rec, sizeof(xx_data_struct_record));
    rec->offset = -1;
    rec->size = -1;
}

void xx_data_struct_record_free_elem(void *element) {
    if (element) {
        xx_data_struct_record_cleanup((xx_data_struct_record *)element);
    }
}

bool xx_data_struct_record_set_name(xx_data_struct_record *rec, const wchar_t *name) {
    if (!rec) {
        return false;
    }
    wchar_t *copy = name ? xx_str_wdup(name) : NULL;
    if (name && !copy) {
        return false;
    }
    if (rec->name) {
        xx_str_wfree(rec->name);
    }
    rec->name = copy;
    return true;
}

bool xx_data_struct_record_set_type(xx_data_struct_record *rec, const wchar_t *type) {
    if (!rec) {
        return false;
    }
    wchar_t *copy = type ? xx_str_wdup(type) : NULL;
    if (type && !copy) {
        return false;
    }
    if (rec->type) {
        xx_str_wfree(rec->type);
    }
    rec->type = copy;
    return true;
}

bool xx_data_struct_record_set_value(xx_data_struct_record *rec, const xx_var *value) {
    if (!rec || !value) {
        return false;
    }
    return xx_var_copy(&rec->value, value);
}

bool xx_data_struct_record_set_display_value(xx_data_struct_record *rec, const wchar_t *display_value) {
    if (!rec) {
        return false;
    }
    wchar_t *copy = display_value ? xx_str_wdup(display_value) : NULL;
    if (display_value && !copy) {
        return false;
    }
    if (rec->display_value) {
        xx_str_wfree(rec->display_value);
    }
    rec->display_value = copy;
    return true;
}

bool xx_data_struct_record_populate(xx_data_struct_record *rec, xx_io_device *device,
                                   int64_t parent_offset, const xx_data_struct_field_desc *field,
                                   bool is_big_endian) {
    if (!rec || !device || !field) {
        return false;
    }
    xx_data_struct_record_init(rec);

    rec->offset = field->rel_offset;
    rec->size = field->size;
    rec->property = field->property;

    int64_t abs_offset = parent_offset + field->rel_offset;
    uint64_t raw_value = 0;
    if (field->size == 1) {
        raw_value = xx_io_get_u8(device, abs_offset);
    } else if (field->size == 2) {
        raw_value = xx_io_get_u16(device, abs_offset, is_big_endian);
    } else if (field->size == 8) {
        raw_value = xx_io_get_u64(device, abs_offset, is_big_endian);
    } else {
        raw_value = xx_io_get_u32(device, abs_offset, is_big_endian);
    }

    xx_var_set_u64(&rec->value, raw_value);

    char display_ascii[32];
    wchar_t display_buf[32];
    int display_length;
    size_t display_index;
    bool as_hex = (field->property & (XX_DATA_STRUCT_RECORD_PROPERTY_ID | XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)) != 0;
    if (as_hex) {
        display_length = xx_rt_snprintf(display_ascii, sizeof(display_ascii),
                                        "0x%08llX",
                                        (unsigned long long)raw_value);
    } else {
        display_length = xx_rt_snprintf(display_ascii, sizeof(display_ascii),
                                        "%llu",
                                        (unsigned long long)raw_value);
    }
    if (display_length < 0 ||
        (size_t)display_length >= sizeof(display_ascii)) {
        return false;
    }
    for (display_index = 0U; display_index <= (size_t)display_length;
         ++display_index) {
        display_buf[display_index] =
            (wchar_t)(unsigned char)display_ascii[display_index];
    }

    xx_data_struct_record_set_name(rec, field->name);
    xx_data_struct_record_set_type(rec, field->type);
    xx_data_struct_record_set_display_value(rec, display_buf);

    return true;
}

/* ========================================================================= */
/* --- Data Struct Records Stream Reading Operations                      --- */
/* ========================================================================= */

void xx_data_struct_record_state_init(xx_data_struct_record_state *state, Abstractformat *fmt, const xx_data_struct *ds) {
    if (!state) {
        return;
    }
    xx_mem_zero(state, sizeof(xx_data_struct_record_state));
    state->format = fmt;
    if (ds) {
        state->parent_struct = *ds;
    }
    xx_data_struct_record_init(&state->current_record);
    state->has_record = false;
    state->current_index = -1;
    state->total_records = -1;
}

void xx_data_struct_record_state_cleanup(xx_data_struct_record_state *state) {
    if (!state) {
        return;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (state->free_internal && state->internal_state) {
        state->free_internal(state->internal_state);
        state->internal_state = NULL;
    }
    xx_mem_zero(state, sizeof(xx_data_struct_record_state));
    state->current_index = -1;
    state->total_records = -1;
}

void xx_data_struct_record_state_free(xx_data_struct_record_state *state) {
    if (!state) {
        return;
    }
    xx_data_struct_record_state_cleanup(state);
    xx_mem_free(state);
}

xx_data_struct_record_state *xx_format_create_data_struct_records_reading(Abstractformat *f, const xx_data_struct *ds, xx_pd_struct *pd) {
    if (!f || !ds) {
        return NULL;
    }
    if (!xx_format_handle_split_format(f, pd)) {
        return NULL;
    }
    if (!f->base_info_handled) {
        xx_format_handle_base_info(f, pd);
    }
    if (f->create_data_struct_records_reading) {
        return (f->create_data_struct_records_reading)(f, ds, pd);
    }
    return NULL;
}

const xx_data_struct_record *xx_format_get_current_data_struct_record(Abstractformat *f, xx_data_struct_record_state *state) {
    if (!state) {
        return NULL;
    }
    if (f && f->get_current_data_struct_record) {
        return (f->get_current_data_struct_record)(f, state);
    }
    return state->has_record ? &state->current_record : NULL;
}

bool xx_format_data_struct_record_move_to_next(Abstractformat *f, xx_data_struct_record_state *state, xx_pd_struct *pd) {
    if (!state) {
        return false;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->data_struct_record_move_to_next) {
        return (f->data_struct_record_move_to_next)(f, state, pd);
    }
    return false;
}

void xx_format_free_data_struct_records_reading(Abstractformat *f, xx_data_struct_record_state *state) {
    if (!state) {
        return;
    }
    if (!f) {
        f = state->format;
    }
    if (f && f->free_data_struct_records_reading) {
        (f->free_data_struct_records_reading)(f, state);
    } else {
        xx_data_struct_record_state_free(state);
    }
}
