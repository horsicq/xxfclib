/* SPDX-License-Identifier: MIT. Original parser from primary documented layout facts. */
#include "xxfclib/formats/heathkit_h17/xx_heathkit_h17.h"
#include "../disk_additions/xx_disk_additions.h"

/* Author specification h17disk-v2_0_0.pdf: BE block lengths, H8DB at256. */
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){uint8_t h[8],x[3];uint64_t at=8,n=(uint64_t)pm_available(f),data_at=0,data_n=0,meta_at=0,meta_n=0;uint32_t sides=0,tracks=0,blocks=0;char name[64];
 if(!da_read(f,0,h,8,pd)||xx_rt_memcmp(h,"H17D",4)||h[4]!='2'||h[5]!='0'||h[6]!='0'||h[7]!=255U)return false;
 while(at<n){uint32_t len,i;if(++blocks>4096U||!da_read(f,at,h,8,pd))return false;len=xx_data_get_u32(h+4, 4, 0, true);if(len>n-at-8U)return false;for(i=0;i<4U;++i)if(h[i]<32U||h[i]>126U)return false;
  if(!xx_rt_memcmp(h,"DskF",4)){if(at!=8U||sides||len<2U||len>3U||!da_read(f,at+8U,x,len,pd)||!x[0]||x[0]>2U||(x[1]!=40U&&x[1]!=80U)||(len==3U&&x[2]>1U))return false;sides=x[0];tracks=x[1];}
  else if(!sides)return false;
  else if(!xx_rt_memcmp(h,"H8DB",4)){if(data_at||at+8U!=256U||len!=(uint64_t)sides*tracks*10U*256U)return false;data_at=at+8U;data_n=len;}
  else if(!xx_rt_memcmp(h,"SecM",4)){if(meta_at||len!=(uint64_t)sides*tracks*10U*16U)return false;meta_at=at+8U;meta_n=len;}
  xx_rt_snprintf(name,sizeof(name),"block-%c%c%c%c.h17",h[0],h[1],h[2],h[3]);if(!da_add(f,s,name,at,len+8U))return false;at+=len+8U;
 }
 if(!data_at||!da_add(f,s,"sector-image.h8d",data_at,data_n))return false;
 if(meta_at){uint64_t i;uint8_t m[16];for(i=0;i<meta_n;i+=16U){uint64_t off;if(!da_read(f,meta_at+i,m,16,pd))return false;off=xx_data_get_u32(m, 4, 0, true);if((m[4]&0x7fU)||xx_data_get_u16(m+12, 2, 0, true)<256U)((xx_disk_additions_info *)f)->incomplete=true;if(!off&&(m[4]&0x40U)&&!xx_data_get_u16(m+12, 2, 0, true))continue;if(off<data_at||off>data_at+data_n||256U>data_at+data_n-off||((off-data_at)&255U)||xx_data_get_u16(m+12, 2, 0, true)>256U)return false;}}
 s->size=(int64_t)n;return da_poll(pd);
}
DA_API(heathkit_h17,XX_FILE_TYPE_HEATHKIT_H17,"h17")
