/* SPDX-License-Identifier: MIT. The filesystem implementation runs in a
 * separate GPL helper; only bounded binary pipe messages cross this API. */
static const char *vr_grub_name(xx_file_type_t type) {
    switch(type) {
    case XX_FILE_TYPE_XFS:return "xfs"; case XX_FILE_TYPE_F2FS:return "f2fs";
    case XX_FILE_TYPE_JFS:return "jfs"; case XX_FILE_TYPE_NILFS2:return "nilfs2";
    case XX_FILE_TYPE_REISERFS:return "reiserfs";
    case XX_FILE_TYPE_ATHEOS_FS:return "afs"; case XX_FILE_TYPE_BEOS_FS:return "bfs";
    case XX_FILE_TYPE_AMIGA_SFS:return "sfs"; case XX_FILE_TYPE_ZFS:return "zfs"; default:return NULL;
    }
}
typedef struct vr_grub_context { char *path; xx_grub_backend_options options; uint64_t retained; } vr_grub_context;
typedef struct vr_grub_listing { Abstractformat *format; pm_stream *stream; xx_grub_backend_options options; uint64_t used, parent_limit, total_limit; } vr_grub_listing;
static void vr_grub_free(void *opaque) {
    vr_grub_context *c=(vr_grub_context *)opaque; if(c) { xx_str_free(c->path); xx_mem_free(c); }
}
static bool vr_grub_read(Abstractformat *f,pm_member *member,xx_io_device *output,xx_pd_struct *pd) {
    vr_grub_context *c=(vr_grub_context *)member->context; xx_grub_backend_options options;
    const xx_var *v; uint64_t budget; if(!c || !vr_poll(pd)) return false; options=c->options; options.pd=pd; budget=options.memory_limit;
    v=xx_format_resolve_extra_parameter(f,((xx_volume *)f)->parse_options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(v) options.max_member_size=xx_var_get_u64(v);
    v=xx_format_resolve_extra_parameter(f,((xx_volume *)f)->parse_options,XX_META_ID_OPT_MEMORY_LIMIT);
    if(v) { uint64_t limit=xx_var_get_u64(v); budget=limit<UINT64_C(256)*1024*1024?limit:UINT64_C(256)*1024*1024; }
    /* Retained member metadata and the client's transfer buffer share the
     * operation budget with the helper's bounded decoder allocations. */
    if(budget<=c->retained+73728U) return false;
    options.memory_limit=budget-c->retained-73728U;
    return xx_grub_backend_read(f->device,f->base_address,pm_available(f),vr_grub_name(f->file_type),c->path,(uint64_t)member->size,output,&options);
}
static bool vr_grub_safe_path(const char *p) {
    const char *start=p; size_t n=0; if(!p || !*p) return false;
    for(;;++p) { unsigned char c=(unsigned char)*p; ++n;
        if(n>4096 || (c && (c<32 || c==127 || c=='\\' || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*'))) return false;
        if(c=='/' || !c) { size_t size=(size_t)(p-start);
            if(!size || (size==1 && start[0]=='.') || (size==2 && start[0]=='.' && start[1]=='.') || p[-1]=='.' || p[-1]==' ') return false;
            start=p+1; if(!c) return true;
        }
    }
}
static bool vr_grub_entry(void *opaque,const xx_grub_backend_entry *entry) {
    vr_grub_listing *list=(vr_grub_listing *)opaque; vr_grub_context *c; pm_member *m; const char *path=entry->path;
    uint64_t bytes;
    if(path && *path=='/') ++path;
    if(entry->directory && path && !*path) return true;
    if(!vr_grub_safe_path(path) || entry->size>INT64_MAX || entry->size>list->options.max_member_size) return false;
    bytes=sizeof(pm_member)*2+sizeof(*c)+2*(uint64_t)xx_rt_strlen(path)+3;
    if(bytes>list->parent_limit-list->used) return false;
    c=(vr_grub_context *)xx_mem_alloc(sizeof(*c)); if(!c) return false;
    c->path=xx_str_dup(entry->path); c->options=list->options; c->options.memory_limit=list->total_limit; c->options.pd=NULL; c->options.status=NULL; c->retained=0;
    if(!c->path || !pm_add(list->format,list->stream,"file",0,0)) { vr_grub_free(c); return false; }
    m=&list->stream->items[list->stream->count-1]; m->context=c; m->free_context=vr_grub_free;
    m->display_name=xx_str_dup(path); m->size=(int64_t)entry->size; m->read_all=vr_grub_read;
    m->directory=entry->directory; if(m->directory) { m->size=0; m->read_all=NULL; }
    if(!m->display_name) return false; list->used+=bytes; return true;
}
static bool vr_grub_files(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    xx_volume *r=(xx_volume *)f; vr_grub_listing list; const xx_var *v; xx_grub_backend_status status=XX_GRUB_BACKEND_LIMIT;
    xx_mem_zero(&list,sizeof(list)); list.format=f; list.stream=s; list.used=4096U;
    list.options.memory_limit=UINT64_C(256)*1024*1024; list.options.max_member_size=UINT64_MAX;
    list.options.pd=pd; list.options.status=&status;
    v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MEMORY_LIMIT);
    if(v) { uint64_t limit=xx_var_get_u64(v); if(limit<list.options.memory_limit) list.options.memory_limit=limit; }
    list.total_limit=list.options.memory_limit;
    if(list.total_limit<=139264U) return false;
    list.parent_limit=(list.total_limit-73728U)/2;
    list.options.memory_limit=list.parent_limit;
    v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if(v) list.options.max_member_size=xx_var_get_u64(v);
    if(!list.options.memory_limit || !xx_grub_backend_list(f->device,f->base_address,pm_available(f),vr_grub_name(f->file_type),&list.options,vr_grub_entry,&list)) {
        if(pd && !pd->last_error) xx_pd_set_error(pd,1,status==XX_GRUB_BACKEND_UNAVAILABLE?"Filesystem helper unavailable":"Filesystem helper could not completely read this volume");
        return false;
    }
    { size_t i; for(i=0;i<s->count;++i) if(s->items[i].context && s->items[i].free_context==vr_grub_free)
        ((vr_grub_context *)s->items[i].context)->retained=list.used; }
    r->capability="filesystem file extraction through the bundled GRUB helper";
    return vr_info(f,s,"Integrity scope: all listed file bytes are read during TEST; filesystem repair is not performed.\n");
}
