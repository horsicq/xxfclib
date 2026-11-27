/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Scoped physical layout of UPX-packed PE SFX. */
static bool sfx7_packed_pe_overlay(Abstractformat *f,int64_t *overlay,xx_pd_struct *pd)
{
    uint8_t dos[64],coff[24],opt[64],row[40]; carrier_extent spans[96];
    int64_t limit=pm_available(f),end; uint32_t pe,headers; uint16_t count,optional,magic;
    bool upx0=false,upx1=false; uint64_t first=UINT64_MAX,physical,table;
    if(carrier_stop(pd) || !pm_read(f,0,dos,64) || xx_rt_memcmp(dos,"MZ",2)) return false;
    pe=xx_data_get_u32(dos+60,4,0,false);
    if(pe<64 || pe>1048576 || !pm_read(f,pe,coff,24) || xx_rt_memcmp(coff,"PE\0\0",4)) return false;
    count=xx_data_get_u16(coff+6,2,0,false); optional=xx_data_get_u16(coff+20,2,0,false);
    if(!count || count>96 || optional<64 || optional>4096 || !pm_read(f,(int64_t)pe+24,opt,64)) return false;
    magic=xx_data_get_u16(opt,2,0,false); headers=xx_data_get_u32(opt+60,4,0,false);
    table=(uint64_t)pe+24+optional; physical=(table+(uint64_t)count*40+511U)&~UINT64_C(511);
    if((magic!=0x10b && magic!=0x20b) || xx_data_get_u32(opt+36,4,0,false)!=512U ||
       headers<=physical || (headers&511U) || !carrier_range(limit,0,headers) || !carrier_range(limit,table,(uint64_t)count*40)) return false;
    end=(int64_t)physical;
    for(unsigned i=0;i<count;i++) {
        if(carrier_stop(pd) || !pm_read(f,(int64_t)table+(int64_t)i*40,row,40)) return false;
        uint32_t bytes=xx_data_get_u32(row+16,4,0,false), at=xx_data_get_u32(row+20,4,0,false);
        if(!xx_rt_memcmp(row,"UPX0\0\0\0\0",8)) {
            if(upx0 || bytes || !xx_data_get_u32(row+8,4,0,false)) return false;
            upx0=true;
        }
        if(!xx_rt_memcmp(row,"UPX1\0\0\0\0",8)) { if(upx1 || !bytes) return false; upx1=true; }
        if(bytes && (at<physical || !carrier_range(limit,at,bytes))) return false;
        spans[i].lo=bytes?at:limit; spans[i].hi=bytes?(int64_t)at+bytes:limit;
        if(bytes && at<first) first=at;
        if(bytes && (int64_t)at+bytes>end) end=(int64_t)at+bytes;
    }
    if(!upx0 || !upx1 || first!=physical || headers<=first || headers>(uint64_t)end || !carrier_extents(spans,count,pd)) return false;
    *overlay=end; return true;
}
static bool sfx7_packed_overlay_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    static const uint8_t sig[]={0x37,0x7a,0xbc,0xaf,0x27,0x1c};
    int64_t low,limit=pm_available(f); unsigned candidates=0;
    if(!sfx7_packed_pe_overlay(f,&low,pd) || low>=limit) return false;
    int64_t end=limit-low>16777216?low+16777216:limit;
    while(low<=end-6 && !carrier_stop(pd)) {
        int64_t at=xx_io_find_bytes_buffer_optimize_ex(f->device,f->base_address+low,end-low,sig,sizeof(sig),xx_get_file_buffer_size(),pd);
        if(at<0 || ++candidates>8) return false;
        at-=f->base_address;
        if(executable_carrier_at_parse(f,s,at,pd)) return true;
        if(s->count) return false;
        low=at+1;
    }
    return false;
}
