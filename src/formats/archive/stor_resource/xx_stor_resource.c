/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * STOR stored resources, independently established from the ARC9 DATA.BIN.
 * Not LDS: four-byte signature + LE32 directory offset, then 21-byte entries.
 * No directory count is established. Accept exact EOF or a bounded following
 * Smacker header/tables/extent; never accept arbitrary invalid records as EOF.
 */
#include "xxfclib/formats/archive/stor_resource/xx_stor_resource.h"
#include "../../xx_payload_members.h"
#define STOR_MEMORY_CAP (256U * 1024U * 1024U)
#define STOR_COPY_SIZE 16384U
#define STOR_WORKSPACE (STOR_COPY_SIZE + 4096U)
#define STOR_RECORD_CAP 100000U

typedef struct stor_member { int64_t header_offset; uint64_t resident; } stor_member;
static uint32_t stor_u32(const uint8_t *p)
{ return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t stor_budget(Abstractformat *f)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):STOR_MEMORY_CAP;
    return n<STOR_MEMORY_CAP?n:STOR_MEMORY_CAP;
}
/* This establishes an overlay boundary, not full media/codec validity.
 * Smacker layout: FFmpeg libavformat/smacker.c. Do not allocate its payload. */
static bool stor_smacker_boundary(Abstractformat *f,int64_t at,xx_pd_struct *pd)
{
    uint8_t h[104], sizes[1024];
    uint32_t frames,flags,trees,i;
    uint64_t available,extent;
    int64_t total=pm_available(f);
    if (at<0 || at>total || total-at<104 || !pm_read(f,at,h,sizeof(h)) ||
        (xx_rt_memcmp(h,"SMK2",4U) && xx_rt_memcmp(h,"SMK4",4U))) return false;
    frames=stor_u32(h+12);flags=stor_u32(h+20);trees=stor_u32(h+52);
    if (!stor_u32(h+4) || stor_u32(h+4)>32768U || !stor_u32(h+8) || stor_u32(h+8)>32768U ||
        !frames || frames>STOR_RECORD_CAP || flags>7U || !trees || trees>16777216U ||
        !stor_u32(h+16) || stor_u32(h+16)==0x80000000U) return false;
    frames+=flags&1U;
    available=(uint64_t)(total-at);
    extent=104U+(uint64_t)frames*5U+trees;
    if (extent>available) return false;
    for (i=0;i<frames;) {
        uint32_t count=frames-i>256U?256U:frames-i,j;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at+104+(int64_t)i*4,sizes,(size_t)count*4U)) return false;
        for (j=0;j<count;++j) {
            uint32_t n=stor_u32(sizes+j*4U)&~3U;
            if (!n || n>available-extent) return false;
            extent+=n;
        }
        i+=count;
    }
    return true;
}
static bool stor_copy(Abstractformat *f,pm_member *m,xx_io_device *output,xx_pd_struct *pd)
{
    uint8_t buffer[STOR_COPY_SIZE];
    stor_member *context=(stor_member *)m->context;
    uint64_t budget=stor_budget(f),left=(uint64_t)m->size;
    int64_t at=m->offset-f->base_address;
    if (!context || context->resident>budget || STOR_WORKSPACE>budget-context->resident) return false;
    while (left) {
        size_t n=left>sizeof(buffer)?sizeof(buffer):(size_t)left,done=0;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,buffer,n)) return false;
        while (output && done<n) {
            ssize_t wrote;
            if (pd && xx_pd_is_stopped(pd)) return false;
            wrote=xx_io_write(output,buffer+done,n-done);
            if (wrote<=0 || (size_t)wrote>n-done) return false;
            done+=(size_t)wrote;
        }
        left-=n;at+=(int64_t)n;
    }
    return !(pd && xx_pd_is_stopped(pd));
}
static bool stor_name(const uint8_t *raw,char *name,size_t ordinal)
{
    size_t n;
    if (!raw[0]) {
        /* Blank-name records retain payloads without inventing lost names. */
        (void)xx_rt_snprintf(name,32U,"unnamed-%06u.bin",(unsigned)ordinal);
        return true;
    }
    for (n=0;n<13U && raw[n];++n) {
        unsigned c=raw[n];
        if (c<32U || c>126U || c=='/' || c=='\\' || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*') return false;
        name[n]=(char)c;
    }
    if (!n || n==13U || name[n-1U]=='.' || name[n-1U]==' ') return false;
    name[n]=0;
    return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    uint8_t h[21];
    uint32_t directory;
    uint64_t previous=8U,budget=stor_budget(f),maximum=UINT64_MAX;
    const xx_var *limit=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    int64_t total=pm_available(f),at;
    size_t i;
    if (limit) maximum=xx_var_get_u64(limit);
    if ((pd && xx_pd_is_stopped(pd)) || total<29 || !pm_read(f,0,h,8U) || xx_rt_memcmp(h,"STOR",4U)) return false;
    directory=stor_u32(h+4);
    if (directory<8U || directory>(uint64_t)(total-21) || budget<sizeof(pm_stream)+STOR_WORKSPACE) return false;
    at=directory;
    for (;;) {
        uint32_t offset,size;
        uint64_t resident;
        size_t capacity;
        char name[32];
        stor_member *context;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (s->count && (at==total || stor_smacker_boundary(f,at,pd))) break;
        if (s->count>=STOR_RECORD_CAP || total-at<21 || !pm_read(f,at,h,21U) || !stor_name(h,name,s->count)) return false;
        offset=stor_u32(h+13);size=stor_u32(h+17);
        if (offset<8U || offset<previous || offset>directory || size>directory-offset || size>maximum || (!s->count && offset!=8U)) return false;
        previous=(uint64_t)offset+size;
        capacity=s->capacity;
        if (capacity==s->count) capacity=capacity?capacity*2U:8U;
        resident=sizeof(pm_stream)+(uint64_t)capacity*sizeof(pm_member)+(uint64_t)(s->count+1U)*sizeof(stor_member);
        if (resident>budget || STOR_WORKSPACE>budget-resident) return false;
        context=(stor_member *)xx_mem_alloc(sizeof(*context));
        if (!context) return false;
        context->header_offset=f->base_address+at;context->resident=0;
        if (!pm_add(f,s,name,offset,size)) { xx_mem_free(context);return false; }
        s->items[s->count-1U].context=context;
        s->items[s->count-1U].free_context=xx_mem_free;
        s->items[s->count-1U].read_all=stor_copy;
        at+=21;
    }
    /* Index coverage authenticates its end; interior gaps may retain older
     * unindexed resources. Those are not fabricated as directory members. */
    if (!s->count || previous!=directory) return false;
    for (i=0;i<s->count;++i) ((stor_member *)s->items[i].context)->resident=
        sizeof(pm_stream)+(uint64_t)s->capacity*sizeof(pm_member)+(uint64_t)s->count*sizeof(stor_member);
    s->size=at;
    return !(pd && xx_pd_is_stopped(pd));
}
static const xx_archive_record *stor_current(Abstractformat *f,xx_archive_record_state *state)
{
    const xx_archive_record *r=pm_current(f,state);
    if (r) {
        pm_stream *s=(pm_stream *)state->internal_state;
        state->current_record.header_offset=((stor_member *)s->items[s->index].context)->header_offset;
        state->current_record.header_size=21;
    }
    return r;
}
Abstractformat *xx_stor_resource_create(xx_io_device *device,int64_t base_address)
{
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) {
        pm_init(f,device,base_address,XX_FILE_TYPE_STOR_RESOURCE,"bin");
        f->get_current_archive_record=stor_current;
    }
    return f;
}
void xx_stor_resource_free(Abstractformat *f)
{ if (f) { xx_format_destroy(f);xx_mem_free(f); } }
#include "../../xx_format_abstract_extractor_adapter.h"
static Abstractformat *stor_open(xx_io_device *d) { return xx_stor_resource_create(d,0); }
static const xx_file_type_t stor_types[]={XX_FILE_TYPE_STOR_RESOURCE};
static const uint8_t stor_magic[]={'S','T','O','R'};
static const xx_format_search_anchor stor_anchors[]={{stor_magic,4U,0U}};
const xx_format_search_desc xx_stor_resource_search_desc={stor_types,1U,stor_anchors,1U,stor_open,xx_stor_resource_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(stor_resource,xx_stor_resource_search_desc)
