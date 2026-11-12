/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Stored named RAWFILE and FILE payload resources only. Generic PE sections/UI resources
 * remain outside this reader. Numeric IDs/languages determine output names. */
#include "xxfclib/formats/archive/pe_rawfile/xx_pe_rawfile.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "../../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
#define PF_FILE INT64_C(536870912)
#define PF_MEMBER UINT64_C(268435456)
#define PF_RECORDS 4096U
#define PF_VISITS 16384U
#define PF_MEMORY UINT64_C(67108864)
typedef struct pf_section { uint32_t va,raw,size; } pf_section;
typedef struct pf_context {
    Abstractformat *format; int64_t total,root; uint32_t root_size,headers;
    pf_section sections[96]; unsigned section_count,visits,dirs_count;
    uint32_t dirs[PF_RECORDS+3U];
} pf_context;
typedef struct pf_member { int64_t entry; uint64_t resident; uint8_t descriptor[16]; } pf_member;
static uint16_t pf_u16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t pf_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t pf_budget(Abstractformat *f)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t limit=v?xx_var_get_u64(v):PF_MEMORY; return limit<PF_MEMORY?limit:PF_MEMORY;
}
/* Reject ambiguous section mappings and ranges crossing a second section. */
static bool pf_map(pf_context *c,uint32_t rva,uint32_t size,int64_t *offset)
{
    bool found=false;int64_t answer=0;unsigned i;uint64_t end=(uint64_t)rva+size;
    if(end>UINT64_C(0x100000000))return false;
    if(rva<c->headers) {
        if(size>c->headers-rva)return false;
        found=true;answer=rva;
    }
    for(i=0;i<c->section_count;++i) {
        const pf_section *s=&c->sections[i];uint64_t section_end=(uint64_t)s->va+s->size;
        if(!s->size)continue;
        if(size) {
            if((uint64_t)rva>=section_end || (uint64_t)s->va>=end)continue;
            /* Even a partial competing mapping is ambiguous. */
            if(rva<s->va || end>section_end)return false;
        } else if(rva<s->va || (uint64_t)rva>section_end)continue;
        if(found)return false;
        answer=(int64_t)s->raw+(int64_t)((uint64_t)rva-s->va);found=true;
    }
    if(!found || answer<0 || answer>c->total || size>(uint64_t)(c->total-answer))return false;
    *offset=answer;return true;
}
static bool pf_read(pf_context *c,uint32_t at,void *bytes,size_t size)
{
    return at<=c->root_size && size<=c->root_size-at && pm_read(c->format,c->root+(int64_t)at,bytes,size);
}
static bool pf_dir(pf_context *c,uint32_t at,unsigned *count,xx_pd_struct *pd)
{
    uint8_t h[16];unsigned i;
    if((at&3U) || c->dirs_count>=PF_RECORDS+3U || xx_pd_is_stopped(pd))return false;
    for(i=0;i<c->dirs_count;++i)if(c->dirs[i]==at)return false;
    if(!pf_read(c,at,h,sizeof(h)))return false;
    *count=(unsigned)pf_u16(h+12)+pf_u16(h+14);
    if(*count>PF_RECORDS || (uint64_t)at+16U+(uint64_t)*count*8U>c->root_size)return false;
    c->dirs[c->dirs_count++]=at;return true;
}
static bool pf_entry(pf_context *c,uint32_t dir,unsigned i,uint32_t *id,uint32_t *child,xx_pd_struct *pd)
{
    uint8_t row[8];
    if(++c->visits>PF_VISITS || xx_pd_is_stopped(pd) || !pf_read(c,dir+16U+i*8U,row,sizeof(row)))return false;
    *id=pf_u32(row);*child=pf_u32(row+4);return true;
}
static bool pf_type(pf_context *c,uint32_t id,unsigned *selected)
{
    static const uint8_t rawfile[]={ 'R',0,'A',0,'W',0,'F',0,'I',0,'L',0,'E',0 };
    static const uint8_t file[]={ 'F',0,'I',0,'L',0,'E',0 };
    uint8_t length[2],name[sizeof(rawfile)];uint32_t at=id&UINT32_C(0x7fffffff);unsigned units;
    *selected=0U;if(!(id&UINT32_C(0x80000000)))return true;
    if(!pf_read(c,at,length,2))return false;
    units=pf_u16(length);
    if(units>256U || at>c->root_size-2U || (uint64_t)units*2U>c->root_size-at-2U)return false;
    if(units==7U) {if(!pf_read(c,at+2U,name,sizeof(rawfile)))return false;if(!xx_rt_memcmp(name,rawfile,sizeof(rawfile)))*selected=1U;}
    else if(units==4U) {if(!pf_read(c,at+2U,name,sizeof(file)))return false;if(!xx_rt_memcmp(name,file,sizeof(file)))*selected=2U;}
    return true;
}
static bool pf_copy(Abstractformat *f,pm_member *m,xx_io_device *destination,xx_pd_struct *pd)
{
    pf_member *record=(pf_member *)m->context;uint8_t descriptor[16],*buffer=NULL;
    uint64_t done=0,budget=pf_budget(f);size_t capacity=65536U;bool ok=false;
    if(!record || record->resident>=budget || xx_pd_is_stopped(pd) ||
       !pm_read(f,record->entry-f->base_address,descriptor,sizeof(descriptor)) || xx_rt_memcmp(descriptor,record->descriptor,sizeof(descriptor)))return false;
    if(capacity>budget-record->resident)capacity=(size_t)(budget-record->resident);
    if(!capacity)return false;
    buffer=(uint8_t *)xx_mem_alloc(capacity);if(!buffer)return false;
    while(done<(uint64_t)m->size) {
        size_t take=(uint64_t)m->size-done<capacity?(size_t)((uint64_t)m->size-done):capacity,put=0;
        if(xx_pd_is_stopped(pd) || !pm_read(f,m->offset-f->base_address+(int64_t)done,buffer,take))goto done;
        while(destination && put<take) {
            ssize_t amount;
            if(xx_pd_is_stopped(pd))goto done;
            amount=xx_io_write(destination,buffer+put,take-put);
            if(amount<=0 || (size_t)amount>take-put)goto done;
            put+=(size_t)amount;
        }
        done+=take;
    }
    ok=!xx_pd_is_stopped(pd);
done:xx_mem_free(buffer);return ok;
}
static int pf_compare(const void *a,const void *b)
{ return xx_rt_strcmp(((const pm_member *)a)->name,((const pm_member *)b)->name); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    pf_context c;uint8_t dos[64],coff[24],opt[136],row[40];uint32_t pe,dirs_offset,resource_rva;
    uint16_t optional_size,magic;unsigned roots,i;unsigned selected_types=0U;uint64_t resident,maximum=PF_MEMBER;
    int64_t table,image_end;const xx_var *v;xx_pe carrier;bool complete;
    xx_mem_zero(&c,sizeof(c));c.format=f;c.total=pm_available(f);
    if(!f || f->is_mapped || c.total<512 || c.total>PF_FILE || xx_pd_is_stopped(pd) ||
       pf_budget(f)<sizeof(c)+sizeof(*s)+96U*sizeof(xx_pe_section) || !pm_read(f,0,dos,sizeof(dos)) || dos[0]!='M' || dos[1]!='Z')return false;
    pe=pf_u32(dos+60);
    if(pe<64U || pe>1048576U || !pm_read(f,pe,coff,sizeof(coff)) || xx_rt_memcmp(coff,"PE\0\0",4U))return false;
    c.section_count=pf_u16(coff+6);optional_size=pf_u16(coff+20);
    if(!c.section_count || c.section_count>96U || optional_size<120U || !pm_read(f,(int64_t)pe+24,opt,120U))return false;
    magic=pf_u16(opt);
    if(magic==0x10bU)dirs_offset=96U;
    else if(magic==0x20bU && optional_size>=136U && pm_read(f,(int64_t)pe+24,opt,sizeof(opt)))dirs_offset=112U;
    else return false;
    if(pf_u32(opt+dirs_offset-4U)<3U || optional_size<dirs_offset+24U)return false;
    resource_rva=pf_u32(opt+dirs_offset+16U);c.root_size=pf_u32(opt+dirs_offset+20U);c.headers=pf_u32(opt+60);
    table=(int64_t)pe+24+optional_size;image_end=c.headers;
    if(!resource_rva || c.root_size<16U || c.headers>c.total || table+(int64_t)c.section_count*40>c.headers)return false;
    for(i=0;i<c.section_count;++i) {
        pf_section *section=&c.sections[i];int64_t end;
        if(xx_pd_is_stopped(pd) || !pm_read(f,table+(int64_t)i*40,row,sizeof(row)))return false;
        section->va=pf_u32(row+12);section->size=pf_u32(row+16);section->raw=pf_u32(row+20);
        if((uint64_t)section->raw+section->size>(uint64_t)c.total || (uint64_t)section->va+section->size>UINT64_C(0x100000000))return false;
        end=(int64_t)section->raw+section->size;if(section->size && end>image_end)image_end=end;
    }
    /* This initial scope deliberately rejects every appended tail, including
     * certificates/debug data outside sections; stronger SFX readers win. */
    if(image_end!=c.total || !pf_map(&c,resource_rva,c.root_size,&c.root))return false;
    xx_pe_init(&carrier,f->device,f->base_address);complete=xx_pe_check_is_valid(&carrier.format,pd);xx_pe_destroy(&carrier);
    if(!complete || !pf_dir(&c,0,&roots,pd))return false;
    v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MAX_MEMBER_SIZE);if(v && xx_var_get_u64(v)<maximum)maximum=xx_var_get_u64(v);
    for(i=0;i<roots;++i) {
        uint32_t type,next;unsigned wanted;unsigned ids,j;uint32_t ids_at;
        if(!pf_entry(&c,0,i,&type,&next,pd) || !pf_type(&c,type,&wanted))return false;
        if(!wanted)continue;
        if((selected_types&wanted) || !(next&UINT32_C(0x80000000)))return false;
        selected_types|=wanted;ids_at=next&UINT32_C(0x7fffffff);
        if(!pf_dir(&c,ids_at,&ids,pd) || !ids)return false;
        for(j=0;j<ids;++j) {
            uint32_t id,lang_next,langs_at;unsigned langs,k;
            if(!pf_entry(&c,ids_at,j,&id,&lang_next,pd) || (id&UINT32_C(0x80000000)) || !(lang_next&UINT32_C(0x80000000)))return false;
            langs_at=lang_next&UINT32_C(0x7fffffff);
            if(!pf_dir(&c,langs_at,&langs,pd) || !langs)return false;
            for(k=0;k<langs;++k) {
                uint32_t lang,leaf,data_size,data_rva;uint8_t descriptor[16];int64_t at;pm_member *member;pf_member *record;char name[96];size_t capacity;
                if(!pf_entry(&c,langs_at,k,&lang,&leaf,pd) || (lang&UINT32_C(0x80000000)) || (leaf&UINT32_C(0x80000000)) ||
                   !pf_read(&c,leaf,descriptor,sizeof(descriptor)) || pf_u32(descriptor+12))return false;
                data_rva=pf_u32(descriptor);data_size=pf_u32(descriptor+4);
                if(s->count>=PF_RECORDS || data_size>maximum || !pf_map(&c,data_rva,data_size,&at))return false;
                capacity=s->capacity;if(capacity==s->count)capacity=capacity?capacity*2U:8U;
                resident=sizeof(c)+sizeof(*s)+capacity*sizeof(pm_member)+(s->count+1U)*sizeof(pf_member);
                if(resident>=pf_budget(f))return false;
                xx_rt_snprintf(name,sizeof(name),"%s-%08u-%08u.bin",wanted==1U?"rawfile":"file",(unsigned)id,(unsigned)lang);
                if(!pm_add(f,s,name,at,data_size))return false;
                member=&s->items[s->count-1U];xx_rt_snprintf(member->name,sizeof(member->name),"%s",name);
                record=(pf_member *)xx_mem_alloc(sizeof(*record));if(!record)return false;
                record->entry=f->base_address+c.root+leaf;record->resident=resident;xx_mem_copy(record->descriptor,descriptor,sizeof(descriptor));
                member->context=record;member->free_context=xx_mem_free;member->read_all=pf_copy;
            }
        }
    }
    if(!selected_types || !s->count || xx_pd_is_stopped(pd))return false;
    xx_rt_qsort(s->items,s->count,sizeof(*s->items),pf_compare);
    resident=sizeof(c)+sizeof(*s)+s->capacity*sizeof(pm_member)+s->count*sizeof(pf_member);
    for(i=0;i<s->count;++i) {
        if(i && !xx_rt_strcmp(s->items[i-1U].name,s->items[i].name))return false;
        ((pf_member *)s->items[i].context)->resident=resident;
    }
    s->size=c.total;return true;
}
static const xx_archive_record *pf_current(Abstractformat *f,xx_archive_record_state *state)
{
    const xx_archive_record *record=pm_current(f,state);
    if(record) {pm_stream *s=(pm_stream *)state->internal_state;pf_member *m=(pf_member *)s->items[s->index].context;state->current_record.header_offset=m->entry;state->current_record.header_size=16;}
    return record;
}
static void pf_destroy(Abstractformat *f) {if(f)xx_format_cleanup_extra_parameters(f);}
void xx_pe_rawfile_init(xx_pe_rawfile *reader,xx_io_device *device,int64_t base)
{
    if(!reader)return;
    xx_mem_zero(reader,sizeof(*reader));pm_init(&reader->format,device,base,XX_FILE_TYPE_PE_RAWFILE,"exe");
    reader->format.get_current_archive_record=pf_current;reader->format.destroy=pf_destroy;
}
void xx_pe_rawfile_destroy(xx_pe_rawfile *reader) {if(reader)pf_destroy(&reader->format);}
xx_pe_rawfile *xx_pe_rawfile_create(xx_io_device *device,int64_t base)
{ xx_pe_rawfile *r=(xx_pe_rawfile *)xx_mem_alloc(sizeof(*r));if(r)xx_pe_rawfile_init(r,device,base);return r; }
void xx_pe_rawfile_free(xx_pe_rawfile *reader) {if(reader){xx_pe_rawfile_destroy(reader);xx_mem_free(reader);}}
