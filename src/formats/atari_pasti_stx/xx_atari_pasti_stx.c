/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Inert encoded components only: no playback, emulation or filesystem recovery.
 */
#include "xxfclib/formats/atari_pasti_stx/xx_atari_pasti_stx.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../nintendo_sdat/xx_twelfth_c.h"

static uint16_t stx_id_crc(const uint8_t *p) {
    uint8_t bytes[8] = {0xa1, 0xa1, 0xa1, 0xfe, 0, 0, 0, 0};
    xx_rt_memcpy(bytes + 4, p, 4);
    return xx_crc16_ccitt_calc(0xffffU, bytes, sizeof(bytes));
}
static bool read_components(Abstractformat *f,pm_stream *s,tc_blob *b) {
 const uint8_t *p=b->p;uint32_t a=16,i,j,tracks;uint8_t seen[256]={0};char label[64];
 if(b->n<16 || xx_rt_memcmp(p,"RSY\0",4) || pm_le16(p+4)!=3 || (pm_le16(p+6)!=1 && pm_le16(p+6)!=0xcc) || pm_le16(p+8) || (p[11]!=0 && p[11]!=2) || !tc_zero(p+12,4) || !p[10]) return false;
 tracks=p[10];if(!tc_emit(f,s,b,"stx-file-descriptor.bin",0,16)) return false;
 for(i=0;i<tracks;++i) {uint32_t z,n,flags,base,maxend,extcount=0;tc_extent ext[256];uint8_t track;
  if(!tc_span(b,a,16) || !tc_work(b,1)) return false;z=pm_le32(p+a);n=pm_le16(p+a+8);flags=pm_le16(p+a+10);track=p[a+14];
  if(z<16 || !tc_span(b,a,z) || !n || n>64 || (flags!=0 && flags!=1 && flags!=0x21) || pm_le32(p+a+4) || (track&127)>85 || seen[track] || p[a+15]) return false;seen[track]=1;
  base=a+16+((flags&1)?16*n:0);if(base>a+z) return false;maxend=base;
  xx_rt_snprintf(label,sizeof(label),"track-%03u-descriptor.bin",track);if(!tc_emit(f,s,b,label,a,base-a)) return false;
  for(j=0;j<n;++j) {uint32_t off,size;
   if(flags&1) {uint32_t q=a+16+j*16;off=pm_le32(p+q);if(p[q+11]>3 || p[q+14] || p[q+15] || p[q+8]!=(track&127) || p[q+9]!=(track>>7) || !p[q+10] || pm_be16(p+q+12)!=stx_id_crc(p+q+8)) return false;size=128U<<p[q+11];}
   else {off=j*512;size=512;}
   if(off>a+z-base || size>a+z-base-off || !tc_claim(b,ext,&extcount,base+off,size,false)) return false;
   xx_rt_snprintf(label,sizeof(label),"track-%03u-sector-%02u.bin",track,j);if(!tc_emit(f,s,b,label,base+off,size)) return false;if(base+off+size>maxend) maxend=base+off+size;
  }if(maxend!=a+z) return false;a+=z;
 }if(a!=b->n) return false;s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { tc_blob b;bool ok;if(!tc_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_atari_pasti_stx_init(xx_atari_pasti_stx *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ATARI_PASTI_STX,"atari_pasti_stx");} }
xx_atari_pasti_stx *xx_atari_pasti_stx_create(xx_io_device *d,int64_t b) { xx_atari_pasti_stx *r=(xx_atari_pasti_stx *)xx_mem_alloc(sizeof(*r));if(r) xx_atari_pasti_stx_init(r,d,b);return r; }
void xx_atari_pasti_stx_destroy(xx_atari_pasti_stx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_atari_pasti_stx_free(xx_atari_pasti_stx *r) { if(r) {xx_atari_pasti_stx_destroy(r);xx_mem_free(r);} }
bool xx_atari_pasti_stx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_atari_pasti_stx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
