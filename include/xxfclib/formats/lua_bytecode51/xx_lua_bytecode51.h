/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://www.lua.org/source/5.1/lundump.c.html
 * Lua5.1 little-endian chunks with4-byte integers/instructions,4/8-byte size_t and IEEE64 numbers. Parses all nested prototype/code/constant/debug tables, opcode numbers, stack counts and debug ranges; recursion32,1024 prototypes/1million instructions. Exports header and encoded prototype tree. Other Lua versions/architectures, VM semantic verification and execution unsupported.
 */
#ifndef XX_LUA_BYTECODE51_H
#define XX_LUA_BYTECODE51_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lua_bytecode51 { Abstractformat format; } xx_lua_bytecode51;
XXFC_API void xx_lua_bytecode51_init(xx_lua_bytecode51 *,xx_io_device *,int64_t);
XXFC_API xx_lua_bytecode51 *xx_lua_bytecode51_create(xx_io_device *,int64_t);
XXFC_API void xx_lua_bytecode51_destroy(xx_lua_bytecode51 *);
XXFC_API void xx_lua_bytecode51_free(xx_lua_bytecode51 *);
XXFC_API bool xx_lua_bytecode51_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lua_bytecode51_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lua_bytecode51_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lua_bytecode51_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lua_bytecode51_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
