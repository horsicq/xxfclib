/* SPDX-License-Identifier: MIT. AFF10 page directory and bounded decoder.
 * Facts: AFFLIB segment definitions and AFF on-disk specification. */
typedef struct vr_aff_page { uint64_t index,offset; uint32_t packed,flags; } vr_aff_page;
typedef struct vr_aff_image {
    vr_aff_page *pages; size_t count,capacity; uint64_t size,memory_limit; uint32_t page_size;
    uint8_t *cache; uint64_t cached_page; bool cached;
} vr_aff_image;
static void vr_aff_free(void *p) { vr_aff_image *a=(vr_aff_image *)p; if(a) { xx_mem_free(a->pages); xx_mem_free(a->cache); xx_mem_free(a); } }
static int vr_aff_order(const void *aa,const void *bb) {
    const vr_aff_page *a=(const vr_aff_page *)aa,*b=(const vr_aff_page *)bb;
    return a->index<b->index?-1:a->index>b->index?1:a->offset<b->offset?-1:a->offset>b->offset?1:0;
}
static bool vr_aff_cache(Abstractformat *f,vr_aff_image *a,size_t index,xx_pd_struct *pd) {
    vr_aff_page *page=&a->pages[index]; uint32_t expected=(uint32_t)((a->size-page->index*a->page_size)<a->page_size?(a->size-page->index*a->page_size):a->page_size);
    uint8_t *packed=NULL; xx_io_device *src=NULL,*dst=NULL; bool ok=false;
    if(a->cached && a->cached_page==page->index) return true;
    if(!a->cache) { a->cache=(uint8_t *)xx_mem_alloc(a->page_size); if(!a->cache) return false; }
    if(!(page->flags&1)) {
        if(page->packed!=expected || !pm_read(f,(int64_t)page->offset,a->cache,expected)) { return false; } ok=true;
    } else if((page->flags&0xF0)==0x30) {
        uint8_t count[4]; if(page->packed!=4 || !pm_read(f,(int64_t)page->offset,count,4) || pm_be32(count)!=expected) return false;
        xx_mem_zero(a->cache,expected); ok=true;
    } else {
        uint64_t used=(uint64_t)a->capacity*sizeof(*a->pages)+a->page_size+page->packed;
        if(page->packed>64U*1024U*1024U || used>a->memory_limit) return false;
        packed=(uint8_t *)xx_mem_alloc(page->packed?page->packed:1); if(!packed || !pm_read(f,(int64_t)page->offset,packed,page->packed)) goto done;
        src=xx_io_mem_open_ro(packed,page->packed); dst=xx_io_mem_open(a->cache,expected); if(!src || !dst) goto done;
        if((page->flags&0xF0)==0) { xx_zlib z; xx_zlib_init(&z,src,0); ok=xx_zlib_unpack_to_device(&z,dst,pd); xx_zlib_destroy(&z); }
        else if((page->flags&0xF0)==0x20 && page->packed>=13 && mm_le64(packed+5)==expected && pm_le32(packed+1)<=a->memory_limit-used) {
            ok=xx_lzma_unpack_device(src,13,page->packed-13,packed,5,expected,dst,pd);
        }
        ok=ok && xx_io_tell(dst)==expected;
    }
done:
    if(src) { xx_io_close(src); } if(dst) xx_io_close(dst); xx_mem_free(packed);
    a->cached=ok; if(ok) a->cached_page=page->index; return ok && vr_poll(pd);
}
static bool vr_aff_read(Abstractformat *f,pm_member *member,uint64_t at,void *data,size_t size,xx_pd_struct *pd) {
    vr_aff_image *a=(vr_aff_image *)member->context; size_t done=0;
    if(!a || at>a->size || size>a->size-at) return false;
    while(done<size) { size_t index=(size_t)(at/a->page_size),skip=(size_t)(at%a->page_size),chunk=a->page_size-skip;
        if(chunk>size-done) chunk=size-done;
        if(index>=a->count || !vr_poll(pd) || !vr_aff_cache(f,a,index,pd)) return false;
        xx_rt_memcpy((uint8_t *)data+done,a->cache+skip,chunk); at+=chunk; done+=chunk;
    } return vr_poll(pd);
}
static bool vr_aff(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t at=8,available=(uint64_t)pm_available(f); uint8_t h[16],tail[8]; vr_aff_image *a=NULL; xx_volume *r=(xx_volume *)f; bool ok=false;
    if(!vr_equal(f,0,"AFF10\r\n\0",8)) return false;
    a=(vr_aff_image *)xx_mem_alloc(sizeof(*a)); if(!a) return false; xx_mem_zero(a,sizeof(*a)); a->memory_limit=UINT64_C(256)*1024*1024;
    { const xx_var *v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MEMORY_LIMIT); if(v) a->memory_limit=xx_var_get_u64(v); }
    while(at<available) { uint32_t name_size,packed,flags; uint64_t segment; char name[128];
        if(!vr_poll(pd) || at>INT64_MAX || !pm_read(f,(int64_t)at,h,16) || xx_rt_memcmp(h,"AFF\0",4)) goto done;
        name_size=pm_be32(h+4); packed=pm_be32(h+8); flags=pm_be32(h+12); segment=24+(uint64_t)name_size+packed;
        if(name_size>=sizeof(name) || segment>available-at || !pm_read(f,(int64_t)(at+16),name,name_size) || !pm_read(f,(int64_t)(at+segment-8),tail,8) || xx_rt_memcmp(tail,"ATT\0",4) || pm_be32(tail+4)!=segment) goto done;
        name[name_size]=0; if(xx_rt_strlen(name)!=name_size) goto done;
        if(!xx_rt_strcmp(name,"pagesize") || !xx_rt_strcmp(name,"segsize")) { if(packed || !vr_power(flags) || flags<512 || flags>64U*1024U*1024U) goto done; a->page_size=flags; }
        else if(!xx_rt_strcmp(name,"imagesize")) { uint8_t q[8]; if(packed!=8 || flags!=2 || !pm_read(f,(int64_t)(at+16+name_size),q,8)) goto done; a->size=(uint64_t)pm_be32(q)|((uint64_t)pm_be32(q+4)<<32); }
        else if(!xx_rt_strncmp(name,"page",4) || !xx_rt_strncmp(name,"seg",3)) { const char *p=name+(!xx_rt_strncmp(name,"page",4)?4:3); uint64_t index; const char *begin=p;
            if(!vr_uint(&p,&index) || p==begin) goto done;
            if(!*p) { vr_aff_page *next; size_t cap;
                if(a->count>=1000000 || (flags&~0x33U) || ((flags&1) && (flags&0xF0)!=0 && (flags&0xF0)!=0x20 && (flags&0xF0)!=0x30)) goto done;
                if(a->count==a->capacity) { cap=a->capacity?a->capacity*2:16; if(cap*sizeof(*next)>a->memory_limit) goto done; next=(vr_aff_page *)xx_mem_realloc(a->pages,cap*sizeof(*next)); if(!next) goto done; a->pages=next; a->capacity=cap; }
                a->pages[a->count].index=index; a->pages[a->count].offset=at+16+name_size; a->pages[a->count].packed=packed; a->pages[a->count++].flags=flags;
            } else if(!xx_rt_strcmp(p,"/aes256")) { xx_pd_set_error(pd,1,"AFF encrypted pages require a supported AFF key provider"); goto done; }
        }
        at+=segment;
    }
    if(!a->page_size || !a->size || a->size>INT64_MAX || !a->count || (uint64_t)a->capacity*sizeof(*a->pages)+a->page_size>a->memory_limit) goto done;
    qsort(a->pages,a->count,sizeof(*a->pages),vr_aff_order);
    { size_t i,write=0; for(i=0;i<a->count;++i) { if(i+1<a->count && a->pages[i].index==a->pages[i+1].index) continue; a->pages[write++]=a->pages[i]; } a->count=write;
      if((a->size+a->page_size-1)/a->page_size!=a->count) goto done;
      for(i=0;i<a->count;++i) if(a->pages[i].index!=i) goto done; }
    { const xx_var *v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MAX_MEMBER_SIZE); if(v && a->size>xx_var_get_u64(v)) goto done; }
    if(!pm_add(f,s,"image.raw",0,0)) goto done;
    s->items[s->count-1].offset=-1; s->items[s->count-1].size=(int64_t)a->size; s->items[s->count-1].packed_size=(int64_t)available;
    s->items[s->count-1].context=a; s->items[s->count-1].free_context=vr_aff_free; s->items[s->count-1].read_range=vr_aff_read;
    r->block_size=a->page_size; r->blocks=a->count; a=NULL;
    r->capability="AFF10 logical-image extraction: stored, zlib, LZMA and zero pages; bounded page cache";
    ok=vr_info(f,s,"Zlib Adler-32 and declared decoded page sizes are validated while extracting/testing. Digital signatures are not authenticated.\n");
done: vr_aff_free(a); return ok;
}
