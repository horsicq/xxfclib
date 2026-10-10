/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_UE2_GAMES_H
#define XX_UE2_GAMES_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Stable IDs, also exposed by the public file-type catalogue. */
#define XX_UE2_GAME_BRUNS 2700
#define XX_UE2_GAME_RPGMV 2701
#define XX_UE2_GAME_UTAGE 2702
#define XX_UE2_GAME_YCG 2703
#define XX_UE2_GAME_UNREAL_PAK 2704
#define XX_UE2_GAME_FALLOUT_DAT 2705
#define XX_UE2_GAME_LIVEMAKER_GAL 2706
#define XX_UE2_GAME_SMILE_PACK 2707
typedef struct xx_ue2_games {
    Abstractformat format;
} xx_ue2_games;
XXFC_API xx_ue2_games *xx_ue2_games_create(xx_io_device *, int64_t, xx_file_type_t);
XXFC_API void xx_ue2_games_free(xx_ue2_games *);
XXFC_API xx_file_type_t xx_ue2_games_detect_device(xx_io_device *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
