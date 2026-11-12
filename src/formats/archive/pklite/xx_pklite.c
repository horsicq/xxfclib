/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Independent implementation of the format facts described by Jason Summers:
 * https://entropymine.wordpress.com/2022/01/16/notes-on-pklite-format-part-3/
 * https://entropymine.wordpress.com/2022/05/08/notes-on-pklite-format-part-5/
 * No loader execution, external decompressor, or imported GPL codec code. */
#include "xxfclib/formats/archive/pklite/xx_pklite.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include "../../xx_payload_members.h"

#define PK_INPUT UINT64_C(16777216)
#define PK_CODE UINT64_C(1048576)
#define PK_OUTPUT UINT64_C(2097152)
#define PK_MEMORY UINT64_C(67108864)
#define PK_RELOCS 65535U
#define PK_OVERHEAD (UINT64_C(8192)+sizeof(pm_stream)+sizeof(pm_member))
typedef struct pk_reader {
    const uint8_t *bytes; size_t size,position; uint16_t bits; unsigned remaining;
    bool failed;
} pk_reader;
typedef struct pk_pin { size_t input_size; uint64_t resident; uint8_t sha256[32]; } pk_pin;
typedef struct pk_buffer { uint8_t *bytes; size_t size,capacity; } pk_buffer;
static uint16_t pk_u16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static void pk_put16(uint8_t *p,uint16_t n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static int64_t pk_signed16(uint16_t n) { return n<32768U?(int64_t)n:(int64_t)n-65536; }
static uint64_t pk_budget(Abstractformat *f)
{
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t budget=v?xx_var_get_u64(v):PK_MEMORY;return budget<PK_MEMORY?budget:PK_MEMORY;
}
static bool pk_load_bits(pk_reader *r)
{
    if(r->position>r->size || r->size-r->position<2U) {r->failed=true;return false;}
    r->bits=pk_u16(r->bytes+r->position);r->position+=2U;r->remaining=16U;return true;
}
/* Refill eagerly after the sixteenth bit, before a following literal byte. */
static unsigned pk_bit(pk_reader *r)
{
    unsigned bit;
    if(r->failed || !r->remaining) {r->failed=true;return 0U;}
    bit=r->bits&1U;r->bits>>=1;--r->remaining;
    if(!r->remaining)(void)pk_load_bits(r);
    return bit;
}
static unsigned pk_byte(pk_reader *r)
{
    if(r->failed || r->position>=r->size) {r->failed=true;return 0U;}
    return r->bytes[r->position++];
}
/* Prefix codes are represented as the documented bit strings, rather than
 * importing a decoder's tree, lookup table or implementation. */
static unsigned pk_length(pk_reader *r,bool large)
{
    static const char *const small[]={"010","00","100","101","1100","1101","1110","1111","011"};
    static const char *const big[]={"10","11","000","0010","0011","0100","01010","01011","01100","011010","011011","0111010","0111011","0111100","01111010","01111011","01111100","011111010","011111011","011111100","011111101","011111110","011111111","011100"};
    const char *const *book=large?big:small;unsigned count=large?24U:9U,length=0U,code=0U;
    while(length<9U && !r->failed) {
        unsigned i;code=(code<<1)|pk_bit(r);++length;
        for(i=0;i<count;++i) {
            const char *p=book[i];unsigned value=0U,n=0U;
            while(p[n]) {value=(value<<1)|(unsigned)(p[n]-'0');++n;}
            if(n==length && value==code) {
                if(i==count-1U)return 0U;
                return i+2U;
            }
        }
    }
    r->failed=true;return 0U;
}
static unsigned pk_distance_high(pk_reader *r)
{
    unsigned value=0U,n;
    if(pk_bit(r))return 0U;
    /* After the zero flag, canonical widths are 3,4,5,6 bits for
     * values 1..2,3..6,7..13,14..31 respectively. */
    for(n=1U;n<=6U && !r->failed;++n) {
        value=(value<<1)|pk_bit(r);
        if(n==3U && value<2U)return value+1U;
        if(n==4U && value>=4U && value<=7U)return value-1U;
        if(n==5U && value>=16U && value<=22U)return value-9U;
        if(n==6U && value>=46U && value<=63U)return value-32U;
    }
    r->failed=true;return 0U;
}
static bool pk_reserve(pk_buffer *b,size_t needed,uint64_t maximum,uint64_t available)
{
    size_t capacity;void *p;
    if((uint64_t)needed>maximum || (uint64_t)needed>available)return false;
    if(needed<=b->capacity)return true;
    capacity=b->capacity?b->capacity:4096U;
    while(capacity<needed) {
        if((uint64_t)capacity>=maximum/2U) {capacity=(size_t)maximum;break;}
        capacity*=2U;
    }
    if((uint64_t)capacity>available)capacity=needed;
    p=xx_mem_realloc(b->bytes,capacity);if(!p)return false;
    b->bytes=(uint8_t *)p;b->capacity=capacity;return true;
}
static bool pk_decode(pk_reader *r,pk_buffer *code,bool large,uint64_t available,xx_pd_struct *pd)
{
    uint64_t operations=0;
    if(!pk_load_bits(r))return false;
    for(;;) {
        unsigned length,low,high=0U,distance;size_t i;
        if(xx_pd_is_stopped(pd) || ++operations>PK_CODE*2U)return false;
        if(!pk_bit(r)) {
            unsigned literal=pk_byte(r);
            if(r->failed || !pk_reserve(code,code->size+1U,PK_CODE,available))return false;
            code->bytes[code->size++]=(uint8_t)literal;continue;
        }
        length=pk_length(r,large);if(r->failed)return false;
        if(!length) {
            unsigned extra=pk_byte(r);if(r->failed)return false;
            if(extra==255U)return code->size!=0U;
            if(extra==254U && large)continue;
            if(extra>=253U)return false; /* Uncompressed regions unsupported. */
            length=extra+(large?25U:10U);
        }
        if(length!=2U)high=pk_distance_high(r);
        low=pk_byte(r);distance=(high<<8)|low;
        if(r->failed || !distance || distance>8191U || distance>code->size ||
           !pk_reserve(code,code->size+length,PK_CODE,available))return false;
        for(i=0;i<length;++i) {code->bytes[code->size]=code->bytes[code->size-distance];++code->size;}
    }
}
static bool pk_intro(const uint8_t *b,size_t n,size_t at,bool *large)
{
    static const uint8_t pattern[]={0xb8,0,0,0xba,0,0,0x8c,0xdb,3,0xd8,0x3b,0x1e,2,0,0x73,0x1d,0x83,0xeb,0x20,0xfa,0x8e,0xd3,0xbc,0,2,0xfb,0x83,0xeb,0,0x8e,0xc3,0x53,0xb9,0,0,0x33};
    size_t i;unsigned cx;
    if(at>n || sizeof(pattern)>n-at)return false;
    for(i=0;i<sizeof(pattern);++i) {
        if(i==1U || i==2U || i==4U || i==5U || i==28U || i==33U || i==34U)continue;
        if(b[at+i]!=pattern[i])return false;
    }
    cx=pk_u16(b+at+33U);
    if(cx==0xc3U || cx==0xc4U)*large=false;
    else if(cx==0x122U || cx==0x123U)*large=true;
    else return false;
    return true;
}
static bool pk_tables(const uint8_t *b,size_t n,size_t entry,bool large,size_t *compressed)
{
    static const uint8_t tail[]={1,2,0,0,3,4,5,6,0,0,0,0,0,0,0,0,7,8,9,10,11,12,13};
    size_t at,end,found=0U,matches=0U;
    if(entry>n || n-entry<336U+sizeof(tail))return false;
    end=n-sizeof(tail);if(end>entry+850U)end=entry+850U;
    for(at=entry+336U;at<=end;++at) {
        if(b[at-1U]!=(large?24U:9U) || xx_rt_memcmp(b+at,tail,sizeof(tail)))continue;
        found=(at+sizeof(tail)+15U)&~(size_t)15U;++matches;
    }
    if(matches!=1U || found>=n)return false;
    *compressed=found;return true;
}
static bool pk_relocations(pk_reader *r,pk_buffer *relocations,uint64_t available,xx_pd_struct *pd)
{
    unsigned total=0U;
    while(!r->failed && !xx_pd_is_stopped(pd)) {
        unsigned count=pk_byte(r),segment,i;
        if(r->failed)return false;
        if(!count)return true;
        segment=pk_byte(r);segment|=pk_byte(r)<<8;
        if(r->failed || count>PK_RELOCS-total || !pk_reserve(relocations,(size_t)(total+count)*4U,(uint64_t)PK_RELOCS*4U,available))return false;
        for(i=0;i<count;++i) {
            unsigned offset=pk_byte(r);offset|=pk_byte(r)<<8;
            if(r->failed)return false;
            pk_put16(relocations->bytes+relocations->size,(uint16_t)offset);
            pk_put16(relocations->bytes+relocations->size+2U,(uint16_t)segment);relocations->size+=4U;
        }
        total+=count;
    }
    return false;
}
static bool pk_saved_header(const uint8_t *input,size_t size,size_t outer_header,const pk_buffer *code,const pk_buffer *relocations,
                            const uint8_t footer[8],size_t *copy_at,size_t *copy_size,size_t *header_size,size_t *reloc_at)
{
    size_t at=(size_t)pk_u16(input+24U)+4U*pk_u16(input+6U),length,physical;unsigned last,pages;
    const uint8_t *p;
    if(at>outer_header || outer_header-at<26U || at>size)return false;
    p=input+at;*reloc_at=pk_u16(p+22U);
    if(*reloc_at<28U || *reloc_at-2U>outer_header-at)return false;
    length=*reloc_at-2U;*header_size=(size_t)pk_u16(p+6U)*16U;
    last=pk_u16(p);pages=pk_u16(p+2U);
    if(last>511U || !pages || *header_size<*reloc_at || relocations->size>*header_size-*reloc_at ||
       pk_u16(p+4U)!=relocations->size/4U || pk_u16(p+24U)!=0U)return false;
    physical=(size_t)(pages-1U)*512U+(last?last:512U);
    if(physical<*header_size || physical-*header_size!=code->size || physical>PK_OUTPUT ||
       pk_u16(p+12U)!=pk_u16(footer) || pk_u16(p+14U)!=pk_u16(footer+2U) ||
       pk_u16(p+20U)!=pk_u16(footer+4U) || pk_u16(p+18U)!=pk_u16(footer+6U))return false;
    *copy_at=at;*copy_size=length;return true;
}
static bool pk_copy(Abstractformat *f,pm_member *m,xx_io_device *destination,xx_pd_struct *pd)
{
    pk_pin *pin=(pk_pin *)m->context;xx_sha256_context hash;uint8_t scratch[4096],digest[32];size_t at=0U;
    if(!pin || xx_pd_is_stopped(pd) || pm_available(f)!=(int64_t)pin->input_size ||
       pin->resident>pk_budget(f) || !xx_sha256_init(&hash))return false;
    while(at<pin->input_size) {
        size_t take=pin->input_size-at;if(take>sizeof(scratch))take=sizeof(scratch);
        if(xx_pd_is_stopped(pd) || !pm_read(f,(int64_t)at,scratch,take))return false;
        xx_sha256_update(&hash,scratch,take);at+=take;
    }
    if(!xx_sha256_final(&hash,digest,sizeof(digest)) || xx_rt_memcmp(digest,pin->sha256,sizeof(digest)))return false;
    at=0U;
    while(at<(uint64_t)m->size) {
        size_t take=(size_t)m->size-at,done=0U;if(take>65536U)take=65536U;
        if(xx_pd_is_stopped(pd))return false;
        while(destination && done<take) {
            ssize_t count;
            if(xx_pd_is_stopped(pd))return false;
            count=xx_io_write(destination,m->memory+at+done,take-done);
            if(count<=0 || (size_t)count>take-done)return false;
            done+=(size_t)count;
        }
        at+=take;
    }
    return !xx_pd_is_stopped(pd);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    int64_t span=pm_available(f),entry_signed,entry_ip;size_t size,outer_header,compressed,at,header_size,reloc_at,copy_at=0U,copy_size=0U,output_size;
    uint64_t budget,remaining,maximum=PK_OUTPUT;uint8_t *input=NULL,*output=NULL,footer[8];pk_buffer code={0},relocations={0};pk_reader r;
    bool large,saved,ok=false;pm_member *member;pk_pin *pin=NULL;const xx_var *v;unsigned ss,sp,cs,ip,minmem,maxmem;
    if(!f || f->is_mapped || span<512 || (uint64_t)span>PK_INPUT || xx_pd_is_stopped(pd))return false;
    size=(size_t)span;budget=pk_budget(f);
    if(budget<=PK_OVERHEAD+sizeof(pk_pin)+(uint64_t)size)return false;
    input=(uint8_t *)xx_mem_alloc(size);if(!input)goto done;
    for(at=0U;at<size;) {
        size_t take=size-at;if(take>65536U)take=65536U;
        if(xx_pd_is_stopped(pd) || !pm_read(f,(int64_t)at,input+at,take))goto done;
        at+=take;
    }
    if(input[0]!='M' || input[1]!='Z' || pk_u16(input+6U)>1U || pk_u16(input+26U)!=0U)goto done;
    outer_header=(size_t)pk_u16(input+8U)*16U;
    if(outer_header<80U || outer_header>=size || pk_u16(input+24U)<28U ||
       (size_t)pk_u16(input+24U)+4U*pk_u16(input+6U)>outer_header)goto done;
    entry_signed=(int64_t)outer_header+pk_signed16(pk_u16(input+22U))*16+pk_u16(input+20U);
    if(entry_signed!=(int64_t)outer_header || !pk_intro(input,size,outer_header,&large) ||
       !pk_tables(input,size,outer_header,large,&compressed))goto done;
    remaining=budget-PK_OVERHEAD-size-sizeof(pk_pin);
    xx_mem_zero(&r,sizeof(r));r.bytes=input;r.size=size;r.position=compressed;
    if(!pk_decode(&r,&code,large,remaining,pd) || code.capacity>remaining)goto done;
    remaining-=code.capacity;
    if(!pk_relocations(&r,&relocations,remaining,pd) || relocations.capacity>remaining || r.position>size || size-r.position<8U)goto done;
    remaining-=relocations.capacity;
    xx_mem_copy(footer,input+r.position,sizeof(footer));r.position+=sizeof(footer);
    for(at=r.position;at<size;++at)if(input[at])goto done;
    ss=pk_u16(footer);sp=pk_u16(footer+2U);cs=pk_u16(footer+4U);ip=pk_u16(footer+6U);
    entry_ip=pk_signed16((uint16_t)cs)*16+ip;
    if(entry_ip<0 || (uint64_t)entry_ip>=code.size)goto done;
    for(at=0U;at<relocations.size;at+=4U) {
        uint64_t target=(uint64_t)pk_u16(relocations.bytes+at+2U)*16U+pk_u16(relocations.bytes+at);
        if(target+2U>code.size)goto done;
    }
    saved=pk_saved_header(input,size,outer_header,&code,&relocations,footer,&copy_at,&copy_size,&header_size,&reloc_at);
    if(saved) {minmem=pk_u16(input+copy_at+8U);maxmem=pk_u16(input+copy_at+10U);}
    else {
        int64_t allocation=(int64_t)pk_u16(input+outer_header+1U)*16+256-(int64_t)code.size;
        reloc_at=28U;header_size=(reloc_at+relocations.size+15U)&~(size_t)15U;
        minmem=allocation>=0?(unsigned)((allocation+15)/16):pk_u16(input+10U);
        maxmem=pk_u16(input+12U);if(!maxmem)maxmem=65535U;if(maxmem<minmem)maxmem=minmem;
    }
    if(header_size>PK_OUTPUT || code.size>PK_OUTPUT-header_size || header_size/16U>65535U || minmem>65535U ||
       (uint64_t)ss*16U+sp>(uint64_t)code.size+(uint64_t)minmem*16U)goto done;
    output_size=header_size+code.size;
    v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MAX_MEMBER_SIZE);if(v && xx_var_get_u64(v)<maximum)maximum=xx_var_get_u64(v);
    if((uint64_t)output_size>maximum || (uint64_t)output_size>remaining)goto done;
    output=(uint8_t *)xx_mem_calloc(1U,output_size);if(!output)goto done;
    output[0]='M';output[1]='Z';
    if(saved)xx_mem_copy(output+2U,input+copy_at,copy_size);
    else {
        pk_put16(output+2U,(uint16_t)(output_size%512U));pk_put16(output+4U,(uint16_t)((output_size+511U)/512U));
        pk_put16(output+6U,(uint16_t)(relocations.size/4U));pk_put16(output+8U,(uint16_t)(header_size/16U));
        pk_put16(output+10U,(uint16_t)minmem);pk_put16(output+12U,(uint16_t)maxmem);pk_put16(output+14U,(uint16_t)ss);
        pk_put16(output+16U,(uint16_t)sp);pk_put16(output+20U,(uint16_t)ip);pk_put16(output+22U,(uint16_t)cs);pk_put16(output+24U,(uint16_t)reloc_at);
    }
    if(relocations.size)xx_mem_copy(output+reloc_at,relocations.bytes,relocations.size);
    xx_mem_copy(output+header_size,code.bytes,code.size);
    pin=(pk_pin *)xx_mem_alloc(sizeof(*pin));if(!pin || !xx_sha256_memory(input,size,pin->sha256))goto done;
    pin->input_size=size;pin->resident=PK_OVERHEAD+sizeof(*pin)+output_size;
    if(!pm_add(f,s,"decoded.exe",0,span))goto done;
    member=&s->items[s->count-1U];xx_rt_snprintf(member->name,sizeof(member->name),"decoded.exe");
    member->size=(int64_t)output_size;member->compression_method=UINT16_MAX;member->memory=output;output=NULL;
    member->context=pin;member->free_context=xx_mem_free;member->read_all=pk_copy;pin=NULL;
    s->size=span;ok=!xx_pd_is_stopped(pd);
done:
    xx_mem_free(input);xx_mem_free(code.bytes);xx_mem_free(relocations.bytes);xx_mem_free(output);xx_mem_free(pin);
    return ok;
}
static void pk_destroy(Abstractformat *f) {if(f)xx_format_cleanup_extra_parameters(f);}
void xx_pklite_init(xx_pklite *reader,xx_io_device *device,int64_t base)
{
    if(!reader)return;
    xx_mem_zero(reader,sizeof(*reader));pm_init(&reader->format,device,base,XX_FILE_TYPE_PKLITE,"exe");reader->format.destroy=pk_destroy;
}
void xx_pklite_destroy(xx_pklite *reader) {if(reader)pk_destroy(&reader->format);}
xx_pklite *xx_pklite_create(xx_io_device *device,int64_t base)
{ xx_pklite *r=(xx_pklite *)xx_mem_alloc(sizeof(*r));if(r)xx_pklite_init(r,device,base);return r; }
void xx_pklite_free(xx_pklite *reader) {if(reader){xx_pklite_destroy(reader);xx_mem_free(reader);}}
