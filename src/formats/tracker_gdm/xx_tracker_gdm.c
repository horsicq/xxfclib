/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/gdm_load.c
 * General Digital Music1.0 with157-byte header, disjoint offset-based orders/instruments/patterns/samples and no message/scroller blocks. Up to256 patterns/samples and32 channels. Checks full packed event fields,1-64 row terminators, sample flags/loops (GDM loopEnd minus one, in frames) and physical overlap. Exports descriptor/orders/instruments/patterns and original8/16-bit sample bytes; no playback.256MiB cap.
 */
#include "xxfclib/formats/tracker_gdm/xx_tracker_gdm.h"
#include "../tracker_mod/xx_eighth_media.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 uint8_t h[157],b[62],q[2],orders[256]; uint32_t np,ns,no,i,j,lengths[256],atord,atpat,atins,atsmp; uint64_t measured=157; fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0}; char label[40];
 if(!fd_get(&c,h,157) || xx_rt_memcmp(h,"GDM\xfe",4) || xx_rt_memcmp(h+68,"\r\n\x1aGMFS",7) || h[75]!=1 || h[76] || h[113]>64 || !h[114] || h[115]<32 || !em_zero(h+137,20)) return false;
 for(i=0;i<32;++i) { if(h[81+i]>16 && h[81+i]!=255) return false; } atord=pm_le32(h+118);no=h[122]+1U;atpat=pm_le32(h+123);np=h[127]+1U;atins=pm_le32(h+128);atsmp=pm_le32(h+132);ns=h[136]+1U;
 if(atord<157 || atpat<157 || atins<157 || atsmp<157 || !pm_read(f,atord,orders,no) || !em_emit(f,s,"descriptor.bin",0,157,c.end) || !em_emit(f,s,"orders.bin",atord,no,c.end) || !em_emit(f,s,"instruments.bin",atins,(uint64_t)ns*62,c.end)) { return false; } for(i=0;i<no;++i) if(orders[i]>=np) return false;
 if((uint64_t)atord+no>measured) { measured=(uint64_t)atord+no; } if((uint64_t)atins+ns*62U>measured) measured=(uint64_t)atins+ns*62U;
 for(i=0;i<ns;++i) { uint32_t len,a,z; if(fd_stop(pd) || !pm_read(f,atins+(int64_t)i*62,b,62) || (len=pm_le32(b+45))>16777216 || (b[57]&~15U) || !pm_le16(b+58) || ((b[57]&4) && b[60]>64 && b[60]!=255) || ((b[57]&8) && b[61]>16 && b[61]!=255) || (b[57]&1 && (!(z=pm_le32(b+53)) || (a=pm_le32(b+49))>=z-1 || z-1>(len>>((b[57]&2)?1:0)))) || ((b[57]&2) && (len&1))) return false; lengths[i]=len; }
 c.at=atpat;
 for(i=0;i<np;++i) { uint64_t start=c.at,end; uint32_t rows=0; if(!fd_get(&c,q,2) || pm_le16(q)<3 || !fd_range(start,pm_le16(q),c.end)) return false; end=start+pm_le16(q); { fd_cursor d=c;d.end=end;
  while(d.at<end) { uint32_t tag,fxn=0; if(!fd_get(&d,b,1)) return false; tag=b[0]; if(!tag) { if(++rows>64) return false; continue; } if(rows>=64 || (tag&0x80)) return false; if(tag&0x20) { if(!fd_get(&d,b,2) || b[1]>ns) return false; } if(tag&0x40) do { if(++fxn>4 || !fd_get(&d,b,1)) return false; j=b[0]; if((j&0xc0)!=0xc0 && !fd_get(&d,b,1)) return false; } while(j&0x20); }
  if(!rows) return false; } c.at=end; xx_rt_snprintf(label,sizeof(label),"pattern-%u.bin",i); if(!em_emit(f,s,label,start,end-start,c.end)) return false;
 } if(c.at>measured) measured=c.at; c.at=atsmp;
 for(i=0;i<ns;++i) { if(lengths[i]) { xx_rt_snprintf(label,sizeof(label),"sample-%u.pcm",i);if(!em_take_emit(&c,s,label,lengths[i])) return false; } } if(c.at>measured) measured=c.at; s->size=(int64_t)measured;return true;
}

void xx_tracker_gdm_init(xx_tracker_gdm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_GDM,"gdm"); } }
xx_tracker_gdm *xx_tracker_gdm_create(xx_io_device *d,int64_t b) { xx_tracker_gdm *r=(xx_tracker_gdm *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_gdm_init(r,d,b); return r; }
void xx_tracker_gdm_destroy(xx_tracker_gdm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_gdm_free(xx_tracker_gdm *r) { if(r) { xx_tracker_gdm_destroy(r); xx_mem_free(r); } }
bool xx_tracker_gdm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_gdm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
