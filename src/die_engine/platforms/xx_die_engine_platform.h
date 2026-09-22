/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_die_engine_platform.h
 * @brief Internal platform interface for the scan engine.
 *
 * The engine is almost entirely OS-agnostic -- it reads bytes that are already
 * in memory. The one thing it cannot answer portably is which system it is
 * running on, which the script API exposes to signature scripts.
 */

#ifndef XX_DIE_ENGINE_PLATFORM_H
#define XX_DIE_ENGINE_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The host OS name as signature scripts see it.
 *
 * One of "win32", "macos" or "linux". Returned to scripts by the DIE script
 * API's getOS(), so the spelling is part of the signature-database contract
 * and must not be "improved".
 */
const char *xx_die_engine_platform_os_name(void);

#ifdef __cplusplus
}
#endif

#endif /* XX_DIE_ENGINE_PLATFORM_H */
