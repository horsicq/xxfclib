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
#include "../common/xx_retro_disk_components.h"
#include "../apple_family/xx_apple_gcr.h"

static bool wz_emit(af_work *w,const retro_disk_blob *b,const char *name,uint32_t at,uint32_t size){
 char label[96];if(!retro_disk_range(b,at,size) || w->s->count>=4096U)return false;
 xx_rt_snprintf(label,sizeof(label),"%04u-%s",(unsigned)w->s->count,name);return af_add(w,label,at,size,NULL);
}

static bool parse_blob(Abstractformat *f,pm_stream *s,retro_disk_blob *b,af_work *w) {
 uint32_t at=12,info=0,tmap=0,trks=0,trksz=0,flux=0,flux_header=0;
 uint32_t meta=0,metasz=0,i,version,nonempty=0,info_version;
 uint8_t mapped[160]={0},fluxmapped[160]={0};
 retro_disk_span spans[160]; uint32_t count=0; bool ok; char name[48]; (void)f;
 if(!retro_disk_range(b,0,12) || (xx_rt_memcmp(b->p,"WOZ1\xff\x0a\x0d\x0a",8) && xx_rt_memcmp(b->p,"WOZ2\xff\x0a\x0d\x0a",8))) return false;
 version=b->p[3]-'0'; if(xx_data_get_u32(b->p+8, 4, 0, false) && (retro_disk_crc(b,12,b->n-12,&ok)!=xx_data_get_u32(b->p+8, 4, 0, false) || !ok)) return false;
 while(at<b->n) { uint32_t z; if(!retro_disk_range(b,at,8) || (z=xx_data_get_u32(b->p+at+4, 4, 0, false))>b->n-at-8) return false;
  if(!xx_rt_memcmp(b->p+at,"INFO",4)) { if(info || z!=60) return false; info=at+8; }
  else if(!xx_rt_memcmp(b->p+at,"TMAP",4)) { if(tmap || z!=160) return false; tmap=at+8; }
  else if(!xx_rt_memcmp(b->p+at,"TRKS",4)) { if(trks || z<(version==1 ? 6656U : 1280U)) return false; trks=at+8; trksz=z; }
  else if(!xx_rt_memcmp(b->p+at,"FLUX",4)) { if(flux || z!=160U) return false; flux_header=at; flux=at+8; }
  else if(!xx_rt_memcmp(b->p+at,"META",4)) { if(meta || !z || z>1048576U) return false; meta=at+8; metasz=z; }
  else { return false; } at+=8+z;
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
     !xx_data_get_u16(b->p+info+44, 2, 0, false)) return false;
  if(flux) {
   if(info_version<3U || (flux_header&511U) ||
      xx_data_get_u16(b->p+info+46, 2, 0, false)!=flux_header/512U ||
      !xx_data_get_u16(b->p+info+48, 2, 0, false)) return false;
  } else if(xx_data_get_u16(b->p+info+46, 2, 0, false) || xx_data_get_u16(b->p+info+48, 2, 0, false)) return false;
 }
 for(i=0;i<160;++i) if(b->p[tmap+i]!=255) { if(b->p[tmap+i]>=160) return false; mapped[b->p[tmap+i]]=1; }
 if(!wz_emit(w,b,"disk-descriptor.bin",0,12) || !wz_emit(w,b,"info.bin",info,60) || !wz_emit(w,b,"track-map.bin",tmap,160)) return false;
 if(flux) {
  uint32_t mapped_flux=0;
  for(i=0;i<160;++i) if(b->p[flux+i]!=255) {
   if(b->p[flux+i]>=160) return false;
   fluxmapped[b->p[flux+i]]=1; ++mapped_flux;
  }
  if(!mapped_flux || !wz_emit(w,b,"flux-map.bin",flux,160)) return false;
 }
 if(version==1) {
  uint32_t tracks=trksz/6656U; if(trksz%6656U || tracks>160) return false;
  for(i=0;i<160;++i) { uint32_t a,z,bits,splice; if(i>=tracks) { if(mapped[i]) return false; continue; } a=trks+i*6656; z=xx_data_get_u16(b->p+a+6646, 2, 0, false); bits=xx_data_get_u16(b->p+a+6648, 2, 0, false); splice=xx_data_get_u16(b->p+a+6650, 2, 0, false); if(!z || z>6646 || !bits || bits>z*8 || (splice!=65535U && (splice>bits || b->p[a+6653]<8U || b->p[a+6653]>10U)) || !retro_disk_zero(b->p+a+6654,2)) return false; ++nonempty; xx_rt_snprintf(name,sizeof(name),"track-%u.bits",i); if(!wz_emit(w,b,name,a,z)) return false; }
 } else {
  if(!wz_emit(w,b,"track-table.bin",trks,1280)) return false;
  for(i=0;i<160;++i) { const uint8_t *p=b->p+trks+i*8; uint32_t a=(uint32_t)xx_data_get_u16(p, 2, 0, false)*512U,z=(uint32_t)xx_data_get_u16(p+2, 2, 0, false)*512U,bits=xx_data_get_u32(p+4, 4, 0, false);
   bool is_flux=fluxmapped[i]!=0;
   if(!a && !z && !bits) { if(mapped[i] || is_flux) return false; continue; }
   if(!a || !z || !bits || (mapped[i] && is_flux) ||
      z/512U>xx_data_get_u16(b->p+info+(is_flux ? 48U : 44U), 2, 0, false) ||
      (is_flux ? bits>z : bits>z*8U) ||
      a<trks+1280 || a>trks+trksz || z>trks+trksz-a ||
      !retro_disk_disjoint(spans,&count,160,a,z)) return false;
   ++nonempty;
   xx_rt_snprintf(name,sizeof(name),"track-%u.%s",i,is_flux ? "flux" : "bits");
   if(!wz_emit(w,b,name,a,is_flux ? bits : (bits+7U)/8U)) return false;
  }
 }
 if(meta) { uint32_t pos; bool tab=false; for(pos=0;pos<metasz;++pos) { uint8_t c=b->p[meta+pos]; if(!(pos&4095U) && !retro_disk_poll(b)) return false; if(c=='\t') { if(tab) return false; tab=true; } else if(c=='\n') { if(!tab) return false; tab=false; } else if(c<32 || c>126) return false; } if(tab || b->p[meta+metasz-1]!='\n' || !wz_emit(w,b,"metadata.txt",meta,metasz)) return false; }
 if(!nonempty) { return false; } s->size=at; return true;
}

static bool wz_decode(af_work *w,const af_blob *b){
 uint32_t at=12,info=0,map=0,tracks=0,i,cylinders=35;unsigned sectors=0;uint8_t *image=NULL,*latch=NULL;bool ok=false,full=true,recognized=false;
 xx_apple_woz *r=(xx_apple_woz *)w->f;r->incomplete=false;
 while(at<b->n){uint32_t z=xx_data_get_u32(b->p+at+4, 4, 0, false);if(!xx_rt_memcmp(b->p+at,"INFO",4))info=at+8;else if(!xx_rt_memcmp(b->p+at,"TMAP",4))map=at+8;else if(!xx_rt_memcmp(b->p+at,"TRKS",4))tracks=at+8;at+=8U+z;}
 if(b->p[info+1]!=1U){r->note="WOZ original 3.5-inch components; Macintosh GCR filesystem decoding unavailable";return true;}
 for(i=35U;i<40U;++i)if(b->p[map+i*4U]!=255U)cylinders=i+1U;
 image=af_alloc(w,cylinders*4096U,false);latch=af_alloc(w,131074U,false);if(!image || !latch)goto done;
 for(i=0;i<cylinders;++i){uint32_t index=b->p[map+i*4U],start,bits,n;unsigned count=0;int found;
  if(index==255U){full=false;continue;}
  if(b->p[3]=='1'){start=tracks+index*6656U;bits=xx_data_get_u16(b->p+start+6648U, 2, 0, false);}
  else{const uint8_t *e=b->p+tracks+index*8U;start=(uint32_t)xx_data_get_u16(e, 2, 0, false)*512U;bits=xx_data_get_u32(e+4, 4, 0, false);}
  if(!bits || bits>1048576U){full=false;continue;}
  n=ag_latch(w,b->p+start,bits,latch,131074U);if(!n || (found=ag_track(w,latch,n,i,image+i*4096U,&count))<0)goto done;
  if(sectors && count && sectors!=count) {goto done; } if(count)sectors=count;if(!count || found!=(int)count)full=false;
 }
 if(full && sectors){if(sectors==13U)for(i=1;i<cylinders;++i)xx_rt_memmove(image+i*3328U,image+i*4096U,3328U);
  if(!ag_files(w,image,cylinders,sectors,&recognized) || !af_copy(w,"decoded-sectors.do",image,cylinders*sectors*256U))goto done;}
 r->incomplete=!full;r->cylinders=cylinders;r->heads=1;r->sector_size=256;r->sectors_per_track=sectors;
 r->note=full?(recognized?"WOZ bitstream sectors checksum verified; native filesystem files extracted":"WOZ complete authenticated sectors; no supported filesystem root"):
  "WOZ original track/flux components; missing or nonstandard sectors prevent complete filesystem reconstruction";
 ok=af_poll(w);
done:af_release(w,latch,131074U);af_release(w,image,cylinders*4096U);return ok;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){
 af_work w;af_blob b;retro_disk_blob legacy;bool ok;
 if(!af_init(&w,f,s,pd) || !af_load(&w,&b))return false;
 legacy.p=b.p;legacy.n=b.n;legacy.crc_budget=RETRO_DISK_LIMIT;legacy.pd=pd;
 ok=parse_blob(f,s,&legacy,&w) && wz_decode(&w,&b);if(ok)((xx_apple_woz *)f)->number_of_records=s->count;
 af_release(&w,b.p,b.n);return ok;
}
AF_DEFINE_READER(apple_woz,XX_FILE_TYPE_APPLE_WOZ,"woz")

