/* Independent DGCA entropy model. SPDX-License-Identifier: MIT. */
#ifndef DGCA_ENTROPY_H
#define DGCA_ENTROPY_H
#include "dgca_native.h"
dg_status dg_entropy_decode(const dg_callbacks *, const unsigned char *, size_t, unsigned char *, size_t, uint32_t parameters);
#endif
