/* SPDX-License-Identifier: MIT. Original ORICDISK/MFM_DISK component parser.
 * Facts: Fabrice Frances format description retained in HxC oricdsk_format.h.
 */
#include "xxfclib/formats/oric_dsk/xx_oric_dsk.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 hx_blob b;uint32_t tracks,sides,geometry,size,i;bool raw,ok=false;char name[96],info[256];
 if(!hx_load(f,&b,pd))return false;
 raw=hx_tag(&b,0,"MFM_DISK",8);HX_NEED((raw||hx_tag(&b,0,"ORICDISK",8))&&hx_span(&b,0,256));
 sides=pm_le32(b.p+8);tracks=pm_le32(b.p+12);geometry=pm_le32(b.p+16);HX_NEED(sides&&sides<=2&&tracks&&tracks<=170);
 if(raw){HX_NEED(geometry==1||geometry==2);HX_NEED((b.n-256)%(tracks*sides)==0);size=(uint32_t)((b.n-256)/(tracks*sides));HX_NEED(size>=128&&size<=65536&&!(size&255U));}
 else{HX_NEED(geometry&&geometry<=64);size=geometry*256U;}
 HX_NEED(b.n==256+(uint64_t)tracks*sides*size&&hx_emit(f,s,&b,"descriptor.oric-dsk",0,256));
 for(i=0;i<tracks*sides;++i){unsigned c=(raw&&geometry==2)?i/sides:i%tracks,h=(raw&&geometry==2)?i%sides:i/tracks;xx_rt_snprintf(name,sizeof(name),"track-C%03u-H%u.%s",c,h,raw?"decoded-mfm-bytes":"sectors");HX_NEED(hx_emit(f,s,&b,name,256+(uint64_t)i*size,size));}
 xx_rt_snprintf(info,sizeof(info),"Format: Oric %s\nCylinders: %u\nSides: %u\nTrack bytes: %u\nRepresentation: original %s; raw-track gaps and address marks retained\nIntegrity: geometry and extent; no container checksum\n",raw?"MFM_DISK":"ORICDISK",tracks,sides,size,raw?"clock-stripped MFM track bytes":"sector bytes");
 HX_NEED(hx_text(f,s,&b,info));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
HX_API(oric_dsk,XX_FILE_TYPE_ORIC_DSK,"dsk")
