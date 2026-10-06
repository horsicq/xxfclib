/* SPDX-License-Identifier: MIT. Original read-only GFS2 directory and data
 * trees. Facts: Linux gfs2_ondisk.h. No kernel code is copied or linked. */
typedef struct vr_gfs { Abstractformat *f; pm_stream *s; xx_pd_struct *pd; uint32_t block; uint64_t limit,used,ancestors[64]; unsigned depth; } vr_gfs;
static bool vr_gfs_take(vr_gfs *g,uint64_t bytes) { if(g->used>g->limit || bytes>g->limit-g->used) return false; g->used+=bytes; return true; }
static bool vr_gfs_header(const uint8_t *b,uint32_t type) { return pm_be32(b)==UINT32_C(0x01161970) && pm_be32(b+4)==type; }
static bool vr_gfs_block(vr_gfs *g,uint64_t block,uint8_t *b) { return vr_poll(g->pd) && block && block<=(uint64_t)pm_available(g->f)/g->block && block<=(uint64_t)(INT64_MAX/g->block) && pm_read(g->f,(int64_t)(block*g->block),b,g->block); }
static bool vr_gfs_tree(vr_gfs *g,mm_map *map,const uint8_t *p,size_t count,unsigned height,uint64_t *left,bool journal,unsigned depth) {
    size_t i; uint64_t leaf=g->block-(journal?24U:0U); uint8_t *indirect=NULL;
    if(!height || height>10 || depth>10) return false;
    for(i=0;i<count && *left;++i) { uint64_t block=mm_be64(p+i*8),bytes=*left<leaf?*left:leaf;
        if(!vr_poll(g->pd) || g->used>g->limit || map->count>=(g->limit-g->used)/(2*sizeof(mm_extent))) goto fail;
        if(height==1) {
            if(block>(uint64_t)(INT64_MAX-24)/g->block) goto fail;
            if(journal && block) { uint8_t header[24]; if(block>(uint64_t)(INT64_MAX/g->block) || !pm_read(g->f,(int64_t)(block*g->block),header,24) || !vr_gfs_header(header,7)) goto fail; }
            if(!mm_add(g->f,map,block?(int64_t)(block*g->block)+(journal?24:0):-1,bytes)) { goto fail; } *left-=bytes;
        } else if(!block) {
            unsigned h; uint64_t capacity=leaf; for(h=1;h<height;++h) { uint64_t slots=(g->block-24)/8; if(capacity>UINT64_MAX/slots) { capacity=UINT64_MAX; break; } capacity*=slots; }
            bytes=*left<capacity?*left:capacity; if(!mm_add(g->f,map,-1,bytes)) goto fail; *left-=bytes;
        } else {
            if(!indirect) { if(!vr_gfs_take(g,g->block)) goto fail; indirect=(uint8_t *)xx_mem_alloc(g->block); if(!indirect) { g->used-=g->block; goto fail; } }
            if(!vr_gfs_block(g,block,indirect) || !vr_gfs_header(indirect,5) || !vr_gfs_tree(g,map,indirect+24,(g->block-24)/8,height-1,left,journal,depth+1)) goto fail;
        }
    } if(indirect) g->used-=g->block; xx_mem_free(indirect); return true;
fail: if(indirect) g->used-=g->block; xx_mem_free(indirect); return false;
}
static mm_map *vr_gfs_map(vr_gfs *g,uint64_t block,const uint8_t *inode,bool directory) {
    mm_map *map=mm_create(); uint64_t size=mm_be64(inode+56),left=size; unsigned height=pm_be16(inode+138); bool journal=directory || (pm_be32(inode+128)&1);
    if(!map || size>INT64_MAX) { mm_free(map); return NULL; }
    if(!size) return map;
    if(!height) { if(size>g->block-232 || !mm_add(g->f,map,(int64_t)(block*g->block)+232,size)) { mm_free(map); return NULL; } }
    else if(!vr_gfs_tree(g,map,inode+232,(g->block-232)/8,height,&left,journal,0) || left) { mm_free(map); return NULL; }
    return map;
}
static bool vr_gfs_walk(vr_gfs *,uint64_t,const char *);
static bool vr_gfs_dirents(vr_gfs *g,const uint8_t *b,size_t at,size_t bytes,const char *parent) {
    while(at<bytes) { uint64_t inode; uint16_t record,name_size; char name[96],path[96]; size_t i;
        if(!vr_poll(g->pd) || bytes-at<40) { return false; } inode=mm_be64(b+at+8); record=pm_be16(b+at+20); name_size=pm_be16(b+at+22);
        if(record<40 || (record&7) || record>bytes-at || name_size>record-40 || name_size>=sizeof(name)) return false;
        if(inode) { if(!name_size) return false;
            for(i=0;i<name_size;++i) { uint8_t c=b[at+40+i]; if(c<32 || c>=127 || c=='/' || c=='\\' || c==':' || c=='"' || c=='<' || c=='>' || c=='|' || c=='?' || c=='*') return false; name[i]=(char)c; } name[name_size]=0;
            if(xx_rt_strcmp(name,".") && xx_rt_strcmp(name,"..")) {
                size_t length=xx_rt_strlen(parent); if(length+name_size+(length?1:0)>=sizeof(path)) return false;
                xx_rt_snprintf(path,sizeof(path),"%s%s%s",parent,length?"/":"",name); if(!vr_gfs_walk(g,inode,path)) return false;
            }
        } at+=record;
    } return at==bytes;
}
static bool vr_gfs_walk(vr_gfs *g,uint64_t block,const char *path) {
    uint8_t *inode=NULL,*contents=NULL; mm_map *map=NULL; uint64_t size=0,map_bytes=0; uint32_t mode,flags; bool ok=false; unsigned i;
    if(g->depth>=64 || g->s->count>=1000000) return false;
    for(i=0;i<g->depth;++i) if(g->ancestors[i]==block) return false;
    if(!vr_gfs_take(g,g->block)) return false;
    inode=(uint8_t *)xx_mem_alloc(g->block); if(!inode || !vr_gfs_block(g,block,inode) || !vr_gfs_header(inode,4) || mm_be64(inode+32)!=block) goto done;
    mode=pm_be32(inode+40)&0xF000; flags=pm_be32(inode+128); size=mm_be64(inode+56);
    if(mode==0x8000 || mode==0xA000) { const xx_var *v=xx_format_resolve_extra_parameter(g->f,((xx_volume *)g->f)->parse_options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
        if(v && size>xx_var_get_u64(v)) { goto done; } map=vr_gfs_map(g,block,inode,false); if(!map) goto done;
        if(!vr_gfs_take(g,sizeof(*map)+map->capacity*sizeof(mm_extent)+2*sizeof(pm_member))) goto done;
        if(!mm_emit(g->f,g->s,path,map)) { map=NULL; goto done; } map=NULL;
        xx_rt_snprintf(g->s->items[g->s->count-1].name,sizeof(g->s->items[g->s->count-1].name),"%s",path); ok=true;
    } else if(mode==0x4000) {
        pm_member member; if(size>g->limit || size>32U*1024U*1024U) goto done; map=vr_gfs_map(g,block,inode,true); if(!map) goto done;
        map_bytes=sizeof(*map)+map->capacity*sizeof(mm_extent);
        if(!vr_gfs_take(g,map_bytes)) { map_bytes=0; goto done; }
        if(!vr_gfs_take(g,size?size:1)) goto done;
        contents=(uint8_t *)xx_mem_alloc((size_t)size? (size_t)size:1); if(!contents) { g->used-=size?size:1; goto done; }
        xx_mem_zero(&member,sizeof(member)); member.context=map;
        if(!mm_read(g->f,&member,0,contents,(size_t)size,g->pd)) goto done;
        g->ancestors[g->depth++]=block;
        if(!(flags&2)) ok=vr_gfs_dirents(g,contents,0,(size_t)size,path);
        else {
            uint64_t *seen=NULL; size_t seen_count=0,slots=(size_t)size/8,j; uint8_t *leaf=NULL; uint64_t allocation=(slots?slots:1)*sizeof(*seen)+g->block;
            if((size&7) || slots>UINT64_C(1)<<17) { --g->depth; goto done; }
            if(!vr_gfs_take(g,allocation)) { --g->depth; goto done; }
            seen=(uint64_t *)xx_mem_alloc((slots?slots:1)*sizeof(*seen)); leaf=(uint8_t *)xx_mem_alloc(g->block); ok=seen && leaf;
            for(j=0;ok && j<slots;++j) { uint64_t current=mm_be64(contents+j*8); unsigned chain=0;
                while(current && ok) { size_t k; for(k=0;k<seen_count;++k) if(seen[k]==current) break;
                    /* Several hash slots may share the same initial leaf.
                     * A repeated leaf reached through lf_next is a cycle. */
                    if(k<seen_count) { if(chain) ok=false; break; }
                    if(seen_count>=slots || chain++>=4096 || !vr_gfs_block(g,current,leaf) || !vr_gfs_header(leaf,6)) { ok=false; break; }
                    seen[seen_count++]=current; ok=vr_gfs_dirents(g,leaf,104,g->block,path); current=mm_be64(leaf+32);
                }
            } xx_mem_free(seen); xx_mem_free(leaf); g->used-=allocation;
        } --g->depth;
    } else ok=true; /* Special device nodes have no extractable file bytes. */
done: g->used-=g->block; if(contents) g->used-=size?size:1; g->used-=map_bytes;
    xx_mem_free(inode); xx_mem_free(contents); mm_free(map); return ok;
}
static bool vr_gfs2(Abstractformat *f,pm_stream *s,xx_pd_struct *pd,const uint8_t *super) {
    vr_gfs g; xx_volume *r=(xx_volume *)f; const xx_var *v; xx_mem_zero(&g,sizeof(g)); g.f=f; g.s=s; g.pd=pd; g.block=(uint32_t)r->block_size; g.limit=UINT64_C(256)*1024*1024;
    v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MEMORY_LIMIT); if(v) g.limit=xx_var_get_u64(v);
    if(g.block*12U>g.limit || !vr_gfs_walk(&g,mm_be64(super+88),"")) return false;
    r->capability="GFS2 regular file and symlink-byte extraction: stuffed, direct, indirect, sparse and hashed directories";
    return vr_info(f,s,"Filesystem is read without mounting or journal replay. Symlink targets are exported as ordinary file bytes.\n");
}
