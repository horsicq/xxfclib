/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT
 * XArchive/archives/xlarcpfx.cpp: GEMDOS LArc PFX carrier. Its level-0 LZ5
 * header intentionally overstates the packed length; the real boundary is
 * the GEMDOS DATA segment. The embedded CRC is not a payload checksum.
 * No executable bytes are run. */
#include "xxfclib/formats/larc_pfx/xx_larc_pfx.h"
#include "xxfclib/data/xx_data.h"
#include "../lha/xx_lha_legacy_native.h"
#include "../xx_payload_members.h"
#define PFX_MAX_BUFFER (UINT64_C(256)*1024U*1024U)
typedef struct pfx_member { int64_t header_offset,header_size; } pfx_member;
static uint32_t pfx_be32(const uint8_t *p) {return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static bool pfx_name(const uint8_t *p,size_t n,char *out) {
    size_t i,start=0,end=n;
    for(i=0;i<n;++i) {if(!p[i]){end=i;break;}if(p[i]=='/'||p[i]=='\\')start=i+1;}
    if(end<=start || end-start>64)return false;
    for(i=start;i<end;++i) {unsigned c=p[i];if(c<0x21 || c>0x7e || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|')return false;out[i-start]=(char)c;}
    out[end-start]=0;return xx_rt_strcmp(out,".") && xx_rt_strcmp(out,"..");
}
static bool pfx_read_all(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    uint8_t *packed=NULL,*plain=NULL;size_t at=0;bool valid=false;
    const xx_var *limit=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    if(m->packed_size<=0 || m->size<=0 || (uint64_t)m->packed_size>PFX_MAX_BUFFER || (uint64_t)m->size>PFX_MAX_BUFFER ||
       (limit && (uint64_t)m->packed_size+(uint64_t)m->size>xx_var_get_u64(limit)) || (pd && xx_pd_is_stopped(pd)))return false;
    packed=(uint8_t *)xx_mem_alloc((size_t)m->packed_size);plain=(uint8_t *)xx_mem_alloc((size_t)m->size);if(!packed || !plain)goto done;
    while(at<(size_t)m->packed_size) {size_t n=(size_t)m->packed_size-at;if(n>65536)n=65536;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,m->offset-f->base_address+(int64_t)at,packed+at,n))goto done;at+=n;}
    if(!xx_lha_legacy_decode_native(XX_LHA_LEGACY_LZ5,packed,(size_t)m->packed_size,plain,(size_t)m->size,pd))goto done;
    for(at=0;out && at<(size_t)m->size;) {size_t n=(size_t)m->size-at;ssize_t wrote;if(n>65536)n=65536;
        if(pd && xx_pd_is_stopped(pd))goto done;wrote=xx_io_write(out,plain+at,n);if(wrote<=0 || (size_t)wrote>n)goto done;at+=(size_t)wrote;}
    valid=!(pd && xx_pd_is_stopped(pd));
done:xx_mem_free(packed);xx_mem_free(plain);return valid;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t program[28],marker[4],header[257];uint64_t text,data,bss,symbols,at,end,packed,plain,stream;unsigned hs,i,sum=0,name_size;int64_t size=pm_available(f);char name[65];pm_member *m;pfx_member *context;
    if(size<60 || size>INT32_MAX || (pd && xx_pd_is_stopped(pd)) || !pm_read(f,0,program,sizeof(program)) || program[0]!=0x60 || program[1]!=0x1a)return false;
    text=pfx_be32(program+2);data=pfx_be32(program+6);bss=pfx_be32(program+10);symbols=pfx_be32(program+14);
    if(text<32 || text>INT32_MAX || !data || data>INT32_MAX || bss>INT32_MAX || symbols>INT32_MAX)return false;
    at=28+text;end=at+data;
    if(at>(uint64_t)size || end>(uint64_t)size || data<24 || !pm_read(f,(int64_t)at-4,marker,4) || xx_rt_memcmp(marker,"\xde\xad\xfa\xce",4) || !pm_read(f,(int64_t)at,header,24))return false;
    hs=(unsigned)header[0]+2U;
    if(hs<24 || hs>data || !pm_read(f,(int64_t)at,header,hs) || xx_rt_memcmp(header+2,"-lz5-",5) || header[20]!=0)return false;
    for(i=2;i<hs;++i)sum+=header[i];if((uint8_t)sum!=header[1])return false;
    name_size=header[21];if(24U+name_size>hs)return false;
    packed=xx_data_get_u32(header+7,4,0,false);plain=xx_data_get_u32(header+11,4,0,false);stream=end-at-hs;
    if(!stream || packed<=stream || !plain || plain>INT32_MAX || plain>stream*9U+144U)return false;
    if(!pfx_name(header+22,name_size,name)) {
        const char *source=xx_io_source_path(f->device);
        if(!source || !pfx_name((const uint8_t *)source,xx_rt_strlen(source),name))
            xx_mem_copy(name,"data",5);
    }
    if(!pm_add(f,s,"payload",(int64_t)(at+hs),(int64_t)stream))return false;
    m=&s->items[0];m->size=(int64_t)plain;m->read_all=pfx_read_all;m->display_name=xx_str_dup(name);context=(pfx_member *)xx_mem_alloc(sizeof(*context));
    if(!context || !m->display_name){xx_mem_free(context);return false;}
    context->header_offset=f->base_address+(int64_t)at;context->header_size=hs;m->context=context;m->free_context=xx_mem_free;s->size=size;xx_format_set_version(f,"LArc -lz5-");return !(pd && xx_pd_is_stopped(pd));
}
static xx_archive_record_state *pfx_records(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd) {
    xx_archive_record_state *st=pm_create_records(f,opts,pd);if(st && st->has_record) {pm_stream *s=(pm_stream *)st->internal_state;pfx_member *m=(pfx_member *)s->items[0].context;
        st->current_record.header_offset=m->header_offset;st->current_record.header_size=m->header_size;
        if(!xx_archive_record_set_meta_u64(&st->current_record,XX_META_ID_COMPRESSION_METHOD,XX_LHA_LEGACY_LZ5)){pm_free_records(f,st);return NULL;}}
    return st;
}
void xx_larc_pfx_init(xx_larc_pfx *r,xx_io_device *d,int64_t base){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,base,XX_FILE_TYPE_LARC_PFX,"prg");r->format.endian=XX_ENDIAN_BIG;xx_format_set_mime_type(&r->format,"application/x-atari-st-executable");r->format.create_archive_records_reading=pfx_records;}}
xx_larc_pfx *xx_larc_pfx_create(xx_io_device *d,int64_t base){xx_larc_pfx *r=(xx_larc_pfx *)xx_mem_alloc(sizeof(*r));if(r)xx_larc_pfx_init(r,d,base);return r;}
void xx_larc_pfx_destroy(xx_larc_pfx *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_larc_pfx_free(xx_larc_pfx *r){if(r){xx_larc_pfx_destroy(r);xx_mem_free(r);}}
bool xx_larc_pfx_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_larc_pfx_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
xx_file_type_t xx_larc_pfx_detect(xx_io_device *d,int64_t base){xx_larc_pfx r;bool valid;xx_larc_pfx_init(&r,d,base);valid=pm_valid(&r.format,NULL);xx_larc_pfx_destroy(&r);return valid?XX_FILE_TYPE_LARC_PFX:XX_FILE_TYPE_UNKNOWN;}
