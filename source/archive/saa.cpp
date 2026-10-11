#if defined(SACO_BUILD)
#   include <stdafx.h>
#else // SACO_BUILD
#   include "utils/log.inl"
#   include <algorithm>
#   include <cstring>
#endif // SACO_BUILD

#include "saa.h"

#include <cctype>
#include <fstream>

#define SAA_LOG_NAMES // log files names

namespace saa
{
    namespace
    {
        constexpr std::uint32_t kRounds = 32;
        constexpr std::uint32_t kDelta = 0x9E3779B9u;
        constexpr std::uint32_t kInitialSum = kDelta * kRounds;
        constexpr std::uint32_t kMask = 0xFFFFFFFFu; // valid signature -> no masking tricks

        std::uint32_t ReadLe32(const std::uint8_t* p)
        {
            std::uint32_t v;
            std::memcpy(&v, p, 4);
            return v;
        }
    }

    void Tea::SetKey(const std::uint8_t* bytes, std::uint8_t xorByte)
    {
        std::uint8_t raw[16];
        std::memcpy(raw, bytes, sizeof(raw));
        for (auto& b : raw)
            b ^= xorByte;
        std::memcpy(key_, raw, sizeof(key_));
    }

    void Tea::DecryptBlock(std::uint32_t& v0, std::uint32_t& v1)
    {
        const std::uint32_t c0 = v0, c1 = v1;
        std::uint32_t sum = kInitialSum;
        for (std::uint32_t i = 0; i < kRounds; ++i)
        {
            v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key_[(sum >> 11) & 3u]);
            sum -= kDelta;
            v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key_[sum & 3u]);
        }
        key_[0] ^= c0;
        key_[1] ^= c1;
        key_[2] ^= c0;
        key_[3] ^= c1;
    }

    void Tea::EncryptBlock(std::uint32_t& v0, std::uint32_t& v1)
    {
        std::uint32_t sum = 0;
        for (std::uint32_t i = 0; i < kRounds; ++i)
        {
            v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key_[sum & 3u]);
            sum += kDelta;
            v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key_[(sum >> 11) & 3u]);
        }
        key_[0] ^= v0;
        key_[1] ^= v1;
        key_[2] ^= v0;
        key_[3] ^= v1;
    }

    void Tea::DecryptData(std::uint8_t* data, std::uint32_t length)
    {
        // whole 8 byte blocks only, the original would overread on odd sizes
        for (std::uint32_t off = 0; off + 8 <= length; off += 8)
        {
            std::uint32_t v0 = ReadLe32(data + off), v1 = ReadLe32(data + off + 4);
            DecryptBlock(v0, v1);
            std::memcpy(data + off, &v0, 4);
            std::memcpy(data + off + 4, &v1, 4);
        }
    }

    void Tea::EncryptData(std::uint8_t* data, std::uint32_t length)
    {
        for (std::uint32_t off = 0; off + 8 <= length; off += 8)
        {
            std::uint32_t v0 = ReadLe32(data + off), v1 = ReadLe32(data + off + 4);
            EncryptBlock(v0, v1);
            std::memcpy(data + off, &v0, 4);
            std::memcpy(data + off + 4, &v1, 4);
        }
    }

    KeySet MakeKeySet(const std::uint8_t* encodedPublicKey, std::size_t publicKeySize, const std::uint8_t* encodedTeaKey, std::uint8_t xorByte)
    {
        KeySet keys;
        keys.publicKeyBlob.resize(publicKeySize);
        for (std::size_t i = 0; i < publicKeySize; ++i)
            keys.publicKeyBlob[i] = encodedPublicKey[i] ^ xorByte;
        for (std::size_t i = 0; i < kTeaKeySize; ++i)
            keys.teaKey[i] = encodedTeaKey[i] ^ xorByte;
        return keys;
    }

    std::uint32_t HashString(std::string_view text)
    {
        const auto* key = reinterpret_cast<const std::uint8_t*>(text.data());
        const auto length = static_cast<std::uint32_t>(text.size());
        std::uint32_t a = 0x9E3779B9u, b = 0x9E3779B9u, c = 0x12345678u;
        std::uint32_t len = length;

        auto mix = [&] {
            a -= b;
            a -= c;
            a ^= (c >> 13);
            b -= c;
            b -= a;
            b ^= (a << 8);
            c -= a;
            c -= b;
            c ^= (b >> 13);
            a -= b;
            a -= c;
            a ^= (c >> 12);
            b -= c;
            b -= a;
            b ^= (a << 16);
            c -= a;
            c -= b;
            c ^= (b >> 5);
            a -= b;
            a -= c;
            a ^= (c >> 3);
            b -= c;
            b -= a;
            b ^= (a << 10);
            c -= a;
            c -= b;
            c ^= (b >> 15);
        };

        while (len >= 12)
        {
            a += ReadLe32(key);
            b += ReadLe32(key + 4);
            c += ReadLe32(key + 8);
            mix();
            key += 12;
            len -= 12;
        }

        c += length;
        switch (len) // the low byte of c is taken by the length
        {
            case 11:
                c += std::uint32_t(key[10]) << 24;
                [[fallthrough]];
            case 10:
                c += std::uint32_t(key[9]) << 16;
                [[fallthrough]];
            case 9:
                c += std::uint32_t(key[8]) << 8;
                [[fallthrough]];
            case 8:
                b += std::uint32_t(key[7]) << 24;
                [[fallthrough]];
            case 7:
                b += std::uint32_t(key[6]) << 16;
                [[fallthrough]];
            case 6:
                b += std::uint32_t(key[5]) << 8;
                [[fallthrough]];
            case 5:
                b += key[4];
                [[fallthrough]];
            case 4:
                a += std::uint32_t(key[3]) << 24;
                [[fallthrough]];
            case 3:
                a += std::uint32_t(key[2]) << 16;
                [[fallthrough]];
            case 2:
                a += std::uint32_t(key[1]) << 8;
                [[fallthrough]];
            case 1:
                a += key[0];
                [[fallthrough]];
            default:
                break;
        }
        mix();
        return c;
    }

#if defined(SAA_LOG_NAMES)
    namespace
    {
        constexpr BuiltinName kBuiltin[] = {
            {0x03EF665C, "tracks4.dat"},  {0x04EF7129, "props.ide"},    {0x11A462D1, "script.img"},  {0x131FAD35, "shopping.dat"}, {0x260D1B97, "vehicle.txd"},
            {0x2ACF3319, "default.ide"},  {0x2CC7CE25, "handling.cfg"}, {0x4633BCEB, "stream.ini"},  {0x4E643D5E, "default.dat"},  {0x508AB83F, "surface.dat"},
            {0x5440792E, "ar_stats.dat"}, {0x57D2BFE5, "carmods.dat"},  {0x6C53ABCC, "LAxref.ide"},  {0x6C62978A, "peds.ide"},     {0x6FDCA2BE, "ped.dat"},
            {0x7ABAA31E, "LAn2.ide"},     {0x9C2824DB, "LAw2.ide"},     {0xA848B69A, "bindat.bin"},  {0xB7FFA1CB, "object.dat"},   {0xBADDEA6B, "logo.png"},
            {0xC1D9E789, "timecyc.dat"},  {0xCBC78E39, "melee.dat"},    {0xCED408A7, "tracks2.dat"}, {0xD83F24DD, "main.scm"},     {0xE9A7DF22, "gta.dat"},
            {0xEBFA9AB6, "weapon.dat"},   {0xEE6DFCB7, "vehicles.ide"}, {0xF7979239, "loadscs.txd"},
        };
    }

    const BuiltinName* BuiltinNames(std::size_t& count)
    {
        count = sizeof(kBuiltin) / sizeof(kBuiltin[0]);
        return kBuiltin;
    }

    const char* BuiltinNameFromHash(std::uint32_t hash)
    {
        for (const auto& b : kBuiltin)
            if (b.hash == hash)
                return b.name;
        return nullptr;
    }
#endif

    bool Archive::Open(const std::string& path, const std::vector<KeySet>& trustedKeys, SignatureVerifier verifier)
    {
        Log("[saa::open] path=%s", path.c_str());

        Close();
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            Log("[saa::open] can't open file");
            return false;
        }
        const std::streamsize size = file.tellg();
        if (size <= 0)
        {
            Log("[saa::open] file is empty");
            return false;
        }
        std::vector<std::uint8_t> image(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(image.data()), size))
        {
            Log("[saa::open] read failed, size=%lld", static_cast<long long>(size));
            return false;
        }
        return OpenMemory(std::move(image), trustedKeys, verifier);
    }

    bool Archive::OpenMemory(std::vector<std::uint8_t> image, const std::vector<KeySet>& trustedKeys, SignatureVerifier verifier)
    {
        Close();
        image_ = std::move(image);
        Log("[saa::open] image size=%zu", image_.size());

        if (!ParseHeader() || !LoadEntries(trustedKeys, verifier))
        {
            Log("[saa::open] failed");
            Close();
            return false;
        }
        open_ = true;

        std::uint32_t files = 0;
        for (std::uint32_t i = 0; i < kMaxEntries; ++i)
            files += IsValidFile(i);
        Log("[saa::open] success, files=%u", files);
        return true;
    }

    void Archive::Close()
    {
        if (open_)
            Log("[saa::close] cached files=%zu", cache_.size());

        open_ = false;
        matchedKeyIndex_ = -1;
        signatureSize_ = 0;
        image_ = {};
        byHash_.clear();
        cache_.clear();
    }

    bool Archive::ParseHeader()
    {
        if (image_.size() < kHeaderSize)
        {
            Log("[saa::header] too small");
            return false;
        }
        // completeId is at 0xF8 and xorKey right after it
        const std::uint32_t xorKey = ReadLe32(&image_[0xFC]);
        const std::uint32_t id = ReadLe32(&image_[0xF8]) ^ xorKey;
        if ((id & 0x000FFFFFu) != kFileId || (id & 0x00700000u) != (kFileVersion << 20))
        {
            Log("[saa::header] bad id=0x%08X", id);
            return false;
        }
        invalidIndex_ = static_cast<std::uint8_t>((xorKey >> 5) & 0xFFu);
        signatureSize_ = (id >> 23) & 0xFFu;
        if (signatureSize_ == 0)
        {
            Log("[saa::header] zero signature size");
            return false;
        }
        return true;
    }

    bool Archive::LoadEntries(const std::vector<KeySet>& trustedKeys, SignatureVerifier verifier)
    {
        constexpr std::uint32_t tableSize = kMaxEntries * sizeof(Entry);
        if (verifier == nullptr || trustedKeys.empty() || image_.size() < kHeaderSize + tableSize + signatureSize_)
        {
            Log("[saa::entries] no verifier/keys or image too small");
            return false;
        }

        const std::size_t signatureBegin = image_.size() - signatureSize_;
        matchedKeyIndex_ = -1;
        for (std::size_t i = 0; i < trustedKeys.size(); ++i)
        {
            if (verifier(trustedKeys[i], image_.data() + kHeaderSize, signatureBegin - kHeaderSize, image_.data() + signatureBegin, signatureSize_))
            {
                matchedKeyIndex_ = static_cast<int>(i);
                break;
            }
        }
        if (matchedKeyIndex_ < 0)
        {
            Log("[saa::entries] signature check failed (no trusted key matched)");
            return false;
        }
        Log("[saa::entries] signature ok, key set #%d", matchedKeyIndex_);

        std::uint8_t table[tableSize];
        std::memcpy(table, image_.data() + (signatureBegin - tableSize), tableSize);
        Tea tea;
        tea.SetKey(trustedKeys[static_cast<std::size_t>(matchedKeyIndex_)].teaKey.data(), 0);
        tea.DecryptData(table, tableSize);
        std::memcpy(entries_.data(), table, tableSize);

        // duplicate hashes: first one wins
        byHash_.clear();
        for (std::uint32_t i = 0; i < kMaxEntries; ++i)
        {
            const std::uint32_t hash = entries_[i].nameHash;
            byHash_.emplace(hash, i);
        }
        return true;
    }

    bool Archive::IsValidFile(std::uint32_t id) const
    {
        // fillers point at themselves
        return open_ && id < kMaxEntries && entries_[id].Previous(kMask) != id;
    }

    bool Archive::FindFileByHash(std::uint32_t hash, std::uint32_t& id) const
    {
        if (!open_)
            return false;
        const auto it = byHash_.find(hash);
        if (it == byHash_.end() || !IsValidFile(it->second))
            return false;
        id = it->second;
        return true;
    }

    bool Archive::FindFile(std::string_view name, std::uint32_t& id) const
    {
        if (name.size() >= 260)
            return false;
        char lower[260];
        for (std::size_t i = 0; i < name.size(); ++i)
            lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
        return FindFileByHash(HashString(std::string_view(lower, name.size())), id);
    }

    bool Archive::FileHash(std::uint32_t id, std::uint32_t& hash) const
    {
        if (!IsValidFile(id))
            return false;
        hash = entries_[id].nameHash;
        return true;
    }

    bool Archive::FileSize(std::uint32_t id, std::uint32_t& size) const
    {
        if (!IsValidFile(id))
            return false;
        const std::uint32_t s = entries_[id].Size(kMask);
        if (s == 0)
            return false;
        size = s;
        return true;
    }

    bool Archive::FileData(std::uint32_t id, const std::uint8_t*& data, std::shared_ptr<const std::vector<std::uint8_t>>* owner)
    {
        std::uint32_t fileSize;
        if (!FileSize(id, fileSize))
            return false;
        if (const auto it = cache_.find(id); it != cache_.end())
        {
            data = it->second->data();
            if (owner)
                *owner = it->second;
            return true;
        }

        const Entry& entry = entries_[id];
        std::uint8_t previous = entry.Previous(kMask);

        // files are stored one after another along the "previous" chain,
        // so the offset is the sum of everything before this one
        std::uint64_t offset = kHeaderSize;
        std::uint32_t guard = 0;
        while (previous != invalidIndex_)
        {
            if (++guard > kMaxEntries)
            {
                Log("[saa::file] id=%u, broken chain", id);
                return false;
            }
            const std::uint32_t d = entries_[previous].Decode(kMask);
            offset += AlignBlock(d >> 8);
            previous = static_cast<std::uint8_t>(d);
        }

        const std::uint32_t padded = AlignBlock(fileSize);
        if (offset + padded > image_.size())
        {
            Log("[saa::file] id=%u, data out of range (offset=%llu, size=%u)", id, static_cast<unsigned long long>(offset), padded);
            return false;
        }

        // file key = 16 bytes of the decrypted table at hash % (2048 - 16)
        constexpr std::uint32_t tableSize = kMaxEntries * sizeof(Entry);
        const auto* tableBytes = reinterpret_cast<const std::uint8_t*>(entries_.data());
        Tea tea;
        tea.SetKey(tableBytes + (entry.nameHash % (tableSize - kTeaKeySize)), 0);

        std::vector<std::uint8_t> buf(image_.begin() + static_cast<std::ptrdiff_t>(offset), image_.begin() + static_cast<std::ptrdiff_t>(offset + padded));
        for (std::uint32_t pos = 0; pos < fileSize; pos += kBlockSize)
            tea.DecryptData(buf.data() + pos, kBlockSize);

        const auto& cached = cache_.emplace(id, std::make_shared<const std::vector<std::uint8_t>>(std::move(buf))).first->second;
        data = cached->data();
        if (owner)
            *owner = cached;

#if defined(SAA_LOG_NAMES)
        const char* name = BuiltinNameFromHash(entry.nameHash);
        Log("[saa::file] id=%u, name=%s, size=%u", id, name ? name : "?", fileSize);
#else
        Log("[saa::file] id=%u, size=%u", id, fileSize);
#endif // SAA_LOG_NAMES
        return true;
    }

    void Archive::UnloadFile(std::uint32_t id)
    {
        cache_.erase(id);
    }
}
