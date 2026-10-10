/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for WebExe M2E container.
 */
#include "xxfclib/formats/webexe/xx_webexe.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#define WEBEXE_MEMORY_CAP (UINT64_C(64)*1024*1024)

typedef struct webexe_read_context {uint32_t crc;uint64_t resident;} webexe_read_context;
typedef struct webexe_sink {uint8_t *p;size_t n,at;} webexe_sink;
static uint32_t webexe_u32(const uint8_t *p) {return xx_data_get_u32(p,4,0,false);}
static bool webexe_stop(xx_pd_struct *pd) {return pd && xx_pd_is_stopped(pd);}
static bool webexe_span(uint64_t p,uint64_t z,uint64_t n) {return p<=n && z<=n-p;}
static uint64_t webexe_limit(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    return v?xx_var_get_u64(v):UINT64_C(256)*1024*1024;
}
static uint64_t webexe_resident(pm_stream *s) {
    uint64_t used=s?(uint64_t)s->capacity*sizeof(pm_member):0;size_t i;
    if(s)for(i=0;i<s->count;++i){pm_member *m=&s->items[i];uint64_t z=m->memory?(uint64_t)m->size:0;
        if(m->display_name)z+=xx_rt_strlen(m->display_name)+1;
        if(m->context)z+=sizeof(webexe_read_context);
        used+=z;}
    return used;
}
static bool webexe_budget(Abstractformat *f,pm_stream *s,uint64_t extra) {
    uint64_t used=webexe_resident(s),limit=webexe_limit(f);return used<=limit&&extra<=limit-used;
}
static bool webexe_decode_budget(Abstractformat *f,pm_member *m) {
    webexe_read_context *c=m->context;
    return c&&webexe_budget(f,NULL,c->resident+(uint64_t)m->packed_size+(uint64_t)m->size);
}
static bool webexe_device_leaf(const char *p,size_t n) {
    char leaf[5];size_t i=0;while(i<n&&p[i]!='.'&&i<sizeof(leaf)-1){unsigned char c=(unsigned char)p[i];leaf[i]=(char)(c>='a'&&c<='z'?c-32:c);++i;}leaf[i]=0;
    if(i<n&&p[i]!='.')return false;
    return !xx_rt_strcmp(leaf,"CON")||!xx_rt_strcmp(leaf,"PRN")||!xx_rt_strcmp(leaf,"AUX")||!xx_rt_strcmp(leaf,"NUL")||
           (i==4&&(!xx_rt_memcmp(leaf,"COM",3)||!xx_rt_memcmp(leaf,"LPT",3))&&leaf[3]>='1'&&leaf[3]<='9');
}
static bool webexe_name(Abstractformat *f,pm_stream *s,const char *name,int64_t at,int64_t size) {
    char *copy;size_t i,n=xx_rt_strlen(name),part=0;
    if(!n || n>4096 || name[0]=='/' || name[0]=='\\') return false;
    if(!webexe_budget(f,s,n+1+(s->count==s->capacity?(uint64_t)(s->capacity?s->capacity:8)*sizeof(pm_member):0)))return false;
    copy=xx_str_dup(name);if(!copy)return false;
    for(i=0;i<=n;++i) {unsigned c=(unsigned char)copy[i];
        if(c=='\\')copy[i]='/';
        if(c && (c<32 || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*'))goto bad;
        if(!c || copy[i]=='/') {size_t z=i-part;if(!z || (z==1&&copy[part]=='.') || (z==2&&copy[part]=='.'&&copy[part+1]=='.') || copy[i-1]=='.' || copy[i-1]==' ' || webexe_device_leaf(copy+part,z))goto bad;part=i+1;}
    }
    if(!webexe_budget(f,s,s->count==s->capacity?(uint64_t)(s->capacity?s->capacity:8)*sizeof(pm_member):0)||!pm_add(f,s,name,at,size))goto bad;
    s->items[s->count-1].display_name=copy;return true;
bad:xx_str_free(copy);return false;
}
static int64_t webexe_overlay(Abstractformat *f) {
    uint8_t h[64],p[24],section[40];uint32_t pe;unsigned count,opt,i;uint64_t end,n=(uint64_t)pm_available(f);
    if(n<2 || !pm_read(f,0,h,2))return -1;if(h[0]!='M'||h[1]!='Z')return 0;
    if(n<64 || !pm_read(f,0,h,64))return -1;pe=webexe_u32(h+60);
    if(!webexe_span(pe,24,n)||!pm_read(f,pe,p,24)||xx_rt_memcmp(p,"PE\0\0",4))return -1;
    count=xx_data_get_u16(p+6,2,0,false);opt=xx_data_get_u16(p+20,2,0,false);
    if(!count || count>96 || opt<64 || opt>4096 || !webexe_span((uint64_t)pe+24,opt+(uint64_t)count*40,n) || !pm_read(f,(int64_t)pe+84,h,4))return -1;
    end=webexe_u32(h);
    for(i=0;i<count;++i) {uint64_t finish;if(!pm_read(f,(int64_t)pe+24+opt+(int64_t)i*40,section,40))return -1;
        finish=(uint64_t)webexe_u32(section+16)+webexe_u32(section+20);if(finish>n)return -1;if(finish>end)end=finish;}
    /* Some signed launchers append their archive after WIN_CERTIFICATE. */
    if(!pm_read(f,(int64_t)pe+24,h,2))return -1;
    {unsigned dd=xx_data_get_u16(h,2,0,false)==0x10b?128:xx_data_get_u16(h,2,0,false)==0x20b?144:0;
        if(dd && opt>=dd+8 && pm_read(f,(int64_t)pe+24+dd,h,8)) {uint64_t cert=webexe_u32(h),bytes=webexe_u32(h+4);
            if(bytes>=8 && webexe_span(cert,bytes,n) && (cert==end || (webexe_span(end,bytes,n) && cert+bytes==n))) {uint64_t at=cert;bool prefix=cert!=end;
                if(prefix) {uint8_t first[8],last[8];
                    if(!pm_read(f,(int64_t)end,first,8)||!pm_read(f,(int64_t)cert,last,8)||xx_rt_memcmp(first,last,8))return (int64_t)end;
                    /* A re-signed launcher retains its older certificate
                     * before the appended archive. Their signatures differ. */
                }
                while(at<cert+bytes) {uint32_t size;if(!pm_read(f,(int64_t)at,h,8))return -1;size=webexe_u32(h);
                    if(size<8 || !webexe_span(at,size,cert+bytes) || xx_data_get_u16(h+4,2,0,false)!=0x200 || xx_data_get_u16(h+6,2,0,false)!=2)return -1;
                    at=(at+size+7)&~UINT64_C(7);}
                if(at!=cert+bytes)return -1;end=prefix?end+bytes:at;
            }
        }
    }
    return end<=n?(int64_t)end:-1;
}
static ssize_t webexe_write(xx_io_device *d,const void *p,size_t n) {webexe_sink *s=d->priv;if(n>s->n-s->at)return -1;if(n)xx_rt_memcpy(s->p+s->at,p,n);s->at+=n;return (ssize_t)n;}
static bool webexe_inflate(const uint8_t *p,size_t n,uint8_t *out,size_t z,xx_pd_struct *pd) {
    xx_io_device sink;webexe_sink s={out,z,0};size_t used=0;xx_mem_zero(&sink,sizeof(sink));sink.priv=&s;sink.write=webexe_write;
    return !webexe_stop(pd) && xx_deflate_unpack_memory_to_device_ex(p,n,&sink,&used,false,pd) && used==n && s.at==z && !webexe_stop(pd);
}
static bool webexe_digits(const uint8_t *p,unsigned count,uint64_t *out) {
    unsigned i;uint64_t n=0;for(i=0;i<count;++i){if(p[i]<'0'||p[i]>'9')return false;n=n*10+p[i]-'0';}*out=n;return true;
}
static bool webexe_webexe_header(Abstractformat *f,uint64_t *begin,uint64_t *end) {
    uint8_t h[27],probe[1024];uint64_t physical;size_t i,bytes;int64_t at=webexe_overlay(f),n=pm_available(f);
    if(at<0||!pm_read(f,at,h,27)||xx_rt_memcmp(h,"*M2E*",5)||h[5]<'1'||h[5]>'9'||h[6]!='A'||
       !webexe_digits(h+7,10,end)||!webexe_digits(h+17,10,&physical)||physical!=(uint64_t)n||*end>(uint64_t)n||*end<=(uint64_t)at+27)return false;
    bytes=(size_t)(*end-(uint64_t)at-27);if(bytes>sizeof(probe))bytes=sizeof(probe);
    if(!pm_read(f,at+27,probe,bytes))return false;
    for(i=0;i+4<=bytes;++i)if(!xx_rt_memcmp(probe+i,"PK\x03\x04",4)){*begin=(uint64_t)at+27+i;return true;}
    return false;
}
static bool webexe_zip_read(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    uint8_t *packed=NULL,*plain=NULL;size_t at=0,z=(size_t)m->size;bool ok=false;
    if((uint64_t)m->packed_size>WEBEXE_MEMORY_CAP||(uint64_t)m->size>WEBEXE_MEMORY_CAP||!webexe_decode_budget(f,m)||webexe_stop(pd))return false;
    packed=xx_mem_alloc(m->packed_size?(size_t)m->packed_size:1);plain=xx_mem_alloc(z?z:1);
    if(!packed||!plain||!pm_read(f,m->offset-f->base_address,packed,(size_t)m->packed_size))goto done;
    if(m->compression_method==8){if(!webexe_inflate(packed,(size_t)m->packed_size,plain,z,pd))goto done;}
    else {if(m->packed_size!=m->size)goto done;if(z)xx_rt_memcpy(plain,packed,z);}
    if(xx_crc32_calc(0,plain,z)!=((webexe_read_context *)m->context)->crc)goto done;
    while(out&&at<z){size_t size=z-at;ssize_t got;if(size>65536)size=65536;if(webexe_stop(pd))goto done;got=xx_io_write(out,plain+at,size);if(got<=0||(size_t)got>size)goto done;at+=(size_t)got;}ok=!webexe_stop(pd);
done:xx_mem_free(packed);xx_mem_free(plain);return ok;
}
static bool webexe_webexe(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t at,end;uint8_t h[46];if(!webexe_webexe_header(f,&at,&end))return false;
    while(at<end){uint32_t sig;uint64_t next;if(webexe_stop(pd)||!pm_read(f,(int64_t)at,h,4))return false;sig=webexe_u32(h);
        if(sig==UINT32_C(0x04034b50)){char name[4097];uint16_t flags,method,len,extra;uint32_t packed,plain;webexe_read_context *context;
            if(!webexe_span(at,30,end)||!pm_read(f,(int64_t)at,h,30))return false;
            flags=xx_data_get_u16(h+6,2,0,false);method=xx_data_get_u16(h+8,2,0,false);packed=webexe_u32(h+18);plain=webexe_u32(h+22);len=xx_data_get_u16(h+26,2,0,false);extra=xx_data_get_u16(h+28,2,0,false);
            if(flags&~UINT16_C(0x0806)|| (method!=0&&method!=8)||!len||len>4096||packed>WEBEXE_MEMORY_CAP||plain>WEBEXE_MEMORY_CAP||!webexe_span(at+30,(uint64_t)len+extra+packed,end)||!pm_read(f,(int64_t)at+30,name,len)||xx_rt_memchr(name,0,len))return false;
            name[len]=0;next=at+30+len+extra;
            /* WebExe repeats a placeholder leaf ending in a dot. Give each
             * ZIP payload its own portable numbered leaf; the opaque vendor
             * index does not supply recovered document names here. */
            if(name[len-1]=='.'||name[len-1]==' ')name[len-1]='_';
            if(!webexe_name(f,s,name,(int64_t)next,packed))return false;
            xx_str_free(s->items[s->count-1].display_name);s->items[s->count-1].display_name=NULL;
            if(!webexe_budget(f,s,sizeof(*context)))return false;context=xx_mem_calloc(1,sizeof(*context));if(!context)return false;context->crc=webexe_u32(h+14);
            s->items[s->count-1].context=context;s->items[s->count-1].free_context=xx_mem_free;s->items[s->count-1].size=plain;s->items[s->count-1].compression_method=method;s->items[s->count-1].read_all=webexe_zip_read;at=next+packed;
        }else if(sig==UINT32_C(0x02014b50)){
            if(!webexe_span(at,46,end)||!pm_read(f,(int64_t)at,h,46))return false;next=46U+xx_data_get_u16(h+28,2,0,false)+xx_data_get_u16(h+30,2,0,false)+xx_data_get_u16(h+32,2,0,false);if(!webexe_span(at,next,end))return false;at+=next;
        }else if(sig==UINT32_C(0x06054b50)){
            if(!webexe_span(at,22,end)||!pm_read(f,(int64_t)at,h,22))return false;next=22U+xx_data_get_u16(h+20,2,0,false);if(!webexe_span(at,next,end))return false;at+=next;
        }else return false;
    }s->size=pm_available(f);return s->count&&at==end;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    bool ok;size_t i;uint64_t resident;
    if(webexe_stop(pd)||!webexe_budget(f,s,8*sizeof(pm_member)))return false;
    ok=webexe_webexe(f,s,pd);
    if(!ok)return false;
    for(i=0;i<s->count;++i)if(s->items[i].read_all&&!s->items[i].context){webexe_read_context *c;if(!webexe_budget(f,s,sizeof(*c)))return false;c=xx_mem_calloc(1,sizeof(*c));if(!c)return false;s->items[i].context=c;s->items[i].free_context=xx_mem_free;}
    resident=webexe_resident(s);if(!webexe_budget(f,s,0))return false;
    for(i=0;i<s->count;++i)if(s->items[i].read_all)((webexe_read_context *)s->items[i].context)->resident=resident;
    return true;
}

Abstractformat *xx_webexe_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f){xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,XX_FILE_TYPE_WEBEXE,"exe");}
    return f;
}
void xx_webexe_free(Abstractformat *f) {
    if(f){xx_format_cleanup_extra_parameters(f);xx_mem_free(f);}
}

xx_file_type_t xx_webexe_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[40];int64_t cursor,n;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d||base<0||xx_io_size(d)<base)return type;
    cursor=xx_io_tell(d);if(cursor<0)return type;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;
    n=pm_available(&f);
    if(n>=24 && pm_read(&f,0,h,24)) {int64_t at=webexe_overlay(&f);if(at>=0&&pm_read(&f,at,h,19)&&!xx_rt_memcmp(h,"*M2E*",5)){uint64_t begin,end;if(webexe_webexe_header(&f,&begin,&end))type=XX_FILE_TYPE_WEBEXE;}}
    if(xx_io_seek64(d,cursor,SEEK_SET))return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_webexe_open(xx_io_device *d) {return xx_webexe_create(d,0);}
static const xx_file_type_t xx_webexe_types[]={XX_FILE_TYPE_WEBEXE};
static const xx_format_search_desc xx_webexe_desc={xx_webexe_types,1,NULL,0,xx_webexe_open,xx_webexe_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(webexe,xx_webexe_desc)
