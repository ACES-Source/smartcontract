// Copyright (c) 2009-2017 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Verge Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VERGE_CRYPTO_HMAC_SHA256_H
#define VERGE_CRYPTO_HMAC_SHA256_H

#include <crypto/sha256.h>

#include <stdint.h>
#include <stdlib.h>

/** A hasher class for HMAC-SHA-256. */
class CHMAC_SHA256
{
private:
    CSHA256 outer;
    CSHA256 inner;

public:
    static const size_t OUTPUT_SIZE = 32;

    CHMAC_SHA256(const unsigned char* key, size_t keylen);
    CHMAC_SHA256& Write(const unsigned char* data, size_t len)
    {
        inner.Write(data, len);
        return *this;
    }
    void Finalize(unsigned char hash[OUTPUT_SIZE]);
};

#endif // VERGE_CRYPTO_HMAC_SHA256_H
