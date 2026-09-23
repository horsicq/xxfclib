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

/**
 * @file xxfc_defs.h
 * @brief Common definitions, error codes, and macros for xxfclib.
 */

#ifndef XXFC_DEFS_H
#define XXFC_DEFS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Library Version */
#define XXFCLIB_VERSION_MAJOR 0
#define XXFCLIB_VERSION_MINOR 1
#define XXFCLIB_VERSION_PATCH 0
#define XXFCLIB_VERSION_STRING "0.1.0"

/* Status / Return Codes */
typedef enum xxfc_status_e {
    XXFC_OK                 =  0,   /**< Operation successful */
    XXFC_ERR_GENERIC        = -1,   /**< General / unknown error */
    XXFC_ERR_NULL_PARAM     = -2,   /**< Null pointer argument passed */
    XXFC_ERR_OUT_OF_MEMORY  = -3,   /**< Memory allocation failure */
    XXFC_ERR_OUT_OF_BOUNDS  = -4,   /**< Index or offset exceeds buffer range */
    XXFC_ERR_INVALID_ARG    = -5,   /**< Invalid argument or state */
    XXFC_ERR_READ_ONLY      = -6,   /**< Modification attempted on a read-only view */
    XXFC_ERR_IO             = -7    /**< Input/output or file access failure */
} xxfc_status_t;

/* File Types */
typedef enum xx_file_type_e {
    XX_FILE_TYPE_UNKNOWN = 0,
    XX_FILE_TYPE_BINARY  = 1,
    XX_FILE_TYPE_ZIP     = 2,
    XX_FILE_TYPE_ZIP64   = 3,
    XX_FILE_TYPE_7ZIP    = 4,
    XX_FILE_TYPE_RAR     = 5,
    XX_FILE_TYPE_AR      = 6,
    XX_FILE_TYPE_BZ2     = 7,
    XX_FILE_TYPE_GZ      = 8,
    XX_FILE_TYPE_XZ      = 9,
    XX_FILE_TYPE_TAR     = 10,
    XX_FILE_TYPE_JAR     = 11,
    XX_FILE_TYPE_APK     = 12,
    XX_FILE_TYPE_TAR_GZ  = 13,
    XX_FILE_TYPE_TAR_BZ2 = 14,
    XX_FILE_TYPE_TAR_XZ  = 15,
    XX_FILE_TYPE_IPA     = 16,
    XX_FILE_TYPE_NPM     = 17,
    XX_FILE_TYPE_ISO9660 = 18,
    XX_FILE_TYPE_TAR_LZ4 = 19,
    XX_FILE_TYPE_ACE = 20,
    XX_FILE_TYPE_AIN = 21,
    XX_FILE_TYPE_ALDUS = 22,
    XX_FILE_TYPE_ALZ = 23,
    XX_FILE_TYPE_AMPK = 24,
    XX_FILE_TYPE_AODOS = 25,
    XX_FILE_TYPE_ARCFS = 26,
    XX_FILE_TYPE_PDP11AR = 27,
    XX_FILE_TYPE_ARTIPACK = 28,
    XX_FILE_TYPE_ARCV4 = 29,
    XX_FILE_TYPE_ARCV2 = 30,
    XX_FILE_TYPE_WARC = 31,
    XX_FILE_TYPE_TAR_ZSTD = 32,
    XX_FILE_TYPE_PE32 = 33,
    XX_FILE_TYPE_PE64 = 34,
    XX_FILE_TYPE_CPIO = 35,
    XX_FILE_TYPE_MTREE = 36,
    XX_FILE_TYPE_TAR_NEXTSTEP = 37,
    XX_FILE_TYPE_TAR_COMPRESS = 38,
    XX_FILE_TYPE_TAR_LZIP = 39,
    XX_FILE_TYPE_TAR_LZMA = 40,
    XX_FILE_TYPE_TAR_LZOP = 41,
    XX_FILE_TYPE_TARX1 = 42,
    XX_FILE_TYPE_TARX2 = 43,
    XX_FILE_TYPE_MSDOS = 44,
    XX_FILE_TYPE_ARJ = 45,
    XX_FILE_TYPE_CAB = 46,
    XX_FILE_TYPE_AIXBFF = 47,
    XX_FILE_TYPE_ARX = 48,
    XX_FILE_TYPE_LZIP = 49,
    XX_FILE_TYPE_ELF32 = 50,
    XX_FILE_TYPE_ELF64 = 51,
    XX_FILE_TYPE_MACHO32 = 52,
    XX_FILE_TYPE_MACHO64 = 53,
    XX_FILE_TYPE_NE = 54,
    XX_FILE_TYPE_LE = 55,
    XX_FILE_TYPE_LX = 56,
    XX_FILE_TYPE_DEX = 57,
    XX_FILE_TYPE_LZMA = 58,
    XX_FILE_TYPE_ZSTD = 59,
    XX_FILE_TYPE_LZ4 = 60,
    XX_FILE_TYPE_LZ5 = 61,
    XX_FILE_TYPE_LIZARD = 62,
    XX_FILE_TYPE_BROTLI = 63,
    XX_FILE_TYPE_UNIX_PACK = 64,
    XX_FILE_TYPE_ZLIB = 65,
    XX_FILE_TYPE_UNIX_COMPRESS = 66,
    XX_FILE_TYPE_GIT_OBJECT = 67,
    XX_FILE_TYPE_MS_COMPRESS = 68,
    XX_FILE_TYPE_ASH0 = 69,
    XX_FILE_TYPE_WII_LZ77 = 70,
    XX_FILE_TYPE_LZV1 = 71,
    XX_FILE_TYPE_ORACLE_SQUEEZE = 72,
    XX_FILE_TYPE_SOFTRONICS = 73,
    XX_FILE_TYPE_LOGITECH_COMPRESS = 74,
    XX_FILE_TYPE_DMA_PACKED = 75,
    XX_FILE_TYPE_GAS_HUFF = 76,
    XX_FILE_TYPE_HUF = 77,
    XX_FILE_TYPE_LZDIET = 78,
    XX_FILE_TYPE_LZPIS2 = 79,
    XX_FILE_TYPE_BCM = 80,
    XX_FILE_TYPE_LPAQ8 = 81,
    XX_FILE_TYPE_PEA = 82,
    XX_FILE_TYPE_ZPAQ = 83,
    XX_FILE_TYPE_FREEARC = 84,
    XX_FILE_TYPE_AP4 = 85,
    XX_FILE_TYPE_ARQ = 86,
    XX_FILE_TYPE_ASAR = 87,
    XX_FILE_TYPE_ASCEND = 88,
    XX_FILE_TYPE_BIGF = 89,
    XX_FILE_TYPE_MARC = 90,
    XX_FILE_TYPE_ZFSF = 91,
    XX_FILE_TYPE_PACKIT = 92,
    XX_FILE_TYPE_TWS = 93,
    XX_FILE_TYPE_BIGAF = 94,
    XX_FILE_TYPE_CRU = 95,
    XX_FILE_TYPE_FRONTPAGETHEME = 96,
    XX_FILE_TYPE_HLB = 97,
    XX_FILE_TYPE_IRIXSA = 98,
    XX_FILE_TYPE_JAM = 99,
    XX_FILE_TYPE_KRML = 100,
    XX_FILE_TYPE_LBRCOBOL = 101,
    XX_FILE_TYPE_MINIDUMP = 102,
    XX_FILE_TYPE_POWERBOARDBBS = 103,
    XX_FILE_TYPE_SCI = 104,
    XX_FILE_TYPE_SEADATA = 105,
    XX_FILE_TYPE_SECONDNATURE = 106,
    XX_FILE_TYPE_SOS = 107,
    XX_FILE_TYPE_SW = 108,
    XX_FILE_TYPE_SWAGPACKET = 109,
    XX_FILE_TYPE_TRCPAK = 110,
    XX_FILE_TYPE_CFL = 111,
    XX_FILE_TYPE_DPK = 112,
    XX_FILE_TYPE_DSL2 = 113,
    XX_FILE_TYPE_DTPACKED = 114,
    XX_FILE_TYPE_FIZ = 115,
    XX_FILE_TYPE_FLD = 116,
    XX_FILE_TYPE_IBMZPAK = 117,
    XX_FILE_TYPE_IGF1 = 118,
    XX_FILE_TYPE_IGF2 = 119,
    XX_FILE_TYPE_INTEDUFT = 120,
    XX_FILE_TYPE_JM93 = 121,
    XX_FILE_TYPE_LSZ = 122,
    XX_FILE_TYPE_MIZ = 123,
    XX_FILE_TYPE_MVA = 124,
    XX_FILE_TYPE_POVLABLZH = 125,
    XX_FILE_TYPE_POWERARC = 126,
    XX_FILE_TYPE_QIP1 = 127,
    XX_FILE_TYPE_QUARTERDECKQP = 128,
    XX_FILE_TYPE_RCF = 129,
    XX_FILE_TYPE_RIVERSOFT = 130,
    XX_FILE_TYPE_SWAG = 131,
    XX_FILE_TYPE_TGCF = 132,
    XX_FILE_TYPE_TRC = 133,
    XX_FILE_TYPE_ZLWB = 134,
    XX_FILE_TYPE_ZZ = 135,
    XX_FILE_TYPE_ZZZ = 136,
    XX_FILE_TYPE_JGPAK = 137,
    XX_FILE_TYPE_BORLANDPACK = 138,
    XX_FILE_TYPE_ECMPACKED = 139,
    XX_FILE_TYPE_JETBBS = 140,
    XX_FILE_TYPE_QUALITAS = 141,
    XX_FILE_TYPE_BWF = 142,
    XX_FILE_TYPE_ZAP = 143,
    XX_FILE_TYPE_STORK = 144,
    XX_FILE_TYPE_ASCENDBACKUP = 145,
    XX_FILE_TYPE_PYZ = 146,
    XX_FILE_TYPE_FMC1 = 147,
    XX_FILE_TYPE_XAR = 148,
    XX_FILE_TYPE_LHA = 149,
    XX_FILE_TYPE_SPIS = 150,
    XX_FILE_TYPE_AMIGALZX = 151,
    XX_FILE_TYPE_SEAARC = 152,
    XX_FILE_TYPE_AMIGAHUNK = 153,
    XX_FILE_TYPE_ATARIST = 154,
    XX_FILE_TYPE_DOS16M = 155,
    XX_FILE_TYPE_DOS4G = 156,
    XX_FILE_TYPE_COM = 157,
    XX_FILE_TYPE_ASYMETRIX = 158,
    XX_FILE_TYPE_BWCF = 159,
    XX_FILE_TYPE_CHIEFLZ = 160,
    XX_FILE_TYPE_CHIEFLZMULTI = 161,
    XX_FILE_TYPE_CLP = 162,
    XX_FILE_TYPE_CMP = 163,
    XX_FILE_TYPE_DISKDOUBLER = 164,
    XX_FILE_TYPE_EA = 165,
    XX_FILE_TYPE_EALIB = 166,
    XX_FILE_TYPE_EAREFPACK = 167,
    XX_FILE_TYPE_FLS = 168,
    XX_FILE_TYPE_GENIUS = 169,
    XX_FILE_TYPE_HA = 170,
    XX_FILE_TYPE_HZL = 171,
    XX_FILE_TYPE_KBOOM = 172,
    XX_FILE_TYPE_LZHCXP = 173,
    XX_FILE_TYPE_LZWD = 174,
    XX_FILE_TYPE_MI10 = 175,
    XX_FILE_TYPE_NPACK = 176,
    XX_FILE_TYPE_PAKLEO = 177,
    XX_FILE_TYPE_SCL = 178,
    XX_FILE_TYPE_ZCMP = 179,
    XX_FILE_TYPE_ZPAK = 180,
    XX_FILE_TYPE_NETWAREPACKED = 181,
    XX_FILE_TYPE_ZTC = 182,
    XX_FILE_TYPE_GLU = 183,
    XX_FILE_TYPE_GTU = 184,
    XX_FILE_TYPE_IBMSPACK = 185,
    XX_FILE_TYPE_JBF = 186,
    XX_FILE_TYPE_PCOMMOS2 = 187,
    XX_FILE_TYPE_STK = 188,
    XX_FILE_TYPE_TERSE = 189,
    XX_FILE_TYPE_ZOO = 190,
    XX_FILE_TYPE_SQX = 191,
    XX_FILE_TYPE_IMP = 192,
    XX_FILE_TYPE_COMPACTPRO = 193,
    XX_FILE_TYPE_HAP = 194,
    XX_FILE_TYPE_IRWINPAC = 195,
    XX_FILE_TYPE_IVT = 196,
    XX_FILE_TYPE_KOLIBRIKPACK = 197,
    XX_FILE_TYPE_LIM = 198,
    XX_FILE_TYPE_LOFI = 199,
    XX_FILE_TYPE_PKT = 200,
    XX_FILE_TYPE_QDA = 201,
    XX_FILE_TYPE_QNXBASE = 202,
    XX_FILE_TYPE_RID = 203,
    XX_FILE_TYPE_ROMPAQ = 204,
    XX_FILE_TYPE_RTA = 205,
    XX_FILE_TYPE_RTPATCH = 206,
    XX_FILE_TYPE_STYLUS = 207,
    XX_FILE_TYPE_TI99ARC = 208,
    XX_FILE_TYPE_TIVOLI = 209,
    XX_FILE_TYPE_VMARC = 210,
    XX_FILE_TYPE_WINTERSOFT = 211,
    XX_FILE_TYPE_WPK = 212,
    XX_FILE_TYPE_XEDITPACK = 213,
    XX_FILE_TYPE_ZIE = 214,
    XX_FILE_TYPE_SQUASHFS = 215,
    XX_FILE_TYPE_NTFS = 216,
    XX_FILE_TYPE_UDF = 217,
    XX_FILE_TYPE_ROMFS = 218,
    XX_FILE_TYPE_SQZ = 219,
    XX_FILE_TYPE_TOPSPEED = 220,
    XX_FILE_TYPE_TPS = 221,
    XX_FILE_TYPE_ULEAD = 222,
    XX_FILE_TYPE_QUANTUM = 223,
    XX_FILE_TYPE_ZXZIP = 224,
    XX_FILE_TYPE_ZOOM = 225,
    XX_FILE_TYPE_SFPACK = 226,
    XX_FILE_TYPE_CLAYLZ = 227,
    XX_FILE_TYPE_C64WRAPTOR = 228,
    XX_FILE_TYPE_CORELLTEC = 229,
    XX_FILE_TYPE_PCSECURE = 230,
    XX_FILE_TYPE_RSVK = 231,
    XX_FILE_TYPE_RAW_LZW15V = 232,
    XX_FILE_TYPE_SAF = 233,
    XX_FILE_TYPE_SLS = 234,
    XX_FILE_TYPE_NID = 235,
    XX_FILE_TYPE_GAMOS = 236,
    XX_FILE_TYPE_PANORAMA = 237,
    XX_FILE_TYPE_FPAK = 238,
    XX_FILE_TYPE_CRAMFS = 239,
    XX_FILE_TYPE_JFFS2 = 240,
    XX_FILE_TYPE_YAFFS = 241,
    XX_FILE_TYPE_UBI = 242,
    XX_FILE_TYPE_UBIFS = 243,
    XX_FILE_TYPE_EXT = 244,
    XX_FILE_TYPE_FAT = 245,
    XX_FILE_TYPE_MBR = 246,
    XX_FILE_TYPE_GPT = 247,
    XX_FILE_TYPE_SPARSE = 248,
    XX_FILE_TYPE_UIMAGE = 249,
    XX_FILE_TYPE_DTB = 250,
    XX_FILE_TYPE_TRX = 251,
    XX_FILE_TYPE_SEAMA = 252,
    XX_FILE_TYPE_CHK = 253,
    XX_FILE_TYPE_PACKIMG = 254,
    XX_FILE_TYPE_DLOB = 255,
    XX_FILE_TYPE_WINCE = 256,
    XX_FILE_TYPE_BINHDR = 257,
    XX_FILE_TYPE_RTK = 258,
    XX_FILE_TYPE_CSMAN = 259,
    XX_FILE_TYPE_VXWORKS = 260,
    XX_FILE_TYPE_UEFI_FV = 261,
    XX_FILE_TYPE_UEFI_CAPSULE = 262,
    XX_FILE_TYPE_QCOW = 263,
    XX_FILE_TYPE_QNX6 = 264,
    XX_FILE_TYPE_LUKS = 265,
    XX_FILE_TYPE_APFS = 266,
    XX_FILE_TYPE_BTRFS = 267,
    XX_FILE_TYPE_LOGFS = 268,
    XX_FILE_TYPE_DMG = 269,
    XX_FILE_TYPE_DMS = 270,
    XX_FILE_TYPE_RESOURCEFORK = 271,
    XX_FILE_TYPE_APPLESINGLE = 272,
    XX_FILE_TYPE_MACBINARY = 273,
    XX_FILE_TYPE_PP20 = 274,
    XX_FILE_TYPE_BEATTHEHOUSE = 275,
    XX_FILE_TYPE_KPCK = 276,
    XX_FILE_TYPE_BATTLEISLE = 277,
    XX_FILE_TYPE_PERFORM = 278,
    XX_FILE_TYPE_MATHCAD = 279,
    XX_FILE_TYPE_NETWARE2 = 280,
    XX_FILE_TYPE_SHAR = 281,
    XX_FILE_TYPE_RNC = 282,
    XX_FILE_TYPE_IBMPACK = 283,
    XX_FILE_TYPE_CAZIP = 284,
    XX_FILE_TYPE_TPWM = 285,
    XX_FILE_TYPE_MRNZ = 286,
    XX_FILE_TYPE_EDC = 287,
    XX_FILE_TYPE_MXS = 288,
    XX_FILE_TYPE_XORARCHIVE = 289,
    XX_FILE_TYPE_MWAVE = 290,
    XX_FILE_TYPE_FINEAR = 291,
    XX_FILE_TYPE_GST = 292,
    XX_FILE_TYPE_WINLINK = 293,
    XX_FILE_TYPE_FTCOMP = 294,
    XX_FILE_TYPE_GPFPACK = 295,
    XX_FILE_TYPE_SCO = 296,
    XX_FILE_TYPE_UNIX_COMPACT = 297,
    XX_FILE_TYPE_PSDC = 298,
    XX_FILE_TYPE_IS3 = 299,
    XX_FILE_TYPE_IS5 = 300,
    XX_FILE_TYPE_IS7INX = 301,
    XX_FILE_TYPE_EDILZSS = 302,
    XX_FILE_TYPE_SAVEDSKF = 303,
    XX_FILE_TYPE_GOB = 304,
    XX_FILE_TYPE_DEBUGSCR = 305,
    XX_FILE_TYPE_DCLFT = 306,
    XX_FILE_TYPE_STUFFIT = 307,
    XX_FILE_TYPE_BINARYII = 308,
    XX_FILE_TYPE_BINHEX = 309,
    XX_FILE_TYPE_PMA = 310,
    XX_FILE_TYPE_LZK00 = 311,
    XX_FILE_TYPE_COMPAQLZH = 312,
    XX_FILE_TYPE_ARCV = 313,
    XX_FILE_TYPE_LIFKD = 314,
    XX_FILE_TYPE_TRDOS = 315,
    XX_FILE_TYPE_SQUEEZE1 = 316,
    XX_FILE_TYPE_IZPACK = 317,
    XX_FILE_TYPE_IS11 = 318,
    XX_FILE_TYPE_GKSETUP = 319,
    XX_FILE_TYPE_PCINSTALL = 320,
    XX_FILE_TYPE_COPYQM = 321,
    XX_FILE_TYPE_TELEDISK = 322,
    XX_FILE_TYPE_HFE = 323,
    XX_FILE_TYPE_FDI = 324,
    XX_FILE_TYPE_TWOIMG = 325,
    XX_FILE_TYPE_IMD = 326,
    XX_FILE_TYPE_DISKDUPE = 327,
    XX_FILE_TYPE_PMDISKCOPY = 328,
    XX_FILE_TYPE_DISKJUGGLER = 329,
    XX_FILE_TYPE_COPYQMEXE = 330,
    XX_FILE_TYPE_PAX = 331,
    XX_FILE_TYPE_SOLARISPKG = 332,
    XX_FILE_TYPE_BEOSPKG = 333,
    XX_FILE_TYPE_VMSPCSI = 334,
    XX_FILE_TYPE_VMSDB = 335,
    XX_FILE_TYPE_PCXLIB = 336,
    XX_FILE_TYPE_HOG2 = 337,
    XX_FILE_TYPE_SINNER = 338,
    XX_FILE_TYPE_PSN = 339,
    XX_FILE_TYPE_GRASP = 340,
    XX_FILE_TYPE_MEGATECHVOL = 341,
    XX_FILE_TYPE_STUNTS = 342,
    XX_FILE_TYPE_NOTETAB = 343,
    XX_FILE_TYPE_MCC = 344,
    XX_FILE_TYPE_TNEF = 345,
    XX_FILE_TYPE_OPC = 346,
    XX_FILE_TYPE_QRST = 347,
    XX_FILE_TYPE_PAIN = 348,
    XX_FILE_TYPE_XLAS = 349,
    XX_FILE_TYPE_MDCD = 350,
    XX_FILE_TYPE_SSM = 351,
    XX_FILE_TYPE_BVRP = 352,
    XX_FILE_TYPE_BCW = 353,
    XX_FILE_TYPE_SCF = 354,
    XX_FILE_TYPE_RECOGNITA = 355,
    XX_FILE_TYPE_JASC = 356,
    XX_FILE_TYPE_BINDER = 357,
    XX_FILE_TYPE_CSIDOS = 358,
    XX_FILE_TYPE_CAT = 359,
    XX_FILE_TYPE_BND = 360,
    XX_FILE_TYPE_SMSIPAK = 361,
    XX_FILE_TYPE_CPX = 362,
    XX_FILE_TYPE_DISKEXPRESS = 363,
    XX_FILE_TYPE_RED = 364,
    XX_FILE_TYPE_SHRINKWRAP = 365,
    /* CPX v4 shares the CPX container and reader; a distinct id because the
     * two are reported separately. */
    XX_FILE_TYPE_CPX4 = 366,
    XX_FILE_TYPE_GXL = 367,
    XX_FILE_TYPE_AIAFF = 368,
    XX_FILE_TYPE_SOFTPAQ2 = 369,
    XX_FILE_TYPE_WIM = 370,
    XX_FILE_TYPE_VHDDYNAMIC = 371,
    XX_FILE_TYPE_VMDK = 372,
    XX_FILE_TYPE_CISO = 373,
    XX_FILE_TYPE_COPYDISK = 374,
    XX_FILE_TYPE_HDCOPY = 375,
    XX_FILE_TYPE_APRICOT = 376,
    XX_FILE_TYPE_SABDU = 377,
    XX_FILE_TYPE_MPQ = 378,
    XX_FILE_TYPE_PHAR = 379,
    XX_FILE_TYPE_SQ = 380,
    XX_FILE_TYPE_SQUEEZE2 = 381,
    XX_FILE_TYPE_DBZ = 382,
    XX_FILE_TYPE_STAC = 383,
    XX_FILE_TYPE_SPK = 384,
    XX_FILE_TYPE_WRZL = 385,
    XX_FILE_TYPE_BAGF = 386,
    XX_FILE_TYPE_EMT = 387,
    XX_FILE_TYPE_QIP2 = 388,
    XX_FILE_TYPE_LIF = 389,
    XX_FILE_TYPE_IXA = 390,
    XX_FILE_TYPE_LSPACK10 = 391,
    XX_FILE_TYPE_STARKIT = 392,
    XX_FILE_TYPE_PAPERPORT = 393,
    XX_FILE_TYPE_RNCA = 394,
    XX_FILE_TYPE_HOG = 396,
    XX_FILE_TYPE_AGIS = 397,
    XX_FILE_TYPE_VOLITIONVPFT = 398,
    XX_FILE_TYPE_WINTERMUTEDCP = 399,
    XX_FILE_TYPE_BSN = 400,
    XX_FILE_TYPE_RES = 401,
    XX_FILE_TYPE_RSC = 402,
    XX_FILE_TYPE_TEACY = 403,
    XX_FILE_TYPE_SETTLERSFT = 404,
    XX_FILE_TYPE_WOLFFT = 405,
    XX_FILE_TYPE_BOO = 406,
    XX_FILE_TYPE_VMSSAVESET = 407,
    XX_FILE_TYPE_RAWSTAC = 410,
    XX_FILE_TYPE_ANDROIDBOOT = 411,
    XX_FILE_TYPE_ARCADYAN = 412,
    XX_FILE_TYPE_AUTEL = 413,
    XX_FILE_TYPE_DKBS = 414,
    XX_FILE_TYPE_DLINK_TLV = 415,
    XX_FILE_TYPE_DLKE = 416,
    XX_FILE_TYPE_ECOS = 417,
    XX_FILE_TYPE_ENCFW = 418,
    XX_FILE_TYPE_ENCRPTED_IMG = 419,
    XX_FILE_TYPE_JBOOT = 420,
    XX_FILE_TYPE_LINGVOARC = 421,
    XX_FILE_TYPE_LZ4DEMO = 422,
    XX_FILE_TYPE_MATTER_OTA = 423,
    XX_FILE_TYPE_MH01 = 424,
    XX_FILE_TYPE_SHRS = 425,
    XX_FILE_TYPE_SILMARILSFT = 426,
    XX_FILE_TYPE_TPLINK = 427,
    XX_FILE_TYPE_TWRX = 428,
    XX_FILE_TYPE_UBOOT_ENV = 429,
    XX_FILE_TYPE_INFOGRAMESFT = 430,
    XX_FILE_TYPE_PDB = 431,
    XX_FILE_TYPE_XPAK = 432,
    XX_FILE_TYPE_DCLRAW = 433,
    XX_FILE_TYPE_SREC = 434,
    XX_FILE_TYPE_LZOP = 435
} xx_file_type_t;

typedef xx_file_type_t xx_file_type;
typedef xx_file_type_t FILE_TYPE;

#ifndef ZIP
#define ZIP   XX_FILE_TYPE_ZIP
#endif
#ifndef ZIP64
#define ZIP64 XX_FILE_TYPE_ZIP64
#endif
#ifndef JAR
#define JAR   XX_FILE_TYPE_JAR
#endif
#ifndef APK
#define APK   XX_FILE_TYPE_APK
#endif
#ifndef IPA
#define IPA   XX_FILE_TYPE_IPA
#endif
#ifndef NPM
#define NPM   XX_FILE_TYPE_NPM
#endif
#ifndef TAR_GZ
#define TAR_GZ XX_FILE_TYPE_TAR_GZ
#endif
#ifndef TAR_BZ2
#define TAR_BZ2 XX_FILE_TYPE_TAR_BZ2
#endif
#ifndef TAR_XZ
#define TAR_XZ XX_FILE_TYPE_TAR_XZ
#endif
#ifndef ISO9660
#define ISO9660 XX_FILE_TYPE_ISO9660
#endif
#ifndef TAR_LZ4
#define TAR_LZ4 XX_FILE_TYPE_TAR_LZ4
#endif
#ifndef TAR_ZSTD
#define TAR_ZSTD XX_FILE_TYPE_TAR_ZSTD
#endif
#ifndef CPIO
#define CPIO XX_FILE_TYPE_CPIO
#endif
#ifndef MTREE
#define MTREE XX_FILE_TYPE_MTREE
#endif
#ifndef TAR_NEXTSTEP
#define TAR_NEXTSTEP XX_FILE_TYPE_TAR_NEXTSTEP
#endif
#ifndef TAR_COMPRESS
#define TAR_COMPRESS XX_FILE_TYPE_TAR_COMPRESS
#endif
#ifndef TAR_LZIP
#define TAR_LZIP XX_FILE_TYPE_TAR_LZIP
#endif
#ifndef TAR_LZMA
#define TAR_LZMA XX_FILE_TYPE_TAR_LZMA
#endif
#ifndef TAR_LZOP
#define TAR_LZOP XX_FILE_TYPE_TAR_LZOP
#endif
#ifndef TARX1
#define TARX1 XX_FILE_TYPE_TARX1
#endif
#ifndef TARX2
#define TARX2 XX_FILE_TYPE_TARX2
#endif
#ifndef MSDOS
#define MSDOS XX_FILE_TYPE_MSDOS
#endif
#ifndef ACE
#define ACE XX_FILE_TYPE_ACE
#endif
#ifndef PE32
#define PE32 XX_FILE_TYPE_PE32
#endif
#ifndef PE64
#define PE64 XX_FILE_TYPE_PE64
#endif
#ifndef ARJ
#define ARJ XX_FILE_TYPE_ARJ
#endif
#ifndef CAB
#define CAB XX_FILE_TYPE_CAB
#endif
#ifndef AIXBFF
#define AIXBFF XX_FILE_TYPE_AIXBFF
#endif
#ifndef ARX
#define ARX XX_FILE_TYPE_ARX
#endif
#ifndef LZIP
#define LZIP XX_FILE_TYPE_LZIP
#endif
#ifndef LZMA
#define LZMA XX_FILE_TYPE_LZMA
#endif
#ifndef ZSTD
#define ZSTD XX_FILE_TYPE_ZSTD
#endif
#ifndef LZ4
#define LZ4 XX_FILE_TYPE_LZ4
#endif
#ifndef LZ5
#define LZ5 XX_FILE_TYPE_LZ5
#endif
#ifndef LIZARD
#define LIZARD XX_FILE_TYPE_LIZARD
#endif
#ifndef BROTLI
#define BROTLI XX_FILE_TYPE_BROTLI
#endif
#ifndef UNIX_PACK
#define UNIX_PACK XX_FILE_TYPE_UNIX_PACK
#endif
#ifndef ZLIB
#define ZLIB XX_FILE_TYPE_ZLIB
#endif
#ifndef UNIX_COMPRESS
#define UNIX_COMPRESS XX_FILE_TYPE_UNIX_COMPRESS
#endif
#ifndef GIT_OBJECT
#define GIT_OBJECT XX_FILE_TYPE_GIT_OBJECT
#endif
#ifndef MS_COMPRESS
#define MS_COMPRESS XX_FILE_TYPE_MS_COMPRESS
#endif
#ifndef ASH0
#define ASH0 XX_FILE_TYPE_ASH0
#endif
#ifndef WII_LZ77
#define WII_LZ77 XX_FILE_TYPE_WII_LZ77
#endif
#ifndef LZV1
#define LZV1 XX_FILE_TYPE_LZV1
#endif
#ifndef ORACLE_SQUEEZE
#define ORACLE_SQUEEZE XX_FILE_TYPE_ORACLE_SQUEEZE
#endif
#ifndef SOFTRONICS
#define SOFTRONICS XX_FILE_TYPE_SOFTRONICS
#endif
#ifndef LOGITECH_COMPRESS
#define LOGITECH_COMPRESS XX_FILE_TYPE_LOGITECH_COMPRESS
#endif
#ifndef DMA_PACKED
#define DMA_PACKED XX_FILE_TYPE_DMA_PACKED
#endif
#ifndef GAS_HUFF
#define GAS_HUFF XX_FILE_TYPE_GAS_HUFF
#endif
#ifndef HUF
#define HUF XX_FILE_TYPE_HUF
#endif
#ifndef LZDIET
#define LZDIET XX_FILE_TYPE_LZDIET
#endif
#ifndef LZPIS2
#define LZPIS2 XX_FILE_TYPE_LZPIS2
#endif
#ifndef BCM
#define BCM XX_FILE_TYPE_BCM
#endif
#ifndef LPAQ8
#define LPAQ8 XX_FILE_TYPE_LPAQ8
#endif
#ifndef PEA
#define PEA XX_FILE_TYPE_PEA
#endif
#ifndef ZPAQ
#define ZPAQ XX_FILE_TYPE_ZPAQ
#endif
#ifndef FREEARC
#define FREEARC XX_FILE_TYPE_FREEARC
#endif
#ifndef AP4
#define AP4 XX_FILE_TYPE_AP4
#endif
#ifndef ARQ
#define ARQ XX_FILE_TYPE_ARQ
#endif
#ifndef ASAR
#define ASAR XX_FILE_TYPE_ASAR
#endif
#ifndef ASCEND
#define ASCEND XX_FILE_TYPE_ASCEND
#endif
#ifndef BIGF
#define BIGF XX_FILE_TYPE_BIGF
#endif
#ifndef MARC
#define MARC XX_FILE_TYPE_MARC
#endif
#ifndef ZFSF
#define ZFSF XX_FILE_TYPE_ZFSF
#endif
#ifndef PACKIT
#define PACKIT XX_FILE_TYPE_PACKIT
#endif
#ifndef TWS
#define TWS XX_FILE_TYPE_TWS
#endif
#ifndef BIGAF
#define BIGAF XX_FILE_TYPE_BIGAF
#endif
#ifndef CRU
#define CRU XX_FILE_TYPE_CRU
#endif
#ifndef FRONTPAGETHEME
#define FRONTPAGETHEME XX_FILE_TYPE_FRONTPAGETHEME
#endif
#ifndef HLB
#define HLB XX_FILE_TYPE_HLB
#endif
#ifndef IRIXSA
#define IRIXSA XX_FILE_TYPE_IRIXSA
#endif
#ifndef JAM
#define JAM XX_FILE_TYPE_JAM
#endif
#ifndef KRML
#define KRML XX_FILE_TYPE_KRML
#endif
#ifndef LBRCOBOL
#define LBRCOBOL XX_FILE_TYPE_LBRCOBOL
#endif
#ifndef MINIDUMP
#define MINIDUMP XX_FILE_TYPE_MINIDUMP
#endif
#ifndef POWERBOARDBBS
#define POWERBOARDBBS XX_FILE_TYPE_POWERBOARDBBS
#endif
#ifndef SCI
#define SCI XX_FILE_TYPE_SCI
#endif
#ifndef SEADATA
#define SEADATA XX_FILE_TYPE_SEADATA
#endif
#ifndef SECONDNATURE
#define SECONDNATURE XX_FILE_TYPE_SECONDNATURE
#endif
#ifndef SOS
#define SOS XX_FILE_TYPE_SOS
#endif
#ifndef SW
#define SW XX_FILE_TYPE_SW
#endif
#ifndef SWAGPACKET
#define SWAGPACKET XX_FILE_TYPE_SWAGPACKET
#endif
#ifndef TRCPAK
#define TRCPAK XX_FILE_TYPE_TRCPAK
#endif
#ifndef CFL
#define CFL XX_FILE_TYPE_CFL
#endif
#ifndef DPK
#define DPK XX_FILE_TYPE_DPK
#endif
#ifndef DSL2
#define DSL2 XX_FILE_TYPE_DSL2
#endif
#ifndef DTPACKED
#define DTPACKED XX_FILE_TYPE_DTPACKED
#endif
#ifndef FIZ
#define FIZ XX_FILE_TYPE_FIZ
#endif
#ifndef FLD
#define FLD XX_FILE_TYPE_FLD
#endif
#ifndef IBMZPAK
#define IBMZPAK XX_FILE_TYPE_IBMZPAK
#endif
#ifndef IGF1
#define IGF1 XX_FILE_TYPE_IGF1
#endif
#ifndef IGF2
#define IGF2 XX_FILE_TYPE_IGF2
#endif
#ifndef INTEDUFT
#define INTEDUFT XX_FILE_TYPE_INTEDUFT
#endif
#ifndef JM93
#define JM93 XX_FILE_TYPE_JM93
#endif
#ifndef LSZ
#define LSZ XX_FILE_TYPE_LSZ
#endif
#ifndef MIZ
#define MIZ XX_FILE_TYPE_MIZ
#endif
#ifndef MVA
#define MVA XX_FILE_TYPE_MVA
#endif
#ifndef POVLABLZH
#define POVLABLZH XX_FILE_TYPE_POVLABLZH
#endif
#ifndef POWERARC
#define POWERARC XX_FILE_TYPE_POWERARC
#endif
#ifndef QIP1
#define QIP1 XX_FILE_TYPE_QIP1
#endif
#ifndef QUARTERDECKQP
#define QUARTERDECKQP XX_FILE_TYPE_QUARTERDECKQP
#endif
#ifndef RCF
#define RCF XX_FILE_TYPE_RCF
#endif
#ifndef RIVERSOFT
#define RIVERSOFT XX_FILE_TYPE_RIVERSOFT
#endif
#ifndef SWAG
#define SWAG XX_FILE_TYPE_SWAG
#endif
#ifndef TGCF
#define TGCF XX_FILE_TYPE_TGCF
#endif
#ifndef TRC
#define TRC XX_FILE_TYPE_TRC
#endif
#ifndef ZLWB
#define ZLWB XX_FILE_TYPE_ZLWB
#endif
#ifndef ZZ
#define ZZ XX_FILE_TYPE_ZZ
#endif
#ifndef ZZZ
#define ZZZ XX_FILE_TYPE_ZZZ
#endif
#ifndef JGPAK
#define JGPAK XX_FILE_TYPE_JGPAK
#endif
#ifndef BORLANDPACK
#define BORLANDPACK XX_FILE_TYPE_BORLANDPACK
#endif
#ifndef ECMPACKED
#define ECMPACKED XX_FILE_TYPE_ECMPACKED
#endif
#ifndef JETBBS
#define JETBBS XX_FILE_TYPE_JETBBS
#endif
#ifndef QUALITAS
#define QUALITAS XX_FILE_TYPE_QUALITAS
#endif
#ifndef BWF
#define BWF XX_FILE_TYPE_BWF
#endif
#ifndef ZAP
#define ZAP XX_FILE_TYPE_ZAP
#endif
#ifndef STORK
#define STORK XX_FILE_TYPE_STORK
#endif
#ifndef ASCENDBACKUP
#define ASCENDBACKUP XX_FILE_TYPE_ASCENDBACKUP
#endif
#ifndef PYZ
#define PYZ XX_FILE_TYPE_PYZ
#endif
#ifndef FMC1
#define FMC1 XX_FILE_TYPE_FMC1
#endif
#ifndef XAR
#define XAR XX_FILE_TYPE_XAR
#endif
#ifndef LHA
#define LHA XX_FILE_TYPE_LHA
#endif
#ifndef SPIS
#define SPIS XX_FILE_TYPE_SPIS
#endif
#ifndef AMIGALZX
#define AMIGALZX XX_FILE_TYPE_AMIGALZX
#endif
#ifndef SEAARC
#define SEAARC XX_FILE_TYPE_SEAARC
#endif
#ifndef ELF32
#define ELF32 XX_FILE_TYPE_ELF32
#endif
#ifndef ELF64
#define ELF64 XX_FILE_TYPE_ELF64
#endif
#ifndef MACHO32
#define MACHO32 XX_FILE_TYPE_MACHO32
#endif
#ifndef MACHO64
#define MACHO64 XX_FILE_TYPE_MACHO64
#endif
#ifndef NE
#define NE XX_FILE_TYPE_NE
#endif
#ifndef LE
#define LE XX_FILE_TYPE_LE
#endif
#ifndef LX
#define LX XX_FILE_TYPE_LX
#endif
#ifndef DEX
#define DEX XX_FILE_TYPE_DEX
#endif

/* Operating System Types */
typedef enum xx_os_e {
    XX_OS_UNKNOWN  = 0,
    XX_OS_GENERIC  = 1,
    XX_OS_WINDOWS  = 2,
    XX_OS_LINUX    = 3,
    XX_OS_MACOS    = 4,
    XX_OS_UNIX     = 5,
    XX_OS_DOS      = 6,
    XX_OS_FREEBSD  = 7,
    XX_OS_ANDROID  = 8,
    XX_OS_IOS      = 9,
    XX_OS_OS2      = 10
} xx_os_t;

typedef xx_os_t xx_os;
typedef xx_os_t OS_TYPE;

#ifndef OS_UNKNOWN
#define OS_UNKNOWN  XX_OS_UNKNOWN
#endif
#ifndef OS_GENERIC
#define OS_GENERIC  XX_OS_GENERIC
#endif
#ifndef OS_WINDOWS
#define OS_WINDOWS  XX_OS_WINDOWS
#endif
#ifndef OS_LINUX
#define OS_LINUX    XX_OS_LINUX
#endif
#ifndef OS_MACOS
#define OS_MACOS    XX_OS_MACOS
#endif
#ifndef OS_UNIX
#define OS_UNIX     XX_OS_UNIX
#endif
#ifndef OS_DOS
#define OS_DOS      XX_OS_DOS
#endif
#ifndef OS_FREEBSD
#define OS_FREEBSD  XX_OS_FREEBSD
#endif
#ifndef OS_ANDROID
#define OS_ANDROID  XX_OS_ANDROID
#endif
#ifndef OS_IOS
#define OS_IOS      XX_OS_IOS
#endif
#ifndef OS_OS2
#define OS_OS2      XX_OS_OS2
#endif

/* Architecture Types */
typedef enum xx_arch_e {
    XX_ARCH_UNKNOWN = 0,
    XX_ARCH_GENERIC = 1,
    XX_ARCH_X86_16  = 2,
    XX_ARCH_X86     = 3,
    XX_ARCH_X86_64  = 4,
    XX_ARCH_ARM     = 5,
    XX_ARCH_ARM64   = 6,
    XX_ARCH_MIPS    = 7,
    XX_ARCH_MIPS64  = 8,
    XX_ARCH_PPC     = 9,
    XX_ARCH_PPC64   = 10,
    XX_ARCH_RISCV   = 11,
    XX_ARCH_RISCV64 = 12,
    XX_ARCH_SPARC   = 13,
    XX_ARCH_SPARC64 = 14,
    XX_ARCH_M68K    = 15,
    XX_ARCH_AVR     = 16,
    XX_ARCH_SH      = 17,
    XX_ARCH_WASM    = 18,
    XX_ARCH_JVM     = 19,
    XX_ARCH_DOTNET  = 20,
    XX_ARCH_DALVIK  = 21,

    /* Common aliases */
    XX_ARCH_I386    = 3,
    XX_ARCH_X86_32  = 3,
    XX_ARCH_AMD64   = 4,
    XX_ARCH_X64     = 4,
    XX_ARCH_ARM32   = 5,
    XX_ARCH_AARCH64 = 6,
    XX_ARCH_CIL     = 20
} xx_arch_t;

typedef xx_arch_t xx_arch;
typedef xx_arch_t ARCH_TYPE;
typedef xx_arch_t xx_format_arch_t;
typedef xx_arch_t FORMAT_ARCH;

#ifndef ARCH_UNKNOWN
#define ARCH_UNKNOWN XX_ARCH_UNKNOWN
#endif
#ifndef ARCH_GENERIC
#define ARCH_GENERIC XX_ARCH_GENERIC
#endif
#ifndef ARCH_X86_16
#define ARCH_X86_16  XX_ARCH_X86_16
#endif
#ifndef ARCH_X86
#define ARCH_X86     XX_ARCH_X86
#endif
#ifndef ARCH_X86_64
#define ARCH_X86_64  XX_ARCH_X86_64
#endif
#ifndef ARCH_ARM
#define ARCH_ARM     XX_ARCH_ARM
#endif
#ifndef ARCH_ARM64
#define ARCH_ARM64   XX_ARCH_ARM64
#endif
#ifndef ARCH_MIPS
#define ARCH_MIPS    XX_ARCH_MIPS
#endif
#ifndef ARCH_MIPS64
#define ARCH_MIPS64  XX_ARCH_MIPS64
#endif
#ifndef ARCH_PPC
#define ARCH_PPC     XX_ARCH_PPC
#endif
#ifndef ARCH_PPC64
#define ARCH_PPC64   XX_ARCH_PPC64
#endif
#ifndef ARCH_RISCV
#define ARCH_RISCV   XX_ARCH_RISCV
#endif
#ifndef ARCH_RISCV64
#define ARCH_RISCV64 XX_ARCH_RISCV64
#endif
#ifndef ARCH_WASM
#define ARCH_WASM    XX_ARCH_WASM
#endif
#ifndef ARCH_DALVIK
#define ARCH_DALVIK  XX_ARCH_DALVIK
#endif

/* Binary / Application Format Types */
typedef enum xx_format_type_e {
    XX_FORMAT_TYPE_UNKNOWN              = 0,
    XX_FORMAT_TYPE_CONSOLE_APPLICATION  = 1,
    XX_FORMAT_TYPE_GUI_APPLICATION      = 2,
    XX_FORMAT_TYPE_LIBRARY              = 3,
    XX_FORMAT_TYPE_DRIVER               = 4,
    XX_FORMAT_TYPE_STATIC_LIBRARY       = 5,
    XX_FORMAT_TYPE_SERVICE              = 6,
    XX_FORMAT_TYPE_DAEMON               = 7,
    XX_FORMAT_TYPE_BOOT                 = 8,
    XX_FORMAT_TYPE_FIRMWARE             = 9,
    XX_FORMAT_TYPE_OBJECT               = 10,
    XX_FORMAT_TYPE_PACKAGE              = 11,
    XX_FORMAT_TYPE_RAW                  = 12,
    XX_FORMAT_TYPE_ARCHIVE              = 13,
    XX_FORMAT_TYPE_CUSTOM               = 100,

    /* Aliases */
    XX_TYPE_UNKNOWN                     = 0,
    XX_TYPE_CONSOLE_APPLICATION         = 1,
    XX_TYPE_CONSOLE                     = 1,
    XX_TYPE_GUI_APPLICATION             = 2,
    XX_TYPE_GUI                         = 2,
    XX_TYPE_LIBRARY                     = 3,
    XX_TYPE_DLL                         = 3,
    XX_TYPE_SHARED_LIBRARY              = 3,
    XX_TYPE_DRIVER                      = 4,
    XX_TYPE_STATIC_LIBRARY              = 5,
    XX_TYPE_SERVICE                     = 6,
    XX_TYPE_DAEMON                      = 7,
    XX_TYPE_BOOT                        = 8,
    XX_TYPE_FIRMWARE                    = 9,
    XX_TYPE_OBJECT                      = 10,
    XX_TYPE_PACKAGE                     = 11,
    XX_TYPE_RAW                         = 12,
    XX_TYPE_ARCHIVE                     = 13,
    XX_TYPE_CUSTOM                      = 100
} xx_format_type_t;

typedef xx_format_type_t xx_format_type;
typedef xx_format_type_t xx_type_t;
typedef xx_format_type_t xx_type;
typedef xx_format_type_t FORMAT_TYPE;
typedef xx_format_type_t TYPE;

#ifndef TYPE_UNKNOWN
#define TYPE_UNKNOWN             XX_TYPE_UNKNOWN
#endif
#ifndef TYPE_CONSOLE_APPLICATION
#define TYPE_CONSOLE_APPLICATION XX_TYPE_CONSOLE_APPLICATION
#endif
#ifndef TYPE_CONSOLE
#define TYPE_CONSOLE             XX_TYPE_CONSOLE
#endif
#ifndef TYPE_GUI_APPLICATION
#define TYPE_GUI_APPLICATION     XX_TYPE_GUI_APPLICATION
#endif
#ifndef TYPE_GUI
#define TYPE_GUI                 XX_TYPE_GUI
#endif
#ifndef TYPE_LIBRARY
#define TYPE_LIBRARY             XX_TYPE_LIBRARY
#endif
#ifndef TYPE_DRIVER
#define TYPE_DRIVER              XX_TYPE_DRIVER
#endif
#ifndef TYPE_STATIC_LIBRARY
#define TYPE_STATIC_LIBRARY      XX_TYPE_STATIC_LIBRARY
#endif
#ifndef TYPE_SERVICE
#define TYPE_SERVICE             XX_TYPE_SERVICE
#endif
#ifndef TYPE_DAEMON
#define TYPE_DAEMON              XX_TYPE_DAEMON
#endif
#ifndef TYPE_BOOT
#define TYPE_BOOT                XX_TYPE_BOOT
#endif
#ifndef TYPE_FIRMWARE
#define TYPE_FIRMWARE            XX_TYPE_FIRMWARE
#endif
#ifndef TYPE_OBJECT
#define TYPE_OBJECT              XX_TYPE_OBJECT
#endif
#ifndef TYPE_PACKAGE
#define TYPE_PACKAGE             XX_TYPE_PACKAGE
#endif
#ifndef TYPE_RAW
#define TYPE_RAW                 XX_TYPE_RAW
#endif
#ifndef TYPE_ARCHIVE
#define TYPE_ARCHIVE             XX_TYPE_ARCHIVE
#endif

#ifndef FORMAT_TYPE_UNKNOWN
#define FORMAT_TYPE_UNKNOWN XX_FORMAT_TYPE_UNKNOWN
#endif
#ifndef FORMAT_TYPE_CONSOLE_APPLICATION
#define FORMAT_TYPE_CONSOLE_APPLICATION XX_FORMAT_TYPE_CONSOLE_APPLICATION
#endif
#ifndef FORMAT_TYPE_LIBRARY
#define FORMAT_TYPE_LIBRARY XX_FORMAT_TYPE_LIBRARY
#endif
#ifndef FORMAT_TYPE_DRIVER
#define FORMAT_TYPE_DRIVER  XX_FORMAT_TYPE_DRIVER
#endif
#ifndef FORMAT_TYPE_ARCHIVE
#define FORMAT_TYPE_ARCHIVE XX_FORMAT_TYPE_ARCHIVE
#endif


/* Endianness */
typedef enum xx_endian_e {
    XX_ENDIAN_UNKNOWN = 0,
    XX_ENDIAN_LITTLE  = 1,
    XX_ENDIAN_BIG     = 2
} xx_endian_t;

typedef xx_endian_t xx_endian;
typedef xx_endian_t ENDIAN;
typedef xx_endian_t xx_format_endian_t;
typedef xx_endian_t FORMAT_ENDIAN;

#ifndef ENDIAN_UNKNOWN
#define ENDIAN_UNKNOWN XX_ENDIAN_UNKNOWN
#endif
#ifndef ENDIAN_LITTLE
#define ENDIAN_LITTLE  XX_ENDIAN_LITTLE
#endif
#ifndef ENDIAN_BIG
#define ENDIAN_BIG     XX_ENDIAN_BIG
#endif

#ifndef XX_FORMAT_ENDIAN_UNKNOWN
#define XX_FORMAT_ENDIAN_UNKNOWN XX_ENDIAN_UNKNOWN
#endif
#ifndef XX_FORMAT_ENDIAN_LITTLE
#define XX_FORMAT_ENDIAN_LITTLE  XX_ENDIAN_LITTLE
#endif
#ifndef XX_FORMAT_ENDIAN_BIG
#define XX_FORMAT_ENDIAN_BIG     XX_ENDIAN_BIG
#endif
#ifndef FORMAT_ENDIAN_UNKNOWN
#define FORMAT_ENDIAN_UNKNOWN    XX_ENDIAN_UNKNOWN
#endif
#ifndef FORMAT_ENDIAN_LITTLE
#define FORMAT_ENDIAN_LITTLE     XX_ENDIAN_LITTLE
#endif
#ifndef FORMAT_ENDIAN_BIG
#define FORMAT_ENDIAN_BIG        XX_ENDIAN_BIG
#endif


/* Helper Macros */
#ifndef XXFC_UNUSED
#define XXFC_UNUSED(x) ((void)(x))
#endif

/* Export / Import decorations */
#if defined(_WIN32) && defined(__TINYC__)
#  if defined(XXFC_STATIC)
#    define XXFC_API
#  elif defined(XXFC_BUILD_SHARED) || defined(xxfclib_EXPORTS) || defined(xxfclib_shared_EXPORTS)
#    define XXFC_API __attribute__((dllexport))
#  else
#    define XXFC_API __attribute__((dllimport))
#  endif
#elif defined(_WIN32) || defined(__CYGWIN__)
#  if defined(XXFC_STATIC)
#    define XXFC_API
#  elif defined(XXFC_BUILD_SHARED) || defined(xxfclib_EXPORTS) || defined(xxfclib_shared_EXPORTS)
#    define XXFC_API __declspec(dllexport)
#  else
#    define XXFC_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  if defined(XXFC_STATIC)
#    define XXFC_API
#  elif defined(XXFC_BUILD_SHARED) || defined(xxfclib_EXPORTS) || defined(xxfclib_shared_EXPORTS)
#    define XXFC_API __attribute__((visibility("default")))
#  else
#    define XXFC_API
#  endif
#else
#  define XXFC_API
#endif

/* Enum to String helpers */
XXFC_API const char *xx_type_to_string(xx_format_type_t type);
XXFC_API const char *xx_arch_to_string(xx_arch_t arch);
XXFC_API const char *xx_os_to_string(xx_os_t os);

#ifdef __cplusplus
}
#endif

#endif /* XXFC_DEFS_H */

#ifndef AMIGAHUNK
#define AMIGAHUNK XX_FILE_TYPE_AMIGAHUNK
#endif

#ifndef ATARIST
#define ATARIST XX_FILE_TYPE_ATARIST
#endif

#ifndef DOS16M
#define DOS16M XX_FILE_TYPE_DOS16M
#endif

#ifndef DOS4G
#define DOS4G XX_FILE_TYPE_DOS4G
#endif

#ifndef COM
#define COM XX_FILE_TYPE_COM
#endif
#ifndef ASYMETRIX
#define ASYMETRIX XX_FILE_TYPE_ASYMETRIX
#endif
#ifndef BWCF
#define BWCF XX_FILE_TYPE_BWCF
#endif
#ifndef CHIEFLZ
#define CHIEFLZ XX_FILE_TYPE_CHIEFLZ
#endif
#ifndef CHIEFLZMULTI
#define CHIEFLZMULTI XX_FILE_TYPE_CHIEFLZMULTI
#endif
#ifndef CLP
#define CLP XX_FILE_TYPE_CLP
#endif
#ifndef CMP
#define CMP XX_FILE_TYPE_CMP
#endif
#ifndef DISKDOUBLER
#define DISKDOUBLER XX_FILE_TYPE_DISKDOUBLER
#endif
#ifndef EA
#define EA XX_FILE_TYPE_EA
#endif
#ifndef EALIB
#define EALIB XX_FILE_TYPE_EALIB
#endif
#ifndef EAREFPACK
#define EAREFPACK XX_FILE_TYPE_EAREFPACK
#endif
#ifndef FLS
#define FLS XX_FILE_TYPE_FLS
#endif
#ifndef GENIUS
#define GENIUS XX_FILE_TYPE_GENIUS
#endif
#ifndef HA
#define HA XX_FILE_TYPE_HA
#endif
#ifndef HZL
#define HZL XX_FILE_TYPE_HZL
#endif
#ifndef KBOOM
#define KBOOM XX_FILE_TYPE_KBOOM
#endif
#ifndef LZHCXP
#define LZHCXP XX_FILE_TYPE_LZHCXP
#endif
#ifndef LZWD
#define LZWD XX_FILE_TYPE_LZWD
#endif
#ifndef MI10
#define MI10 XX_FILE_TYPE_MI10
#endif
#ifndef NPACK
#define NPACK XX_FILE_TYPE_NPACK
#endif
#ifndef PAKLEO
#define PAKLEO XX_FILE_TYPE_PAKLEO
#endif
#ifndef SCL
#define SCL XX_FILE_TYPE_SCL
#endif
#ifndef ZCMP
#define ZCMP XX_FILE_TYPE_ZCMP
#endif
#ifndef ZPAK
#define ZPAK XX_FILE_TYPE_ZPAK
#endif
#ifndef NETWAREPACKED
#define NETWAREPACKED XX_FILE_TYPE_NETWAREPACKED
#endif
#ifndef ZTC
#define ZTC XX_FILE_TYPE_ZTC
#endif
#ifndef GLU
#define GLU XX_FILE_TYPE_GLU
#endif
#ifndef GTU
#define GTU XX_FILE_TYPE_GTU
#endif
#ifndef IBMSPACK
#define IBMSPACK XX_FILE_TYPE_IBMSPACK
#endif
#ifndef JBF
#define JBF XX_FILE_TYPE_JBF
#endif
#ifndef PCOMMOS2
#define PCOMMOS2 XX_FILE_TYPE_PCOMMOS2
#endif
#ifndef STK
#define STK XX_FILE_TYPE_STK
#endif
#ifndef TERSE
#define TERSE XX_FILE_TYPE_TERSE
#endif
#ifndef ZOO
#define ZOO XX_FILE_TYPE_ZOO
#endif
#ifndef SQX
#define SQX XX_FILE_TYPE_SQX
#endif
#ifndef IMP
#define IMP XX_FILE_TYPE_IMP
#endif
#ifndef COMPACTPRO
#define COMPACTPRO XX_FILE_TYPE_COMPACTPRO
#endif
#ifndef HAP
#define HAP XX_FILE_TYPE_HAP
#endif
#ifndef IRWINPAC
#define IRWINPAC XX_FILE_TYPE_IRWINPAC
#endif
#ifndef IVT
#define IVT XX_FILE_TYPE_IVT
#endif
#ifndef KOLIBRIKPACK
#define KOLIBRIKPACK XX_FILE_TYPE_KOLIBRIKPACK
#endif
#ifndef LIM
#define LIM XX_FILE_TYPE_LIM
#endif
#ifndef LOFI
#define LOFI XX_FILE_TYPE_LOFI
#endif
#ifndef PKT
#define PKT XX_FILE_TYPE_PKT
#endif
#ifndef QDA
#define QDA XX_FILE_TYPE_QDA
#endif
#ifndef QNXBASE
#define QNXBASE XX_FILE_TYPE_QNXBASE
#endif
#ifndef RID
#define RID XX_FILE_TYPE_RID
#endif
#ifndef ROMPAQ
#define ROMPAQ XX_FILE_TYPE_ROMPAQ
#endif
#ifndef RTA
#define RTA XX_FILE_TYPE_RTA
#endif
#ifndef RTPATCH
#define RTPATCH XX_FILE_TYPE_RTPATCH
#endif
#ifndef STYLUS
#define STYLUS XX_FILE_TYPE_STYLUS
#endif
#ifndef TI99ARC
#define TI99ARC XX_FILE_TYPE_TI99ARC
#endif
#ifndef TIVOLI
#define TIVOLI XX_FILE_TYPE_TIVOLI
#endif
#ifndef VMARC
#define VMARC XX_FILE_TYPE_VMARC
#endif
#ifndef WINTERSOFT
#define WINTERSOFT XX_FILE_TYPE_WINTERSOFT
#endif
#ifndef WPK
#define WPK XX_FILE_TYPE_WPK
#endif
#ifndef XEDITPACK
#define XEDITPACK XX_FILE_TYPE_XEDITPACK
#endif
#ifndef ZIE
#define ZIE XX_FILE_TYPE_ZIE
#endif
#ifndef SQUASHFS
#define SQUASHFS XX_FILE_TYPE_SQUASHFS
#endif
#ifndef NTFS
#define NTFS XX_FILE_TYPE_NTFS
#endif
#ifndef UDF
#define UDF XX_FILE_TYPE_UDF
#endif
#ifndef ROMFS
#define ROMFS XX_FILE_TYPE_ROMFS
#endif
#ifndef SQZ
#define SQZ XX_FILE_TYPE_SQZ
#endif
#ifndef TOPSPEED
#define TOPSPEED XX_FILE_TYPE_TOPSPEED
#endif
#ifndef TPS
#define TPS XX_FILE_TYPE_TPS
#endif
#ifndef ULEAD
#define ULEAD XX_FILE_TYPE_ULEAD
#endif
#ifndef QUANTUM
#define QUANTUM XX_FILE_TYPE_QUANTUM
#endif
#ifndef ZXZIP
#define ZXZIP XX_FILE_TYPE_ZXZIP
#endif
#ifndef ZOOM
#define ZOOM XX_FILE_TYPE_ZOOM
#endif
#ifndef SFPACK
#define SFPACK XX_FILE_TYPE_SFPACK
#endif
#ifndef CLAYLZ
#define CLAYLZ XX_FILE_TYPE_CLAYLZ
#endif
#ifndef C64WRAPTOR
#define C64WRAPTOR XX_FILE_TYPE_C64WRAPTOR
#endif
#ifndef CORELLTEC
#define CORELLTEC XX_FILE_TYPE_CORELLTEC
#endif
#ifndef PCSECURE
#define PCSECURE XX_FILE_TYPE_PCSECURE
#endif
#ifndef RSVK
#define RSVK XX_FILE_TYPE_RSVK
#endif
#ifndef RAW_LZW15V
#define RAW_LZW15V XX_FILE_TYPE_RAW_LZW15V
#endif
#ifndef SAF
#define SAF XX_FILE_TYPE_SAF
#endif
#ifndef SLS
#define SLS XX_FILE_TYPE_SLS
#endif
#ifndef NID
#define NID XX_FILE_TYPE_NID
#endif
#ifndef GAMOS
#define GAMOS XX_FILE_TYPE_GAMOS
#endif
#ifndef PANORAMA
#define PANORAMA XX_FILE_TYPE_PANORAMA
#endif
#ifndef FPAK
#define FPAK XX_FILE_TYPE_FPAK
#endif
#ifndef CRAMFS
#define CRAMFS XX_FILE_TYPE_CRAMFS
#endif
#ifndef JFFS2
#define JFFS2 XX_FILE_TYPE_JFFS2
#endif
#ifndef YAFFS
#define YAFFS XX_FILE_TYPE_YAFFS
#endif
#ifndef UBI
#define UBI XX_FILE_TYPE_UBI
#endif
#ifndef UBIFS
#define UBIFS XX_FILE_TYPE_UBIFS
#endif
#ifndef EXT
#define EXT XX_FILE_TYPE_EXT
#endif
#ifndef FAT
#define FAT XX_FILE_TYPE_FAT
#endif
#ifndef MBR
#define MBR XX_FILE_TYPE_MBR
#endif
#ifndef GPT
#define GPT XX_FILE_TYPE_GPT
#endif
#ifndef SPARSE
#define SPARSE XX_FILE_TYPE_SPARSE
#endif
#ifndef UIMAGE
#define UIMAGE XX_FILE_TYPE_UIMAGE
#endif
#ifndef DTB
#define DTB XX_FILE_TYPE_DTB
#endif
#ifndef TRX
#define TRX XX_FILE_TYPE_TRX
#endif
#ifndef SEAMA
#define SEAMA XX_FILE_TYPE_SEAMA
#endif
#ifndef CHK
#define CHK XX_FILE_TYPE_CHK
#endif
#ifndef PACKIMG
#define PACKIMG XX_FILE_TYPE_PACKIMG
#endif
#ifndef DLOB
#define DLOB XX_FILE_TYPE_DLOB
#endif
#ifndef WINCE
#define WINCE XX_FILE_TYPE_WINCE
#endif
#ifndef BINHDR
#define BINHDR XX_FILE_TYPE_BINHDR
#endif
#ifndef RTK
#define RTK XX_FILE_TYPE_RTK
#endif
#ifndef CSMAN
#define CSMAN XX_FILE_TYPE_CSMAN
#endif
#ifndef VXWORKS
#define VXWORKS XX_FILE_TYPE_VXWORKS
#endif
#ifndef UEFI_FV
#define UEFI_FV XX_FILE_TYPE_UEFI_FV
#endif
#ifndef UEFI_CAPSULE
#define UEFI_CAPSULE XX_FILE_TYPE_UEFI_CAPSULE
#endif
#ifndef QCOW
#define QCOW XX_FILE_TYPE_QCOW
#endif
#ifndef QNX6
#define QNX6 XX_FILE_TYPE_QNX6
#endif
#ifndef LUKS
#define LUKS XX_FILE_TYPE_LUKS
#endif
#ifndef APFS
#define APFS XX_FILE_TYPE_APFS
#endif
#ifndef BTRFS
#define BTRFS XX_FILE_TYPE_BTRFS
#endif
#ifndef LOGFS
#define LOGFS XX_FILE_TYPE_LOGFS
#endif
#ifndef DMG
#define DMG XX_FILE_TYPE_DMG
#endif
#ifndef APPLESINGLE
#define APPLESINGLE XX_FILE_TYPE_APPLESINGLE
#endif
#ifndef MACBINARY
#define MACBINARY XX_FILE_TYPE_MACBINARY
#endif
#ifndef PP20
#define PP20 XX_FILE_TYPE_PP20
#endif
#ifndef BEATTHEHOUSE
#define BEATTHEHOUSE XX_FILE_TYPE_BEATTHEHOUSE
#endif
#ifndef KPCK
#define KPCK XX_FILE_TYPE_KPCK
#endif
#ifndef BATTLEISLE
#define BATTLEISLE XX_FILE_TYPE_BATTLEISLE
#endif
#ifndef PERFORM
#define PERFORM XX_FILE_TYPE_PERFORM
#endif
#ifndef MATHCAD
#define MATHCAD XX_FILE_TYPE_MATHCAD
#endif
#ifndef NETWARE2
#define NETWARE2 XX_FILE_TYPE_NETWARE2
#endif
#ifndef SHAR
#define SHAR XX_FILE_TYPE_SHAR
#endif
#ifndef RNC
#define RNC XX_FILE_TYPE_RNC
#endif
#ifndef IBMPACK
#define IBMPACK XX_FILE_TYPE_IBMPACK
#endif
#ifndef CAZIP
#define CAZIP XX_FILE_TYPE_CAZIP
#endif
#ifndef TPWM
#define TPWM XX_FILE_TYPE_TPWM
#endif
#ifndef MRNZ
#define MRNZ XX_FILE_TYPE_MRNZ
#endif
#ifndef EDC
#define EDC XX_FILE_TYPE_EDC
#endif
#ifndef MXS
#define MXS XX_FILE_TYPE_MXS
#endif
#ifndef XORARCHIVE
#define XORARCHIVE XX_FILE_TYPE_XORARCHIVE
#endif
#ifndef MWAVE
#define MWAVE XX_FILE_TYPE_MWAVE
#endif
#ifndef FINEAR
#define FINEAR XX_FILE_TYPE_FINEAR
#endif
#ifndef GST
#define GST XX_FILE_TYPE_GST
#endif
#ifndef WINLINK
#define WINLINK XX_FILE_TYPE_WINLINK
#endif
#ifndef FTCOMP
#define FTCOMP XX_FILE_TYPE_FTCOMP
#endif
#ifndef GPFPACK
#define GPFPACK XX_FILE_TYPE_GPFPACK
#endif
#ifndef SCO
#define SCO XX_FILE_TYPE_SCO
#endif
#ifndef UNIX_COMPACT
#define UNIX_COMPACT XX_FILE_TYPE_UNIX_COMPACT
#endif
#ifndef PSDC
#define PSDC XX_FILE_TYPE_PSDC
#endif
#ifndef IS3
#define IS3 XX_FILE_TYPE_IS3
#endif
#ifndef IS5
#define IS5 XX_FILE_TYPE_IS5
#endif
#ifndef IS7INX
#define IS7INX XX_FILE_TYPE_IS7INX
#endif
#ifndef EDILZSS
#define EDILZSS XX_FILE_TYPE_EDILZSS
#endif
#ifndef SAVEDSKF
#define SAVEDSKF XX_FILE_TYPE_SAVEDSKF
#endif
#ifndef GOB
#define GOB XX_FILE_TYPE_GOB
#endif
#ifndef DEBUGSCR
#define DEBUGSCR XX_FILE_TYPE_DEBUGSCR
#endif
#ifndef DCLFT
#define DCLFT XX_FILE_TYPE_DCLFT
#endif
#ifndef STUFFIT
#define STUFFIT XX_FILE_TYPE_STUFFIT
#endif
#ifndef BINARYII
#define BINARYII XX_FILE_TYPE_BINARYII
#endif
#ifndef BINHEX
#define BINHEX XX_FILE_TYPE_BINHEX
#endif
#ifndef PMA
#define PMA XX_FILE_TYPE_PMA
#endif
#ifndef LZK00
#define LZK00 XX_FILE_TYPE_LZK00
#endif
#ifndef COMPAQLZH
#define COMPAQLZH XX_FILE_TYPE_COMPAQLZH
#endif
#ifndef ARCV
#define ARCV XX_FILE_TYPE_ARCV
#endif
#ifndef LIFKD
#define LIFKD XX_FILE_TYPE_LIFKD
#endif
#ifndef TRDOS
#define TRDOS XX_FILE_TYPE_TRDOS
#endif
#ifndef SQUEEZE1
#define SQUEEZE1 XX_FILE_TYPE_SQUEEZE1
#endif
#ifndef IZPACK
#define IZPACK XX_FILE_TYPE_IZPACK
#endif
#ifndef IS11
#define IS11 XX_FILE_TYPE_IS11
#endif
#ifndef GKSETUP
#define GKSETUP XX_FILE_TYPE_GKSETUP
#endif
#ifndef PCINSTALL
#define PCINSTALL XX_FILE_TYPE_PCINSTALL
#endif
#ifndef COPYQM
#define COPYQM XX_FILE_TYPE_COPYQM
#endif
#ifndef TELEDISK
#define TELEDISK XX_FILE_TYPE_TELEDISK
#endif
#ifndef HFE
#define HFE XX_FILE_TYPE_HFE
#endif
#ifndef FDI
#define FDI XX_FILE_TYPE_FDI
#endif
#ifndef TWOIMG
#define TWOIMG XX_FILE_TYPE_TWOIMG
#endif
#ifndef IMD
#define IMD XX_FILE_TYPE_IMD
#endif
#ifndef DISKDUPE
#define DISKDUPE XX_FILE_TYPE_DISKDUPE
#endif
#ifndef PMDISKCOPY
#define PMDISKCOPY XX_FILE_TYPE_PMDISKCOPY
#endif
#ifndef DISKJUGGLER
#define DISKJUGGLER XX_FILE_TYPE_DISKJUGGLER
#endif
#ifndef COPYQMEXE
#define COPYQMEXE XX_FILE_TYPE_COPYQMEXE
#endif
#ifndef PAX
#define PAX XX_FILE_TYPE_PAX
#endif
#ifndef SOLARISPKG
#define SOLARISPKG XX_FILE_TYPE_SOLARISPKG
#endif
#ifndef BEOSPKG
#define BEOSPKG XX_FILE_TYPE_BEOSPKG
#endif
#ifndef VMSPCSI
#define VMSPCSI XX_FILE_TYPE_VMSPCSI
#endif
#ifndef VMSDB
#define VMSDB XX_FILE_TYPE_VMSDB
#endif
#ifndef PCXLIB
#define PCXLIB XX_FILE_TYPE_PCXLIB
#endif
#ifndef HOG2
#define HOG2 XX_FILE_TYPE_HOG2
#endif
#ifndef SINNER
#define SINNER XX_FILE_TYPE_SINNER
#endif
#ifndef PSN
#define PSN XX_FILE_TYPE_PSN
#endif
#ifndef GRASP
#define GRASP XX_FILE_TYPE_GRASP
#endif
#ifndef MEGATECHVOL
#define MEGATECHVOL XX_FILE_TYPE_MEGATECHVOL
#endif
#ifndef STUNTS
#define STUNTS XX_FILE_TYPE_STUNTS
#endif
#ifndef NOTETAB
#define NOTETAB XX_FILE_TYPE_NOTETAB
#endif
#ifndef MCC
#define MCC XX_FILE_TYPE_MCC
#endif
#ifndef TNEF
#define TNEF XX_FILE_TYPE_TNEF
#endif
#ifndef OPC
#define OPC XX_FILE_TYPE_OPC
#endif
#ifndef QRST
#define QRST XX_FILE_TYPE_QRST
#endif
#ifndef PAIN
#define PAIN XX_FILE_TYPE_PAIN
#endif
#ifndef XLAS
#define XLAS XX_FILE_TYPE_XLAS
#endif
#ifndef MDCD
#define MDCD XX_FILE_TYPE_MDCD
#endif
#ifndef SSM
#define SSM XX_FILE_TYPE_SSM
#endif
#ifndef BVRP
#define BVRP XX_FILE_TYPE_BVRP
#endif
#ifndef BCW
#define BCW XX_FILE_TYPE_BCW
#endif
#ifndef SCF
#define SCF XX_FILE_TYPE_SCF
#endif
#ifndef RECOGNITA
#define RECOGNITA XX_FILE_TYPE_RECOGNITA
#endif
#ifndef JASC
#define JASC XX_FILE_TYPE_JASC
#endif
#ifndef BINDER
#define BINDER XX_FILE_TYPE_BINDER
#endif
#ifndef CSIDOS
#define CSIDOS XX_FILE_TYPE_CSIDOS
#endif
#ifndef CAT
#define CAT XX_FILE_TYPE_CAT
#endif
#ifndef BND
#define BND XX_FILE_TYPE_BND
#endif
#ifndef SMSIPAK
#define SMSIPAK XX_FILE_TYPE_SMSIPAK
#endif
#ifndef CPX
#define CPX XX_FILE_TYPE_CPX
#endif
#ifndef DISKEXPRESS
#define DISKEXPRESS XX_FILE_TYPE_DISKEXPRESS
#endif
#ifndef RED
#define RED XX_FILE_TYPE_RED
#endif
#ifndef SHRINKWRAP
#define SHRINKWRAP XX_FILE_TYPE_SHRINKWRAP
#endif
#ifndef CPX4
#define CPX4 XX_FILE_TYPE_CPX4
#endif
#ifndef GXL
#define GXL XX_FILE_TYPE_GXL
#endif
#ifndef AIAFF
#define AIAFF XX_FILE_TYPE_AIAFF
#endif
#ifndef SOFTPAQ2
#define SOFTPAQ2 XX_FILE_TYPE_SOFTPAQ2
#endif
#ifndef WIM
#define WIM XX_FILE_TYPE_WIM
#endif
#ifndef VHDDYNAMIC
#define VHDDYNAMIC XX_FILE_TYPE_VHDDYNAMIC
#endif
#ifndef VMDK
#define VMDK XX_FILE_TYPE_VMDK
#endif
#ifndef CISO
#define CISO XX_FILE_TYPE_CISO
#endif
#ifndef COPYDISK
#define COPYDISK XX_FILE_TYPE_COPYDISK
#endif
#ifndef HDCOPY
#define HDCOPY XX_FILE_TYPE_HDCOPY
#endif
#ifndef APRICOT
#define APRICOT XX_FILE_TYPE_APRICOT
#endif
#ifndef SABDU
#define SABDU XX_FILE_TYPE_SABDU
#endif
#ifndef MPQ
#define MPQ XX_FILE_TYPE_MPQ
#endif
#ifndef PHAR
#define PHAR XX_FILE_TYPE_PHAR
#endif
#ifndef SQ
#define SQ XX_FILE_TYPE_SQ
#endif
#ifndef SQUEEZE2
#define SQUEEZE2 XX_FILE_TYPE_SQUEEZE2
#endif
#ifndef DBZ
#define DBZ XX_FILE_TYPE_DBZ
#endif
#ifndef STAC
#define STAC XX_FILE_TYPE_STAC
#endif
#ifndef SPK
#define SPK XX_FILE_TYPE_SPK
#endif
#ifndef WRZL
#define WRZL XX_FILE_TYPE_WRZL
#endif
#ifndef BAGF
#define BAGF XX_FILE_TYPE_BAGF
#endif
#ifndef EMT
#define EMT XX_FILE_TYPE_EMT
#endif
#ifndef QIP2
#define QIP2 XX_FILE_TYPE_QIP2
#endif
#ifndef LIF
#define LIF XX_FILE_TYPE_LIF
#endif
#ifndef IXA
#define IXA XX_FILE_TYPE_IXA
#endif
#ifndef LSPACK10
#define LSPACK10 XX_FILE_TYPE_LSPACK10
#endif
#ifndef STARKIT
#define STARKIT XX_FILE_TYPE_STARKIT
#endif
#ifndef PAPERPORT
#define PAPERPORT XX_FILE_TYPE_PAPERPORT
#endif
#ifndef RNCA
#define RNCA XX_FILE_TYPE_RNCA
#endif
#ifndef HOG
#define HOG XX_FILE_TYPE_HOG
#endif
#ifndef AGIS
#define AGIS XX_FILE_TYPE_AGIS
#endif
#ifndef VOLITIONVPFT
#define VOLITIONVPFT XX_FILE_TYPE_VOLITIONVPFT
#endif
#ifndef WINTERMUTEDCP
#define WINTERMUTEDCP XX_FILE_TYPE_WINTERMUTEDCP
#endif
#ifndef BSN
#define BSN XX_FILE_TYPE_BSN
#endif
#ifndef RES
#define RES XX_FILE_TYPE_RES
#endif
#ifndef RSC
#define RSC XX_FILE_TYPE_RSC
#endif
#ifndef TEACY
#define TEACY XX_FILE_TYPE_TEACY
#endif
#ifndef SETTLERSFT
#define SETTLERSFT XX_FILE_TYPE_SETTLERSFT
#endif
#ifndef WOLFFT
#define WOLFFT XX_FILE_TYPE_WOLFFT
#endif
#ifndef BOO
#define BOO XX_FILE_TYPE_BOO
#endif
#ifndef VMSSAVESET
#define VMSSAVESET XX_FILE_TYPE_VMSSAVESET
#endif
#ifndef RAWSTAC
#define RAWSTAC XX_FILE_TYPE_RAWSTAC
#endif
#ifndef ANDROIDBOOT
#define ANDROIDBOOT XX_FILE_TYPE_ANDROIDBOOT
#endif
#ifndef ARCADYAN
#define ARCADYAN XX_FILE_TYPE_ARCADYAN
#endif
#ifndef AUTEL
#define AUTEL XX_FILE_TYPE_AUTEL
#endif
#ifndef DKBS
#define DKBS XX_FILE_TYPE_DKBS
#endif
#ifndef DLINK_TLV
#define DLINK_TLV XX_FILE_TYPE_DLINK_TLV
#endif
#ifndef DLKE
#define DLKE XX_FILE_TYPE_DLKE
#endif
#ifndef ECOS
#define ECOS XX_FILE_TYPE_ECOS
#endif
#ifndef ENCFW
#define ENCFW XX_FILE_TYPE_ENCFW
#endif
#ifndef ENCRPTED_IMG
#define ENCRPTED_IMG XX_FILE_TYPE_ENCRPTED_IMG
#endif
#ifndef JBOOT
#define JBOOT XX_FILE_TYPE_JBOOT
#endif
#ifndef LINGVOARC
#define LINGVOARC XX_FILE_TYPE_LINGVOARC
#endif
#ifndef LZ4DEMO
#define LZ4DEMO XX_FILE_TYPE_LZ4DEMO
#endif
#ifndef MATTER_OTA
#define MATTER_OTA XX_FILE_TYPE_MATTER_OTA
#endif
#ifndef MH01
#define MH01 XX_FILE_TYPE_MH01
#endif
#ifndef SHRS
#define SHRS XX_FILE_TYPE_SHRS
#endif
#ifndef SILMARILSFT
#define SILMARILSFT XX_FILE_TYPE_SILMARILSFT
#endif
#ifndef TPLINK
#define TPLINK XX_FILE_TYPE_TPLINK
#endif
#ifndef TWRX
#define TWRX XX_FILE_TYPE_TWRX
#endif
#ifndef UBOOT_ENV
#define UBOOT_ENV XX_FILE_TYPE_UBOOT_ENV
#endif
#ifndef INFOGRAMESFT
#define INFOGRAMESFT XX_FILE_TYPE_INFOGRAMESFT
#endif
#ifndef PDB
#define PDB XX_FILE_TYPE_PDB
#endif
#ifndef XPAK
#define XPAK XX_FILE_TYPE_XPAK
#endif
#ifndef DCLRAW
#define DCLRAW XX_FILE_TYPE_DCLRAW
#endif
#ifndef SREC
#define SREC XX_FILE_TYPE_SREC
#endif
#ifndef LZOP
#define LZOP XX_FILE_TYPE_LZOP
#endif
#ifndef DMS
#define DMS XX_FILE_TYPE_DMS
#endif
#ifndef RESOURCEFORK
#define RESOURCEFORK XX_FILE_TYPE_RESOURCEFORK
#endif
