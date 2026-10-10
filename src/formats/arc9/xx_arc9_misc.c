/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded parsers for vendor-produced ARC9 containers.
 * Layout evidence and cross-checks: arc9/FORMAT.md. No payload execution. */
#include "xx_arc9_misc.h"
#include "../xx_payload_members.h"
#include "xxfclib/formats/starkit/xx_starkit.h"
#include "xxfclib/formats/fat/xx_fat.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#define AM_CAP (UINT64_C(64)*1024*1024)
typedef struct am_read_context {uint32_t crc;uint64_t resident;} am_read_context;
static uint32_t am_u32(const uint8_t *p) {return xx_data_get_u32(p,4,0,false);}
static bool am_stop(xx_pd_struct *pd) {return pd && xx_pd_is_stopped(pd);}
static bool am_span(uint64_t p,uint64_t z,uint64_t n) {return p<=n && z<=n-p;}
static uint64_t am_limit(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    return v?xx_var_get_u64(v):UINT64_C(256)*1024*1024;
}
static uint64_t am_resident(pm_stream *s) {
    uint64_t used=s?(uint64_t)s->capacity*sizeof(pm_member):0;size_t i;
    if(s)for(i=0;i<s->count;++i){pm_member *m=&s->items[i];uint64_t z=m->memory?(uint64_t)m->size:0;
        if(m->display_name)z+=xx_rt_strlen(m->display_name)+1;
        if(m->context)z+=sizeof(am_read_context);
        used+=z;}
    return used;
}
static bool am_budget(Abstractformat *f,pm_stream *s,uint64_t extra) {
    uint64_t used=am_resident(s),limit=am_limit(f);return used<=limit&&extra<=limit-used;
}
static bool am_decode_budget(Abstractformat *f,pm_member *m) {
    am_read_context *c=m->context;
    return c&&am_budget(f,NULL,c->resident+(uint64_t)m->packed_size+(uint64_t)m->size);
}
static bool am_device_leaf(const char *p,size_t n) {
    char leaf[5];size_t i=0;while(i<n&&p[i]!='.'&&i<sizeof(leaf)-1){unsigned char c=(unsigned char)p[i];leaf[i]=(char)(c>='a'&&c<='z'?c-32:c);++i;}leaf[i]=0;
    if(i<n&&p[i]!='.')return false;
    return !xx_rt_strcmp(leaf,"CON")||!xx_rt_strcmp(leaf,"PRN")||!xx_rt_strcmp(leaf,"AUX")||!xx_rt_strcmp(leaf,"NUL")||
           (i==4&&(!xx_rt_memcmp(leaf,"COM",3)||!xx_rt_memcmp(leaf,"LPT",3))&&leaf[3]>='1'&&leaf[3]<='9');
}
static bool am_name(Abstractformat *f,pm_stream *s,const char *name,int64_t at,int64_t size) {
    char *copy;size_t i,n=xx_rt_strlen(name),part=0;
    if(!n || n>4096 || name[0]=='/' || name[0]=='\\') return false;
    if(!am_budget(f,s,n+1+(s->count==s->capacity?(uint64_t)(s->capacity?s->capacity:8)*sizeof(pm_member):0)))return false;
    copy=xx_str_dup(name);if(!copy)return false;
    for(i=0;i<=n;++i) {unsigned c=(unsigned char)copy[i];
        if(c=='\\')copy[i]='/';
        if(c && (c<32 || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*'))goto bad;
        if(!c || copy[i]=='/') {size_t z=i-part;if(!z || (z==1&&copy[part]=='.') || (z==2&&copy[part]=='.'&&copy[part+1]=='.') || copy[i-1]=='.' || copy[i-1]==' ' || am_device_leaf(copy+part,z))goto bad;part=i+1;}
    }
    if(!am_budget(f,s,s->count==s->capacity?(uint64_t)(s->capacity?s->capacity:8)*sizeof(pm_member):0)||!pm_add(f,s,name,at,size))goto bad;
    s->items[s->count-1].display_name=copy;return true;
bad:xx_str_free(copy);return false;
}
static bool am_memory(Abstractformat *f,pm_stream *s,const char *name,const uint8_t *p,size_t n) {
    uint8_t *copy;if(n>AM_CAP || !am_budget(f,s,n) || !am_name(f,s,name,0,0))return false;
    if(!am_budget(f,s,n?n:1))return false;copy=xx_mem_alloc(n?n:1);if(!copy)return false;if(n)xx_rt_memcpy(copy,p,n);
    s->items[s->count-1].memory=copy;s->items[s->count-1].size=(int64_t)n;return true;
}
/* Locate the exact PE overlay; an embedded signature inside a section is not
 * enough to select an installer. Raw containers start at their own offset. */
static int64_t am_overlay(Abstractformat *f) {
    uint8_t h[64],p[24],section[40];uint32_t pe;unsigned count,opt,i;uint64_t end,n=(uint64_t)pm_available(f);
    if(n<2 || !pm_read(f,0,h,2))return -1;if(h[0]!='M'||h[1]!='Z')return 0;
    if(n<64 || !pm_read(f,0,h,64))return -1;pe=am_u32(h+60);
    if(!am_span(pe,24,n)||!pm_read(f,pe,p,24)||xx_rt_memcmp(p,"PE\0\0",4))return -1;
    count=xx_data_get_u16(p+6,2,0,false);opt=xx_data_get_u16(p+20,2,0,false);
    if(!count || count>96 || opt<64 || opt>4096 || !am_span((uint64_t)pe+24,opt+(uint64_t)count*40,n) || !pm_read(f,(int64_t)pe+84,h,4))return -1;
    end=am_u32(h);
    for(i=0;i<count;++i) {uint64_t finish;if(!pm_read(f,(int64_t)pe+24+opt+(int64_t)i*40,section,40))return -1;
        finish=(uint64_t)am_u32(section+16)+am_u32(section+20);if(finish>n)return -1;if(finish>end)end=finish;}
    /* Some signed launchers append their archive after WIN_CERTIFICATE. */
    if(!pm_read(f,(int64_t)pe+24,h,2))return -1;
    {unsigned dd=xx_data_get_u16(h,2,0,false)==0x10b?128:xx_data_get_u16(h,2,0,false)==0x20b?144:0;
        if(dd && opt>=dd+8 && pm_read(f,(int64_t)pe+24+dd,h,8)) {uint64_t cert=am_u32(h),bytes=am_u32(h+4);
            if(bytes>=8 && am_span(cert,bytes,n) && (cert==end || (am_span(end,bytes,n) && cert+bytes==n))) {uint64_t at=cert;bool prefix=cert!=end;
                if(prefix) {uint8_t first[8],last[8];
                    if(!pm_read(f,(int64_t)end,first,8)||!pm_read(f,(int64_t)cert,last,8)||xx_rt_memcmp(first,last,8))return (int64_t)end;
                    /* A re-signed launcher retains its older certificate
                     * before the appended archive. Their signatures differ. */
                }
                while(at<cert+bytes) {uint32_t size;if(!pm_read(f,(int64_t)at,h,8))return -1;size=am_u32(h);
                    if(size<8 || !am_span(at,size,cert+bytes) || xx_data_get_u16(h+4,2,0,false)!=0x200 || xx_data_get_u16(h+6,2,0,false)!=2)return -1;
                    at=(at+size+7)&~UINT64_C(7);}
                if(at!=cert+bytes)return -1;end=prefix?end+bytes:at;
            }
        }
    }
    return end<=n?(int64_t)end:-1;
}
static int64_t am_archive_end(Abstractformat *f) {
    uint8_t h[64],p[24];uint32_t pe;unsigned opt,dd;uint64_t cert,bytes,n=(uint64_t)pm_available(f);
    if(n<64||!pm_read(f,0,h,64)||h[0]!='M'||h[1]!='Z')return (int64_t)n;pe=am_u32(h+60);
    if(!am_span(pe,24,n)||!pm_read(f,pe,p,24)||xx_rt_memcmp(p,"PE\0\0",4))return (int64_t)n;
    opt=xx_data_get_u16(p+20,2,0,false);if(!pm_read(f,(int64_t)pe+24,h,2))return (int64_t)n;
    dd=xx_data_get_u16(h,2,0,false)==0x10b?128:xx_data_get_u16(h,2,0,false)==0x20b?144:0;
    if(!dd||opt<dd+8||!pm_read(f,(int64_t)pe+24+dd,h,8))return (int64_t)n;cert=am_u32(h);bytes=am_u32(h+4);
    if(bytes<8||!cert||!am_span(cert,bytes,n)||cert+bytes!=n||!pm_read(f,(int64_t)cert,h,8)||am_u32(h)<8||am_u32(h)>bytes||xx_data_get_u16(h+4,2,0,false)!=0x200||xx_data_get_u16(h+6,2,0,false)!=2)return (int64_t)n;
    return (int64_t)cert;
}
typedef struct am_sink {uint8_t *p;size_t n,at;} am_sink;
static ssize_t am_write(xx_io_device *d,const void *p,size_t n) {am_sink *s=d->priv;if(n>s->n-s->at)return -1;if(n)xx_rt_memcpy(s->p+s->at,p,n);s->at+=n;return (ssize_t)n;}
static bool am_inflate(const uint8_t *p,size_t n,uint8_t *out,size_t z,xx_pd_struct *pd) {
    xx_io_device sink;am_sink s={out,z,0};size_t used=0;xx_mem_zero(&sink,sizeof(sink));sink.priv=&s;sink.write=am_write;
    return !am_stop(pd) && xx_deflate_unpack_memory_to_device_ex(p,n,&sink,&used,false,pd) && used==n && s.at==z && !am_stop(pd);
}
static bool am_gzip(const uint8_t *p,size_t n,uint8_t *out,size_t z,xx_pd_struct *pd) {
    return n>=18 && !xx_rt_memcmp(p,"\x1f\x8b\x08\x00",4) && am_u32(p+n-4)==(uint32_t)z && am_inflate(p+10,n-18,out,z,pd) && xx_crc32_calc(0,out,z)==am_u32(p+n-8);
}
static bool am_desksoft(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[19],c;int64_t at=am_overlay(f),n=pm_available(f);char name[4097];size_t k;
    if(at<0||!pm_read(f,at,h,19)||xx_rt_memcmp(h,"<Setup Data Header>",19))return false;at+=19;
    for(;;) {uint32_t size;if(am_stop(pd)||!pm_read(f,at,h,16))return false;
        if(!xx_rt_memcmp(h,"<Setup Data End>",16)) {s->size=at+16;return s->count && s->size==n;}
        for(k=0;k<4096;++k) {if(!pm_read(f,at++,&c,1))return false;name[k]=(char)c;if(!c)break;}if(k==4096||!k)return false;
        if(!pm_read(f,at,h,4))return false;size=am_u32(h);at+=4;if(!am_name(f,s,name,at,size))return false;at+=size;
    }
}
static bool am_visualware(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[28],*packed=NULL,*plain=NULL;uint64_t at,n=(uint64_t)pm_available(f);uint32_t z=0,count=0,block;unsigned i;size_t p=0;bool ok=false,legacy=false;int64_t overlay=am_overlay(f);
    if(overlay<0)return false;at=(uint64_t)overlay;
    for(i=0;i<3;++i) {const char *sig=i==0?"\\]_B":i==1?"\\]_V":"\\]_Z";
        if(am_stop(pd)||!pm_read(f,(int64_t)at,h,28)||xx_rt_memcmp(h,"JKMNPQSTVWYZ",12)||xx_rt_memcmp(h+12,sig,4))return false;
        if(!i)legacy=!xx_rt_memcmp(h+20,"\xca\xfe\xba\xbe",4);
        block=am_u32(h+(legacy?16:24));if(legacy&&i==2){if(block<4)return false;block-=4;z=am_u32(h+20);}
        at+=legacy?(i==2?24:20):28;if(!block || !am_span(at,block,n))return false;
        if(i<2)at=(at+block+15)&~UINT64_C(15);
    }
    if(block>AM_CAP || block<26 || !am_budget(f,s,block))return false;packed=xx_mem_alloc(block);if(!packed||!pm_read(f,(int64_t)at,packed,block))goto done;
    if(!legacy){for(i=0;i<block;++i) {if(!(i&65535U)&&am_stop(pd))goto done;packed[i]=(uint8_t)((packed[i]^(uint8_t)i)+0x9c);}
        count=am_u32(packed);z=am_u32(packed+4);if(!count||count>65536)goto done;}
    if(z>AM_CAP||!am_budget(f,s,(uint64_t)block+2U*z+2U*(uint64_t)(legacy?65536:count)*sizeof(pm_member)))goto done;plain=xx_mem_alloc(z?z:1);if(!plain||!am_gzip(packed+(legacy?0:8),block-(legacy?0:8),plain,z,pd))goto done;
    for(i=0;legacy?p<z:i<count;++i) {char name[256];uint32_t size;size_t len;
        if(i>=65536)goto done;
        if(am_stop(pd)||!am_span(p,5,z)||am_u32(plain+p)!=UINT32_C(0xeadc12f0))goto done;len=plain[p+4];p+=5;
        if(!len || !am_span(p,len+5,z) || plain[p+len]!=0 || xx_rt_memchr(plain+p,0,len))goto done;xx_rt_memcpy(name,plain+p,len);name[len]=0;p+=len+1;size=am_u32(plain+p);p+=4;
        if(!am_span(p,size,z)||!am_memory(f,s,name,plain+p,size))goto done;s->items[s->count-1].packed_size=block;s->items[s->count-1].compression_method=8;p+=size;
    }
    if(p!=z)goto done;s->size=(int64_t)n;ok=true;
done:xx_mem_free(packed);xx_mem_free(plain);return ok;
}
static bool am_lzh_read(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    uint8_t *packed=NULL,*plain=NULL;size_t written=0,at=0;bool ok=false;
    if((uint64_t)m->packed_size>AM_CAP || (uint64_t)m->size>AM_CAP || !am_decode_budget(f,m) || am_stop(pd))return false;
    packed=xx_mem_alloc((size_t)m->packed_size);plain=xx_mem_alloc(m->size?(size_t)m->size:1);
    if(!packed||!plain||!pm_read(f,m->offset-f->base_address,packed,(size_t)m->packed_size))goto done;
    if(!xx_lzh1_decode_memory(packed,(size_t)m->packed_size,plain,(size_t)m->size,&written)||written!=(uint64_t)m->size)goto done;
    while(out && at<written) {ssize_t got;size_t n=written-at;if(n>65536)n=65536;if(am_stop(pd))goto done;got=xx_io_write(out,plain+at,n);if(got<=0||(size_t)got>n)goto done;at+=(size_t)got;}ok=!am_stop(pd);
done:xx_mem_free(packed);xx_mem_free(plain);return ok;
}
static bool am_meta(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[112];int64_t at=am_overlay(f),n=am_archive_end(f);unsigned count=0;
    if(at<0||!pm_read(f,at,h,4)||am_u32(h)!=UINT32_C(0xabba1973))return false;at+=4;
    for(;;) {char name[104];uint32_t packed,plain;unsigned len;
        if(am_stop(pd)||!pm_read(f,at,h,8))return false;
        if(am_u32(h+4)==UINT32_C(0xabc01973)) {s->size=pm_available(f);return count && at+8==n && am_u32(h)>0;}
        if(++count>65536||!pm_read(f,at,h,112))return false;len=h[0];packed=am_u32(h+104);plain=am_u32(h+108);
        if(!len||len>103||!packed||packed>AM_CAP||plain>AM_CAP||xx_rt_memchr(h+1,0,len))return false;xx_rt_memcpy(name,h+1,len);name[len]=0;
        if(!am_name(f,s,name,at+112,packed))return false;s->items[s->count-1].size=plain;s->items[s->count-1].read_all=am_lzh_read;s->items[s->count-1].compression_method=1;at+=112+(int64_t)packed;
    }
}
static bool am_evd(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[24],sig[8];int64_t at=24,n=pm_available(f);
    if(!pm_read(f,0,h,24)||am_u32(h)||am_u32(h+4)||am_u32(h+8)!=12||(am_u32(h+12)!=1&&am_u32(h+12)!=2)||!am_u32(h+16)||!am_u32(h+20))return false;
    while(at<n) {uint16_t type;uint32_t id,z;char name[48];if(am_stop(pd)||!pm_read(f,at,h,12))return false;
        type=xx_data_get_u16(h,2,0,false);id=am_u32(h+4);z=am_u32(h+8);if(type!=xx_data_get_u16(h+2,2,0,false)||(type!=1&&type!=2)||!id||z<8||!pm_read(f,at+12,sig,8))return false;
        if(type==1?(sig[0]!=255||sig[1]!=216||sig[2]!=255):xx_rt_memcmp(sig,"\x89PNG\r\n\x1a\n",8))return false;
        xx_rt_snprintf(name,sizeof(name),"%u.%s",id,type==1?"jpg":"png");if(!am_name(f,s,name,at+12,z))return false;at+=12+(int64_t)z;
    }s->size=n;return s->count && at==n;
}
static bool am_line(Abstractformat *f,uint64_t *at,char *line,size_t cap,xx_pd_struct *pd) {
    size_t k=0;uint8_t c;for(;;) {if(am_stop(pd)||k+1>=cap||!pm_read(f,(int64_t)(*at)++,&c,1))return false;
        if(c=='\r') {if(!pm_read(f,(int64_t)(*at)++,&c,1)||c!='\n')return false;line[k]=0;return true;}if(c<32 && c!='\t')return false;line[k++]=(char)c;}
}
static bool am_decimal(const char *p,uint64_t *value) {
    uint64_t z=0;unsigned digits=0;while(*p==' ')++p;while(*p>='0'&&*p<='9') {if(z>(UINT64_MAX-9)/10)return false;z=z*10+(unsigned)(*p++-'0');++digits;}while(*p==' ')++p;if(!digits||*p)return false;*value=z;return true;
}
static bool am_audials(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    char line[8192];uint64_t at=0,count,i,n=(uint64_t)pm_available(f),end=0;
    if(!am_line(f,&at,line,sizeof(line),pd)||xx_rt_strncmp(line,"FilesNumber=",12)||!am_decimal(line+12,&count)||!count||count>65536)return false;
    for(i=0;i<count;++i) {char *tab,*second;uint64_t offset,size;size_t len;
        if(!am_line(f,&at,line,sizeof(line),pd)||!(tab=xx_rt_strchr(line,'\t')))return false;*tab++=0;second=xx_rt_strchr(tab,'\t');if(!second)return false;*second++=0;
        len=xx_rt_strlen(line);while(len&&line[len-1]==' ')line[--len]=0;if(!am_decimal(tab,&offset)||!am_decimal(second,&size)||!am_span(offset,size,n)||!am_name(f,s,line,0,0))return false;
        s->items[s->count-1].offset=(int64_t)offset;s->items[s->count-1].size=s->items[s->count-1].packed_size=(int64_t)size;
    }
    for(i=0;i<count;++i) {pm_member *m=&s->items[i];uint64_t offset=(uint64_t)m->offset,size=(uint64_t)m->size;if(!am_span(at+offset,size,n))return false;m->offset=f->base_address+(int64_t)(at+offset);if(at+offset+size>end)end=at+offset+size;}
    s->size=(int64_t)n;return end==n;
}
static bool am_zlib_read(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    uint8_t *packed=NULL,*plain=NULL;size_t written=(size_t)m->size,at=0;bool ok=false;
    if(m->packed_size<6||(uint64_t)m->packed_size>AM_CAP||(uint64_t)m->size>AM_CAP||!am_decode_budget(f,m)||am_stop(pd))return false;
    packed=xx_mem_alloc((size_t)m->packed_size);plain=xx_mem_alloc(m->size?(size_t)m->size:1);if(!packed||!plain||!pm_read(f,m->offset-f->base_address,packed,(size_t)m->packed_size))goto done;
    if((packed[0]&15)!=8 || (packed[0]>>4)>7 || packed[1]&32 || (((unsigned)packed[0]<<8)|packed[1])%31 ||
       !am_inflate(packed+2,(size_t)m->packed_size-6,plain,written,pd) || !xx_zlib_stream_trailer_matches(packed,(size_t)m->packed_size,plain,written))goto done;
    while(out&&at<written) {size_t z=written-at;ssize_t got;if(z>65536)z=65536;if(am_stop(pd))goto done;got=xx_io_write(out,plain+at,z);if(got<=0||(size_t)got>z)goto done;at+=(size_t)got;}ok=!am_stop(pd);
done:xx_mem_free(packed);xx_mem_free(plain);return ok;
}
static bool am_lyme_table(Abstractformat *f,pm_stream *s,int64_t at,uint32_t count,xx_pd_struct *pd) {
    uint8_t h[12];uint32_t i;
    if(!count||count>65536)return false;
    for(i=0;i<count;++i) {char name[4097];uint32_t len,offset,plain,packed;
        at-=4;if(am_stop(pd)||!pm_read(f,at,h,4))return false;len=am_u32(h);if(!len||len>4096)return false;at-=len;
        if(!pm_read(f,at,name,len)||xx_rt_memchr(name,0,len))return false;name[len]=0;at-=12;if(!pm_read(f,at,h,12))return false;
        offset=am_u32(h);plain=am_u32(h+4);packed=am_u32(h+8);if(!packed||plain>AM_CAP||packed>AM_CAP||(uint64_t)offset+packed>(uint64_t)at||!am_name(f,s,name,offset,packed))return false;
        s->items[s->count-1].size=plain;s->items[s->count-1].compression_method=8;s->items[s->count-1].read_all=am_zlib_read;
    }s->size=pm_available(f);return true;
}
static bool am_lyme(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[18];int64_t n=pm_available(f),at=n-18;uint32_t count;
    if(!pm_read(f,at,h,18)||xx_rt_memcmp(h+4,"1.10!LYME_SFX!",14))return false;
    count=am_u32(h);
    /* Optional application label preceding the count: zero, string, length,
     * then label kind (1 or 3). It is not another file-table record. */
    if(at>=12 && pm_read(f,at-8,h,8) && (am_u32(h+4)==1 || am_u32(h+4)==3)) {
        uint32_t len=am_u32(h);int64_t begin=at-8-(int64_t)len-4;
        if(len<=4096 && begin>=0 && pm_read(f,begin,h,4) && !am_u32(h))at=begin;
    }
    return am_lyme_table(f,s,at,count,pd);
}
static int64_t am_starkit_offset(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[4096];int64_t n;size_t size,i;xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;n=pm_available(&f);
    size=n<4096?(size_t)n:4096;if(n<8||!pm_read(&f,0,h,size)||xx_rt_memcmp(h,"#!/bin/sh",9))return -1;
    for(i=9;i+8<=size;++i)if(!xx_rt_memcmp(h+i,"JL\x1a\0",4)&&xx_data_get_u32(h+i+4,4,0,true)==(uint64_t)n-i)return base+(int64_t)i;
    return -1;
}
static bool am_psa_header(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[84];int64_t n;xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;n=pm_available(&f);
    return n>=596 && pm_read(&f,0,h,84) && !xx_rt_memcmp(h,"                beer!",21) && h[21]<=3 && am_u32(h+30) && (uint64_t)am_u32(h+30)*512+84==(uint64_t)n;
}
static bool am_digits(const uint8_t *p,unsigned count,uint64_t *out) {
    unsigned i;uint64_t n=0;for(i=0;i<count;++i){if(p[i]<'0'||p[i]>'9')return false;n=n*10+p[i]-'0';}*out=n;return true;
}
static bool am_webexe_header(Abstractformat *f,uint64_t *begin,uint64_t *end) {
    uint8_t h[27],probe[1024];uint64_t physical;size_t i,bytes;int64_t at=am_overlay(f),n=pm_available(f);
    if(at<0||!pm_read(f,at,h,27)||xx_rt_memcmp(h,"*M2E*",5)||h[5]<'1'||h[5]>'9'||h[6]!='A'||
       !am_digits(h+7,10,end)||!am_digits(h+17,10,&physical)||physical!=(uint64_t)n||*end>(uint64_t)n||*end<=(uint64_t)at+27)return false;
    bytes=(size_t)(*end-(uint64_t)at-27);if(bytes>sizeof(probe))bytes=sizeof(probe);
    if(!pm_read(f,at+27,probe,bytes))return false;
    for(i=0;i+4<=bytes;++i)if(!xx_rt_memcmp(probe+i,"PK\x03\x04",4)){*begin=(uint64_t)at+27+i;return true;}
    return false;
}
static bool am_zip_read(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    uint8_t *packed=NULL,*plain=NULL;size_t at=0,z=(size_t)m->size;bool ok=false;
    if((uint64_t)m->packed_size>AM_CAP||(uint64_t)m->size>AM_CAP||!am_decode_budget(f,m)||am_stop(pd))return false;
    packed=xx_mem_alloc(m->packed_size?(size_t)m->packed_size:1);plain=xx_mem_alloc(z?z:1);
    if(!packed||!plain||!pm_read(f,m->offset-f->base_address,packed,(size_t)m->packed_size))goto done;
    if(m->compression_method==8){if(!am_inflate(packed,(size_t)m->packed_size,plain,z,pd))goto done;}
    else {if(m->packed_size!=m->size)goto done;if(z)xx_rt_memcpy(plain,packed,z);}
    if(xx_crc32_calc(0,plain,z)!=((am_read_context *)m->context)->crc)goto done;
    while(out&&at<z){size_t size=z-at;ssize_t got;if(size>65536)size=65536;if(am_stop(pd))goto done;got=xx_io_write(out,plain+at,size);if(got<=0||(size_t)got>size)goto done;at+=(size_t)got;}ok=!am_stop(pd);
done:xx_mem_free(packed);xx_mem_free(plain);return ok;
}
/* WebExe stores successive ZIP local records before an opaque vendor index.
 * A conventional ZIP opener often finds only the final tiny ZIP. Consume the
 * complete declared record region, verifying every local payload's CRC. */
static bool am_webexe(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t at,end;uint8_t h[46];if(!am_webexe_header(f,&at,&end))return false;
    while(at<end){uint32_t sig;uint64_t next;if(am_stop(pd)||!pm_read(f,(int64_t)at,h,4))return false;sig=am_u32(h);
        if(sig==UINT32_C(0x04034b50)){char name[4097];uint16_t flags,method,len,extra;uint32_t packed,plain;am_read_context *context;
            if(!am_span(at,30,end)||!pm_read(f,(int64_t)at,h,30))return false;
            flags=xx_data_get_u16(h+6,2,0,false);method=xx_data_get_u16(h+8,2,0,false);packed=am_u32(h+18);plain=am_u32(h+22);len=xx_data_get_u16(h+26,2,0,false);extra=xx_data_get_u16(h+28,2,0,false);
            if(flags&~UINT16_C(0x0806)|| (method!=0&&method!=8)||!len||len>4096||packed>AM_CAP||plain>AM_CAP||!am_span(at+30,(uint64_t)len+extra+packed,end)||!pm_read(f,(int64_t)at+30,name,len)||xx_rt_memchr(name,0,len))return false;
            name[len]=0;next=at+30+len+extra;
            /* WebExe repeats a placeholder leaf ending in a dot. Give each
             * ZIP payload its own portable numbered leaf; the opaque vendor
             * index does not supply recovered document names here. */
            if(name[len-1]=='.'||name[len-1]==' ')name[len-1]='_';
            if(!am_name(f,s,name,(int64_t)next,packed))return false;
            xx_str_free(s->items[s->count-1].display_name);s->items[s->count-1].display_name=NULL;
            if(!am_budget(f,s,sizeof(*context)))return false;context=xx_mem_calloc(1,sizeof(*context));if(!context)return false;context->crc=am_u32(h+14);
            s->items[s->count-1].context=context;s->items[s->count-1].free_context=xx_mem_free;s->items[s->count-1].size=plain;s->items[s->count-1].compression_method=method;s->items[s->count-1].read_all=am_zip_read;at=next+packed;
        }else if(sig==UINT32_C(0x02014b50)){
            if(!am_span(at,46,end)||!pm_read(f,(int64_t)at,h,46))return false;next=46U+xx_data_get_u16(h+28,2,0,false)+xx_data_get_u16(h+30,2,0,false)+xx_data_get_u16(h+32,2,0,false);if(!am_span(at,next,end))return false;at+=next;
        }else if(sig==UINT32_C(0x06054b50)){
            if(!am_span(at,22,end)||!pm_read(f,(int64_t)at,h,22))return false;next=22U+xx_data_get_u16(h+20,2,0,false);if(!am_span(at,next,end))return false;at+=next;
        }else return false;
    }s->size=pm_available(f);return s->count&&at==end;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    bool ok=false;size_t i;uint64_t resident;
    if(am_stop(pd)||!am_budget(f,s,8*sizeof(pm_member)))return false;
    switch(f->file_type) {
    case XX_FILE_TYPE_DESKSOFT:ok=am_desksoft(f,s,pd);break;
    case XX_FILE_TYPE_VISUALWARE:ok=am_visualware(f,s,pd);break;
    case XX_FILE_TYPE_METAPRODUCTS:ok=am_meta(f,s,pd);break;
    case XX_FILE_TYPE_EVD:ok=am_evd(f,s,pd);break;
    case XX_FILE_TYPE_AUDIALS:ok=am_audials(f,s,pd);break;
    case XX_FILE_TYPE_LYME_SFX:ok=am_lyme(f,s,pd);break;
    case XX_FILE_TYPE_WEBEXE:ok=am_webexe(f,s,pd);break;
    default:return false;
    }
    if(!ok)return false;
    for(i=0;i<s->count;++i)if(s->items[i].read_all&&!s->items[i].context){am_read_context *c;if(!am_budget(f,s,sizeof(*c)))return false;c=xx_mem_calloc(1,sizeof(*c));if(!c)return false;s->items[i].context=c;s->items[i].free_context=xx_mem_free;}
    resident=am_resident(s);if(!am_budget(f,s,0))return false;
    for(i=0;i<s->count;++i)if(s->items[i].read_all)((am_read_context *)s->items[i].context)->resident=resident;
    return true;
}
typedef struct am_carrier {int64_t size;bool (*info)(Abstractformat *,xx_pd_struct *);void (*destroy)(Abstractformat *);} am_carrier;
static void am_carrier_destroy(Abstractformat *f) {
    am_carrier *c=f?f->priv:NULL;void (*destroy)(Abstractformat *);if(!c)return;
    destroy=c->destroy;f->priv=NULL;xx_mem_free(c);if(destroy)destroy(f);else xx_format_cleanup_extra_parameters(f);
}
static bool am_carrier_info(Abstractformat *f,xx_pd_struct *pd) {
    am_carrier *c=f->priv;if(!c||!c->info(f,pd))return false;f->format_size=c->size;return true;
}
static int64_t am_carrier_size(Abstractformat *f,xx_pd_struct *pd) {
    if(!f||!f->priv||(!f->base_info_handled&&!am_carrier_info(f,pd)))return -1;
    f->format_size=((am_carrier *)f->priv)->size;return f->format_size;
}
static uint64_t am_carrier_records(Abstractformat *f,xx_pd_struct *pd) {
    return am_carrier_size(f,pd)>=0?f->number_of_archive_records:0;
}
static bool am_set_carrier(Abstractformat *f,xx_file_type_t type,int64_t base) {
    am_carrier *c;if(!f)return false;c=xx_mem_alloc(sizeof(*c));if(!c)return false;
    c->size=xx_io_total_size(f->device)-base;c->info=f->handle_base_info;c->destroy=f->destroy;f->priv=c;f->file_type=type;f->handle_base_info=am_carrier_info;f->get_format_size=am_carrier_size;f->get_number_of_archive_records=am_carrier_records;f->destroy=am_carrier_destroy;return true;
}
Abstractformat *xx_arc9_misc_create(xx_io_device *d,int64_t base,xx_file_type_t type) {
    Abstractformat *f;const char *ext="exe";
    if(type==XX_FILE_TYPE_BINSH_STARKIT) {int64_t at=am_starkit_offset(d,base);xx_starkit *r;if(at<0&&d&&xx_io_total_size(d)>base)return NULL;r=xx_starkit_create(d,at<0?base:at);if(r&&!am_set_carrier(&r->format,type,base)){xx_starkit_free(r);r=NULL;}return (Abstractformat *)r;}
    if(type==XX_FILE_TYPE_PSA_DISK) {bool valid=am_psa_header(d,base);xx_fat *r;if(!valid&&d&&xx_io_total_size(d)>base)return NULL;r=xx_fat_create(d,valid?base+84:base);if(r&&!am_set_carrier(&r->format,type,base)){xx_fat_free(r);r=NULL;}return (Abstractformat *)r;}
    if(type==XX_FILE_TYPE_EVD)ext="evd";else if(type==XX_FILE_TYPE_AUDIALS)ext="cmp";else if(type==XX_FILE_TYPE_LYME_SFX)ext="scr";
    f=xx_mem_alloc(sizeof(*f));if(f){xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,type,ext);}return f;
}
void xx_arc9_misc_free(Abstractformat *f) {if(f){if(f->file_type==XX_FILE_TYPE_BINSH_STARKIT||f->file_type==XX_FILE_TYPE_PSA_DISK)am_carrier_destroy(f);else xx_format_cleanup_extra_parameters(f);xx_mem_free(f);}}
bool xx_arc9_misc_probe(xx_io_device *d,int64_t base,xx_file_type_t type) {Abstractformat *f=xx_arc9_misc_create(d,base,type);bool ok=f&&xx_format_is_valid(f,NULL);xx_arc9_misc_free(f);return ok;}
xx_file_type_t xx_arc9_misc_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[40];int64_t at,n;xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;n=pm_available(&f);
    if(n<24||!pm_read(&f,0,h,24))return type;
    if(!xx_rt_memcmp(h,"FilesNumber=",12))return XX_FILE_TYPE_AUDIALS;
    if(!xx_rt_memcmp(h,"                beer!",21) && am_psa_header(d,base))return XX_FILE_TYPE_PSA_DISK;
    if(!xx_rt_memcmp(h,"#!/bin/sh",9)&&am_starkit_offset(d,base)>=0)return XX_FILE_TYPE_BINSH_STARKIT;
    if(!am_u32(h)&&!am_u32(h+4)&&am_u32(h+8)==12&&(am_u32(h+12)==1||am_u32(h+12)==2)&&am_u32(h+16)&&am_u32(h+20))return XX_FILE_TYPE_EVD;
    if(pm_read(&f,n-14,h,14)&&!xx_rt_memcmp(h,"1.10!LYME_SFX!",14))return XX_FILE_TYPE_LYME_SFX;
    at=am_overlay(&f);if(at<0||!pm_read(&f,at,h,19))return type;
    if(!xx_rt_memcmp(h,"<Setup Data Header>",19))return XX_FILE_TYPE_DESKSOFT;
    if(!xx_rt_memcmp(h,"JKMNPQSTVWYZ\\]_B",16))return XX_FILE_TYPE_VISUALWARE;
    if(am_u32(h)==UINT32_C(0xabba1973))return XX_FILE_TYPE_METAPRODUCTS;
    if(!xx_rt_memcmp(h,"*M2E*",5)){uint64_t begin,end;if(am_webexe_header(&f,&begin,&end))return XX_FILE_TYPE_WEBEXE;}
    return type;
}
