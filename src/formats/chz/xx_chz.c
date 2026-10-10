/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Raw ChArc chain; framing follows XArchive/archives/xchz.cpp (MIT).
 * Directory components are preserved as escaped bytes in unique flat leaves.
 * Split volumes publish complete prefix members only, never a partial member.
 */
#include "xxfclib/formats/chz/xx_chz.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/algo/charc/xx_charc.h"
#define CHZ_MAX_SIZE (UINT64_C(512)*1024*1024)
#define CHZ_MAX_NAME 4096U
#define CHZ_MAX_RECORDS 65536U
static bool chz_write(xx_io_device *d,const uint8_t *p,size_t n,xx_pd_struct *pd){size_t at=0;while(d && at<n){size_t take=n-at;ssize_t amount;if(take>65536)take=65536;if(xx_pd_is_stopped(pd))return false;amount=xx_io_write(d,p+at,take);if(amount<=0 || (size_t)amount>take)return false;at+=(size_t)amount;}return !xx_pd_is_stopped(pd);}
static bool chz_unpack(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd){uint8_t *packed=NULL,*plain=NULL;size_t got=0;bool ok=false;const xx_var *limit;if(xx_pd_is_stopped(pd) || m->size<0 || m->packed_size<0 || (uint64_t)m->size>CHZ_MAX_SIZE || (uint64_t)m->packed_size>CHZ_MAX_SIZE)return false;limit=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);if(limit && (uint64_t)m->size+(uint64_t)m->packed_size>xx_var_get_u64(limit))return false;packed=(uint8_t *)xx_mem_alloc(m->packed_size?(size_t)m->packed_size:1);plain=(uint8_t *)xx_mem_alloc(m->size?(size_t)m->size:1);if(!packed || !plain || !pm_read(f,m->offset-f->base_address,packed,(size_t)m->packed_size))goto done;if(!xx_charc_decode_memory(packed,(size_t)m->packed_size,plain,(size_t)m->size,&got) || got!=(size_t)m->size || xx_pd_is_stopped(pd))goto done;ok=chz_write(out,plain,got,pd);done:xx_mem_free(packed);xx_mem_free(plain);return ok;}
/* Control bytes cannot occur in DOS names. Percent encoding avoids collisions
 * between raw bytes and escape spelling; separators stay inside the flat leaf. */
static bool chz_name(const uint8_t *raw,size_t n,char *out,size_t cap){static const char hex[]="0123456789ABCDEF";size_t i,at=0;if(!n)return false;for(i=0;i<n;++i){uint8_t c=raw[i];if(c<32 || c==127)return false;if(c>=33 && c<127 && c!='%' && c!='/' && c!='\\' && c!=':' && c!='*' && c!='?' && c!='"' && c!='<' && c!='>' && c!='|'){if(at+1>=cap)return false;out[at++]=(char)c;}else{if(at+3>=cap)return false;out[at++]='%';out[at++]=hex[c>>4];out[at++]=hex[c&15];}}out[at]=0;return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){int64_t n=pm_available(f),at=0;uint8_t h[24],raw[CHZ_MAX_NAME];char dirs[64][768],name[CHZ_MAX_NAME*3+1],full[CHZ_MAX_NAME*3+64*768+80];size_t depth=0,records=0;if(n<25)return false;
 while(at<n){uint32_t tag;if(++records>CHZ_MAX_RECORDS || xx_pd_is_stopped(pd) || n-at<4 || !pm_read(f,at,h,4))return false;tag=xx_data_get_u32(h,4,0,false);
  if(tag==UINT32_C(0x46684353)){uint32_t record,unpacked;uint16_t len;uint8_t method;int64_t packed;size_t i,pos;pm_member *m;
   if(n-at<24 || !pm_read(f,at,h,24))return false;record=xx_data_get_u32(h+4,4,0,false);unpacked=xx_data_get_u32(h+8,4,0,false);len=xx_data_get_u16(h+22,2,0,false);method=h[20];
   if(!len || len>CHZ_MAX_NAME || record<24U+len || record>INT32_MAX || unpacked>CHZ_MAX_SIZE || len>(uint64_t)(n-at-24) || method>1)return false;
   if(!pm_read(f,at+24,raw,len) || !chz_name(raw,len,name,sizeof(name)))return false;packed=record-24U-len;
   if((uint64_t)packed>CHZ_MAX_SIZE || (!packed && unpacked) || (method==0 && packed!=unpacked))return false;
   if(record>(uint64_t)(n-at)){if(!s->count)return false;break;}
   pos=0;for(i=0;i<depth;++i){size_t z=xx_rt_strlen(dirs[i]);if(pos+z+3>=sizeof(full))return false;xx_rt_memcpy(full+pos,dirs[i],z);pos+=z;xx_rt_memcpy(full+pos,"%2F",3);pos+=3;}
   if(pos+xx_rt_strlen(name)+1>=sizeof(full))return false;xx_rt_memcpy(full+pos,name,xx_rt_strlen(name)+1);
   if(!pm_add(f,s,full,at+24+len,packed))return false;m=&s->items[s->count-1];
   {char number[24];char *display;xx_rt_snprintf(number,sizeof(number),"%04u-",(unsigned)(s->count-1));display=xx_str_concat(number,full);if(!display)return false;m->display_name=display;}
   m->size=unpacked;m->compression_method=method;if(method && unpacked)m->read_all=chz_unpack;
   at+=record;
  }else if(tag==UINT32_C(0x44684353)){uint8_t len;if(n-at<10 || !pm_read(f,at,h,10) || h[8] || !(len=h[9]) || depth>=64 || len>(uint64_t)(n-at-10) || !pm_read(f,at+10,raw,len) || !chz_name(raw,len,dirs[depth],sizeof(dirs[depth])))return false;++depth;at+=10+len;
  }else if(tag==UINT32_C(0x64684353)){if(!depth)return false;--depth;at+=4;}else return false;
 }
 s->size=at;return s->count!=0 && !xx_pd_is_stopped(pd);
}
void xx_chz_init(xx_chz *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_CHZ,"chz");r->format.check_is_valid=xx_chz_check_is_valid;r->format.handle_base_info=xx_chz_handle_base_info;}}
xx_chz *xx_chz_create(xx_io_device *d,int64_t b){xx_chz *r=(xx_chz *)xx_mem_alloc(sizeof(*r));if(r)xx_chz_init(r,d,b);return r;}
void xx_chz_destroy(xx_chz *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_chz_free(xx_chz *r){if(r){xx_chz_destroy(r);xx_mem_free(r);}}
bool xx_chz_check_is_valid(Abstractformat *f,xx_pd_struct *pd){int64_t saved;bool valid;if(!f || !f->device || (saved=xx_io_tell(f->device))<0)return false;valid=pm_valid(f,pd);return xx_io_seek64(f->device,saved,SEEK_SET)==0 && valid;}
bool xx_chz_handle_base_info(Abstractformat *f,xx_pd_struct *pd){int64_t saved;bool valid;if(!f || !f->device || (saved=xx_io_tell(f->device))<0)return false;valid=pm_handle(f,pd);if(xx_io_seek64(f->device,saved,SEEK_SET)!=0){f->is_valid=false;return false;}return valid;}
xx_file_type_t xx_chz_detect(xx_io_device *d,int64_t b){uint8_t h[4];xx_chz r;bool valid;if(!xx_io_read_at(d,b,h,4) || (xx_rt_memcmp(h,"SChF",4) && xx_rt_memcmp(h,"SChD",4)))return XX_FILE_TYPE_UNKNOWN;xx_chz_init(&r,d,b);valid=xx_chz_check_is_valid(&r.format,NULL);xx_chz_destroy(&r);return valid?XX_FILE_TYPE_CHZ:XX_FILE_TYPE_UNKNOWN;}
