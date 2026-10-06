/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/mdl_load.c
 * DigiTrakker MDL1.0/1.1 typed chunks with IN/PA/TR/II/IS/SA and optional VE/PE/FE/ME, up to256 patterns/instruments/samples and4096 tracks. Checks packed track commands, instrument/sample references, envelope records, exact stored or length-framed packed sample extents. Exports complete original chunks; compressed sample bytes and effects remain encoded, no decoding/playback.256MiB cap.
 */
#include "xxfclib/formats/tracker_mdl/xx_tracker_mdl.h"
#include "../tracker_mod/xx_eighth_media.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 uint8_t h[5],head[6],b[96],q[4],orders[256],sampleids[256]={0},instrumentids[256]={0},insrefs[256]={0},samplerefs[256]={0}; uint32_t lengths[256]={0},packing[256]={0}; uint32_t channels=0,patterns=0,tracks=0,samples=0,instruments=0,used=0,maxtrack=0,no=0,i,j,count=0; fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0}; char label[40];
 if(!fd_get(&c,h,5) || xx_rt_memcmp(h,"DMDL",4) || (h[4]!=0x10 && h[4]!=0x11)) return false;
 while(c.at<c.end) { uint64_t start=c.at; uint32_t n,flag; fd_cursor d; if(++count>32 || !fd_get(&c,head,6) || !fd_range(c.at,n=pm_le32(head+2),c.end) || !n) return false; d=c; d.end=c.at+n;
  if(!xx_rt_memcmp(head,"IN",2)) { flag=1; if(used || n<91 || !fd_get(&d,b,91) || !(no=pm_le16(b+52)) || no>256 || pm_le16(b+54)>=no || !b[57] || b[58]<32 || !fd_get(&d,orders,no)) return false; for(j=0;j<32 && !(b[59+j]&0x80);++j) if(b[59+j]>127) return false; channels=j; if(!channels) return false; }
  else if(!xx_rt_memcmp(head,"PA",2)) { flag=2; if(!(used&1) || !fd_get(&d,b,1) || !(patterns=b[0])) return false; for(i=0;i<patterns;++i) { if(!fd_get(&d,b,18) || !b[0] || b[0]>32) return false; for(j=0;j<b[0];++j) { if(!fd_get(&d,q,2)) return false; if(pm_le16(q)>maxtrack) maxtrack=pm_le16(q); } } for(i=0;i<no;++i) if(orders[i]>=patterns) return false; }
  else if(!xx_rt_memcmp(head,"TR",2)) { flag=4; if(!(used&2) || !fd_get(&d,q,2) || !(tracks=pm_le16(q)) || tracks>4096 || maxtrack>tracks) return false;
   for(i=0;i<tracks;++i) { uint32_t row=0; uint64_t end; if(!fd_get(&d,q,2) || !fd_range(d.at,pm_le16(q),d.end)) return false; end=d.at+pm_le16(q); while(d.at<end) { uint32_t tag,mode,k; if(row>255 || !fd_get(&d,b,1)) return false; tag=b[0]; mode=tag&3; if(mode==0) row+=(tag>>2)+1; else if(mode==1) { if(!row || row+(tag>>2)>255) return false; row+=(tag>>2)+1; } else if(mode==2) { if((tag>>2)==row) return false; ++row; } else { for(k=2;k<8;++k) if(tag&(1U<<k)) { if(d.at>=end || !fd_get(&d,b,1)) return false; if(k==3) insrefs[b[0]]=1; } ++row; } } if(d.at!=end || row>256) return false; }
  }
  else if(!xx_rt_memcmp(head,"II",2)) { flag=8; if(!fd_get(&d,b,1) || !(instruments=b[0])) return false; for(i=0;i<instruments;++i) { uint32_t subs,id; if(!fd_get(&d,b,34) || !(id=b[0]) || instrumentids[id] || !(subs=b[1]) || subs>64) return false; instrumentids[id]=1; for(j=0;j<subs;++j) { if(!fd_get(&d,b,14) || !b[0] || b[1]>119 || b[4]>127 || b[12]) return false; samplerefs[b[0]]=1; } } }
  else if(!xx_rt_memcmp(head,"IS",2)) { flag=16; if(!fd_get(&d,b,1) || !(samples=b[0])) return false; for(i=0;i<samples;++i) { uint32_t id,len,loop,nloop; if(!fd_get(&d,b,59) || !(id=b[0]) || sampleids[id] || !(len=pm_le32(b+45)) || len>16777216 || !pm_le32(b+41) || !em_loop(loop=pm_le32(b+49),nloop=pm_le32(b+53),len) || (b[58]&~15U) || ((b[58]&1) && ((len|loop|nloop)&1))) return false; sampleids[id]=1; lengths[i]=len; packing[i]=(b[58]>>2)&3; if(packing[i]==3 || (packing[i]==1 && (b[58]&1)) || (packing[i]==2 && !(b[58]&1))) return false; } }
  else if(!xx_rt_memcmp(head,"SA",2)) { flag=32; if(!(used&16)) return false; for(i=0;i<samples;++i) { uint32_t len=lengths[i]; if(packing[i]) { if(!fd_get(&d,q,4) || !(len=pm_le32(q)) || len>16777216) return false; } if(!fd_skip(&d,len)) return false; } }
  else if(!xx_rt_memcmp(head,"VE",2) || !xx_rt_memcmp(head,"PE",2) || !xx_rt_memcmp(head,"FE",2)) { uint32_t ne; uint8_t ids[64]={0};flag=!xx_rt_memcmp(head,"VE",2) ? 64:!xx_rt_memcmp(head,"PE",2) ? 128:256; if(!fd_get(&d,b,1) || (ne=b[0])>64 || n!=1U+(uint64_t)ne*33) return false; for(i=0;i<ne;++i) { if(!fd_get(&d,b,33) || b[0]>=64 || ids[b[0]] || (b[31]&15U)>14 || (b[32]&15U)>14 || (b[32]>>4)>14) return false;ids[b[0]]=1; } }
  else if(!xx_rt_memcmp(head,"ME",2)) { flag=512; if(n>1048576 || !fd_skip(&d,n)) return false; }
  else return false;
  if(used&flag) { return false; } used|=flag; if(flag!=1 && d.at!=d.end) return false; xx_rt_snprintf(label,sizeof(label),"chunk-%c%c.bin",head[0],head[1]); if(!em_emit(f,s,label,start,6U+(uint64_t)n,c.end)) return false; c.at=d.end;
 }
 if((used&63)!=63 || !patterns || !channels) { return false; } for(i=1;i<256;++i) if((insrefs[i] && !instrumentids[i]) || (samplerefs[i] && !sampleids[i])) return false; s->size=(int64_t)c.at; return true;
}

void xx_tracker_mdl_init(xx_tracker_mdl *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_MDL,"mdl"); } }
xx_tracker_mdl *xx_tracker_mdl_create(xx_io_device *d,int64_t b) { xx_tracker_mdl *r=(xx_tracker_mdl *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_mdl_init(r,d,b); return r; }
void xx_tracker_mdl_destroy(xx_tracker_mdl *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_mdl_free(xx_tracker_mdl *r) { if(r) { xx_tracker_mdl_destroy(r); xx_mem_free(r); } }
bool xx_tracker_mdl_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_mdl_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
