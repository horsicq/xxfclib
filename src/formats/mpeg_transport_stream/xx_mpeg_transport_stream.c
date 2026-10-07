/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/mpegts.c
 * 188-byte MPEG transport packets, one program, single-packet PAT/PMT and optional SDT sections with complete lengths and MPEG CRC32. Packet/adaptation/PES-start framing, PID declarations and continuity are checked; at most4096 packets. Original encoded transport packets are exported;192/204-byte variants, fragmented tables, encryption, multiple programs and codec decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/mpeg_transport_stream/xx_mpeg_transport_stream.h"
#include "../audio_dolby_ac3/xx_ninth_media.h"
static bool ts_descriptors(const uint8_t *b,uint64_t p,uint64_t end) { while(p<end){uint32_t len;if(!ng_span(p,2,end))return false;len=b[p+1];p+=2;if(!ng_span(p,len,end))return false;p+=len;}return p==end; }
static bool ts_pcr(const uint8_t *p) { return (p[4]&0x7e)==0x7e&&(((p[4]&1)<<8)|p[5])<300; }
static bool ng_quick(Abstractformat *f,uint64_t n) { uint8_t h;if(n<564||n%188||n/188>4096)return false;return pm_read(f,0,&h,1)&&h==71&&pm_read(f,188,&h,1)&&h==71&&pm_read(f,376,&h,1)&&h==71; }
static bool ng_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint8_t counters[8192],declared[8192],started[8192];uint32_t remaining[8192];uint16_t pmt=8191,program=0;unsigned pat_seen=0,pmt_seen=0,pes_seen=0,packets=0;uint64_t at;char label[40];
 if(n<564||n%188||n/188>4096) {return false; } xx_rt_memset(counters,255,sizeof(counters));xx_mem_zero(declared,sizeof(declared));xx_mem_zero(remaining,sizeof(remaining));xx_mem_zero(started,sizeof(started));
 for(at=0;at<n;at+=188){uint64_t p=at+4,end=at+188;uint32_t pid,control,cc;bool reset=false,pusi;if(ng_stop(pd)||b[at]!=71||(b[at+1]&0x80)||(b[at+3]&0xc0)||!(control=(b[at+3]>>4)&3))return false;pid=((b[at+1]&31)<<8)|b[at+2];pusi=(b[at+1]&0x40)!=0;cc=b[at+3]&15;
  if(control&2){uint32_t bytes,flags=0;uint64_t ae;if(p==end)return false;bytes=b[p++];if(bytes>end-p)return false;ae=p+bytes;if(bytes){flags=b[p++];reset=(flags&128)!=0;if(flags&16){if(!ng_span(p,6,ae)||!ts_pcr(b+p))return false;p+=6;}if(flags&8){if(!ng_span(p,6,ae)||!ts_pcr(b+p))return false;p+=6;}if(flags&4){if(p==ae)return false;++p;}if(flags&2){if(p==ae)return false;bytes=b[p++];if(!ng_span(p,bytes,ae))return false;p+=bytes;}if(flags&1)return false;while(p<ae)if(b[p++]!=255)return false;}if(control==2&&p!=end)return false;}
  if(control&1){if(p>=end)return false;if(pid!=8191&&counters[pid]!=255&&!reset&&cc!=((uint32_t)(counters[pid]+1)&15U))return false;counters[pid]=(uint8_t)cc;
   if(pid==0||pid==17||pid==pmt){uint64_t se,sp;uint32_t size;if(!pusi||b[p++]||!ng_span(p,3,end)||(b[p+1]&0xf0)!=(pid==17?0xf0:0xb0))return false;size=((b[p+1]&15)<<8)|b[p+2];se=p+3+size;if(size<9||!ng_span(p,3+size,end)||ng_crc_mpeg(b+p,3+size)||!(b[p+5]&1)||(b[p+5]&0xc0)!=0xc0||b[p+6]||b[p+7])return false;for(sp=se;sp<end;++sp)if(b[sp]!=255)return false;
    if(pid==0){uint16_t id,next;if(b[p]||size!=13||(b[p+10]&0xe0)!=0xe0||!(id=xx_data_get_u16(b+p+8, 2, 0, true))||(next=xx_data_get_u16(b+p+10, 2, 0, true)&8191)<32||next==8191||(pat_seen&&(program!=id||pmt!=next)))return false;program=id;pmt=next;++pat_seen;}
    else if(pid==17){uint64_t q=p+11;if(b[p]!=0x42||size<12)return false;while(q<se-4){uint32_t bytes;if(!ng_span(q,5,se-4)||(b[q+2]&0xfc)!=0xfc)return false;bytes=((b[q+3]&15)<<8)|b[q+4];q+=5;if(!ng_span(q,bytes,se-4)||!ts_descriptors(b,q,q+bytes))return false;q+=bytes;}if(q!=se-4)return false;}
    else {uint64_t q;uint32_t bytes;unsigned streams=0;if(b[p]!=2||xx_data_get_u16(b+p+3, 2, 0, true)!=program||size<13||(b[p+8]&0xe0)!=0xe0||(b[p+10]&0xf0)!=0xf0)return false;bytes=xx_data_get_u16(b+p+10, 2, 0, true)&4095;q=p+12;if(!ng_span(q,bytes,se-4)||!ts_descriptors(b,q,q+bytes))return false;q+=bytes;while(q<se-4){uint32_t id;if(!ng_span(q,5,se-4)||!b[q]||(b[q+1]&0xe0)!=0xe0||(b[q+3]&0xf0)!=0xf0||(id=xx_data_get_u16(b+q+1, 2, 0, true)&8191)<32||id==8191||id==pmt||++streams>32)return false;bytes=xx_data_get_u16(b+q+3, 2, 0, true)&4095;q+=5;if(!ng_span(q,bytes,se-4)||!ts_descriptors(b,q,q+bytes))return false;q+=bytes;declared[id]=1;}if(q!=se-4||!streams||!declared[xx_data_get_u16(b+p+8, 2, 0, true)&8191])return false;++pmt_seen;}
   }
   else if(pid!=8191){uint32_t take=(uint32_t)(end-p);if(!pmt_seen||!declared[pid])return false;if(pusi){uint32_t len,flags,head,need;if(remaining[pid]||!ng_span(p,9,end)||b[p]||b[p+1]||b[p+2]!=1||(b[p+3]!=0xbd&&(b[p+3]<0xc0||b[p+3]>0xef))||(b[p+6]&0xc0)!=0x80||(b[p+6]&0x30)||(b[p+7]&63))return false;len=xx_data_get_u16(b+p+4, 2, 0, true);flags=b[p+7]>>6;head=b[p+8];need=flags==2?5:flags==3?10:0;if(flags==1||head<need||!ng_span(p+9,head,end))return false;if(need&&(!(b[p+9]&1)||!(b[p+11]&1)||!(b[p+13]&1)||(b[p+9]>>4)!=(flags==2?2:3)))return false;if(need==10&&(!(b[p+14]&1)||!(b[p+16]&1)||!(b[p+18]&1)||(b[p+14]>>4)!=1))return false;remaining[pid]=len?len+6:0;if(len&&remaining[pid]<9+head)return false;started[pid]=1;++pes_seen;}else if(!started[pid])return false;
    if(remaining[pid]){if(take>remaining[pid])return false;remaining[pid]-=take;}
   }
  }
  xx_rt_snprintf(label,sizeof(label),"packet-%u-pid-%u.ts",packets++,pid);if(!ng_emit(f,s,label,at,188,n))return false;
 }
 {unsigned i;for(i=0;i<8192;++i)if(remaining[i])return false;}s->size=(int64_t)n;return pat_seen&&pmt_seen&&pes_seen;
}

void xx_mpeg_transport_stream_init(xx_mpeg_transport_stream *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_MPEG_TRANSPORT_STREAM,"ts");}}
xx_mpeg_transport_stream *xx_mpeg_transport_stream_create(xx_io_device *d,int64_t at) {xx_mpeg_transport_stream *r=(xx_mpeg_transport_stream *)xx_mem_alloc(sizeof(*r));if(r)xx_mpeg_transport_stream_init(r,d,at);return r;}
void xx_mpeg_transport_stream_destroy(xx_mpeg_transport_stream *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_mpeg_transport_stream_free(xx_mpeg_transport_stream *r) {if(r){xx_mpeg_transport_stream_destroy(r);xx_mem_free(r);}}
bool xx_mpeg_transport_stream_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_mpeg_transport_stream_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
