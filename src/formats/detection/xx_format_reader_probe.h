/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_FORMAT_READER_PROBE_H
#define XX_FORMAT_READER_PROBE_H

/* The explicit records in xx_format_detect_device.c select the existing
 * validator, null-device policy and stack-frame policy independently.
 * Custom readers remain ordinary functions. Do not infer these policies
 * from a file type: some readers intentionally use base-info acceptance.
 * Keep parameter/local identifiers explicit so expansion can be compared
 * exactly against the original private functions. */
#define XX_FORMAT_INLINE_ALLOWED
#define XX_FORMAT_REQUIRE_DEVICE(device) \
    if (!device) return false;
#define XX_FORMAT_ALLOW_NULL(device)
#define XX_FORMAT_READER_ADAPTER(frame, symbol, reader_type, device, reader, result, guard, method) \
    static frame bool symbol(xx_io_device *device)                                                  \
    {                                                                                               \
        reader_type reader;                                                                         \
        bool result;                                                                                \
        guard(device) reader_type##_init(&reader, device, 0);                                       \
        result = reader_type##_##method(&reader.format, NULL);                                      \
        reader_type##_destroy(&reader);                                                             \
        return result;                                                                              \
    }

#endif
