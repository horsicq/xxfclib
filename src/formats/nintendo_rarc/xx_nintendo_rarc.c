/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Nintendo RARC reader. Layout reference: https://github.com/magcius/noclip.website/blob/master/src/Common/JSYSTEM/JKRArchive.ts
 * Parsing and extraction are independently implemented with bounded I/O.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/nintendo_rarc/xx_nintendo_rarc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_nintendo_rarc_MAX_MEMBERS 1000000U
typedef struct xx_nintendo_rarc_member_s {
    char *name;
    int64_t header_offset, header_size, data_offset;
    int64_t compressed_size, uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
    int64_t preload_offset;
    uint32_t preload_size, crc32;
    bool has_crc, unavailable;
    xx_io_device *data_device;
} xx_nintendo_rarc_member;
typedef struct xx_nintendo_rarc_stream_s {
    xx_nintendo_rarc_member *items;
    size_t count, capacity, index;
    int64_t archive_size;
    uint64_t unavailable_members, unsupported_members;
} xx_nintendo_rarc_stream;
static void xx_nintendo_rarc_vtable_destroy(Abstractformat *self);

static bool xx_nintendo_rarc_range_within(int64_t span, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= span && size <= span-offset;
}
static bool xx_nintendo_rarc_read_from(xx_io_device *dev, int64_t offset,
                              uint8_t *out, size_t size) {
    size_t done = 0;
    if (!dev || offset < 0 || xx_io_seek64(dev,offset,SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t received = xx_io_read(dev,out+done,size-done);
        if (received <= 0 || (size_t)received > size-done) return false;
        done += (size_t)received;
    }
    return true;
}
static bool xx_nintendo_rarc_read_at(Abstractformat *self,int64_t offset,
                             uint8_t *out,size_t size) {
    return self && xx_nintendo_rarc_read_from(self->device,offset,out,size);
}
static bool xx_nintendo_rarc_read_rel(Abstractformat *self,int64_t span,int64_t offset,
                              uint8_t *out,size_t size) {
    return size <= (size_t)INT64_MAX &&
           xx_nintendo_rarc_range_within(span,offset,(int64_t)size) &&
           xx_nintendo_rarc_read_at(self,self->base_address+offset,out,size);
}
static bool xx_nintendo_rarc_path_safe(const char *name) {
    const char *at = name;
    if (!at || !*at || *at == '/' || *at == '\\') return false;
    while (*at) {
        const char *end=at;
        size_t len;
        char stem[5]={0}; size_t i;
        while (*end && *end != '/' && *end != '\\') {
            unsigned char c=(unsigned char)*end;
            if (c < 32U || c == ':' || c == '*' || c == '?' || c == '"' ||
                c == '<' || c == '>' || c == '|') return false;
            ++end;
        }
        len=(size_t)(end-at);
        if (!len || (len == 1U && at[0] == '.') ||
            (len == 2U && at[0] == '.' && at[1] == '.') ||
            at[len-1U] == '.' || at[len-1U] == ' ') return false;
        for (i=0;i<len && i<4U && at[i]!='.';++i) {
            char c=at[i]; stem[i]=(c>='a' && c<='z')?(char)(c-'a'+'A'):c;
        }
        if ((i==3U && (!xx_rt_strcmp(stem,"CON") || !xx_rt_strcmp(stem,"PRN") ||
            !xx_rt_strcmp(stem,"AUX") || !xx_rt_strcmp(stem,"NUL"))) ||
            (i==4U && (!xx_rt_strncmp(stem,"COM",3U) || !xx_rt_strncmp(stem,"LPT",3U)) &&
            stem[3]>='1' && stem[3]<='9')) return false;
        at=*end?end+1:end;
    }
    return true;
}
static void xx_nintendo_rarc_stream_free(void *pointer) {
    xx_nintendo_rarc_stream *s=(xx_nintendo_rarc_stream *)pointer; size_t i;
    if (!s) return;
    for(i=0;i<s->count;++i) xx_str_free(s->items[i].name);
    xx_mem_free(s->items); xx_mem_free(s);
}
static bool xx_nintendo_rarc_names_equal(const char *a,const char *b) {
    while (*a && *b) {
        unsigned char ca=(unsigned char)*a++, cb=(unsigned char)*b++;
        if(ca>='A' && ca<='Z') ca=(unsigned char)(ca-'A'+'a');
        if(cb>='A' && cb<='Z') cb=(unsigned char)(cb-'A'+'a');
        if(ca!=cb) return false;
    }
    return *a==*b;
}
/* Duplicate lump names are legal: preserve every record under a unique name. */
static bool xx_nintendo_rarc_add(xx_nintendo_rarc_stream *s,xx_nintendo_rarc_member *m) {
    size_t i; char *original=m->name; unsigned suffix=1U;
    if(s->count >= XX_nintendo_rarc_MAX_MEMBERS || !original) return false;
    for (;;) {
        bool found=false;
        for(i=0;i<s->count;++i) if(xx_nintendo_rarc_names_equal(s->items[i].name,m->name)) {
            found=true; break;
        }
        if(!found) break;
        {
            char tail[32]; char *replacement;
            xx_rt_snprintf(tail,sizeof(tail),"__%u",++suffix);
            replacement=xx_str_concat(original,tail);
            if(!replacement) { if(m->name!=original) xx_str_free(m->name); m->name=original; return false; }
            if(m->name!=original) xx_str_free(m->name);
            m->name=replacement;
        }
    }
    if(s->count == s->capacity) {
        size_t cap=s->capacity?s->capacity*2U:16U;
        xx_nintendo_rarc_member *grown;
        if(cap > XX_nintendo_rarc_MAX_MEMBERS) cap=XX_nintendo_rarc_MAX_MEMBERS;
        grown=(xx_nintendo_rarc_member *)xx_mem_realloc(s->items,cap*sizeof(*grown));
        if(!grown) { if(m->name!=original) xx_str_free(m->name); m->name=original; return false; }
        s->items=grown; s->capacity=cap;
    }
    if(m->name!=original) xx_str_free(original);
    s->items[s->count++]=*m;
    return true;
}
static bool xx_nintendo_rarc_add_member(Abstractformat *self,xx_nintendo_rarc_stream *s,
    const char *name,int64_t h,int64_t hs,int64_t off,int64_t size,bool folder) {
    xx_nintendo_rarc_member m; size_t i;
    xx_mem_zero(&m,sizeof(m));
    m.name=xx_str_dup(name);
    if(!m.name) return false;
    for(i=0;m.name[i];++i) if(m.name[i]=='\\') m.name[i]='/';
    m.header_offset=self->base_address+h; m.header_size=hs;
    m.data_offset=self->base_address+off;
    m.compressed_size=size; m.uncompressed_size=size;
    m.is_folder=folder;
    if(!xx_nintendo_rarc_add(s,&m)) { xx_str_free(m.name); return false; }
    if(off+size > s->archive_size) s->archive_size=off+size;
    return true;
}
static inline bool xx_nintendo_rarc_fixed_name(const uint8_t *src,size_t size,char *out) {
    size_t i=0;
    while(i<size && src[i]) { if(src[i]<32U) return false; out[i]=(char)src[i]; ++i; }
    out[i]=0; return i!=0;
}
static inline bool xx_nintendo_rarc_string(Abstractformat *self,int64_t span,
    int64_t *offset,int64_t end,char *out,size_t capacity) {
    size_t i=0;
    while(*offset < end && i+1U<capacity) {
        uint8_t c;
        if(!xx_nintendo_rarc_read_rel(self,span,(*offset)++,&c,1U)) return false;
        out[i++]=(char)c;
        if(!c) return true;
        if(c<32U) return false;
    }
    return false;
}
static inline bool xx_nintendo_rarc_pool_name(Abstractformat *self,int64_t span,
    int64_t pool,int64_t pool_size,uint32_t offset,char *out,size_t capacity) {
    int64_t at=pool+(int64_t)offset;
    return (int64_t)offset<pool_size &&
           xx_nintendo_rarc_string(self,span,&at,pool+pool_size,out,capacity) && out[0];
}

static bool xx_nintendo_rarc_decode_stored(Abstractformat *self,const xx_nintendo_rarc_member *member,
                           uint8_t **out,size_t *out_size,xx_pd_struct *pd) {
    uint8_t *buffer;
    size_t size;
    *out=NULL; *out_size=0;
    if(member->unavailable || member->method != 0U ||
       member->uncompressed_size<0 ||
       (uint64_t)member->uncompressed_size>SIZE_MAX ||
       (pd && xx_pd_is_stopped(pd))) return false;
    size=(size_t)member->uncompressed_size;
    buffer=(uint8_t *)xx_mem_alloc(size?size:1U);
    if(!buffer) return false;
    if((member->preload_size && !xx_nintendo_rarc_read_at(self,member->preload_offset,
            buffer,member->preload_size)) ||
       (size>member->preload_size && !xx_nintendo_rarc_read_from(
            member->data_device?member->data_device:self->device,
            member->data_offset,buffer+member->preload_size,
            size-member->preload_size)) ||
       (member->has_crc && xx_crc32(XX_CRC_TYPE_CRC32,buffer,size)!=member->crc32)) {
        xx_mem_free(buffer); return false;
    }
    *out=buffer; *out_size=size; return true;
}

/* Bounded Yaz0/Yay0: reject invalid back-references and output overruns. */
static bool xx_nintendo_rarc_decode(Abstractformat *self,const xx_nintendo_rarc_member *m,
    uint8_t **out,size_t *out_size,xx_pd_struct *pd) {
    uint8_t *packed=NULL,*plain=NULL; size_t packed_size,size,at=16U,pos=0U;
    size_t links=0U,data=0U,mask_end=0U; uint32_t mask=0U; unsigned bits=0U; bool yaz;
    if(!m->method) return xx_nintendo_rarc_decode_stored(self,m,out,out_size,pd);
    *out=NULL; *out_size=0;
    if((m->method!=4U && m->method!=5U) || m->compressed_size<16 || m->uncompressed_size<0 ||
       (uint64_t)m->compressed_size>SIZE_MAX || (uint64_t)m->uncompressed_size>SIZE_MAX) return false;
    packed_size=(size_t)m->compressed_size; size=(size_t)m->uncompressed_size; yaz=m->method==4U;
    packed=(uint8_t *)xx_mem_alloc(packed_size); plain=(uint8_t *)xx_mem_alloc(size?size:1U);
    if(!packed || !plain || !xx_nintendo_rarc_read_at(self,m->data_offset,packed,packed_size) ||
       xx_rt_memcmp(packed,yaz?"Yaz0":"Yay0",4U) || xx_data_get_u32(packed+4, 4, 0, true)!=size) goto failed;
    if(!yaz) {
        links=xx_data_get_u32(packed+8, 4, 0, true); data=xx_data_get_u32(packed+12, 4, 0, true);
        if(links<16U || links>data || data>packed_size) goto failed;
        mask_end=links;
    }
    while(pos<size) {
        bool literal;
        if(pd && xx_pd_is_stopped(pd)) goto failed;
        if(!bits) {
            if(yaz) { if(at>=packed_size) goto failed; mask=packed[at++]; bits=8U; }
            else { if(at>mask_end || mask_end-at<4U) goto failed; mask=xx_data_get_u32(packed+at, 4, 0, true); at+=4U; bits=32U; }
        }
        literal=(mask&(yaz?0x80U:0x80000000U))!=0; mask<<=1U; --bits;
        if(literal) {
            size_t *cursor=yaz?&at:&data;
            if(*cursor>=packed_size) goto failed;
            plain[pos++]=packed[(*cursor)++];
        } else {
            uint16_t token; size_t length,distance,j;
            size_t *cursor=yaz?&at:&links;
            size_t end=yaz?packed_size:(size_t)xx_data_get_u32(packed+12, 4, 0, true);
            if(*cursor>end || end-*cursor<2U) goto failed;
            token=xx_data_get_u16(packed+*cursor, 2, 0, true); *cursor+=2U;
            distance=(token&0xFFFU)+1U; length=token>>12U;
            if(!length) {
                size_t *extra=yaz?&at:&data;
                if(*extra>=packed_size) goto failed;
                length=(size_t)packed[(*extra)++]+18U;
            } else length+=2U;
            if(distance>pos || length>size-pos) goto failed;
            for(j=0;j<length;++j) { plain[pos]=plain[pos-distance]; ++pos; }
        }
    }
    xx_mem_free(packed); *out=plain; *out_size=size; return true;
failed:
    xx_mem_free(packed); xx_mem_free(plain); return false;
}

typedef struct xx_nintendo_rarc_directory_context {
    Abstractformat *self; xx_nintendo_rarc_stream *stream; xx_pd_struct *pd;
    int64_t span,nodes,entries,pool,pool_size,data,data_size;
    uint32_t node_count,entry_count; bool be;
    uint8_t *visited,*entry_visited;
} xx_nintendo_rarc_directory_context;
static bool xx_nintendo_rarc_directory(xx_nintendo_rarc_directory_context *c,uint32_t index,
    uint32_t parent,const char *path,unsigned depth) {
    uint8_t node[16]; uint32_t first,count,j; char node_name[1024];
    if(depth>256U || index>=c->node_count || c->visited[index] ||
       (c->pd && xx_pd_is_stopped(c->pd)) ||
       !xx_nintendo_rarc_read_rel(c->self,c->span,c->nodes+(int64_t)index*16,node,sizeof(node))) return false;
    c->visited[index]=1;
    if(!index && xx_rt_memcmp(node,c->be?"ROOT":"TOOR",4U)) return false;
    if(!xx_nintendo_rarc_pool_name(c->self,c->span,c->pool,c->pool_size,
        xx_data_get_u32(node+4, 4, 0, c->be),node_name,sizeof(node_name))) return false;
    count=xx_data_get_u16(node+10, 2, 0, c->be); first=xx_data_get_u32(node+12, 4, 0, c->be);
    if(first>c->entry_count || count>c->entry_count-first) return false;
    for(j=0;j<count;++j) {
        uint8_t entry[20]; uint32_t fi=first+j,field,flags,offset,size;
        int64_t header=c->entries+(int64_t)fi*20;
        char leaf[1024]; char *name;
        if(c->entry_visited[fi] || !xx_nintendo_rarc_read_rel(c->self,c->span,header,entry,sizeof(entry))) return false;
        c->entry_visited[fi]=1;
        field=xx_data_get_u32(entry+4, 4, 0, c->be); flags=field>>24;
        offset=xx_data_get_u32(entry+8, 4, 0, c->be); size=xx_data_get_u32(entry+12, 4, 0, c->be);
        if(!xx_nintendo_rarc_pool_name(c->self,c->span,c->pool,c->pool_size,field&0xFFFFFFU,leaf,sizeof(leaf))) return false;
        if(flags&2U) {
            if(!xx_rt_strcmp(leaf,".")) { if(offset!=index) return false; continue; }
            if(!xx_rt_strcmp(leaf,"..")) { if(offset!=parent && !(index==0 && offset==0)) return false; continue; }
        }
        if(xx_rt_strchr(leaf,'/') || xx_rt_strchr(leaf,'\\')) return false;
        name=*path?xx_str_concat3(path,"/",leaf):xx_str_dup(leaf);
        if(!name || xx_str_len(name)>4095U) { xx_str_free(name); return false; }
        if(flags&2U) {
            bool valid=xx_nintendo_rarc_add_member(c->self,c->stream,name,header,20,0,0,true) &&
                xx_nintendo_rarc_directory(c,offset,index,name,depth+1U);
            xx_str_free(name); if(!valid) return false;
        } else {
            xx_nintendo_rarc_member *m;
            bool valid=(flags&1U)!=0 && xx_nintendo_rarc_range_within(c->data_size,offset,size) &&
                xx_nintendo_rarc_add_member(c->self,c->stream,name,header,20,c->data+offset,size,false);
            xx_str_free(name); if(!valid) return false;
            m=&c->stream->items[c->stream->count-1U];
            if(flags&4U) {
                uint8_t packed_header[16]; bool yaz=(flags&0x80U)!=0;
                if(size<16U || !xx_nintendo_rarc_read_rel(c->self,c->span,c->data+offset,packed_header,sizeof(packed_header)) ||
                   xx_rt_memcmp(packed_header,yaz?"Yaz0":"Yay0",4U)) return false;
                m->method=yaz?4U:5U;
                m->uncompressed_size=xx_data_get_u32(packed_header+4, 4, 0, true);
                if(!yaz) {
                    uint32_t links=xx_data_get_u32(packed_header+8, 4, 0, true),data=xx_data_get_u32(packed_header+12, 4, 0, true);
                    if(links<16U || links>data || data>size) return false;
                }
            }
        }
    }
    return true;
}

static xx_nintendo_rarc_stream *xx_nintendo_rarc_parse(Abstractformat *self,xx_pd_struct *pd) {
    int64_t total,span;
    xx_nintendo_rarc_stream *s;
    if(!self || !self->device || self->base_address<0 || (pd && xx_pd_is_stopped(pd))) return NULL;
    total=xx_io_total_size(self->device);
    if(total<self->base_address) return NULL;
    span=total-self->base_address;
    s=(xx_nintendo_rarc_stream *)xx_mem_alloc(sizeof(*s));
    if(!s) return NULL;
    xx_mem_zero(s,sizeof(*s));
    {

    uint8_t h[64]; xx_nintendo_rarc_directory_context c; uint32_t i;
    int64_t declared;
    if(!xx_nintendo_rarc_read_rel(self,span,0,h,sizeof(h))) goto fail;
    xx_mem_zero(&c,sizeof(c));
    c.be=xx_rt_memcmp(h,"RARC",4U)==0;
    if(!c.be && xx_rt_memcmp(h,"CRAR",4U)) goto fail;
    declared=xx_data_get_u32(h+4, 4, 0, c.be);
    if(declared<64 || declared>span || xx_data_get_u32(h+8, 4, 0, c.be)!=32U) goto fail;
    c.data=32+(int64_t)xx_data_get_u32(h+12, 4, 0, c.be); c.data_size=xx_data_get_u32(h+16, 4, 0, c.be);
    c.node_count=xx_data_get_u32(h+32, 4, 0, c.be); c.nodes=32+(int64_t)xx_data_get_u32(h+36, 4, 0, c.be);
    c.entry_count=xx_data_get_u32(h+40, 4, 0, c.be); c.entries=32+(int64_t)xx_data_get_u32(h+44, 4, 0, c.be);
    c.pool_size=xx_data_get_u32(h+48, 4, 0, c.be); c.pool=32+(int64_t)xx_data_get_u32(h+52, 4, 0, c.be);
    if(!c.node_count || c.node_count>XX_nintendo_rarc_MAX_MEMBERS || c.entry_count>XX_nintendo_rarc_MAX_MEMBERS ||
       c.data<64 || c.nodes<64 || c.entries<64 || c.pool<64 || c.pool_size<1 ||
       !xx_nintendo_rarc_range_within(declared,c.data,c.data_size) ||
       !xx_nintendo_rarc_range_within(c.data,c.nodes,(int64_t)c.node_count*16) ||
       !xx_nintendo_rarc_range_within(c.data,c.entries,(int64_t)c.entry_count*20) ||
       !xx_nintendo_rarc_range_within(c.data,c.pool,c.pool_size)) goto fail;
    c.visited=(uint8_t *)xx_mem_calloc(c.node_count,1U);
    c.entry_visited=(uint8_t *)xx_mem_calloc(c.entry_count?c.entry_count:1U,1U);
    c.self=self; c.stream=s; c.pd=pd; c.span=declared;
    if(!c.visited || !c.entry_visited || !xx_nintendo_rarc_directory(&c,0,UINT32_MAX,"",0U)) goto rarc_fail;
    for(i=0;i<c.node_count;++i) if(!c.visited[i]) goto rarc_fail;
    for(i=0;i<c.entry_count;++i) if(!c.entry_visited[i]) goto rarc_fail;
    xx_mem_free(c.visited); xx_mem_free(c.entry_visited);
    s->archive_size=declared; self->endian=c.be?XX_ENDIAN_BIG:XX_ENDIAN_LITTLE;
    goto rarc_done;
rarc_fail:
    xx_mem_free(c.visited); xx_mem_free(c.entry_visited); goto fail;
rarc_done:;

    }
    return s;
fail:
    xx_nintendo_rarc_stream_free(s); return NULL;
}
/* ---------------------------------------------------------- lifecycle --- */

void xx_nintendo_rarc_init(xx_nintendo_rarc *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_NINTENDO_RARC;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-nintendo-rarc");
    xx_format_set_extension(&archive->format, "arc");
    archive->format.check_is_valid = xx_nintendo_rarc_check_is_valid;
    archive->format.handle_base_info = xx_nintendo_rarc_handle_base_info;
    archive->format.get_format_size = xx_nintendo_rarc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_nintendo_rarc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_nintendo_rarc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_nintendo_rarc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_nintendo_rarc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_nintendo_rarc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_nintendo_rarc_free_archive_records_reading;
    archive->format.destroy = xx_nintendo_rarc_vtable_destroy;
}

xx_nintendo_rarc *xx_nintendo_rarc_create(xx_io_device *device, int64_t base_address) {
    xx_nintendo_rarc *archive = (xx_nintendo_rarc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_nintendo_rarc_init(archive, device, base_address);
    return archive;
}

void xx_nintendo_rarc_destroy(xx_nintendo_rarc *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_nintendo_rarc_free(xx_nintendo_rarc *archive) {
    if (!archive) return;
    xx_nintendo_rarc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_nintendo_rarc_vtable_destroy(Abstractformat *self) {
    xx_nintendo_rarc_destroy((xx_nintendo_rarc *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_nintendo_rarc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_nintendo_rarc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_nintendo_rarc_parse(self, pd);
    if (!stream) return false;
    xx_nintendo_rarc_stream_free(stream);
    return true;
}

bool xx_nintendo_rarc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_nintendo_rarc *archive = (xx_nintendo_rarc *)self;
    xx_nintendo_rarc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_nintendo_rarc_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->unavailable_members = stream->unavailable_members;
    archive->unsupported_members = stream->unsupported_members;
    xx_nintendo_rarc_stream_free(stream);
    return true;
}

int64_t xx_nintendo_rarc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_nintendo_rarc_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_nintendo_rarc *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_nintendo_rarc_set_record(xx_archive_record *record,
                                 const xx_nintendo_rarc_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_nintendo_rarc_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_nintendo_rarc_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_nintendo_rarc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_nintendo_rarc_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_nintendo_rarc_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_nintendo_rarc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_nintendo_rarc_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_nintendo_rarc_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_nintendo_rarc_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_nintendo_rarc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_nintendo_rarc_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_nintendo_rarc_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_nintendo_rarc_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_nintendo_rarc_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_nintendo_rarc_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_nintendo_rarc_stream *stream;
    const xx_nintendo_rarc_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_nintendo_rarc_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_nintendo_rarc_path_safe(member->name)) return false;

    path_option = xx_nintendo_rarc_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_nintendo_rarc_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_nintendo_rarc_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_nintendo_rarc_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
