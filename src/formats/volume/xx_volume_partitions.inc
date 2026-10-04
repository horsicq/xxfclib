/* SPDX-License-Identifier: MIT. Original bounded partition mappers.
 * Facts: Aaru 5.4.2 partition structures and operating-system disk headers. */
static bool vr_part(Abstractformat *f,pm_stream *s,unsigned index,const char *kind,uint64_t offset,uint64_t size) {
    char name[80]; uint64_t available=(uint64_t)pm_available(f);
    if(!size) return true;
    if(offset>available || size>available-offset || offset>INT64_MAX || size>INT64_MAX) return false;
    xx_rt_snprintf(name,sizeof(name),"partition-%02u-%s.img",index,kind);
    return pm_add(f,s,name,(int64_t)offset,(int64_t)size);
}
static bool vr_atari_type(uint32_t type) {
    const char *types[]={"GEM","BGM","XGM","LNX","SWP","RAW","NBD","NBS","UNX","MAC","MIX","MNX"}; unsigned i;
    if(!((type>>24)&1U)) return false;
    for(i=0;i<sizeof(types)/sizeof(types[0]);++i) if((type&UINT32_C(0xFFFFFF))==((uint32_t)types[i][0]<<16|(uint32_t)types[i][1]<<8|(uint32_t)types[i][2])) return true;
    return false;
}
static bool vr_atari_entries(Abstractformat *f,pm_stream *s,const uint8_t *b,size_t start,unsigned count,uint64_t root,uint64_t base,uint64_t *seen,unsigned *seen_count,xx_pd_struct *pd) {
    unsigned i; for(i=0;i<count;++i) { const uint8_t *e=b+start+i*12; uint32_t type=pm_be32(e),length=pm_be32(e+8); uint64_t sector=pm_be32(e+4); char label[4];
        if(!vr_poll(pd)) return false; if(!length || !((type>>24)&1U)) continue; if(!vr_atari_type(type)) return false;
        if((type&UINT32_C(0xFFFFFF))==UINT32_C(0x58474D)) {
            uint8_t next[512]; unsigned j; uint64_t where=sector+root;
            if(*seen_count>=64 || where>INT64_MAX/512) return false;
            for(j=0;j<*seen_count;++j) if(seen[j]==where) return false;
            seen[(*seen_count)++]=where;
            if(!pm_read(f,(int64_t)(where*512),next,512) || !vr_atari_entries(f,s,next,454,4,root?root:where,where,seen,seen_count,pd)) return false;
        } else {
            label[0]=(char)(type>>16); label[1]=(char)(type>>8); label[2]=(char)type; label[3]=0;
            if(!vr_part(f,s,(unsigned)s->count,label,(sector+base)*512,(uint64_t)length*512)) return false;
        }
    } return true;
}
static bool vr_uint(const char **cursor,uint64_t *value) {
    uint64_t n=0; unsigned digits=0; while(**cursor==' ' || **cursor=='\t') ++*cursor;
    while(**cursor>='0' && **cursor<='9') { unsigned d=(unsigned)(*(*cursor)++-'0'); if(n>(UINT64_MAX-d)/10) return false; n=n*10+d; ++digits; }
    *value=n; return digits!=0;
}
static bool vr_partitions(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    xx_volume *r=(xx_volume *)f; uint8_t b[8192]; int64_t at=0; unsigned i; size_t table_size=512;
    r->block_size=r->logical_sector_size?r->logical_sector_size:512;
    r->capability="partition-table validation and bounded partition-image extraction";
    switch(f->file_type) {
    case XX_FILE_TYPE_DEC_DISKLABEL:
        at=31*512; if(!pm_read(f,at,b,512) || pm_le32(b+440)!=0x032957 || pm_le32(b+444)!=1) return false;
        for(i=0;i<8;++i) if(!vr_part(f,s,i,"DEC",(uint64_t)pm_le32(b+452+i*8)*512,(uint64_t)pm_le32(b+448+i*8)*512)) return false; break;
    case XX_FILE_TYPE_DRAGONFLY_DISKLABEL:
        table_size=2048; if(!pm_read(f,at,b,table_size) || pm_le32(b+512)!=UINT32_C(0xC4464C59) || !pm_le32(b+524) || pm_le32(b+524)>16) return false;
        { uint32_t crc=pm_le32(b+516); xx_mem_zero(b+516,4);
          if(xx_crc32(XX_CRC_TYPE_CRC32,b+512,200+(size_t)pm_le32(b+524)*64)!=crc) return false; }
        for(i=0;i<pm_le32(b+524);++i) if(!vr_part(f,s,i,"BSD64",mm_le64(b+712+i*64),mm_le64(b+720+i*64))) return false; break;
    case XX_FILE_TYPE_SGI_VOLUME_HEADER:
        if(!pm_read(f,at,b,512) || pm_be32(b)!=UINT32_C(0x0BE5A941)) return false;
        { uint32_t sum=0,bytes=pm_be16(b+40); for(i=0;i<128;++i) sum+=pm_be32(b+i*4); if(sum || !vr_power(bytes) || bytes<128 || bytes>8192) return false;
          r->block_size=bytes; r->big_endian=true;
          for(i=0;i<16;++i) { const uint8_t *e=b+312+i*12; uint32_t type=pm_be32(e+8); if(type==0 || type==6) continue;
              if(!vr_part(f,s,i,"SGI",(uint64_t)pm_be32(e+4)*bytes,(uint64_t)pm_be32(e)*bytes)) return false; } }
        break;
    case XX_FILE_TYPE_HUMAN68K_PARTITIONS:
        at=2048; if(!pm_read(f,at,b,512) || pm_be32(b)!=UINT32_C(0x5836384B)) {
            at=1024; if(!pm_read(f,at,b,256) || pm_be32(b)!=UINT32_C(0x5836384B)) return false; r->block_size=256; table_size=256;
        } else r->block_size=1024;
        r->big_endian=true;
        for(i=0;i<8;++i) { const uint8_t *e=b+16+i*16; char name[9]; xx_rt_memcpy(name,e,8); name[8]=0;
            if(!vr_part(f,s,i,name,(uint64_t)(pm_be32(e+8)&UINT32_C(0xFFFFFF))*r->block_size,(uint64_t)pm_be32(e+12)*r->block_size)) return false; } break;
    case XX_FILE_TYPE_RIO_KARMA_PARTITIONS:
        if(!pm_read(f,at,b,512) || pm_le16(b+510)!=0xAB56) return false;
        for(i=0;i<2;++i) { const uint8_t *e=b+270+i*16; if(e[4]==0x4D && !vr_part(f,s,i,"Karma",(uint64_t)pm_le32(e+8)*512,(uint64_t)pm_le32(e+12)*512)) return false; } break;
    case XX_FILE_TYPE_PLAN9_PARTITIONS:
        at=512; if(!pm_read(f,at,b,512) || xx_rt_memcmp(b,"part ",5)) return false;
        { const char *p=(const char *)b; b[511]=0;
          while(*p) { char name[64]; size_t n=0; uint64_t first,last;
              if(xx_rt_strncmp(p,"part ",5)) return false; p+=5;
              while(*p && *p!=' ' && *p!='\n') { if(n+1>=sizeof(name) || (unsigned char)*p<33) return false; name[n++]=*p++; } name[n]=0;
              if(!n || !vr_uint(&p,&first) || !vr_uint(&p,&last) || last<=first || first>INT64_MAX/512 || last>INT64_MAX/512) return false;
              while(*p==' ' || *p=='\r') ++p; if(*p && *p!='\n') return false; if(*p=='\n') ++p;
              if(!vr_part(f,s,(unsigned)s->count,name,first*512,(last-first)*512)) return false;
          } } break;
    case XX_FILE_TYPE_XENIX_PARTITIONS:
        at=42*512; if(!pm_read(f,at,b,512) || pm_le16(b)!=0x1234) return false;
        for(i=0;i<16;++i) { uint32_t first=pm_le32(b+2+i*8),size=pm_le32(b+6+i*8);
            if((first&UINT32_C(0x80000000)) || (size&UINT32_C(0x80000000)) || !vr_part(f,s,i,"XENIX",((uint64_t)first+977)*1024,(uint64_t)size*1024)) return false; } break;
    case XX_FILE_TYPE_MINIX_PARTITIONS:
        if(!pm_read(f,at,b,512) || pm_le16(b+510)!=0xAA55) return false;
        for(i=0;i<4;++i) { const uint8_t *e=b+446+i*16; if(e[4] && e[4]!=0x81) return false;
            if(e[4]==0x81 && !vr_part(f,s,i,"Minix",(uint64_t)pm_le32(e+8)*512,(uint64_t)pm_le32(e+12)*512)) return false; } break;
    case XX_FILE_TYPE_ATARI_AHDI:
        if(!pm_read(f,at,b,512) || !pm_be32(b+450) || pm_be32(b+450)>(uint64_t)pm_available(f)/512) return false;
        { uint64_t seen[64]={0}; unsigned count=0;
          if(!vr_atari_entries(f,s,b,454,4,0,0,seen,&count,pd) || !vr_atari_entries(f,s,b,342,8,0,0,seen,&count,pd)) return false; }
        r->big_endian=true; break;
    case XX_FILE_TYPE_NEXT_DISKLABEL:
        { int offsets[]={0,2048,7680,8192}; bool found=false; for(i=0;i<4;++i) { at=offsets[i]; if(!pm_read(f,at,b,1024)) continue;
              uint32_t magic=pm_be32(b); if(magic==UINT32_C(0x4E655854) || magic==UINT32_C(0x646C5632) || magic==UINT32_C(0x646C5633)) { found=true; break; } }
          if(!found) return false; }
        table_size=1024; r->block_size=pm_be32(b+92); r->big_endian=true;
        if(!vr_power(r->block_size) || r->block_size<128 || r->block_size>8192 || (pm_be16(b+112)&0x8000)) return false;
        vr_label(r,b+12,24);
        for(i=0;i<8;++i) { const uint8_t *e=b+190+i*44; uint32_t first=pm_be32(e),length=pm_be32(e+4); if(!length || length==UINT32_MAX) continue;
            if((first&UINT32_C(0x80000000)) || !vr_part(f,s,i,"NeXT",((uint64_t)first+pm_be16(b+112))*r->block_size,(uint64_t)length*r->block_size)) return false; } break;
    case XX_FILE_TYPE_APRICOT_PARTITIONS:
        if(!pm_read(f,at,b,512) || !b[12] || b[12]>8 || b[13]!=1 || b[8]>4 || b[11]>5) return false;
        r->block_size=pm_le16(b+14); if(!vr_power(r->block_size) || r->block_size<128 || r->block_size>8192 || !b[22] || !pm_le16(b+16) || !pm_le32(b+18)) return false;
        if((uint64_t)b[22]*pm_le16(b+16)*pm_le32(b+18)>(uint64_t)pm_available(f)/r->block_size) return false;
        for(i=0;i<b[12];++i) { const uint8_t *e=b+320+i*16;
            if(pm_le16(e)!=r->block_size || !vr_part(f,s,i,"Apricot",(uint64_t)pm_le16(e+14)*r->block_size,(uint64_t)pm_le16(e+8)*r->block_size)) return false; } break;
    case XX_FILE_TYPE_ACORN_PARTITIONS:
        at=3072; if(!pm_read(f,at,b,512)) return false;
        { uint32_t sum=0,heads=b[450]+((b[456]>>6)&1),spt=b[449],bytes; uint64_t map; uint8_t entries[512];
          for(i=0;i<511;++i) sum=(sum&255)+(sum>>8)+b[i]; sum=(sum&255)+(sum>>8);
          if((uint8_t)sum!=b[511] || b[448]<7 || b[448]>13 || !heads || !spt) return false;
          bytes=1U<<b[448]; map=(uint64_t)pm_le16(b+509)*heads*spt; r->block_size=bytes;
          if(map>INT64_MAX/bytes || !pm_read(f,(int64_t)(map*bytes),entries,512)) return false;
          if((b[508]&15)==9) { for(i=0;i<42;++i) { const uint8_t *e=entries+i*12; uint32_t magic=pm_le32(e);
              if(magic!=UINT32_C(0xDEAFA1DE) && magic!=UINT32_C(0xDEAFAB1E)) continue;
              if(!vr_part(f,s,i,magic==UINT32_C(0xDEAFA1DE)?"Linux":"swap",(map+pm_le32(e+4))*bytes,(uint64_t)pm_le32(e+8)*bytes)) return false; } }
          else if(((b[508]&15)==1 || (b[508]&15)==2) && pm_le32(entries)==UINT32_C(0x4A657320)) {
              for(i=0;i<8;++i) { const uint8_t *e=entries+8+i*28; char name[17]; xx_rt_memcpy(name,e+12,16); name[16]=0;
                  if(!vr_part(f,s,i,name,(map+pm_le32(e))*bytes,(uint64_t)pm_le32(e+4)*bytes)) return false; } }
          else return false;
        } break;
    case XX_FILE_TYPE_PC98_PARTITIONS:
        if(!r->heads || !r->sectors_per_track || !r->logical_sector_size) { xx_pd_set_error(pd,1,"PC-9800 partition extraction requires explicit CHS geometry"); return false; }
        if(!pm_read(f,0,b,r->logical_sector_size) || pm_le16(b+r->logical_sector_size-2)!=0xAA55 || !xx_rt_memcmp(b+54,"FAT",3)) return false;
        at=r->logical_sector_size; if(!pm_read(f,at,b,256)) return false; table_size=256;
        for(i=0;i<8;++i) { const uint8_t *e=b+i*32; uint64_t first,last; char name[17]; if(!e[0]) continue;
            if(e[9]>=r->heads || e[13]>=r->heads || e[8]>=r->sectors_per_track || e[12]>=r->sectors_per_track) return false;
            first=((uint64_t)pm_le16(e+10)*r->heads+e[9])*r->sectors_per_track+e[8];
            last=((uint64_t)pm_le16(e+14)*r->heads+e[13])*r->sectors_per_track+e[12];
            xx_rt_memcpy(name,e+16,16); name[16]=0;
            if(last<first || !vr_part(f,s,i,name,first*r->block_size,(last-first+1)*r->block_size)) return false;
        } break;
    case XX_FILE_TYPE_XBOX360_LAYOUT:
        { uint64_t available=(uint64_t)pm_available(f);
          const uint64_t offsets[]={UINT64_C(0x2000),UINT64_C(0x80000),UINT64_C(0x80080000),UINT64_C(0x10C080000),UINT64_C(0x118EB0000),UINT64_C(0x120EB0000),UINT64_C(0x130EB0000)};
          const uint64_t lengths[]={UINT64_C(0x80000),UINT64_C(0x80000000),UINT64_C(0xA0E30000),UINT64_C(0xCE30000),UINT64_C(0x8000000),UINT64_C(0x10000000)};
          const char *names[]={"security","system-cache","game-cache","system","system2","compatibility","data"};
          if(available<=offsets[6] || (!vr_equal(f,(int64_t)offsets[6],"FATX",4) && !vr_equal(f,(int64_t)offsets[6],"XTAF",4))) return false;
          /* Aaru-compatible region views intentionally overlap. The game
           * region is an aggregate view, not an inferred adjacent extent. */
          for(i=0;i<7;++i) if(!vr_part(f,s,i,names[i],offsets[i],i==6?available-offsets[i]:lengths[i])) return false;
          r->capability="Xbox 360 retail fixed-region images (overlapping Aaru-compatible views)";
        } break;
    default: return false;
    }
    if(!s->count || !vr_poll(pd)) return false;
    r->blocks=(uint64_t)pm_available(f)/r->block_size;
    return pm_add(f,s,"partition-table.bin",at,(int64_t)table_size) && vr_info(f,s,"Integrity scope: partition-table structure and source bounds. Partition filesystem content is exported without interpretation.\n");
}
