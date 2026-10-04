/* SPDX-License-Identifier: MIT. Original parser from primary documented layout facts. */
#include "xxfclib/formats/wc_disk_image/xx_wc_disk_image.h"
#include "../disk_additions/xx_disk_additions.h"

/* WC DISK IMAGE version0/1, sector flags and CRC16/IBM(seed0). */
static uint16_t wc_crc(const uint8_t *p,size_t n){uint16_t crc=0;size_t i;unsigned k;for(i=0;i<n;++i){crc^=p[i];for(k=0;k<8U;++k)crc=(uint16_t)((crc>>1)^((crc&1U)?0xa001U:0U));}return crc;}
static bool wc_track(Abstractformat *f,pm_stream *s,uint64_t *at,unsigned c,unsigned h,unsigned sectors,xx_pd_struct *pd,const char *name){
 da_run runs[18];uint8_t head[6],body[512];unsigned i;xx_disk_additions_info *r=(xx_disk_additions_info *)f;
 for(i=0;i<sectors;++i){da_run *run=runs+i;if(!da_read(f,*at,head,6,pd)||head[1]!=h||head[2]!=i+1U||head[3]!=c||head[0]>2U)return false;*at+=6U;run->at=*at;run->bytes=512;run->count=1;run->stride=0;run->source=NULL;run->fill=-1;
  if(head[0]==0U){if(!da_read(f,*at,body,512,pd)||wc_crc(body,512)!=pm_le16(head+4))return false;*at+=512;}
  else if(head[0]==1U){run->fill=0;r->incomplete=true;}else run->fill=head[4];
 }
 return da_map_add(f,s,name,runs,sectors);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){uint8_t h[32],x[6];uint64_t at=32;unsigned c,head,i;char name[64];xx_disk_additions_info *r=(xx_disk_additions_info *)f;
 r->incomplete=false;if(!da_read(f,0,h,32,pd)||xx_rt_memcmp(h,"WC DISK IMAGE\x1a\x1a\0",16)||h[16]>1U||!h[17]||h[17]>2U||!h[18]||h[18]>18U||!h[19]||h[19]>80U||(h[24]&~3U))return false;
 for(i=20;i<24U;++i)if(h[i]>1U||(h[i]&&((i-20U)&1U)>=h[17]))return false;
 if(!da_add(f,s,"descriptor.d2f",0,32))return false;
 /* A logical track is a single member; CHS names preserve exact ordering and
  * extra tracks without inventing missing opposite-side data. */
 for(c=0;c<h[19];++c)for(head=0;head<h[17];++head){xx_rt_snprintf(name,sizeof(name),"c%03u-h%u-sectors.img",c,head);if(!wc_track(f,s,&at,c,head,h[18],pd,name))return false;}
 for(i=0;i<4U;++i)if(h[20U+i]){xx_rt_snprintf(name,sizeof(name),"extra-c%03u-h%u-sectors.img",h[19]+i/2U,i&1U);if(!wc_track(f,s,&at,h[19]+i/2U,i&1U,h[18],pd,name))return false;}
 for(i=0;i<2U;++i)if(h[24]&(1U<<i)){uint32_t n;if(!da_read(f,at,x,6,pd)||x[0]!=3U+i)return false;n=pm_le16(x+4);at+=6;if(!da_add(f,s,i?"directory.txt":"comment.txt",at,n))return false;at+=n;}
 if(at!=(uint64_t)pm_available(f))return false;s->size=(int64_t)at;return da_poll(pd);
}
DA_API(wc_disk_image,XX_FILE_TYPE_WC_DISK_IMAGE,"d2f")
