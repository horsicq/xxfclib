/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Inert encoded components only: no playback, emulation or filesystem recovery.
 */
#include "xxfclib/formats/dragon_vdk/xx_dragon_vdk.h"
#include "../nintendo_sdat/xx_twelfth_c.h"

static bool read_components(Abstractformat *f,pm_stream *s,tc_blob *b) {
 const uint8_t *p=b->p;uint32_t head,a,c,h,j,name;char label[64];
 if(b->n<12 || p[0]!='d' || p[1]!='k' || p[4]!=0x10 || p[5]!=0x10 || !p[8] || p[8]>86 || !p[9] || p[9]>2 || p[10]&~1U || p[11]&7) return false;
 head=pm_le16(p+2);name=p[11]>>3;if(head<12 || head>b->n || name>head-12 || (uint64_t)p[8]*p[9]*18*256!=b->n-head) return false;
 for(j=0;j<name;++j) if(p[12+j]<32 || p[12+j]>126) return false;
 if(!tc_emit(f,s,b,"vdk-descriptor.bin",0,head)) { return false; } a=head;
 for(c=0;c<p[8];++c) for(h=0;h<p[9];++h) for(j=1;j<=18;++j) {xx_rt_snprintf(label,sizeof(label),"cylinder-%02u-head-%u-sector-%02u.bin",c,h,j);if(!tc_emit(f,s,b,label,a,256)) return false;a+=256;}
 if(a!=b->n) { return false; } s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { tc_blob b;bool ok;if(!tc_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_dragon_vdk_init(xx_dragon_vdk *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_DRAGON_VDK,"dragon_vdk");} }
xx_dragon_vdk *xx_dragon_vdk_create(xx_io_device *d,int64_t b) { xx_dragon_vdk *r=(xx_dragon_vdk *)xx_mem_alloc(sizeof(*r));if(r) xx_dragon_vdk_init(r,d,b);return r; }
void xx_dragon_vdk_destroy(xx_dragon_vdk *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_dragon_vdk_free(xx_dragon_vdk *r) { if(r) {xx_dragon_vdk_destroy(r);xx_mem_free(r);} }
bool xx_dragon_vdk_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_dragon_vdk_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
