/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_pd.h
 * @brief Progress, cancellation, and error reporting data structure and APIs.
 */

#ifndef XX_PD_H
#define XX_PD_H

#include "xxfclib/xxfc_defs.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_PD_LEVELS 5

/**
 * @brief Progress record for an individual nested level of an operation.
 */
typedef struct xx_pd_record {
    uint64_t current;       /**< Current progress counter (e.g. 500) */
    uint64_t total;         /**< Total units for this level (e.g. 10000) */
    bool is_busy;           /**< true if this nested level is currently active */
    char status[64];        /**< Description or label of the operation */
} xx_pd_record;

/**
 * @brief Main progress and control structure containing up to 5 nested levels.
 */
typedef struct xx_pd_struct {
    xx_pd_record records[XX_PD_LEVELS];
    volatile bool is_stop;  /**< Cancellation flag: if true, all loops and functions safe-stop */
    int last_error;         /**< Last error code (0 if OK) */
    char error_string[128]; /**< Last error message */
    void *user_data;        /**< Optional user context pointer */
} xx_pd_struct;

/**
 * @brief Initialize an xx_pd_struct to all zeros / clean state.
 * @return Cleanly initialized xx_pd_struct.
 */
XXFC_API xx_pd_struct xx_pd_init(void);

/**
 * @brief Enter a new nested level in the progress struct.
 * @param pd Pointer to xx_pd_struct (may be NULL).
 * @param total Total units expected for this level.
 * @param status Optional descriptive status string (may be NULL).
 * @return Index of the acquired level (0 to 4), or -1 if pd is NULL or all levels are busy.
 */
XXFC_API int xx_pd_enter_level(xx_pd_struct *pd, uint64_t total, const char *status);

/**
 * @brief Update the current progress value for an active level.
 * @param pd Pointer to xx_pd_struct (may be NULL).
 * @param level Level index (0 to 4).
 * @param current New current progress value.
 */
XXFC_API void xx_pd_set_current(xx_pd_struct *pd, int level, uint64_t current);

/**
 * @brief Increment the current progress value for an active level.
 * @param pd Pointer to xx_pd_struct (may be NULL).
 * @param level Level index (0 to 4).
 * @param delta Amount to add to current progress.
 */
XXFC_API void xx_pd_increment_current(xx_pd_struct *pd, int level, uint64_t delta);

/**
 * @brief Leave an active nested level, marking it free for future operations.
 * @param pd Pointer to xx_pd_struct (may be NULL).
 * @param level Level index (0 to 4).
 */
XXFC_API void xx_pd_leave_level(xx_pd_struct *pd, int level);

/**
 * @brief Request all active operations watching this struct to safely stop.
 * @param pd Pointer to xx_pd_struct (may be NULL).
 */
XXFC_API void xx_pd_stop(xx_pd_struct *pd);

/**
 * @brief Check if stop was requested on the progress struct.
 * @param pd Pointer to xx_pd_struct (may be NULL).
 * @return true if stop was requested, false otherwise (or if pd is NULL).
 */
XXFC_API bool xx_pd_is_stopped(const xx_pd_struct *pd);

/**
 * @brief Set the last error code and error message on the progress struct.
 * @param pd Pointer to xx_pd_struct (may be NULL).
 * @param error_code Integer error code.
 * @param error_str Error message string (may be NULL).
 */
XXFC_API void xx_pd_set_error(xx_pd_struct *pd, int error_code, const char *error_str);

/**
 * @brief Clear any error information stored on the progress struct.
 * @param pd Pointer to xx_pd_struct (may be NULL).
 */
XXFC_API void xx_pd_clear_error(xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_PD_H */
