/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Extra rows of the abstract image interface table (xx_image_props.c).
 * Each row: {XX_FILE_TYPE_x, "id", "Display name", "ext1;ext2", "mime/type",
 *            XX_IMAGE_IF_* flags, reader-or-NULL},
 * The readers live in xx_image_props_readers.inc, included before this file.
 */
#define XX_IMAGE_PROPS_EXTRA_ROWS \
    {XX_FILE_TYPE_ADOBE_ACO, "adobe_aco", "Adobe Color Swatch", "aco", "application/x-photoshop-color-swatch", XX_IMAGE_IF_PALETTE, NULL}, \
    {XX_FILE_TYPE_ADOBE_ACT, "adobe_act", "Adobe Color Table", "act", "", XX_IMAGE_IF_PALETTE, NULL}, \
    {XX_FILE_TYPE_ADOBE_ASE, "adobe_ase", "Adobe Swatch Exchange", "ase", "", XX_IMAGE_IF_PALETTE, NULL}, \
    {XX_FILE_TYPE_ALIAS_PIX, "alias_pix", "Alias PIX image", "pix;als;alias", "image/x-alias-pix", XX_IMAGE_IF_RASTER, ip_read_alias_pix}, \
    {XX_FILE_TYPE_AMIGA_DISKOBJECT, "amiga_diskobject", "Amiga Workbench DiskObject icon", "info", "", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_amiga_diskobject}, \
    {XX_FILE_TYPE_APPLE_PICT, "apple_pict", "Apple QuickDraw PICT", "pict;pct;pic", "image/x-pict", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_VECTOR, ip_read_apple_pict}, \
    {XX_FILE_TYPE_ASEPRITE, "aseprite", "Aseprite sprite animation", "ase;aseprite", "image/x-aseprite", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_aseprite}, \
    {XX_FILE_TYPE_ASTC_TEXTURE, "astc_texture", "Adaptive Scalable Texture Compression", "astc", "image/x-astc", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE, ip_read_astc_texture}, \
    {XX_FILE_TYPE_AUTOCAD_DXF, "autocad_dxf", "AutoCAD Drawing Exchange Format", "dxf", "image/vnd.dxf", XX_IMAGE_IF_VECTOR, NULL}, \
    {XX_FILE_TYPE_AVS_IMAGE, "avs_image", "AVS X image", "avs", "image/x-avs", XX_IMAGE_IF_RASTER, ip_read_avs_image}, \
    {XX_FILE_TYPE_BASIS_TEXTURE, "basis_texture", "Basis Universal texture", "basis", "image/x-basis", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI | XX_IMAGE_IF_LOSSY, ip_read_basis_texture}, \
    {XX_FILE_TYPE_BPG_IMAGE, "bpg_image", "Better Portable Graphics", "bpg", "image/x-bpg", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_LOSSY, ip_read_bpg_image}, \
    {XX_FILE_TYPE_CINEON, "cineon", "Kodak Cineon film image", "cin", "image/x-cineon", XX_IMAGE_IF_RASTER, ip_read_cineon}, \
    {XX_FILE_TYPE_DDS, "dds", "DirectDraw Surface", "dds", "image/x-dds", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI, ip_read_dds}, \
    {XX_FILE_TYPE_DEC_SIXEL, "dec_sixel", "DEC Sixel graphics", "six;sixel", "image/x-sixel", XX_IMAGE_IF_RASTER, ip_read_dec_sixel}, \
    {XX_FILE_TYPE_DPX, "dpx", "Digital Picture Exchange", "dpx", "image/x-dpx", XX_IMAGE_IF_RASTER, ip_read_dpx}, \
    {XX_FILE_TYPE_EA_FSH, "ea_fsh", "Electronic Arts FSH / SHPI image pack", "fsh;qfs", "", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI, ip_read_ea_fsh}, \
    {XX_FILE_TYPE_EMF, "emf", "Enhanced Metafile", "emf", "image/emf", XX_IMAGE_IF_VECTOR, ip_read_emf}, \
    {XX_FILE_TYPE_EVD, "evd", "EVD map image", "evd", "", XX_IMAGE_IF_RASTER, NULL}, \
    {XX_FILE_TYPE_FARBFELD, "farbfeld", "farbfeld 16-bit RGBA image", "ff", "image/x-farbfeld", XX_IMAGE_IF_RASTER, ip_read_farbfeld}, \
    {XX_FILE_TYPE_FUJIFILM_RAF, "fujifilm_raf", "Fujifilm CCD-RAW", "raf", "image/x-fuji-raf", XX_IMAGE_IF_RASTER, ip_read_fujifilm_raf}, \
    {XX_FILE_TYPE_GIMP_GBR, "gimp_gbr", "GIMP brush", "gbr", "image/x-gimp-gbr", XX_IMAGE_IF_RASTER, ip_read_gimp_gbr}, \
    {XX_FILE_TYPE_GIMP_GGR, "gimp_ggr", "GIMP gradient", "ggr", "image/x-gimp-ggr", XX_IMAGE_IF_PALETTE, NULL}, \
    {XX_FILE_TYPE_GIMP_GIH, "gimp_gih", "GIMP animated brush pipe", "gih", "image/x-gimp-gih", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, NULL}, \
    {XX_FILE_TYPE_GIMP_GPL, "gimp_gpl", "GIMP palette", "gpl", "text/x-gimp-palette", XX_IMAGE_IF_PALETTE, NULL}, \
    {XX_FILE_TYPE_GIMP_PAT, "gimp_pat", "GIMP pattern", "pat", "image/x-gimp-pat", XX_IMAGE_IF_RASTER, ip_read_gimp_pat}, \
    {XX_FILE_TYPE_GIMP_XCF, "gimp_xcf", "GIMP eXperimental Computing Facility", "xcf", "image/x-xcf", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_gimp_xcf}, \
    {XX_FILE_TYPE_GODOT_CTEX, "godot_ctex", "Godot stream texture", "ctex", "", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI, NULL}, \
    {XX_FILE_TYPE_HPGL_PLOT, "hpgl_plot", "HP-GL plotter drawing", "plt;hpgl", "application/vnd.hp-hpgl", XX_IMAGE_IF_VECTOR, NULL}, \
    {XX_FILE_TYPE_ICNS, "icns", "Apple Icon Image", "icns", "image/x-icns", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_icns}, \
    {XX_FILE_TYPE_ICO, "ico", "Windows Icon", "ico;cur", "image/x-icon", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_ico}, \
    {XX_FILE_TYPE_IFF_ILBM, "iff_ilbm", "Amiga IFF Interleaved Bitmap", "iff;ilbm;lbm;ham;ham8", "image/x-ilbm", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_iff_ilbm}, \
    {XX_FILE_TYPE_IMAGEMAGICK_MIFF, "imagemagick_miff", "ImageMagick Magick Image File Format", "miff;mif", "image/x-miff", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, NULL}, \
    {XX_FILE_TYPE_IMAGEMAGICK_MVG, "imagemagick_mvg", "ImageMagick Magick Vector Graphics", "mvg", "image/x-mvg", XX_IMAGE_IF_VECTOR, NULL}, \
    {XX_FILE_TYPE_JASC_PALETTE, "jasc_palette", "JASC Paint Shop Pro palette", "pal", "", XX_IMAGE_IF_PALETTE, NULL}, \
    {XX_FILE_TYPE_JBIG2, "jbig2", "Joint Bi-level Image Experts Group 2", "jb2;jbig2", "image/x-jbig2", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_jbig2}, \
    {XX_FILE_TYPE_JPEG2000_JP2, "jpeg2000_jp2", "JPEG 2000 Part 1 JP2 image", "jp2;j2k;jpx;jpm", "image/jp2", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_LOSSY, ip_read_jpeg2000_jp2}, \
    {XX_FILE_TYPE_JPEG2000_PGX, "jpeg2000_pgx", "JPEG 2000 VM PGX image", "pgx", "image/x-pgx", XX_IMAGE_IF_RASTER, ip_read_jpeg2000_pgx}, \
    {XX_FILE_TYPE_KHOROS_VIFF, "khoros_viff", "Khoros Visualization Image File Format", "xv;viff", "image/x-viff", XX_IMAGE_IF_RASTER, ip_read_khoros_viff}, \
    {XX_FILE_TYPE_KTX, "ktx", "Khronos Texture", "ktx", "image/ktx", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI, ip_read_ktx}, \
    {XX_FILE_TYPE_KTX2, "ktx2", "Khronos Texture 2.0", "ktx2", "image/ktx2", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI, ip_read_ktx2}, \
    {XX_FILE_TYPE_MINOLTA_MRW, "minolta_mrw", "Minolta DiMAGE RAW", "mrw", "image/x-minolta-mrw", XX_IMAGE_IF_RASTER, ip_read_minolta_mrw}, \
    {XX_FILE_TYPE_MTV_IMAGE, "mtv_image", "MTV raytracing image", "mtv", "image/x-mtv", XX_IMAGE_IF_RASTER, ip_read_mtv_image}, \
    {XX_FILE_TYPE_NETPBM_PFM, "netpbm_pfm", "Portable Float Map HDR image", "pfm", "image/x-portable-floatmap", XX_IMAGE_IF_RASTER, ip_read_netpbm_pfm}, \
    {XX_FILE_TYPE_NINTENDO_BCH, "nintendo_bch", "Nintendo 3DS Binary CTR HIERARCHY texture", "bch", "", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI, NULL}, \
    {XX_FILE_TYPE_NINTENDO_BNTX, "nintendo_bntx", "Nintendo Switch Binary NX Texture", "bntx", "", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI, ip_read_nintendo_bntx}, \
    {XX_FILE_TYPE_NINTENDO_TPL, "nintendo_tpl", "Nintendo GameCube / Wii Texture Palette", "tpl", "", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI, ip_read_nintendo_tpl}, \
    {XX_FILE_TYPE_NOKIA_OTA_BITMAP, "nokia_ota_bitmap", "Nokia Over The Air bitmap", "otb;ota", "image/vnd.nok-oplogo-color", XX_IMAGE_IF_RASTER, ip_read_nokia_ota_bitmap}, \
    {XX_FILE_TYPE_OPENEXR, "openexr", "ILM OpenEXR High Dynamic-Range image", "exr", "image/x-exr", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_openexr}, \
    {XX_FILE_TYPE_PAINTSHOP_PSP, "paintshop_psp", "Paint Shop Pro image", "psp;pspimage", "image/x-paintshoppro", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_paintshop_psp}, \
    {XX_FILE_TYPE_PALM_BITMAP, "palm_bitmap", "PalmOS bitmap image", "palm;prc;pdb", "image/x-palm", XX_IMAGE_IF_RASTER, ip_read_palm_bitmap}, \
    {XX_FILE_TYPE_PCX, "pcx", "ZSoft PC Paintbrush bitmap", "pcx;pcc", "image/x-pcx", XX_IMAGE_IF_RASTER, ip_read_pcx}, \
    {XX_FILE_TYPE_PHOTOSHOP_ABR, "photoshop_abr", "Adobe Photoshop brush collection", "abr", "application/x-photoshop-abr", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_photoshop_abr}, \
    {XX_FILE_TYPE_PHOTOSHOP_PAT, "photoshop_pat", "Adobe Photoshop pattern library", "pat", "application/x-photoshop-pat", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_photoshop_pat}, \
    {XX_FILE_TYPE_PHOTOSHOP_PSD, "photoshop_psd", "Adobe Photoshop Document", "psd;psb", "image/vnd.adobe.photoshop", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_photoshop_psd}, \
    {XX_FILE_TYPE_PKM_TEXTURE, "pkm_texture", "Ericsson ETC1 / ETC2 texture", "pkm", "image/x-pkm", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE, ip_read_pkm_texture}, \
    {XX_FILE_TYPE_PNM, "pnm", "Portable Anymap (PBM/PGM/PPM/PAM)", "pnm;pbm;pgm;ppm;pam", "image/x-portable-anymap", XX_IMAGE_IF_RASTER, ip_read_pnm}, \
    {XX_FILE_TYPE_PVR, "pvr", "PowerVR texture container", "pvr", "image/x-pvr", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI, ip_read_pvr}, \
    {XX_FILE_TYPE_PX, "px", "Pixel image", "px", "", XX_IMAGE_IF_RASTER, NULL}, \
    {XX_FILE_TYPE_QOI, "qoi", "Quite OK Image format", "qoi", "image/qoi", XX_IMAGE_IF_RASTER, ip_read_qoi}, \
    {XX_FILE_TYPE_QT_QPICTURE, "qt_qpicture", "Qt QPicture vector recording", "pic", "", XX_IMAGE_IF_VECTOR, NULL}, \
    {XX_FILE_TYPE_RADIANCE_HDR, "radiance_hdr", "Radiance RGBE HDR image", "hdr;pic;rgbe", "image/vnd.radiance", XX_IMAGE_IF_RASTER, ip_read_radiance_hdr}, \
    {XX_FILE_TYPE_RISC_OS_SPRITE, "risc_os_sprite", "Acorn RISC OS sprite area", "ff9;sprite", "", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_risc_os_sprite}, \
    {XX_FILE_TYPE_SEGA_GVR, "sega_gvr", "Sega GameCube GVR texture", "gvr", "", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE, ip_read_sega_gvr}, \
    {XX_FILE_TYPE_SEGA_PVR2, "sega_pvr2", "Sega Dreamcast PVR texture", "pvr", "", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE, ip_read_sega_pvr2}, \
    {XX_FILE_TYPE_SGI_RGB, "sgi_rgb", "Silicon Graphics RGB image", "sgi;rgb;rgba;bw", "image/x-sgi", XX_IMAGE_IF_RASTER, ip_read_sgi_rgb}, \
    {XX_FILE_TYPE_SIGMA_X3F, "sigma_x3f", "Sigma Foveon X3F RAW", "x3f", "image/x-sigma-x3f", XX_IMAGE_IF_RASTER, ip_read_sigma_x3f}, \
    {XX_FILE_TYPE_SOFTIMAGE_PIC, "softimage_pic", "Softimage PIC image", "pic;si", "image/x-softimage-pic", XX_IMAGE_IF_RASTER, ip_read_softimage_pic}, \
    {XX_FILE_TYPE_SONY_TIM, "sony_tim", "Sony PlayStation TIM texture", "tim", "image/x-sony-tim", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE, ip_read_sony_tim}, \
    {XX_FILE_TYPE_SONY_TIM2, "sony_tim2", "Sony PlayStation 2 TIM2 texture", "tm2;tim2", "image/x-sony-tim2", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI, ip_read_sony_tim2}, \
    {XX_FILE_TYPE_SUN_RASTER, "sun_raster", "Sun Rasterfile", "ras;sun", "image/x-sun-raster", XX_IMAGE_IF_RASTER, ip_read_sun_raster}, \
    {XX_FILE_TYPE_SVG, "svg", "Scalable Vector Graphics", "svg;svgz", "image/svg+xml", XX_IMAGE_IF_VECTOR, ip_read_svg}, \
    {XX_FILE_TYPE_TGA, "tga", "Truevision Targa image", "tga;vda;icb;vst", "image/x-tga", XX_IMAGE_IF_RASTER, ip_read_tga}, \
    {XX_FILE_TYPE_TIFF, "tiff", "Tagged Image File Format", "tif;tiff", "image/tiff", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_tiff}, \
    {XX_FILE_TYPE_UTAH_RLE, "utah_rle", "Utah Raster Toolkit RLE image", "rle", "image/x-rle", XX_IMAGE_IF_RASTER, ip_read_utah_rle}, \
    {XX_FILE_TYPE_VALVE_VTF, "valve_vtf", "Valve Source VTF texture", "vtf", "image/x-vtf", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_TEXTURE | XX_IMAGE_IF_MULTI, ip_read_valve_vtf}, \
    {XX_FILE_TYPE_WBMP_IMAGE, "wbmp_image", "WAP Wireless Bitmap", "wbmp;wbm", "image/vnd.wap.wbmp", XX_IMAGE_IF_RASTER, ip_read_wbmp_image}, \
    {XX_FILE_TYPE_WINDOWS_ANI, "windows_ani", "Windows Animated Cursor", "ani", "application/x-navi-animation", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_windows_ani}, \
    {XX_FILE_TYPE_WMF, "wmf", "Windows Metafile", "wmf", "image/wmf", XX_IMAGE_IF_VECTOR, ip_read_wmf}, \
    {XX_FILE_TYPE_WORDPERFECT_WPG, "wordperfect_wpg", "WordPerfect Graphics", "wpg", "application/x-wordperfect-graphics", XX_IMAGE_IF_VECTOR | XX_IMAGE_IF_RASTER, NULL}, \
    {XX_FILE_TYPE_X11_XBM, "x11_xbm", "X11 Bitmap", "xbm;bm", "image/x-xbitmap", XX_IMAGE_IF_RASTER, ip_read_x11_xbm}, \
    {XX_FILE_TYPE_XCURSOR, "xcursor", "X11 Cursor", "cur;xcur", "image/x-xcursor", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_xcursor}, \
    {XX_FILE_TYPE_XFIG, "xfig", "Xfig vector drawing", "fig", "image/x-xfig", XX_IMAGE_IF_VECTOR, NULL}, \
    {XX_FILE_TYPE_XPM, "xpm", "X11 Pixmap", "xpm;pm", "image/x-xpixmap", XX_IMAGE_IF_RASTER, ip_read_xpm}, \
    {XX_FILE_TYPE_XWD, "xwd", "X Window Dump", "xwd;xdmp", "image/x-xwindowdump", XX_IMAGE_IF_RASTER, ip_read_xwd}
