/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Scoped legacy linker .bss layout for CAB SFX. */
static bool sfx_cab_legacy_bss(Abstractformat *f,int64_t *low,xx_pd_struct *pd)
{
    uint8_t dos[64],coff[24],opt[64],row[40]; carrier_extent spans[96];
    int64_t limit=pm_available(f); uint32_t pe,headers; uint16_t count,optional;
    uint64_t table; bool bss=false,physical=false;
    if(carrier_stop(pd) || !pm_read(f,0,dos,64) || xx_rt_memcmp(dos,"MZ",2)) return false;
    pe=xx_data_get_u32(dos+60,4,0,false);
    if(pe<64 || pe>1048576 || !pm_read(f,pe,coff,24) || xx_rt_memcmp(coff,"PE\0\0",4)) return false;
    count=xx_data_get_u16(coff+6,2,0,false); optional=xx_data_get_u16(coff+20,2,0,false);
    if(!count || count>96 || optional<64 || optional>4096 || !pm_read(f,(int64_t)pe+24,opt,64)) return false;
    headers=xx_data_get_u32(opt+60,4,0,false); table=(uint64_t)pe+24+optional;
    if(xx_data_get_u16(opt,2,0,false)!=0x10b || xx_data_get_u32(opt+36,4,0,false)!=512 ||
       !headers || (headers&511U) || headers<table+(uint64_t)count*40 || headers>(uint64_t)limit ||
       !carrier_range(limit,table,(uint64_t)count*40)) return false;
    for(unsigned i=0;i<count;i++) {
        if(carrier_stop(pd) || !pm_read(f,(int64_t)table+(int64_t)i*40,row,40)) return false;
        uint32_t n=xx_data_get_u32(row+16,4,0,false),at=xx_data_get_u32(row+20,4,0,false);
        if(n && !at) {
            uint32_t virtual_size=xx_data_get_u32(row+8,4,0,false),flags=xx_data_get_u32(row+36,4,0,false);
            if(bss || xx_rt_memcmp(row,".bss\0\0\0\0",8) || flags!=0xc0000080U || !virtual_size ||
               (((uint64_t)virtual_size+511U) & ~UINT64_C(511))!=n || xx_data_get_u32(opt+12,4,0,false)!=n) return false;
            bss=true; n=0;
        }
        if(n && (at<headers || (at&511U) || (n&511U) || !carrier_range(limit,at,n))) return false;
        spans[i].lo=n?at:limit; spans[i].hi=n?(int64_t)at+n:limit;
        if(n) physical=true;
    }
    if(!bss || !physical || !carrier_extents(spans,count,pd)) return false;
    *low=64; return true;
}
