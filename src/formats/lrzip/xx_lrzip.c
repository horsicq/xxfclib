/* SPDX-License-Identifier: MIT. Original LRZIP 0.6 two-stream/RZIP decoder from author format facts.
 * Stored, BZip2, LZO, raw LZMA and zlib blocks are decoded; encrypted and ZPAQ variants fail explicitly. */
#include "xxfclib/formats/lrzip/xx_lrzip.h"
#include "../xx_legacy_archive.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzo/xx_lzo.h"
#include "xxfclib/algo/hash/xx_hash.h"
typedef struct lr_block {uint32_t at,packed,plain,next,stream;} lr_block;
static uint64_t lr_uint(const uint8_t *p,unsigned width){uint64_t v=0;for(unsigned i=0;i<width;++i)v|=(uint64_t)p[i]<<(8*i);return v;}
static int lr_order(const void *a,const void *b){uint32_t x=((const lr_block *)a)->at,y=((const lr_block *)b)->at;return x<y?-1:x>y?1:0;}
static bool lr_decode(ac_blob *b,uint8_t type,uint32_t at,uint32_t packed,uint8_t *out,uint32_t plain){size_t wrote=0;xx_io_device *src=NULL,*dst=NULL;bool ok=false;uint64_t workspace=0;
 if(!ac_poll(b))return false;if(type==3){if(packed!=plain)return false;memcpy(out,b->p+at,plain);return true;}
 if(type==4)workspace=12U*1024U*1024U;else if(type==6){uint32_t dict=pm_le32(b->p+17);if(dict>16U*1024U*1024U)return false;workspace=(uint64_t)dict+1024U*1024U;}else if(type==7)workspace=128U*1024U;else if(type!=5)return ac_error(b,"LRZIP block codec unsupported (ZPAQ/filter variant)");
 if(workspace>b->limit-b->used)return ac_error(b,"LRZIP codec workspace exceeds memory limit");b->used+=workspace;
 if(type==5)ok=xx_lzo1x_decompress(b->p+at,packed,out,plain,&wrote);
 else{src=xx_io_mem_open_ro(b->p+at,packed);dst=xx_io_mem_open(out,plain);if(src&&dst){if(type==4)ok=xx_bzip2_unpack_device(src,0,packed,dst,b->pd);
  else if(type==6)ok=xx_lzma_unpack_device(src,0,packed,b->p+16,5,plain,dst,b->pd);
  else if(type==7){if(packed<6||!xx_zlib_stream_header_is_valid(b->p+at,packed)||(b->p[at+1]&32U))ok=false;else ok=xx_deflate_unpack_device(src,2,packed-6U,dst,false,b->pd);}
  if(ok){int64_t size=xx_io_tell(dst);ok=size==plain;wrote=ok?plain:0;}
 }}if(src)xx_io_close(src);if(dst)xx_io_close(dst);b->used-=workspace;
 if(ok&&type==7)ok=xx_zlib_stream_trailer_matches(b->p+at,packed,out,plain);return ok&&wrote==plain&&ac_poll(b);
}
static bool lr_parse(Abstractformat *f,pm_stream *s,ac_blob *b){uint32_t cursor=24,total=0,capacity,end;uint8_t *out=NULL;uint64_t expected;bool final=false,ok=false,md5;lr_block *blocks=NULL;
 if(b->n<24||memcmp(b->p,"LRZI",4))return false;if(b->p[4]!=0||b->p[5]!=6)return ac_error(b,"LRZIP revision unsupported (supported wire revision 0.6)");
 if(b->p[22])return ac_error(b,"LRZIP encrypted stream requires unsupported key derivation");if(b->p[14]||b->p[15]||b->p[21]>1||b->p[23])return false;expected=lr_uint(b->p+6,8);if(expected>AC_MAX_BYTES)return false;md5=b->p[21]!=0;end=b->n-(md5?16U:0U);if(end<24)return false;capacity=expected?(uint32_t)expected:AC_MAX_BYTES;out=ac_alloc(b,capacity);blocks=(lr_block *)ac_alloc(b,65536U*sizeof(*blocks));if(!out||!blocks)goto done;
 while(!final){unsigned width,head;uint32_t base,hs,count=0,streamsize[2]={0,0},streampos[2]={0,0},chunk_size,readend;uint8_t *streams[2]={NULL,NULL};bool chunk_ok=false;
  if(!ac_poll(b)||cursor>=end||(width=b->p[cursor++])<1||width>8||!ac_span(b,cursor,1U+width))goto chunk_done;final=b->p[cursor++]!=0;if(b->p[cursor-1]>1)goto chunk_done;
  {uint64_t n=lr_uint(b->p+cursor,width);if(n>capacity-total)goto chunk_done;chunk_size=(uint32_t)n;cursor+=width;}
  base=cursor;hs=1U+3U*width;if(base>end||2U*hs>end-base)goto chunk_done;
  for(unsigned j=0;j<2;++j){const uint8_t *initial=b->p+base+j*hs;uint64_t next;if(initial[0]!=3||lr_uint(initial+1,width)||lr_uint(initial+1+width,width))goto chunk_done;next=lr_uint(initial+1+2U*width,width);
   while(next){uint64_t packed,plain,link;uint32_t at;if(count==65536U||next<2U*hs||next>end-base||hs>end-base-next)goto chunk_done;at=base+(uint32_t)next;packed=lr_uint(b->p+at+1,width);plain=lr_uint(b->p+at+1+width,width);link=lr_uint(b->p+at+1+2U*width,width);
    if(!packed||!plain||packed>end-at-hs||plain>AC_MAX_BYTES-streamsize[j]||(link&&link<=next)||link>UINT32_MAX)goto chunk_done;blocks[count].at=at;blocks[count].packed=(uint32_t)packed;blocks[count].plain=(uint32_t)plain;blocks[count].next=(uint32_t)link;blocks[count++].stream=j;streamsize[j]+=(uint32_t)plain;next=link;
   }
  }
  qsort(blocks,count,sizeof(*blocks),lr_order);readend=base+2U*hs;for(uint32_t i=0;i<count;++i){if(blocks[i].at!=readend)goto chunk_done;readend+=hs+blocks[i].packed;}if(readend>end)goto chunk_done;
  streams[0]=ac_alloc(b,streamsize[0]);streams[1]=ac_alloc(b,streamsize[1]);if(!streams[0]||!streams[1])goto chunk_done;
  for(uint32_t i=0;i<count;++i){lr_block *r=blocks+i;unsigned j=r->stream;if(!lr_decode(b,b->p[r->at],r->at+hs,r->packed,streams[j]+streampos[j],r->plain))goto chunk_done;streampos[j]+=r->plain;}
  {uint32_t at=0,literal=0,start=total;for(;;){uint32_t length;if(!ac_poll(b)||streamsize[0]-at<3)goto chunk_done;head=streams[0][at++];length=(uint32_t)lr_uint(streams[0]+at,2);at+=2;if(!head&&!length)break;if(!length||length>capacity-total)goto chunk_done;
    if(!head){if(length>streamsize[1]-literal)goto chunk_done;memcpy(out+total,streams[1]+literal,length);literal+=length;total+=length;}
    else{uint64_t offset;if(streamsize[0]-at<width)goto chunk_done;offset=lr_uint(streams[0]+at,width);at+=width;if(!offset||offset>total)goto chunk_done;while(length--){out[total]=out[total-(uint32_t)offset];++total;}}
   }
   if(total-start!=chunk_size||literal!=streamsize[1])goto chunk_done;
   if(!md5){if(streamsize[0]-at!=4U||pm_le32(streams[0]+at)!=ac_crc32(out+start,total-start,0))goto chunk_done;at+=4;}
   if(at!=streamsize[0])goto chunk_done;
  }
  cursor=readend;chunk_ok=true;
chunk_done:ac_release(b,streams[0],streamsize[0]);ac_release(b,streams[1],streamsize[1]);if(!chunk_ok)goto done;
 }
 if(cursor!=end||(expected&&total!=expected))goto done;if(md5){uint8_t digest[16];if(!xx_md5_memory(out,total,digest)||memcmp(digest,b->p+end,16))goto done;}
 if(!ac_compact(b,&out,capacity,total))goto done;capacity=total;ac_release(b,(uint8_t *)blocks,65536U*sizeof(*blocks));blocks=NULL;ok=ac_memory(f,s,b,"decoded.bin",out,total,b->n-24,1);out=NULL;
done:ac_release(b,out,capacity);ac_release(b,(uint8_t *)blocks,65536U*sizeof(*blocks));return ok;
}
AC_PARSE(lr_parse)
AC_DEFINE(lrzip,XX_FILE_TYPE_LRZIP,"lrz")
