#include <stdafx.h>

#include "main.h"

#include "game/CGame.h"

#include "CLabelPool.h"

#include "gui/CLabel.h"
#include "net/CNetGame.h"
#include "pools/CPlayerPool.h"
#include "players/CRemotePlayer.h"
#include "game/entities/CPed.h"
#include "game/entities/CVehicle.h"

#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace
{
    constexpr std::size_t kKeyReplacementLimit = 0x18F;
    constexpr std::size_t kR5KeyBufferSize = 0x805;

    // samp.dll + 0xB6100. R5 only expands GTA ~k~ control-key tokens when the
    // source is shorter than 400 bytes and the expanded result fits maxLength.
    std::size_t ExpandPlayerControlKeys(char* text, std::size_t maxLength)
    {
        if (!text)
            return 0;

        const std::size_t length = std::strlen(text);
        if (length >= 0x190 || !std::strstr(text, "~k~"))
            return 0;

        char buffer[kR5KeyBufferSize]{};
        std::memcpy(buffer, text, length + 1);

        CGame::ExpandControlKeys(buffer);

        const std::size_t expandedLength = std::strlen(buffer);
        if (expandedLength > maxLength)
            return 0;

        std::memcpy(text, buffer, expandedLength + 1);
        return expandedLength;
    }

    D3DCOLOR ToD3DColor(D3DCOLOR rgba)
    {
        // R5: rol/ror-equivalent conversion RRGGBBAA -> AARRGGBB.
        return (rgba << 24u) | (rgba >> 8u);
    }
}

CLabelPool::CLabelPool()
{
    std::memset(m_object, 0, sizeof(m_object));
    std::memset(m_bNotEmpty, 0, sizeof(m_bNotEmpty));
}

CLabelPool::~CLabelPool()
{
    for (ID id = 0; id < MAX_LABELS; ++id)
    {
        if (m_bNotEmpty[id])
            Delete(id);
    }
}

void CLabelPool::Create(ID id, const char* text, D3DCOLOR color, CVector position, float drawDistance, bool behindWalls, ID attachedToPlayer, ID attachedToVehicle)
{
    if (id >= MAX_LABELS || !text || !std::isfinite(drawDistance) || drawDistance < 0 || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        return;

    const std::size_t length = std::strlen(text);
    // key expansion can make the string longer than the source
    auto* newText = static_cast<char*>(std::calloc(std::max(length, kKeyReplacementLimit) + 1, 1));
    if (!newText)
        return;
    std::memcpy(newText, text, length + 1);
    if (length < 0x190)
        ExpandPlayerControlKeys(newText, kKeyReplacementLimit);
    Delete(id);
    auto& label = m_object[id];
    label.m_pText = newText;

    label.m_color = ToD3DColor(color);
    label.m_position = position;
    label.m_fDrawDistance = drawDistance;
    label.m_bBehindWalls = behindWalls;
    label.m_nAttachedToPlayer = attachedToPlayer;
    label.m_nAttachedToVehicle = attachedToVehicle;
    m_bNotEmpty[id] = TRUE;
}

BOOL CLabelPool::Delete(ID id)
{
    if (id >= MAX_LABELS || !m_bNotEmpty[id])
        return FALSE;

    auto& label = m_object[id];
    if (label.m_pText)
        std::free(label.m_pText);

    std::memset(&label, 0, sizeof(label));
    m_bNotEmpty[id] = FALSE;
    return TRUE;
}

void CLabelPool::Draw()
{
    if (!g_label)
        return;

    CVector localPlayerPosition{};
    const bool haveLocalPlayer = CPed::GetLocalPlayerPosition(localPlayerPosition);
    if (!haveLocalPlayer)
        return;

    g_label->Begin();

    for (ID id = 0; id < MAX_LABELS; ++id)
    {
        if (!m_bNotEmpty[id])
            continue;

        auto& label = m_object[id];
        if (!label.m_pText || !*label.m_pText)
            continue;

        const BOOL shadow = (label.m_color & 0xFF000000u) == 0xFF000000u;
        CVector worldPosition{};
        float distance = 0.0f;

        if (label.m_nAttachedToPlayer != INVALID_ID)
        {
            auto* netGame = g_netGame.get();
            auto* playerPool = netGame ? netGame->GetPlayerPool() : nullptr;
            CPed* ped = nullptr;
            if (playerPool && label.m_nAttachedToPlayer == playerPool->m_localInfo.m_nId)
                ped = playerPool->GetLocalPlayer() ? playerPool->GetLocalPlayer()->GetPed() : nullptr;
            else if (auto* player = playerPool ? playerPool->GetPlayer(label.m_nAttachedToPlayer) : nullptr)
                ped = player->m_pPed;
            if (!ped || !ped->DoesExist())
                continue;
            CMatrix matrix = CMatrix::Unity();
            ped->GetMatrix(&matrix);
            worldPosition = matrix.TransformPoint(label.m_position);
            distance = ped->GetDistanceToLocalPlayer();
            if (distance > label.m_fDrawDistance)
                continue;
        }
        else if (label.m_nAttachedToVehicle != INVALID_ID)
        {
            auto* netGame = g_netGame.get();
            auto* vehiclePool = netGame ? netGame->GetVehiclePool() : nullptr;
            auto* vehicle = vehiclePool ? vehiclePool->Get(label.m_nAttachedToVehicle) : nullptr;
            if (!vehicle || !vehicle->IsValid())
                continue;
            CMatrix matrix = CMatrix::Unity();
            vehicle->GetMatrix(&matrix);
            worldPosition = matrix.TransformPoint(label.m_position);
            distance = vehicle->GetDistanceToLocalPlayer();
            if (distance > label.m_fDrawDistance)
                continue;
        }
        else
        {
            worldPosition = label.m_position;
            distance = std::sqrt((worldPosition - localPlayerPosition).SquaredMagnitude());
            if (distance > label.m_fDrawDistance)
                continue;
        }

        g_label->Draw(&worldPosition, label.m_pText, label.m_color, shadow, label.m_bBehindWalls);
    }

    g_label->End();
}
