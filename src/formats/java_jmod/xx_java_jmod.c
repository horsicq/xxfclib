/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/openjdk/jdk/master/src/java.base/share/classes/jdk/internal/jmod/JmodFile.java */
#include "xxfclib/formats/java_jmod/xx_java_jmod.h"
#include "../xx_seventh_data.h"
#include "../makeself/xx_fourth_wrapper_table.h"

static bool jmod_origin(Abstractformat *f,int64_t end,xx_pd_struct *pd) {
    uint8_t h[22];int64_t at=end-22,low=end>65561?end-65557:4;
    for(;at>=low;--at) {if(fd_stop(pd) || !pm_read(f,at,h,4)) return false;
        if(xx_rt_memcmp(h,"PK\5\6",4) || !pm_read(f,at,h,22) || at+22+xx_data_get_u16(h+20, 2, 0, false)!=end) continue;
        return (uint64_t)xx_data_get_u32(h+12, 4, 0, false)+xx_data_get_u32(h+16, 4, 0, false)+4U==(uint64_t)at;
    }return false;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    int64_t available=pm_available(f);uint8_t h[8];
    if(fd_stop(pd) || available<34 || available>67108864 || !pm_read(f,0,h,8) || xx_rt_memcmp(h,"JM\1\0PK\3\4",8) || !jmod_origin(f,available,pd) || !wg_zip(f,4,available,pd)) return false;
    if(!pm_add(f,s,"module.zip",4,available-4)) { return false; } s->size=available;return true;
}

void xx_java_jmod_init(xx_java_jmod *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_JAVA_JMOD,"java_jmod"); } }
xx_java_jmod *xx_java_jmod_create(xx_io_device *d,int64_t b) { xx_java_jmod *r=(xx_java_jmod *)xx_mem_alloc(sizeof(*r)); if(r) xx_java_jmod_init(r,d,b); return r; }
void xx_java_jmod_destroy(xx_java_jmod *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_java_jmod_free(xx_java_jmod *r) { if(r) { xx_java_jmod_destroy(r); xx_mem_free(r); } }
bool xx_java_jmod_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_java_jmod_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
