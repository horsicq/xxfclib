/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/OpenMPT/openmpt/master/soundlib/Load_psm.cpp
 * New Epic MASI PSM FILE/MAINSONG containers with regular4-byte pattern IDs, up to256 patterns/samples/32 channels and one song. Validates chunk tiling, OPLH playlist/settings opcode framing, packed row extents (including well-framed inactive rows, at most256 total), all used sample identities/loops/rates and order references. Exports original chunks; sample bytes remain delta-coded, no decoding/playback. PSM16/Sinaria, unknown opcodes/chunks and extension modes rejected.256MiB cap.
 */
#include "xxfclib/formats/tracker_psm/xx_tracker_psm.h"
#include "../tracker_mod/xx_eighth_media.h"

static bool em_psmid(const uint8_t *b,uint32_t *id) {unsigned i;bool digit=false;*id=0;if(b[0]!='P') return false;for(i=1;i<4;++i) {if(b[i]>='0' && b[i]<='9') {if(digit && b[i-1]==' ') return false;digit=true;*id=*id*10+b[i]-'0';} else if(b[i]!=' ') return false;}return digit && *id<256;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 uint8_t h[12],b[96],q[8],patids[256]={0},smids[256]={0},samplerefs[256]={0};uint32_t total,count=0,patterns=0,samples=0,ch=0,used=0,orders[256],no=0,i,j;fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0};char label[40];
 if(!fd_get(&c,h,12) || xx_rt_memcmp(h,"PSM ",4) || xx_rt_memcmp(h+8,"FILE",4) || (uint64_t)pm_le32(h+4)+12>c.end || (uint64_t)pm_le32(h+4)+12>268435456) return false;total=pm_le32(h+4)+12;c.end=total;if(!em_emit(f,s,"descriptor.bin",0,12,total)) return false;
 while(c.at<c.end) {uint64_t start=c.at;uint32_t n;fd_cursor d;if(++count>1024 || !fd_get(&c,h,8) || !fd_range(c.at,n=pm_le32(h+4),c.end)) return false;d=c;d.end=c.at+n;
  if(!xx_rt_memcmp(h,"SDFT",4)) {if(used&1 || n!=8 || !fd_get(&d,b,8) || xx_rt_memcmp(b,"MAINSONG",8)) return false;used|=1;}
  else if(!xx_rt_memcmp(h,"TITL",4)) {if(used&2 || n>1024 || !fd_skip(&d,n)) return false;used|=2;}
  else if(!xx_rt_memcmp(h,"PBOD",4)) {uint32_t id,rows;if(!(used&1) || !fd_get(&d,b,10) || pm_le32(b)!=n || !em_psmid(b+4,&id) || patids[id] || !(rows=pm_le16(b+8)) || rows>256) return false;patids[id]=1;++patterns;
   for(i=0;i<rows || d.at<d.end;++i) {uint32_t sz;uint64_t end;if(i>=256 || !fd_get(&d,q,2) || (sz=pm_le16(q))<2 || !fd_range(d.at,sz-2,d.end)) return false;end=d.at+sz-2;{fd_cursor row=d;row.end=end;while(row.at<end) {uint32_t mask;if(!fd_get(&row,q,2) || ((mask=q[0])&15U) || q[1]>=32 || ((used&4) && q[1]>=ch) || !mask) return false;if(q[1]+1U>ch) ch=q[1]+1U;if(mask&128 && !fd_get(&row,q,1)) return false;if(mask&64) {if(!fd_get(&row,q,1)) return false;samplerefs[q[0]]=1;}if(mask&32 && (!fd_get(&row,q,1) || q[0]>127)) return false;if(mask&16) {if(!fd_get(&row,q,2)) return false;if(q[0]==0x29 && !fd_skip(&row,2)) return false;if(q[0]==0x33 && !fd_skip(&row,1)) return false;}}}d.at=end;}
  }
  else if(!xx_rt_memcmp(h,"DSMP",4)) {uint32_t id,len,a,z;if(!(used&1) || !fd_get(&d,b,96) || ((b[0]&~0x80U)) || (id=pm_le16(b+52))>=256 || smids[id] || (len=pm_le32(b+54))>16777216 || n!=96U+(uint64_t)len || b[68]>127 || !pm_le32(b+73) || pm_le32(b+73)>384000) return false;a=pm_le32(b+58);z=pm_le32(b+62);if(b[0]&128 && (a>=len || (z!=UINT32_MAX && (a>z || z>len)))) return false;smids[id]=1;++samples;if(!fd_skip(&d,len)) return false; }
  else if(!xx_rt_memcmp(h,"SONG",4)) {uint32_t subcount=0;bool playlist=false;if(!(used&1) || used&4 || !fd_get(&d,b,11) || b[9]!=1 || !b[10] || b[10]>32) return false;if(ch>b[10]) return false;ch=b[10];used|=4;
   while(d.at<d.end) {uint32_t sn;fd_cursor sub;if(++subcount>16 || !fd_get(&d,q,8) || !fd_range(d.at,sn=pm_le32(q+4),d.end)) return false;sub=d;sub.end=d.at+sn;
    if(!xx_rt_memcmp(q,"OPLH",4)) {uint32_t ops=0,decl;if(playlist || !fd_get(&sub,b,2)) return false;playlist=true;decl=pm_le16(b);while(sub.at<sub.end) {uint32_t op;if(!fd_get(&sub,b,1)) return false;if(!(op=b[0])) {if(sub.at!=sub.end) return false;break;}++ops;if(op==1) {if(no>=256 || !fd_get(&sub,b,4) || !em_psmid(b,&orders[no++])) return false;}else if(op==2) {if(!fd_skip(&sub,4)) return false;}else if(op==3 || op==4) {if(!fd_skip(&sub,op==3 ? 3:2)) return false;}else if(op==5 || op==14) {if(!fd_get(&sub,b,2) || b[0]>=ch) return false;}else if(op==6) {if(!fd_skip(&sub,1)) return false;}else if(op==7 || op==8) {if(!fd_get(&sub,b,1) || !b[0] || (op==8 && b[0]<32)) return false;}else if(op==12) {if(!fd_get(&sub,b,6) || xx_rt_memcmp(b,"\0\xff\0\0\1\0",6)) return false;}else if(op==13) {if(!fd_get(&sub,b,3) || b[0]>=ch || (b[2]!=0 && b[2]!=2 && b[2]!=4)) return false;}else return false;}if(!ops || ops>decl+1U) return false;
    } else if(!xx_rt_memcmp(q,"DATE",4)) {if(sn!=6 || !fd_skip(&sub,sn)) return false;}else if(!xx_rt_memcmp(q,"PATT",4) || !xx_rt_memcmp(q,"DSAM",4)) {if(sn>4096 || !fd_skip(&sub,sn)) return false;}else return false;if(sub.at!=sub.end) return false;d.at=sub.end;
   }if(!playlist || !no) return false;
  }else return false;if(d.at!=d.end) return false;xx_rt_snprintf(label,sizeof(label),"chunk-%c%c%c%c.bin",h[0],h[1],h[2],h[3]);if(!em_emit(f,s,label,start,8U+(uint64_t)n,c.end)) return false;c.at=d.end;
 }
 if((used&5)!=5 || !patterns || !samples ) return false;for(j=0;j<256;++j) if(samplerefs[j] && !smids[j]) return false;for(j=0;j<no;++j) if(!patids[orders[j]]) return false;s->size=total;return true;
}

void xx_tracker_psm_init(xx_tracker_psm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_PSM,"psm"); } }
xx_tracker_psm *xx_tracker_psm_create(xx_io_device *d,int64_t b) { xx_tracker_psm *r=(xx_tracker_psm *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_psm_init(r,d,b); return r; }
void xx_tracker_psm_destroy(xx_tracker_psm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_psm_free(xx_tracker_psm *r) { if(r) { xx_tracker_psm_destroy(r); xx_mem_free(r); } }
bool xx_tracker_psm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_psm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
