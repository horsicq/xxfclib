/* SPDX-License-Identifier: MIT. Reuse the bounded helper iterator ABI. */
#include "xxfclib/formats/media_engine/xx_media_engine.h"
#include "xxfclib/rt/xx_rt.h"

Abstractformat *xx_media_engine_create(xx_io_device *device, int64_t base, const char *mode) {
    if (mode && xx_rt_strcmp(mode, "audio") && xx_rt_strcmp(mode, "video") && xx_rt_strcmp(mode, "frames")) return NULL;
    return xx_sevenzip_engine_create_helper(device, base, mode, "xfu_media_helper.exe", XX_FILE_TYPE_UNKNOWN, "");
}

void xx_media_engine_free(Abstractformat *format) { xx_sevenzip_engine_free(format); }
xx_sevenzip_backend_status xx_media_engine_get_status(const Abstractformat *format) { return xx_sevenzip_engine_get_status(format); }
const char *xx_media_engine_get_details(const Abstractformat *format) { return xx_sevenzip_engine_get_handler(format); }
const char *xx_media_engine_get_mode(const Abstractformat *format) {
    const char *details = xx_media_engine_get_details(format);
    if (!details) return NULL;
    if (!xx_rt_strncmp(details, "frames", 6U) && (!details[6] || details[6] == ':')) return "frames";
    if (!xx_rt_strncmp(details, "audio", 5U) && (!details[5] || details[5] == ':')) return "audio";
    if (!xx_rt_strncmp(details, "video", 5U) && (!details[5] || details[5] == ':')) return "video";
    return NULL;
}
bool xx_media_engine_set_source_path(Abstractformat *format, const char *path) { return xx_sevenzip_engine_set_source_path(format, path); }
