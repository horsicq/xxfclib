/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft PDB: compiler/build info and procedure details from module symbols.
 */
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_debug.h"
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_def.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/memory/xx_arena.h"

#include <stdlib.h>
#include <string.h>

#define SYMS_MAX_STREAM_SIZE (512u * 1024u * 1024u)

typedef struct syms_reg_entry {
    uint16_t id;
    const char *name;
} syms_reg_entry;

static const syms_reg_entry syms_regs_x86[294] = {
    {0, "NONE"},
    {1, "AL"},
    {2, "CL"},
    {3, "DL"},
    {4, "BL"},
    {5, "AH"},
    {6, "CH"},
    {7, "DH"},
    {8, "BH"},
    {9, "AX"},
    {10, "CX"},
    {11, "DX"},
    {12, "BX"},
    {13, "SP"},
    {14, "BP"},
    {15, "SI"},
    {16, "DI"},
    {17, "EAX"},
    {18, "ECX"},
    {19, "EDX"},
    {20, "EBX"},
    {21, "ESP"},
    {22, "EBP"},
    {23, "ESI"},
    {24, "EDI"},
    {25, "ES"},
    {26, "CS"},
    {27, "SS"},
    {28, "DS"},
    {29, "FS"},
    {30, "GS"},
    {31, "IP"},
    {32, "FLAGS"},
    {33, "EIP"},
    {34, "EFLAGS"},
    {40, "TEMP"},
    {41, "TEMPH"},
    {42, "QUOTE"},
    {43, "PCDR3"},
    {44, "PCDR4"},
    {45, "PCDR5"},
    {46, "PCDR6"},
    {47, "PCDR7"},
    {80, "CR0"},
    {81, "CR1"},
    {82, "CR2"},
    {83, "CR3"},
    {84, "CR4"},
    {90, "DR0"},
    {91, "DR1"},
    {92, "DR2"},
    {93, "DR3"},
    {94, "DR4"},
    {95, "DR5"},
    {96, "DR6"},
    {97, "DR7"},
    {110, "GDTR"},
    {111, "GDTL"},
    {112, "IDTR"},
    {113, "IDTL"},
    {114, "LDTR"},
    {115, "TR"},
    {116, "PSEUDO1"},
    {117, "PSEUDO2"},
    {118, "PSEUDO3"},
    {119, "PSEUDO4"},
    {120, "PSEUDO5"},
    {121, "PSEUDO6"},
    {122, "PSEUDO7"},
    {123, "PSEUDO8"},
    {124, "PSEUDO9"},
    {128, "ST0"},
    {129, "ST1"},
    {130, "ST2"},
    {131, "ST3"},
    {132, "ST4"},
    {133, "ST5"},
    {134, "ST6"},
    {135, "ST7"},
    {136, "CTRL"},
    {137, "STAT"},
    {138, "TAG"},
    {139, "FPIP"},
    {140, "FPCS"},
    {141, "FPDO"},
    {142, "FPDS"},
    {143, "ISEM"},
    {144, "FPEIP"},
    {145, "FPEDO"},
    {146, "MM0"},
    {147, "MM1"},
    {148, "MM2"},
    {149, "MM3"},
    {150, "MM4"},
    {151, "MM5"},
    {152, "MM6"},
    {153, "MM7"},
    {154, "XMM0"},
    {155, "XMM1"},
    {156, "XMM2"},
    {157, "XMM3"},
    {158, "XMM4"},
    {159, "XMM5"},
    {160, "XMM6"},
    {161, "XMM7"},
    {211, "MXCSR"},
    {212, "EDXEAX"},
    {220, "EMM0L"},
    {221, "EMM1L"},
    {222, "EMM2L"},
    {223, "EMM3L"},
    {224, "EMM4L"},
    {225, "EMM5L"},
    {226, "EMM6L"},
    {227, "EMM7L"},
    {228, "EMM0H"},
    {229, "EMM1H"},
    {230, "EMM2H"},
    {231, "EMM3H"},
    {232, "EMM4H"},
    {233, "EMM5H"},
    {234, "EMM6H"},
    {235, "EMM7H"},
    {236, "MM00"},
    {237, "MM01"},
    {238, "MM10"},
    {239, "MM11"},
    {240, "MM20"},
    {241, "MM21"},
    {242, "MM30"},
    {243, "MM31"},
    {244, "MM40"},
    {245, "MM41"},
    {246, "MM50"},
    {247, "MM51"},
    {248, "MM60"},
    {249, "MM61"},
    {250, "MM70"},
    {251, "MM71"},
    {252, "XMM8"},
    {253, "XMM9"},
    {254, "XMM10"},
    {255, "XMM11"},
    {256, "XMM12"},
    {257, "XMM13"},
    {258, "XMM14"},
    {259, "XMM15"},
    {324, "SIL"},
    {325, "DIL"},
    {326, "BPL"},
    {327, "SPL"},
    {328, "RAX"},
    {329, "RBX"},
    {330, "RCX"},
    {331, "RDX"},
    {332, "RSI"},
    {333, "RDI"},
    {334, "RBP"},
    {335, "RSP"},
    {336, "R8"},
    {337, "R9"},
    {338, "R10"},
    {339, "R11"},
    {340, "R12"},
    {341, "R13"},
    {342, "R14"},
    {343, "R15"},
    {344, "R8B"},
    {345, "R9B"},
    {346, "R10B"},
    {347, "R11B"},
    {348, "R12B"},
    {349, "R13B"},
    {350, "R14B"},
    {351, "R15B"},
    {352, "R8W"},
    {353, "R9W"},
    {354, "R10W"},
    {355, "R11W"},
    {356, "R12W"},
    {357, "R13W"},
    {358, "R14W"},
    {359, "R15W"},
    {360, "R8D"},
    {361, "R9D"},
    {362, "R10D"},
    {363, "R11D"},
    {364, "R12D"},
    {365, "R13D"},
    {366, "R14D"},
    {367, "R15D"},
    {368, "AMD64_YMM0"},
    {369, "AMD64_YMM1"},
    {370, "AMD64_YMM2"},
    {371, "AMD64_YMM3"},
    {372, "AMD64_YMM4"},
    {373, "AMD64_YMM5"},
    {374, "AMD64_YMM6"},
    {375, "AMD64_YMM7"},
    {376, "AMD64_YMM8"},
    {377, "AMD64_YMM9"},
    {378, "AMD64_YMM10"},
    {379, "AMD64_YMM11"},
    {380, "AMD64_YMM12"},
    {381, "AMD64_YMM13"},
    {382, "AMD64_YMM14"},
    {383, "AMD64_YMM15"},
    {396, "BND0"},
    {397, "BND1"},
    {398, "BND2"},
    {694, "AMD64_XMM16"},
    {695, "AMD64_XMM17"},
    {696, "AMD64_XMM18"},
    {697, "AMD64_XMM19"},
    {698, "AMD64_XMM20"},
    {699, "AMD64_XMM21"},
    {700, "AMD64_XMM22"},
    {701, "AMD64_XMM23"},
    {702, "AMD64_XMM24"},
    {703, "AMD64_XMM25"},
    {704, "AMD64_XMM26"},
    {705, "AMD64_XMM27"},
    {706, "AMD64_XMM28"},
    {707, "AMD64_XMM29"},
    {708, "AMD64_XMM30"},
    {709, "AMD64_XMM31"},
    {710, "AMD64_YMM16"},
    {711, "AMD64_YMM17"},
    {712, "AMD64_YMM18"},
    {713, "AMD64_YMM19"},
    {714, "AMD64_YMM20"},
    {715, "AMD64_YMM21"},
    {716, "AMD64_YMM22"},
    {717, "AMD64_YMM23"},
    {718, "AMD64_YMM24"},
    {719, "AMD64_YMM25"},
    {720, "AMD64_YMM26"},
    {721, "AMD64_YMM27"},
    {722, "AMD64_YMM28"},
    {723, "AMD64_YMM29"},
    {724, "AMD64_YMM30"},
    {725, "AMD64_YMM31"},
    {726, "AMD64_ZMM0"},
    {727, "AMD64_ZMM1"},
    {728, "AMD64_ZMM2"},
    {729, "AMD64_ZMM3"},
    {730, "AMD64_ZMM4"},
    {731, "AMD64_ZMM5"},
    {732, "AMD64_ZMM6"},
    {733, "AMD64_ZMM7"},
    {734, "AMD64_ZMM8"},
    {735, "AMD64_ZMM9"},
    {736, "AMD64_ZMM10"},
    {737, "AMD64_ZMM11"},
    {738, "AMD64_ZMM12"},
    {739, "AMD64_ZMM13"},
    {740, "AMD64_ZMM14"},
    {741, "AMD64_ZMM15"},
    {742, "AMD64_ZMM16"},
    {743, "AMD64_ZMM17"},
    {744, "AMD64_ZMM18"},
    {745, "AMD64_ZMM19"},
    {746, "AMD64_ZMM20"},
    {747, "AMD64_ZMM21"},
    {748, "AMD64_ZMM22"},
    {749, "AMD64_ZMM23"},
    {750, "AMD64_ZMM24"},
    {751, "AMD64_ZMM25"},
    {752, "AMD64_ZMM26"},
    {753, "AMD64_ZMM27"},
    {754, "AMD64_ZMM28"},
    {755, "AMD64_ZMM29"},
    {756, "AMD64_ZMM30"},
    {757, "AMD64_ZMM31"},
    {758, "AMD64_K0"},
    {759, "AMD64_K1"},
    {760, "AMD64_K2"},
    {761, "AMD64_K3"},
    {762, "AMD64_K4"},
    {763, "AMD64_K5"},
    {764, "AMD64_K6"},
    {765, "AMD64_K7"},
    {30001, "TEB"},
    {30002, "TIMER"},
    {30003, "EFAD1"},
    {30004, "EFAD2"},
    {30005, "EFAD3"},
    {30006, "VFRAME"},
    {30007, "HANDLE"},
    {30008, "PARAMS"},
    {30009, "LOCALS"},
    {30010, "TID"},
    {30011, "ENV"},
    {30012, "CMDLN"},
};

/* ARM / Thumb (CV_CFL_ARM3..ARM7, THUMB, ARMNT): CV_HREG_e CV_ARM_* of cvconst.h */
static const syms_reg_entry syms_regs_arm[173] = {
    {0, "NONE"},      {10, "R0"},       {11, "R1"},       {12, "R2"},       {13, "R3"},       {14, "R4"},       {15, "R5"},       {16, "R6"},     {17, "R7"},
    {18, "R8"},       {19, "R9"},       {20, "R10"},      {21, "R11"},      {22, "R12"},      {23, "SP"},       {24, "LR"},       {25, "PC"},     {26, "CPSR"},
    {27, "ACC0"},     {40, "FPSCR"},    {41, "FPEXC"},    {50, "FS0"},      {51, "FS1"},      {52, "FS2"},      {53, "FS3"},      {54, "FS4"},    {55, "FS5"},
    {56, "FS6"},      {57, "FS7"},      {58, "FS8"},      {59, "FS9"},      {60, "FS10"},     {61, "FS11"},     {62, "FS12"},     {63, "FS13"},   {64, "FS14"},
    {65, "FS15"},     {66, "FS16"},     {67, "FS17"},     {68, "FS18"},     {69, "FS19"},     {70, "FS20"},     {71, "FS21"},     {72, "FS22"},   {73, "FS23"},
    {74, "FS24"},     {75, "FS25"},     {76, "FS26"},     {77, "FS27"},     {78, "FS28"},     {79, "FS29"},     {80, "FS30"},     {81, "FS31"},   {90, "FPEXTRA0"},
    {91, "FPEXTRA1"}, {92, "FPEXTRA2"}, {93, "FPEXTRA3"}, {94, "FPEXTRA4"}, {95, "FPEXTRA5"}, {96, "FPEXTRA6"}, {97, "FPEXTRA7"}, {128, "WR0"},   {129, "WR1"},
    {130, "WR2"},     {131, "WR3"},     {132, "WR4"},     {133, "WR5"},     {134, "WR6"},     {135, "WR7"},     {136, "WR8"},     {137, "WR9"},   {138, "WR10"},
    {139, "WR11"},    {140, "WR12"},    {141, "WR13"},    {142, "WR14"},    {143, "WR15"},    {144, "WCID"},    {145, "WCON"},    {146, "WCSSF"}, {147, "WCASF"},
    {148, "WC4"},     {149, "WC5"},     {150, "WC6"},     {151, "WC7"},     {152, "WCGR0"},   {153, "WCGR1"},   {154, "WCGR2"},   {155, "WCGR3"}, {156, "WC12"},
    {157, "WC13"},    {158, "WC14"},    {159, "WC15"},    {200, "FS32"},    {201, "FS33"},    {202, "FS34"},    {203, "FS35"},    {204, "FS36"},  {205, "FS37"},
    {206, "FS38"},    {207, "FS39"},    {208, "FS40"},    {209, "FS41"},    {210, "FS42"},    {211, "FS43"},    {212, "FS44"},    {213, "FS45"},  {214, "FS46"},
    {215, "FS47"},    {216, "FS48"},    {217, "FS49"},    {218, "FS50"},    {219, "FS51"},    {220, "FS52"},    {221, "FS53"},    {222, "FS54"},  {223, "FS55"},
    {224, "FS56"},    {225, "FS57"},    {226, "FS58"},    {227, "FS59"},    {228, "FS60"},    {229, "FS61"},    {230, "FS62"},    {231, "FS63"},  {300, "ND0"},
    {301, "ND1"},     {302, "ND2"},     {303, "ND3"},     {304, "ND4"},     {305, "ND5"},     {306, "ND6"},     {307, "ND7"},     {308, "ND8"},   {309, "ND9"},
    {310, "ND10"},    {311, "ND11"},    {312, "ND12"},    {313, "ND13"},    {314, "ND14"},    {315, "ND15"},    {316, "ND16"},    {317, "ND17"},  {318, "ND18"},
    {319, "ND19"},    {320, "ND20"},    {321, "ND21"},    {322, "ND22"},    {323, "ND23"},    {324, "ND24"},    {325, "ND25"},    {326, "ND26"},  {327, "ND27"},
    {328, "ND28"},    {329, "ND29"},    {330, "ND30"},    {331, "ND31"},    {400, "NQ0"},     {401, "NQ1"},     {402, "NQ2"},     {403, "NQ3"},   {404, "NQ4"},
    {405, "NQ5"},     {406, "NQ6"},     {407, "NQ7"},     {408, "NQ8"},     {409, "NQ9"},     {410, "NQ10"},    {411, "NQ11"},    {412, "NQ12"},  {413, "NQ13"},
    {414, "NQ14"},    {415, "NQ15"},
};

static const syms_reg_entry syms_regs_arm64[229] = {
    {0, "ARM64_NOREG"}, {10, "ARM64_W0"},   {11, "ARM64_W1"},   {12, "ARM64_W2"},    {13, "ARM64_W3"},    {14, "ARM64_W4"},   {15, "ARM64_W5"},   {16, "ARM64_W6"},
    {17, "ARM64_W7"},   {18, "ARM64_W8"},   {19, "ARM64_W9"},   {20, "ARM64_W10"},   {21, "ARM64_W11"},   {22, "ARM64_W12"},  {23, "ARM64_W13"},  {24, "ARM64_W14"},
    {25, "ARM64_W15"},  {26, "ARM64_W16"},  {27, "ARM64_W17"},  {28, "ARM64_W18"},   {29, "ARM64_W19"},   {30, "ARM64_W20"},  {31, "ARM64_W21"},  {32, "ARM64_W22"},
    {33, "ARM64_W23"},  {34, "ARM64_W24"},  {35, "ARM64_W25"},  {36, "ARM64_W26"},   {37, "ARM64_W27"},   {38, "ARM64_W28"},  {39, "ARM64_W29"},  {40, "ARM64_W30"},
    {41, "ARM64_WZR"},  {50, "ARM64_X0"},   {51, "ARM64_X1"},   {52, "ARM64_X2"},    {53, "ARM64_X3"},    {54, "ARM64_X4"},   {55, "ARM64_X5"},   {56, "ARM64_X6"},
    {57, "ARM64_X7"},   {58, "ARM64_X8"},   {59, "ARM64_X9"},   {60, "ARM64_X10"},   {61, "ARM64_X11"},   {62, "ARM64_X12"},  {63, "ARM64_X13"},  {64, "ARM64_X14"},
    {65, "ARM64_X15"},  {66, "ARM64_X16"},  {67, "ARM64_X17"},  {68, "ARM64_X18"},   {69, "ARM64_X19"},   {70, "ARM64_X20"},  {71, "ARM64_X21"},  {72, "ARM64_X22"},
    {73, "ARM64_X23"},  {74, "ARM64_X24"},  {75, "ARM64_X25"},  {76, "ARM64_X26"},   {77, "ARM64_X27"},   {78, "ARM64_X28"},  {79, "ARM64_FP"},   {80, "ARM64_LR"},
    {81, "ARM64_SP"},   {82, "ARM64_ZR"},   {90, "ARM64_NZCV"}, {100, "ARM64_S0"},   {101, "ARM64_S1"},   {102, "ARM64_S2"},  {103, "ARM64_S3"},  {104, "ARM64_S4"},
    {105, "ARM64_S5"},  {106, "ARM64_S6"},  {107, "ARM64_S7"},  {108, "ARM64_S8"},   {109, "ARM64_S9"},   {110, "ARM64_S10"}, {111, "ARM64_S11"}, {112, "ARM64_S12"},
    {113, "ARM64_S13"}, {114, "ARM64_S14"}, {115, "ARM64_S15"}, {116, "ARM64_S16"},  {117, "ARM64_S17"},  {118, "ARM64_S18"}, {119, "ARM64_S19"}, {120, "ARM64_S20"},
    {121, "ARM64_S21"}, {122, "ARM64_S22"}, {123, "ARM64_S23"}, {124, "ARM64_S24"},  {125, "ARM64_S25"},  {126, "ARM64_S26"}, {127, "ARM64_S27"}, {128, "ARM64_S28"},
    {129, "ARM64_S29"}, {130, "ARM64_S30"}, {131, "ARM64_S31"}, {140, "ARM64_D0"},   {141, "ARM64_D1"},   {142, "ARM64_D2"},  {143, "ARM64_D3"},  {144, "ARM64_D4"},
    {145, "ARM64_D5"},  {146, "ARM64_D6"},  {147, "ARM64_D7"},  {148, "ARM64_D8"},   {149, "ARM64_D9"},   {150, "ARM64_D10"}, {151, "ARM64_D11"}, {152, "ARM64_D12"},
    {153, "ARM64_D13"}, {154, "ARM64_D14"}, {155, "ARM64_D15"}, {156, "ARM64_D16"},  {157, "ARM64_D17"},  {158, "ARM64_D18"}, {159, "ARM64_D19"}, {160, "ARM64_D20"},
    {161, "ARM64_D21"}, {162, "ARM64_D22"}, {163, "ARM64_D23"}, {164, "ARM64_D24"},  {165, "ARM64_D25"},  {166, "ARM64_D26"}, {167, "ARM64_D27"}, {168, "ARM64_D28"},
    {169, "ARM64_D29"}, {170, "ARM64_D30"}, {171, "ARM64_D31"}, {180, "ARM64_Q0"},   {181, "ARM64_Q1"},   {182, "ARM64_Q2"},  {183, "ARM64_Q3"},  {184, "ARM64_Q4"},
    {185, "ARM64_Q5"},  {186, "ARM64_Q6"},  {187, "ARM64_Q7"},  {188, "ARM64_Q8"},   {189, "ARM64_Q9"},   {190, "ARM64_Q10"}, {191, "ARM64_Q11"}, {192, "ARM64_Q12"},
    {193, "ARM64_Q13"}, {194, "ARM64_Q14"}, {195, "ARM64_Q15"}, {196, "ARM64_Q16"},  {197, "ARM64_Q17"},  {198, "ARM64_Q18"}, {199, "ARM64_Q19"}, {200, "ARM64_Q20"},
    {201, "ARM64_Q21"}, {202, "ARM64_Q22"}, {203, "ARM64_Q23"}, {204, "ARM64_Q24"},  {205, "ARM64_Q25"},  {206, "ARM64_Q26"}, {207, "ARM64_Q27"}, {208, "ARM64_Q28"},
    {209, "ARM64_Q29"}, {210, "ARM64_Q30"}, {211, "ARM64_Q31"}, {220, "ARM64_FPSR"}, {221, "ARM64_FPCR"}, {230, "ARM64_B0"},  {231, "ARM64_B1"},  {232, "ARM64_B2"},
    {233, "ARM64_B3"},  {234, "ARM64_B4"},  {235, "ARM64_B5"},  {236, "ARM64_B6"},   {237, "ARM64_B7"},   {238, "ARM64_B8"},  {239, "ARM64_B9"},  {240, "ARM64_B10"},
    {241, "ARM64_B11"}, {242, "ARM64_B12"}, {243, "ARM64_B13"}, {244, "ARM64_B14"},  {245, "ARM64_B15"},  {246, "ARM64_B16"}, {247, "ARM64_B17"}, {248, "ARM64_B18"},
    {249, "ARM64_B19"}, {250, "ARM64_B20"}, {251, "ARM64_B21"}, {252, "ARM64_B22"},  {253, "ARM64_B23"},  {254, "ARM64_B24"}, {255, "ARM64_B25"}, {256, "ARM64_B26"},
    {257, "ARM64_B27"}, {258, "ARM64_B28"}, {259, "ARM64_B29"}, {260, "ARM64_B30"},  {261, "ARM64_B31"},  {270, "ARM64_H0"},  {271, "ARM64_H1"},  {272, "ARM64_H2"},
    {273, "ARM64_H3"},  {274, "ARM64_H4"},  {275, "ARM64_H5"},  {276, "ARM64_H6"},   {277, "ARM64_H7"},   {278, "ARM64_H8"},  {279, "ARM64_H9"},  {280, "ARM64_H10"},
    {281, "ARM64_H11"}, {282, "ARM64_H12"}, {283, "ARM64_H13"}, {284, "ARM64_H14"},  {285, "ARM64_H15"},  {286, "ARM64_H16"}, {287, "ARM64_H17"}, {288, "ARM64_H18"},
    {289, "ARM64_H19"}, {290, "ARM64_H20"}, {291, "ARM64_H21"}, {292, "ARM64_H22"},  {293, "ARM64_H23"},  {294, "ARM64_H24"}, {295, "ARM64_H25"}, {296, "ARM64_H26"},
    {297, "ARM64_H27"}, {298, "ARM64_H28"}, {299, "ARM64_H29"}, {300, "ARM64_H30"},  {301, "ARM64_H31"},
};

static const syms_reg_entry syms_cpu_table[62] = {
    {0, "intel 8080"},
    {1, "intel 8086"},
    {2, "intel 80286"},
    {3, "intel 80386"},
    {4, "intel 80486"},
    {5, "intel pentium"},
    {6, "intel pentium pro"},
    {7, "intel pentium 3"},
    {16, "mips"},
    {17, "mips-16"},
    {18, "mips-32"},
    {19, "mips-64"},
    {20, "mips i"},
    {21, "mips ii"},
    {22, "mips iii"},
    {23, "mips iv"},
    {24, "mips v"},
    {32, "motorola 68000"},
    {33, "motorola 68010"},
    {34, "motorola 68020"},
    {35, "motorola 68030"},
    {36, "motorola 68040"},
    {48, "alpha"},
    {49, "alpha 21164"},
    {50, "alpha 21164a"},
    {51, "alpha 21264"},
    {52, "alpha 21364"},
    {64, "powerpc 601"},
    {65, "powerpc 603"},
    {66, "powerpc 604"},
    {67, "powerpc 620"},
    {68, "powerpc fp"},
    {69, "powerpc be"},
    {80, "sh3"},
    {81, "sh3e"},
    {82, "sh3 dsp"},
    {83, "sh4"},
    {84, "shmedia"},
    {96, "arm 3"},
    {97, "arm 4"},
    {98, "arm 4t"},
    {99, "arm 5"},
    {100, "arm 5t"},
    {101, "arm 6"},
    {102, "arm xmac"},
    {103, "arm wmmx"},
    {104, "arm 7"},
    {112, "omni"},
    {128, "intel itanium ia64"},
    {129, "intel itanium ia64 2"},
    {144, "cee"},
    {160, "am33"},
    {176, "m32r"},
    {192, "tri-core"},
    {208, "intel x86-x64"},
    {224, "ebc"},
    {240, "thumb"},
    {244, "arm nt"},
    {246, "arm64"},
    {248, "arm64ec"},
    {249, "arm64x"},
    {255, "unknown"},
};

static const syms_reg_entry syms_lang_table[25] = {
    {0, "c"},       {1, "c++"},    {2, "fortran"},   {3, "masm"},    {4, "pascal"}, {5, "basic"},       {6, "cobol"},  {7, "link"},  {8, "cvtres"},
    {9, "cvtpgd"},  {10, "c#"},    {11, "vb"},       {12, "il asm"}, {13, "java"},  {14, "javascript"}, {15, "msil"},  {16, "hlsl"}, {17, "objc"},
    {18, "objc++"}, {19, "swift"}, {20, "aliasobj"}, {21, "rust"},   {22, "go"},    {68, "d"},          {83, "swift"},
};

/* ---- little endian bounds-checked helpers ---------------------------- */

static inline uint8_t syms_u8(const uint8_t *p)
{
    return p[0];
}

static inline uint16_t syms_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t syms_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t syms_u64(const uint8_t *p)
{
    return (uint64_t)syms_u32(p) | ((uint64_t)syms_u32(p + 4) << 32);
}

static inline int32_t syms_i32(const uint8_t *p)
{
    return (int32_t)syms_u32(p);
}

static inline int64_t syms_i64(const uint8_t *p)
{
    return (int64_t)syms_u64(p);
}

/* Safely extracts a NUL-terminated string within max_len bytes */
static const char *syms_str(const uint8_t *p, size_t max_len)
{
    if (!p || max_len == 0) return "";
    /* The string is inside a buffer that ends with 0, but check bounds */
    size_t len = 0;
    while (len < max_len && p[len] != 0) {
        len++;
    }
    return (const char *)p;
}

/* Numeric leaf decoding as in XPDB / xx_microsoft_pdb.c */
static int32_t syms_read_numeric_leaf(const uint8_t *data, size_t max_len, int64_t *val)
{
    *val = 0;
    if (max_len < 2) return 0;
    uint16_t leaf = syms_u16(data);
    if (leaf < XX_MS_PDB_LF_NUMERIC) {
        *val = leaf;
        return 2;
    } else if (leaf == XX_MS_PDB_LF_CHAR) {
        if (max_len < 3) return 2;
        *val = (int8_t)syms_u8(data + 2);
        return 3;
    } else if (leaf == XX_MS_PDB_LF_SHORT) {
        if (max_len < 4) return 2;
        *val = (int16_t)syms_u16(data + 2);
        return 4;
    } else if (leaf == XX_MS_PDB_LF_USHORT) {
        if (max_len < 4) return 2;
        *val = syms_u16(data + 2);
        return 4;
    } else if (leaf == XX_MS_PDB_LF_LONG) {
        if (max_len < 6) return 2;
        *val = syms_i32(data + 2);
        return 6;
    } else if (leaf == XX_MS_PDB_LF_ULONG) {
        if (max_len < 6) return 2;
        *val = (int64_t)syms_u32(data + 2);
        return 6;
    } else if (leaf == XX_MS_PDB_LF_QUADWORD) {
        if (max_len < 10) return 2;
        *val = syms_i64(data + 2);
        return 10;
    } else if (leaf == XX_MS_PDB_LF_UQUADWORD) {
        if (max_len < 10) return 2;
        *val = syms_i64(data + 2);
        return 10;
    }
    return 2;
}

/* Compressed uint reader for binary annotations */
static uint32_t syms_read_compressed_uint(const uint8_t *data, size_t size, size_t *pos)
{
    if (*pos >= size) return 0;
    uint8_t b1 = data[(*pos)++];
    if ((b1 & 0x80) == 0) {
        return b1;
    }
    if ((b1 & 0xC0) == 0x80) {
        if (*pos >= size) return 0;
        uint8_t b2 = data[(*pos)++];
        return ((uint32_t)(b1 & 0x3F) << 8) | b2;
    }
    if ((b1 & 0xE0) == 0xC0) {
        if (*pos + 3 > size) {
            *pos = size;
            return 0;
        }
        uint8_t b2 = data[(*pos)++];
        uint8_t b3 = data[(*pos)++];
        uint8_t b4 = data[(*pos)++];
        return ((uint32_t)(b1 & 0x1F) << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 8) | b4;
    }
    return 0;
}

static inline int32_t syms_decode_signed(uint32_t v)
{
    return (v & 1) ? -(int32_t)(v >> 1) : (int32_t)(v >> 1);
}

/* ---- string table lookups -------------------------------------------- */

const char *xx_ms_pdb_language_to_string(uint32_t language)
{
    size_t count = sizeof(syms_lang_table) / sizeof(syms_lang_table[0]);
    for (size_t i = 0; i < count; i++) {
        if (syms_lang_table[i].id == (uint16_t)language) {
            return syms_lang_table[i].name;
        }
    }
    return "Unknown";
}

const char *xx_ms_pdb_cpu_to_string(uint16_t cpu)
{
    size_t count = sizeof(syms_cpu_table) / sizeof(syms_cpu_table[0]);
    for (size_t i = 0; i < count; i++) {
        if (syms_cpu_table[i].id == cpu) {
            return syms_cpu_table[i].name;
        }
    }
    return "Unknown";
}

const char *xx_ms_pdb_register_to_string(uint16_t cpu, uint16_t reg)
{
    if (cpu == 0xF6 || cpu == 0xF8 || cpu == 0xF9 || cpu == 0xAA64 || cpu == 0xA641 || cpu == 0xA64E) {
        /* ARM64 */
        size_t count = sizeof(syms_regs_arm64) / sizeof(syms_regs_arm64[0]);
        for (size_t i = 0; i < count; i++) {
            if (syms_regs_arm64[i].id == reg) {
                return syms_regs_arm64[i].name;
            }
        }
    } else if ((cpu >= 0x60 && cpu <= 0x6F) || cpu == 0xF0 || cpu == 0xF4 || cpu == 0x01C0 || cpu == 0x01C2 || cpu == 0x01C4) {
        /* ARM / Thumb */
        size_t count = sizeof(syms_regs_arm) / sizeof(syms_regs_arm[0]);
        for (size_t i = 0; i < count; i++) {
            if (syms_regs_arm[i].id == reg) {
                return syms_regs_arm[i].name;
            }
        }
    } else {
        /* x86 and x64 */
        size_t count = sizeof(syms_regs_x86) / sizeof(syms_regs_x86[0]);
        for (size_t i = 0; i < count; i++) {
            if (syms_regs_x86[i].id == reg) {
                return syms_regs_x86[i].name;
            }
        }
    }
    return "Unknown";
}

/* ---- S_BUILDINFO resolution helper ----------------------------------- */

static const char *syms_resolve_string_id(xx_ms_pdb *pdb, xx_arena_t *arena, uint32_t id_index)
{
    if (id_index == 0) return "";
    xx_ms_pdb_id_record rec = {0};
    if (!xx_ms_pdb_get_id_by_index(pdb, id_index, &rec) || !rec.is_valid) {
        return "";
    }
    if (rec.list_index == 0) {
        return rec.name ? xx_arena_strdup(arena, rec.name) : "";
    }

    xx_ms_pdb_u32_list sub_list = {0};
    if (!xx_ms_pdb_get_id_list(pdb, rec.list_index, &sub_list)) {
        return rec.name ? xx_arena_strdup(arena, rec.name) : "";
    }

    size_t total_len = 0;
    for (size_t i = 0; i < sub_list.count; i++) {
        xx_ms_pdb_id_record sub_rec = {0};
        if (xx_ms_pdb_get_id_by_index(pdb, sub_list.items[i], &sub_rec) && sub_rec.is_valid && sub_rec.name) {
            total_len += strlen(sub_rec.name);
        }
    }
    if (rec.name) total_len += strlen(rec.name);

    char *buf = (char *)xx_arena_alloc(arena, total_len + 1);
    if (!buf) {
        xx_ms_pdb_u32_list_free(&sub_list);
        return "";
    }
    buf[0] = '\0';
    for (size_t i = 0; i < sub_list.count; i++) {
        xx_ms_pdb_id_record sub_rec = {0};
        if (xx_ms_pdb_get_id_by_index(pdb, sub_list.items[i], &sub_rec) && sub_rec.is_valid && sub_rec.name) {
            strcat(buf, sub_rec.name);
        }
    }
    if (rec.name) strcat(buf, rec.name);
    xx_ms_pdb_u32_list_free(&sub_list);
    return buf;
}

static void syms_resolve_build_info(xx_ms_pdb *pdb, xx_arena_t *arena, uint32_t build_id, xx_ms_pdb_compile_info *out)
{
    xx_ms_pdb_u32_list list = {0};
    if (!xx_ms_pdb_get_id_list(pdb, build_id, &list)) return;

    for (size_t i = 0; i < list.count && i < 5; i++) {
        uint32_t arg_id = list.items[i];
        const char *resolved = syms_resolve_string_id(pdb, arena, arg_id);
        switch (i) {
            case 0: out->build_cwd = resolved; break;
            case 1: out->build_tool = resolved; break;
            case 2: out->build_source = resolved; break;
            case 3: out->build_pdb = resolved; break;
            case 4: out->build_command_line = resolved; break;
        }
    }
    xx_ms_pdb_u32_list_free(&list);
}

/* ---- xx_ms_pdb_get_module_compile_info ------------------------------- */

bool xx_ms_pdb_get_module_compile_info(xx_ms_pdb *pdb, int32_t module_index, xx_ms_pdb_compile_info *out)
{
    if (!pdb || !out) return false;
    memset(out, 0, sizeof(*out));
    out->module_index = module_index;
    out->version = "";
    out->extra_strings = "";
    out->object_name = "";
    out->build_cwd = "";
    out->build_tool = "";
    out->build_source = "";
    out->build_pdb = "";
    out->build_command_line = "";

    xx_arena_t *arena = (xx_arena_t *)xx_mem_alloc(sizeof(xx_arena_t));
    if (!arena) return false;
    xx_arena_init(arena);
    out->internal_ = arena;

    xx_ms_pdb_module_list mods = {0};
    if (!xx_ms_pdb_get_modules(pdb, &mods)) {
        xx_ms_pdb_compile_info_free(out);
        return false;
    }
    if (module_index < 0 || (size_t)module_index >= mods.count) {
        xx_ms_pdb_module_list_free(&mods);
        xx_ms_pdb_compile_info_free(out);
        return false;
    }

    const xx_ms_pdb_module *mod = &mods.items[module_index];
    if (mod->stream_index == 0xFFFF || mod->sym_byte_size <= 4) {
        xx_ms_pdb_module_list_free(&mods);
        return true;
    }

    uint32_t sym_size = mod->sym_byte_size;
    if (sym_size > SYMS_MAX_STREAM_SIZE) sym_size = SYMS_MAX_STREAM_SIZE;

    uint8_t *data = (uint8_t *)xx_mem_alloc(sym_size + 4);
    if (!data) {
        xx_ms_pdb_module_list_free(&mods);
        xx_ms_pdb_compile_info_free(out);
        return false;
    }

    int64_t bytes_read = xx_ms_pdb_read_stream(pdb, mod->stream_index, 0, data, sym_size);
    if (bytes_read != (int64_t)sym_size) {
        xx_mem_free(data);
        xx_ms_pdb_module_list_free(&mods);
        return true;
    }
    data[sym_size] = 0;
    data[sym_size + 1] = 0;
    xx_ms_pdb_module_list_free(&mods);

    /* Temporary environment pairs buffer */
    xx_ms_pdb_env_pair *env_buf = NULL;
    size_t env_cap = 0;
    size_t env_count = 0;

    uint32_t off = 4;
    while (off + 4 <= sym_size) {
        uint16_t reclen = syms_u16(data + off);
        uint16_t kind = syms_u16(data + off + 2);
        if (reclen < 2) break;
        uint32_t next_off = off + reclen + 2;
        if (next_off > sym_size) break;

        const uint8_t *rec = data + off;
        size_t rec_len = reclen;

        if (kind == XX_MS_PDB_S_OBJNAME && rec_len >= 6) {
            out->has_objname = true;
            out->object_signature = syms_u32(rec + 4);
            out->object_name = xx_arena_strdup(arena, syms_str(rec + 8, rec_len - 6));
        } else if (kind == XX_MS_PDB_S_OBJNAME_ST && rec_len >= 7) {
            out->has_objname = true;
            out->object_signature = syms_u32(rec + 4);
            uint8_t st_len = syms_u8(rec + 8);
            out->object_name = xx_arena_strndup(arena, (const char *)(rec + 9), st_len);
        } else if (kind == XX_MS_PDB_S_COMPILE3 && rec_len >= 26) {
            out->has_compile = true;
            out->compile_kind = XX_MS_PDB_S_COMPILE3;
            uint32_t raw_flags = syms_u32(rec + 4);
            out->language = raw_flags & 0xFF;
            out->flags = (raw_flags >> 8) & 0xFFFFFF;
            out->ec = (out->flags & (1 << 0)) != 0;
            out->no_dbg_info = (out->flags & (1 << 1)) != 0;
            out->ltcg = (out->flags & (1 << 2)) != 0;
            out->no_data_align = (out->flags & (1 << 3)) != 0;
            out->managed_present = (out->flags & (1 << 4)) != 0;
            out->security_checks = (out->flags & (1 << 5)) != 0;
            out->hot_patch = (out->flags & (1 << 6)) != 0;
            out->cvtcil = (out->flags & (1 << 7)) != 0;
            out->msil_module = (out->flags & (1 << 8)) != 0;
            out->sdl = (out->flags & (1 << 9)) != 0;
            out->pgo = (out->flags & (1 << 10)) != 0;
            out->exp_module = (out->flags & (1 << 11)) != 0;
            out->machine = syms_u16(rec + 8);
            out->fe_major = syms_u16(rec + 10);
            out->fe_minor = syms_u16(rec + 12);
            out->fe_build = syms_u16(rec + 14);
            out->fe_qfe = syms_u16(rec + 16);
            out->be_major = syms_u16(rec + 18);
            out->be_minor = syms_u16(rec + 20);
            out->be_build = syms_u16(rec + 22);
            out->be_qfe = syms_u16(rec + 24);
            out->version = xx_arena_strdup(arena, syms_str(rec + 26, rec_len - 24));
        } else if (kind == 0x1116 /* S_COMPILE2 */ && rec_len >= 22) {
            out->has_compile = true;
            out->compile_kind = 0x1116;
            uint32_t raw_flags = syms_u32(rec + 4);
            out->language = raw_flags & 0xFF;
            out->flags = (raw_flags >> 8) & 0xFFFFFF;
            out->ec = (out->flags & (1 << 0)) != 0;
            out->no_dbg_info = (out->flags & (1 << 1)) != 0;
            out->ltcg = (out->flags & (1 << 2)) != 0;
            out->no_data_align = (out->flags & (1 << 3)) != 0;
            out->managed_present = (out->flags & (1 << 4)) != 0;
            out->security_checks = (out->flags & (1 << 5)) != 0;
            out->hot_patch = (out->flags & (1 << 6)) != 0;
            out->cvtcil = (out->flags & (1 << 7)) != 0;
            out->msil_module = (out->flags & (1 << 8)) != 0;
            out->sdl = (out->flags & (1 << 9)) != 0;
            out->pgo = (out->flags & (1 << 10)) != 0;
            out->exp_module = (out->flags & (1 << 11)) != 0;
            out->machine = syms_u16(rec + 8);
            out->fe_major = syms_u16(rec + 10);
            out->fe_minor = syms_u16(rec + 12);
            out->fe_build = syms_u16(rec + 14);
            out->be_major = syms_u16(rec + 16);
            out->be_minor = syms_u16(rec + 18);
            out->be_build = syms_u16(rec + 20);
            const char *vstr = syms_str(rec + 22, rec_len - 20);
            out->version = xx_arena_strdup(arena, vstr);
            size_t vlen = strlen(vstr);
            size_t extra_pos = 22 + vlen + 1;
            /* Extra strings joined with ' ' */
            if (extra_pos < rec_len + 2) {
                size_t extra_bytes = (rec_len + 2) - extra_pos;
                char *joined = (char *)xx_arena_alloc(arena, extra_bytes + 1);
                if (joined) {
                    size_t jlen = 0;
                    size_t p = extra_pos;
                    while (p < rec_len + 2 && rec[p] != 0) {
                        const char *s = (const char *)(rec + p);
                        size_t slen = strlen(s);
                        if (jlen > 0) joined[jlen++] = ' ';
                        memcpy(joined + jlen, s, slen);
                        jlen += slen;
                        joined[jlen] = '\0';
                        p += slen + 1;
                    }
                    out->extra_strings = joined;
                }
            }
        } else if (kind == XX_MS_PDB_S_COMPILE && rec_len >= 8) {
            out->has_compile = true;
            out->compile_kind = XX_MS_PDB_S_COMPILE;
            out->machine = syms_u8(rec + 4);
            out->language = syms_u8(rec + 5);
            out->flags = (uint32_t)syms_u8(rec + 6) | ((uint32_t)syms_u8(rec + 7) << 8) | ((uint32_t)syms_u8(rec + 8) << 16);
            uint8_t st_len = syms_u8(rec + 9);
            out->version = xx_arena_strndup(arena, (const char *)(rec + 10), st_len);
        } else if (kind == XX_MS_PDB_S_ENVBLOCK && rec_len >= 3) {
            /* rec + 4 is flags (u8), then key \0 val \0 ... \0 */
            size_t p = 5;
            while (p < rec_len + 2 && rec[p] != 0) {
                const char *k = (const char *)(rec + p);
                size_t klen = strlen(k);
                p += klen + 1;
                if (p >= rec_len + 2) break;
                const char *v = (const char *)(rec + p);
                size_t vlen = strlen(v);
                p += vlen + 1;

                if (env_count >= env_cap) {
                    size_t new_cap = env_cap ? env_cap * 2 : 16;
                    xx_ms_pdb_env_pair *nb = (xx_ms_pdb_env_pair *)xx_mem_realloc(env_buf, new_cap * sizeof(xx_ms_pdb_env_pair));
                    if (nb) {
                        env_buf = nb;
                        env_cap = new_cap;
                    }
                }
                if (env_count < env_cap) {
                    env_buf[env_count].key = xx_arena_strdup(arena, k);
                    env_buf[env_count].value = xx_arena_strdup(arena, v);
                    env_count++;
                }
            }
        } else if (kind == XX_MS_PDB_S_BUILDINFO && rec_len >= 6) {
            out->has_build_info = true;
            out->build_info_id = syms_u32(rec + 4);
            syms_resolve_build_info(pdb, arena, out->build_info_id, out);
        }

        off = next_off;
    }

    if (env_count > 0 && env_buf) {
        out->env = (xx_ms_pdb_env_pair *)xx_arena_alloc_array(arena, env_count, sizeof(xx_ms_pdb_env_pair));
        if (out->env) {
            memcpy(out->env, env_buf, env_count * sizeof(xx_ms_pdb_env_pair));
            out->env_count = env_count;
        }
    }
    if (env_buf) xx_mem_free(env_buf);
    xx_mem_free(data);
    return true;
}

void xx_ms_pdb_compile_info_free(xx_ms_pdb_compile_info *info)
{
    if (!info) return;
    if (info->internal_) {
        xx_arena_t *arena = (xx_arena_t *)info->internal_;
        xx_arena_free(arena);
        xx_mem_free(arena);
    }
    memset(info, 0, sizeof(*info));
}

/* ---- procedure and symbol detail parsing ----------------------------- */

static void syms_parse_defrange(const xx_ms_pdb_addr_map *map, const uint8_t *rec, uint16_t rec_len, uint16_t kind, xx_ms_pdb_def_range *range)
{
    memset(range, 0, sizeof(*range));
    range->kind = kind;
    if (kind == 0x1144 /* S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE */) {
        range->offset = syms_i32(rec + 4);
        range->rva_start = XX_MS_PDB_NO_RVA;
        range->length = 0;
        range->gap_count = 0;
        range->reg = 0;
        return;
    }

    uint32_t addr_range_off = 0;
    if (kind == 0x1141 /* S_DEFRANGE_REGISTER */) {
        range->reg = syms_u16(rec + 4);
        range->offset = 0;
        addr_range_off = 8;
    } else if (kind == 0x1142 /* S_DEFRANGE_FRAMEPOINTER_REL */) {
        range->offset = syms_i32(rec + 4);
        range->reg = 0;
        addr_range_off = 8;
    } else if (kind == 0x1143 /* S_DEFRANGE_SUBFIELD_REGISTER */) {
        range->reg = syms_u16(rec + 4);
        range->offset = (int32_t)syms_u32(rec + 8);
        addr_range_off = 12;
    } else if (kind == 0x1145 /* S_DEFRANGE_REGISTER_REL */) {
        range->reg = syms_u16(rec + 4);
        range->offset = syms_i32(rec + 8);
        addr_range_off = 12;
    } else if (kind == 0x1140 /* S_DEFRANGE_SUBFIELD */) {
        range->offset = (int32_t)syms_u32(rec + 8);
        range->reg = 0;
        addr_range_off = 12;
    } else {
        /* 0x113F S_DEFRANGE */
        range->reg = 0;
        range->offset = 0;
        addr_range_off = 8;
    }

    if (addr_range_off + 8 <= (uint32_t)(rec_len + 2)) {
        uint32_t off = syms_u32(rec + addr_range_off);
        uint16_t seg = syms_u16(rec + addr_range_off + 4);
        uint16_t len = syms_u16(rec + addr_range_off + 6);
        range->length = len;
        range->rva_start = map ? xx_ms_pdb_addr_map_rva(map, seg, off) : XX_MS_PDB_NO_RVA;

        uint32_t fixed_end = addr_range_off + 8;
        if ((uint32_t)(rec_len + 2) >= fixed_end) {
            range->gap_count = (size_t)((rec_len + 2 - fixed_end) / 4);
        } else {
            range->gap_count = 0;
        }
    }
}

static void syms_decode_inline_annotations(xx_arena_t *arena, uint64_t proc_rva, const uint8_t *ann, size_t ann_size, xx_ms_pdb_inline_site *site)
{
    size_t pos = 0;
    uint32_t cur_code_offset = 0;
    int32_t cur_line_offset = 0;
    uint32_t cur_file_id = UINT32_MAX;

    xx_ms_pdb_inline_range *temp_ranges = NULL;
    size_t r_cap = 0;
    size_t r_count = 0;

    while (pos < ann_size) {
        uint32_t opcode = syms_read_compressed_uint(ann, ann_size, &pos);
        if (opcode == 0) break;

        switch (opcode) {
            case 1: /* CodeOffset */
            case 3: /* ChangeCodeOffset */ {
                uint32_t delta = syms_read_compressed_uint(ann, ann_size, &pos);
                cur_code_offset += delta;
                if (r_count >= r_cap) {
                    size_t nc = r_cap ? r_cap * 2 : 8;
                    xx_ms_pdb_inline_range *nb = (xx_ms_pdb_inline_range *)xx_mem_realloc(temp_ranges, nc * sizeof(xx_ms_pdb_inline_range));
                    if (nb) {
                        temp_ranges = nb;
                        r_cap = nc;
                    }
                }
                if (r_count < r_cap) {
                    temp_ranges[r_count].code_offset = cur_code_offset;
                    temp_ranges[r_count].line_offset = cur_line_offset;
                    temp_ranges[r_count].file_id = cur_file_id;
                    temp_ranges[r_count].length = 0;
                    temp_ranges[r_count].rva = (proc_rva != XX_MS_PDB_NO_RVA) ? (proc_rva + cur_code_offset) : XX_MS_PDB_NO_RVA;
                    r_count++;
                }
                break;
            }
            case 2: /* ChangeCodeOffsetBase */ {
                cur_code_offset = syms_read_compressed_uint(ann, ann_size, &pos);
                break;
            }
            case 4: /* ChangeCodeLength */ {
                uint32_t len = syms_read_compressed_uint(ann, ann_size, &pos);
                if (r_count > 0) {
                    temp_ranges[r_count - 1].length = len;
                }
                break;
            }
            case 5: /* ChangeFile */ {
                cur_file_id = syms_read_compressed_uint(ann, ann_size, &pos);
                break;
            }
            case 6: /* ChangeLineOffset */ {
                uint32_t v = syms_read_compressed_uint(ann, ann_size, &pos);
                cur_line_offset += syms_decode_signed(v);
                break;
            }
            case 7:  /* ChangeLineEndDelta */
            case 8:  /* ChangeRangeKind */
            case 9:  /* ChangeColumnStart */
            case 10: /* ChangeColumnEndDelta */
            case 13: /* ChangeColumnEnd */ {
                (void)syms_read_compressed_uint(ann, ann_size, &pos);
                break;
            }
            case 11: /* ChangeCodeOffsetAndLineOffset */ {
                uint32_t v = syms_read_compressed_uint(ann, ann_size, &pos);
                uint32_t code_delta = v & 0xF;
                int32_t line_delta = syms_decode_signed(v >> 4);
                cur_code_offset += code_delta;
                cur_line_offset += line_delta;
                if (r_count >= r_cap) {
                    size_t nc = r_cap ? r_cap * 2 : 8;
                    xx_ms_pdb_inline_range *nb = (xx_ms_pdb_inline_range *)xx_mem_realloc(temp_ranges, nc * sizeof(xx_ms_pdb_inline_range));
                    if (nb) {
                        temp_ranges = nb;
                        r_cap = nc;
                    }
                }
                if (r_count < r_cap) {
                    temp_ranges[r_count].code_offset = cur_code_offset;
                    temp_ranges[r_count].line_offset = cur_line_offset;
                    temp_ranges[r_count].file_id = cur_file_id;
                    temp_ranges[r_count].length = 0;
                    temp_ranges[r_count].rva = (proc_rva != XX_MS_PDB_NO_RVA) ? (proc_rva + cur_code_offset) : XX_MS_PDB_NO_RVA;
                    r_count++;
                }
                break;
            }
            case 12: /* ChangeCodeLengthAndCodeOffset */ {
                uint32_t length = syms_read_compressed_uint(ann, ann_size, &pos);
                uint32_t offset_delta = syms_read_compressed_uint(ann, ann_size, &pos);
                cur_code_offset += offset_delta;
                if (r_count >= r_cap) {
                    size_t nc = r_cap ? r_cap * 2 : 8;
                    xx_ms_pdb_inline_range *nb = (xx_ms_pdb_inline_range *)xx_mem_realloc(temp_ranges, nc * sizeof(xx_ms_pdb_inline_range));
                    if (nb) {
                        temp_ranges = nb;
                        r_cap = nc;
                    }
                }
                if (r_count < r_cap) {
                    temp_ranges[r_count].code_offset = cur_code_offset;
                    temp_ranges[r_count].line_offset = cur_line_offset;
                    temp_ranges[r_count].file_id = cur_file_id;
                    temp_ranges[r_count].length = length;
                    temp_ranges[r_count].rva = (proc_rva != XX_MS_PDB_NO_RVA) ? (proc_rva + cur_code_offset) : XX_MS_PDB_NO_RVA;
                    r_count++;
                }
                break;
            }
            default: break;
        }
    }

    /* Fix zero lengths from next range start */
    for (size_t i = 0; i + 1 < r_count; i++) {
        if (temp_ranges[i].length == 0) {
            if (temp_ranges[i + 1].code_offset >= temp_ranges[i].code_offset) {
                temp_ranges[i].length = temp_ranges[i + 1].code_offset - temp_ranges[i].code_offset;
            }
        }
    }

    if (r_count > 0 && temp_ranges) {
        site->ranges = (xx_ms_pdb_inline_range *)xx_arena_alloc_array(arena, r_count, sizeof(xx_ms_pdb_inline_range));
        if (site->ranges) {
            memcpy(site->ranges, temp_ranges, r_count * sizeof(xx_ms_pdb_inline_range));
            site->range_count = r_count;
        }
    }
    if (temp_ranges) xx_mem_free(temp_ranges);
}

/* Dynamic buffer types for procedure assembly */
typedef struct syms_temp_local {
    xx_ms_pdb_local base;
    xx_ms_pdb_def_range *ranges_buf;
    size_t ranges_cap;
    size_t ranges_count;
} syms_temp_local;

typedef struct syms_temp_proc {
    xx_ms_pdb_proc_detail detail;
    syms_temp_local *locals_buf;
    size_t locals_cap;
    size_t locals_count;
    xx_ms_pdb_inline_site *inlines_buf;
    size_t inlines_cap;
    size_t inlines_count;
    int32_t inline_stack[256];
    int32_t inline_stack_top;
    uint32_t expected_params;
    uint32_t param_seen;
    int32_t scope_depth;
} syms_temp_proc;

static void syms_finalize_proc(xx_arena_t *arena, syms_temp_proc *tp)
{
    /* Finalize locals */
    if (tp->locals_count > 0 && tp->locals_buf) {
        tp->detail.locals = (xx_ms_pdb_local *)xx_arena_alloc_array(arena, tp->locals_count, sizeof(xx_ms_pdb_local));
        if (tp->detail.locals) {
            for (size_t i = 0; i < tp->locals_count; i++) {
                tp->detail.locals[i] = tp->locals_buf[i].base;
                if (tp->locals_buf[i].ranges_count > 0 && tp->locals_buf[i].ranges_buf) {
                    tp->detail.locals[i].ranges = (xx_ms_pdb_def_range *)xx_arena_alloc_array(arena, tp->locals_buf[i].ranges_count, sizeof(xx_ms_pdb_def_range));
                    if (tp->detail.locals[i].ranges) {
                        memcpy(tp->detail.locals[i].ranges, tp->locals_buf[i].ranges_buf, tp->locals_buf[i].ranges_count * sizeof(xx_ms_pdb_def_range));
                        tp->detail.locals[i].range_count = tp->locals_buf[i].ranges_count;
                    }
                }
                if (tp->locals_buf[i].ranges_buf) xx_mem_free(tp->locals_buf[i].ranges_buf);
            }
            tp->detail.local_count = tp->locals_count;
        }
        xx_mem_free(tp->locals_buf);
        tp->locals_buf = NULL;
        tp->locals_count = 0;
    }

    /* Finalize inlines */
    if (tp->inlines_count > 0 && tp->inlines_buf) {
        tp->detail.inlines = (xx_ms_pdb_inline_site *)xx_arena_alloc_array(arena, tp->inlines_count, sizeof(xx_ms_pdb_inline_site));
        if (tp->detail.inlines) {
            memcpy(tp->detail.inlines, tp->inlines_buf, tp->inlines_count * sizeof(xx_ms_pdb_inline_site));
            tp->detail.inline_count = tp->inlines_count;
        }
        xx_mem_free(tp->inlines_buf);
        tp->inlines_buf = NULL;
        tp->inlines_count = 0;
    }
}

bool xx_ms_pdb_get_module_procs(xx_ms_pdb *pdb, int32_t module_index, const xx_ms_pdb_addr_map *map, xx_ms_pdb_module_procs *out)
{
    if (!pdb || !out) return false;
    memset(out, 0, sizeof(*out));
    out->module_index = module_index;

    xx_arena_t *arena = (xx_arena_t *)xx_mem_alloc(sizeof(xx_arena_t));
    if (!arena) return false;
    xx_arena_init(arena);
    out->internal_ = arena;

    xx_ms_pdb_module_list mods = {0};
    if (!xx_ms_pdb_get_modules(pdb, &mods)) {
        xx_ms_pdb_module_procs_free(out);
        return false;
    }
    if (module_index < 0 || (size_t)module_index >= mods.count) {
        xx_ms_pdb_module_list_free(&mods);
        xx_ms_pdb_module_procs_free(out);
        return false;
    }

    const xx_ms_pdb_module *mod = &mods.items[module_index];
    if (mod->stream_index == 0xFFFF || mod->sym_byte_size <= 4) {
        xx_ms_pdb_module_list_free(&mods);
        return true;
    }

    uint32_t sym_size = mod->sym_byte_size;
    if (sym_size > SYMS_MAX_STREAM_SIZE) sym_size = SYMS_MAX_STREAM_SIZE;

    uint8_t *data = (uint8_t *)xx_mem_alloc(sym_size + 4);
    if (!data) {
        xx_ms_pdb_module_list_free(&mods);
        xx_ms_pdb_module_procs_free(out);
        return false;
    }

    int64_t bytes_read = xx_ms_pdb_read_stream(pdb, mod->stream_index, 0, data, sym_size);
    if (bytes_read != (int64_t)sym_size) {
        xx_mem_free(data);
        xx_ms_pdb_module_list_free(&mods);
        return true;
    }
    data[sym_size] = 0;
    data[sym_size + 1] = 0;
    xx_ms_pdb_module_list_free(&mods);

    /* Machine detection from DBI / module compile info */
    uint16_t module_machine = 0;
    xx_ms_pdb_dbi_info dbi = xx_ms_pdb_get_dbi_info(pdb);
    if (dbi.is_valid) module_machine = dbi.machine;

    /* Dynamic buffers for procs and thunks */
    xx_ms_pdb_proc_detail *procs_buf = NULL;
    size_t procs_cap = 0;
    size_t procs_count = 0;

    xx_ms_pdb_thunk *thunks_buf = NULL;
    size_t thunks_cap = 0;
    size_t thunks_count = 0;

    syms_temp_proc cur_proc;
    memset(&cur_proc, 0, sizeof(cur_proc));
    cur_proc.inline_stack_top = -1;
    bool in_proc = false;

    uint32_t off = 4;
    while (off + 4 <= sym_size) {
        uint16_t reclen = syms_u16(data + off);
        uint16_t kind = syms_u16(data + off + 2);
        if (reclen < 2) break;
        uint32_t next_off = off + reclen + 2;
        if (next_off > sym_size) break;

        const uint8_t *rec = data + off;
        size_t rec_len = reclen;

        /* Check for module machine update from compile symbol */
        if (kind == XX_MS_PDB_S_COMPILE3 && rec_len >= 10) {
            module_machine = syms_u16(rec + 8);
        } else if (kind == 0x1116 /* S_COMPILE2 */ && rec_len >= 10) {
            module_machine = syms_u16(rec + 8);
        } else if (kind == XX_MS_PDB_S_COMPILE && rec_len >= 6) {
            module_machine = syms_u8(rec + 4);
        }

        /* Procedure start symbols */
        if (kind == XX_MS_PDB_S_GPROC32 || kind == XX_MS_PDB_S_LPROC32 || kind == XX_MS_PDB_S_GPROC32_ID || kind == XX_MS_PDB_S_LPROC32_ID ||
            kind == 0x1155 /* S_LPROC32_DPC */ || kind == 0x1156 /* S_LPROC32_DPC_ID */) {
            if (in_proc) {
                /* Nested proc: close the current one first */
                syms_finalize_proc(arena, &cur_proc);
                if (procs_count >= procs_cap) {
                    size_t nc = procs_cap ? procs_cap * 2 : 16;
                    xx_ms_pdb_proc_detail *nb = (xx_ms_pdb_proc_detail *)xx_mem_realloc(procs_buf, nc * sizeof(xx_ms_pdb_proc_detail));
                    if (nb) {
                        procs_buf = nb;
                        procs_cap = nc;
                    }
                }
                if (procs_count < procs_cap) {
                    procs_buf[procs_count++] = cur_proc.detail;
                }
                memset(&cur_proc, 0, sizeof(cur_proc));
                cur_proc.inline_stack_top = -1;
                in_proc = false;
            }

            if (rec_len >= 37) {
                in_proc = true;
                memset(&cur_proc, 0, sizeof(cur_proc));
                cur_proc.inline_stack_top = -1;
                cur_proc.detail.kind = kind;
                cur_proc.detail.record_offset = (int64_t)off;
                cur_proc.detail.code_size = syms_u32(rec + 16);
                cur_proc.detail.debug_start = syms_u32(rec + 20);
                cur_proc.detail.debug_end = syms_u32(rec + 24);
                uint32_t type_or_id = syms_u32(rec + 28);
                cur_proc.detail.offset = syms_u32(rec + 32);
                cur_proc.detail.segment = syms_u16(rec + 36);
                cur_proc.detail.proc_flags = syms_u8(rec + 38);
                cur_proc.detail.name = xx_arena_strdup(arena, syms_str(rec + 39, rec_len - 37));
                cur_proc.detail.rva = map ? xx_ms_pdb_addr_map_rva(map, cur_proc.detail.segment, cur_proc.detail.offset) : XX_MS_PDB_NO_RVA;

                if (kind == XX_MS_PDB_S_GPROC32_ID || kind == XX_MS_PDB_S_LPROC32_ID || kind == 0x1156) {
                    cur_proc.detail.id_index = type_or_id;
                    xx_ms_pdb_id_record id_rec = {0};
                    if (xx_ms_pdb_get_id_by_index(pdb, type_or_id, &id_rec) && id_rec.is_valid &&
                        (id_rec.kind == XX_MS_PDB_LF_FUNC_ID || id_rec.kind == XX_MS_PDB_LF_MFUNC_ID)) {
                        cur_proc.detail.type_index = id_rec.type_index;
                    } else {
                        cur_proc.detail.type_index = type_or_id;
                    }
                } else {
                    cur_proc.detail.id_index = 0;
                    cur_proc.detail.type_index = type_or_id;
                }

                /* Expected parameter count from type */
                cur_proc.expected_params = 0;
                xx_ms_pdb_cvtype cvtype = xx_ms_pdb_get_type_by_index(pdb, cur_proc.detail.type_index);
                if (cvtype.is_valid) {
                    if (cvtype.kind == XX_MS_PDB_LF_PROCEDURE) {
                        cur_proc.expected_params = cvtype.count;
                    } else if (cvtype.kind == XX_MS_PDB_LF_MFUNCTION) {
                        cur_proc.expected_params = cvtype.count + (cvtype.this_type_index != 0 ? 1 : 0);
                    }
                }
            }
        } else if (in_proc) {
            if (kind == XX_MS_PDB_S_BLOCK32) {
                cur_proc.detail.block_count++;
                cur_proc.scope_depth++;
            } else if (kind == XX_MS_PDB_S_END || kind == XX_MS_PDB_S_PROC_ID_END) {
                if (cur_proc.scope_depth > 0) {
                    cur_proc.scope_depth--;
                } else {
                    /* Procedure end */
                    syms_finalize_proc(arena, &cur_proc);
                    if (procs_count >= procs_cap) {
                        size_t nc = procs_cap ? procs_cap * 2 : 16;
                        xx_ms_pdb_proc_detail *nb = (xx_ms_pdb_proc_detail *)xx_mem_realloc(procs_buf, nc * sizeof(xx_ms_pdb_proc_detail));
                        if (nb) {
                            procs_buf = nb;
                            procs_cap = nc;
                        }
                    }
                    if (procs_count < procs_cap) {
                        procs_buf[procs_count++] = cur_proc.detail;
                    }
                    memset(&cur_proc, 0, sizeof(cur_proc));
                    cur_proc.inline_stack_top = -1;
                    in_proc = false;
                }
            } else if (kind == XX_MS_PDB_S_LABEL32) {
                cur_proc.detail.label_count++;
            } else if (kind == XX_MS_PDB_S_CALLSITEINFO) {
                cur_proc.detail.has_callsite_info = true;
            } else if (kind == XX_MS_PDB_S_FRAMEPROC && rec_len >= 28) {
                cur_proc.detail.has_frame = true;
                cur_proc.detail.frame_size = syms_u32(rec + 4);
                cur_proc.detail.frame_padding = syms_u32(rec + 8);
                cur_proc.detail.frame_padding_offset = syms_u32(rec + 12);
                cur_proc.detail.saved_regs_size = syms_u32(rec + 16);
                cur_proc.detail.frame_flags = syms_u32(rec + 26);

                uint32_t l_idx = (cur_proc.detail.frame_flags >> 14) & 3;
                uint32_t p_idx = (cur_proc.detail.frame_flags >> 16) & 3;
                if (module_machine == 0xF6 || module_machine == 0xAA64) {
                    cur_proc.detail.frame_local_base_reg = 0;
                    cur_proc.detail.frame_param_base_reg = 0;
                } else if (module_machine == 0xD0 || module_machine == 0x8664) {
                    static const uint16_t x64_fp_regs[4] = {0, 0x14F, 0x14E, 0x155};
                    cur_proc.detail.frame_local_base_reg = x64_fp_regs[l_idx];
                    cur_proc.detail.frame_param_base_reg = x64_fp_regs[p_idx];
                } else {
                    static const uint16_t x86_fp_regs[4] = {0, 0x7536, 0x16, 0x14};
                    cur_proc.detail.frame_local_base_reg = x86_fp_regs[l_idx];
                    cur_proc.detail.frame_param_base_reg = x86_fp_regs[p_idx];
                }
            } else if (kind == XX_MS_PDB_S_INLINESITE || kind == 0x115D /* S_INLINESITE2 */) {
                if (rec_len >= 14) {
                    xx_ms_pdb_inline_site site = {0};
                    site.inlinee = syms_u32(rec + 12);
                    xx_ms_pdb_id_record in_id = {0};
                    if (xx_ms_pdb_get_id_by_index(pdb, site.inlinee, &in_id) && in_id.is_valid && in_id.name) {
                        site.name = xx_arena_strdup(arena, in_id.name);
                    } else {
                        site.name = "";
                    }
                    site.parent = (cur_proc.inline_stack_top >= 0) ? cur_proc.inline_stack[cur_proc.inline_stack_top] : -1;
                    site.depth = cur_proc.inline_stack_top + 1;

                    uint32_t ann_off = (kind == 0x115D) ? 20 : 16;
                    if ((size_t)(rec_len + 2) >= ann_off) {
                        syms_decode_inline_annotations(arena, cur_proc.detail.rva, rec + ann_off, (rec_len + 2) - ann_off, &site);
                    }

                    if (cur_proc.inlines_count >= cur_proc.inlines_cap) {
                        size_t nc = cur_proc.inlines_cap ? cur_proc.inlines_cap * 2 : 8;
                        xx_ms_pdb_inline_site *nb = (xx_ms_pdb_inline_site *)xx_mem_realloc(cur_proc.inlines_buf, nc * sizeof(xx_ms_pdb_inline_site));
                        if (nb) {
                            cur_proc.inlines_buf = nb;
                            cur_proc.inlines_cap = nc;
                        }
                    }
                    if (cur_proc.inlines_count < cur_proc.inlines_cap) {
                        int32_t s_idx = (int32_t)cur_proc.inlines_count;
                        cur_proc.inlines_buf[cur_proc.inlines_count++] = site;
                        if (cur_proc.inline_stack_top + 1 < 256) {
                            cur_proc.inline_stack[++cur_proc.inline_stack_top] = s_idx;
                        }
                    }
                }
            } else if (kind == XX_MS_PDB_S_INLINESITE_END) {
                if (cur_proc.inline_stack_top >= 0) {
                    cur_proc.inline_stack_top--;
                }
            } else if (kind == XX_MS_PDB_S_REGREL32 && rec_len >= 12) {
                syms_temp_local loc;
                memset(&loc, 0, sizeof(loc));
                loc.base.symbol_kind = kind;
                loc.base.location = XX_MS_PDB_LOC_REGREL;
                loc.base.offset = syms_i32(rec + 4);
                loc.base.type_index = syms_u32(rec + 8);
                loc.base.reg = syms_u16(rec + 12);
                loc.base.name = xx_arena_strdup(arena, syms_str(rec + 14, rec_len - 12));
                loc.base.depth = cur_proc.scope_depth;
                if (cur_proc.scope_depth == 0 && cur_proc.param_seen < cur_proc.expected_params) {
                    loc.base.is_param = true;
                    cur_proc.param_seen++;
                }
                if (cur_proc.locals_count >= cur_proc.locals_cap) {
                    size_t nc = cur_proc.locals_cap ? cur_proc.locals_cap * 2 : 16;
                    syms_temp_local *nb = (syms_temp_local *)xx_mem_realloc(cur_proc.locals_buf, nc * sizeof(syms_temp_local));
                    if (nb) {
                        cur_proc.locals_buf = nb;
                        cur_proc.locals_cap = nc;
                    }
                }
                if (cur_proc.locals_count < cur_proc.locals_cap) cur_proc.locals_buf[cur_proc.locals_count++] = loc;
            } else if (kind == 0x110B /* S_BPREL32 */ && rec_len >= 10) {
                syms_temp_local loc;
                memset(&loc, 0, sizeof(loc));
                loc.base.symbol_kind = kind;
                loc.base.location = XX_MS_PDB_LOC_BPREL;
                loc.base.offset = syms_i32(rec + 4);
                loc.base.type_index = syms_u32(rec + 8);
                loc.base.name = xx_arena_strdup(arena, syms_str(rec + 12, rec_len - 10));
                loc.base.depth = cur_proc.scope_depth;
                if (cur_proc.scope_depth == 0 && cur_proc.param_seen < cur_proc.expected_params) {
                    loc.base.is_param = true;
                    cur_proc.param_seen++;
                }
                if (cur_proc.locals_count >= cur_proc.locals_cap) {
                    size_t nc = cur_proc.locals_cap ? cur_proc.locals_cap * 2 : 16;
                    syms_temp_local *nb = (syms_temp_local *)xx_mem_realloc(cur_proc.locals_buf, nc * sizeof(syms_temp_local));
                    if (nb) {
                        cur_proc.locals_buf = nb;
                        cur_proc.locals_cap = nc;
                    }
                }
                if (cur_proc.locals_count < cur_proc.locals_cap) cur_proc.locals_buf[cur_proc.locals_count++] = loc;
            } else if (kind == XX_MS_PDB_S_REGISTER && rec_len >= 8) {
                syms_temp_local loc;
                memset(&loc, 0, sizeof(loc));
                loc.base.symbol_kind = kind;
                loc.base.location = XX_MS_PDB_LOC_REGISTER;
                loc.base.type_index = syms_u32(rec + 4);
                loc.base.reg = syms_u16(rec + 8);
                loc.base.name = xx_arena_strdup(arena, syms_str(rec + 10, rec_len - 8));
                loc.base.depth = cur_proc.scope_depth;
                if (cur_proc.scope_depth == 0 && cur_proc.param_seen < cur_proc.expected_params) {
                    loc.base.is_param = true;
                    cur_proc.param_seen++;
                }
                if (cur_proc.locals_count >= cur_proc.locals_cap) {
                    size_t nc = cur_proc.locals_cap ? cur_proc.locals_cap * 2 : 16;
                    syms_temp_local *nb = (syms_temp_local *)xx_mem_realloc(cur_proc.locals_buf, nc * sizeof(syms_temp_local));
                    if (nb) {
                        cur_proc.locals_buf = nb;
                        cur_proc.locals_cap = nc;
                    }
                }
                if (cur_proc.locals_count < cur_proc.locals_cap) cur_proc.locals_buf[cur_proc.locals_count++] = loc;
            } else if (kind == XX_MS_PDB_S_LOCAL && rec_len >= 8) {
                syms_temp_local loc;
                memset(&loc, 0, sizeof(loc));
                loc.base.symbol_kind = kind;
                loc.base.location = XX_MS_PDB_LOC_LOCAL;
                loc.base.type_index = syms_u32(rec + 4);
                loc.base.local_flags = syms_u16(rec + 8);
                loc.base.is_param = (loc.base.local_flags & 1) != 0;
                loc.base.name = xx_arena_strdup(arena, syms_str(rec + 10, rec_len - 8));
                loc.base.depth = cur_proc.scope_depth;
                if (cur_proc.locals_count >= cur_proc.locals_cap) {
                    size_t nc = cur_proc.locals_cap ? cur_proc.locals_cap * 2 : 16;
                    syms_temp_local *nb = (syms_temp_local *)xx_mem_realloc(cur_proc.locals_buf, nc * sizeof(syms_temp_local));
                    if (nb) {
                        cur_proc.locals_buf = nb;
                        cur_proc.locals_cap = nc;
                    }
                }
                if (cur_proc.locals_count < cur_proc.locals_cap) cur_proc.locals_buf[cur_proc.locals_count++] = loc;
            } else if (kind >= 0x113F && kind <= 0x1145) {
                /* S_DEFRANGE_* belongs to the last S_LOCAL */
                if (cur_proc.locals_count > 0 && cur_proc.locals_buf[cur_proc.locals_count - 1].base.location == XX_MS_PDB_LOC_LOCAL) {
                    syms_temp_local *last_loc = &cur_proc.locals_buf[cur_proc.locals_count - 1];
                    xx_ms_pdb_def_range dr;
                    syms_parse_defrange(map, rec, reclen, kind, &dr);
                    if (last_loc->ranges_count >= last_loc->ranges_cap) {
                        size_t nc = last_loc->ranges_cap ? last_loc->ranges_cap * 2 : 4;
                        xx_ms_pdb_def_range *nb = (xx_ms_pdb_def_range *)xx_mem_realloc(last_loc->ranges_buf, nc * sizeof(xx_ms_pdb_def_range));
                        if (nb) {
                            last_loc->ranges_buf = nb;
                            last_loc->ranges_cap = nc;
                        }
                    }
                    if (last_loc->ranges_count < last_loc->ranges_cap) {
                        last_loc->ranges_buf[last_loc->ranges_count++] = dr;
                    }
                }
            } else if ((kind == XX_MS_PDB_S_LDATA32 || kind == XX_MS_PDB_S_GDATA32 || kind == XX_MS_PDB_S_LTHREAD32 || kind == XX_MS_PDB_S_GTHREAD32) && rec_len >= 12) {
                syms_temp_local loc;
                memset(&loc, 0, sizeof(loc));
                loc.base.symbol_kind = kind;
                loc.base.location = XX_MS_PDB_LOC_STATIC;
                loc.base.type_index = syms_u32(rec + 4);
                uint32_t data_off = syms_u32(rec + 8);
                uint16_t data_seg = syms_u16(rec + 12);
                loc.base.rva = map ? xx_ms_pdb_addr_map_rva(map, data_seg, data_off) : XX_MS_PDB_NO_RVA;
                loc.base.name = xx_arena_strdup(arena, syms_str(rec + 14, rec_len - 12));
                loc.base.depth = cur_proc.scope_depth;
                if (cur_proc.locals_count >= cur_proc.locals_cap) {
                    size_t nc = cur_proc.locals_cap ? cur_proc.locals_cap * 2 : 16;
                    syms_temp_local *nb = (syms_temp_local *)xx_mem_realloc(cur_proc.locals_buf, nc * sizeof(syms_temp_local));
                    if (nb) {
                        cur_proc.locals_buf = nb;
                        cur_proc.locals_cap = nc;
                    }
                }
                if (cur_proc.locals_count < cur_proc.locals_cap) cur_proc.locals_buf[cur_proc.locals_count++] = loc;
            } else if (kind == XX_MS_PDB_S_CONSTANT && rec_len >= 6) {
                syms_temp_local loc;
                memset(&loc, 0, sizeof(loc));
                loc.base.symbol_kind = kind;
                loc.base.location = XX_MS_PDB_LOC_CONSTANT;
                loc.base.type_index = syms_u32(rec + 4);
                int64_t val = 0;
                int32_t val_size = syms_read_numeric_leaf(rec + 8, rec_len - 6, &val);
                loc.base.value = val;
                loc.base.name = xx_arena_strdup(arena, syms_str(rec + 8 + val_size, (rec_len >= (size_t)(6 + val_size)) ? (rec_len - 6 - val_size) : 0));
                loc.base.depth = cur_proc.scope_depth;
                if (cur_proc.locals_count >= cur_proc.locals_cap) {
                    size_t nc = cur_proc.locals_cap ? cur_proc.locals_cap * 2 : 16;
                    syms_temp_local *nb = (syms_temp_local *)xx_mem_realloc(cur_proc.locals_buf, nc * sizeof(syms_temp_local));
                    if (nb) {
                        cur_proc.locals_buf = nb;
                        cur_proc.locals_cap = nc;
                    }
                }
                if (cur_proc.locals_count < cur_proc.locals_cap) cur_proc.locals_buf[cur_proc.locals_count++] = loc;
            }
        }

        /* Thunk symbols outside or inside procs */
        if (kind == XX_MS_PDB_S_THUNK32 && rec_len >= 23) {
            xx_ms_pdb_thunk thunk;
            memset(&thunk, 0, sizeof(thunk));
            thunk.offset = syms_u32(rec + 16);
            thunk.segment = syms_u16(rec + 20);
            thunk.length = syms_u16(rec + 22);
            thunk.ordinal = syms_u8(rec + 24);
            thunk.name = xx_arena_strdup(arena, syms_str(rec + 25, rec_len - 23));
            thunk.rva = map ? xx_ms_pdb_addr_map_rva(map, thunk.segment, thunk.offset) : XX_MS_PDB_NO_RVA;

            if (thunks_count >= thunks_cap) {
                size_t nc = thunks_cap ? thunks_cap * 2 : 8;
                xx_ms_pdb_thunk *nb = (xx_ms_pdb_thunk *)xx_mem_realloc(thunks_buf, nc * sizeof(xx_ms_pdb_thunk));
                if (nb) {
                    thunks_buf = nb;
                    thunks_cap = nc;
                }
            }
            if (thunks_count < thunks_cap) thunks_buf[thunks_count++] = thunk;
        }

        off = next_off;
    }

    /* If stream ended while inside a proc */
    if (in_proc) {
        syms_finalize_proc(arena, &cur_proc);
        if (procs_count >= procs_cap) {
            size_t nc = procs_cap ? procs_cap * 2 : 16;
            xx_ms_pdb_proc_detail *nb = (xx_ms_pdb_proc_detail *)xx_mem_realloc(procs_buf, nc * sizeof(xx_ms_pdb_proc_detail));
            if (nb) {
                procs_buf = nb;
                procs_cap = nc;
            }
        }
        if (procs_count < procs_cap) {
            procs_buf[procs_count++] = cur_proc.detail;
        }
    }

    /* Copy procs and thunks to arena */
    if (procs_count > 0 && procs_buf) {
        out->procs = (xx_ms_pdb_proc_detail *)xx_arena_alloc_array(arena, procs_count, sizeof(xx_ms_pdb_proc_detail));
        if (out->procs) {
            memcpy(out->procs, procs_buf, procs_count * sizeof(xx_ms_pdb_proc_detail));
            out->proc_count = procs_count;
        }
        xx_mem_free(procs_buf);
    }

    if (thunks_count > 0 && thunks_buf) {
        out->thunks = (xx_ms_pdb_thunk *)xx_arena_alloc_array(arena, thunks_count, sizeof(xx_ms_pdb_thunk));
        if (out->thunks) {
            memcpy(out->thunks, thunks_buf, thunks_count * sizeof(xx_ms_pdb_thunk));
            out->thunk_count = thunks_count;
        }
        xx_mem_free(thunks_buf);
    }

    xx_mem_free(data);
    return true;
}

void xx_ms_pdb_module_procs_free(xx_ms_pdb_module_procs *procs)
{
    if (!procs) return;
    if (procs->internal_) {
        xx_arena_t *arena = (xx_arena_t *)procs->internal_;
        xx_arena_free(arena);
        xx_mem_free(arena);
    }
    memset(procs, 0, sizeof(*procs));
}
