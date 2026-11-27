/*********************************************************************
* Filename:   sha256.h
* Author:     Brad Conte (brad AT bradconte.com)
* Source:     https://github.com/B-Con/crypto-algorithms (public domain)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Defines the API for the corresponding SHA-256 implementation.
* Note:       Vendored as-is apart from clang-format style, the Details line,
              and the hex helper added at the end.
*********************************************************************/

#ifndef SHA256_H
#define SHA256_H

/*************************** HEADER FILES ***************************/
#include <stddef.h>

/****************************** MACROS ******************************/
#define SHA256_BLOCK_SIZE 32 // SHA256 outputs a 32 byte digest

/**************************** DATA TYPES ****************************/
typedef unsigned char BYTE; // 8-bit byte
typedef unsigned int WORD;  // 32-bit word, change to "long" for 16-bit machines

typedef struct
{
    BYTE data[64];
    WORD datalen;
    unsigned long long bitlen;
    WORD state[8];
} SHA256_CTX;

/*********************** FUNCTION DECLARATIONS **********************/
void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const BYTE data[], size_t len);
void sha256_final(SHA256_CTX *ctx, BYTE hash[]);

/********************* ADDED BY THIS PROJECT ************************/
#define SHA256_HEX_SIZE (SHA256_BLOCK_SIZE * 2 + 1)

/** Lowercase hex of @p hash and a NUL, for a log line. */
void sha256_hex(const BYTE hash[], char out[SHA256_HEX_SIZE]);

#endif // SHA256_H
