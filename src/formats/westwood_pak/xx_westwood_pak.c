/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Westwood PAK layout: https://github.com/Will40/dunepak/blob/master/src/main.rs
 * The three directory endings are described at
 * https://moddingwiki.shikadi.net/wiki/PAK_Format_(Westwood).
 * This is an independent, bounded parser using the stored-member adapter.
 */
#include "xxfclib/formats/westwood_pak/xx_westwood_pak.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define XX_WESTWOOD_PAK_MAX_DIRECTORY (2U*1024U*1024U)

/* Original Westwood names are 8.3, while open-source Dune Legacy writes long
 * names in the same directory grammar. Keep names bounded and path-safe;
 * exact table termination and monotone offsets do the structural detection. */
static bool wp_name(const uint8_t *p,size_t n) {
    size_t i;
    if(!n || n>80) return false;
    for(i=0;i<n;++i) {
        uint8_t c=p[i];
        if(c=='.' && (i==0 || i+1==n)) return false;
        if(c<'!' || c>'~' || c=='/' || c=='\\' || c==':' ||
           c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') return false;
    }
    return true;
}

static bool wp_same_name(const char *a,const char *b) {
    while(*a && *b) {
        unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='a' && x<='z') x=(unsigned char)(x-'a'+'A');
        if(y>='a' && y<='z') y=(unsigned char)(y-'a'+'A');
        if(x!=y) return false;
    }
    return !*a && !*b;
}

static bool wp_reserved_name(const char *name) {
    static const char *const reserved[]={"CON","PRN","AUX","NUL","CONIN$","CONOUT$","CLOCK$"};
    char stem[16]; size_t i=0,j;
    while(name[i] && name[i]!='.' && i+1U<sizeof(stem)) {
        unsigned char c=(unsigned char)name[i];
        stem[i]=(char)(c>='a' && c<='z' ? c-'a'+'A' : c);
        ++i;
    }
    if(name[i] && name[i]!='.') return false;
    stem[i]=0;
    for(j=0;j<sizeof(reserved)/sizeof(reserved[0]);++j)
        if(wp_same_name(stem,reserved[j])) return true;
    return i==4U && stem[3]>='0' && stem[3]<='9' &&
        ((stem[0]=='C' && stem[1]=='O' && stem[2]=='M') ||
         (stem[0]=='L' && stem[1]=='P' && stem[2]=='T'));
}

static bool wp_finish(pm_stream *s,uint32_t last,uint32_t end) {
    pm_member *m;
    if(!s->count || end<last) return false;
    m=&s->items[s->count-1];
    m->size=m->packed_size=(int64_t)end-last;
    return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[4],*dir=NULL; uint32_t first,previous=0,total32;
    size_t at=0; int64_t total=pm_available(f); bool ok=false;
    if(total<10 || total>UINT32_MAX || !pm_read(f,0,h,4)) return false;
    total32=(uint32_t)total; first=xx_data_get_u32(h, 4, 0, false);
    if(first<10 || first>XX_WESTWOOD_PAK_MAX_DIRECTORY || first>total32) return false;
    dir=(uint8_t *)xx_mem_alloc(first);
    if(!dir || !pm_read(f,0,dir,first)) goto done;
    while(at<first && s->count<65536U) {
        uint32_t offset; size_t name_start,len;
        char name[81]; size_t i;
        if((pd && xx_pd_is_stopped(pd)) || first-at<4) goto done;
        offset=xx_data_get_u32(dir+at, 4, 0, false); at+=4;
        /* Dune II/Kyrandia v2: zero terminator, last file ends at EOF. */
        if(!offset) {
            if(at!=first || !wp_finish(s,previous,total32)) goto done;
            s->size=total; ok=true; goto done;
        }
        /* Eye of the Beholder v1: final offset occupies the last four
         * bytes of the table. Accept the unambiguous EOF value only. */
        if(at==first) {
            if(offset!=total32 || !wp_finish(s,previous,offset)) goto done;
            s->size=offset; ok=true; goto done;
        }
        /* Kyrandia v3: explicit final data offset with an empty filename,
         * followed by the zero terminator. Any trailing bytes are overlay. */
        if(!dir[at]) {
            if(!s->count || at+5U!=first || xx_data_get_u32(dir+at+1U, 4, 0, false)!=0 ||
               offset<previous || offset>total32 || !wp_finish(s,previous,offset)) goto done;
            s->size=offset; ok=true; goto done;
        }
        name_start=at;
        while(at<first && dir[at]) ++at;
        len=at-name_start;
        if(at==first || !wp_name(dir+name_start,len) || offset<first ||
           offset>total32 || (s->count && offset<previous) ||
           (!s->count && offset!=first)) goto done;
        xx_rt_memcpy(name,dir+name_start,len); name[len]=0; ++at;
        if(wp_reserved_name(name)) goto done;
        for(i=0;i<s->count;++i)
            if(wp_same_name(s->items[i].name,name)) goto done;
        if(s->count && !wp_finish(s,previous,offset)) goto done;
        if(!pm_add(f,s,name,offset,0)) goto done;
        /* The shared adapter prefixes names for ambiguous formats. Westwood
         * PAK stores exact resource names, so retain them after its validated
         * single-leaf name and duplicate checks. */
        xx_rt_memcpy(s->items[s->count-1U].name,name,len+1U);
        previous=offset;
    }
done:
    if(dir) xx_mem_free(dir);
    return ok;
}

void xx_westwood_pak_init(xx_westwood_pak *r,xx_io_device *d,int64_t base) {
    if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,base,XX_FILE_TYPE_WESTWOOD_PAK,"pak"); }
}
xx_westwood_pak *xx_westwood_pak_create(xx_io_device *d,int64_t base) {
    xx_westwood_pak *r=(xx_westwood_pak *)xx_mem_alloc(sizeof(*r));
    if(r) { xx_westwood_pak_init(r,d,base); } return r;
}
void xx_westwood_pak_destroy(xx_westwood_pak *r) {
    if(r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_westwood_pak_free(xx_westwood_pak *r) {
    if(r) { xx_westwood_pak_destroy(r); xx_mem_free(r); }
}
bool xx_westwood_pak_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {
    return pm_valid(f,pd);
}
bool xx_westwood_pak_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {
    return pm_handle(f,pd);
}
