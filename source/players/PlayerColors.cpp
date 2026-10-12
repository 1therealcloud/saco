#include <stdafx.h>

#include "PlayerColors.h"

namespace PlayerColors
{
    namespace
    {
        constexpr std::array<std::uint32_t, 100> kDefaultColors{
            0xFF8C13FFu, 0xC715FFFFu, 0x20B2AAFFu, 0xDC143CFFu, 0x6495EDFFu, 0xF0E68CFFu, 0x778899FFu, 0xFF1493FFu, 0xF4A460FFu, 0xEE82EEFFu, 0xFFD720FFu, 0x8B4513FFu, 0x4949A0FFu,
            0x148B8BFFu, 0x14FF7FFFu, 0x556B2FFFu, 0x0FD9FAFFu, 0x10DC29FFu, 0x534081FFu, 0x0495CDFFu, 0xEF6CE8FFu, 0xBD34DAFFu, 0x247C1BFFu, 0x0C8E5DFFu, 0x635B03FFu, 0xCB7ED3FFu,
            0x65ADEBFFu, 0x5C1ACCFFu, 0xF2F853FFu, 0x11F891FFu, 0x7B39AAFFu, 0x53EB10FFu, 0x54137DFFu, 0x275222FFu, 0xF09F5BFFu, 0x3D0A4FFFu, 0x22F767FFu, 0xD63034FFu, 0x9A6980FFu,
            0xDFB935FFu, 0x3793FAFFu, 0x90239DFFu, 0xE9AB2FFFu, 0xAF2FF3FFu, 0x057F94FFu, 0xB98519FFu, 0x388EEAFFu, 0x028151FFu, 0xA55043FFu, 0x0DE018FFu, 0x93AB1CFFu, 0x95BAF0FFu,
            0x369976FFu, 0x18F71FFFu, 0x4B8987FFu, 0x491B9EFFu, 0x829DC7FFu, 0xBCE635FFu, 0xCEA6DFFFu, 0x20D4ADFFu, 0x2D74FDFFu, 0x3C1C0DFFu, 0x12D6D4FFu, 0x48C000FFu, 0x2A51E2FFu,
            0xE3AC12FFu, 0xFC42A8FFu, 0x2FC827FFu, 0x1A30BFFFu, 0xB740C2FFu, 0x42ACF5FFu, 0x2FD9DEFFu, 0xFAFB71FFu, 0x05D1CDFFu, 0xC471BDFFu, 0x94436EFFu, 0xC1F7ECFFu, 0xCE79EEFFu,
            0xBD1EF2FFu, 0x93B7E4FFu, 0x3214AAFFu, 0x184D3BFFu, 0xAE4B99FFu, 0x7E49D7FFu, 0x4C436EFFu, 0xFA24CCFFu, 0xCE76BEFFu, 0xA04E0AFFu, 0x9F945CFFu, 0xDCDE3DFFu, 0x10C9C5FFu,
            0x70524DFFu, 0x0BE472FFu, 0x8A2CD7FFu, 0x6152C2FFu, 0xCF72A9FFu, 0xE59338FFu, 0xEEDC2DFFu, 0xD8C762FFu, 0x3FE65CFFu,
        };

        std::array<std::uint32_t, COUNT> g_colors{};
    }

    void SetColor(std::uint32_t id, std::uint32_t color)
    {
        if (id < g_colors.size())
            g_colors[id] = color;
    }

    std::uint32_t GetColor(std::uint32_t id)
    {
        ZoneScoped;
        switch (id)
        {
            case 1004:
                return 0x89A4C440u;
            case 1005:
                return 0xAA0000FFu;
            case 1006:
                return 0xE2C063FFu;
            default:
                break;
        }

        return id < g_colors.size() ? g_colors[id] : 0;
    }

    void Initialize()
    {
        for (std::size_t i = 0; i < g_colors.size(); ++i)
            g_colors[i] = kDefaultColors[i % kDefaultColors.size()];
    }
}
