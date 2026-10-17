/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "CActor.h"
#include "CPed.h"
#include "main.h"
#include "net/CNetGame.h"

#include "game/Scripting.h"
#include "game/ModelInfo.h"
#include "game/render/models.h"

namespace
{
    constexpr scm::OpcodeInfo REQUEST_MODEL{0x0247, "i"};
    constexpr scm::OpcodeInfo IS_MODEL_AVAILABLE{0x0248, "i"};

    constexpr scm::OpcodeInfo CREATE_ACTOR{0x009A, "iifffv"};
    constexpr scm::OpcodeInfo SET_ACTOR_ROTATION{0x0173, "if"};
    constexpr scm::OpcodeInfo SET_ACTOR_PROPERTY_0446{0x0446, "ii"};
    constexpr scm::OpcodeInfo SET_ACTOR_DECISION_MAKER{0x060B, "ii"};
    constexpr scm::OpcodeInfo EXPLODE_ACTOR_HEAD{0x0321, "i"};
    constexpr scm::OpcodeInfo SET_ACTOR_IMMUNITIES{0x02AB, "iiiiii"};

    constexpr scm::OpcodeInfo REQUEST_ANIMATION{0x04ED, "s"};
    constexpr scm::OpcodeInfo IS_ANIMATION_LOADED{0x04EE, "s"};
    constexpr scm::OpcodeInfo PERFORM_ANIMATION{0x0812, "issfiiiii"};

    template <class T>
    T Read(const void* data, std::size_t offset)
    {
        T value;
        std::memcpy(&value, static_cast<const std::uint8_t*>(data) + offset, sizeof(value));
        return value;
    }

    template <class T>
    void Write(void* data, std::size_t offset, const T& value)
    {
        std::memcpy(static_cast<std::uint8_t*>(data) + offset, &value, sizeof(value));
    }

    void* PedFromHandle(std::uint32_t handle)
    {
        auto* pool = *reinterpret_cast<void**>(0xB74490);
        if (!pool || !handle || (handle >> 8) >= Read<std::uint32_t>(pool, 8))
            return nullptr;

        return reinterpret_cast<void*(__thiscall*)(void*, int)>(0x404910)(pool, static_cast<int>(handle));
    }

    bool IsCurrentPed(const CActor* actor)
    {
        return actor && actor->m_pGamePed && PedFromHandle(actor->m_handle) == actor->m_pGamePed && Read<std::uintptr_t>(actor->m_pGamePed, 0) != 0x863C40;
    }

    bool IsModelAvailable(int model)
    {
        if (ModelInfo::IsOwnedClone(model))
            return ModelInfo::IsReady(model);
        if (model < 0 || model >= ModelInfo::MAX_MODELS)
            return false;

        return scm::ProcessCommand(&IS_MODEL_AVAILABLE, model) != 0;
    }

    bool IsAnimationLoaded(const char* file)
    {
        return file && scm::ProcessCommand(&IS_ANIMATION_LOADED, file) != 0;
    }

    struct PendingAnimation
    {
        std::string name, library;
        void* ped;
        std::uint32_t handle, requested;
        float delta;
        int loop, lockX, lockY, freeze, time;
    };
    std::unordered_map<CActor*, PendingAnimation> g_pendingAnimations;

    float HeadingToRadians(float angle)
    {
        ZoneScoped;
        // samp.dll::0xB5970: only (0, 360) is accepted. Values above 180
        // are represented as the equivalent negative angle.
        if (!(angle > 0.0f && angle < 360.0f))
            return 0.0f;

        constexpr float PI = 3.1415927f;
        constexpr float INV_180 = 1.0f / 180.0f;

        if (angle <= 180.0f)
            return angle * PI * INV_180;

        return (angle - 360.0f) * PI * INV_180;
    }
}

void CActor::GetBonePosition(CVector& position, unsigned int bone, bool update) const
{
    CPed::GetNativeBonePosition(m_pGamePed, &position, bone, update);
}

CActor::CActor(int model, CVector position, float rotation, std::uint32_t custom)
{
    ZoneScoped;
    m_pGamePed = nullptr;
    m_pGameEntity = nullptr;
    m_handle = 0;
    m_bInvulnerable = false;

    if (model < 0 || (model >= ModelInfo::MAX_MODELS && !ModelInfo::IsOwnedClone(model)))
        return;

    m_pendingModel = model;
    if (g_DLClient && g_netGame && custom >= 20001 && custom <= 30000 && g_netGame->GetCustomModels().IsSkinModel(static_cast<int>(custom)))
        m_pendingCustomModel = static_cast<int>(custom);
    m_pendingPosition = position;
    m_pendingRotation = rotation;
    m_modelRequested = GetTickCount();
    if (!IsModelAvailable(model))
        scm::ProcessCommand(&REQUEST_MODEL, model);
}

void CActor::ProcessStreaming()
{
    ZoneScopedN("Deferred actor spawn");
    if (m_pendingModel < 0)
        return;
    if (m_pendingCustomModel && g_netGame && !g_netGame->GetCustomModels().IsCacheReady())
        m_modelRequested = GetTickCount();
    if (GetTickCount() - m_modelRequested >= 10000)
    {
#if defined(_DEBUG)
        Log("[actor] model load timed out: model=%d", m_pendingModel);
#endif // _DEBUG
        m_pendingModel = -1;
        g_pendingAnimations.erase(this);
        return;
    }
    if (!IsModelAvailable(m_pendingModel))
        return;
    int model = m_pendingModel;
    if (m_pendingCustomModel && g_netGame)
    {
        if (!g_netGame->GetCustomModels().LoadSkin(m_pendingCustomModel))
            return;
        model = m_pendingCustomModel;
        m_pendingCustomModel = 0;
    }
    const auto position = m_pendingPosition;
    const float rotation = m_pendingRotation;
    m_pendingModel = -1;
    std::uint32_t handle = 0;
    scm::ProcessCommand(&CREATE_ACTOR, 5, ModelInfo::GetBaseModel(model), static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z - 1.0f),
                        &handle);
    m_handle = handle;
    m_pGamePed = PedFromHandle(handle);
    m_pGameEntity = m_pGamePed;

    if (!IsCurrentPed(this))
    {
        m_handle = 0;
        m_pGamePed = nullptr;
        m_pGameEntity = nullptr;
        return;
    }

    if (ModelInfo::IsOwnedClone(model))
    {
        CEntity::SetModelIndex(model);
        if (GetModelIndex() != model)
        {
            Destroy();
            return;
        }
        if (ModelInfo::AddReference(model))
            m_pinnedCustomModel = model;
    }

    scm::ProcessCommand(&SET_ACTOR_ROTATION, handle, static_cast<double>(rotation));
    scm::ProcessCommand(&SET_ACTOR_PROPERTY_0446, handle, 0);
    scm::ProcessCommand(&SET_ACTOR_DECISION_MAKER, handle, 0x10006);
    SetInvulnerable(m_bInvulnerable);
    SetHealth(m_pendingHealth);
    if (auto it = g_pendingAnimations.find(this); it != g_pendingAnimations.end())
    {
        it->second.ped = m_pGamePed;
        it->second.handle = m_handle;
        it->second.requested = GetTickCount();
    }
}

CActor::~CActor()
{
    Destroy();
}

void CActor::Destroy()
{
    ZoneScoped;
    m_pendingModel = -1;
    g_pendingAnimations.erase(this);
    if (IsCurrentPed(this))
    {
        using DeletingDestructor = void(__thiscall*)(void*, unsigned int);
        const auto destructor = Read<DeletingDestructor>(Read<void*>(m_pGamePed, 0), 0);
        if (destructor)
            destructor(m_pGamePed, 1);
    }

    if (m_pinnedCustomModel)
    {
        ModelInfo::RemoveReference(m_pinnedCustomModel);
        game::render::models::unload_custom(m_pinnedCustomModel);
        m_pinnedCustomModel = 0;
    }
    m_handle = 0;
    m_pGamePed = nullptr;
    m_pGameEntity = nullptr;
}

void CActor::PerformAnimation(const char* animation, const char* file, float frameDelta, int lockA, int lockX, int lockY, int lockF, int time)
{
    ZoneScoped;
    if ((!IsCurrentPed(this) && !IsPending()) || !animation || !*animation || !file || !*file || !std::isfinite(frameDelta) || std::strlen(animation) >= 64 ||
        std::strlen(file) >= 64 || _stricmp(file, "SEX") == 0)
        return;
    if (!IsPending() && IsAnimationLoaded(file))
    {
        g_pendingAnimations.erase(this);
        scm::ProcessCommand(&PERFORM_ANIMATION, m_handle, animation, file, static_cast<double>(frameDelta), lockA, lockX, lockY, lockF, time);
        return;
    }
    const auto previous = g_pendingAnimations.find(this);
    const bool sameLibrary = previous != g_pendingAnimations.end() && previous->second.library == file;
    const auto requested = sameLibrary ? previous->second.requested : GetTickCount();
    g_pendingAnimations.insert_or_assign(this, PendingAnimation{animation, file, m_pGamePed, m_handle, requested, frameDelta, lockA, lockX, lockY, lockF, time});
    if (!sameLibrary && !IsAnimationLoaded(file))
        scm::ProcessCommand(&REQUEST_ANIMATION, file);
}

void CActor::ProcessAnimations()
{
    ZoneScoped;
    const auto now = GetTickCount();
    for (auto it = g_pendingAnimations.begin(); it != g_pendingAnimations.end();)
    {
        auto* actor = it->first;
        const auto& animation = it->second;
        if (actor->IsPending())
        {
            ++it;
            continue;
        }
        if (!IsCurrentPed(actor) || actor->m_pGamePed != animation.ped || actor->m_handle != animation.handle || now - animation.requested >= 5000 || actor->GetState() == 54 ||
            actor->GetState() == 55)
        {
            it = g_pendingAnimations.erase(it);
            continue;
        }
        if (!IsAnimationLoaded(animation.library.c_str()))
        {
            ++it;
            continue;
        }
        scm::ProcessCommand(&PERFORM_ANIMATION, actor->m_handle, animation.name.c_str(), animation.library.c_str(), static_cast<double>(animation.delta), animation.loop,
                            animation.lockX, animation.lockY, animation.freeze, animation.time);
        it = g_pendingAnimations.erase(it);
    }
}

void CActor::ClearAnimations()
{
    ZoneScoped;
    g_pendingAnimations.erase(this);
    if (!IsCurrentPed(this))
        return;

    // gta 1.0 us ped intelligence
    auto* intelligence = Read<void*>(m_pGamePed, 0x47C);
    if (intelligence)
        reinterpret_cast<void(__thiscall*)(void*, int)>(0x601640)(intelligence, 1);
}

void CActor::SetRotation(float angle)
{
    if (!std::isfinite(angle))
        return;
    m_pendingRotation = angle;
    if (IsCurrentPed(this))
        Write(m_pGamePed, 0x55C, HeadingToRadians(angle));
}

float CActor::GetHealth() const
{
    return IsCurrentPed(this) ? Read<float>(m_pGamePed, 0x540) : m_pendingHealth;
}

void CActor::SetHealth(float value)
{
    if (!std::isfinite(value))
        return;
    m_pendingHealth = value;
    if (!IsCurrentPed(this))
        return;

    Write(m_pGamePed, 0x540, value);
    if (Read<float>(m_pGamePed, 0x540) <= 0.0f)
        scm::ProcessCommand(&EXPLODE_ACTOR_HEAD, m_handle);
}

float CActor::GetArmour() const
{
    return IsCurrentPed(this) ? Read<float>(m_pGamePed, 0x548) : 0.0f;
}

void CActor::SetArmour(float value)
{
    if (IsCurrentPed(this))
        Write(m_pGamePed, 0x548, value);
}

int CActor::GetFlags() const
{
    return IsCurrentPed(this) ? Read<int>(m_pGamePed, 0x46C) : 0;
}

void CActor::SetFlags(int value)
{
    if (IsCurrentPed(this))
        Write(m_pGamePed, 0x46C, value);
}

BOOL CActor::IsDead() const
{
    return !IsCurrentPed(this) || Read<float>(m_pGamePed, 0x540) <= 0.0f;
}

char CActor::GetState() const
{
    // a failed constructor or a removed native ped has no state
    return IsCurrentPed(this) ? Read<char>(m_pGamePed, 0x530) : 0;
}

void CActor::SetState(char value)
{
    if (IsCurrentPed(this))
        Write(m_pGamePed, 0x530, static_cast<int>(static_cast<unsigned char>(value)));
}

BOOL CActor::IsOnGround() const
{
    if (!IsCurrentPed(this))
        return FALSE;

    return (Read<std::uint32_t>(m_pGamePed, 0x46C) >> 8) & 1;
}

void CActor::SetInvulnerable(bool enable)
{
    ZoneScoped;
    m_bInvulnerable = enable;
    if (!IsCurrentPed(this))
        return;

    if (enable)
        scm::ProcessCommand(&SET_ACTOR_IMMUNITIES, m_handle, 1, 1, 1, 1, 1);
    else
        scm::ProcessCommand(&SET_ACTOR_IMMUNITIES, m_handle, 0, 0, 0, 1, 0);
}

void CActor::Teleport(CVector position)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        return;
    m_pendingPosition = position;
    if (IsPending())
        m_pendingPosition.z += 1.0f;
    if (!IsPending())
        CEntity::Teleport(position);
}
