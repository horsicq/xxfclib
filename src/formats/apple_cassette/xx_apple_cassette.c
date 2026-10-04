/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original Apple II cassette WAV decoder. PCM crossing timing is interpreted
 * from Apple cassette format facts: 770Hz leader, short sync, 2kHz zero/1kHz
 * one, MSB-first bytes, final XOR checksum seeded FF. No audio is executed.
 * https://github.com/fadden/CiderPress2/blob/main/CommonUtil/CassetteDecoder.cs
 * Zero-crossing input only; severely biased/distorted tape needs restoration.
 */
#include "xxfclib/formats/apple_cassette/xx_apple_cassette.h"
#include "../apple_family/xx_apple_family_private.h"
#define AC_MAX_DATA (512U*1024U)
typedef struct ac_scan {af_work *w;uint8_t *data;uint32_t used,bits,value,leader,phase,state;uint8_t checksum;} ac_scan;
static bool ac_finish(ac_scan *a) {
    char name[48];bool ok;
    if(!a->used || a->bits || a->checksum)return false;
    xx_rt_snprintf(name,sizeof(name),"cassette-%03u.bin",(unsigned)a->w->s->count+1U);
    ok=af_copy(a->w,name,a->data,a->used-1U);a->used=a->bits=a->value=a->leader=a->phase=a->state=0;a->checksum=255U;return ok;
}
static bool ac_half(ac_scan *a,uint32_t usec) {
    uint32_t period;
    if(a->state==1U && usec>=50U && usec<=350U){a->state=2U;a->phase=usec;return true;}
    if(a->state==2U){period=a->phase+usec;a->phase=0;
        if(period>=262U && period<=638U && usec>=156U && usec<=344U){a->state=3U;a->checksum=255U;return true;}
        a->state=a->leader=0;return true;}
    if(!a->phase){a->phase=usec;return true;}period=a->phase+usec;a->phase=0;
    if(a->state==3U){unsigned bit;
        if(period>=312U && period<=688U)bit=0;
        else if(period>=812U && period<=1188U)bit=1;
        else return ac_finish(a);
        a->value=(a->value<<1U)|bit;
        if(++a->bits==8U){if(a->used>=AC_MAX_DATA || a->used>a->w->member_limit)return false;
            a->data[a->used++]=(uint8_t)a->value;a->checksum^=(uint8_t)a->value;a->bits=a->value=0;}
    }else if(period>=1084U && period<=1516U){if(++a->leader>=770U)a->state=1U;}
    else a->leader=a->state=0;
    return af_poll(a->w);
}
static int32_t ac_sample(const uint8_t *p,unsigned bytes) {
    uint32_t v=0;unsigned i;if(bytes==1U)return (int32_t)p[0]-128;
    for(i=0;i<bytes;++i)v|=(uint32_t)p[i]<<(i*8U);
    if(bytes<4U && (v&(1U<<(bytes*8U-1U))))v|=UINT32_MAX<<(bytes*8U);return (int32_t)v;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    af_work w;af_blob b;ac_scan scan;xx_apple_cassette *r=(xx_apple_cassette *)f;
    uint32_t at=12,fmt=0,data=0,bytes=0,rate,frames,i,last=0;unsigned width,channels,align,channel;bool positive=false,have=false,ok=false;
    if(!af_init(&w,f,s,pd) || !af_load(&w,&b))return false;xx_mem_zero(&scan,sizeof(scan));scan.w=&w;scan.checksum=255U;
    if(b.n<44U || xx_rt_memcmp(b.p,"RIFF",4) || xx_rt_memcmp(b.p+8,"WAVE",4) || (uint64_t)pm_le32(b.p+4)+8U!=b.n)goto done;
    while(at<b.n){uint32_t n;if(!af_range(&b,at,8U))goto done;n=pm_le32(b.p+at+4);if(!af_range(&b,(uint64_t)at+8U,(uint64_t)n+(n&1U)))goto done;
        if(!xx_rt_memcmp(b.p+at,"fmt ",4)){if(fmt || (n!=16U && n!=18U) || (n==18U && pm_le16(b.p+at+24)))goto done;fmt=at+8U;}
        else if(!xx_rt_memcmp(b.p+at,"data",4)){if(data)goto done;data=at+8U;bytes=n;}at+=8U+n+(n&1U);}
    if(!fmt || !data || pm_le16(b.p+fmt)!=1U)goto done;channels=pm_le16(b.p+fmt+2);rate=pm_le32(b.p+fmt+4);align=pm_le16(b.p+fmt+12);width=pm_le16(b.p+fmt+14)/8U;
    if(channels<1U || channels>2U || rate<8000U || rate>192000U || width<1U || width>4U || pm_le16(b.p+fmt+14)!=width*8U ||
       align!=width*channels || pm_le32(b.p+fmt+8)!=(uint64_t)rate*align || !bytes || bytes%align || r->profile>channels)goto done;
    channel=r->profile?r->profile-1U:0U;frames=bytes/align;scan.data=af_alloc(&w,AC_MAX_DATA,false);if(!scan.data)goto done;
    for(i=0;i<frames;++i){int32_t value=ac_sample(b.p+data+(uint64_t)i*align+channel*width,width);bool sign=value>=0;
        if(!(i&4095U) && !af_poll(&w))goto done;
        if(!have){positive=sign;have=true;last=i;continue;}
        if(sign!=positive){uint64_t usec=(uint64_t)(i-last)*1000000U/rate;positive=sign;last=i;
            if(usec>UINT32_MAX || !ac_half(&scan,(uint32_t)usec))goto done;}
    }
    if(scan.state==3U && !ac_finish(&scan))goto done;
    if(!s->count)goto done;s->size=b.n;r->number_of_records=s->count;r->detected_profile=channel+1U;
    r->note="Apple II PCM cassette: leader/sync, byte framing and XOR checksum verified; zero-crossing channel decoder; binary blocks exclude tape checksum";ok=af_poll(&w);
done:af_release(&w,scan.data,AC_MAX_DATA);af_release(&w,b.p,b.n);return ok;
}
AF_DEFINE_READER(apple_cassette,XX_FILE_TYPE_APPLE_CASSETTE,"wav")
