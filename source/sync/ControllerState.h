#pragma once

#include <cstddef>
#include <cstdint>

struct ControllerState
{
    std::int16_t m_sLeftStickX = 0; // left = -128, right = 128
    std::int16_t m_sLeftStickY = 0; // forward = -128, back = 128
    union
    {
        struct
        {
            std::uint16_t m_bLeftShoulder1 : 1;  // alt fire
            std::uint16_t m_bShockButtonL : 1;   // crouch
            std::uint16_t m_bButtonCircle : 1;   // fire
            std::uint16_t m_bButtonCross : 1;    // sprint or accelerate
            std::uint16_t m_bButtonTriangle : 1; // enter or exit vehicle
            std::uint16_t m_bButtonSquare : 1;   // jump or reverse
            std::uint16_t m_bRightShoulder2 : 1; // look right
            std::uint16_t m_bRightShoulder1 : 1; // aim or hand brake
            std::uint16_t m_bLeftShoulder2 : 1;  // look left
            std::uint16_t m_bShockButtonR : 1;   // look behind
            std::uint16_t m_bPedWalk : 1;        // walk
            std::uint16_t m_bRightStickDown : 1;
            std::uint16_t m_bRightStickUp : 1;
            std::uint16_t m_bRightStickRight : 1;
            std::uint16_t m_bRightStickLeft : 1;
            std::uint16_t : 1;
        };
        std::int16_t m_value = 0;
    };
};

static_assert(sizeof(ControllerState) == 6 && offsetof(ControllerState, m_value) == 4);
