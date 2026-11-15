/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
/* YARA-X v1.23.0 OID string resolution for the Authenticode helpers
 * (generated data; do not edit). Included by xx_pe_yx_authenticode.c. */

static const struct {
    const char *oid;
    const char *name;
} xx_pe_xx_pe_yx_oid_names[] = {
    {"1.2.840.113549.2.5", "md5"}, /* explicit:rfc5912::ID_MD_5 */
    {"1.3.14.3.2.26", "sha1"}, /* explicit:rfc5912::ID_SHA_1 */
    {"2.16.840.1.101.3.4.2.1", "sha256"}, /* explicit:rfc5912::ID_SHA_256 */
    {"2.16.840.1.101.3.4.2.2", "sha384"}, /* explicit:rfc5912::ID_SHA_384 */
    {"2.16.840.1.101.3.4.2.3", "sha512"}, /* explicit:rfc5912::ID_SHA_512 */
    {"1.2.840.113549.1.1.4", "md5WithRSAEncryption"}, /* explicit:rfc5912::MD_5_WITH_RSA_ENCRYPTION */
    {"1.2.840.113549.1.1.5", "sha1WithRSAEncryption"}, /* explicit:rfc5912::SHA_1_WITH_RSA_ENCRYPTION */
    {"1.3.14.3.2.29", "sha1WithRSAEncryption"}, /* explicit:oid::SHA1_WITH_RSA_ENCRYPTION_OBSOLETE */
    {"1.2.840.113549.1.1.11", "sha256WithRSAEncryption"}, /* explicit:rfc5912::SHA_256_WITH_RSA_ENCRYPTION */
    {"1.2.840.113549.1.1.12", "sha384WithRSAEncryption"}, /* explicit:rfc5912::SHA_384_WITH_RSA_ENCRYPTION */
    {"1.2.840.113549.1.1.13", "sha512WithRSAEncryption"}, /* explicit:rfc5912::SHA_512_WITH_RSA_ENCRYPTION */
    {"1.2.840.10040.4.3", "dsaWithSHA1"}, /* explicit:rfc5912::DSA_WITH_SHA_1 */
    {"2.16.840.1.101.3.4.3.1", "dsa_with_SHA224"}, /* explicit:rfc5912::DSA_WITH_SHA_224 */
    {"2.16.840.1.101.3.4.3.2", "dsa_with_SHA256"}, /* explicit:rfc5912::DSA_WITH_SHA_256 */
    {"2.5.4.6", "C"}, /* explicit:rfc4519::C */
    {"2.5.4.3", "CN"}, /* explicit:rfc4519::COMMON_NAME */
    {"2.5.4.10", "O"}, /* explicit:rfc4519::O */
    {"2.5.4.11", "OU"}, /* explicit:rfc4519::OU */
    {"2.5.4.8", "ST"}, /* explicit:rfc4519::ST */
    {"2.5.4.9", "street"}, /* explicit:rfc4519::STREET */
    {"1.3.6.1.4.1.311.60.2.1.3", "jurisdictionC"}, /* explicit:oid::JURISDICTION_C */
    {"1.3.6.1.4.1.311.60.2.1.1", "jurisdictionL"}, /* explicit:oid::JURISDICTION_L */
    {"1.3.6.1.4.1.311.60.2.1.2", "jurisdictionST"}, /* explicit:oid::JURISDICTION_ST */
    {"1.2.112.0.2.0.34.101.66", "bake"}, /* bake::BAKE */
    {"1.2.112.0.2.0.34.101.66.101", "bake-kdf"}, /* bake::BAKE_KDF */
    {"1.2.112.0.2.0.34.101.66.11", "bake-bmqv"}, /* bake::BAKE_BMQV */
    {"1.2.112.0.2.0.34.101.66.12", "bake-bsts"}, /* bake::BAKE_BSTS */
    {"1.2.112.0.2.0.34.101.66.2", "bake-keys"}, /* bake::BAKE_KEYS */
    {"1.2.112.0.2.0.34.101.66.2.1", "bake-pubkey"}, /* bake::BAKE_PUBKEY */
    {"1.2.112.0.2.0.34.101.66.201", "bake-swu"}, /* bake::BAKE_SWU */
    {"1.2.112.0.2.0.34.101.66.21", "bake-bpace"}, /* bake::BAKE_BPACE */
    {"1.2.112.0.2.0.34.101.66.31", "bake-dh"}, /* bake::BAKE_DH */
    {"1.2.112.0.2.0.34.101.77", "bash"}, /* bash::BASH */
    {"1.2.112.0.2.0.34.101.77.101", "bash-f"}, /* bash::BASH_F */
    {"1.2.112.0.2.0.34.101.77.11", "bash256"}, /* bash::BASH_256 */
    {"1.2.112.0.2.0.34.101.77.12", "bash384"}, /* bash::BASH_384 */
    {"1.2.112.0.2.0.34.101.77.13", "bash512"}, /* bash::BASH_512 */
    {"1.2.112.0.2.0.34.101.77.21", "bash-prg-hash2561"}, /* bash::BASH_PRG_HASH_2561 */
    {"1.2.112.0.2.0.34.101.77.22", "bash-prg-hash2562"}, /* bash::BASH_PRG_HASH_2562 */
    {"1.2.112.0.2.0.34.101.77.23", "bash-prg-hash3841"}, /* bash::BASH_PRG_HASH_3841 */
    {"1.2.112.0.2.0.34.101.77.24", "bash-prg-hash3842"}, /* bash::BASH_PRG_HASH_3842 */
    {"1.2.112.0.2.0.34.101.77.25", "bash-prg-hash5121"}, /* bash::BASH_PRG_HASH_5121 */
    {"1.2.112.0.2.0.34.101.77.26", "bash-prg-hash5122"}, /* bash::BASH_PRG_HASH_5122 */
    {"1.2.112.0.2.0.34.101.77.31", "bash-prg-ae1281"}, /* bash::BASH_PRG_AE_1281 */
    {"1.2.112.0.2.0.34.101.77.32", "bash-prg-ae1282"}, /* bash::BASH_PRG_AE_1282 */
    {"1.2.112.0.2.0.34.101.77.33", "bash-prg-ae1921"}, /* bash::BASH_PRG_AE_1921 */
    {"1.2.112.0.2.0.34.101.77.34", "bash-prg-ae1922"}, /* bash::BASH_PRG_AE_1922 */
    {"1.2.112.0.2.0.34.101.77.35", "bash-prg-ae2561"}, /* bash::BASH_PRG_AE_2561 */
    {"1.2.112.0.2.0.34.101.77.36", "bash-prg-ae2562"}, /* bash::BASH_PRG_AE_2562 */
    {"1.2.112.0.2.0.34.101.31", "belt"}, /* belt::BELT */
    {"1.2.112.0.2.0.34.101.31.101", "belt-keyrep"}, /* belt::BELT_KEYREP */
    {"1.2.112.0.2.0.34.101.31.11", "belt-ecb128"}, /* belt::BELT_ECB_128 */
    {"1.2.112.0.2.0.34.101.31.111", "belt-bde128"}, /* belt::BELT_BDE_128 */
    {"1.2.112.0.2.0.34.101.31.112", "belt-bde192"}, /* belt::BELT_BDE_192 */
    {"1.2.112.0.2.0.34.101.31.113", "belt-bde256"}, /* belt::BELT_BDE_256 */
    {"1.2.112.0.2.0.34.101.31.12", "belt-ecb192"}, /* belt::BELT_ECB_192 */
    {"1.2.112.0.2.0.34.101.31.121", "belt-sde128"}, /* belt::BELT_SDE_128 */
    {"1.2.112.0.2.0.34.101.31.122", "belt-sde192"}, /* belt::BELT_SDE_192 */
    {"1.2.112.0.2.0.34.101.31.123", "belt-sde256"}, /* belt::BELT_SDE_256 */
    {"1.2.112.0.2.0.34.101.31.13", "belt-ecb256"}, /* belt::BELT_ECB_256 */
    {"1.2.112.0.2.0.34.101.31.131", "belt-fmt128"}, /* belt::BELT_FMT_128 */
    {"1.2.112.0.2.0.34.101.31.132", "belt-fmt192"}, /* belt::BELT_FMT_192 */
    {"1.2.112.0.2.0.34.101.31.133", "belt-fmt256"}, /* belt::BELT_FMT_256 */
    {"1.2.112.0.2.0.34.101.31.21", "belt-cbc128"}, /* belt::BELT_CBC_128 */
    {"1.2.112.0.2.0.34.101.31.22", "belt-cbc192"}, /* belt::BELT_CBC_192 */
    {"1.2.112.0.2.0.34.101.31.23", "belt-cbc256"}, /* belt::BELT_CBC_256 */
    {"1.2.112.0.2.0.34.101.31.3", "belt-block128"}, /* belt::BELT_BLOCK_128 */
    {"1.2.112.0.2.0.34.101.31.31", "belt-cfb128"}, /* belt::BELT_CFB_128 */
    {"1.2.112.0.2.0.34.101.31.32", "belt-cfb192"}, /* belt::BELT_CFB_192 */
    {"1.2.112.0.2.0.34.101.31.33", "belt-cfb256"}, /* belt::BELT_CFB_256 */
    {"1.2.112.0.2.0.34.101.31.4", "belt-block192"}, /* belt::BELT_BLOCK_192 */
    {"1.2.112.0.2.0.34.101.31.41", "belt-ctr128"}, /* belt::BELT_CTR_128 */
    {"1.2.112.0.2.0.34.101.31.42", "belt-ctr192"}, /* belt::BELT_CTR_192 */
    {"1.2.112.0.2.0.34.101.31.43", "belt-ctr256"}, /* belt::BELT_CTR_256 */
    {"1.2.112.0.2.0.34.101.31.5", "belt-block256"}, /* belt::BELT_BLOCK_256 */
    {"1.2.112.0.2.0.34.101.31.51", "belt-mac128"}, /* belt::BELT_MAC_128 */
    {"1.2.112.0.2.0.34.101.31.52", "belt-mac192"}, /* belt::BELT_MAC_192 */
    {"1.2.112.0.2.0.34.101.31.53", "belt-mac256"}, /* belt::BELT_MAC_256 */
    {"1.2.112.0.2.0.34.101.31.6", "belt-wblock128"}, /* belt::BELT_WBLOCK_128 */
    {"1.2.112.0.2.0.34.101.31.61", "belt-dwp128"}, /* belt::BELT_DWP_128 */
    {"1.2.112.0.2.0.34.101.31.62", "belt-dwp192"}, /* belt::BELT_DWP_192 */
    {"1.2.112.0.2.0.34.101.31.63", "belt-dwp256"}, /* belt::BELT_DWP_256 */
    {"1.2.112.0.2.0.34.101.31.64", "belt-che128"}, /* belt::BELT_CHE_128 */
    {"1.2.112.0.2.0.34.101.31.65", "belt-che192"}, /* belt::BELT_CHE_192 */
    {"1.2.112.0.2.0.34.101.31.66", "belt-che256"}, /* belt::BELT_CHE_256 */
    {"1.2.112.0.2.0.34.101.31.7", "belt-wblock192"}, /* belt::BELT_WBLOCK_192 */
    {"1.2.112.0.2.0.34.101.31.71", "belt-kwp128"}, /* belt::BELT_KWP_128 */
    {"1.2.112.0.2.0.34.101.31.72", "belt-kwp192"}, /* belt::BELT_KWP_192 */
    {"1.2.112.0.2.0.34.101.31.73", "belt-kwp256"}, /* belt::BELT_KWP_256 */
    {"1.2.112.0.2.0.34.101.31.8", "belt-wblock256"}, /* belt::BELT_WBLOCK_256 */
    {"1.2.112.0.2.0.34.101.31.81", "belt-hash"}, /* belt::BELT_HASH */
    {"1.2.112.0.2.0.34.101.31.9", "belt-compress"}, /* belt::BELT_COMPRESS */
    {"1.2.112.0.2.0.34.101.31.91", "belt-keyexpand"}, /* belt::BELT_KEYEXPAND */
    {"1.2.112.0.2.0.34.101.45", "bign"}, /* bign::BIGN */
    {"1.2.112.0.2.0.34.101.45.11", "bign-with-hspec"}, /* bign::BIGN_WITH_HSPEC */
    {"1.2.112.0.2.0.34.101.45.12", "bign-with-hbelt"}, /* bign::BIGN_WITH_HBELT */
    {"1.2.112.0.2.0.34.101.45.2", "bign-keys"}, /* bign::BIGN_KEYS */
    {"1.2.112.0.2.0.34.101.45.2.1", "bign-pubkey"}, /* bign::BIGN_PUBKEY */
    {"1.2.112.0.2.0.34.101.45.21", "bign-genec"}, /* bign::BIGN_GENEC */
    {"1.2.112.0.2.0.34.101.45.22", "bign-valec"}, /* bign::BIGN_VALEC */
    {"1.2.112.0.2.0.34.101.45.3", "bign-curves"}, /* bign::BIGN_CURVES */
    {"1.2.112.0.2.0.34.101.45.3.1", "bign-curve256v1"}, /* bign::BIGN_CURVE_256_V_1 */
    {"1.2.112.0.2.0.34.101.45.3.2", "bign-curve384v1"}, /* bign::BIGN_CURVE_384_V_1 */
    {"1.2.112.0.2.0.34.101.45.3.3", "bign-curve512v1"}, /* bign::BIGN_CURVE_512_V_1 */
    {"1.2.112.0.2.0.34.101.45.31", "bign-genkeypair"}, /* bign::BIGN_GENKEYPAIR */
    {"1.2.112.0.2.0.34.101.45.32", "bign-valpubkey"}, /* bign::BIGN_VALPUBKEY */
    {"1.2.112.0.2.0.34.101.45.4", "bign-fields"}, /* bign::BIGN_FIELDS */
    {"1.2.112.0.2.0.34.101.45.4.1", "bign-primefield"}, /* bign::BIGN_PRIMEFIELD */
    {"1.2.112.0.2.0.34.101.45.41", "bign-keytransport"}, /* bign::BIGN_KEYTRANSPORT */
    {"1.2.112.0.2.0.34.101.45.61", "bign-genk"}, /* bign::BIGN_GENK */
    {"1.2.112.0.2.0.34.101.45.71", "bign-ibs-with-hspec"}, /* bign::BIGN_IBS_WITH_HSPEC */
    {"1.2.112.0.2.0.34.101.45.72", "bign-ibs-with-hbelt"}, /* bign::BIGN_IBS_WITH_HBELT */
    {"1.2.112.0.2.0.34.101.78", "bpki"}, /* bpki::BPKI */
    {"1.2.112.0.2.0.34.101.78.2", "bpki-role"}, /* bpki::BPKI_ROLE */
    {"1.2.112.0.2.0.34.101.78.2.0", "bpki-role-ca0"}, /* bpki::BPKI_ROLE_CA_0 */
    {"1.2.112.0.2.0.34.101.78.2.1", "bpki-role-ca1"}, /* bpki::BPKI_ROLE_CA_1 */
    {"1.2.112.0.2.0.34.101.78.2.10", "bpki-role-aa"}, /* bpki::BPKI_ROLE_AA */
    {"1.2.112.0.2.0.34.101.78.2.2", "bpki-role-ca2"}, /* bpki::BPKI_ROLE_CA_2 */
    {"1.2.112.0.2.0.34.101.78.2.20", "bpki-role-ra"}, /* bpki::BPKI_ROLE_RA */
    {"1.2.112.0.2.0.34.101.78.2.30", "bpki-role-ocsp"}, /* bpki::BPKI_ROLE_OCSP */
    {"1.2.112.0.2.0.34.101.78.2.31", "bpki-role-tsa"}, /* bpki::BPKI_ROLE_TSA */
    {"1.2.112.0.2.0.34.101.78.2.32", "bpki-role-dvcs"}, /* bpki::BPKI_ROLE_DVCS */
    {"1.2.112.0.2.0.34.101.78.2.33", "bpki-role-ids"}, /* bpki::BPKI_ROLE_IDS */
    {"1.2.112.0.2.0.34.101.78.2.50", "bpki-role-tls"}, /* bpki::BPKI_ROLE_TLS */
    {"1.2.112.0.2.0.34.101.78.2.60", "bpki-role-np"}, /* bpki::BPKI_ROLE_NP */
    {"1.2.112.0.2.0.34.101.78.2.61", "bpki-role-fnp"}, /* bpki::BPKI_ROLE_FNP */
    {"1.2.112.0.2.0.34.101.78.2.62", "bpki-role-lr"}, /* bpki::BPKI_ROLE_LR */
    {"1.2.112.0.2.0.34.101.78.2.70", "bpki-role-acd"}, /* bpki::BPKI_ROLE_ACD */
    {"1.2.112.0.2.0.34.101.78.3", "bpki-eku"}, /* bpki::BPKI_EKU */
    {"1.2.112.0.2.0.34.101.78.3.1", "bpki-eku-serverTM"}, /* bpki::BPKI_EKU_SERVER_TM */
    {"1.2.112.0.2.0.34.101.78.3.2", "bpki-eku-clientTM"}, /* bpki::BPKI_EKU_CLIENT_TM */
    {"1.2.112.0.2.0.34.101.78.4", "bpki-at"}, /* bpki::BPKI_AT */
    {"1.2.112.0.2.0.34.101.78.4.1", "bpki-at-certificateValidity"}, /* bpki::BPKI_AT_CERTIFICATE_VALIDITY */
    {"1.2.112.0.2.0.34.101.78.5", "bpki-ct"}, /* bpki::BPKI_CT */
    {"1.2.112.0.2.0.34.101.78.5.1", "bpki-ct-enroll1-req"}, /* bpki::BPKI_CT_ENROLL_1_REQ */
    {"1.2.112.0.2.0.34.101.78.5.2", "bpki-ct-enroll2-req"}, /* bpki::BPKI_CT_ENROLL_2_REQ */
    {"1.2.112.0.2.0.34.101.78.5.3", "bpki-ct-reenroll-req"}, /* bpki::BPKI_CT_REENROLL_REQ */
    {"1.2.112.0.2.0.34.101.78.5.4", "bpki-ct-spawn-req"}, /* bpki::BPKI_CT_SPAWN_REQ */
    {"1.2.112.0.2.0.34.101.78.5.5", "bpki-ct-setpwd-req"}, /* bpki::BPKI_CT_SETPWD_REQ */
    {"1.2.112.0.2.0.34.101.78.5.6", "bpki-ct-revoke-req"}, /* bpki::BPKI_CT_REVOKE_REQ */
    {"1.2.112.0.2.0.34.101.78.5.7", "bpki-ct-resp"}, /* bpki::BPKI_CT_RESP */
    {"1.2.112.0.2.0.34.101.47", "brng"}, /* brng::BRNG */
    {"1.2.112.0.2.0.34.101.47.11", "hmac-hspec"}, /* brng::HMAC_HSPEC */
    {"1.2.112.0.2.0.34.101.47.111", "hotp-hspec"}, /* brng::HOTP_HSPEC */
    {"1.2.112.0.2.0.34.101.47.112", "hotp-hbelt"}, /* brng::HOTP_HBELT */
    {"1.2.112.0.2.0.34.101.47.12", "hmac-hbelt"}, /* brng::HMAC_HBELT */
    {"1.2.112.0.2.0.34.101.47.121", "totp-hspec"}, /* brng::TOTP_HSPEC */
    {"1.2.112.0.2.0.34.101.47.122", "totp-hbelt"}, /* brng::TOTP_HBELT */
    {"1.2.112.0.2.0.34.101.47.131", "ocra-hspec"}, /* brng::OCRA_HSPEC */
    {"1.2.112.0.2.0.34.101.47.132", "ocra-hbelt"}, /* brng::OCRA_HBELT */
    {"1.2.112.0.2.0.34.101.47.21", "brng-ctr-hspec"}, /* brng::BRNG_CTR_HSPEC */
    {"1.2.112.0.2.0.34.101.47.22", "brng-ctr-hbelt"}, /* brng::BRNG_CTR_HBELT */
    {"1.2.112.0.2.0.34.101.47.23", "brng-ctr-stb11761"}, /* brng::BRNG_CTR_STB_11761 */
    {"1.2.112.0.2.0.34.101.47.31", "brng-hmac-hspec"}, /* brng::BRNG_HMAC_HSPEC */
    {"1.2.112.0.2.0.34.101.47.32", "brng-hmac-hbelt"}, /* brng::BRNG_HMAC_HBELT */
    {"1.2.112.0.2.0.34.101.79", "btok"}, /* btok::BTOK */
    {"1.2.112.0.2.0.34.101.79.11", "btok-bauth"}, /* btok::BTOK_BAUTH */
    {"1.2.112.0.2.0.34.101.79.12", "btok-bauth1"}, /* btok::BTOK_BAUTH_1 */
    {"1.2.112.0.2.0.34.101.79.5", "btok-attrs"}, /* btok::BTOK_ATTRS */
    {"1.2.112.0.2.0.34.101.79.5.1", "id-DocumentValidity"}, /* btok::ID_DOCUMENT_VALIDITY */
    {"1.2.112.0.2.0.34.101.79.5.2", "id-AgeVerification"}, /* btok::ID_AGE_VERIFICATION */
    {"1.2.112.0.2.0.34.101.79.5.3", "id-PlaceVerification"}, /* btok::ID_PLACE_VERIFICATION */
    {"1.2.112.0.2.0.34.101.79.6", "btok-access"}, /* btok::BTOK_ACCESS */
    {"1.2.112.0.2.0.34.101.79.6.1", "id-eIdAccess"}, /* btok::ID_E_ID_ACCESS */
    {"1.2.112.0.2.0.34.101.79.6.2", "id-eSignAccess"}, /* btok::ID_E_SIGN_ACCESS */
    {"1.2.112.0.2.0.34.101.79.7", "btok-apps"}, /* btok::BTOK_APPS */
    {"1.2.112.0.2.0.34.101.79.7.1", "id-eID"}, /* btok::ID_EID */
    {"1.2.112.0.2.0.34.101.79.7.2", "id-eSign"}, /* btok::ID_E_SIGN */
    {"1.2.112.0.2.0.34.101.79.8", "btok-cvext"}, /* btok::BTOK_CVEXT */
    {"1.2.112.0.2.0.34.101.79.8.1", "id-SignAuthExt"}, /* btok::ID_SIGN_AUTH_EXT */
    {"2.16.840.1.101.3.4", "nistAlgorithms"}, /* fips202::NIST_ALGORITHMS */
    {"2.16.840.1.101.3.4.2", "hashAlgs"}, /* fips202::HASH_ALGS */
    {"2.16.840.1.101.3.4.2.10", "id-sha3-512"}, /* fips202::ID_SHA_3_512 */
    {"2.16.840.1.101.3.4.2.11", "id-shake128"}, /* fips202::ID_SHAKE_128 */
    {"2.16.840.1.101.3.4.2.12", "id-shake256"}, /* fips202::ID_SHAKE_256 */
    {"2.16.840.1.101.3.4.2.7", "id-sha3-224"}, /* fips202::ID_SHA_3_224 */
    {"2.16.840.1.101.3.4.2.8", "id-sha3-256"}, /* fips202::ID_SHA_3_256 */
    {"2.16.840.1.101.3.4.2.9", "id-sha3-384"}, /* fips202::ID_SHA_3_384 */
    {"2.16.840.1.101.3.4.4", "kems"}, /* fips203::KEMS */
    {"2.16.840.1.101.3.4.4.1", "id-alg-ml-kem-512"}, /* fips203::ID_ALG_ML_KEM_512 */
    {"2.16.840.1.101.3.4.4.2", "id-alg-ml-kem-768"}, /* fips203::ID_ALG_ML_KEM_768 */
    {"2.16.840.1.101.3.4.4.3", "id-alg-ml-kem-1024"}, /* fips203::ID_ALG_ML_KEM_1024 */
    {"2.16.840.1.101.3.4.3", "sigAlgs"}, /* fips204::SIG_ALGS */
    {"2.16.840.1.101.3.4.3.17", "id-ml-dsa-44"}, /* fips204::ID_ML_DSA_44 */
    {"2.16.840.1.101.3.4.3.18", "id-ml-dsa-65"}, /* fips204::ID_ML_DSA_65 */
    {"2.16.840.1.101.3.4.3.19", "id-ml-dsa-87"}, /* fips204::ID_ML_DSA_87 */
    {"2.16.840.1.101.3.4.3.32", "id-hash-ml-dsa-44-with-sha512"}, /* fips204::ID_HASH_ML_DSA_44_WITH_SHA_512 */
    {"2.16.840.1.101.3.4.3.33", "id-hash-ml-dsa-65-with-sha512"}, /* fips204::ID_HASH_ML_DSA_65_WITH_SHA_512 */
    {"2.16.840.1.101.3.4.3.34", "id-hash-ml-dsa-87-with-sha512"}, /* fips204::ID_HASH_ML_DSA_87_WITH_SHA_512 */
    {"2.16.840.1.101.3.4.3.20", "id-slh-dsa-sha2-128s"}, /* fips205::ID_SLH_DSA_SHA_2_128_S */
    {"2.16.840.1.101.3.4.3.21", "id-slh-dsa-sha2-128f"}, /* fips205::ID_SLH_DSA_SHA_2_128_F */
    {"2.16.840.1.101.3.4.3.22", "id-slh-dsa-sha2-192s"}, /* fips205::ID_SLH_DSA_SHA_2_192_S */
    {"2.16.840.1.101.3.4.3.23", "id-slh-dsa-sha2-192f"}, /* fips205::ID_SLH_DSA_SHA_2_192_F */
    {"2.16.840.1.101.3.4.3.24", "id-slh-dsa-sha2-256s"}, /* fips205::ID_SLH_DSA_SHA_2_256_S */
    {"2.16.840.1.101.3.4.3.25", "id-slh-dsa-sha2-256f"}, /* fips205::ID_SLH_DSA_SHA_2_256_F */
    {"2.16.840.1.101.3.4.3.26", "id-slh-dsa-shake-128s"}, /* fips205::ID_SLH_DSA_SHAKE_128_S */
    {"2.16.840.1.101.3.4.3.27", "id-slh-dsa-shake-128f"}, /* fips205::ID_SLH_DSA_SHAKE_128_F */
    {"2.16.840.1.101.3.4.3.28", "id-slh-dsa-shake-192s"}, /* fips205::ID_SLH_DSA_SHAKE_192_S */
    {"2.16.840.1.101.3.4.3.29", "id-slh-dsa-shake-192f"}, /* fips205::ID_SLH_DSA_SHAKE_192_F */
    {"2.16.840.1.101.3.4.3.30", "id-slh-dsa-shake-256s"}, /* fips205::ID_SLH_DSA_SHAKE_256_S */
    {"2.16.840.1.101.3.4.3.31", "id-slh-dsa-shake-256f"}, /* fips205::ID_SLH_DSA_SHAKE_256_F */
    {"2.16.840.1.101.3.4.3.35", "id-hash-slh-dsa-sha2-128s-with-sha256"}, /* fips205::ID_HASH_SLH_DSA_SHA_2_128_S_WITH_SHA_256 */
    {"2.16.840.1.101.3.4.3.36", "id-hash-slh-dsa-sha2-128f-with-sha256"}, /* fips205::ID_HASH_SLH_DSA_SHA_2_128_F_WITH_SHA_256 */
    {"2.16.840.1.101.3.4.3.37", "id-hash-slh-dsa-sha2-192s-with-sha512"}, /* fips205::ID_HASH_SLH_DSA_SHA_2_192_S_WITH_SHA_512 */
    {"2.16.840.1.101.3.4.3.38", "id-hash-slh-dsa-sha2-192f-with-sha512"}, /* fips205::ID_HASH_SLH_DSA_SHA_2_192_F_WITH_SHA_512 */
    {"2.16.840.1.101.3.4.3.39", "id-hash-slh-dsa-sha2-256s-with-sha512"}, /* fips205::ID_HASH_SLH_DSA_SHA_2_256_S_WITH_SHA_512 */
    {"2.16.840.1.101.3.4.3.40", "id-hash-slh-dsa-sha2-256f-with-sha512"}, /* fips205::ID_HASH_SLH_DSA_SHA_2_256_F_WITH_SHA_512 */
    {"2.16.840.1.101.3.4.3.41", "id-hash-slh-dsa-shake-128s-with-shake128"}, /* fips205::ID_HASH_SLH_DSA_SHAKE_128_S_WITH_SHAKE_128 */
    {"2.16.840.1.101.3.4.3.42", "id-hash-slh-dsa-shake-128f-with-shake128"}, /* fips205::ID_HASH_SLH_DSA_SHAKE_128_F_WITH_SHAKE_128 */
    {"2.16.840.1.101.3.4.3.43", "id-hash-slh-dsa-shake-192s-with-shake256"}, /* fips205::ID_HASH_SLH_DSA_SHAKE_192_S_WITH_SHAKE_256 */
    {"2.16.840.1.101.3.4.3.44", "id-hash-slh-dsa-shake-192f-with-shake256"}, /* fips205::ID_HASH_SLH_DSA_SHAKE_192_F_WITH_SHAKE_256 */
    {"2.16.840.1.101.3.4.3.45", "id-hash-slh-dsa-shake-256s-with-shake256"}, /* fips205::ID_HASH_SLH_DSA_SHAKE_256_S_WITH_SHAKE_256 */
    {"2.16.840.1.101.3.4.3.46", "id-hash-slh-dsa-shake-256f-with-shake256"}, /* fips205::ID_HASH_SLH_DSA_SHAKE_256_F_WITH_SHAKE_256 */
    {"0.9.2342.19200300.100.1.2", "textEncodedORAddress"}, /* rfc1274::TEXT_ENCODED_OR_ADDRESS */
    {"0.9.2342.19200300.100.1.22", "otherMailbox"}, /* rfc1274::OTHER_MAILBOX */
    {"0.9.2342.19200300.100.1.23", "lastModifiedTime"}, /* rfc1274::LAST_MODIFIED_TIME */
    {"0.9.2342.19200300.100.1.24", "lastModifiedBy"}, /* rfc1274::LAST_MODIFIED_BY */
    {"0.9.2342.19200300.100.1.26", "aRecord"}, /* rfc1274::A_RECORD */
    {"0.9.2342.19200300.100.1.27", "mDRecord"}, /* rfc1274::MD_RECORD */
    {"0.9.2342.19200300.100.1.28", "mXRecord"}, /* rfc1274::MX_RECORD */
    {"0.9.2342.19200300.100.1.29", "nSRecord"}, /* rfc1274::NS_RECORD */
    {"0.9.2342.19200300.100.1.30", "sOARecord"}, /* rfc1274::SOA_RECORD */
    {"0.9.2342.19200300.100.1.31", "cNAMERecord"}, /* rfc1274::CNAME_RECORD */
    {"0.9.2342.19200300.100.1.46", "janetMailbox"}, /* rfc1274::JANET_MAILBOX */
    {"0.9.2342.19200300.100.1.47", "mailPreferenceOption"}, /* rfc1274::MAIL_PREFERENCE_OPTION */
    {"0.9.2342.19200300.100.1.49", "dSAQuality"}, /* rfc1274::DSA_QUALITY */
    {"0.9.2342.19200300.100.1.51", "subtreeMinimumQuality"}, /* rfc1274::SUBTREE_MINIMUM_QUALITY */
    {"0.9.2342.19200300.100.1.52", "subtreeMaximumQuality"}, /* rfc1274::SUBTREE_MAXIMUM_QUALITY */
    {"0.9.2342.19200300.100.1.53", "personalSignature"}, /* rfc1274::PERSONAL_SIGNATURE */
    {"0.9.2342.19200300.100.1.54", "dITRedirect"}, /* rfc1274::DIT_REDIRECT */
    {"0.9.2342.19200300.100.1.55", "audio"}, /* rfc1274::AUDIO */
    {"0.9.2342.19200300.100.1.7", "photo"}, /* rfc1274::PHOTO */
    {"0.9.2342.19200300.100.4.15", "dNSDomain"}, /* rfc1274::DNS_DOMAIN */
    {"0.9.2342.19200300.100.4.20", "pilotOrganization"}, /* rfc1274::PILOT_ORGANIZATION */
    {"0.9.2342.19200300.100.4.21", "pilotDSA"}, /* rfc1274::PILOT_DSA */
    {"0.9.2342.19200300.100.4.22", "qualityLabelledData"}, /* rfc1274::QUALITY_LABELLED_DATA */
    {"0.9.2342.19200300.100.4.3", "pilotObject"}, /* rfc1274::PILOT_OBJECT */
    {"0.9.2342.19200300.100.4.4", "pilotPerson"}, /* rfc1274::PILOT_PERSON */
    {"1.3.6.1.4.1.250.1.57", "labeledURI"}, /* rfc2079::LABELED_URI */
    {"1.3.6.1.4.1.250.3.15", "labeledURIObject"}, /* rfc2079::LABELED_URI_OBJECT */
    {"1.3.6.1.4.1.453.7.1.1", "rFC822ToX400Mapping"}, /* rfc2164::RFC_822_TO_X_400_MAPPING */
    {"1.3.6.1.4.1.453.7.1.2", "x400ToRFC822Mapping"}, /* rfc2164::X_400_TO_RFC_822_MAPPING */
    {"1.3.6.1.4.1.453.7.1.3", "omittedORAddressComponent"}, /* rfc2164::OMITTED_OR_ADDRESS_COMPONENT */
    {"1.3.6.1.4.1.453.7.1.4", "mixerGateway"}, /* rfc2164::MIXER_GATEWAY */
    {"1.3.6.1.4.1.453.7.2.3", "associatedX400Gateway"}, /* rfc2164::ASSOCIATED_X_400_GATEWAY */
    {"1.3.6.1.4.1.453.7.2.6", "associatedORAddress"}, /* rfc2164::ASSOCIATED_OR_ADDRESS */
    {"1.3.6.1.4.1.453.7.2.7", "oRAddressComponentType"}, /* rfc2164::OR_ADDRESS_COMPONENT_TYPE */
    {"1.3.6.1.4.1.453.7.2.8", "associatedInternetGateway"}, /* rfc2164::ASSOCIATED_INTERNET_GATEWAY */
    {"1.3.6.1.4.1.453.7.2.9", "mcgamTables"}, /* rfc2164::MCGAM_TABLES */
    {"1.3.6.1.4.1.1466.345", "domainNameForm"}, /* rfc2247::DOMAIN_NAME_FORM */
    {"2.5.13.22", "presentationAddressMatch"}, /* rfc2252::PRESENTATION_ADDRESS_MATCH */
    {"2.5.13.24", "protocolInformationMatch"}, /* rfc2252::PROTOCOL_INFORMATION_MATCH */
    {"2.5.4.2", "knowledgeInformation"}, /* rfc2256::KNOWLEDGE_INFORMATION */
    {"2.5.4.29", "presentationAddress"}, /* rfc2256::PRESENTATION_ADDRESS */
    {"2.5.4.30", "supportedApplicationContext"}, /* rfc2256::SUPPORTED_APPLICATION_CONTEXT */
    {"2.5.4.48", "protocolInformation"}, /* rfc2256::PROTOCOL_INFORMATION */
    {"2.5.4.54", "dmdName"}, /* rfc2256::DMD_NAME */
    {"2.5.6.12", "applicationEntity"}, /* rfc2256::APPLICATION_ENTITY */
    {"2.5.6.13", "dSA"}, /* rfc2256::DSA */
    {"2.5.6.20", "dmd"}, /* rfc2256::DMD */
    {"1.3.6.1.4.1.453.7.1.5", "distinguishedNameTableEntry"}, /* rfc2293::DISTINGUISHED_NAME_TABLE_ENTRY */
    {"1.3.6.1.4.1.453.7.2.1", "textTableKey"}, /* rfc2293::TEXT_TABLE_KEY */
    {"1.3.6.1.4.1.453.7.2.2", "textTableValue"}, /* rfc2293::TEXT_TABLE_VALUE */
    {"1.3.6.1.4.1.1466.101.119.2", "dynamicObject"}, /* rfc2589::DYNAMIC_OBJECT */
    {"1.3.6.1.4.1.1466.101.119.3", "entryTtl"}, /* rfc2589::ENTRY_TTL */
    {"1.3.6.1.4.1.1466.101.119.4", "dynamicSubtrees"}, /* rfc2589::DYNAMIC_SUBTREES */
    {"1.2.840.113556.1.4.478", "calCalURI"}, /* rfc2739::CAL_CAL_URI */
    {"1.2.840.113556.1.4.479", "calFBURL"}, /* rfc2739::CAL_FBURL */
    {"1.2.840.113556.1.4.480", "calCAPURI"}, /* rfc2739::CAL_CAPURI */
    {"1.2.840.113556.1.4.481", "calCalAdrURI"}, /* rfc2739::CAL_CAL_ADR_URI */
    {"1.2.840.113556.1.4.482", "calOtherCalURIs"}, /* rfc2739::CAL_OTHER_CAL_UR_IS */
    {"1.2.840.113556.1.4.483", "calOtherFBURLs"}, /* rfc2739::CAL_OTHER_FBUR_LS */
    {"1.2.840.113556.1.4.484", "calOtherCAPURIs"}, /* rfc2739::CAL_OTHER_CAPUR_IS */
    {"1.2.840.113556.1.4.485", "calOtherCalAdrURIs"}, /* rfc2739::CAL_OTHER_CAL_ADR_UR_IS */
    {"1.2.840.113556.1.5.87", "calEntry"}, /* rfc2739::CAL_ENTRY */
    {"0.9.2342.19200300.100.1.60", "jpegPhoto"}, /* rfc2798::JPEG_PHOTO */
    {"2.16.840.1.113730.3.1.1", "carLicense"}, /* rfc2798::CAR_LICENSE */
    {"2.16.840.1.113730.3.1.2", "departmentNumber"}, /* rfc2798::DEPARTMENT_NUMBER */
    {"2.16.840.1.113730.3.1.216", "userPKCS12"}, /* rfc2798::USER_PKCS_12 */
    {"2.16.840.1.113730.3.1.241", "displayName"}, /* rfc2798::DISPLAY_NAME */
    {"2.16.840.1.113730.3.1.3", "employeeNumber"}, /* rfc2798::EMPLOYEE_NUMBER */
    {"2.16.840.1.113730.3.1.39", "preferredLanguage"}, /* rfc2798::PREFERRED_LANGUAGE */
    {"2.16.840.1.113730.3.1.4", "employeeType"}, /* rfc2798::EMPLOYEE_TYPE */
    {"2.16.840.1.113730.3.1.40", "userSMIMECertificate"}, /* rfc2798::USER_SMIME_CERTIFICATE */
    {"2.16.840.1.113730.3.2.2", "inetOrgPerson"}, /* rfc2798::INET_ORG_PERSON */
    {"1.2.840.113549.1.9", "pkcs-9"}, /* rfc2985::PKCS_9 */
    {"1.2.840.113549.1.9.0", "pkcs-9-mo"}, /* rfc2985::PKCS_9_MO */
    {"1.2.840.113549.1.9.1", "pkcs-9-at-emailAddress"}, /* rfc2985::PKCS_9_AT_EMAIL_ADDRESS */
    {"1.2.840.113549.1.9.10", "pkcs-9-at-issuerAndSerialNumber"}, /* rfc2985::PKCS_9_AT_ISSUER_AND_SERIAL_NUMBER */
    {"1.2.840.113549.1.9.11", "pkcs-9-at-passwordCheck"}, /* rfc2985::PKCS_9_AT_PASSWORD_CHECK */
    {"1.2.840.113549.1.9.12", "pkcs-9-at-publicKey"}, /* rfc2985::PKCS_9_AT_PUBLIC_KEY */
    {"1.2.840.113549.1.9.13", "pkcs-9-at-signingDescription"}, /* rfc2985::PKCS_9_AT_SIGNING_DESCRIPTION */
    {"1.2.840.113549.1.9.14", "pkcs-9-at-extensionRequest"}, /* rfc2985::PKCS_9_AT_EXTENSION_REQUEST */
    {"1.2.840.113549.1.9.15", "pkcs-9-at-smimeCapabilities"}, /* rfc2985::PKCS_9_AT_SMIME_CAPABILITIES */
    {"1.2.840.113549.1.9.16", "smime"}, /* rfc2985::SMIME */
    {"1.2.840.113549.1.9.2", "pkcs-9-at-unstructuredName"}, /* rfc2985::PKCS_9_AT_UNSTRUCTURED_NAME */
    {"1.2.840.113549.1.9.20", "pkcs-9-at-friendlyName"}, /* rfc2985::PKCS_9_AT_FRIENDLY_NAME */
    {"1.2.840.113549.1.9.21", "pkcs-9-at-localKeyId"}, /* rfc2985::PKCS_9_AT_LOCAL_KEY_ID */
    {"1.2.840.113549.1.9.22", "certTypes"}, /* rfc2985::CERT_TYPES */
    {"1.2.840.113549.1.9.23", "crlTypes"}, /* rfc2985::CRL_TYPES */
    {"1.2.840.113549.1.9.24", "pkcs-9-oc"}, /* rfc2985::PKCS_9_OC */
    {"1.2.840.113549.1.9.24.1", "pkcs-9-oc-pkcsEntity"}, /* rfc2985::PKCS_9_OC_PKCS_ENTITY */
    {"1.2.840.113549.1.9.24.2", "pkcs-9-oc-naturalPerson"}, /* rfc2985::PKCS_9_OC_NATURAL_PERSON */
    {"1.2.840.113549.1.9.25", "pkcs-9-at"}, /* rfc2985::PKCS_9_AT */
    {"1.2.840.113549.1.9.25.1", "pkcs-9-at-pkcs15Token"}, /* rfc2985::PKCS_9_AT_PKCS_15_TOKEN */
    {"1.2.840.113549.1.9.25.2", "pkcs-9-at-encryptedPrivateKeyInfo"}, /* rfc2985::PKCS_9_AT_ENCRYPTED_PRIVATE_KEY_INFO */
    {"1.2.840.113549.1.9.25.3", "pkcs-9-at-randomNonce"}, /* rfc2985::PKCS_9_AT_RANDOM_NONCE */
    {"1.2.840.113549.1.9.25.4", "pkcs-9-at-sequenceNumber"}, /* rfc2985::PKCS_9_AT_SEQUENCE_NUMBER */
    {"1.2.840.113549.1.9.25.5", "pkcs-9-at-pkcs7PDU"}, /* rfc2985::PKCS_9_AT_PKCS_7_PDU */
    {"1.2.840.113549.1.9.26", "pkcs-9-sx"}, /* rfc2985::PKCS_9_SX */
    {"1.2.840.113549.1.9.26.1", "pkcs-9-sx-pkcs9String"}, /* rfc2985::PKCS_9_SX_PKCS_9_STRING */
    {"1.2.840.113549.1.9.26.2", "pkcs-9-sx-signingTime"}, /* rfc2985::PKCS_9_SX_SIGNING_TIME */
    {"1.2.840.113549.1.9.27", "pkcs-9-mr"}, /* rfc2985::PKCS_9_MR */
    {"1.2.840.113549.1.9.27.1", "pkcs-9-mr-caseIgnoreMatch"}, /* rfc2985::PKCS_9_MR_CASE_IGNORE_MATCH */
    {"1.2.840.113549.1.9.27.2", "pkcs-9-mr-signingTimeMatch"}, /* rfc2985::PKCS_9_MR_SIGNING_TIME_MATCH */
    {"1.2.840.113549.1.9.3", "pkcs-9-at-contentType"}, /* rfc2985::PKCS_9_AT_CONTENT_TYPE */
    {"1.2.840.113549.1.9.4", "pkcs-9-at-messageDigest"}, /* rfc2985::PKCS_9_AT_MESSAGE_DIGEST */
    {"1.2.840.113549.1.9.5", "pkcs-9-at-signingTime"}, /* rfc2985::PKCS_9_AT_SIGNING_TIME */
    {"1.2.840.113549.1.9.6", "pkcs-9-at-counterSignature"}, /* rfc2985::PKCS_9_AT_COUNTER_SIGNATURE */
    {"1.2.840.113549.1.9.7", "pkcs-9-at-challengePassword"}, /* rfc2985::PKCS_9_AT_CHALLENGE_PASSWORD */
    {"1.2.840.113549.1.9.8", "pkcs-9-at-unstructuredAddress"}, /* rfc2985::PKCS_9_AT_UNSTRUCTURED_ADDRESS */
    {"1.2.840.113549.1.9.9", "pkcs-9-at-extendedCertificateAttributes"}, /* rfc2985::PKCS_9_AT_EXTENDED_CERTIFICATE_ATTRIBUTES */
    {"1.3.6.1.5.5.7.9", "ietf-at"}, /* rfc2985::IETF_AT */
    {"1.3.6.1.5.5.7.9.1", "pkcs-9-at-dateOfBirth"}, /* rfc2985::PKCS_9_AT_DATE_OF_BIRTH */
    {"1.3.6.1.5.5.7.9.2", "pkcs-9-at-placeOfBirth"}, /* rfc2985::PKCS_9_AT_PLACE_OF_BIRTH */
    {"1.3.6.1.5.5.7.9.3", "pkcs-9-at-gender"}, /* rfc2985::PKCS_9_AT_GENDER */
    {"1.3.6.1.5.5.7.9.4", "pkcs-9-at-countryOfCitizenship"}, /* rfc2985::PKCS_9_AT_COUNTRY_OF_CITIZENSHIP */
    {"1.3.6.1.5.5.7.9.5", "pkcs-9-at-countryOfResidence"}, /* rfc2985::PKCS_9_AT_COUNTRY_OF_RESIDENCE */
    {"1.2.840.113549.1.9.16.1.4", "id-ct-TSTInfo"}, /* rfc3161::ID_CT_TST_INFO */
    {"1.2.840.113549.1.9.16.2.14", "id-aa-timeStampToken"}, /* rfc3161::ID_AA_TIME_STAMP_TOKEN */
    {"2.5.4.65", "pseudonym"}, /* rfc3280::PSEUDONYM */
    {"2.16.840.1.113730.3.1.34", "ref"}, /* rfc3296::REF */
    {"2.16.840.1.113730.3.2.6", "referral"}, /* rfc3296::REFERRAL */
    {"2.5.18.12", "collectiveAttributeSubentries"}, /* rfc3671::COLLECTIVE_ATTRIBUTE_SUBENTRIES */
    {"2.5.18.7", "collectiveExclusions"}, /* rfc3671::COLLECTIVE_EXCLUSIONS */
    {"2.5.20.2", "collectiveAttributeSubentry"}, /* rfc3671::COLLECTIVE_ATTRIBUTE_SUBENTRY */
    {"2.5.4.10.1", "c-o"}, /* rfc3671::C_O */
    {"2.5.4.11.1", "c-ou"}, /* rfc3671::C_OU */
    {"2.5.4.16.1", "c-PostalAddress"}, /* rfc3671::C_POSTAL_ADDRESS */
    {"2.5.4.17.1", "c-PostalCode"}, /* rfc3671::C_POSTAL_CODE */
    {"2.5.4.18.1", "c-PostOfficeBox"}, /* rfc3671::C_POST_OFFICE_BOX */
    {"2.5.4.19.1", "c-PhysicalDeliveryOffice"}, /* rfc3671::C_PHYSICAL_DELIVERY_OFFICE */
    {"2.5.4.20.1", "c-TelephoneNumber"}, /* rfc3671::C_TELEPHONE_NUMBER */
    {"2.5.4.21.1", "c-TelexNumber"}, /* rfc3671::C_TELEX_NUMBER */
    {"2.5.4.23.1", "c-FacsimileTelephoneNumber"}, /* rfc3671::C_FACSIMILE_TELEPHONE_NUMBER */
    {"2.5.4.25.1", "c-InternationalISDNNumber"}, /* rfc3671::C_INTERNATIONAL_ISDN_NUMBER */
    {"2.5.4.7.1", "c-l"}, /* rfc3671::C_L */
    {"2.5.4.8.1", "c-st"}, /* rfc3671::C_ST */
    {"2.5.4.9.1", "c-street"}, /* rfc3671::C_STREET */
    {"2.5.17.0", "subentry"}, /* rfc3672::SUBENTRY */
    {"2.5.18.5", "administrativeRole"}, /* rfc3672::ADMINISTRATIVE_ROLE */
    {"2.5.18.6", "subtreeSpecification"}, /* rfc3672::SUBTREE_SPECIFICATION */
    {"2.5.23.1", "autonomousArea"}, /* rfc3672::AUTONOMOUS_AREA */
    {"2.5.23.2", "accessControlSpecificArea"}, /* rfc3672::ACCESS_CONTROL_SPECIFIC_AREA */
    {"2.5.23.3", "accessControlInnerArea"}, /* rfc3672::ACCESS_CONTROL_INNER_AREA */
    {"2.5.23.4", "subschemaAdminSpecificArea"}, /* rfc3672::SUBSCHEMA_ADMIN_SPECIFIC_AREA */
    {"2.5.23.5", "collectiveAttributeSpecificArea"}, /* rfc3672::COLLECTIVE_ATTRIBUTE_SPECIFIC_AREA */
    {"2.5.23.6", "collectiveAttributeInnerArea"}, /* rfc3672::COLLECTIVE_ATTRIBUTE_INNER_AREA */
    {"1.2.36.79672281.1.13.2", "componentFilterMatch"}, /* rfc3687::COMPONENT_FILTER_MATCH */
    {"1.2.36.79672281.1.13.3", "rdnMatch"}, /* rfc3687::RDN_MATCH */
    {"1.2.36.79672281.1.13.5", "presentMatch"}, /* rfc3687::PRESENT_MATCH */
    {"1.2.36.79672281.1.13.6", "allComponentsMatch"}, /* rfc3687::ALL_COMPONENTS_MATCH */
    {"1.2.36.79672281.1.13.7", "directoryComponentsMatch"}, /* rfc3687::DIRECTORY_COMPONENTS_MATCH */
    {"2.5.13.41", "storedPrefixMatch"}, /* rfc3698::STORED_PREFIX_MATCH */
    {"1.3.6.1.1.6.1.1", "pcimPolicy"}, /* rfc3703::PCIM_POLICY */
    {"1.3.6.1.1.6.1.10", "pcimRuleActionAssociation"}, /* rfc3703::PCIM_RULE_ACTION_ASSOCIATION */
    {"1.3.6.1.1.6.1.11", "pcimConditionAuxClass"}, /* rfc3703::PCIM_CONDITION_AUX_CLASS */
    {"1.3.6.1.1.6.1.12", "pcimTPCAuxClass"}, /* rfc3703::PCIM_TPC_AUX_CLASS */
    {"1.3.6.1.1.6.1.13", "pcimConditionVendorAuxClass"}, /* rfc3703::PCIM_CONDITION_VENDOR_AUX_CLASS */
    {"1.3.6.1.1.6.1.14", "pcimActionAuxClass"}, /* rfc3703::PCIM_ACTION_AUX_CLASS */
    {"1.3.6.1.1.6.1.15", "pcimActionVendorAuxClass"}, /* rfc3703::PCIM_ACTION_VENDOR_AUX_CLASS */
    {"1.3.6.1.1.6.1.16", "pcimPolicyInstance"}, /* rfc3703::PCIM_POLICY_INSTANCE */
    {"1.3.6.1.1.6.1.17", "pcimElementAuxClass"}, /* rfc3703::PCIM_ELEMENT_AUX_CLASS */
    {"1.3.6.1.1.6.1.18", "pcimRepository"}, /* rfc3703::PCIM_REPOSITORY */
    {"1.3.6.1.1.6.1.19", "pcimRepositoryAuxClass"}, /* rfc3703::PCIM_REPOSITORY_AUX_CLASS */
    {"1.3.6.1.1.6.1.2", "pcimGroup"}, /* rfc3703::PCIM_GROUP */
    {"1.3.6.1.1.6.1.20", "pcimRepositoryInstance"}, /* rfc3703::PCIM_REPOSITORY_INSTANCE */
    {"1.3.6.1.1.6.1.21", "pcimSubtreesPtrAuxClass"}, /* rfc3703::PCIM_SUBTREES_PTR_AUX_CLASS */
    {"1.3.6.1.1.6.1.22", "pcimGroupContainmentAuxClass"}, /* rfc3703::PCIM_GROUP_CONTAINMENT_AUX_CLASS */
    {"1.3.6.1.1.6.1.23", "pcimRuleContainmentAuxClass"}, /* rfc3703::PCIM_RULE_CONTAINMENT_AUX_CLASS */
    {"1.3.6.1.1.6.1.3", "pcimGroupAuxClass"}, /* rfc3703::PCIM_GROUP_AUX_CLASS */
    {"1.3.6.1.1.6.1.4", "pcimGroupInstance"}, /* rfc3703::PCIM_GROUP_INSTANCE */
    {"1.3.6.1.1.6.1.5", "pcimRule"}, /* rfc3703::PCIM_RULE */
    {"1.3.6.1.1.6.1.6", "pcimRuleAuxClass"}, /* rfc3703::PCIM_RULE_AUX_CLASS */
    {"1.3.6.1.1.6.1.7", "pcimRuleInstance"}, /* rfc3703::PCIM_RULE_INSTANCE */
    {"1.3.6.1.1.6.1.8", "pcimRuleConditionAssociation"}, /* rfc3703::PCIM_RULE_CONDITION_ASSOCIATION */
    {"1.3.6.1.1.6.1.9", "pcimRuleValidityAssociation"}, /* rfc3703::PCIM_RULE_VALIDITY_ASSOCIATION */
    {"1.3.6.1.1.6.2.10", "pcimRuleValidityPeriodList"}, /* rfc3703::PCIM_RULE_VALIDITY_PERIOD_LIST */
    {"1.3.6.1.1.6.2.11", "pcimRuleUsage"}, /* rfc3703::PCIM_RULE_USAGE */
    {"1.3.6.1.1.6.2.12", "pcimRulePriority"}, /* rfc3703::PCIM_RULE_PRIORITY */
    {"1.3.6.1.1.6.2.13", "pcimRuleMandatory"}, /* rfc3703::PCIM_RULE_MANDATORY */
    {"1.3.6.1.1.6.2.14", "pcimRuleSequencedActions"}, /* rfc3703::PCIM_RULE_SEQUENCED_ACTIONS */
    {"1.3.6.1.1.6.2.15", "pcimRoles"}, /* rfc3703::PCIM_ROLES */
    {"1.3.6.1.1.6.2.16", "pcimConditionGroupNumber"}, /* rfc3703::PCIM_CONDITION_GROUP_NUMBER */
    {"1.3.6.1.1.6.2.17", "pcimConditionNegated"}, /* rfc3703::PCIM_CONDITION_NEGATED */
    {"1.3.6.1.1.6.2.18", "pcimConditionName"}, /* rfc3703::PCIM_CONDITION_NAME */
    {"1.3.6.1.1.6.2.19", "pcimConditionDN"}, /* rfc3703::PCIM_CONDITION_DN */
    {"1.3.6.1.1.6.2.20", "pcimValidityConditionName"}, /* rfc3703::PCIM_VALIDITY_CONDITION_NAME */
    {"1.3.6.1.1.6.2.21", "pcimTimePeriodConditionDN"}, /* rfc3703::PCIM_TIME_PERIOD_CONDITION_DN */
    {"1.3.6.1.1.6.2.22", "pcimActionName"}, /* rfc3703::PCIM_ACTION_NAME */
    {"1.3.6.1.1.6.2.23", "pcimActionOrder"}, /* rfc3703::PCIM_ACTION_ORDER */
    {"1.3.6.1.1.6.2.24", "pcimActionDN"}, /* rfc3703::PCIM_ACTION_DN */
    {"1.3.6.1.1.6.2.25", "pcimTPCTime"}, /* rfc3703::PCIM_TPC_TIME */
    {"1.3.6.1.1.6.2.26", "pcimTPCMonthOfYearMask"}, /* rfc3703::PCIM_TPC_MONTH_OF_YEAR_MASK */
    {"1.3.6.1.1.6.2.27", "pcimTPCDayOfMonthMask"}, /* rfc3703::PCIM_TPC_DAY_OF_MONTH_MASK */
    {"1.3.6.1.1.6.2.28", "pcimTPCDayOfWeekMask"}, /* rfc3703::PCIM_TPC_DAY_OF_WEEK_MASK */
    {"1.3.6.1.1.6.2.29", "pcimTPCTimeOfDayMask"}, /* rfc3703::PCIM_TPC_TIME_OF_DAY_MASK */
    {"1.3.6.1.1.6.2.3", "pcimKeywords"}, /* rfc3703::PCIM_KEYWORDS */
    {"1.3.6.1.1.6.2.30", "pcimTPCLocalOrUtcTime"}, /* rfc3703::PCIM_TPC_LOCAL_OR_UTC_TIME */
    {"1.3.6.1.1.6.2.31", "pcimVendorConstraintData"}, /* rfc3703::PCIM_VENDOR_CONSTRAINT_DATA */
    {"1.3.6.1.1.6.2.32", "pcimVendorConstraintEncoding"}, /* rfc3703::PCIM_VENDOR_CONSTRAINT_ENCODING */
    {"1.3.6.1.1.6.2.33", "pcimVendorActionData"}, /* rfc3703::PCIM_VENDOR_ACTION_DATA */
    {"1.3.6.1.1.6.2.34", "pcimVendorActionEncoding"}, /* rfc3703::PCIM_VENDOR_ACTION_ENCODING */
    {"1.3.6.1.1.6.2.35", "pcimPolicyInstanceName"}, /* rfc3703::PCIM_POLICY_INSTANCE_NAME */
    {"1.3.6.1.1.6.2.36", "pcimRepositoryName"}, /* rfc3703::PCIM_REPOSITORY_NAME */
    {"1.3.6.1.1.6.2.37", "pcimSubtreesAuxContainedSet"}, /* rfc3703::PCIM_SUBTREES_AUX_CONTAINED_SET */
    {"1.3.6.1.1.6.2.38", "pcimGroupsAuxContainedSet"}, /* rfc3703::PCIM_GROUPS_AUX_CONTAINED_SET */
    {"1.3.6.1.1.6.2.39", "pcimRulesAuxContainedSet"}, /* rfc3703::PCIM_RULES_AUX_CONTAINED_SET */
    {"1.3.6.1.1.6.2.4", "pcimGroupName"}, /* rfc3703::PCIM_GROUP_NAME */
    {"1.3.6.1.1.6.2.5", "pcimRuleName"}, /* rfc3703::PCIM_RULE_NAME */
    {"1.3.6.1.1.6.2.6", "pcimRuleEnabled"}, /* rfc3703::PCIM_RULE_ENABLED */
    {"1.3.6.1.1.6.2.7", "pcimRuleConditionListType"}, /* rfc3703::PCIM_RULE_CONDITION_LIST_TYPE */
    {"1.3.6.1.1.6.2.8", "pcimRuleConditionList"}, /* rfc3703::PCIM_RULE_CONDITION_LIST */
    {"1.3.6.1.1.6.2.9", "pcimRuleActionList"}, /* rfc3703::PCIM_RULE_ACTION_LIST */
    {"1.3.18.0.2.4.1107", "printer-xri-supported"}, /* rfc3712::PRINTER_XRI_SUPPORTED */
    {"1.3.18.0.2.4.1108", "printer-aliases"}, /* rfc3712::PRINTER_ALIASES */
    {"1.3.18.0.2.4.1109", "printer-charset-configured"}, /* rfc3712::PRINTER_CHARSET_CONFIGURED */
    {"1.3.18.0.2.4.1110", "printer-job-priority-supported"}, /* rfc3712::PRINTER_JOB_PRIORITY_SUPPORTED */
    {"1.3.18.0.2.4.1111", "printer-job-k-octets-supported"}, /* rfc3712::PRINTER_JOB_K_OCTETS_SUPPORTED */
    {"1.3.18.0.2.4.1112", "printer-current-operator"}, /* rfc3712::PRINTER_CURRENT_OPERATOR */
    {"1.3.18.0.2.4.1113", "printer-service-person"}, /* rfc3712::PRINTER_SERVICE_PERSON */
    {"1.3.18.0.2.4.1114", "printer-delivery-orientation-supported"}, /* rfc3712::PRINTER_DELIVERY_ORIENTATION_SUPPORTED */
    {"1.3.18.0.2.4.1115", "printer-stacking-order-supported"}, /* rfc3712::PRINTER_STACKING_ORDER_SUPPORTED */
    {"1.3.18.0.2.4.1116", "printer-output-features-supported"}, /* rfc3712::PRINTER_OUTPUT_FEATURES_SUPPORTED */
    {"1.3.18.0.2.4.1117", "printer-media-local-supported"}, /* rfc3712::PRINTER_MEDIA_LOCAL_SUPPORTED */
    {"1.3.18.0.2.4.1118", "printer-copies-supported"}, /* rfc3712::PRINTER_COPIES_SUPPORTED */
    {"1.3.18.0.2.4.1119", "printer-natural-language-configured"}, /* rfc3712::PRINTER_NATURAL_LANGUAGE_CONFIGURED */
    {"1.3.18.0.2.4.1120", "printer-print-quality-supported"}, /* rfc3712::PRINTER_PRINT_QUALITY_SUPPORTED */
    {"1.3.18.0.2.4.1121", "printer-resolution-supported"}, /* rfc3712::PRINTER_RESOLUTION_SUPPORTED */
    {"1.3.18.0.2.4.1122", "printer-media-supported"}, /* rfc3712::PRINTER_MEDIA_SUPPORTED */
    {"1.3.18.0.2.4.1123", "printer-sides-supported"}, /* rfc3712::PRINTER_SIDES_SUPPORTED */
    {"1.3.18.0.2.4.1124", "printer-number-up-supported"}, /* rfc3712::PRINTER_NUMBER_UP_SUPPORTED */
    {"1.3.18.0.2.4.1125", "printer-finishings-supported"}, /* rfc3712::PRINTER_FINISHINGS_SUPPORTED */
    {"1.3.18.0.2.4.1126", "printer-pages-per-minute-color"}, /* rfc3712::PRINTER_PAGES_PER_MINUTE_COLOR */
    {"1.3.18.0.2.4.1127", "printer-pages-per-minute"}, /* rfc3712::PRINTER_PAGES_PER_MINUTE */
    {"1.3.18.0.2.4.1128", "printer-compression-supported"}, /* rfc3712::PRINTER_COMPRESSION_SUPPORTED */
    {"1.3.18.0.2.4.1129", "printer-color-supported"}, /* rfc3712::PRINTER_COLOR_SUPPORTED */
    {"1.3.18.0.2.4.1130", "printer-document-format-supported"}, /* rfc3712::PRINTER_DOCUMENT_FORMAT_SUPPORTED */
    {"1.3.18.0.2.4.1131", "printer-charset-supported"}, /* rfc3712::PRINTER_CHARSET_SUPPORTED */
    {"1.3.18.0.2.4.1132", "printer-multiple-document-jobs-supported"}, /* rfc3712::PRINTER_MULTIPLE_DOCUMENT_JOBS_SUPPORTED */
    {"1.3.18.0.2.4.1133", "printer-ipp-versions-supported"}, /* rfc3712::PRINTER_IPP_VERSIONS_SUPPORTED */
    {"1.3.18.0.2.4.1134", "printer-more-info"}, /* rfc3712::PRINTER_MORE_INFO */
    {"1.3.18.0.2.4.1135", "printer-name"}, /* rfc3712::PRINTER_NAME */
    {"1.3.18.0.2.4.1136", "printer-location"}, /* rfc3712::PRINTER_LOCATION */
    {"1.3.18.0.2.4.1137", "printer-generated-natural-language-supported"}, /* rfc3712::PRINTER_GENERATED_NATURAL_LANGUAGE_SUPPORTED */
    {"1.3.18.0.2.4.1138", "printer-make-and-model"}, /* rfc3712::PRINTER_MAKE_AND_MODEL */
    {"1.3.18.0.2.4.1139", "printer-info"}, /* rfc3712::PRINTER_INFO */
    {"1.3.18.0.2.4.1140", "printer-uri"}, /* rfc3712::PRINTER_URI */
    {"1.3.18.0.2.6.253", "printerLPR"}, /* rfc3712::PRINTER_LPR */
    {"1.3.18.0.2.6.254", "slpServicePrinter"}, /* rfc3712::SLP_SERVICE_PRINTER */
    {"1.3.18.0.2.6.255", "printerService"}, /* rfc3712::PRINTER_SERVICE */
    {"1.3.18.0.2.6.256", "printerIPP"}, /* rfc3712::PRINTER_IPP */
    {"1.3.18.0.2.6.257", "printerServiceAuxClass"}, /* rfc3712::PRINTER_SERVICE_AUX_CLASS */
    {"1.3.18.0.2.6.258", "printerAbstract"}, /* rfc3712::PRINTER_ABSTRACT */
    {"1.3.6.1.1.9.1.1", "pcelsPolicySet"}, /* rfc4104::PCELS_POLICY_SET */
    {"1.3.6.1.1.9.1.10", "pcelsActionAssociation"}, /* rfc4104::PCELS_ACTION_ASSOCIATION */
    {"1.3.6.1.1.9.1.11", "pcelsSimpleConditionAuxClass"}, /* rfc4104::PCELS_SIMPLE_CONDITION_AUX_CLASS */
    {"1.3.6.1.1.9.1.12", "pcelsCompoundConditionAuxClass"}, /* rfc4104::PCELS_COMPOUND_CONDITION_AUX_CLASS */
    {"1.3.6.1.1.9.1.13", "pcelsCompoundFilterConditionAuxClass"}, /* rfc4104::PCELS_COMPOUND_FILTER_CONDITION_AUX_CLASS */
    {"1.3.6.1.1.9.1.14", "pcelsSimpleActionAuxClass"}, /* rfc4104::PCELS_SIMPLE_ACTION_AUX_CLASS */
    {"1.3.6.1.1.9.1.15", "pcelsCompoundActionAuxClass"}, /* rfc4104::PCELS_COMPOUND_ACTION_AUX_CLASS */
    {"1.3.6.1.1.9.1.16", "pcelsVariable"}, /* rfc4104::PCELS_VARIABLE */
    {"1.3.6.1.1.9.1.17", "pcelsExplicitVariableAuxClass"}, /* rfc4104::PCELS_EXPLICIT_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.18", "pcelsImplicitVariableAuxClass"}, /* rfc4104::PCELS_IMPLICIT_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.19", "pcelsSourceIPv4VariableAuxClass"}, /* rfc4104::PCELS_SOURCE_I_PV_4_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.2", "pcelsPolicySetAssociation"}, /* rfc4104::PCELS_POLICY_SET_ASSOCIATION */
    {"1.3.6.1.1.9.1.20", "pcelsSourceIPv6VariableAuxClass"}, /* rfc4104::PCELS_SOURCE_I_PV_6_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.21", "pcelsDestinationIPv4VariableAuxClass"}, /* rfc4104::PCELS_DESTINATION_I_PV_4_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.22", "pcelsDestinationIPv6VariableAuxClass"}, /* rfc4104::PCELS_DESTINATION_I_PV_6_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.23", "pcelsSourcePortVariableAuxClass"}, /* rfc4104::PCELS_SOURCE_PORT_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.24", "pcelsDestinationPortVariableAuxClass"}, /* rfc4104::PCELS_DESTINATION_PORT_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.25", "pcelsIPProtocolVariableAuxClass"}, /* rfc4104::PCELS_IP_PROTOCOL_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.26", "pcelsIPVersionVariableAuxClass"}, /* rfc4104::PCELS_IP_VERSION_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.27", "pcelsIPToSVariableAuxClass"}, /* rfc4104::PCELS_IP_TO_S_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.28", "pcelsDSCPVariableAuxClass"}, /* rfc4104::PCELS_DSCP_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.29", "pcelsFlowIdVariableAuxClass"}, /* rfc4104::PCELS_FLOW_ID_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.3", "pcelsGroup"}, /* rfc4104::PCELS_GROUP */
    {"1.3.6.1.1.9.1.30", "pcelsSourceMACVariableAuxClass"}, /* rfc4104::PCELS_SOURCE_MAC_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.31", "pcelsDestinationMACVariableAuxClass"}, /* rfc4104::PCELS_DESTINATION_MAC_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.32", "pcelsVLANVariableAuxClass"}, /* rfc4104::PCELS_VLAN_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.33", "pcelsCoSVariableAuxClass"}, /* rfc4104::PCELS_CO_S_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.34", "pcelsEthertypeVariableAuxClass"}, /* rfc4104::PCELS_ETHERTYPE_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.35", "pcelsSourceSAPVariableAuxClass"}, /* rfc4104::PCELS_SOURCE_SAP_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.36", "pcelsDestinationSAPVariableAuxClass"}, /* rfc4104::PCELS_DESTINATION_SAP_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.37", "pcelsSNAPOUIVariableAuxClass"}, /* rfc4104::PCELS_SNAPOUI_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.38", "pcelsSNAPTypeVariableAuxClass"}, /* rfc4104::PCELS_SNAP_TYPE_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.39", "pcelsFlowDirectionVariableAuxClass"}, /* rfc4104::PCELS_FLOW_DIRECTION_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.4", "pcelsGroupAuxClass"}, /* rfc4104::PCELS_GROUP_AUX_CLASS */
    {"1.3.6.1.1.9.1.40", "pcelsValueAuxClass"}, /* rfc4104::PCELS_VALUE_AUX_CLASS */
    {"1.3.6.1.1.9.1.41", "pcelsIPv4AddrValueAuxClass"}, /* rfc4104::PCELS_I_PV_4_ADDR_VALUE_AUX_CLASS */
    {"1.3.6.1.1.9.1.42", "pcelsIPv6AddrValueAuxClass"}, /* rfc4104::PCELS_I_PV_6_ADDR_VALUE_AUX_CLASS */
    {"1.3.6.1.1.9.1.43", "pcelsMACAddrValueAuxClass"}, /* rfc4104::PCELS_MAC_ADDR_VALUE_AUX_CLASS */
    {"1.3.6.1.1.9.1.44", "pcelsStringValueAuxClass"}, /* rfc4104::PCELS_STRING_VALUE_AUX_CLASS */
    {"1.3.6.1.1.9.1.45", "pcelsBitStringValueAuxClass"}, /* rfc4104::PCELS_BIT_STRING_VALUE_AUX_CLASS */
    {"1.3.6.1.1.9.1.46", "pcelsIntegerValueAuxClass"}, /* rfc4104::PCELS_INTEGER_VALUE_AUX_CLASS */
    {"1.3.6.1.1.9.1.47", "pcelsBooleanValueAuxClass"}, /* rfc4104::PCELS_BOOLEAN_VALUE_AUX_CLASS */
    {"1.3.6.1.1.9.1.48", "pcelsReusableContainer"}, /* rfc4104::PCELS_REUSABLE_CONTAINER */
    {"1.3.6.1.1.9.1.49", "pcelsReusableContainerAuxClass"}, /* rfc4104::PCELS_REUSABLE_CONTAINER_AUX_CLASS */
    {"1.3.6.1.1.9.1.5", "pcelsGroupInstance"}, /* rfc4104::PCELS_GROUP_INSTANCE */
    {"1.3.6.1.1.9.1.50", "pcelsReusableContainerInstance"}, /* rfc4104::PCELS_REUSABLE_CONTAINER_INSTANCE */
    {"1.3.6.1.1.9.1.51", "pcelsRoleCollection"}, /* rfc4104::PCELS_ROLE_COLLECTION */
    {"1.3.6.1.1.9.1.52", "pcelsFilterEntryBase"}, /* rfc4104::PCELS_FILTER_ENTRY_BASE */
    {"1.3.6.1.1.9.1.53", "pcelsIPHeadersFilter"}, /* rfc4104::PCELS_IP_HEADERS_FILTER */
    {"1.3.6.1.1.9.1.54", "pcels8021Filter"}, /* rfc4104::PCELS_8021_FILTER */
    {"1.3.6.1.1.9.1.55", "pcelsFilterListAuxClass"}, /* rfc4104::PCELS_FILTER_LIST_AUX_CLASS */
    {"1.3.6.1.1.9.1.56", "pcelsVendorVariableAuxClass"}, /* rfc4104::PCELS_VENDOR_VARIABLE_AUX_CLASS */
    {"1.3.6.1.1.9.1.57", "pcelsVendorValueAuxClass"}, /* rfc4104::PCELS_VENDOR_VALUE_AUX_CLASS */
    {"1.3.6.1.1.9.1.6", "pcelsRule"}, /* rfc4104::PCELS_RULE */
    {"1.3.6.1.1.9.1.7", "pcelsRuleAuxClass"}, /* rfc4104::PCELS_RULE_AUX_CLASS */
    {"1.3.6.1.1.9.1.8", "pcelsRuleInstance"}, /* rfc4104::PCELS_RULE_INSTANCE */
    {"1.3.6.1.1.9.1.9", "pcelsConditionAssociation"}, /* rfc4104::PCELS_CONDITION_ASSOCIATION */
    {"1.3.6.1.1.9.2.1", "pcelsPolicySetName"}, /* rfc4104::PCELS_POLICY_SET_NAME */
    {"1.3.6.1.1.9.2.10", "pcelsExecutionStrategy"}, /* rfc4104::PCELS_EXECUTION_STRATEGY */
    {"1.3.6.1.1.9.2.11", "pcelsVariableDN"}, /* rfc4104::PCELS_VARIABLE_DN */
    {"1.3.6.1.1.9.2.12", "pcelsValueDN"}, /* rfc4104::PCELS_VALUE_DN */
    {"1.3.6.1.1.9.2.13", "pcelsIsMirrored"}, /* rfc4104::PCELS_IS_MIRRORED */
    {"1.3.6.1.1.9.2.14", "pcelsVariableName"}, /* rfc4104::PCELS_VARIABLE_NAME */
    {"1.3.6.1.1.9.2.15", "pcelsExpectedValueList"}, /* rfc4104::PCELS_EXPECTED_VALUE_LIST */
    {"1.3.6.1.1.9.2.16", "pcelsVariableModelClass"}, /* rfc4104::PCELS_VARIABLE_MODEL_CLASS */
    {"1.3.6.1.1.9.2.17", "pcelsVariableModelProperty"}, /* rfc4104::PCELS_VARIABLE_MODEL_PROPERTY */
    {"1.3.6.1.1.9.2.18", "pcelsExpectedValueTypes"}, /* rfc4104::PCELS_EXPECTED_VALUE_TYPES */
    {"1.3.6.1.1.9.2.19", "pcelsValueName"}, /* rfc4104::PCELS_VALUE_NAME */
    {"1.3.6.1.1.9.2.2", "pcelsDecisionStrategy"}, /* rfc4104::PCELS_DECISION_STRATEGY */
    {"1.3.6.1.1.9.2.20", "pcelsIPv4AddrList"}, /* rfc4104::PCELS_I_PV_4_ADDR_LIST */
    {"1.3.6.1.1.9.2.21", "pcelsIPv6AddrList"}, /* rfc4104::PCELS_I_PV_6_ADDR_LIST */
    {"1.3.6.1.1.9.2.22", "pcelsMACAddrList"}, /* rfc4104::PCELS_MAC_ADDR_LIST */
    {"1.3.6.1.1.9.2.23", "pcelsStringList"}, /* rfc4104::PCELS_STRING_LIST */
    {"1.3.6.1.1.9.2.24", "pcelsBitStringList"}, /* rfc4104::PCELS_BIT_STRING_LIST */
    {"1.3.6.1.1.9.2.25", "pcelsIntegerList"}, /* rfc4104::PCELS_INTEGER_LIST */
    {"1.3.6.1.1.9.2.26", "pcelsBoolean"}, /* rfc4104::PCELS_BOOLEAN */
    {"1.3.6.1.1.9.2.27", "pcelsReusableContainerName"}, /* rfc4104::PCELS_REUSABLE_CONTAINER_NAME */
    {"1.3.6.1.1.9.2.28", "pcelsReusableContainerList"}, /* rfc4104::PCELS_REUSABLE_CONTAINER_LIST */
    {"1.3.6.1.1.9.2.29", "pcelsRole"}, /* rfc4104::PCELS_ROLE */
    {"1.3.6.1.1.9.2.3", "pcelsPolicySetList"}, /* rfc4104::PCELS_POLICY_SET_LIST */
    {"1.3.6.1.1.9.2.30", "pcelsRoleCollectionName"}, /* rfc4104::PCELS_ROLE_COLLECTION_NAME */
    {"1.3.6.1.1.9.2.31", "pcelsElementList"}, /* rfc4104::PCELS_ELEMENT_LIST */
    {"1.3.6.1.1.9.2.32", "pcelsFilterName"}, /* rfc4104::PCELS_FILTER_NAME */
    {"1.3.6.1.1.9.2.33", "pcelsFilterIsNegated"}, /* rfc4104::PCELS_FILTER_IS_NEGATED */
    {"1.3.6.1.1.9.2.34", "pcelsIPHdrVersion"}, /* rfc4104::PCELS_IP_HDR_VERSION */
    {"1.3.6.1.1.9.2.35", "pcelsIPHdrSourceAddress"}, /* rfc4104::PCELS_IP_HDR_SOURCE_ADDRESS */
    {"1.3.6.1.1.9.2.36", "pcelsIPHdrSourceAddressEndOfRange"}, /* rfc4104::PCELS_IP_HDR_SOURCE_ADDRESS_END_OF_RANGE */
    {"1.3.6.1.1.9.2.37", "pcelsIPHdrSourceMask"}, /* rfc4104::PCELS_IP_HDR_SOURCE_MASK */
    {"1.3.6.1.1.9.2.38", "pcelsIPHdrDestAddress"}, /* rfc4104::PCELS_IP_HDR_DEST_ADDRESS */
    {"1.3.6.1.1.9.2.39", "pcelsIPHdrDestAddressEndOfRange"}, /* rfc4104::PCELS_IP_HDR_DEST_ADDRESS_END_OF_RANGE */
    {"1.3.6.1.1.9.2.4", "pcelsPriority"}, /* rfc4104::PCELS_PRIORITY */
    {"1.3.6.1.1.9.2.40", "pcelsIPHdrDestMask"}, /* rfc4104::PCELS_IP_HDR_DEST_MASK */
    {"1.3.6.1.1.9.2.41", "pcelsIPHdrProtocolID"}, /* rfc4104::PCELS_IP_HDR_PROTOCOL_ID */
    {"1.3.6.1.1.9.2.42", "pcelsIPHdrSourcePortStart"}, /* rfc4104::PCELS_IP_HDR_SOURCE_PORT_START */
    {"1.3.6.1.1.9.2.43", "pcelsIPHdrSourcePortEnd"}, /* rfc4104::PCELS_IP_HDR_SOURCE_PORT_END */
    {"1.3.6.1.1.9.2.44", "pcelsIPHdrDestPortStart"}, /* rfc4104::PCELS_IP_HDR_DEST_PORT_START */
    {"1.3.6.1.1.9.2.45", "pcelsIPHdrDestPortEnd"}, /* rfc4104::PCELS_IP_HDR_DEST_PORT_END */
    {"1.3.6.1.1.9.2.46", "pcelsIPHdrDSCPList"}, /* rfc4104::PCELS_IP_HDR_DSCP_LIST */
    {"1.3.6.1.1.9.2.47", "pcelsIPHdrFlowLabel"}, /* rfc4104::PCELS_IP_HDR_FLOW_LABEL */
    {"1.3.6.1.1.9.2.48", "pcels8021HdrSourceMACAddress"}, /* rfc4104::PCELS_8021_HDR_SOURCE_MAC_ADDRESS */
    {"1.3.6.1.1.9.2.49", "pcels8021HdrSourceMACMask"}, /* rfc4104::PCELS_8021_HDR_SOURCE_MAC_MASK */
    {"1.3.6.1.1.9.2.5", "pcelsPolicySetDN"}, /* rfc4104::PCELS_POLICY_SET_DN */
    {"1.3.6.1.1.9.2.50", "pcels8021HdrDestMACAddress"}, /* rfc4104::PCELS_8021_HDR_DEST_MAC_ADDRESS */
    {"1.3.6.1.1.9.2.51", "pcels8021HdrDestMACMask"}, /* rfc4104::PCELS_8021_HDR_DEST_MAC_MASK */
    {"1.3.6.1.1.9.2.52", "pcels8021HdrProtocolID"}, /* rfc4104::PCELS_8021_HDR_PROTOCOL_ID */
    {"1.3.6.1.1.9.2.53", "pcels8021HdrPriority"}, /* rfc4104::PCELS_8021_HDR_PRIORITY */
    {"1.3.6.1.1.9.2.54", "pcels8021HdrVLANID"}, /* rfc4104::PCELS_8021_HDR_VLANID */
    {"1.3.6.1.1.9.2.55", "pcelsFilterListName"}, /* rfc4104::PCELS_FILTER_LIST_NAME */
    {"1.3.6.1.1.9.2.56", "pcelsFilterDirection"}, /* rfc4104::PCELS_FILTER_DIRECTION */
    {"1.3.6.1.1.9.2.57", "pcelsFilterEntryList"}, /* rfc4104::PCELS_FILTER_ENTRY_LIST */
    {"1.3.6.1.1.9.2.58", "pcelsVendorVariableData"}, /* rfc4104::PCELS_VENDOR_VARIABLE_DATA */
    {"1.3.6.1.1.9.2.59", "pcelsVendorVariableEncoding"}, /* rfc4104::PCELS_VENDOR_VARIABLE_ENCODING */
    {"1.3.6.1.1.9.2.6", "pcelsConditionListType"}, /* rfc4104::PCELS_CONDITION_LIST_TYPE */
    {"1.3.6.1.1.9.2.60", "pcelsVendorValueData"}, /* rfc4104::PCELS_VENDOR_VALUE_DATA */
    {"1.3.6.1.1.9.2.61", "pcelsVendorValueEncoding"}, /* rfc4104::PCELS_VENDOR_VALUE_ENCODING */
    {"1.3.6.1.1.9.2.62", "pcelsRuleValidityPeriodList"}, /* rfc4104::PCELS_RULE_VALIDITY_PERIOD_LIST */
    {"1.3.6.1.1.9.2.7", "pcelsConditionList"}, /* rfc4104::PCELS_CONDITION_LIST */
    {"1.3.6.1.1.9.2.8", "pcelsActionList"}, /* rfc4104::PCELS_ACTION_LIST */
    {"1.3.6.1.1.9.2.9", "pcelsSequencedActions"}, /* rfc4104::PCELS_SEQUENCED_ACTIONS */
    {"1.3.6.1.1.11.1.1", "vPIMUser"}, /* rfc4237::VPIM_USER */
    {"1.3.6.1.1.11.2.1", "vPIMTelephoneNumber"}, /* rfc4237::VPIM_TELEPHONE_NUMBER */
    {"1.3.6.1.1.11.2.10", "vPIMSubMailboxes"}, /* rfc4237::VPIM_SUB_MAILBOXES */
    {"1.3.6.1.1.11.2.2", "vPIMRfc822Mailbox"}, /* rfc4237::VPIM_RFC_822_MAILBOX */
    {"1.3.6.1.1.11.2.3", "vPIMSpokenName"}, /* rfc4237::VPIM_SPOKEN_NAME */
    {"1.3.6.1.1.11.2.4", "vPIMSupportedUABehaviors"}, /* rfc4237::VPIM_SUPPORTED_UA_BEHAVIORS */
    {"1.3.6.1.1.11.2.5", "vPIMSupportedAudioMediaTypes"}, /* rfc4237::VPIM_SUPPORTED_AUDIO_MEDIA_TYPES */
    {"1.3.6.1.1.11.2.6", "vPIMSupportedMessageContext"}, /* rfc4237::VPIM_SUPPORTED_MESSAGE_CONTEXT */
    {"1.3.6.1.1.11.2.7", "vPIMTextName"}, /* rfc4237::VPIM_TEXT_NAME */
    {"1.3.6.1.1.11.2.8", "vPIMExtendedAbsenceStatus"}, /* rfc4237::VPIM_EXTENDED_ABSENCE_STATUS */
    {"1.3.6.1.1.11.2.9", "vPIMMaxMessageSize"}, /* rfc4237::VPIM_MAX_MESSAGE_SIZE */
    {"1.3.6.1.1.10.15.1", "uddiBusinessEntityNameForm"}, /* rfc4403::UDDI_BUSINESS_ENTITY_NAME_FORM */
    {"1.3.6.1.1.10.15.10", "uddiv3EntityObituaryNameForm"}, /* rfc4403::UDDIV_3_ENTITY_OBITUARY_NAME_FORM */
    {"1.3.6.1.1.10.15.2", "uddiContactNameForm"}, /* rfc4403::UDDI_CONTACT_NAME_FORM */
    {"1.3.6.1.1.10.15.3", "uddiAddressNameForm"}, /* rfc4403::UDDI_ADDRESS_NAME_FORM */
    {"1.3.6.1.1.10.15.4", "uddiBusinessServiceNameForm"}, /* rfc4403::UDDI_BUSINESS_SERVICE_NAME_FORM */
    {"1.3.6.1.1.10.15.5", "uddiBindingTemplateNameForm"}, /* rfc4403::UDDI_BINDING_TEMPLATE_NAME_FORM */
    {"1.3.6.1.1.10.15.6", "uddiTModelInstanceInfoNameForm"}, /* rfc4403::UDDI_T_MODEL_INSTANCE_INFO_NAME_FORM */
    {"1.3.6.1.1.10.15.7", "uddiTModelNameForm"}, /* rfc4403::UDDI_T_MODEL_NAME_FORM */
    {"1.3.6.1.1.10.15.8", "uddiPublisherAssertionNameForm"}, /* rfc4403::UDDI_PUBLISHER_ASSERTION_NAME_FORM */
    {"1.3.6.1.1.10.15.9", "uddiv3SubscriptionNameForm"}, /* rfc4403::UDDIV_3_SUBSCRIPTION_NAME_FORM */
    {"1.3.6.1.1.10.4.1", "uddiBusinessKey"}, /* rfc4403::UDDI_BUSINESS_KEY */
    {"1.3.6.1.1.10.4.10", "uddiEMail"}, /* rfc4403::UDDI_E_MAIL */
    {"1.3.6.1.1.10.4.11", "uddiSortCode"}, /* rfc4403::UDDI_SORT_CODE */
    {"1.3.6.1.1.10.4.12", "uddiTModelKey"}, /* rfc4403::UDDI_T_MODEL_KEY */
    {"1.3.6.1.1.10.4.13", "uddiAddressLine"}, /* rfc4403::UDDI_ADDRESS_LINE */
    {"1.3.6.1.1.10.4.14", "uddiIdentifierBag"}, /* rfc4403::UDDI_IDENTIFIER_BAG */
    {"1.3.6.1.1.10.4.15", "uddiCategoryBag"}, /* rfc4403::UDDI_CATEGORY_BAG */
    {"1.3.6.1.1.10.4.16", "uddiKeyedReference"}, /* rfc4403::UDDI_KEYED_REFERENCE */
    {"1.3.6.1.1.10.4.17", "uddiServiceKey"}, /* rfc4403::UDDI_SERVICE_KEY */
    {"1.3.6.1.1.10.4.18", "uddiBindingKey"}, /* rfc4403::UDDI_BINDING_KEY */
    {"1.3.6.1.1.10.4.19", "uddiAccessPoint"}, /* rfc4403::UDDI_ACCESS_POINT */
    {"1.3.6.1.1.10.4.2", "uddiAuthorizedName"}, /* rfc4403::UDDI_AUTHORIZED_NAME */
    {"1.3.6.1.1.10.4.20", "uddiHostingRedirector"}, /* rfc4403::UDDI_HOSTING_REDIRECTOR */
    {"1.3.6.1.1.10.4.21", "uddiInstanceDescription"}, /* rfc4403::UDDI_INSTANCE_DESCRIPTION */
    {"1.3.6.1.1.10.4.22", "uddiInstanceParms"}, /* rfc4403::UDDI_INSTANCE_PARMS */
    {"1.3.6.1.1.10.4.23", "uddiOverviewDescription"}, /* rfc4403::UDDI_OVERVIEW_DESCRIPTION */
    {"1.3.6.1.1.10.4.24", "uddiOverviewURL"}, /* rfc4403::UDDI_OVERVIEW_URL */
    {"1.3.6.1.1.10.4.25", "uddiFromKey"}, /* rfc4403::UDDI_FROM_KEY */
    {"1.3.6.1.1.10.4.26", "uddiToKey"}, /* rfc4403::UDDI_TO_KEY */
    {"1.3.6.1.1.10.4.27", "uddiUUID"}, /* rfc4403::UDDI_UUID */
    {"1.3.6.1.1.10.4.28", "uddiIsHidden"}, /* rfc4403::UDDI_IS_HIDDEN */
    {"1.3.6.1.1.10.4.29", "uddiIsProjection"}, /* rfc4403::UDDI_IS_PROJECTION */
    {"1.3.6.1.1.10.4.3", "uddiOperator"}, /* rfc4403::UDDI_OPERATOR */
    {"1.3.6.1.1.10.4.30", "uddiLang"}, /* rfc4403::UDDI_LANG */
    {"1.3.6.1.1.10.4.31", "uddiv3BusinessKey"}, /* rfc4403::UDDIV_3_BUSINESS_KEY */
    {"1.3.6.1.1.10.4.32", "uddiv3ServiceKey"}, /* rfc4403::UDDIV_3_SERVICE_KEY */
    {"1.3.6.1.1.10.4.33", "uddiv3BindingKey"}, /* rfc4403::UDDIV_3_BINDING_KEY */
    {"1.3.6.1.1.10.4.34", "uddiv3TmodelKey"}, /* rfc4403::UDDIV_3_TMODEL_KEY */
    {"1.3.6.1.1.10.4.35", "uddiv3DigitalSignature"}, /* rfc4403::UDDIV_3_DIGITAL_SIGNATURE */
    {"1.3.6.1.1.10.4.36", "uddiv3NodeId"}, /* rfc4403::UDDIV_3_NODE_ID */
    {"1.3.6.1.1.10.4.37", "uddiv3EntityModificationTime"}, /* rfc4403::UDDIV_3_ENTITY_MODIFICATION_TIME */
    {"1.3.6.1.1.10.4.38", "uddiv3SubscriptionKey"}, /* rfc4403::UDDIV_3_SUBSCRIPTION_KEY */
    {"1.3.6.1.1.10.4.39", "uddiv3SubscriptionFilter"}, /* rfc4403::UDDIV_3_SUBSCRIPTION_FILTER */
    {"1.3.6.1.1.10.4.4", "uddiName"}, /* rfc4403::UDDI_NAME */
    {"1.3.6.1.1.10.4.40", "uddiv3NotificationInterval"}, /* rfc4403::UDDIV_3_NOTIFICATION_INTERVAL */
    {"1.3.6.1.1.10.4.41", "uddiv3MaxEntities"}, /* rfc4403::UDDIV_3_MAX_ENTITIES */
    {"1.3.6.1.1.10.4.42", "uddiv3ExpiresAfter"}, /* rfc4403::UDDIV_3_EXPIRES_AFTER */
    {"1.3.6.1.1.10.4.43", "uddiv3BriefResponse"}, /* rfc4403::UDDIV_3_BRIEF_RESPONSE */
    {"1.3.6.1.1.10.4.44", "uddiv3EntityKey"}, /* rfc4403::UDDIV_3_ENTITY_KEY */
    {"1.3.6.1.1.10.4.45", "uddiv3EntityCreationTime"}, /* rfc4403::UDDIV_3_ENTITY_CREATION_TIME */
    {"1.3.6.1.1.10.4.46", "uddiv3EntityDeletionTime"}, /* rfc4403::UDDIV_3_ENTITY_DELETION_TIME */
    {"1.3.6.1.1.10.4.5", "uddiDescription"}, /* rfc4403::UDDI_DESCRIPTION */
    {"1.3.6.1.1.10.4.6", "uddiDiscoveryURLs"}, /* rfc4403::UDDI_DISCOVERY_UR_LS */
    {"1.3.6.1.1.10.4.7", "uddiUseType"}, /* rfc4403::UDDI_USE_TYPE */
    {"1.3.6.1.1.10.4.8", "uddiPersonName"}, /* rfc4403::UDDI_PERSON_NAME */
    {"1.3.6.1.1.10.4.9", "uddiPhone"}, /* rfc4403::UDDI_PHONE */
    {"1.3.6.1.1.10.6.1", "uddiBusinessEntity"}, /* rfc4403::UDDI_BUSINESS_ENTITY */
    {"1.3.6.1.1.10.6.10", "uddiv3EntityObituary"}, /* rfc4403::UDDIV_3_ENTITY_OBITUARY */
    {"1.3.6.1.1.10.6.2", "uddiContact"}, /* rfc4403::UDDI_CONTACT */
    {"1.3.6.1.1.10.6.3", "uddiAddress"}, /* rfc4403::UDDI_ADDRESS */
    {"1.3.6.1.1.10.6.4", "uddiBusinessService"}, /* rfc4403::UDDI_BUSINESS_SERVICE */
    {"1.3.6.1.1.10.6.5", "uddiBindingTemplate"}, /* rfc4403::UDDI_BINDING_TEMPLATE */
    {"1.3.6.1.1.10.6.6", "uddiTModelInstanceInfo"}, /* rfc4403::UDDI_T_MODEL_INSTANCE_INFO */
    {"1.3.6.1.1.10.6.7", "uddiTModel"}, /* rfc4403::UDDI_T_MODEL */
    {"1.3.6.1.1.10.6.8", "uddiPublisherAssertion"}, /* rfc4403::UDDI_PUBLISHER_ASSERTION */
    {"1.3.6.1.1.10.6.9", "uddiv3Subscription"}, /* rfc4403::UDDIV_3_SUBSCRIPTION */
    {"1.3.6.1.4.1.1466.101.120.111", "extensibleObject"}, /* rfc4512::EXTENSIBLE_OBJECT */
    {"1.3.6.1.4.1.1466.101.120.13", "supportedControl"}, /* rfc4512::SUPPORTED_CONTROL */
    {"1.3.6.1.4.1.1466.101.120.14", "supportedSASLMechanisms"}, /* rfc4512::SUPPORTED_SASL_MECHANISMS */
    {"1.3.6.1.4.1.1466.101.120.15", "supportedLDAPVersion"}, /* rfc4512::SUPPORTED_LDAP_VERSION */
    {"1.3.6.1.4.1.1466.101.120.16", "ldapSyntaxes"}, /* rfc4512::LDAP_SYNTAXES */
    {"1.3.6.1.4.1.1466.101.120.5", "namingContexts"}, /* rfc4512::NAMING_CONTEXTS */
    {"1.3.6.1.4.1.1466.101.120.6", "altServer"}, /* rfc4512::ALT_SERVER */
    {"1.3.6.1.4.1.1466.101.120.7", "supportedExtension"}, /* rfc4512::SUPPORTED_EXTENSION */
    {"1.3.6.1.4.1.4203.1.3.5", "supportedFeatures"}, /* rfc4512::SUPPORTED_FEATURES */
    {"2.5.18.1", "createTimestamp"}, /* rfc4512::CREATE_TIMESTAMP */
    {"2.5.18.10", "subschemaSubentry"}, /* rfc4512::SUBSCHEMA_SUBENTRY */
    {"2.5.18.2", "modifyTimestamp"}, /* rfc4512::MODIFY_TIMESTAMP */
    {"2.5.18.3", "creatorsName"}, /* rfc4512::CREATORS_NAME */
    {"2.5.18.4", "modifiersName"}, /* rfc4512::MODIFIERS_NAME */
    {"2.5.20.1", "subschema"}, /* rfc4512::SUBSCHEMA */
    {"2.5.21.1", "dITStructureRules"}, /* rfc4512::DIT_STRUCTURE_RULES */
    {"2.5.21.10", "governingStructureRule"}, /* rfc4512::GOVERNING_STRUCTURE_RULE */
    {"2.5.21.2", "dITContentRules"}, /* rfc4512::DIT_CONTENT_RULES */
    {"2.5.21.4", "matchingRules"}, /* rfc4512::MATCHING_RULES */
    {"2.5.21.5", "attributeTypes"}, /* rfc4512::ATTRIBUTE_TYPES */
    {"2.5.21.6", "objectClasses"}, /* rfc4512::OBJECT_CLASSES */
    {"2.5.21.7", "nameForms"}, /* rfc4512::NAME_FORMS */
    {"2.5.21.8", "matchingRuleUse"}, /* rfc4512::MATCHING_RULE_USE */
    {"2.5.21.9", "structuralObjectClass"}, /* rfc4512::STRUCTURAL_OBJECT_CLASS */
    {"2.5.4.0", "objectClass"}, /* rfc4512::OBJECT_CLASS */
    {"2.5.4.1", "aliasedObjectName"}, /* rfc4512::ALIASED_OBJECT_NAME */
    {"2.5.6.0", "top"}, /* rfc4512::TOP */
    {"2.5.6.1", "alias"}, /* rfc4512::ALIAS */
    {"1.3.6.1.4.1.1466.109.114.1", "caseExactIA5Match"}, /* rfc4517::CASE_EXACT_IA_5_MATCH */
    {"1.3.6.1.4.1.1466.109.114.2", "caseIgnoreIA5Match"}, /* rfc4517::CASE_IGNORE_IA_5_MATCH */
    {"1.3.6.1.4.1.1466.109.114.3", "caseIgnoreIA5SubstringsMatch"}, /* rfc4517::CASE_IGNORE_IA_5_SUBSTRINGS_MATCH */
    {"2.5.13.0", "objectIdentifierMatch"}, /* rfc4517::OBJECT_IDENTIFIER_MATCH */
    {"2.5.13.1", "distinguishedNameMatch"}, /* rfc4517::DISTINGUISHED_NAME_MATCH */
    {"2.5.13.10", "numericStringSubstringsMatch"}, /* rfc4517::NUMERIC_STRING_SUBSTRINGS_MATCH */
    {"2.5.13.11", "caseIgnoreListMatch"}, /* rfc4517::CASE_IGNORE_LIST_MATCH */
    {"2.5.13.12", "caseIgnoreListSubstringsMatch"}, /* rfc4517::CASE_IGNORE_LIST_SUBSTRINGS_MATCH */
    {"2.5.13.13", "booleanMatch"}, /* rfc4517::BOOLEAN_MATCH */
    {"2.5.13.14", "integerMatch"}, /* rfc4517::INTEGER_MATCH */
    {"2.5.13.15", "integerOrderingMatch"}, /* rfc4517::INTEGER_ORDERING_MATCH */
    {"2.5.13.16", "bitStringMatch"}, /* rfc4517::BIT_STRING_MATCH */
    {"2.5.13.17", "octetStringMatch"}, /* rfc4517::OCTET_STRING_MATCH */
    {"2.5.13.18", "octetStringOrderingMatch"}, /* rfc4517::OCTET_STRING_ORDERING_MATCH */
    {"2.5.13.2", "caseIgnoreMatch"}, /* rfc4517::CASE_IGNORE_MATCH */
    {"2.5.13.20", "telephoneNumberMatch"}, /* rfc4517::TELEPHONE_NUMBER_MATCH */
    {"2.5.13.21", "telephoneNumberSubstringsMatch"}, /* rfc4517::TELEPHONE_NUMBER_SUBSTRINGS_MATCH */
    {"2.5.13.23", "uniqueMemberMatch"}, /* rfc4517::UNIQUE_MEMBER_MATCH */
    {"2.5.13.27", "generalizedTimeMatch"}, /* rfc4517::GENERALIZED_TIME_MATCH */
    {"2.5.13.28", "generalizedTimeOrderingMatch"}, /* rfc4517::GENERALIZED_TIME_ORDERING_MATCH */
    {"2.5.13.29", "integerFirstComponentMatch"}, /* rfc4517::INTEGER_FIRST_COMPONENT_MATCH */
    {"2.5.13.3", "caseIgnoreOrderingMatch"}, /* rfc4517::CASE_IGNORE_ORDERING_MATCH */
    {"2.5.13.30", "objectIdentifierFirstComponentMatch"}, /* rfc4517::OBJECT_IDENTIFIER_FIRST_COMPONENT_MATCH */
    {"2.5.13.31", "directoryStringFirstComponentMatch"}, /* rfc4517::DIRECTORY_STRING_FIRST_COMPONENT_MATCH */
    {"2.5.13.32", "wordMatch"}, /* rfc4517::WORD_MATCH */
    {"2.5.13.33", "keywordMatch"}, /* rfc4517::KEYWORD_MATCH */
    {"2.5.13.4", "caseIgnoreSubstringsMatch"}, /* rfc4517::CASE_IGNORE_SUBSTRINGS_MATCH */
    {"2.5.13.5", "caseExactMatch"}, /* rfc4517::CASE_EXACT_MATCH */
    {"2.5.13.6", "caseExactOrderingMatch"}, /* rfc4517::CASE_EXACT_ORDERING_MATCH */
    {"2.5.13.7", "caseExactSubstringsMatch"}, /* rfc4517::CASE_EXACT_SUBSTRINGS_MATCH */
    {"2.5.13.8", "numericStringMatch"}, /* rfc4517::NUMERIC_STRING_MATCH */
    {"2.5.13.9", "numericStringOrderingMatch"}, /* rfc4517::NUMERIC_STRING_ORDERING_MATCH */
    {"0.9.2342.19200300.100.1.1", "uid"}, /* rfc4519::UID */
    {"0.9.2342.19200300.100.1.25", "DC"}, /* rfc4519::DC */
    {"1.3.6.1.1.3.1", "uidObject"}, /* rfc4519::UID_OBJECT */
    {"1.3.6.1.4.1.1466.344", "dcObject"}, /* rfc4519::DC_OBJECT */
    {"2.5.4.12", "title"}, /* rfc4519::TITLE */
    {"2.5.4.13", "description"}, /* rfc4519::DESCRIPTION */
    {"2.5.4.14", "searchGuide"}, /* rfc4519::SEARCH_GUIDE */
    {"2.5.4.15", "businessCategory"}, /* rfc4519::BUSINESS_CATEGORY */
    {"2.5.4.16", "postalAddress"}, /* rfc4519::POSTAL_ADDRESS */
    {"2.5.4.17", "postalCode"}, /* rfc4519::POSTAL_CODE */
    {"2.5.4.18", "postOfficeBox"}, /* rfc4519::POST_OFFICE_BOX */
    {"2.5.4.19", "physicalDeliveryOfficeName"}, /* rfc4519::PHYSICAL_DELIVERY_OFFICE_NAME */
    {"2.5.4.20", "telephoneNumber"}, /* rfc4519::TELEPHONE_NUMBER */
    {"2.5.4.21", "telexNumber"}, /* rfc4519::TELEX_NUMBER */
    {"2.5.4.22", "teletexTerminalIdentifier"}, /* rfc4519::TELETEX_TERMINAL_IDENTIFIER */
    {"2.5.4.23", "facsimileTelephoneNumber"}, /* rfc4519::FACSIMILE_TELEPHONE_NUMBER */
    {"2.5.4.24", "x121Address"}, /* rfc4519::X_121_ADDRESS */
    {"2.5.4.25", "internationaliSDNNumber"}, /* rfc4519::INTERNATIONALI_SDN_NUMBER */
    {"2.5.4.26", "registeredAddress"}, /* rfc4519::REGISTERED_ADDRESS */
    {"2.5.4.27", "destinationIndicator"}, /* rfc4519::DESTINATION_INDICATOR */
    {"2.5.4.28", "preferredDeliveryMethod"}, /* rfc4519::PREFERRED_DELIVERY_METHOD */
    {"2.5.4.31", "member"}, /* rfc4519::MEMBER */
    {"2.5.4.32", "owner"}, /* rfc4519::OWNER */
    {"2.5.4.33", "roleOccupant"}, /* rfc4519::ROLE_OCCUPANT */
    {"2.5.4.34", "seeAlso"}, /* rfc4519::SEE_ALSO */
    {"2.5.4.35", "userPassword"}, /* rfc4519::USER_PASSWORD */
    {"2.5.4.4", "sn"}, /* rfc4519::SN */
    {"2.5.4.41", "name"}, /* rfc4519::NAME */
    {"2.5.4.42", "GN"}, /* rfc4519::GN */
    {"2.5.4.43", "initials"}, /* rfc4519::INITIALS */
    {"2.5.4.44", "generationQualifier"}, /* rfc4519::GENERATION_QUALIFIER */
    {"2.5.4.45", "x500UniqueIdentifier"}, /* rfc4519::X_500_UNIQUE_IDENTIFIER */
    {"2.5.4.46", "dnQualifier"}, /* rfc4519::DN_QUALIFIER */
    {"2.5.4.47", "enhancedSearchGuide"}, /* rfc4519::ENHANCED_SEARCH_GUIDE */
    {"2.5.4.49", "distinguishedName"}, /* rfc4519::DISTINGUISHED_NAME */
    {"2.5.4.5", "serialNumber"}, /* rfc4519::SERIAL_NUMBER */
    {"2.5.4.50", "uniqueMember"}, /* rfc4519::UNIQUE_MEMBER */
    {"2.5.4.51", "houseIdentifier"}, /* rfc4519::HOUSE_IDENTIFIER */
    {"2.5.4.7", "L"}, /* rfc4519::L */
    {"2.5.6.10", "residentialPerson"}, /* rfc4519::RESIDENTIAL_PERSON */
    {"2.5.6.11", "applicationProcess"}, /* rfc4519::APPLICATION_PROCESS */
    {"2.5.6.14", "device"}, /* rfc4519::DEVICE */
    {"2.5.6.17", "groupOfUniqueNames"}, /* rfc4519::GROUP_OF_UNIQUE_NAMES */
    {"2.5.6.2", "country"}, /* rfc4519::COUNTRY */
    {"2.5.6.3", "locality"}, /* rfc4519::LOCALITY */
    {"2.5.6.4", "organization"}, /* rfc4519::ORGANIZATION */
    {"2.5.6.5", "organizationalUnit"}, /* rfc4519::ORGANIZATIONAL_UNIT */
    {"2.5.6.6", "person"}, /* rfc4519::PERSON */
    {"2.5.6.7", "organizationalPerson"}, /* rfc4519::ORGANIZATIONAL_PERSON */
    {"2.5.6.8", "organizationalRole"}, /* rfc4519::ORGANIZATIONAL_ROLE */
    {"2.5.6.9", "groupOfNames"}, /* rfc4519::GROUP_OF_NAMES */
    {"2.5.13.34", "certificateExactMatch"}, /* rfc4523::CERTIFICATE_EXACT_MATCH */
    {"2.5.13.35", "certificateMatch"}, /* rfc4523::CERTIFICATE_MATCH */
    {"2.5.13.36", "certificatePairExactMatch"}, /* rfc4523::CERTIFICATE_PAIR_EXACT_MATCH */
    {"2.5.13.37", "certificatePairMatch"}, /* rfc4523::CERTIFICATE_PAIR_MATCH */
    {"2.5.13.38", "certificateListExactMatch"}, /* rfc4523::CERTIFICATE_LIST_EXACT_MATCH */
    {"2.5.13.39", "certificateListMatch"}, /* rfc4523::CERTIFICATE_LIST_MATCH */
    {"2.5.13.40", "algorithmIdentifierMatch"}, /* rfc4523::ALGORITHM_IDENTIFIER_MATCH */
    {"2.5.4.36", "userCertificate"}, /* rfc4523::USER_CERTIFICATE */
    {"2.5.4.37", "cACertificate"}, /* rfc4523::CA_CERTIFICATE */
    {"2.5.4.38", "authorityRevocationList"}, /* rfc4523::AUTHORITY_REVOCATION_LIST */
    {"2.5.4.39", "certificateRevocationList"}, /* rfc4523::CERTIFICATE_REVOCATION_LIST */
    {"2.5.4.40", "crossCertificatePair"}, /* rfc4523::CROSS_CERTIFICATE_PAIR */
    {"2.5.4.52", "supportedAlgorithms"}, /* rfc4523::SUPPORTED_ALGORITHMS */
    {"2.5.4.53", "deltaRevocationList"}, /* rfc4523::DELTA_REVOCATION_LIST */
    {"2.5.6.15", "strongAuthenticationUser"}, /* rfc4523::STRONG_AUTHENTICATION_USER */
    {"2.5.6.16", "certificationAuthority"}, /* rfc4523::CERTIFICATION_AUTHORITY */
    {"2.5.6.16.2", "certificationAuthority-V2"}, /* rfc4523::CERTIFICATION_AUTHORITY_V_2 */
    {"2.5.6.18", "userSecurityInformation"}, /* rfc4523::USER_SECURITY_INFORMATION */
    {"2.5.6.19", "cRLDistributionPoint"}, /* rfc4523::CRL_DISTRIBUTION_POINT */
    {"2.5.6.21", "pkiUser"}, /* rfc4523::PKI_USER */
    {"2.5.6.22", "pkiCA"}, /* rfc4523::PKI_CA */
    {"2.5.6.23", "deltaCRL"}, /* rfc4523::DELTA_CRL */
    {"0.9.2342.19200300.100.1.10", "manager"}, /* rfc4524::MANAGER */
    {"0.9.2342.19200300.100.1.11", "documentIdentifier"}, /* rfc4524::DOCUMENT_IDENTIFIER */
    {"0.9.2342.19200300.100.1.12", "documentTitle"}, /* rfc4524::DOCUMENT_TITLE */
    {"0.9.2342.19200300.100.1.13", "documentVersion"}, /* rfc4524::DOCUMENT_VERSION */
    {"0.9.2342.19200300.100.1.14", "documentAuthor"}, /* rfc4524::DOCUMENT_AUTHOR */
    {"0.9.2342.19200300.100.1.15", "documentLocation"}, /* rfc4524::DOCUMENT_LOCATION */
    {"0.9.2342.19200300.100.1.20", "homePhone"}, /* rfc4524::HOME_PHONE */
    {"0.9.2342.19200300.100.1.21", "secretary"}, /* rfc4524::SECRETARY */
    {"0.9.2342.19200300.100.1.3", "mail"}, /* rfc4524::MAIL */
    {"0.9.2342.19200300.100.1.37", "associatedDomain"}, /* rfc4524::ASSOCIATED_DOMAIN */
    {"0.9.2342.19200300.100.1.38", "associatedName"}, /* rfc4524::ASSOCIATED_NAME */
    {"0.9.2342.19200300.100.1.39", "homePostalAddress"}, /* rfc4524::HOME_POSTAL_ADDRESS */
    {"0.9.2342.19200300.100.1.4", "info"}, /* rfc4524::INFO */
    {"0.9.2342.19200300.100.1.40", "personalTitle"}, /* rfc4524::PERSONAL_TITLE */
    {"0.9.2342.19200300.100.1.41", "mobile"}, /* rfc4524::MOBILE */
    {"0.9.2342.19200300.100.1.42", "pager"}, /* rfc4524::PAGER */
    {"0.9.2342.19200300.100.1.43", "co"}, /* rfc4524::CO */
    {"0.9.2342.19200300.100.1.44", "uniqueIdentifier"}, /* rfc4524::UNIQUE_IDENTIFIER */
    {"0.9.2342.19200300.100.1.45", "organizationalStatus"}, /* rfc4524::ORGANIZATIONAL_STATUS */
    {"0.9.2342.19200300.100.1.48", "buildingName"}, /* rfc4524::BUILDING_NAME */
    {"0.9.2342.19200300.100.1.5", "drink"}, /* rfc4524::DRINK */
    {"0.9.2342.19200300.100.1.50", "singleLevelQuality"}, /* rfc4524::SINGLE_LEVEL_QUALITY */
    {"0.9.2342.19200300.100.1.56", "documentPublisher"}, /* rfc4524::DOCUMENT_PUBLISHER */
    {"0.9.2342.19200300.100.1.6", "roomNumber"}, /* rfc4524::ROOM_NUMBER */
    {"0.9.2342.19200300.100.1.8", "userClass"}, /* rfc4524::USER_CLASS */
    {"0.9.2342.19200300.100.1.9", "host"}, /* rfc4524::HOST */
    {"0.9.2342.19200300.100.4.13", "domain"}, /* rfc4524::DOMAIN */
    {"0.9.2342.19200300.100.4.14", "RFC822LocalPart"}, /* rfc4524::RFC_822_LOCAL_PART */
    {"0.9.2342.19200300.100.4.17", "domainRelatedObject"}, /* rfc4524::DOMAIN_RELATED_OBJECT */
    {"0.9.2342.19200300.100.4.18", "friendlyCountry"}, /* rfc4524::FRIENDLY_COUNTRY */
    {"0.9.2342.19200300.100.4.19", "simpleSecurityObject"}, /* rfc4524::SIMPLE_SECURITY_OBJECT */
    {"0.9.2342.19200300.100.4.5", "account"}, /* rfc4524::ACCOUNT */
    {"0.9.2342.19200300.100.4.6", "document"}, /* rfc4524::DOCUMENT */
    {"0.9.2342.19200300.100.4.7", "room"}, /* rfc4524::ROOM */
    {"0.9.2342.19200300.100.4.8", "documentSeries"}, /* rfc4524::DOCUMENT_SERIES */
    {"1.3.6.1.1.16.2", "uuidMatch"}, /* rfc4530::UUID_MATCH */
    {"1.3.6.1.1.16.3", "uuidOrderingMatch"}, /* rfc4530::UUID_ORDERING_MATCH */
    {"1.3.6.1.1.16.4", "entryUUID"}, /* rfc4530::ENTRY_UUID */
    {"1.3.6.1.4.1.11.1.3.1.1.0", "defaultServerList"}, /* rfc4876::DEFAULT_SERVER_LIST */
    {"1.3.6.1.4.1.11.1.3.1.1.1", "defaultSearchBase"}, /* rfc4876::DEFAULT_SEARCH_BASE */
    {"1.3.6.1.4.1.11.1.3.1.1.10", "credentialLevel"}, /* rfc4876::CREDENTIAL_LEVEL */
    {"1.3.6.1.4.1.11.1.3.1.1.11", "objectclassMap"}, /* rfc4876::OBJECTCLASS_MAP */
    {"1.3.6.1.4.1.11.1.3.1.1.12", "defaultSearchScope"}, /* rfc4876::DEFAULT_SEARCH_SCOPE */
    {"1.3.6.1.4.1.11.1.3.1.1.13", "serviceCredentialLevel"}, /* rfc4876::SERVICE_CREDENTIAL_LEVEL */
    {"1.3.6.1.4.1.11.1.3.1.1.14", "serviceSearchDescriptor"}, /* rfc4876::SERVICE_SEARCH_DESCRIPTOR */
    {"1.3.6.1.4.1.11.1.3.1.1.15", "serviceAuthenticationMethod"}, /* rfc4876::SERVICE_AUTHENTICATION_METHOD */
    {"1.3.6.1.4.1.11.1.3.1.1.16", "dereferenceAliases"}, /* rfc4876::DEREFERENCE_ALIASES */
    {"1.3.6.1.4.1.11.1.3.1.1.2", "preferredServerList"}, /* rfc4876::PREFERRED_SERVER_LIST */
    {"1.3.6.1.4.1.11.1.3.1.1.3", "searchTimeLimit"}, /* rfc4876::SEARCH_TIME_LIMIT */
    {"1.3.6.1.4.1.11.1.3.1.1.4", "bindTimeLimit"}, /* rfc4876::BIND_TIME_LIMIT */
    {"1.3.6.1.4.1.11.1.3.1.1.5", "followReferrals"}, /* rfc4876::FOLLOW_REFERRALS */
    {"1.3.6.1.4.1.11.1.3.1.1.6", "authenticationMethod"}, /* rfc4876::AUTHENTICATION_METHOD */
    {"1.3.6.1.4.1.11.1.3.1.1.7", "profileTTL"}, /* rfc4876::PROFILE_TTL */
    {"1.3.6.1.4.1.11.1.3.1.1.9", "attributeMap"}, /* rfc4876::ATTRIBUTE_MAP */
    {"1.3.6.1.4.1.11.1.3.1.2.5", "DUAConfigProfile"}, /* rfc4876::DUA_CONFIG_PROFILE */
    {"1.3.6.1.1.20", "entryDN"}, /* rfc5020::ENTRY_DN */
    {"1.3.6.1.5.5.7", "id-pkix"}, /* rfc5280::ID_PKIX */
    {"1.3.6.1.5.5.7.1", "id-pe"}, /* rfc5280::ID_PE */
    {"1.3.6.1.5.5.7.1.1", "id-pe-authorityInfoAccess"}, /* rfc5280::ID_PE_AUTHORITY_INFO_ACCESS */
    {"1.3.6.1.5.5.7.1.11", "id-pe-subjectInfoAccess"}, /* rfc5280::ID_PE_SUBJECT_INFO_ACCESS */
    {"1.3.6.1.5.5.7.2", "id-qt"}, /* rfc5280::ID_QT */
    {"1.3.6.1.5.5.7.2.1", "id-qt-cps"}, /* rfc5280::ID_QT_CPS */
    {"1.3.6.1.5.5.7.2.2", "id-qt-unotice"}, /* rfc5280::ID_QT_UNOTICE */
    {"1.3.6.1.5.5.7.3", "id-kp"}, /* rfc5280::ID_KP */
    {"1.3.6.1.5.5.7.3.1", "id-kp-serverAuth"}, /* rfc5280::ID_KP_SERVER_AUTH */
    {"1.3.6.1.5.5.7.3.2", "id-kp-clientAuth"}, /* rfc5280::ID_KP_CLIENT_AUTH */
    {"1.3.6.1.5.5.7.3.3", "id-kp-codeSigning"}, /* rfc5280::ID_KP_CODE_SIGNING */
    {"1.3.6.1.5.5.7.3.4", "id-kp-emailProtection"}, /* rfc5280::ID_KP_EMAIL_PROTECTION */
    {"1.3.6.1.5.5.7.3.8", "id-kp-timeStamping"}, /* rfc5280::ID_KP_TIME_STAMPING */
    {"1.3.6.1.5.5.7.3.9", "id-kp-OCSPSigning"}, /* rfc5280::ID_KP_OCSP_SIGNING */
    {"1.3.6.1.5.5.7.48", "id-ad"}, /* rfc5280::ID_AD */
    {"1.3.6.1.5.5.7.48.1", "id-ad-ocsp"}, /* rfc5280::ID_AD_OCSP */
    {"1.3.6.1.5.5.7.48.2", "id-ad-caIssuers"}, /* rfc5280::ID_AD_CA_ISSUERS */
    {"1.3.6.1.5.5.7.48.3", "id-ad-timeStamping"}, /* rfc5280::ID_AD_TIME_STAMPING */
    {"1.3.6.1.5.5.7.48.5", "id-ad-caRepository"}, /* rfc5280::ID_AD_CA_REPOSITORY */
    {"2.2.840.10040.2", "holdInstruction"}, /* rfc5280::HOLD_INSTRUCTION */
    {"2.2.840.10040.2.1", "id-holdinstruction-none"}, /* rfc5280::ID_HOLDINSTRUCTION_NONE */
    {"2.2.840.10040.2.2", "id-holdinstruction-callissuer"}, /* rfc5280::ID_HOLDINSTRUCTION_CALLISSUER */
    {"2.2.840.10040.2.3", "id-holdinstruction-reject"}, /* rfc5280::ID_HOLDINSTRUCTION_REJECT */
    {"2.5.29", "id-ce"}, /* rfc5280::ID_CE */
    {"2.5.29.14", "id-ce-subjectKeyIdentifier"}, /* rfc5280::ID_CE_SUBJECT_KEY_IDENTIFIER */
    {"2.5.29.15", "id-ce-keyUsage"}, /* rfc5280::ID_CE_KEY_USAGE */
    {"2.5.29.16", "id-ce-privateKeyUsagePeriod"}, /* rfc5280::ID_CE_PRIVATE_KEY_USAGE_PERIOD */
    {"2.5.29.17", "id-ce-subjectAltName"}, /* rfc5280::ID_CE_SUBJECT_ALT_NAME */
    {"2.5.29.18", "id-ce-issuerAltName"}, /* rfc5280::ID_CE_ISSUER_ALT_NAME */
    {"2.5.29.19", "id-ce-basicConstraints"}, /* rfc5280::ID_CE_BASIC_CONSTRAINTS */
    {"2.5.29.20", "id-ce-cRLNumber"}, /* rfc5280::ID_CE_CRL_NUMBER */
    {"2.5.29.21", "id-ce-cRLReasons"}, /* rfc5280::ID_CE_CRL_REASONS */
    {"2.5.29.23", "id-ce-holdInstructionCode"}, /* rfc5280::ID_CE_HOLD_INSTRUCTION_CODE */
    {"2.5.29.24", "id-ce-invalidityDate"}, /* rfc5280::ID_CE_INVALIDITY_DATE */
    {"2.5.29.27", "id-ce-deltaCRLIndicator"}, /* rfc5280::ID_CE_DELTA_CRL_INDICATOR */
    {"2.5.29.28", "id-ce-issuingDistributionPoint"}, /* rfc5280::ID_CE_ISSUING_DISTRIBUTION_POINT */
    {"2.5.29.29", "id-ce-certificateIssuer"}, /* rfc5280::ID_CE_CERTIFICATE_ISSUER */
    {"2.5.29.30", "id-ce-nameConstraints"}, /* rfc5280::ID_CE_NAME_CONSTRAINTS */
    {"2.5.29.31", "id-ce-cRLDistributionPoints"}, /* rfc5280::ID_CE_CRL_DISTRIBUTION_POINTS */
    {"2.5.29.32", "id-ce-certificatePolicies"}, /* rfc5280::ID_CE_CERTIFICATE_POLICIES */
    {"2.5.29.32.0", "anyPolicy"}, /* rfc5280::ANY_POLICY */
    {"2.5.29.33", "id-ce-policyMappings"}, /* rfc5280::ID_CE_POLICY_MAPPINGS */
    {"2.5.29.35", "id-ce-authorityKeyIdentifier"}, /* rfc5280::ID_CE_AUTHORITY_KEY_IDENTIFIER */
    {"2.5.29.36", "id-ce-policyConstraints"}, /* rfc5280::ID_CE_POLICY_CONSTRAINTS */
    {"2.5.29.37", "id-ce-extKeyUsage"}, /* rfc5280::ID_CE_EXT_KEY_USAGE */
    {"2.5.29.37.0", "anyExtendedKeyUsage"}, /* rfc5280::ANY_EXTENDED_KEY_USAGE */
    {"2.5.29.46", "id-ce-freshestCRL"}, /* rfc5280::ID_CE_FRESHEST_CRL */
    {"2.5.29.54", "id-ce-inhibitAnyPolicy"}, /* rfc5280::ID_CE_INHIBIT_ANY_POLICY */
    {"2.5.29.9", "id-ce-subjectDirectoryAttributes"}, /* rfc5280::ID_CE_SUBJECT_DIRECTORY_ATTRIBUTES */
    {"2.5.4", "id-at"}, /* rfc5280::ID_AT */
    {"1.3.36.3.3.2.8.1", "ellipticCurve"}, /* rfc5639::ELLIPTIC_CURVE */
    {"1.3.36.3.3.2.8.1.1", "versionOne"}, /* rfc5639::VERSION_ONE */
    {"1.3.36.3.3.2.8.1.1.1", "brainpoolP160r1"}, /* rfc5639::BRAINPOOL_P_160_R_1 */
    {"1.3.36.3.3.2.8.1.1.10", "brainpoolP320t1"}, /* rfc5639::BRAINPOOL_P_320_T_1 */
    {"1.3.36.3.3.2.8.1.1.11", "brainpoolP384r1"}, /* rfc5639::BRAINPOOL_P_384_R_1 */
    {"1.3.36.3.3.2.8.1.1.12", "brainpoolP384t1"}, /* rfc5639::BRAINPOOL_P_384_T_1 */
    {"1.3.36.3.3.2.8.1.1.13", "brainpoolP512r1"}, /* rfc5639::BRAINPOOL_P_512_R_1 */
    {"1.3.36.3.3.2.8.1.1.14", "brainpoolP512t1"}, /* rfc5639::BRAINPOOL_P_512_T_1 */
    {"1.3.36.3.3.2.8.1.1.2", "brainpoolP160t1"}, /* rfc5639::BRAINPOOL_P_160_T_1 */
    {"1.3.36.3.3.2.8.1.1.3", "brainpoolP192r1"}, /* rfc5639::BRAINPOOL_P_192_R_1 */
    {"1.3.36.3.3.2.8.1.1.4", "brainpoolP192t1"}, /* rfc5639::BRAINPOOL_P_192_T_1 */
    {"1.3.36.3.3.2.8.1.1.5", "brainpoolP224r1"}, /* rfc5639::BRAINPOOL_P_224_R_1 */
    {"1.3.36.3.3.2.8.1.1.6", "brainpoolP224t1"}, /* rfc5639::BRAINPOOL_P_224_T_1 */
    {"1.3.36.3.3.2.8.1.1.7", "brainpoolP256r1"}, /* rfc5639::BRAINPOOL_P_256_R_1 */
    {"1.3.36.3.3.2.8.1.1.8", "brainpoolP256t1"}, /* rfc5639::BRAINPOOL_P_256_T_1 */
    {"1.3.36.3.3.2.8.1.1.9", "brainpoolP320r1"}, /* rfc5639::BRAINPOOL_P_320_R_1 */
    {"1.2.840.10045", "ansi-x9-62"}, /* rfc5753::ANSI_X_9_62 */
    {"1.2.840.10045.2.1", "id-ecPublicKey"}, /* rfc5753::ID_EC_PUBLIC_KEY */
    {"1.2.840.113549.2.10", "id-hmacWithSHA384"}, /* rfc5753::ID_HMAC_WITH_SHA_384 */
    {"1.2.840.113549.2.11", "id-hmacWithSHA512"}, /* rfc5753::ID_HMAC_WITH_SHA_512 */
    {"1.2.840.113549.2.8", "id-hmacWithSHA224"}, /* rfc5753::ID_HMAC_WITH_SHA_224 */
    {"1.2.840.113549.2.9", "id-hmacWithSHA256"}, /* rfc5753::ID_HMAC_WITH_SHA_256 */
    {"1.3.132.1", "secg-scheme"}, /* rfc5753::SECG_SCHEME */
    {"1.3.132.1.11.0", "dhSinglePass-stdDH-sha224kdf-scheme"}, /* rfc5753::DH_SINGLE_PASS_STD_DH_SHA_224_KDF_SCHEME */
    {"1.3.132.1.11.1", "dhSinglePass-stdDH-sha256kdf-scheme"}, /* rfc5753::DH_SINGLE_PASS_STD_DH_SHA_256_KDF_SCHEME */
    {"1.3.132.1.11.2", "dhSinglePass-stdDH-sha384kdf-scheme"}, /* rfc5753::DH_SINGLE_PASS_STD_DH_SHA_384_KDF_SCHEME */
    {"1.3.132.1.11.3", "dhSinglePass-stdDH-sha512kdf-scheme"}, /* rfc5753::DH_SINGLE_PASS_STD_DH_SHA_512_KDF_SCHEME */
    {"1.3.132.1.14.0", "dhSinglePass-cofactorDH-sha224kdf-scheme"}, /* rfc5753::DH_SINGLE_PASS_COFACTOR_DH_SHA_224_KDF_SCHEME */
    {"1.3.132.1.14.1", "dhSinglePass-cofactorDH-sha256kdf-scheme"}, /* rfc5753::DH_SINGLE_PASS_COFACTOR_DH_SHA_256_KDF_SCHEME */
    {"1.3.132.1.14.2", "dhSinglePass-cofactorDH-sha384kdf-scheme"}, /* rfc5753::DH_SINGLE_PASS_COFACTOR_DH_SHA_384_KDF_SCHEME */
    {"1.3.132.1.14.3", "dhSinglePass-cofactorDH-sha512kdf-scheme"}, /* rfc5753::DH_SINGLE_PASS_COFACTOR_DH_SHA_512_KDF_SCHEME */
    {"1.3.132.1.15.0", "mqvSinglePass-sha224kdf-scheme"}, /* rfc5753::MQV_SINGLE_PASS_SHA_224_KDF_SCHEME */
    {"1.3.132.1.15.1", "mqvSinglePass-sha256kdf-scheme"}, /* rfc5753::MQV_SINGLE_PASS_SHA_256_KDF_SCHEME */
    {"1.3.132.1.15.2", "mqvSinglePass-sha384kdf-scheme"}, /* rfc5753::MQV_SINGLE_PASS_SHA_384_KDF_SCHEME */
    {"1.3.132.1.15.3", "mqvSinglePass-sha512kdf-scheme"}, /* rfc5753::MQV_SINGLE_PASS_SHA_512_KDF_SCHEME */
    {"1.3.133.16.840.63.0", "x9-63-scheme"}, /* rfc5753::X_9_63_SCHEME */
    {"1.3.133.16.840.63.0.16", "mqvSinglePass-sha1kdf-scheme"}, /* rfc5753::MQV_SINGLE_PASS_SHA_1_KDF_SCHEME */
    {"1.3.133.16.840.63.0.2", "dhSinglePass-stdDH-sha1kdf-scheme"}, /* rfc5753::DH_SINGLE_PASS_STD_DH_SHA_1_KDF_SCHEME */
    {"1.3.133.16.840.63.0.3", "dhSinglePass-cofactorDH-sha1kdf-scheme"}, /* rfc5753::DH_SINGLE_PASS_COFACTOR_DH_SHA_1_KDF_SCHEME */
    {"1.2.840.113549.1.5.12", "id-PBKDF2"}, /* rfc5911::ID_PBKDF_2 */
    {"1.2.840.113549.1.7.1", "id-data"}, /* rfc5911::ID_DATA */
    {"1.2.840.113549.1.7.2", "id-signedData"}, /* rfc5911::ID_SIGNED_DATA */
    {"1.2.840.113549.1.7.3", "id-envelopedData"}, /* rfc5911::ID_ENVELOPED_DATA */
    {"1.2.840.113549.1.7.5", "id-digestedData"}, /* rfc5911::ID_DIGESTED_DATA */
    {"1.2.840.113549.1.7.6", "id-encryptedData"}, /* rfc5911::ID_ENCRYPTED_DATA */
    {"1.2.840.113549.1.9.16.1.1", "id-ct-receipt"}, /* rfc5911::ID_CT_RECEIPT */
    {"1.2.840.113549.1.9.16.1.16", "id-ct-firmwarePackage"}, /* rfc5911::ID_CT_FIRMWARE_PACKAGE */
    {"1.2.840.113549.1.9.16.1.17", "id-ct-firmwareLoadReceipt"}, /* rfc5911::ID_CT_FIRMWARE_LOAD_RECEIPT */
    {"1.2.840.113549.1.9.16.1.18", "id-ct-firmwareLoadError"}, /* rfc5911::ID_CT_FIRMWARE_LOAD_ERROR */
    {"1.2.840.113549.1.9.16.1.2", "id-ct-authData"}, /* rfc5911::ID_CT_AUTH_DATA */
    {"1.2.840.113549.1.9.16.1.23", "id-ct-authEnvelopedData"}, /* rfc5911::ID_CT_AUTH_ENVELOPED_DATA */
    {"1.2.840.113549.1.9.16.1.6", "id-ct-contentInfo"}, /* rfc5911::ID_CT_CONTENT_INFO */
    {"1.2.840.113549.1.9.16.11", "id-cap"}, /* rfc5911::ID_CAP */
    {"1.2.840.113549.1.9.16.11.1", "id-cap-preferBinaryInside"}, /* rfc5911::ID_CAP_PREFER_BINARY_INSIDE */
    {"1.2.840.113549.1.9.16.2", "id-aa"}, /* rfc5911::ID_AA */
    {"1.2.840.113549.1.9.16.2.1", "id-aa-receiptRequest"}, /* rfc5911::ID_AA_RECEIPT_REQUEST */
    {"1.2.840.113549.1.9.16.2.10", "id-aa-contentReference"}, /* rfc5911::ID_AA_CONTENT_REFERENCE */
    {"1.2.840.113549.1.9.16.2.11", "id-aa-encrypKeyPref"}, /* rfc5911::ID_AA_ENCRYP_KEY_PREF */
    {"1.2.840.113549.1.9.16.2.12", "id-aa-signingCertificate"}, /* rfc5911::ID_AA_SIGNING_CERTIFICATE */
    {"1.2.840.113549.1.9.16.2.2", "id-aa-securityLabel"}, /* rfc5911::ID_AA_SECURITY_LABEL */
    {"1.2.840.113549.1.9.16.2.3", "id-aa-mlExpandHistory"}, /* rfc5911::ID_AA_ML_EXPAND_HISTORY */
    {"1.2.840.113549.1.9.16.2.35", "id-aa-firmwarePackageID"}, /* rfc5911::ID_AA_FIRMWARE_PACKAGE_ID */
    {"1.2.840.113549.1.9.16.2.36", "id-aa-targetHardwareIDs"}, /* rfc5911::ID_AA_TARGET_HARDWARE_I_DS */
    {"1.2.840.113549.1.9.16.2.37", "id-aa-decryptKeyID"}, /* rfc5911::ID_AA_DECRYPT_KEY_ID */
    {"1.2.840.113549.1.9.16.2.38", "id-aa-implCryptoAlgs"}, /* rfc5911::ID_AA_IMPL_CRYPTO_ALGS */
    {"1.2.840.113549.1.9.16.2.39", "id-aa-wrappedFirmwareKey"}, /* rfc5911::ID_AA_WRAPPED_FIRMWARE_KEY */
    {"1.2.840.113549.1.9.16.2.4", "id-aa-contentHint"}, /* rfc5911::ID_AA_CONTENT_HINT */
    {"1.2.840.113549.1.9.16.2.40", "id-aa-communityIdentifiers"}, /* rfc5911::ID_AA_COMMUNITY_IDENTIFIERS */
    {"1.2.840.113549.1.9.16.2.42", "id-aa-firmwarePackageInfo"}, /* rfc5911::ID_AA_FIRMWARE_PACKAGE_INFO */
    {"1.2.840.113549.1.9.16.2.43", "id-aa-implCompressAlgs"}, /* rfc5911::ID_AA_IMPL_COMPRESS_ALGS */
    {"1.2.840.113549.1.9.16.2.47", "id-aa-signingCertificateV2"}, /* rfc5911::ID_AA_SIGNING_CERTIFICATE_V_2 */
    {"1.2.840.113549.1.9.16.2.49", "id-aa-er-internal"}, /* rfc5911::ID_AA_ER_INTERNAL */
    {"1.2.840.113549.1.9.16.2.5", "id-aa-msgSigDigest"}, /* rfc5911::ID_AA_MSG_SIG_DIGEST */
    {"1.2.840.113549.1.9.16.2.50", "id-aa-er-external"}, /* rfc5911::ID_AA_ER_EXTERNAL */
    {"1.2.840.113549.1.9.16.2.7", "id-aa-contentIdentifier"}, /* rfc5911::ID_AA_CONTENT_IDENTIFIER */
    {"1.2.840.113549.1.9.16.2.9", "id-aa-equivalentLabels"}, /* rfc5911::ID_AA_EQUIVALENT_LABELS */
    {"1.2.840.113549.1.9.16.3.10", "id-alg-SSDH"}, /* rfc5911::ID_ALG_SSDH */
    {"1.2.840.113549.1.9.16.3.5", "id-alg-ESDH"}, /* rfc5911::ID_ALG_ESDH */
    {"1.2.840.113549.1.9.16.3.6", "id-alg-CMS3DESwrap"}, /* rfc5911::ID_ALG_CMS_3_DE_SWRAP */
    {"1.2.840.113549.1.9.16.3.7", "id-alg-CMSRC2wrap"}, /* rfc5911::ID_ALG_CMSRC_2_WRAP */
    {"1.2.840.113549.1.9.16.8", "id-skd"}, /* rfc5911::ID_SKD */
    {"1.2.840.113549.1.9.16.8.1", "id-skd-glUseKEK"}, /* rfc5911::ID_SKD_GL_USE_KEK */
    {"1.2.840.113549.1.9.16.8.11", "id-skd-glaQueryRequest"}, /* rfc5911::ID_SKD_GLA_QUERY_REQUEST */
    {"1.2.840.113549.1.9.16.8.12", "id-skd-glaQueryResponse"}, /* rfc5911::ID_SKD_GLA_QUERY_RESPONSE */
    {"1.2.840.113549.1.9.16.8.13", "id-skd-glProvideCert"}, /* rfc5911::ID_SKD_GL_PROVIDE_CERT */
    {"1.2.840.113549.1.9.16.8.14", "id-skd-glManageCert"}, /* rfc5911::ID_SKD_GL_MANAGE_CERT */
    {"1.2.840.113549.1.9.16.8.15", "id-skd-glKey"}, /* rfc5911::ID_SKD_GL_KEY */
    {"1.2.840.113549.1.9.16.8.2", "id-skd-glDelete"}, /* rfc5911::ID_SKD_GL_DELETE */
    {"1.2.840.113549.1.9.16.8.3", "id-skd-glAddMember"}, /* rfc5911::ID_SKD_GL_ADD_MEMBER */
    {"1.2.840.113549.1.9.16.8.4", "id-skd-glDeleteMember"}, /* rfc5911::ID_SKD_GL_DELETE_MEMBER */
    {"1.2.840.113549.1.9.16.8.5", "id-skd-glRekey"}, /* rfc5911::ID_SKD_GL_REKEY */
    {"1.2.840.113549.1.9.16.8.6", "id-skd-glAddOwner"}, /* rfc5911::ID_SKD_GL_ADD_OWNER */
    {"1.2.840.113549.1.9.16.8.7", "id-skd-glRemoveOwner"}, /* rfc5911::ID_SKD_GL_REMOVE_OWNER */
    {"1.2.840.113549.1.9.16.8.8", "id-skd-glKeyCompromise"}, /* rfc5911::ID_SKD_GL_KEY_COMPROMISE */
    {"1.2.840.113549.1.9.16.8.9", "id-skd-glkRefresh"}, /* rfc5911::ID_SKD_GLK_REFRESH */
    {"1.2.840.113549.3.2", "rc2-cbc"}, /* rfc5911::RC_2_CBC */
    {"1.2.840.113549.3.7", "des-ede3-cbc"}, /* rfc5911::DES_EDE_3_CBC */
    {"1.3.6.1.5.5.11", "ltans"}, /* rfc5911::LTANS */
    {"1.3.6.1.5.5.7.15.1", "id-cet-skdFailInfo"}, /* rfc5911::ID_CET_SKD_FAIL_INFO */
    {"1.3.6.1.5.5.7.7.99", "id-cmc-glaRR"}, /* rfc5911::ID_CMC_GLA_RR */
    {"1.3.6.1.5.5.7.7.99.1", "id-cmc-gla-skdAlgRequest"}, /* rfc5911::ID_CMC_GLA_SKD_ALG_REQUEST */
    {"1.3.6.1.5.5.7.7.99.2", "id-cmc-gla-skdAlgResponse"}, /* rfc5911::ID_CMC_GLA_SKD_ALG_RESPONSE */
    {"1.3.6.1.5.5.7.8.4", "id-on-hardwareModuleName"}, /* rfc5911::ID_ON_HARDWARE_MODULE_NAME */
    {"1.3.6.1.5.5.8.1.2", "hMAC-SHA1"}, /* rfc5911::HMAC_SHA_1 */
    {"2.16.840.1.101.3.4.1", "aes"}, /* rfc5911::AES */
    {"2.16.840.1.101.3.4.1.2", "id-aes128-CBC"}, /* rfc5911::ID_AES_128_CBC */
    {"2.16.840.1.101.3.4.1.22", "id-aes192-CBC"}, /* rfc5911::ID_AES_192_CBC */
    {"2.16.840.1.101.3.4.1.25", "id-aes192-wrap"}, /* rfc5911::ID_AES_192_WRAP */
    {"2.16.840.1.101.3.4.1.26", "id-aes192-GCM"}, /* rfc5911::ID_AES_192_GCM */
    {"2.16.840.1.101.3.4.1.27", "id-aes192-CCM"}, /* rfc5911::ID_AES_192_CCM */
    {"2.16.840.1.101.3.4.1.42", "id-aes256-CBC"}, /* rfc5911::ID_AES_256_CBC */
    {"2.16.840.1.101.3.4.1.45", "id-aes256-wrap"}, /* rfc5911::ID_AES_256_WRAP */
    {"2.16.840.1.101.3.4.1.46", "id-aes256-GCM"}, /* rfc5911::ID_AES_256_GCM */
    {"2.16.840.1.101.3.4.1.47", "id-aes256-CCM"}, /* rfc5911::ID_AES_256_CCM */
    {"2.16.840.1.101.3.4.1.5", "id-aes128-wrap"}, /* rfc5911::ID_AES_128_WRAP */
    {"2.16.840.1.101.3.4.1.6", "id-aes128-GCM"}, /* rfc5911::ID_AES_128_GCM */
    {"2.16.840.1.101.3.4.1.7", "id-aes128-CCM"}, /* rfc5911::ID_AES_128_CCM */
    {"1.2.840.10040.4.1", "id-dsa"}, /* rfc5912::ID_DSA */
    {"1.2.840.10045.3.1.7", "secp256r1"}, /* rfc5912::SECP_256_R_1 */
    {"1.2.840.10045.4.3.1", "ecdsa-with-SHA224"}, /* rfc5912::ECDSA_WITH_SHA_224 */
    {"1.2.840.10045.4.3.2", "ecdsa-with-SHA256"}, /* rfc5912::ECDSA_WITH_SHA_256 */
    {"1.2.840.10045.4.3.3", "ecdsa-with-SHA384"}, /* rfc5912::ECDSA_WITH_SHA_384 */
    {"1.2.840.10045.4.3.4", "ecdsa-with-SHA512"}, /* rfc5912::ECDSA_WITH_SHA_512 */
    {"1.2.840.10046.2.1", "dhpublicnumber"}, /* rfc5912::DHPUBLICNUMBER */
    {"1.2.840.113533.7.66.13", "id-PasswordBasedMac"}, /* rfc5912::ID_PASSWORD_BASED_MAC */
    {"1.2.840.113533.7.66.30", "id-DHBasedMac"}, /* rfc5912::ID_DH_BASED_MAC */
    {"1.2.840.113549.1.1", "pkcs-1"}, /* rfc5912::PKCS_1 */
    {"1.2.840.113549.1.1.1", "rsaEncryption"}, /* rfc5912::RSA_ENCRYPTION */
    {"1.2.840.113549.1.1.10", "id-RSASSA-PSS"}, /* rfc5912::ID_RSASSA_PSS */
    {"1.2.840.113549.1.1.14", "sha224WithRSAEncryption"}, /* rfc5912::SHA_224_WITH_RSA_ENCRYPTION */
    {"1.2.840.113549.1.1.2", "md2WithRSAEncryption"}, /* rfc5912::MD_2_WITH_RSA_ENCRYPTION */
    {"1.2.840.113549.1.1.7", "id-RSAES-OAEP"}, /* rfc5912::ID_RSAES_OAEP */
    {"1.2.840.113549.1.1.8", "id-mgf1"}, /* rfc5912::ID_MGF_1 */
    {"1.2.840.113549.1.1.9", "id-pSpecified"}, /* rfc5912::ID_P_SPECIFIED */
    {"1.2.840.113549.1.9.16.1", "id-ct"}, /* rfc5912::ID_CT */
    {"1.2.840.113549.1.9.16.1.10", "id-ct-scvp-certValRequest"}, /* rfc5912::ID_CT_SCVP_CERT_VAL_REQUEST */
    {"1.2.840.113549.1.9.16.1.11", "id-ct-scvp-certValResponse"}, /* rfc5912::ID_CT_SCVP_CERT_VAL_RESPONSE */
    {"1.2.840.113549.1.9.16.1.12", "id-ct-scvp-valPolRequest"}, /* rfc5912::ID_CT_SCVP_VAL_POL_REQUEST */
    {"1.2.840.113549.1.9.16.1.13", "id-ct-scvp-valPolResponse"}, /* rfc5912::ID_CT_SCVP_VAL_POL_RESPONSE */
    {"1.2.840.113549.1.9.16.1.21", "id-ct-encKeyWithID"}, /* rfc5912::ID_CT_ENC_KEY_WITH_ID */
    {"1.2.840.113549.1.9.16.2.34", "id-aa-cmc-unsignedData"}, /* rfc5912::ID_AA_CMC_UNSIGNED_DATA */
    {"1.2.840.113549.2.2", "id-md2"}, /* rfc5912::ID_MD_2 */
    {"1.3.132.0.1", "sect163k1"}, /* rfc5912::SECT_163_K_1 */
    {"1.3.132.0.15", "sect163r2"}, /* rfc5912::SECT_163_R_2 */
    {"1.3.132.0.16", "sect283k1"}, /* rfc5912::SECT_283_K_1 */
    {"1.3.132.0.17", "sect283r1"}, /* rfc5912::SECT_283_R_1 */
    {"1.3.132.0.26", "sect233k1"}, /* rfc5912::SECT_233_K_1 */
    {"1.3.132.0.27", "sect233r1"}, /* rfc5912::SECT_233_R_1 */
    {"1.3.132.0.33", "secp224r1"}, /* rfc5912::SECP_224_R_1 */
    {"1.3.132.0.34", "secp384r1"}, /* rfc5912::SECP_384_R_1 */
    {"1.3.132.0.35", "secp521r1"}, /* rfc5912::SECP_521_R_1 */
    {"1.3.132.0.36", "sect409k1"}, /* rfc5912::SECT_409_K_1 */
    {"1.3.132.0.37", "sect409r1"}, /* rfc5912::SECT_409_R_1 */
    {"1.3.132.0.38", "sect571k1"}, /* rfc5912::SECT_571_K_1 */
    {"1.3.132.0.39", "sect571r1"}, /* rfc5912::SECT_571_R_1 */
    {"1.3.132.1.12", "id-ecDH"}, /* rfc5912::ID_EC_DH */
    {"1.3.132.1.13", "id-ecMQV"}, /* rfc5912::ID_EC_MQV */
    {"1.3.6.1.5.5.7.1.10", "id-pe-ac-proxying"}, /* rfc5912::ID_PE_AC_PROXYING */
    {"1.3.6.1.5.5.7.1.4", "id-pe-ac-auditIdentity"}, /* rfc5912::ID_PE_AC_AUDIT_IDENTITY */
    {"1.3.6.1.5.5.7.1.6", "id-pe-aaControls"}, /* rfc5912::ID_PE_AA_CONTROLS */
    {"1.3.6.1.5.5.7.10", "id-aca"}, /* rfc5912::ID_ACA */
    {"1.3.6.1.5.5.7.10.1", "id-aca-authenticationInfo"}, /* rfc5912::ID_ACA_AUTHENTICATION_INFO */
    {"1.3.6.1.5.5.7.10.2", "id-aca-accessIdentity"}, /* rfc5912::ID_ACA_ACCESS_IDENTITY */
    {"1.3.6.1.5.5.7.10.3", "id-aca-chargingIdentity"}, /* rfc5912::ID_ACA_CHARGING_IDENTITY */
    {"1.3.6.1.5.5.7.10.4", "id-aca-group"}, /* rfc5912::ID_ACA_GROUP */
    {"1.3.6.1.5.5.7.10.6", "id-aca-encAttrs"}, /* rfc5912::ID_ACA_ENC_ATTRS */
    {"1.3.6.1.5.5.7.12", "id-cct"}, /* rfc5912::ID_CCT */
    {"1.3.6.1.5.5.7.12.2", "id-cct-PKIData"}, /* rfc5912::ID_CCT_PKI_DATA */
    {"1.3.6.1.5.5.7.12.3", "id-cct-PKIResponse"}, /* rfc5912::ID_CCT_PKI_RESPONSE */
    {"1.3.6.1.5.5.7.17", "id-stc"}, /* rfc5912::ID_STC */
    {"1.3.6.1.5.5.7.17.1", "id-stc-build-pkc-path"}, /* rfc5912::ID_STC_BUILD_PKC_PATH */
    {"1.3.6.1.5.5.7.17.2", "id-stc-build-valid-pkc-path"}, /* rfc5912::ID_STC_BUILD_VALID_PKC_PATH */
    {"1.3.6.1.5.5.7.17.3", "id-stc-build-status-checked-pkc-path"}, /* rfc5912::ID_STC_BUILD_STATUS_CHECKED_PKC_PATH */
    {"1.3.6.1.5.5.7.17.4", "id-stc-build-aa-path"}, /* rfc5912::ID_STC_BUILD_AA_PATH */
    {"1.3.6.1.5.5.7.17.5", "id-stc-build-valid-aa-path"}, /* rfc5912::ID_STC_BUILD_VALID_AA_PATH */
    {"1.3.6.1.5.5.7.17.6", "id-stc-build-status-checked-aa-path"}, /* rfc5912::ID_STC_BUILD_STATUS_CHECKED_AA_PATH */
    {"1.3.6.1.5.5.7.17.7", "id-stc-status-check-ac-and-build-status-checked-aa-path"}, /* rfc5912::ID_STC_STATUS_CHECK_AC_AND_BUILD_STATUS_CHECKED_AA_PATH */
    {"1.3.6.1.5.5.7.18", "id-swb"}, /* rfc5912::ID_SWB */
    {"1.3.6.1.5.5.7.18.1", "id-swb-pkc-best-cert-path"}, /* rfc5912::ID_SWB_PKC_BEST_CERT_PATH */
    {"1.3.6.1.5.5.7.18.10", "id-swb-pkc-cert"}, /* rfc5912::ID_SWB_PKC_CERT */
    {"1.3.6.1.5.5.7.18.11", "id-swb-ac-cert"}, /* rfc5912::ID_SWB_AC_CERT */
    {"1.3.6.1.5.5.7.18.12", "id-swb-pkc-all-cert-paths"}, /* rfc5912::ID_SWB_PKC_ALL_CERT_PATHS */
    {"1.3.6.1.5.5.7.18.13", "id-swb-pkc-ee-revocation-info"}, /* rfc5912::ID_SWB_PKC_EE_REVOCATION_INFO */
    {"1.3.6.1.5.5.7.18.14", "id-swb-pkc-CAs-revocation-info"}, /* rfc5912::ID_SWB_PKC_C_AS_REVOCATION_INFO */
    {"1.3.6.1.5.5.7.18.2", "id-swb-pkc-revocation-info"}, /* rfc5912::ID_SWB_PKC_REVOCATION_INFO */
    {"1.3.6.1.5.5.7.18.4", "id-swb-pkc-public-key-info"}, /* rfc5912::ID_SWB_PKC_PUBLIC_KEY_INFO */
    {"1.3.6.1.5.5.7.18.5", "id-swb-aa-cert-path"}, /* rfc5912::ID_SWB_AA_CERT_PATH */
    {"1.3.6.1.5.5.7.18.6", "id-swb-aa-revocation-info"}, /* rfc5912::ID_SWB_AA_REVOCATION_INFO */
    {"1.3.6.1.5.5.7.18.7", "id-swb-ac-revocation-info"}, /* rfc5912::ID_SWB_AC_REVOCATION_INFO */
    {"1.3.6.1.5.5.7.18.9", "id-swb-relayed-responses"}, /* rfc5912::ID_SWB_RELAYED_RESPONSES */
    {"1.3.6.1.5.5.7.19", "id-svp"}, /* rfc5912::ID_SVP */
    {"1.3.6.1.5.5.7.19.1", "id-svp-defaultValPolicy"}, /* rfc5912::ID_SVP_DEFAULT_VAL_POLICY */
    {"1.3.6.1.5.5.7.19.2", "id-svp-nameValAlg"}, /* rfc5912::ID_SVP_NAME_VAL_ALG */
    {"1.3.6.1.5.5.7.19.3", "id-svp-basicValAlg"}, /* rfc5912::ID_SVP_BASIC_VAL_ALG */
    {"1.3.6.1.5.5.7.19.4", "NameCompAlgSet"}, /* rfc5912::NAME_COMP_ALG_SET */
    {"1.3.6.1.5.5.7.3.15", "id-kp-scvpServer"}, /* rfc5912::ID_KP_SCVP_SERVER */
    {"1.3.6.1.5.5.7.3.16", "id-kp-scvpClient"}, /* rfc5912::ID_KP_SCVP_CLIENT */
    {"1.3.6.1.5.5.7.4", "id-it"}, /* rfc5912::ID_IT */
    {"1.3.6.1.5.5.7.4.1", "id-it-caProtEncCert"}, /* rfc5912::ID_IT_CA_PROT_ENC_CERT */
    {"1.3.6.1.5.5.7.4.10", "id-it-keyPairParamReq"}, /* rfc5912::ID_IT_KEY_PAIR_PARAM_REQ */
    {"1.3.6.1.5.5.7.4.11", "id-it-keyPairParamRep"}, /* rfc5912::ID_IT_KEY_PAIR_PARAM_REP */
    {"1.3.6.1.5.5.7.4.12", "id-it-revPassphrase"}, /* rfc5912::ID_IT_REV_PASSPHRASE */
    {"1.3.6.1.5.5.7.4.13", "id-it-implicitConfirm"}, /* rfc5912::ID_IT_IMPLICIT_CONFIRM */
    {"1.3.6.1.5.5.7.4.14", "id-it-confirmWaitTime"}, /* rfc5912::ID_IT_CONFIRM_WAIT_TIME */
    {"1.3.6.1.5.5.7.4.15", "id-it-origPKIMessage"}, /* rfc5912::ID_IT_ORIG_PKI_MESSAGE */
    {"1.3.6.1.5.5.7.4.16", "id-it-suppLangTags"}, /* rfc5912::ID_IT_SUPP_LANG_TAGS */
    {"1.3.6.1.5.5.7.4.2", "id-it-signKeyPairTypes"}, /* rfc5912::ID_IT_SIGN_KEY_PAIR_TYPES */
    {"1.3.6.1.5.5.7.4.3", "id-it-encKeyPairTypes"}, /* rfc5912::ID_IT_ENC_KEY_PAIR_TYPES */
    {"1.3.6.1.5.5.7.4.4", "id-it-preferredSymmAlg"}, /* rfc5912::ID_IT_PREFERRED_SYMM_ALG */
    {"1.3.6.1.5.5.7.4.5", "id-it-caKeyUpdateInfo"}, /* rfc5912::ID_IT_CA_KEY_UPDATE_INFO */
    {"1.3.6.1.5.5.7.4.6", "id-it-currentCRL"}, /* rfc5912::ID_IT_CURRENT_CRL */
    {"1.3.6.1.5.5.7.4.7", "id-it-unsupportedOIDs"}, /* rfc5912::ID_IT_UNSUPPORTED_OI_DS */
    {"1.3.6.1.5.5.7.5", "id-pkip"}, /* rfc5912::ID_PKIP */
    {"1.3.6.1.5.5.7.5.1", "id-regCtrl"}, /* rfc5912::ID_REG_CTRL */
    {"1.3.6.1.5.5.7.5.1.1", "id-regCtrl-regToken"}, /* rfc5912::ID_REG_CTRL_REG_TOKEN */
    {"1.3.6.1.5.5.7.5.1.2", "id-regCtrl-authenticator"}, /* rfc5912::ID_REG_CTRL_AUTHENTICATOR */
    {"1.3.6.1.5.5.7.5.1.3", "id-regCtrl-pkiPublicationInfo"}, /* rfc5912::ID_REG_CTRL_PKI_PUBLICATION_INFO */
    {"1.3.6.1.5.5.7.5.1.4", "id-regCtrl-pkiArchiveOptions"}, /* rfc5912::ID_REG_CTRL_PKI_ARCHIVE_OPTIONS */
    {"1.3.6.1.5.5.7.5.1.5", "id-regCtrl-oldCertID"}, /* rfc5912::ID_REG_CTRL_OLD_CERT_ID */
    {"1.3.6.1.5.5.7.5.1.6", "id-regCtrl-protocolEncrKey"}, /* rfc5912::ID_REG_CTRL_PROTOCOL_ENCR_KEY */
    {"1.3.6.1.5.5.7.5.2", "id-regInfo"}, /* rfc5912::ID_REG_INFO */
    {"1.3.6.1.5.5.7.5.2.1", "id-regInfo-utf8Pairs"}, /* rfc5912::ID_REG_INFO_UTF_8_PAIRS */
    {"1.3.6.1.5.5.7.5.2.2", "id-regInfo-certReq"}, /* rfc5912::ID_REG_INFO_CERT_REQ */
    {"1.3.6.1.5.5.7.6.2", "id-alg-noSignature"}, /* rfc5912::ID_ALG_NO_SIGNATURE */
    {"1.3.6.1.5.5.7.7", "id-cmc"}, /* rfc5912::ID_CMC */
    {"1.3.6.1.5.5.7.7.1", "id-cmc-statusInfo"}, /* rfc5912::ID_CMC_STATUS_INFO */
    {"1.3.6.1.5.5.7.7.10", "id-cmc-decryptedPOP"}, /* rfc5912::ID_CMC_DECRYPTED_POP */
    {"1.3.6.1.5.5.7.7.11", "id-cmc-lraPOPWitness"}, /* rfc5912::ID_CMC_LRA_POP_WITNESS */
    {"1.3.6.1.5.5.7.7.15", "id-cmc-getCert"}, /* rfc5912::ID_CMC_GET_CERT */
    {"1.3.6.1.5.5.7.7.16", "id-cmc-getCRL"}, /* rfc5912::ID_CMC_GET_CRL */
    {"1.3.6.1.5.5.7.7.17", "id-cmc-revokeRequest"}, /* rfc5912::ID_CMC_REVOKE_REQUEST */
    {"1.3.6.1.5.5.7.7.18", "id-cmc-regInfo"}, /* rfc5912::ID_CMC_REG_INFO */
    {"1.3.6.1.5.5.7.7.19", "id-cmc-responseInfo"}, /* rfc5912::ID_CMC_RESPONSE_INFO */
    {"1.3.6.1.5.5.7.7.2", "id-cmc-identification"}, /* rfc5912::ID_CMC_IDENTIFICATION */
    {"1.3.6.1.5.5.7.7.21", "id-cmc-queryPending"}, /* rfc5912::ID_CMC_QUERY_PENDING */
    {"1.3.6.1.5.5.7.7.22", "id-cmc-popLinkRandom"}, /* rfc5912::ID_CMC_POP_LINK_RANDOM */
    {"1.3.6.1.5.5.7.7.23", "id-cmc-popLinkWitness"}, /* rfc5912::ID_CMC_POP_LINK_WITNESS */
    {"1.3.6.1.5.5.7.7.24", "id-cmc-confirmCertAcceptance"}, /* rfc5912::ID_CMC_CONFIRM_CERT_ACCEPTANCE */
    {"1.3.6.1.5.5.7.7.25", "id-cmc-statusInfoV2"}, /* rfc5912::ID_CMC_STATUS_INFO_V_2 */
    {"1.3.6.1.5.5.7.7.26", "id-cmc-trustedAnchors"}, /* rfc5912::ID_CMC_TRUSTED_ANCHORS */
    {"1.3.6.1.5.5.7.7.27", "id-cmc-authData"}, /* rfc5912::ID_CMC_AUTH_DATA */
    {"1.3.6.1.5.5.7.7.28", "id-cmc-batchRequests"}, /* rfc5912::ID_CMC_BATCH_REQUESTS */
    {"1.3.6.1.5.5.7.7.29", "id-cmc-batchResponses"}, /* rfc5912::ID_CMC_BATCH_RESPONSES */
    {"1.3.6.1.5.5.7.7.3", "id-cmc-identityProof"}, /* rfc5912::ID_CMC_IDENTITY_PROOF */
    {"1.3.6.1.5.5.7.7.30", "id-cmc-publishCert"}, /* rfc5912::ID_CMC_PUBLISH_CERT */
    {"1.3.6.1.5.5.7.7.31", "id-cmc-modCertTemplate"}, /* rfc5912::ID_CMC_MOD_CERT_TEMPLATE */
    {"1.3.6.1.5.5.7.7.32", "id-cmc-controlProcessed"}, /* rfc5912::ID_CMC_CONTROL_PROCESSED */
    {"1.3.6.1.5.5.7.7.33", "id-cmc-identityProofV2"}, /* rfc5912::ID_CMC_IDENTITY_PROOF_V_2 */
    {"1.3.6.1.5.5.7.7.34", "id-cmc-popLinkWitnessV2"}, /* rfc5912::ID_CMC_POP_LINK_WITNESS_V_2 */
    {"1.3.6.1.5.5.7.7.4", "id-cmc-dataReturn"}, /* rfc5912::ID_CMC_DATA_RETURN */
    {"1.3.6.1.5.5.7.7.5", "id-cmc-transactionId"}, /* rfc5912::ID_CMC_TRANSACTION_ID */
    {"1.3.6.1.5.5.7.7.6", "id-cmc-senderNonce"}, /* rfc5912::ID_CMC_SENDER_NONCE */
    {"1.3.6.1.5.5.7.7.7", "id-cmc-recipientNonce"}, /* rfc5912::ID_CMC_RECIPIENT_NONCE */
    {"1.3.6.1.5.5.7.7.8", "id-cmc-addExtensions"}, /* rfc5912::ID_CMC_ADD_EXTENSIONS */
    {"1.3.6.1.5.5.7.7.9", "id-cmc-encryptedPOP"}, /* rfc5912::ID_CMC_ENCRYPTED_POP */
    {"2.16.840.1.101.2.1.1.22", "id-keyExchangeAlgorithm"}, /* rfc5912::ID_KEY_EXCHANGE_ALGORITHM */
    {"2.16.840.1.101.3.4.2.4", "id-sha224"}, /* rfc5912::ID_SHA_224 */
    {"2.5.29.55", "id-ce-targetInformation"}, /* rfc5912::ID_CE_TARGET_INFORMATION */
    {"2.5.29.56", "id-ce-noRevAvail"}, /* rfc5912::ID_CE_NO_REV_AVAIL */
    {"2.5.4.72", "id-at-role"}, /* rfc5912::ID_AT_ROLE */
    {"1.3.6.1.4.1.16572.2.1.1", "LDIFLocationURLObject"}, /* rfc6109::LDIF_LOCATION_URL_OBJECT */
    {"1.3.6.1.4.1.16572.2.1.2", "provider"}, /* rfc6109::PROVIDER */
    {"1.3.6.1.4.1.16572.2.2.1", "providerCertificateHash"}, /* rfc6109::PROVIDER_CERTIFICATE_HASH */
    {"1.3.6.1.4.1.16572.2.2.2", "providerCertificate"}, /* rfc6109::PROVIDER_CERTIFICATE */
    {"1.3.6.1.4.1.16572.2.2.3", "providerName"}, /* rfc6109::PROVIDER_NAME */
    {"1.3.6.1.4.1.16572.2.2.4", "mailReceipt"}, /* rfc6109::MAIL_RECEIPT */
    {"1.3.6.1.4.1.16572.2.2.5", "managedDomains"}, /* rfc6109::MANAGED_DOMAINS */
    {"1.3.6.1.4.1.16572.2.2.6", "LDIFLocationURL"}, /* rfc6109::LDIF_LOCATION_URL */
    {"1.3.6.1.4.1.16572.2.2.7", "providerUnit"}, /* rfc6109::PROVIDER_UNIT */
    {"1.2.840.113549", "rsadsi"}, /* rfc6268::RSADSI */
    {"1.2.840.113549.1.9.16.1.19", "id-ct-contentCollection"}, /* rfc6268::ID_CT_CONTENT_COLLECTION */
    {"1.2.840.113549.1.9.16.1.20", "id-ct-contentWithAttrs"}, /* rfc6268::ID_CT_CONTENT_WITH_ATTRS */
    {"1.2.840.113549.1.9.16.1.9", "id-ct-compressedData"}, /* rfc6268::ID_CT_COMPRESSED_DATA */
    {"1.2.840.113549.1.9.16.2.46", "id-aa-binarySigningTime"}, /* rfc6268::ID_AA_BINARY_SIGNING_TIME */
    {"1.2.840.113549.1.9.16.3.8", "id-alg-zlibCompress"}, /* rfc6268::ID_ALG_ZLIB_COMPRESS */
    {"1.2.840.113549.1.9.2.51", "id-aa-multipleSignatures"}, /* rfc6268::ID_AA_MULTIPLE_SIGNATURES */
    {"1.2.840.113549.2", "digestAlgorithm"}, /* rfc6268::DIGEST_ALGORITHM */
    {"1.3.6.1.5.5.7.48.1.1", "id-pkix-ocsp-basic"}, /* rfc6960::ID_PKIX_OCSP_BASIC */
    {"1.3.6.1.5.5.7.48.1.2", "id-pkix-ocsp-nonce"}, /* rfc6960::ID_PKIX_OCSP_NONCE */
    {"1.3.6.1.5.5.7.48.1.3", "id-pkix-ocsp-crl"}, /* rfc6960::ID_PKIX_OCSP_CRL */
    {"1.3.6.1.5.5.7.48.1.4", "id-pkix-ocsp-response"}, /* rfc6960::ID_PKIX_OCSP_RESPONSE */
    {"1.3.6.1.5.5.7.48.1.5", "id-pkix-ocsp-nocheck"}, /* rfc6960::ID_PKIX_OCSP_NOCHECK */
    {"1.3.6.1.5.5.7.48.1.6", "id-pkix-ocsp-archive-cutoff"}, /* rfc6960::ID_PKIX_OCSP_ARCHIVE_CUTOFF */
    {"1.3.6.1.5.5.7.48.1.7", "id-pkix-ocsp-service-locator"}, /* rfc6960::ID_PKIX_OCSP_SERVICE_LOCATOR */
    {"1.3.6.1.5.5.7.48.1.8", "id-pkix-ocsp-pref-sig-algs"}, /* rfc6960::ID_PKIX_OCSP_PREF_SIG_ALGS */
    {"1.3.6.1.5.5.7.48.1.9", "id-pkix-ocsp-extended-revoke"}, /* rfc6960::ID_PKIX_OCSP_EXTENDED_REVOKE */
    {"1.3.6.1.4.1.11129", "google"}, /* rfc6962::GOOGLE */
    {"1.3.6.1.4.1.11129.2.4.2", "ct-precert-scts"}, /* rfc6962::CT_PRECERT_SCTS */
    {"1.3.6.1.4.1.11129.2.4.3", "ct-precert-poison"}, /* rfc6962::CT_PRECERT_POISON */
    {"1.3.6.1.4.1.11129.2.4.4", "ct-precert-signing-cert"}, /* rfc6962::CT_PRECERT_SIGNING_CERT */
    {"1.2.840.113549.1.9.16.0", "id-mod"}, /* rfc7107::ID_MOD */
    {"1.2.840.113549.1.9.16.10", "id-eit"}, /* rfc7107::ID_EIT */
    {"1.2.840.113549.1.9.16.12", "id-pskc"}, /* rfc7107::ID_PSKC */
    {"1.2.840.113549.1.9.16.3", "id-alg"}, /* rfc7107::ID_ALG */
    {"1.2.840.113549.1.9.16.4", "id-cd"}, /* rfc7107::ID_CD */
    {"1.2.840.113549.1.9.16.5", "id-spq"}, /* rfc7107::ID_SPQ */
    {"1.2.840.113549.1.9.16.6", "id-cti"}, /* rfc7107::ID_CTI */
    {"1.2.840.113549.1.9.16.7", "id-tsp"}, /* rfc7107::ID_TSP */
    {"1.2.840.113549.1.9.16.9", "id-sti"}, /* rfc7107::ID_STI */
    {"1.2.840.113549.1", "pkcs"}, /* rfc7292::PKCS */
    {"1.2.840.113549.1.12", "pkcs-12"}, /* rfc7292::PKCS_12 */
    {"1.2.840.113549.1.12.1", "pkcs-12PbeIds"}, /* rfc7292::PKCS_12_PBE_IDS */
    {"1.2.840.113549.1.12.1.1", "pbeWithSHAAnd128BitRC4"}, /* rfc7292::PBE_WITH_SHA_AND_128_BIT_RC_4 */
    {"1.2.840.113549.1.12.1.2", "pbeWithSHAAnd40BitRC4"}, /* rfc7292::PBE_WITH_SHA_AND_40_BIT_RC_4 */
    {"1.2.840.113549.1.12.1.3", "pbeWithSHAAnd3-KeyTripleDES-CBC"}, /* rfc7292::PBE_WITH_SHA_AND_3_KEY_TRIPLE_DES_CBC */
    {"1.2.840.113549.1.12.1.4", "pbeWithSHAAnd2-KeyTripleDES-CBC"}, /* rfc7292::PBE_WITH_SHA_AND_2_KEY_TRIPLE_DES_CBC */
    {"1.2.840.113549.1.12.1.5", "pbeWithSHAAnd128BitRC2-CBC"}, /* rfc7292::PBE_WITH_SHA_AND_128_BIT_RC_2_CBC */
    {"1.2.840.113549.1.12.1.6", "pbewithSHAAnd40BitRC2-CBC"}, /* rfc7292::PBEWITH_SHA_AND_40_BIT_RC_2_CBC */
    {"1.2.840.113549.1.12.10.1", "bagtypes"}, /* rfc7292::BAGTYPES */
    {"1.3.6.1.5.5.7.0", "id-mod"}, /* rfc7299::ID_MOD */
    {"1.3.6.1.5.5.7.11", "id-qcs"}, /* rfc7299::ID_QCS */
    {"1.3.6.1.5.5.7.13", "id-TEST"}, /* rfc7299::ID_TEST */
    {"1.3.6.1.5.5.7.14", "id-cp"}, /* rfc7299::ID_CP */
    {"1.3.6.1.5.5.7.15", "id-cet"}, /* rfc7299::ID_CET */
    {"1.3.6.1.5.5.7.16", "id-ri"}, /* rfc7299::ID_RI */
    {"1.3.6.1.5.5.7.20", "id-logo"}, /* rfc7299::ID_LOGO */
    {"1.3.6.1.5.5.7.21", "id-ppl"}, /* rfc7299::ID_PPL */
    {"1.3.6.1.5.5.7.22", "id-mr"}, /* rfc7299::ID_MR */
    {"1.3.6.1.5.5.7.23", "id-skis"}, /* rfc7299::ID_SKIS */
    {"1.3.6.1.5.5.7.6", "id-alg"}, /* rfc7299::ID_ALG */
    {"1.3.6.1.5.5.7.8", "id-on"}, /* rfc7299::ID_ON */
    {"1.3.6.1.4.1.31103.1.1", "fedfsUuid"}, /* rfc7532::FEDFS_UUID */
    {"1.3.6.1.4.1.31103.1.10", "fedfsFslPort"}, /* rfc7532::FEDFS_FSL_PORT */
    {"1.3.6.1.4.1.31103.1.100", "fedfsNfsPath"}, /* rfc7532::FEDFS_NFS_PATH */
    {"1.3.6.1.4.1.31103.1.1001", "fedfsNsdbContainerInfo"}, /* rfc7532::FEDFS_NSDB_CONTAINER_INFO */
    {"1.3.6.1.4.1.31103.1.1002", "fedfsFsn"}, /* rfc7532::FEDFS_FSN */
    {"1.3.6.1.4.1.31103.1.1003", "fedfsFsl"}, /* rfc7532::FEDFS_FSL */
    {"1.3.6.1.4.1.31103.1.1004", "fedfsNfsFsl"}, /* rfc7532::FEDFS_NFS_FSL */
    {"1.3.6.1.4.1.31103.1.101", "fedfsNfsMajorVer"}, /* rfc7532::FEDFS_NFS_MAJOR_VER */
    {"1.3.6.1.4.1.31103.1.102", "fedfsNfsMinorVer"}, /* rfc7532::FEDFS_NFS_MINOR_VER */
    {"1.3.6.1.4.1.31103.1.103", "fedfsNfsCurrency"}, /* rfc7532::FEDFS_NFS_CURRENCY */
    {"1.3.6.1.4.1.31103.1.104", "fedfsNfsGenFlagWritable"}, /* rfc7532::FEDFS_NFS_GEN_FLAG_WRITABLE */
    {"1.3.6.1.4.1.31103.1.105", "fedfsNfsGenFlagGoing"}, /* rfc7532::FEDFS_NFS_GEN_FLAG_GOING */
    {"1.3.6.1.4.1.31103.1.106", "fedfsNfsGenFlagSplit"}, /* rfc7532::FEDFS_NFS_GEN_FLAG_SPLIT */
    {"1.3.6.1.4.1.31103.1.107", "fedfsNfsTransFlagRdma"}, /* rfc7532::FEDFS_NFS_TRANS_FLAG_RDMA */
    {"1.3.6.1.4.1.31103.1.108", "fedfsNfsClassSimul"}, /* rfc7532::FEDFS_NFS_CLASS_SIMUL */
    {"1.3.6.1.4.1.31103.1.109", "fedfsNfsClassHandle"}, /* rfc7532::FEDFS_NFS_CLASS_HANDLE */
    {"1.3.6.1.4.1.31103.1.11", "fedfsFslTTL"}, /* rfc7532::FEDFS_FSL_TTL */
    {"1.3.6.1.4.1.31103.1.110", "fedfsNfsClassFileid"}, /* rfc7532::FEDFS_NFS_CLASS_FILEID */
    {"1.3.6.1.4.1.31103.1.111", "fedfsNfsClassWritever"}, /* rfc7532::FEDFS_NFS_CLASS_WRITEVER */
    {"1.3.6.1.4.1.31103.1.112", "fedfsNfsClassChange"}, /* rfc7532::FEDFS_NFS_CLASS_CHANGE */
    {"1.3.6.1.4.1.31103.1.113", "fedfsNfsClassReaddir"}, /* rfc7532::FEDFS_NFS_CLASS_READDIR */
    {"1.3.6.1.4.1.31103.1.114", "fedfsNfsReadRank"}, /* rfc7532::FEDFS_NFS_READ_RANK */
    {"1.3.6.1.4.1.31103.1.115", "fedfsNfsReadOrder"}, /* rfc7532::FEDFS_NFS_READ_ORDER */
    {"1.3.6.1.4.1.31103.1.116", "fedfsNfsWriteRank"}, /* rfc7532::FEDFS_NFS_WRITE_RANK */
    {"1.3.6.1.4.1.31103.1.117", "fedfsNfsWriteOrder"}, /* rfc7532::FEDFS_NFS_WRITE_ORDER */
    {"1.3.6.1.4.1.31103.1.118", "fedfsNfsVarSub"}, /* rfc7532::FEDFS_NFS_VAR_SUB */
    {"1.3.6.1.4.1.31103.1.119", "fedfsNfsValidFor"}, /* rfc7532::FEDFS_NFS_VALID_FOR */
    {"1.3.6.1.4.1.31103.1.12", "fedfsAnnotation"}, /* rfc7532::FEDFS_ANNOTATION */
    {"1.3.6.1.4.1.31103.1.120", "fedfsNfsURI"}, /* rfc7532::FEDFS_NFS_URI */
    {"1.3.6.1.4.1.31103.1.13", "fedfsDescr"}, /* rfc7532::FEDFS_DESCR */
    {"1.3.6.1.4.1.31103.1.14", "fedfsNceDN"}, /* rfc7532::FEDFS_NCE_DN */
    {"1.3.6.1.4.1.31103.1.15", "fedfsFsnTTL"}, /* rfc7532::FEDFS_FSN_TTL */
    {"1.3.6.1.4.1.31103.1.2", "fedfsNetAddr"}, /* rfc7532::FEDFS_NET_ADDR */
    {"1.3.6.1.4.1.31103.1.3", "fedfsNetPort"}, /* rfc7532::FEDFS_NET_PORT */
    {"1.3.6.1.4.1.31103.1.4", "fedfsFsnUuid"}, /* rfc7532::FEDFS_FSN_UUID */
    {"1.3.6.1.4.1.31103.1.5", "fedfsNsdbName"}, /* rfc7532::FEDFS_NSDB_NAME */
    {"1.3.6.1.4.1.31103.1.6", "fedfsNsdbPort"}, /* rfc7532::FEDFS_NSDB_PORT */
    {"1.3.6.1.4.1.31103.1.7", "fedfsNcePrefix"}, /* rfc7532::FEDFS_NCE_PREFIX */
    {"1.3.6.1.4.1.31103.1.8", "fedfsFslUuid"}, /* rfc7532::FEDFS_FSL_UUID */
    {"1.3.6.1.4.1.31103.1.9", "fedfsFslHost"}, /* rfc7532::FEDFS_FSL_HOST */
    {"1.3.18.0.2.24.46.1.101", "printer-device-id"}, /* rfc7612::PRINTER_DEVICE_ID */
    {"1.3.18.0.2.24.46.1.102", "printer-device-service-count"}, /* rfc7612::PRINTER_DEVICE_SERVICE_COUNT */
    {"1.3.18.0.2.24.46.1.104", "printer-uuid"}, /* rfc7612::PRINTER_UUID */
    {"1.3.18.0.2.24.46.1.105", "printer-charge-info"}, /* rfc7612::PRINTER_CHARGE_INFO */
    {"1.3.18.0.2.24.46.1.106", "printer-charge-info-uri"}, /* rfc7612::PRINTER_CHARGE_INFO_URI */
    {"1.3.18.0.2.24.46.1.107", "printer-geo-location"}, /* rfc7612::PRINTER_GEO_LOCATION */
    {"1.3.18.0.2.24.46.1.108", "printer-ipp-features-supported"}, /* rfc7612::PRINTER_IPP_FEATURES_SUPPORTED */
    {"1.3.6.1.4.1.1722.12.2", "hashAlgs"}, /* rfc7693::HASH_ALGS */
    {"1.3.6.1.4.1.1722.12.2.1", "blake2b"}, /* rfc7693::BLAKE_2_B */
    {"1.3.6.1.4.1.1722.12.2.1.12", "id-blake2b384"}, /* rfc7693::ID_BLAKE_2_B_384 */
    {"1.3.6.1.4.1.1722.12.2.1.16", "id-blake2b512"}, /* rfc7693::ID_BLAKE_2_B_512 */
    {"1.3.6.1.4.1.1722.12.2.1.5", "id-blake2b160"}, /* rfc7693::ID_BLAKE_2_B_160 */
    {"1.3.6.1.4.1.1722.12.2.1.8", "id-blake2b256"}, /* rfc7693::ID_BLAKE_2_B_256 */
    {"1.3.6.1.4.1.1722.12.2.2", "blake2s"}, /* rfc7693::BLAKE_2_S */
    {"1.3.6.1.4.1.1722.12.2.2.4", "id-blake2s128"}, /* rfc7693::ID_BLAKE_2_S_128 */
    {"1.3.6.1.4.1.1722.12.2.2.5", "id-blake2s160"}, /* rfc7693::ID_BLAKE_2_S_160 */
    {"1.3.6.1.4.1.1722.12.2.2.7", "id-blake2s224"}, /* rfc7693::ID_BLAKE_2_S_224 */
    {"1.3.6.1.4.1.1722.12.2.2.8", "id-blake2s256"}, /* rfc7693::ID_BLAKE_2_S_256 */
    {"1.3.6.1.4.1.1722.12.3", "macAlgs"}, /* rfc7693::MAC_ALGS */
    {"1.3.6.1.1.23.1", "JIDObject"}, /* rfc8284::JID_OBJECT */
    {"1.3.6.1.1.23.2", "jid"}, /* rfc8284::JID */
    {"1.3.101", "id-edwards-curve-algs"}, /* rfc8410::ID_EDWARDS_CURVE_ALGS */
    {"1.3.101.110", "id-X25519"}, /* rfc8410::ID_X_25519 */
    {"1.3.101.111", "id-X448"}, /* rfc8410::ID_X_448 */
    {"1.3.101.112", "id-Ed25519"}, /* rfc8410::ID_ED_25519 */
    {"1.3.101.113", "id-Ed448"}, /* rfc8410::ID_ED_448 */
    {"1.3.6.1.5.5.7.24", "id-scep"}, /* rfc8894::ID_SCEP */
    {"1.3.6.1.5.5.7.24.1", "id-scep-failInfoText"}, /* rfc8894::ID_SCEP_FAIL_INFO_TEXT */
    {"2.16.840.1.113733", "id-VeriSign"}, /* rfc8894::ID_VERI_SIGN */
    {"2.16.840.1.113733.1", "id-pki"}, /* rfc8894::ID_PKI */
    {"2.16.840.1.113733.1.9", "id-attributes"}, /* rfc8894::ID_ATTRIBUTES */
    {"2.16.840.1.113733.1.9.2", "id-messageType"}, /* rfc8894::ID_MESSAGE_TYPE */
    {"2.16.840.1.113733.1.9.3", "id-pkiStatus"}, /* rfc8894::ID_PKI_STATUS */
    {"2.16.840.1.113733.1.9.4", "id-failInfo"}, /* rfc8894::ID_FAIL_INFO */
    {"2.16.840.1.113733.1.9.5", "id-senderNonce"}, /* rfc8894::ID_SENDER_NONCE */
    {"2.16.840.1.113733.1.9.6", "id-recipientNonce"}, /* rfc8894::ID_RECIPIENT_NONCE */
    {"2.16.840.1.113733.1.9.7", "id-transactionID"}, /* rfc8894::ID_TRANSACTION_ID */
    {"1.2.840.113549.1.9.16.0.78", "id-mod-sha3-oids-2023"}, /* rfc9688::ID_MOD_SHA_3_OIDS_2023 */
    {"1.2.840.113549.1.9.16.3.32", "id-alg-hkdf-with-sha3-224"}, /* rfc9688::ID_ALG_HKDF_WITH_SHA_3_224 */
    {"1.2.840.113549.1.9.16.3.33", "id-alg-hkdf-with-sha3-256"}, /* rfc9688::ID_ALG_HKDF_WITH_SHA_3_256 */
    {"1.2.840.113549.1.9.16.3.34", "id-alg-hkdf-with-sha3-384"}, /* rfc9688::ID_ALG_HKDF_WITH_SHA_3_384 */
    {"1.2.840.113549.1.9.16.3.35", "id-alg-hkdf-with-sha3-512"}, /* rfc9688::ID_ALG_HKDF_WITH_SHA_3_512 */
    {"1.3.133.16.840.9.44", "x9-44"}, /* rfc9688::X_9_44 */
    {"1.3.133.16.840.9.44.1", "x9-44-components"}, /* rfc9688::X_9_44_COMPONENTS */
    {"1.3.133.16.840.9.44.1.1", "id-kdf-kdf2"}, /* rfc9688::ID_KDF_KDF_2 */
    {"1.3.133.16.840.9.44.1.2", "id-kdf-kdf3"}, /* rfc9688::ID_KDF_KDF_3 */
    {"2.16.840.1.101.3.4.2.13", "id-hmacWithSHA3-224"}, /* rfc9688::ID_HMAC_WITH_SHA_3_224 */
    {"2.16.840.1.101.3.4.2.14", "id-hmacWithSHA3-256"}, /* rfc9688::ID_HMAC_WITH_SHA_3_256 */
    {"2.16.840.1.101.3.4.2.15", "id-hmacWithSHA3-384"}, /* rfc9688::ID_HMAC_WITH_SHA_3_384 */
    {"2.16.840.1.101.3.4.2.16", "id-hmacWithSHA3-512"}, /* rfc9688::ID_HMAC_WITH_SHA_3_512 */
    {"2.16.840.1.101.3.4.2.21", "id-kmac128"}, /* rfc9688::ID_KMAC_128 */
    {"2.16.840.1.101.3.4.2.22", "id-kmac256"}, /* rfc9688::ID_KMAC_256 */
    {"2.16.840.1.101.3.4.3.10", "id-ecdsa-with-sha3-256"}, /* rfc9688::ID_ECDSA_WITH_SHA_3_256 */
    {"2.16.840.1.101.3.4.3.11", "id-ecdsa-with-sha3-384"}, /* rfc9688::ID_ECDSA_WITH_SHA_3_384 */
    {"2.16.840.1.101.3.4.3.12", "id-ecdsa-with-sha3-512"}, /* rfc9688::ID_ECDSA_WITH_SHA_3_512 */
    {"2.16.840.1.101.3.4.3.9", "id-ecdsa-with-sha3-224"}, /* rfc9688::ID_ECDSA_WITH_SHA_3_224 */
    {"2.23.133", "tcgOrganization"}, /* tcgtpm::TCG_ORGANIZATION */
    {"2.23.133.1", "tcg-tcpaSpecVersion"}, /* tcgtpm::TCG_TCPA_SPEC_VERSION */
    {"2.23.133.1.0", "tcg-sv-tpm12"}, /* tcgtpm::TCG_SV_TPM_12 */
    {"2.23.133.1.2", "tcg-sv-tpm20"}, /* tcgtpm::TCG_SV_TPM_20 */
    {"2.23.133.2", "tcg-attribute"}, /* tcgtpm::TCG_ATTRIBUTE */
    {"2.23.133.2.1", "tcg-at-tpmManufacturer"}, /* tcgtpm::TCG_AT_TPM_MANUFACTURER */
    {"2.23.133.2.10", "tcg-at-securityQualities"}, /* tcgtpm::TCG_AT_SECURITY_QUALITIES */
    {"2.23.133.2.11", "tcg-at-tpmProtectionProfile"}, /* tcgtpm::TCG_AT_TPM_PROTECTION_PROFILE */
    {"2.23.133.2.12", "tcg-at-tpmSecurityTarget"}, /* tcgtpm::TCG_AT_TPM_SECURITY_TARGET */
    {"2.23.133.2.13", "tcg-at-tbbProtectionProfile"}, /* tcgtpm::TCG_AT_TBB_PROTECTION_PROFILE */
    {"2.23.133.2.14", "tcg-at-tbbSecurityTarget"}, /* tcgtpm::TCG_AT_TBB_SECURITY_TARGET */
    {"2.23.133.2.15", "tcg-at-tpmIdLabel"}, /* tcgtpm::TCG_AT_TPM_ID_LABEL */
    {"2.23.133.2.16", "tcg-at-tpmSpecification"}, /* tcgtpm::TCG_AT_TPM_SPECIFICATION */
    {"2.23.133.2.17", "tcg-at-tcgPlatformSpecification"}, /* tcgtpm::TCG_AT_TCG_PLATFORM_SPECIFICATION */
    {"2.23.133.2.18", "tcg-at-tpmSecurityAssertions"}, /* tcgtpm::TCG_AT_TPM_SECURITY_ASSERTIONS */
    {"2.23.133.2.19", "tcg-at-tbbSecurityAssertions"}, /* tcgtpm::TCG_AT_TBB_SECURITY_ASSERTIONS */
    {"2.23.133.2.2", "tcg-at-tpmModel"}, /* tcgtpm::TCG_AT_TPM_MODEL */
    {"2.23.133.2.3", "tcg-at-tpmVersion"}, /* tcgtpm::TCG_AT_TPM_VERSION */
    {"2.23.133.2.4", "tcg-at-platformManufacturer"}, /* tcgtpm::TCG_AT_PLATFORM_MANUFACTURER */
    {"2.23.133.2.5", "tcg-at-platformModel"}, /* tcgtpm::TCG_AT_PLATFORM_MODEL */
    {"2.23.133.2.6", "tcg-at-platformVersion"}, /* tcgtpm::TCG_AT_PLATFORM_VERSION */
    {"2.23.133.3", "tcg-protocol"}, /* tcgtpm::TCG_PROTOCOL */
    {"2.23.133.3.1", "tcg-prt-tpmIdProtocol"}, /* tcgtpm::TCG_PRT_TPM_ID_PROTOCOL */
    {"2.23.133.4", "tcg-algorithm"}, /* tcgtpm::TCG_ALGORITHM */
    {"2.23.133.4.1", "tcg-algorithm-null"}, /* tcgtpm::TCG_ALGORITHM_NULL */
    {"2.23.133.6", "tcg-ce"}, /* tcgtpm::TCG_CE */
    {"2.23.133.6.2", "tcg-ce-relevantCredentials"}, /* tcgtpm::TCG_CE_RELEVANT_CREDENTIALS */
    {"2.23.133.6.3", "tcg-ce-relevantManifests"}, /* tcgtpm::TCG_CE_RELEVANT_MANIFESTS */
    {"2.23.133.6.4", "tcg-ce-virtualPlatformAttestationService"}, /* tcgtpm::TCG_CE_VIRTUAL_PLATFORM_ATTESTATION_SERVICE */
    {"2.23.133.6.5", "tcg-ce-migrationControllerAttestationService"}, /* tcgtpm::TCG_CE_MIGRATION_CONTROLLER_ATTESTATION_SERVICE */
    {"2.23.133.6.6", "tcg-ce-migrationControllerRegistrationService"}, /* tcgtpm::TCG_CE_MIGRATION_CONTROLLER_REGISTRATION_SERVICE */
    {"2.23.133.6.7", "tcg-ce-virtualPlatformBackupService"}, /* tcgtpm::TCG_CE_VIRTUAL_PLATFORM_BACKUP_SERVICE */
    {"2.23.133.8", "tcg-kp"}, /* tcgtpm::TCG_KP */
    {"2.23.133.8.1", "tcg-kp-EKCertificate"}, /* tcgtpm::TCG_KP_EK_CERTIFICATE */
    {"2.23.133.8.2", "tcg-kp-PlatformCertificate"}, /* tcgtpm::TCG_KP_PLATFORM_CERTIFICATE */
    {"2.23.133.8.3", "tcg-kp-AIKCertificate"}, /* tcgtpm::TCG_KP_AIK_CERTIFICATE */
};

#define YX_OID_NAMES_COUNT ((size_t)1352)

/*
 * Sorted indices of xx_pe_xx_pe_yx_oid_names[] for O(log N) binary search by dotted OID string.
 */
static const uint16_t xx_pe_yx_oid_sorted_idx[1352] = {
     720,  793,  794,  795,  796,  797,  798,  213,  799,  800,  214,  215,
     216,  721,  217,  218,  219,  220,  801,  221,  222,  802,  803,  804,
     805,  806,  807,  808,  809,  810,  811,  223,  224,  812,  225,  813,
     814,  226,  227,  228,  229,  230,  815,  816,  275,  231,  817,  818,
     819,  820,  232,  821,  822,  823,  233,  234,  235,  236,  237,  824,
     825,  826,  827,   49,   50,   51,   52,   53,   54,   55,   56,   57,
      58,   59,   60,   61,   62,   63,   64,   65,   66,   67,   68,   69,
      70,   71,   72,   73,   74,   75,   76,   77,   78,   79,   80,   81,
      82,   83,   84,   85,   86,   87,   88,   89,   90,   91,   92,   93,
      94,   95,   96,   97,   98,   99,  100,  101,  102,  103,  104,  105,
     106,  107,  108,  109,  110,  111,  141,  142,  143,  144,  145,  146,
     147,  148,  149,  150,  151,  152,  153,  154,   23,   24,   25,   26,
      27,   28,   29,   30,   31,   32,   33,   34,   35,   36,   37,   38,
      39,   40,   41,   42,   43,   44,   45,   46,   47,   48,  112,  113,
     114,  115,  116,  117,  118,  119,  120,  121,  122,  123,  124,  125,
     126,  127,  128,  129,  130,  131,  132,  133,  134,  135,  136,  137,
     138,  139,  140,  155,  156,  157,  158,  159,  160,  161,  162,  163,
     164,  165,  166,  167,  168,  169,  358,  359,  360,  361,  362, 1016,
      11,  915,  916, 1017, 1018, 1019, 1020, 1021, 1022, 1023, 1024, 1168,
    1198, 1025, 1026, 1027,    8,    9,   10, 1028, 1029,    5,    6, 1030,
    1031, 1032, 1199, 1200, 1201, 1202, 1203, 1204, 1205, 1206, 1207,  938,
     939,  940,  941,  942,  943,  285,  286,  287,  288,  289,  290,  291,
     292,  293,  294, 1189, 1297, 1033,  944, 1034, 1035, 1036, 1037,  945,
     946,  947, 1169,  948, 1170, 1038,  949,  328,  950, 1171, 1190,  951,
     952, 1191,  953,  954,  955,  956,  957,  329,  958,  959, 1039,  960,
     961,  962,  963,  964,  965,  966,  967,  968, 1172,  969,  970,  971,
     972,  973,  974, 1192,  975, 1298, 1299, 1300, 1301,  976,  977,  978,
    1173, 1193, 1194, 1195, 1196,  979,  980,  981,  982,  983,  984,  985,
     986,  987,  988,  989,  990,  991,  992,  993, 1197,  295, 1174,  296,
     297,  298,  299,  300,  301,  302,  303,  304,  305,  306,  307,  308,
     309,  310,  311,  312,  313,  314,  315,  316,  317,  318,  319,  320,
     321, 1175,  917,  918, 1040,    0,  919,  920,  994,  995,  266,  267,
     268,  269,  270,  271,  272,  273,  274, 1281, 1282, 1283, 1284, 1285,
    1041, 1042, 1043, 1044, 1045, 1046, 1047, 1048, 1049, 1050, 1051, 1052,
    1053,  921,  922,  923,  924,  925, 1054, 1055,  926,  927,  928,  929,
     930,  931,  932,  933,  934,  935,  936,  937, 1302, 1303, 1304, 1305,
       1,    7, 1260, 1261, 1262, 1263, 1264, 1265, 1266,  424,  425,  426,
     427,  428,  429,  430,  431,  432,  433,  434,  435,  436,  437,  438,
     439,  440,  441,  442,  443,  444,  445,  446,  447,  448,  449,  450,
     451,  452,  453,  454,  455,  456,  457,  458,  459,  460,  461,  462,
     463,  899,  900,  901,  902,  903,  904,  905,  906,  907,  908,  909,
     910,  911,  912,  913,  914,  594,  595,  596,  597,  598,  599,  600,
     601,  602,  603,  604,  605,  606,  607,  608,  609,  610,  611,  612,
     613,  614,  615,  616,  617,  618,  619,  620,  621,  622,  623,  624,
     625,  626,  627,  628,  629,  630,  631,  632,  633,  634,  635,  636,
     637,  638,  639,  640,  641,  642,  643,  644,  645,  646,  647,  648,
     649,  650,  651,  652,  653,  654,  655,  656,  657,  658,  659,  583,
     584,  585,  586,  587,  588,  589,  590,  591,  592,  593,  828,  829,
     830,  848, 1279, 1280,  722,  364,  365,  366,  367,  368,  369,  370,
     371,  372,  373,  374,  375,  376,  377,  378,  379,  380,  381,  382,
     383,  384,  385,  386,  387,  388,  389,  390,  391,  392,  393,  394,
     395,  396,  397,  398,  399,  400,  401,  402,  403,  404,  405,  406,
     407,  408,  409,  410,  411,  412,  413,  414,  415,  416,  417,  418,
     419,  420,  421,  422,  423,  464,  465,  466,  467,  468,  469,  470,
     471,  472,  473,  474,  475,  476,  477,  478,  479,  480,  481,  482,
     483,  484,  485,  486,  487,  488,  489,  490,  491,  492,  493,  494,
     495,  496,  497,  498,  499,  500,  501,  502,  503,  504,  505,  506,
     507,  508,  509,  510,  511,  512,  513,  514,  515,  516,  517,  518,
     519,  520,  521,  522,  523,  524,  525,  526,  527,  528,  529,  530,
     531,  532,  533,  534,  535,  536,  537,  538,  539,  540,  541,  542,
     543,  544,  545,  546,  547,  548,  549,  550,  551,  552,  553,  554,
     555,  556,  557,  558,  559,  560,  561,  562,  563,  564,  565,  566,
     567,  568,  569,  570,  571,  572,  573,  574,  575,  576,  577,  578,
     579,  580,  581,  582,  831,  832,  833,  834,  835,  836,  837,  838,
     839,  840,  841,  842,  843,  844,  845,  846,  847, 1185, 1186, 1187,
    1188,  263,  264,  265,  660,  661,  662,  663,  664,  665,  666,  667,
     688,  689,  690,  723,  249, 1159, 1160, 1161, 1162, 1163, 1164, 1165,
    1166, 1167, 1267, 1268, 1269, 1270, 1271, 1272, 1273, 1274, 1275, 1276,
    1277, 1278,  238,  239,   21,   22,   20, 1220, 1221, 1222, 1223, 1224,
    1225, 1226, 1227, 1228, 1229, 1230, 1231, 1232, 1233, 1234, 1235, 1236,
    1237, 1238, 1239, 1240, 1241, 1242, 1243, 1244, 1245, 1246, 1247, 1248,
    1249, 1250, 1251, 1252, 1253, 1254, 1255, 1256, 1257, 1258, 1259,  668,
     240,  241,  242,  243,  260,  261,  262,  244,  245,  246,  247,  248,
     996,  849, 1208,  850,  851, 1056,  852, 1057, 1058, 1059, 1060, 1061,
    1062, 1063, 1064, 1209, 1065, 1066, 1067, 1210, 1211, 1212,  997, 1213,
    1068, 1069, 1070, 1071, 1072, 1073, 1074, 1075, 1076, 1077, 1078, 1079,
    1080, 1081, 1082, 1083, 1084, 1085, 1086, 1087, 1088, 1089, 1090, 1091,
    1092, 1093,  853,  854,  855, 1214, 1215, 1216, 1217, 1286, 1287,  856,
     857, 1094, 1095,  858,  859,  860,  861,  862, 1096, 1097, 1098, 1099,
    1100, 1101, 1102, 1103, 1104, 1105, 1106, 1107, 1108, 1109, 1110,  863,
     864, 1176, 1177, 1178, 1179, 1180, 1181, 1182, 1183, 1184,  865,  866,
     867, 1111, 1112, 1113, 1114, 1115, 1116, 1117, 1118, 1119, 1120, 1121,
    1218, 1122, 1123, 1124, 1125, 1126, 1127, 1128, 1129, 1130, 1131, 1132,
    1133, 1134, 1135, 1136, 1137, 1138, 1139, 1140, 1141, 1142, 1143, 1144,
    1145, 1146, 1147, 1148, 1149, 1150, 1151, 1152, 1153,  998,  999, 1000,
    1219, 1001,  322,  323,  324,  325,  326,  327, 1002, 1154,  170, 1003,
    1004, 1005, 1006, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014, 1015,
     171,    2,  172,  173,  174, 1306, 1307, 1308, 1309,    3, 1310, 1311,
       4, 1155,  175,  176,  177,  182,   12, 1312, 1313, 1314,  183,  184,
     185,   13,  189,  190,  191,  192,  193,  194,  195,  196,  197,  198,
     199,  200,  186,  187,  188,  201,  202,  203,  204,  205,  206,  207,
     208,  209,  210,  211,  212, 1315,  178,  179,  180,  181,  276,  277,
     278,  279,  280,  331,  281,  282,  283,  284,  332, 1288, 1289, 1290,
    1291, 1292, 1293, 1294, 1295, 1296,  868,  869,  870,  871, 1316, 1317,
    1318, 1319, 1320, 1321, 1322, 1323, 1324, 1325, 1326, 1327, 1328, 1329,
    1330, 1331, 1332, 1333, 1334, 1335, 1336, 1337, 1338, 1339, 1340, 1341,
    1342, 1343, 1344, 1345, 1346, 1347, 1348, 1349, 1350, 1351,  691,  692,
     693,  694,  695,  696,  697,  698,  699,  700,  701,  702,  703,  704,
     250,  705,  251,  706,  707,  708,  709,  710,  711,  712,  713,  771,
     772,  773,  774,  775,  776,  714,  777,  363,  715,  716,  717,  718,
     719,  349,  669,  670,  333,  671,  672,  673,  350,  351,  334,  674,
     335,  675,  676,  677,  678,  679,  680,  681,  682,  683,  352,  353,
     354,  355,  356,  357,  872,  873,  874,  875,  876,  877,  878,  879,
     880,  881,  882,  883,  884,  885,  886,  887,  888,  889,  890,  891,
     892,  893,  894,  895,  896, 1156, 1157,  897,  898,  684,  685,   16,
     336,   17,  337,  724,  725,  726,  727,  728,  338,  729,  339,  730,
     340,  731,  341,  252,  732,  342,  733,  343,  734,  735,  344,  736,
     737,  345,  738,  739,  740,  253,   15,  254,  741,  742,  743,  744,
     745,  778,  779,  780,  781,  746,  782,  747,  748,  749,  750,  751,
     752,  753,  255,  754,  755,  756,  757,  783,  784,  256,   14,  330,
     758,  346, 1158,   18,  347,   19,  348,  686,  687,  759,  760,  257,
     258,  761,  785,  786,  787,  762,  788,  789,  763,  259,  790,  791,
     792,  764,  765,  766,  767,  768,  769,  770,
};

/*
 * Look up an OID string in dotted format (e.g. "2.5.4.3").
 * Returns the canonical YARA-X name, or NULL if not found.
 * Uses binary search over xx_pe_yx_oid_sorted_idx[].
 */
static const char *xx_pe_yx_oid_name(const char *dotted)
{
    if (!dotted) return NULL;
    size_t low = 0;
    size_t high = sizeof(xx_pe_yx_oid_sorted_idx) / sizeof(xx_pe_yx_oid_sorted_idx[0]);
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int cmp = strcmp(dotted, xx_pe_xx_pe_yx_oid_names[xx_pe_yx_oid_sorted_idx[mid]].oid);
        if (cmp == 0)
            return xx_pe_xx_pe_yx_oid_names[xx_pe_yx_oid_sorted_idx[mid]].name;
        if (cmp < 0)
            high = mid;
        else
            low = mid + 1;
    }
    return NULL;
}

