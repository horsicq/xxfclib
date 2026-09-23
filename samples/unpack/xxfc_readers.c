/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* GENERATED -- do not edit by hand. See README.md.
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
static Abstractformat *mk_ext(xx_io_device *d, int64_t b) {
    xx_ext *r = xx_ext_create(d, b);
    return r ? &r->format : NULL;
}
static void rm_ext(void *p) { xx_ext_free((xx_ext *)p); }
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

static xxfc_reader_entry g_readers[] = {
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
    { "cazip", mk_cazip, rm_cazip, XX_FILE_TYPE_UNKNOWN },
    { "cfl", mk_cfl, rm_cfl, XX_FILE_TYPE_UNKNOWN },
    { "chieflz", mk_chieflz, rm_chieflz, XX_FILE_TYPE_UNKNOWN },
    { "chieflzmulti", mk_chieflzmulti, rm_chieflzmulti, XX_FILE_TYPE_UNKNOWN },
    { "chk", mk_chk, rm_chk, XX_FILE_TYPE_UNKNOWN },
    { "ciso", mk_ciso, rm_ciso, XX_FILE_TYPE_UNKNOWN },
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
            g_readers[i].type = xx_format_get_file_type(fmt);
            g_readers[i].release(fmt);
        }
        xx_io_close(dev);
    }
    g_learned = 1;
    return g_readers;
}

bool xxfc_open(xxfc_opened *out, xx_io_device *device, int64_t base_address) {
    xxfc_reader_entry *table = xxfc_reader_table();
    xx_file_type_t type;
    size_t i;

    if (!out || !device) return false;
    out->format = NULL;
    out->release = NULL;
    out->reader_name = NULL;
    out->type = XX_FILE_TYPE_UNKNOWN;

    type = xx_format_get_file_type_device(device);
    out->type = type;
    /* BINARY is the detector's way of saying "bytes, but nothing I know", so
     * it names no format to route to. It is also what the few readers that
     * decide their real type only after parsing (elf, macho, pe, atarist)
     * answer at construction -- so without this, every unrecognised file
     * would be handed to whichever of those came first in the table. */
    if (type == XX_FILE_TYPE_UNKNOWN || type == XX_FILE_TYPE_BINARY)
        return false;

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
