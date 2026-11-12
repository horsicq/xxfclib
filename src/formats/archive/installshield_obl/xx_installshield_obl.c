/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * InstallShield OBL: stored InstallScript objects in a pOdA directory.
 * See docs/INSTALLSHIELD_OBL.md for independently checked layout evidence. */
#include "xxfclib/formats/archive/installshield_obl/xx_installshield_obl.h"
#include "../../xx_payload_members.h"
#define OBL_MEMORY_CAP (256U * 1024U * 1024U)
#define OBL_WORKSPACE 20480U
typedef struct obl_member { int64_t header; uint32_t header_size; uint64_t resident; } obl_member;
static uint32_t obl_u32(const uint8_t *p)
{ return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t obl_budget(Abstractformat *f)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):OBL_MEMORY_CAP;
    return n<OBL_MEMORY_CAP?n:OBL_MEMORY_CAP;
}
static bool obl_copy(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd)
{
    uint8_t buffer[16384]; uint64_t left=(uint64_t)m->size,budget=obl_budget(f);
    obl_member *ctx=(obl_member *)m->context; int64_t at=m->offset-f->base_address;
    if (!ctx || ctx->resident>budget || OBL_WORKSPACE>budget-ctx->resident) return false;
    while (left) {
        size_t n=left>sizeof(buffer)?sizeof(buffer):(size_t)left,done=0;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,buffer,n)) return false;
        while (out && done<n) {
            ssize_t wrote;
            if (pd && xx_pd_is_stopped(pd)) return false;
            wrote=xx_io_write(out,buffer+done,n-done);
            if (wrote<=0 || (size_t)wrote>n-done) return false;
            done+=(size_t)wrote;
        }
        at+=(int64_t)n;left-=n;
    }
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    uint8_t h[12]; uint32_t count,i; int64_t total=pm_available(f),at=12,end=12,first=total;
    uint64_t names=0,budget=obl_budget(f),maximum=UINT64_MAX;
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (v) maximum=xx_var_get_u64(v);
    if ((pd && xx_pd_is_stopped(pd)) || total<12 || !pm_read(f,0,h,12U) ||
        xx_rt_memcmp(h,"pOdA",4U) || obl_u32(h+4)!=1U) return false;
    count=obl_u32(h+8);
    if (!count || count>100000U || count>(uint64_t)(total-12)/11U) return false;
    for (i=0;i<count;++i) {
        uint32_t length,offset,size; uint64_t resident; size_t capacity,j,prefix;
        char *name; obl_member *ctx; int64_t header=at;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,h,2U)) return false;
        length=h[0]|((uint32_t)h[1]<<8);at+=2;
        if (!length || at>total || (uint64_t)(total-at)<length+8U) return false;
        capacity=s->capacity==s->count?(s->capacity?s->capacity*2U:8U):s->capacity;
        resident=sizeof(pm_stream)+(uint64_t)capacity*sizeof(pm_member)+
            (uint64_t)(i+1U)*sizeof(obl_member)+names+length+16U;
        if (resident>budget || OBL_WORKSPACE>budget-resident) return false;
        name=(char *)xx_mem_alloc(length+16U);
        if (!name) return false;
        prefix=(size_t)xx_rt_snprintf(name,16U,"%04u-",i);
        if (!pm_read(f,at,name+prefix,length) || !pm_read(f,at+length,h,8U)) { xx_mem_free(name);return false; }
        for (j=0;j<length;++j) {
            unsigned char c=(unsigned char)name[prefix+j];
            if (!c) { xx_mem_free(name);return false; }
            if (c<32U || c>126U || c=='/' || c=='\\' || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*') name[prefix+j]='_';
        }
        if (name[prefix+length-1U]=='.' || name[prefix+length-1U]==' ') name[prefix+length-1U]='_';
        name[prefix+length]=0;at+=length+8U;
        offset=obl_u32(h);size=obl_u32(h+4);
        if (offset>(uint64_t)total || size>(uint64_t)total-offset || size>maximum) { xx_mem_free(name);return false; }
        ctx=(obl_member *)xx_mem_alloc(sizeof(*ctx));
        if (!ctx) { xx_mem_free(name);return false; }
        ctx->header=f->base_address+header;ctx->header_size=length+10U;ctx->resident=0;
        if (!pm_add(f,s,"object.obs",offset,size)) { xx_mem_free(name);xx_mem_free(ctx);return false; }
        s->items[i].display_name=name;s->items[i].context=ctx;s->items[i].free_context=xx_mem_free;s->items[i].read_all=obl_copy;
        names+=length+16U;
        if (offset<first) first=offset;
        if ((int64_t)offset+size>end) end=(int64_t)offset+size;
    }
    /* Payload order is not prescribed; every extent must follow the full index. */
    if (first<at) return false;
    for (i=0;i<count;++i) ((obl_member *)s->items[i].context)->resident=
        sizeof(pm_stream)+(uint64_t)s->capacity*sizeof(pm_member)+(uint64_t)count*sizeof(obl_member)+names;
    s->size=end>at?end:at;
    return !(pd && xx_pd_is_stopped(pd));
}
static const xx_archive_record *obl_current(Abstractformat *f,xx_archive_record_state *state)
{
    const xx_archive_record *r=pm_current(f,state);
    if (r) {
        pm_stream *s=(pm_stream *)state->internal_state;
        obl_member *ctx=(obl_member *)s->items[s->index].context;
        state->current_record.header_offset=ctx->header;state->current_record.header_size=ctx->header_size;
    }
    return r;
}
Abstractformat *xx_installshield_obl_create(xx_io_device *device,int64_t base_address)
{
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) { pm_init(f,device,base_address,XX_FILE_TYPE_INSTALLSHIELD_OBL,"obl");f->get_current_archive_record=obl_current; }
    return f;
}
void xx_installshield_obl_free(Abstractformat *f)
{ if (f) { xx_format_destroy(f);xx_mem_free(f); } }
#include "../../xx_format_abstract_extractor_adapter.h"
static Abstractformat *obl_open(xx_io_device *d) { return xx_installshield_obl_create(d,0); }
static const xx_file_type_t obl_types[]={XX_FILE_TYPE_INSTALLSHIELD_OBL};
static const uint8_t obl_magic[]={'p','O','d','A'};
static const xx_format_search_anchor obl_anchors[]={{obl_magic,4U,0U}};
const xx_format_search_desc xx_installshield_obl_search_desc={obl_types,1U,obl_anchors,1U,obl_open,xx_installshield_obl_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(installshield_obl,xx_installshield_obl_search_desc)
