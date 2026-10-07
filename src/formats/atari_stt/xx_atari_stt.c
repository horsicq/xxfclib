/* SPDX-License-Identifier: MIT. Original parser of the author's STT Format.txt.
 * Unknown flag sections retain their complete bounded track container.
 * ID CRC values may intentionally model bad sectors and are preserved as data.
 */
#include "xxfclib/formats/atari_stt/xx_atari_stt.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 hx_blob b;hx_range ranges[4096];unsigned nr=0;uint32_t tracks,sides,i;uint64_t index_end,maximum;bool ok=false;char name[96],info[256];
 if(!hx_load(f,&b,pd))return false;
 HX_NEED(hx_tag(&b,0,"STEM",4)&&hx_span(&b,0,14)&&xx_data_get_u16(b.p+4, 2, 0, false)==1);tracks=xx_data_get_u16(b.p+10, 2, 0, false);sides=xx_data_get_u16(b.p+12, 2, 0, false);
 HX_NEED(tracks&&tracks<=86&&sides&&sides<=2);index_end=14+(uint64_t)tracks*sides*6;maximum=index_end;
 HX_NEED(hx_claim(&b,ranges,&nr,0,index_end)&&hx_emit(f,s,&b,"descriptor.stt",0,index_end));
 for(i=0;i<tracks*sides;++i){const uint8_t *e=b.p+14+i*6;uint32_t a=xx_data_get_u32(e, 4, 0, false),z=xx_data_get_u16(e+4, 2, 0, false),flags,section=6,bit;
  HX_NEED(z>=6&&a>=index_end&&hx_claim(&b,ranges,&nr,a,z)&&hx_tag(&b,a,"TRCK",4));flags=xx_data_get_u16(b.p+a+4, 2, 0, false);
  HX_NEED((flags&xx_data_get_u16(b.p+8, 2, 0, false))==xx_data_get_u16(b.p+8, 2, 0, false));
  for(bit=0;bit<16;++bit)if(flags&(1U<<bit)){uint32_t end,k;
   HX_NEED(section+2U<=z);end=xx_data_get_u16(b.p+a+section, 2, 0, false);HX_NEED(end>=section+2U&&end<=z);
   if(bit==0){uint32_t count,table_end;HX_NEED(end>=section+6U);count=xx_data_get_u16(b.p+a+section+4, 2, 0, false);HX_NEED(count<=256);table_end=section+6U+count*10U;HX_NEED(table_end<=end);
    for(k=0;k<count;++k){const uint8_t *q=b.p+a+section+6+k*10;uint32_t off=xx_data_get_u16(q+6, 2, 0, false),len=xx_data_get_u16(q+8, 2, 0, false);HX_NEED(hx_poll(&b)&&off>=table_end&&off<=end&&len<=end-off);
     xx_rt_snprintf(name,sizeof(name),"C%03u-H%u-sector-%03u-ID%03u.bin",i%tracks,i/tracks,k,q[2]);HX_NEED(hx_emit(f,s,&b,name,a+off,len));
    }
   }else if(bit==1){uint32_t off,len;HX_NEED(end>=section+8);off=xx_data_get_u16(b.p+a+section+4, 2, 0, false);len=xx_data_get_u16(b.p+a+section+6, 2, 0, false);HX_NEED(off>=section+8&&off<=end&&len<=end-off);xx_rt_snprintf(name,sizeof(name),"track-C%03u-H%u.raw-bytes",i%tracks,i/tracks);HX_NEED(hx_emit(f,s,&b,name,a+off,len));}
   section=end;
  }
  xx_rt_snprintf(name,sizeof(name),"track-C%03u-H%u.original-stt",i%tracks,i/tracks);HX_NEED(hx_emit(f,s,&b,name,a,z));if((uint64_t)a+z>maximum)maximum=(uint64_t)a+z;
 }
 HX_NEED(maximum==b.n);xx_rt_snprintf(info,sizeof(info),"Format: Steem STT\nCylinders: %u\nSides: %u\nRepresentation: stored sectors/raw sections plus complete original track sections (including unknown flags)\nIntegrity: container spans; recorded ID CRCs are preserved, not asserted valid\n",tracks,sides);
 HX_NEED(hx_text(f,s,&b,info));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
HX_API(atari_stt,XX_FILE_TYPE_ATARI_STT,"stt")
