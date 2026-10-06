/* SPDX-License-Identifier: MIT. Original headerless FEI track reader.
 * HxC documents side-major 12,500/25,000-byte LSB-first raw MFM tracks.
 * File size alone is ambiguous, so recognition requires matching IBM ID CRCs
 * at both side boundaries. Non-IBM/undecodable FEI remains unidentified.
 */
#include "xxfclib/formats/fei/xx_fei.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static unsigned fei_bit(const uint8_t *p,uint32_t bit){return (p[bit/8U]>>(bit%8U))&1U;}
static uint16_t fei_word(const uint8_t *p,uint32_t bit){unsigned k;uint16_t v=0;for(k=0;k<16;++k)v=(uint16_t)((v<<1)|fei_bit(p,bit+k));return v;}
static uint8_t fei_data(uint16_t word){unsigned k;uint8_t v=0;for(k=0;k<8;++k)v=(uint8_t)((v<<1)|((word>>(14U-k*2U))&1U));return v;}
static bool fei_id(hx_blob *b,uint64_t a,uint32_t n,uint32_t cylinder,uint32_t head){uint32_t bit;const uint8_t *p=b->p+a;bool found=false;
 for(bit=0;bit+160U<=n*8U;++bit){uint8_t id[10];unsigned j;
  if(!(bit&4095U)&&!hx_poll(b))return false;
  if(fei_word(p,bit)!=0x4489U||fei_word(p,bit+16)!=0x4489U||fei_word(p,bit+32)!=0x4489U)continue;
  id[0]=id[1]=id[2]=0xa1;for(j=0;j<7;++j)id[3+j]=fei_data(fei_word(p,bit+48+j*16));
  if(id[3]==0xfe&&id[6]&&id[7]<=6&&hx_crc16(id,10)==0){if(id[4]!=cylinder||id[5]!=head)return false;found=true;}
 }return found;
}
static bool fei_geometry(hx_blob *b,uint32_t bytes,uint32_t *tracks){uint32_t n;
 if(b->n%(2U*bytes)) {return false; } n=(uint32_t)(b->n/(2U*bytes));if(!n||n>170U)return false;
 if(!hx_work(b,(uint64_t)bytes*32U))return false;
 if(!fei_id(b,0,bytes,0,0)||!fei_id(b,(uint64_t)n*bytes,bytes,0,1)||
    !fei_id(b,(uint64_t)(n-1)*bytes,bytes,n-1,0)||!fei_id(b,(uint64_t)(2*n-1)*bytes,bytes,n-1,1))return false;
 *tracks=n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){hx_blob b;uint32_t bytes=25000,tracks,i;bool ok=false;char name[96],info[256];
 if(!hx_load(f,&b,pd))return false;
 if(!fei_geometry(&b,bytes,&tracks)){bytes=12500;HX_NEED(fei_geometry(&b,bytes,&tracks));}
 for(i=0;i<tracks*2U;++i){xx_rt_snprintf(name,sizeof(name),"track-C%03u-H%u.lsb-mfm-bitcells",i%tracks,i/tracks);HX_NEED(hx_emit(f,s,&b,name,(uint64_t)i*bytes,bytes));}
 xx_rt_snprintf(info,sizeof(info),"Format: FEI\nCylinders: %u\nSides: 2\nTrack bytes: %u\nRepresentation: original side-major LSB-first MFM tracks\nRecognition: matching IBM address-field CRCs at side boundaries\nIntegrity: extent; interior sector/data CRCs are not verified\n",tracks,bytes);
 HX_NEED(hx_text(f,s,&b,info));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
HX_API(fei,XX_FILE_TYPE_FEI,"fei")
