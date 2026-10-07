/* SPDX-License-Identifier: MIT. Original Atari DIM envelope/geometry reader.
 * Facts: HxC dim_format.h and original DIM header description in dim-format.txt.
 * Sparse used-sector payloads are preserved, not filled with invented sectors.
 */
#include "xxfclib/formats/atari_dim/xx_atari_dim.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 hx_blob b;uint32_t tracks,sides,sectors,size,start,end;uint64_t plain;bool ok=false;char info[512];
 if(!hx_load(f,&b,pd))return false;
 HX_NEED(hx_tag(&b,0,"BB",2)&&hx_span(&b,0,32));sides=b.p[6]+1U;sectors=b.p[8];start=b.p[10];end=b.p[12];size=xx_data_get_u16(b.p+14, 2, 0, true);if(!size)size=512;
 HX_NEED(b.p[3]<=1&&sides<=2&&sectors&&sectors<=64&&start<=end&&end<170&&b.p[13]<=1&&size>=128&&size<=8192&&!(size&(size-1U)));tracks=end-start+1;plain=(uint64_t)tracks*sides*sectors*size;
 HX_NEED(plain<=HX_MAX_FILE&&hx_emit(f,s,&b,"descriptor.dim",0,32));
 if(b.p[3]){HX_NEED(b.n>32&&b.n-32<=plain&&hx_emit(f,s,&b,"unreconstructed-used-sectors.dim",32,b.n-32));}
 else HX_NEED(b.n==32+plain&&hx_emit(f,s,&b,"sector-image.st",32,plain));
 xx_rt_snprintf(info,sizeof(info),"Format: Atari DIM\nStart cylinder: %u\nEnd cylinder: %u\nSides: %u\nSectors per track: %u\nSector bytes: %u\nUsed sectors only: %u\nStored payload bytes: %llu\nRepresentation: %s\nIntegrity: known header and extent constraints; sparse reconstruction unavailable\n",start,end,sides,sectors,size,b.p[3],(unsigned long long)(b.n-32),b.p[3]?"original opaque sparse payload; no logical image reconstruction":"original complete sector image");
 HX_NEED(hx_text(f,s,&b,info));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
HX_API(atari_dim,XX_FILE_TYPE_ATARI_DIM,"dim")
