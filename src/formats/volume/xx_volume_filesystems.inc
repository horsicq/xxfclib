/* SPDX-License-Identifier: MIT. Filesystem identity and superblock metadata.
 * Facts: Aaru 5.4.2 filesystem descriptors, Linux on-disk headers. */
static bool vr_fs_super(Abstractformat *f,int64_t *where,uint8_t *b,size_t n) {
    return pm_read(f,*where,b,n);
}
static bool vr_filesystem(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    xx_volume *r=(xx_volume *)f; uint8_t b[4096]; int64_t at=0,source=pm_available(f); size_t header=512;
    uint32_t magic=0; bool found=false; unsigned i; r->capability="filesystem identification and superblock metadata; file extraction unavailable";
    switch(f->file_type) {
    case XX_FILE_TYPE_ATHEOS_FS: case XX_FILE_TYPE_BEOS_FS:
        for(i=0;i<(f->file_type==XX_FILE_TYPE_ATHEOS_FS?1U:3U);++i) {
            at=f->file_type==XX_FILE_TYPE_ATHEOS_FS?1024:(int64_t)i*512;
            if(!vr_fs_super(f,&at,b,512)) continue;
            magic=f->file_type==XX_FILE_TYPE_ATHEOS_FS?UINT32_C(0x41465331):UINT32_C(0x42465331);
            r->big_endian=pm_be32(b+32)==magic;
            if(vr_u32(b+32,r->big_endian)!=magic || vr_u32(b+68,r->big_endian)!=UINT32_C(0xDD121031) || vr_u32(b+112,r->big_endian)!=UINT32_C(0x15B6830E)) continue;
            r->block_size=vr_u32(b+40,r->big_endian); r->blocks=vr_u64(b+48,r->big_endian);
            if(!vr_power(r->block_size) || r->block_size<512 || r->block_size>65536 || !r->blocks || r->blocks>(uint64_t)source/r->block_size || vr_u64(b+56,r->big_endian)>r->blocks) return false;
            vr_label(r,b,32); found=true; break;
        }
        break;
    case XX_FILE_TYPE_COHERENT_FS: case XX_FILE_TYPE_SYSV_FS: case XX_FILE_TYPE_XENIX_FS: case XX_FILE_TYPE_UNIX_V7:
        for(i=0;i<16 && !found;++i) {
            at=(int64_t)i*512; if(!vr_fs_super(f,&at,b,1024)) continue;
            if(f->file_type==XX_FILE_TYPE_COHERENT_FS) {
                found=(!xx_rt_memcmp(b+484,"noname",6) && !xx_rt_memcmp(b+490,"nopack",6)) ||
                    (!xx_rt_memcmp(b+484,"xxxxx",5) && !xx_rt_memcmp(b+490,"xxxxx",5));
            } else if(f->file_type==XX_FILE_TYPE_UNIX_V7) {
                found=vr_classic_v7_identity(r,b,source,NULL);
            } else {
                size_t positions[3]={f->file_type==XX_FILE_TYPE_SYSV_FS?504:496,1016,1016}; unsigned j;
                magic=f->file_type==XX_FILE_TYPE_SYSV_FS?UINT32_C(0xFD187E20):UINT32_C(0x002B5544);
                for(j=0;j<2;++j) if(pm_le32(b+positions[j])==magic || pm_be32(b+positions[j])==magic) {
                    r->big_endian=pm_be32(b+positions[j])==magic; found=true; break;
                }
            }
            if(found) { r->block_size=512; vr_label(r,b+484,6); header=1024; }
        }
        break;
    case XX_FILE_TYPE_ODS2_FS: case XX_FILE_TYPE_RT11_FS:
        at=512; if(!vr_fs_super(f,&at,b,512)) return false;
        if(f->file_type==XX_FILE_TYPE_RT11_FS) found=!xx_rt_memcmp(b+496,"DECRT11A    ",12);
        else found=!xx_rt_memcmp(b+496,"DECFILE11A  ",12) || !xx_rt_memcmp(b+496,"DECFILE11B  ",12);
        r->block_size=512; r->blocks=(uint64_t)source/512; vr_label(r,b+472,12); break;
    case XX_FILE_TYPE_ECMA67_FS:
        at=768; header=128; if(!vr_fs_super(f,&at,b,128)) return false;
        found=!xx_rt_memcmp(b,"VOL",3) && (b[3]==1 || b[3]=='1') && b[75]==0x31; r->block_size=128; r->blocks=(uint64_t)source/128; vr_label(r,b+4,6); break;
    case XX_FILE_TYPE_F2FS:
        at=1024; if(!vr_fs_super(f,&at,b,512) || pm_le32(b)!=UINT32_C(0xF2F52010)) return false;
        if(pm_le32(b+8)>16 || pm_le32(b+12)>16 || pm_le32(b+16)!=12) return false;
        r->block_size=4096; r->blocks=mm_le64(b+36); r->version=pm_le16(b+4); found=r->blocks && r->blocks<=(uint64_t)source/4096; break;
    case XX_FILE_TYPE_FOSSIL_FS:
        at=131072; if(!vr_fs_super(f,&at,b,512) || pm_be32(b)!=UINT32_C(0x3776AE89)) return false;
        r->big_endian=true; r->block_size=pm_be16(b+6); r->version=pm_be16(b+4); r->blocks=pm_be32(b+20);
        found=vr_power(r->block_size) && r->block_size>=512 && r->blocks<=(uint64_t)source/r->block_size && pm_be32(b+8)<r->blocks; break;
    case XX_FILE_TYPE_HAMMER_FS:
        header=1928; if(!vr_fs_super(f,&at,b,header)) return false;
        r->big_endian=mm_be64(b)==UINT64_C(0xC8414D4DC5523031); found=vr_u64(b,r->big_endian)==UINT64_C(0xC8414D4DC5523031); r->block_size=8192; break;
    case XX_FILE_TYPE_HPOFS:
        if(!vr_fs_super(f,&at,b,512)) return false;
        found=!xx_rt_memcmp(b+54,"HPOFS\0\0\0",8) || !xx_rt_memcmp(b+54,"HPOFS   ",8);
        r->block_size=pm_le16(b+11); r->blocks=pm_le32(b+32); if(!r->blocks) r->blocks=pm_le16(b+19);
        found=found && vr_power(r->block_size) && r->block_size>=512 && r->blocks<=(uint64_t)source/r->block_size; vr_label(r,b+43,11); break;
    case XX_FILE_TYPE_HP_LIF:
        if(!vr_fs_super(f,&at,b,512) || pm_be16(b)!=0x8000) return false;
        r->big_endian=true; r->block_size=256; vr_label(r,b+2,6);
        found=pm_be32(b+8)>0 && (uint64_t)pm_be32(b+8)+pm_be32(b+16)<=(uint64_t)source/256; r->version=pm_be16(b+20); break;
    case XX_FILE_TYPE_JFS:
        at=32768; if(!vr_fs_super(f,&at,b,512) || xx_rt_memcmp(b,"JFS1",4)) return false;
        r->version=pm_le32(b+4); r->block_size=pm_le32(b+16); r->blocks=mm_le64(b+8);
        found=vr_power(r->block_size) && r->block_size>=512 && r->block_size<=65536; vr_label(r,b+152,16); break;
    case XX_FILE_TYPE_LINUX_EXT:
        at=1024; if(!vr_fs_super(f,&at,b,512) || pm_le16(b+56)!=0x137D) return false;
        r->block_size=1024; r->blocks=pm_le32(b+4); found=r->blocks && r->blocks<=(uint64_t)source/1024; break;
    case XX_FILE_TYPE_LOCUS_FS:
        for(i=0;i<9;++i) { at=(int64_t)i*512; if(!vr_fs_super(f,&at,b,512)) continue;
            magic=pm_le32(b); r->big_endian=(magic==UINT32_C(0xCDDDEEFF) || magic==UINT32_C(0xCCDDEEFF));
            if(magic==UINT32_C(0xFFEEDDCD) || magic==UINT32_C(0xFFEEDDCC) || r->big_endian) { found=true; break; } }
        break;
    case XX_FILE_TYPE_HPFS:
        at=8192; if(!vr_fs_super(f,&at,b,512)) return false;
        found=pm_le32(b)==UINT32_C(0xF995E849) && pm_le32(b+4)==UINT32_C(0xFA53E9C5); r->version=b[8]; r->block_size=512; r->blocks=pm_le32(b+16);
        found=found && r->blocks && r->blocks<=(uint64_t)source/512; break;
    case XX_FILE_TYPE_REFS:
        if(!vr_fs_super(f,&at,b,512)) return false;
        found=!xx_rt_memcmp(b+3,"ReFS\0\0\0\0",8) && !xx_rt_memcmp(b+11,"\0\0\0\0\0",5) && !xx_rt_memcmp(b+16,"FSRS",4);
        r->block_size=pm_le32(b+32); r->blocks=mm_le64(b+24); found=found && vr_power(r->block_size) && r->block_size>=512 && r->blocks<=(uint64_t)source/r->block_size; break;
    case XX_FILE_TYPE_NILFS2:
        at=1024; if(!vr_fs_super(f,&at,b,512) || pm_le16(b+6)!=0x3434) return false;
        r->version=pm_le32(b); magic=pm_le32(b+20); if(magic>6) return false;
        r->block_size=UINT64_C(1024)<<magic; r->blocks=mm_le64(b+32)/r->block_size;
        found=r->blocks && r->blocks<=(uint64_t)source/r->block_size && pm_le16(b+8)>=128; break;
    case XX_FILE_TYPE_AMIGA_PFS:
        at=1024; if(!vr_fs_super(f,&at,b,512)) return false; magic=pm_be32(b);
        found=magic==UINT32_C(0x41465301) || magic==UINT32_C(0x50465302) || magic==UINT32_C(0x50465301) || magic==UINT32_C(0x6D754146) || magic==UINT32_C(0x6D755046); r->big_endian=true; r->block_size=512; break;
    case XX_FILE_TYPE_QNX4:
        at=512; if(!vr_fs_super(f,&at,b,512) || b[0]!='/') return false;
        for(i=1;i<16;++i) if(b[i]) return false;
        for(i=0;i<3;++i) { const uint8_t *inode=b+i*64; if((pm_le32(inode+16)&511) || pm_le32(inode+20)>=(uint64_t)source/512 || !(inode[63]&1)) return false; }
        found=pm_le32(b+16)>=512; r->block_size=512; r->blocks=(uint64_t)source/512; break;
    case XX_FILE_TYPE_REISERFS:
        at=65536; if(!vr_fs_super(f,&at,b,512)) return false;
        found=!xx_rt_memcmp(b+52,"ReIsErFs",8) || !xx_rt_memcmp(b+52,"ReIsEr2Fs",9) || !xx_rt_memcmp(b+52,"ReIsEr3Fs",9);
        if(!found) { at=8192; if(!vr_fs_super(f,&at,b,512)) return false; found=!xx_rt_memcmp(b+52,"ReIsErFs",8); }
        if(!found) { at=65536; if(!vr_fs_super(f,&at,b,512)) return false; found=!xx_rt_memcmp(b,"ReIsEr4",7); r->capability="Reiser4 filesystem identity"; break; }
        r->block_size=pm_le16(b+44); r->blocks=pm_le32(b); found=found && vr_power(r->block_size) && r->block_size>=512 && r->blocks<=(uint64_t)source/r->block_size; break;
    case XX_FILE_TYPE_SGI_EFS:
        at=512; if(!vr_fs_super(f,&at,b,512)) return false; magic=pm_be32(b+28); r->big_endian=true;
        found=magic==0x00072959 || magic==0x0007295A; r->block_size=512; r->blocks=pm_be32(b); found=found && r->blocks && r->blocks<=(uint64_t)source/512; break;
    case XX_FILE_TYPE_AMIGA_SFS:
        if(!vr_fs_super(f,&at,b,512)) return false; magic=pm_be32(b); r->big_endian=true;
        found=magic==UINT32_C(0x53465300) || magic==UINT32_C(0x53465302); r->version=magic&255; break;
    case XX_FILE_TYPE_SOLAR_FS:
        if(!vr_fs_super(f,&at,b,512)) return false;
        found=b[37]==0x29 && !xx_rt_memcmp(b+53,"SOL_FS  ",8); r->block_size=pm_le16(b+11); r->blocks=pm_le16(b+18); vr_label(r,b+42,11); break;
    case XX_FILE_TYPE_UNICOS_FS:
        r->big_endian=true;
        if(vr_fs_super(f,&at,b,512)) found=mm_be64(b)==UINT64_C(0x6E6331667331636E);
        if(!found) { at=4096; if(vr_fs_super(f,&at,b,512)) found=mm_be64(b)==UINT64_C(0x6E6331667331636E); }
        break;
    case XX_FILE_TYPE_UNIX_BFS:
        if(!vr_fs_super(f,&at,b,512) || pm_le32(b)!=UINT32_C(0x1BADFACE)) return false;
        found=pm_le32(b+4)>=512 && pm_le32(b+4)<=pm_le32(b+8) && pm_le32(b+8)<(uint64_t)source; r->block_size=512; vr_label(r,b+34,6); break;
    case XX_FILE_TYPE_VXFS:
        at=1024; if(!vr_fs_super(f,&at,b,512)) return false; r->big_endian=pm_be32(b)==UINT32_C(0xA501FCF5); found=vr_u32(b,r->big_endian)==UINT32_C(0xA501FCF5); r->version=vr_u32(b+4,r->big_endian); break;
    case XX_FILE_TYPE_VMFS:
        at=1048576; if(!vr_fs_super(f,&at,b,512)) return false; found=pm_le32(b)==UINT32_C(0xC001D00D); r->version=pm_le32(b+4); break;
    case XX_FILE_TYPE_XIA_FS:
        if(!vr_fs_super(f,&at,b,1024)) return false; found=pm_le32(b+572)==UINT32_C(0x012FD16D); r->block_size=pm_le32(b+512); r->blocks=pm_le32(b+516);
        found=found && vr_power(r->block_size) && r->block_size>=1024 && r->blocks<=(uint64_t)source/r->block_size; header=1024; break;
    case XX_FILE_TYPE_XFS:
        if(!vr_fs_super(f,&at,b,512) || xx_rt_memcmp(b,"XFSB",4)) return false;
        r->big_endian=true; r->block_size=pm_be32(b+4); r->blocks=mm_be64(b+8); r->version=pm_be16(b+100)&15; vr_label(r,b+108,12);
        found=vr_power(r->block_size) && r->block_size>=512 && r->block_size<=65536 && r->blocks && r->blocks<=(uint64_t)source/r->block_size; break;
    case XX_FILE_TYPE_ZFS:
        { int64_t positions[6]={8192,15872,130560,392704,source-131584,source-393728};
          for(i=0;i<6 && !found;++i) { at=positions[i]; if(at<0 || !vr_fs_super(f,&at,b,512)) continue;
              found=mm_le64(b+472)==UINT64_C(0x0210DA7AB10C7A11) || mm_be64(b+472)==UINT64_C(0x0210DA7AB10C7A11); }
        }
        r->big_endian=mm_be64(b+472)==UINT64_C(0x0210DA7AB10C7A11); break;
    case XX_FILE_TYPE_GFS2:
        at=65536; if(!vr_fs_super(f,&at,b,512)) return false;
        found=pm_be32(b)==UINT32_C(0x01161970) && pm_be32(b+4)==1 && pm_be32(b+24)==1801;
        r->big_endian=true; r->block_size=pm_be32(b+36); r->version=pm_be32(b+24); vr_label(r,b+160,64);
        found=found && vr_power(r->block_size) && r->block_size>=512 && r->block_size<=65536; break;
    default: return false;
    }
    if(!found || !vr_poll(pd)) return false;
    if(!r->identity_only && f->file_type==XX_FILE_TYPE_HPFS) return vr_hpfs_files(f,s,pd,b);
    if(!r->identity_only && (f->file_type==XX_FILE_TYPE_RT11_FS || f->file_type==XX_FILE_TYPE_HP_LIF || f->file_type==XX_FILE_TYPE_ECMA67_FS || f->file_type==XX_FILE_TYPE_LOCUS_FS || f->file_type==XX_FILE_TYPE_AMIGA_PFS || f->file_type==XX_FILE_TYPE_HPOFS || f->file_type==XX_FILE_TYPE_ODS2_FS || f->file_type==XX_FILE_TYPE_UNICOS_FS)) return vr_legacy_files(f,s,pd);
    if(!r->identity_only && vr_modern_name(f->file_type) && (f->file_type!=XX_FILE_TYPE_REISERFS || !r->block_size)) return vr_modern_files(f,s,pd);
    if(!r->identity_only && vr_classic_name(f->file_type)) return vr_classic_files(f,s,pd,b,at);
    if(f->file_type==XX_FILE_TYPE_GFS2) return vr_gfs2(f,s,pd,b);
    if(!r->identity_only && vr_grub_name(f->file_type) && (f->file_type!=XX_FILE_TYPE_REISERFS || r->block_size)) return vr_grub_files(f,s,pd);
    if(!r->identity_only) { if(pd && !pd->last_error) xx_pd_set_error(pd,1,"Filesystem directory and allocation layout is unsupported"); return false; }
    return pm_add(f,s,"superblock.bin",at,(int64_t)header) && vr_info(f,s,"Integrity scope: filesystem identity and superblock fields only. File data and allocation trees are not tested.\n");
}
