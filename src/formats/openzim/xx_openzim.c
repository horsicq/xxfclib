/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://wiki.openzim.org/wiki/ZIM_file_format */
#include "xxfclib/formats/openzim/xx_openzim.h"
#include "../microsoft_msf/xx_tenth_containers.h"

#include "xxfclib/formats/xz/xx_xz.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
static bool zi_gap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) {return a+n<=b || b+m<=a;}
typedef struct zi_dir {uint64_t at,n;uint32_t cluster,blob,redirect;bool link;char name[80];} zi_dir;
static bool zi_decode(nh_blob *b,uint64_t at,uint64_t end,uint8_t **out,uint64_t *size) {
    unsigned kind=b->p[(size_t)at]&15;uint64_t n=end-at-1;xx_io_device *input=NULL,*output=NULL;xx_xz *xz=NULL;bool ok=false;*out=NULL;*size=0;
    if(b->p[(size_t)at]&0xe0) return false;
    if(kind==0 || kind==1) {if(n>1048576) return false;*out=(uint8_t *)xx_mem_alloc((size_t)(n ? n:1));if(!*out) return false;xx_rt_memcpy(*out,b->p+(size_t)at+1,(size_t)n);*size=n;return true;}
    if(kind==4) {input=xx_io_mem_open_ro(b->p+(size_t)at+1,(size_t)n);if(!input) goto done;xz=xx_xz_create(input,0);if(!xz || !xx_xz_handle_base_info(&xz->format,b->pd) || xz->format.format_size!=(int64_t)n || xz->uncompressed_size>1048576) goto done;*size=xz->uncompressed_size;*out=(uint8_t *)xx_mem_alloc((size_t)(*size ? *size:1));if(!*out) goto done;output=xx_io_mem_open(*out,(size_t)*size);if(!output || !xx_xz_unpack_to_device(xz,output,b->pd)) goto done;ok=true;}
    else if(kind==5) {uint64_t p=at+5;unsigned flag,single,width,dict;uint8_t d;size_t made=0;if(n<6 || xx_data_get_u32(b->p+(size_t)at+1, 4, 0, false)!=0xfd2fb528U) goto done;d=b->p[(size_t)p++];flag=d>>6;single=(d>>5)&1;dict=d&3;if(d&0x18) goto done;if(!single) ++p;dict=dict==3 ? 4:dict; /* dictionary lengths 0,1,2,4 */
        if(!eh_span(p,dict,end)) { goto done; } for(unsigned i=0;i<dict;++i) if(b->p[(size_t)(p+i)]) goto done;p+=dict;width=flag ? 1U<<flag:single;if(!width || !eh_span(p,width,end)) goto done;*size=0;for(unsigned i=0;i<width;++i) *size|=(uint64_t)b->p[(size_t)(p+i)]<<(8*i);if(flag==1) *size+=256;if(*size>1048576) goto done;*out=(uint8_t *)xx_mem_alloc((size_t)(*size ? *size:1));if(!*out || fd_stop(b->pd) || !xx_zstd_decompress_memory(b->p+(size_t)at+1,(size_t)n,*out,(size_t)*size,&made) || made!=*size || fd_stop(b->pd)) goto done;ok=true;
    }
done:if(xz) xx_xz_free(xz);if(input) xx_io_close(input);if(output) xx_io_close(output);if(!ok && *out) {xx_mem_free(*out);*out=NULL;}return ok;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b;zi_dir *dirs=NULL;uint64_t *clusters=NULL;uint8_t *decoded=NULL,*owned=NULL;uint64_t at,j,k,entries,nc,urls,titles,cptr,mime,checksum,mimeend,mimes=0,total=0;xx_hash_context hash;uint8_t digest[16];bool ok=false;
    if(!nh_load(f,&b,pd)) { return false; } NH_NEED(nh_span(&b,0,80) && !xx_rt_memcmp(b.p,"ZIM\x04",4) && (xx_data_get_u16(b.p+4, 2, 0, false)==5 || xx_data_get_u16(b.p+4, 2, 0, false)==6));entries=xx_data_get_u32(b.p+24, 4, 0, false);nc=xx_data_get_u32(b.p+28, 4, 0, false);urls=xx_data_get_u64(b.p+32, 8, 0, false);titles=xx_data_get_u64(b.p+40, 8, 0, false);cptr=xx_data_get_u64(b.p+48, 8, 0, false);mime=xx_data_get_u64(b.p+56, 8, 0, false);checksum=xx_data_get_u64(b.p+72, 8, 0, false);
    NH_NEED(entries && entries<=2048 && nc && nc<=1024 && mime==80 && checksum==b.n-16 && nh_span(&b,urls,entries*8) && nh_span(&b,titles,entries*4) && nh_span(&b,cptr,nc*8) && urls>=80 && titles>=80 && cptr>=80 && urls+entries*8<=checksum && titles+entries*4<=checksum && cptr+nc*8<=checksum);
    NH_NEED(xx_data_get_u16(b.p+6, 2, 0, false)<=1 && zi_gap(urls,entries*8,titles,entries*4) && zi_gap(urls,entries*8,cptr,nc*8) && zi_gap(titles,entries*4,cptr,nc*8));
    NH_NEED((xx_data_get_u32(b.p+64, 4, 0, false)==0xffffffffU || xx_data_get_u32(b.p+64, 4, 0, false)<entries) && (xx_data_get_u32(b.p+68, 4, 0, false)==0xffffffffU || xx_data_get_u32(b.p+68, 4, 0, false)<entries));NH_NEED(xx_hash_init(&hash,XX_HASH_MD5));
    for(at=0;at<checksum;) {size_t n=checksum-at>65536 ? 65536:(size_t)(checksum-at);NH_NEED(!fd_stop(pd));xx_hash_update(&hash,b.p+(size_t)at,n);at+=n;}NH_NEED(xx_hash_final(&hash,digest,sizeof(digest)) && !xx_rt_memcmp(digest,b.p+(size_t)checksum,16));
    at=mime;while(at<checksum && b.p[(size_t)at]) {NH_NEED(th_z(&b,&at,checksum,false) && ++mimes<=4094);}NH_NEED(at<checksum && mimes);mimeend=at+1;NH_NEED(urls>=mimeend && titles>=mimeend && cptr>=mimeend);
    dirs=(zi_dir *)xx_mem_alloc((size_t)entries*sizeof(*dirs));clusters=(uint64_t *)xx_mem_alloc((size_t)nc*sizeof(*clusters));NH_NEED(dirs && clusters);xx_mem_zero(dirs,(size_t)entries*sizeof(*dirs));
    for(j=0;j<nc;++j) {clusters[j]=xx_data_get_u64(b.p+(size_t)(cptr+j*8), 8, 0, false);NH_NEED(clusters[j]>=mimeend && clusters[j]<checksum && (!j || clusters[j]>clusters[j-1]));}
    for(j=0;j<entries;++j) {uint64_t start,path,end=checksum;uint16_t type;dirs[j].at=xx_data_get_u64(b.p+(size_t)(urls+j*8), 8, 0, false);at=dirs[j].at;NH_NEED(at>=mimeend && nh_span(&b,at,12) && xx_data_get_u32(b.p+(size_t)at+4, 4, 0, false)==0 && !b.p[(size_t)at+2]);type=xx_data_get_u16(b.p+(size_t)at, 2, 0, false);dirs[j].link=type==0xffff;NH_NEED(dirs[j].link || type<mimes);NH_NEED(xx_data_get_u32(b.p+(size_t)(titles+j*4), 4, 0, false)<entries);
        if(dirs[j].link) {dirs[j].redirect=xx_data_get_u32(b.p+(size_t)at+8, 4, 0, false);NH_NEED(dirs[j].redirect<entries && dirs[j].redirect!=j);at+=12;}else {NH_NEED(nh_span(&b,at,16));dirs[j].cluster=xx_data_get_u32(b.p+(size_t)at+8, 4, 0, false);dirs[j].blob=xx_data_get_u32(b.p+(size_t)at+12, 4, 0, false);NH_NEED(dirs[j].cluster<nc);at+=16;}
        path=at;NH_NEED(th_z(&b,&at,end,false));xx_rt_snprintf(dirs[j].name,sizeof(dirs[j].name),"%c-%.*s",b.p[(size_t)dirs[j].at+3],(int)(at-path-1),b.p+(size_t)path);NH_NEED(th_z(&b,&at,end,true));dirs[j].n=at-dirs[j].at;NH_NEED(zi_gap(dirs[j].at,dirs[j].n,urls,entries*8) && zi_gap(dirs[j].at,dirs[j].n,titles,entries*4) && zi_gap(dirs[j].at,dirs[j].n,cptr,nc*8));
        for(k=0;k<j;++k) {NH_NEED(at<=dirs[k].at || dirs[j].at>=dirs[k].at+dirs[k].n);NH_NEED(xx_data_get_u32(b.p+(size_t)(titles+j*4), 4, 0, false)!=xx_data_get_u32(b.p+(size_t)(titles+k*4), 4, 0, false));}
        start=dirs[j].redirect;if(dirs[j].link) {for(k=0;k<entries;++k) {if(start==j) NH_NEED(false);if(start>=j) break;if(!dirs[start].link) break;start=dirs[start].redirect;}NH_NEED(k<entries);}
    }
    NH_NEED(nh_add(f,s,&b,"zim-header",0,80) && nh_add(f,s,&b,"mime-list",mime,mimeend-mime));
    for(j=0;j<nc;++j) {uint64_t end=checksum,dn,first,blobs,width,prev=0;unsigned flag=b.p[(size_t)clusters[j]];
        if(j+1<nc) { end=clusters[j+1]; } if(urls>clusters[j] && urls<end) end=urls;if(titles>clusters[j] && titles<end) end=titles;if(cptr>clusters[j] && cptr<end) end=cptr;
        for(k=0;k<entries;++k) {NH_NEED(dirs[k].at+dirs[k].n<=clusters[j] || dirs[k].at>=end || dirs[k].at>clusters[j]);if(dirs[k].at>clusters[j] && dirs[k].at<end) end=dirs[k].at;}
        NH_NEED(end>clusters[j]+1 && zi_decode(&b,clusters[j],end,&decoded,&dn));width=flag&16 ? 8:4;NH_NEED(dn>=width);first=width==8 ? xx_data_get_u64(decoded, 8, 0, false):xx_data_get_u32(decoded, 4, 0, false);NH_NEED(first>=width*2 && !(first%width) && first<=dn);blobs=first/width-1;
        for(k=0;k<=blobs;++k) {uint64_t v=width==8 ? xx_data_get_u64(decoded+(size_t)(k*width), 8, 0, false):xx_data_get_u32(decoded+(size_t)(k*width), 4, 0, false);NH_NEED(v>=first && v<=dn && (!k || v>=prev));prev=v;}NH_NEED(prev==dn);
        for(k=0;k<entries;++k) if(!dirs[k].link && dirs[k].cluster==j) {uint64_t lo,hi,n;NH_NEED(dirs[k].blob<blobs);at=(uint64_t)dirs[k].blob*width;lo=width==8 ? xx_data_get_u64(decoded+(size_t)at, 8, 0, false):xx_data_get_u32(decoded+(size_t)at, 4, 0, false);hi=width==8 ? xx_data_get_u64(decoded+(size_t)at+8, 8, 0, false):xx_data_get_u32(decoded+(size_t)at+4, 4, 0, false);n=hi-lo;NH_NEED(n<=67108864-total);total+=n;owned=(uint8_t *)xx_mem_alloc((size_t)(n ? n:1));NH_NEED(owned);xx_rt_memcpy(owned,decoded+(size_t)lo,(size_t)n);NH_NEED(th_mem(f,s,dirs[k].name,&owned,n));}
        xx_mem_free(decoded);decoded=NULL;
    }
    for(j=0;j<entries;++j) if(dirs[j].link) {uint64_t target=j;for(k=0;k<entries && dirs[target].link;++k) target=dirs[target].redirect;NH_NEED(k<entries && nh_add(f,s,&b,"redirect",dirs[j].at,dirs[j].n));}
    NH_NEED(nh_add(f,s,&b,"url-pointers",urls,entries*8) && nh_add(f,s,&b,"title-pointers",titles,entries*4) && nh_add(f,s,&b,"cluster-pointers",cptr,nc*8) && nh_add(f,s,&b,"md5",checksum,16));s->size=(int64_t)b.n;ok=true;
done:if(dirs) xx_mem_free(dirs);if(clusters) xx_mem_free(clusters);if(decoded) xx_mem_free(decoded);if(owned) xx_mem_free(owned);xx_mem_free(b.p);return ok;
}
void xx_openzim_init(xx_openzim *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_OPENZIM,"zim"); } }
xx_openzim *xx_openzim_create(xx_io_device *d,int64_t b) { xx_openzim *r=(xx_openzim *)xx_mem_alloc(sizeof(*r)); if(r) xx_openzim_init(r,d,b); return r; }
void xx_openzim_destroy(xx_openzim *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_openzim_free(xx_openzim *r) { if(r) { xx_openzim_destroy(r); xx_mem_free(r); } }
bool xx_openzim_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_openzim_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
