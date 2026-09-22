/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFC_SAMPLE_PE_DUMP_H
#define XXFC_SAMPLE_PE_DUMP_H

#include <xxfclib/formats/xx_format.h>

#include <stdbool.h>

bool xxfc_dump_pe_imports(Abstractformat *format);
bool xxfc_dump_pe_exports(Abstractformat *format);
bool xxfc_dump_pe_resources(Abstractformat *format);

#endif /* XXFC_SAMPLE_PE_DUMP_H */
