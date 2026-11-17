/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * DOS self-extractor whose appended ZIP keeps offsets absolute from the file
 * start (the common PKSFX/PKLITE case, no offset bias and no duplicated
 * directory) and whose members are traditional ZipCrypto with no in-file
 * password. Such a file is a valid leading-data ZIP; descend into it and let
 * the key-state dictionary recover the key. This strategy only claims the file
 * when a key-state is actually confirmed (decode + CRC of a member), so files
 * that cannot be recovered keep their previous opaque-payload behavior. */
#ifndef XX_DOS_ZIP_CRYPTO_H
#define XX_DOS_ZIP_CRYPTO_H
static int sfx_dos_zip_crypto_ensure(xx_sfx_zipcentral *r, xx_pd_struct *pd)
{
    Abstractformat *f = &r->format;
    int64_t size = pm_available(f);
    uint8_t h[2];
    xx_archive_record_state *state = NULL;
    const xx_archive_record *record;
    uint64_t count = 0;
    bool valid = false;

    if (r->checked_crypto) return r->inner_ready ? 1 : 0;
    r->checked_crypto = true;
    if (size < 86 || !pm_read(f, 0, h, sizeof(h)) || h[0] != 'M' || h[1] != 'Z') return 0;

    xx_zip_init(&r->inner, f->device, f->base_address);
    r->inner.allow_leading_data = true;
    /* Keep the key-state dictionary enabled on this reader (default). */
    if (!xx_zip_handle_base_info(&r->inner.format, pd) || r->inner.format.format_size != size || !r->inner.format.number_of_archive_records ||
        r->inner.format.number_of_archive_records > 4096U || r->inner.directory_damaged)
        goto discard;

    state = xx_zip_create_archive_records_reading(&r->inner.format, NULL, pd);
    if (!state) goto discard;
    valid = true;
    while ((record = xx_zip_get_current_archive_record(&r->inner.format, state)) != NULL) {
        if (carrier_stop(pd) || ++count > 4096U || record->compressed_size < 0 ||
            (record->data_offset < 0 && !xx_zip_resolve_record_data_offset(&r->inner, &state->current_record)) || record->data_offset < 0 || record->data_offset > size ||
            record->compressed_size > size - record->data_offset) {
            valid = false;
            break;
        }
        if (!xx_zip_archive_record_move_to_next(&r->inner.format, state, pd)) break;
    }
    if (count != r->inner.format.number_of_archive_records) valid = false;
    xx_zip_free_archive_records_reading(&r->inner.format, state);
    if (!valid) goto discard;

    /* Only claim the file when a ZipCrypto key-state is actually recovered. */
    if (!xx_zip_recover_zipcrypto_keystate(&r->inner, pd) || !r->inner.zc_have) goto discard;
    r->inner_ready = true;
    return 1;
discard:
    xx_zip_destroy(&r->inner);
    return 0;
}
#endif
