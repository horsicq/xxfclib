/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * On-disk layout: John Elliott, LDBS disc image format v0.6.
 * LibDsk is an independent fixture producer; no LibDsk code is imported.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ldbs/xx_ldbs.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LDBS_SOURCE_MAX (32U*1024U*1024U)
#define LDBS_IMAGE_MAX (16U*1024U*1024U)
#define LDBS_BLOCK_MAX 32768U
#define LDBS_TRACK_MAX 512U
#define LDBS_SECTOR_MAX 64U
#define LDBS_SECTOR_BYTES_MAX 8192U
#define LDBS_WORK_EXTRA 16384U

typedef struct ldbs_block_s {
    uint32_t at,alloc,used,next;
    uint8_t type[4];
    uint8_t mark;
} ldbs_block;
typedef struct ldbs_sector_s {
    uint32_t at;
    uint16_t bytes;
    uint8_t id,fill,blank;
} ldbs_sector;
typedef struct ldbs_track_s {
    uint16_t cyl;
    uint8_t head,count;
    ldbs_sector sector[LDBS_SECTOR_MAX];
} ldbs_track;
typedef struct ldbs_view_s {
    ldbs_block *block;
    ldbs_track *track;
    uint32_t blocks,tracks,source_size,raw_size,work;
    uint16_t cylinders,sector_bytes;
    uint8_t heads,sectors,sector_base;
} ldbs_view;
typedef struct ldbs_cursor_s { size_t index; } ldbs_cursor;

static uint16_t ldbs_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));
}
static uint32_t ldbs_u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static bool ldbs_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool ldbs_read(Abstractformat *f,uint32_t at,void *out,size_t n,
                      xx_pd_struct *pd) {
    uint8_t *p=(uint8_t *)out;
    size_t done=0U;
    if (ldbs_stop(pd) || xx_io_seek64(f->device,f->base_address+at,
                                      SEEK_SET)!=0) return false;
    while (done<n) {
        ssize_t got;
        if (ldbs_stop(pd)) return false;
        got=xx_io_read(f->device,p+done,n-done);
        if (got<=0 || (size_t)got>n-done || ldbs_stop(pd)) return false;
        done+=(size_t)got;
    }
    return !ldbs_stop(pd);
}
static bool ldbs_write(xx_io_device *d,const uint8_t *p,size_t n,
                       xx_pd_struct *pd) {
    size_t done=0U;
    if (!d || ldbs_stop(pd)) return false;
    while (done<n) {
        ssize_t put;
        if (ldbs_stop(pd)) return false;
        put=xx_io_write(d,p+done,n-done);
        if (put<=0 || (size_t)put>n-done || ldbs_stop(pd)) return false;
        done+=(size_t)put;
    }
    return !ldbs_stop(pd);
}
static void ldbs_release(ldbs_view *v) {
    if (!v) return;
    if (v->block) xx_mem_free(v->block);
    if (v->track) xx_mem_free(v->track);
    xx_mem_free(v);
}
static ldbs_block *ldbs_find(const ldbs_view *v,uint32_t at) {
    uint32_t lo=0U,hi=v->blocks;
    while (lo<hi) {
        uint32_t mid=lo+(hi-lo)/2U;
        if (v->block[mid].at<at) lo=mid+1U;
        else hi=mid;
    }
    return lo<v->blocks && v->block[lo].at==at ? &v->block[lo] : NULL;
}
static int ldbs_track_cmp(const void *a,const void *b) {
    const ldbs_track *x=(const ldbs_track *)a,*y=(const ldbs_track *)b;
    uint32_t kx=(uint32_t)x->cyl*2U+x->head;
    uint32_t ky=(uint32_t)y->cyl*2U+y->head;
    return kx<ky ? -1 : kx>ky ? 1 : 0;
}
static int ldbs_sector_cmp(const void *a,const void *b) {
    const ldbs_sector *x=(const ldbs_sector *)a,*y=(const ldbs_sector *)b;
    return x->id<y->id ? -1 : x->id>y->id ? 1 : 0;
}
static uint64_t ldbs_memory_cap(Abstractformat *f,const xx_list_s *opts) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,opts,
                                                      XX_META_ID_OPT_MEMORY_LIMIT);
    return v ? xx_var_get_u64(v) : UINT64_MAX;
}
static bool ldbs_member_cap(Abstractformat *f,const xx_list_s *opts,
                            uint32_t bytes) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,opts,
                                                XX_META_ID_OPT_MAX_MEMBER_SIZE);
    return !v || (uint64_t)bytes<=xx_var_get_u64(v);
}
static ldbs_view *ldbs_parse_body(Abstractformat *f,const xx_list_s *opts,
                                  xx_pd_struct *pd) {
    uint8_t h[20],e[8],t[18],geom[15];
    int64_t total;
    uint32_t size,at,nblock=0U,i,dirat,track_count=0U,geom_at=0U;
    uint32_t used_head,free_head;
    const ldbs_block *dir,*gb;
    ldbs_view *v=NULL;
    uint64_t cap=ldbs_memory_cap(f,opts),work;
    uint16_t entries,maxc=0U,secbytes=0U;
    uint8_t maxh=0U,spt=0U,sbase=0U;
    bool ok=false;
    if (!f || !f->device || f->base_address<0 || ldbs_stop(pd)) return NULL;
    total=xx_io_total_size(f->device);
    if (total<f->base_address || total-f->base_address<42 ||
        total-f->base_address>LDBS_SOURCE_MAX) return NULL;
    size=(uint32_t)(total-f->base_address);
    if (!ldbs_read(f,0U,h,20U,pd) || memcmp(h,"LBS\1DSK\2",8U))
        return NULL;
    dirat=ldbs_u32(h+16U);
    if (!dirat || dirat>=size || (ldbs_u32(h+12U) &&
        ldbs_u32(h+12U)>=size) || (ldbs_u32(h+8U) &&
        ldbs_u32(h+8U)>=size)) return NULL;
    for (at=20U;at<size;) {
        uint32_t alloc,used;
        if (size-at<20U || !ldbs_read(f,at,h,20U,pd) ||
            memcmp(h,"LDB\1",4U)) return NULL;
        alloc=ldbs_u32(h+8U);used=ldbs_u32(h+12U);
        if (used>alloc || alloc>size-at-20U ||
            (++nblock)>LDBS_BLOCK_MAX) return NULL;
        at+=20U+alloc;
    }
    work=(uint64_t)sizeof(*v)+(uint64_t)nblock*sizeof(ldbs_block)+
         LDBS_WORK_EXTRA;
    if (work>cap || work>UINT32_MAX) return NULL;
    v=(ldbs_view *)xx_mem_calloc(1U,sizeof(*v));
    if (!v) return NULL;
    v->block=(ldbs_block *)xx_mem_calloc(nblock,sizeof(ldbs_block));
    if (!v->block) goto done;
    v->blocks=nblock;v->source_size=size;
    for (at=20U,i=0U;i<nblock;++i) {
        ldbs_block *b=&v->block[i];
        if (!ldbs_read(f,at,h,20U,pd)) goto done;
        b->at=at;b->alloc=ldbs_u32(h+8U);
        b->used=ldbs_u32(h+12U);b->next=ldbs_u32(h+16U);
        memcpy(b->type,h+4U,4U);
        at+=20U+b->alloc;
    }
    for (i=0U;i<nblock;++i) {
        const ldbs_block *b=&v->block[i];
        if ((b->next && !ldbs_find(v,b->next)) ||
            (!b->type[0] && (b->type[1] || b->type[2] || b->type[3] ||
                             b->used))) goto done;
    }
    if (!ldbs_read(f,0U,h,20U,pd) ||
        (ldbs_u32(h+8U) && !ldbs_find(v,ldbs_u32(h+8U))) ||
        (ldbs_u32(h+12U) && !ldbs_find(v,ldbs_u32(h+12U)))) goto done;
    used_head=ldbs_u32(h+8U);free_head=ldbs_u32(h+12U);
    while (used_head) {
        ldbs_block *b=ldbs_find(v,used_head);
        if (!b || b->mark ||
            (!b->type[0] && !b->type[1] && !b->type[2] &&
             !b->type[3])) goto done;
        b->mark=1U;used_head=b->next;
    }
    while (free_head) {
        ldbs_block *b=ldbs_find(v,free_head);
        if (!b || b->mark || b->type[0] || b->type[1] ||
            b->type[2] || b->type[3] || b->used) goto done;
        b->mark=2U;free_head=b->next;
    }
    for (i=0U;i<nblock;++i) if (!v->block[i].mark) goto done;
    dir=ldbs_find(v,dirat);
    if (!dir || memcmp(dir->type,"DIR\1",4U) || dir->used<2U ||
        !ldbs_read(f,dirat+20U,e,2U,pd)) goto done;
    entries=ldbs_u16(e);
    if ((uint32_t)entries>=(LDBS_TRACK_MAX+32U) ||
        dir->used!=2U+8U*(uint32_t)entries) goto done;
    for (i=0U;i<entries;++i) {
        if (!ldbs_read(f,dirat+22U+8U*i,e,8U,pd)) goto done;
        if (e[0]=='T') {
            if (ldbs_u16(e+1U)>255U || e[3]>1U ||
                !ldbs_find(v,ldbs_u32(e+4U)) ||
                ++track_count>LDBS_TRACK_MAX) goto done;
        } else if (!memcmp(e,"GEOM",4U)) {
            if (geom_at || !ldbs_find(v,ldbs_u32(e+4U))) goto done;
            geom_at=ldbs_u32(e+4U);
        }
    }
    if (!track_count) goto done;
    work+=(uint64_t)track_count*sizeof(ldbs_track);
    if (work>cap || work>UINT32_MAX) goto done;
    v->work=(uint32_t)work;
    v->track=(ldbs_track *)xx_mem_calloc(track_count,sizeof(ldbs_track));
    if (!v->track) goto done;
    v->tracks=track_count;
    track_count=0U;
    for (i=0U;i<entries;++i) {
        const ldbs_block *tb;
        ldbs_track *tr;
        uint16_t fixed,dlen,count;
        uint32_t j;
        if (!ldbs_read(f,dirat+22U+8U*i,e,8U,pd)) goto done;
        if (e[0]!='T') continue;
        tb=ldbs_find(v,ldbs_u32(e+4U));
        if (!tb || memcmp(tb->type,e,4U) || tb->used<12U ||
            !ldbs_read(f,tb->at+20U,t,12U,pd)) goto done;
        fixed=ldbs_u16(t);dlen=ldbs_u16(t+2U);count=ldbs_u16(t+4U);
        if (fixed<12U || dlen<16U || count==0U ||
            count>LDBS_SECTOR_MAX ||
            (uint64_t)fixed+(uint64_t)dlen*count>tb->used ||
            (t[7]!=0U && t[7]!=1U && t[7]!=2U)) goto done;
        tr=&v->track[track_count++];
        tr->cyl=ldbs_u16(e+1U);tr->head=e[3];
        tr->count=(uint8_t)count;
        if (tr->cyl>maxc) maxc=tr->cyl;
        if (tr->head>maxh) maxh=tr->head;
        if (!spt) spt=(uint8_t)count;
        if (spt!=count) goto done;
        for (j=0U;j<count;++j) {
            ldbs_sector *s=&tr->sector[j];
            const ldbs_block *sb;
            uint32_t id,bytes,blockid,trail;
            size_t take=dlen>=18U ? 18U : 16U;
            if (!ldbs_read(f,tb->at+20U+fixed+(uint32_t)dlen*j,
                           t,take,pd)) goto done;
            id=t[2U];bytes=dlen>=18U ? ldbs_u16(t+16U) : 0U;
            if (!bytes && t[3U]<=7U) bytes=128U<<t[3U];
            blockid=ldbs_u32(t+8U);trail=ldbs_u16(t+12U);
            if (t[0U]!=tr->cyl || t[1U]!=tr->head || t[3U]>7U ||
                t[4U] || t[5U] || t[6U]>1U ||
                bytes==0U || bytes>LDBS_SECTOR_BYTES_MAX ||
                (secbytes && bytes!=secbytes)) goto done;
            secbytes=(uint16_t)bytes;
            s->id=(uint8_t)id;s->bytes=(uint16_t)bytes;
            if (t[6U]==0U) {
                if (blockid || trail) goto done;
                s->blank=1U;s->fill=t[7U];
            } else {
                sb=ldbs_find(v,blockid);
                if (!sb || sb->type[0]!='S' ||
                    sb->type[1]!=(uint8_t)tr->cyl ||
                    sb->type[2]!=tr->head || sb->type[3]!=id ||
                    (uint64_t)bytes+trail!=sb->used) goto done;
                s->at=sb->at+20U;
            }
        }
        qsort(tr->sector,count,sizeof(ldbs_sector),ldbs_sector_cmp);
        if (track_count==1U) sbase=tr->sector[0].id;
        for (j=0U;j<count;++j) {
            if ((uint32_t)tr->sector[j].id!=(uint32_t)sbase+j)
                goto done;
        }
    }
    if ((uint32_t)(maxc+1U)*(uint32_t)(maxh+1U)!=v->tracks)
        goto done;
    qsort(v->track,v->tracks,sizeof(ldbs_track),ldbs_track_cmp);
    for (i=0U;i<v->tracks;++i) {
        if (v->track[i].cyl!=i/(maxh+1U) ||
            v->track[i].head!=i%(maxh+1U)) goto done;
    }
    if (geom_at) {
        gb=ldbs_find(v,geom_at);
        if (!gb || memcmp(gb->type,"GEOM",4U) || gb->used<15U ||
            !ldbs_read(f,gb->at+20U,geom,15U,pd) || geom[0U]!=0U ||
            ldbs_u16(geom+1U)!=(uint16_t)(maxc+1U) ||
            geom[3U]!=(uint8_t)(maxh+1U) || geom[4U]!=spt ||
            geom[5U]!=sbase || ldbs_u16(geom+6U)!=secbytes ||
            geom[12U]) goto done;
    }
    if ((uint64_t)v->tracks*spt*secbytes>LDBS_IMAGE_MAX) goto done;
    v->cylinders=(uint16_t)(maxc+1U);v->heads=(uint8_t)(maxh+1U);
    v->sectors=spt;v->sector_base=sbase;v->sector_bytes=secbytes;
    v->raw_size=v->tracks*(uint32_t)spt*secbytes;
    if (!ldbs_member_cap(f,opts,v->raw_size)) goto done;
    ok=true;
done:
    if (!ok) { ldbs_release(v);v=NULL; }
    return v;
}
static ldbs_view *ldbs_parse(Abstractformat *f,const xx_list_s *opts,
                             xx_pd_struct *pd) {
    ldbs_view *v;
    int64_t saved;
    if (!f || !f->device) return NULL;
    saved=xx_io_tell(f->device);
    if (saved<0) return NULL;
    v=ldbs_parse_body(f,opts,pd);
    if (xx_io_seek64(f->device,saved,SEEK_SET)!=0) {
        ldbs_release(v);v=NULL;
    }
    return v;
}
static bool ldbs_emit(Abstractformat *f,const ldbs_view *v,
                      xx_io_device *out,xx_pd_struct *pd) {
    uint8_t sector[LDBS_SECTOR_BYTES_MAX];
    uint32_t i,j;
    for (i=0U;i<v->tracks;++i) for (j=0U;j<v->sectors;++j) {
        const ldbs_sector *s=&v->track[i].sector[j];
        if (ldbs_stop(pd)) return false;
        if (s->blank) memset(sector,s->fill,v->sector_bytes);
        else if (!ldbs_read(f,s->at,sector,v->sector_bytes,pd))
            return false;
        if (!ldbs_write(out,sector,v->sector_bytes,pd)) return false;
    }
    return !ldbs_stop(pd);
}
static bool ldbs_unpack_device(Abstractformat *f,const xx_list_s *opts,
                               xx_io_device *out,xx_pd_struct *pd) {
    ldbs_view *v;
    int64_t saved;
    bool ok;
    if (!f || !out || out==f->device || ldbs_stop(pd)) return false;
    v=ldbs_parse(f,opts,pd);
    if (!v) return false;
    saved=xx_io_tell(f->device);
    if (saved<0) {ldbs_release(v);return false;}
    ok=ldbs_emit(f,v,out,pd);
    if (xx_io_seek64(f->device,saved,SEEK_SET)!=0) ok=false;
    ldbs_release(v);
    return ok && !ldbs_stop(pd);
}
static bool ldbs_check(Abstractformat *f,xx_pd_struct *pd) {
    ldbs_view *v=ldbs_parse(f,NULL,pd);
    if (!v) return false;
    ldbs_release(v);return true;
}
static bool ldbs_handle(Abstractformat *f,xx_pd_struct *pd) {
    ldbs_view *v=ldbs_parse(f,NULL,pd);
    if (!v) {
        if (f) {f->is_valid=false;f->base_info_handled=false;}
        return false;
    }
    f->format_size=v->source_size;
    f->overlay_offset=-1;f->overlay_size=0;
    f->number_of_archive_records=1U;
    f->is_valid=true;f->base_info_handled=true;
    ldbs_release(v);return true;
}
static int64_t ldbs_size(Abstractformat *f,xx_pd_struct *pd) {
    return f && (f->base_info_handled || ldbs_handle(f,pd)) ?
           f->format_size : -1;
}
static uint64_t ldbs_count(Abstractformat *f,xx_pd_struct *pd) {
    return f && (f->base_info_handled || ldbs_handle(f,pd)) ?
           f->number_of_archive_records : 0U;
}
static bool ldbs_record(xx_archive_record_state *s,uint32_t raw) {
    xx_archive_record *r=&s->current_record;
    xx_archive_record_cleanup(r);xx_archive_record_init(r);
    r->header_offset=s->format->base_address;
    r->data_offset=s->format->base_address;
    r->compressed_size=s->format->format_size;
    return xx_archive_record_set_original_name(r,"disk.img") &&
           xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)s->format->format_size) &&
           xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,
                                          raw) &&
           xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSION_METHOD,1U) &&
           xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,false) &&
           xx_archive_record_set_meta_bool(r,XX_META_ID_IS_ENCRYPTED,false);
}
static void ldbs_free_cursor(void *p) {if (p) xx_mem_free(p);}
static xx_archive_record_state *ldbs_create(Abstractformat *f,
                                             const xx_list_s *opts,
                                             xx_pd_struct *pd) {
    xx_archive_record_state *s;
    ldbs_view *v;
    ldbs_cursor *c;
    size_t i;
    if (!f || ldbs_stop(pd) ||
        (!f->base_info_handled && !ldbs_handle(f,pd))) return NULL;
    v=ldbs_parse(f,opts,pd);
    if (!v) return NULL;
    s=(xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    c=(ldbs_cursor *)xx_mem_alloc(sizeof(*c));
    if (!s || !c) {
        if (s) xx_mem_free(s);
        if (c) xx_mem_free(c);
        ldbs_release(v);return NULL;
    }
    c->index=0U;
    xx_archive_record_state_init(s,f);
    s->internal_state=c;s->free_internal=ldbs_free_cursor;
    s->total_records=1U;
    for (i=0U;opts && i<opts->count;++i) {
        const xx_meta *m=(const xx_meta *)xx_list_at(opts,i);
        xx_meta copy;
        if (!m) continue;
        xx_meta_init(&copy,m->meta_id);
        if (!xx_var_copy(&copy.var,&m->var) ||
            !xx_list_append(&s->options,&copy)) {
            xx_meta_cleanup(&copy);xx_archive_record_state_free(s);
            ldbs_release(v);return NULL;
        }
    }
    s->has_record=ldbs_record(s,v->raw_size);
    ldbs_release(v);
    if (!s->has_record) {xx_archive_record_state_free(s);return NULL;}
    return s;
}
static const xx_archive_record *ldbs_current(Abstractformat *f,
                                               xx_archive_record_state *s) {
    return f && s && s->format==f && s->has_record ?
           &s->current_record : NULL;
}
static bool ldbs_next(Abstractformat *f,xx_archive_record_state *s,
                      xx_pd_struct *pd) {
    if (!f || !s || s->format!=f || !s->has_record || ldbs_stop(pd))
        return false;
    s->has_record=false;return false;
}
static ssize_t ldbs_discard(xx_io_device *d,const void *p,size_t n) {
    (void)d;(void)p;return (ssize_t)n;
}
static bool ldbs_same_path(const char *a,const char *b) {
    while (*a && *b) {
        char x=*a++,y=*b++;
        if (x=='\\') x='/';
        if (y=='\\') y='/';
        if (x>='A'&&x<='Z') x=(char)(x+32);
        if (y>='A'&&y<='Z') y=(char)(y+32);
        if (x!=y) return false;
    }
    return *a==*b;
}
static xx_io_device *ldbs_stage(const char *dest,char **stage) {
    char *parent=xx_str_dup(dest);
    size_t i,cut=0U;
    unsigned attempt;
    *stage=NULL;
    if (!parent) return NULL;
    for (i=0U;parent[i];++i)
        if (parent[i]=='/'||parent[i]=='\\') cut=i+1U;
    parent[cut]=0;
    for (attempt=0U;attempt<128U;++attempt) {
        char suffix[40],*candidate;
        xx_io_device *d;
        (void)xx_rt_snprintf(suffix,sizeof(suffix),".xx_ldbs.tmp.%u",attempt);
        candidate=xx_str_concat(parent,suffix);
        if (!candidate) break;
        if (ldbs_same_path(candidate,dest)) {
            xx_str_free(candidate);continue;
        }
        d=xx_io_file_open(candidate,"wbx");
        if (d) {*stage=candidate;xx_str_free(parent);return d;}
        xx_str_free(candidate);
    }
    xx_str_free(parent);return NULL;
}
static bool ldbs_unpack(Abstractformat *f,xx_archive_record_state *s,
                        xx_pd_struct *pd) {
    const xx_var *v,*ov;
    const char *base=NULL;
    char *owned=NULL,*dest=NULL,*stage=NULL;
    xx_io_device *out=NULL,discard;
    bool ok=false,overwrite=false;
    if (!f || !s || s->format!=f || !s->has_record || ldbs_stop(pd))
        return false;
    v=xx_format_resolve_extra_parameter(f,&s->options,
                                        XX_META_ID_OPT_UNPACK_PATH);
    if (!v) {
        xx_rt_memset(&discard,0,sizeof(discard));
        discard.write=ldbs_discard;
        return ldbs_unpack_device(f,&s->options,&discard,pd);
    }
    if (v->type==XX_VAR_TYPE_STRING || v->type==XX_VAR_TYPE_STRING_VIEW)
        base=xx_var_get_str(v);
    else if (v->type==XX_VAR_TYPE_WSTRING ||
             v->type==XX_VAR_TYPE_WSTRING_VIEW)
        base=owned=xx_str_unicode_to_utf8(xx_var_get_wstr(v));
    if (!base) goto done;
    dest=base[0] && base[xx_str_len(base)-1U]!='/' &&
         base[xx_str_len(base)-1U]!='\\' ?
         xx_str_concat3(base,"/","disk.img") :
         xx_str_concat(base,"disk.img");
    if (!dest) goto done;
    ov=xx_format_resolve_extra_parameter(f,&s->options,
                                         XX_META_ID_OPT_OVERWRITE);
    overwrite=ov && xx_var_get_bool(ov);
    if ((!overwrite && xx_io_file_exists_a(dest)) ||
        !xx_store_create_dirs_a(dest,false) || ldbs_stop(pd)) goto done;
    out=ldbs_stage(dest,&stage);
    if (!out) goto done;
    ok=ldbs_unpack_device(f,&s->options,out,pd);
    if (xx_io_close(out)!=0) ok=false;
    out=NULL;
    if (ok && !ldbs_stop(pd))
        ok=xx_io_file_replace_a(stage,dest,overwrite);
    else ok=false;
done:
    if (out) {(void)xx_io_close(out);ok=false;}
    if (stage) {
        if (!ok) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    if (dest) xx_str_free(dest);
    if (owned) xx_str_free(owned);
    return ok;
}
static void ldbs_free_records(Abstractformat *f,xx_archive_record_state *s) {
    (void)f;xx_archive_record_state_free(s);
}
static void ldbs_destroy_vtable(Abstractformat *f) {
    if (f) xx_format_cleanup_extra_parameters(f);
}
void xx_ldbs_init(xx_ldbs *r,xx_io_device *d,int64_t b) {
    if (!r) return;
    xx_rt_memset(r,0,sizeof(*r));xx_format_init(&r->format,d,b);
    r->format.file_type=XX_FILE_TYPE_LDBS;
    r->format.format_type=XX_TYPE_ARCHIVE;r->format.is_archive=true;
    xx_format_set_extension(&r->format,"ldbs");
    r->format.check_is_valid=ldbs_check;
    r->format.handle_base_info=ldbs_handle;
    r->format.get_format_size=ldbs_size;
    r->format.get_number_of_archive_records=ldbs_count;
    r->format.create_archive_records_reading=ldbs_create;
    r->format.get_current_archive_record=ldbs_current;
    r->format.archive_record_move_to_next=ldbs_next;
    r->format.unpack_current_archive_record=ldbs_unpack;
    r->format.free_archive_records_reading=ldbs_free_records;
    r->format.destroy=ldbs_destroy_vtable;
}
xx_ldbs *xx_ldbs_create(xx_io_device *d,int64_t b) {
    xx_ldbs *r=(xx_ldbs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_ldbs_init(r,d,b);
    return r;
}
void xx_ldbs_destroy(xx_ldbs *r) {
    if (r) ldbs_destroy_vtable(&r->format);
}
void xx_ldbs_free(xx_ldbs *r) {
    if (r) {xx_ldbs_destroy(r);xx_mem_free(r);}
}
bool xx_ldbs_unpack_to_device(xx_ldbs *r,xx_io_device *d,
                              xx_pd_struct *pd) {
    return r && ldbs_unpack_device(&r->format,NULL,d,pd);
}
