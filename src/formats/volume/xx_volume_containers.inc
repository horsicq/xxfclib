/* SPDX-License-Identifier: MIT. Original volume and executable containers. */
static uint32_t vr_crc(uint32_t crc,const uint8_t *p,size_t n) {
    size_t i; unsigned j; for(i=0;i<n;++i) { crc^=p[i]; for(j=0;j<8;++j) crc=(crc>>1)^((crc&1)?UINT32_C(0xEDB88320):0); } return crc;
}
static bool vr_name(char *out,size_t capacity,const uint8_t *data,size_t n) {
    size_t i; if(!capacity) return false; for(i=0;i<n && data[i];++i) { if(i+1>=capacity || data[i]<32 || data[i]>=127 || data[i]=='/' || data[i]=='\\') return false; out[i]=(char)data[i]; }
    out[i]=0; return i!=0;
}
static bool vr_machine(uint16_t m) {
    switch(m) { case 0x14C: case 0x8664: case 0x1C0: case 0x1C2: case 0x1C4: case 0xAA64: case 0x5032: case 0x5064: case 0x5128: case 0x1F0: case 0x1F1: case 0x166: case 0x169: case 0x1A2: case 0x1A3: case 0x1A6: case 0x1A8: case 0x200: case 0x9041: case 0x6232: case 0x6264: return true; default: return false; }
}
static bool vr_coff(Abstractformat *f,pm_stream *s,xx_pd_struct *pd,bool te) {
    uint8_t h[40],e[40]; unsigned count,i; int64_t sections,adjust=0; uint64_t sym=0,symbol_count=0,strings=0; uint32_t string_size=0; xx_volume *r=(xx_volume *)f;
    if(!pm_read(f,0,h,te?40:20)) return false;
    if(te) { if(xx_data_get_u16(h, 2, 0, false)!=0x5A56 || !vr_machine(xx_data_get_u16(h+2, 2, 0, false)) || h[4]==0 || h[4]>96 || xx_data_get_u16(h+6, 2, 0, false)<40) return false;
        count=h[4]; sections=40; adjust=40-(int64_t)xx_data_get_u16(h+6, 2, 0, false); }
    else { if(!vr_machine(xx_data_get_u16(h, 2, 0, false)) || !(count=xx_data_get_u16(h+2, 2, 0, false)) || count>1024 || xx_data_get_u16(h+16, 2, 0, false)!=0 || (xx_data_get_u16(h+18, 2, 0, false)&2)) return false;
        sections=20; sym=xx_data_get_u32(h+8, 4, 0, false); symbol_count=xx_data_get_u32(h+12, 4, 0, false);
        if(symbol_count && (sym>(uint64_t)pm_available(f) || symbol_count>((uint64_t)pm_available(f)-sym)/18)) return false;
        if(sym) { uint8_t length[4]; strings=sym+symbol_count*18;
            if(strings>INT64_MAX || !pm_read(f,(int64_t)strings,length,4) || (string_size=xx_data_get_u32(length, 4, 0, false))<4 || string_size>(uint64_t)pm_available(f)-strings) return false; }
    }
    if((uint64_t)sections+(uint64_t)count*40>(uint64_t)pm_available(f)) return false;
    for(i=0;i<count;++i) { uint32_t bytes,raw; int64_t offset; char name[128],section[80];
        if(!vr_poll(pd) || !pm_read(f,sections+(int64_t)i*40,e,40)) return false;
        bytes=xx_data_get_u32(e+16, 4, 0, false); raw=xx_data_get_u32(e+20, 4, 0, false);
        if(!te && e[0]=='/') { uint32_t value=0; unsigned j; size_t length=0; uint8_t text[sizeof(section)];
            for(j=1;j<8 && e[j];++j) { if(e[j]<'0' || e[j]>'9' || value>(UINT32_MAX-9)/10) return false; value=value*10+(e[j]-'0'); }
            if(j==1 || value<4 || value>=string_size) return false;
            while(length<sizeof(text) && value+length<string_size) { if(!pm_read(f,(int64_t)(strings+value+length),text+length,1)) return false; if(!text[length++]) break; }
            if(!length || text[length-1] || !vr_name(section,sizeof(section),text,length)) return false;
        } else if(!vr_name(section,sizeof(section),e,8)) return false;
        if(!bytes) { continue; } offset=(int64_t)raw+adjust;
        if(offset<sections+(int64_t)count*40 || !pm_add(f,s,section,offset,bytes)) return false;
        xx_rt_snprintf(name,sizeof(name),"section-%02u-%s.bin",i,section);
        /* pm_add sanitizes names and adds collision-resistant sequence IDs. */
        xx_rt_snprintf(s->items[s->count-1].name,sizeof(s->items[s->count-1].name),"%04u-%s",i,name);
    }
    if(!te && symbol_count && !pm_add(f,s,"coff-symbols.bin",(int64_t)sym,(int64_t)symbol_count*18)) return false;
    if(!te && string_size>4 && !pm_add(f,s,"coff-string-table.bin",(int64_t)strings,string_size)) return false;
    if(!s->count) { return false; } r->capability=te?"UEFI TE header validation and original section extraction":"COFF object header validation and original section extraction";
    return vr_info(f,s,"Sections retain their original bytes; relocation and execution are not performed.\n");
}
typedef struct vr_lp_table { uint32_t offset,count,stride; } vr_lp_table;
static bool vr_lp_desc(vr_lp_table *t,const uint8_t *p,uint32_t limit,uint32_t minimum) {
    t->offset=xx_data_get_u32(p, 4, 0, false); t->count=xx_data_get_u32(p+4, 4, 0, false); t->stride=xx_data_get_u32(p+8, 4, 0, false);
    return t->stride>=minimum && t->count<=1000000 && t->offset<=limit && (uint64_t)t->count*t->stride<=limit-t->offset;
}
static bool vr_lp(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t geom[52],h[256],digest[32],*tables=NULL; uint32_t maximum,slots,header,bytes,i; vr_lp_table pt,et,gt,dt; bool ok=false; xx_volume *r=(xx_volume *)f;
    if(!pm_read(f,4096,geom,52) || xx_data_get_u32(geom, 4, 0, false)!=UINT32_C(0x616C4467) || xx_data_get_u32(geom+4, 4, 0, false)!=52) return false;
    xx_rt_memcpy(digest,geom+8,32); xx_mem_zero(geom+8,32);
    { uint8_t actual[32]; if(!xx_sha256_memory(geom,52,actual) || xx_rt_memcmp(digest,actual,32)) return false; }
    maximum=xx_data_get_u32(geom+40, 4, 0, false); slots=xx_data_get_u32(geom+44, 4, 0, false);
    if(!maximum || maximum>16U*1024U*1024U || (maximum&511) || !slots || slots>32 || !vr_power(xx_data_get_u32(geom+48, 4, 0, false)) || xx_data_get_u32(geom+48, 4, 0, false)<512) return false;
    if(!pm_read(f,12288,h,128) || xx_data_get_u32(h, 4, 0, false)!=UINT32_C(0x414C5030) || xx_data_get_u16(h+4, 2, 0, false)!=10 || xx_data_get_u16(h+6, 2, 0, false)>2) return false;
    header=xx_data_get_u32(h+8, 4, 0, false); if(header!=128 && header!=256) return false; if(header==256 && !pm_read(f,12288,h,256)) return false;
    xx_rt_memcpy(digest,h+12,32); xx_mem_zero(h+12,32);
    { uint8_t actual[32]; if(!xx_sha256_memory(h,header,actual) || xx_rt_memcmp(digest,actual,32)) return false; }
    bytes=xx_data_get_u32(h+44, 4, 0, false); if(bytes>maximum-header || bytes>16U*1024U*1024U) return false;
    if(!vr_lp_desc(&pt,h+80,bytes,52) || !vr_lp_desc(&et,h+92,bytes,24) || !vr_lp_desc(&gt,h+104,bytes,48) || !vr_lp_desc(&dt,h+116,bytes,64) || !dt.count || !pt.count) return false;
    { const xx_var *v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MEMORY_LIMIT); if(v && bytes>xx_var_get_u64(v)) return false; }
    tables=(uint8_t *)xx_mem_alloc(bytes?bytes:1); if(!tables) return false;
    if(!pm_read(f,12288+header,tables,bytes) || !xx_sha256_memory(tables,bytes,digest) || xx_rt_memcmp(digest,h+48,32)) goto done;
    for(i=0;i<pt.count;++i) { const uint8_t *p=tables+pt.offset+(size_t)i*pt.stride; uint32_t first=xx_data_get_u32(p+40, 4, 0, false),count=xx_data_get_u32(p+44, 4, 0, false),j; char name[64]; mm_map *map;
        if(!vr_poll(pd) || !vr_name(name,sizeof(name),p,36) || first>et.count || count>et.count-first || xx_data_get_u32(p+48, 4, 0, false)>=gt.count || (xx_data_get_u32(p+36, 4, 0, false)&~15U)) goto done;
        if(!count || (xx_data_get_u32(p+36, 4, 0, false)&8)) { continue; } map=mm_create(); if(!map) goto done;
        for(j=0;j<count;++j) { const uint8_t *e=tables+et.offset+(size_t)(first+j)*et.stride; uint64_t sectors=xx_data_get_u64(e, 8, 0, false),sector=xx_data_get_u64(e+12, 8, 0, false); uint32_t type=xx_data_get_u32(e+8, 4, 0, false),dev=xx_data_get_u32(e+20, 4, 0, false);
            if(!vr_poll(pd) || !sectors || sectors>INT64_MAX/512 || type>1 || (type==0 && (dev!=0 || sector>INT64_MAX/512 || sector<xx_data_get_u64(tables+dt.offset, 8, 0, false)))) { mm_free(map); xx_pd_set_error(pd,1,"LP extent requires an absent block device or unsupported target"); goto done; }
            if(!mm_add(f,map,type==1?-1:(int64_t)(sector*512),sectors*512)) { mm_free(map); goto done; }
        }
        { const xx_var *v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MAX_MEMBER_SIZE); if(v && map->size>xx_var_get_u64(v)) { mm_free(map); goto done; } }
        if(!mm_emit(f,s,name,map)) goto done;
    }
    r->version=10; r->block_size=xx_data_get_u32(geom+48, 4, 0, false); r->capability="logical partition reconstruction with linear and zero extents; SHA-256 metadata validation";
    ok=s->count && vr_info(f,s,"Metadata slot: 0\nLinear extents must reside in the supplied super image; absent external block devices are rejected.\n");
done: xx_mem_free(tables); return ok;
}
/* A bounded syntax tree for LVM's text metadata. No text is evaluated as code. */
typedef struct vr_cfg_node { char name[96],text[128]; uint64_t number; int parent; unsigned kind; } vr_cfg_node;
typedef struct vr_cfg { const char *p,*end; vr_cfg_node *nodes; size_t count; unsigned depth; } vr_cfg;
static void vr_cfg_space(vr_cfg *c) { while(c->p<c->end) { if(*c->p=='#') { while(c->p<c->end && *c->p!='\n') ++c->p; } else if(*c->p==' ' || *c->p=='\t' || *c->p=='\r' || *c->p=='\n') ++c->p; else break; } }
static int vr_cfg_new(vr_cfg *c,int parent) { size_t at=c->count; if(at>=16384) return -1; xx_mem_zero(&c->nodes[at],sizeof(c->nodes[at])); c->nodes[at].parent=parent; ++c->count; return (int)at; }
static bool vr_cfg_word(vr_cfg *c,char *out,size_t cap) { size_t n=0; while(c->p<c->end && ((*c->p>='A' && *c->p<='Z') || (*c->p>='a' && *c->p<='z') || (*c->p>='0' && *c->p<='9') || *c->p=='_' || *c->p=='-' || *c->p=='.' || *c->p=='+')) { if(n+1>=cap) return false; out[n++]=*c->p++; } out[n]=0; return n!=0; }
static bool vr_cfg_value(vr_cfg *c,int index) {
    vr_cfg_node *node=&c->nodes[index]; vr_cfg_space(c); if(c->p>=c->end) return false;
    if(*c->p=='"') { size_t n=0; node->kind=2; ++c->p; while(c->p<c->end && *c->p!='"') { char v=*c->p++; if(v=='\\') { if(c->p==c->end) return false; v=*c->p++; }
            if((unsigned char)v<32 || n+1>=sizeof(node->text)) { return false; } node->text[n++]=v; } if(c->p==c->end) return false; ++c->p; node->text[n]=0; return true; }
    if(*c->p=='[') { if(c->depth++>=16) return false; node->kind=3; ++c->p; vr_cfg_space(c); while(c->p<c->end && *c->p!=']') { int child=vr_cfg_new(c,index); if(child<0 || !vr_cfg_value(c,child)) return false; vr_cfg_space(c); if(c->p<c->end && *c->p==',') { ++c->p; vr_cfg_space(c); } else if(c->p==c->end || *c->p!=']') return false; }
        if(c->p==c->end) { return false; } ++c->p; --c->depth; return true; }
    node->kind=1; { const char *start=c->p; uint64_t n=0; while(c->p<c->end && *c->p>='0' && *c->p<='9') { unsigned d=(unsigned)(*c->p++-'0'); if(n>(UINT64_MAX-d)/10) return false; n=n*10+d; } node->number=n; return c->p>start; }
}
static bool vr_cfg_block(vr_cfg *c,int parent) {
    if(c->depth++>=16) { return false; } vr_cfg_space(c);
    while(c->p<c->end && *c->p!='}') { int index=vr_cfg_new(c,parent); if(index<0 || !vr_cfg_word(c,c->nodes[index].name,sizeof(c->nodes[index].name))) return false;
        vr_cfg_space(c); if(c->p==c->end) return false;
        if(*c->p=='{') { ++c->p; if(!vr_cfg_block(c,index) || c->p==c->end || *c->p!='}') return false; ++c->p; }
        else if(*c->p=='=') { ++c->p; if(!vr_cfg_value(c,index)) return false; }
        else { return false; } vr_cfg_space(c);
    } --c->depth; return true;
}
static int vr_cfg_find(vr_cfg *c,int parent,const char *name) { size_t i; for(i=0;i<c->count;++i) if(c->nodes[i].parent==parent && !xx_rt_strcmp(c->nodes[i].name,name)) return (int)i; return -1; }
static bool vr_cfg_num(vr_cfg *c,int parent,const char *name,uint64_t *n) { int i=vr_cfg_find(c,parent,name); if(i<0 || c->nodes[i].kind!=1) return false; *n=c->nodes[i].number; return true; }
static bool vr_lvm_id(const char *text,const uint8_t *id) { size_t i=0; while(*text) { if(*text=='-') { ++text; continue; } if(i>=32 || (uint8_t)*text++!=id[i++]) return false; } return i==32; }
static bool vr_lvm(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t label[512],mda[512]; uint32_t pvheader; uint64_t meta=0,metasize=0,offset,size,extent=0,pe_start=0,pe_count=0,limit=UINT64_C(256)*1024*1024,used; size_t pos,i; char *text=NULL; vr_cfg cfg; int group=-1,pvs,lvs,pv=-1; bool ok=false; xx_volume *r=(xx_volume *)f;
    xx_mem_zero(&cfg,sizeof(cfg));
    for(i=0;i<4;++i) if(pm_read(f,(int64_t)i*512,label,512) && !xx_rt_memcmp(label,"LABELONE",8)) break;
    if(i==4 || xx_data_get_u64(label+8, 8, 0, false)!=i || xx_rt_memcmp(label+24,"LVM2 001",8) || xx_data_get_u32(label+16, 4, 0, false)!=vr_crc(UINT32_C(0xF597A6CF),label+20,492)) return false;
    pvheader=xx_data_get_u32(label+20, 4, 0, false); if(pvheader<32 || pvheader>440) return false; pos=pvheader+40;
    if(xx_data_get_u64(label+pvheader+32, 8, 0, false)>(uint64_t)pm_available(f)) return false;
    while(pos<=496 && (xx_data_get_u64(label+pos, 8, 0, false) || xx_data_get_u64(label+pos+8, 8, 0, false))) pos+=16;
    pos+=16; if(pos>496) return false; meta=xx_data_get_u64(label+pos, 8, 0, false); metasize=xx_data_get_u64(label+pos+8, 8, 0, false);
    if(meta>INT64_MAX || metasize<512 || metasize>64U*1024U*1024U || !pm_read(f,(int64_t)meta,mda,512) || xx_rt_memcmp(mda+4," LVM2 x[5A%r0N*>",16) || xx_data_get_u32(mda+20, 4, 0, false)!=1 || xx_data_get_u32(mda, 4, 0, false)!=vr_crc(UINT32_C(0xF597A6CF),mda+4,508) || xx_data_get_u64(mda+24, 8, 0, false)!=meta || xx_data_get_u64(mda+32, 8, 0, false)!=metasize) return false;
    offset=xx_data_get_u64(mda+40, 8, 0, false); size=xx_data_get_u64(mda+48, 8, 0, false); if(offset<512 || offset>=metasize || !size || size>16U*1024U*1024U || size>metasize-512) return false;
    { const xx_var *v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MEMORY_LIMIT); if(v) limit=xx_var_get_u64(v); }
    used=2*size+16384*sizeof(vr_cfg_node)+65536U;
    if(used>limit) return false;
    text=(char *)xx_mem_alloc((size_t)size+1); cfg.nodes=(vr_cfg_node *)xx_mem_alloc(16384*sizeof(*cfg.nodes)); if(!text || !cfg.nodes) goto done;
    if(size<=metasize-offset) { if(!pm_read(f,(int64_t)(meta+offset),text,(size_t)size)) goto done; }
    else { size_t first=(size_t)(metasize-offset); if(!pm_read(f,(int64_t)(meta+offset),text,first) || !pm_read(f,(int64_t)(meta+512),text+first,(size_t)size-first)) goto done; }
    if(xx_data_get_u32(mda+56, 4, 0, false)!=vr_crc(UINT32_C(0xF597A6CF),(uint8_t *)text,(size_t)size)) goto done;
    text[size]=0; cfg.p=text; cfg.end=text+xx_rt_strlen(text); if(!vr_cfg_block(&cfg,-1) || cfg.p!=cfg.end || !vr_poll(pd)) goto done;
    for(i=0;i<cfg.count;++i) if(cfg.nodes[i].parent==-1 && cfg.nodes[i].kind==0) { if(group>=0) goto done; group=(int)i; }
    if(group<0 || !vr_cfg_num(&cfg,group,"extent_size",&extent) || !vr_power(extent) || extent>INT64_MAX/512) goto done;
    pvs=vr_cfg_find(&cfg,group,"physical_volumes"); lvs=vr_cfg_find(&cfg,group,"logical_volumes"); if(pvs<0 || lvs<0) goto done;
    for(i=0;i<cfg.count;++i) if(cfg.nodes[i].parent==pvs && cfg.nodes[i].kind==0) { int id=vr_cfg_find(&cfg,(int)i,"id"); if(id>=0 && vr_lvm_id(cfg.nodes[id].text,label+pvheader)) { pv=(int)i; break; } }
    if(pv<0 || !vr_cfg_num(&cfg,pv,"pe_start",&pe_start) || !vr_cfg_num(&cfg,pv,"pe_count",&pe_count) || pe_start>INT64_MAX/512 || pe_count>INT64_MAX/(extent*512)) goto done;
    for(i=0;i<cfg.count;++i) if(cfg.nodes[i].parent==lvs && cfg.nodes[i].kind==0) { size_t j; mm_map *map=mm_create(); uint64_t expected=0,declared=0,reserve; char name[128]; unsigned segments=0; int lv=(int)i;
        if(!map) goto done;
        if(!vr_cfg_num(&cfg,lv,"segment_count",&declared) || !declared || declared>16384) { mm_free(map); goto done; }
        reserve=sizeof(*map)+2*sizeof(pm_member)+2*(declared+32U)*sizeof(mm_extent);
        if(reserve>limit-used) { mm_free(map); goto done; } used+=reserve;
        for(j=i+1;j<cfg.count;++j) if(cfg.nodes[j].parent==lv && cfg.nodes[j].kind==0 && !xx_rt_strncmp(cfg.nodes[j].name,"segment",7)) {
            uint64_t start,count,stripes,physical=0; int seg=(int)j,type=vr_cfg_find(&cfg,seg,"type"),array=vr_cfg_find(&cfg,seg,"stripes"); size_t k; int string=-1,number=-1;
            char sequence[32]; xx_rt_snprintf(sequence,sizeof(sequence),"segment%u",segments+1);
            if(segments>=declared || xx_rt_strcmp(cfg.nodes[j].name,sequence)) { mm_free(map); goto done; }
            if(!vr_poll(pd) || type<0 || xx_rt_strcmp(cfg.nodes[type].text,"striped") || !vr_cfg_num(&cfg,seg,"start_extent",&start) || start!=expected || !vr_cfg_num(&cfg,seg,"extent_count",&count) || !count || !vr_cfg_num(&cfg,seg,"stripe_count",&stripes) || stripes!=1 || array<0) { mm_free(map); xx_pd_set_error(pd,1,"LVM logical volume needs unsupported RAID/thin/multiple-PV mapping"); goto done; }
            for(k=(size_t)array+1;k<cfg.count;++k) if(cfg.nodes[k].parent==array) { if(string<0 && cfg.nodes[k].kind==2) string=(int)k; else if(number<0 && cfg.nodes[k].kind==1) number=(int)k; else { mm_free(map); goto done; } }
            if(string<0 || number<0 || xx_rt_strcmp(cfg.nodes[string].text,cfg.nodes[pv].name)) { mm_free(map); xx_pd_set_error(pd,1,"LVM extent belongs to an absent physical volume"); goto done; }
            physical=cfg.nodes[number].number;
            if(physical>pe_count || count>pe_count-physical || expected>UINT64_MAX-count || !mm_add(f,map,(int64_t)((pe_start+physical*extent)*512),count*extent*512)) { mm_free(map); goto done; }
            expected+=count; ++segments;
        }
        if(segments!=declared) { mm_free(map); goto done; } xx_rt_snprintf(name,sizeof(name),"%s.img",cfg.nodes[i].name); if(!mm_emit(f,s,name,map)) goto done;
    }
    vr_label(r,(uint8_t *)cfg.nodes[group].name,xx_rt_strlen(cfg.nodes[group].name)); r->block_size=extent*512; r->blocks=pe_count;
    r->capability="LVM2 checksummed metadata and single-PV linear logical-volume extraction, including multiple segments";
    ok=s->count && mm_text(f,s,"lvm-metadata.txt",text) && vr_info(f,s,"Missing physical volumes, striped RAID, thin and snapshot targets are rejected.\n");
done: xx_mem_free(text); xx_mem_free(cfg.nodes); return ok;
}
static bool vr_container(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    switch(f->file_type) {
    case XX_FILE_TYPE_COFF_OBJECT: return vr_coff(f,s,pd,false);
    case XX_FILE_TYPE_TE_EXECUTABLE: return vr_coff(f,s,pd,true);
    case XX_FILE_TYPE_ANDROID_LP: return vr_lp(f,s,pd);
    case XX_FILE_TYPE_LVM2: return vr_lvm(f,s,pd);
    case XX_FILE_TYPE_AFF: return vr_aff(f,s,pd);
    case XX_FILE_TYPE_MD_RAID: return vr_md(f,s,pd);
    case XX_FILE_TYPE_WINDOWS_LDM: return vr_ldm(f,s,pd);
    default: return false;
    }
}
