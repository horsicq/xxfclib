/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://developer.gimp.org/core/standards/xcf/
 * XCF0-3 8-bit RGB/grayscale/indexed layers/channels, complete typed property framing, hierarchy/level/tile references and stored or validated RLE tile planes. Original encoded tiles and structures exported; zlib/fractal compression, newer precision versions and rendering unsupported. Layer masks and unallocated empty tile levels supported; bounded editing-state property payloads retained opaque.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/gimp_xcf/xx_gimp_xcf.h"
#include "../wavefront_obj/xx_eleventh_media.h"
#include "xxfclib/data/xx_data.h"
static bool eg_quick(Abstractformat *f,uint64_t n) {uint8_t b[14];return n>=50&&pm_read(f,0,b,14)&&eg_tag(b,"gimp xcf ",9)&&!b[13];}
typedef struct xc_region {uint64_t p,z;char label[48];} xc_region;
typedef struct xc_state {Abstractformat *f;pm_stream *s;const uint8_t *b;uint64_t n,budget;xx_pd_struct *pd;xc_region *r;uint32_t count,compression,base,colors;} xc_state;
static bool xc_add(xc_state *x,uint64_t p,uint64_t z,const char *label) {
 uint32_t i;if(!z||x->count==4096||!eg_span(p,z,x->n))return false;
 for(i=0;i<x->count;++i)if(p<x->r[i].p+x->r[i].z&&x->r[i].p<p+z)return false;
 x->r[x->count].p=p;x->r[x->count].z=z;xx_rt_snprintf(x->r[x->count].label,48,"%s",label);++x->count;return true;
}
static bool xc_string(xc_state *x,uint64_t *p,uint64_t end) {
 uint32_t z;if(!eg_span(*p,4,end))return false;z=xx_data_get_u32(x->b+*p, 4, 0, true);*p+=4;
 if(!z||z>65536||!eg_span(*p,z,end)||x->b[*p+z-1]||!eg_utf(x->b+*p,z-1,false,x->pd)) {return false; } *p+=z;return true;
}
static bool xc_props(xc_state *x,uint64_t *p,bool image) {
 uint32_t count=0;const uint8_t *b=x->b;uint64_t seen=0;
 for(;;){uint32_t type,z,v;uint64_t end;if(eg_stop(x->pd)||++count>1024||!eg_span(*p,8,x->n))return false;type=xx_data_get_u32(b+*p, 4, 0, true);z=xx_data_get_u32(b+*p+4, 4, 0, true);*p+=8;if(!type)return z==0;
  if(type>63||z>1048576||!eg_span(*p,z,x->n)||(seen&((uint64_t)1<<type))) {return false; } seen|=(uint64_t)1<<type;end=*p+z;v=z>=4?xx_data_get_u32(b+*p, 4, 0, true):0;
  if(type==1){if(!image||z<4||v>256||z!=4+v*3)return false;x->colors=v;}
  else if(type==2||type==3||type==4){if(z)return false;}
  else if(type==6){if(z!=4||v>255)return false;}
  else if(type==7){if(z!=4||v>31)return false;}
  else if(type>=8&&type<=14){if(z!=4||v>1)return false;}
  else if(type==15){if(z!=8)return false;}
  else if(type==16){if(z!=3)return false;}
  else if(type==17){if(!image||z!=1||b[*p]>1)return false;x->compression=b[*p];}
  else if(type==18){uint32_t i;if(z%5)return false;for(i=0;i<z;i+=5)if(b[*p+i+4]<1||b[*p+i+4]>2)return false;}
  else if(type==19){if(z!=8||!eg_f32(v)||!eg_f32(xx_data_get_u32(b+*p+4, 4, 0, true))||(v&0x80000000U)||!(v&0x7fffffffU)||(xx_data_get_u32(b+*p+4, 4, 0, true)&0x80000000U)||!(xx_data_get_u32(b+*p+4, 4, 0, true)&0x7fffffffU))return false;}
  else if(type==20||type==22){if(z!=4)return false;}
  else if(type==21){uint64_t q=*p;while(q<end){uint32_t bytes;if(!xc_string(x,&q,end)||!eg_span(q,8,end))return false;bytes=xx_data_get_u32(b+q+4, 4, 0, true);q+=8;if(!eg_span(q,bytes,end))return false;q+=bytes;}}
  else if(type==27){if(z%8)return false;}
  /* Other bounded editing-state payloads retain their original encoding. */
  *p=end;
 }
}
static bool xc_tile(xc_state *x,uint64_t *p,uint32_t pixels,uint32_t bpp) {
 uint32_t channel;uint64_t start=*p;if(!x->compression){uint64_t z=(uint64_t)pixels*bpp;if(!eg_span(*p,z,x->n))return false;*p+=z;}
 else for(channel=0;channel<bpp;++channel){uint32_t out=0;
  while(out<pixels){uint8_t code;uint32_t count;uint64_t z;if(eg_stop(x->pd)||!eg_span(*p,1,x->n))return false;code=x->b[(*p)++];
   if(code==127||code==128){if(!eg_span(*p,2,x->n))return false;count=xx_data_get_u16(x->b+*p, 2, 0, true);*p+=2;}else count=code<127?code+1U:256U-code;
   z=code<128?1:count;if(!count||count>pixels-out||!eg_span(*p,z,x->n))return false;*p+=z;out+=count;
  }}
 return xc_add(x,start,*p-start,"tile.xcf");
}
static bool xc_hierarchy(xc_state *x,uint32_t at,uint32_t w,uint32_t h,uint32_t bpp) {
 uint64_t p=at;uint32_t levels[16],count=0,i,tw=w,th=h;const uint8_t *b=x->b;
 if(!eg_span(p,16,x->n)||xx_data_get_u32(b+p, 4, 0, true)!=w||xx_data_get_u32(b+p+4, 4, 0, true)!=h||xx_data_get_u32(b+p+8, 4, 0, true)!=bpp) {return false; } p+=12;
 for(;;){uint32_t v;if(!eg_span(p,4,x->n))return false;v=xx_data_get_u32(b+p, 4, 0, true);p+=4;if(!v)break;if(count==16)return false;levels[count++]=v;}
 if(!count||!xc_add(x,at,p-at,"hierarchy.xcf"))return false;
 for(i=0;i<count;++i){uint64_t start=levels[i];uint32_t tiles=((tw+63)/64)*((th+63)/64),j;uint32_t ptr[4096];
  p=start;if(!tiles||tiles>4096||!eg_span(p,12,x->n)||xx_data_get_u32(b+p, 4, 0, true)!=tw||xx_data_get_u32(b+p+4, 4, 0, true)!=th)return false;p+=8;
  if(i||xx_data_get_u32(b+p, 4, 0, true)==0){if(xx_data_get_u32(b+p, 4, 0, true))return false;p+=4;}
  else{if(!eg_span(p,(uint64_t)(tiles+1)*4,x->n))return false;
   for(j=0;j<tiles;++j){ptr[j]=xx_data_get_u32(b+p+j*4, 4, 0, true);if(!ptr[j])return false;}p+=(uint64_t)tiles*4;if(xx_data_get_u32(b+p, 4, 0, true))return false;p+=4;
   for(j=0;j<tiles;++j){uint32_t xx=j%((tw+63)/64),yy=j/((tw+63)/64),cw=tw-xx*64,ch=th-yy*64;uint64_t data=ptr[j];if(cw>64)cw=64;if(ch>64)ch=64;if(j&&ptr[j]<=ptr[j-1])return false;
    if(!xc_tile(x,&data,cw*ch,bpp)||(j+1<tiles&&data!=ptr[j+1]))return false;}
  }
  if(!xc_add(x,start,p-start,i?"dummy-level.xcf":"tile-directory.xcf")) {return false; } tw=tw>1?tw/2:1;th=th>1?th/2:1;
 }
 return true;
}
static bool xc_drawable(xc_state *x,uint32_t at,bool layer,uint32_t canvasw,uint32_t canvash) {
 const uint8_t *b=x->b;uint64_t p=at;uint32_t w,h,type,bpp,hptr,mask=0;
 if(!eg_span(p,layer?16:12,x->n)) {return false; } w=xx_data_get_u32(b+p, 4, 0, true);h=xx_data_get_u32(b+p+4, 4, 0, true);p+=8;
 if(!w||!h||w>16384||h>16384||(uint64_t)w*h>16777216)return false;
 if(layer){type=xx_data_get_u32(b+p, 4, 0, true);p+=4;if(type>5||type/2!=x->base)return false;bpp=type<2?3+(type&1):1+(type&1);}
 else{bpp=1;if(w!=canvasw||h!=canvash)return false;}
 if((uint64_t)w*h*bpp>67108864||x->budget>67108864-(uint64_t)w*h*bpp) {return false; } x->budget+=(uint64_t)w*h*bpp;
 if(!xc_string(x,&p,x->n)||!xc_props(x,&p,false)||!eg_span(p,layer?8:4,x->n)) {return false; } hptr=xx_data_get_u32(b+p, 4, 0, true);p+=4;if(layer){mask=xx_data_get_u32(b+p, 4, 0, true);p+=4;}
 if(!xc_add(x,at,p-at,layer?"layer.xcf":"channel.xcf"))return false;
 if(hptr&&!xc_hierarchy(x,hptr,w,h,bpp))return false;
 if(mask&&!xc_drawable(x,mask,false,w,h)) {return false; } return true;
}
static bool eg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 xc_state x;uint32_t w,h,layers[1024],channels[1024],lc=0,cc=0,i;uint64_t p=26,covered=0;bool result=false;xx_mem_zero(&x,sizeof(x));x.f=f;x.s=s;x.b=b;x.n=n;x.pd=pd;
 if(!eg_tag(b,"gimp xcf ",9)||b[13]||(!eg_tag(b+9,"file",4)&&(!eg_tag(b+9,"v00",3)||b[12]<'1'||b[12]>'3'))||(w=xx_data_get_u32(b+14, 4, 0, true))<1||w>16384||(h=xx_data_get_u32(b+18, 4, 0, true))<1||h>16384||(x.base=xx_data_get_u32(b+22, 4, 0, true))>2)return false;
 x.r=(xc_region *)xx_mem_alloc(4096*sizeof(*x.r));if(!x.r)return false;
 if(!xc_props(&x,&p,true)||(x.base==2&&!x.colors))goto done;
 for(;;){uint32_t v;if(!eg_span(p,4,n))goto done;v=xx_data_get_u32(b+p, 4, 0, true);p+=4;if(!v)break;if(lc==1024)goto done;layers[lc++]=v;}
 for(;;){uint32_t v;if(!eg_span(p,4,n))goto done;v=xx_data_get_u32(b+p, 4, 0, true);p+=4;if(!v)break;if(cc==1024)goto done;channels[cc++]=v;}
 if(!lc||!xc_add(&x,0,p,"descriptor.xcf"))goto done;
 for(i=0;i<lc;++i)if(eg_stop(pd)||!xc_drawable(&x,layers[i],true,w,h))goto done;
 for(i=0;i<cc;++i)if(eg_stop(pd)||!xc_drawable(&x,channels[i],false,w,h))goto done;
 for(i=0;i<x.count;++i){uint32_t j,best=i;xc_region swap;
  for(j=i+1;j<x.count;++j) {if(x.r[j].p<x.r[best].p)best=j; } swap=x.r[i];x.r[i]=x.r[best];x.r[best]=swap;
  if(x.r[i].p!=covered||!eg_emit(f,s,x.r[i].label,x.r[i].p,x.r[i].z,n)) {goto done; } covered+=x.r[i].z;}
 if(covered!=n) {goto done; } s->size=(int64_t)n;result=true;
done:xx_mem_free(x.r);return result;
}

void xx_gimp_xcf_init(xx_gimp_xcf *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_GIMP_XCF,"xcf");}}
xx_gimp_xcf *xx_gimp_xcf_create(xx_io_device *d,int64_t at) {xx_gimp_xcf *r=(xx_gimp_xcf *)xx_mem_alloc(sizeof(*r));if(r)xx_gimp_xcf_init(r,d,at);return r;}
void xx_gimp_xcf_destroy(xx_gimp_xcf *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_gimp_xcf_free(xx_gimp_xcf *r) {if(r){xx_gimp_xcf_destroy(r);xx_mem_free(r);}}
bool xx_gimp_xcf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_gimp_xcf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
