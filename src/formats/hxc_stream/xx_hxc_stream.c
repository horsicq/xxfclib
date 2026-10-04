/* SPDX-License-Identifier: MIT. Original Pauline HxCStream chunk parser.
 * Facts: HxC hxcstream_format.h; Pauline dump_chunk.c generate_chunk framing.
 * Container CRC includes arbitrary authenticated alignment padding.
 */
#include "xxfclib/formats/hxc_stream/xx_hxc_stream.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static bool hs_pulses(hx_blob *b,const uint8_t *p,uint32_t z,uint32_t expected){uint32_t a=0,count=0,iterations=0;while(a<z){uint8_t c=p[a++];uint32_t n,value;unsigned k;if(!(iterations++&4095U)&&!hx_poll(b))return false;
 if(c<128U){if(c)++count;continue;}if(c<192U){n=1;value=c&63U;}else if(c<224U){n=2;value=c&31U;}else if(c<240U){n=3;value=c&15U;}else return false;
 if(n>z-a)return false;for(k=0;k<n;++k)value=(value<<8)|p[a++];if(!value)return false;++count;
 }return count==expected;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){hx_blob b;uint64_t at=0;uint32_t packets=0,previous=0,streams=0;bool ok=false;char name[96],info[384];
 if(!hx_load(f,&b,pd))return false;
 while(at<b.n){uint32_t z,number,blocks=0;uint64_t p,end;
  HX_NEED(packets<1024&&hx_tag(&b,at,"CHKH",4)&&hx_span(&b,at,16));z=pm_le32(b.p+at+4);number=pm_le32(b.p+at+8);
  HX_NEED(z>=24&&!(z&3U)&&hx_span(&b,at,z)&&(!packets||number>previous)&&hx_pauline_crc(&b,at,z-4U,pm_le32(b.p+at+z-4)));previous=number;end=at+z-4;p=at+12;
  xx_rt_snprintf(name,sizeof(name),"packet-%04u.original.hxcstream",number);HX_NEED(hx_emit(f,s,&b,name,at,z));
  while(p<end){uint32_t type,payload,packed,plain,over;HX_NEED(++blocks<=64&&end-p>=8);type=pm_le32(b.p+p);payload=pm_le32(b.p+p+4);HX_NEED(!(payload&3U)&&payload<=end-p-8U);
   if(type==0){HX_NEED(payload);xx_rt_snprintf(name,sizeof(name),"packet-%04u-metadata.bin",number);HX_NEED(hx_emit(f,s,&b,name,p+8,payload));}
   else if(type==1||type==2){over=type==1?8U:12U;HX_NEED(payload>=over&&end-p>=8U+over);packed=pm_le32(b.p+p+8);plain=pm_le32(b.p+p+12);
    HX_NEED(packed&&payload==over+((packed+3U)&~3U)&&packed<=payload-over);if(type==1)HX_NEED(!(plain&1U));
    xx_rt_snprintf(name,sizeof(name),"packet-%04u-block-%02u.%s",number,blocks,type==1?"io-u16le":"flux-delta-encoded");HX_NEED(hx_decode(f,s,&b,name,p+8+over,packed,plain,true));
    if(type==2){pm_member *m=&s->items[s->count-1];HX_NEED(hs_pulses(&b,m->memory,plain,pm_le32(b.p+p+16)));++streams;}
   }else goto done;
   p+=8U+payload;
  }HX_NEED(p==end&&blocks);at+=z;++packets;
 }
 HX_NEED(packets&&streams);xx_rt_snprintf(info,sizeof(info),"Format: Pauline HxCStream\nPackets: %u\nFlux blocks: %u\nRepresentation: complete original packets, metadata, LZ4-decoded IO/encoded transition bytes\nIntegrity: every packet CRC-32, exact LZ4 size and transition count; no MFM/GCR sector decoding\n",packets,streams);
 HX_NEED(hx_text(f,s,&b,info));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
HX_API(hxc_stream,XX_FILE_TYPE_HXC_STREAM,"hxcstream")
