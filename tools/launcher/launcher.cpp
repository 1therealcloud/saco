#include <stdafx.h>
#include <psapi.h>
#include <stop_token>
#include <thread>
#include "resource.h"

namespace samp_debug
{

    enum class launch_status
    {
        success,
        cancelled,
        failed
    };

    class unique_handle
    {
    public:
        unique_handle() noexcept = default;
        explicit unique_handle(HANDLE value) noexcept : value_(value)
        {}
        ~unique_handle()
        {
            reset();
        }

        unique_handle(const unique_handle&) = delete;
        unique_handle& operator=(const unique_handle&) = delete;

        unique_handle(unique_handle&& other) noexcept : value_(std::exchange(other.value_, nullptr))
        {}

        unique_handle& operator=(unique_handle&& other) noexcept
        {
            if (this != &other)
            {
                reset(std::exchange(other.value_, nullptr));
            }
            return *this;
        }

        HANDLE get() const noexcept
        {
            return value_;
        }
        explicit operator bool() const noexcept
        {
            return value_ && value_ != INVALID_HANDLE_VALUE;
        }

        void reset(HANDLE replacement = nullptr) noexcept
        {
            if (*this)
            {
                CloseHandle(value_);
            }
            value_ = replacement;
        }

    private:
        HANDLE value_ = nullptr;
    };

    class pending_process
    {
    public:
        explicit pending_process(const PROCESS_INFORMATION& info) noexcept : process_(info.hProcess), primary_thread_(info.hThread)
        {}

        ~pending_process()
        {
            // An incomplete injection must not leave a suspended gta_sa.exe behind.
            if (!committed_ && process_)
            {
                TerminateProcess(process_.get(), 1);
                WaitForSingleObject(process_.get(), 2000);
            }
        }

        pending_process(const pending_process&) = delete;
        pending_process& operator=(const pending_process&) = delete;

        HANDLE process() const noexcept
        {
            return process_.get();
        }
        HANDLE thread() const noexcept
        {
            return primary_thread_.get();
        }
        void commit() noexcept
        {
            committed_ = true;
        }

    private:
        unique_handle process_;
        unique_handle primary_thread_;
        bool committed_ = false;
    };

    class remote_memory
    {
    public:
        remote_memory(HANDLE process, SIZE_T size) noexcept : process_(process), address_(VirtualAllocEx(process, nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
        {}

        ~remote_memory()
        {
            if (address_)
            {
                VirtualFreeEx(process_, address_, 0, MEM_RELEASE);
            }
        }

        remote_memory(const remote_memory&) = delete;
        remote_memory& operator=(const remote_memory&) = delete;

        LPVOID get() const noexcept
        {
            return address_;
        }
        explicit operator bool() const noexcept
        {
            return address_ != nullptr;
        }

        // On timeout/cancellation the remote loader may still read this buffer.
        // The failing child is terminated by pending_process, which then reclaims it.
        void abandon() noexcept
        {
            address_ = nullptr;
        }

    private:
        HANDLE process_ = nullptr;
        LPVOID address_ = nullptr;
    };

    namespace
    {

        constexpr DWORD kLoadTimeoutMs = 30'000;
        constexpr DWORD kWaitSliceMs = 50;

        bool is_regular_file(const std::wstring& path) noexcept
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        }

        std::wstring basename(std::wstring path)
        {
            const auto separator = path.find_last_of(L"\\/");
            if (separator != std::wstring::npos)
                path.erase(0, separator + 1);
            return path;
        }

        std::wstring module_name(HMODULE module)
        {
            std::vector<wchar_t> buffer(MAX_PATH);
            for (;;)
            {
                const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (!length)
                    return {};
                if (length < buffer.size())
                {
                    return basename(std::wstring(buffer.data(), length));
                }
                if (buffer.size() >= 32768)
                    return {};
                buffer.resize(buffer.size() * 2);
            }
        }

        bool mapped_image_matches(HANDLE process, LPCVOID address, const std::wstring& expected_name)
        {
            std::vector<wchar_t> buffer(MAX_PATH);
            for (;;)
            {
                // Unlike Toolhelp32's module list, the underlying mapped image is
                // queryable before a suspended process initializes its loader list.
                const DWORD length = GetMappedFileNameW(process, const_cast<LPVOID>(address), buffer.data(), static_cast<DWORD>(buffer.size()));
                if (!length)
                    return false;
                if (length < buffer.size())
                {
                    const auto actual_name = basename(std::wstring(buffer.data(), length));
                    return CompareStringOrdinal(actual_name.c_str(), -1, expected_name.c_str(), -1, TRUE) == CSTR_EQUAL;
                }
                if (buffer.size() >= 32768)
                    return false;
                buffer.resize(buffer.size() * 2);
            }
        }

        LPTHREAD_START_ROUTINE resolve_remote_loadlibrary(HANDLE process)
        {
            const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
            if (!kernel)
                return nullptr;

            const FARPROC local_proc = GetProcAddress(kernel, "LoadLibraryW");
            if (!local_proc)
                return nullptr;

            HMODULE owner = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(local_proc), &owner))
            {
                return nullptr;
            }

            const auto expected_name = module_name(owner);
            if (expected_name.empty())
                return nullptr;

            const auto offset = reinterpret_cast<std::uintptr_t>(local_proc) - reinterpret_cast<std::uintptr_t>(owner);

            // The loader list for a freshly CREATE_SUSPENDED process may be empty.
            // Walk memory regions instead; MEM_IMAGE comes from the kernel's section
            // mappings and does not require PEB.Ldr to have been initialized.
            SYSTEM_INFO system{};
            GetSystemInfo(&system);
            const auto limit = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress));

            std::uint64_t cursor = reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress);
            while (cursor <= limit)
            {
                MEMORY_BASIC_INFORMATION info{};
                const auto address = reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(cursor));
                if (!VirtualQueryEx(process, address, &info, sizeof(info)))
                {
                    cursor += system.dwPageSize;
                    continue;
                }

                const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
                if (info.State == MEM_COMMIT && info.Type == MEM_IMAGE && info.AllocationBase == info.BaseAddress &&
                    mapped_image_matches(process, info.AllocationBase, expected_name))
                {
                    const std::uint64_t remote_address = static_cast<std::uint64_t>(base) + offset;
                    if (remote_address <= limit)
                    {
                        MEMORY_BASIC_INFORMATION code_region{};
                        if (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(remote_address)), &code_region, sizeof(code_region)) &&
                            code_region.State == MEM_COMMIT && code_region.Type == MEM_IMAGE && code_region.AllocationBase == info.AllocationBase)
                        {
                            return reinterpret_cast<LPTHREAD_START_ROUTINE>(static_cast<std::uintptr_t>(remote_address));
                        }
                    }
                    return nullptr;
                }

                const std::uint64_t next = static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(info.RegionSize);
                if (next <= cursor)
                    break;
                cursor = next;
            }
            return nullptr;
        }

    } // namespace

    launch_status launch_game(const std::wstring& executable, const std::wstring& arguments, const std::wstring& dll_path, std::stop_token stop)
    {
        if (stop.stop_requested())
        {
            return launch_status::cancelled;
        }
        if (!is_regular_file(executable) || !is_regular_file(dll_path))
        {
            return launch_status::failed;
        }

        const auto separator = executable.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            return launch_status::failed;
        }
        const std::wstring directory = executable.substr(0, separator);
        std::wstring command_line = L"\"" + executable + L"\" " + arguments;

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_DEFAULT_ERROR_MODE, nullptr, directory.c_str(), &startup,
                            &info))
        {
            return launch_status::failed;
        }

        pending_process child(info);
        if (stop.stop_requested())
        {
            return launch_status::cancelled;
        }

        const auto loader = resolve_remote_loadlibrary(child.process());
        if (!loader)
        {
            return launch_status::failed;
        }

        const SIZE_T path_bytes = (dll_path.size() + 1) * sizeof(wchar_t);
        remote_memory remote_path(child.process(), path_bytes);
        if (!remote_path)
        {
            return launch_status::failed;
        }

        SIZE_T written = 0;
        if (!WriteProcessMemory(child.process(), remote_path.get(), dll_path.c_str(), path_bytes, &written) || written != path_bytes)
        {
            return launch_status::failed;
        }

        if (stop.stop_requested())
        {
            return launch_status::cancelled;
        }

        unique_handle loader_thread(CreateRemoteThread(child.process(), nullptr, 0, loader, remote_path.get(), 0, nullptr));
        if (!loader_thread)
        {
            return launch_status::failed;
        }

        const ULONGLONG started = GetTickCount64();
        for (;;)
        {
            if (stop.stop_requested())
            {
                remote_path.abandon();
                return launch_status::cancelled;
            }

            const DWORD wait = WaitForSingleObject(loader_thread.get(), kWaitSliceMs);
            if (wait == WAIT_OBJECT_0)
            {
                break;
            }
            if (wait != WAIT_TIMEOUT || GetTickCount64() - started >= kLoadTimeoutMs)
            {
                // The remote thread may still be reading its DLL path.
                remote_path.abandon();
                return launch_status::failed;
            }
        }

        DWORD exit_code = 0;
        DWORD module = 0;
        if (!GetExitCodeProcess(child.process(), &exit_code) || exit_code != STILL_ACTIVE || !GetExitCodeThread(loader_thread.get(), &module) || module == 0)
        {
            return launch_status::failed;
        }
        if (stop.stop_requested())
        {
            return launch_status::cancelled;
        }

        if (ResumeThread(child.thread()) == static_cast<DWORD>(-1))
        {
            return launch_status::failed;
        }

        child.commit();
        return launch_status::success;
    }

} // namespace samp_debug

namespace
{

    constexpr UINT WM_LAUNCH_FINISHED = WM_APP + 1;

    struct dialog_state
    {
        std::jthread worker;
        bool launching = false;
    };

    std::wstring executable_directory()
    {
        std::vector<wchar_t> buffer(MAX_PATH);
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (!length)
                return {};
            if (length < buffer.size())
            {
                std::wstring filename(buffer.data(), length);
                const auto separator = filename.find_last_of(L"\\/");
                if (separator == std::wstring::npos)
                    return {};
                return filename.substr(0, separator + 1);
            }
            if (buffer.size() >= 32768)
                return {};
            buffer.resize(buffer.size() * 2);
        }
    }

    void show_error()
    {
        MessageBoxW(nullptr, L"Initialization failed.\r\nPlease reinstall.", L"SA:MP", MB_OK | MB_ICONERROR | MB_TOPMOST);
    }

    void set_launch_buttons(HWND dialog, BOOL enabled)
    {
        EnableWindow(GetDlgItem(dialog, IDC_LAUNCH_DEBUG), enabled);
        EnableWindow(GetDlgItem(dialog, IDC_LOCALHOST), enabled);
    }

    void launch(HWND dialog, dialog_state& state, const wchar_t* arguments)
    {
        if (state.launching)
            return;

        if (state.worker.joinable())
        {
            state.worker.join(); // The previous worker has posted its completion.
        }

        const std::wstring directory = executable_directory();
        if (directory.empty())
        {
            show_error();
            return;
        }

        const std::wstring executable = directory + L"gta_sa.exe";
        const std::wstring dll_path = directory + L"samp.dll";
        const std::wstring parameters = arguments;

        state.launching = true;
        set_launch_buttons(dialog, FALSE);

        try
        {
            state.worker = std::jthread([dialog, executable, dll_path, parameters](std::stop_token stop) {
                auto status = samp_debug::launch_status::failed;
                try
                {
                    status = samp_debug::launch_game(executable, parameters, dll_path, stop);
                }
                catch (...)
                {
                    status = samp_debug::launch_status::failed;
                }
                PostMessageW(dialog, WM_LAUNCH_FINISHED, static_cast<WPARAM>(status), 0);
            });
        }
        catch (...)
        {
            state.launching = false;
            set_launch_buttons(dialog, TRUE);
            show_error();
        }
    }

    INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
    {
        auto* state = reinterpret_cast<dialog_state*>(GetWindowLongPtrW(dialog, DWLP_USER));

        switch (message)
        {
            case WM_INITDIALOG:
            {
                state = reinterpret_cast<dialog_state*>(lparam);
                SetWindowLongPtrW(dialog, DWLP_USER, lparam);
                const auto icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_SAMP));
                SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
                SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
                return TRUE;
            }

            case WM_COMMAND:
                if (HIWORD(wparam) != BN_CLICKED && HIWORD(wparam) != 0)
                    return FALSE;
                switch (LOWORD(wparam))
                {
                    case IDC_LAUNCH_DEBUG:
                        if (state)
                            launch(dialog, *state, L"-d");
                        return TRUE;
                    case IDC_LOCALHOST:
                        if (state)
                        {
                            launch(dialog, *state, L"-c -h 127.0.0.1 -p 7777 -n Player");
                        }
                        return TRUE;
                    case IDC_EXIT:
                    case IDOK:
                    case IDCANCEL:
                        SendMessageW(dialog, WM_CLOSE, 0, 0);
                        return TRUE;
                }
                return FALSE;

            case WM_LAUNCH_FINISHED:
                if (state)
                {
                    state->launching = false;
                    if (state->worker.joinable())
                        state->worker.join();
                    set_launch_buttons(dialog, TRUE);
                    if (static_cast<samp_debug::launch_status>(wparam) == samp_debug::launch_status::failed)
                    {
                        show_error();
                    }
                }
                return TRUE;

            case WM_CLOSE:
                if (state && state->worker.joinable())
                {
                    // launch_game observes cancellation during its bounded wait and
                    // terminates an unsuccessful suspended child before returning.
                    state->worker.request_stop();
                    state->worker.join();
                }
                EndDialog(dialog, IDCANCEL);
                return TRUE;
        }
        return FALSE;
    }

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    dialog_state state;
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_DEBUG_LAUNCHER), nullptr, dialog_proc, reinterpret_cast<LPARAM>(&state));
    return 0;
}
