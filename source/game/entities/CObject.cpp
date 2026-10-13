/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "main.h"

#include "CObject.h"
#include "game/patch/hooks.h"
#include "net/CNetGame.h"
#include "pools/CObjectPool.h"
#include "pools/CVehiclePool.h"
#include "game/render/CObjectMaterialText.h"

#include "game/ModelInfo.h"
#include "game/render/models.h"
#include "game/CGame.h"
#include "game/Scripting.h"
#include "game/entities/CPed.h"
#include "game/render/textures.h"
#include <memory>

namespace
{
    constexpr scm::OpcodeInfo CREATE_OBJECT{0x0107, "ifffv"};
    constexpr scm::OpcodeInfo DELETE_OBJECT{0x0108, "i"};
    constexpr scm::OpcodeInfo SET_ROTATION{0x0453, "ifff"};
    constexpr scm::OpcodeInfo ATTACH_VEHICLE{0x0681, "iiffffff"};
    constexpr scm::OpcodeInfo ATTACH_OBJECT{0x069A, "iiffffff"};
    constexpr scm::OpcodeInfo ATTACH_PLAYER{0x069B, "iiffffff"};
    constexpr scm::OpcodeInfo IS_ATTACHED{0x0685, "i"};
    constexpr scm::OpcodeInfo DETACH_OBJECT{0x0682, "ifff"};
    constexpr float RADIANS = 0.017453292f;

    enum class AttachmentKind : std::uint8_t
    {
        None,
        Vehicle,
        Object,
        Player
    };

    struct MaterialGeometry
    {
        RpGeometry* source = nullptr;
        RpGeometry* geometry = nullptr;
        ~MaterialGeometry()
        {
            if (geometry)
                RpGeometryDestroy(geometry);
            if (source)
                RpGeometryDestroy(source);
        }
    };

    std::unique_ptr<MaterialGeometry> CloneMaterialGeometry(RpGeometry* source)
    {
        // Stream plugins as well as mesh data; do not copy RenderWare structs bytewise.
        RwMemory memory{};
        auto* output = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMWRITE, &memory);
        if (!output)
            return {};
        // Bind the existing textures after reading; texture streaming would depend
        // on whichever TXD is current during this object's render hook.
        std::vector<std::pair<RpMaterial*, RwTexture*>> textures;
        for (int i = 0; i < RpGeometryGetNumMaterials(source); ++i)
        {
            auto* material = RpGeometryGetMaterial(source, i);
            if (std::none_of(textures.begin(), textures.end(), [material](const auto& entry) {
                    return entry.first == material;
                }))
                textures.emplace_back(material, material->texture);
        }
        for (const auto& [material, texture] : textures)
            material->texture = nullptr;
        const bool written = RpGeometryStreamWrite(source, output) != nullptr;
        for (const auto& [material, texture] : textures)
            material->texture = texture;
        RwStreamClose(output, &memory);
        RpGeometry* clone = nullptr;
        if (written)
        {
            if (auto* input = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &memory))
            {
                if (RwStreamFindChunk(input, rwID_GEOMETRY, nullptr, nullptr))
                    clone = RpGeometryStreamRead(input);
                RwStreamClose(input, nullptr);
            }
        }
        if (memory.start)
            RwFree(memory.start);
        if (!clone)
            return {};
        if (RpGeometryGetNumMaterials(clone) != RpGeometryGetNumMaterials(source))
        {
            RpGeometryDestroy(clone);
            return {};
        }
        for (int i = 0; i < RpGeometryGetNumMaterials(source); ++i)
        {
            auto* original = RpGeometryGetMaterial(source, i);
            auto* material = RpGeometryGetMaterial(clone, i);
            RpMaterialSetTexture(material, original->texture);
            material->color = original->color;
            material->surfaceProps = original->surfaceProps;
        }
        clone->flags = (source->flags | rpGEOMETRYMODULATEMATERIALCOLOR) & ~rpGEOMETRYPRELIT;
        auto result = std::make_unique<MaterialGeometry>();
        ++source->refCount;
        result->source = source;
        result->geometry = clone;
        return result;
    }

    struct Runtime
    {
        CMatrix start = CMatrix::Unity();
        CQuaternion startRotation{}, targetRotation{}, currentRotation{};
        float distance = 0.0f;
        float targetHeading = 0.0f;
        float headingDelta = 0.0f;
        std::uint32_t movementStartMs = 0;
        std::uint32_t movementLastMs = 0;
        bool rotate = false;
        bool customLighting = false;
        bool poolManaged = false;
        int pinnedModel = 0;
        unsigned char hourOn = 0, hourOff = 0;
        unsigned int outsideFrames = 0;
        AttachmentKind attachment = AttachmentKind::None;
        void* attachedNative = nullptr;
        std::uint32_t attachedHandle = 0;
        CObject* attachedObject = nullptr;
        std::unordered_map<RpGeometry*, std::unique_ptr<MaterialGeometry>> materialGeometries;
    };
    float NormalizeHeadingR5(float degrees)
    {
        if (degrees > 180.0f)
            degrees -= 360.0f;
        if (degrees <= -180.0f)
            degrees += 360.0f;
        return degrees;
    }

    float HeadingDifferenceR5(float from, float to)
    {
        return NormalizeHeadingR5(NormalizeHeadingR5(to) - from);
    }

    std::uint32_t MotionMilliseconds()
    {
        ZoneScoped;
        static LARGE_INTEGER frequency{};
        if (!frequency.QuadPart)
            QueryPerformanceFrequency(&frequency);

        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        if (!frequency.QuadPart)
            return GetTickCount();

        const auto ticks = counter.QuadPart;
        return static_cast<std::uint32_t>((ticks / frequency.QuadPart) * 1000 + (ticks % frequency.QuadPart) * 1000 / frequency.QuadPart);
    }

    bool TimeVisible(const Runtime& runtime)
    {
        ZoneScoped;
        if (!runtime.hourOn && !runtime.hourOff)
            return true;

        const auto hour = *reinterpret_cast<const std::uint8_t*>(0xB70153);
        if (runtime.hourOn < runtime.hourOff)
            return hour >= runtime.hourOn && hour < runtime.hourOff;
        if (runtime.hourOn == runtime.hourOff)
            return false;
        return hour >= runtime.hourOn || hour < runtime.hourOff;
    }

    std::unordered_map<CObject*, Runtime> g_objects;
    std::unordered_map<void*, CObject*> g_nativeObjects;
    CObject* g_explicitRenderObject = nullptr;
    RwTexture* g_blankTexture = nullptr;

    RwTexture* GetBlankTexture()
    {
        ZoneScoped;
        if (g_blankTexture)
            return RwTextureAddRef(g_blankTexture);

        const int slot = game::render::textures::find_slot("blanktex");
        const int dictionary = slot >= 0 ? slot : game::render::textures::load_dictionary("blanktex", "SAMP\\blanktex.txd");
        if (dictionary >= 0)
            g_blankTexture = game::render::textures::read(dictionary, "blanktex").release();

        if (!g_blankTexture)
        {
            auto* raster = RwRasterCreate(2, 2, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
            if (raster)
            {
                if (auto* pixels = RwRasterLock(raster, 0, rwRASTERLOCKWRITE))
                {
                    for (int row = 0; row < raster->height; ++row)
                        std::memset(pixels + row * raster->stride, 0xFF, raster->width * 4);
                    RwRasterUnlock(raster);
                    g_blankTexture = RwTextureCreate(raster);
                }
                if (!g_blankTexture)
                    RwRasterDestroy(raster);
            }
        }

        return g_blankTexture ? RwTextureAddRef(g_blankTexture) : nullptr;
    }

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
    void OnStreamingReady(const CObject& object)
    {
        ZoneScoped;
        auto* native = object.GetNativeHandle();
        if (!native || Read<std::uint32_t>(native, 0) == 0x863C40u)
            return;

        if (auto* info = ModelInfo::GetRelocated(object.m_nModel))
        {
            const auto flags = Read<std::uint8_t>(info, 0x12);
            Write(info, 0x12, static_cast<std::uint8_t>(flags & ~1u));
        }
    }

    bool AtomicModel(void* info)
    {
        const auto table = Read<std::uintptr_t>(info, 0);
        return table == 0x85BBF0 || table == 0x85BC30 || table == 0x85BC70 || table == 0x85BCB0 || table == 0x85BCF0;
    }
    bool EnsureModelLoaded(int model, int attempts = 250)
    {
        ZoneScoped;
        auto* info = ModelInfo::GetRelocated(model);
        if (!info)
            return false;
        if (Read<void*>(info, 0x1C))
            return true;
        if (ModelInfo::IsOwnedClone(model))
            return false;
        reinterpret_cast<void(__cdecl*)(int, int)>(0x4087E0)(model, 2);
        for (int i = 0; i < attempts; ++i)
        {
            reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
            if (Read<void*>(info, 0x1C))
                return true;
            Sleep(2);
        }
        return false;
    }
    void* ObjectFromHandle(std::uint32_t handle)
    {
        auto* pool = *reinterpret_cast<void**>(0xB7449C);
        return pool && handle ? reinterpret_cast<void*(__thiscall*)(void*, std::uint32_t)>(0x465040)(pool, handle) : nullptr;
    }
    CMatrix Rotation(CVector rotation)
    {
        ZoneScoped;
        // r5 uses gta's Rz * Rx * Ry rotation order
        const float x = rotation.x * RADIANS, y = rotation.y * RADIANS, z = rotation.z * RADIANS;
        const float sx = std::sin(x), cx = std::cos(x), sy = std::sin(y), cy = std::cos(y);
        const float sz = std::sin(z), cz = std::cos(z);
        CMatrix matrix = CMatrix::Unity();
        matrix.GetRight() = CVector{cz * cy - sz * sx * sy, sz * cy + cz * sx * sy, -cx * sy};
        matrix.GetForward() = CVector{-sz * cx, cz * cx, sx};
        matrix.GetUp() = CVector{cz * sy + sz * sx * cy, sz * sy - cz * sx * cy, cx * cy};
        return matrix;
    }

    // geometry is shared between model instances; restore every change after this draw
    struct MaterialScope
    {
        struct MaterialState
        {
            RpMaterial* material;
            RwTexture* texture;
            RwRGBA color;
            RwSurfaceProperties surface;
        };
        struct GeometryState
        {
            RpGeometry* geometry;
            RpAtomic* atomic;
            RwUInt32 flags;
        };
        struct AtomicState
        {
            RpAtomic* atomic;
            RpGeometry* geometry;
            RwResEntry* instance;
        };
        std::vector<AtomicState> atomics;
        std::vector<MaterialState> materials;
        std::vector<GeometryState> geometries;
        CObject& object;

        explicit MaterialScope(CObject& value) : object(value)
        {
            ZoneScoped;
            auto* rw = object.GetRwObject();
            if (!rw || !object.m_bHasCustomMaterial)
                return;
            if (rw->type == rpATOMIC)
                Apply(reinterpret_cast<RpAtomic*>(rw));
            else if (rw->type == rpCLUMP)
                RpClumpForAllAtomics(
                    reinterpret_cast<RpClump*>(rw),
                    [](RpAtomic* atomic, void* data) {
                        static_cast<MaterialScope*>(data)->Apply(atomic);
                        return atomic;
                    },
                    this);
        }
        void Apply(RpAtomic* atomic)
        {
            ZoneScoped;
            auto* geometry = RpAtomicGetGeometry(atomic);
            if (!geometry)
                return;
            const int materialCount = std::min(RpGeometryGetNumMaterials(geometry), static_cast<int>(CObject::MAX_MATERIALS));
            bool textured = false;
            for (int i = 0; i < materialCount; ++i)
                textured |= object.m_material.m_nType[i] == CObject::MATERIAL_TYPE_TEXTURE;
            // Single-morph, non-native geometry keeps its instance on the geometry.
            if (textured && !atomic->repEntry && (geometry->flags & rpGEOMETRYPRELIT) && !(geometry->flags & rpGEOMETRYNATIVEFLAGSMASK) &&
                RpGeometryGetNumMorphTargets(geometry) == 1)
            {
                auto& cache = g_objects.at(&object).materialGeometries;
                auto found = cache.find(geometry);
                if (found == cache.end())
                {
                    auto clone = CloneMaterialGeometry(geometry);
                    if (clone)
                        found = cache.emplace(geometry, std::move(clone)).first;
                }
                if (found != cache.end())
                {
                    atomics.push_back({atomic, geometry, atomic->repEntry});
                    geometry = found->second->geometry;
                    atomic->geometry = geometry;
                    atomic->repEntry = nullptr;
                }
            }
            if (std::any_of(geometries.begin(), geometries.end(), [geometry](const auto& state) {
                    return state.geometry == geometry;
                }))
                return;
            geometries.push_back({geometry, atomic, geometry->flags});
            const int count = std::min(RpGeometryGetNumMaterials(geometry), static_cast<int>(CObject::MAX_MATERIALS));
            for (int i = 0; i < count; ++i)
            {
                if (object.m_material.m_nType[i] == CObject::MATERIAL_TYPE_NONE)
                    continue;
                auto* material = RpGeometryGetMaterial(geometry, i);
                if (!material)
                    continue;
                if (std::any_of(materials.begin(), materials.end(), [material](const auto& state) {
                        return state.material == material;
                    }))
                    continue;
                materials.push_back({material, material->texture, material->color, material->surfaceProps});
                if (object.m_material.m_pTextBackground[i])
                    material->texture = object.m_material.m_pTextBackground[i];
                const auto color = object.m_material.m_color[i];
                // transparent black is a valid SA-MP material color too
                if (object.m_material.m_nType[i] == CObject::MATERIAL_TYPE_TEXTURE)
                {
                    geometry->flags |= rpGEOMETRYMODULATEMATERIALCOLOR;
                    geometry->flags &= ~rpGEOMETRYPRELIT;
                    material->surfaceProps = {1.0f, 0.0f, 1.0f};
                    material->color = {static_cast<RwUInt8>(color), static_cast<RwUInt8>(color >> 8), static_cast<RwUInt8>(color >> 16), static_cast<RwUInt8>(color >> 24)};
                }
            }
            if ((geometry->flags ^ geometries.back().flags) & rpGEOMETRYPRELIT)
                InvalidateInstance(geometries.back());
        }
        static void InvalidateInstance(const GeometryState& state)
        {
            // PRELIT changes the D3D vertex layout; shared instances must rebuild it.
            if (state.geometry->repEntry)
                RwResourcesFreeResEntry(state.geometry->repEntry);
            if (state.atomic->repEntry)
                RwResourcesFreeResEntry(state.atomic->repEntry);
        }
        ~MaterialScope()
        {
            ZoneScoped;
            for (auto& state : materials)
            {
                state.material->texture = state.texture;
                state.material->color = state.color;
                state.material->surfaceProps = state.surface;
            }
            for (auto& state : geometries)
            {
                if ((state.geometry->flags ^ state.flags) & rpGEOMETRYPRELIT)
                    InvalidateInstance(state);
                state.geometry->flags = state.flags;
            }
            for (const auto& state : atomics)
            {
                if (state.atomic->repEntry)
                    RwResourcesFreeResEntry(state.atomic->repEntry);
                state.atomic->geometry = state.geometry;
                state.atomic->repEntry = state.instance;
            }
        }
    };
}

namespace game::objects::detail
{
    bool IgnoresCamera(void* entity)
    {
        const auto found = g_nativeObjects.find(entity);
        if (found != g_nativeObjects.end() && found->second && found->second->m_bDontCollideWithCamera)
            return true;
        return false;
    }
    void RenderNativeObject(void* entity, game::hooks::ObjectRenderFn draw)
    {
        ZoneScoped;
        const auto found = g_nativeObjects.find(entity);
        if (found != g_nativeObjects.end())
        {
            auto* object = found->second;
            const auto& runtime = g_objects.at(object);
            const bool nativeStreaming = runtime.poolManaged && !object->m_bHasCustomMaterial && object->m_fDrawDistance <= 300.0f && !runtime.hourOn && !runtime.hourOff;
            if (runtime.poolManaged && !nativeStreaming && g_explicitRenderObject != object)
                return;
            if (!nativeStreaming && object->m_fDrawDistance > 0.0f && object->GetDistanceToCamera() > object->m_fDrawDistance)
                return;
            if (!TimeVisible(runtime))
                return;
            MaterialScope materials(*object);
            const auto flags = Read<std::uint32_t>(entity, 0x1C);
            if (runtime.customLighting)
                Write(entity, 0x1C, flags | 0x10000000u);
            draw(entity);
            if (runtime.customLighting)
                Write(entity, 0x1C, (Read<std::uint32_t>(entity, 0x1C) & ~0x10000000u) | (flags & 0x10000000u));
            return;
        }
        draw(entity);
    }
}

CObject::CObject(int model, CVector position, CVector rotation, float drawDistance, int customLighting, char hourOn, char hourOff)
{
    ZoneScoped;
    auto& runtime = g_objects[this];
    runtime.customLighting = customLighting != 0;
    runtime.hourOn = static_cast<unsigned char>(hourOn);
    runtime.hourOff = static_cast<unsigned char>(hourOff);
    if (!ModelInfo::GetRelocated(model))
        model = 18631;
    auto* info = ModelInfo::GetRelocated(model);
    if (!info)
        return;
    if (!EnsureModelLoaded(model))
        return;
    m_nModel = model;
    m_bUsesModelDrawDistance = AtomicModel(info);
    if (m_bUsesModelDrawDistance && drawDistance > 0.0f && std::isfinite(drawDistance))
        Write(info, 0x18, drawDistance);
    m_fDrawDistance = m_bUsesModelDrawDistance ? Read<float>(info, 0x18) : drawDistance;
    m_bDrawLast = (Read<std::uint8_t>(info, 0x12) & 2) != 0;
    scm::ProcessCommand(&CREATE_OBJECT, ModelInfo::GetBaseModel(model), static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z),
                        &m_handle);
    m_pGameEntity = ObjectFromHandle(m_handle);
    if (!IsValid())
        return;
    if (ModelInfo::IsOwnedClone(model))
    {
        Remove();
        CEntity::SetModelIndex(model);
        if (GetModelIndex() != model)
        {
            scm::ProcessCommand(&DELETE_OBJECT, m_handle);
            m_handle = 0;
            m_pGameEntity = nullptr;
            return;
        }
    }
    if (ModelInfo::AddReference(model))
        runtime.pinnedModel = model;
    g_nativeObjects.emplace(m_pGameEntity, this);
    CMatrix matrix = CMatrix::Unity();
    GetMatrix(&matrix);
    matrix.GetPosition() = position;
    SetMatrix(matrix);
    SetRotation(&rotation);
    if (ModelInfo::IsOwnedClone(model))
        Add();
    Write(m_pGameEntity, 0x1C, Read<std::uint32_t>(m_pGameEntity, 0x1C) | 0x80000000u);
    if (auto* collision = Read<void*>(info, 0x14))
        m_fBoundingRadius = Read<float>(collision, 0x24);
    m_position = position;
    GetMatrix(&runtime.start);
    runtime.start.CopyToRwMatrix(&m_targetMatrix);
}
CObject::~CObject()
{
    ZoneScoped;
    g_nativeObjects.erase(m_pGameEntity);
    for (auto& [object, runtime] : g_objects)
        if (runtime.attachedObject == this)
            object->Detach();
    for (int i = 0; i < MAX_MATERIALS; ++i)
        ClearMaterial(i);
    if (m_handle && ObjectFromHandle(m_handle) == m_pGameEntity && IsValid())
    {
        DeleteRwObject();
        scm::ProcessCommand(&DELETE_OBJECT, m_handle);
        if (auto* info = ModelInfo::GetRelocated(m_nModel); info && !Read<std::uint16_t>(info, 8))
            if (g_game)
                g_game->ReleaseModel(m_nModel, false);
    }
    if (const auto it = g_objects.find(this); it != g_objects.end() && it->second.pinnedModel)
    {
        ModelInfo::RemoveReference(it->second.pinnedModel);
        game::render::models::unload_custom(it->second.pinnedModel);
    }
    m_pGameEntity = nullptr;
    m_handle = 0;
    g_objects.erase(this);
}
void CObject::Add()
{
    if (IsValid() && !DoesExist())
        reinterpret_cast<void(__cdecl*)(void*)>(0x563220)(m_pGameEntity);
}
void CObject::Remove()
{
    if (DoesExist())
        reinterpret_cast<void(__cdecl*)(void*)>(0x563280)(m_pGameEntity);
}
float CObject::GetDistance(const CMatrix* matrix) const
{
    return matrix ? (matrix->GetPosition() - CVector(m_targetMatrix.pos)).Magnitude() : 0.0f;
}
void CObject::Stop()
{
    SetSpeed({});
    SetTurnSpeed({});
    m_bMoving &= ~1;
}
void CObject::SetRotation(const CVector* rotation)
{
    if (!rotation || !IsValid())
        return;
    scm::ProcessCommand(&SET_ROTATION, m_handle, static_cast<double>(rotation->x), static_cast<double>(rotation->y), static_cast<double>(rotation->z));
    m_rotation = *rotation;
}
CVector CObject::GetRotation() const
{
    ZoneScoped;
    if (!IsValid())
        return m_rotation;
    CMatrix matrix = CMatrix::Unity();
    GetMatrix(&matrix);
    // invert the Rz * Rx * Ry convention used by gta and Rotation()
    const float x = std::asin(std::clamp(matrix.GetForward().z, -1.0f, 1.0f));
    float y = 0, z = 0;
    if (std::abs(std::cos(x)) > 0.00001f)
    {
        y = std::atan2(-matrix.GetRight().z, matrix.GetUp().z);
        z = std::atan2(-matrix.GetForward().x, matrix.GetForward().y);
    }
    else
        z = std::atan2(matrix.GetRight().y, matrix.GetRight().x);
    return {x / RADIANS, y / RADIANS, z / RADIANS};
}

void CObject::Rotate(CVector rotation)
{
    m_rotation = rotation;
    const auto matrix = Rotation(rotation);
    const auto position = m_targetMatrix.pos;
    matrix.CopyToRwMatrix(&m_targetMatrix);
    m_targetMatrix.pos = position;
}
void CObject::SetAttachedToVehicle(std::uint16_t id, const CVector* offset, const CVector* rotation)
{
    ZoneScoped;
    auto& runtime = g_objects.at(this);
    if (id == 0xFFFF)
    {
        // r5 only clears the stored fields, no gta detach
        m_nAttachedToVehicle = 0xFFFF;
        m_attachOffset = CVector{};
        m_attachRotation = CVector{};
        return;
    }
    if (!offset || !rotation)
        return;
    if (runtime.attachment != AttachmentKind::None || m_nAttachedToObject != 0xFFFF || m_nAttachedToVehicle != 0xFFFF)
        Detach();
    Stop();
    m_nAttachedToVehicle = id;
    m_nAttachedToObject = 0xFFFF;
    m_attachOffset = *offset;
    m_attachRotation = *rotation;
    runtime.attachment = AttachmentKind::Vehicle;
    runtime.attachedNative = nullptr;
    runtime.attachedHandle = 0;
    runtime.attachedObject = nullptr;
}
void CObject::SetAttachedToObject(std::uint16_t id, const CVector* offset, const CVector* rotation, char syncRotation)
{
    ZoneScoped;
    auto& runtime = g_objects.at(this);
    if (id == 0xFFFF)
    {
        // r5 only clears the stored fields, no gta detach
        m_nAttachedToObject = 0xFFFF;
        m_attachOffset = CVector{};
        m_attachRotation = CVector{};
        m_bSyncRotation = true;
        return;
    }
    if (!offset || !rotation)
        return;
    if (runtime.attachment != AttachmentKind::None || m_nAttachedToObject != 0xFFFF || m_nAttachedToVehicle != 0xFFFF)
        Detach();
    Stop();
    m_nAttachedToObject = id;
    m_nAttachedToVehicle = 0xFFFF;
    m_attachOffset = *offset;
    m_attachRotation = *rotation;
    m_bSyncRotation = syncRotation != 0;
    runtime.attachment = AttachmentKind::Object;
    runtime.attachedNative = nullptr;
    runtime.attachedHandle = 0;
    runtime.attachedObject = nullptr;
    CMatrix matrix = CMatrix::Unity();
    GetMatrix(&matrix);
    matrix.CopyToRwMatrix(&m_targetMatrix);
    runtime.start = matrix;
}
void CObject::AttachToVehicle(CEntity* vehicle)
{
    ZoneScoped;
    if (!IsValid() || !vehicle || !vehicle->DoesExist())
        return;

    auto& runtime = g_objects.at(this);
    const bool attached = scm::ProcessCommand(&IS_ATTACHED, m_handle) != 0;
    if (runtime.attachment != AttachmentKind::Vehicle || runtime.attachedNative != vehicle->GetNativeHandle() || runtime.attachedHandle != vehicle->m_handle || !attached)
    {
        if (attached)
            scm::ProcessCommand(&DETACH_OBJECT, m_handle, 0.0, 0.0, 0.0);
        scm::ProcessCommand(&ATTACH_VEHICLE, m_handle, vehicle->m_handle, static_cast<double>(m_attachOffset.x), static_cast<double>(m_attachOffset.y),
                            static_cast<double>(m_attachOffset.z), static_cast<double>(m_attachRotation.x), static_cast<double>(m_attachRotation.y),
                            static_cast<double>(m_attachRotation.z));
        runtime.attachment = AttachmentKind::Vehicle;
        runtime.attachedNative = vehicle->GetNativeHandle();
        runtime.attachedHandle = vehicle->m_handle;
        runtime.attachedObject = nullptr;
    }
}
void CObject::AttachToObject(CObject* object)
{
    ZoneScoped;
    if (!IsValid() || !object || object == this || !object->DoesExist())
        return;

    auto& runtime = g_objects.at(this);
    const bool attached = scm::ProcessCommand(&IS_ATTACHED, m_handle) != 0;
    if (runtime.attachment != AttachmentKind::Object || runtime.attachedNative != object->GetNativeHandle() || runtime.attachedHandle != object->m_handle || !attached)
    {
        if (attached)
            scm::ProcessCommand(&DETACH_OBJECT, m_handle, 0.0, 0.0, 0.0);
        scm::ProcessCommand(&ATTACH_OBJECT, m_handle, object->m_handle, static_cast<double>(m_attachOffset.x), static_cast<double>(m_attachOffset.y),
                            static_cast<double>(m_attachOffset.z), static_cast<double>(m_attachRotation.x), static_cast<double>(m_attachRotation.y),
                            static_cast<double>(m_attachRotation.z));
        runtime.attachment = AttachmentKind::Object;
        runtime.attachedNative = object->GetNativeHandle();
        runtime.attachedHandle = object->m_handle;
    }
    runtime.attachedObject = object;
    if (!m_bSyncRotation)
    {
        CMatrix matrix = runtime.start;
        matrix.RotateX(m_attachRotation.x * RADIANS, true);
        matrix.RotateY(m_attachRotation.y * RADIANS, true);
        matrix.RotateZ(m_attachRotation.z * RADIANS, true);
        CMatrix current = CMatrix::Unity();
        GetMatrix(&current);
        matrix.GetPosition() = current.GetPosition();
        SetMatrix(matrix);
        SetSpeed({});
        SetTurnSpeed({});
        UpdateRwFrame();
    }
}

void CObject::AttachToPlayer(CPed* ped, const CVector* offset, const CVector* rotation)
{
    ZoneScoped;
    if (!IsValid() || !ped || !ped->DoesExist() || !offset || !rotation)
        return;

    Detach();
    Stop();
    m_attachOffset = *offset;
    m_attachRotation = *rotation;
    auto& runtime = g_objects.at(this);
    scm::ProcessCommand(&ATTACH_PLAYER, m_handle, ped->m_handle, static_cast<double>(offset->x), static_cast<double>(offset->y), static_cast<double>(offset->z),
                        static_cast<double>(rotation->x), static_cast<double>(rotation->y), static_cast<double>(rotation->z));
    runtime.attachment = AttachmentKind::Player;
    runtime.attachedNative = ped->GetNativeHandle();
    runtime.attachedHandle = ped->m_handle;
    runtime.attachedObject = nullptr;
}
void CObject::SetCameraCollision(bool enable)
{
    m_bDontCollideWithCamera = !enable;
}

void CObject::Detach()
{
    ZoneScoped;
    if (IsValid() && scm::ProcessCommand(&IS_ATTACHED, m_handle))
        scm::ProcessCommand(&DETACH_OBJECT, m_handle, 0.0, 0.0, 0.0);
    m_nAttachedToObject = m_nAttachedToVehicle = 0xFFFF;
    auto& runtime = g_objects.at(this);
    runtime.attachment = AttachmentKind::None;
    runtime.attachedNative = nullptr;
    runtime.attachedHandle = 0;
    runtime.attachedObject = nullptr;
}
BOOL CObject::AttachedToMovingEntity() const
{
    ZoneScoped;
    if (m_nAttachedToObject != 0xFFFF)
    {
        auto* netGame = g_netGame.get();
        auto* objectPool = netGame ? netGame->GetObjectPool() : nullptr;
        auto* object = objectPool ? objectPool->Get(m_nAttachedToObject) : g_objects.at(const_cast<CObject*>(this)).attachedObject;
        return object && (object->m_bMoving & 1);
    }
    return m_nAttachedToVehicle != 0xFFFF || g_objects.at(const_cast<CObject*>(this)).attachment == AttachmentKind::Player || (m_bMoving & 1);
}
float CObject::MoveTo(CVector position, float speed, CVector rotation)
{
    ZoneScoped;
    if (!IsValid() || speed <= 0.0f || !std::isfinite(speed) || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        return 0.0f;

    auto& runtime = g_objects.at(this);
    if (m_bMoving & 1)
    {
        // r5 finishes the previous move before replacing its target
        CMatrix previous = CMatrix::Unity();
        GetMatrix(&previous);
        previous.GetPosition() = m_targetMatrix.pos;
        if (runtime.rotate)
        {
            previous.GetRight() = m_targetMatrix.right;
            previous.GetForward() = m_targetMatrix.up;
            previous.GetUp() = m_targetMatrix.at;
        }
        Stop();
        UpdateMatrixInWorld(previous);
        m_position = previous.GetPosition();
    }

    GetMatrix(&runtime.start);
    runtime.start.CopyToRwMatrix(&m_targetMatrix);
    m_targetMatrix.pos = position;
    m_fSpeed = speed;
    runtime.movementStartMs = MotionMilliseconds();
    runtime.movementLastMs = runtime.movementStartMs;

    runtime.rotate = rotation.x > -999.0f && rotation.y > -999.0f && rotation.z > -999.0f && std::isfinite(rotation.x) && std::isfinite(rotation.y) && std::isfinite(rotation.z);
    if (runtime.rotate)
    {
        runtime.startRotation.Set(runtime.start.ToRwMatrix());
        float currentX = 0.0f, currentY = 0.0f, currentZ = 0.0f;
        GetEulerInverted(&currentX, &currentY, &currentZ);
        runtime.targetHeading = NormalizeHeadingR5(rotation.z);
        runtime.headingDelta = HeadingDifferenceR5(currentZ, runtime.targetHeading);
        Rotate(rotation);
        runtime.targetRotation.Set(m_targetMatrix);
        runtime.startRotation.Normalise();
        runtime.targetRotation.Normalise();
        runtime.currentRotation = runtime.startRotation;
    }
    runtime.distance = (position - runtime.start.GetPosition()).Magnitude();
    m_bMoving |= 1;
    Write(m_pGameEntity, 0x40, Read<std::uint32_t>(m_pGameEntity, 0x40) & ~8u);
    return runtime.distance / speed;
}

void CObject::Process(float elapsedSeconds)
{
    ZoneScoped;
    if (!IsValid())
    {
        m_bReadyToRender = false;
        return;
    }

    auto& runtime = g_objects.at(this);
    m_fDistanceToCamera = GetDistanceToCamera();
    const bool timed = runtime.hourOn != 0 || runtime.hourOff != 0;
    const bool fullStreaming = m_bHasCustomMaterial || m_fDrawDistance > 300.0f || timed;
    const bool inRange = m_fDistanceToCamera <= m_fDrawDistance;
    auto* game = g_game.get();

    if (fullStreaming)
    {
        // r5 0xa9560: distance and time control rw lifetime together
        m_bReadyToRender = false;
        if (inRange && TimeVisible(runtime))
        {
            if (!GetRwObject() && game)
            {
                if (!game->IsModelAvailable(m_nModel))
                    game->RequestModel(m_nModel, 2);
                else
                {
                    SetModelIndex(m_nModel);
                }
            }
            if (GetRwObject())
            {
                m_bReadyToRender = true;
                runtime.outsideFrames = 0;
                m_nNoDrawFrames = 0;
                OnStreamingReady(*this);
            }
        }
        else if (GetRwObject())
        {
            ZoneScoped;
            ++runtime.outsideFrames;
            m_nNoDrawFrames = runtime.outsideFrames;
            if (runtime.outsideFrames >= 1000)
            {
                DeleteRwObject();
                runtime.outsideFrames = 0;
                m_nNoDrawFrames = 0;
                if (m_nModel > 0 && game)
                {
                    if (auto* info = ModelInfo::GetRelocated(m_nModel); info && !Read<std::uint16_t>(info, 8))
                        game->ReleaseModel(m_nModel, false);
                }
            }
        }
    }
    else
    {
        // r5 0xa9650: small objects use gta's renderer, not object pool drawing
        m_bReadyToRender = false;
        if (!GetRwObject() && game)
        {
            const auto* ped = game->GetPlayerPed();
            const auto* native = ped ? ped->GetNativePedHandle() : nullptr;
            const bool grounded = native && (Read<std::uint32_t>(native, 0x46C) & 0x100u);
            if (grounded && m_fDistanceToCamera < m_fDrawDistance - 30.0f)
            {
                if (!game->IsModelAvailable(m_nModel))
                    game->RequestModel(m_nModel, 2);
            }
        }
    }
    if (m_nAttachedToVehicle != 0xFFFF)
    {
        auto* netGame = g_netGame.get();
        auto* vehiclePool = netGame ? netGame->GetVehiclePool() : nullptr;
        if (vehiclePool)
            AttachToVehicle(vehiclePool->Get(m_nAttachedToVehicle));
        return;
    }
    if (m_nAttachedToObject != 0xFFFF)
    {
        auto* netGame = g_netGame.get();
        auto* objectPool = netGame ? netGame->GetObjectPool() : nullptr;
        if (objectPool)
            AttachToObject(objectPool->Get(m_nAttachedToObject));
        return;
    }
    if (!(m_bMoving & 1) || elapsedSeconds <= 0.0f || !std::isfinite(elapsedSeconds))
        return;

    CMatrix matrix = CMatrix::Unity();
    GetMatrix(&matrix);
    m_position = matrix.GetPosition();
    const CVector difference = CVector(m_targetMatrix.pos) - matrix.GetPosition();
    const float remaining = difference.Magnitude();
    const float step = elapsedSeconds * m_fSpeed;
    if (!std::isfinite(remaining) || !std::isfinite(step) || step <= 0.0f)
        return;

    if (remaining <= step)
    {
        matrix.GetPosition() = m_targetMatrix.pos;
        if (runtime.rotate)
        {
            runtime.currentRotation = runtime.targetRotation;
            matrix.GetRight() = m_targetMatrix.right;
            matrix.GetForward() = m_targetMatrix.up;
            matrix.GetUp() = m_targetMatrix.at;
        }
        UpdateMatrixInWorld(matrix);
        m_position = matrix.GetPosition();
        Stop();
        return;
    }

    const CVector displacement = difference * (step / remaining);
    CVector velocity = displacement * (0.02f / elapsedSeconds);

    // r5 0xa9801: signed milliseconds since moveto
    const std::uint32_t now = MotionMilliseconds();
    const float expectedDistance = static_cast<float>(static_cast<std::int32_t>(now - runtime.movementStartMs)) * 0.001f * m_fSpeed;
    runtime.movementLastMs = now;
    const float coveredDistance = runtime.distance - remaining;

    // r5 0xa99f5: correct velocity when distance drift exceeds 0.1
    const float distanceError = expectedDistance - coveredDistance;
    if (std::isfinite(distanceError) && std::abs(distanceError) > 0.1f)
    {
        if (expectedDistance > coveredDistance)
            velocity *= 1.0f + (expectedDistance - coveredDistance) * 0.1f;
        else if (coveredDistance > expectedDistance)
            velocity *= 1.0f - (coveredDistance - expectedDistance) * 0.1f;
    }
    SetSpeed(velocity);

    // r5 0x9fd60: apply native velocity using gta timestep
    const float gtaStep = *reinterpret_cast<const float*>(0xB7CB5C);
    CVector nativeSpeed{};
    GetSpeed(&nativeSpeed);
    if (std::isfinite(gtaStep))
        matrix.GetPosition() += nativeSpeed * gtaStep;
    UpdateMatrixInWorld(matrix);

    // r5 reads the native matrix again after UpdatePosition (0x9fd60)
    GetMatrix(&matrix);
    m_position = matrix.GetPosition();

    if (runtime.rotate)
    {
        const CVector position = matrix.GetPosition();

        // r5 0xa9aa3: turn speed uses move step and saved angle delta
        float currentX = 0.0f, currentY = 0.0f, currentZ = 0.0f;
        GetEulerInverted(&currentX, &currentY, &currentZ);
        const float nextHeading = runtime.targetHeading - step * runtime.headingDelta;
        const float headingError = HeadingDifferenceR5(currentZ, nextHeading);
        const float turnVelocity = std::clamp(headingError * 0.01f, -0.001f, 0.001f);
        SetTurnSpeed({0.0f, 0.0f, turnVelocity});

        // r5 0xa9980: interpolate using distance before this frame
        const float progress = runtime.distance > 0.0f ? (runtime.distance - remaining) / runtime.distance : 1.0f;
        runtime.currentRotation.Slerp(runtime.startRotation, runtime.targetRotation, progress);
        runtime.currentRotation.Normalise();
        RwMatrix orientation{};
        runtime.currentRotation.Get(&orientation);
        matrix.UpdateMatrix(&orientation);
        matrix.GetPosition() = position;
    }
    UpdateMatrixInWorld(matrix);
    m_position = matrix.GetPosition();
}
void CObject::ClearMaterial(int index)
{
    ZoneScoped;
    if (m_material.m_pTextBackground[index])
        RwTextureDestroy(m_material.m_pTextBackground[index]);
    m_material.m_pTextBackground[index] = nullptr;
    delete[] m_material.m_szText[index];
    m_material.m_szText[index] = nullptr;
    m_material.m_nType[index] = MATERIAL_TYPE_NONE;
    m_material.m_bTextureWasCreated[index] = FALSE;
    m_material.m_color[index] = 0;
}
void CObject::SetMaterial(int model, int index, const char* txd, const char* texture, D3DCOLOR color)
{
    ZoneScoped;
    if (index < 0 || index >= MAX_MATERIALS || !txd || !texture)
        return;

    // R5 checks the source-model lookup before discarding the old material.
    if (model > 0 && !ModelInfo::GetRelocated(model))
        return;

    ClearMaterial(index);
    RwTexture* replacement = nullptr;

    if (model > 0)
    {
        auto* info = ModelInfo::GetRelocated(model);
        if (EnsureModelLoaded(model))
        {
            replacement = game::render::textures::read(game::render::textures::find_slot(txd), texture).release();

            // R5 releases an unreferenced source model even if it was loaded
            // before SetMaterial(). Use the current reference count.
            if (!Read<std::uint16_t>(info, 8) && Read<void*>(info, 0x1C))
                if (g_game)
                    g_game->ReleaseModel(model, false);
        }
    }
    else if (model == 0)
    {
        replacement = GetBlankTexture();
    }

    // The color-only model (-1) intentionally has no replacement texture.
    // R5 also records the color/type after a texture lookup fails.
    m_material.m_pTextBackground[index] = replacement;
    m_material.m_color[index] = color;
    m_material.m_nType[index] = MATERIAL_TYPE_TEXTURE;
    m_bHasCustomMaterial = TRUE;
}

void CObject::SetMaterialText(int index, const char* text, char size, const char* font, char fontSize, bool bold, D3DCOLOR fontColor, D3DCOLOR backgroundColor, char align)
{
    ZoneScoped;
    int width, height;
    if (index < 0 || index >= MAX_MATERIALS || !text || !font || !GetMaterialSize(static_cast<std::uint8_t>(size), &width, &height))
        return;
    const auto length = std::strlen(text);
    auto copy = std::make_unique<char[]>(length + 1);
    std::memcpy(copy.get(), text, length + 1);
    ClearMaterial(index);
    auto& info = m_material.m_textInfo[index];
    info = {};
    info.m_nMaterialIndex = static_cast<char>(index);
    info.m_nMaterialSize = size;
    strncpy_s(info.m_szFont, font, _TRUNCATE);
    info.m_nFontSize = fontSize;
    info.m_bBold = bold;
    info.m_fontColor = fontColor;
    info.m_backgroundColor = backgroundColor;
    info.m_align = align;
    m_material.m_szText[index] = copy.release();
    m_material.m_nType[index] = MATERIAL_TYPE_TEXT;
    m_bHasCustomMaterial = TRUE;
}
bool CObject::GetMaterialSize(int size, int* width, int* height)
{
    ZoneScoped;
    constexpr int sizes[][2] = {{32, 32},  {64, 32},   {64, 64},   {128, 32}, {128, 64},  {128, 128}, {256, 32},
                                {256, 64}, {256, 128}, {256, 256}, {512, 64}, {512, 128}, {512, 256}, {512, 512}};
    if (!width || !height || size < 10 || size > 140 || size % 10)
        return false;
    *width = sizes[size / 10 - 1][0];
    *height = sizes[size / 10 - 1][1];
    return true;
}
void CObject::ConstructMaterialText()
{
    ZoneScoped;
    if (!g_objectMaterialText)
        return;
    for (int i = 0; i < MAX_MATERIALS; ++i)
    {
        if (m_material.m_nType[i] != MATERIAL_TYPE_TEXT || m_material.m_bTextureWasCreated[i] || !m_material.m_szText[i])
            continue;
        int width, height;
        if (!GetMaterialSize(static_cast<std::uint8_t>(m_material.m_textInfo[i].m_nMaterialSize), &width, &height))
            continue;
        const auto& info = m_material.m_textInfo[i];
        auto* texture = g_objectMaterialText->CreateRwTexture(m_material.m_szText[i], info.m_szFont, static_cast<unsigned char>(info.m_nFontSize), width, height, info.m_fontColor,
                                                              info.m_backgroundColor, info.m_bBold, info.m_align);
        if (!texture)
            continue;
        m_material.m_pTextBackground[i] = texture;
        m_material.m_bTextureWasCreated[i] = TRUE;
    }
}
void CObject::ShutdownMaterialText()
{
    ZoneScoped;
    for (int i = 0; i < MAX_MATERIALS; ++i)
        if (m_material.m_nType[i] == MATERIAL_TYPE_TEXT)
        {
            if (m_material.m_pTextBackground[i])
                RwTextureDestroy(m_material.m_pTextBackground[i]);
            m_material.m_pTextBackground[i] = nullptr;
            m_material.m_bTextureWasCreated[i] = FALSE;
        }
}
void CObject::Render()
{
    ZoneScoped;
    if (!IsValid() || !GetRwObject())
        return;
    auto* vtable = Read<void*>(m_pGameEntity, 0);
    Read<void(__thiscall*)(void*)>(vtable, 0x44)(m_pGameEntity);
    auto* previous = g_explicitRenderObject;
    g_explicitRenderObject = this;
    reinterpret_cast<game::hooks::ObjectRenderFn>(0x553260)(m_pGameEntity);
    g_explicitRenderObject = previous;
}
void CObject::Draw()
{
    ZoneScoped;
    if (m_fDrawDistance <= 0.0f || GetDistanceToCamera() <= m_fDrawDistance)
        Render();
}
void CObject::SetPoolManaged(bool managed)
{
    g_objects.at(this).poolManaged = managed;
}

void CObject::ProcessAll(float elapsedSeconds)
{
    ZoneScoped;
    for (auto& [object, runtime] : g_objects)
        if (!runtime.poolManaged)
            object->Process(elapsedSeconds);
}
void CObject::ConstructAllMaterialText()
{
    for (auto& [object, runtime] : g_objects)
        if (!runtime.poolManaged)
            object->ConstructMaterialText();
}
