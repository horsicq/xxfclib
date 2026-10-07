/* SPDX-License-Identifier: MIT
 * Cheap bounded gates only. A candidate is NOT claimed until its factory
 * validates the entire structure/decoded checksums. Any device seeks restore
 * the caller's position, including cancellation-free signature tail reads.
 */
static bool xadg_read(xx_io_device *d,int64_t at,uint8_t *p,size_t n) {
    int64_t saved; size_t done=0; bool ok=false;
    if(!d || at<0 || (saved=xx_io_tell(d))<0 || xx_io_seek64(d,at,SEEK_SET)!=0) return false;
    while(done<n) { ssize_t got=xx_io_read(d,p+done,n-done); if(got<=0 || (size_t)got>n-done) goto finish; done+=(size_t)got; }
    ok=true;
finish: if(xx_io_seek64(d,saved,SEEK_SET)!=0) ok=false; return ok;
}
static bool xadg_nsis(const uint8_t *p,int64_t available) {
    uint32_t header=xx_data_get_u32(p+20, 4, 0, false),span=xx_data_get_u32(p+24, 4, 0, false);
    return available>=32 && !(xx_data_get_u32(p, 4, 0, false)&~15U) && xx_data_get_u32(p+4, 4, 0, false)==0xDEADBEEFU &&
        xx_rt_memcmp(p+8,"NullsoftInst",12)==0 && header>0 && header<=16U*1024U*1024U && span>=32U && span<=(uint64_t)available;
}
static bool xx_archive_addition_candidate(xx_file_type_t type,xx_io_device *device,const uint8_t *first,size_t size,int64_t source_total) {
    uint32_t n,a,c; size_t i;
    if(!first || source_total<0 || size>(uint64_t)source_total) return false;
    switch(type) {
    case XX_FILE_TYPE_NSIS:
        if(size>=28U && xadg_nsis(first,source_total)) return true;
        if(size>=28U && first[0]=='M' && first[1]=='Z') {
            int64_t offset,limit=source_total-28; uint8_t header[28];
            if(limit>8*1024*1024) limit=8*1024*1024;
            for(offset=512;offset<=limit;offset+=512)
                if(!xadg_read(device,offset,header,sizeof(header))) return false;
                else if(xadg_nsis(header,source_total-offset)) return true;
        }
        return false;
    case XX_FILE_TYPE_NOWCOMPRESS:
        if(size<134U || first[0] || first[1]!=2U || !(n=xx_data_get_u32(first+8, 4, 0, true)) || n>65535U || 24U+110U*(uint64_t)n>(uint64_t)source_total || !first[24] || first[24]>31U) return false;
        a=0; for(i=24;i<130;++i) a+=first[i]; return a==xx_data_get_u32(first+130, 4, 0, true);
    case XX_FILE_TYPE_CPM_CRUNCH:
        if(size<11U || first[0]!=0x76U || (first[1]!=0xFEU && first[1]!=0xFDU)) return false;
        for(i=2;i<size && i<95U && first[i];++i) if(first[i]<32U || first[i]==127U) return false;
        return i>2U && i+5U<=size && !first[i] && first[i+1U]>=0x10U && first[i+1U]<=0x2FU && first[i+2U]>=0x10U && first[i+2U]<=0x2FU && first[i+3U]<=1U && !first[i+4U];
    case XX_FILE_TYPE_AMPLUS:
        return size>=12U && !xx_rt_memcmp(first,"FORM",4) && !xx_rt_memcmp(first+8,"APUP",4) && xx_data_get_u32(first+4, 4, 0, true)==(uint64_t)source_total-8U;
    case XX_FILE_TYPE_WARP:
        return size>=26U && !xx_rt_memcmp(first,"Warp v1.1",10) && xx_data_get_u16(first+10, 2, 0, true)<80U &&
            (!xx_rt_memcmp(first+12,"TOP",4) || !xx_rt_memcmp(first+12,"BOT",4)) && xx_data_get_u16(first+16, 2, 0, true)==1U && xx_data_get_u16(first+18, 2, 0, true)<=3U && xx_data_get_u32(first+22, 4, 0, true)<=(uint64_t)source_total-26U;
    case XX_FILE_TYPE_LHWARP:
        if(size<36U || first[0]!=1U || first[1]>3U || first[2]>9U || (!first[1] && !first[2]) || first[3] || xx_data_get_u16(first+4, 2, 0, true)>xx_data_get_u16(first+6, 2, 0, true) || xx_data_get_u16(first+6, 2, 0, true)>=80U || xx_data_get_u32(first+8, 4, 0, true)>65535U || xx_data_get_u32(first+12, 4, 0, true)>xx_data_get_u32(first+8, 4, 0, true)) return false;
        a=first[1]<3U?16U:20U; c=xx_data_get_u32(first+12, 4, 0, true);
        if(c) return a+(uint64_t)c<=(uint64_t)source_total;
        if(size<a+(first[1]?20U:16U) || first[a+2U]!=xx_data_get_u16(first+4, 2, 0, true)) return false;
        if(!first[1]) return xx_data_get_u32(first+a+4U, 4, 0, true)==11264U;
        if(first[a+1U]!=0 && first[a+1U]!=2U) return false;
        n=first[a+4U]|(uint32_t)first[a+5U]<<8U|(uint32_t)first[a+6U]<<16U;
        if(n&~0x3FFFFFU) { return false; } c=0; for(i=0;i<22U;++i) if(n&(1U<<i)) c+=528U;
        return xx_data_get_u32(first+a+8U, 4, 0, true)==c && xx_data_get_u32(first+a+12U, 4, 0, true)<=(uint64_t)source_total-a-20U;
    case XX_FILE_TYPE_COMPDISK:
        return size>=16U && !xx_rt_memcmp(first,"COMP",4) && xx_data_get_u32(first+4, 4, 0, true)==5U && !xx_data_get_u32(first+8, 4, 0, true) && !xx_data_get_u32(first+12, 4, 0, true) && source_total>=16+80*6;
    case XX_FILE_TYPE_ARC_CBM:
        if(size>=11U && (first[0]==1U || first[0]==2U) && first[1]<=(first[0]==1U?2U:5U) && first[10]>0U && first[10]<=16U && (first[9]=='P' || first[9]=='S' || first[9]=='U' || (first[0]==2U && first[9]=='R'))) {
            n=first[7]|(uint32_t)first[8]<<8U; return n && n*254U<=(uint64_t)source_total && size>=11U+first[10]+(first[0]==2U?3U:0U);
        }
        /* BASIC SYS SFX has a line-derived exact record offset. */
        if(size>=9U && first[6]==0x9EU && first[7]=='(') {
            uint8_t record[32]; n=first[4]|(uint32_t)first[5]<<8U;
            if(n<7U || n>4096U) { return false; } a=(n-6U)*254U; if(n==15U && first[8]=='7') --a;
            return source_total>=a+32U && xadg_read(device,a,record,sizeof(record)) && xx_archive_addition_candidate(type,NULL,record,sizeof(record),source_total-a);
        }
        return false;
    case XX_FILE_TYPE_CRUNCHDISK:
        if(size<32U || xx_rt_memcmp(first,"CDF0",4)) return false;
        n=xx_data_get_u32(first+4, 4, 0, true); a=xx_data_get_u32(first+8, 4, 0, true); c=xx_data_get_u32(first+12, 4, 0, true);
        return n>=128U && n<=8192U && !(n&(n-1U)) && a>0U && a<=64U && c>0U && c<=2U &&
            xx_data_get_u32(first+16, 4, 0, true)<=xx_data_get_u32(first+20, 4, 0, true) && xx_data_get_u32(first+20, 4, 0, true)<=255U && !first[25] && xx_data_get_u16(first+28, 2, 0, true)<=4U && xx_data_get_u16(first+30, 2, 0, true)<=2U;
    case XX_FILE_TYPE_LHF:
        return size>=8U && !xx_rt_memcmp(first,"LhF",4) && xx_data_get_u32(first+4, 4, 0, true)==(uint64_t)source_total;
    case XX_FILE_TYPE_SHRINK_CDAF:
        return size>=26U && !xx_rt_memcmp(first,"FORM",4) && xx_data_get_u32(first+4, 4, 0, true)==(uint64_t)source_total-8U && !xx_rt_memcmp(first+8,"CDAFNAME",8) && xx_data_get_u32(first+16, 4, 0, true)==6U && !xx_rt_memcmp(first+20,"shrink",6);
    case XX_FILE_TYPE_SPACK:
        if(size>=6U && !xx_rt_memcmp(first,"PACKFI",6) && source_total>=26) { uint8_t tail[12];
            return xadg_read(device,source_total-12,tail,sizeof(tail)) && (!xx_rt_memcmp(tail,"INDX",4) || !xx_rt_memcmp(tail+4,"INDX",4)); }
        if(size>=68U && source_total>6628 && xx_data_get_u32(first, 4, 0, true)==0x3F3U && xx_data_get_u32(first+40, 4, 0, true)==0x42ADFFFCU && xx_data_get_u32(first+48, 4, 0, true)==0x2C780004U && xx_data_get_u32(first+52, 4, 0, true)==0x4EAEFE68U && xx_data_get_u32(first+60, 4, 0, true)==0x208093C9U && xx_data_get_u32(first+64, 4, 0, true)==0x4EAEFEDAU) return true;
        return false;
    case XX_FILE_TYPE_PCOMPRESS_PACK:
        if(size<10U || xx_rt_memcmp(first,"PACK",4)) return false;
        for(i=4;i<size && i<94U && first[i]!='\n';++i) if((first[i]&127U)<32U) return false;
        return i>4U && i+5U<=size && first[i]=='\n' && xx_data_get_u32(first+i+1U, 4, 0, true)<=(uint64_t)source_total-i-5U;

    case XX_FILE_TYPE_BALZ:
        return size>=13U && first[0]==0xbaU && first[5]==0 && first[6]==0 && first[7]==0 && first[8]==0 && xx_data_get_u32(first+1, 4, 0, false)<=64U*1024U*1024U;
    case XX_FILE_TYPE_PAQ8:
        if(size>=10U && (!xx_rt_memcmp(first,"paq8l -",7)||!xx_rt_memcmp(first,"paq8f -",7)||!xx_rt_memcmp(first,"paq8o -",7)))
            return first[7]>='0' && first[7]<='9' && first[8]==13U && first[9]==10U;
        return size>=11U && !xx_rt_memcmp(first,"paq8jd -",8) && first[8]>='0' && first[8]<='9' && first[9]==13U && first[10]==10U;
    case XX_FILE_TYPE_LRZIP:
        return size>=24U && !xx_rt_memcmp(first,"LRZI",4) && first[4]==0U && first[5]==6U;
    case XX_FILE_TYPE_GRZIP:
        return size>=12U && !xx_rt_memcmp(first,"GRZipII\0\2\4:)",12);
    case XX_FILE_TYPE_LPAQ8:
        return size>=10U && first[0]=='p' && first[1]=='Q' && first[2]==8 && first[3]>='0' && first[3]<='9' && first[8]<=2;
    case XX_FILE_TYPE_LPAQ1:
        return size>=9U && first[0]=='p' && first[1]=='Q' && first[2]==1 && first[3]>='0' && first[3]<='9';
    case XX_FILE_TYPE_LPAQ5:
        return size>=9U && first[0]=='p' && first[1]=='Q' && first[2]==5 && first[3]>='0' && first[3]<='9';
    case 2220: /* Savage: custom LH5 compressed floppy, not standard LHA. */
        return size>=31U && first[0]==29 && !xx_rt_memcmp(first+2,"*SVG*",5) && xx_data_get_u32(first+11, 4, 0, false)==901120U && xx_data_get_u32(first+7, 4, 0, false)<=(uint64_t)source_total-31U;
    case 2221: case 2222: case 2223: case 2224: case 2225: {
        static const uint32_t words[5][5]={
          {0x4eaefdd8,0x4a806700,0x01142e28,0x00a07254,0x2f410020},
          {0xfdd823c0,0x2c404eae,0x0000405a,0x207c0000,0x020c7600},
          {0x4c534658,0x2c790000,0x01144aaa,0x00ac6600,0x001841ea},
          {0x43f90000,0x00000318,0x02ec2c79,0x00000004,0x4eaefdd8},
          {0x42adfff4,0x00044eae,0x4aac00ac,0x67026014,0x41ec005c}};
        static const uint32_t offsets[2][5]={{40,48,56,60,64},{60,68,76,80,84}};
        unsigned which=(unsigned)type-2221U,group=which<2?1:0;
        if(size<88U||xx_data_get_u32(first, 4, 0, true)!=0x3f3U)return false;
        for(i=0;i<5;++i)if(xx_data_get_u32(first+offsets[group][i], 4, 0, true)!=words[which][i])return false;
        return true;
    }
    default: return false;
    }
}
