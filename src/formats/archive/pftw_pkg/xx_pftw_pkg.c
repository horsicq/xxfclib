/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Standalone encoded PackageForTheWeb CAB, see docs/PFTW_PKG.md. */
#include "xxfclib/formats/archive/pftw_pkg/xx_pftw_pkg.h"
#include "xxfclib/formats/archive/cab/xx_cab.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "../../xx_archive_output_name.h"
#include <stdio.h>
#define PFTW_CAP (256U*1024U*1024U)
typedef struct pftw_view {
    xx_cab cab;
    xx_io_device view,*source;
    int64_t base,size,pos;
    uint8_t key[32];size_t key_length;
    uint64_t required;
    xx_pd_struct *active_pd;
} pftw_view;
static uint16_t pw16(const uint8_t *p) { return (uint16_t)(p[0]|((unsigned)p[1]<<8)); }
static uint32_t pw32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint8_t swap4(uint8_t b) { return (uint8_t)((b>>4)|(b<<4)); }
static bool stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static uint64_t pw_limit(Abstractformat *f,const xx_list_s *opts,uint32_t id,uint64_t fallback)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,opts,id);
    uint64_t n=v?xx_var_get_u64(v):fallback;
    return id==XX_META_ID_OPT_MEMORY_LIMIT && n>PFTW_CAP?PFTW_CAP:n;
}
static ssize_t pw_read(xx_io_device *d,void *buffer,size_t n)
{
    pftw_view *p=(pftw_view *)d->priv;size_t i;
    if ((!buffer && n) || !p->key_length || stopped(p->active_pd)) return -1;
    if ((uint64_t)n>(uint64_t)(p->size-p->pos)) n=(size_t)(p->size-p->pos);
    if (!n) return 0;
    if (!xx_io_read_at(p->source,p->base+p->pos,buffer,n)) return -1;
    for(i=0;i<n;++i) ((uint8_t *)buffer)[i]=swap4(((uint8_t *)buffer)[i]^p->key[((uint64_t)p->pos+i)%p->key_length]);
    p->pos+=(int64_t)n;return (ssize_t)n;
}
static int pw_seek64(xx_io_device *d,int64_t off,int whence)
{
    pftw_view *p=(pftw_view *)d->priv;int64_t base;
    if(whence==SEEK_SET) base=0;else if(whence==SEEK_CUR) base=p->pos;else if(whence==SEEK_END) base=p->size;else return -1;
    if(off < -base || off>p->size-base) return -1;
    p->pos=base+off;return 0;
}
static int pw_seek(xx_io_device *d,long off,int whence) { return pw_seek64(d,off,whence); }
static int64_t pw_tell(xx_io_device *d) { return ((pftw_view *)d->priv)->pos; }
static int64_t pw_size(xx_io_device *d) { return ((pftw_view *)d->priv)->size; }
/* Recover a bounded periodic key from the invariant 16-byte CAB prefix.
 * A header match is only a candidate: directory, extents and CFDATA checksums
 * must subsequently validate. No password value is inferred from key bytes. */
static bool pw_key(pftw_view *p,uint8_t *h)
{
    uint8_t raw[36],known[32]={'M','S','C','F',0},stream[32];size_t i,n;bool short_period=false;
    if(p->size<70 || p->size>UINT32_MAX || !xx_io_read_at(p->source,p->base,raw,sizeof(raw))) return false;
    for(i=0;i<4;++i) known[8+i]=(uint8_t)((uint64_t)p->size>>(8U*i));
    for(i=0;i<16;++i) stream[i]=raw[i]^swap4(known[i]);
    for(n=1;n<=16;++n) {
        for(i=n;i<16 && stream[i]==stream[i%n];++i) {}
        if(i<16) continue;
        if(n<16) short_period=true;
        for(i=0;i<36;++i) h[i]=swap4(raw[i]^stream[i%n]);
        if(pw32(h+20) || h[24]!=3 || h[25]!=1 || pw16(h+26)!=1 || !pw16(h+28) || pw16(h+30) || pw16(h+34) || pw32(h+16)<44U || pw32(h+16)>=(uint64_t)p->size) continue;
        xx_rt_memcpy(p->key,stream,n);p->key_length=n;return true;
    }
    /* A repeated short prefix already establishes its key grammar. Do not
     * reinterpret a damaged short-key header using a guessed longer key. */
    if(short_period) { p->key_length=0;return false; }
    /* Canonical single-member CABs expose another sixteen known bytes:
     * file table at 44, reserved word, version, one folder and one file.
     * The variable set ID is never guessed. This extends recovery through
     * period 32; full block checksums still authenticate the candidate. */
    known[16]=44;known[24]=3;known[25]=1;known[26]=1;known[28]=1;
    for(i=16;i<32;++i) stream[i]=raw[i]^swap4(known[i]);
    for(n=17;n<=32;++n) {
        for(i=n;i<32 && stream[i]==stream[i%n];++i) {}
        if(i<32) continue;
        for(i=0;i<36;++i) h[i]=swap4(raw[i]^stream[i%n]);
        if(pw16(h+34)) continue;
        xx_rt_memcpy(p->key,stream,n);p->key_length=n;return true;
    }
    p->key_length=0;return false;
}
/* CAB checksum XORs LE32 words; a final 1..3-byte fragment is big endian. */
static uint32_t pw_sum(const uint8_t *b,size_t n)
{
    size_t i=0;uint32_t sum=0,tail=0;
    for(;i+4<=n;i+=4) sum^=pw32(b+i);
    for(;i<n;++i) tail=(tail<<8)|b[i];
    return sum^tail;
}
static bool pw_preflight(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd)
{
    pftw_view *p=(pftw_view *)f;uint8_t h[36],folder[8],b[16384];char name[32768];
    uint32_t filesat,data;uint16_t count,blocks,method,i;int64_t at;
    uint64_t plain=0,budget=pw_limit(f,opts,XX_META_ID_OPT_MEMORY_LIMIT,PFTW_CAP),maximum=pw_limit(f,opts,XX_META_ID_OPT_MAX_MEMBER_SIZE,UINT64_MAX);
    if(stopped(pd) || budget<sizeof(*p)+sizeof(b) || !pw_key(p,h)) return false;
    p->required=8U*(uint64_t)p->size+256U*pw16(h+28)+1048576U;
    if(p->required>budget || !xx_io_read_at(&p->view,36,folder,8U)) return false;
    filesat=pw32(h+16);count=pw16(h+28);data=pw32(folder);blocks=pw16(folder+4);method=pw16(folder+6);
    if(!blocks || method>1U || data<filesat || data>=(uint64_t)p->size || (uint64_t)count*17U>data-filesat) return false;
    at=data;
    for(i=0;i<blocks;++i) {
        uint16_t packed,unpacked;uint32_t expected,sum;size_t left;
        if(stopped(pd) || !xx_io_read_at(&p->view,at,h,8U)) return false;
        packed=pw16(h+4);unpacked=pw16(h+6);expected=pw32(h);sum=pw32(h+4);at+=8;
        if(!packed || !unpacked || unpacked>32768U || packed>(uint64_t)(p->size-at) || (method==0 && packed!=unpacked)) return false;
        if(method==1 && (!xx_io_read_at(&p->view,at,b,2U) || b[0]!='C' || b[1]!='K')) return false;
        plain+=unpacked;
        if(plain>(budget-p->required)/3U) return false;
        for(left=packed;left;) {
            size_t n=left>sizeof(b)?sizeof(b):left;
            if(stopped(pd) || !xx_io_read_at(&p->view,at,b,n)) return false;
            sum^=pw_sum(b,n);at+=(int64_t)n;left-=n;
        }
        if(expected && sum!=expected) return false;
    }
    if(at!=p->size) return false;
    p->required+=3U*plain;at=filesat;
    for(i=0;i<count;++i) {
        size_t length=0;char *safe;
        if(stopped(pd) || at>data || data-at<17 || !xx_io_read_at(&p->view,at,h,16U)) return false;
        if(pw16(h+8) || pw32(h)>maximum || pw32(h+4)>plain || pw32(h)>plain-pw32(h+4)) return false;
        at+=16;
        do {
            if(stopped(pd) || at>=data || length>=sizeof(name) || !xx_io_read_at(&p->view,at++,b,1U)) return false;
            name[length]=(char)b[0];if(b[0]) ++length;
        } while(b[0]);
        if(!length) return false;
        safe=xx_archive_output_name(name,0,XX_ARCHIVE_NAME_ROOTED);
        if(!safe) return false;
        xx_mem_free(safe);
    }
    return at<=data && !stopped(pd);
}
static bool pw_valid(Abstractformat *f,xx_pd_struct *pd)
{ return pw_preflight(f,NULL,pd) && xx_cab_check_is_valid(f,pd); }
static bool pw_info(Abstractformat *f,xx_pd_struct *pd)
{
    bool ok=pw_preflight(f,NULL,pd) && xx_cab_handle_base_info(f,pd);
    f->file_type=XX_FILE_TYPE_PFTW_PKG;return ok;
}
static int64_t pw_format_size(Abstractformat *f,xx_pd_struct *pd) { return pw_info(f,pd)?f->format_size:-1; }
static uint64_t pw_count(Abstractformat *f,xx_pd_struct *pd) { return pw_info(f,pd)?f->number_of_archive_records:0; }
static xx_file_type_t pw_type(Abstractformat *f) { (void)f;return XX_FILE_TYPE_PFTW_PKG; }
static xx_archive_record_state *pw_records(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd)
{ return pw_preflight(f,opts,pd)?xx_cab_create_archive_records_reading(f,opts,pd):NULL; }
static const xx_archive_record *pw_current(Abstractformat *f,xx_archive_record_state *s)
{
    const xx_archive_record *r=xx_cab_get_current_archive_record(f,s);
    if(r && !xx_archive_record_get_meta_bool(r,XX_META_ID_IS_ENCRYPTED,false)) {
        char *name=xx_archive_output_name(xx_archive_record_get_original_name(r),0,XX_ARCHIVE_NAME_ROOTED);
        bool ok=name && xx_archive_record_set_original_name(&s->current_record,name) &&
            xx_archive_record_set_meta_bool(&s->current_record,XX_META_ID_IS_ENCRYPTED,true);
        xx_mem_free(name);if(!ok) return NULL;
    }
    return r;
}
static bool pw_unpack(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd)
{
    pftw_view *p=(pftw_view *)f;const xx_archive_record *r=pw_current(f,s);bool ok;
    if(!r || stopped(pd) || p->required>pw_limit(f,&s->options,XX_META_ID_OPT_MEMORY_LIMIT,PFTW_CAP) ||
        xx_archive_record_get_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,UINT64_MAX)>pw_limit(f,&s->options,XX_META_ID_OPT_MAX_MEMBER_SIZE,UINT64_MAX)) return false;
    p->active_pd=pd;ok=xx_cab_unpack_current_archive_record(f,s,pd);p->active_pd=NULL;return ok && !stopped(pd);
}
static bool pw_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd)
{ return !stopped(pd) && xx_cab_archive_record_move_to_next(f,s,pd); }
static void pw_destroy(Abstractformat *f)
{
    pftw_view *p=(pftw_view *)f;xx_cab_destroy(&p->cab);xx_mem_zero(p->key,sizeof(p->key));
}
Abstractformat *xx_pftw_pkg_create(xx_io_device *d,int64_t base)
{
    pftw_view *p;Abstractformat *f;
    if(!d || base<0 || xx_io_size(d)<base) return NULL;
    p=(pftw_view *)xx_mem_calloc(1,sizeof(*p));if(!p) return NULL;
    p->source=d;p->base=base;p->size=xx_io_size(d)-base;p->view.priv=p;
    p->view.read=pw_read;p->view.seek=pw_seek;p->view.seek64=pw_seek64;p->view.tell=pw_tell;p->view.total_size=pw_size;
    xx_cab_init(&p->cab,&p->view,0);f=&p->cab.format;
    f->file_type=XX_FILE_TYPE_PFTW_PKG;xx_format_set_extension(f,"pkg");f->is_crypted=true;
    f->destroy=pw_destroy;f->get_file_type=pw_type;f->check_is_valid=pw_valid;f->handle_base_info=pw_info;
    f->get_format_size=pw_format_size;f->get_number_of_archive_records=pw_count;f->create_archive_records_reading=pw_records;
    f->get_current_archive_record=pw_current;f->unpack_current_archive_record=pw_unpack;f->archive_record_move_to_next=pw_next;
    return f;
}
void xx_pftw_pkg_free(Abstractformat *f) { if(f) { pw_destroy(f);xx_mem_free(f); } }
#include "../../xx_format_abstract_extractor_adapter.h"
static Abstractformat *pw_open(xx_io_device *d) { return xx_pftw_pkg_create(d,0); }
static const xx_file_type_t pw_types[]={XX_FILE_TYPE_PFTW_PKG};
const xx_format_search_desc xx_pftw_pkg_search_desc={pw_types,1U,NULL,0U,pw_open,xx_pftw_pkg_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(pftw_pkg,xx_pftw_pkg_search_desc)
