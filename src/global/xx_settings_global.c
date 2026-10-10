/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/global/xx_settings_global.h"

static struct xx_settings_s *g_settings = NULL;

void xx_set_settings(struct xx_settings_s *settings)
{
    g_settings = settings;
}

struct xx_settings_s *xx_get_settings(void)
{
    return g_settings;
}

void xx_global_set_settings(struct xx_settings_s *settings)
{
    xx_set_settings(settings);
}

struct xx_settings_s *xx_global_get_settings(void)
{
    return xx_get_settings();
}
