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
 * @file xx_pd.c
 * @brief Progress, cancellation, and error reporting implementation.
 */

#include "xxfclib/data/xx_pd.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#if defined(_MSC_VER)
#define XX_PD_THREAD_LOCAL __declspec(thread)
#else
#define XX_PD_THREAD_LOCAL _Thread_local
#endif

static XX_PD_THREAD_LOCAL xx_pd_observer pd_observer;
static XX_PD_THREAD_LOCAL bool pd_observer_active;

xx_pd_observer xx_pd_set_observer(const xx_pd_struct *pd,
                                xx_pd_observer_fn callback, void *user_data) {
    xx_pd_observer previous = pd_observer;
    pd_observer.progress = pd && callback ? pd : NULL;
    pd_observer.callback = pd && callback ? callback : NULL;
    pd_observer.user_data = pd && callback ? user_data : NULL;
    return previous;
}

static bool poll_observer(const xx_pd_struct *pd) {
    bool stopped;
    if (!pd || pd != pd_observer.progress || !pd_observer.callback || pd_observer_active)
        return false;
    pd_observer_active = true;
    stopped = pd_observer.callback(pd, pd_observer.user_data);
    pd_observer_active = false;
    return stopped;
}

static void notify_observer(xx_pd_struct *pd) {
    if (poll_observer(pd)) pd->is_stop = true;
}

xx_pd_struct xx_pd_init(void) {
    xx_pd_struct pd;
    xx_mem_zero(&pd, sizeof(pd));
    return pd;
}

int xx_pd_enter_level(xx_pd_struct *pd, uint64_t total, const char *status) {
    if (!pd) {
        return -1;
    }

    for (int i = 0; i < XX_PD_LEVELS; ++i) {
        if (!pd->records[i].is_busy) {
            pd->records[i].is_busy = true;
            pd->records[i].current = 0;
            pd->records[i].total = total;
            pd->records[i].status[0] = '\0';

            if (status) {
                size_t len = xx_str_len(status);
                size_t max_copy = sizeof(pd->records[i].status) - 1;
                if (len > max_copy) {
                    len = max_copy;
                }
                if (len > 0) {
                    xx_mem_copy(pd->records[i].status, status, len);
                }
                pd->records[i].status[len] = '\0';
            }
            notify_observer(pd);
            return i;
        }
    }

    return -1;
}

void xx_pd_set_current(xx_pd_struct *pd, int level, uint64_t current) {
    if (!pd || level < 0 || level >= XX_PD_LEVELS) {
        return;
    }
    pd->records[level].current = current;
    notify_observer(pd);
}

void xx_pd_increment_current(xx_pd_struct *pd, int level, uint64_t delta) {
    if (!pd || level < 0 || level >= XX_PD_LEVELS) {
        return;
    }
    pd->records[level].current += delta;
    notify_observer(pd);
}

void xx_pd_leave_level(xx_pd_struct *pd, int level) {
    if (!pd || level < 0 || level >= XX_PD_LEVELS) {
        return;
    }
    pd->records[level].is_busy = false;
    pd->records[level].current = 0;
    pd->records[level].total = 0;
    pd->records[level].status[0] = '\0';
    notify_observer(pd);
}

void xx_pd_stop(xx_pd_struct *pd) {
    if (pd) {
        pd->is_stop = true;
        notify_observer(pd);
    }
}

bool xx_pd_is_stopped(const xx_pd_struct *pd) {
    return pd ? pd->is_stop || poll_observer(pd) : false;
}

void xx_pd_set_error(xx_pd_struct *pd, int error_code, const char *error_str) {
    if (!pd) {
        return;
    }
    pd->last_error = error_code;
    pd->error_string[0] = '\0';

    if (error_str) {
        size_t len = xx_str_len(error_str);
        size_t max_copy = sizeof(pd->error_string) - 1;
        if (len > max_copy) {
            len = max_copy;
        }
        if (len > 0) {
            xx_mem_copy(pd->error_string, error_str, len);
        }
        pd->error_string[len] = '\0';
    }
    notify_observer(pd);
}

void xx_pd_clear_error(xx_pd_struct *pd) {
    if (!pd) {
        return;
    }
    pd->last_error = 0;
    pd->error_string[0] = '\0';
    notify_observer(pd);
}
