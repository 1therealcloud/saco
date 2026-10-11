#include "saa_pack.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <numeric>
#include <random>
#include <unordered_set>

namespace saa
{
    namespace
    {
        constexpr std::uint32_t kFakeHeaderMagic = 0x32414153u; // "SAA2", decoy v1 header
        constexpr std::uint32_t kFakeHeaderFileCount = 0x16;
        constexpr std::uint32_t kMaxFileSize = 0xFFFFFFu; // 24 bit size field
        constexpr std::uint32_t kTableSize = kMaxEntries * sizeof(Entry);

        void WriteLe32(std::uint8_t* destination, std::uint32_t value)
        {
            std::memcpy(destination, &value, 4);
        }

        std::string LowerFileNameOnly(const std::string& path)
        {
            const std::size_t separator = path.find_last_of("/\\");
            std::string name = separator == std::string::npos ? path : path.substr(separator + 1);
            for (auto& character : name)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return name;
        }

        std::uint32_t EncodeDataBlock(std::uint32_t size, std::uint8_t previous, std::uint32_t nameHash)
        {
            const std::uint32_t decoded = (size << 8) | previous;
            return RotL32(decoded ^ nameHash, 19) ^ kEntryMagic;
        }
    }

    bool BuildArchive(const std::vector<PackFile>& files, const PackOptions& options, std::vector<std::uint8_t>& image, std::string& error)
    {
        image.clear();

        if (!options.sign)
        {
            error = "no sign function";
            return false;
        }
        if (files.empty())
        {
            error = "no files to pack";
            return false;
        }
        if (files.size() > kMaxEntries - 1) // one slot is the reserved "invalid index"
        {
            error = "too many files (max 255)";
            return false;
        }

        std::mt19937_64 randomEngine(options.randomSeed != 0 ? options.randomSeed : std::random_device{}());
        auto randomBytes = [&](std::uint8_t* destination, std::size_t count) {
            for (std::size_t i = 0; i < count; ++i)
                destination[i] = static_cast<std::uint8_t>(randomEngine());
        };
        auto random32 = [&] {
            return static_cast<std::uint32_t>(randomEngine());
        };

        // names -> hashes, reject empty files and hash duplicates
        std::vector<std::uint32_t> fileHashes;
        std::unordered_set<std::uint32_t> usedHashes;
        for (const auto& file : files)
        {
            const std::string name = LowerFileNameOnly(file.name);
            if (name.empty())
            {
                error = "empty file name";
                return false;
            }
            if (file.content.empty() || file.content.size() > kMaxFileSize)
            {
                error = "bad size (must be 1 byte .. 16 MB): " + name;
                return false;
            }
            const std::uint32_t hash = HashString(name);
            if (!usedHashes.insert(hash).second)
            {
                error = "duplicate name hash: " + name;
                return false;
            }
            fileHashes.push_back(hash);
        }

        // header: random everywhere except the decoy v1 part and the id fields
        std::uint8_t header[kHeaderSize];
        randomBytes(header, sizeof(header));
        WriteLe32(header + 0, kFakeHeaderMagic);
        WriteLe32(header + 4, kFakeHeaderFileCount);
        const std::uint32_t xorKey = random32();
        const std::uint8_t invalidIndex = static_cast<std::uint8_t>((xorKey >> 5) & 0xFFu);

        // entry table: random fillers that point at themselves
        std::array<Entry, kMaxEntries> entries{};
        for (std::uint32_t i = 0; i < kMaxEntries; ++i)
        {
            std::uint32_t hash;
            do
            {
                hash = random32();
            }
            while (usedHashes.count(hash) != 0);
            entries[i].nameHash = hash;
            entries[i].dataBlock = EncodeDataBlock(random32() & kMaxFileSize, static_cast<std::uint8_t>(i), hash);
        }

        // real files go to random distinct slots, never to the invalid index
        std::vector<std::uint32_t> slots;
        for (std::uint32_t i = 0; i < kMaxEntries; ++i)
            if (i != invalidIndex)
                slots.push_back(i);
        std::shuffle(slots.begin(), slots.end(), randomEngine);

        for (std::size_t fileIndex = 0; fileIndex < files.size(); ++fileIndex)
        {
            const std::uint32_t slot = slots[fileIndex];
            const std::uint8_t previous = fileIndex == 0 ? invalidIndex : static_cast<std::uint8_t>(slots[fileIndex - 1]);
            entries[slot].nameHash = fileHashes[fileIndex];
            entries[slot].dataBlock = EncodeDataBlock(static_cast<std::uint32_t>(files[fileIndex].content.size()), previous, fileHashes[fileIndex]);
        }

        std::uint8_t tableBytes[kTableSize];
        std::memcpy(tableBytes, entries.data(), kTableSize);

        // image = header | file blocks | encrypted table | signature
        image.assign(kHeaderSize, 0);

        // file data. the key comes from the plain (already obfuscated) table,
        // and the tea chain runs through all blocks of one file
        for (std::size_t fileIndex = 0; fileIndex < files.size(); ++fileIndex)
        {
            const auto& content = files[fileIndex].content;
            Tea tea;
            tea.SetKey(tableBytes + (fileHashes[fileIndex] % (kTableSize - kTeaKeySize)), 0);

            for (std::size_t position = 0; position < content.size(); position += kBlockSize)
            {
                std::uint8_t block[kBlockSize];
                const std::size_t chunk = std::min<std::size_t>(kBlockSize, content.size() - position);
                std::memcpy(block, content.data() + position, chunk);
                if (chunk < kBlockSize)
                    randomBytes(block + chunk, kBlockSize - chunk);
                tea.EncryptData(block, kBlockSize);
                image.insert(image.end(), block, block + kBlockSize);
            }
        }

        // entry table, encrypted with the key set's tea key
        {
            std::uint8_t encryptedTable[kTableSize];
            std::memcpy(encryptedTable, tableBytes, kTableSize);
            Tea tea;
            tea.SetKey(options.keys.teaKey.data(), 0);
            tea.EncryptData(encryptedTable, kTableSize);
            image.insert(image.end(), encryptedTable, encryptedTable + kTableSize);
        }

        // signature covers everything after the header
        std::vector<std::uint8_t> signature;
        if (!options.sign(image.data() + kHeaderSize, image.size() - kHeaderSize, signature))
        {
            error = "signing failed";
            image.clear();
            return false;
        }
        if (signature.empty() || signature.size() > 0xFF)
        {
            error = "signature size doesn't fit the header (need 1..255 bytes, 1024 bit rsa gives 128)";
            image.clear();
            return false;
        }
        image.insert(image.end(), signature.begin(), signature.end());

        // header id fields are written last because they hold the signature size
        const std::uint32_t completeId = kFileId | (kFileVersion << 20) | (static_cast<std::uint32_t>(signature.size()) << 23);
        WriteLe32(header + 0xF8, completeId ^ xorKey);
        WriteLe32(header + 0xFC, xorKey);
        std::memcpy(image.data(), header, sizeof(header));
        return true;
    }
}
