/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://www.cbmstuff.com/downloads/scp/scp_image_specs.txt
 * SCP floppy mode, including FPCS footer, EXTS/WRSP write splices, and
 * legacy single-sided track numbering. Original flux records are exported.
 */
#include "xxfclib/formats/supercard_scp/xx_supercard_scp.h"
#include "../vice_x64/xx_ninth_retro.h"

static bool scp_footer(Abstractformat *f, pm_stream *s, const nh_blob *b,
                       nh_span *spans, uint32_t *span_count,
                       uint32_t *data_end) {
 uint32_t at, first, i; static const char *const labels[6]={
  "drive-manufacturer.txt","drive-model.txt","drive-serial.txt",
  "creator.txt","application.txt","comments.txt"};
 if (!(b->p[8]&0x20U)) { *data_end=b->n; return true; }
 if (b->n<688U+48U) return false;
 at=b->n-48U; first=at;
 if (xx_rt_memcmp(b->p+at+44U,"FPCS",4) ||
     !nh_disjoint(spans,span_count,1024U,at,48U)) return false;
 for (i=0;i<6U;++i) {
  uint32_t off=pm_le32(b->p+at+i*4U), len, j;
  if (!off) { if (i==4U) return false; continue; }
  if (off<688U || off>=at || !nh_range(b,off,2U)) return false;
  len=pm_le16(b->p+off);
  if (!len || len>4096U || off+3U+len>at || b->p[off+2U+len] ||
      !nh_disjoint(spans,span_count,1024U,off,3U+len)) return false;
  for(j=0U;j<len;++j) if(!b->p[off+2U+j]) return false;
  if(!nh_emit(f,s,b,labels[i],off+2U,len)) return false;
  if(off<first) first=off;
 }
 if (!nh_emit(f,s,b,"footer.bin",at,48U)) return false;
 *data_end=first; return true;
}

static bool scp_extension(Abstractformat *f, pm_stream *s,
                          const nh_blob *b, nh_span *spans,
                          uint32_t *span_count, uint32_t first) {
 uint32_t p, len, i;
 if (first==688U || (!(b->p[8]&0x20U) &&
     (first<692U || xx_rt_memcmp(b->p+688U,"EXTS",4U))))
  return true;
 if (first<708U || !nh_range(b,688U,first-688U) ||
     xx_rt_memcmp(b->p+688U,"EXTS",4U) ||
     (len=pm_le32(b->p+692U))!=first-696U ||
     !nh_disjoint(spans,span_count,1024U,688U,first-688U)) return false;
 p=696U;
 if (first-p!=684U || xx_rt_memcmp(b->p+p,"WRSP",4U) ||
     pm_le32(b->p+p+4U)!=676U || pm_le32(b->p+p+8U)) return false;
 for(i=0U;i<168U;++i)
  if (!pm_le32(b->p+16U+i*4U) && pm_le32(b->p+p+12U+i*4U))
   return false;
 return nh_emit(f,s,b,"write-splices.bin",p+12U,168U*4U) &&
        nh_emit(f,s,b,"extension.bin",688U,first-688U);
}

static bool parse_blob(Abstractformat *f,pm_stream *s,nh_blob *b) {
 nh_span spans[1024]; uint32_t count=0,i,j,revs,start,end,extent=688,sum=0,tracks=0,first=UINT32_MAX,data_end; bool legacy=false; char name[48];
 if(!nh_range(b,0,688) || xx_rt_memcmp(b->p,"SCP",3) || (b->p[3]&15U)>9 || (b->p[3]>>4)>3 || !(revs=b->p[5]) || revs>5 || (start=b->p[6])>(end=b->p[7]) || end>167 || (b->p[8]&0x40U) || b->p[9] || b->p[10]>2 || ((b->p[8]&16U) && revs!=1)) return false;
 for(i=16;i<b->n;++i) { if(!(i&4095U) && !nh_poll(b)) return false; sum+=b->p[i]; }
 if((!(b->p[8]&16U) && sum!=pm_le32(b->p+12)) || ((b->p[8]&16U) && pm_le32(b->p+12))) return false;
 if(!nh_emit(f,s,b,"flux-descriptor.bin",0,688)) return false;
 for(i=0U;i<168U;++i) {
  uint32_t a=pm_le32(b->p+16U+i*4U);
  if(!a) continue;
  if(i<start || i>end || a<688U) return false;
  if(a<first) first=a;
  if(b->p[10]==1U && (i&1U) && b->p[4]!=0U) legacy=true;
  if(b->p[10]==2U && !(i&1U)) legacy=true;
 }
 if(first==UINT32_MAX || (legacy && end>83U) ||
    !scp_footer(f,s,b,spans,&count,&data_end) ||
    !scp_extension(f,s,b,spans,&count,first)) return false;
 for(i=0;i<168;++i) {
  uint32_t a=pm_le32(b->p+16+i*4),z=4+12*revs,logical=legacy ? (2U*i+b->p[10]-1U) : i; if(!a) continue;
  if(a<first || a>data_end || z>data_end-a || !nh_range(b,a,z) || xx_rt_memcmp(b->p+a,"TRK",3) || b->p[a+3]!=i || !nh_disjoint(spans,&count,1024,a,z)) return false;
  ++tracks; xx_rt_snprintf(name,sizeof(name),"track-%u-descriptor.bin",logical); if(!nh_emit(f,s,b,name,a,z)) return false; if(extent<a+z) extent=a+z;
  for(j=0;j<revs;++j) { const uint8_t *p=b->p+a+4+j*12; uint32_t len=pm_le32(p+4),rel=pm_le32(p+8),at;
   if(!pm_le32(p) || !len || len>NH_LIMIT/2 || rel<z || rel>data_end-a || len*2U>data_end-a-rel || !nh_range(b,at=a+rel,len*2U) || !nh_disjoint(spans,&count,1024,at,len*2U)) return false;
   xx_rt_snprintf(name,sizeof(name),"track-%u-revolution-%u.flux",logical,j); if(!nh_emit(f,s,b,name,at,len*2U)) return false; if(extent<at+len*2U) extent=at+len*2U;
  }
 }
 if(!tracks || extent>data_end) return false; s->size=(b->p[8]&0x20U)?b->n:extent; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { nh_blob b; bool ok; if(!nh_load(f,&b,pd)) return false; ok=parse_blob(f,s,&b); xx_mem_free(b.p); return ok; }

void xx_supercard_scp_init(xx_supercard_scp *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SUPERCARD_SCP,"scp"); } }
xx_supercard_scp *xx_supercard_scp_create(xx_io_device *d,int64_t b) { xx_supercard_scp *r=(xx_supercard_scp *)xx_mem_alloc(sizeof(*r)); if(r) xx_supercard_scp_init(r,d,b); return r; }
void xx_supercard_scp_destroy(xx_supercard_scp *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_supercard_scp_free(xx_supercard_scp *r) { if(r) { xx_supercard_scp_destroy(r); xx_mem_free(r); } }
bool xx_supercard_scp_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_supercard_scp_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
