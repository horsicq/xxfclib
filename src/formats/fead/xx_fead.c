/* Copyright (c) 2026 hors<horsicq@gmail.com>; SPDX-License-Identifier: MIT
 * Independently derived NOS AG/FEAD payload grammar. No installer code or
 * archive-provided DLL is executed or loaded. See tests/fead/FORMAT.md. */
#include "xxfclib/formats/fead/xx_fead.h"
#include "fead_restore.h"
#include "../ue2_indexed.h"
#include "../../algo/lzma/xx_lzma_internal.h"
#include "fead_bcj2.h"
#define FI_TYPE ((xx_file_type_t)2768)
#define FI_LIMIT (UINT64_C(512)*1024*1024)
#define FI_DATA_LIMIT (UINT64_C(256)*1024*1024)
#define FI_PACKED_LIMIT (UINT64_C(128)*1024*1024)
#define FI_META_LIMIT (UINT64_C(16)*1024*1024)
#define FI_CODEC_RESERVE (UINT64_C(4)*1024*1024)
typedef struct fi_cursor { const uint8_t *bytes; size_t size, at; } fi_cursor;
typedef struct fi_index {
    ue2_index records;
    fead_outer_member *outer;
    size_t outer_count;
    uint8_t *meta[6]; size_t meta_size[6];
    fead_resource *resources; size_t resource_count;
    fead_action *actions; size_t action_count;
    uint32_t packed[4], output[4]; unsigned exponent[3];
    int64_t stream;
    uint64_t owned;
    uint8_t **payloads;
    bool restored;
    xx_io_device *source_device;
    int64_t source_base;
} fi_index;
typedef struct fi_format { Abstractformat format; fi_index *index; uint64_t generation; } fi_format;
typedef struct fi_state {ue2_state record;uint64_t generation;} fi_state;
static bool fi_fail(xx_pd_struct *pd,const char *s) {if(pd)xx_pd_set_error(pd,1,s);return false;}
static uint64_t fi_budget(Abstractformat *f,const xx_list_s *opts) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,opts,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v?xx_var_get_u64(v):FI_LIMIT;return n<FI_LIMIT?n:FI_LIMIT;
}
static bool fi_room(uint64_t owned,uint64_t workspace,uint64_t budget,uint64_t extra) {
    return owned<=budget&&workspace<=budget-owned&&extra<=budget-owned-workspace;
}
static uint8_t fi_unmask(uint8_t b) {
    b=(uint8_t)((b>>1)|(b<<7));b=(uint8_t)(b+0xb2);return (uint8_t)((b>>1)|(b<<7));
}
static bool fi_read_at_pd(xx_io_device *d,int64_t at,void *out,size_t n,xx_pd_struct *pd) {
    size_t done=0;
    if(!ue2_range(xx_io_total_size(d),at,(int64_t)n)||xx_io_seek64(d,at,XX_RT_SEEK_SET))return false;
    while(done<n) {size_t take=n-done;ssize_t got;if(pd&&xx_pd_is_stopped(pd))return false;if(take>65536)take=65536;got=xx_io_read(d,(uint8_t*)out+done,take);if(got<=0||(size_t)got>take)return false;done+=(size_t)got;}
    return true;
}
static bool fi_read_at(xx_io_device *d,int64_t at,void *out,size_t n) {return fi_read_at_pd(d,at,out,n,NULL);}
static bool fi_masked_read_pd(xx_io_device *d,int64_t at,uint8_t *out,size_t n,xx_pd_struct *pd) {
    size_t i;if(!fi_read_at_pd(d,at,out,n,pd))return false;
    for(i=0;i<n;i++){if(!(i&65535)&&pd&&xx_pd_is_stopped(pd))return false;out[i]=fi_unmask(out[i]);}return true;
}
static bool fi_masked_read(xx_io_device *d,int64_t at,uint8_t *out,size_t n) {return fi_masked_read_pd(d,at,out,n,NULL);}
/* Exact PE overlay or raw NOS archive. Never scan arbitrary child bytes. */
static int64_t fi_header(xx_io_device *d,int64_t base) {
    uint8_t h[64],pe[24],section[40];uint32_t peoff;unsigned count,optional,i;
    int64_t total=xx_io_total_size(d),at=base,end;
    if(!ue2_range(total,base,18)||!fi_read_at(d,base,h,18))return -1;
    if(h[0]=='M'&&h[1]=='Z') {
        if(!fi_read_at(d,base,h,64))return -1;peoff=ue2_u32(h+60);
        if(peoff>16U*1024U*1024U||!ue2_range(total-base,peoff,24)||!fi_read_at(d,base+peoff,pe,24)||xx_rt_memcmp(pe,"PE\0\0",4))return -1;
        count=ue2_u16(pe+6);optional=ue2_u16(pe+20);
        if(!count||count>96||optional<64||optional>4096||!ue2_range(total-base,(int64_t)peoff+24,(int64_t)optional+count*40)||!fi_read_at(d,base+peoff+24+60,h,4))return -1;
        end=ue2_u32(h);if(end>total-base)return -1;
        for(i=0;i<count;i++) {
            uint64_t finish;if(!fi_read_at(d,base+peoff+24+optional+i*40,section,40))return -1;
            finish=(uint64_t)ue2_u32(section+20)+ue2_u32(section+16);if(finish>(uint64_t)(total-base))return -1;
            if((int64_t)finish>end)end=(int64_t)finish;
        }
        at=base+end;if(!fi_read_at(d,at,h,18))return -1;
    }
    if(!xx_rt_memcmp(h,"NOS_PO",6)) {if(h[6]!=3||!ue2_range(total,at,25))return -1;at+=7;}
    if(!fi_masked_read(d,at,h,6)||xx_rt_memcmp(h,"NOS AG",6))return -1;
    return at;
}
static const uint8_t *fi_take(fi_cursor *c,size_t n) {
    const uint8_t *p;if(n>c->size-c->at)return NULL;p=c->bytes+c->at;c->at+=n;return p;
}
static bool fi_byte(fi_cursor *c,unsigned *n) {const uint8_t *p=fi_take(c,1);if(!p)return false;*n=*p;return true;}
static bool fi_dword(fi_cursor *c,uint32_t *n) {const uint8_t *p=fi_take(c,4);if(!p)return false;*n=ue2_u32(p);return true;}
static bool fi_lzma(const uint8_t *input,size_t available,uint8_t *output,size_t size,unsigned exponent,size_t *consumed,xx_pd_struct *pd) {
    lzma_props props={3,0,2,0};lzma_range_dec rd;size_t written=0;bool ok;
    *consumed=0;if(!size)return true;if(exponent<12||exponent>26)return false;props.dict_size=UINT32_C(1)<<exponent;
    if(!lzma_rd_init(&rd,NULL,input,available,(int64_t)available))return false;
    ok=xx_lzma_decompress_stream(&rd,&props,(int64_t)size,NULL,output,size,&written,pd)&&written==size&&!rd.error&&rd.code==0;
    *consumed=rd.mem_pos-(rd.ibuf_len-rd.ibuf_pos);lzma_rd_free(&rd);return ok&&*consumed<=available;
}
static bool fi_select(fi_cursor *main,fi_cursor *out,fi_index *ix,unsigned *next) {
    unsigned selector;if(!fi_byte(main,&selector))return false;
    if(!selector){*out=*main;return false;} /* inline sections intentionally unsupported until bounded grammar proof */
    if(*next>=6)return false;out->bytes=ix->meta[*next];out->size=ix->meta_size[*next];out->at=0;(*next)++;return true;
}
static char *fi_name(fi_cursor *c,unsigned encoding,uint64_t owned,uint64_t workspace,uint64_t budget) {
    unsigned units=0,i;const uint8_t *p;char *out;
    if(encoding!=1&&encoding!=3)return NULL;
    p=fi_take(c,encoding==1?1:2);if(!p)return NULL;units=encoding==1?*p:ue2_u16(p);
    if(!units||units>8192||!fi_room(owned,workspace,budget,(uint64_t)units+1)||!(p=fi_take(c,units)))return NULL;
    out=(char*)xx_mem_alloc((size_t)units+1);if(!out)return NULL;
    for(i=0;i<units;i++){if(p[i]<32||p[i]>=128){xx_mem_free(out);return NULL;}out[i]=p[i]=='\\'?'/':(char)p[i];}out[units]=0;
    if(!ue2_safe_name(out)){xx_mem_free(out);return NULL;}return out;
}
static void fi_index_free(fi_index *ix) {
    size_t i;if(!ix)return;
    if(ix->outer)for(i=0;i<ix->outer_count;i++)xx_str_free((char*)ix->outer[i].name);
    for(i=0;i<6;i++)xx_mem_free(ix->meta[i]);
    if(ix->payloads)for(i=0;i<ix->records.count;i++)xx_mem_free(ix->payloads[i]);
    xx_mem_free(ix->payloads);xx_mem_free(ix->outer);xx_mem_free(ix->resources);xx_mem_free(ix->actions);ue2_index_free(&ix->records);
}
static bool fi_resource_table(fi_index *ix,fi_cursor *c,uint64_t workspace,uint64_t budget) {
    uint32_t sum,total,n;const uint8_t *p;size_t count=0,action=0,i;
    fi_cursor scan=*c;if(!fi_dword(&scan,&total)||total>FI_META_LIMIT)return false;sum=0;
    while(sum<total) {if(!fi_dword(&scan,&n)||n<8||n>total-sum||!(p=fi_take(&scan,n))||++count>100000)return false;sum+=n;}
    if(sum!=total||scan.at!=scan.size)return false;
    if(!fi_room(ix->owned,workspace,budget,count*sizeof(*ix->resources)))return false;
    ix->resources=(fead_resource*)xx_mem_calloc(count,sizeof(*ix->resources));if(!ix->resources)return false;
    ix->resource_count=count;ix->owned+=count*sizeof(*ix->resources);
    if(!fi_dword(c,&total))return false;
    for(i=0;i<count;i++) {
        fead_resource *r=&ix->resources[i];if(!fi_dword(c,&n)||!(p=fi_take(c,n)))return false;
        r->tag=ue2_u32(p);r->id=ue2_u32(p+4);r->data=p+8;r->size=n-8;
        if(r->tag==UINT32_C(0x41504f53)) {
            size_t j;if(action||!r->size||(r->size%8))return false;action=r->size/8;
            if(!fi_room(ix->owned,workspace,budget,action*sizeof(*ix->actions)))return false;
            ix->actions=(fead_action*)xx_mem_calloc(action,sizeof(*ix->actions));if(!ix->actions)return false;
            ix->action_count=action;ix->owned+=action*sizeof(*ix->actions);
            for(j=0;j<action;j++) {
                uint32_t start=ue2_u32(r->data+j*8),end=j+1<action?ue2_u32(r->data+(j+1)*8):ix->output[3];
                unsigned type=ue2_u32(r->data+j*8+4);
                if(start>end||end>ix->output[3]||(!j&&start)||(type!=0&&type!=2&&type!=5&&type!=7&&type!=18&&type!=29))return false;
                ix->actions[j].end=end;ix->actions[j].type=type;
            }
        }
    }
    return action>0;
}
static fi_index *fi_parse(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd) {
    int64_t saved=xx_io_tell(f->device),total=xx_io_total_size(f->device),at;
    uint8_t h[18],*tail=NULL;size_t tail_size,i,used;fi_index *ix=NULL;fi_cursor c,section;uint32_t n,sum;unsigned next=0,tag,encoding;
    uint64_t budget=fi_budget(f,opts),owned=0;
    if((pd&&xx_pd_is_stopped(pd))||(at=fi_header(f->device,f->base_address))<0||!fi_masked_read(f->device,at,h,18))goto bad;
    n=ue2_u32(h+10);if(n>FI_PACKED_LIMIT||!ue2_range(total,at+18,n))goto bad;
    tail_size=(size_t)(total-at-18-n);if(tail_size<40||tail_size>FI_META_LIMIT||tail_size>budget||FI_CODEC_RESERVE+(UINT64_C(1)<<20)>budget-tail_size)goto bad;
    tail=(uint8_t*)xx_mem_alloc(tail_size);ix=(fi_index*)xx_mem_calloc(1,sizeof(*ix));if(!tail||!ix||!fi_masked_read_pd(f->device,at+18+n,tail,tail_size,pd))goto bad;
    ix->stream=at+18;ix->records.size=total-f->base_address;ix->owned=sizeof(*ix);ix->source_device=f->device;ix->source_base=f->base_address;c.bytes=tail;c.size=tail_size;c.at=0;
    if(!fi_byte(&c,&tag)||tag!=1||!fi_byte(&c,&tag)||tag!=3)goto bad;
    for(i=0;i<6;i++) {
        if(!fi_dword(&c,&n)||n>FI_META_LIMIT||owned>budget||n>budget-owned||tail_size+FI_CODEC_RESERVE+(UINT64_C(1)<<20)>budget-owned-n)goto bad;
        ix->meta_size[i]=n;ix->meta[i]=(uint8_t*)xx_mem_alloc(n?n:1);if(!ix->meta[i])goto bad;
        if(!fi_lzma(c.bytes+c.at,c.size-c.at,ix->meta[i],n,20,&used,pd)||!fi_take(&c,used))goto bad;
        owned+=n;ix->owned+=n;
    }
    if(!fi_byte(&c,&tag)||tag!=4||!fi_byte(&c,&tag)||tag!=6)goto bad;
    sum=0;for(i=0;i<4;i++){if(!fi_dword(&c,&ix->packed[i])||ix->packed[i]>FI_PACKED_LIMIT-sum)goto bad;sum+=ix->packed[i];}
    if(sum!=ue2_u32(h+10)||!fi_byte(&c,&tag)||tag!=7)goto bad;
    for(i=0;i<3;i++)if(!fi_byte(&c,&ix->exponent[i])||ix->exponent[i]<12||ix->exponent[i]>26)goto bad;
    sum=0;for(i=0;i<4;i++){if(!fi_dword(&c,&ix->output[i])||ix->output[i]>FI_DATA_LIMIT)goto bad;if(i<3){if(ix->output[i]>FI_DATA_LIMIT-sum)goto bad;sum+=ix->output[i];}}
    if(sum!=ix->output[3]||!sum||!fi_byte(&c,&tag)||tag||!fi_byte(&c,&tag)||tag!=5||!fi_dword(&c,&n)||!n||n>100000)goto bad;
    if(!fi_room(ix->owned,tail_size,budget,n*sizeof(*ix->outer)))goto bad;
    ix->outer_count=n;ix->outer=(fead_outer_member*)xx_mem_calloc(n,sizeof(*ix->outer));if(!ix->outer)goto bad;ix->owned+=n*sizeof(*ix->outer);
    if(!fi_select(&c,&section,ix,&next)||!fi_byte(&section,&encoding))goto bad;
    if(encoding==9){if(!fi_take(&section,4)||!fi_byte(&section,&encoding))goto bad;}
    for(i=0;i<n;i++){ix->outer[i].name=fi_name(&section,encoding,ix->owned,tail_size,budget);if(!ix->outer[i].name)goto bad;ix->owned+=xx_rt_strlen(ix->outer[i].name)+1;}
    if(section.at!=section.size||!fi_select(&c,&section,ix,&next))goto bad;
    for(i=0;i<n;i++)if(!fi_dword(&section,&ix->outer[i].flags)||(ix->outer[i].flags&UINT32_C(0x01000000)))goto bad;
    if(section.at!=section.size||!fi_select(&c,&section,ix,&next))goto bad;
    for(i=0;i<n;i++)if(!(ix->outer[i].flags&0x10)&&!fi_dword(&section,&ix->outer[i].size))goto bad;
    if(section.at!=section.size||!fi_select(&c,&section,ix,&next))goto bad;
    for(i=0;i<n;i++){const uint8_t *p=fi_take(&section,8);if(!p)goto bad;ix->outer[i].filetime=ue2_u64(p);}
    if(section.at!=section.size||!fi_select(&c,&section,ix,&next))goto bad;
    for(i=1;i<n;i++){uint32_t delta;if(!fi_dword(&section,&delta)||delta)goto bad;}
    if(section.at!=section.size||!fi_byte(&c,&tag)||tag!=0x10||!fi_select(&c,&section,ix,&next)||!fi_resource_table(ix,&section,tail_size,budget)||next!=6)goto bad;
    for(i=0;i<n;i++){
        size_t old=ix->records.capacity,new_capacity=old;uint64_t name_size=xx_rt_strlen(ix->outer[i].name)+1;
        if(ix->records.count==old)new_capacity=old?old*2:32;
        if(!fi_room(ix->owned,tail_size,budget,name_size+(new_capacity!=old?new_capacity*sizeof(ue2_member):0))||!ue2_add(&ix->records,ix->outer[i].name,0,ix->outer[i].size,ix->outer[i].filetime))goto bad;
        ix->owned+=name_size+(new_capacity-old)*sizeof(ue2_member);ix->records.members[i].is_folder=(ix->outer[i].flags&0x10)!=0;
    }
    if(ix->owned>budget)goto bad;xx_mem_free(tail);if(saved>=0)xx_io_seek64(f->device,saved,XX_RT_SEEK_SET);return ix;
bad:
    xx_mem_free(tail);fi_index_free(ix);if(saved>=0)xx_io_seek64(f->device,saved,XX_RT_SEEK_SET);return NULL;
}
typedef struct fi_emit_state {fi_index *index;size_t capacity;uint64_t *used,limit;xx_pd_struct *pd;} fi_emit_state;
static bool fi_charge(uint64_t *used,uint64_t limit,uint64_t n) {if(*used>limit||n>limit-*used)return false;*used+=n;return true;}
static bool fi_emit(void *user,const char *name,const uint8_t *bytes,size_t length,uint64_t time,uint32_t flags,bool directory) {
    fi_emit_state *s=(fi_emit_state*)user;fi_index *ix=s->index;size_t nc=ix->records.capacity,pc=s->capacity;
    uint64_t allocation;uint8_t *owned=NULL,**payloads;size_t name_length;
    (void)flags;
    if(!ue2_safe_name(name)||(!directory&&length&&!bytes)||ix->records.count>=100000)return false;
    if(ix->records.count==nc)nc=nc?nc*2:32;
    if(ix->records.count==pc)pc=pc?pc*2:32;
    name_length=xx_rt_strlen(name)+1;
    allocation=(uint64_t)(nc-ix->records.capacity)*sizeof(ue2_member)+(uint64_t)(pc-s->capacity)*sizeof(uint8_t*)+name_length+(directory?0:length);
    /* realloc may allocate the complete new block while the old block is live. */
    {uint64_t transient=(nc!=ix->records.capacity?ix->records.capacity*sizeof(ue2_member):0)+(pc!=s->capacity?s->capacity*sizeof(uint8_t*):0);
     if(!fi_room(*s->used,transient,s->limit,allocation)||!fi_charge(s->used,s->limit,allocation))return false;}
    if(pc!=s->capacity){payloads=(uint8_t**)xx_mem_realloc(ix->payloads,pc*sizeof(*payloads));if(!payloads)return false;ix->payloads=payloads;s->capacity=pc;}
    if(!directory&&length){size_t done=0;owned=(uint8_t*)xx_mem_alloc(length);if(!owned)return false;while(done<length){size_t n=length-done;if(n>65536)n=65536;if(s->pd&&xx_pd_is_stopped(s->pd)){xx_mem_free(owned);return false;}xx_rt_memcpy(owned+done,bytes+done,n);done+=n;}}
    if(!ue2_add(&ix->records,name,(int64_t)ix->records.count,directory?0:(int64_t)length,time)){xx_mem_free(owned);return false;}
    ix->records.members[ix->records.count-1].is_folder=directory;ix->payloads[ix->records.count-1]=owned;ix->owned+=allocation;return true;
}
static bool fi_restore(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd) {
    fi_index *ix=((fi_format*)f)->index,*flat=NULL;fi_emit_state emitter;fead_restore_context context;
    uint8_t *streams[4]={NULL,NULL,NULL,NULL},*packed=NULL,*main=NULL;size_t i,consumed;
    uint64_t used=ix->owned,limit=fi_budget(f,opts);int64_t saved=xx_io_tell(f->device),positions[4];bool ok=false;
    if(ix->restored)return used<=limit;if((pd&&xx_pd_is_stopped(pd))||used>limit)goto done;
    positions[0]=ix->stream;positions[3]=positions[0]+ix->packed[0];positions[1]=positions[3]+ix->packed[3];positions[2]=positions[1]+ix->packed[1];
    for(i=0;i<3;i++) {
        uint64_t dictionary=UINT64_C(1)<<ix->exponent[i];
        if(!fi_charge(&used,limit,ix->packed[i])||!fi_charge(&used,limit,ix->output[i])||dictionary>limit-used||FI_CODEC_RESERVE>limit-used-dictionary)goto done;
        packed=(uint8_t*)xx_mem_alloc(ix->packed[i]?ix->packed[i]:1);streams[i]=(uint8_t*)xx_mem_alloc(ix->output[i]?ix->output[i]:1);
        if(!packed||!streams[i]||!fi_masked_read_pd(f->device,positions[i],packed,ix->packed[i],pd)||!fi_lzma(packed,ix->packed[i],streams[i],ix->output[i],ix->exponent[i],&consumed,pd)||consumed!=ix->packed[i])goto done;
        xx_mem_free(packed);packed=NULL;used-=ix->packed[i];
    }
    if(!fi_charge(&used,limit,ix->packed[3])||!fi_charge(&used,limit,ix->output[3]))goto done;
    streams[3]=(uint8_t*)xx_mem_alloc(ix->packed[3]?ix->packed[3]:1);main=(uint8_t*)xx_mem_alloc(ix->output[3]);
    if(!streams[3]||!main||!fi_masked_read_pd(f->device,positions[3],streams[3],ix->packed[3],pd))goto done;
    {const uint8_t *input[4]={streams[0],streams[1],streams[2],streams[3]};size_t sizes[4]={ix->output[0],ix->output[1],ix->output[2],ix->packed[3]};
     if(!fead_bcj2_decode(input,sizes,main,ix->output[3],pd)||(pd&&xx_pd_is_stopped(pd)))goto done;}
    for(i=0;i<4;i++){xx_mem_free(streams[i]);streams[i]=NULL;used-=i==3?ix->packed[i]:ix->output[i];}
    if(!fi_charge(&used,limit,sizeof(*flat)))goto done;flat=(fi_index*)xx_mem_calloc(1,sizeof(*flat));if(!flat)goto done;
    emitter.index=flat;emitter.capacity=0;emitter.used=&used;emitter.limit=limit;emitter.pd=pd;
    xx_rt_memset(&context,0,sizeof(context));context.data=main;context.size=ix->output[3];context.members=ix->outer;context.member_count=ix->outer_count;
    context.resources=ix->resources;context.resource_count=ix->resource_count;context.actions=ix->actions;context.action_count=ix->action_count;
    context.memory_limit=limit;context.memory_used=&used;context.pd=pd;context.emit=fi_emit;context.user=&emitter;
    if(!fead_restore_archive(&context)||(pd&&xx_pd_is_stopped(pd))||!flat->records.count)goto done;
    ix->owned-=ix->records.capacity*sizeof(ue2_member);
    for(i=0;i<ix->records.count;i++){ix->owned-=xx_rt_strlen(ix->records.members[i].name)+1;xx_str_free(ix->records.members[i].name);}xx_mem_free(ix->records.members);
    ix->records.members=flat->records.members;ix->records.count=flat->records.count;ix->records.capacity=flat->records.capacity;ix->payloads=flat->payloads;
    ix->owned+=flat->owned;flat->records.members=NULL;flat->records.count=0;flat->payloads=NULL;ix->restored=true;
    f->number_of_archive_records=ix->records.count;ok=true;
done:
    for(i=0;i<4;i++)xx_mem_free(streams[i]);xx_mem_free(main);xx_mem_free(packed);fi_index_free(flat);
    if(saved>=0)xx_io_seek64(f->device,saved,XX_RT_SEEK_SET);
    return ok?true:fi_fail(pd,"FEAD payload is damaged, unsupported, or exceeds the memory limit");
}
static bool fi_info_options(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd) {
    fi_index *ix;xx_var note;bool ok;
    fi_format *format=(fi_format*)f;
    if(!f)return false;
    if(format->index){
        if(f->base_info_handled&&f->is_valid&&format->index->source_device==f->device&&format->index->source_base==f->base_address)return format->index->owned<=fi_budget(f,opts);
        fi_index_free(format->index);format->index=NULL;++format->generation;if(!format->generation)++format->generation;f->base_info_handled=false;f->is_valid=false;f->number_of_archive_records=0;
    }
    ix=fi_parse(f,opts,pd);if(!ix)return fi_fail(pd,"Invalid or unsupported Netopsystems FEAD metadata");
    xx_var_init(&note);ok=xx_var_set_str(&note,"FEAD NOS AG: original clear members and decomposed original CAB inner files; compressed CAB/EXE parent bytes are not reconstructed. Supported inverse: historical SOPA18 PNG/BILZ/CAB grammar; SOPA29 and custom inverse versions are unsupported.")&&xx_format_set_extra_parameter(f,XX_META_ID_COMMENT,&note);xx_var_cleanup(&note);
    if(!ok){fi_index_free(ix);return false;}return ue2_accept(f,&ix->records);
}
static bool fi_info(Abstractformat *f,xx_pd_struct *pd) {return fi_info_options(f,NULL,pd);}
static int64_t fi_size(Abstractformat *f,xx_pd_struct *pd) {return f&&fi_info(f,pd)?f->format_size:-1;}
static uint64_t fi_count(Abstractformat *f,xx_pd_struct *pd) {
    if(!f||!fi_info(f,pd)||!((fi_format*)f)->index||!fi_restore(f,NULL,pd))return 0;
    return f->number_of_archive_records;
}
static xx_archive_record_state *fi_records(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd) {
    xx_archive_record_state *s;fi_state *state;
    if(!f)return NULL;
    if(!fi_info_options(f,opts,pd))return NULL;
    if(!((fi_format*)f)->index||!fi_restore(f,opts,pd)||(s=ue2_records(f,opts,pd))==NULL)return NULL;
    state=(fi_state*)xx_mem_alloc(sizeof(*state));if(!state){xx_archive_record_state_free(s);return NULL;}
    state->record=*(ue2_state*)s->internal_state;state->generation=((fi_format*)f)->generation;
    xx_mem_free(s->internal_state);s->internal_state=state;return s;
}
static const xx_archive_record *fi_current(Abstractformat *f,xx_archive_record_state *s) {
    fi_index *ix=f?((fi_format*)f)->index:NULL;ue2_state *state=s?(ue2_state*)s->internal_state:NULL;const xx_archive_record *r=ue2_current(f,s);
    uint64_t time;size_t i;const char *comment="FEAD original file payload";
    if(!r||!ix||!ix->restored||!f->base_info_handled||!f->is_valid||!state||ix->source_device!=f->device||ix->source_base!=f->base_address||((fi_state*)state)->generation!=((fi_format*)f)->generation||state->index!=&ix->records||state->cursor>=ix->records.count)return NULL;
    time=ix->records.members[state->cursor].tag;time=time>=UINT64_C(116444736000000000)?time/UINT64_C(10000000)-UINT64_C(11644473600):0;
    if(ix->records.members[state->cursor].is_folder)for(i=0;i<ix->outer_count;i++)if(ix->outer[i].size&&!xx_rt_strcmp(ix->outer[i].name,ix->records.members[state->cursor].name)){comment="FEAD transformed CAB container: original inner file payloads; original compressed parent bytes are not reconstructed";break;}
    if(xx_rt_strstr(ix->records.members[state->cursor].name,"/__carrier_"))comment="FEAD preserved executable carrier bytes";
    if(!xx_archive_record_set_meta_u64(&s->current_record,XX_META_ID_COMPRESSION_METHOD,0x303011b)||!xx_archive_record_set_meta_u64(&s->current_record,XX_META_ID_TIMESTAMP,time)||!xx_archive_record_set_meta_str(&s->current_record,XX_META_ID_COMMENT,comment))return NULL;
    return r;
}
static bool fi_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd) {return fi_current(f,s)&&ue2_next(f,s,pd);}
static bool fi_unpack(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd) {
    const xx_archive_record *r=fi_current(f,s);fi_index *ix=((fi_format*)f)->index;ue2_state *state=s?(ue2_state*)s->internal_state:NULL;
    const xx_var *pathvar,*maxvar;const char *base=NULL;char *converted=NULL,*path=NULL;xx_io_device *memory=NULL;bool ok=false,folder;
    if(!r||(pd&&xx_pd_is_stopped(pd))||ix->owned>fi_budget(f,&s->options))return false;
    folder=ix->records.members[state->cursor].is_folder;maxvar=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(maxvar&&(uint64_t)r->compressed_size>xx_var_get_u64(maxvar))return false;
    pathvar=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_UNPACK_PATH);if(!pathvar)return true;
    if(pathvar->type==XX_VAR_TYPE_STRING||pathvar->type==XX_VAR_TYPE_STRING_VIEW)base=xx_var_get_str(pathvar);
    else if(pathvar->type==XX_VAR_TYPE_WSTRING||pathvar->type==XX_VAR_TYPE_WSTRING_VIEW){converted=xx_str_unicode_to_utf8(xx_var_get_wstr(pathvar));base=converted;}
    if(!base||!ue2_safe_name(xx_archive_record_get_original_name(r))||!(path=xx_str_concat3(base,"/",xx_archive_record_get_original_name(r)))||!xx_store_create_dirs_a(path,folder))goto done;
    if(folder)ok=true;else if((memory=xx_io_mem_open_ro(ix->payloads[state->cursor],(size_t)r->compressed_size))!=NULL)ok=xx_store_unpack_device_to_file(memory,0,r->compressed_size,path,pd);
done:
    xx_io_close(memory);xx_str_free(path);xx_str_free(converted);return ok;
}
static void fi_destroy(Abstractformat *f) {fi_format *format=(fi_format*)f;fi_index_free(format->index);format->index=NULL;++format->generation;if(!format->generation)++format->generation;f->base_info_handled=false;f->is_valid=false;f->number_of_archive_records=0;xx_format_cleanup_extra_parameters(f);}
Abstractformat *xx_fead_create(xx_io_device *d,int64_t base) {
    fi_format *f=(fi_format*)xx_mem_calloc(1,sizeof(*f));if(!f)return NULL;f->generation=1;
    ue2_init_format(&f->format,d,base,FI_TYPE,"exe","application/x-netopsystems-fead");f->format.check_is_valid=fi_info;f->format.handle_base_info=fi_info;
    f->format.get_format_size=fi_size;f->format.get_number_of_archive_records=fi_count;f->format.create_archive_records_reading=fi_records;f->format.get_current_archive_record=fi_current;f->format.archive_record_move_to_next=fi_next;f->format.unpack_current_archive_record=fi_unpack;f->format.destroy=fi_destroy;return &f->format;
}
void xx_fead_free(Abstractformat *f) {if(f){fi_destroy(f);xx_mem_free(f);}}
xx_file_type_t xx_fead_detect_device(xx_io_device *d,xx_pd_struct *pd) {
    int64_t saved;Abstractformat *f=NULL;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    if(!d||(pd&&xx_pd_is_stopped(pd)))return type;saved=xx_io_tell(d);
    if(fi_header(d,0)>=0){f=xx_fead_create(d,0);if(f&&fi_info(f,pd))type=FI_TYPE;}xx_fead_free(f);
    if(saved>=0)xx_io_seek64(d,saved,XX_RT_SEEK_SET);return type;
}
