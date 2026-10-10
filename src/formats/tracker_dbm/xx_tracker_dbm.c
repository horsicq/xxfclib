/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/dbm_load.c
 * DigiBooster Pro2 DBM0 with NAME/INFO/SONG/INST/PATT/SMPL complete chunk tables, one song, up to256 patterns/instruments/samples and64 channels. Checks row packet fields, instrument/sample references, sample formats8/16/32-bit, loop spans and complete chunk consumption, with at most four zero row-padding bytes per pattern. Exports encoded chunks without playback. Envelope/echo/other extension chunks unsupported.256MiB cap.
 */
#include "xxfclib/formats/tracker_dbm/xx_tracker_dbm.h"
#include "../common/xx_tracker_components.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 uint8_t h[8],b[64],q[8]; uint32_t ni=0,ns=0,np=0,ch=0,used=0,count=0,i,j,samplelen[256]={0}; uint16_t instsample[256];uint32_t loopa[256],loopn[256],loopflags[256]; binary_cursor c={f,0,(uint64_t)pm_available(f),pd,0}; char label[40];
 if(!binary_get(&c,h,8) || xx_rt_memcmp(h,"DBM0",4) || h[4]!=2 || h[6] || h[7] || !tracker_component_emit(f,s,"descriptor.bin",0,8,c.end)) return false;
 while(c.at<c.end) { uint64_t start=c.at; uint32_t n,flag; binary_cursor d; if(++count>16 || !binary_get(&c,h,8) || !binary_range(c.at,n=xx_data_get_u32(h+4, 4, 0, true),c.end)) return false; d=c;d.end=c.at+n;
  if(!xx_rt_memcmp(h,"NAME",4)) { flag=1; if(used || n!=44 || !binary_skip(&d,n)) return false; }
  else if(!xx_rt_memcmp(h,"INFO",4)) { flag=2; if(!(used&1) || n!=10 || !binary_get(&d,b,10) || !(ni=xx_data_get_u16(b, 2, 0, true)) || ni>256 || !(ns=xx_data_get_u16(b+2, 2, 0, true)) || ns>256 || xx_data_get_u16(b+4, 2, 0, true)!=1 || !(np=xx_data_get_u16(b+6, 2, 0, true)) || np>256 || !(ch=xx_data_get_u16(b+8, 2, 0, true)) || ch>64) return false; }
  else if(!xx_rt_memcmp(h,"SONG",4)) { uint32_t no; flag=4; if(!(used&2) || !binary_get(&d,b,46) || !(no=xx_data_get_u16(b+44, 2, 0, true)) || no>256 || n!=46U+no*2) return false;for(i=0;i<no;++i) if(!binary_get(&d,q,2) || xx_data_get_u16(q, 2, 0, true)>=np) return false; }
  else if(!xx_rt_memcmp(h,"INST",4)) { flag=8;if(!(used&2) || n!=(uint64_t)ni*50) return false; for(i=0;i<ni;++i) { if(!binary_get(&d,b,50) || !(instsample[i]=xx_data_get_u16(b+30, 2, 0, true)) || instsample[i]>ns || xx_data_get_u16(b+32, 2, 0, true)>64 || !xx_data_get_u32(b+34, 4, 0, true) || (xx_data_get_u16(b+48, 2, 0, true)&~3U)) return false;loopa[i]=xx_data_get_u32(b+38, 4, 0, true);loopn[i]=xx_data_get_u32(b+42, 4, 0, true);loopflags[i]=xx_data_get_u16(b+48, 2, 0, true); } }
  else if(!xx_rt_memcmp(h,"PATT",4)) { flag=16; if(!(used&2)) return false; for(i=0;i<np;++i) { uint32_t rows,r=0,sz;uint64_t end; if(!binary_get(&d,b,6) || !(rows=xx_data_get_u16(b, 2, 0, true)) || rows>256 || !binary_range(d.at,sz=xx_data_get_u32(b+2, 4, 0, true),d.end)) return false;end=d.at+sz;while(d.at<end) {uint32_t channel,mask; if(!binary_get(&d,b,1)) return false;channel=b[0];if(!channel) {if(r<rows) ++r;else if(end-d.at>3) return false;continue;} if(channel>ch || r>=rows || d.at>=end || !binary_get(&d,b,1) || ((mask=b[0])&~63U)) return false;for(j=0;j<6;++j) if(mask&(1U<<j)) {if(d.at>=end || !binary_get(&d,b,1) || (j==1 && b[0]>ni)) return false;} } if(r!=rows) return false; } }
  else if(!xx_rt_memcmp(h,"SMPL",4)) { flag=32;if(!(used&2)) return false;for(i=0;i<ns;++i) {uint32_t fmt,len; if(!binary_get(&d,b,8) || ((fmt=xx_data_get_u32(b, 4, 0, true))!=1 && fmt!=2 && fmt!=4) || (len=xx_data_get_u32(b+4, 4, 0, true))>16777216 || !binary_skip(&d,(uint64_t)len*fmt)) return false;samplelen[i]=len;} }
  else { return false; } if(used&flag || d.at!=d.end) return false;used|=flag;xx_rt_snprintf(label,sizeof(label),"chunk-%c%c%c%c.bin",h[0],h[1],h[2],h[3]);if(!tracker_component_emit(f,s,label,start,8U+n,c.end)) return false;c.at=d.end;
 }
 if(used!=63) { return false; } for(i=0;i<ni;++i) if(loopflags[i] && (!loopn[i] || !tracker_component_loop(loopa[i],loopn[i],samplelen[instsample[i]-1]))) return false;s->size=(int64_t)c.at;return true;
}

void xx_tracker_dbm_init(xx_tracker_dbm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_DBM,"dbm"); } }
xx_tracker_dbm *xx_tracker_dbm_create(xx_io_device *d,int64_t b) { xx_tracker_dbm *r=(xx_tracker_dbm *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_dbm_init(r,d,b); return r; }
void xx_tracker_dbm_destroy(xx_tracker_dbm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_dbm_free(xx_tracker_dbm *r) { if(r) { xx_tracker_dbm_destroy(r); xx_mem_free(r); } }
bool xx_tracker_dbm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_dbm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
