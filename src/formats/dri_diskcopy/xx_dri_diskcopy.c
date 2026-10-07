/* SPDX-License-Identifier: MIT. Original parser from documented field facts; no upstream implementation copied. */
#include "xxfclib/formats/dri_diskcopy/xx_dri_diskcopy.h"
#include "../disk_additions/xx_disk_additions.h"

/* Packed footer415: signature51 + two identical packed182-byte BPBs. */
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){uint8_t h[415];int64_t n=pm_available(f);uint32_t c,heads,spt,bps,sectors;unsigned i;bool vendor=false;
 if(n<=415||!da_read(f,(uint64_t)n-415U,h,415,pd)||xx_rt_memcmp(h,"DiskImage ",10)||xx_rt_memcmp(h+51,h+233,182))return false;
 for(i=10;i+20U<=51U;++i) {if(!xx_rt_memcmp(h+i,"Digital Research Inc",20)){vendor=true;break;} } if(!vendor)return false;
 c=xx_data_get_u16(h+55, 2, 0, false);bps=xx_data_get_u16(h+58, 2, 0, false);sectors=xx_data_get_u16(h+66, 2, 0, false);spt=xx_data_get_u16(h+69, 2, 0, false);heads=xx_data_get_u16(h+71, 2, 0, false);
 if(!c||!heads||heads>2U||!spt||spt>64U||bps<128U||bps>16384U||(bps&(bps-1U))||(uint64_t)c*heads*spt!=sectors||(uint64_t)sectors*bps!=(uint64_t)n-415U)return false;
 if(!da_add(f,s,"descriptor.dsk-footer",n-415U,415)||!da_add(f,s,"sector-image.img",0,n-415U)) {return false; } s->size=n;return true;
}
DA_API(dri_diskcopy,XX_FILE_TYPE_DRI_DISKCOPY,"dsk")
