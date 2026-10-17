/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "models.h"
#include "textures.h"
#include "game/ModelInfo.h"
#include "game/patch/hooks.h"
#include <cstdio>
#include <vector>
#include <unordered_map>
#include <cmath>
#include "game/RenderWare/RenderWare.h"

namespace game::render::models
{
    void ClumpDeleter::operator()(RpClump* clump) const noexcept
    {
        if (clump)
            RpClumpDestroy(clump);
    }

    Model load(const char* path, int textureSlot)
    {
        ZoneScoped;
        if (!path || !*path)
            return {};
        const textures::DictionaryScope dictionary(textureSlot);
        if (!dictionary)
            return {};
        RwStream* stream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, path);
        if (!stream)
            return {};
        Model model;
        // rwID_CLUMP = 0x10; skip the file header before RpClumpStreamRead
        if (RwStreamFindChunk(stream, rwID_CLUMP, nullptr, nullptr))
            model.reset(RpClumpStreamRead(stream));
        RwStreamClose(stream, nullptr);
        return model;
    }

    Model clone(RpClump* model)
    {
        return Model(model ? RpClumpClone(model) : nullptr);
    }
}

namespace game::render::models
{
    namespace
    {
        struct FileStream
        {
            std::vector<std::uint8_t> bytes;
            RwStream* stream{};
            explicit FileStream(const std::filesystem::path& path)
            {
                FILE* file = nullptr;
                if (_wfopen_s(&file, path.c_str(), L"rb"))
                    return;
                const std::unique_ptr<FILE, decltype(&std::fclose)> input(file, &std::fclose);
                if (_fseeki64(file, 0, SEEK_END))
                    return;
                const auto length = _ftelli64(file);
                if (length <= 0 || length > 64 * 1024 * 1024 || _fseeki64(file, 0, SEEK_SET))
                    return;
                bytes.resize(static_cast<std::size_t>(length));
                if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
                    return;
                RwMemory memory{bytes.data(), static_cast<RwUInt32>(bytes.size())};
                stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &memory);
            }
            ~FileStream()
            {
                if (stream)
                    RwStreamClose(stream, nullptr);
            }
        };
        struct Dictionary
        {
            int slot{};
            unsigned models{};
        };
        struct CustomModel
        {
            int base{};
            std::filesystem::path dff, txd;
            bool skin{};
        };
        std::unordered_map<std::filesystem::path, Dictionary> g_customDictionaries;
        std::unordered_map<int, CustomModel> g_customModels;
        int g_loadingModel{};
        bool g_loadingSkin{};

        void CollectDictionaries()
        {
            for (auto it = g_customDictionaries.begin(); it != g_customDictionaries.end();)
            {
                if (it->second.models)
                {
                    ++it;
                    continue;
                }
                textures::remove_ref(it->second.slot);
                if (textures::remove_dictionary(it->second.slot))
                    it = g_customDictionaries.erase(it);
                else
                {
                    textures::add_ref(it->second.slot);
                    ++it;
                }
            }
        }
        struct LoadingModelScope
        {
            int previous;
            bool previousSkin;
            LoadingModelScope(int model, bool skin) : previous(g_loadingModel), previousSkin(g_loadingSkin)
            {
                g_loadingModel = model;
                g_loadingSkin = skin;
            }
            ~LoadingModelScope()
            {
                g_loadingModel = previous;
                g_loadingSkin = previousSkin;
            }
        };
        RpHAnimHierarchy* FindHierarchy(RwFrame* frame)
        {
            if (!frame)
                return nullptr;
            if (auto* hierarchy = RpHAnimFrameGetHierarchy(frame))
                return hierarchy;
            RpHAnimHierarchy* hierarchy = nullptr;
            RwFrameForAllChildren(
                frame,
                [](RwFrame* child, void* context) -> RwFrame* {
                    auto& result = *static_cast<RpHAnimHierarchy**>(context);
                    result = FindHierarchy(child);
                    return result ? nullptr : child;
                },
                &hierarchy);
            return hierarchy;
        }
        bool ValidSkin(RpClump* clump)
        {
            auto* hierarchy = FindHierarchy(RpClumpGetFrame(clump));
            if (!hierarchy || !hierarchy->pMatrixArray || hierarchy->numNodes <= 0 || hierarchy->numNodes > 256)
                return false;
            // Native hit collision creation indexes these skeleton bones.
            for (int bone : {1, 3, 5, 21, 31, 41, 51})
                if (RpHAnimIDGetIndex(hierarchy, bone) < 0)
                    return false;
            bool valid = true;
            int count = 0;
            struct Check
            {
                bool& valid;
                int& count;
                RpHAnimHierarchy* hierarchy;
            } check{valid, count, hierarchy};
            RpClumpForAllAtomics(
                clump,
                [](RpAtomic* atomic, void* context) -> RpAtomic* {
                    auto& state = *static_cast<Check*>(context);
                    ++state.count;
                    auto* geometry = RpAtomicGetGeometry(atomic);
                    auto* skin = geometry ? RpSkinGeometryGetSkin(geometry) : nullptr;
                    const auto bones = skin ? RpSkinGetNumBones(skin) : 0;
                    if (!geometry || RpGeometryGetNumMorphTargets(geometry) <= 0 || RpGeometryGetNumVertices(geometry) <= 0)
                        state.valid = false;
                    else if (!skin)
                        state.valid &= state.count != 1;
                    else if (!bones || bones > static_cast<unsigned>(state.hierarchy->numNodes))
                        state.valid = false;
                    else
                    {
                        const auto* weights = RpSkinGetVertexBoneWeights(skin);
                        const auto* indices = RpSkinGetVertexBoneIndices(skin);
                        if (!weights || !indices)
                            state.valid = false;
                        else
                            for (int v = 0; v < RpGeometryGetNumVertices(geometry) && state.valid; ++v)
                            {
                                const auto& weight = weights[v];
                                const float values[]{weight.w0, weight.w1, weight.w2, weight.w3};
                                float sum = 0;
                                for (unsigned i = 0; i < 4; ++i)
                                {
                                    state.valid &= std::isfinite(values[i]) && values[i] >= 0 && (values[i] == 0 || ((indices[v] >> (i * 8)) & 255) < bones);
                                    sum += values[i];
                                }
                                state.valid &= std::isfinite(sum) && sum > 0;
                            }
                    }
                    return state.valid ? atomic : nullptr;
                },
                &check);
            return valid && count > 0;
        }
        bool ValidObject(RpClump* clump, void* info)
        {
            struct Check
            {
                int normal{}, damaged{};
                bool valid{true}, damageModel{};
            } check;
            check.damageModel = *static_cast<const std::uintptr_t*>(info) == 0x85BC30;
            RpClumpForAllAtomics(
                clump,
                [](RpAtomic* atomic, void* context) -> RpAtomic* {
                    auto& state = *static_cast<Check*>(context);
                    auto* frame = RpAtomicGetFrame(atomic);
                    const char* name = frame ? reinterpret_cast<const char*(__cdecl*)(RwFrame*)>(0x72FB30)(frame) : nullptr;
                    if (!name || strnlen(name, 24) >= 24 || !RpAtomicGetGeometry(atomic) || RpSkinGeometryGetSkin(RpAtomicGetGeometry(atomic)))
                        state.valid = false;
                    else
                    {
                        char modelName[24]{};
                        bool damaged{};
                        reinterpret_cast<void(__cdecl*)(const char*, char*, bool*)>(0x5370A0)(name, modelName, &damaged);
                        if (damaged)
                            state.valid &= state.damageModel && ++state.damaged == 1;
                        else
                            state.valid &= ++state.normal == 1;
                    }
                    return state.valid ? atomic : nullptr;
                },
                &check);
            return check.valid && check.normal == 1;
        }
        template <class T>
        T Read(const std::uint8_t* data, std::size_t offset)
        {
            T value{};
            std::memcpy(&value, data + offset, sizeof(value));
            return value;
        }
        bool ValidCollision(const std::vector<std::uint8_t>& data)
        {
            if (data.size() < 120 || Read<std::uint32_t>(data.data(), 0) != 0x334C4F43)
                return false;
            const auto declaredSize = Read<std::uint32_t>(data.data(), 4);
            // DL compares the size field directly with the plugin payload size.
            if (declaredSize < 112 || declaredSize > data.size())
                return false;
            const std::uint64_t total = std::min<std::uint64_t>(static_cast<std::uint64_t>(declaredSize) + 8, data.size());
            for (std::size_t i = 32; i < 72; i += 4)
                if (!std::isfinite(Read<float>(data.data(), i)))
                    return false;
            if (Read<float>(data.data(), 68) < 0)
                return false;
            const auto range = [&](std::uint32_t offset, std::uint64_t size) {
                return size ? offset >= 116 && static_cast<std::uint64_t>(offset) + 4 + size <= total
                            : !offset || (offset >= 116 && static_cast<std::uint64_t>(offset) + 4 <= total);
            };
            if (!range(Read<std::uint32_t>(data.data(), 84), Read<std::uint16_t>(data.data(), 72) * 20u) ||
                !range(Read<std::uint32_t>(data.data(), 88), Read<std::uint16_t>(data.data(), 74) * 28u) || !range(Read<std::uint32_t>(data.data(), 92), data[78] * 32u))
                return false;
            const auto mesh = [&](std::uint32_t vertices, std::uint32_t faces, std::uint32_t count) {
                if (!range(faces, static_cast<std::uint64_t>(count) * 8))
                    return false;
                std::uint32_t maximum = 0;
                for (std::uint32_t i = 0; i < count; ++i)
                    for (std::uint32_t j = 0; j < 3; ++j)
                        maximum = std::max(maximum, static_cast<std::uint32_t>(Read<std::uint16_t>(data.data(), static_cast<std::size_t>(faces) + 4 + i * 8 + j * 2)));
                return range(vertices, count ? static_cast<std::uint64_t>(maximum + 1) * 6 : 0);
            };
            const auto faces = Read<std::uint32_t>(data.data(), 100);
            const auto count = Read<std::uint16_t>(data.data(), 76);
            if (!mesh(Read<std::uint32_t>(data.data(), 96), faces, count) ||
                !mesh(Read<std::uint32_t>(data.data(), 112), Read<std::uint32_t>(data.data(), 116), Read<std::uint32_t>(data.data(), 108)))
                return false;
            if (Read<std::uint32_t>(data.data(), 80) & 8)
            {
                if (!count || faces < 120)
                    return false;
                const auto groups = Read<std::uint32_t>(data.data(), faces);
                if (static_cast<std::uint64_t>(groups) * 28 + 120 > faces)
                    return false;
                for (std::uint32_t i = 0; i < groups; ++i)
                {
                    const auto group = faces - groups * 28 + i * 28;
                    const auto first = Read<std::uint16_t>(data.data(), group + 24), last = Read<std::uint16_t>(data.data(), group + 26);
                    if (first > last || last >= count)
                        return false;
                }
            }
            return true;
        }
    }
    bool loading_custom_model()
    {
        return g_loadingModel != 0;
    }
    RwStream* read_custom_collision(RwStream* stream, int length)
    {
        if (!stream || length < 0 || length > 0x40000)
            return nullptr;
        if (g_loadingSkin)
            return RwStreamSkip(stream, static_cast<RwUInt32>(length));
        std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
        if (RwStreamRead(stream, data.data(), static_cast<RwUInt32>(length)) != static_cast<RwUInt32>(length) || !ValidCollision(data))
        {
#if defined(_DEBUG)
            Log("[dl:native] invalid COL3: id=%d bytes=%d", g_loadingModel, length);
#endif // _DEBUG
            return nullptr;
        }
        // The native loader includes the eight-byte file header in its size.
        // Preserve DL's length convention without reading past the received chunk.
        data.resize(std::max<std::size_t>(data.size(), static_cast<std::size_t>(Read<std::uint32_t>(data.data(), 4)) + 8), 0);
        auto* collision = reinterpret_cast<void*(__cdecl*)(unsigned)>(0x40FC30)(0x30);
        if (!collision)
            return nullptr;
        reinterpret_cast<void(__thiscall*)(void*)>(0x40FB60)(collision);
        reinterpret_cast<void(__cdecl*)(void*, unsigned, void*, const char*)>(0x537CE0)(data.data() + 32, Read<std::uint32_t>(data.data(), 4) - 24, collision, nullptr);
        if (!ModelInfo::SetCollision(g_loadingModel, collision))
        {
            reinterpret_cast<void(__thiscall*)(void*)>(0x40F9E0)(collision);
            reinterpret_cast<void(__cdecl*)(void*)>(0x40FC40)(collision);
            return nullptr;
        }
        return stream;
    }
    bool unload_custom(int model)
    {
        const auto it = g_customModels.find(model);
        if (it == g_customModels.end())
            return false;
        if (!ModelInfo::DestroyClone(model))
            return false;
        const auto dictionary = g_customDictionaries.find(it->second.txd);
        if (dictionary != g_customDictionaries.end() && dictionary->second.models)
            --dictionary->second.models;
        g_customModels.erase(it);
        CollectDictionaries();
        return true;
    }
    void collect_custom_models()
    {
        std::vector<int> pending;
        for (const auto& [id, model] : g_customModels)
            if (!ModelInfo::GetReferences(id))
                pending.push_back(id);
        for (int id : pending)
            unload_custom(id);
        CollectDictionaries();
    }
    bool load_custom(int baseModel, int model, const std::filesystem::path& dff, const std::filesystem::path& txd, bool skin)
    {
        ZoneScoped;
        if (baseModel < 0 || baseModel >= ModelInfo::MAX_MODELS || !ModelInfo::Valid(model) || dff.empty() || txd.empty() ||
            (skin ? model < 20001 || model > 30000 || baseModel > 311 : model > -1000 || model < -30000))
            return false;
        if (const auto it = g_customModels.find(model); it != g_customModels.end())
        {
            if (it->second.base == baseModel && it->second.skin == skin && it->second.dff == dff && it->second.txd == txd && ModelInfo::IsReady(model))
            {
                return true;
            }
            if (!unload_custom(model))
                return false;
        }
        if (!game::hooks::RegisterCustomModelPlugins())
            return false;
        try
        {
            if (!ModelInfo::IsReady(baseModel))
            {
                reinterpret_cast<void(__cdecl*)(int, int)>(0x4087E0)(baseModel, 0);
            }
            if (!ModelInfo::IsReady(baseModel))
            {
#if defined(_DEBUG)
                Log("[dl:native] base unavailable: id=%d base=%d", model, baseModel);
#endif // _DEBUG
                return false;
            }
            auto* info = skin ? ModelInfo::ClonePed(baseModel, model) : ModelInfo::CloneAtomic(baseModel, model);
            if (!info)
            {
#if defined(_DEBUG)
                Log("[dl:native] clone failed: id=%d base=%d source=%p destination=%p", model, baseModel, ModelInfo::GetRelocated(baseModel), ModelInfo::GetRelocated(model));
#endif // _DEBUG
                return false;
            }
            if (!ModelInfo::PinBaseModel(model))
            {
                ModelInfo::DestroyClone(model);
                return false;
            }
            auto dictionary = g_customDictionaries.find(txd);
            if (dictionary == g_customDictionaries.end())
            {
                FileStream input(txd);
                static unsigned sequence{};
                char name[32]{};
                std::snprintf(name, sizeof(name), "dl_%X", ++sequence);
                const int slot = input.stream ? textures::load_dictionary(name, input.stream) : -1;
                if (slot < 0)
                {
#if defined(_DEBUG)
                    Log("[dl:native] TXD failed: id=%d stream=%p", model, input.stream);
#endif // _DEBUG
                    ModelInfo::DestroyClone(model);
                    return false;
                }
                textures::add_ref(slot);
                try
                {
                    dictionary = g_customDictionaries.emplace(txd, Dictionary{slot, 0}).first;
                }
                catch (...)
                {
                    textures::remove_ref(slot);
                    textures::remove_dictionary(slot);
                    throw;
                }
            }
            g_customModels.emplace(model, CustomModel{baseModel, dff, txd, skin});
            ++dictionary->second.models;
            bool loaded = false;
            if (ModelInfo::SetTextureSlot(model, dictionary->second.slot))
            {
                const textures::DictionaryScope scope(dictionary->second.slot);
                const LoadingModelScope loading(model, skin);
                FileStream input(dff);
                Model clump;
                if (scope && input.stream && RwStreamFindChunk(input.stream, rwID_CLUMP, nullptr, nullptr))
                    clump.reset(RpClumpStreamRead(input.stream));
                if (clump && skin && ValidSkin(clump.get()))
                {
                    if (ModelInfo::AttachPedClump(model, clump.get()))
                    {
                        clump.release();
                        loaded = true;
                    }
                }
                else if (clump && !skin && ValidObject(clump.get(), info))
                {
                    auto& atomicModel = *reinterpret_cast<int*>(0xB71840);
                    const int previous = atomicModel;
                    atomicModel = model;
                    RpClumpForAllAtomics(clump.get(), reinterpret_cast<RpAtomicCallBack>(0x537150), clump.get());
                    atomicModel = previous;
                    loaded = ModelInfo::IsReady(model);
                }
            }
#if defined(_DEBUG)
            if (!loaded)
                Log("[dl:native] model loading failed: id=%d skin=%d references=%d", model, skin, ModelInfo::GetReferences(model));
#endif // _DEBUG
            if (!loaded)
                unload_custom(model);
            return loaded;
        }
        catch (...)
        {
            if (g_customModels.contains(model))
                unload_custom(model);
            else
                ModelInfo::DestroyClone(model);
            CollectDictionaries();
            return false;
        }
    }
}
