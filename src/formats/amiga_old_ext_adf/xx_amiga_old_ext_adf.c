/* SPDX-License-Identifier: MIT. Original UAE--ADF component framing.
 * Facts: HxC oldextadf_loader; 160 sync/length entries followed by track bytes.
 */
#include "xxfclib/formats/amiga_old_ext_adf/xx_amiga_old_ext_adf.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 hx_blob b;uint32_t i;uint64_t at=648;bool ok=false;char name[96];
 if(!hx_load(f,&b,pd))return false;
 HX_NEED(hx_tag(&b,0,"UAE--ADF",8)&&hx_span(&b,0,648)&&hx_emit(f,s,&b,"descriptor.old-ext-adf",0,648));
 for(i=0;i<160;++i){const uint8_t *q=b.p+8+i*4;uint32_t sync=pm_be16(q),z=pm_be16(q+2);HX_NEED(hx_span(&b,at,z));
  if(z){uint8_t *copy;xx_rt_snprintf(name,sizeof(name),"track-C%03u-H%u.%s",i/2U,i%2U,sync?"mfm-bitcells":"sectors");
   if(sync){copy=hx_alloc(f,&b,z+2U);HX_NEED(copy);copy[0]=q[0];copy[1]=q[1];xx_rt_memcpy(copy+2,b.p+at,z);if(!hx_owned(f,s,&b,name,copy,z+2U)){xx_mem_free(copy);goto done;}}
   else HX_NEED(!(z&511U)&&hx_emit(f,s,&b,name,at,z));
  }at+=z;
 }
 HX_NEED(at==b.n&&hx_text(f,s,&b,"Format: UAE old extended ADF\nTrack sides: 160\nRepresentation: sector bytes or MFM bytes with original leading sync word restored\nIntegrity: container lengths; no track checksum\n"));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
HX_API(amiga_old_ext_adf,XX_FILE_TYPE_AMIGA_OLD_EXT_ADF,"adf")
