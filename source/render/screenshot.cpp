#include <stdafx.h>

#include "screenshot.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

#include <chrono>
#include <future>
#include <memory>
#include <system_error>
#include <new>

namespace
{
    volatile LONG g_pending = 0;
    std::future<screenshot::Result> g_save;

    struct SurfaceRelease
    {
        void operator()(IDirect3DSurface9* surface) const
        {
            if (surface)
                surface->Release();
        }
    };
    using Surface = std::unique_ptr<IDirect3DSurface9, SurfaceRelease>;

    struct File
    {
        HANDLE handle = INVALID_HANDLE_VALUE;
        bool ok = true;
        ~File()
        {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
        }
    };

    void WritePng(void* context, void* data, int size)
    {
        auto& file = *static_cast<File*>(context);
        auto* bytes = static_cast<const BYTE*>(data);
        while (file.ok && size > 0)
        {
            DWORD written = 0;
            file.ok = WriteFile(file.handle, bytes, static_cast<DWORD>(size), &written, nullptr) && written != 0;
            if (file.ok)
            {
                bytes += written;
                size -= static_cast<int>(written);
            }
        }
    }
    bool GetPixelSize(D3DFORMAT format, std::size_t& size)
    {
        switch (format)
        {
            case D3DFMT_R8G8B8:
                size = 3;
                return true;
            case D3DFMT_A8R8G8B8:
            case D3DFMT_X8R8G8B8:
            case D3DFMT_A8B8G8R8:
            case D3DFMT_X8B8G8R8:
            case D3DFMT_A2R10G10B10:
            case D3DFMT_A2B10G10R10:
                size = 4;
                return true;
            case D3DFMT_R5G6B5:
            case D3DFMT_X1R5G5B5:
            case D3DFMT_A1R5G5B5:
            case D3DFMT_A4R4G4B4:
            case D3DFMT_X4R4G4B4:
                size = 2;
                return true;
            default:
                return false;
        }
    }

    std::uint8_t Expand5(std::uint32_t value)
    {
        return static_cast<std::uint8_t>((value << 3) | (value >> 2));
    }

    std::uint8_t Expand6(std::uint32_t value)
    {
        return static_cast<std::uint8_t>((value << 2) | (value >> 4));
    }

    std::uint8_t Expand4(std::uint32_t value)
    {
        return static_cast<std::uint8_t>((value << 4) | value);
    }

    bool ConvertToRgba(const D3DLOCKED_RECT& locked, const D3DSURFACE_DESC& description, std::vector<std::uint8_t>& rgba)
    {
        std::size_t pixelSize = 0;
        if (!GetPixelSize(description.Format, pixelSize) || locked.Pitch <= 0 || !locked.pBits)
            return false;

        const std::size_t width = description.Width;
        const std::size_t height = description.Height;
        if (width == 0 || height == 0 || width > std::numeric_limits<std::size_t>::max() / 4 / height)
        {
            return false;
        }
        if (width * pixelSize > static_cast<std::size_t>(locked.Pitch))
            return false;

        rgba.resize(width * height * 4);
        const auto* const source = static_cast<const std::uint8_t*>(locked.pBits);
        for (std::size_t y = 0; y != height; ++y)
        {
            const std::uint8_t* input = source + y * locked.Pitch;
            std::uint8_t* output = rgba.data() + y * width * 4;
            for (std::size_t x = 0; x != width; ++x)
            {
                switch (description.Format)
                {
                    case D3DFMT_R8G8B8:
                        output[0] = input[2];
                        output[1] = input[1];
                        output[2] = input[0];
                        output[3] = 0xFF;
                        break;
                    case D3DFMT_A8R8G8B8:
                        output[0] = input[2];
                        output[1] = input[1];
                        output[2] = input[0];
                        output[3] = input[3];
                        break;
                    case D3DFMT_X8R8G8B8:
                        output[0] = input[2];
                        output[1] = input[1];
                        output[2] = input[0];
                        output[3] = 0xFF;
                        break;
                    case D3DFMT_A8B8G8R8:
                        output[0] = input[0];
                        output[1] = input[1];
                        output[2] = input[2];
                        output[3] = input[3];
                        break;
                    case D3DFMT_X8B8G8R8:
                        output[0] = input[0];
                        output[1] = input[1];
                        output[2] = input[2];
                        output[3] = 0xFF;
                        break;
                    case D3DFMT_A2R10G10B10:
                    {
                        const std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(input);
                        output[0] = static_cast<std::uint8_t>((value >> 22) & 0xFF);
                        output[1] = static_cast<std::uint8_t>((value >> 12) & 0xFF);
                        output[2] = static_cast<std::uint8_t>((value >> 2) & 0xFF);
                        output[3] = static_cast<std::uint8_t>(((value >> 30) & 3) * 85);
                        break;
                    }
                    case D3DFMT_A2B10G10R10:
                    {
                        const std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(input);
                        output[0] = static_cast<std::uint8_t>((value >> 2) & 0xFF);
                        output[1] = static_cast<std::uint8_t>((value >> 12) & 0xFF);
                        output[2] = static_cast<std::uint8_t>((value >> 22) & 0xFF);
                        output[3] = static_cast<std::uint8_t>(((value >> 30) & 3) * 85);
                        break;
                    }
                    case D3DFMT_R5G6B5:
                    {
                        const std::uint16_t value = *reinterpret_cast<const std::uint16_t*>(input);
                        output[0] = Expand5((value >> 11) & 31);
                        output[1] = Expand6((value >> 5) & 63);
                        output[2] = Expand5(value & 31);
                        output[3] = 0xFF;
                        break;
                    }
                    case D3DFMT_X1R5G5B5:
                    case D3DFMT_A1R5G5B5:
                    {
                        const std::uint16_t value = *reinterpret_cast<const std::uint16_t*>(input);
                        output[0] = Expand5((value >> 10) & 31);
                        output[1] = Expand5((value >> 5) & 31);
                        output[2] = Expand5(value & 31);
                        output[3] = description.Format == D3DFMT_A1R5G5B5 ? ((value & 0x8000) != 0 ? 0xFF : 0x00) : 0xFF;
                        break;
                    }
                    case D3DFMT_A4R4G4B4:
                    case D3DFMT_X4R4G4B4:
                    {
                        const std::uint16_t value = *reinterpret_cast<const std::uint16_t*>(input);
                        output[0] = Expand4((value >> 8) & 15);
                        output[1] = Expand4((value >> 4) & 15);
                        output[2] = Expand4(value & 15);
                        output[3] = description.Format == D3DFMT_A4R4G4B4 ? Expand4((value >> 12) & 15) : 0xFF;
                        break;
                    }
                    default:
                        return false;
                }

                // The render target already contains the final alpha-blended
                // RGB result. Its alpha channel is render-state residue (most
                // visibly from dialogs and textdraws), not screenshot opacity.
                output[3] = 0xFF;
                input += pixelSize;
                output += 4;
            }
        }
        return true;
    }

    bool Capture(IDirect3DDevice9* device, std::vector<std::uint8_t>& rgba, UINT& width, UINT& height)
    {
        if (!device)
            return false;
        IDirect3DSurface9* raw = nullptr;
        if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &raw)))
            return false;
        Surface back(raw);
        D3DSURFACE_DESC description{};
        if (!back || FAILED(back->GetDesc(&description)))
            return false;
        // bound temporary memory use and keep the PNG stride in range
        if (!description.Width || !description.Height || description.Width > 16384 || description.Height > 16384 ||
            static_cast<std::uint64_t>(description.Width) * description.Height * 4 > 256ull * 1024 * 1024)
            return false;
        Surface resolved;
        auto* source = back.get();
        if (description.MultiSampleType != D3DMULTISAMPLE_NONE)
        {
            raw = nullptr;
            const HRESULT result = device->CreateRenderTarget(description.Width, description.Height, description.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &raw, nullptr);
            resolved.reset(raw);
            if (FAILED(result) || !resolved || FAILED(device->StretchRect(back.get(), nullptr, resolved.get(), nullptr, D3DTEXF_NONE)))
                return false;
            source = resolved.get();
        }
        raw = nullptr;
        const HRESULT result = device->CreateOffscreenPlainSurface(description.Width, description.Height, description.Format, D3DPOOL_SYSTEMMEM, &raw, nullptr);
        Surface readback(raw);
        if (FAILED(result) || !readback || FAILED(device->GetRenderTargetData(source, readback.get())))
            return false;
        D3DLOCKED_RECT locked{};
        if (FAILED(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
            return false;
        bool converted = false;
        try
        {
            converted = ConvertToRgba(locked, description, rgba);
        }
        catch (const std::bad_alloc&)
        {
            converted = false;
        }
        const HRESULT unlocked = readback->UnlockRect();
        width = description.Width;
        height = description.Height;
        return converted && SUCCEEDED(unlocked);
    }

    screenshot::Result SavePng(std::vector<std::uint8_t> rgba, UINT width, UINT height, const std::string& userDirectory)
    {
        screenshot::Result result;
        result.requested = true;
        stbi_write_png_compression_level = 8;
        // screenshots don't need alpha; compact the buffer in the PNG worker
        const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            rgba[pixel * 3] = rgba[pixel * 4];
            rgba[pixel * 3 + 1] = rgba[pixel * 4 + 1];
            rgba[pixel * 3 + 2] = rgba[pixel * 4 + 2];
        }
        rgba.resize(pixelCount * 3);
        std::string directory = userDirectory;
        if (directory.back() != '\\' && directory.back() != '/')
            directory += '\\';
        directory += "screens";
        if (!CreateDirectoryA(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return result;

        for (int index = 0; index < 1000; ++index)
        {
            char name[32]{};
            sprintf_s(name, "\\sa-mp-%03i.png", index);
            const std::string path = directory + name;
            File file;
            // reserve the name without overwriting existing screenshots
            file.handle = CreateFileA(path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file.handle == INVALID_HANDLE_VALUE)
            {
                if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS)
                    continue;
                Log("[screenshot] create failed, error=%lu", GetLastError());
                return result;
            }
            const bool encoded = stbi_write_png_to_func(WritePng, &file, static_cast<int>(width), static_cast<int>(height), 3, rgba.data(), static_cast<int>(width * 3)) != 0;
            result.saved = encoded && file.ok;
            if (!result.saved)
            {
                FILE_DISPOSITION_INFO disposition{TRUE};
                SetFileInformationByHandle(file.handle, FileDispositionInfo, &disposition, sizeof(disposition));
                Log("[screenshot] PNG write failed: %s", path.c_str());
            }
            else
            {
                result.index = index;
                Log("[screenshot] saved: %s", path.c_str());
            }
            return result;
        }
        Log("[screenshot] all 1000 screenshot names are occupied");
        return result;
    }
}

namespace screenshot
{
    void Request()
    {
        InterlockedExchange(&g_pending, 1);
    }

    Result ProcessPendingCapture(IDirect3DDevice9* device, const std::string& userDirectory)
    {
        if (g_save.valid())
        {
            if (g_save.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return {};
            try
            {
                return g_save.get();
            }
            catch (const std::exception& error)
            {
                Log("[screenshot] PNG worker failed: %s", error.what());
                return {true, false, -1};
            }
        }
        Result result;
        if (!InterlockedExchange(&g_pending, 0))
            return result;
        result.requested = true;
        if (userDirectory.empty())
            return result;
        std::vector<std::uint8_t> rgba;
        UINT width = 0, height = 0;
        if (!Capture(device, rgba, width, height))
        {
            Log("[screenshot] capture failed");
            return result;
        }
        try
        {
            g_save = std::async(std::launch::async, SavePng, std::move(rgba), width, height, userDirectory);
            return {};
        }
        catch (const std::system_error&)
        {
            Log("[screenshot] failed to start PNG worker");
            return result;
        }
        catch (const std::bad_alloc&)
        {
            Log("[screenshot] failed to allocate PNG job");
            return result;
        }
    }
}
