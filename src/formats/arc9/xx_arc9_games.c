/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded game/resource archive readers. No helper processes.
 * GTA VER2: https://github.com/connorhaigh/gta-img/blob/master/src/read.rs
 * PBO: https://community.bohemia.net/wiki/PBO
 * XUIZ: https://github.com/rene0/xbox360/blob/master/extract360.py
 * Remaining table grammars cross-checked against recovered U3 readers and
 * the original ARC9 samples; neither detection nor TEST writes payloads.
 */
#include "xx_arc9_games.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include <string.h>

#define AG_MAX_NAME 4096U
#define AG_MAX_RECORDS 100000U
static uint16_t ag_le16(const uint8_t *b) { return (uint16_t)((uint16_t)b[0] | (uint16_t)b[1]<<8); }
static uint32_t ag_le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24; }
static uint16_t ag_be16(const uint8_t *b) { return (uint16_t)((uint16_t)b[0]<<8 | b[1]); }
static uint32_t ag_be32(const uint8_t *b) { return (uint32_t)b[0]<<24 | (uint32_t)b[1]<<16 | (uint32_t)b[2]<<8 | b[3]; }
static bool ag_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool ag_range(uint64_t at,uint64_t size,uint64_t end) { return at<=end && size<=end-at && at<=INT64_MAX && size<=INT64_MAX; }
static bool ag_write(xx_io_device *out,const uint8_t *data,size_t size) {
    size_t done=0;
    if(!out) return true;
    while(done<size) { ssize_t n=xx_io_write(out,data+done,size-done); if(n<=0 || (size_t)n>size-done) return false; done+=(size_t)n; }
    return true;
}
typedef struct ag_input { Abstractformat *f; uint64_t next,end; size_t pos,count; uint8_t bytes[8192]; } ag_input;
static bool ag_byte(ag_input *in,unsigned *value) {
    if(in->pos==in->count) {
        uint64_t left=in->end-in->next;
        in->count=left<sizeof(in->bytes)?(size_t)left:sizeof(in->bytes);in->pos=0;
        if(!in->count || !pm_read(in->f,(int64_t)in->next,in->bytes,in->count)) return false;
        in->next+=in->count;
    }
    *value=in->bytes[in->pos++];return true;
}
/* Keep original paths, but never allow absolute paths, parent traversal or
 * Windows alternate streams. The payload adapter owns the normalized name. */
static bool ag_name(char *name) {
    size_t i,start=0,n=xx_rt_strlen(name);
    /* A relative-root prefix is customary in several resource packers. */
    while(n>=2 && name[0]=='.' && (name[1]=='/' || name[1]=='\\')) { memmove(name,name+2,n-1);n-=2; }
    if(!n || n>AG_MAX_NAME || name[0]=='/' || name[0]=='\\') return false;
    for(i=0;i<=n;++i) {
        unsigned char c=(unsigned char)name[i];
        if(c=='\\') name[i]='/';
        if(c && (c<32 || c==127 || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*')) return false;
        if(!c || name[i]=='/') {
            size_t len=i-start;
            if(!len || (len==1 && name[start]=='.') || (len==2 && name[start]=='.' && name[start+1]=='.') || name[i-1]=='.' || name[i-1]==' ') return false;
            start=i+1;
        }
    }
    return true;
}
static bool ag_add(Abstractformat *f,pm_stream *s,char *name,uint64_t at,uint64_t size) {
    if(s->count>=AG_MAX_RECORDS || !ag_name(name) || !pm_add(f,s,name,(int64_t)at,(int64_t)size)) return false;
    s->items[s->count-1].display_name=xx_str_dup(name);
    return s->items[s->count-1].display_name!=NULL;
}
static bool ag_zstr(Abstractformat *f,uint64_t *at,uint64_t end,char *name) {
    size_t used=0;
    while(*at<end && used<AG_MAX_NAME) {
        uint8_t b[256];size_t i,n=end-*at<sizeof(b)?(size_t)(end-*at):sizeof(b);
        if(!pm_read(f,(int64_t)*at,b,n)) return false;
        for(i=0;i<n;++i) { ++*at; if(!b[i]) { name[used]=0;return true; } if(used==AG_MAX_NAME) return false;name[used++]=(char)b[i]; }
    }
    return false;
}
static bool ag_gta(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[32];uint64_t n=(uint64_t)pm_available(f),table;uint32_t i,count;
    if(n<8 || !pm_read(f,0,h,8) || memcmp(h,"VER2",4)) return false;
    count=ag_le32(h+4);table=8ULL+32ULL*count;
    if(count>AG_MAX_RECORDS || table>n) return false;
    for(i=0;i<count;++i) {
        char name[25];size_t k;uint64_t at,size;
        if(ag_stop(pd) || !pm_read(f,8+(int64_t)i*32,h,32)) return false;
        at=(uint64_t)ag_le32(h)*2048U;
        /* VER2 uses a 16-bit sector count plus a reserved 16-bit field;
         * accepting a 32-bit length incorrectly turns flags into gigabytes. */
        size=(uint64_t)ag_le16(h+4)*2048U;
        if(ag_le16(h+6)!=0 || !ag_range(at,size,n) || (size && at<table)) return false;
        for(k=0;k<24 && h[8+k];++k) name[k]=(char)h[8+k];
        name[k]=0;
        if(!ag_add(f,s,name,at,size)) return false;
    }
    s->size=(int64_t)n;return true;
}
static bool ag_named_table(Abstractformat *f,pm_stream *s,xx_pd_struct *pd,bool pfpk) {
    uint8_t h[8];uint64_t n=(uint64_t)pm_available(f),p=8;uint32_t i,count,magic;
    if(n<8 || !pm_read(f,0,h,8)) return false;
    magic=ag_le32(h);count=ag_le32(h+4);
    if((pfpk ? magic!=0x4b504650U : (magic!=0x0012ec9cU && magic!=0x773e8b56U && magic!=0x00ba09b8U)) || !count || count>AG_MAX_RECORDS || count>(n-8)/10) return false;
    for(i=0;i<count;++i) {
        char name[AG_MAX_NAME+1];uint64_t at,size;
        if(ag_stop(pd)) return false;
        if(pfpk) {
            uint8_t len;
            if(!pm_read(f,(int64_t)p++,&len,1) || !len || !ag_range(p,len,n) || !pm_read(f,(int64_t)p,name,len)) return false;
            name[len]=0;p+=len;
        } else if(!ag_zstr(f,&p,n,name) || !name[0]) return false;
        if(!pm_read(f,(int64_t)p,h,8)) return false;p+=8;
        at=ag_le32(h);size=ag_le32(h+4);
        if(!ag_range(at,size,n) || !ag_add(f,s,name,at,size)) return false;
    }
    for(i=0;i<count;++i) if((uint64_t)(s->items[i].offset-f->base_address)<p && s->items[i].size) return false;
    s->size=(int64_t)n;return true;
}
static bool ag_utf16be(const uint8_t *b,size_t units,char *out) {
    size_t i,p=0;
    for(i=0;i<units;++i) {
        uint32_t c=ag_be16(b+2*i);
        if(!c) return false;
        if(c>=0xd800 && c<=0xdbff) { uint32_t d;if(++i>=units || (d=ag_be16(b+2*i))<0xdc00 || d>0xdfff) return false;c=0x10000+((c-0xd800)<<10)+(d-0xdc00); }
        else if(c>=0xdc00 && c<=0xdfff) return false;
        if(c<0x80) out[p++]=(char)c;
        else if(c<0x800) { out[p++]=(char)(0xc0|(c>>6));out[p++]=(char)(0x80|(c&63)); }
        else if(c<0x10000) { out[p++]=(char)(0xe0|(c>>12));out[p++]=(char)(0x80|((c>>6)&63));out[p++]=(char)(0x80|(c&63)); }
        else { out[p++]=(char)(0xf0|(c>>18));out[p++]=(char)(0x80|((c>>12)&63));out[p++]=(char)(0x80|((c>>6)&63));out[p++]=(char)(0x80|(c&63)); }
    }
    out[p]=0;return true;
}
static bool ag_xuiz_table(Abstractformat *f,pm_stream *s,xx_pd_struct *pd,uint64_t table_end,uint32_t count,bool extended,bool emit) {
    uint64_t p=22,n=(uint64_t)pm_available(f);uint32_t i;
    for(i=0;i<count;++i) {
        uint8_t h[19],raw[510];char name[1021];uint64_t size,at;size_t units,extra=extended?10U:0U;
        if(ag_stop(pd) || !ag_range(p,extra+9,table_end) || !pm_read(f,(int64_t)p,h,extra+9)) return false;
        size=ag_be32(h+extra);at=ag_be32(h+extra+4);units=h[extra+8];p+=extra+9;
        if(!units || !ag_range(p,units*2,table_end) || !pm_read(f,(int64_t)p,raw,units*2) || !ag_utf16be(raw,units,name)) return false;
        p+=units*2;
        if(!ag_name(name) || !ag_range(table_end+at,size,n)) return false;
        if(emit && !ag_add(f,s,name,table_end+at,size)) return false;
    }
    return p==table_end;
}
static bool ag_xuiz(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[22];uint64_t n=(uint64_t)pm_available(f),end;uint32_t count;bool short_ok,long_ok;
    if(n<22 || !pm_read(f,0,h,22) || memcmp(h,"XUIZ",4) || ag_be32(h+4)!=1 || ag_be32(h+8)!=n) return false;
    end=22ULL+ag_be32(h+16);count=ag_be16(h+20);
    if(!count || count>AG_MAX_RECORDS || end>n) return false;
    short_ok=ag_xuiz_table(f,s,pd,end,count,false,false);long_ok=ag_xuiz_table(f,s,pd,end,count,true,false);
    if(short_ok==long_ok || !ag_xuiz_table(f,s,pd,end,count,long_ok,true)) return false;
    s->size=(int64_t)n;return true;
}
/* PBO compressed bytes use a 4 KiB space-filled history and a trailing
 * additive checksum. Decode into a fixed buffer even during archive TEST. */
static bool ag_pbo_read(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    uint8_t ring[4096],buffer[4096],b[4];uint64_t p=(uint64_t)(m->offset-f->base_address),end=p+(uint64_t)m->packed_size,produced=0;size_t used=0;unsigned flags=0,history=0;uint32_t checksum=0;ag_input in;
    memset(ring,32,sizeof(ring));
    if(m->packed_size<4) return false;
    xx_mem_zero(&in,sizeof(in));in.f=f;in.next=p;in.end=end-4;
    while(produced<(uint64_t)m->size) {
        unsigned c,count=1,from=0;
        if(ag_stop(pd)) return false;
        flags>>=1;
        if(!(flags&256)) { unsigned bits;if(!ag_byte(&in,&bits)) return false;flags=bits|0xff00U; }
        if(!ag_byte(&in,&c)) return false;
        if(!(flags&1)) {
            unsigned d;if(!ag_byte(&in,&d)) return false;
            from=(history-((d>>4)*256U+c))&4095U;count=(d&15U)+3U;
        }
        if(count>(uint64_t)m->size-produced) return false;
        while(count--) {
            if(!(flags&1)) { c=ring[from];from=(from+1)&4095U; }
            ring[history]=(uint8_t)c;history=(history+1)&4095U;buffer[used++]=(uint8_t)c;checksum+=c;++produced;
            if(used==sizeof(buffer)) { if(!ag_write(out,buffer,used)) return false;used=0; }
        }
    }
    /* Any unused bits in the final flag byte need no extra token bytes. */
    if(in.next-(in.count-in.pos)!=end-4 || !pm_read(f,(int64_t)(end-4),b,4) || checksum!=ag_le32(b)) return false;
    return !ag_stop(pd) && ag_write(out,buffer,used);
}
static bool ag_pbo(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t n=(uint64_t)pm_available(f),p=0,data;uint8_t h[20];uint32_t i;bool properties=false;
    if(n<21) return false;
    for(;;) {
        char name[AG_MAX_NAME+1];uint32_t method,size,original;
        if(ag_stop(pd) || !ag_zstr(f,&p,n,name) || !pm_read(f,(int64_t)p,h,20)) return false;p+=20;
        method=ag_le32(h);original=ag_le32(h+4);size=ag_le32(h+16);
        if(!name[0]) {
            if(method==0x56657273U && !s->count && !properties) {
                properties=true;
                if(original || ag_le32(h+8) || ag_le32(h+12) || size) return false;
                for(;;) { char value[AG_MAX_NAME+1];if(!ag_zstr(f,&p,n,name)) return false;if(!name[0]) break;if(!ag_zstr(f,&p,n,value)) return false; }
                continue;
            }
            /* OFP also uses the last compression tag in the empty sentinel. */
            if((method!=0 && method!=0x43707273U) || original || ag_le32(h+8) || ag_le32(h+12) || size) return false;
            break;
        }
        if((method!=0 && method!=0x43707273U) || ag_le32(h+8) || (method==0 && original && original!=size) || (method!=0 && size<4) || size>n) return false;
        if(!ag_add(f,s,name,0,size)) return false;
        if(method) { pm_member *m=&s->items[s->count-1];m->size=original;m->compression_method=1;m->read_all=ag_pbo_read; }
    }
    data=p;
    for(i=0;i<s->count;++i) { pm_member *m=&s->items[i];if(!ag_range(data,(uint64_t)m->packed_size,n)) return false;m->offset=f->base_address+(int64_t)data;data+=(uint64_t)m->packed_size; }
    /* Classic PBO has no overall trailer. Refuse unknown trailing material,
     * rather than claim to validate an unimplemented signature variant. */
    if(data!=n || !s->count) return false;
    s->size=(int64_t)n;return true;
}

static void ag_free(void *p) { xx_mem_free(p); }
static int ag_pam_find(const uint8_t *rows,uint32_t count,uint32_t id) {
    uint32_t left=0,right=count;
    while(left<right) { uint32_t middle=left+(right-left)/2,key=ag_le32(rows+(size_t)middle*24);if(key<id) left=middle+1;else if(key>id) right=middle;else return (int)middle; }
    return -1;
}
static bool ag_pam(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[36],*rows=NULL;uint64_t n=(uint64_t)pm_available(f),names,end;uint32_t count,i;bool ok=false;
    if(n<36 || !pm_read(f,0,h,36) || memcmp(h,"PAM_PAK\0",8)) return false;
    count=ag_le16(h+8);names=36ULL+32ULL*count;end=names+ag_le32(h+12);
    if(!count || end>n || names>=end) return false;
    rows=(uint8_t *)xx_mem_alloc((size_t)count*24);
    if(!rows || !pm_read(f,36+(int64_t)count*8,rows,(size_t)count*24)) goto done;
    for(i=1;i<count;++i) if(ag_le32(rows+(size_t)(i-1)*24)>=ag_le32(rows+(size_t)i*24)) goto done;
    for(i=0;i<count;++i) {
        uint32_t chain[256],depth=0,j=i,number;uint8_t item[8];uint64_t at,size;char name[AG_MAX_NAME+1];size_t used=0;
        if(ag_stop(pd)) goto done;
        for(;;) {
            const uint8_t *r=rows+(size_t)j*24;uint32_t parent=ag_le32(r+16),k;
            if(depth==256) goto done;
            for(k=0;k<depth;++k) if(chain[k]==j) goto done;
            chain[depth++]=j;
            if(!parent) break;
            { int found=ag_pam_find(rows,count,parent);if(found<0) goto done;j=(uint32_t)found; }
        }
        while(depth) {
            char part[AG_MAX_NAME+1];const uint8_t *r=rows+(size_t)chain[--depth]*24;uint64_t pos=names+ag_le32(r+8);size_t len;
            if(pos<names || pos>=end || !ag_zstr(f,&pos,end,part) || !ag_name(part) || strchr(part,'/')) goto done;
            len=xx_rt_strlen(part);if(used+len+(used?1:0)>AG_MAX_NAME) goto done;
            if(used) name[used++]='/';memcpy(name+used,part,len);used+=len;name[used]=0;
        }
        number=ag_le16(rows+(size_t)i*24+4);
        if(number>=count || !pm_read(f,36+(int64_t)number*8,item,8)) goto done;
        size=ag_le32(item);at=ag_le32(item+4);
        if(at==0xffffffffU) {
            if(size!=0xc0000000U || !ag_add(f,s,name,end,0)) goto done;
            s->items[s->count-1].directory=true;
        } else if(at<end || !ag_range(at,size,n) || !ag_add(f,s,name,at,size)) goto done;
    }
    s->size=(int64_t)n;ok=true;
done:xx_mem_free(rows);return ok;
}

typedef struct ag_titan_block { uint32_t at,packed,size; } ag_titan_block;
typedef struct ag_titan_member { uint32_t count,checksum;ag_titan_block blocks[1]; } ag_titan_member;
static bool ag_titan_read(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    ag_titan_member *ctx=(ag_titan_member *)m->context;uint32_t i,checksum=XX_ADLER32_INIT;uint64_t total=0;
    for(i=0;i<ctx->count;++i) {
        ag_titan_block *b=&ctx->blocks[i];uint8_t *packed=NULL,*plain=NULL;bool ok=false;
        if(ag_stop(pd)) return false;
        if(b->packed==b->size) {
            uint8_t buffer[65536];uint64_t offset=0;
            while(offset<b->size) {size_t amount=b->size-offset<sizeof(buffer)?(size_t)(b->size-offset):sizeof(buffer);if(ag_stop(pd) || !pm_read(f,(int64_t)(b->at+offset),buffer,amount) || !ag_write(out,buffer,amount)) return false;checksum=xx_adler32_update(checksum,buffer,amount);offset+=amount;}
            total+=b->size;if(total>(uint64_t)m->size) return false;continue;
        }
        if(b->size>16U*1024U*1024U || b->packed>32U*1024U*1024U) return false;
        packed=(uint8_t *)xx_mem_alloc(b->packed?b->packed:1);
        if(!packed || !pm_read(f,b->at,packed,b->packed)) { xx_mem_free(packed);return false; }
        if(b->packed==b->size) { plain=packed;ok=true; }
        else if(b->packed>=6 && xx_zlib_stream_header_is_valid(packed,b->packed)) {
            xx_io_device *memory;size_t consumed=0;
            plain=(uint8_t *)xx_mem_alloc(b->size?b->size:1);
            memory=plain?xx_io_mem_open(plain,b->size):NULL;
            if(memory) {
                ok=xx_deflate_unpack_memory_to_device_ex(packed+2,b->packed-6,memory,&consumed,false,pd) && consumed==b->packed-6 && xx_io_tell(memory)==b->size && xx_zlib_stream_trailer_matches(packed,b->packed,plain,b->size);
                xx_io_close(memory);
            }
        }
        if(ok) { checksum=xx_adler32_update(checksum,plain,b->size);total+=b->size;ok=total<=(uint64_t)m->size && ag_write(out,plain,b->size); }
        if(plain!=packed) xx_mem_free(plain);xx_mem_free(packed);
        if(!ok) return false;
    }
    return total==(uint64_t)m->size && checksum==ctx->checksum && !ag_stop(pd);
}
static bool ag_titan(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[44];uint64_t n=(uint64_t)pm_available(f),parts,names,records,name_size;uint32_t count,blocks,i;
    if(n<32 || !pm_read(f,0,h,32) || memcmp(h,"ARC\0",4) || ag_le32(h+4)!=1) return false;
    count=ag_le32(h+8);blocks=ag_le32(h+12);parts=ag_le32(h+24);name_size=ag_le32(h+20);names=parts+ag_le32(h+16);records=names+name_size;
    if(!count || count>AG_MAX_RECORDS || blocks>AG_MAX_RECORDS || ag_le32(h+16)!=12ULL*blocks || parts<32 || !ag_range(parts,12ULL*blocks,n) || !ag_range(names,name_size,n) || !ag_range(records,44ULL*count,n) || records+44ULL*count!=n) return false;
    for(i=0;i<count;++i) {
        uint32_t type,at,packed,size,used,first,len,k;uint64_t pos,sum_packed=0,sum_size=0;ag_titan_member *ctx;char name[AG_MAX_NAME+1];size_t allocation;
        if(ag_stop(pd) || !pm_read(f,(int64_t)(records+44ULL*i),h,44)) return false;
        type=ag_le32(h);at=ag_le32(h+4);packed=ag_le32(h+8);size=ag_le32(h+12);used=ag_le32(h+28);first=ag_le32(h+32);len=ag_le32(h+36);pos=names+ag_le32(h+40);
        if((type!=1 && type!=3) || !len || len>AG_MAX_NAME || pos<names || !ag_range(pos,len,name_size+names) || !pm_read(f,(int64_t)pos,name,len)) return false;
        /* The recorded name length excludes the NUL terminator. */
        name[len]=0;
        if(memchr(name,0,len) || !ag_name(name)) return false;
        {uint8_t nul;if(pos+len>=records || !pm_read(f,(int64_t)(pos+len),&nul,1) || nul) return false;}
        if(type==1) { if(packed!=size || !ag_range(at,size,parts)) return false;used=1; }
        else if(!used || used>blocks || first>blocks-used) return false;
        allocation=sizeof(*ctx)+(size_t)(used-1)*sizeof(ctx->blocks[0]);ctx=(ag_titan_member *)xx_mem_alloc(allocation);
        if(!ctx) return false;
        ctx->count=used;ctx->checksum=ag_le32(h+16);
        for(k=0;k<used;++k) {
            ag_titan_block *b=&ctx->blocks[k];uint8_t bh[12];
            if(type==1) { b->at=at;b->packed=packed;b->size=size; }
            else {
                if(!pm_read(f,(int64_t)(parts+12ULL*(first+k)),bh,12)) { xx_mem_free(ctx);return false; }
                b->at=ag_le32(bh);b->packed=ag_le32(bh+4);b->size=ag_le32(bh+8);
            }
            if(!ag_range(b->at,b->packed,parts) || b->at<32 || (b->packed!=b->size && (b->size>16U*1024U*1024U || b->packed>32U*1024U*1024U))) { xx_mem_free(ctx);return false; }
            sum_packed+=b->packed;sum_size+=b->size;
        }
        if(sum_packed!=packed || sum_size!=size || !ag_add(f,s,name,at,packed)) { xx_mem_free(ctx);return false; }
        {pm_member *m=&s->items[s->count-1];m->size=size;m->context=ctx;m->free_context=ag_free;m->read_all=ag_titan_read;m->compression_method=type==3?8:0;}
    }
    s->size=(int64_t)n;return true;
}

/* Sandlot SBPAK uses a recoverable byte mask for payloads and a separate
 * alternating mask for the reversed, length-salted name strings. */
static bool ag_sb_range(Abstractformat *f,pm_member *m,uint64_t at,void *destination,size_t size,xx_pd_struct *pd) {
    size_t i;uint8_t *out=(uint8_t *)destination,key=*(uint8_t *)m->context;
    if(ag_stop(pd) || !ag_range(at,size,(uint64_t)m->size) || !pm_read(f,m->offset-f->base_address+(int64_t)at,out,size)) return false;
    for(i=0;i<size;++i) out[i]^=key;
    return true;
}
static bool ag_sb_string(const uint8_t *names,size_t length,uint32_t at,char *out) {
    size_t i,n=0;
    if(at>=length) return false;
    while(at+n<length && names[at+n]) { if(++n>AG_MAX_NAME) return false; }
    if(at+n==length || !n) return false;
    for(i=0;i<n;++i) out[i]=(char)names[at+n-1-i];out[n]=0;return true;
}
static bool ag_sb_key(const uint8_t *names,size_t length,size_t at,int *keys) {
    int key;if(at>=length) return false;
    key=names[at]^(uint8_t)(at-length);
    if(keys[at&1]>=0 && keys[at&1]!=key) return false;
    keys[at&1]=key;return true;
}
static bool ag_sbpak(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[52],r[16],*names=NULL,key;uint64_t n=(uint64_t)pm_available(f),names_at,data;uint32_t count,i;size_t length,k;int keys[2]={-1,-1};bool ok=false;
    if(n<52 || !pm_read(f,0,h,52) || memcmp(h,"SBPAK V 1.0\r\n",13)) return false;
    key=h[28];for(k=0;k<52;++k) h[k]^=key;
    count=ag_le32(h+36);data=ag_le32(h+44);names_at=52ULL+16ULL*count;
    if(ag_le32(h+20)!=0x01020304U || ag_le32(h+24)!=0xffffffffU || ag_le32(h+28)!=0x00010000U || ag_le32(h+48)!=n || !count || count>AG_MAX_RECORDS || data<=names_at || data>n || data-names_at>32U*1024U*1024U) return false;
    length=(size_t)(data-names_at);names=(uint8_t *)xx_mem_alloc(length);
    if(!names || !pm_read(f,(int64_t)names_at,names,length)) goto done;
    for(k=0;k<length;++k) names[k]^=key;
    if(!ag_sb_key(names,length,length-1,keys)) goto done;
    for(i=0;i<count;++i) {
        uint32_t off;
        if(ag_stop(pd) || !pm_read(f,52+(int64_t)i*16,r,16)) goto done;
        for(k=0;k<16;++k) r[k]^=key;off=ag_le32(r+8)&0xffffffU;
        if(off>=length || (off && !ag_sb_key(names,length,off-1,keys))) goto done;
    }
    if(keys[0]<0) keys[0]=keys[1]==0xaa?0xa1:keys[1]==0xb1?0xb6:keys[1]==0xc4?0xc4:-1;
    if(keys[1]<0) keys[1]=keys[0]==0xa1?0xaa:keys[0]==0xb6?0xb1:keys[0]==0xc4?0xc4:-1;
    if(keys[0]<0 || keys[1]<0) goto done;
    for(k=0;k<length;++k) names[k]=(uint8_t)((names[k]^keys[k&1])+(length-k));
    for(i=0;i<count;++i) {
        char name[AG_MAX_NAME+1],leaf[AG_MAX_NAME+1];uint32_t size,at,no,dir;size_t a,b;
        if(ag_stop(pd) || !pm_read(f,52+(int64_t)i*16,r,16)) goto done;
        for(k=0;k<16;++k) r[k]^=key;
        size=ag_le32(r);at=ag_le32(r+4);no=ag_le32(r+8)&0xffffffU;dir=ag_le32(r+12)&0xffffffU;
        if(no>UINT32_MAX-2 || !ag_sb_string(names,length,no+2,leaf)) goto done;
        name[0]=0;
        if(dir) { if(!ag_sb_string(names,length,dir,name)) goto done;a=xx_rt_strlen(name);while(a && (name[a-1]=='/' || name[a-1]=='\\')) name[--a]=0; }
        a=xx_rt_strlen(name);b=xx_rt_strlen(leaf);
        if(a+b+(a?1:0)>AG_MAX_NAME) goto done;
        if(a) name[a++]='/';memcpy(name+a,leaf,b+1);
        if(!ag_range(data+at,size,n) || !ag_add(f,s,name,data+at,size)) goto done;
        {pm_member *m=&s->items[s->count-1];m->context=xx_mem_alloc(1);if(!m->context) goto done;*(uint8_t *)m->context=key;m->free_context=ag_free;m->read_range=ag_sb_range;}
    }
    s->size=(int64_t)n;ok=true;
done:xx_mem_free(names);return ok;
}

typedef struct ag_bits { const uint8_t *bytes;size_t size,bit; } ag_bits;
static bool ag_getbits(ag_bits *in,unsigned count,unsigned *value) {
    unsigned i,v=0;if(count>24 || count>in->size*8-in->bit) return false;
    for(i=0;i<count;++i) { v|=((in->bytes[in->bit>>3]>>(in->bit&7))&1U)<<i;++in->bit; }
    *value=v;return true;
}
static bool ag_cgjp_decode(const uint8_t *src,size_t packed,uint8_t *out,size_t size,xx_pd_struct *pd) {
    ag_bits in;size_t p=0;
    if(packed<6 || memcmp(src,"DS\0\1",4)) return false;
    in.bytes=src+4;in.size=packed-4;in.bit=0;
    while(p<size) {
        unsigned tag,value,distance,zeros=0,length,i;
        if(ag_stop(pd) || !ag_getbits(&in,2,&tag)) return false;
        if(tag==1 || tag==2) { if(!ag_getbits(&in,7,&value)) return false;out[p++]=(uint8_t)(value|(tag==1?128:0));continue; }
        if(tag==0) { if(!ag_getbits(&in,6,&distance)) return false; }
        else {
            if(!ag_getbits(&in,1,&value)) return false;
            if(!ag_getbits(&in,value?12:8,&distance)) return false;distance+=value?320:64;
        }
        if(distance==4415) continue;
        if(!distance || distance>p) return false;
        for(;;) { if(!ag_getbits(&in,1,&value)) return false;if(value) break;if(++zeros>15) return false; }
        if(!zeros) length=2;
        else { if(!ag_getbits(&in,zeros,&value)) return false;length=value+(1U<<zeros)+1; }
        if(length>size-p) return false;
        for(i=0;i<length;++i) { out[p]=out[p-distance];++p; }
    }
    /* The packet ends with the 15-bit maximum-distance marker. Only zero
     * alignment bits may follow; no undeclared compressed material is used. */
    {unsigned marker,padding;if(!ag_getbits(&in,15,&marker) || marker!=32767 || in.size*8-in.bit>7) return false;while(in.bit<in.size*8) if(!ag_getbits(&in,1,&padding) || padding) return false;}
    return !ag_stop(pd);
}
static bool ag_cgjp_read(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    uint8_t input[32768],plain[32768],h[4];uint64_t p=(uint64_t)(m->offset-f->base_address),end=p+(uint64_t)m->packed_size,total=0;
    while(p<end) {
        uint32_t size,packed;
        if(ag_stop(pd) || !ag_range(p,4,end) || !pm_read(f,(int64_t)p,h,4)) return false;
        size=ag_le16(h);packed=ag_le16(h+2);p+=4;
        if(!size || size>32768 || packed>32768 || !ag_range(p,packed?packed:size,end)) return false;
        if(packed) { if(!pm_read(f,(int64_t)p,input,packed) || !ag_cgjp_decode(input,packed,plain,size,pd)) return false;p+=packed; }
        else { if(!pm_read(f,(int64_t)p,plain,size)) return false;p+=size; }
        if(size>(uint64_t)m->size-total || !ag_write(out,plain,size)) return false;total+=size;
    }
    return total==(uint64_t)m->size && !ag_stop(pd);
}
static bool ag_cgjp(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t n=(uint64_t)pm_available(f),p=0;
    while(p<n) {
        uint8_t h[4];uint64_t start,total=0;uint32_t chunks=0;bool compressed=false;char name[40];
        if(ag_stop(pd) || !ag_range(p,4,n) || !pm_read(f,(int64_t)p,h,4) || memcmp(h,"CGJP",4)) return false;
        p+=4;start=p;
        while(p<n) {
            uint32_t size,packed;
            if(ag_stop(pd) || !ag_range(p,4,n) || !pm_read(f,(int64_t)p,h,4)) return false;
            if(!memcmp(h,"CGJP",4)) break;
            size=ag_le16(h);packed=ag_le16(h+2);p+=4;
            if(!size || size>32768 || packed>32768 || !ag_range(p,packed?packed:size,n) || total>(uint64_t)INT64_MAX-size || ++chunks>AG_MAX_RECORDS) return false;
            if(packed) { if(packed<6 || !pm_read(f,(int64_t)p,h,4) || memcmp(h,"DS\0\1",4)) return false;compressed=true; }
            total+=size;p+=packed?packed:size;
        }
        if(!chunks) return false;
        xx_rt_snprintf(name,sizeof(name),"File%u.bin",(unsigned)s->count+1);
        if(!ag_add(f,s,name,start,p-start)) return false;
        {pm_member *m=&s->items[s->count-1];m->size=(int64_t)total;m->read_all=ag_cgjp_read;m->compression_method=compressed?1:0;}
    }
    s->size=(int64_t)n;return s->count!=0;
}

/* Xbox STFS mapping and hash-tree layout:
 * https://github.com/hetelek/Velocity/blob/master/XboxInternals/Stfs/StfsPackage.cpp
 * The RSA package signature is outside archive integrity testing; the SHA-1
 * metadata, hash-table hierarchy and every used data block are verified. */
static uint32_t ag_le24(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16; }
static uint32_t ag_be24(const uint8_t *p) { return (uint32_t)p[0]<<16|(uint32_t)p[1]<<8|p[2]; }
typedef struct ag_stfs_volume { uint64_t first;uint32_t allocated;unsigned sex,level;uint8_t *entries; } ag_stfs_volume;
typedef struct ag_stfs_block { uint64_t at;uint8_t hash[20]; } ag_stfs_block;
typedef struct ag_stfs_member { uint32_t count;ag_stfs_block blocks[1]; } ag_stfs_member;
static uint64_t ag_stfs_data(const ag_stfs_volume *v,uint32_t block) {
    uint64_t physical=block+(((uint64_t)block+170)/170<<v->sex);
    if(block>=170) physical+=((uint64_t)block+28900)/28900<<v->sex;
    if(block>=28900) physical+=UINT64_C(1)<<v->sex;
    return v->first+physical*4096;
}
static uint64_t ag_stfs_table(const ag_stfs_volume *v,uint32_t block,unsigned level) {
    uint64_t copies=UINT64_C(1)<<v->sex,step0=170+copies,step1=28900+171*copies,physical;
    if(level==2) physical=step1;
    else if(level==1) physical=block<28900?step0:copies+(uint64_t)(block/28900)*step1;
    else if(block<170) physical=0;
    else physical=(uint64_t)(block/170)*step0+(((uint64_t)block/28900+1)<<v->sex)+(block>=28900?copies:0);
    return v->first+physical*4096;
}
static bool ag_stfs_hashed(Abstractformat *f,uint64_t at,const uint8_t *expected,uint8_t *block,xx_pd_struct *pd) {
    uint8_t hash[20];return !ag_stop(pd) && ag_range(at,4096,(uint64_t)pm_available(f)) && pm_read(f,(int64_t)at,block,4096) && xx_sha1_memory(block,4096,hash) && !memcmp(hash,expected,20);
}
static bool ag_stfs_read(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    ag_stfs_member *ctx=(ag_stfs_member *)m->context;uint8_t block[4096];uint32_t i;uint64_t left=(uint64_t)m->size;
    for(i=0;i<ctx->count;++i) { size_t n=left<4096?(size_t)left:4096;if(!n || !ag_stfs_hashed(f,ctx->blocks[i].at,ctx->blocks[i].hash,block,pd) || !ag_write(out,block,n)) return false;left-=n; }
    return left==0 && !ag_stop(pd);
}
static bool ag_stfs(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[0x3ad],top[4096],middle[4096],bottom[4096],hash[20],*rows=NULL,*used=NULL;ag_stfs_volume v;xx_sha1_context sha;
    uint64_t n=(uint64_t)pm_available(f),p;uint32_t table_blocks,table_start,capacity,i,j,block;bool ok=false;
    memset(&v,0,sizeof(v));
    if(n<sizeof(h) || !pm_read(f,0,h,sizeof(h)) || (memcmp(h,"PIRS",4) && memcmp(h,"LIVE",4) && memcmp(h,"CON ",4)) || h[0x379]!=0x24 || (h[0x37b]&~3U) || ag_be32(h+0x3a9)!=0) return false;
    v.first=((uint64_t)ag_be32(h+0x340)+4095)&~4095ULL;v.allocated=ag_be32(h+0x395);v.sex=(~h[0x37b])&1;
    v.level=v.allocated<=170?0:v.allocated<=28900?1:2;table_blocks=ag_le16(h+0x37c);table_start=ag_le24(h+0x37e);capacity=table_blocks*64;
    if(v.first<sizeof(h) || v.first>16U*1024U*1024U || v.first>n || !v.allocated || v.allocated>1000000U || !table_blocks || capacity>AG_MAX_RECORDS || table_blocks>v.allocated || table_start>=v.allocated || !ag_range(ag_stfs_data(&v,v.allocated-1),4096,n)) return false;
    xx_sha1_init(&sha);
    for(p=0x344;p<v.first;) { size_t amount=v.first-p<4096?(size_t)(v.first-p):4096;if(ag_stop(pd) || !pm_read(f,(int64_t)p,bottom,amount)) goto done;xx_sha1_update(&sha,bottom,amount);p+=amount; }
    if(!xx_sha1_final(&sha,hash,20) || memcmp(hash,h+0x32c,20) || !ag_stfs_hashed(f,ag_stfs_table(&v,0,v.level)+(uint64_t)(h[0x37b]&2)*2048,h+0x381,top,pd)) goto done;
    v.entries=(uint8_t *)xx_mem_alloc((size_t)v.allocated*24);used=(uint8_t *)xx_mem_alloc(v.allocated);rows=(uint8_t *)xx_mem_alloc((size_t)table_blocks*4096);
    if(!v.entries || !used || !rows) goto done;memset(used,0,v.allocated);
    for(i=0;i<v.allocated;i+=170) {
        const uint8_t *parent;uint32_t available=v.allocated-i<170?v.allocated-i:170;
        if(!v.level) memcpy(bottom,top,4096);
        else {
            if(v.level==1) parent=top+(i/170)*24;
            else {
                const uint8_t *upper=top+(i/28900)*24;
                if(i%28900==0 && !ag_stfs_hashed(f,ag_stfs_table(&v,i,1)+(v.sex?(uint64_t)(upper[20]&0x40)*64:0),upper,middle,pd)) goto done;
                parent=middle+((i/170)%170)*24;
            }
            if(!ag_stfs_hashed(f,ag_stfs_table(&v,i,0)+(v.sex?(uint64_t)(parent[20]&0x40)*64:0),parent,bottom,pd)) goto done;
        }
        memcpy(v.entries+(size_t)i*24,bottom,(size_t)available*24);
    }
    block=table_start;
    for(i=0;i<table_blocks;++i) {
        const uint8_t *entry;
        if(block>=v.allocated || used[block]) goto done;entry=v.entries+(size_t)block*24;
        if(entry[20]<0x80 || !ag_stfs_hashed(f,ag_stfs_data(&v,block),entry,rows+(size_t)i*4096,pd)) goto done;
        used[block]=1;block=ag_be24(entry+21);
    }
    if(block!=0xffffffU) goto done;
    for(i=0;i<capacity;++i) {
        const uint8_t *r=rows+(size_t)i*64;uint32_t len=r[40]&63,chain[256],depth=0,node=i;char name[AG_MAX_NAME+1];size_t length=0;
        if(!len) continue;
        if(ag_stop(pd) || len>40) goto done;
        for(;;) {
            const uint8_t *part=rows+(size_t)node*64;uint32_t parent=ag_be16(part+50),k;
            if(!(part[40]&63) || (part[40]&63)>40 || depth==256) goto done;
            for(k=0;k<depth;++k) if(chain[k]==node) goto done;chain[depth++]=node;
            if(parent==0xffffU) break;
            if(parent>=capacity || !(rows[(size_t)parent*64+40]&0x80)) goto done;node=parent;
        }
        while(depth) {
            const uint8_t *part=rows+(size_t)chain[--depth]*64;size_t amount=part[40]&63;char component[41];
            memcpy(component,part,amount);component[amount]=0;
            if(memchr(component,0,amount) || !ag_name(component) || strchr(component,'/') || length+amount+(length?1:0)>AG_MAX_NAME) goto done;
            if(length) name[length++]='/';memcpy(name+length,component,amount);length+=amount;name[length]=0;
        }
        if(r[40]&0x80) { if(!ag_add(f,s,name,0,0)) goto done;s->items[s->count-1].directory=true; }
        else {
            uint32_t size=ag_be32(r+52),count=ag_le24(r+41),start=ag_le24(r+47);ag_stfs_member *ctx;size_t allocation;
            if(count!=((uint64_t)size+4095)/4096 || ag_le24(r+44)!=count || count>v.allocated) goto done;
            allocation=sizeof(*ctx)+(count?(size_t)(count-1)*sizeof(ctx->blocks[0]):0);ctx=(ag_stfs_member *)xx_mem_alloc(allocation);if(!ctx) goto done;ctx->count=count;block=start;
            for(j=0;j<count;++j) {
                const uint8_t *entry;
                if(ag_stop(pd) || block>=v.allocated || used[block]) { xx_mem_free(ctx);goto done; }
                entry=v.entries+(size_t)block*24;
                if(entry[20]<0x80) { xx_mem_free(ctx);goto done; }
                used[block]=2;ctx->blocks[j].at=ag_stfs_data(&v,block);memcpy(ctx->blocks[j].hash,entry,20);
                block=(r[40]&0x40)?block+1:ag_be24(entry+21);
            }
            if(count && !(r[40]&0x40) && block!=0xffffffU) { xx_mem_free(ctx);goto done; }
            /* Logical blocks can span hash tables, so compressed_size here
             * describes allocated content blocks rather than a linear slice. */
            if(!ag_add(f,s,name,count?ctx->blocks[0].at:0,0)) { xx_mem_free(ctx);goto done; }
            {pm_member *m=&s->items[s->count-1];m->size=size;m->packed_size=(int64_t)count*4096;m->context=ctx;m->free_context=ag_free;m->read_all=ag_stfs_read;}
        }
    }
    s->size=(int64_t)n;ok=s->count!=0;
done:xx_mem_free(v.entries);xx_mem_free(used);xx_mem_free(rows);return ok;
}

/* Photodex named-resource packages (PXT/PXS): preserve every named value,
 * including icons, thumbnails, style definitions and resource envelopes.
 * Resource envelopes are exported intact, without pretending their internal
 * bitmap/video encodings are directly usable image files. */
static bool ag_px(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[62];uint64_t n=(uint64_t)pm_available(f),p;uint32_t descriptors,outer=0;
    if(n<62 || !pm_read(f,0,h,62) || memcmp(h,"Photodex Presenter Stream\x1a\n\r\n\x1a",30) || ag_le16(h+34)<1 || ag_le16(h+34)>5) return false;
    descriptors=ag_le32(h+58);p=62ULL+44ULL*descriptors;
    if(descriptors>AG_MAX_RECORDS || p>n) return false;
    while(p<n) {
        uint8_t bh[12];uint32_t count,i;uint64_t end;
        if(ag_stop(pd) || ++outer>AG_MAX_RECORDS || !ag_range(p,12,n) || !pm_read(f,(int64_t)p,bh,12)) return false;
        p+=12;end=p+ag_le32(bh+4);
        if(end>n || ag_le16(bh)!=0 || ag_le16(bh+2)!=1 || !ag_range(p,4,end) || !pm_read(f,(int64_t)p,bh,4)) return false;
        count=ag_le32(bh);p+=4;
        if(!count || count>AG_MAX_RECORDS-s->count) return false;
        for(i=0;i<count;++i) {
            uint8_t rh[37],magic[2];char category[33],name[80];size_t len;uint32_t size;const char *ext="bin";
            if(ag_stop(pd) || !ag_range(p,37,end) || !pm_read(f,(int64_t)p,rh,37)) return false;p+=37;
            for(len=0;len<32 && rh[len];++len) category[len]=(char)rh[len];category[len]=0;
            if(!len || len==32 || !ag_name(category) || strchr(category,'/') || rh[36]!=1) return false;
            size=ag_le32(rh+32);
            if(!ag_range(p,size,end)) return false;
            if(size>=2 && !pm_read(f,(int64_t)p,magic,2)) return false;
            if((!strcmp(category,"thumbnail") || !strcmp(category,"icon")) && size>=2 && magic[0]==0xff && magic[1]==0xd8) ext="jpg";
            xx_rt_snprintf(name,sizeof(name),"%s/%06u.%s",category,(unsigned)s->count+1,ext);
            if(!ag_add(f,s,name,p,size)) return false;p+=size;
        }
        if(p!=end) return false;
    }
    s->size=(int64_t)n;return s->count!=0;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    if(pm_available(f)<0 || ag_stop(pd)) return false;
    switch(f->file_type) {
    case XX_FILE_TYPE_GTA_IMG:return ag_gta(f,s,pd);
    case XX_FILE_TYPE_PFPK:return ag_named_table(f,s,pd,true);
    case XX_FILE_TYPE_BIRDIES:return ag_named_table(f,s,pd,false);
    case XX_FILE_TYPE_XUIZ:return ag_xuiz(f,s,pd);
    case XX_FILE_TYPE_PBO:return ag_pbo(f,s,pd);
    case XX_FILE_TYPE_PAM_PAK:return ag_pam(f,s,pd);
    case XX_FILE_TYPE_TITAN_QUEST:return ag_titan(f,s,pd);
    case XX_FILE_TYPE_SBPAK:return ag_sbpak(f,s,pd);
    case XX_FILE_TYPE_CGJP:return ag_cgjp(f,s,pd);
    case XX_FILE_TYPE_PIRS:return ag_stfs(f,s,pd);
    case XX_FILE_TYPE_PX:return ag_px(f,s,pd);
    default:return false;
    }
}
Abstractformat *xx_arc9_games_create(xx_io_device *d,int64_t base,xx_file_type_t type) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));const char *ext="pak";
    if(!f) return NULL;
    if(type==XX_FILE_TYPE_GTA_IMG) ext="img";else if(type==XX_FILE_TYPE_PBO) ext="pbo";else if(type==XX_FILE_TYPE_XUIZ) ext="xzp";else if(type==XX_FILE_TYPE_TITAN_QUEST) ext="arc";
    xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,type,ext);return f;
}
void xx_arc9_games_free(Abstractformat *f) { if(f) { xx_format_cleanup_extra_parameters(f);xx_mem_free(f); } }
bool xx_arc9_games_probe(xx_io_device *d,int64_t base,xx_file_type_t type) { Abstractformat *f=xx_arc9_games_create(d,base,type);bool result=f && pm_valid(f,NULL);xx_arc9_games_free(f);return result; }
xx_file_type_t xx_arc9_games_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[64];int64_t size,old=xx_io_tell(d);uint32_t magic;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;size=pm_available(&f);
    if(size<8 || !pm_read(&f,0,h,(size_t)(size<64?size:64))) goto done;
    magic=ag_le32(h);
    if(magic==0x32524556U) type=XX_FILE_TYPE_GTA_IMG;
    else if(magic==0x4b504650U) type=XX_FILE_TYPE_PFPK;
    else if(magic==0x0012ec9cU || magic==0x773e8b56U || magic==0x00ba09b8U) type=XX_FILE_TYPE_BIRDIES;
    else if(magic==0x5a495558U) type=XX_FILE_TYPE_XUIZ;
    else if(magic==0x5f4d4150U && size>=36 && !memcmp(h,"PAM_PAK\0",8)) type=XX_FILE_TYPE_PAM_PAK;
    else if(magic==0x00435241U && size>=32 && ag_le32(h+4)==1) type=XX_FILE_TYPE_TITAN_QUEST;
    else if(magic==0x504a4743U) type=XX_FILE_TYPE_CGJP;
    else if(magic==0x53524950U || magic==0x4556494cU || magic==0x204e4f43U) type=XX_FILE_TYPE_PIRS;
    else if(size>=62 && !memcmp(h,"Photodex Presenter Stream\x1a\n\r\n\x1a",30)) type=XX_FILE_TYPE_PX;
    else if(size>=52 && !memcmp(h,"SBPAK V 1.0\r\n",13)) type=XX_FILE_TYPE_SBPAK;
    else {
        size_t i,limit=(size_t)(size<64?size:64);
        /* PBO has no mandatory magic; the first complete bounded entry must
         * have a normal path and its reserved field and method must agree. */
        if(h[0]==0 && size>=21 && ag_le32(h+1)==0x56657273U) type=XX_FILE_TYPE_PBO;
        else for(i=0;i<limit;++i) { if(!h[i]) { if(i>0 && i+21<=limit && (ag_le32(h+i+1)==0 || ag_le32(h+i+1)==0x43707273U) && ag_le32(h+i+9)==0) type=XX_FILE_TYPE_PBO;break; } if(h[i]<32 || h[i]>=127) break; }
    }
    if(type==XX_FILE_TYPE_PBO && !xx_arc9_games_probe(d,base,type)) type=XX_FILE_TYPE_UNKNOWN;
done:
    if(old>=0) xx_io_seek64(d,old,SEEK_SET);
    return type;
}
