#pragma once

#include "archive/saa.h"

#include <functional>
#include <string>
#include <vector>

namespace saa
{
    struct PackFile
    {
        std::string name; // path or bare name, only the file name part is used (lowercased)
        std::vector<std::uint8_t> content;
    };

    // signs data, fills signature. 1..255 bytes (goes to the 8 bit field in the header)
    using SignFunction = std::function<bool(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& signature)>;

    struct PackOptions
    {
        KeySet keys; // only teaKey is used here, the rsa part lives inside sign
        SignFunction sign;
        std::uint64_t randomSeed = 0; // 0 = random, anything else = reproducible image (tests)
    };

    // builds a complete .saa image. false + error text on failure
    bool BuildArchive(const std::vector<PackFile>& files, const PackOptions& options, std::vector<std::uint8_t>& image, std::string& error);
}
