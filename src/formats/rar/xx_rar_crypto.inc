/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Password and member-cipher glue; cryptographic primitives live in xx_aes.c.
 */

static bool xx_rar_password_utf8(const Abstractformat *self, const xx_list_s *options,
                                 uint8_t **bytes, size_t *size) {
    const xx_var *value = xx_format_resolve_extra_parameter(self, options, XX_META_ID_OPT_PASSWORD);
    const uint8_t *source = NULL;
    char *converted = NULL;
    wchar_t *wide = NULL;
    size_t n = 0;
    *bytes = NULL; *size = 0;
    if (!value) return false;
    if (value->type == XX_VAR_TYPE_STRING || value->type == XX_VAR_TYPE_STRING_VIEW) {
        source = (const uint8_t *)xx_var_get_str(value); n = value->val.str.len;
    } else if (value->type == XX_VAR_TYPE_BYTES || value->type == XX_VAR_TYPE_BYTES_VIEW) {
        source = xx_var_get_bytes(value, &n);
    } else if (value->type == XX_VAR_TYPE_WSTRING || value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        const wchar_t *text = xx_var_get_wstr(value);
        n = value->val.wstr.len;
        if (n > 1048576U || (!text && n)) return false;
        wide = (wchar_t *)xx_mem_calloc(n + 1U, sizeof(wchar_t));
        if (!wide) return false;
        if (n) xx_rt_memcpy(wide, text, n * sizeof(wchar_t));
        converted = xx_str_unicode_to_utf8(wide);
        xx_mem_zero(wide, (n + 1U) * sizeof(wchar_t)); xx_mem_free(wide);
        if (!converted) return false;
        source = (const uint8_t *)converted; n = xx_rt_strlen(converted);
    } else return false;
    if (n > 1048576U || (!source && n)) goto failed;
    *bytes = (uint8_t *)xx_mem_alloc(n + 1U);
    if (!*bytes) goto failed;
    if (n) xx_rt_memcpy(*bytes, source, n);
    (*bytes)[n] = 0; *size = n;
    if (converted) { xx_mem_zero(converted, n); xx_str_free(converted); }
    return true;
failed:
    if (converted) { xx_mem_zero(converted, n); xx_str_free(converted); }
    return false;
}

static bool xx_rar_derive3(const uint8_t *utf8, size_t size, const uint8_t *salt,
                           uint8_t key[16], uint8_t iv[16], xx_pd_struct *pd) {
    uint8_t *utf16;
    size_t i = 0, n = 0;
    bool ok = false;
    if (size > (SIZE_MAX - 2U) / 2U) return false;
    utf16 = (uint8_t *)xx_mem_alloc(size * 2U + 2U);
    if (!utf16) return false;
    while (i < size) {
        uint32_t cp = utf8[i++];
        unsigned extra = 0, j;
        if (cp >= 0xc2U && cp <= 0xdfU) { cp &= 31U; extra = 1; }
        else if (cp >= 0xe0U && cp <= 0xefU) { cp &= 15U; extra = 2; }
        else if (cp >= 0xf0U && cp <= 0xf4U) { cp &= 7U; extra = 3; }
        else if (cp >= 0x80U) goto done;
        if (extra > size - i) goto done;
        for (j = 0; j < extra; ++j) {
            if ((utf8[i] & 0xc0U) != 0x80U) goto done;
            cp = (cp << 6) | (utf8[i++] & 63U);
        }
        if ((extra == 1 && cp < 0x80U) || (extra == 2 && cp < 0x800U) ||
            (extra == 3 && cp < 0x10000U) || cp > 0x10ffffU ||
            (cp >= 0xd800U && cp <= 0xdfffU)) goto done;
        if (cp > 0xffffU) {
            uint32_t high = 0xd800U + ((cp - 0x10000U) >> 10);
            utf16[n++] = (uint8_t)high; utf16[n++] = (uint8_t)(high >> 8);
            cp = 0xdc00U + ((cp - 0x10000U) & 1023U);
        }
        utf16[n++] = (uint8_t)cp; utf16[n++] = (uint8_t)(cp >> 8);
    }
    ok = xx_rar3_aes_derive(utf16, n, salt, key, iv, pd);
done:
    xx_mem_zero(utf16, size * 2U + 2U); xx_mem_free(utf16);
    return ok;
}

static bool xx_rar_member_keys(const Abstractformat *self, const xx_list_s *options,
                               const xx_rar_file_info *info, uint8_t key[32],
                               size_t *key_size,uint8_t iv[16],uint8_t hash_key[32],
                               xx_pd_struct *pd) {
    uint8_t check[8] = {0}, *password = NULL;
    size_t password_size = 0;
    bool ok = false;
    if (!xx_rar_password_utf8(self, options, &password, &password_size)) return false;
    if (info->crypto_version == 3) {
        *key_size = 16;
        if (!xx_rar_derive3(password, password_size,
                info->crypto_flags ? info->crypto_salt : NULL, key, iv, pd)) goto done;
    } else if (info->crypto_version == 5) {
        *key_size = 32;
        if (!xx_rar5_aes_derive(password, password_size, info->crypto_salt,
                info->crypto_log, key, hash_key, check, pd) ||
            ((info->crypto_flags & 1U) &&
             !xx_rar5_aes_check_password(info->crypto_check, check))) goto done;
        xx_rt_memcpy(iv, info->crypto_iv, 16);
    } else goto done;
    ok = !xx_pd_is_stopped(pd);
done:
    if (!ok) {
        xx_rar_secure_clear(key,32);xx_rar_secure_clear(iv,16);xx_rar_secure_clear(hash_key,32);
    }
    xx_rar_secure_clear(check,sizeof(check));
    xx_rar_secure_clear(password,password_size);xx_mem_free(password);
    return ok;
}

static bool xx_rar_decrypt_blocks(uint8_t *data,size_t size,const uint8_t *key,
                                   size_t key_size,const uint8_t initial_iv[16],
                                   xx_pd_struct *pd) {
    size_t done=0;
    uint8_t iv[16],next_iv[16]={0};
    bool ok=false;
    if (size&15U) return false;
    xx_rt_memcpy(iv,initial_iv,16);
    while (done < size) {
        size_t n = size-done > 65536U ? 65536U : size-done;
        if (xx_pd_is_stopped(pd)) goto done;
        /* CBC chains through ciphertext, so save the final input block before
         * decrypting this in-place chunk. Volume boundaries are immaterial. */
        xx_rt_memcpy(next_iv,data+done+n-16U,16);
        if (!xx_aes_cbc_decrypt(data+done,n,key,key_size,iv,data+done)) goto done;
        xx_rt_memcpy(iv,next_iv,16); done += n;
    }
    ok = !xx_pd_is_stopped(pd);
done:
    xx_rar_secure_clear(iv,sizeof(iv));xx_rar_secure_clear(next_iv,sizeof(next_iv));
    return ok;
}

static bool xx_rar_verify_fragment(const Abstractformat *self,xx_io_device *source,
                                    const xx_rar_file_info *info,xx_pd_struct *pd) {
    uint8_t key[32]={0},iv[16]={0},hash_key[32]={0},digest[32]={0};
    size_t key_size=0;
    uint64_t crc;
    bool mac=info->crypto_version==5 && (info->crypto_flags&2U)!=0;
    bool ok=false;
    if(mac && !xx_rar_member_keys(self,NULL,info,key,&key_size,iv,hash_key,pd)) goto finished;
    if(info->has_crc) {
        uint32_t check;
        if(!xx_crc_calculate_device_by_type(source,info->block.data_offset,
                info->block.data_size,XX_CRC_TYPE_CRC32,pd,&crc)) goto finished;
        check=(uint32_t)crc;
        if(mac) check=xx_rar5_aes_mac_crc32(hash_key,check);
        if(check!=info->data_crc) goto finished;
    }
    if(info->has_blake2) {
        if(!xx_blake2sp_calc_device(source,info->block.data_offset,info->block.data_size,digest,pd) ||
            (mac && !xx_rar5_aes_mac_hash(hash_key,digest,digest)) ||
            xx_rt_memcmp(digest,info->blake2,32)) goto finished;
    }
    ok=!xx_pd_is_stopped(pd);
finished:
    xx_rar_secure_clear(key,sizeof(key));xx_rar_secure_clear(iv,sizeof(iv));
    xx_rar_secure_clear(hash_key,sizeof(hash_key));xx_rar_secure_clear(digest,sizeof(digest));
    return ok;
}
