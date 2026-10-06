/* SPDX-License-Identifier: MIT. Original parser from documented field facts; no upstream implementation copied. */
#include "xxfclib/formats/ray_dim/xx_ray_dim.h"
#include "../disk_additions/xx_disk_additions.h"

/* Original DIM uses highest cylinder/head indices and one SPT-byte status
 * trailer following each track's512-byte sectors. Aaru writer produces a
 * header-count/plain-sector variant; extent distinguishes both unambiguously. */
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){uint8_t h[84];uint64_t n=(uint64_t)pm_available(f),tracks,bytes;uint32_t c,heads,spt;unsigned i;bool vendor=false;da_run r;
 if(!da_read(f,0,h,84,pd)||xx_rt_memcmp(h,"Disk IMage VER ",15)||h[80]<1U||h[80]>5U||!h[82]||h[82]>64U||h[83]>2U)return false;
 for(i=15;i+14U<=80U;++i) {if(!xx_rt_memcmp(h+i,"Ray Arachelian",14)){vendor=true;break;} } if(!vendor)return false;c=(uint32_t)h[81]+1U;heads=(uint32_t)h[83]+1U;spt=h[82];tracks=(uint64_t)c*heads;bytes=(uint64_t)spt*512U;
 if(h[83]<=1U&&n==84U+tracks*(bytes+spt)){r.at=84;r.bytes=bytes;r.count=tracks;r.stride=bytes+spt;r.fill=-1;r.source=NULL;if(!da_map_add(f,s,"sector-image.img",&r,1))return false;if(!da_one(f,s,"sector-status.bin",84U+bytes,spt,tracks,bytes+spt,-1,NULL))return false;}
 else if(h[81]&&h[83]&&n==84U+(uint64_t)h[81]*h[83]*bytes){if(!da_add(f,s,"sector-image.img",84,n-84U))return false;}
 else { return false; } if(!da_add(f,s,"descriptor.dim",0,84))return false;s->size=(int64_t)n;return true;
}
DA_API(ray_dim,XX_FILE_TYPE_RAY_DIM,"dim")
