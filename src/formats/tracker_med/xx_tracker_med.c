/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/mmd1_load.c
 * OctaMED MMD0/MMD1 one-song containers, up to255 patterns/63 stored mono8-bit samples/32 channels/3200 rows. Checks pointer tables, disjoint metadata/pattern/sample extents, note/sample references, order list and repeat ranges. Optional80-byte expansion supports bounded instrument extension/name records, annotation and song name only. Exports original song/tables/patterns/sample/metadata records. MMD2/MMD3, synthetic/multioctave/packed/stereo/external samples, linked songs and other expansion pointers rejected; no playback.256MiB cap.
 */
#include "xxfclib/formats/tracker_med/xx_tracker_med.h"
#include "../tracker_mod/xx_eighth_media.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 uint8_t h[52],song[788],p[8],b[1024];uint32_t total,so,pa,sa,np,ns,no,i,exp;bool v1;fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0};char label[40];
 if(!fd_get(&c,h,52) || (xx_rt_memcmp(h,"MMD0",4) && xx_rt_memcmp(h,"MMD1",4)) || (total=xx_data_get_u32(h+4, 4, 0, true))>c.end || total>268435456 || total<52 || h[51] || xx_data_get_u32(h+20, 4, 0, true) || xx_data_get_u32(h+28, 4, 0, true) || xx_data_get_u32(h+36, 4, 0, true)) { return false; } v1=h[3]=='1';so=xx_data_get_u32(h+8, 4, 0, true);pa=xx_data_get_u32(h+16, 4, 0, true);sa=xx_data_get_u32(h+24, 4, 0, true);exp=xx_data_get_u32(h+32, 4, 0, true);c.end=total;
 if(so<52 || !fd_range(so,788,total) || !pm_read(f,so,song,788) || !(np=xx_data_get_u16(song+504, 2, 0, true)) || np>255 || !(no=xx_data_get_u16(song+506, 2, 0, true)) || no>256 || (ns=song[787])>63 || !xx_data_get_u16(song+764, 2, 0, true) || !song[769] || song[786]>64 || pa<52 || (ns && sa<52) || !em_emit(f,s,"descriptor.bin",0,52,total) || !em_emit(f,s,"song.bin",so,788,total) || !em_emit(f,s,"pattern-pointers.bin",pa,(uint64_t)np*4,total) || (ns && !em_emit(f,s,"sample-pointers.bin",sa,(uint64_t)ns*4,total))) return false;
 for(i=0;i<no;++i) { if(song[508+i]>=np) return false; } for(i=0;i<16;++i) if(song[770+i]>64) return false;
 for(i=0;i<np;++i) {uint32_t off,ch,rows,stride=v1 ? 4:3;uint64_t n; if(fd_stop(pd) || !pm_read(f,pa+(int64_t)i*4,p,4) || (off=xx_data_get_u32(p, 4, 0, true))<52 || !pm_read(f,off,p,v1 ? 8:2)) return false;ch=v1 ? xx_data_get_u16(p, 2, 0, true):p[0];rows=1U+(v1 ? xx_data_get_u16(p+2, 2, 0, true):p[1]);if(!ch || ch>32 || rows>3200 || (v1 && xx_data_get_u32(p+4, 4, 0, true))) return false;n=(uint64_t)ch*rows*stride;c.at=off+(v1 ? 8U:2U);if(!fd_range(c.at,n,total)) return false;
  {fd_cursor d=c;d.end=c.at+n;while(d.at<d.end) {uint32_t count=(uint32_t)((d.end-d.at)/stride),k;if(count>256) count=256;if(!fd_get(&d,b,(size_t)count*stride)) return false;for(k=0;k<count;++k) {uint8_t *e=b+k*stride;uint32_t id=v1 ? e[1]&63U:((e[0]&128U)>>3)|((e[0]&64U)>>1)|(e[1]>>4);if(id>ns || (v1 && (e[0]&0x7f)>84)) return false;} } }
  xx_rt_snprintf(label,sizeof(label),"pattern-%u.bin",i);if(!em_emit(f,s,label,off,n+(v1 ? 8U:2U),total)) return false;
 }
 for(i=0;i<ns;++i) {uint32_t off,len,a,z;if(!pm_read(f,sa+(int64_t)i*4,p,4)) return false;if(!(off=xx_data_get_u32(p, 4, 0, true))) continue;if(off<52 || !pm_read(f,off,p,6) || xx_data_get_u16(p+4, 2, 0, true) || (len=xx_data_get_u32(p, 4, 0, true))>16777216 || (song[i*8+6]>64) || (song[i*8+4] || song[i*8+5])) return false;a=(uint32_t)xx_data_get_u16(song+i*8, 2, 0, true)*2;z=(uint32_t)xx_data_get_u16(song+i*8+2, 2, 0, true)*2;if(z>2 && !em_loop(a,z,len)) return false;xx_rt_snprintf(label,sizeof(label),"sample-%u.bin",i+1);if(!em_emit(f,s,label,off,6U+(uint64_t)len,total)) return false; }
 if(exp) {uint32_t off,n,entries,width;if(exp<52 || !pm_read(f,exp,b,80) || xx_data_get_u32(b, 4, 0, true) || xx_data_get_u32(b+32, 4, 0, true) || xx_data_get_u32(b+40, 4, 0, true) || !em_zero(b+52,28) || !em_emit(f,s,"expansion.bin",exp,80,total)) return false;
  off=xx_data_get_u32(b+4, 4, 0, true);entries=xx_data_get_u16(b+8, 2, 0, true);width=xx_data_get_u16(b+10, 2, 0, true);if(entries) {if(entries>ns || width<2 || width>22 || off<52 || !em_emit(f,s,"instrument-extensions.bin",off,(uint64_t)entries*width,total)) return false;}else if(off) return false;
  off=xx_data_get_u32(b+20, 4, 0, true);entries=xx_data_get_u16(b+24, 2, 0, true);width=xx_data_get_u16(b+26, 2, 0, true);if(entries) {if(entries>ns || width<40 || width>256 || off<52 || !em_emit(f,s,"instrument-names.bin",off,(uint64_t)entries*width,total)) return false;}else if(off) return false;
  off=xx_data_get_u32(b+12, 4, 0, true);n=xx_data_get_u32(b+16, 4, 0, true);if(n) {if(n>1048576 || off<52 || !em_emit(f,s,"annotation.bin",off,n,total)) return false;}else if(off) return false;
  off=xx_data_get_u32(b+44, 4, 0, true);n=xx_data_get_u32(b+48, 4, 0, true);if(n) {if(n>4096 || off<52 || !em_emit(f,s,"song-name.bin",off,n,total)) return false;}else if(off) return false;
 }
 s->size=total;return true;
}

void xx_tracker_med_init(xx_tracker_med *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_MED,"med"); } }
xx_tracker_med *xx_tracker_med_create(xx_io_device *d,int64_t b) { xx_tracker_med *r=(xx_tracker_med *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_med_init(r,d,b); return r; }
void xx_tracker_med_destroy(xx_tracker_med *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_med_free(xx_tracker_med *r) { if(r) { xx_tracker_med_destroy(r); xx_mem_free(r); } }
bool xx_tracker_med_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_med_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
