/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/rmdec.c
 * RealMedia v0 RMF/PROP/CONT/MDPR/DATA and optional INDX, complete object table, typed stream descriptors and packet lengths/stream references. Original metadata and encoded packets are exported, including the original FFmpeg DATA-size-plus18/eight-zero-byte-footer convention; RMFFv1, multirate/linked DATA sections, encrypted/LIVE layouts and codec decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/realmedia_rm/xx_realmedia_rm.h"
#include "../audio_dolby_ac3/xx_ninth_media.h"
static bool ng_quick(Abstractformat *f,uint64_t n) { uint8_t h[18];return ng_probe(f,n,h,18)&&!xx_rt_memcmp(h,".RMF",4)&&pm_be32(h+4)==18&&!pm_be16(h+8)&&!pm_be32(h+10)&&pm_be32(h+14)>=4&&pm_be32(h+14)<=20; }
static bool ng_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t at=18,data_at=0,index_at=0;uint32_t objects,seen=1,streams=0,declared_streams=0,packets=0,declared_packets=0;uint16_t ids[16];uint32_t times[16];uint64_t packet_at[4096];bool prop=false,cont=false,data=false;char label[48];
 if(n<50||xx_rt_memcmp(b,".RMF",4)||pm_be32(b+4)!=18||pm_be16(b+8)||pm_be32(b+10)||(objects=pm_be32(b+14))<4||objects>20||!ng_emit(f,s,"rmf-header.bin",0,18,n))return false;xx_mem_zero(times,sizeof(times));
 while(at<n){uint32_t size;uint64_t end;if(ng_stop(pd)||!ng_span(at,10,n))return false;size=pm_be32(b+at+4);end=at+size;if(size<10||pm_be16(b+at+8))return false;
  if(!xx_rt_memcmp(b+at,"PROP",4)){if(prop||data||size!=50||!ng_span(at,size,n)||!(declared_streams=pm_be16(b+at+46))||declared_streams>16||pm_be16(b+at+48)&~3U)return false;declared_packets=pm_be32(b+at+26);data_at=pm_be32(b+at+42);index_at=pm_be32(b+at+38);prop=true;}
  else if(!xx_rt_memcmp(b+at,"CONT",4)){unsigned i;uint64_t p=at+10;if(cont||data||!ng_span(at,size,n))return false;for(i=0;i<4;++i){uint32_t len;if(!ng_span(p,2,end))return false;len=pm_be16(b+p);p+=2;if(!ng_span(p,len,end))return false;p+=len;}if(p!=end)return false;cont=true;}
  else if(!xx_rt_memcmp(b+at,"MDPR",4)){uint64_t p=at+40;uint32_t len;unsigned i;if(data||streams>=16||size<46||!ng_span(at,size,n))return false;ids[streams]=pm_be16(b+at+10);for(i=0;i<streams;++i)if(ids[i]==ids[streams])return false;
   if(p>=end)return false;len=b[p++];if(!ng_span(p,len,end))return false;p+=len;if(p>=end)return false;len=b[p++];if(!ng_span(p,len,end))return false;p+=len;if(!ng_span(p,4,end))return false;len=pm_be32(b+p);p+=4;if(len<4||!ng_span(p,len,end)||p+len!=end)return false;
   if(len>=12&&!xx_rt_memcmp(b+p+4,"VIDO",4)){if(len<26||pm_be32(b+p)!=len||!pm_be16(b+p+12)||!pm_be16(b+p+14)||pm_be16(b+p+12)>8192||pm_be16(b+p+14)>8192)return false;}
   else if(len<8||b[p]!='.'||b[p+1]!='r'||b[p+2]!='a'||b[p+3]!=0xfd)return false;++streams;
  }
  else if(!xx_rt_memcmp(b+at,"DATA",4)){uint64_t p=at+18;uint32_t count,i;if(data||!prop||streams!=declared_streams||at!=data_at||!ng_span(at,18,n)||pm_be32(b+at+14)||(count=pm_be32(b+at+10))<1||count>4090||count!=declared_packets)return false;data=true;
   if(!ng_emit(f,s,"data-header.bin",at,18,n))return false;
   for(i=0;i<count;++i){uint32_t bytes,timestamp;uint16_t id;unsigned j;if(ng_stop(pd)||!ng_span(p,12,n)||pm_be16(b+p)||(bytes=pm_be16(b+p+2))<=12||!ng_span(p,bytes,n)||b[p+10]||b[p+11]&~3U)return false;id=pm_be16(b+p+4);timestamp=pm_be32(b+p+6);for(j=0;j<streams;++j)if(ids[j]==id)break;if(j==streams||timestamp<times[j])return false;times[j]=timestamp;packet_at[packets++]=p;xx_rt_snprintf(label,sizeof(label),"packet-%u-stream-%u.rm",i,id);if(!ng_emit(f,s,label,p,bytes,n))return false;p+=bytes;}
   if(end!=p&&end!=p+18)return false;if(end==p+18){if(n-p!=8||!ng_zero(b+p,8)||!ng_emit(f,s,"ffmpeg-footer.bin",p,8,n))return false;end=n;}else end=p;
   at=end;++seen;continue;
  }
  else if(!xx_rt_memcmp(b+at,"INDX",4)){uint32_t count,i;uint16_t id;unsigned j;if(!data||at!=index_at||size<20||!ng_span(at,size,n)||pm_be32(b+at+16)||(count=pm_be32(b+at+10))>packets||size!=20+(uint64_t)count*14)return false;id=pm_be16(b+at+14);for(j=0;j<streams;++j)if(ids[j]==id)break;if(j==streams)return false;for(i=0;i<count;++i){uint64_t p=at+20+(uint64_t)i*14;uint32_t number=pm_be32(b+p+10);if(pm_be16(b+p)||number>=packets||pm_be32(b+p+6)!=packet_at[number]||pm_be16(b+packet_at[number]+4)!=id||pm_be32(b+p+2)!=pm_be32(b+packet_at[number]+6))return false;}index_at=0;}
  else return false;
  if(!ng_span(at,size,n))return false;xx_rt_snprintf(label,sizeof(label),"object-%u-%.4s.bin",seen,b+at);if(!ng_emit(f,s,label,at,size,n))return false;at=end;++seen;
 }
 if(!prop||!cont||!data||streams!=declared_streams||index_at||seen!=objects||packets!=declared_packets)return false;s->size=(int64_t)at;return true;
}

void xx_realmedia_rm_init(xx_realmedia_rm *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_REALMEDIA_RM,"rm");}}
xx_realmedia_rm *xx_realmedia_rm_create(xx_io_device *d,int64_t at) {xx_realmedia_rm *r=(xx_realmedia_rm *)xx_mem_alloc(sizeof(*r));if(r)xx_realmedia_rm_init(r,d,at);return r;}
void xx_realmedia_rm_destroy(xx_realmedia_rm *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_realmedia_rm_free(xx_realmedia_rm *r) {if(r){xx_realmedia_rm_destroy(r);xx_mem_free(r);}}
bool xx_realmedia_rm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_realmedia_rm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
