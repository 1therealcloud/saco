/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "model_preview.h"

#include "game/ModelInfo.h"
#include "game/RenderWare/RenderWare.h"
#include <cmath>

namespace game::render::model_preview
{
    static void* RenderStateArgument(RwRenderState state, std::uintptr_t& value)
    {
        ZoneScoped;
        return state == rwRENDERSTATEFOGDENSITY ? static_cast<void*>(&value) : reinterpret_cast<void*>(value);
    }
    namespace
    {
        template <class T>
        T Read(const void* data, std::size_t offset)
        {
            T value;
            std::memcpy(&value, static_cast<const std::uint8_t*>(data) + offset, sizeof(value));
            return value;
        }

        struct Instance
        {
            void* info;
            RwObject* object = nullptr;
            bool referenced = false;
            explicit Instance(void* modelInfo) : info(modelInfo)
            {}
            ~Instance()
            {
                ZoneScoped;
                if (object && object->type == rpCLUMP)
                    RpClumpDestroy(reinterpret_cast<RpClump*>(object));
                else if (object && object->type == rpATOMIC)
                {
                    auto* atomic = reinterpret_cast<RpAtomic*>(object);
                    auto* frame = RpAtomicGetFrame(atomic);
                    RpAtomicDestroy(atomic);
                    if (frame)
                        RwFrameDestroyHierarchy(frame);
                }
                if (referenced)
                    reinterpret_cast<void(__thiscall*)(void*)>(0x4C4BB0)(info);
            }
        };

        struct DeviceState
        {
            IDirect3DDevice9* device;
            IDirect3DStateBlock9* block = nullptr;
            IDirect3DSurface9* target = nullptr;
            IDirect3DSurface9* depth = nullptr;
            D3DVIEWPORT9 viewport{};
            D3DMATRIX view{}, projection{};
            std::array<std::uintptr_t, 32> states{};
            std::array<bool, 32> present{};
            std::array<std::uint8_t, 8 * 24> textureStates{};
            std::array<std::uint32_t, 210> nativeStates{};
            std::array<std::array<std::uint32_t, 33>, 8> stageStates{};
            std::array<std::array<std::uint32_t, 14>, 8> samplerStates{};
            D3DMATERIAL9 material{};
            D3DMATRIX world{};
            bool valid = false;
            explicit DeviceState(IDirect3DDevice9* value) : device(value)
            {
                ZoneScoped;
                if (!device || FAILED(device->CreateStateBlock(D3DSBT_ALL, &block)) || FAILED(device->GetRenderTarget(0, &target)) || FAILED(device->GetViewport(&viewport)) ||
                    FAILED(device->GetTransform(D3DTS_VIEW, &view)) || FAILED(device->GetTransform(D3DTS_PROJECTION, &projection)))
                    return;
                device->GetDepthStencilSurface(&depth);
                device->GetMaterial(&material);
                device->GetTransform(D3DTS_WORLD, &world);
                for (std::size_t i = 0; i < nativeStates.size(); ++i)
                    RwD3D9GetRenderState(static_cast<RwUInt32>(i), &nativeStates[i]);
                for (RwUInt32 stage = 0; stage < 8; ++stage)
                {
                    for (RwUInt32 i = 0; i < 33; ++i)
                        RwD3D9GetTextureStageState(stage, i, &stageStates[stage][i]);
                    for (RwUInt32 i = 0; i < 14; ++i)
                        RwD3D9GetSamplerState(stage, i, &samplerStates[stage][i]);
                }
                std::memcpy(textureStates.data(), reinterpret_cast<void*>(0xC9A508), textureStates.size());
                for (int i = 1; i < 32; ++i)
                    present[i] = RwRenderStateGet(static_cast<RwRenderState>(i), &states[i]) != FALSE;
                valid = true;
            }
            void RestoreRenderWare()
            {
                ZoneScoped;
                // fog restoration reads the active camera's near/far planes
                // keep the preview camera updating until RW states have been restored
                for (int i = 1; i < 32; ++i)
                    if (present[i])
                        RwRenderStateSet(static_cast<RwRenderState>(i), RenderStateArgument(static_cast<RwRenderState>(i), states[i]));
            }
            ~DeviceState()
            {
                ZoneScoped;
                if (valid)
                {
                    device->SetRenderTarget(0, target);
                    *reinterpret_cast<IDirect3DSurface9**>(0xC98090) = target;
                    device->SetDepthStencilSurface(depth);
                    *reinterpret_cast<IDirect3DSurface9**>(0xC9808C) = depth;
                    for (std::size_t i = 0; i < nativeStates.size(); ++i)
                        RwD3D9SetRenderState(static_cast<RwUInt32>(i), nativeStates[i]);
                    for (RwUInt32 stage = 0; stage < 8; ++stage)
                    {
                        for (RwUInt32 i = 0; i < 33; ++i)
                            RwD3D9SetTextureStageState(stage, i, stageStates[stage][i]);
                        for (RwUInt32 i = 1; i < 14; ++i)
                            RwD3D9SetSamplerState(stage, i, samplerStates[stage][i]);
                    }
                    _rwD3D9RenderStateFlushCache();
                    RwD3D9SetMaterial(&material);
                    block->Apply();
                    device->SetViewport(&viewport);
                    // explicit calls also restore the matrices cached by our D3D hooks
                    device->SetTransform(D3DTS_VIEW, &view);
                    device->SetTransform(D3DTS_PROJECTION, &projection);
                    RwD3D9SetTransform(D3DTS_VIEW, &view);
                    RwD3D9SetTransform(D3DTS_PROJECTION, &projection);
                    RwD3D9SetTransform(D3DTS_WORLD, &world);
                    // Apply bypasses RW's binding caches; force the next pipeline to bind its own resources.
                    std::memset(reinterpret_cast<void*>(0x8E2440), 0xFF, 5 * sizeof(std::uint32_t));
                    for (int stream = 0; stream < 2; ++stream)
                        *reinterpret_cast<std::uint32_t*>(0xC97BD8 + stream * 16) = 0xFFFFFFFFu;
                    // RW also reads these cached raster pointers when saving render states.
                    std::memcpy(reinterpret_cast<void*>(0xC9A508), textureStates.data(), textureStates.size());
                }
                if (depth)
                    depth->Release();
                if (target)
                    target->Release();
                if (block)
                    block->Release();
            }
        };

        struct Scene
        {
            RwCamera* camera = nullptr;
            RwFrame* frame = nullptr;
            RwRaster* raster = nullptr;
            RwRaster* depth = nullptr;
            RpLight* light = nullptr;
            RpWorld* world = nullptr;
            bool cameraAdded = false, lightAdded = false, updating = false;
            ~Scene()
            {
                ZoneScoped;
                if (updating)
                    RwCameraEndUpdate(camera);
                if (cameraAdded)
                    RpWorldRemoveCamera(world, camera);
                if (lightAdded)
                    RpWorldRemoveLight(world, light);
                if (light)
                    RpLightDestroy(light);
                if (camera)
                {
                    RwCameraSetRaster(camera, nullptr);
                    RwCameraSetZRaster(camera, nullptr);
                    RwCameraSetFrame(camera, nullptr);
                    RwCameraDestroy(camera);
                }
                if (frame)
                    RwFrameDestroy(frame);
                if (depth)
                    RwRasterDestroy(depth);
                if (raster)
                    RwRasterDestroy(raster);
            }
        };

        struct VehicleColors
        {
            void* info = nullptr;
            std::array<std::uint8_t, 4> current{}, model{};
            void Set(void* vehicleInfo, int color1, int color2)
            {
                ZoneScoped;
                info = vehicleInfo;
                std::memcpy(current.data(), reinterpret_cast<void*>(0xB4E3F0), 4);
                std::memcpy(model.data(), static_cast<std::uint8_t*>(info) + 0x2D2, 4);
                std::array<std::uint8_t, 4> selected{};
                reinterpret_cast<void(__thiscall*)(void*, std::uint8_t&, std::uint8_t&, std::uint8_t&, std::uint8_t&, int)>(0x4C8500)(info, selected[0], selected[1], selected[2],
                                                                                                                                      selected[3], 1);
                if (color1 >= 0 && color1 <= 255)
                    selected[0] = static_cast<std::uint8_t>(color1);
                if (color2 >= 0 && color2 <= 255)
                    selected[1] = static_cast<std::uint8_t>(color2);
                reinterpret_cast<void(__thiscall*)(void*, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t)>(0x4C84B0)(info, selected[0], selected[1], selected[2],
                                                                                                                             selected[3]);
            }
            ~VehicleColors()
            {
                if (info)
                {
                    std::memcpy(reinterpret_cast<void*>(0xB4E3F0), current.data(), 4);
                    std::memcpy(static_cast<std::uint8_t*>(info) + 0x2D2, model.data(), 4);
                }
            }
        };

        RpAtomic* PrepareAtomic(RpAtomic* atomic, void*)
        {
            ZoneScoped;
            const auto callback = reinterpret_cast<std::uintptr_t>(RpAtomicGetRenderCallBack(atomic));
            if (callback == 0x7331E0 || callback == 0x732820 || callback == 0x7334F0)
            {
                RpAtomicSetFlags(atomic, RpAtomicGetFlags(atomic) & ~rpATOMICRENDER);
                return atomic;
            }
            // use the regular RenderWare callback instead of world/vehicle visibility callbacks
            RpAtomicSetRenderCallBack(atomic, nullptr);
            if (auto* hierarchy = RpSkinAtomicGetHAnimHierarchy(atomic))
                RpHAnimHierarchyUpdateMatrices(hierarchy);
            return atomic;
        }
    }

    textures::Texture create(int model, CVector rotation, float zoom, std::uint32_t background, int color1, int color2)
    {
        ZoneScoped;
        if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) || !std::isfinite(rotation.z) || !std::isfinite(zoom) || zoom <= 0 || zoom > 100 ||
            *reinterpret_cast<void**>(0xC9BCC0))
            return {};
        // R5 substitutes these problematic preview models
        if (model == 570)
            model = 538;
        else if (model == 569)
            model = 537;
        else if (model == 1373 || model == 3118 || model == 3552 || model == 3553)
            model = 18631;
        auto* info = ModelInfo::GetRelocated(model);
        auto* world = *reinterpret_cast<RpWorld**>(0xC17038);
        if (!info || !world)
            return {};
        DeviceState deviceState(*reinterpret_cast<IDirect3DDevice9**>(0xC97C28));
        if (!deviceState.valid)
            return {};
        if (!Read<void*>(info, 0x1C))
        {
            // request without persistent game/mission flags; the native streaming call is bounded
            reinterpret_cast<void(__cdecl*)(int, int)>(0x4087E0)(model, 0);
            reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
            if (!Read<void*>(info, 0x1C))
                return {};
        }

        Instance instance(info);
        reinterpret_cast<void(__thiscall*)(void*)>(0x4C4BA0)(info);
        instance.referenced = true;
        auto** vtable = *reinterpret_cast<void***>(info);
        // R5 +0xB6250 uses the no-argument CreateInstance slot at +0x2C
        instance.object = reinterpret_cast<RwObject*(__thiscall*)(void*)>(vtable[11])(info);
        if (!instance.object || (instance.object->type != rpATOMIC && instance.object->type != rpCLUMP))
            return {};
        auto* modelFrame = static_cast<RwFrame*>(instance.object->parent);
        if (!modelFrame)
            return {};

        CVector center{};
        float radius = 1;
        if (auto* collision = Read<void*>(info, 0x14))
        {
            center = Read<CVector>(collision, 0x18);
            radius = Read<float>(collision, 0x24);
        }
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z))
            center = CVector{};
        if (!std::isfinite(radius) || radius <= 0)
            radius = 1;
        const int type = reinterpret_cast<std::uint8_t(__thiscall*)(void*)>(vtable[4])(info);
        VehicleColors colors;
        if (type == 6)
            colors.Set(info, color1, color2);
        if (type == 7 && instance.object->type == rpCLUMP)
        {
            auto* clump = reinterpret_cast<RpClump*>(instance.object);
            reinterpret_cast<void(__cdecl*)(RpClump*)>(0x4D6720)(clump);
            reinterpret_cast<void*(__cdecl*)(RpClump*, int, int, float)>(0x4D4610)(clump, 0, 3, 1000.0f);
            reinterpret_cast<void(__cdecl*)(RpClump*, float, bool)>(0x4D34F0)(clump, 0.02f, true);
        }

        // R5's preview camera sits at z=50 and looks along -Y
        const RwV3d xAxis{1, 0, 0}, yAxis{0, 1, 0}, zAxis{0, 0, 1};
        const RwV3d cameraPosition{0, 0, 50};
        RwFrameRotate(modelFrame, &xAxis, rotation.x, rwCOMBINEREPLACE);
        RwFrameRotate(modelFrame, &yAxis, rotation.y, rwCOMBINEPRECONCAT);
        RwFrameRotate(modelFrame, &zAxis, rotation.z, rwCOMBINEPRECONCAT);
        const float distance = (0.1f + 2.25f * radius) * zoom;
        const RwV3d position{-center.x, -distance - center.y, 50 - center.z};
        RwFrameTranslate(modelFrame, &position, rwCOMBINEPOSTCONCAT);
        RwFrameUpdateObjects(modelFrame);
        if (instance.object->type == rpCLUMP)
            RpClumpForAllAtomics(reinterpret_cast<RpClump*>(instance.object), PrepareAtomic, nullptr);
        else
            PrepareAtomic(reinterpret_cast<RpAtomic*>(instance.object), nullptr);

        Scene scene;
        scene.world = world;
        scene.camera = RwCameraCreate();
        scene.frame = RwFrameCreate();
        scene.raster = RwRasterCreate(256, 256, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
        scene.depth = RwRasterCreate(256, 256, 0, rwRASTERTYPEZBUFFER);
        scene.light = RpLightCreate(rpLIGHTAMBIENT);
        if (!scene.camera || !scene.frame || !scene.raster || !scene.depth || !scene.light)
            return {};
        RwFrameRotate(scene.frame, &xAxis, 90, rwCOMBINEREPLACE);
        RwFrameTranslate(scene.frame, &cameraPosition, rwCOMBINEPOSTCONCAT);
        RwCameraSetFrame(scene.camera, scene.frame);
        RwCameraSetRaster(scene.camera, scene.raster);
        RwCameraSetZRaster(scene.camera, scene.depth);
        RwCameraSetProjection(scene.camera, rwPERSPECTIVE);
        RwCameraSetNearClipPlane(scene.camera, 0.01f);
        RwCameraSetFarClipPlane(scene.camera, std::max(300.0f, distance + radius * 2));
        const RwV2d view{0.5f, 0.5f};
        RwCameraSetViewWindow(scene.camera, &view);
        const RwRGBAReal white{1, 1, 1, 1};
        RpLightSetColor(scene.light, &white);
        if (!RpWorldAddCamera(world, scene.camera))
            return {};
        scene.cameraAdded = true;
        if (!RpWorldAddLight(world, scene.light))
            return {};
        scene.lightAdded = true;
        RwRGBA clear{};
        std::memcpy(&clear, &background, sizeof(clear));
        if (!RwCameraBeginUpdate(scene.camera))
            return {};
        scene.updating = true;
        // isolate the off-screen model pass from GUI blend, alpha-test and scissor states
        auto* device = deviceState.device;
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        if (!RwCameraClear(scene.camera, &clear, rwCAMERACLEARIMAGE | rwCAMERACLEARZ))
        {
            deviceState.RestoreRenderWare();
            return {};
        }
        device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
        device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
        device->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, reinterpret_cast<void*>(TRUE));
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, reinterpret_cast<void*>(TRUE));
        RwRenderStateSet(rwRENDERSTATESHADEMODE, reinterpret_cast<void*>(rwSHADEMODEGOURAUD));
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, nullptr);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, reinterpret_cast<void*>(rwBLENDSRCALPHA));
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, reinterpret_cast<void*>(rwBLENDINVSRCALPHA));
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, reinterpret_cast<void*>(rwALPHATESTFUNCTIONALWAYS));
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, nullptr);
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, nullptr);
        RwRenderStateSet(rwRENDERSTATECULLMODE, reinterpret_cast<void*>(rwCULLMODECULLNONE));
        const bool vehicle = type == 6 && instance.object->type == rpCLUMP;
        if (vehicle)
            reinterpret_cast<void(__cdecl*)(RpClump*)>(0x4C8430)(reinterpret_cast<RpClump*>(instance.object));
        if (instance.object->type == rpCLUMP)
            RpClumpRender(reinterpret_cast<RpClump*>(instance.object));
        else
            RpAtomicRender(reinterpret_cast<RpAtomic*>(instance.object));
        if (vehicle)
            reinterpret_cast<void(__cdecl*)(RpClump*)>(0x4C8460)(reinterpret_cast<RpClump*>(instance.object));
        deviceState.RestoreRenderWare();
        RwCameraEndUpdate(scene.camera);
        scene.updating = false;
        textures::Texture texture(RwTextureCreate(scene.raster));
        if (texture)
        {
            scene.raster = nullptr; // ownership transferred to RwTexture
            (void)RwTextureSetFilterMode(texture.get(), rwFILTERLINEAR);
            (void)RwTextureSetAddressing(texture.get(), rwTEXTUREADDRESSCLAMP);
        }
        return texture;
    }
}
