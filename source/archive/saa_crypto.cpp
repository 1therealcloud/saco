/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#if defined(SACO_BUILD)
#   include <stdafx.h>
#else // SACO_BUILD
#   include "utils/log.inl"
#endif // SACO_BUILD

#include "saa_crypto.h"

#if !defined(SACO_BUILD)
#   include <windows.h>
#endif // SACO_BUILD

#include <wincrypt.h>

namespace saa
{
    namespace
    {
        bool VerifyRsaSha1(const KeySet& keys, const std::uint8_t* data, std::size_t size, const std::uint8_t* signature, std::size_t signatureSize)
        {
            // verifycontext, so no persistent key container is needed for verifying
            HCRYPTPROV provider = 0;
            if (!CryptAcquireContextA(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
            {
#if defined(_DEBUG)
                Log("[saa::verify] CryptAcquireContext failed, error=%lu", GetLastError());
#endif // _DEBUG
                return false;
            }

            bool verified = false;
            HCRYPTHASH hash = 0;
            HCRYPTKEY key = 0;
            if (CryptCreateHash(provider, CALG_SHA1, 0, 0, &hash) && CryptImportKey(provider, keys.publicKeyBlob.data(), static_cast<DWORD>(keys.publicKeyBlob.size()), 0, 0, &key))
            {
                bool hashed = true;
                for (std::size_t position = 0; position < size && hashed;)
                {
                    const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size - position, 0x2800));
                    hashed = CryptHashData(hash, data + position, chunk, 0) != FALSE;
                    position += chunk;
                }
                verified = hashed && CryptVerifySignatureA(hash, signature, static_cast<DWORD>(signatureSize), key, nullptr, CRYPT_NOHASHOID) != FALSE;
                if (!verified)
                {
#if defined(_DEBUG)
                    Log("[saa::verify] verify failed, hashed=%d, error=%lu", hashed, GetLastError());
#endif // _DEBUG
                }
            }
            else
            {
#if defined(_DEBUG)
                Log("[saa::verify] CryptCreateHash/CryptImportKey failed, error=%lu", GetLastError());
#endif // _DEBUG
            }
            if (key)
                CryptDestroyKey(key);
            if (hash)
                CryptDestroyHash(hash);
            CryptReleaseContext(provider, 0);
            return verified;
        }
    }

    SignatureVerifier DefaultSignatureVerifier()
    {
        return &VerifyRsaSha1;
    }
}

namespace saa::win
{
    namespace
    {
        // CryptSignHash signs with the AT_SIGNATURE key of the *container*, not with an imported key handle,
        // so signing needs a real (temporary) named container, like the original tool had ("SAMP")
        constexpr const char* kContainerName = "saa_tool_temp";

        class TemporaryContainer
        {
        public:
            TemporaryContainer()
            {
                if (!CryptAcquireContextA(&provider_, kContainerName, nullptr, PROV_RSA_FULL, 0))
                    if (!CryptAcquireContextA(&provider_, kContainerName, nullptr, PROV_RSA_FULL, CRYPT_NEWKEYSET))
                        provider_ = 0;
            }
            ~TemporaryContainer()
            {
                if (provider_)
                {
                    CryptReleaseContext(provider_, 0);
                    HCRYPTPROV ignored = 0;
                    CryptAcquireContextA(&ignored, kContainerName, nullptr, PROV_RSA_FULL, CRYPT_DELETEKEYSET);
                }
            }
            TemporaryContainer(const TemporaryContainer&) = delete;
            TemporaryContainer& operator=(const TemporaryContainer&) = delete;

            HCRYPTPROV Provider() const
            {
                return provider_;
            }

        private:
            HCRYPTPROV provider_ = 0;
        };

        bool ExportBlob(HCRYPTKEY key, DWORD blobType, std::vector<std::uint8_t>& blob)
        {
            DWORD length = 0;
            if (!CryptExportKey(key, 0, blobType, 0, nullptr, &length))
                return false;
            blob.resize(length);
            if (!CryptExportKey(key, 0, blobType, 0, blob.data(), &length))
                return false;
            blob.resize(length);
            return true;
        }
    }

    bool GenerateRsaKeyPair(std::vector<std::uint8_t>& privateBlob, std::vector<std::uint8_t>& publicBlob)
    {
        TemporaryContainer container;
        if (!container.Provider())
            return false;

        HCRYPTKEY key = 0;
        if (!CryptGenKey(container.Provider(), AT_SIGNATURE, (1024u << 16) | CRYPT_EXPORTABLE, &key))
            return false;
        const bool ok = ExportBlob(key, PRIVATEKEYBLOB, privateBlob) && ExportBlob(key, PUBLICKEYBLOB, publicBlob);
        CryptDestroyKey(key);
        return ok;
    }

    bool SignRsaSha1(const std::vector<std::uint8_t>& privateBlob, const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& signature)
    {
        TemporaryContainer container;
        if (!container.Provider())
            return false;

        HCRYPTKEY key = 0;
        if (!CryptImportKey(container.Provider(), privateBlob.data(), static_cast<DWORD>(privateBlob.size()), 0, CRYPT_EXPORTABLE, &key))
            return false;

        HCRYPTHASH hash = 0;
        bool ok = CryptCreateHash(container.Provider(), CALG_SHA1, 0, 0, &hash) != FALSE;
        for (std::size_t position = 0; ok && position < size;)
        {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size - position, 0x2800));
            ok = CryptHashData(hash, data + position, chunk, 0) != FALSE;
            position += chunk;
        }

        DWORD signatureLength = 0;
        ok = ok && CryptSignHashA(hash, AT_SIGNATURE, nullptr, CRYPT_NOHASHOID, nullptr, &signatureLength);
        if (ok)
        {
            signature.resize(signatureLength);
            ok = CryptSignHashA(hash, AT_SIGNATURE, nullptr, CRYPT_NOHASHOID, signature.data(), &signatureLength) != FALSE;
            signature.resize(signatureLength);
        }

        if (hash)
            CryptDestroyHash(hash);
        CryptDestroyKey(key);
        return ok;
    }

    bool RandomBytes(std::uint8_t* destination, std::size_t count)
    {
        HCRYPTPROV provider = 0;
        if (!CryptAcquireContextA(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
            return false;
        const bool ok = CryptGenRandom(provider, static_cast<DWORD>(count), destination) != FALSE;
        CryptReleaseContext(provider, 0);
        return ok;
    }
}
