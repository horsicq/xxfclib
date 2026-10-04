/* SPDX-License-Identifier: MIT
 * Original read-only HPFS reader. On-disk facts: Linux v6.12 fs/hpfs/hpfs.h.
 * Directory B-trees and fnode/anode allocation trees are traversed separately;
 * payload extents remain lazy and TEST never creates an output file. */
typedef struct vh_state {
    Abstractformat *f; pm_stream *s; xx_pd_struct *pd;
    uint64_t sectors, limit, used, max_member;
    uint32_t *visited; size_t count, capacity;
    uint32_t hot_from[256],hot_to[256]; unsigned hot_count;
} vh_state;
static bool vh_budget(vh_state *v,uint64_t n) {
    if(!vr_poll(v->pd) || v->used>v->limit || n>v->limit-v->used) return false;
    v->used+=n; return true;
}
static bool vh_seen(vh_state *v,uint32_t sector) {
    size_t i; uint32_t *p;
    if(!sector || sector>=v->sectors || v->count>=100000U) return false;
    for(i=0;i<v->count;++i) if(v->visited[i]==sector) return false;
    if(v->count==v->capacity) {
        size_t cap=v->capacity?v->capacity*2:64;
        if(!vh_budget(v,(cap-v->capacity)*sizeof(*p))) return false;
        p=(uint32_t *)xx_mem_realloc(v->visited,cap*sizeof(*p)); if(!p) return false;
        v->visited=p; v->capacity=cap;
    }
    v->visited[v->count++]=sector; return true;
}
static bool vh_read(vh_state *v,uint32_t sector,void *p,size_t n) {
    size_t done=0;
    if(sector>=v->sectors || n>v->sectors*512-(uint64_t)sector*512) return false;
    while(done<n) { unsigned i; uint32_t physical=sector+(uint32_t)(done/512); size_t take=n-done<512?n-done:512;
        for(i=0;i<v->hot_count;++i) if(v->hot_from[i]==physical) { physical=v->hot_to[i]; break; }
        if(!vr_poll(v->pd) || !pm_read(v->f,(int64_t)physical*512,(uint8_t *)p+done,take)) return false; done+=take;
    }
    return true;
}
static bool vh_extent(vh_state *v,mm_map *m,uint32_t sector,uint64_t bytes) {
    while(bytes) { unsigned i; uint32_t physical=sector; uint64_t take=bytes;
        for(i=0;i<v->hot_count;++i) {
            if(v->hot_from[i]==sector) { physical=v->hot_to[i]; take=bytes<512?bytes:512; break; }
            if(v->hot_from[i]>sector && (uint64_t)(v->hot_from[i]-sector)*512<take) take=(uint64_t)(v->hot_from[i]-sector)*512;
        }
        if(m->count==m->capacity && !vh_budget(v,(m->capacity?m->capacity:8)*sizeof(mm_extent))) return false;
        if(!mm_add(v->f,m,(int64_t)physical*512,take)) return false;
        sector+=(uint32_t)((take+511)/512); bytes-=take;
    }
    return true;
}
static mm_map *vh_map(vh_state *v) { return vh_budget(v,sizeof(mm_map))?mm_create():NULL; }
static bool vh_add(vh_state *v,mm_map *m,int64_t source,uint64_t size) {
    if(!size) return true;
    if(m->count==m->capacity && !vh_budget(v,(m->capacity?m->capacity:8)*sizeof(mm_extent))) return false;
    return vr_poll(v->pd) && mm_add(v->f,m,source,size);
}
static bool vh_named(vh_state *v,const char *path,mm_map *map,bool directory) {
    pm_member *member; vc_member_context *context; char *name;
    if((map && map->size>v->max_member) || !vh_budget(v,2*sizeof(pm_member)+sizeof(vc_member_context)+xx_rt_strlen(path)+1)) { mm_free(map); return false; }
    name=xx_str_dup(path); context=(vc_member_context *)xx_mem_alloc(sizeof(*context));
    if(!name || !context) { xx_mem_free(name); xx_mem_free(context); mm_free(map); return false; }
    if(map) { if(!mm_emit(v->f,v->s,"file",map)) { xx_mem_free(name); xx_mem_free(context); return false; } }
    else if(!pm_add(v->f,v->s,"directory",0,0)) { xx_mem_free(name); xx_mem_free(context); return false; }
    member=&v->s->items[v->s->count-1]; member->display_name=name; member->directory=directory;
    context->map=map; context->retained=0; context->limit=v->limit;
    member->context=context; member->free_context=vc_member_free; member->read_range=map?vc_member_range:NULL; member->read_all=vc_member_unpack;
    return true;
}
static bool vh_path(const char *parent,const uint8_t *name,size_t n,char *out,size_t cap) {
    size_t a=xx_rt_strlen(parent),i,k=a; if(!n || n>255 || a+2*n+2>cap) return false;
    if((n==1 && name[0]=='.') || (n==2 && name[0]=='.' && name[1]=='.') || name[n-1]=='.' || name[n-1]==' ') return false;
    xx_rt_memcpy(out,parent,a); if(a) out[k++]='/';
    for(i=0;i<n;++i) { unsigned c=name[i];
        if(c<32 || c==127 || c=='/' || c=='\\' || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*') return false;
        if(c<128) out[k++]=(char)c; else { out[k++]=(char)(0xc0|(c>>6)); out[k++]=(char)(0x80|(c&63)); }
    }
    out[k]=0; return true;
}
static bool vh_tree(vh_state *v,const uint8_t *b,size_t bt,size_t array,unsigned maxleaf,unsigned maxnode,mm_map *m,uint64_t size,uint64_t *next,unsigned depth) {
    unsigned i,n=b[bt+5]; bool internal=(b[bt]&0x80)!=0; uint32_t previous=0;
    if(depth>32 || n>(internal?maxnode:maxleaf) || !n || b[bt+4]+n!=(internal?maxnode:maxleaf) || pm_le16(b+bt+6)!=8+n*(internal?8:12)) return false;
    for(i=0;i<n;++i) { const uint8_t *e=b+array+i*(internal?8:12); uint32_t file=pm_le32(e);
        if(!vr_poll(v->pd) || (i && file<=previous)) return false; previous=file;
        if(internal) {
            uint8_t a[512]; uint32_t child=pm_le32(e+4);
            if(!vh_seen(v,child) || !vh_read(v,child,a,sizeof(a)) || pm_le32(a)!=UINT32_C(0x37E40AAE) || pm_le32(a+4)!=child) return false;
            if(!vh_tree(v,a,12,20,40,60,m,size,next,depth+1)) return false;
            /* Separator keys are exclusive upper file-sector bounds. A
             * traversal that ignores them can accept allocations that the
             * actual HPFS lookup cannot reach. Nonfinal spans meet the next
             * separator exactly; the final sentinel covers the entire span. */
            if(*next>(uint64_t)file*512 || (i+1<n && *next!=(uint64_t)file*512)) return false;
        } else {
            uint32_t count=pm_le32(e+4),disk=pm_le32(e+8); uint64_t bytes=(uint64_t)count*512,logical=(uint64_t)file*512;
            if(!count || !disk || disk>=v->sectors || count>v->sectors-disk || logical!=*next || bytes>UINT64_MAX-*next) return false;
            if(logical<size) {
                uint64_t take=bytes<size-logical?bytes:size-logical;
                if(!vh_extent(v,m,disk,take)) return false;
            }
            *next+=bytes;
        }
    }
    return true;
}
/* HPFS Unix symlinks store their target in the SYMLINK EA, rather than in the
 * fnode's regular data stream. EA lists and indirect values use the same
 * sector/anode allocation grammar. Only small headers are read eagerly. */
static void vh_drop(vh_state *v,mm_map *m) { if(m) { v->used-=sizeof(*m)+m->capacity*sizeof(mm_extent); mm_free(m); } }
static mm_map *vh_ea_map(vh_state *v,uint32_t sector,bool anode,uint64_t size) {
    mm_map *m=vh_map(v); uint64_t next=0; uint8_t b[512];
    if(!m || size>16U*1024U*1024U || !sector || sector>=v->sectors) goto fail;
    if(anode) {
        if(!vh_seen(v,sector) || !vh_read(v,sector,b,sizeof(b)) || pm_le32(b)!=UINT32_C(0x37E40AAE) || pm_le32(b+4)!=sector || !vh_tree(v,b,12,20,40,60,m,size,&next,0) || next<size) goto fail;
    } else if(size>(v->sectors-sector)*512 || !vh_extent(v,m,sector,size)) goto fail;
    return m;
fail: vh_drop(v,m); return NULL;
}
static bool vh_map_read(vh_state *v,mm_map *m,uint64_t at,void *b,size_t n) {
    pm_member member; xx_mem_zero(&member,sizeof(member)); member.context=m; return mm_read(v->f,&member,at,b,n,v->pd);
}
static mm_map *vh_slice(vh_state *v,mm_map *source,uint64_t at,uint64_t size) {
    mm_map *m=vh_map(v); uint64_t start=0,left=size; size_t i;
    if(!m || at>source->size || size>source->size-at) goto fail;
    for(i=0;i<source->count && left;++i) { const mm_extent *e=&source->extents[i]; uint64_t skip,take;
        if(at>=start+e->length) { start+=e->length; continue; }
        skip=at-start; take=e->length-skip<left?e->length-skip:left;
        if(e->source<0 || !vh_add(v,m,e->source+(int64_t)skip,take)) goto fail;
        left-=take; at+=take; start+=e->length;
    }
    if(left) goto fail; return m;
fail: vh_drop(v,m); return NULL;
}
static bool vh_ea_scan(vh_state *v,mm_map *source,uint64_t at,uint64_t size,mm_map **target) {
    uint64_t end; uint8_t b[268];
    if(at>source->size || size>source->size-at) return false; end=at+size;
    while(at<end) { unsigned n,value,i; size_t header; uint64_t stride;
        if(end-at<4 || !vh_map_read(v,source,at,b,4)) return false; n=b[1]; value=pm_le16(b+2); stride=5U+n+value;
        if(!n || stride>end-at || (b[0]&~0x83U) || ((b[0]&2) && !(b[0]&1)) || ((b[0]&1) && value!=8)) return false;
        header=5U+n+((b[0]&1)?8U:0U); if(!vh_map_read(v,source,at+4,b+4,header-4) || b[4+n]) return false;
        for(i=0;i<n;++i) if(!b[4+i]) return false;
        if(n==7 && !xx_rt_memcmp(b+4,"SYMLINK",7)) {
            uint64_t length=(b[0]&1)?pm_le32(b+5+n):value;
            if(*target || length>v->max_member) return false;
            *target=(b[0]&1)?vh_ea_map(v,pm_le32(b+9+n),(b[0]&2)!=0,length):vh_slice(v,source,at+5+n,length);
            if(!*target) return false;
        }
        at+=stride;
    }
    return true;
}
static bool vh_symlink(vh_state *v,uint32_t sector,const uint8_t b[512],mm_map **target) {
    mm_map *source=NULL; uint32_t external=pm_le32(b+44); unsigned resident=pm_le16(b+52),offset=pm_le16(b+184),acl=pm_le16(b+40); bool ok=false;
    *target=NULL;
    if(resident) {
        if(offset<196 || offset>512 || acl>512-offset || resident>512-offset-acl) goto done;
        source=vh_ea_map(v,sector,false,512); if(!source || !vh_ea_scan(v,source,offset+acl,resident,target)) goto done; vh_drop(v,source); source=NULL;
    }
    if(external) {
        source=vh_ea_map(v,pm_le32(b+48),(pm_le16(b+54)&2)!=0,external);
        if(!source || !vh_ea_scan(v,source,0,external,target)) goto done;
    }
    ok=true;
done: vh_drop(v,source); if(!ok) { vh_drop(v,*target); *target=NULL; } return ok;
}
static bool vh_fnode(vh_state *v,uint32_t sector,const char *path,bool directory,uint32_t directory_size,unsigned depth);
static bool vh_dnode(vh_state *v,uint32_t sector,const char *path,unsigned depth) {
    uint8_t b[2048]; size_t at,end; bool last=false;
    if(depth>64 || !vh_seen(v,sector) || !vh_read(v,sector,b,sizeof(b)) || pm_le32(b)!=UINT32_C(0x77E40AAE) || pm_le32(b+16)!=sector) return false;
    end=pm_le32(b+4); if(end<52 || end>sizeof(b) || (end&3)) return false;
    for(at=20;at<end;) { const uint8_t *e=b+at; uint16_t len; unsigned flags,n; char child[4097];
        if(end-at<32 || last || !vr_poll(v->pd)) return false;
        len=pm_le16(e); flags=e[2]; n=e[30];
        if(len<32 || (len&3) || len>end-at || 31U+n+((flags&4)?4U:0U)>len) return false;
        if(flags&4) { uint32_t down=pm_le32(e+len-4); if(!vh_dnode(v,down,path,depth+1)) return false; }
        if(flags&8) { if(n!=1 || e[31]!=255) return false; last=true; }
        else if(flags&1) { if(n!=2 || e[31]!=1 || e[32]!=1) return false; }
        else {
            if(!vh_path(path,e+31,n,child,sizeof(child)) || !vh_fnode(v,pm_le32(e+4),child,(e[3]&0x10)!=0,pm_le32(e+12),depth+1)) return false;
        }
        at+=len;
    }
    return last;
}
static bool vh_fnode(vh_state *v,uint32_t sector,const char *path,bool directory,uint32_t directory_size,unsigned depth) {
    uint8_t b[512]; mm_map *m=NULL; uint64_t size,next=0;
    if(depth>64 || !vh_seen(v,sector) || !vh_read(v,sector,b,sizeof(b)) || pm_le32(b)!=UINT32_C(0xF7E40AAE) || (((pm_le16(b+54)&0x100)!=0)!=directory)) return false;
    size=pm_le32(b+160); if(!vh_symlink(v,sector,b,&m)) return false;
    if(m) { if(directory || size!=directory_size) { vh_drop(v,m); return false; } return vh_named(v,path,m,false); }
    if(directory) {
        if(*path && !vh_named(v,path,NULL,true)) return false;
        if((b[56]&0x80) || b[60]!=7 || b[61]!=1 || pm_le16(b+62)!=20 || pm_le32(b+64)!=0 || pm_le32(b+68)!=4) return false;
        return vh_dnode(v,pm_le32(b+72),path,depth+1);
    }
    if(size!=directory_size || size>v->max_member) return false;
    m=vh_map(v); if(!m) return false;
    if(size && (!vh_tree(v,b,56,64,8,12,m,size,&next,0) || next<size)) { mm_free(m); return false; }
    if(!size && b[61]!=0) { if(!vh_tree(v,b,56,64,8,12,m,0,&next,0)) { mm_free(m); return false; } }
    if(!size && !b[61] && ((b[56]&0x80) || b[60]!=8 || pm_le16(b+62)!=8)) { mm_free(m); return false; }
    return vh_named(v,path,m,false);
}
static bool vr_hpfs_files(Abstractformat *f,pm_stream *s,xx_pd_struct *pd,const uint8_t *super) {
    vh_state v; uint8_t spare[512]; const xx_var *opt; bool ok; xx_volume *r=(xx_volume *)f;
    xx_mem_zero(&v,sizeof(v)); v.f=f; v.s=s; v.pd=pd; v.sectors=pm_le32(super+16); v.limit=UINT64_C(256)*1024*1024; v.used=UINT64_C(512)*1024+xx_get_file_buffer_size(); v.max_member=UINT64_MAX;
    opt=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MEMORY_LIMIT); if(opt && xx_var_get_u64(opt)<v.limit) v.limit=xx_var_get_u64(opt);
    opt=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MAX_MEMBER_SIZE); if(opt) v.max_member=xx_var_get_u64(opt);
    if(v.limit<v.used || !vh_read(&v,17,spare,sizeof(spare)) || pm_le32(spare)!=UINT32_C(0xF9911849) || pm_le32(spare+4)!=UINT32_C(0xFA5229C5)) return false;
    if(pm_le32(spare+16)) {
        uint8_t hot[2048]; uint32_t used=pm_le32(spare+16),slots=pm_le32(spare+20),table=pm_le32(spare+12); unsigned i,j;
        if(!table || used>slots || slots>256 || !vh_read(&v,table,hot,sizeof(hot))) return false;
        for(i=0;i<used;++i) { v.hot_from[i]=pm_le32(hot+i*4); v.hot_to[i]=pm_le32(hot+(slots+i)*4);
            if(!v.hot_from[i] || !v.hot_to[i] || v.hot_from[i]>=v.sectors || v.hot_to[i]>=v.sectors || v.hot_from[i]==v.hot_to[i]) return false;
            for(j=0;j<i;++j) if(v.hot_from[j]==v.hot_from[i] || v.hot_to[j]==v.hot_to[i]) return false;
        }
        for(i=0;i<used;++i) for(j=0;j<used;++j) if(v.hot_to[i]==v.hot_from[j]) return false;
        v.hot_count=used;
    }
    ok=vh_fnode(&v,pm_le32(super+12),"",true,0,0); xx_mem_free(v.visited);
    if(!ok) { if(pd && !pd->last_error) xx_pd_set_error(pd,1,"HPFS directory or allocation tree is invalid or exceeds the operation limits"); return false; }
    r->capability="HPFS directory and file extraction";
    if(!vh_budget(&v,4096+2*sizeof(pm_member)+sizeof(vc_member_context)) || !vr_info(f,s,"Integrity scope: directory and allocation trees are validated; TEST reads all listed file bytes.\nSYMLINK EA targets are exported as ordinary bytes; resident, external and indirect EA allocations are mapped.\nLegacy name bytes use Latin-1 conversion. EA lists/targets are bounded to 16 MiB.\n")) return false;
    { size_t i; pm_member *info=&s->items[s->count-1]; vc_member_context *context=(vc_member_context *)xx_mem_alloc(sizeof(*context));
      if(!context) return false; context->map=NULL; context->retained=0; context->limit=v.limit; info->context=context; info->free_context=vc_member_free; info->read_all=vc_member_unpack;
      for(i=0;i<s->count;++i) if(s->items[i].free_context==vc_member_free) ((vc_member_context *)s->items[i].context)->retained=v.used-xx_get_file_buffer_size(); }
    return true;
}
