/* SPDX-License-Identifier: MIT. Internal filesystem policy boundaries. */
#ifndef XX_IO_POLICY_H
#define XX_IO_POLICY_H
#include "xxfclib/io/xx_io.h"
bool xx_io_policy_mutation_allowed(void);
bool xx_io_policy_file_open_allowed(const char *mode);
xx_io_device *xx_io_memory_temp_open(void);
bool xx_io_memory_temp_is_device(const xx_io_device *device);
#endif
