/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/algo/kpa/xx_kpa.h"

/* Precomputed KPA key offset tables for key lengths 1..20 (index 0 is dummy) */
static const uint8_t encryptedPeKey0Offsets[21] = {0, 40, 40, 42, 40, 40, 42, 42, 40, 45, 40, 44, 48, 52, 42, 45, 48, 51, 54, 57, 40};
static const uint8_t encryptedPeKey1Offsets[21] = {0, 40, 41, 40, 41, 41, 43, 43, 41, 46, 41, 45, 49, 40, 43, 46, 49, 52, 55, 58, 41};
static const uint8_t encryptedPeLfa0Offsets[21] = {0, 40, 40, 42, 40, 40, 42, 46, 44, 42, 40, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40};
static const uint8_t encryptedPeLfa1Offsets[21] = {0, 40, 41, 40, 41, 41, 43, 40, 45, 43, 41, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41};
static const uint8_t encryptedPeLfa2Offsets[21] = {0, 40, 40, 41, 42, 42, 44, 41, 46, 44, 42, 40, 50, 49, 48, 47, 46, 45, 44, 43, 42};

static inline uint8_t decrypt_byte(uint8_t cipher, uint8_t encZero, int mode)
{
    switch (mode) {
        case 0: return (uint8_t)(cipher ^ encZero);
        case 1: return (uint8_t)((cipher - encZero) & 0xFF);
        case 2: return (uint8_t)((encZero - cipher) & 0xFF);
        default: return 0;
    }
}

static bool verify_encrypted_pe_signature(const uint8_t *buf, size_t peStartOffset, size_t maxValidLfaNew, int keyLength, int mode)
{
    uint8_t encZero = buf[peStartOffset + encryptedPeLfa2Offsets[keyLength]];
    uint8_t cipherByte = buf[peStartOffset + 0x3E];
    uint32_t valLfa2 = decrypt_byte(cipherByte, encZero, mode);

    if (((size_t)valLfa2 << 16) >= maxValidLfaNew) {
        return false;
    }

    encZero = buf[peStartOffset + encryptedPeLfa1Offsets[keyLength]];
    cipherByte = buf[peStartOffset + 0x3D];
    uint32_t valLfa1 = decrypt_byte(cipherByte, encZero, mode);

    encZero = buf[peStartOffset + encryptedPeLfa0Offsets[keyLength]];
    cipherByte = buf[peStartOffset + 0x3C];
    uint32_t valLfa0 = decrypt_byte(cipherByte, encZero, mode);

    uint32_t lfaNewOffset = valLfa0 | (valLfa1 << 8) | (valLfa2 << 16);

    if (lfaNewOffset <= 0x40 || (size_t)lfaNewOffset >= maxValidLfaNew) {
        return false;
    }

    size_t baseZeroOffset = peStartOffset + 40;
    size_t peSignatureOffset = peStartOffset + lfaNewOffset;
    unsigned int baseRemainder = (unsigned int)((lfaNewOffset - 40) % (uint32_t)keyLength);

    /* Verify PE signature "PE\0\0" */
    encZero = buf[baseZeroOffset + (baseRemainder % (uint32_t)keyLength)];
    cipherByte = buf[peSignatureOffset];
    if (decrypt_byte(cipherByte, encZero, mode) != 0x50) {
        return false;
    }

    encZero = buf[baseZeroOffset + ((baseRemainder + 1) % (uint32_t)keyLength)];
    cipherByte = buf[peSignatureOffset + 1];
    if (decrypt_byte(cipherByte, encZero, mode) != 0x45) {
        return false;
    }

    encZero = buf[baseZeroOffset + ((baseRemainder + 2) % (uint32_t)keyLength)];
    cipherByte = buf[peSignatureOffset + 2];
    if (decrypt_byte(cipherByte, encZero, mode) != 0x00) {
        return false;
    }

    encZero = buf[baseZeroOffset + ((baseRemainder + 3) % (uint32_t)keyLength)];
    cipherByte = buf[peSignatureOffset + 3];
    if (decrypt_byte(cipherByte, encZero, mode) != 0x00) {
        return false;
    }

    /* Verify OptionalHeader Magic: 0x010B (PE32) or 0x020B (PE32+) at offset 0x18 */
    encZero = buf[baseZeroOffset + ((baseRemainder + 0x18) % (uint32_t)keyLength)];
    cipherByte = buf[peSignatureOffset + 0x18];
    uint16_t magic1 = decrypt_byte(cipherByte, encZero, mode);

    encZero = buf[baseZeroOffset + ((baseRemainder + 0x19) % (uint32_t)keyLength)];
    cipherByte = buf[peSignatureOffset + 0x19];
    uint16_t magic2 = decrypt_byte(cipherByte, encZero, mode);
    uint16_t headerMagic = (uint16_t)(magic1 | (magic2 << 8));

    if (headerMagic != 0x010B && headerMagic != 0x020B) {
        return false;
    }

    /* Verify NumberOfSections at offset 0x06: 0 < totalSections <= 48 */
    encZero = buf[baseZeroOffset + ((baseRemainder + 0x06) % (uint32_t)keyLength)];
    cipherByte = buf[peSignatureOffset + 0x06];
    uint16_t sec1 = decrypt_byte(cipherByte, encZero, mode);

    encZero = buf[baseZeroOffset + ((baseRemainder + 0x07) % (uint32_t)keyLength)];
    cipherByte = buf[peSignatureOffset + 0x07];
    uint16_t sec2 = decrypt_byte(cipherByte, encZero, mode);
    uint16_t totalSections = (uint16_t)(sec1 | (sec2 << 8));

    if (totalSections == 0 || totalSections > 48) {
        return false;
    }

    /* Verify Characteristics at offset 0x16: IMAGE_FILE_EXECUTABLE_IMAGE (0x0002) */
    encZero = buf[baseZeroOffset + ((baseRemainder + 0x16) % (uint32_t)keyLength)];
    cipherByte = buf[peSignatureOffset + 0x16];
    uint16_t char1 = decrypt_byte(cipherByte, encZero, mode);

    encZero = buf[baseZeroOffset + ((baseRemainder + 0x17) % (uint32_t)keyLength)];
    cipherByte = buf[peSignatureOffset + 0x17];
    uint16_t char2 = decrypt_byte(cipherByte, encZero, mode);
    uint16_t characteristics = (uint16_t)(char1 | (char2 << 8));

    return (characteristics & 0x0002) != 0;
}

const char *xx_kpa_scan_buffer_encrypted_pe(const void *data, size_t size)
{
    if (!data || size < 0x100) {
        return "";
    }

    const uint8_t *buf = (const uint8_t *)data;
    const size_t maxSearchIndex = size - 0x100;

    for (size_t offset = 0; offset < maxSearchIndex; offset++) {
        const uint8_t cipherByte3 = buf[offset + 3];
        const uint8_t cipherLfaMsb = buf[offset + 0x3F];
        uint32_t candidateKeyLengths = 0;

        if (cipherByte3 == buf[offset + 40] && cipherLfaMsb == buf[offset + 40]) {
            candidateKeyLengths |= 0x00001; /* 1 */
        }

        if (cipherByte3 == buf[offset + 41]) {
            if (cipherLfaMsb == buf[offset + 41]) candidateKeyLengths |= 0x00002; /* 2 */
            if (cipherLfaMsb == buf[offset + 44]) candidateKeyLengths |= 0x40000; /* 19 */
        }

        if (cipherByte3 == buf[offset + 42]) {
            if (cipherLfaMsb == buf[offset + 42]) candidateKeyLengths |= 0x00004; /* 3 */
            if (cipherLfaMsb == buf[offset + 50]) candidateKeyLengths |= 0x01000; /* 13 */
        }

        if (cipherByte3 == buf[offset + 43]) {
            if (cipherLfaMsb == buf[offset + 43]) candidateKeyLengths |= 0x80218; /* 4, 5, 10, 20 */
            if (cipherLfaMsb == buf[offset + 47]) candidateKeyLengths |= 0x00080; /* 8 */
        }

        if (cipherByte3 == buf[offset + 45]) {
            if (cipherLfaMsb == buf[offset + 45]) candidateKeyLengths |= 0x00020; /* 6 */
            if (cipherLfaMsb == buf[offset + 42]) candidateKeyLengths |= 0x00040; /* 7 */
            if (cipherLfaMsb == buf[offset + 49]) candidateKeyLengths |= 0x02000; /* 14 */
        }

        if (cipherByte3 == buf[offset + 47] && cipherLfaMsb == buf[offset + 41]) {
            candidateKeyLengths |= 0x00400; /* 11 */
        }

        if (cipherByte3 == buf[offset + 48]) {
            if (cipherLfaMsb == buf[offset + 45]) candidateKeyLengths |= 0x00100; /* 9 */
            if (cipherLfaMsb == buf[offset + 48]) candidateKeyLengths |= 0x04000; /* 15 */
        }

        if (cipherByte3 == buf[offset + 51]) {
            if (cipherLfaMsb == buf[offset + 51]) candidateKeyLengths |= 0x00800; /* 12 */
            if (cipherLfaMsb == buf[offset + 47]) candidateKeyLengths |= 0x08000; /* 16 */
        }

        if (cipherByte3 == buf[offset + 54] && cipherLfaMsb == buf[offset + 46]) {
            candidateKeyLengths |= 0x10000; /* 17 */
        }

        if (cipherByte3 == buf[offset + 57] && cipherLfaMsb == buf[offset + 45]) {
            candidateKeyLengths |= 0x20000; /* 18 */
        }

        if (candidateKeyLengths == 0) {
            continue;
        }

        const uint8_t cipherM = buf[offset];
        const uint8_t cipherZ = buf[offset + 1];

        if (cipherM == 0x4D && cipherZ == 0x5A) {
            continue; /* Plain unencrypted PE, ignore */
        }

        const uint8_t keyXorM = cipherM ^ 0x4D;
        const uint8_t keyXorZ = cipherZ ^ 0x5A;
        const uint8_t keyAddM = (uint8_t)((cipherM - 0x4D) & 0xFF);
        const uint8_t keyAddZ = (uint8_t)((cipherZ - 0x5A) & 0xFF);
        const uint8_t keyRevM = (uint8_t)((cipherM + 0x4D) & 0xFF);
        const uint8_t keyRevZ = (uint8_t)((cipherZ + 0x5A) & 0xFF);
        const size_t maxValidLfaNew = size - offset - 0x20;

        for (int keyLength = 1, keyLengthBit = 1; candidateKeyLengths != 0; keyLength++, keyLengthBit <<= 1) {
            if (!(candidateKeyLengths & (uint32_t)keyLengthBit)) {
                continue;
            }

            candidateKeyLengths ^= (uint32_t)keyLengthBit;

            const uint8_t cipherByteKey0 = buf[offset + encryptedPeKey0Offsets[keyLength]];
            const uint8_t cipherByteKey1 = buf[offset + encryptedPeKey1Offsets[keyLength]];

            if (cipherByteKey0 == keyXorM && cipherByteKey1 == keyXorZ) {
                if (verify_encrypted_pe_signature(buf, offset, maxValidLfaNew, keyLength, 0)) {
                    return "XOR-XNOR";
                }
            } else if (cipherByteKey0 == keyAddM) {
                if (cipherByteKey1 == keyAddZ && verify_encrypted_pe_signature(buf, offset, maxValidLfaNew, keyLength, 1)) {
                    return "ADD-SUB";
                }
            } else if (cipherByteKey0 == keyRevM) {
                if (cipherByteKey1 == keyRevZ && verify_encrypted_pe_signature(buf, offset, maxValidLfaNew, keyLength, 2)) {
                    return "SUB-REV";
                }
            }
        }
    }

    return "";
}

const char *xx_scan_buffer_for_encrypted_pe(const void *data, size_t size)
{
    return xx_kpa_scan_buffer_encrypted_pe(data, size);
}
