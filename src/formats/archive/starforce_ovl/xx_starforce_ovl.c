/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * StarForce OVL v2 indexed, localized zlib resources. */
#include "xxfclib/formats/archive/starforce_ovl/xx_starforce_ovl.h"
#include "../../xx_payload_members.h"
#include "../../xx_archive_output_name.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/codepages/xx_codepage.h"
#define FO_CAP UINT64_C(268435456)
#define FO_MEMBER UINT64_C(67108864)
#define FO_DIRECTORY UINT64_C(16777216)
#define FO_WORK UINT64_C(1048576)
typedef struct fo_member { uint64_t resident;int64_t header;uint32_t header_size,profile,language; } fo_member;
static uint32_t fo_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t fo_u16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint64_t fo_budget(Abstractformat *f)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):FO_CAP;return n<FO_CAP?n:FO_CAP;
}
static uint8_t *fo_decode(Abstractformat *f,uint64_t at,uint32_t packed,uint32_t plain,uint64_t resident,xx_pd_struct *pd)
{
    uint8_t *input=NULL,*output=NULL;xx_io_device *destination=NULL;size_t consumed=0;bool ok=false;
    if(packed<8U || packed>FO_MEMBER || plain>FO_MEMBER || resident+packed+(uint64_t)plain+FO_WORK>fo_budget(f) || (pd && xx_pd_is_stopped(pd)))return NULL;
    input=(uint8_t *)xx_mem_alloc(packed);output=(uint8_t *)xx_mem_alloc(plain?plain:1U);
    if(!input || !output || !pm_read(f,(int64_t)at,input,packed) || !xx_zlib_stream_header_is_valid(input,packed) || (input[1]&32U))goto done;
    destination=xx_io_mem_open(output,plain);if(!destination)goto done;
    ok=xx_deflate_unpack_memory_to_device_ex(input+2,packed-6U,destination,&consumed,false,pd) && consumed==packed-6U &&
        xx_io_tell(destination)==plain && xx_zlib_stream_trailer_matches(input,packed,output,plain) && !(pd && xx_pd_is_stopped(pd));
done:
    if(destination)xx_io_close(destination);
    xx_mem_free(input);if(!ok) { xx_mem_free(output);output=NULL; }return output;
}
static bool fo_read_all(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd)
{
    fo_member *c=(fo_member *)m->context;uint8_t *data;size_t at=0;bool ok=true;
    if(!c)return false;
    data=fo_decode(f,(uint64_t)(m->offset-f->base_address),(uint32_t)m->packed_size,(uint32_t)m->size,c->resident,pd);if(!data)return false;
    while(out && at<(uint64_t)m->size) {
        size_t n=(uint64_t)m->size-at>16384U?16384U:(size_t)m->size-at;ssize_t wrote;
        if(pd && xx_pd_is_stopped(pd)) { ok=false;break; }
        wrote=xx_io_write(out,data+at,n);if(wrote<=0 || (size_t)wrote>n) { ok=false;break; }at+=(size_t)wrote;
    }
    xx_mem_free(data);return ok && !(pd && xx_pd_is_stopped(pd));
}
/* Structural certificate framing only; this is not a signature trust check. */
static bool fo_tail(Abstractformat *f,uint64_t end,uint64_t total,uint32_t cert,uint32_t cert_size,xx_pd_struct *pd)
{
    uint8_t h[8];uint64_t at,limit;
    if(!cert && !cert_size)return end==total;
    if(!cert || !cert_size || (uint64_t)cert!=((end+7U)&~UINT64_C(7)) || (uint64_t)cert+cert_size!=total)return false;
    if(!pm_read(f,(int64_t)end,h,(size_t)(cert-end)))return false;
    for(at=0;at<cert-end;++at)if(h[at])return false;
    at=cert;limit=(uint64_t)cert+cert_size;
    while(at<limit) {
        uint32_t length;uint64_t next,i;
        if((pd && xx_pd_is_stopped(pd)) || limit-at<8U || !pm_read(f,(int64_t)at,h,8U))return false;
        length=fo_u32(h);next=at+(((uint64_t)length+7U)&~UINT64_C(7));
        if(length<8U || length>limit-at || next>limit || (fo_u16(h+4)!=0x200U && fo_u16(h+4)!=0x100U) || fo_u16(h+6)!=2U)return false;
        if(!pm_read(f,(int64_t)(at+length),h,(size_t)(next-at-length)))return false;
        for(i=0;i<next-at-length;++i)if(h[i])return false;
        at=next;
    }
    return at==limit;
}
static char *fo_name(const uint8_t *directory,uint32_t length,uint64_t *cursor)
{
    const xx_codepage *codec=xx_codepage_find("UTF-16LE");uint16_t chars;size_t used=0,written=0,i;char *utf8,*safe;
    if(!codec || *cursor>length || length-*cursor<2U)return NULL;
    chars=fo_u16(directory+*cursor);*cursor+=2;
    if(!chars || chars>1024U || (uint64_t)chars*2U>length-*cursor)return NULL;
    for(i=0;i<chars;++i)if(!fo_u16(directory+*cursor+i*2U))return NULL;
    utf8=(char *)xx_mem_alloc((size_t)chars*3U+1U);if(!utf8)return NULL;
    if(xx_codepage_to_utf8(codec,directory+*cursor,(size_t)chars*2U,(uint8_t *)utf8,(size_t)chars*3U,&used,&written)!=XX_CODEPAGE_OK || used!=(size_t)chars*2U) {
        xx_mem_free(utf8);return NULL;
    }
    utf8[written]=0;*cursor+=(uint64_t)chars*2U;safe=xx_archive_output_name(utf8,0,0);xx_mem_free(utf8);return safe;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    uint8_t h[24],*directory=NULL;int64_t total=pm_available(f);uint64_t archive=0,index,names_at,next=24U,names=0,maximum=FO_MEMBER,budget=fo_budget(f);
    uint32_t size=0,diroff=0,plain=0,packed=0,roots,cert=0,cert_size=0,i;bool ok=false;
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(v && xx_var_get_u64(v)<maximum)maximum=xx_var_get_u64(v);
    if(total<32 || budget<FO_WORK || (pd && xx_pd_is_stopped(pd)) || !pm_read(f,0,h,24))return false;
    if(h[0]=='M' && h[1]=='Z') {
        xx_pe *pe=xx_pe_create(f->device,f->base_address);int64_t extent;uint32_t section;if(!pe)return false;
        extent=xx_pe_get_format_size(&pe->format,pd);
        if(extent>0) {
            /* PE format size includes certificates; resources start after the
             * section image and precede that separately bounded certificate. */
            extent=pe->size_of_headers;
            for(section=0;section<pe->number_of_sections;++section) {
                uint64_t end=(uint64_t)pe->sections[section].raw_offset+pe->sections[section].raw_size;
                if(pe->sections[section].raw_size && end>(uint64_t)extent)extent=(int64_t)end;
            }
        }
        cert=pe->data_directory_rva[4];cert_size=pe->data_directory_size[4];xx_pe_free(pe);
        if(extent<=0 || extent>total-32)return false;
        archive=(uint64_t)extent;if(!pm_read(f,extent,h,24))return false;
    }
    if(!xx_rt_memcmp(h,"OVL ",4) && fo_u32(h+4)==2U) {
        size=fo_u32(h+8);diroff=fo_u32(h+12);plain=fo_u32(h+16);packed=fo_u32(h+20);
        if(diroff<24U || (uint64_t)diroff+packed!=size || size>(uint64_t)total-archive ||
            !fo_tail(f,archive+size,(uint64_t)total,cert,cert_size,pd))return false;
    } else {
        uint64_t limit=cert?cert:(uint64_t)total;unsigned pad,found=0;
        for(pad=0;pad<=(cert?7U:0U);++pad) {
            uint64_t end;uint32_t candidate,cp,cu;
            if(limit<pad+20U || limit-pad-20U<archive)continue;
            end=limit-pad;
            if(!pm_read(f,(int64_t)(end-20U),h,20U) || xx_rt_memcmp(h+16,"SOVL",4U) || fo_u32(h+12)!=1U)continue;
            candidate=fo_u32(h);cp=fo_u32(h+4);cu=fo_u32(h+8);
            if(candidate!=end-archive || candidate<20U || cp>candidate-20U ||
                !fo_tail(f,end,(uint64_t)total,cert,cert_size,pd))continue;
            if(++found>1U)return false;
            size=candidate;packed=cp;plain=cu;diroff=candidate-20U-cp;
        }
        if(!found)return false;
        next=0;
    }
    if(packed<8U || plain<4U || plain>FO_DIRECTORY || packed>FO_DIRECTORY)return false;
    directory=fo_decode(f,archive+diroff,packed,plain,sizeof(pm_stream),pd);if(!directory)return false;
    roots=fo_u32(directory);index=4U+(uint64_t)roots*8U;
    if(!roots || roots>100000U || index>plain)goto done;
    for(i=0;i<roots;++i) {
        uint32_t meta=fo_u32(directory+4U+(uint64_t)i*8U+4U),profiles;
        if((pd && xx_pd_is_stopped(pd)) || meta!=index || plain-index<4U)goto done;
        profiles=fo_u32(directory+meta);
        if(!profiles || profiles>100000U || (uint64_t)profiles*8U>plain-index-4U)goto done;
        index+=4U+(uint64_t)profiles*8U;
    }
    names_at=index;
    for(i=0;i<roots;++i) {
        uint32_t meta=fo_u32(directory+4U+(uint64_t)i*8U+4U),profiles=fo_u32(directory+meta),profile;
        for(profile=0;profile<profiles;++profile) {
            uint32_t variants_at=fo_u32(directory+meta+8U+(uint64_t)profile*8U),variants;
            if((pd && xx_pd_is_stopped(pd)) || variants_at!=names_at || plain-names_at<4U)goto done;
            variants=fo_u32(directory+names_at);
            if(!variants || variants>100000U || (uint64_t)variants*16U>plain-names_at-4U)goto done;
            names_at+=4U+(uint64_t)variants*16U;
        }
    }
    for(i=0;i<roots;++i) {
        uint32_t meta=fo_u32(directory+4U+(uint64_t)i*8U+4U),profiles=fo_u32(directory+meta),profile;char *safe;
        if(fo_u32(directory+4U+(uint64_t)i*8U)!=names_at || !(safe=fo_name(directory,plain,&names_at)))goto done;
        for(profile=0;profile<profiles;++profile) {
        uint32_t tag=fo_u32(directory+meta+4U+(uint64_t)profile*8U),variants_at=fo_u32(directory+meta+8U+(uint64_t)profile*8U),variants=fo_u32(directory+variants_at),j;
        for(j=0;j<variants;++j) {
            const uint8_t *entry=directory+variants_at+4U+(uint64_t)j*16U;uint32_t lang=fo_u32(entry),offset=fo_u32(entry+4U),member_size=fo_u32(entry+8U),member_packed=fo_u32(entry+12U);
            size_t n=xx_rt_strlen(safe)+32U,capacity=s->count==s->capacity?(s->capacity?s->capacity*2U:8U):s->capacity;
            uint64_t resident=sizeof(pm_stream)+(uint64_t)capacity*sizeof(pm_member)+(s->count+1U)*sizeof(fo_member)+names+n;
            fo_member *ctx;char *name;pm_member *m;
            if((pd && xx_pd_is_stopped(pd)) || s->count>=100000U || offset!=next || member_size>maximum || member_packed<8U || member_packed>FO_MEMBER ||
                next>diroff || member_packed>diroff-next || resident+plain+FO_WORK>budget) { xx_mem_free(safe);goto done; }
            name=(char *)xx_mem_alloc(n);ctx=(fo_member *)xx_mem_alloc(sizeof(*ctx));
            if(!name || !ctx) { xx_mem_free(name);xx_mem_free(ctx);xx_mem_free(safe);goto done; }
            (void)xx_rt_snprintf(name,n,"%04u-%08x-%s",(unsigned)s->count,lang,safe);
            if(!pm_add(f,s,safe,(int64_t)(archive+offset),member_packed)) { xx_mem_free(name);xx_mem_free(ctx);xx_mem_free(safe);goto done; }
            m=&s->items[s->count-1];m->display_name=name;m->context=ctx;m->free_context=xx_mem_free;m->read_all=fo_read_all;m->size=member_size;m->compression_method=8;
            ctx->resident=0;ctx->header=f->base_address+(int64_t)(archive+diroff);ctx->header_size=packed;ctx->profile=tag;ctx->language=lang;next+=member_packed;names+=n;
        }
        }
        xx_mem_free(safe);
    }
    if(next!=diroff || names_at!=plain)goto done;
    for(i=0;i<s->count;++i)((fo_member *)s->items[i].context)->resident=sizeof(pm_stream)+(uint64_t)s->capacity*sizeof(pm_member)+(uint64_t)s->count*sizeof(fo_member)+names;
    s->size=(int64_t)(archive+size);ok=!(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(directory);return ok;
}
static const xx_archive_record *fo_current(Abstractformat *f,xx_archive_record_state *state)
{
    const xx_archive_record *r=pm_current(f,state);
    if(r) { char tags[96];pm_stream *s=(pm_stream *)state->internal_state;fo_member *c=(fo_member *)s->items[s->index].context;
        state->current_record.header_offset=c->header;state->current_record.header_size=c->header_size;
        (void)xx_rt_snprintf(tags,sizeof(tags),"profile_tag=0x%08x; language_tag=0x%08x",c->profile,c->language);
        if(!xx_archive_record_set_meta_str(&state->current_record,XX_META_ID_COMMENT,tags))return NULL; }
    return r;
}
Abstractformat *xx_starforce_ovl_create(xx_io_device *d,int64_t base)
{
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f) { pm_init(f,d,base,XX_FILE_TYPE_STARFORCE_OVL,"ovl");f->get_current_archive_record=fo_current; }return f;
}
void xx_starforce_ovl_free(Abstractformat *f) { if(f) { xx_format_destroy(f);xx_mem_free(f); } }
#include "../../xx_format_abstract_extractor_adapter.h"
static Abstractformat *fo_open(xx_io_device *d) { return xx_starforce_ovl_create(d,0); }
static const xx_file_type_t fo_types[]={XX_FILE_TYPE_STARFORCE_OVL};
const xx_format_search_desc xx_starforce_ovl_search_desc={fo_types,1U,NULL,0U,fo_open,xx_starforce_ovl_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(starforce_ovl,xx_starforce_ovl_search_desc)
