/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for Audials CMP resources.
 */
#include "xxfclib/formats/audials/xx_audials.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#define AUDIALS_MEMORY_CAP (UINT64_C(64)*1024*1024)

typedef struct audials_read_context {uint32_t crc;uint64_t resident;} audials_read_context;
static bool audials_stop(xx_pd_struct *pd) {return pd && xx_pd_is_stopped(pd);}
static bool audials_span(uint64_t p,uint64_t z,uint64_t n) {return p<=n && z<=n-p;}
static uint64_t audials_limit(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    return v?xx_var_get_u64(v):UINT64_C(256)*1024*1024;
}
static uint64_t audials_resident(pm_stream *s) {
    uint64_t used=s?(uint64_t)s->capacity*sizeof(pm_member):0;size_t i;
    if(s)for(i=0;i<s->count;++i){pm_member *m=&s->items[i];uint64_t z=m->memory?(uint64_t)m->size:0;
        if(m->display_name)z+=xx_rt_strlen(m->display_name)+1;
        if(m->context)z+=sizeof(audials_read_context);
        used+=z;}
    return used;
}
static bool audials_budget(Abstractformat *f,pm_stream *s,uint64_t extra) {
    uint64_t used=audials_resident(s),limit=audials_limit(f);return used<=limit&&extra<=limit-used;
}
static bool audials_device_leaf(const char *p,size_t n) {
    char leaf[5];size_t i=0;while(i<n&&p[i]!='.'&&i<sizeof(leaf)-1){unsigned char c=(unsigned char)p[i];leaf[i]=(char)(c>='a'&&c<='z'?c-32:c);++i;}leaf[i]=0;
    if(i<n&&p[i]!='.')return false;
    return !xx_rt_strcmp(leaf,"CON")||!xx_rt_strcmp(leaf,"PRN")||!xx_rt_strcmp(leaf,"AUX")||!xx_rt_strcmp(leaf,"NUL")||
           (i==4&&(!xx_rt_memcmp(leaf,"COM",3)||!xx_rt_memcmp(leaf,"LPT",3))&&leaf[3]>='1'&&leaf[3]<='9');
}
static bool audials_name(Abstractformat *f,pm_stream *s,const char *name,int64_t at,int64_t size) {
    char *copy;size_t i,n=xx_rt_strlen(name),part=0;
    if(!n || n>4096 || name[0]=='/' || name[0]=='\\') return false;
    if(!audials_budget(f,s,n+1+(s->count==s->capacity?(uint64_t)(s->capacity?s->capacity:8)*sizeof(pm_member):0)))return false;
    copy=xx_str_dup(name);if(!copy)return false;
    for(i=0;i<=n;++i) {unsigned c=(unsigned char)copy[i];
        if(c=='\\')copy[i]='/';
        if(c && (c<32 || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*'))goto bad;
        if(!c || copy[i]=='/') {size_t z=i-part;if(!z || (z==1&&copy[part]=='.') || (z==2&&copy[part]=='.'&&copy[part+1]=='.') || copy[i-1]=='.' || copy[i-1]==' ' || audials_device_leaf(copy+part,z))goto bad;part=i+1;}
    }
    if(!audials_budget(f,s,s->count==s->capacity?(uint64_t)(s->capacity?s->capacity:8)*sizeof(pm_member):0)||!pm_add(f,s,name,at,size))goto bad;
    s->items[s->count-1].display_name=copy;return true;
bad:xx_str_free(copy);return false;
}
static bool audials_line(Abstractformat *f,uint64_t *at,char *line,size_t cap,xx_pd_struct *pd) {
    size_t k=0;uint8_t c;for(;;) {if(audials_stop(pd)||k+1>=cap||!pm_read(f,(int64_t)(*at)++,&c,1))return false;
        if(c=='\r') {if(!pm_read(f,(int64_t)(*at)++,&c,1)||c!='\n')return false;line[k]=0;return true;}if(c<32 && c!='\t')return false;line[k++]=(char)c;}
}
static bool audials_decimal(const char *p,uint64_t *value) {
    uint64_t z=0;unsigned digits=0;while(*p==' ')++p;while(*p>='0'&&*p<='9') {if(z>(UINT64_MAX-9)/10)return false;z=z*10+(unsigned)(*p++-'0');++digits;}while(*p==' ')++p;if(!digits||*p)return false;*value=z;return true;
}
static bool audials_audials(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    char line[8192];uint64_t at=0,count,i,n=(uint64_t)pm_available(f),end=0;
    if(!audials_line(f,&at,line,sizeof(line),pd)||xx_rt_strncmp(line,"FilesNumber=",12)||!audials_decimal(line+12,&count)||!count||count>65536)return false;
    for(i=0;i<count;++i) {char *tab,*second;uint64_t offset,size;size_t len;
        if(!audials_line(f,&at,line,sizeof(line),pd)||!(tab=xx_rt_strchr(line,'\t')))return false;*tab++=0;second=xx_rt_strchr(tab,'\t');if(!second)return false;*second++=0;
        len=xx_rt_strlen(line);while(len&&line[len-1]==' ')line[--len]=0;if(!audials_decimal(tab,&offset)||!audials_decimal(second,&size)||!audials_span(offset,size,n)||!audials_name(f,s,line,0,0))return false;
        s->items[s->count-1].offset=(int64_t)offset;s->items[s->count-1].size=s->items[s->count-1].packed_size=(int64_t)size;
    }
    for(i=0;i<count;++i) {pm_member *m=&s->items[i];uint64_t offset=(uint64_t)m->offset,size=(uint64_t)m->size;if(!audials_span(at+offset,size,n))return false;m->offset=f->base_address+(int64_t)(at+offset);if(at+offset+size>end)end=at+offset+size;}
    s->size=(int64_t)n;return end==n;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    bool ok;size_t i;uint64_t resident;
    if(audials_stop(pd)||!audials_budget(f,s,8*sizeof(pm_member)))return false;
    ok=audials_audials(f,s,pd);
    if(!ok)return false;
    for(i=0;i<s->count;++i)if(s->items[i].read_all&&!s->items[i].context){audials_read_context *c;if(!audials_budget(f,s,sizeof(*c)))return false;c=xx_mem_calloc(1,sizeof(*c));if(!c)return false;s->items[i].context=c;s->items[i].free_context=xx_mem_free;}
    resident=audials_resident(s);if(!audials_budget(f,s,0))return false;
    for(i=0;i<s->count;++i)if(s->items[i].read_all)((audials_read_context *)s->items[i].context)->resident=resident;
    return true;
}

Abstractformat *xx_audials_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f){xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,XX_FILE_TYPE_AUDIALS,"cmp");}
    return f;
}
void xx_audials_free(Abstractformat *f) {
    if(f){xx_format_cleanup_extra_parameters(f);xx_mem_free(f);}
}

xx_file_type_t xx_audials_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[40];int64_t cursor,n;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d||base<0||xx_io_size(d)<base)return type;
    cursor=xx_io_tell(d);if(cursor<0)return type;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;
    n=pm_available(&f);
    if(n>=24 && pm_read(&f,0,h,24) && !xx_rt_memcmp(h,"FilesNumber=",12))type=XX_FILE_TYPE_AUDIALS;
    if(xx_io_seek64(d,cursor,SEEK_SET))return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_audials_open(xx_io_device *d) {return xx_audials_create(d,0);}
static const xx_file_type_t xx_audials_types[]={XX_FILE_TYPE_AUDIALS};
static const xx_format_search_desc xx_audials_desc={xx_audials_types,1,NULL,0,xx_audials_open,xx_audials_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(audials,xx_audials_desc)
