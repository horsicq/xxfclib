/* SPDX-License-Identifier: MIT. Read-only MD and LDM mappings.
 * Facts: Linux md_p.h and public LDM on-disk field definitions. */
static bool vr_md90(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t b[4096]; uint64_t source=(uint64_t)pm_available(f),at,size,sum=0; uint32_t expected,level,disks,role,state; unsigned i; bool be; xx_volume *r=(xx_volume *)f;
    if(source<131072) { return false; } at=(source&~UINT64_C(65535))-65536;
    if(!pm_read(f,(int64_t)at,b,sizeof(b))) return false;
    be=xx_data_get_u32(b, 4, 0, true)==UINT32_C(0xA92B4EFC);
    if(xx_data_get_u32(b, 4, 0, be)!=UINT32_C(0xA92B4EFC) || xx_data_get_u32(b+4, 4, 0, be)!=0 || xx_data_get_u32(b+8, 4, 0, be)!=90) return false;
    expected=xx_data_get_u32(b+152, 4, 0, be); xx_mem_zero(b+152,4); for(i=0;i<1024;++i) sum+=xx_data_get_u32(b+i*4, 4, 0, be);
    if((uint32_t)((sum&UINT32_MAX)+(sum>>32))!=expected) return false;
    level=xx_data_get_u32(b+28, 4, 0, be); disks=xx_data_get_u32(b+40, 4, 0, be); size=(uint64_t)xx_data_get_u32(b+32, 4, 0, be)*1024;
    role=xx_data_get_u32(b+3968+12, 4, 0, be); state=xx_data_get_u32(b+3968+16, 4, 0, be);
    if(!disks || disks>27 || !size || size>at || role>=disks || (state&7)!=6 || !(xx_data_get_u32(b+132, 4, 0, be)&1) || (xx_data_get_u32(b+132, 4, 0, be)&2)) {
        xx_pd_set_error(pd,1,"MD 0.90 component is not a clean synchronized data member"); return false;
    }
    if((level!=1 && disks!=1) || (level!=1 && level!=0 && level!=UINT32_MAX)) { xx_pd_set_error(pd,1,"MD mapping requires the complete array member set"); return false; }
    if(!vr_poll(pd) || !pm_add(f,s,"array.img",0,(int64_t)size)) return false;
    r->version=90; r->big_endian=be; r->block_size=512; r->blocks=size/512;
    r->capability="MD 0.90 clean RAID1 mirror or single-member linear/RAID0 reconstruction";
    return vr_info(f,s,"The complete superblock checksum, clean state and synchronized member role are validated.\n");
}
static bool vr_md(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t b[8192]; uint64_t sectors=(uint64_t)pm_available(f)/512,locations[3]={0,8,0}; unsigned i; xx_volume *r=(xx_volume *)f;
    if(sectors<32) { return false; } locations[2]=(sectors-16)&~UINT64_C(7);
    for(i=0;i<3;++i) if(pm_read(f,(int64_t)(locations[i]*512),b,256) && xx_data_get_u32(b, 4, 0, false)==UINT32_C(0xA92B4EFC) && xx_data_get_u32(b+4, 4, 0, false)==1) break;
    if(i==3) return vr_md90(f,s,pd);
    { uint32_t maxdev=xx_data_get_u32(b+220, 4, 0, false),device=xx_data_get_u32(b+160, 4, 0, false),disks=xx_data_get_u32(b+92, 4, 0, false),level=xx_data_get_u32(b+72, 4, 0, false),csum=xx_data_get_u32(b+216, 4, 0, false),features=xx_data_get_u32(b+8, 4, 0, false); uint64_t sum=0,data=xx_data_get_u64(b+128, 8, 0, false),size=xx_data_get_u64(b+80, 8, 0, false); size_t n,j;
      if(!maxdev || maxdev>3840 || device>=maxdev || !disks || disks>maxdev || xx_data_get_u64(b+144, 8, 0, false)!=locations[i] || !size || data>sectors || size>sectors-data || size>xx_data_get_u64(b+136, 8, 0, false)) return false;
      n=256+maxdev*2U; if(n>sizeof(b) || !pm_read(f,(int64_t)(locations[i]*512),b,n)) return false;
      xx_mem_zero(b+216,4); for(j=0;j+4<=n;j+=4) sum+=xx_data_get_u32(b+j, 4, 0, false); if(n&2) sum+=xx_data_get_u16(b+n-2, 2, 0, false);
      if((uint32_t)((sum&UINT32_MAX)+(sum>>32))!=csum || xx_data_get_u16(b+256+device*2, 2, 0, false)>=disks || (features&(2U|4U|8U|16U|32U|64U|512U))) return false;
      if(level!=1 && disks!=1) { xx_pd_set_error(pd,1,"MD RAID mapping requires the other array members"); return false; }
      if(level!=1 && level!=0 && level!=UINT32_MAX) { xx_pd_set_error(pd,1,"unsupported MD RAID target"); return false; }
      if(xx_data_get_u64(b+208, 8, 0, false)!=UINT64_MAX && xx_data_get_u64(b+208, 8, 0, false)<size) { xx_pd_set_error(pd,1,"MD array member is not fully synchronized"); return false; }
      if(!pm_add(f,s,"array.img",(int64_t)(data*512),(int64_t)(size*512))) return false;
      r->version=1; r->block_size=512; r->blocks=size; vr_label(r,b+32,32); r->capability=level==1?"MD v1 RAID1 synchronized member extraction":"MD v1 single-member linear/RAID0 extraction";
      return vr_poll(pd) && vr_info(f,s,"MD superblock checksum and role are validated. Multi-member striped/parity arrays require their complete member set.\n");
    }
}
typedef struct vr_ldm_record { uint64_t id,parent,disk,start,size,logical,children; uint8_t type,component_type; char name[96]; uint8_t uuid[16]; } vr_ldm_record;
static bool vr_ldm_var(const uint8_t *p,size_t n,size_t at,size_t *next,uint64_t *number,char *text,size_t cap) {
    size_t bytes,i; uint64_t value=0; if(at>=n || (bytes=p[at])>n-at-1) return false;
    if(number) { if(!bytes || bytes>8) return false; for(i=0;i<bytes;++i) value=(value<<8)|p[at+1+i]; *number=value; }
    if(text) { if(!cap || bytes>=cap) return false; for(i=0;i<bytes;++i) { if(p[at+1+i]<32 || p[at+1+i]>=127) return false; text[i]=(char)p[at+1+i]; } text[bytes]=0; }
    if(next) { *next=at+1+bytes; } return true;
}
static bool vr_ldm_uuid(uint8_t out[16],const char *text) {
    unsigned n=0,value=0; while(*text) { unsigned d; char c=*text++; if(c=='-') continue;
        if(c>='0' && c<='9') d=(unsigned)(c-'0'); else if(c>='a' && c<='f') d=(unsigned)(c-'a'+10); else if(c>='A' && c<='F') d=(unsigned)(c-'A'+10); else return false;
        if(n>=32) { return false; } value=(value<<4)|d; if(n&1) out[n/2]=(uint8_t)value; ++n;
    } return n==32;
}
static bool vr_ldm_record_parse(const uint8_t *p,size_t n,vr_ldm_record *r) {
    size_t afterid,aftername,next; char state[128]; uint64_t unused; uint32_t declared; uint8_t flags;
    if(n<32 || xx_rt_memcmp(p,"VBLK",4) || (declared=xx_data_get_u32(p+20, 4, 0, true))<8 || declared>n ||
       !vr_ldm_var(p,n,24,&afterid,&r->id,NULL,0) || !vr_ldm_var(p,n,afterid,&aftername,NULL,r->name,sizeof(r->name))) return false;
    /* The declared length counts the type's static fields plus variable-field
     * widths, not a universal offset from the VBLK magic. Physical reads are
     * independently bounded by the complete (possibly assembled) frame. */
    flags=p[18];
    r->type=p[19];
    if(r->type==0x34) { char uuid[64]; size_t alternate;
        return vr_ldm_var(p,n,aftername,&alternate,NULL,uuid,sizeof(uuid)) && vr_ldm_uuid(r->uuid,uuid) &&
            vr_ldm_var(p,n,alternate,&next,NULL,NULL,0) && declared==12+next-24;
    }
    if(r->type==0x44) { if(aftername+16>n || declared!=45+aftername-24) return false; xx_rt_memcpy(r->uuid,p+aftername,16); return true; }
    if(r->type==0x33) {
        size_t delta=aftername-24,after_size,after_parent; if(52+delta>n) return false;
        r->start=xx_data_get_u64(p+36+delta, 8, 0, true); r->logical=xx_data_get_u64(p+44+delta, 8, 0, true);
        if(!vr_ldm_var(p,n,52+delta,&after_size,&r->size,NULL,0) || !vr_ldm_var(p,n,after_size,&after_parent,&r->parent,NULL,0) ||
           !vr_ldm_var(p,n,after_parent,&next,&r->disk,NULL,0)) return false;
        if((flags&8) && !vr_ldm_var(p,n,next,&next,&unused,NULL,0)) return false;
        return declared>=28+next-52;
    }
    if(r->type==0x32) { size_t delta,after_children;
        if(!vr_ldm_var(p,n,aftername,&next,NULL,state,sizeof(state))) { return false; } delta=next-24; if(24+delta>=n) return false;
        r->component_type=p[24+delta];
        if(!vr_ldm_var(p,n,29+delta,&after_children,&r->children,NULL,0) || !r->children || r->children>16384 ||
           !vr_ldm_var(p,n,45+after_children-29,&next,&r->parent,NULL,0)) return false;
        delta=next-45;
        if(flags&16) {
            if(!vr_ldm_var(p,n,46+delta,&next,&unused,NULL,0) || !vr_ldm_var(p,n,next,&next,&unused,NULL,0)) return false;
            delta=next-46;
        }
        return declared==22+delta;
    }
    if(r->type==0x51) { size_t a,b,c;
        size_t delta,tail;
        if(!vr_ldm_var(p,n,aftername,&a,NULL,state,sizeof(state)) || !vr_ldm_var(p,n,a,&b,NULL,state,sizeof(state)) ||
           !vr_ldm_var(p,n,45+b-24,&c,&unused,NULL,0) || !vr_ldm_var(p,n,61+c-45,&next,&r->size,NULL,0)) return false;
        delta=next-61; tail=82+delta;
        if(tail>n) return false; /* Partition type and the sixteen-byte GUID. */
        if((flags&8) && !vr_ldm_var(p,n,tail,&tail,NULL,NULL,0)) return false;
        if((flags&32) && !vr_ldm_var(p,n,tail,&tail,NULL,NULL,0)) return false;
        if((flags&128) && !vr_ldm_var(p,n,tail,&tail,&unused,NULL,0)) return false;
        if((flags&2) && !vr_ldm_var(p,n,tail,&tail,NULL,NULL,0)) return false;
        return declared>=58+tail-82;
    }
    if(r->type==0x35) {
        size_t delta;
        if(!vr_ldm_var(p,n,aftername,&next,NULL,NULL,0)) { return false; } delta=next-24;
        if(flags&8) {
            if(!vr_ldm_var(p,n,36+delta,&next,NULL,NULL,0) || !vr_ldm_var(p,n,next,&next,NULL,NULL,0)) return false;
            delta=next-36;
        }
        return declared==12+delta;
    }
    if(r->type==0x45) {
        size_t delta=aftername-24;
        if(flags&8) {
            if(!vr_ldm_var(p,n,68+delta,&next,NULL,NULL,0) || !vr_ldm_var(p,n,next,&next,NULL,NULL,0)) return false;
            delta=next-68;
        }
        return declared==44+delta;
    }
    return true;
}
static bool vr_ldm(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t ph[512],uuid[16],*database=NULL; uint64_t data_start,data_size,config,config_size,current_disk=UINT64_MAX,limit=UINT64_C(256)*1024*1024,used; uint32_t block_size,offset,last; vr_ldm_record *records=NULL; size_t count=0,i; bool ok=false; xx_volume *r=(xx_volume *)f;
    if(!pm_read(f,3072,ph,512) || xx_rt_memcmp(ph,"PRIVHEAD",8) || xx_data_get_u16(ph+12, 2, 0, true)!=2 || (xx_data_get_u16(ph+14, 2, 0, true)!=11 && xx_data_get_u16(ph+14, 2, 0, true)!=12)) return false;
    { char text[37]; xx_rt_memcpy(text,ph+48,36); text[36]=0; if(!vr_ldm_uuid(uuid,text)) return false; }
    data_start=xx_data_get_u64(ph+283, 8, 0, true); data_size=xx_data_get_u64(ph+291, 8, 0, true); config=xx_data_get_u64(ph+299, 8, 0, true); config_size=xx_data_get_u64(ph+307, 8, 0, true);
    if(!data_size || data_start>config || data_size>config-data_start || config>INT64_MAX/512 || config_size<32 || config_size>32768 || config>(uint64_t)pm_available(f)/512 || config_size>(uint64_t)pm_available(f)/512-config) return false;
    { const xx_var *v=xx_format_resolve_extra_parameter(f,r->parse_options,XX_META_ID_OPT_MEMORY_LIMIT); if(v) limit=xx_var_get_u64(v); }
    used=config_size*512+16384*sizeof(*records)+262144U+65536U;
    if(used>limit) return false;
    database=(uint8_t *)xx_mem_alloc((size_t)config_size*512); records=(vr_ldm_record *)xx_mem_alloc(16384*sizeof(*records)); if(!database || !records || !pm_read(f,(int64_t)(config*512),database,(size_t)config_size*512)) goto done;
    { unsigned t; bool found=false; uint64_t extent=0; const uint8_t *first=NULL;
      for(t=1;t<=2;++t) { const uint8_t *toc=database+t*512; uint64_t start,length,log_start,log_size;
        if(xx_rt_memcmp(toc,"TOCBLOCK",8)) continue;
        start=xx_data_get_u64(toc+46, 8, 0, true); length=xx_data_get_u64(toc+54, 8, 0, true); log_start=xx_data_get_u64(toc+80, 8, 0, true); log_size=xx_data_get_u64(toc+88, 8, 0, true);
        if(xx_rt_memcmp(toc+36,"config",7) || xx_rt_memcmp(toc+70,"log",4) || start!=17 || !length || start>config_size || length>config_size-start || log_start>config_size || log_size>config_size-log_start) goto done;
        if(first && (xx_rt_memcmp(first+46,toc+46,16) || xx_rt_memcmp(first+80,toc+80,16))) goto done;
        first=toc; found=true; extent=length*512;
      }
      if(!found || (uint64_t)xx_data_get_u32(database+17*512+4, 4, 0, true)*xx_data_get_u32(database+17*512+8, 4, 0, true)>extent) goto done;
    }
    { const uint8_t *vm=database+17*512; if(xx_rt_memcmp(vm,"VMDB",4) || xx_data_get_u16(vm+16, 2, 0, true)!=1 || xx_data_get_u16(vm+18, 2, 0, true)!=4 || xx_data_get_u16(vm+20, 2, 0, true)!=10) goto done; block_size=xx_data_get_u32(vm+8, 4, 0, true); offset=xx_data_get_u32(vm+12, 4, 0, true); last=xx_data_get_u32(vm+4, 4, 0, true);
      if(block_size<32 || block_size>4096 || !vr_power(block_size) || offset<512 || offset%block_size || offset>(uint64_t)last*block_size || (uint64_t)last*block_size>config_size*512-17*512 || last-offset/block_size>65536U) goto done;
      for(i=offset;i<(uint64_t)last*block_size;i+=block_size) { const uint8_t *p=vm+i; uint16_t total,part; uint8_t *assembled=NULL; size_t n=block_size,j;
          if(block_size>(uint64_t)last*block_size-i || !vr_poll(pd) || xx_rt_memcmp(p,"VBLK",4)) { goto done; } total=xx_data_get_u16(p+14, 2, 0, true); part=xx_data_get_u16(p+12, 2, 0, true); if(!total) { if(part) goto done; continue; }
          if(total>64 || part>=total || count>=16384) goto done;
          if(part) { size_t roots=0;
              for(j=offset;j<(uint64_t)last*block_size;j+=block_size) { const uint8_t *q=vm+j;
                  if(block_size>(uint64_t)last*block_size-j || !vr_poll(pd)) goto done;
                  if(xx_data_get_u32(q+8, 4, 0, true)==xx_data_get_u32(p+8, 4, 0, true) && !xx_data_get_u16(q+12, 2, 0, true) && xx_data_get_u16(q+14, 2, 0, true)==total && !xx_rt_memcmp(q,"VBLK",4)) ++roots;
              }
              if(roots!=1) { goto done; } continue;
          }
          if(total>1) { uint64_t group=xx_data_get_u32(p+8, 4, 0, true); uint64_t mask=0; n=16+(size_t)total*(block_size-16); assembled=(uint8_t *)xx_mem_alloc(n); if(!assembled) goto done; xx_rt_memcpy(assembled,p,16);
              for(j=offset;j<(uint64_t)last*block_size;j+=block_size) { const uint8_t *q=vm+j; uint16_t piece;
                  if(block_size>(uint64_t)last*block_size-j || !vr_poll(pd)) { xx_mem_free(assembled); goto done; }
                  piece=xx_data_get_u16(q+12, 2, 0, true); if(xx_data_get_u32(q+8, 4, 0, true)!=group) continue;
                  if(xx_rt_memcmp(q,"VBLK",4) || xx_data_get_u16(q+14, 2, 0, true)!=total || piece>=total || (mask&(UINT64_C(1)<<piece))) { xx_mem_free(assembled); goto done; }
                  mask|=UINT64_C(1)<<piece; xx_rt_memcpy(assembled+16+(size_t)piece*(block_size-16),q+16,block_size-16);
              }
              if(mask!=(total==64?UINT64_MAX:((UINT64_C(1)<<total)-1))) { xx_mem_free(assembled); goto done; } p=assembled;
          }
          xx_mem_zero(&records[count],sizeof(*records)); if(!vr_ldm_record_parse(p,n,&records[count])) { xx_mem_free(assembled); goto done; } xx_mem_free(assembled);
          if((records[count].type==0x34 || records[count].type==0x44) && !xx_rt_memcmp(records[count].uuid,uuid,16)) current_disk=records[count].id;
          ++count;
      }
    }
    if(current_disk==UINT64_MAX) goto done;
    for(i=0;i<count;++i) if(records[i].type==0x32) { size_t j,parts=0,consumed=0; uint64_t expected=0,reserve; mm_map *map=mm_create(); char name[96]; if(!map) goto done;
        for(j=0;j<count;++j) if(records[j].type==0x33 && records[j].parent==records[i].id) ++parts;
        if(parts!=records[i].children) { mm_free(map); goto done; }
        reserve=sizeof(*map)+2*sizeof(pm_member)+2*(parts+32U)*sizeof(mm_extent);
        if(reserve>limit-used) { mm_free(map); goto done; } used+=reserve;
        if(records[i].component_type!=2) { mm_free(map); xx_pd_set_error(pd,1,"Unsupported LDM striped/parity component"); goto done; }
        for(;;) { vr_ldm_record *part=NULL;
            for(j=0;j<count;++j) if(records[j].type==0x33 && records[j].parent==records[i].id && records[j].logical==expected && records[j].size) { if(part) { mm_free(map); goto done; } part=&records[j]; }
            if(!part) break;
            if(part->disk!=current_disk || part->start>data_size || part->size>data_size-part->start || part->size>INT64_MAX/512 || !mm_add(f,map,(int64_t)((data_start+part->start)*512),part->size*512)) { mm_free(map); xx_pd_set_error(pd,1,"LDM volume needs an absent disk or invalid extent"); goto done; }
            expected+=part->size; ++consumed;
        }
        if(!expected || consumed!=parts) { mm_free(map); goto done; }
        for(j=0;j<count;++j) if(records[j].type==0x51 && records[j].id==records[i].parent) break;
        if(j==count || records[j].size!=expected) { mm_free(map); goto done; }
        xx_rt_snprintf(name,sizeof(name),"%.70s.img",records[j].name); if(!mm_emit(f,s,name,map)) goto done;
    }
    r->block_size=512; r->blocks=data_size; r->version=xx_data_get_u16(ph+14, 2, 0, true); r->big_endian=true; r->capability="Windows LDM basic and single-disk spanned logical-volume extraction";
    ok=s->count && pm_add(f,s,"ldm-database.bin",(int64_t)(config*512),(int64_t)(config_size*512)) && vr_info(f,s,"Striped/parity components and volumes requiring absent disks are not reconstructed.\n");
done: xx_mem_free(database); xx_mem_free(records); return ok;
}
