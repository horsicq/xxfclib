/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-nlmp/b38c36ed-2804-4868-a9ff-8dd3182128e4 */
#include "xxfclib/formats/ntlm_message/xx_ntlm_message.h"
#include "../xx_sixteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=32&&b.n<=4096&&th_eq(&b,0,7,"NTLMSSP")&&!b.p[7]&&pm_le32(b.p+8)==1);uint32_t flags=pm_le32(b.p+12);NH_NEED((flags&0x200)&&flags&3U&&!(flags&0x1d244508U));uint64_t header=(flags&0x02000000)?40:32;NH_NEED(nh_span(&b,0,header));if(header==40)NH_NEED(nh_zero(&b,36,3)&&b.p[39]==15);uint64_t offsets[2],lengths[2];for(unsigned i=0;i<2;++i){uint64_t p=16+(uint64_t)i*8,n=pm_le16(b.p+(size_t)p),max=pm_le16(b.p+(size_t)p+2),off=pm_le32(b.p+(size_t)p+4);NH_NEED(n==max&&n<=255);if(n)NH_NEED((flags&(i?0x2000U:0x1000U))&&off>=header&&nh_span(&b,off,n)&&f15_ascii(&b,off,n,false));else NH_NEED(!(flags&(i?0x2000U:0x1000U))&&off<=b.n);offsets[i]=off;lengths[i]=n;}uint64_t order[2]={0,1};if(lengths[1]&&(!lengths[0]||offsets[1]<offsets[0])){order[0]=1;order[1]=0;}uint64_t at=header;for(unsigned k=0;k<2;++k){unsigned i=(unsigned)order[k];if(lengths[i]){NH_NEED(offsets[i]==at);at+=lengths[i];}}NH_NEED(at==b.n&&nh_add(f,s,&b,"ntlm-negotiate-fields",0,header));for(unsigned i=0;i<2;++i)if(lengths[i])NH_NEED(nh_add(f,s,&b,i?"workstation-oem":"domain-oem",offsets[i],lengths[i]));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_ntlm_message_init(xx_ntlm_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_NTLM_MESSAGE,"bin");}}
xx_ntlm_message *xx_ntlm_message_create(xx_io_device *d,int64_t b){xx_ntlm_message *r=(xx_ntlm_message *)xx_mem_alloc(sizeof(*r));if(r)xx_ntlm_message_init(r,d,b);return r;}
void xx_ntlm_message_destroy(xx_ntlm_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ntlm_message_free(xx_ntlm_message *r){if(r){xx_ntlm_message_destroy(r);xx_mem_free(r);}}
bool xx_ntlm_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_ntlm_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
