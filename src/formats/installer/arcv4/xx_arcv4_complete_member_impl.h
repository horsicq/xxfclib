/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_ARCV4_COMPLETE_MEMBER_IMPL_H
#define XX_ARCV4_COMPLETE_MEMBER_IMPL_H
#include "../../../fs/platforms/xx_fs_platform.h"
#define ARCV4_RECOVERY_LIMIT (64U*1024U*1024U)
typedef struct arcv4_recovery {
    Abstractformat *source;
    int64_t at,bytes;
    uint32_t packed,raw,method;
    const char *name,*directory;
    unsigned entries,attempts;
    bool conflict;
    uint8_t *plain;
    size_t size;
    xx_pd_struct *pd;
} arcv4_recovery;
static const char *arcv4_leaf(const char *p)
{
    const char *leaf=p;
    for(;*p;++p)if(*p=='/'||*p=='\\')leaf=p+1;
    return leaf;
}
static bool arcv4_prefix_equal(arcv4_recovery *s,xx_io_device *d,int64_t at)
{
    uint8_t a[32768],b[32768];size_t done=0;
    while(done<s->packed) {
        size_t take=s->packed-done;if(take>sizeof(a))take=sizeof(a);
        if(xx_pd_is_stopped(s->pd)||!arcv4_read_at(s->source->device,s->at+(int64_t)done,a,take)||
           !arcv4_read_at(d,at+(int64_t)done,b,take)||xx_rt_memcmp(a,b,take))return false;
        done+=take;
    }
    return true;
}
static bool arcv4_recovery_candidate(void *opaque,const char *name,bool directory)
{
    arcv4_recovery *s=(arcv4_recovery*)opaque;
    xx_io_device *d=NULL;Abstractformat f;arcv4_stream *catalog=NULL;
    uint8_t header[6],*plain=NULL;size_t size=0;int64_t physical;char *path;bool more=true;
    if(++s->entries>4096U||xx_pd_is_stopped(s->pd))return false;
    if(directory)return true;
    if(xx_str_len(s->directory)+xx_str_len(name)>4094U)return false;
    path=xx_str_concat3(s->directory,"/",name);if(!path)return false;
    d=xx_io_file_open(path,"rb");xx_str_free(path);if(!d)return true;
    if(!xx_io_read_at(d,0,header,sizeof(header))||xx_rt_memcmp(header,"ARCV\0\4",6U))goto done;
    physical=xx_io_total_size(d);
    if(physical<0||physical>ARCV4_RECOVERY_LIMIT)goto done;
    if(physical>INT64_C(512)*1024*1024-s->bytes){more=false;goto done;}
    s->bytes+=physical;f=*s->source;f.device=d;f.base_address=0;
    if(!arcv4_parse(&f,&catalog,s->pd))goto done;
    for(size_t i=0;i<catalog->count;++i) {
        arcv4_member *m=&catalog->items[i];
        if(m->spanned||m->method!=s->method||m->original_size!=s->raw||m->packed_size<=s->packed||
           m->packed_size>ARCV4_RECOVERY_LIMIT||xx_str_icmp(arcv4_leaf(m->name),arcv4_leaf(s->name)))continue;
        if(++s->attempts>32U){more=false;break;}
        if(!arcv4_prefix_equal(s,d,m->data_offset)||xx_pd_is_stopped(s->pd)||!arcv4_decode_member(&f,m,&plain,&size,s->pd))continue;
        if(s->plain) {
            if(size!=s->size||xx_rt_memcmp(plain,s->plain,size)){s->conflict=true;more=false;}
            xx_mem_free(plain);plain=NULL;
            if(!more)break;
        } else {s->plain=plain;s->size=size;plain=NULL;}
    }
done:
    xx_mem_free(plain);arcv4_stream_free(catalog);xx_io_close(d);return more;
}
bool xx_arcv4_recover_split_prefix(Abstractformat *source,int64_t at,uint32_t packed,uint32_t raw,
                                  uint32_t method,const char *name,uint8_t **plain,size_t *size,xx_pd_struct *pd)
{
    arcv4_recovery s;const char *path,*p,*value;char *directory=NULL,*extra=NULL;size_t prefix=0;
    int64_t saved;bool ok=false;
    if(!source||!source->device||at<0||packed>ARCV4_RECOVERY_LIMIT||!raw||raw>ARCV4_RECOVERY_LIMIT||
       (method!=0U&&method!=2U)||!name||!plain||!size||xx_pd_is_stopped(pd))return false;
    saved=xx_io_tell(source->device);if(saved<0)return false;
    xx_mem_zero(&s,sizeof(s));s.source=source;s.at=at;s.packed=packed;s.raw=raw;s.method=method;s.name=name;s.pd=pd;
    value=xx_rt_getenv("XXFC_ARCV4_COMPANION_DIR");if(value&&*value&&xx_str_len(value)<4095U)extra=xx_str_dup(value);
    path=xx_io_source_path(source->device);
    if(path&&xx_str_len(path)<4095U) {
        for(p=path;*p;++p)if(*p=='/'||*p=='\\')prefix=(size_t)(p-path)+1U;
        directory=prefix?xx_str_create_len(prefix):xx_str_dup(".");
        if(!directory)goto done;
        if(prefix){xx_mem_copy(directory,path,prefix);directory[prefix]=0;}
        s.directory=directory;if(!xx_fs_platform_enumerate(directory,arcv4_recovery_candidate,&s))goto done;
    }
    if(extra&&(!directory||xx_str_icmp(extra,directory))) {
        s.directory=extra;if(!xx_fs_platform_enumerate(extra,arcv4_recovery_candidate,&s))goto done;
    }
    if(s.plain&&!s.conflict&&!xx_pd_is_stopped(pd)){*plain=s.plain;*size=s.size;s.plain=NULL;ok=true;}
done:
    xx_mem_free(s.plain);xx_str_free(directory);xx_str_free(extra);(void)xx_io_seek64(source->device,saved,SEEK_SET);return ok;
}
#endif
