/* SPDX-License-Identifier: MIT. Original parser from primary documented layout facts. */
#include "xxfclib/formats/micral_n_raw/xx_micral_n_raw.h"
#include "../disk_additions/xx_disk_additions.h"

/* Explicit raw geometry, not automatic magic detection:64x32x128 bytes/side. */
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){uint64_t n=(uint64_t)pm_available(f);hx_blob b;char info[256];xx_mem_zero(&b,sizeof(b));b.pd=pd;if(n!=262144U&&n!=524288U)return false;
 xx_rt_snprintf(info,sizeof(info),"Format: Micral N explicit raw geometry\nCylinders: 64\nHeads: %u\nSectors per track: 32\nSector bytes: 128\nRepresentation: original declared raw-sector order; no filesystem extraction\n",n==262144U?1U:2U);
 if(!da_add(f,s,"sector-image.mic",0,n)||!hx_text(f,s,&b,info))return false;s->size=(int64_t)n;return da_poll(pd);
}
DA_API(micral_n_raw,XX_FILE_TYPE_MICRAL_N_RAW,"mic")
