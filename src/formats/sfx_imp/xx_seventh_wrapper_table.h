/* SPDX-License-Identifier: MIT. Seventh batch private full-framing carrier helpers. */
#ifndef XX_SEVENTH_WRAPPER_TABLE_H
#define XX_SEVENTH_WRAPPER_TABLE_H
#include "../sfx_arcv2/xx_sixth_wrapper_table.h"
#include "xxfclib/formats/imp/xx_imp.h"
#include "xxfclib/formats/starkit/xx_starkit.h"
#include "xxfclib/formats/chm/xx_chm.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xx_seventh_ain.h"
#define W7_LIMIT 16777216U
#define W7_COUNT 4096U
static uint8_t*w7_load(Abstractformat*f,size_t*n,xx_pd_struct*pd) {int64_t limit=pm_available(f);uint8_t*b;size_t i;if(limit<=0 || limit>W7_LIMIT || wg_stop(pd))return NULL;*n=(size_t)limit;b=(uint8_t*)xx_mem_alloc(*n);if(!b)return NULL;for(i=0;i<*n;i+=65536U){size_t z=*n-i>65536U ? 65536U:*n-i;if(wg_stop(pd) || !pm_read(f,(int64_t)i,b+i,z)){xx_mem_free(b);return NULL;}}return b;}
static bool w7_range(size_t n,uint64_t p,uint64_t z) { return p<=n && z<=n-p; }
static uint16_t w7_crc16(const uint8_t *b,size_t n,uint16_t c,xx_pd_struct *pd) {
    size_t at=0U;
    while(at<n) { size_t part=n-at>4096U ? 4096U:n-at;
        if(wg_stop(pd)) return 0U;
        c=xx_crc16_ccitt_calc(c,b+at,part);at+=part;
    }
    return c;
}
static bool w7_string(const uint8_t *b,size_t n,size_t *p,size_t maximum) { size_t start=*p;while(*p<n && b[*p]) { if(*p-start>=maximum) return false;++*p; } if(*p==n)return false;++*p;return true; }
static bool w7_nested(Abstractformat *f,int64_t at,w5_open open,w5_close close,uint32_t expected,xx_pd_struct *pd) {
    Abstractformat *r=open(f->device,f->base_address+at);xx_archive_record_state *st=NULL;const xx_archive_record *rec;size_t count=0;bool ok=false;int64_t size=pm_available(f)-at;
    if(!r)return false;if(!wg_stop(pd) && xx_format_handle_base_info(r,pd) && r->format_size==size && r->number_of_archive_records && r->number_of_archive_records<=W7_COUNT) {
        st=xx_format_create_archive_records_reading(r,NULL,pd);if(st) { ok=true;while((rec=xx_format_get_current_archive_record(r,st))!=NULL) { if(wg_stop(pd) || ++count>W7_COUNT || rec->compressed_size<0 || rec->data_offset<f->base_address+at || !wg_range(pm_available(f),rec->data_offset-f->base_address,(uint64_t)rec->compressed_size)) {ok=false;break;} if(!xx_format_archive_record_move_to_next(r,st,pd))break;} if(!count || count!=r->number_of_archive_records || (expected && count!=expected))ok=false; }
    } if(st)xx_format_free_archive_records_reading(r,st);close(r);return ok;
}
/* Some legacy linkers retain SizeOfRawData for a pure BSS section whose
 * PointerToRawData is zero. The section has no initialized/code bytes, and
 * contributes no file extent. All physical PE ranges remain authenticated. */
static bool w7_carrier(Abstractformat*f,int64_t*low,xx_pd_struct*pd) {
    uint8_t h[64];uint64_t image,headers;uint32_t pe,size;uint16_t count,opt;int64_t table,limit=pm_available(f);wg_extent sections[96];unsigned i;
    if(w6_carrier(f,low,true,false,pd))return true;if(!pm_read(f,0,h,64) || xx_rt_memcmp(h,"MZ",2))return false;headers=(uint64_t)pm_le16(h+8)*16;image=pm_le16(h+4);if(!image || pm_le16(h+2)>511 || headers<28)return false;image=(image-1)*512+(pm_le16(h+2) ? pm_le16(h+2):512);if(headers>image || image>(uint64_t)limit || (uint64_t)pm_le16(h+24)+4U*pm_le16(h+6)>headers)return false;
    pe=pm_le32(h+60);if(pe<64 || pe>1048576 || !pm_read(f,pe,h,24) || xx_rt_memcmp(h,"PE\0\0",4))return false;count=pm_le16(h+6);opt=pm_le16(h+20);if(!count || count>96 || opt<64 || opt>4096 || !pm_read(f,(int64_t)pe+24,h,64) || (pm_le16(h)!=0x10b && pm_le16(h)!=0x20b))return false;table=(int64_t)pe+24+opt;size=pm_le32(h+60);if(size<table+(int64_t)count*40 || size>(uint64_t)limit || !wg_range(limit,table,(uint64_t)count*40))return false;
    for(i=0;i<count;++i){uint32_t raw,p,flags;if(wg_stop(pd) || !pm_read(f,table+(int64_t)i*40,h,40))return false;raw=pm_le32(h+16);p=pm_le32(h+20);flags=pm_le32(h+36);if(!p && (flags&0xe0)==0x80)raw=0;if(raw && (p<size || !wg_range(limit,p,raw)))return false;sections[i].lo=raw ? p:limit;sections[i].hi=raw ? (int64_t)p+raw:limit;}if(!wg_extents(sections,count,pd))return false;*low=64;return true;
}
typedef bool (*w7_validate)(Abstractformat *,int64_t,const uint8_t *,size_t,xx_pd_struct *);
/* A complete candidate is a bounded archive tail. A plausible first header
 * makes subsequent structural failure fatal, so a damaged archive cannot be
 * accepted as a later suffix. Weak signatures get a cheap grammar gate. */
static bool w7_gate(unsigned kind,const uint8_t *b,size_t n) {
    switch(kind) {
    case 1:return n>=39 && b[2]==1 && b[3]>=39;
    case 2:return n>=21 && pm_le16(b+2)>0 && pm_le16(b+2)<=W7_COUNT && ((b[4]>>4)==2 || b[4]==255);
    case 3:return n>=42 && (b[11+10]==0 || b[11+10]==2);
    case 4:return n>=25 && b[2]=='R' && pm_le16(b+5)==25;
    case 5:return n>=24 && (b[1]&15)>=1 && (b[1]&15)<=4 && (b[1]>>4)>=1 && (b[1]>>4)<=3;
    case 9:return n>=28 && (pm_be16(b+6)==0x130 || pm_be16(b+6)==0x140 || pm_be16(b+6)==0x160);
    default:return true;
    }
}
static bool w7_carried(Abstractformat *f,pm_stream *s,const uint8_t *sig,size_t siglen,int adjust,unsigned kind,w7_validate validate,const char *label,xx_pd_struct *pd) {
    int64_t low,limit=pm_available(f);size_t n,i;unsigned attempts=0;uint8_t *b;bool ok=false;
    if(!w7_carrier(f,&low,pd) || low>=limit || limit-low>W7_LIMIT) return false;n=(size_t)(limit-low);b=(uint8_t *)xx_mem_alloc(n);if(!b)return false;
    for(i=0;i<n;i+=65536U) { size_t z=n-i>65536U ? 65536U:n-i;if(wg_stop(pd) || !pm_read(f,low+(int64_t)i,b+i,z))goto done; }
    for(i=0;i+siglen<=n;++i) { int64_t p=low+(int64_t)i+adjust;size_t off;if((i&4095U)==0 && wg_stop(pd))goto done;if(xx_rt_memcmp(b+i,sig,siglen) || p<low)continue;off=(size_t)(p-low);if(!w7_gate(kind,b+off,n-off))continue;if(++attempts>8)break;
        if(validate(f,p,b+off,n-off,pd)) { ok=w6_component(f,s,p,(int64_t)(n-off),label);break; }
        /* RED 16-bit loaders can contain an earlier embedded engine I.EXE;
         * only that exact carrier helper may be skipped after its full CRC. */
        if(kind==1 && n-off>=41 && w7_range(n,off,b[off+3]) && !xx_rt_memcmp(b+off+26,"I.EXE",5) && w7_crc16(b+off+2,b[off+3]-4,65535,pd)==pm_be16(b+off+b[off+3]-2))continue;
        break;
    }
done:xx_mem_free(b);return ok && !wg_stop(pd);
}
static Abstractformat *w7_imp_open(xx_io_device*d,int64_t b) {xx_imp*r=xx_imp_create(d,b);return r ? &r->format:NULL;}
static void w7_imp_close(Abstractformat*f) {xx_imp_free((xx_imp*)f);}
static bool w7_imp(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    uint8_t h[42];uint32_t count,dir;size_t pos;unsigned chunks=0;uint16_t expected;if(n<42)return false;count=pm_le32(b+8);dir=pm_le32(b+4);if(!count || count>1024 || dir<42 || !w7_range(n,dir,12) || n-dir>1048576 || pm_le16(b+38)&5 || xx_rt_memcmp(b+dir,"IMPDE\0",6))return false;
    xx_rt_memcpy(h,b,42);expected=pm_le16(h+40);h[40]=h[41]=0;if((w6_crc(h,42)&65535)!=expected)return false;
    pos=dir;while(pos<n) {uint64_t bits=0;uint32_t plain,packed;unsigned i;if(wg_stop(pd) || ++chunks>4096 || !w7_range(n,pos,12) || xx_rt_memcmp(b+pos,"IMPDE\0",6))return false;for(i=0;i<6;++i)bits|=(uint64_t)b[pos+6+i]<<(8*i);plain=(uint32_t)((bits>>5)&0xfffff);packed=(uint32_t)((bits>>25)&0xfffff);if((bits&15)>1 || !plain || plain>8192 || packed<6 || !w7_range(n,pos+6,packed) || (!(bits&15) && packed<6U+plain))return false;pos+=6U+packed;}return w7_nested(f,at,w7_imp_open,w7_imp_close,count,pd);
}
static bool w7_red(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    size_t p=0;unsigned count=0;(void)f;(void)at;
    while(p<n) {size_t hs;uint32_t packed,raw;uint16_t method;if(wg_stop(pd) || ++count>W7_COUNT || !w7_range(n,p,39) || b[p]!='R' || b[p+1]!='R' || b[p+2]!=1)return false;hs=b[p+3];if(hs<39 || !w7_range(n,p,hs) || w7_crc16(b+p+2,hs-4,65535,pd)!=pm_be16(b+p+hs-2) || pm_le16(b+p+20) || pm_le16(b+p+22)!=1)return false;
        packed=pm_le32(b+p+8);raw=pm_le32(b+p+12);method=pm_le16(b+p+24);if((method!=1 && method!=9 && method!=11) || (method==1 && packed!=raw) || packed>INT32_MAX || raw>INT32_MAX || !w7_range(n,p+hs,packed) || !b[p+26] || !xx_rt_memchr(b+p+26,0,hs-28))return false;p+=hs+packed;
    }return count!=0;
}
static bool w7_ha(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    size_t p=4;unsigned i,count; (void)f;(void)at;if(n<4 || (count=pm_le16(b+2))==0 || count>W7_COUNT)return false;
    for(i=0;i<count;++i) {uint32_t packed,raw;uint8_t type,machine;size_t data;if(wg_stop(pd) || !w7_range(n,p,17))return false;type=b[p];packed=pm_le32(b+p+1);raw=pm_le32(b+p+5);if(type==255 || (type>>4)!=2 || ((type&15)>2 && (type&15)!=14 && (type&15)!=15) || packed>INT32_MAX || raw>INT32_MAX)return false;p+=17;if(!w7_string(b,n,&p,1024) || !w7_string(b,n,&p,1024) || p==n)return false;machine=b[p++];data=p+machine;if(!w7_range(n,p,machine) || !w7_range(n,data,packed))return false;
        if((type&15)==0 && packed!=raw)return false;if((type&15)>=14 && (packed || raw))return false;p=data+packed;
    }return p==n;
}
static bool w7_lzx(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    size_t p=10;unsigned count=0;uint64_t group=0;uint8_t method=0;(void)f;(void)at;if(n<10)return false;
    while(p<n) {uint8_t h[541];size_t z;uint32_t raw,packed,crc;if(wg_stop(pd) || ++count>W7_COUNT || !w7_range(n,p,31))return false;raw=pm_le32(b+p+2);packed=pm_le32(b+p+6);z=31U+b[p+30]+b[p+14];if(!b[p+30] || (b[p+11]!=0 && b[p+11]!=2) || (b[p+12]&~1U) || !w7_range(n,p,z) || !w7_range(n,p+z,packed))return false;xx_rt_memcpy(h,b+p,z);crc=pm_le32(h+26);xx_rt_memset(h+26,0,4);if(!w6_crc_checked(h,z,crc,pd))return false;
        if(group && method!=b[p+11])return false;method=b[p+11];group+=raw;if(group>W7_LIMIT)return false;if(packed) {if(!method && (group!=packed || !w6_crc_checked(b+p+z,packed,pm_le32(b+p+22),pd)))return false;group=0;}else if(!(b[p+12]&1) && raw)return false;p+=z+packed;
    }return count && !group;
}
static bool w7_sqx(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    size_t p=25;unsigned count=0;(void)f;(void)at;if(n<25 || b[2]!='R' || pm_le16(b+5)!=25 || xx_rt_memcmp(b+7,"-sqx-",5) || pm_le16(b+3)&0x10)return false;
    while(p<n) {uint8_t type;uint16_t flags,hs;uint64_t packed,raw;size_t cursor,name;if(wg_stop(pd) || !w7_range(n,p,7))return false;type=b[p+2];flags=pm_le16(b+p+3);hs=pm_le16(b+p+5);if(hs<7 || !w7_range(n,p,hs))return false;if(type=='A' || type=='S' || type=='X')return count && hs==7 && !flags && p+7==n;
        if(type!='D' || ++count>W7_COUNT || flags&0x8008 || hs<35)return false;cursor=26;packed=pm_le32(b+p+25);raw=pm_le32(b+p+29);if(flags&0x80) {if(hs<43)return false;packed|=(uint64_t)pm_le32(b+p+33)<<32;raw|=(uint64_t)pm_le32(b+p+37)<<32;cursor=34;}name=pm_le16(b+p+7+cursor);if(name>4096 || 7+cursor+2+name>hs || b[p+12]>4 || packed>INT64_MAX || raw>INT64_MAX || !w7_range(n,p+hs,packed))return false;p+=hs+(size_t)packed;
    }return false;
}
static bool w7_ain(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    uint16_t count,sum=0;uint32_t off;size_t i,pos=0,written=0,rc=0;wg_extent ranges[1024];uint8_t*dir;uint64_t group=0,skip=0;bool active=false,ok=false;(void)f;(void)at;
    if(n<24 || b[0]!='!' || (b[1]&15)!=4 || (b[1]>>4)<1 || (b[1]>>4)>3 || pm_le16(b+2))return false;for(i=0;i<22;++i)sum=(uint16_t)(sum+b[i]);count=pm_le16(b+8);off=pm_le32(b+14);if(sum!=(uint16_t)(pm_le16(b+22)^0x5555) || !count || count>1024 || off<24 || off>=n || n-off>1048576)return false;
    dir=(uint8_t*)xx_mem_alloc(1048576);if(!dir)return false;if(!w7_ain_directory(b+off,n-off,0,dir,1048576,&written,pd))goto done;
    for(i=0;i<count;++i) {const uint8_t*r;uint8_t flags;uint32_t raw,stored,member;if(wg_stop(pd) || !w7_range(written,pos,29))goto done;r=dir+pos;pos+=29;flags=r[22];raw=pm_le32(r+5);stored=pm_le32(r+9);member=pm_le32(r+13);if(flags&0xe7 || raw>W7_LIMIT || !w7_string(dir,written,&pos,1024) || pos>=written || dir[pos++] || !r[29])goto done;
        if(flags&16) {if(active)goto done;group=member;skip=0;active=true;}if(!active || group<24 || !w7_range(off,group+skip,raw))goto done;skip+=raw;if(flags&8) {if(stored!=skip)goto done;ranges[rc].lo=(int64_t)group;ranges[rc].hi=(int64_t)(group+skip);++rc;active=false;}
    }ok=!active && pos==written && wg_extents(ranges,rc,pd);
done:xx_mem_free(dir);return ok;
}
static bool w7_hap(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    size_t p=15;unsigned i,count=0;(void)f;(void)at;if(n<15)return false;for(i=4;i<15;++i)if(b[i])return false;
    while(p<n) {uint32_t packed,raw;uint8_t method;if(wg_stop(pd) || ++count>W7_COUNT || !w7_range(n,p,40) || pm_le32(b+p)!=0x574a688e || b[p+16] || !b[p+26] || !xx_rt_memchr(b+p+26,0,13))return false;packed=pm_le32(b+p+4);raw=pm_le32(b+p+22);method=b[p+39];if(packed>INT32_MAX || raw>INT32_MAX || (method!=0x15 && method!=0x16) || !w7_range(n,p+40,packed))return false;if(method==0x15 && (packed!=raw || !w6_crc_checked(b+p+40,packed,pm_le32(b+p+8),pd)))return false;p+=40+packed;
    }return count!=0;
}
static bool w7_zoo(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    wg_extent ranges[W7_COUNT*2+2];size_t rc=0,p;unsigned count=0;uint32_t first;(void)f;(void)at;if(n<34 || pm_le32(b+20)!=0xfdc4a7dc || (first=pm_le32(b+24))<34 || first+pm_le32(b+28)!=0)return false;ranges[rc].lo=0;ranges[rc].hi=ranges[rc].lo+(34);++rc;p=first;
    for(;;) {size_t hs=51;uint32_t next,data,packed,raw;if(wg_stop(pd) || ++count>W7_COUNT || !w7_range(n,p,51) || pm_le32(b+p)!=0xfdc4a7dc || (b[p+4]!=1 && b[p+4]!=2) || b[p+5]>2)return false;next=pm_le32(b+p+6);data=pm_le32(b+p+10);raw=pm_le32(b+p+20);packed=pm_le32(b+p+24);if(b[p+4]==2) {if(!w7_range(n,p,53))return false;hs=53U+pm_le16(b+p+51);if(!w7_range(n,p,hs))return false;if(hs>=58 && 58U+b[p+56]+b[p+57]>hs)return false;}ranges[rc].lo=(int64_t)p;ranges[rc].hi=ranges[rc].lo+((int64_t)hs);++rc;
        if(!next) {if(data || packed || raw)return false;break;}if(!b[p+38] || !data || !w7_range(n,data,packed) || (b[p+5]==0 && raw!=packed) || !w7_range(n,pm_le32(b+p+32),pm_le16(b+p+36)))return false;if(packed) {ranges[rc].lo=data;ranges[rc].hi=ranges[rc].lo+(packed);++rc;}p=next;
    }return count>1 && wg_extents(ranges,rc,pd);
}
static bool w7_cazip(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    uint8_t*raw;size_t used=0,wrote=0;bool ok=false;(void)f;(void)at;if(n<=22 || b[8]<'0' || b[8]>'9' || b[9]<'0' || b[9]>'9' || pm_le16(b+10)!=1 || pm_le16(b+12)!=1 || b[18] || b[19] || b[20]>1 || b[21]<4 || b[21]>6 || wg_stop(pd))return false;
    if(!xx_dcl_scan_memory(b+20,n-20,W7_LIMIT,&used,&wrote) || used!=n-20 || wrote>W7_LIMIT || wg_stop(pd))return false;raw=(uint8_t*)xx_mem_alloc(wrote ? wrote:1);if(!raw)return false;if(xx_dcl_decode_memory(b+20,n-20,raw,wrote,&wrote) && used==n-20 && !wg_stop(pd) && w6_crc_checked(raw,wrote,pm_le32(b+14),pd))ok=true;xx_mem_free(raw);return ok;
}
/* The extent table holds up to 2*W7_COUNT+1 entries (128 KiB), so it lives
 * on the heap: this validator runs beneath the content detector, whose own
 * frame already takes most of a 1 MiB thread stack. */
static bool w7_tgcf_walk(const uint8_t*b,size_t n,wg_extent*ranges,xx_pd_struct*pd) {
    size_t p,initial;bool extended;size_t rc=0;unsigned count=0;uint16_t version,name;if(n<28)return false;version=pm_be16(b+6);extended=version==0x160;if(version!=0x130 && version!=0x140 && !extended)return false;name=pm_be16(b+26);if(!name || name>1024 || !w7_range(n,28,name+4U+(extended ? 4U:0U)))return false;initial=28U+name+4U+(extended ? 4U:0U);p=extended ? pm_be32(b+28+name):initial;if(p<initial || p>n)return false;ranges[rc].lo=0;ranges[rc].hi=ranges[rc].lo+((int64_t)initial);++rc;
    while(p<n) {size_t q,data;uint32_t packed,raw;uint16_t method;if(n-p==10 && !xx_rt_memcmp(b+p,"TGCF",4) && pm_be32(b+p+4)>0 && pm_be32(b+p+4)<=W7_COUNT){p=n;break;}if(wg_stop(pd) || ++count>W7_COUNT || !w7_range(n,p,36) || xx_rt_memcmp(b+p,"TGCF",4) || pm_le16(b+p+12)==2)return false;method=pm_le16(b+p+14);packed=pm_be32(b+p+20);raw=pm_be32(b+p+24);if(method!=0 && method!=4)return false;q=p+36;if(!w7_string(b,n,&q,1024) || !w7_string(b,n,&q,1024) || !w7_range(n,q,5U+(extended ? 4U:0U)))return false;++q;data=extended ? pm_be32(b+q):q+4;if(extended)q+=4;q+=4;if(!w7_range(n,data,packed) || (method==0 && packed!=raw))return false;ranges[rc].lo=(int64_t)p;ranges[rc].hi=ranges[rc].lo+((int64_t)(q-p));++rc;if(packed) {ranges[rc].lo=(int64_t)data;ranges[rc].hi=ranges[rc].lo+(packed);++rc;}p=extended ? q:data+packed;
    }return count && wg_extents(ranges,rc,pd);
}
static bool w7_tgcf(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    wg_extent*ranges;bool ok;(void)f;(void)at;if(n<28)return false;
    ranges=(wg_extent*)xx_mem_alloc((W7_COUNT*2U+1U)*sizeof(*ranges));if(!ranges)return false;
    ok=w7_tgcf_walk(b,n,ranges,pd);xx_mem_free(ranges);return ok;
}
static bool w7_var(const uint8_t*b,size_t n,size_t*p,uint64_t*v) {unsigned i;uint64_t r=0;for(i=0;i<9 && *p<n;++i) {uint8_t c=b[(*p)++];r=(r<<7)|(c&127);if(c&128) {*v=r;return true;}}return false;}
static bool w7_col(const uint8_t*b,size_t n,size_t*p,uint64_t*size,uint64_t*pos) {*pos=0;return w7_var(b,n,p,size) && (!*size || w7_var(b,n,p,pos));}
static bool w7_prop(const uint8_t*b,size_t n,size_t*p) {uint64_t size,pos;if(!w7_col(b,n,p,&size,&pos))return false;if(size && !w7_col(b,n,p,&size,&pos))return false;return w7_col(b,n,p,&size,&pos);}
static Abstractformat*w7_starkit_open(xx_io_device*d,int64_t b) {xx_starkit*r=xx_starkit_create(d,b);return r ? &r->format:NULL;}
static void w7_starkit_close(Abstractformat*f) {xx_starkit_free((xx_starkit*)f);}
static bool w7_starkit(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    static const char schema[]="dirs[name:S,parent:I,files[name:S,size:I,date:I,contents:B]]";size_t p,q;uint64_t root,size,pos,value,dirs,views,viewsize,rows,total=0,i;if(n<24 || pm_be32(b+4)!=n || pm_be32(b+n-16)!=0x80000000 || pm_be32(b+n-12)!=n-16 || !(pm_be32(b+n-8)&0x80000000))return false;size=pm_be32(b+n-8)&0x7fffffff;root=pm_be32(b+n-4);if(size>4096 || !w7_range(n-16,root,size) || size<2+sizeof(schema)-1 || b[root]!=128 || b[root+1]!=(128+sizeof(schema)-1) || xx_rt_memcmp(b+root+2,schema,sizeof(schema)-1))return false;p=(size_t)root+2+sizeof(schema)-1;if(!w7_var(b,(size_t)(root+size),&p,&value) || value!=1 || !w7_col(b,(size_t)(root+size),&p,&size,&pos) || !size || size>65536 || !w7_range(n-16,pos,size))return false;p=(size_t)pos;q=(size_t)(pos+size);if(!w7_var(b,q,&p,&value) || value || !w7_var(b,q,&p,&dirs) || !dirs || dirs>256 || !w7_prop(b,q,&p) || !w7_col(b,q,&p,&size,&pos) || !w7_col(b,q,&p,&viewsize,&views) || p!=q || viewsize>1048576 || !w7_range(n-16,views,viewsize))return false;p=(size_t)views;q=(size_t)(views+viewsize);
    for(i=0;i<dirs;++i) {if(wg_stop(pd) || !w7_var(b,q,&p,&value) || value || !w7_var(b,q,&p,&rows) || rows>W7_COUNT-total)return false;total+=rows;if(rows && (!w7_prop(b,q,&p) || !w7_col(b,q,&p,&size,&pos) || !w7_col(b,q,&p,&size,&pos) || !w7_prop(b,q,&p)))return false;}if(p!=q || !total)return false;return w7_nested(f,at,w7_starkit_open,w7_starkit_close,(uint32_t)total,pd);
}
static bool w7_alz(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    size_t p=8;unsigned count=0;(void)f;(void)at;if(n<12 || xx_rt_memcmp(b,"ALZ\1",4))return false;
    while(p<n) {uint32_t magic;size_t name,width;uint64_t packed=0,raw=0;if(wg_stop(pd) || !w7_range(n,p,4))return false;magic=pm_le32(b+p);p+=4;if(magic==0x015a4c43) {if(!w7_range(n,p,8))return false;p+=8;continue;}if(magic==0x025a4c43)return count && p==n;if(magic!=0x015a4c42 || ++count>W7_COUNT || !w7_range(n,p,9))return false;name=pm_le16(b+p);width=b[p+7]>>4;if((b[p+7]&15) || !name || name>4096 || (width!=0 && width!=1 && width!=2 && width!=4 && width!=8))return false;p+=9;if(width) {unsigned i;if(!w7_range(n,p,6+2*width) || b[p]>2 || b[p+1])return false;for(i=0;i<width;++i){packed|=(uint64_t)b[p+6+i]<<(8*i);raw|=(uint64_t)b[p+6+width+i]<<(8*i);}if(!b[p] && packed!=raw)return false;p+=6+2*width;}if(!w7_range(n,p,name) || !w7_range(n,p+name,packed))return false;p+=name+(size_t)packed;
    }return false;
}
static Abstractformat*w7_chm_open(xx_io_device*d,int64_t b) {xx_chm*r=xx_chm_create(d,b);return r ? &r->format:NULL;}
static void w7_chm_close(Abstractformat*f) {xx_chm_free((xx_chm*)f);}
static bool w7_chm(Abstractformat*f,int64_t at,const uint8_t*b,size_t n,xx_pd_struct*pd) {
    uint64_t section,dir,size,content;uint32_t chunk,count,i;size_t total=0;wg_extent ext[3];if(n<96 || pm_le32(b+4)!=3 || pm_le32(b+8)!=96)return false;section=w6_u64(b+56,false);dir=w6_u64(b+72,false);size=w6_u64(b+80,false);content=w6_u64(b+88,false);if(!w7_range(n,section,24) || w6_u64(b+64,false)!=24 || pm_le32(b+(size_t)section)!=0x1fe || w6_u64(b+(size_t)section+8,false)!=n || size>262144 || size<84 || !w7_range(n,dir,size) || content<dir+size || content>n || xx_rt_memcmp(b+(size_t)dir,"ITSP",4) || pm_le32(b+(size_t)dir+8)!=84)return false;chunk=pm_le32(b+(size_t)dir+16);count=pm_le32(b+(size_t)dir+44);if(chunk<32 || chunk>65536 || !count || count>4096 || 84+(uint64_t)count*chunk!=size)return false;ext[0].lo=0;ext[0].hi=96;ext[1].lo=(int64_t)section;ext[1].hi=ext[1].lo+24;ext[2].lo=(int64_t)dir;ext[2].hi=ext[2].lo+(int64_t)size;if(!wg_extents(ext,3,pd))return false;
    for(i=0;i<count;++i) {size_t p=(size_t)dir+84+(size_t)i*chunk;if(wg_stop(pd))return false;if(!xx_rt_memcmp(b+p,"PMGL",4)) {uint32_t free=pm_le32(b+p+4);if(free<2 || free>chunk-20)return false;total+=pm_le16(b+p+chunk-2);if(total>W7_COUNT)return false;}else if(xx_rt_memcmp(b+p,"PMGI",4))return false;}return total && w7_nested(f,at,w7_chm_open,w7_chm_close,0,pd);
}
#endif
