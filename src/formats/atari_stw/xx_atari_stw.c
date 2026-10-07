/* SPDX-License-Identifier: MIT. Original parser of Steem STW 1.x specification.
 * All words big endian; each data byte is represented by one MFM word.
 */
#include "xxfclib/formats/atari_stw/xx_atari_stw.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 hx_blob b;uint32_t tracks,sides,words,i;uint64_t at=10;bool ok=false;char name[96],info[256];
 if(!hx_load(f,&b,pd))return false;
 HX_NEED(hx_tag(&b,0,"STW\0",4)&&hx_span(&b,0,10));tracks=b.p[7];sides=b.p[6];words=xx_data_get_u16(b.p+8, 2, 0, true);
 HX_NEED(xx_data_get_u16(b.p+4, 2, 0, true)>=0x100&&xx_data_get_u16(b.p+4, 2, 0, true)<0x200&&tracks&&tracks<=170&&sides&&sides<=2&&words>=128&&words<=32768&&b.n==10+(uint64_t)tracks*sides*(5U+2U*words));
 HX_NEED(hx_emit(f,s,&b,"descriptor.stw",0,10));
 for(i=0;i<tracks*sides;++i){HX_NEED(hx_tag(&b,at,"TRK",3)&&b.p[at+3]==i%sides&&b.p[at+4]==i/sides);xx_rt_snprintf(name,sizeof(name),"track-C%03u-H%u.mfm-words-be",i/sides,i%sides);HX_NEED(hx_emit(f,s,&b,name,at+5,words*2U));at+=5+words*2U;}
 xx_rt_snprintf(info,sizeof(info),"Format: Steem STW\nCylinders: %u\nSides: %u\nMFM words per track: %u\nRepresentation: complete original clock/data MFM words; no sector decoding\nIntegrity: framing and geometry; no track checksum\n",tracks,sides,words);
 HX_NEED(hx_text(f,s,&b,info));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
HX_API(atari_stw,XX_FILE_TYPE_ATARI_STW,"stw")
