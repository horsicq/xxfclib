/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_formats.c - JPEG, APK, PDF and DEX script API dispatch. */

#include "xx_die_engine_api_internal.h"
#include "xxfclib/list/xx_list.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/formats/installer/apk/xx_apk.h"

bool die_engine_api_dispatch_formats(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        /* ------------------------------------------------------- JPEG */
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
        case A_getAndroidManifestRecord: {
            char *pRecordName = arg_string(pCtx, nArgc, pArgv, 0);
            char *pValue = pEngine->bHasApk ? xx_apk_manifest_record(&pEngine->apk, pRecordName) : NULL;
            JSVal res = js_str(pCtx, pValue ? pValue : "");

            cd_free(pRecordName);
            xx_str_free(pValue);

            *pResult = res;
            return true;
        }

        case A_getAndroidManifest:
            *pResult = js_str(pCtx, (pEngine->bHasApk && xx_apk_get_manifest(&pEngine->apk)) ? xx_apk_get_manifest(&pEngine->apk) : "");
            return true;

        /* ------------------------------------------------------- PDF  */
        case A_getHeaderCommentAsHex: {
            char *pHex = pEngine->bHasPdf ? xx_pdf_get_header_comment_hex(&pEngine->pdf, NULL) : NULL;
            JSVal res = js_str(pCtx, pHex ? pHex : "");

            xx_str_free(pHex);

            *pResult = res;
            return true;
        }

        case A_getStringValuesByKey:
        case A_getValuesByKey: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal res = js_new_array(pCtx);

            if (pEngine->bHasPdf) {
                xx_list_s values;
                size_t i = 0;

                if (xx_list_init(&values, sizeof(char *), NULL)) {
                    /* pdf_script inspects at most 20 parts per object. */
                    (void)xx_pdf_get_values_by_key(&pEngine->pdf, pKey, id == A_getStringValuesByKey, 20, &values, NULL);

                    for (i = 0; i < values.count; i++) {
                        char **pValue = (char **)xx_list_at(&values, i);
                        js_array_push(pCtx, res, js_str(pCtx, *pValue ? *pValue : ""));
                        xx_str_free(*pValue);
                    }

                    xx_list_cleanup(&values);
                }
            }

            cd_free(pKey);

            *pResult = res;
            return true;
        }

        case A_isValuesHexByKey: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasPdf ? xx_pdf_is_values_hex_by_key(&pEngine->pdf, pKey, 20, NULL) : 0;

            cd_free(pKey);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isEncrypted:
            *pResult = js_bool(pEngine->bHasPdf ? xx_pdf_is_encrypted(&pEngine->pdf) : 0);
            return true;

        case A_getEncryption:
        case A_getPermissions: {
            char *pInfo = NULL;
            JSVal res;

            if (pEngine->bHasPdf) {
                pInfo = id == A_getEncryption ? xx_pdf_get_encryption(&pEngine->pdf, NULL) : xx_pdf_get_permissions(&pEngine->pdf, NULL);
            }

            res = js_str(pCtx, pInfo ? pInfo : "");
            xx_str_free(pInfo);

            *pResult = res;
            return true;
        }

        /* ------------------------------------------------------- DEX  */
        case A_isDexStringPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasDex ? xx_dex_string_present(&pEngine->dex, pString) : 0;

            cd_free(pString);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isDexItemStringPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasDex ? xx_dex_item_string_present(&pEngine->dex, pString) : 0;

            cd_free(pString);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_getMapItemsHash:
            *pResult = js_num((double)(pEngine->bHasDex ? xx_dex_get_map_hash(&pEngine->dex) : 0));
            return true;

        default:
            break;
    }

    return false;
}
