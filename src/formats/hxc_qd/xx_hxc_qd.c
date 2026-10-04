/* SPDX-License-Identifier: MIT. Original HxC QuickDisk framing from qd_format.h.
 * LSB-first raw cell bytes and switch positions are retained.
 */
#include "xxfclib/formats/hxc_qd/xx_hxc_qd.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 hx_blob b;hx_range ranges[4096];unsigned nr=0;uint32_t tracks,sides,list,i,present=0;uint64_t end,maximum;bool ok=false;char name[96],info[256];
 if(!hx_load(f,&b,pd))return false;
 HX_NEED(hx_tag(&b,0,"HXCQDDRV",8)&&hx_span(&b,0,40));tracks=pm_le32(b.p+12);sides=pm_le32(b.p+16);list=pm_le32(b.p+36);
 HX_NEED(pm_le32(b.p+8)==0&&tracks&&tracks<=170&&sides&&sides<=2&&pm_le32(b.p+24)<=1&&pm_le32(b.p+28)&&pm_le32(b.p+28)<=50000000&&list>=40&&!(list&511U));end=(uint64_t)list+(uint64_t)tracks*sides*16;maximum=end;
 HX_NEED(hx_claim(&b,ranges,&nr,0,end)&&hx_emit(f,s,&b,"descriptor.qd",0,end));
 for(i=0;i<tracks*sides;++i){const uint8_t *q=b.p+list+i*16;uint32_t off=pm_le32(q),z=pm_le32(q+4),start=pm_le32(q+8),stop=pm_le32(q+12);
  if(!z){HX_NEED(!off&&!start&&!stop);continue;}HX_NEED(z<=1048576&&off>=end&&!(off&511U)&&start<=z&&stop<=z&&hx_claim(&b,ranges,&nr,off,z));
  xx_rt_snprintf(name,sizeof(name),"disk-%03u-side-%u.lsb-bitcells",i/sides,i%sides);HX_NEED(hx_emit(f,s,&b,name,off,z));++present;if((uint64_t)off+z>maximum)maximum=(uint64_t)off+z;
 }
 HX_NEED(present&&maximum<=b.n&&b.n-maximum<512U&&hx_zero(&b,maximum,b.n-maximum));
 xx_rt_snprintf(info,sizeof(info),"Format: HxC QuickDisk\nDisks: %u\nSides: %u\nCell rate: %u\nRepresentation: original LSB-first cell bytes and descriptor switch positions\nIntegrity: geometry/nonoverlapping spans; no track checksum\n",tracks,sides,pm_le32(b.p+28));
 HX_NEED(hx_text(f,s,&b,info));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
HX_API(hxc_qd,XX_FILE_TYPE_HXC_QD,"qd")
