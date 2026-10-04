/* SPDX-License-Identifier: MIT. Original AFI block-graph and checksum parser.
 * Facts: official HxC afi_format.h/afi_writer.c. AFI's "GZIP" packer is
 * actually an RFC1950 zlib stream. RLE/LZW reserved packers remain opaque,
 * checksum-verified stored components, explicitly labelled in their names.
 */
#include "xxfclib/formats/hxc_afi/xx_hxc_afi.h"
#include "xx_hxc_tracks.h"
static bool afi_tag(hx_blob *b,uint64_t a,const char *tag){size_t n=xx_rt_strlen(tag);return n<16&&hx_tag(b,a,tag,n)&&hx_zero(b,a+n,16-n);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){
 hx_blob b;hx_range ranges[4096];unsigned nr=0;uint32_t list,infos,count,i,strings,lo,hi,ls,hs;uint64_t maximum=0;bool ok=false;char name[96],text[512];uint8_t seen[340];
 static const char *types[]={"","MFM_DATA","INDEX_DATA","BITRATE_DATA","PDC_DATA","WEAKBITS_DATA","CELL_DATA"};
 if(!hx_load(f,&b,pd))return false;xx_mem_zero(seen,sizeof(seen));
 HX_NEED(afi_tag(&b,0,"AFI_FLOPPY_IMG")&&hx_span(&b,0,32)&&b.p[16]==0&&b.p[17]<=2&&pm_le32(b.p+18)==32&&hx_ccitt(&b,0,32));
 infos=pm_le32(b.p+22);list=pm_le32(b.p+26);HX_NEED(hx_claim(&b,ranges,&nr,0,32)&&infos>=32&&list>=32&&afi_tag(&b,infos,"AFI_INFO")&&hx_span(&b,infos,54));
 lo=pm_le32(b.p+infos+32);hi=pm_le32(b.p+infos+36);ls=pm_le32(b.p+infos+40);hs=pm_le32(b.p+infos+44);strings=pm_le32(b.p+infos+48);
 HX_NEED(lo<=hi&&hi<170&&ls<=hs&&hs<=1&&strings<=256);count=(hi-lo+1U)*(hs-ls+1U);
 HX_NEED(pm_le32(b.p+infos+28)==count&&hx_span(&b,infos,54+(uint64_t)strings*4)&&hx_ccitt(&b,infos,54+(uint64_t)strings*4)&&hx_claim(&b,ranges,&nr,infos,54+(uint64_t)strings*4));
 maximum=infos+54+(uint64_t)strings*4;HX_NEED(hx_emit(f,s,&b,"floppy-info.afi",infos,54+(uint64_t)strings*4));
 for(i=0;i<strings;++i){uint64_t a=(uint64_t)infos+pm_le32(b.p+infos+52+i*4);uint32_t z;
  HX_NEED(afi_tag(&b,a,"STRING")&&hx_span(&b,a,38));z=pm_le32(b.p+a+32);HX_NEED(z<=1048576&&hx_claim(&b,ranges,&nr,a,38+(uint64_t)z)&&hx_ccitt(&b,a,38+(uint64_t)z));
  xx_rt_snprintf(name,sizeof(name),"string-%03u.original.afi",i);HX_NEED(hx_emit(f,s,&b,name,a,38+(uint64_t)z));if(a+38+z>maximum)maximum=a+38+z;
 }
 HX_NEED(afi_tag(&b,list,"TRACKLIST")&&hx_span(&b,list,22+(uint64_t)count*4)&&pm_le32(b.p+list+16)==count&&hx_ccitt(&b,list,22+(uint64_t)count*4)&&hx_claim(&b,ranges,&nr,list,22+(uint64_t)count*4));
 HX_NEED(hx_emit(f,s,&b,"descriptor.afi",0,32)&&hx_emit(f,s,&b,"track-index.afi",list,22+(uint64_t)count*4));if(list+22+(uint64_t)count*4>maximum)maximum=list+22+(uint64_t)count*4;
 for(i=0;i<count;++i){uint64_t a=(uint64_t)list+pm_le32(b.p+list+20+i*4);uint32_t cylinder,head,mode,elements,chunks,j,id;uint64_t header;
  HX_NEED(afi_tag(&b,a,"TRACK")&&hx_span(&b,a,38));cylinder=pm_le32(b.p+a+16);head=pm_le32(b.p+a+20);mode=pm_le32(b.p+a+24);elements=pm_le32(b.p+a+28);chunks=pm_le32(b.p+a+32);
  HX_NEED(cylinder>=lo&&cylinder<=hi&&head>=ls&&head<=hs&&mode<=3&&elements<=8U*1048576U&&chunks<=16);id=(cylinder-lo)*(hs-ls+1U)+head-ls;HX_NEED(id<count&&!seen[id]);seen[id]=1;header=38+(uint64_t)chunks*4;
  HX_NEED(hx_ccitt(&b,a,header)&&hx_claim(&b,ranges,&nr,a,header));xx_rt_snprintf(name,sizeof(name),"track-C%03u-H%u.descriptor.afi",cylinder,head);HX_NEED(hx_emit(f,s,&b,name,a,header));if(a+header>maximum)maximum=a+header;
  for(j=0;j<chunks;++j){uint64_t d=a+pm_le32(b.p+a+36+j*4);uint32_t type,bits,packed,packer,plain;
   HX_NEED(afi_tag(&b,d,"TRACKDATA")&&hx_span(&b,d,54));type=pm_le32(b.p+d+16);bits=pm_le32(b.p+d+36);packed=pm_le32(b.p+d+40);packer=pm_le32(b.p+d+44);plain=pm_le32(b.p+d+48);
   HX_NEED(type<=6&&afi_tag(&b,d+20,types[type])&&bits&&bits<=32&&packed<=HX_MAX_FILE&&plain<=HX_MAX_OUTPUT&&packer<=3&&hx_claim(&b,ranges,&nr,d,54+(uint64_t)packed)&&hx_ccitt(&b,d,54+(uint64_t)packed));
   xx_rt_snprintf(name,sizeof(name),"C%03u-H%u-block-%02u.original.afi",cylinder,head,j);HX_NEED(hx_emit(f,s,&b,name,d,54+(uint64_t)packed));
   xx_rt_snprintf(name,sizeof(name),"C%03u-H%u-block-%02u-%s.%s",cylinder,head,j,types[type][0]?types[type]:"NONE",packer<=1?"decoded-bytes":"unsupported-packed-bytes");
   if(packer==0){HX_NEED(packed==plain&&hx_emit(f,s,&b,name,d+52,plain));}
   else if(packer==1){HX_NEED(hx_decode(f,s,&b,name,d+52,packed,plain,false));}
   else HX_NEED(hx_emit(f,s,&b,name,d+52,packed));
   if(d+54+packed>maximum)maximum=d+54+packed;
  }
 }
 HX_NEED(maximum==b.n);xx_rt_snprintf(text,sizeof(text),"Format: HxC AFI 0.%u\nCylinder range: %u..%u\nSide range: %u..%u\nTrack sides: %u\nRepresentation: complete descriptors/CRC blocks and decoded uncompressed/zlib data; reserved RLE/LZW retained opaque\nIntegrity: every header/info/string/index/track/data CRC-16, nonoverlapping extents, exact zlib sizes and Adler-32\n",b.p[17],lo,hi,ls,hs,count);
 HX_NEED(hx_text(f,s,&b,text));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
HX_API(hxc_afi,XX_FILE_TYPE_HXC_AFI,"afi")
