/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_HXC_LOGIC_ANALYZER_H
#define XX_HXC_LOGIC_ANALYZER_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_hxc_logic_analyzer;
typedef xx_hxc_logic_analyzer xx_hxc_logic_analyzer_t;
XXFC_API void xx_hxc_logic_analyzer_init(xx_hxc_logic_analyzer *,xx_io_device *,int64_t);
XXFC_API xx_hxc_logic_analyzer *xx_hxc_logic_analyzer_create(xx_io_device *,int64_t);
XXFC_API void xx_hxc_logic_analyzer_destroy(xx_hxc_logic_analyzer *);
XXFC_API void xx_hxc_logic_analyzer_free(xx_hxc_logic_analyzer *);
XXFC_API bool xx_hxc_logic_analyzer_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_hxc_logic_analyzer_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_hxc_logic_analyzer_set_signals(xx_hxc_logic_analyzer *,uint32_t sample_hz,unsigned data_bit,unsigned index_bit);
#ifdef __cplusplus
}
#endif
#endif
