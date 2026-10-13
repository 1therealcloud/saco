/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "main.h"

#include "FileSystem.h"
#include "archive/saa.h"

#include <memory>

namespace filesystem
{
    namespace
    {
        // r5 virtual handles live in this range
        constexpr std::uintptr_t kFirstVirtualHandle = 0xFF000001u;
        constexpr std::uintptr_t kVirtualHandleLimit = 0xFF000101u;
        constexpr std::size_t kMaxOpenFiles = 50;

        struct OpenFile
        {
            const std::uint8_t* data = nullptr;
            std::shared_ptr<const std::vector<std::uint8_t>> owner;
            std::uint32_t size = 0;
            std::uint64_t position = 0;
        };

        enum class OpenResult
        {
            NotFound,
            Opened,
            Failed
        };

        using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
        using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
        using SetFilePointerFn = DWORD(WINAPI*)(HANDLE, LONG, PLONG, DWORD);
        using GetFileSizeFn = DWORD(WINAPI*)(HANDLE, LPDWORD);
        using GetFileTypeFn = DWORD(WINAPI*)(HANDLE);
        using CloseHandleFn = BOOL(WINAPI*)(HANDLE);

        std::unordered_map<std::uintptr_t, OpenFile> g_files;
        std::mutex g_mutex;
        std::uintptr_t g_nextHandle = kFirstVirtualHandle;
        std::vector<LPVOID> g_hookTargets;
        bool g_installed = false;

        CreateFileAFn g_createFileA = nullptr;
        ReadFileFn g_readFile = nullptr;
        SetFilePointerFn g_setFilePointer = nullptr;
        GetFileSizeFn g_getFileSize = nullptr;
        GetFileTypeFn g_getFileType = nullptr;
        CloseHandleFn g_closeHandle = nullptr;

        std::uintptr_t HandleValue(HANDLE handle)
        {
            return reinterpret_cast<std::uintptr_t>(handle);
        }

        bool LooksVirtual(HANDLE handle)
        {
            const auto value = HandleValue(handle);
            return value >= kFirstVirtualHandle && value < kVirtualHandleLimit;
        }

        std::string_view FileNameOnly(std::string_view path)
        {
            while (!path.empty() && path.back() == ' ')
                path.remove_suffix(1);

            const auto pos = path.find_last_of("\\/");
            return pos == std::string_view::npos ? path : path.substr(pos + 1);
        }

        HANDLE AllocateHandleLocked()
        {
            ZoneScoped;
            // r5 keeps 50 open archive files
            if (g_files.size() >= kMaxOpenFiles)
            {
                SetLastError(ERROR_TOO_MANY_OPEN_FILES);
                return INVALID_HANDLE_VALUE;
            }

            const auto start = g_nextHandle;
            do
            {
                const auto value = g_nextHandle;
                g_nextHandle += 2;
                if (g_nextHandle >= kVirtualHandleLimit)
                    g_nextHandle = kFirstVirtualHandle;

                if (g_files.find(value) == g_files.end())
                    return reinterpret_cast<HANDLE>(value);
            }
            while (g_nextHandle != start);

            SetLastError(ERROR_TOO_MANY_OPEN_FILES);
            return INVALID_HANDLE_VALUE;
        }

        OpenResult OpenA(LPCSTR path, HANDLE& handle)
        {
            ZoneScoped;
            handle = INVALID_HANDLE_VALUE;
            if (!path)
                return OpenResult::NotFound;

            const auto name = FileNameOnly(std::string_view(path));
            if (name.empty())
                return OpenResult::NotFound;

            std::scoped_lock lock(g_mutex);
            if (!g_archive.IsOpen())
                return OpenResult::NotFound;

            std::uint32_t id{};
            if (!g_archive.FindFile(name, id))
                return OpenResult::NotFound;

            std::uint32_t size{};
            const std::uint8_t* data = nullptr;
            std::shared_ptr<const std::vector<std::uint8_t>> owner;
            if (!g_archive.FileSize(id, size) || !g_archive.FileData(id, data, &owner))
            {
                SetLastError(ERROR_FILE_CORRUPT);
                return OpenResult::Failed;
            }

            handle = AllocateHandleLocked();
            if (handle == INVALID_HANDLE_VALUE)
                return OpenResult::Failed;

            g_files.emplace(HandleValue(handle), OpenFile{data, std::move(owner), size, 0});
            return OpenResult::Opened;
        }

        HANDLE WINAPI HookCreateFileA(LPCSTR fileName, DWORD desiredAccess, DWORD shareMode, LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
                                      DWORD flagsAndAttributes, HANDLE templateFile)
        {
            ZoneScoped;
            // archive files are read-only; writes and creation belong to the real filesystem
            constexpr DWORD writeAccess =
                GENERIC_WRITE | GENERIC_ALL | MAXIMUM_ALLOWED | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE | WRITE_DAC | WRITE_OWNER;
            if (creationDisposition != OPEN_EXISTING || (desiredAccess & writeAccess) || (flagsAndAttributes & FILE_FLAG_DELETE_ON_CLOSE))
                return g_createFileA(fileName, desiredAccess, shareMode, securityAttributes, creationDisposition, flagsAndAttributes, templateFile);

            // try samp.saa first, then the real file
            HANDLE handle{};
            const auto result = OpenA(fileName, handle);
            if (result == OpenResult::Opened)
                return handle;
            if (result == OpenResult::Failed)
                return INVALID_HANDLE_VALUE;

            return g_createFileA(fileName, desiredAccess, shareMode, securityAttributes, creationDisposition, flagsAndAttributes, templateFile);
        }

        BOOL WINAPI HookReadFile(HANDLE file, LPVOID buffer, DWORD bytesToRead, LPDWORD bytesRead, LPOVERLAPPED overlapped)
        {
            ZoneScoped;
            if (!LooksVirtual(file))
                return g_readFile(file, buffer, bytesToRead, bytesRead, overlapped);

            std::scoped_lock lock(g_mutex);
            const auto it = g_files.find(HandleValue(file));
            if (it == g_files.end())
                return g_readFile(file, buffer, bytesToRead, bytesRead, overlapped);

            if (bytesRead)
                *bytesRead = 0;
            if ((!buffer && bytesToRead) || (!bytesRead && !overlapped))
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }

            auto& openFile = it->second;
            const std::uint64_t position = overlapped ? (static_cast<std::uint64_t>(overlapped->OffsetHigh) << 32) | overlapped->Offset : openFile.position;
            DWORD count = 0;
            if (position < openFile.size)
            {
                count = static_cast<DWORD>(std::min<std::uint64_t>(bytesToRead, openFile.size - position));
                if (count)
                    std::memcpy(buffer, openFile.data + static_cast<std::size_t>(position), count);
            }

            // keep r5's shared position after reads with an explicit offset
            openFile.position = position + count;
            if (bytesRead)
                *bytesRead = count;
            if (overlapped)
            {
                overlapped->Internal = 0;
                overlapped->InternalHigh = count;
                const auto event = reinterpret_cast<HANDLE>(reinterpret_cast<std::uintptr_t>(overlapped->hEvent) & ~std::uintptr_t{1});
                if (event)
                    SetEvent(event);
            }

            // preserve r5's successful eof reads
            if (bytesToRead && (position >= openFile.size || count == openFile.size - position))
                SetLastError(ERROR_HANDLE_EOF);
            return TRUE;
        }

        DWORD WINAPI HookGetFileSize(HANDLE file, LPDWORD fileSizeHigh)
        {
            ZoneScoped;
            if (!LooksVirtual(file))
                return g_getFileSize(file, fileSizeHigh);

            std::scoped_lock lock(g_mutex);
            const auto it = g_files.find(HandleValue(file));
            if (it == g_files.end())
                return g_getFileSize(file, fileSizeHigh);

            if (fileSizeHigh)
                *fileSizeHigh = 0;

            return it->second.size;
        }

        DWORD WINAPI HookSetFilePointer(HANDLE file, LONG distanceLow, PLONG distanceHigh, DWORD moveMethod)
        {
            ZoneScoped;
            if (!LooksVirtual(file))
                return g_setFilePointer(file, distanceLow, distanceHigh, moveMethod);

            std::scoped_lock lock(g_mutex);
            const auto it = g_files.find(HandleValue(file));
            if (it == g_files.end())
                return g_setFilePointer(file, distanceLow, distanceHigh, moveMethod);

            auto& openFile = it->second;
            std::int64_t base = 0;
            switch (moveMethod)
            {
                case FILE_BEGIN:
                    break;
                case FILE_CURRENT:
                    if (openFile.position > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                    {
                        SetLastError(ERROR_INVALID_PARAMETER);
                        return INVALID_SET_FILE_POINTER;
                    }
                    base = static_cast<std::int64_t>(openFile.position);
                    break;
                case FILE_END:
                    base = openFile.size;
                    break;
                default:
                    SetLastError(ERROR_INVALID_PARAMETER);
                    return INVALID_SET_FILE_POINTER;
            }

            LARGE_INTEGER distance{};
            if (distanceHigh)
            {
                distance.LowPart = static_cast<DWORD>(distanceLow);
                distance.HighPart = *distanceHigh;
            }
            else
                distance.QuadPart = distanceLow;

            if (distance.QuadPart < -base)
            {
                SetLastError(ERROR_NEGATIVE_SEEK);
                return INVALID_SET_FILE_POINTER;
            }
            if (distance.QuadPart > std::numeric_limits<std::int64_t>::max() - base)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return INVALID_SET_FILE_POINTER;
            }

            const auto position = static_cast<std::uint64_t>(base + distance.QuadPart);
            if (!distanceHigh && position > std::numeric_limits<DWORD>::max())
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return INVALID_SET_FILE_POINTER;
            }
            openFile.position = position;
            if (distanceHigh)
                *distanceHigh = static_cast<LONG>(position >> 32);
            SetLastError(NO_ERROR);
            return static_cast<DWORD>(position);
        }

        BOOL WINAPI HookCloseHandle(HANDLE file)
        {
            ZoneScoped;
            if (!LooksVirtual(file))
                return g_closeHandle(file);

            std::scoped_lock lock(g_mutex);
            const auto it = g_files.find(HandleValue(file));
            if (it == g_files.end())
                return g_closeHandle(file);

            g_files.erase(it);
            return TRUE;
        }

        DWORD WINAPI HookGetFileType(HANDLE file)
        {
            ZoneScoped;
            if (!LooksVirtual(file))
                return g_getFileType(file);

            std::scoped_lock lock(g_mutex);
            if (g_files.find(HandleValue(file)) == g_files.end())
                return g_getFileType(file);

            return FILE_TYPE_DISK;
        }

        template <typename T>
        bool CreateKernelHook(HMODULE kernel32, const char* name, LPVOID detour, T& original)
        {
            ZoneScoped;
            const auto target = reinterpret_cast<LPVOID>(GetProcAddress(kernel32, name));
            if (!target)
            {
#if defined(_DEBUG)
                Log("[filesystem] can't find %s", name);
#endif // _DEBUG
                return false;
            }

            LPVOID trampoline = nullptr;
            const auto status = MH_CreateHook(target, detour, &trampoline);
            if (status != MH_OK)
            {
#if defined(_DEBUG)
                Log("[filesystem] can't hook %s, status=%d", name, static_cast<int>(status));
#endif // _DEBUG
                return false;
            }

            original = reinterpret_cast<T>(trampoline);
            g_hookTargets.push_back(target);
            return true;
        }

        void RemoveHooks()
        {
            ZoneScoped;
            for (auto it = g_hookTargets.rbegin(); it != g_hookTargets.rend(); ++it)
                MH_DisableHook(*it);

            for (auto it = g_hookTargets.rbegin(); it != g_hookTargets.rend(); ++it)
                MH_RemoveHook(*it);

            g_hookTargets.clear();
        }
    }

    bool install()
    {
        ZoneScoped;
        if (g_installed)
            return true;

        if (!g_archive.IsOpen())
        {
#if defined(_DEBUG)
            Log("[filesystem] archive isn't open");
#endif // _DEBUG
            return false;
        }

        const auto kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (!kernel32)
        {
#if defined(_DEBUG)
            Log("[filesystem] can't get kernel32.dll");
#endif // _DEBUG
            return false;
        }

        g_nextHandle = kFirstVirtualHandle;

        // same winapi hook set as r5
        if (!CreateKernelHook(kernel32, "CreateFileA", reinterpret_cast<LPVOID>(&HookCreateFileA), g_createFileA) ||
            !CreateKernelHook(kernel32, "ReadFile", reinterpret_cast<LPVOID>(&HookReadFile), g_readFile) ||
            !CreateKernelHook(kernel32, "GetFileSize", reinterpret_cast<LPVOID>(&HookGetFileSize), g_getFileSize) ||
            !CreateKernelHook(kernel32, "SetFilePointer", reinterpret_cast<LPVOID>(&HookSetFilePointer), g_setFilePointer) ||
            !CreateKernelHook(kernel32, "CloseHandle", reinterpret_cast<LPVOID>(&HookCloseHandle), g_closeHandle) ||
            !CreateKernelHook(kernel32, "GetFileType", reinterpret_cast<LPVOID>(&HookGetFileType), g_getFileType))
        {
            RemoveHooks();
            return false;
        }

        for (const auto target : g_hookTargets)
        {
            const auto status = MH_QueueEnableHook(target);
            if (status != MH_OK)
            {
#if defined(_DEBUG)
                Log("[filesystem] can't queue hook, status=%d", static_cast<int>(status));
#endif // _DEBUG
                RemoveHooks();
                return false;
            }
        }

        const auto status = MH_ApplyQueued();
        if (status != MH_OK)
        {
#if defined(_DEBUG)
            Log("[filesystem] can't enable hooks, status=%d", static_cast<int>(status));
#endif // _DEBUG
            RemoveHooks();
            return false;
        }

        g_installed = true;
#if defined(_DEBUG)
        Log("[filesystem] installed");
#endif // _DEBUG
        return true;
    }
}
