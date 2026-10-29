/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_LINGVOARC_VOLUMES_H
#define XX_LINGVOARC_VOLUMES_H
#include "../../../fs/platforms/xx_fs_platform.h"
#define LVA_JOIN_LIMIT (64U*1024U*1024U)
#define LVA_SCAN_LIMIT (INT64_C(512)*1024*1024)
typedef struct lva_volume_scan {
    Abstractformat *selected;
    const lva_member *member;
    const char *directory,*selected_name;
    uint32_t version;
    unsigned seen,attempts,accepted;
    int64_t bytes;
    uint8_t *plain;
    size_t plain_size;
    xx_pd_struct *pd;
} lva_volume_scan;

static bool lva_join_copy(xx_io_device *d,int64_t at,uint8_t *out,size_t n,xx_pd_struct *pd)
{
    while(n) {
        size_t take=n>32768U?32768U:n;
        if(xx_pd_is_stopped(pd)||!xx_io_read_at(d,at,out,take))return false;
        at+=(int64_t)take;out+=take;n-=take;
    }
    return true;
}

static bool lva_volume_candidate(void *opaque,const char *name,bool is_directory)
{
    lva_volume_scan *s=(lva_volume_scan*)opaque;
    xx_io_device *d=NULL,*memory=NULL;
    lva_stream *catalog=NULL;
    lva_member *other,start;
    Abstractformat candidate,joined;
    uint8_t header[18],fh[17],*packed=NULL,*plain=NULL;
    size_t plain_size=0,total;
    int64_t size;
    char *path;
    bool keep_going=true;
    if(++s->seen>4096U||xx_pd_is_stopped(s->pd))return false;
    if(is_directory||(s->selected_name&&!xx_rt_strcmp(name,s->selected_name)))return true;
    if(xx_str_len(s->directory)+xx_str_len(name)>4094U)return false;
    path=xx_str_concat3(s->directory,"/",name);if(!path)return false;
    d=xx_io_file_open(path,"rb");xx_str_free(path);if(!d)return true;
    if(!xx_io_read_at(d,0,header,sizeof(header))||xx_rt_memcmp(header,"lingvoArc",9U)||
       header[9]!=(uint8_t)('0'+s->version))goto done;
    size=xx_io_total_size(d);
    if(size<18||size>LVA_SCAN_LIMIT-s->bytes){keep_going=false;goto done;}
    s->bytes+=size;candidate=*s->selected;candidate.device=d;candidate.base_address=0;
    if(!lva_parse_container(&candidate,size,header,&catalog))goto done;
    other=s->member->method==6U?&catalog->items[0]:&catalog->items[catalog->count-1U];
    if(other->method!=(s->member->method==6U?2U:6U)||other->timestamp!=s->member->timestamp||
       xx_rt_strcmp(other->name,s->member->name))goto done;
    /* A first fragment must finish its physical volume. A continuation is
     * the first indexed member; names alone never establish a match. */
    if((other->method==6U&&other->data_offset+other->size!=size)||
       s->member->size<1||other->size<1||s->member->size>LVA_JOIN_LIMIT||other->size>LVA_JOIN_LIMIT-s->member->size)goto done;
    if(++s->attempts>32U){keep_going=false;goto done;}
    start=s->member->method==6U?*s->member:*other;
    if(start.size<17||!xx_io_read_at(s->member->method==6U?s->selected->device:d,start.data_offset,fh,sizeof(fh))||
       xx_rt_memcmp(fh,"FINEAR\xdd\x88\xdd",9U)||xx_data_get_u32(fh+13,4,0,false)>LVA_JOIN_LIMIT)goto done;
    total=(size_t)(s->member->size+other->size);packed=(uint8_t*)xx_mem_alloc(total);if(!packed){keep_going=false;goto done;}
    if(s->member->method==6U) {
        if(!lva_join_copy(s->selected->device,s->member->data_offset,packed,(size_t)s->member->size,s->pd)||
           !lva_join_copy(d,other->data_offset,packed+(size_t)s->member->size,(size_t)other->size,s->pd))goto done;
    } else {
        if(!lva_join_copy(d,other->data_offset,packed,(size_t)other->size,s->pd)||
           !lva_join_copy(s->selected->device,s->member->data_offset,packed+(size_t)other->size,(size_t)s->member->size,s->pd))goto done;
    }
    memory=xx_io_mem_open_ro(packed,total);if(!memory){keep_going=false;goto done;}
    joined=*s->selected;joined.device=memory;joined.base_address=0;start.data_offset=0;start.size=(int64_t)total;start.method=0;
    if(!xx_pd_is_stopped(s->pd)&&lva_decode_whole(&joined,&start,&plain,&plain_size)) {
        if(++s->accepted!=1U){keep_going=false;goto done;}
        s->plain=plain;s->plain_size=plain_size;plain=NULL;
    }
done:
    xx_mem_free(plain);if(memory)xx_io_close(memory);xx_mem_free(packed);
    if(catalog)lva_stream_free(catalog);
    xx_io_close(d);return keep_going;
}

/* Avoid enumerating the selected directory twice for ordinary spelling
 * differences. Other aliases remain conservative: two accepted candidates
 * fail the existing unique-companion requirement. */
static size_t lva_directory_length(const char *path)
{
    size_t length=xx_str_len(path);
    while(length>1U&&(path[length-1U]=='/'||path[length-1U]=='\\')) {
#if defined(_WIN32)
        /* A drive-relative "C:" is not the root directory "C:\\". */
        if(length==3U&&path[1]==':')break;
#endif
        --length;
    }
    return length;
}
static bool lva_same_directory(const char *a,const char *b)
{
    size_t na=lva_directory_length(a),nb=lva_directory_length(b),i;
    if(na!=nb)return false;
    for(i=0;i<na;++i) {
        unsigned char x=(unsigned char)a[i],y=(unsigned char)b[i];
#if defined(_WIN32)
        if(x=='\\')x='/';
        if(y=='\\')y='/';
        if(x>='A'&&x<='Z')x=(unsigned char)(x+('a'-'A'));
        if(y>='A'&&y<='Z')y=(unsigned char)(y+('a'-'A'));
#endif
        if(x!=y)return false;
    }
    return true;
}

static bool lva_decode_fragment(Abstractformat *f,const lva_member *m,uint8_t **plain,size_t *plain_size,xx_pd_struct *pd)
{
    lva_volume_scan s;
    lva_stream *catalog=NULL;
    const char *source,*p,*value;
    char *directory=NULL,*extra=NULL;
    uint8_t header[18];
    size_t prefix=0;
    int64_t size,saved;
    bool ok=false;
    if((m->method!=2U&&m->method!=6U)||f->base_address||!(source=xx_io_source_path(f->device))||
       xx_str_len(source)>4094U||xx_pd_is_stopped(pd))return false;
    saved=xx_io_tell(f->device);size=xx_io_total_size(f->device);xx_mem_zero(&s,sizeof(s));
    if(saved<0||!xx_io_read_at(f->device,0,header,sizeof(header))||!lva_parse_container(f,size,header,&catalog))goto done;
    if((m->method==6U&&(m->descriptor_offset!=catalog->items[catalog->count-1U].descriptor_offset||m->data_offset+m->size!=size))||
       (m->method==2U&&m->descriptor_offset!=catalog->items[0].descriptor_offset))goto done;
    for(p=source;*p;p++)if(*p=='/'||*p=='\\')prefix=(size_t)(p-source)+1U;
    directory=prefix?xx_str_create_len(prefix):xx_str_dup(".");if(!directory)goto done;
    if(prefix){xx_mem_copy(directory,source,prefix);directory[prefix]=0;}
    value=xx_rt_getenv("XXFC_LINGVOARC_COMPANION_DIR");
    if(value&&*value) {
        if(xx_str_len(value)>4094U||!(extra=xx_str_dup(value)))goto done;
    }
    s.selected=f;s.member=m;s.directory=directory;s.selected_name=source+prefix;s.version=catalog->version;s.pd=pd;
    if(!xx_fs_platform_enumerate(directory,lva_volume_candidate,&s))goto done;
    if(extra&&!lva_same_directory(directory,extra)) {
        /* One context keeps all work limits and ambiguity checks aggregate.
         * A same basename in another directory is not the selected device. */
        s.directory=extra;s.selected_name=NULL;
        if(!xx_fs_platform_enumerate(extra,lva_volume_candidate,&s))goto done;
    }
    if(s.accepted==1U&&!xx_pd_is_stopped(pd))ok=true;
done:
    if(saved>=0&&xx_io_seek64(f->device,saved,SEEK_SET))ok=false;
    if(ok){*plain=s.plain;*plain_size=s.plain_size;s.plain=NULL;}
    xx_mem_free(s.plain);xx_str_free(directory);xx_str_free(extra);if(catalog)lva_stream_free(catalog);
    return ok;
}
#endif
