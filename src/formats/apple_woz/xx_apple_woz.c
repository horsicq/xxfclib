/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://applesaucefdc.com/woz/reference2/
 * WOZ1 5.25-inch and WOZ2 5.25-/3.5-inch bitstream images, plus the WOZ2
 * INFO v3/FLUX extension. INFO/TMAP/TRKS and optional FLUX/META; CRC checked
 * when declared. Original bitstream and flux-timing bytes are exported.
 */
#include "xxfclib/formats/apple_woz/xx_apple_woz.h"
#include "../vice_x64/xx_ninth_retro.h"

static bool parse_blob(Abstractformat *f,pm_stream *s,nh_blob *b) {
 uint32_t at=12,info=0,tmap=0,trks=0,trksz=0,flux=0,flux_header=0;
 uint32_t meta=0,metasz=0,i,version,nonempty=0,info_version;
 uint8_t mapped[160]={0},fluxmapped[160]={0};
 nh_span spans[160]; uint32_t count=0; bool ok; char name[48];
 if(!nh_range(b,0,12) || (xx_rt_memcmp(b->p,"WOZ1\xff\x0a\x0d\x0a",8) && xx_rt_memcmp(b->p,"WOZ2\xff\x0a\x0d\x0a",8))) return false;
 version=b->p[3]-'0'; if(pm_le32(b->p+8) && (nh_crc(b,12,b->n-12,&ok)!=pm_le32(b->p+8) || !ok)) return false;
 while(at<b->n) { uint32_t z; if(!nh_range(b,at,8) || (z=pm_le32(b->p+at+4))>b->n-at-8) return false;
  if(!xx_rt_memcmp(b->p+at,"INFO",4)) { if(info || z!=60) return false; info=at+8; }
  else if(!xx_rt_memcmp(b->p+at,"TMAP",4)) { if(tmap || z!=160) return false; tmap=at+8; }
  else if(!xx_rt_memcmp(b->p+at,"TRKS",4)) { if(trks || z<(version==1 ? 6656U : 1280U)) return false; trks=at+8; trksz=z; }
  else if(!xx_rt_memcmp(b->p+at,"FLUX",4)) { if(flux || z!=160U) return false; flux_header=at; flux=at+8; }
  else if(!xx_rt_memcmp(b->p+at,"META",4)) { if(meta || !z || z>1048576U) return false; meta=at+8; metasz=z; }
  else return false; at+=8+z;
 }
 if(info!=20 || tmap!=88 || trks!=256 || b->p[info+2]>1 ||
    b->p[info+3]>1 || b->p[info+4]>1) return false;
 info_version=b->p[info];
 if(version==1) {
  if(info_version!=1U || b->p[info+1]!=1U || flux) return false;
 } else {
  uint32_t disk_type=b->p[info+1],sides=b->p[info+37];
  uint32_t timing=b->p[info+39];
  if((info_version!=2U && info_version!=3U) ||
     (disk_type!=1U && disk_type!=2U) ||
     (disk_type==1U && (sides!=1U || b->p[info+38]>3U ||
                       timing<24U || timing>40U)) ||
     (disk_type==2U && ((sides!=1U && sides!=2U) ||
                        b->p[info+38]!=0U || timing<8U || timing>24U)) ||
     !pm_le16(b->p+info+44)) return false;
  if(flux) {
   if(info_version<3U || (flux_header&511U) ||
      pm_le16(b->p+info+46)!=flux_header/512U ||
      !pm_le16(b->p+info+48)) return false;
  } else if(pm_le16(b->p+info+46) || pm_le16(b->p+info+48)) return false;
 }
 for(i=0;i<160;++i) if(b->p[tmap+i]!=255) { if(b->p[tmap+i]>=160) return false; mapped[b->p[tmap+i]]=1; }
 if(!nh_emit(f,s,b,"disk-descriptor.bin",0,12) || !nh_emit(f,s,b,"info.bin",info,60) || !nh_emit(f,s,b,"track-map.bin",tmap,160)) return false;
 if(flux) {
  uint32_t mapped_flux=0;
  for(i=0;i<160;++i) if(b->p[flux+i]!=255) {
   if(b->p[flux+i]>=160) return false;
   fluxmapped[b->p[flux+i]]=1; ++mapped_flux;
  }
  if(!mapped_flux || !nh_emit(f,s,b,"flux-map.bin",flux,160)) return false;
 }
 if(version==1) {
  uint32_t tracks=trksz/6656U; if(trksz%6656U || tracks>160) return false;
  for(i=0;i<160;++i) { uint32_t a,z,bits,splice; if(i>=tracks) { if(mapped[i]) return false; continue; } a=trks+i*6656; z=pm_le16(b->p+a+6646); bits=pm_le16(b->p+a+6648); splice=pm_le16(b->p+a+6650); if(!z || z>6646 || !bits || bits>z*8 || (splice!=65535U && (splice>bits || b->p[a+6653]<8U || b->p[a+6653]>10U)) || !nh_zero(b->p+a+6654,2)) return false; ++nonempty; xx_rt_snprintf(name,sizeof(name),"track-%u.bits",i); if(!nh_emit(f,s,b,name,a,z)) return false; }
 } else {
  if(!nh_emit(f,s,b,"track-table.bin",trks,1280)) return false;
  for(i=0;i<160;++i) { const uint8_t *p=b->p+trks+i*8; uint32_t a=(uint32_t)pm_le16(p)*512U,z=(uint32_t)pm_le16(p+2)*512U,bits=pm_le32(p+4);
   bool is_flux=fluxmapped[i]!=0;
   if(!a && !z && !bits) { if(mapped[i] || is_flux) return false; continue; }
   if(!a || !z || !bits || (mapped[i] && is_flux) ||
      z/512U>pm_le16(b->p+info+(is_flux ? 48U : 44U)) ||
      (is_flux ? bits>z : bits>z*8U) ||
      a<trks+1280 || a>trks+trksz || z>trks+trksz-a ||
      !nh_disjoint(spans,&count,160,a,z)) return false;
   ++nonempty;
   xx_rt_snprintf(name,sizeof(name),"track-%u.%s",i,is_flux ? "flux" : "bits");
   if(!nh_emit(f,s,b,name,a,is_flux ? bits : (bits+7U)/8U)) return false;
  }
 }
 if(meta) { uint32_t pos; bool tab=false; for(pos=0;pos<metasz;++pos) { uint8_t c=b->p[meta+pos]; if(!(pos&4095U) && !nh_poll(b)) return false; if(c=='\t') { if(tab) return false; tab=true; } else if(c=='\n') { if(!tab) return false; tab=false; } else if(c<32 || c>126) return false; } if(tab || b->p[meta+metasz-1]!='\n' || !nh_emit(f,s,b,"metadata.txt",meta,metasz)) return false; }
 if(!nonempty) return false; s->size=at; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { nh_blob b; bool ok; if(!nh_load(f,&b,pd)) return false; ok=parse_blob(f,s,&b); xx_mem_free(b.p); return ok; }

void xx_apple_woz_init(xx_apple_woz *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_APPLE_WOZ,"woz"); } }
xx_apple_woz *xx_apple_woz_create(xx_io_device *d,int64_t b) { xx_apple_woz *r=(xx_apple_woz *)xx_mem_alloc(sizeof(*r)); if(r) xx_apple_woz_init(r,d,b); return r; }
void xx_apple_woz_destroy(xx_apple_woz *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_apple_woz_free(xx_apple_woz *r) { if(r) { xx_apple_woz_destroy(r); xx_mem_free(r); } }
bool xx_apple_woz_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_apple_woz_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
