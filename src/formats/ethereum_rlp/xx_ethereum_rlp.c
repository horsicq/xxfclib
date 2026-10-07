/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://ethereum.org/developers/docs/data-structures-and-encoding/rlp */
#include "xxfclib/formats/ethereum_rlp/xx_ethereum_rlp.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_sixteenth_wrappers.h"

typedef struct rlp_work {uint64_t items;unsigned root_children;} rlp_work;
static bool item(Abstractformat *f,pm_stream *s,nh_blob *b,uint64_t *at,uint64_t end,unsigned depth,rlp_work *work){if(depth>32||++work->items>4096||fd_stop(b->pd)||!th_take(b,at,end,1))return false;uint64_t start=*at-1,n;unsigned code=b->p[(size_t)start];bool list=code>=192;if(code<128)return nh_add(f,s,b,"stored-leaf",start,1);unsigned shortbase=list?192:128,longbase=list?247:183;if(code<=longbase)n=code-shortbase;else{unsigned bytes=code-longbase;if(bytes>8||!th_take(b,at,end,bytes)||!b->p[(size_t)(*at-bytes)])return false;n=0;for(unsigned j=0;j<bytes;++j)n=(n<<8)|b->p[(size_t)(*at-bytes+j)];if(n<56)return false;}uint64_t p=*at;if(!th_take(b,at,end,n))return false;if(!list&&n==1&&b->p[(size_t)p]<128)return false;if(!nh_add(f,s,b,list?"rlp-list-header":"rlp-string-header",start,p-start))return false;if(list){uint64_t pos=p;while(pos<p+n){if(!depth)++work->root_children;if(!item(f,s,b,&pos,p+n,depth+1,work))return false;}return pos==p+n;}return !n||nh_add(f,s,b,"stored-leaf",p,n);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint64_t at=0;rlp_work work={0,0};bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n>=58&&b.p[0]>=248&&b.p[0]<=251&&item(f,s,&b,&at,b.n,0,&work)&&at==b.n&&work.root_children>=2);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_ethereum_rlp_init(xx_ethereum_rlp *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ETHEREUM_RLP,"bin");}}
xx_ethereum_rlp *xx_ethereum_rlp_create(xx_io_device *d,int64_t b){xx_ethereum_rlp *r=(xx_ethereum_rlp *)xx_mem_alloc(sizeof(*r));if(r)xx_ethereum_rlp_init(r,d,b);return r;}
void xx_ethereum_rlp_destroy(xx_ethereum_rlp *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ethereum_rlp_free(xx_ethereum_rlp *r){if(r){xx_ethereum_rlp_destroy(r);xx_mem_free(r);}}
bool xx_ethereum_rlp_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_ethereum_rlp_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
