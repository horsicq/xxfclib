/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/KLayout/klayout/master/src/plugins/streamers/gds2/db_plugin/dbGDS2Reader.cc
 * Calma GDSII Stream: complete typed library/structure/element framing, bounded coordinates and names, finite positive units, geometry and local references. Original layout records exported; no layout execution. OASIS is a distinct unsupported grammar.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/calma_gdsii/xx_calma_gdsii.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t b[6];return n>=6&&pm_read(f,0,b,6)&&xx_data_get_u16(b, 2, 0, true)==6&&b[2]==0&&b[3]==2;}
static bool gs_name(const uint8_t *b,unsigned z,char out[64]) {unsigned i;if(!z||z>64)return false;if(!b[z-1])--z;if(!z||z>=64)return false;for(i=0;i<z;++i)if(b[i]<33||b[i]>126)return false;xx_rt_memcpy(out,b,z);out[z]=0;return true;}
static bool gs_real(const uint8_t *b,bool positive) {double v=0;unsigned i,ex=b[0]&127;bool neg=(b[0]&128)!=0;if(tb_zero(b,8))return !positive;if(!(b[1]&0xf0))return false;for(i=1;i<8;++i)v=v*256+b[i];v/=72057594037927936.0;if(ex>=64){for(i=64;i<ex;++i)v*=16;}else for(i=ex;i<64;++i)v/=16;return v<=3.402823466e38&&(!positive||(!neg&&v>0));}
static bool gs_dates(const uint8_t *b,unsigned z) {unsigned i;if(z!=24)return false;for(i=0;i<12;++i){unsigned v=xx_data_get_u16(b+i*2, 2, 0, true),k=i%6;if(k==0){if(v>9999)return false;}else if(k==1){if(v<1||v>12)return false;}else if(k==2){if(v<1||v>31)return false;}else if(v>(k==3?23U:59U))return false;}return true;}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t p=0,start=0,work=0;unsigned stage=0,element=0,names_count=0,refs_count=0,seen=0;char (*names)[64]=NULL,(*refs)[64]=NULL,name[64];bool result=false;uint32_t xy_count=0;const uint8_t *xy=NULL;
 names=(char (*)[64])xx_mem_alloc(4096*64);refs=(char (*)[64])xx_mem_alloc(4096*64);if(!names||!refs)goto done;
#define GS(x) do{if(!(x))goto done;}while(0)
 while(p<n){uint16_t z;unsigned type,dtype,size;const uint8_t *data;GS(!tb_stop(pd)&&++work<=1000000&&tb_span(p,4,n));z=xx_data_get_u16(b+p, 2, 0, true);type=b[p+2];dtype=b[p+3];GS(z>=4&&!(z&1)&&tb_span(p,z,n));data=b+p+4;size=z-4;
  if(stage==0){GS(type==0&&dtype==2&&size==2&&(xx_data_get_u16(data, 2, 0, true)==3||xx_data_get_u16(data, 2, 0, true)==4||xx_data_get_u16(data, 2, 0, true)==5||xx_data_get_u16(data, 2, 0, true)==6||xx_data_get_u16(data, 2, 0, true)==7||xx_data_get_u16(data, 2, 0, true)==600));stage=1;}
  else if(stage==1){GS(type==1&&dtype==2&&gs_dates(data,size));stage=2;}
  else if(stage==2){GS(type==2&&dtype==6&&gs_name(data,size,name));stage=3;}
  else if(stage==3){GS(type==3&&dtype==5&&size==16&&gs_real(data,true)&&gs_real(data+8,true));stage=4;}
  else if(stage==4){if(type==5){GS(dtype==2&&gs_dates(data,size)&&names_count<4096);if(!names_count)GS(tb_emit(f,s,"library.gds",0,p,n));start=p;stage=5;}
   else {GS(type==4&&dtype==0&&!size&&names_count>0&&p+z==n&&tb_emit(f,s,"end-library.gds",p,z,n));stage=8;}}
  else if(stage==5){unsigned i;GS(type==6&&dtype==6&&gs_name(data,size,name));for(i=0;i<names_count;++i)GS(xx_rt_strcmp(names[i],name)!=0);xx_rt_snprintf(names[names_count++],64,"%s",name);stage=6;}
  else if(stage==6){if(type==7){GS(dtype==0&&!size&&tb_emit(f,s,"structure.gds",start,p+z-start,n));stage=4;}
   else {GS(dtype==0&&!size&&(type==8||type==9||type==10||type==11||type==12||type==21||type==45));element=type;seen=0;xy_count=0;stage=7;}}
  else if(stage==7){unsigned flag=0;GS(type!=5&&type!=7);
   if(type==17){unsigned required=element==10?12:element==11?44:element==12?15:7;GS(dtype==0&&!size&&(seen&required)==required&&!(seen&512));
    GS(xy_count==(element==10||element==12?1U:element==11?3U:element==45?5U:xy_count));if(element==8||element==45)GS(xy_count>=4&&!xx_rt_memcmp(xy,xy+(xy_count-1)*8,8));if(element==9)GS(xy_count>=2);if(element==21)GS(xy_count>=1&&xy_count<=50);stage=6;
   }else if(type==13){flag=1;GS(element!=10&&element!=11&&dtype==2&&size==2&&(int16_t)xx_data_get_u16(data, 2, 0, true)>=0);}
   else if(type==14||type==22||type==42||type==46){flag=2;GS(dtype==2&&size==2&&(int16_t)xx_data_get_u16(data, 2, 0, true)>=0&&type==(element==12?22U:element==21?42U:element==45?46U:14U));}
   else if(type==16){flag=4;GS(dtype==3&&size>=8&&size%8==0&&size<=8192);xy=data;xy_count=size/8;}
   else if(type==18){flag=8;GS((element==10||element==11)&&dtype==6&&refs_count<4096&&gs_name(data,size,refs[refs_count++]));}
   else if(type==25){flag=8;GS(element==12&&dtype==6&&size>=2&&size<=4096);GS(tb_utf(data,size-(data[size-1]==0),false,pd));}
   else if(type==19){flag=32;GS(element==11&&dtype==2&&size==4&&xx_data_get_u16(data, 2, 0, true)>0&&xx_data_get_u16(data+2, 2, 0, true)>0);}
   else if(type==15){flag=64;GS((element==9||element==12)&&dtype==3&&size==4);}
   else if(type==26){flag=128;GS((element==10||element==11||element==12)&&dtype==1&&size==2&&!(xx_data_get_u16(data, 2, 0, true)&~0x8006U));}
   else if(type==27||type==28){flag=type==27?1024:2048;GS((seen&128)&&dtype==5&&size==8&&gs_real(data,type==27));}
   else if(type==23){flag=4096;GS(element==12&&dtype==1&&size==2&&!(xx_data_get_u16(data, 2, 0, true)&~0x3fU));}
   else if(type==33){flag=8192;GS(element==9&&dtype==2&&size==2&&xx_data_get_u16(data, 2, 0, true)<=4);}
   else if(type==38){flag=16384;GS(dtype==1&&size==2&&!(xx_data_get_u16(data, 2, 0, true)&~3U));}
   else if(type==47){flag=32768;GS(dtype==3&&size==4&&!(xx_data_get_u32(data, 4, 0, true)&0xfe000000U));}
   else if(type==43){GS(dtype==2&&size==2&&xx_data_get_u16(data, 2, 0, true)>0&&!(seen&512));seen|=512;}
   else if(type==44){GS(dtype==6&&size&&size<=4096&&(seen&512)&&tb_utf(data,size-(data[size-1]==0),false,pd));seen&=~512U;}
   else if(type==48||type==49){flag=type==48?65536:131072;GS(element==9&&(seen&8192)&&dtype==3&&size==4);}
   else { goto done; } if(flag){GS(!(seen&flag));seen|=flag;}
  }else goto done;p+=z;
 }GS(stage==8);{unsigned i,j;for(i=0;i<refs_count;++i){for(j=0;j<names_count;++j){GS(!tb_stop(pd)&&++work<=16000000);if(!xx_rt_strcmp(refs[i],names[j]))break;}GS(j<names_count);}}s->size=(int64_t)n;result=true;
done:xx_mem_free(names);xx_mem_free(refs);return result;
#undef GS
}

void xx_calma_gdsii_init(xx_calma_gdsii *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_CALMA_GDSII,"gds");}}
xx_calma_gdsii *xx_calma_gdsii_create(xx_io_device *d,int64_t at) {xx_calma_gdsii *r=(xx_calma_gdsii *)xx_mem_alloc(sizeof(*r));if(r)xx_calma_gdsii_init(r,d,at);return r;}
void xx_calma_gdsii_destroy(xx_calma_gdsii *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_calma_gdsii_free(xx_calma_gdsii *r) {if(r){xx_calma_gdsii_destroy(r);xx_mem_free(r);}}
bool xx_calma_gdsii_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_calma_gdsii_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
