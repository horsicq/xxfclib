/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfc_readers.h"
#include <stdio.h>

/* Offline input for generate_fast_extensions.py. No parsing is performed. */
int main(void) {
    static const unsigned char probe[1] = {0};
    xxfc_reader_entry *table = xxfc_reader_table();
    xx_io_device *device = xx_io_mem_open_ro(probe, sizeof(probe));
    size_t i;
    if (!device) return 1;
    for (i = 0; i < xxfc_reader_count(); ++i) {
        Abstractformat *format = table[i].create(device, 0);
        const char *extension;
        if (!format) { xx_io_close(device); return 1; }
        extension = xx_format_get_extension(format);
        printf("%s\t%d\t%s\n", table[i].name, (int)table[i].type,
               extension && extension[0] ? extension : table[i].name);
        table[i].release(format);
    }
    xx_io_close(device);
    return ferror(stdout) ? 1 : 0;
}
