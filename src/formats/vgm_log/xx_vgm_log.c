/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://github.com/vgmrips/vgmplay/blob/master/VGMPlay/vgmspec171.txt
 * VGM 1.00-1.60, documented register writes and bounded YM2612 DAC stream controls, waits and raw PCM blocks; validates END, loop boundaries/nonzero declared sample counts and GD3 UTF-16 framing. A zero-duration loop is inactive as in VGMPlay. Unsupported command families rejected.
 */
#include "xxfclib/formats/vgm_log/xx_vgm_log.h"
#include "../vice_x64/xx_ninth_retro.h"

static bool parse_blob(Abstractformat *f,pm_stream *s,nh_blob *b) {
 uint32_t version,end,data,gd3,loop,at,commands=0,streamend=0; uint64_t samples=0,loopstart=0; bool loopseen=false; char name[48]; uint8_t streams[256]={0},steps[256]={0},bases[256]={0}; uint32_t blocks=0,pcmlen=0;
 if(!nh_range(b,0,64) || xx_rt_memcmp(b->p,"Vgm ",4) || (version=pm_le32(b->p+8))<0x100 || version>0x160 || ((version>>4)&15U)>9 || (version&15U)>9 || pm_le32(b->p+4)>NH_LIMIT-4 || !nh_range(b,0,end=pm_le32(b->p+4)+4) || end<65) return false;
 data=64; if(version>=0x150 && pm_le32(b->p+52)) { if(pm_le32(b->p+52)>end-52) return false; data=52+pm_le32(b->p+52); }
 gd3=pm_le32(b->p+20); if(gd3) { if(gd3>end-20) return false; gd3+=20; }
 loop=pm_le32(b->p+28); if(loop) { if(loop>end-28) return false; loop+=28; }
 if(data<64 || data>=end || (loop && (loop<data || loop>=end)) || (gd3 && gd3<=data) || !nh_emit(f,s,b,"music-descriptor.bin",0,data)) return false;
 at=data;
 while(at<end) {
  uint32_t start=at,z=0; uint8_t op=b->p[at++];
  if(++commands>2000000U || !nh_poll(b)) { return false; } if(start==loop) { loopseen=true; loopstart=samples; }
  if(op==0x66) { streamend=at; break; }
  if(op==0x4f || op==0x50) z=1;
  else if(op>=0x51 && op<=0x5f) z=2;
  else if(op==0x61) { if(end-at<2) return false; samples+=pm_le16(b->p+at); z=2; }
  else if(op==0x62) samples+=735;
  else if(op==0x63) samples+=882;
  else if(op>=0x70 && op<=0x7f) samples+=(op&15U)+1U;
  else if(op>=0x80 && op<=0x8f) samples+=op&15U;
  else if(op==0x67) { if(end-at<6 || b->p[at]!=0x66 || b->p[at+1] || (z=pm_le32(b->p+at+2))>end-at-6 || version<0x150) return false; pcmlen+=z; ++blocks; if(blocks>4096 || pcmlen>NH_LIMIT) return false; z+=6; }
  else if(op==0xe0) { if(version<0x150 || end-at<4 || pm_le32(b->p+at)>pcmlen) return false; z=4; }
  else if(op>=0x90 && op<=0x95) {
   static const uint8_t lengths[6]={4,4,5,10,1,4}; uint32_t id; z=lengths[op-0x90];
   if(version<0x160 || z>end-at) { return false; } id=b->p[at];
   if(id==255U) { if(op!=0x94) return false; }
   else if(op==0x90) { if(b->p[at+1]!=2 || b->p[at+2]>1) return false; streams[id]=1; }
   else if(op==0x91) { if(!(streams[id]&1U) || b->p[at+1] || !(steps[id]=b->p[at+2]) || (bases[id]=b->p[at+3])>=steps[id]) return false; streams[id]|=2; }
   else if(op==0x92) { if(!(streams[id]&1U) || !pm_le32(b->p+at+1) || pm_le32(b->p+at+1)>1000000U) return false; streams[id]|=4; }
   else if(op==0x93) { uint32_t pos=pm_le32(b->p+at+1),len=pm_le32(b->p+at+6),mode=b->p[at+5]; uint64_t span=(uint64_t)steps[id]*(len ? len-1U : 0U)+bases[id]+1U;
    if(streams[id]!=7 || (mode&~0x93U) || ((mode&3U)!=1 && (mode&3U)!=3) || (pos!=UINT32_MAX && (pos>=pcmlen || ((mode&3U)==1 && (!len || span>pcmlen-pos))))) return false; }
   else if(op==0x94) { if(!(streams[id]&1U)) return false; }
   else if(streams[id]!=7 || pm_le16(b->p+at+1)>=blocks || (b->p[at+3]&~0x11U)) return false;
  }
  else return false;
  if(z>end-at || (gd3 && (at>gd3 || z>gd3-at)) || samples>UINT32_MAX) { return false; } at+=z;
 }
 if(!streamend || (loop && !loopseen) || (pm_le32(b->p+24) && samples!=pm_le32(b->p+24)) || (!loop && pm_le32(b->p+32)) || (loop && pm_le32(b->p+32) && samples-loopstart!=pm_le32(b->p+32)) || !nh_emit(f,s,b,"commands.vgmdata",data,streamend-data)) return false;
 if(gd3) {
  uint32_t z,p,strings=0; uint16_t high=0;
  if(gd3!=streamend || end-gd3<12 || xx_rt_memcmp(b->p+gd3,"Gd3 ",4) || pm_le32(b->p+gd3+4)!=0x100 || (z=pm_le32(b->p+gd3+8))!=end-gd3-12 || (z&1U) || z>1048576U) return false;
  for(p=0;p<z;p+=2) { uint16_t c=pm_le16(b->p+gd3+12+p); if(!(p&4095U) && !nh_poll(b)) return false; if(high) { if(c<0xdc00 || c>0xdfff) return false; high=0; } else if(c>=0xd800 && c<=0xdbff) high=c; else if(c>=0xdc00 && c<=0xdfff) return false; else if(!c) ++strings; }
  if(high || strings!=11 || !z || pm_le16(b->p+end-2)) { return false; } xx_rt_snprintf(name,sizeof(name),"metadata.gd3"); if(!nh_emit(f,s,b,name,gd3,end-gd3)) return false;
 } else if(streamend!=end) return false;
 s->size=end; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { nh_blob b; bool ok; if(!nh_load(f,&b,pd)) return false; ok=parse_blob(f,s,&b); xx_mem_free(b.p); return ok; }

void xx_vgm_log_init(xx_vgm_log *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_VGM_LOG,"vgm"); } }
xx_vgm_log *xx_vgm_log_create(xx_io_device *d,int64_t b) { xx_vgm_log *r=(xx_vgm_log *)xx_mem_alloc(sizeof(*r)); if(r) xx_vgm_log_init(r,d,b); return r; }
void xx_vgm_log_destroy(xx_vgm_log *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_vgm_log_free(xx_vgm_log *r) { if(r) { xx_vgm_log_destroy(r); xx_mem_free(r); } }
bool xx_vgm_log_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_vgm_log_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
