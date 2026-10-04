/* SPDX-License-Identifier: MIT. Necessary signatures, followed by full native
 * structural validation. Signatureless and CHS layouts require a profile. */
static bool vdet_at(xx_io_device *d,int64_t at,const void *value,size_t count) {
    uint8_t b[32]; int64_t saved=xx_io_tell(d),total=xx_io_size(d); bool ok=false;
    if(saved>=0 && count<=sizeof(b) && at>=0 && at<=total && count<=(uint64_t)(total-at) && xx_io_seek64(d,at,SEEK_SET)==0) {
        size_t done=0; while(done<count) { ssize_t n=xx_io_read(d,b+done,count-done); if(n<=0 || (size_t)n>count-done) break; done+=(size_t)n; }
        ok=done==count && !xx_rt_memcmp(b,value,count);
    }
    (void)xx_io_seek64(d,saved,SEEK_SET); return ok;
}
static bool vdet_word(xx_io_device *d,int64_t at,uint32_t value,bool be) {
    uint8_t b[4]; unsigned i; for(i=0;i<4;++i) b[i]=(uint8_t)(value>>(be?(3-i)*8:i*8)); return vdet_at(d,at,b,4);
}
static bool xx_volume_addition_candidate(xx_file_type_t type,xx_io_device *d,const uint8_t *b,size_t n,int64_t total) {
    unsigned i;
#define VSTR(at,text) vdet_at(d,at,text,sizeof(text)-1)
#define VW(at,word) (vdet_word(d,at,(uint32_t)(word),false)||vdet_word(d,at,(uint32_t)(word),true))
    switch(type) {
    case XX_FILE_TYPE_ANDROID_LP:return VW(4096,0x616C4467);
    case XX_FILE_TYPE_LVM2:for(i=0;i<4;++i) if(VSTR(i*512,"LABELONE")) return true; return false;
    case XX_FILE_TYPE_COFF_OBJECT:return n>=20 && b[2] && !b[3] && b[2]<=96 && !b[16] && !b[17] && ((b[0]==0x4c&&b[1]==1)||(b[0]==0x64&&b[1]==0x86)||(b[0]==0x64&&b[1]==0xaa)||(b[0]==0xc0&&b[1]==1));
    case XX_FILE_TYPE_TE_EXECUTABLE:return n>=40 && b[0]=='V' && b[1]=='Z';
    case XX_FILE_TYPE_ATHEOS_FS:return VW(1056,0x41465331);
    case XX_FILE_TYPE_BEOS_FS:return VW(32,0x42465331)||VW(544,0x42465331)||VW(1056,0x42465331);
    case XX_FILE_TYPE_ODS2_FS:return VSTR(1008,"DECFILE11A  ")||VSTR(1008,"DECFILE11B  ");
    case XX_FILE_TYPE_RT11_FS:return VSTR(1008,"DECRT11A    ");
    case XX_FILE_TYPE_ECMA67_FS:return VSTR(768,"VOL\x01")||VSTR(768,"VOL1");
    case XX_FILE_TYPE_F2FS:return VW(1024,0xF2F52010);
    case XX_FILE_TYPE_FOSSIL_FS:return VW(131072,0x3776AE89);
    case XX_FILE_TYPE_HAMMER_FS:return VSTR(0,"1\x30R\xc5MM\x41\xc8")||VSTR(0,"\xc8\x41MM\xc5R01");
    case XX_FILE_TYPE_HPOFS:return VSTR(54,"HPOFS");
    case XX_FILE_TYPE_HP_LIF:return n>=2 && b[0]==0x80 && !b[1];
    case XX_FILE_TYPE_JFS:return VSTR(32768,"JFS1");
    case XX_FILE_TYPE_LINUX_EXT:return VSTR(1080,"\x7d\x13");
    case XX_FILE_TYPE_LOCUS_FS:for(i=0;i<9;++i) if(VW(i*512,0xFFEEDDCD)||VW(i*512,0xFFEEDDCC)) return true; return false;
    case XX_FILE_TYPE_HPFS:return VW(8192,0xF995E849);
    case XX_FILE_TYPE_REFS:return VSTR(3,"ReFS\0\0\0\0") && VSTR(16,"FSRS");
    case XX_FILE_TYPE_NILFS2:return VSTR(1030,"\x34\x34");
    case XX_FILE_TYPE_AMIGA_PFS:return VW(1024,0x41465301)||VW(1024,0x50465302)||VW(1024,0x50465301)||VW(1024,0x6D754146)||VW(1024,0x6D755046);
    case XX_FILE_TYPE_QNX4:return VSTR(512,"/\0\0\0\0\0\0\0");
    case XX_FILE_TYPE_REISERFS:return VSTR(65588,"ReIsEr")||VSTR(8244,"ReIsEr")||VSTR(65536,"ReIsEr4");
    case XX_FILE_TYPE_SGI_EFS:return VW(540,0x00072959)||VW(540,0x0007295A);
    case XX_FILE_TYPE_AMIGA_SFS:return VSTR(0,"SFS");
    case XX_FILE_TYPE_SOLAR_FS:return VSTR(53,"SOL_FS  ");
    case XX_FILE_TYPE_UNICOS_FS:return VSTR(0,"nc1fs1cn")||VSTR(4096,"nc1fs1cn");
    case XX_FILE_TYPE_SYSV_FS:case XX_FILE_TYPE_XENIX_FS:for(i=0;i<16;++i) if(VW(i*512+(type==XX_FILE_TYPE_SYSV_FS?504:496),type==XX_FILE_TYPE_SYSV_FS?0xFD187E20:0x002B5544)||vdet_word(d,i*512+1016,type==XX_FILE_TYPE_SYSV_FS?0xFD187E20:0x002B5544,false)||vdet_word(d,i*512+1016,type==XX_FILE_TYPE_SYSV_FS?0xFD187E20:0x002B5544,true)) return true; return false;
    case XX_FILE_TYPE_UNIX_BFS:return VW(0,0x1BADFACE);
    case XX_FILE_TYPE_VXFS:return VW(1024,0xA501FCF5);
    case XX_FILE_TYPE_VMFS:return VW(1048576,0xC001D00D);
    case XX_FILE_TYPE_XIA_FS:return VW(572,0x012FD16D);
    case XX_FILE_TYPE_XFS:return VSTR(0,"XFSB");
    case XX_FILE_TYPE_ZFS: {
        int64_t positions[6]={8664,16344,131032,393176,total-131112,total-393256};
        for(i=0;i<6;++i) if(VSTR(positions[i],"\x11\x7a\x0c\xb1\x7a\xda\x10\x02")||VSTR(positions[i],"\x02\x10\xda\x7a\xb1\x0c\x7a\x11")) return true;
        return false;
    }
    case XX_FILE_TYPE_GFS2:return VW(65536,0x01161970);
    case XX_FILE_TYPE_DEC_DISKLABEL:return VW(16312,0x00032957);
    case XX_FILE_TYPE_DRAGONFLY_DISKLABEL:return VW(512,0xC4464C59);
    case XX_FILE_TYPE_HUMAN68K_PARTITIONS:return VSTR(2048,"X68K")||VSTR(1024,"X68K");
    case XX_FILE_TYPE_ATARI_AHDI:return n>=466 && (b[454]&1) && ((b[455]=='G'&&b[456]=='E'&&b[457]=='M')||(b[455]=='X'&&b[456]=='G'&&b[457]=='M')||(b[455]=='B'&&b[456]=='G'&&b[457]=='M'));
    case XX_FILE_TYPE_NEXT_DISKLABEL:for(i=0;i<4;++i) { int64_t at=i==0?0:i==1?2048:i==2?7680:8192; if(VSTR(at,"NeXT")||VSTR(at,"dlV2")||VSTR(at,"dlV3")) return true; } return false;
    case XX_FILE_TYPE_PLAN9_PARTITIONS:return VSTR(512,"part ");
    case XX_FILE_TYPE_RIO_KARMA_PARTITIONS:return VSTR(510,"\x56\xab");
    case XX_FILE_TYPE_SGI_VOLUME_HEADER:return VW(0,0x0BE5A941);
    case XX_FILE_TYPE_XENIX_PARTITIONS:return VSTR(21504,"\x34\x12");
    case XX_FILE_TYPE_ACORN_PARTITIONS:return n>=1 && total>=3584; /* factory checks boot checksum and secondary table */
    case XX_FILE_TYPE_XBOX360_LAYOUT:return total>INT64_C(0x130EB0000) && (VSTR(INT64_C(0x130EB0000),"FATX")||VSTR(INT64_C(0x130EB0000),"XTAF"));
    case XX_FILE_TYPE_AFF:return VSTR(0,"AFF10\r\n\0");
    case XX_FILE_TYPE_MD_RAID:return VW(0,0xA92B4EFC)||VW(4096,0xA92B4EFC)||(total>8192 && vdet_word(d,((total/512-16)&~INT64_C(7))*512,UINT32_C(0xA92B4EFC),false))||(total>=131072 && VW((total&~INT64_C(65535))-65536,0xA92B4EFC));
    case XX_FILE_TYPE_WINDOWS_LDM:return VSTR(3072,"PRIVHEAD");
    default:return false;
    }
#undef VSTR
#undef VW
}
