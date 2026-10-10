/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_READER_ADAPTER_H
#define XX_READER_ADAPTER_H

#include "../xx_format_abstract_extractor_adapter.h"

/* Readers that represent explicit variants must select that variant even when
 * their shared constructor starts with another type. Ambiguous readers retain
 * the type chosen by their own base-info handler. */
#define XX_READER_TYPE_FILL_UNKNOWN(format, type) \
    if (format && format->file_type == XX_FILE_TYPE_UNKNOWN) format->file_type = type;
#define XX_READER_TYPE_FORCE(format, type) \
    if (format) format->file_type = type;
#define XX_READER_TYPE_PRESERVE(format, type)

#define XX_FORMAT_DEFINE_READER_DESCRIPTOR(name, type) \
    static const xx_file_type_t xx_reader_only_##name##_types[] = {type}; \
    static const xx_format_search_desc xx_reader_only_##name##_desc = { \
        xx_reader_only_##name##_types, 1U, NULL, 0U, \
        xx_reader_only_##name##_open, xx_reader_only_##name##_close, true \
    }; \
    XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(name, xx_reader_only_##name##_desc)

/* The creation expression uses the opener's device argument. Keep its native
 * constructor and destructor together in the owning reader module. */
#define XX_FORMAT_DEFINE_READER_ADAPTER(name, type, create, free_reader, policy) \
    static Abstractformat *xx_reader_only_##name##_open(xx_io_device *device) { \
        Abstractformat *format = (Abstractformat *)create; \
        policy(format, type) \
        return format; \
    } \
    static void xx_reader_only_##name##_close(Abstractformat *format) { \
        free_reader((void *)format); \
    } \
    XX_FORMAT_DEFINE_READER_DESCRIPTOR(name, type)

#endif /* XX_READER_ADAPTER_H */
