/* SPDX-License-Identifier: MIT. Original read-only legacy filesystem parsers.
 * Wire-format references and independent fixture provenance are recorded in
 * docs/LEGACY_FILESYSTEM_EXTRACTION.md. No upstream implementation is copied.
 * All file data stays on the borrowed source and is streamed through maps. */
typedef struct vl_fs {
    Abstractformat *f; pm_stream *s; xx_pd_struct *pd;
    uint64_t limit,used,blocks,ancestors[64]; unsigned depth;
    uint32_t bs,aux,options,index,first,last; bool be,small;
    uint8_t root[4096];
    mm_map *index_map;
} vl_fs;
typedef struct vl_member_context { mm_map *map; uint64_t retained,limit; } vl_member_context;
static void vl_member_free(void *opaque) { vl_member_context *c=(vl_member_context *)opaque; if(c) { mm_free(c->map); xx_mem_free(c); } }
static bool vl_member_unpack(Abstractformat *f,pm_member *m,xx_io_device *output,xx_pd_struct *pd) {
    vl_member_context *c=(vl_member_context *)m->context; const xx_var *v; uint64_t limit,at=0; size_t capacity=xx_get_file_buffer_size(); uint8_t *buffer=NULL; pm_member mapped;
    if(!c || !vr_poll(pd)) { return false; } limit=c->limit;
    v=xx_format_resolve_extra_parameter(f,((xx_volume *)f)->parse_options,XX_META_ID_OPT_MEMORY_LIMIT); if(v) limit=xx_var_get_u64(v);
    if(limit<c->retained || capacity>limit-c->retained) return false;
    if(c->map && m->size) { buffer=(uint8_t *)xx_mem_alloc(capacity); if(!buffer) return false; }
    xx_mem_zero(&mapped,sizeof(mapped)); mapped.context=c->map;
    while(at<(uint64_t)m->size) { size_t n=(uint64_t)m->size-at<capacity?(size_t)((uint64_t)m->size-at):capacity,done=0; const uint8_t *data=m->memory?m->memory+(size_t)at:buffer;
        if(!vr_poll(pd) || (c->map && !mm_read(f,&mapped,at,buffer,n,pd))) { xx_mem_free(buffer); return false; }
        while(output && done<n) { ssize_t amount=xx_io_write(output,data+done,n-done); if(amount<=0 || (size_t)amount>n-done) { xx_mem_free(buffer); return false; } done+=(size_t)amount; }
        at+=n;
    } xx_mem_free(buffer); return vr_poll(pd);
}
static bool vl_take(vl_fs *g,uint64_t n) { if(g->used>g->limit || n>g->limit-g->used) return false; g->used+=n; return true; }
static bool vl_read(vl_fs *g,uint64_t at,void *b,size_t n) { return vr_poll(g->pd) && at<=INT64_MAX && pm_read(g->f,(int64_t)at,b,n); }
static bool vl_block(vl_fs *g,uint64_t b,void *p,size_t n) { return b<g->blocks && n<=(g->blocks-b)*g->bs && vl_read(g,b*g->bs,p,n); }
static bool vl_member_limit(vl_fs *g,uint64_t n) { const xx_var *v=xx_format_resolve_extra_parameter(g->f,((xx_volume *)g->f)->parse_options,XX_META_ID_OPT_MAX_MEMBER_SIZE); return n<=INT64_MAX && (!v || n<=xx_var_get_u64(v)); }
/* Latin-1 names become UTF-8; path delimiters are permitted only by the caller.
 * Reject traversal and device-name punctuation instead of changing identity. */
static bool vl_name(const uint8_t *p,size_t n,char *out,size_t cap,bool path) {
    size_t i,k=0,start=0; while(n && (p[n-1]==' ' || !p[n-1])) --n;
    if(!n) return false;
    for(i=0;i<n;++i) { uint8_t c=p[i]; if(c<32 || c==127 || c=='\\' || c==':' || c=='"' || c=='<' || c=='>' || c=='|' || c=='?' || c=='*' || (c=='/' && !path)) return false;
        if(c=='/') { if(i==start || (i-start==1 && p[start]=='.') || (i-start==2 && p[start]=='.' && p[start+1]=='.')) return false; start=i+1; }
        if(k+(c>=128?2U:1U)>=cap) { return false; } if(c>=128) { out[k++]=(char)(0xC0|(c>>6)); out[k++]=(char)(0x80|(c&63)); } else out[k++]=(char)c;
    }
    if(start==n || (n-start==1 && p[start]=='.') || (n-start==2 && p[start]=='.' && p[start+1]=='.')) { return false; } out[k]=0; return true;
}
static bool vl_path(const char *parent,const char *name,char *path,size_t cap) { size_t a=xx_rt_strlen(parent),b=xx_rt_strlen(name); if(a+b+(a?1U:0U)>=cap) return false; xx_rt_snprintf(path,cap,"%s%s%s",parent,a?"/":"",name); return true; }
static bool vl_named(vl_fs *g,const char *name,mm_map *map,bool directory) {
    size_t n=xx_rt_strlen(name); pm_member *m; char *copy; vl_member_context *context=NULL;
    if(!vl_member_limit(g,map?map->size:0) || n>4095 || g->s->count>=131072 || !vl_take(g,2*sizeof(pm_member)+n+1)) { mm_free(map); return false; }
    copy=xx_str_dup(name); if(!copy) { mm_free(map); return false; }
    if(!vl_take(g,sizeof(*context)) || !(context=(vl_member_context *)xx_mem_alloc(sizeof(*context)))) { xx_mem_free(copy); mm_free(map); return false; } context->map=map; context->retained=0; context->limit=g->limit;
    if(map) { if(!mm_emit(g->f,g->s,name,map)) { xx_mem_free(context); xx_mem_free(copy); return false; } }
    else if(!pm_add(g->f,g->s,name,0,0)) { xx_mem_free(context); xx_mem_free(copy); return false; }
    m=&g->s->items[g->s->count-1]; m->display_name=copy; m->directory=directory;
    m->context=context; m->free_context=vl_member_free; m->read_all=vl_member_unpack; m->read_range=NULL; return true;
}
static mm_map *vl_map(vl_fs *g) { if(!vl_take(g,sizeof(mm_map))) return NULL; return mm_create(); }
static bool vl_extent(vl_fs *g,mm_map *m,int64_t at,uint64_t bytes) {
    size_t old=m->capacity,cap=old?old*2U:8U; if(!bytes) return true;
    if(m->count==old && !vl_take(g,(cap-old)*sizeof(mm_extent))) return false;
    return vr_poll(g->pd) && m->count<262144 && mm_add(g->f,m,at,bytes);
}
static bool vl_contiguous(vl_fs *g,const char *name,uint64_t at,uint64_t bytes) { mm_map *m=vl_map(g); if(!m || at>INT64_MAX || !vl_extent(g,m,(int64_t)at,bytes)) { mm_free(m); return false; } return vl_named(g,name,m,false); }
static bool vl_enter(vl_fs *g,uint64_t id) { unsigned i; if(g->depth>=24) return false; for(i=0;i<g->depth;++i) if(g->ancestors[i]==id) return false; g->ancestors[g->depth++]=id; return true; }
static uint8_t *vl_contents(vl_fs *g,mm_map *m) {
    uint8_t *b; pm_member member; if(m->size>16U*1024U*1024U || !vl_take(g,m->size?m->size:1)) return NULL;
    b=(uint8_t *)xx_mem_alloc((size_t)(m->size?m->size:1)); if(!b) return NULL;
    xx_mem_zero(&member,sizeof(member)); member.context=m;
    if(!mm_read(g->f,&member,0,b,(size_t)m->size,g->pd)) { xx_mem_free(b); g->used-=m->size?m->size:1; return NULL; } return b;
}
static bool vl_rad50(uint16_t v,char out[3]) { static const char alphabet[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789"; unsigned i; if(v>=64000) return false; for(i=3;i>0;--i) { out[i-1]=alphabet[v%40]; v/=40; } return true; }
static bool vl_rt11(vl_fs *g) {
    uint8_t hb[512],b[1024]; uint32_t first,current=1; uint64_t seen=0; unsigned chain=0; uint16_t max=0;
    if(!vl_read(g,512,hb,512)) { return false; } first=xx_data_get_u16(hb+468, 2, 0, false); if(first<2 || first>=g->blocks) return false;
    while(current) { uint32_t at,file; uint16_t next,extra; size_t pos; bool end=false;
        if(current>31 || (seen&(UINT64_C(1)<<current)) || ++chain>31) { return false; } seen|=UINT64_C(1)<<current;
        at=first+(current-1)*2; if(!vl_block(g,at,b,1024)) return false;
        if(!max) { max=xx_data_get_u16(b, 2, 0, false); } if(!max || max>31 || xx_data_get_u16(b, 2, 0, false)!=max || current>max || xx_data_get_u16(b+4, 2, 0, false)>max) return false;
        next=xx_data_get_u16(b+2, 2, 0, false); extra=xx_data_get_u16(b+6, 2, 0, false); file=xx_data_get_u16(b+8, 2, 0, false); if(extra>1000 || (extra&1) || next>max) return false;
        for(pos=10;pos+2<=1024;pos+=14+extra) { uint16_t status=xx_data_get_u16(b+pos, 2, 0, false),type=status&0x0F00,len; char raw[10],name[24]; size_t i,k=0;
            if(type==0x0800) { end=true; break; } if(pos+14+extra>1024 || (type!=0x0100 && type!=0x0200 && type!=0x0400)) return false;
            len=xx_data_get_u16(b+pos+8, 2, 0, false);
            /* Emulator disks may omit trailing unallocated zero sectors.
             * Bounds are required for live files, not free directory runs. */
            if(type==0x0400) { if(file>g->blocks || len>g->blocks-file || !vl_rad50(xx_data_get_u16(b+pos+2, 2, 0, false),raw) || !vl_rad50(xx_data_get_u16(b+pos+4, 2, 0, false),raw+3) || !vl_rad50(xx_data_get_u16(b+pos+6, 2, 0, false),raw+6)) return false;
                for(i=0;i<6 && raw[i]!=' ';++i) { name[k++]=raw[i]; } if(!k) return false;
                if(raw[6]!=' ') { name[k++]='.'; for(i=6;i<9 && raw[i]!=' ';++i) name[k++]=raw[i]; } name[k]=0;
                if(status&0x10) { uint8_t prefix; if(!len || !vl_read(g,(uint64_t)file*512,&prefix,1) || !prefix || prefix>len) return false; if(!vl_contiguous(g,name,(uint64_t)(file+prefix)*512,(uint64_t)(len-prefix)*512)) return false; }
                else if(!vl_contiguous(g,name,(uint64_t)file*512,(uint64_t)len*512)) return false;
            } file+=len;
        } if(!end) return false; current=next;
    } return true;
}
static bool vl_lif(vl_fs *g) {
    uint8_t hb[256],entry[32]; uint64_t first,size,i; char name[32]; bool ended=false;
    if(!vl_read(g,0,hb,256)) { return false; } first=xx_data_get_u32(hb+8, 4, 0, true); size=xx_data_get_u32(hb+16, 4, 0, true); if(!first || !size || first>g->blocks || size>g->blocks-first || size>16384) return false;
    for(i=0;i<size*8;++i) { uint16_t type; uint64_t at,len;
        if(!vl_read(g,first*256+i*32,entry,32)) { return false; } type=xx_data_get_u16(entry+10, 2, 0, true); if(type==0xFFFF) { ended=true; break; }
        if(entry[0]==0xFF || !entry[0] || entry[0]==' ') continue;
        if(!vl_name(entry,10,name,sizeof(name),false)) { return false; } at=xx_data_get_u32(entry+12, 4, 0, true); len=xx_data_get_u32(entry+16, 4, 0, true);
        if(at>g->blocks || len>g->blocks-at || !vl_contiguous(g,name,at*256,len*256)) return false;
    } return ended || i==size*8;
}
static bool vl_decimal(const uint8_t *p,size_t n,uint32_t *value,bool blank) { size_t i; uint32_t v=0; bool digit=false; for(i=0;i<n;++i) { if(p[i]==' ' && !digit) continue; if(p[i]<'0' || p[i]>'9') return false; digit=true; v=v*10+p[i]-'0'; } if(!digit && !blank) return false; *value=v; return true; }
static bool vl_ecma_addr(const uint8_t *p,bool two,uint32_t *lba) { uint32_t c,s,n; if(!vl_decimal(p,2,&c,false) || !vl_decimal(p+2,1,&s,false) || !vl_decimal(p+3,2,&n,false) || c>34 || s>(two?1U:0U) || !n || n>16) return false; *lba=c*(two?32U:16U)+s*16+n-1; return true; }
static uint64_t vl_ecma_offset(uint32_t lba) { return lba<16?(uint64_t)lba*128:2048+(uint64_t)(lba-16)*256; }
static bool vl_ecma(vl_fs *g) {
    uint8_t vol[128],hdr[128],err[128]; unsigned i,end; bool two; uint64_t media; char name[64];
    if(!vl_read(g,768,vol,128) || !vl_read(g,512,err,128)) { return false; } two=vol[71]=='M'; if(vol[71]!=' ' && vol[71]!='1' && !two) return false;
    media=2048+(uint64_t)((two?35U*32U:35U*16U)-16)*256; if((uint64_t)pm_available(g->f)<media) return false;
    /* Raw sectors have no deleted-data mark, so error-map remapping requires
     * an image-layer decoder; non-empty replacement lists are rejected. */
    if(!xx_rt_memcmp(err,"ERMAP",5)) { for(i=5;i<128;++i) if(err[i]!=' ' && err[i]!='0' && err[i]!=0) return false; }
    end=two && vol[78]=='1'?32:16;
    for(i=7;i<end;++i) { uint32_t first,last,eod=0,block=256,unused=0; uint64_t sectors,left; mm_map *m; uint32_t lba;
        if(!vl_read(g,vl_ecma_offset(i),hdr,128)) { return false; } if(xx_rt_memcmp(hdr,"HDR",3)) continue;
        if(hdr[3]!='1' && hdr[3]!=1) { return false; } if(!vl_name(hdr+5,17,name,sizeof(name),false) || !vl_ecma_addr(hdr+28,two,&first) || !vl_ecma_addr(hdr+34,two,&last) || first<(two?32U:16U) || last<first) return false;
        if(hdr[44]!=' ' || (hdr[63]!=' ' && hdr[63]!='S')) return false;
        if(!vl_decimal(hdr+22,5,&block,true) || !vl_decimal(hdr+57,5,&unused,true)) { return false; } if(!block) block=256; if(block>256 || unused>block) return false;
        sectors=(uint64_t)last-first+1; if(vl_ecma_addr(hdr+74,two,&eod)) { if(eod<first || eod>last+1) return false; sectors=eod-first; }
        left=sectors*block; if(sectors) left-=unused; if(!vl_member_limit(g,left)) return false;
        m=vl_map(g); if(!m) return false;
        for(lba=first;left && lba<=last;++lba) { uint64_t n=left<block?left:block; if(!vl_extent(g,m,(int64_t)vl_ecma_offset(lba),n)) { mm_free(m); return false; } left-=n; }
        if(left || !vl_named(g,name,m,false)) return false;
    } return true;
}
/* Locus: 128/512-byte inodes, 10 direct + 3 levels of 32-bit pointers. */
static bool vl_locus_pointer(vl_fs *g,uint64_t ptr,unsigned level,uint64_t logical,uint64_t *out) {
    uint8_t b[4]; uint64_t stride=1,i,slots=g->bs/4; *out=0; if(!ptr) return true; if(!level || level>3 || ptr>=g->blocks) return false;
    for(i=1;i<level;++i) { stride*=slots; } if(logical>=stride*slots) return false;
    if(!vl_read(g,ptr*g->bs+(logical/stride)*4,b,4)) { return false; } ptr=xx_data_get_u32(b, 4, 0, g->be);
    if(level==1) { *out=ptr; return ptr<g->blocks; } return vl_locus_pointer(g,ptr,level-1,logical%stride,out);
}
static mm_map *vl_locus_map(vl_fs *g,uint32_t ino,uint8_t inode[512],uint16_t *mode,uint16_t *flags) {
    uint32_t isize=g->small?512:128,per=g->bs/isize; uint64_t at,size,logical=0,left; mm_map *m;
    if(!ino || ino>(uint64_t)g->aux*per) { return NULL; } at=((uint64_t)(ino-1)/per+2)*g->bs+(ino-1)%per*isize;
    if(!vl_read(g,at,inode,isize)) { return NULL; } *mode=xx_data_get_u16(inode, 2, 0, g->be); *flags=xx_data_get_u16(inode+14, 2, 0, g->be); size=xx_data_get_u32(inode+16, 4, 0, g->be);
    if(!(*flags&0x10) || (*flags&1) || !(*flags&2) || !xx_data_get_u16(inode+2, 2, 0, g->be) || !vl_member_limit(g,size)) return NULL;
    m=vl_map(g); if(!m) return NULL; left=size;
    if(g->small && (inode[75]&1)) { if(size>384 || !vl_extent(g,m,(int64_t)(at+128),size)) { mm_free(m); return NULL; } return m; }
    while(left) { uint64_t ptr=0,n=left<g->bs?left:g->bs,rem,slots=g->bs/4;
        if(logical<10) ptr=xx_data_get_u32(inode+76+logical*4, 4, 0, g->be);
        else { rem=logical-10; if(rem<slots) { if(!vl_locus_pointer(g,xx_data_get_u32(inode+116, 4, 0, g->be),1,rem,&ptr)) goto fail; }
            else if((rem-=slots)<slots*slots) { if(!vl_locus_pointer(g,xx_data_get_u32(inode+120, 4, 0, g->be),2,rem,&ptr)) goto fail; }
            else { rem-=slots*slots; if(!vl_locus_pointer(g,xx_data_get_u32(inode+124, 4, 0, g->be),3,rem,&ptr)) goto fail; }
        } if(ptr>=g->blocks || !vl_extent(g,m,ptr?(int64_t)(ptr*g->bs):-1,n)) goto fail; left-=n; ++logical;
    } return m;
fail: mm_free(m); return NULL;
}
static bool vl_locus_walk(vl_fs *g,uint32_t ino,const char *path) {
    uint8_t inode[512],*b=NULL; uint16_t mode,flags; mm_map *m; uint64_t size=0; bool ok=false; size_t p=0;
    if(!vl_enter(g,ino)) { return false; } m=vl_locus_map(g,ino,inode,&mode,&flags); if(!m) goto done;
    mode&=0xF000; size=m->size;
    if(mode==0x8000 || (flags&0x80)) { ok=vl_named(g,path,m,false); m=NULL; goto done; }
    if(mode!=0x4000) { ok=true; goto done; }
    if(path[0] && !vl_named(g,path,NULL,true)) { goto done; } b=vl_contents(g,m); if(!b) goto done;
    while(p<size) { uint32_t child; size_t stride,n,off; char name[512],full[4096];
        if(flags&0x40) { if(size-p<8) goto done; child=xx_data_get_u32(b+p, 4, 0, g->be); stride=xx_data_get_u16(b+p+4, 2, 0, g->be); n=xx_data_get_u16(b+p+6, 2, 0, g->be); off=8; if(stride<8 || (stride&3) || stride>size-p || n>stride-8) goto done; }
        else { if(size-p<16) goto done; child=xx_data_get_u16(b+p, 2, 0, g->be); stride=16; n=14; off=2; }
        if(child) { while(n && !b[p+off+n-1]) --n; if(n==1 && b[p+off]=='.') {} else if(n==2 && b[p+off]=='.' && b[p+off+1]=='.') {} else {
            if(!vl_name(b+p+off,n,name,sizeof(name),false) || !vl_path(path,name,full,sizeof(full)) || !vl_locus_walk(g,child,full)) goto done;
        } } p+=stride;
    } ok=true;
done: if(b) { g->used-=size?size:1; xx_mem_free(b); } mm_free(m); --g->depth; return ok;
}
static bool vl_locus(vl_fs *g) { uint8_t b[128]; unsigned i; for(i=0;i<=8;++i) { uint32_t magic; if(!vl_read(g,(uint64_t)i*512,b,128)) continue; magic=xx_data_get_u32(b, 4, 0, false); if(magic==UINT32_C(0xFFEEDDCD) || magic==UINT32_C(0xFFEEDDCC) || magic==UINT32_C(0xCDDDEEFF) || magic==UINT32_C(0xCCDDEEFF)) break; }
    if(i>8 || (b[71]!=1 && b[71]!=2)) { return false; } g->be=xx_data_get_u32(b, 4, 0, true)==UINT32_C(0xFFEEDDCD) || xx_data_get_u32(b, 4, 0, true)==UINT32_C(0xFFEEDDCC); g->small=b[71]==1; g->bs=g->small?4096:1024; g->blocks=xx_data_get_u32(b+8, 4, 0, g->be); g->aux=xx_data_get_u32(b+36, 4, 0, g->be);
    if(!g->blocks || g->blocks>(uint64_t)pm_available(g->f)/g->bs || !g->aux || g->aux>g->blocks-2 || (xx_data_get_u16(b+42, 2, 0, g->be)&0x120)) { return false; } return vl_locus_walk(g,2,"");
}
/* Professional File System: index/superindex, anodes and directory blocks. */
static bool vl_pfs_reserved(vl_fs *g,uint32_t block,uint8_t *b) { return block>=g->first && block<=g->last && g->aux/512<=g->last-block+1 && vl_block(g,block,b,g->aux); }
static bool vl_pfs_anode(vl_fs *g,uint32_t nr,uint32_t *count,uint32_t *block,uint32_t *next) {
    uint8_t b[4096]; uint32_t per=(g->aux-16)/12,seq,slot,ip=(g->aux-12)/4,which,idx,addr; size_t at;
    if(g->options&2) { seq=nr>>16; slot=nr&65535; } else { seq=nr/per; slot=nr%per; }
    if(slot>=per) { return false; } which=seq/ip; idx=seq%ip;
    if(g->options&128) { unsigned super=which/ip; if(super>=16 || !g->index || !vl_pfs_reserved(g,g->index,b) || xx_data_get_u16(b, 2, 0, true)!=0x4558) return false;
        addr=xx_data_get_u32(b+64+super*4, 4, 0, true); if(!addr || !vl_pfs_reserved(g,addr,b) || xx_data_get_u16(b, 2, 0, true)!=0x5342 || xx_data_get_u32(b+8, 4, 0, true)!=super) return false; addr=xx_data_get_u32(b+12+(which%ip)*4, 4, 0, true);
    } else { if(which>=99 || 116+which*4+4>g->aux) return false; addr=xx_data_get_u32(g->root+116+which*4, 4, 0, true); }
    if(!addr || !vl_pfs_reserved(g,addr,b) || xx_data_get_u16(b, 2, 0, true)!=0x4942 || xx_data_get_u32(b+8, 4, 0, true)!=which) { return false; } addr=xx_data_get_u32(b+12+idx*4, 4, 0, true);
    if(!addr || !vl_pfs_reserved(g,addr,b) || xx_data_get_u16(b, 2, 0, true)!=0x4142 || xx_data_get_u32(b+8, 4, 0, true)!=seq) { return false; } at=16+slot*12;
    *count=xx_data_get_u32(b+at, 4, 0, true); *block=xx_data_get_u32(b+at+4, 4, 0, true); *next=xx_data_get_u32(b+at+8, 4, 0, true); return true;
}
static mm_map *vl_pfs_map(vl_fs *g,uint32_t nr,uint64_t size) {
    mm_map *m=vl_map(g); uint64_t left=size; uint32_t seen[4096]; unsigned steps=0;
    if(!m || !vl_member_limit(g,size)) goto fail;
    while(left) { uint32_t count,block,next; uint64_t n; unsigned i; if(!nr || steps>=4096) goto fail; for(i=0;i<steps;++i) if(seen[i]==nr) goto fail; seen[steps++]=nr;
        if(!vl_pfs_anode(g,nr,&count,&block,&next) || !count || block>=g->blocks || count>g->blocks-block) { goto fail; } n=(uint64_t)count*512; if(n>left) n=left;
        if(!vl_extent(g,m,(int64_t)((uint64_t)block*512),n)) { goto fail; } left-=n; nr=next;
    } return m;
fail: mm_free(m); return NULL;
}
static bool vl_pfs_extra(const uint8_t *e,size_t size,uint16_t out[11]) { uint16_t flags; unsigned i; size_t at=size; if(at<2) return false; at-=2; flags=xx_data_get_u16(e+at, 2, 0, true); for(i=0;i<11;++i) { out[i]=0; if(flags&(1U<<i)) { if(at<2) return false; at-=2; out[i]=xx_data_get_u16(e+at, 2, 0, true); } } if(flags&0xF800) return false; return at>=(size_t)(18+e[17]+1+e[18+e[17]]); }
static bool vl_pfs_dir(vl_fs *g,uint32_t nr,const char *path) {
    uint8_t b[4096]; uint32_t seen[4096]; unsigned steps=0; bool ok=false;
    if(!vl_enter(g,nr)) { return false; } if(path[0] && !vl_named(g,path,NULL,true)) goto done;
    while(nr) { uint32_t count,block,next,j; unsigned i;
        if(steps>=4096) { goto done; } for(i=0;i<steps;++i) if(seen[i]==nr) goto done; seen[steps++]=nr;
        if(!vl_pfs_anode(g,nr,&count,&block,&next) || !count || block>=g->blocks || count>g->blocks-block || count>262144) goto done;
        for(j=0;j<count;j+=g->aux/512) { size_t at=20; bool end=false;
            if(count-j<g->aux/512 || !vl_block(g,block+j,b,g->aux) || xx_data_get_u16(b, 2, 0, true)!=0x4442) goto done;
            while(at<g->aux) { size_t len=b[at],names; int type; uint32_t child; uint64_t size; uint16_t extra[11]={0}; char name[512],full[4096]; mm_map *map;
                if(!len) { end=true; break; } if(len<20 || (len&1) || len>g->aux-at) goto done; names=b[at+17]; if(!names || 18+names+1>len || 19+names+b[at+18+names]>len) goto done;
                if(!vl_name(b+at+18,names,name,sizeof(name),false) || !vl_path(path,name,full,sizeof(full))) goto done;
                type=(int8_t)b[at+1]; child=xx_data_get_u32(b+at+2, 4, 0, true); size=xx_data_get_u32(b+at+6, 4, 0, true);
                if((g->options&4) && !vl_pfs_extra(b+at,len,extra)) { goto done; } if(g->options&2048) size|=(uint64_t)extra[10]<<32;
                if(type==2) { if(!vl_pfs_dir(g,child,full)) goto done; }
                else if(type==-3 || type==3) { map=vl_pfs_map(g,child,size); if(!map || !vl_named(g,full,map,false)) goto done; }
                else if(type==-4) { uint32_t target=(uint32_t)extra[0]<<16|extra[1]; if(!target) goto done; map=vl_pfs_map(g,target,size); if(!map || !vl_named(g,full,map,false)) goto done; }
                else { goto done; } at+=len;
            } if(!end) goto done;
        } nr=next;
    } ok=true;
done: --g->depth; return ok;
}
static bool vl_pfs(vl_fs *g) { uint8_t b[512]; if(!vl_read(g,1024,b,512)) return false; g->options=xx_data_get_u32(b+4, 4, 0, true); g->first=xx_data_get_u32(b+56, 4, 0, true); g->last=xx_data_get_u32(b+52, 4, 0, true); g->aux=xx_data_get_u16(b+64, 2, 0, true); g->index=xx_data_get_u32(b+88, 4, 0, true);
    if(g->aux<512 || g->aux>4096 || !vr_power(g->aux) || g->first>g->last || g->last>=g->blocks || !vl_read(g,1024,g->root,g->aux)) return false;
    if((g->options&16) && xx_data_get_u32(b+84, 4, 0, true)!=g->blocks) { return false; } return vl_pfs_dir(g,5,"");
}
/* HPOFS: directory DATA leaves hold full paths and explicit data extents. */
static bool vl_hpofs_subf(vl_fs *g,mm_map *m,uint32_t nr,uint64_t *left) {
    uint8_t b[4096]; unsigned count,i; if(!nr || nr==UINT32_MAX) return !*left;
    if(!vl_block(g,nr,b,g->bs) || xx_rt_memcmp(b,"SUBF",4)) { return false; } count=xx_data_get_u16(b+16, 2, 0, true); if(count>(g->bs-32)/8) return false;
    for(i=0;i<count && *left;++i) { uint32_t block=xx_data_get_u32(b+32+i*8+4, 4, 0, true); uint64_t n=(uint64_t)xx_data_get_u16(b+32+i*8, 2, 0, true)*g->bs; if(block==UINT32_MAX) break; if(!n || block>=g->blocks || n>(g->blocks-block)*g->bs) return false; if(n>*left) n=*left; if(!vl_extent(g,m,(int64_t)((uint64_t)block*g->bs),n)) return false; *left-=n; } return !*left;
}
static bool vl_hpofs_node(vl_fs *g,uint32_t nr,bool *leaf) {
    uint8_t *b; size_t bytes=(size_t)g->bs*4,p=36; bool ok=false,end=false; *leaf=false;
    if(!vl_take(g,bytes)) { return false; } b=(uint8_t *)xx_mem_alloc(bytes); if(!b) return false;
    if(!vl_block(g,nr,b,bytes)) { goto done; } if(!xx_rt_memcmp(b,"INDX",4)) { ok=xx_data_get_u16(b+20, 2, 0, true)>0; goto done; }
    if(xx_rt_memcmp(b,"DATA",4) || xx_data_get_u16(b+20, 2, 0, true)!=0) { goto done; } *leaf=true;
    while(p+4<=bytes) { size_t kl=xx_data_get_u16(b+p, 2, 0, true),rd=xx_data_get_u16(b+p+2, 2, 0, true); const uint8_t *e; char name[4096]; uint64_t size,left; mm_map *map;
        if(!kl) { end=true; break; } if(kl<5 || kl>1028 || rd<92 || kl+rd+4>bytes-p || !vl_name(b+p+8,kl-4,name,sizeof(name),true)) goto done; e=b+p+4+kl;
        if(xx_data_get_u32(e, 4, 0, true)!=xx_data_get_u32(e+4, 4, 0, true) || (e[12]!=0x20 && e[12]!=0x60)) goto done;
        if(e[12]==0x20) { if(e[14]&0x10) { if(!vl_named(g,name,NULL,true)) goto done; }
            else { size=xx_data_get_u32(e+88, 4, 0, true); left=size; if(!vl_member_limit(g,size)) goto done; map=vl_map(g); if(!map) goto done;
                if(left && xx_data_get_u16(e+64, 2, 0, true)) { uint32_t start=xx_data_get_u32(e+68, 4, 0, true); uint64_t n=(uint64_t)xx_data_get_u16(e+64, 2, 0, true)*g->bs; if(n>left) n=left; if(!vl_extent(g,map,(int64_t)((uint64_t)start*g->bs),n)) { mm_free(map); goto done; } left-=n; }
                if(left && !vl_hpofs_subf(g,map,xx_data_get_u32(e+76, 4, 0, true),&left)) { mm_free(map); goto done; } if(left || !vl_named(g,name,map,false)) goto done;
            }
        } p+=4+kl+rd;
    } ok=end;
done: xx_mem_free(b); g->used-=bytes; return ok;
}
static bool vl_hpofs(vl_fs *g) {
    uint8_t b[4096]; uint32_t nr,starts[508],counts[508]; unsigned n,i; bool leaf=false,found=false;
    if(g->bs>4096 || !vl_block(g,13,b,g->bs) || xx_rt_memcmp(b,"MEDINFO ",8) || !vl_block(g,14,b,g->bs) || xx_rt_memcmp(b,"VOLINFO ",8) || !vl_block(g,6,b,g->bs) || xx_data_get_u32(b, 4, 0, true)!=3) { return false; } nr=xx_data_get_u32(b+76, 4, 0, true);
    if(!nr || !vl_block(g,nr,b,g->bs) || xx_rt_memcmp(b,"SUBF",4)) { return false; } n=xx_data_get_u16(b+16, 2, 0, true); if(!n || n>(g->bs-32)/8) return false;
    for(i=0;i<n;++i) { counts[i]=xx_data_get_u16(b+32+i*8, 2, 0, true); starts[i]=xx_data_get_u32(b+36+i*8, 4, 0, true); if(starts[i]==UINT32_MAX) { n=i; break; } if(!counts[i] || starts[i]>=g->blocks || counts[i]>g->blocks-starts[i] || counts[i]>262144) return false; }
    for(i=0;i<n;++i) { unsigned j; for(j=i+1;j<n;++j) if(starts[j]<starts[i]) { uint32_t a=starts[i],c=counts[i]; starts[i]=starts[j]; counts[i]=counts[j]; starts[j]=a; counts[j]=c; } }
    for(i=0;i<n;++i) { uint32_t off; if(i && starts[i]<starts[i-1]+counts[i-1]) return false;
        for(off=0;off+4<=counts[i];off+=4) { if(!vl_hpofs_node(g,starts[i]+off,&leaf)) return false; found|=leaf; }
        /* Optical directory B-tree leaves also occupy the gaps between SUBF
         * runs. Examine each possible sector alignment within that bounded
         * directory region, and parse only complete level-zero DATA nodes. */
        if(i+1<n) { uint32_t at=starts[i]+counts[i],end=starts[i+1]; uint8_t signature[22];
            if(end<at || (uint64_t)(end-at)*g->bs>UINT64_C(64)*1024*1024) return false;
            while(at+4<=end) { if(!vl_block(g,at,signature,sizeof(signature))) return false;
                if(!xx_rt_memcmp(signature,"DATA",4) && !xx_data_get_u16(signature+20, 2, 0, true)) { if(!vl_hpofs_node(g,at,&leaf)) return false; found|=leaf; at+=4; }
                else ++at;
            }
        }
    }
    return found;
}
/* DEC ODS-2/5: header checksums, INDEXF.SYS mapping, all retrieval-pointer
 * encodings, extension headers, versioned names and hierarchical directories.
 * RMS record bytes are exported as stored; the record layout is not changed. */
static uint16_t vl_ods_sum(const uint8_t *b,size_t n) { uint16_t v=0; size_t i; for(i=0;i<n;i+=2) v=(uint16_t)(v+xx_data_get_u16(b+i, 2, 0, false)); return v; }
static uint32_t vl_ods_fid(const uint8_t *p) { return xx_data_get_u16(p, 2, 0, false)|((uint32_t)p[5]<<16); }
static bool vl_ods_header(vl_fs *g,uint32_t id,uint16_t seq,uint8_t b[512]) {
    uint64_t lbn=(uint64_t)g->aux+id-1; pm_member member;
    if(!id || id>g->last) return false;
    if(g->index_map) { uint64_t at=((uint64_t)g->index+id-2)*512; xx_mem_zero(&member,sizeof(member)); member.context=g->index_map; if(!mm_read(g->f,&member,at,b,512,g->pd)) return false; }
    else if(!vl_block(g,lbn,b,512)) return false;
    return vl_ods_sum(b,510)==xx_data_get_u16(b+510, 2, 0, false) && (xx_data_get_u16(b+6, 2, 0, false)>>8==2 || xx_data_get_u16(b+6, 2, 0, false)>>8==5) && vl_ods_fid(b+8)==id && (!seq || xx_data_get_u16(b+10, 2, 0, false)==seq) && !b[12] && b[0]>=40 && b[0]<=b[1] && b[1]<=b[2] && b[2]<=b[3] && (size_t)(b[1]+b[58])<=b[2];
}
static mm_map *vl_ods_map(vl_fs *g,const uint8_t original[512],uint64_t size) {
    uint8_t header[512]; uint32_t seen[1024]; unsigned steps=0; uint64_t left=size; mm_map *m=vl_map(g);
    if(!m || !vl_member_limit(g,size)) { goto fail; } xx_rt_memcpy(header,original,512);
    for(;;) { size_t at=(size_t)header[1]*2,end=at+(size_t)header[58]*2; uint32_t ext,id; uint16_t seq;
        while(at<end) { uint16_t w=xx_data_get_u16(header+at, 2, 0, false); unsigned format=w>>14; uint64_t count,lbn,n;
            if(format==0) { at+=2; continue; }
            if(end-at<(format==1?4U:format==2?6U:8U)) goto fail;
            if(format==1) { count=(w&255)+1U; lbn=(uint32_t)(w&0x3F00)<<8|xx_data_get_u16(header+at+2, 2, 0, false); at+=4; }
            else if(format==2) { count=(w&0x3FFF)+1U; lbn=xx_data_get_u32(header+at+2, 4, 0, false); at+=6; }
            else { count=((uint64_t)(w&0x3FFF)<<16|xx_data_get_u16(header+at+2, 2, 0, false))+1; lbn=xx_data_get_u32(header+at+4, 4, 0, false); at+=8; }
            if(lbn>=g->blocks || count>g->blocks-lbn) { goto fail; } n=count*512; if(n>left) n=left; if(n && !vl_extent(g,m,(int64_t)(lbn*512),n)) goto fail; left-=n;
        }
        ext=vl_ods_fid(header+14); seq=xx_data_get_u16(header+16, 2, 0, false); if(!ext) break; if(steps>=1024) goto fail;
        for(id=0;id<steps;++id) { if(seen[id]==ext) goto fail; } seen[steps++]=ext;
        if(header[18] || !vl_ods_header(g,ext,seq,header) || xx_data_get_u16(header+4, 2, 0, false)!=steps) goto fail;
    } if(left) goto fail; return m;
fail: mm_free(m); return NULL;
}
static bool vl_ods_walk(vl_fs *g,uint32_t id,uint16_t sequence,const char *path) {
    uint8_t header[512],*b=NULL; mm_map *m=NULL; uint64_t size=0; uint32_t endblk; bool dir,ok=false; size_t block;
    if(!vl_enter(g,id)) { return false; } if(!vl_ods_header(g,id,sequence,header)) goto done;
    endblk=(uint32_t)xx_data_get_u16(header+28, 2, 0, false)<<16|xx_data_get_u16(header+30, 2, 0, false); if(xx_data_get_u16(header+32, 2, 0, false)>511) goto done;
    size=endblk?((uint64_t)endblk-1)*512+xx_data_get_u16(header+32, 2, 0, false):0; dir=(xx_data_get_u32(header+52, 4, 0, false)&0x2000)!=0;
    if(xx_data_get_u32(header+52, 4, 0, false)&0x8000) { goto done; } m=vl_ods_map(g,header,size); if(!m) goto done;
    if(!dir) { ok=vl_named(g,path,m,false); m=NULL; goto done; }
    if(path[0] && !vl_named(g,path,NULL,true)) { goto done; } b=vl_contents(g,m); if(!b) goto done;
    for(block=0;block<size;block+=512) { size_t p=block,end=block+512<size?block+512:(size_t)size;
        while(p+2<=end) { size_t record=xx_data_get_u16(b+p, 2, 0, false),names,values,next; unsigned encoding; char name[512];
            if(record==0 || record==65535) { break; } next=p+record+2;
            if(record<14 || next>end || record&1 || (b[p+4]&7)) { goto done; } names=b[p+5]; encoding=(b[p+4]>>3)&7;
            values=p+6+((names+1)&~1U); if(!names || values+8>next || (next-values)%8) goto done;
            if(encoding==3) { size_t i,k=0; if(names&1) goto done; for(i=0;i<names;i+=2) { uint16_t c=xx_data_get_u16(b+p+6+i, 2, 0, false); if(c<32 || c>=128 || c=='/' || c=='\\' || c==':') goto done; name[k++]=(char)c; } name[k]=0; }
            else if(encoding>1 || !vl_name(b+p+6,names,name,sizeof(name),false)) goto done;
            for(;values<next;values+=8) { uint32_t child=vl_ods_fid(b+values+2); uint16_t seq=xx_data_get_u16(b+values+4, 2, 0, false),version=xx_data_get_u16(b+values, 2, 0, false); char full[4096],versioned[540];
                if(!child || !version || b[values+6]) { goto done; } if(child==id) continue;
                xx_rt_snprintf(versioned,sizeof(versioned),"%s;%u",name,version);
                if(!vl_path(path,versioned,full,sizeof(full)) || !vl_ods_walk(g,child,seq,full)) goto done;
            } p=next;
        }
    } ok=true;
done: if(b) { g->used-=size?size:1; xx_mem_free(b); } mm_free(m); --g->depth; return ok;
}
static bool vl_ods(vl_fs *g) {
    uint8_t hb[512],index[512]; uint32_t high; bool ok;
    if(!vl_read(g,512,hb,512) || vl_ods_sum(hb,58)!=xx_data_get_u16(hb+58, 2, 0, false) || vl_ods_sum(hb,510)!=xx_data_get_u16(hb+510, 2, 0, false) || (xx_data_get_u16(hb+12, 2, 0, false)>>8!=2 && xx_data_get_u16(hb+12, 2, 0, false)>>8!=5) || xx_data_get_u16(hb+38, 2, 0, false)) return false;
    g->aux=xx_data_get_u32(hb+24, 4, 0, false)+xx_data_get_u16(hb+32, 2, 0, false); g->index=xx_data_get_u16(hb+22, 2, 0, false)+xx_data_get_u16(hb+32, 2, 0, false); g->last=xx_data_get_u32(hb+28, 4, 0, false);
    if(!xx_data_get_u32(hb+24, 4, 0, false) || !xx_data_get_u16(hb+32, 2, 0, false) || !g->index || g->last<5 || g->last>0xFFFFFF || !vl_ods_header(g,1,0,index)) return false;
    high=(uint32_t)xx_data_get_u16(index+24, 2, 0, false)<<16|xx_data_get_u16(index+26, 2, 0, false); if(!high || high>g->blocks) return false;
    g->index_map=vl_ods_map(g,index,(uint64_t)high*512); if(!g->index_map) return false;
    ok=vl_ods_walk(g,4,0,""); mm_free(g->index_map); g->index_map=NULL; return ok;
}
/* Cray NC1FS: 4096-byte blocks and 256-byte inodes. Cross-device NC1/NC2
 * allocations need companion sources; fail instead of assuming they reside
 * in this image. Direct extent allocations are the producer's normal layout. */
static mm_map *vl_unicos_map(vl_fs *g,uint64_t ino,uint8_t inode[256],uint32_t *mode) {
    uint64_t part=(ino>>24)&255,region=(ino>>20)&15,relative=ino&0xFFFFF,start,count,bitmap,at,size,left; mm_map *m; unsigned i;
    if(ino>>32 || part || region>=4 || !relative) return NULL;
    count=xx_data_get_u16(g->root+184+16+region*8+2, 2, 0, true); start=xx_data_get_u32(g->root+184+16+region*8+4, 4, 0, true); bitmap=(xx_data_get_u64(g->root+3280, 8, 0, true)+1)*(xx_data_get_u64(g->root+3264, 8, 0, true)+1);
    if(!count || start>=g->blocks || count>g->blocks-start || bitmap>=count || relative/16>=count-bitmap) return NULL;
    at=(start+bitmap+relative/16)*4096+(relative%16)*256; if(!vl_read(g,at,inode,256)) return NULL;
    *mode=xx_data_get_u32(inode, 4, 0, true)&0xFFFFFF; size=xx_data_get_u64(inode+24, 8, 0, true); left=size;
    if(!(xx_data_get_u32(inode+4, 4, 0, true)&0x1FFFF) || (inode[88]&15)!=1 || (inode[81]&31) || xx_data_get_u16(inode+238, 2, 0, true) || !vl_member_limit(g,size)) return NULL;
    m=vl_map(g); if(!m) return NULL;
    for(i=0;i<8 && left;++i) { uint64_t blocks=xx_data_get_u32(inode+128+i*8, 4, 0, true),block=xx_data_get_u32(inode+132+i*8, 4, 0, true),n;
        if(!blocks || blocks&UINT32_C(0x80000000) || block>=g->blocks || blocks>g->blocks-block) { goto fail; } n=blocks*4096; if(n>left) n=left;
        if(!vl_extent(g,m,(int64_t)(block*4096),n)) { goto fail; } left-=n;
    } if(left) goto fail; return m;
fail: mm_free(m); return NULL;
}
static bool vl_unicos_walk(vl_fs *g,uint64_t ino,const char *path) {
    uint8_t inode[256],*b=NULL; uint32_t mode; mm_map *m=NULL; uint64_t size=0; bool ok=false; size_t p=0;
    if(!vl_enter(g,ino)) { return false; } m=vl_unicos_map(g,ino,inode,&mode); if(!m) goto done; size=m->size; mode&=0xF000;
    if(mode==0x8000 || mode==0xA000) { ok=vl_named(g,path,m,false); m=NULL; goto done; }
    if(mode!=0x4000) { ok=true; goto done; } if(path[0] && !vl_named(g,path,NULL,true)) goto done; b=vl_contents(g,m); if(!b) goto done;
    while(p<size) { uint64_t child,word; size_t stride,n; char name[512],full[4096];
        if(size-p<24) { goto done; } child=xx_data_get_u64(b+p, 8, 0, true); word=xx_data_get_u64(b+p+16, 8, 0, true); stride=(size_t)((word>>10)&0x3FFFFF); n=(size_t)(word&1023);
        if(stride<24 || stride>size-p || stride&7 || n>255 || n>stride-24 || stride>4096-p%4096) goto done;
        if(child) { if(!n) goto done; if(n==1 && b[p+24]=='.') {} else if(n==2 && b[p+24]=='.' && b[p+25]=='.') {} else {
            if(!vl_name(b+p+24,n,name,sizeof(name),false) || !vl_path(path,name,full,sizeof(full)) || !vl_unicos_walk(g,child,full)) goto done;
        } } else if(n) goto done; p+=stride;
    } ok=true;
done: if(b) { g->used-=size?size:1; xx_mem_free(b); } mm_free(m); --g->depth; return ok;
}
static bool vl_unicos(vl_fs *g) {
    uint64_t root; if(!vl_read(g,0,g->root,4096)) return false;
    if(xx_data_get_u64(g->root, 8, 0, true)!=UINT64_C(0x6E6331667331636E) && (!vl_read(g,4096,g->root,4096) || xx_data_get_u64(g->root, 8, 0, true)!=UINT64_C(0x6E6331667331636E))) return false;
    if(xx_data_get_u64(g->root+152, 8, 0, true)!=1 || xx_data_get_u64(g->root+3280, 8, 0, true)>15 || xx_data_get_u64(g->root+3264, 8, 0, true)>15) return false;
    g->bs=4096; g->blocks=xx_data_get_u64(g->root+32, 8, 0, true); root=xx_data_get_u64(g->root+112, 8, 0, true);
    if(!g->blocks || g->blocks>(uint64_t)pm_available(g->f)/4096 || xx_data_get_u32(g->root+192, 4, 0, true)!=0 || xx_data_get_u32(g->root+196, 4, 0, true)!=g->blocks || !root) return false;
    return vl_unicos_walk(g,root,"");
}
static bool vr_legacy_files(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    vl_fs g; xx_volume *r=(xx_volume *)f; const xx_var *v; bool ok=false;
    xx_mem_zero(&g,sizeof(g)); g.f=f; g.s=s; g.pd=pd; g.bs=(uint32_t)r->block_size; if(!g.bs) g.bs=512; g.blocks=(uint64_t)pm_available(f)/g.bs;
    g.limit=UINT64_C(256)*1024*1024; v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MEMORY_LIMIT); if(v && xx_var_get_u64(v)<g.limit) g.limit=xx_var_get_u64(v);
    /* Parser recursion, metadata buffers and final streaming transfer buffer
     * are included alongside retained maps/member arrays in one budget. */
    g.used=UINT64_C(2)*1024*1024; if(g.limit<g.used || !vr_poll(pd)) return false;
    switch(f->file_type) {
    case XX_FILE_TYPE_RT11_FS: ok=vl_rt11(&g); break;
    case XX_FILE_TYPE_HP_LIF: ok=vl_lif(&g); break;
    case XX_FILE_TYPE_ECMA67_FS: ok=vl_ecma(&g); break;
    case XX_FILE_TYPE_LOCUS_FS: ok=vl_locus(&g); break;
    case XX_FILE_TYPE_AMIGA_PFS: ok=vl_pfs(&g); break;
    case XX_FILE_TYPE_HPOFS: ok=vl_hpofs(&g); break;
    case XX_FILE_TYPE_ODS2_FS: ok=vl_ods(&g); break;
    case XX_FILE_TYPE_UNICOS_FS: ok=vl_unicos(&g); break;
    default: return false;
    }
    if(!ok || !vr_poll(pd)) { return false; } r->block_size=g.bs; r->blocks=g.blocks;
    r->capability="native filesystem directory and file-byte extraction; allocation maps are validated and streamed from the borrowed source";
    if(!vl_take(&g,4096) || !vr_info(f,s,"Read-only extraction without mounting or journal replay. Stored record-oriented and symlink bytes are preserved. Unsupported external-device allocations fail explicitly.\n")) return false;
    { size_t i; pm_member *info=&s->items[s->count-1]; vl_member_context *context=(vl_member_context *)xx_mem_alloc(sizeof(*context));
        if(!context || !vl_take(&g,sizeof(*context))) { xx_mem_free(context); return false; } context->map=NULL; context->limit=g.limit; context->retained=g.used;
        info->context=context; info->free_context=vl_member_free; info->read_all=vl_member_unpack;
        for(i=0;i<s->count;++i) if(s->items[i].read_all==vl_member_unpack) ((vl_member_context *)s->items[i].context)->retained=g.used;
    } return true;
}
