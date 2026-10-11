#pragma once

#include <memory>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace saa
{
    constexpr std::uint32_t kFileId = 0x83433;
    constexpr std::uint32_t kFileVersion = 2;
    constexpr std::uint32_t kBlockSize = 0x800;
    constexpr std::uint32_t kMaxEntries = 256;
    constexpr std::uint32_t kHeaderSize = 0x100;
    constexpr std::uint32_t kSignatureSize = 0x80;
    constexpr std::uint32_t kTeaKeySize = 0x10;
    constexpr std::uint32_t kInvalidFile = 0xFFFFFFFFu;

    constexpr std::uint32_t AlignBlock(std::uint32_t size)
    {
        return (size + kBlockSize - 1u) & ~(kBlockSize - 1u);
    }

    constexpr std::uint32_t RotR32(std::uint32_t v, unsigned n)
    {
        return (v >> n) | (v << (32u - n));
    }

    constexpr std::uint32_t RotL32(std::uint32_t v, unsigned n)
    {
        return (v << n) | (v >> (32u - n));
    }

    constexpr std::uint32_t kEntryMagic = 0xC103D3E7u;

#pragma pack(push, 1)
    struct Entry
    {
        std::uint32_t nameHash;
        std::uint32_t dataBlock; // obfuscated (size << 8 | previous index)

        std::uint32_t Decode(std::uint32_t mask) const
        {
            return RotR32(dataBlock ^ kEntryMagic, 19) ^ (nameHash & mask);
        }
        std::uint8_t Previous(std::uint32_t mask) const
        {
            return static_cast<std::uint8_t>(Decode(mask));
        }
        std::uint32_t Size(std::uint32_t mask) const
        {
            return Decode(mask) >> 8;
        }
    };
#pragma pack(pop)
    static_assert(sizeof(Entry) == 8);

    // tea variant from samp: the key gets chained with the ciphertext
    class Tea
    {
    public:
        void SetKey(const std::uint8_t* bytes, std::uint8_t xorByte);
        void DecryptBlock(std::uint32_t& v0, std::uint32_t& v1);
        void EncryptBlock(std::uint32_t& v0, std::uint32_t& v1);
        void DecryptData(std::uint8_t* data, std::uint32_t length);
        void EncryptData(std::uint8_t* data, std::uint32_t length);

    private:
        std::uint32_t key_[4]{};
    };

    // expects an already lowercased name
    std::uint32_t HashString(std::string_view text);

    // one trust root: rsa public key (for the signature) + tea key (for the entry table).
    // official samp.saa and our own archives are just two different key sets
    struct KeySet
    {
        std::vector<std::uint8_t> publicKeyBlob; // wincrypt PUBLICKEYBLOB, plain (not xored)
        std::array<std::uint8_t, kTeaKeySize> teaKey{};
    };

    // builds a key set from xor-encoded constants (the format genkeys emits)
    KeySet MakeKeySet(const std::uint8_t* encodedPublicKey, std::size_t publicKeySize, const std::uint8_t* encodedTeaKey, std::uint8_t xorByte);

    using SignatureVerifier = bool (*)(const KeySet& keys, const std::uint8_t* data, std::size_t size, const std::uint8_t* signature, std::size_t signatureSize);

    // wincrypt rsa + sha1 (saa_crypto.cpp)
    SignatureVerifier DefaultSignatureVerifier();

    struct BuiltinName
    {
        std::uint32_t hash;
        const char* name;
    };
    const BuiltinName* BuiltinNames(std::size_t& count);
    const char* BuiltinNameFromHash(std::uint32_t hash); // nullptr if unknown

    class Archive
    {
    public:
        // all of these return false on any failure, archive stays closed.
        // the archive is accepted if its signature matches any of trustedKeys,
        // and that key set's tea key then decrypts the entry table.
        // a bad signature counts as a failure too
        bool Open(const std::string& path, const std::vector<KeySet>& trustedKeys, SignatureVerifier verifier = DefaultSignatureVerifier());
        bool OpenMemory(std::vector<std::uint8_t> image, const std::vector<KeySet>& trustedKeys, SignatureVerifier verifier = DefaultSignatureVerifier());
        void Close();
        bool IsOpen() const
        {
            return open_;
        }

        // index in trustedKeys of the key set that signed this archive, -1 if closed
        int MatchedKeyIndex() const
        {
            return matchedKeyIndex_;
        }

        // id is an index in the entry table. name is case-insensitive
        bool FindFile(std::string_view name, std::uint32_t& id) const;
        bool FindFileByHash(std::uint32_t hash, std::uint32_t& id) const;
        bool IsValidFile(std::uint32_t id) const;
        bool FileHash(std::uint32_t id, std::uint32_t& hash) const;
        bool FileSize(std::uint32_t id, std::uint32_t& size) const;

        // decrypts on first call and caches. pointer lives until
        // UnloadFile/Close, buffer is padded to 0x800
        // owner keeps the data alive after UnloadFile/Close
        bool FileData(std::uint32_t id, const std::uint8_t*& data, std::shared_ptr<const std::vector<std::uint8_t>>* owner = nullptr);
        void UnloadFile(std::uint32_t id);

    private:
        bool ParseHeader();
        bool LoadEntries(const std::vector<KeySet>& trustedKeys, SignatureVerifier verifier);

        bool open_ = false;
        int matchedKeyIndex_ = -1;
        std::uint32_t signatureSize_ = 0;
        std::vector<std::uint8_t> image_;
        std::uint8_t invalidIndex_ = 0;
        std::array<Entry, kMaxEntries> entries_{};
        std::unordered_map<std::uint32_t, std::uint32_t> byHash_;
        std::unordered_map<std::uint32_t, std::shared_ptr<const std::vector<std::uint8_t>>> cache_;
    };
}
