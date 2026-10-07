/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Inert encoded components only: no playback, emulation or filesystem recovery.
 */
#include "xxfclib/formats/amiga_ipf/xx_amiga_ipf.h"
#include "../nintendo_sdat/xx_twelfth_c.h"
#include "xxfclib/data/xx_data.h"

typedef struct ipf_track {uint32_t key,imge,data,size;} ipf_track;
static bool ipf_stream(tc_blob *b,uint32_t a,uint32_t end,uint32_t cells,bool bits,uint32_t *used) {
 uint32_t start=a,left=cells;while(a<end) {uint32_t v=b->p[a++],bytes=v>>5,param=0,i,kind=v&31;uint64_t amount,n;
  if(!tc_work(b,1) || bytes>4 || bytes>end-a) { return false; } for(i=0;i<bytes;++i) param=(param<<8)|b->p[a++];
  if(!kind) {if(v || left) return false;*used=a-start;return true;}
  if(!bytes || !param || kind>3) { return false; } amount=bits ? param:(uint64_t)param*8;n=(amount+7)/8;
  if(kind!=1) { amount*=2; } if(amount>left || n>end-a) return false;a+=(uint32_t)n;left-=(uint32_t)amount;
 }return false;
}
static bool ipf_data(tc_blob *b,const ipf_track *t,uint32_t encoder) {
 const uint8_t *p=b->p;uint32_t n=xx_data_get_u32(p+t->imge+52, 4, 0, true),i,table=n*32,end=t->data+t->size,count=0,maxend=t->data+table;uint64_t cells=0,data=0,gaps=0;tc_extent ext[512];
 if(!n || n>256 || table>t->size || !tc_claim(b,ext,&count,t->data,table,false)) return false;
 for(i=0;i<n;++i) {uint32_t q=t->data+i*32,dc=xx_data_get_u32(p+q, 4, 0, true),gc=xx_data_get_u32(p+q+4, 4, 0, true),flags=xx_data_get_u32(p+q+20, 4, 0, true),off=xx_data_get_u32(p+q+28, 4, 0, true),used;
  if(!dc || dc>2000000 || gc>2000000 || (gc && gc<8) || xx_data_get_u32(p+q+16, 4, 0, true)!=1 || (flags&~4U) || (encoder==1 && flags) || xx_data_get_u32(p+q+24, 4, 0, true)>255 || off<table || off>=t->size) return false;
  if(!ipf_stream(b,t->data+off,end,dc,encoder==2 && (flags&4),&used) || !tc_claim(b,ext,&count,t->data+off,used,false)) return false;
  if(t->data+off+used>maxend) { maxend=t->data+off+used; } data+=dc;gaps+=gc;cells+=(uint64_t)dc+gc;
 }if(cells>2000000 || cells!=xx_data_get_u32(p+t->imge+48, 4, 0, true) || data!=xx_data_get_u32(p+t->imge+40, 4, 0, true) || gaps!=xx_data_get_u32(p+t->imge+44, 4, 0, true) || end-maxend>3 || !tc_zero(p+maxend,end-maxend)) return false;return true;
}
static bool read_components(Abstractformat *f,pm_stream *s,tc_blob *b) {
 const uint8_t *p=b->p;uint32_t a=0,n=0,info=0,encoder=0,minc=0,maxc=0,minh=0,maxh=0,i,z,crc;ipf_track tracks[170];char label[64];
 if(b->n<12 || xx_rt_memcmp(p,"CAPS",4)) { return false; } xx_mem_zero(tracks,sizeof(tracks));
 while(a<b->n) {uint32_t start=a;if(!tc_span(b,a,12) || !tc_work(b,1)) return false;z=xx_data_get_u32(p+a+4, 4, 0, true);
  if(z<12 || !tc_span(b,a,z) || !tc_crc(b,a,z,true,&crc) || crc!=xx_data_get_u32(p+a+8, 4, 0, true)) { return false; } a+=z;
  if(!xx_rt_memcmp(p+start,"CAPS",4)) {if(start || z!=12) return false;}
  else if(!xx_rt_memcmp(p+start,"INFO",4)) {if(start!=12 || info++ || z!=96 || xx_data_get_u32(p+start+12, 4, 0, true)!=1 || xx_data_get_u32(p+start+20, 4, 0, true)!=1) return false;
   encoder=xx_data_get_u32(p+start+16, 4, 0, true);minc=xx_data_get_u32(p+start+36, 4, 0, true);maxc=xx_data_get_u32(p+start+40, 4, 0, true);minh=xx_data_get_u32(p+start+44, 4, 0, true);maxh=xx_data_get_u32(p+start+48, 4, 0, true);
   if((encoder!=1 && encoder!=2) || minc>maxc || maxc>83 || minh>maxh || maxh>1 || !tc_emit(f,s,b,"ipf-info.bin",start,z)) return false;
  }else if(!xx_rt_memcmp(p+start,"IMGE",4)) {uint32_t key,c,h;if(!info || z!=80 || n==170) return false;key=xx_data_get_u32(p+start+64, 4, 0, true);c=xx_data_get_u32(p+start+12, 4, 0, true);h=xx_data_get_u32(p+start+16, 4, 0, true);
   if(c<minc || c>maxc || h<minh || h>maxh || xx_data_get_u32(p+start+20, 4, 0, true)!=2 || xx_data_get_u32(p+start+24, 4, 0, true)!=1 || xx_data_get_u32(p+start+56, 4, 0, true) || xx_data_get_u32(p+start+60, 4, 0, true) || !tc_zero(p+start+68,12) || xx_data_get_u32(p+start+36, 4, 0, true)>=xx_data_get_u32(p+start+48, 4, 0, true)) return false;
   for(i=0;i<n;++i) if(tracks[i].key==key || (xx_data_get_u32(p+tracks[i].imge+12, 4, 0, true)==c && xx_data_get_u32(p+tracks[i].imge+16, 4, 0, true)==h)) return false;
   tracks[n].key=key;tracks[n].imge=start;++n;
  }else if(!xx_rt_memcmp(p+start,"DATA",4)) {uint32_t key,size,bits;ipf_track *t=NULL;if(!info || z!=28) return false;key=xx_data_get_u32(p+start+24, 4, 0, true);size=xx_data_get_u32(p+start+12, 4, 0, true);bits=xx_data_get_u32(p+start+16, 4, 0, true);
   for(i=0;i<n;++i) { if(tracks[i].key==key) t=&tracks[i]; } if(!t || t->data || !size || !tc_span(b,a,size) || bits!=(uint64_t)size*8 || !tc_crc(b,a,size,false,&crc) || crc!=xx_data_get_u32(p+start+20, 4, 0, true)) return false;
   t->data=a;t->size=size;a+=size;
  }else return false;
 }if(!info || !n) return false;
 for(i=0;i<n;++i) {if(!tracks[i].data || !ipf_data(b,&tracks[i],encoder)) return false;xx_rt_snprintf(label,sizeof(label),"track-%03u-descriptor.bin",i);if(!tc_emit(f,s,b,label,tracks[i].imge,80)) return false;
  xx_rt_snprintf(label,sizeof(label),"track-%03u-encoded-data.bin",i);if(!tc_emit(f,s,b,label,tracks[i].data,tracks[i].size)) return false;
 }s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { tc_blob b;bool ok;if(!tc_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_amiga_ipf_init(xx_amiga_ipf *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_AMIGA_IPF,"amiga_ipf");} }
xx_amiga_ipf *xx_amiga_ipf_create(xx_io_device *d,int64_t b) { xx_amiga_ipf *r=(xx_amiga_ipf *)xx_mem_alloc(sizeof(*r));if(r) xx_amiga_ipf_init(r,d,b);return r; }
void xx_amiga_ipf_destroy(xx_amiga_ipf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_amiga_ipf_free(xx_amiga_ipf *r) { if(r) {xx_amiga_ipf_destroy(r);xx_mem_free(r);} }
bool xx_amiga_ipf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_amiga_ipf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
