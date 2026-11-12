/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * InstallJammer CRAP02 directory, raw Deflate and size-delimited LZMA. */
#include "xxfclib/formats/archive/installjammer/xx_installjammer.h"
#include "../../xx_payload_members.h"
#include "../../xx_archive_output_name.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include "../../../algo/lzma/LzmaDec.h"
#define IJ_CAP UINT64_C(268435456)
#define IJ_MEMBER UINT64_C(67108864)
#define IJ_WORK UINT64_C(1048576)
typedef struct ij_member { int64_t header;uint64_t resident,mtime;bool lzma; } ij_member;
static uint32_t ij_u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t ij_budget(Abstractformat *f)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):IJ_CAP;return n<IJ_CAP?n:IJ_CAP;
}
/* Fixed ASCII fields require digits, a terminator and zero padding. */
static bool ij_decimal(const uint8_t *p,size_t n,uint64_t *value)
{
    size_t i=0;uint64_t result=0;
    while(i<n && p[i]>='0' && p[i]<='9') {
        unsigned d=p[i++]-'0';if(result>((uint64_t)INT64_MAX-d)/10U)return false;result=result*10U+d;
    }
    if(!i || i==n)return false;
    for(;i<n;++i)if(p[i])return false;
    *value=result;return true;
}
/* Optional footer extension: decimal pre-footer length immediately followed
 * by its forty ASCII SHA-1 digits. The original footer leaves this slot zero. */
static bool ij_footer(Abstractformat *f,const uint8_t *h,int64_t end,xx_pd_struct *pd)
{
    size_t n=0,i;uint64_t length;uint8_t number[21]={0},digest[20],block[16384];
    xx_sha1_context sha;int64_t at=0;
    while(n<238U && h[18U+n])++n;
    for(i=n;i<238U;++i)if(h[18U+i])return false;
    if(!n)return true;
    if(n<=40U || n>60U)return false;
    xx_rt_memcpy(number,h+18,n-40U);
    if(!ij_decimal(number,n-40U+1U,&length) || length!=(uint64_t)end || !xx_sha1_init(&sha))return false;
    while(at<end) {
        size_t take=end-at>(int64_t)sizeof(block)?sizeof(block):(size_t)(end-at);
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,block,take))return false;
        xx_sha1_update(&sha,block,take);at+=(int64_t)take;
    }
    if(!xx_sha1_final(&sha,digest,sizeof(digest)))return false;
    for(i=0;i<40U;++i) {
        uint8_t c=h[18U+n-40U+i],v=(i&1U)?digest[i/2U]&15U:digest[i/2U]>>4;
        if(c>='A' && c<='F')c=(uint8_t)(c-'A'+'a');
        if(c!=(uint8_t)"0123456789abcdef"[v])return false;
    }
    return !(pd && xx_pd_is_stopped(pd));
}
static void *ij_allocate(ISzAllocPtr allocator,size_t n) { (void)allocator;return xx_mem_alloc(n); }
static void ij_release(ISzAllocPtr allocator,void *p) { (void)allocator;xx_mem_free(p); }
static const ISzAlloc ij_allocator={ij_allocate,ij_release};
/* Existing native SDK decoder, bounded in both input and output per call.
 * The index supplies the missing LZMA-alone size; both legal finish states
 * require exact indexed output and complete compressed-input consumption. */
static bool ij_lzma(const uint8_t *input,size_t packed,uint8_t *output,size_t plain,xx_pd_struct *pd)
{
    CLzmaDec dec;ELzmaStatus status=LZMA_STATUS_NOT_SPECIFIED;size_t at=5;bool ok=false;
    LzmaDec_Construct(&dec);
    if(packed<10U || input[0]>=225U || input[0]%9U+(input[0]/9U)%5U>4U || ij_u32(input+1)>IJ_MEMBER ||
        LzmaDec_AllocateProbs(&dec,input,5U,&ij_allocator)!=SZ_OK)return false;
    dec.dic=output;dec.dicBufSize=plain?plain:1U;LzmaDec_Init(&dec);
    for(;;) {
        SizeT consumed=packed-at>16384U?16384U:packed-at;
        size_t before=dec.dicPos,limit=plain-before>16384U?before+16384U:plain;SRes result;
        if(pd && xx_pd_is_stopped(pd))break;
        result=LzmaDec_DecodeToDic(&dec,limit,input+at,&consumed,limit==plain?LZMA_FINISH_END:LZMA_FINISH_ANY,&status);
        if(consumed>packed-at || result!=SZ_OK)break;
        at+=consumed;
        if(status==LZMA_STATUS_FINISHED_WITH_MARK || status==LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK) {
            ok=dec.dicPos==plain && at==packed;break;
        }
        if(!consumed && dec.dicPos==before)break;
    }
    LzmaDec_FreeProbs(&dec,&ij_allocator);return ok && !(pd && xx_pd_is_stopped(pd));
}
static bool ij_read_all(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd)
{
    ij_member *ctx=(ij_member *)m->context;uint8_t *packed=NULL,*plain=NULL;xx_io_device *destination=NULL;
    uint64_t budget=ij_budget(f),need;size_t at=0;bool ok=false;
    if(!ctx || m->packed_size<0 || m->size<0 || (uint64_t)m->packed_size>IJ_MEMBER || (uint64_t)m->size>IJ_MEMBER)return false;
    need=ctx->resident+(uint64_t)m->packed_size+(uint64_t)m->size+IJ_WORK;
    if(need>budget || (pd && xx_pd_is_stopped(pd)))return false;
    packed=(uint8_t *)xx_mem_alloc((size_t)m->packed_size);plain=(uint8_t *)xx_mem_alloc(m->size?(size_t)m->size:1U);
    if(!packed || !plain || !pm_read(f,m->offset-f->base_address,packed,(size_t)m->packed_size))goto done;
    if(ctx->lzma)ok=ij_lzma(packed,(size_t)m->packed_size,plain,(size_t)m->size,pd);
    else {
        size_t consumed=0;
        destination=xx_io_mem_open(plain,(size_t)m->size);
        if(destination)ok=xx_deflate_unpack_memory_to_device_ex(packed,(size_t)m->packed_size,destination,&consumed,false,pd) &&
            consumed==(uint64_t)m->packed_size && xx_io_tell(destination)==m->size;
    }
    if(!ok)goto done;
    while(out && at<(uint64_t)m->size) {
        size_t n=(uint64_t)m->size-at>16384U?16384U:(size_t)m->size-at;ssize_t wrote;
        if(pd && xx_pd_is_stopped(pd)) { ok=false;break; }
        wrote=xx_io_write(out,plain+at,n);
        if(wrote<=0 || (size_t)wrote>n) { ok=false;break; }at+=(size_t)wrote;
    }
done:
    if(destination)xx_io_close(destination);
    xx_mem_free(packed);xx_mem_free(plain);return ok && !(pd && xx_pd_is_stopped(pd));
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    uint8_t h[256];int64_t total=pm_available(f),directory_end=total-256;uint64_t directory,count,i,names=0,budget=ij_budget(f),maximum=IJ_MEMBER;
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(v && xx_var_get_u64(v)<maximum)maximum=xx_var_get_u64(v);
    if((pd && xx_pd_is_stopped(pd)) || total<512 || !pm_read(f,directory_end,h,256U) || xx_rt_memcmp(h,"CRAP02",6U) ||
        !ij_decimal(h+6,12U,&directory) || directory>(uint64_t)directory_end || ((uint64_t)directory_end-directory)%256U ||
        budget<IJ_WORK+sizeof(pm_stream) || !ij_footer(f,h,directory_end,pd))return false;
    count=((uint64_t)directory_end-directory)/256U;
    if(!count || count>100000U || budget<IJ_WORK+sizeof(pm_stream))return false;
    for(i=0;i<count;++i) {
        uint64_t plain,packed,mtime,offset,resident;size_t n,capacity;char raw[129],*safe,*name;ij_member *ctx;bool lzma;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(directory+i*256U),h,256U) || xx_rt_memcmp(h,"FILE",4U))return false;
        lzma=!xx_rt_memcmp(h+4,"LZ",2U);
        if(!lzma && xx_rt_memcmp(h+4,"ZL",2U))return false;
        for(n=0;n<128U && h[6+n];++n)raw[n]=(char)h[6+n];
        if(!n || n==128U)return false;
        raw[n]=0;
        for(++n;n<128U;++n)if(h[6+n])return false;
        /* Bytes 170..171 are a bounded opaque record flag slot (observed
         * zero or 'C',zero), not padding in the decimal timestamp field. */
        if(!ij_decimal(h+134,12U,&plain) || !ij_decimal(h+146,12U,&packed) || !ij_decimal(h+158,12U,&mtime) || !ij_decimal(h+172,84U,&offset) ||
            plain>maximum || packed>IJ_MEMBER || packed<(lzma?10U:2U) || offset>directory || packed>directory-offset)return false;
        safe=xx_archive_output_name(raw,0,0);if(!safe)return false;
        n=xx_rt_strlen(safe)+16U;capacity=s->capacity==s->count?(s->capacity?s->capacity*2U:8U):s->capacity;
        resident=sizeof(pm_stream)+(uint64_t)capacity*sizeof(pm_member)+(i+1U)*sizeof(ij_member)+names+n;
        if(resident>budget || IJ_WORK>budget-resident) { xx_mem_free(safe);return false; }
        name=(char *)xx_mem_alloc(n);ctx=(ij_member *)xx_mem_alloc(sizeof(*ctx));
        if(!name || !ctx) { xx_mem_free(safe);xx_mem_free(name);xx_mem_free(ctx);return false; }
        (void)xx_rt_snprintf(name,n,"%04u-%s",(unsigned)i,safe);xx_mem_free(safe);
        if(!pm_add(f,s,raw,(int64_t)offset,(int64_t)packed)) { xx_mem_free(name);xx_mem_free(ctx);return false; }
        ctx->header=f->base_address+(int64_t)(directory+i*256U);ctx->mtime=mtime;ctx->resident=0;ctx->lzma=lzma;
        s->items[i].display_name=name;s->items[i].context=ctx;s->items[i].free_context=xx_mem_free;s->items[i].read_all=ij_read_all;
        s->items[i].size=(int64_t)plain;s->items[i].compression_method=lzma?14U:8U;names+=n;
    }
    for(i=0;i<count;++i)((ij_member *)s->items[i].context)->resident=sizeof(pm_stream)+(uint64_t)s->capacity*sizeof(pm_member)+count*sizeof(ij_member)+names;
    s->size=total;return !(pd && xx_pd_is_stopped(pd));
}
static const xx_archive_record *ij_current(Abstractformat *f,xx_archive_record_state *state)
{
    const xx_archive_record *r=pm_current(f,state);
    if(r) {
        pm_stream *s=(pm_stream *)state->internal_state;ij_member *ctx=(ij_member *)s->items[s->index].context;
        state->current_record.header_offset=ctx->header;state->current_record.header_size=256;
        if(!xx_archive_record_set_meta_u64(&state->current_record,XX_META_ID_TIMESTAMP,ctx->mtime))return NULL;
    }
    return r;
}
Abstractformat *xx_installjammer_create(xx_io_device *d,int64_t base)
{
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(f) { pm_init(f,d,base,XX_FILE_TYPE_INSTALLJAMMER,"bin");f->get_current_archive_record=ij_current; }return f;
}
void xx_installjammer_free(Abstractformat *f) { if(f) { xx_format_destroy(f);xx_mem_free(f); } }
#include "../../xx_format_abstract_extractor_adapter.h"
static Abstractformat *ij_open(xx_io_device *d) { return xx_installjammer_create(d,0); }
static const xx_file_type_t ij_types[]={XX_FILE_TYPE_INSTALLJAMMER};
const xx_format_search_desc xx_installjammer_search_desc={ij_types,1U,NULL,0U,ij_open,xx_installjammer_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(installjammer,xx_installjammer_search_desc)
