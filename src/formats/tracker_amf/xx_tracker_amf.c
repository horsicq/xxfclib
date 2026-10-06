/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/amf_load.c
 * DSMI AMF1.4 with bounded logical/physical track mappings,1-32 channels/256 patterns/255 instruments/4096 tracks, complete three-byte track event records, including zero-event physical tracks and original sequential8-bit samples. Checks row/instrument references, sample loops and all physical lengths. Exports descriptor/orders/instruments/map/tracks and PCM. Earlier AMF revisions and Asylum AMF are rejected; no playback.256MiB cap.
 */
#include "xxfclib/formats/tracker_amf/xx_tracker_amf.h"
#include "../tracker_mod/xx_eighth_media.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 uint8_t h[75],b[65],q[4];uint32_t ni,np,nt,ch,i,j,physical=0,lengths[255],maxrows=0;uint64_t at;fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0};char label[40];
 if(!fd_get(&c,h,75) || xx_rt_memcmp(h,"AMF\x0e",4) || !(ni=h[36]) || !(np=h[37]) || !(nt=pm_le16(h+38)) || nt>4096 || !(ch=h[40]) || ch>32 || h[73]<32 || !h[74]) { return false; } if(!em_emit(f,s,"descriptor.bin",0,75,c.end)) return false;
 at=c.at;for(i=0;i<np;++i) {uint32_t rows;if(!fd_get(&c,q,2) || !(rows=pm_le16(q)) || rows>256) return false;if(rows>maxrows) maxrows=rows;for(j=0;j<ch;++j) if(!fd_get(&c,q,2) || pm_le16(q)>nt) return false;}if(!em_emit(f,s,"orders.bin",at,c.at-at,c.end)) return false;
 at=c.at;for(i=0;i<ni;++i) {uint32_t len,a,z;if(!fd_get(&c,b,65) || b[0]>1 || (len=pm_le32(b+50))>16777216 || !pm_le16(b+54) || b[56]>64 || (a=pm_le32(b+57))>len || (z=pm_le32(b+61))>len || (z && a>=z)) return false;lengths[i]=len;}if(!em_emit(f,s,"instruments.bin",at,c.at-at,c.end)) return false;
 at=c.at;for(i=0;i<nt;++i) {if(!fd_get(&c,q,2) || pm_le16(q)>4096) return false;if(pm_le16(q)>physical) physical=pm_le16(q);}if(!physical || !em_emit(f,s,"track-map.bin",at,c.at-at,c.end)) return false;
 for(i=0;i<physical;++i) {uint32_t n;at=c.at;if(!fd_get(&c,q,3) || (n=pm_le16(q))>65535) return false;for(j=0;j<n;++j) {if(!fd_get(&c,q,3)) return false;if(q[0]==255 && q[1]==255 && q[2]==255) {if(j+1!=n) return false;} else if(q[0]>=maxrows || (q[1]==0x80 && q[2]>=ni) || q[1]>0x97) return false;}xx_rt_snprintf(label,sizeof(label),"track-%u.bin",i+1);if(!em_emit(f,s,label,at,c.at-at,c.end)) return false; }
 for(i=0;i<ni;++i) { if(lengths[i]) {xx_rt_snprintf(label,sizeof(label),"sample-%u.pcm",i+1);if(!em_take_emit(&c,s,label,lengths[i])) return false;} } s->size=(int64_t)c.at;return true;
}

void xx_tracker_amf_init(xx_tracker_amf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_AMF,"amf"); } }
xx_tracker_amf *xx_tracker_amf_create(xx_io_device *d,int64_t b) { xx_tracker_amf *r=(xx_tracker_amf *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_amf_init(r,d,b); return r; }
void xx_tracker_amf_destroy(xx_tracker_amf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_amf_free(xx_tracker_amf *r) { if(r) { xx_tracker_amf_destroy(r); xx_mem_free(r); } }
bool xx_tracker_amf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_amf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
