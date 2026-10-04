/* SPDX-License-Identifier: MIT. Original Bochs Growing v1/v2 run-map parser.
 * Primary field facts: Bochs Developers Guide, harddisk-redologs.html.
 */
#include "xxfclib/formats/bochs_growing/xx_bochs_growing.h"
#include "../disk_additions/xx_disk_additions.h"
static bool bc_append(Abstractformat *f,da_run **runs,size_t *count,size_t *cap,uint64_t at,uint64_t n,int fill,uint64_t working){da_run *r;size_t next;
 if(!n)return true;if(*count){r=*runs+*count-1U;if(r->fill==fill&&(fill>=0||r->at+r->bytes==at)){if(n>DA_MAX_LOGICAL-r->bytes)return false;r->bytes+=n;return true;}}
 if(*count>=DA_MAX_RUNS)return false;if(*count==*cap){void *p;next=*cap?*cap*2U:32U;if(next>DA_MAX_RUNS)next=DA_MAX_RUNS;if(!hx_limit(f,XX_META_ID_OPT_MEMORY_LIMIT,working+next*sizeof(da_run)))return false;p=xx_mem_realloc(*runs,next*sizeof(da_run));if(!p)return false;*runs=(da_run *)p;*cap=next;}
 r=*runs+(*count)++;r->at=at;r->bytes=n;r->count=1;r->stride=0;r->fill=fill;r->source=NULL;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){uint8_t h[512],*catalog=NULL,*bitmap=NULL,*seen=NULL;da_run *runs=NULL;uint32_t entries,bitmap_n,extent,version,i;uint64_t disk,n=(uint64_t)pm_available(f),start,slot,physical,working;size_t count=0,capacity=0;bool ok=false;
 if(!da_read(f,0,h,512,pd)||xx_rt_memcmp(h,"Bochs Virtual HD Image\0",23)||xx_rt_memcmp(h+32,"Redolog\0",8)||xx_rt_memcmp(h+48,"Growing\0",8)||pm_le32(h+68)!=512U)return false;
 version=pm_le32(h+64);if(version!=0x10000U&&version!=0x20000U)return false;entries=pm_le32(h+72);bitmap_n=pm_le32(h+76);extent=pm_le32(h+80);disk=da_le64(h+(version==0x10000U?84:88));
 if(!entries||entries>1048576U||!bitmap_n||bitmap_n>4096U||(bitmap_n&(bitmap_n-1U))||extent!=(uint64_t)bitmap_n*4096U||!disk||(disk&511U)||disk>DA_MAX_LOGICAL||(uint64_t)entries*extent<disk||!hx_limit(f,XX_META_ID_OPT_MAX_MEMBER_SIZE,disk))return false;
 start=512U+(uint64_t)entries*4U;slot=(((uint64_t)bitmap_n+511U)/512U)*512U+extent;if(n<start||(n-start)%slot)return false;physical=(n-start)/slot;if(physical>entries)return false;working=(uint64_t)entries*4U+bitmap_n+(physical?physical:1U);
 if(!hx_limit(f,XX_META_ID_OPT_MEMORY_LIMIT,working))return false;catalog=(uint8_t *)xx_mem_alloc((size_t)entries*4U);bitmap=(uint8_t *)xx_mem_alloc(bitmap_n);seen=(uint8_t *)xx_mem_alloc(physical?(size_t)physical:1U);if(!catalog||!bitmap||!seen)goto done;xx_mem_zero(seen,physical?(size_t)physical:1U);
 if(!da_read(f,512,catalog,(size_t)entries*4U,pd))goto done;
 for(i=0;i<entries;++i){uint32_t where=pm_le32(catalog+i*4U);uint64_t logical=(uint64_t)i*extent,bytes=logical<disk?(disk-logical<extent?disk-logical:extent):0U,sector;
  if(!da_poll(pd))goto done;if(where==UINT32_MAX){if(bytes&&!bc_append(f,&runs,&count,&capacity,0,bytes,0,working))goto done;continue;}
  if(!bytes||where>=physical||seen[where])goto done;seen[where]=1U;if(!da_read(f,start+(uint64_t)where*slot,bitmap,bitmap_n,pd))goto done;
  for(sector=0;sector<(uint64_t)bitmap_n*8U;++sector){bool present=(bitmap[sector/8U]>>(sector&7U))&1U;uint64_t in=sector*512U;if(in>=bytes){if(present)goto done;continue;}
   if(!bc_append(f,&runs,&count,&capacity,start+(uint64_t)where*slot+slot-extent+in,512U,present?-1:0,working))goto done;if(!(sector&255U)&&!da_poll(pd))goto done;
  }
 }
 for(i=0;i<physical;++i)if(!seen[i])goto done;
 if(!hx_limit(f,XX_META_ID_OPT_MEMORY_LIMIT,working+capacity*sizeof(da_run)+sizeof(da_map)+count*sizeof(da_run))||!da_add(f,s,"descriptor-and-catalog.bochs",0,start)||!da_map_add(f,s,"logical-disk.img",runs,count))goto done;s->size=(int64_t)n;ok=true;
done:if(catalog)xx_mem_free(catalog);if(bitmap)xx_mem_free(bitmap);if(seen)xx_mem_free(seen);if(runs)xx_mem_free(runs);return ok;
}
DA_API(bochs_growing,XX_FILE_TYPE_BOCHS_GROWING,"img")
