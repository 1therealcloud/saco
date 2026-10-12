/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <cstddef>
#include <cstdint>

class CControllerState
{
public:
    std::int16_t LeftStickX;  // move/steer left (-128?)/right (+128)
    std::int16_t LeftStickY;  // move back(+128)/forwards(-128?)
    std::int16_t RightStickX; // numpad 6(+128)/numpad 4(-128?)
    std::int16_t RightStickY;

    std::int16_t LeftShoulder1;
    std::int16_t LeftShoulder2;
    std::int16_t RightShoulder1; // target / hand brake
    std::int16_t RightShoulder2;

    std::int16_t DPadUp;    // radio change up           Next radio station / Call gang forward/Recruit gang member
    std::int16_t DPadDown;  // radio change down       Previous radio station / Gang stay back/Release gang (hold)
    std::int16_t DPadLeft;  //                         Skip trip/Action / Negative talk reply
    std::int16_t DPadRight; //                        Next user MP3 track / Positive talk reply

    std::int16_t Start;  //Pause
    std::int16_t Select; //Camera modes

    std::int16_t ButtonSquare;   // jump / reverse      Break/Reverse / Jump/Climb
    std::int16_t ButtonTriangle; // get in/out        Exit vehicle / Enter veihcle
    std::int16_t ButtonCross;    // sprint / accelerate  Accelerate / Sprint/Swim
    std::int16_t ButtonCircle;   // fire                Fire weapon

    std::int16_t ShockButtonL;
    std::int16_t ShockButtonR; // look behind

    std::int16_t m_bChatIndicated;
    std::int16_t m_bPedWalk;
    std::int16_t m_bVehicleMouseLook;
    std::int16_t m_bRadioTrackSkip;
};
