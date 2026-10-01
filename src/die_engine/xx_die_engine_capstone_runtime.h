/* Private adapter for byte-identical, unmodified upstream Capstone sources.
 * Include system declarations before redirects, so hosted headers keep their
 * own signatures. Constant callback initializers avoid shared mutable setup
 * when independent scan engines first use Capstone concurrently. */
#ifndef XXFC_CAPSTONE_RUNTIME_H
#define XXFC_CAPSTONE_RUNTIME_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "xxfclib/rt/xx_rt.h"
#include "xx_die_engine_capstone_symbols.h"

static inline char *xxfc_capstone_strcpy(char *destination, const char *source)
{
    xx_rt_memcpy(destination, source, xx_rt_strlen(source) + 1);
    return destination;
}
static inline char *xxfc_capstone_strcat(char *destination, const char *source)
{
    xxfc_capstone_strcpy(destination + xx_rt_strlen(destination), source);
    return destination;
}
#define malloc xx_rt_malloc
#define calloc xx_rt_calloc
#define realloc xx_rt_realloc
#define free xx_rt_free
#define vsnprintf xx_rt_vsnprintf
#define memcpy xx_rt_memcpy
#define memmove xx_rt_memmove
#define memset xx_rt_memset
#define memcmp xx_rt_memcmp
#define strlen xx_rt_strlen
#define strcmp xx_rt_strcmp
#define strncmp xx_rt_strncmp
#define strncpy xx_rt_strncpy
#define strchr xx_rt_strchr
#define strrchr xx_rt_strrchr
#define strcpy xxfc_capstone_strcpy
#define strcat xxfc_capstone_strcat
#endif
