/* SPDX-License-Identifier: MIT. Original parser from documented field facts; no upstream implementation copied. */
#include "xxfclib/formats/maxi_disk/xx_maxi_disk.h"
#include "../disk_additions/xx_disk_additions.h"

/* MAXI: eight-byte geometry header, then cylinder/head/sector order. */
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){uint8_t h[8];uint64_t n;if(!da_read(f,0,h,8,pd)||h[1]>11U||!h[2]||h[2]>2U||!h[3]||h[3]>90U||h[4]>7U||!h[5]||h[5]>64U)return false;
 n=(uint64_t)h[2]*h[3]*h[5]*(128U<<h[4]);if((uint64_t)pm_available(f)!=8U+n||!da_add(f,s,"descriptor.hdk",0,8)||!da_add(f,s,"sector-image.img",8,n))return false;s->size=(int64_t)(8U+n);return true;
}
DA_API(maxi_disk,XX_FILE_TYPE_MAXI_DISK,"hdk")
