/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for DeskSoft installer.
 */
#include "xxfclib/formats/desksoft/xx_desksoft.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#define DESKSOFT_MEMORY_CAP (UINT64_C(64)*1024*1024)

typedef struct desksoft_read_context {uint32_t crc;uint64_t resident;} desksoft_read_context;
static uint32_t desksoft_u32(const uint8_t *p) {return xx_data_get_u32(p,4,0,false);}
static bool desksoft_stop(xx_pd_struct *pd) {return pd && xx_pd_is_stopped(pd);}
static bool desksoft_span(uint64_t p,uint64_t z,uint64_t n) {return p<=n && z<=n-p;}
static uint64_t desksoft_limit(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    return v?xx_var_get_u64(v):UINT64_C(256)*1024*1024;
}
static uint64_t desksoft_resident(pm_stream *s) {
    uint64_t used=s?(uint64_t)s->capacity*sizeof(pm_member):0;size_t i;
    if(s)for(i=0;i<s->count;++i){pm_member *m=&s->items[i];uint64_t z=m->memory?(uint64_t)m->size:0;
        if(m->display_name)z+=xx_rt_strlen(m->display_name)+1;
        if(m->context)z+=sizeof(desksoft_read_context);
        used+=z;}
    return used;
}
static bool desksoft_budget(Abstractformat *f,pm_stream *s,uint64_t extra) {
    uint64_t used=desksoft_resident(s),limit=desksoft_limit(f);return used<=limit&&extra<=limit-used;
}
static bool desksoft_device_leaf(const char *p,size_t n) {
    char leaf[5];size_t i=0;while(i<n&&p[i]!='.'&&i<sizeof(leaf)-1){unsigned char c=(unsigned char)p[i];leaf[i]=(char)(c>='a'&&c<='z'?c-32:c);++i;}leaf[i]=0;
    if(i<n&&p[i]!='.')return false;
    return !xx_rt_strcmp(leaf,"CON")||!xx_rt_strcmp(leaf,"PRN")||!xx_rt_strcmp(leaf,"AUX")||!xx_rt_strcmp(leaf,"NUL")||
           (i==4&&(!xx_rt_memcmp(leaf,"COM",3)||!xx_rt_memcmp(leaf,"LPT",3))&&leaf[3]>='1'&&leaf[3]<='9');
}
static bool desksoft_name(Abstractformat *f,pm_stream *s,const char *name,int64_t at,int64_t size) {
    char *copy;size_t i,n=xx_rt_strlen(name),part=0;
    if(!n || n>4096 || name[0]=='/' || name[0]=='\\') return false;
    if(!desksoft_budget(f,s,n+1+(s->count==s->capacity?(uint64_t)(s->capacity?s->capacity:8)*sizeof(pm_member):0)))return false;
    copy=xx_str_dup(name);if(!copy)return false;
    for(i=0;i<=n;++i) {unsigned c=(unsigned char)copy[i];
        if(c=='\\')copy[i]='/';
        if(c && (c<32 || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*'))goto bad;
        if(!c || copy[i]=='/') {size_t z=i-part;if(!z || (z==1&&copy[part]=='.') || (z==2&&copy[part]=='.'&&copy[part+1]=='.') || copy[i-1]=='.' || copy[i-1]==' ' || desksoft_device_leaf(copy+part,z))goto bad;part=i+1;}
    }
    if(!desksoft_budget(f,s,s->count==s->capacity?(uint64_t)(s->capacity?s->capacity:8)*sizeof(pm_member):0)||!pm_add(f,s,name,at,size))goto bad;
    s->items[s->count-1].display_name=copy;return true;
bad:xx_str_free(copy);return false;
}
static int64_t desksoft_overlay(Abstractformat *f) {
    uint8_t h[64],p[24],section[40];uint32_t pe;unsigned count,opt,i;uint64_t end,n=(uint64_t)pm_available(f);
    if(n<2 || !pm_read(f,0,h,2))return -1;if(h[0]!='M'||h[1]!='Z')return 0;
    if(n<64 || !pm_read(f,0,h,64))return -1;pe=desksoft_u32(h+60);
    if(!desksoft_span(pe,24,n)||!pm_read(f,pe,p,24)||xx_rt_memcmp(p,"PE\0\0",4))return -1;
    count=xx_data_get_u16(p+6,2,0,false);opt=xx_data_get_u16(p+20,2,0,false);
    if(!count || count>96 || opt<64 || opt>4096 || !desksoft_span((uint64_t)pe+24,opt+(uint64_t)count*40,n) || !pm_read(f,(int64_t)pe+84,h,4))return -1;
    end=desksoft_u32(h);
    for(i=0;i<count;++i) {uint64_t finish;if(!pm_read(f,(int64_t)pe+24+opt+(int64_t)i*40,section,40))return -1;
        finish=(uint64_t)desksoft_u32(section+16)+desksoft_u32(section+20);if(finish>n)return -1;if(finish>end)end=finish;}
    /* Some signed launchers append their archive after WIN_CERTIFICATE. */
    if(!pm_read(f,(int64_t)pe+24,h,2))return -1;
    {unsigned dd=xx_data_get_u16(h,2,0,false)==0x10b?128:xx_data_get_u16(h,2,0,false)==0x20b?144:0;
        if(dd && opt>=dd+8 && pm_read(f,(int64_t)pe+24+dd,h,8)) {uint64_t cert=desksoft_u32(h),bytes=desksoft_u32(h+4);
            if(bytes>=8 && desksoft_span(cert,bytes,n) && (cert==end || (desksoft_span(end,bytes,n) && cert+bytes==n))) {uint64_t at=cert;bool prefix=cert!=end;
                if(prefix) {uint8_t first[8],last[8];
                    if(!pm_read(f,(int64_t)end,first,8)||!pm_read(f,(int64_t)cert,last,8)||xx_rt_memcmp(first,last,8))return (int64_t)end;
                    /* A re-signed launcher retains its older certificate
                     * before the appended archive. Their signatures differ. */
                }
                while(at<cert+bytes) {uint32_t size;if(!pm_read(f,(int64_t)at,h,8))return -1;size=desksoft_u32(h);
                    if(size<8 || !desksoft_span(at,size,cert+bytes) || xx_data_get_u16(h+4,2,0,false)!=0x200 || xx_data_get_u16(h+6,2,0,false)!=2)return -1;
                    at=(at+size+7)&~UINT64_C(7);}
                if(at!=cert+bytes)return -1;end=prefix?end+bytes:at;
            }
        }
    }
    return end<=n?(int64_t)end:-1;
}
static bool desksoft_desksoft(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[19],c;int64_t at=desksoft_overlay(f),n=pm_available(f);char name[4097];size_t k;
    if(at<0||!pm_read(f,at,h,19)||xx_rt_memcmp(h,"<Setup Data Header>",19))return false;at+=19;
    for(;;) {uint32_t size;if(desksoft_stop(pd)||!pm_read(f,at,h,16))return false;
        if(!xx_rt_memcmp(h,"<Setup Data End>",16)) {s->size=at+16;return s->count && s->size==n;}
        for(k=0;k<4096;++k) {if(!pm_read(f,at++,&c,1))return false;name[k]=(char)c;if(!c)break;}if(k==4096||!k)return false;
        if(!pm_read(f,at,h,4))return false;size=desksoft_u32(h);at+=4;if(!desksoft_name(f,s,name,at,size))return false;at+=size;
    }
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    bool ok;size_t i;uint64_t resident;
    if(desksoft_stop(pd)||!desksoft_budget(f,s,8*sizeof(pm_member)))return false;
    ok=desksoft_desksoft(f,s,pd);
    if(!ok)return false;
    for(i=0;i<s->count;++i)if(s->items[i].read_all&&!s->items[i].context){desksoft_read_context *c;if(!desksoft_budget(f,s,sizeof(*c)))return false;c=xx_mem_calloc(1,sizeof(*c));if(!c)return false;s->items[i].context=c;s->items[i].free_context=xx_mem_free;}
    resident=desksoft_resident(s);if(!desksoft_budget(f,s,0))return false;
    for(i=0;i<s->count;++i)if(s->items[i].read_all)((desksoft_read_context *)s->items[i].context)->resident=resident;
    return true;
}

Abstractformat *xx_desksoft_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f){xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,XX_FILE_TYPE_DESKSOFT,"exe");}
    return f;
}
void xx_desksoft_free(Abstractformat *f) {
    if(f){xx_format_cleanup_extra_parameters(f);xx_mem_free(f);}
}

xx_file_type_t xx_desksoft_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[40];int64_t cursor,n;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d||base<0||xx_io_size(d)<base)return type;
    cursor=xx_io_tell(d);if(cursor<0)return type;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;
    n=pm_available(&f);
    if(n>=24 && pm_read(&f,0,h,24)) {int64_t at=desksoft_overlay(&f);if(at>=0&&pm_read(&f,at,h,19)&&!xx_rt_memcmp(h,"<Setup Data Header>",19))type=XX_FILE_TYPE_DESKSOFT;}
    if(xx_io_seek64(d,cursor,SEEK_SET))return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_desksoft_open(xx_io_device *d) {return xx_desksoft_create(d,0);}
static const xx_file_type_t xx_desksoft_types[]={XX_FILE_TYPE_DESKSOFT};
static const xx_format_search_desc xx_desksoft_desc={xx_desksoft_types,1,NULL,0,xx_desksoft_open,xx_desksoft_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(desksoft,xx_desksoft_desc)
