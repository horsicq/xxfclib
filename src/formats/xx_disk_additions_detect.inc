/* SPDX-License-Identifier: MIT. Necessary structural gates only. Factory
 * validation remains mandatory; raw signal/Micral profiles are explicit.
 * No reads, allocation, cursor changes or source bytes beyond first_size.
 */
static uint16_t ddet_le16(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}
static uint32_t ddet_le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool xx_disk_addition_candidate(uint32_t type,const uint8_t *first512,size_t first_size,uint64_t source_total){const uint8_t *p=first512;uint64_t size;uint32_t i,blocks,bps,tracks,copied=0;if(!p)return false;
 switch(type){
 case XX_FILE_TYPE_LISA_BLU:
  if(first_size<23U) {return false; } for(i=0;i<13U;++i)if(p[i]<32U||p[i]>126U)return false;blocks=(uint32_t)p[18]<<16|(uint32_t)p[19]<<8|p[20];bps=(uint32_t)p[21]<<8|p[22];return blocks&&(bps&0xfe00U)==512U&&(uint64_t)(blocks+1U)*bps==source_total;
 case XX_FILE_TYPE_BLINDWRITE4:return first_size>=31U&&source_total<=16U*1024U*1024U&&!xx_rt_memcmp(p,"BLINDWRITE TOC FILE",19);
 case XX_FILE_TYPE_CISCOPY:
  if(first_size<1U||p[0]<1U||p[0]>7U) {return false; } tracks=p[0]<=4U?80U:160U;if(first_size<tracks+2U||p[tracks+1U])return false;for(i=0;i<tracks;++i){if(p[i+1U]!=0x4cU&&p[i+1U]!=0xfaU&&p[i+1U]!=0xfeU)return false;if((p[0]>2U||!(i&1U))&&p[i+1U]==0x4cU)++copied;}bps=(p[0]==1U||p[0]==3U)?8U:(p[0]<=5U?9U:(p[0]==6U?15U:18U));return source_total==tracks+2U+(uint64_t)copied*bps*512U;
 case XX_FILE_TYPE_COPYTAPE:return first_size>=9U&&(!xx_rt_memcmp(p,"CPTP:BLK ",9)||!xx_rt_memcmp(p,"CPTP:MRK\n",9)||!xx_rt_memcmp(p,"CPTP:EOT\n",9));
 case XX_FILE_TYPE_WC_DISK_IMAGE:return first_size>=32U&&!xx_rt_memcmp(p,"WC DISK IMAGE\x1a\x1a\0",16)&&p[16]<=1U&&p[17]>=1U&&p[17]<=2U&&p[18]>=1U&&p[18]<=18U&&p[19]>=1U&&p[19]<=80U;
 case XX_FILE_TYPE_DRI_DISKCOPY:
  /* Signature is in the final415-byte footer. Its strict factory reads only
   * that footer; all documented BPBs imply a multiple128-byte sector body. */
  return source_total>=543U&&(source_total-415U)%128U==0;
 case XX_FILE_TYPE_MAXI_DISK:
  if(first_size<8U||p[1]>11U||!p[2]||p[2]>2U||!p[3]||p[3]>90U||p[4]>7U||!p[5]||p[5]>64U) {return false; } return source_total==8U+(uint64_t)p[2]*p[3]*p[5]*(128U<<p[4]);
 case XX_FILE_TYPE_RAY_DIM:return first_size>=84U&&!xx_rt_memcmp(p,"Disk IMage VER ",15)&&p[80]>=1U&&p[80]<=5U&&p[82]>=1U&&p[82]<=64U&&p[83]<=2U;
 case XX_FILE_TYPE_RS_IDE:
  if(first_size<128U||xx_rt_memcmp(p,"RS-IDE\x1a",7)||(p[8]&~1U)) {return false; } i=ddet_le16(p+9);return i>=128U&&source_total>i&&(source_total-i)%((p[8]&1U)?256U:512U)==0;
 case XX_FILE_TYPE_T98_HDD:
  if(first_size<256U||!(i=ddet_le32(p))||i>1048576U) {return false; } for(blocks=4;blocks<256U;++blocks)if(p[blocks])return false;return source_total==256U+(uint64_t)i*8U*33U*256U;
 case XX_FILE_TYPE_PC_86F:
  if(first_size<12U||xx_rt_memcmp(p,"86BF",4)||p[4]!=12U||p[5]!=2U||(ddet_le16(p+6)&0xe000U)) {return false; } size=8U+((p[6]&8U)?2048U:1024U);return source_total>size&&ddet_le32(p+8)>=size;
 case XX_FILE_TYPE_FDX68_FDX:
  if(first_size<256U||xx_rt_memcmp(p,"FDX\x03",4)) {return false; } blocks=ddet_le32(p+100);tracks=ddet_le32(p+72);i=ddet_le32(p+76);return blocks>16U&&blocks<=1048576U&&tracks>=1U&&tracks<=256U&&i>=1U&&i<=2U&&source_total==256U+(uint64_t)tracks*i*blocks;
 case XX_FILE_TYPE_HEATHKIT_H17:return first_size>=8U&&!xx_rt_memcmp(p,"H17D200\xff",8);
 case XX_FILE_TYPE_BOCHS_GROWING:return first_size>=96U&&!xx_rt_memcmp(p,"Bochs Virtual HD Image\0",23)&&!xx_rt_memcmp(p+32,"Redolog\0",8)&&!xx_rt_memcmp(p+48,"Growing\0",8)&&ddet_le32(p+68)==512U;
 case XX_FILE_TYPE_HXC_LOGIC_ANALYZER:case XX_FILE_TYPE_MICRAL_N_RAW:return false;
 default:return false;
 }
}
