/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/antangelo/xdvdfs/main/xdvdfs-core/src/layout/volume.rs
 * XDVDFS descriptor at sector32, flat root directory with up to1024 stored files. Validates both descriptor signatures, child offsets/cycles, directory-record extents and disjoint file data. Exports files with numeric names; nested directories, full-disc security sectors and ISO hybrids unsupported.
 */
#include "xxfclib/formats/xbox_xdvdfs/xx_xbox_xdvdfs.h"
#include "../xx_payload_members.h"

static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i; if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[2048],e[14],name[255]; uint64_t table,total=67584,end=(uint64_t)pm_available(f); uint32_t extent_n[1024],seen[1024],pending[2048],size,count=0,queue=1,i; char label[40];
    if(!pm_read(f,65536,h,sizeof(h)) || xx_rt_memcmp(h,"MICROSOFT*XBOX*MEDIA",20) || xx_rt_memcmp(h+2028,"MICROSOFT*XBOX*MEDIA",20)) return false;
    table=(uint64_t)pm_le32(h+20)*2048; size=pm_le32(h+24); if(table<67584 || size<14 || size>16777216 || !span(table,size,end)) return false; total=table+size; pending[0]=0;
    while(queue) { uint32_t node=pending[--queue],n,j; uint64_t at,bytes; uint16_t left,right;
      if(stop(pd) || count>=1024 || !span(node,14,size) || !pm_read(f,(int64_t)(table+node),e,14)) return false;
      for(i=0;i<count;++i) if(node==seen[i]) return false; seen[count]=node;
      n=e[13]; bytes=(14U+(uint64_t)n+3)&~3ULL; if(!n || !span(node,bytes,size) || (node&3) || (e[12]&~0x27U) || !pm_read(f,(int64_t)(table+node+14),name,n)) return false;
      for(j=0;j<n;++j) if(name[j]<32 || name[j]=='/' || name[j]=='\\' || !name[j]) return false;
      for(i=0;i<count;++i) if(overlap(node,bytes,seen[i],extent_n[i])) return false; extent_n[count]=(uint32_t)bytes;
      at=(uint64_t)pm_le32(e+4)*2048; n=pm_le32(e+8); if(at<67584 || !span(at,n,end) || overlap(at,n,table,size)) return false;
      xx_rt_snprintf(label,sizeof(label),"file-%u.bin",count); if(!emit(f,s,label,at,n,end)) return false; if(at+n>total) total=at+n;
      left=pm_le16(e); right=pm_le16(e+2); if(queue+2>2048) return false; if(left && left!=65535) pending[queue++]=(uint32_t)left*4; if(right && right!=65535) pending[queue++]=(uint32_t)right*4; ++count;
    }
    if(!count) return false; s->size=(int64_t)total; return true;

}

void xx_xbox_xdvdfs_init(xx_xbox_xdvdfs *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_XBOX_XDVDFS,"iso"); } }
xx_xbox_xdvdfs *xx_xbox_xdvdfs_create(xx_io_device *d,int64_t b) { xx_xbox_xdvdfs *r=(xx_xbox_xdvdfs *)xx_mem_alloc(sizeof(*r)); if(r) xx_xbox_xdvdfs_init(r,d,b); return r; }
void xx_xbox_xdvdfs_destroy(xx_xbox_xdvdfs *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_xbox_xdvdfs_free(xx_xbox_xdvdfs *r) { if(r) { xx_xbox_xdvdfs_destroy(r); xx_mem_free(r); } }
bool xx_xbox_xdvdfs_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_xbox_xdvdfs_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
