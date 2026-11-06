/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_jpeg.c - JPEG script API dispatch. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_jpeg_api_table[] = {
    {"getComment", A_getComment, 0},
    {"getDqtMD5", A_getDqtMD5, 0},
    {"isChunkPresent", A_isChunkPresent, 1},
    {"isExifPresent", A_isExifPresent, 0},
    {"getExifCameraName", A_getExifCameraName, 0},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_jpeg(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_jpeg_api_table);
}
bool die_engine_api_dispatch_jpeg(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        case A_getComment:
            *pResult = js_str(pCtx, (pEngine->bHasJpeg && xx_jpeg_get_comment(&pEngine->jpeg)) ? xx_jpeg_get_comment(&pEngine->jpeg) : "");
            return true;

        case A_getDqtMD5:
            *pResult = js_str(pCtx, (pEngine->bHasJpeg && xx_jpeg_get_dqt_md5(&pEngine->jpeg)) ? xx_jpeg_get_dqt_md5(&pEngine->jpeg) : "");
            return true;

        case A_isExifPresent:
            *pResult = js_bool(pEngine->bHasJpeg && (xx_jpeg_get_exif_size(&pEngine->jpeg) > 0));
            return true;

        case A_isChunkPresent: {
            cd_i64 nId = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            *pResult = js_bool(pEngine->bHasJpeg && xx_jpeg_is_chunk_present(&pEngine->jpeg, (uint8_t)nId));
            return true;
        }

        case A_getExifCameraName:
            *pResult = js_str(pCtx, (pEngine->bHasJpeg && xx_jpeg_get_exif_camera_name(&pEngine->jpeg)) ? xx_jpeg_get_exif_camera_name(&pEngine->jpeg) : "");
            return true;

        /* ------------------------------------------------------- APK  */
        default:
            break;
    }

    return false;
}
