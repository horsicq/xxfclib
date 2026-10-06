/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/dbm_load.c
 * DigiBooster Pro2 DBM0 with NAME/INFO/SONG/INST/PATT/SMPL complete chunk tables, one song, up to256 patterns/instruments/samples and64 channels. Checks row packet fields, instrument/sample references, sample formats8/16/32-bit, loop spans and complete chunk consumption, with at most four zero row-padding bytes per pattern. Exports encoded chunks without playback. Envelope/echo/other extension chunks unsupported.256MiB cap.
 */
#include "xxfclib/formats/tracker_dbm/xx_tracker_dbm.h"
#include "../tracker_mod/xx_eighth_media.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 uint8_t h[8],b[64],q[8]; uint32_t ni=0,ns=0,np=0,ch=0,used=0,count=0,i,j,samplelen[256]={0}; uint16_t instsample[256];uint32_t loopa[256],loopn[256],loopflags[256]; fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0}; char label[40];
 if(!fd_get(&c,h,8) || xx_rt_memcmp(h,"DBM0",4) || h[4]!=2 || h[6] || h[7] || !em_emit(f,s,"descriptor.bin",0,8,c.end)) return false;
 while(c.at<c.end) { uint64_t start=c.at; uint32_t n,flag; fd_cursor d; if(++count>16 || !fd_get(&c,h,8) || !fd_range(c.at,n=pm_be32(h+4),c.end)) return false; d=c;d.end=c.at+n;
  if(!xx_rt_memcmp(h,"NAME",4)) { flag=1; if(used || n!=44 || !fd_skip(&d,n)) return false; }
  else if(!xx_rt_memcmp(h,"INFO",4)) { flag=2; if(!(used&1) || n!=10 || !fd_get(&d,b,10) || !(ni=pm_be16(b)) || ni>256 || !(ns=pm_be16(b+2)) || ns>256 || pm_be16(b+4)!=1 || !(np=pm_be16(b+6)) || np>256 || !(ch=pm_be16(b+8)) || ch>64) return false; }
  else if(!xx_rt_memcmp(h,"SONG",4)) { uint32_t no; flag=4; if(!(used&2) || !fd_get(&d,b,46) || !(no=pm_be16(b+44)) || no>256 || n!=46U+no*2) return false;for(i=0;i<no;++i) if(!fd_get(&d,q,2) || pm_be16(q)>=np) return false; }
  else if(!xx_rt_memcmp(h,"INST",4)) { flag=8;if(!(used&2) || n!=(uint64_t)ni*50) return false; for(i=0;i<ni;++i) { if(!fd_get(&d,b,50) || !(instsample[i]=pm_be16(b+30)) || instsample[i]>ns || pm_be16(b+32)>64 || !pm_be32(b+34) || (pm_be16(b+48)&~3U)) return false;loopa[i]=pm_be32(b+38);loopn[i]=pm_be32(b+42);loopflags[i]=pm_be16(b+48); } }
  else if(!xx_rt_memcmp(h,"PATT",4)) { flag=16; if(!(used&2)) return false; for(i=0;i<np;++i) { uint32_t rows,r=0,sz;uint64_t end; if(!fd_get(&d,b,6) || !(rows=pm_be16(b)) || rows>256 || !fd_range(d.at,sz=pm_be32(b+2),d.end)) return false;end=d.at+sz;while(d.at<end) {uint32_t channel,mask; if(!fd_get(&d,b,1)) return false;channel=b[0];if(!channel) {if(r<rows) ++r;else if(end-d.at>3) return false;continue;} if(channel>ch || r>=rows || d.at>=end || !fd_get(&d,b,1) || ((mask=b[0])&~63U)) return false;for(j=0;j<6;++j) if(mask&(1U<<j)) {if(d.at>=end || !fd_get(&d,b,1) || (j==1 && b[0]>ni)) return false;} } if(r!=rows) return false; } }
  else if(!xx_rt_memcmp(h,"SMPL",4)) { flag=32;if(!(used&2)) return false;for(i=0;i<ns;++i) {uint32_t fmt,len; if(!fd_get(&d,b,8) || ((fmt=pm_be32(b))!=1 && fmt!=2 && fmt!=4) || (len=pm_be32(b+4))>16777216 || !fd_skip(&d,(uint64_t)len*fmt)) return false;samplelen[i]=len;} }
  else { return false; } if(used&flag || d.at!=d.end) return false;used|=flag;xx_rt_snprintf(label,sizeof(label),"chunk-%c%c%c%c.bin",h[0],h[1],h[2],h[3]);if(!em_emit(f,s,label,start,8U+n,c.end)) return false;c.at=d.end;
 }
 if(used!=63) { return false; } for(i=0;i<ni;++i) if(loopflags[i] && (!loopn[i] || !em_loop(loopa[i],loopn[i],samplelen[instsample[i]-1]))) return false;s->size=(int64_t)c.at;return true;
}

void xx_tracker_dbm_init(xx_tracker_dbm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_DBM,"dbm"); } }
xx_tracker_dbm *xx_tracker_dbm_create(xx_io_device *d,int64_t b) { xx_tracker_dbm *r=(xx_tracker_dbm *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_dbm_init(r,d,b); return r; }
void xx_tracker_dbm_destroy(xx_tracker_dbm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_dbm_free(xx_tracker_dbm *r) { if(r) { xx_tracker_dbm_destroy(r); xx_mem_free(r); } }
bool xx_tracker_dbm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_dbm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
