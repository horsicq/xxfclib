/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * D88 layout: https://www.pc98.org/project/doc/d88.html.
 * Independently verified using the LibDsk 1.5.22 D88 producer/reader.
 */
#include "xxfclib/formats/pc98_d88/xx_pc98_d88.h"
#include "../common/xx_disk_music_components.h"
#include <string.h>
#include "xxfclib/data/xx_data.h"
#define D88_MAX_BYTES (32U*1024U*1024U)
#define D88_MAX_DISKS 8U
#define D88_MAX_SECTORS 8192U
#define D88_COPY 32768U

static bool d88_stop(xx_pd_struct *pd) { return pd&&xx_pd_is_stopped(pd); }
static bool d88_load(Abstractformat *f,disk_music_blob *b,xx_pd_struct *pd) {
    int64_t size=pm_available(f); size_t done=0U;
    if (size<672||size>D88_MAX_BYTES||d88_stop(pd)) return false;
    b->p=(uint8_t *)xx_mem_alloc((size_t)size); if (!b->p) return false;
    b->n=(uint64_t)size; b->pd=pd;
    if (xx_io_seek64(f->device,f->base_address,SEEK_SET)!=0) return false;
    while (done<b->n) {
        size_t request=b->n-done>65536U ? 65536U : (size_t)(b->n-done);
        ssize_t got;
        if (d88_stop(pd)) return false;
        got=xx_io_read(f->device,b->p+done,request);
        if (got<=0||(size_t)got>request||d88_stop(pd)) return false;
        done+=(size_t)got;
    }
    return !d88_stop(pd);
}
static bool d88_append_raw(Abstractformat *f,pm_stream *s,disk_music_blob *b,
                           const uint32_t *map,uint32_t count,uint32_t sector_size,
                           unsigned disk_no) {
    uint8_t *image; uint64_t length=(uint64_t)count*sector_size;
    char name[64]; uint32_t i;
    if (!count||!sector_size||length>D88_MAX_BYTES||length>SIZE_MAX) return false;
    image=(uint8_t *)xx_mem_alloc((size_t)length); if (!image) return false;
    for (i=0U;i<count;++i) {
        uint64_t offset=(uint64_t)map[i]-1U;
        if (d88_stop(b->pd)||!disk_music_span(b,offset,sector_size)) {
            xx_mem_free(image); return false;
        }
        xx_rt_memcpy(image+(size_t)i*sector_size,b->p+(size_t)offset,sector_size);
    }
    (void)xx_rt_snprintf(name,sizeof(name),"disk%02u.img",disk_no);
    if (!pm_add(f,s,name,0,0)) { xx_mem_free(image); return false; }
    s->items[s->count-1U].memory=image;
    s->items[s->count-1U].size=(int64_t)length;
    s->items[s->count-1U].packed_size=(int64_t)length;
    s->items[s->count-1U].offset=-1;
    return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    disk_music_blob b={0}; uint32_t *map=NULL; uint64_t disk_at=0U;
    unsigned disk_no=0U; uint32_t all_sectors=0U; bool ok=false;
    DISK_MUSIC_NEED(d88_load(f,&b,pd));
    map=(uint32_t *)xx_mem_alloc(D88_MAX_SECTORS*sizeof(*map));
    DISK_MUSIC_NEED(map);
    while (disk_at<b.n) {
        uint64_t disk_size; uint32_t offsets[164]={0},first=0U,last=0U;
        uint32_t i,table_count,raw_count=0U,raw_size=0U,raw_spt=0U;
        uint32_t expected_track=0U,formatted=0U;
        uint32_t disk_sector_start=all_sectors; bool raw=true;
        char name[64];
        DISK_MUSIC_NEED(disk_no<D88_MAX_DISKS&&disk_music_span(&b,disk_at,672U));
        disk_size=xx_data_get_u32(b.p+(size_t)disk_at+28U, 4, 0, false);
        DISK_MUSIC_NEED(disk_size>=672U&&disk_size<=D88_MAX_BYTES&&
                disk_music_span(&b,disk_at,disk_size));
        for (i=0U;i<160U;++i) {
            uint32_t t=xx_data_get_u32(b.p+(size_t)disk_at+32U+i*4U, 4, 0, false);
            if (t&&!first) first=t;
        }
        DISK_MUSIC_NEED(first==672U||first==688U);
        table_count=first==688U ? 164U : 160U;
        DISK_MUSIC_NEED(disk_size>=first);
        for (i=0U;i<table_count;++i) {
            uint32_t t=xx_data_get_u32(b.p+(size_t)disk_at+32U+i*4U, 4, 0, false);
            offsets[i]=t;
            if (!t) continue;
            DISK_MUSIC_NEED(t>=first&&t<=disk_size&&
                    (t==disk_size||(!last||t>last))&&
                    (last!=disk_size||t==disk_size));
            last=t;
        }
        DISK_MUSIC_NEED(last);
        (void)xx_rt_snprintf(name,sizeof(name),"disk%02u.d88",disk_no);
        DISK_MUSIC_NEED(pm_add(f,s,name,(int64_t)disk_at,(int64_t)disk_size));
        for (i=0U;i<table_count;++i) {
            uint32_t start=offsets[i],end=(uint32_t)disk_size;
            uint32_t at,count,k,slots[129]={0}; bool track_raw=true;
            if (!start||start==disk_size) continue;
            for (k=i+1U;k<table_count;++k) if (offsets[k]) {
                end=offsets[k]; break;
            }
            DISK_MUSIC_NEED(end>start&&end-start>=16U);
            at=start;
            count=xx_data_get_u16(b.p+(size_t)disk_at+at+4U, 2, 0, false);
            DISK_MUSIC_NEED(count&&count<=128U&&count<=D88_MAX_SECTORS-all_sectors);
            ++formatted;
            if (i!=expected_track) raw=false;
            expected_track=i+1U;
            if (!raw_spt) raw_spt=count;
            if (count!=raw_spt) track_raw=false;
            for (k=0U;k<count;++k) {
                const uint8_t *h; uint32_t actual,implied; uint8_t id;
                DISK_MUSIC_NEED(!d88_stop(pd)&&at<=end&&end-at>=16U);
                h=b.p+(size_t)disk_at+at;
                actual=xx_data_get_u16(h+14U, 2, 0, false); id=h[2];
                DISK_MUSIC_NEED(h[3]<=7U&&actual<=16384U&&
                        xx_data_get_u16(h+4U, 2, 0, false)==count&&actual<=end-at-16U);
                implied=128U<<h[3];
                if (h[0]!=i/2U||h[1]!=i%2U||id<1U||id>count||
                    (id<=128U&&slots[id])||h[7]||h[8]||actual!=implied)
                    track_raw=false;
                if (id>=1U&&id<=128U) slots[id]=
                    (uint32_t)(disk_at+at+16U)+1U;
                if (!raw_size) raw_size=actual;
                if (actual!=raw_size) track_raw=false;
                (void)xx_rt_snprintf(name,sizeof(name),
                    "disk%02u-track%03u-slot%03u-sector%03u.bin",
                    disk_no,i,k,(unsigned)id);
                DISK_MUSIC_NEED(pm_add(f,s,name,(int64_t)(disk_at+at+16U),actual));
                ++all_sectors;
                at+=16U+actual;
            }
            DISK_MUSIC_NEED(at==end);
            if (track_raw) for (k=1U;k<=count;++k) {
                if (!slots[k]) { track_raw=false; break; }
            }
            if (!track_raw) raw=false;
            if (raw) for (k=1U;k<=count;++k) {
                DISK_MUSIC_NEED(raw_count<D88_MAX_SECTORS);
                map[raw_count++]=slots[k];
            }
        }
        DISK_MUSIC_NEED(formatted);
        if (raw&&raw_count==all_sectors-disk_sector_start&&raw_size)
            DISK_MUSIC_NEED(d88_append_raw(f,s,&b,map,raw_count,raw_size,disk_no));
        disk_at+=disk_size; ++disk_no;
    }
    DISK_MUSIC_NEED(disk_at==b.n&&disk_no&&all_sectors&&!d88_stop(pd));
    s->size=(int64_t)b.n; ok=true;
done:
    xx_mem_free(map); xx_mem_free(b.p); return ok;
}
static bool d88_member_limit(Abstractformat *f,xx_archive_record_state *state,
                             const pm_stream *s,const pm_member *m) {
    const xx_var *max=xx_format_resolve_extra_parameter(
        f,&state->options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem=xx_format_resolve_extra_parameter(
        f,&state->options,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t needed=sizeof(*s)+s->capacity*sizeof(*s->items)+D88_COPY;
    size_t i;
    for (i=0U;i<s->count;++i) if (s->items[i].memory)
        needed+=(uint64_t)s->items[i].size;
    return (!max||(uint64_t)m->size<=xx_var_get_u64(max))&&
           (!mem||needed<=xx_var_get_u64(mem));
}
bool xx_pc98_d88_extract_record_to_device(Abstractformat *f,
    xx_archive_record_state *state,xx_io_device *out,xx_pd_struct *pd) {
    pm_stream *s; pm_member *m; int64_t saved,done=0; uint8_t buffer[D88_COPY];
    bool ok=true;
    if (!f||!f->device||!state||state->format!=f||!state->has_record||
        out==f->device||d88_stop(pd)) return false;
    s=(pm_stream *)state->internal_state; m=&s->items[s->index];
    if (!d88_member_limit(f,state,s,m)||(saved=xx_io_tell(f->device))<0) return false;
    while (done<m->size&&!d88_stop(pd)) {
        size_t n=(uint64_t)(m->size-done)>D88_COPY ?
            D88_COPY : (size_t)(m->size-done),received=0U,written=0U;
        const uint8_t *data=m->memory ? m->memory+(size_t)done : buffer;
        if (!m->memory) {
            if (xx_io_seek64(f->device,m->offset+done,SEEK_SET)!=0) {
                ok=false; break;
            }
            while (received<n&&!d88_stop(pd)) {
                ssize_t got=xx_io_read(f->device,buffer+received,n-received);
                if (got<=0||(size_t)got>n-received||d88_stop(pd)) {
                    ok=false; break;
                }
                received+=(size_t)got;
            }
            if (!ok||received!=n) { ok=false; break; }
        }
        while (out&&written<n&&!d88_stop(pd)) {
            ssize_t got=xx_io_write(out,data+written,n-written);
            if (got<=0||(size_t)got>n-written||d88_stop(pd)) {
                ok=false; break;
            }
            written+=(size_t)got;
        }
        if (!ok||(out&&written!=n)) { ok=false; break; }
        done+=(int64_t)n;
    }
    if (xx_io_seek64(f->device,saved,SEEK_SET)!=0) ok=false;
    return ok&&done==m->size&&!d88_stop(pd);
}
static bool d88_same_path(const char *a,const char *b) {
    while (*a&&*b) {
        char x=*a++,y=*b++;
        if (x=='\\') { x='/'; } if (y=='\\') y='/';
        if (x>='A'&&x<='Z') x=(char)(x+32);
        if (y>='A'&&y<='Z') y=(char)(y+32);
        if (x!=y) return false;
    }
    return *a==*b;
}
static xx_io_device *d88_stage(const char *destination,char **stage) {
    size_t i,parent=0U; unsigned attempt; char *directory;
    *stage=NULL; directory=xx_str_dup(destination); if (!directory) return NULL;
    for (i=0U;directory[i];++i)
        if (directory[i]=='/'||directory[i]=='\\') parent=i+1U;
    directory[parent]=0;
    for (attempt=0U;attempt<128U;++attempt) {
        char suffix[40],*candidate; xx_io_device *output;
        (void)xx_rt_snprintf(suffix,sizeof(suffix),".xx_d88.tmp.%u",attempt);
        candidate=xx_str_concat(directory,suffix); if (!candidate) break;
        if (d88_same_path(candidate,destination)) {
            xx_str_free(candidate); continue;
        }
        output=xx_io_file_open(candidate,"wbx");
        if (output) { *stage=candidate; xx_str_free(directory); return output; }
        xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
static bool d88_unpack(Abstractformat *f,xx_archive_record_state *state,
                       xx_pd_struct *pd) {
    pm_stream *s; pm_member *m; const xx_var *option,*ov; const char *base=NULL;
    char *owned=NULL,*path=NULL,*stage=NULL; bool ok=false,overwrite;
    if (!f||!state||state->format!=f||!state->has_record||d88_stop(pd))
        return false;
    s=(pm_stream *)state->internal_state; m=&s->items[s->index];
    option=xx_format_resolve_extra_parameter(f,&state->options,XX_META_ID_OPT_UNPACK_PATH);
    ov=xx_format_resolve_extra_parameter(f,&state->options,XX_META_ID_OPT_OVERWRITE);
    overwrite=ov&&xx_var_get_bool(ov);
    if (!option) return xx_pc98_d88_extract_record_to_device(f,state,NULL,pd);
    if (option->type==XX_VAR_TYPE_STRING||option->type==XX_VAR_TYPE_STRING_VIEW)
        base=xx_var_get_str(option);
    else if (option->type==XX_VAR_TYPE_WSTRING||option->type==XX_VAR_TYPE_WSTRING_VIEW) {
        owned=xx_str_unicode_to_utf8(xx_var_get_wstr(option)); base=owned;
    }
    if (!base) goto done;
    path=*base&&base[strlen(base)-1U]!='/'&&base[strlen(base)-1U]!='\\' ?
        xx_str_concat3(base,"/",m->name) : xx_str_concat(base,m->name);
    if (!path||(!overwrite&&xx_io_file_exists_a(path))||
        !xx_store_create_dirs_a(path,false)||d88_stop(pd)) goto done;
    {
        xx_io_device *output=d88_stage(path,&stage);
        if (!output) goto done;
        ok=xx_pc98_d88_extract_record_to_device(f,state,output,pd);
        if (xx_io_close(output)!=0) ok=false;
    }
    if (ok&&!d88_stop(pd)) ok=xx_io_file_replace_a(stage,path,overwrite);
    else ok=false;
done:
    if (stage) { if (!ok) (void)xx_io_file_remove_a(stage); xx_str_free(stage); }
    xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_pc98_d88_init(xx_pc98_d88 *r,xx_io_device *d,int64_t b) {
    if (r) {
        xx_mem_zero(r,sizeof(*r));
        pm_init(&r->format,d,b,XX_FILE_TYPE_PC98_D88,"d88");
        r->format.unpack_current_archive_record=d88_unpack;
    }
}
xx_pc98_d88 *xx_pc98_d88_create(xx_io_device *d,int64_t b) {
    xx_pc98_d88 *r=(xx_pc98_d88 *)xx_mem_alloc(sizeof(*r));
    if (r) { xx_pc98_d88_init(r,d,b); } return r;
}
void xx_pc98_d88_destroy(xx_pc98_d88 *r) {
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_pc98_d88_free(xx_pc98_d88 *r) {
    if (r) { xx_pc98_d88_destroy(r); xx_mem_free(r); }
}
bool xx_pc98_d88_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {
    return pm_valid(f,pd);
}
bool xx_pc98_d88_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {
    return pm_handle(f,pd);
}
int64_t xx_pc98_d88_get_format_size(Abstractformat *f,xx_pd_struct *pd) {
    return pm_size(f,pd);
}
uint64_t xx_pc98_d88_get_number_of_archive_records(Abstractformat *f,
                                                     xx_pd_struct *pd) {
    return pm_count(f,pd);
}
xx_archive_record_state *xx_pc98_d88_create_archive_records_reading(
    Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    return pm_create_records(f,options,pd);
}
const xx_archive_record *xx_pc98_d88_get_current_archive_record(
    Abstractformat *f,xx_archive_record_state *state) {
    return pm_current(f,state);
}
bool xx_pc98_d88_archive_record_move_to_next(Abstractformat *f,
    xx_archive_record_state *state,xx_pd_struct *pd) {
    return pm_next(f,state,pd);
}
bool xx_pc98_d88_unpack_current_archive_record(Abstractformat *f,
    xx_archive_record_state *state,xx_pd_struct *pd) {
    return d88_unpack(f,state,pd);
}
void xx_pc98_d88_free_archive_records_reading(Abstractformat *f,
                                               xx_archive_record_state *state) {
    pm_free_records(f,state);
}
