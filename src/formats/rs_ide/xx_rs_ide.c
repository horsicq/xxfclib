/* SPDX-License-Identifier: MIT. Original parser from documented field facts; no upstream implementation copied. */
#include "xxfclib/formats/rs_ide/xx_rs_ide.h"
#include "../disk_additions/xx_disk_additions.h"

/* RS-IDE: 128-byte minimum header; optional ATA IDENTIFY first106 bytes. */
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){uint8_t h[128];uint32_t at,bytes;int64_t n=pm_available(f);if(!da_read(f,0,h,128,pd)||xx_rt_memcmp(h,"RS-IDE\x1a",7)||(h[8]&~1U))return false;
 at=pm_le16(h+9);bytes=(h[8]&1U)?256U:512U;if(at<128U||n<at||(n-at)<=0||(uint64_t)(n-at)%bytes)return false;
 if(!da_add(f,s,"descriptor.ide",0,at)||!da_add(f,s,"sector-image.img",at,(uint64_t)(n-at))) {return false; } s->size=n;return true;
}
DA_API(rs_ide,XX_FILE_TYPE_RS_IDE,"ide")
