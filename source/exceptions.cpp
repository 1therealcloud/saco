#include <stdafx.h>

#include "resource.h"

#if defined(_DEBUG)
#   include <dbghelp.h>
#endif

namespace exceptions
{
    namespace
    {
        CONTEXT* g_context = nullptr;
        EXCEPTION_RECORD* g_exception = nullptr;
        volatile LONG g_handlingException = 0;

#if defined(_DEBUG)
        HANDLE g_dumpRequested = nullptr;
        HANDLE g_dumpThread = nullptr;
        DWORD g_dumpThreadId = 0;
        DWORD g_crashedThreadId = 0;
        EXCEPTION_RECORD g_dumpException{};
        CONTEXT g_dumpContext{};
        EXCEPTION_POINTERS g_dumpPointers{&g_dumpException, &g_dumpContext};

        void write_minidump()
        {
            SYSTEMTIME time{};
            GetLocalTime(&time);

            char path[128]{};
            std::snprintf(path, sizeof(path), "samp-crash-%04u%02u%02u-%02u%02u%02u-%03u-%lu.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                          time.wMilliseconds, GetCurrentProcessId());

            const HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                Log("[exceptions] can't create minidump, error=%lu", GetLastError());
                return;
            }

            MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
            exceptionInfo.ThreadId = g_crashedThreadId;
            exceptionInfo.ExceptionPointers = &g_dumpPointers;
            exceptionInfo.ClientPointers = FALSE;

            constexpr auto dumpType = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
            const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, dumpType, &exceptionInfo, nullptr, nullptr);
            const DWORD error = written ? ERROR_SUCCESS : GetLastError();
            CloseHandle(file);

            if (written)
                Log("[exceptions] minidump saved: %s", path);
            else
                Log("[exceptions] can't write minidump, error=%lu", error);
        }

        DWORD WINAPI dump_thread(void*)
        {
            if (WaitForSingleObject(g_dumpRequested, INFINITE) == WAIT_OBJECT_0)
                write_minidump();
            return 0;
        }

        void initialize_minidump()
        {
            if (g_dumpThread)
                return;

            g_dumpRequested = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!g_dumpRequested)
            {
                Log("[exceptions] can't create minidump event, error=%lu", GetLastError());
                return;
            }

            g_dumpThread = CreateThread(nullptr, 0, dump_thread, nullptr, 0, &g_dumpThreadId);
            if (!g_dumpThread)
            {
                const DWORD error = GetLastError();
                CloseHandle(g_dumpRequested);
                g_dumpRequested = nullptr;
                Log("[exceptions] can't create minidump thread, error=%lu", error);
            }
        }

        void request_minidump(EXCEPTION_POINTERS* info)
        {
            if (!g_dumpThread || GetCurrentThreadId() == g_dumpThreadId)
                return;

            // keep the dump independent of the crashed thread's exhausted stack
            g_crashedThreadId = GetCurrentThreadId();
            g_dumpException = *info->ExceptionRecord;
            g_dumpException.ExceptionRecord = nullptr;
            g_dumpContext = *info->ContextRecord;

            if (!SetEvent(g_dumpRequested))
            {
                Log("[exceptions] can't request minidump, error=%lu", GetLastError());
                return;
            }

            // do not keep the crash dialog waiting forever if DbgHelp hangs
            const DWORD result = WaitForSingleObject(g_dumpThread, 15000);
            if (result == WAIT_TIMEOUT)
                Log("[exceptions] minidump thread timed out");
            else if (result == WAIT_FAILED)
                Log("[exceptions] can't wait for minidump, error=%lu", GetLastError());
        }
#endif

        const char* exception_name(DWORD code)
        {
            switch (code)
            {
                case EXCEPTION_ACCESS_VIOLATION:
                    return "access violation";

                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                    return "array bounds exceeded";

                case EXCEPTION_BREAKPOINT:
                    return "breakpoint";

                case EXCEPTION_DATATYPE_MISALIGNMENT:
                    return "datatype misalignment";

                case EXCEPTION_FLT_DENORMAL_OPERAND:
                    return "float denormal operand";

                case EXCEPTION_FLT_DIVIDE_BY_ZERO:
                    return "float divide by zero";

                case EXCEPTION_FLT_INEXACT_RESULT:
                    return "float inexact result";

                case EXCEPTION_FLT_INVALID_OPERATION:
                    return "float invalid operation";

                case EXCEPTION_FLT_OVERFLOW:
                    return "float overflow";

                case EXCEPTION_FLT_STACK_CHECK:
                    return "float stack check";

                case EXCEPTION_FLT_UNDERFLOW:
                    return "float underflow";

                case EXCEPTION_ILLEGAL_INSTRUCTION:
                    return "illegal instruction";

                case EXCEPTION_IN_PAGE_ERROR:
                    return "in-page error";

                case EXCEPTION_INT_DIVIDE_BY_ZERO:
                    return "integer divide by zero";

                case EXCEPTION_INT_OVERFLOW:
                    return "integer overflow";

                case EXCEPTION_INVALID_DISPOSITION:
                    return "invalid disposition";

                case EXCEPTION_NONCONTINUABLE_EXCEPTION:
                    return "noncontinuable exception";

                case EXCEPTION_PRIV_INSTRUCTION:
                    return "privileged instruction";

                case EXCEPTION_SINGLE_STEP:
                    return "single step";

                case EXCEPTION_STACK_OVERFLOW:
                    return "stack overflow";

                default:
                    return "unknown exception";
            }
        }

        const char* access_name(ULONG_PTR operation)
        {
            switch (operation)
            {
                case 0:
                    return "read";

                case 1:
                    return "write";

                case 8:
                    return "execute";

                default:
                    return "unknown";
            }
        }

        void report_crash()
        {}

        void release_cursor(bool show = false)
        {
            ClipCursor(nullptr);
            ReleaseCapture();
            SetCursor(LoadCursorA(nullptr, IDC_ARROW));

            if (show)
            {
                while (ShowCursor(TRUE) < 0)
                {
                }
            }
        }

        bool crash_module(char* output, std::size_t size)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(g_exception->ExceptionAddress),
                                    &module))
                return false;

            char path[MAX_PATH]{};
            const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
            if (!length || length >= MAX_PATH)
                return false;

            const char* name = std::strrchr(path, '\\');
            name = name ? name + 1 : path;
            const auto offset = reinterpret_cast<std::uintptr_t>(g_exception->ExceptionAddress) - reinterpret_cast<std::uintptr_t>(module);
            std::snprintf(output, size, "%s + 0x%08X", name, static_cast<unsigned>(offset));
            return true;
        }

        HMODULE get_module()
        {
            HMODULE module = nullptr;

            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&get_module), &module);

            return module;
        }

        HWND get_game_window()
        {
            const HWND foreground = GetForegroundWindow();

            if (foreground)
            {
                DWORD processId = 0;
                GetWindowThreadProcessId(foreground, &processId);

                if (processId == GetCurrentProcessId())
                    return foreground;
            }

            struct Search
            {
                DWORD processId;
                HWND window;
            };

            Search search{GetCurrentProcessId(), nullptr};

            EnumWindows(
                [](HWND hwnd, LPARAM param) -> BOOL {
                    auto& search = *reinterpret_cast<Search*>(param);

                    DWORD processId = 0;
                    GetWindowThreadProcessId(hwnd, &processId);

                    if (processId != search.processId)
                        return TRUE;

                    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
                        return TRUE;

                    char className[64]{};
                    GetClassNameA(hwnd, className, static_cast<int>(sizeof(className)));

                    if (lstrcmpiA(className, "ConsoleWindowClass") == 0)
                        return TRUE;

                    search.window = hwnd;
                    return FALSE;
                },
                reinterpret_cast<LPARAM>(&search));

            return search.window;
        }

        void build_report(char* output, std::size_t size)
        {
            if (!output || size == 0 || !g_context || !g_exception)
                return;

            std::snprintf(output, size,
                          "Exception At Address: 0x%08X\r\n\r\n"
                          "Exception: %s (0x%08X)\r\n"
                          "Registers:\r\n"
                          "EAX: 0x%08X\tEBX: 0x%08X\tECX: 0x%08X\tEDX: 0x%08X\r\n"
                          "ESI: 0x%08X\tEDI: 0x%08X\tEBP: 0x%08X\tESP: 0x%08X\r\n"
                          "EFLAGS: 0x%08X\r\n",
                          g_context->Eip, exception_name(g_exception->ExceptionCode), g_exception->ExceptionCode, g_context->Eax, g_context->Ebx, g_context->Ecx, g_context->Edx,
                          g_context->Esi, g_context->Edi, g_context->Ebp, g_context->Esp, g_context->EFlags);

            char module[MAX_PATH + 32]{};
            if (crash_module(module, sizeof(module)))
            {
                const std::size_t used = std::strlen(output);
                if (used < size)
                    std::snprintf(output + used, size - used, "\r\nModule: %s\r\n", module);
            }

            if ((g_exception->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || g_exception->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && g_exception->NumberParameters >= 2)
            {
                const std::size_t used = std::strlen(output);

                if (used < size)
                {
                    std::snprintf(output + used, size - used, "\r\nAccess: %s\r\nMemory: 0x%08llX\r\n", access_name(g_exception->ExceptionInformation[0]),
                                  static_cast<unsigned long long>(g_exception->ExceptionInformation[1]));
                }
            }

            if (g_exception->ExceptionCode == EXCEPTION_IN_PAGE_ERROR && g_exception->NumberParameters >= 3)
            {
                const std::size_t used = std::strlen(output);
                if (used < size)
                    std::snprintf(output + used, size - used, "I/O status: 0x%08X\r\n", static_cast<unsigned>(g_exception->ExceptionInformation[2]));
            }
        }

        void log_exception()
        {
            if (!g_exception || !g_context)
                return;

            Log("[exceptions] type=%s code=0x%08X address=%p", exception_name(g_exception->ExceptionCode), g_exception->ExceptionCode, g_exception->ExceptionAddress);

            char module[MAX_PATH + 32]{};
            if (crash_module(module, sizeof(module)))
                Log("[exceptions] module=%s", module);

            if ((g_exception->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || g_exception->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && g_exception->NumberParameters >= 2)
            {
                Log("[exceptions] access=%s memory=%p", access_name(g_exception->ExceptionInformation[0]), reinterpret_cast<void*>(g_exception->ExceptionInformation[1]));
            }

            if (g_exception->ExceptionCode == EXCEPTION_IN_PAGE_ERROR && g_exception->NumberParameters >= 3)
                Log("[exceptions] io status=0x%08X", static_cast<unsigned>(g_exception->ExceptionInformation[2]));

            Log("[exceptions] eax=%08X ebx=%08X ecx=%08X edx=%08X", g_context->Eax, g_context->Ebx, g_context->Ecx, g_context->Edx);

            Log("[exceptions] esi=%08X edi=%08X ebp=%08X esp=%08X eip=%08X eflags=%08X", g_context->Esi, g_context->Edi, g_context->Ebp, g_context->Esp, g_context->Eip,
                g_context->EFlags);
        }

        INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wParam, LPARAM)
        {
            switch (message)
            {
                case WM_INITDIALOG:
                {
                    release_cursor();

                    char report[4096]{};
                    build_report(report, sizeof(report));
                    SetDlgItemTextA(dialog, IDC_EDIT1, report);

                    SetForegroundWindow(dialog);
                    SetActiveWindow(dialog);
                    SetFocus(GetDlgItem(dialog, IDC_BUTTON1));

                    return FALSE;
                }

                case WM_MOUSEMOVE:
                    release_cursor();
                    break;

                case WM_SETCURSOR:
                    SetCursor(LoadCursorA(nullptr, IDC_ARROW));
                    return TRUE;

                case WM_COMMAND:
                    switch (LOWORD(wParam))
                    {
                        case IDC_BUTTON1:
                            EndDialog(dialog, TRUE);
                            return TRUE;

                        case IDC_BUTTON2:
                            report_crash();
                            return TRUE;
                    }
                    break;

                case WM_CLOSE:
                    EndDialog(dialog, TRUE);
                    return TRUE;
            }

            return FALSE;
        }

        LONG WINAPI handler(EXCEPTION_POINTERS* info)
        {
            if (!info || !info->ExceptionRecord || !info->ContextRecord)
                return EXCEPTION_CONTINUE_SEARCH;

            if (info->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT)
                return EXCEPTION_CONTINUE_SEARCH;

            if (InterlockedExchange(&g_handlingException, 1) != 0)
                return EXCEPTION_CONTINUE_SEARCH;

            g_exception = info->ExceptionRecord;
            g_context = info->ContextRecord;

#if defined(_DEBUG)
            request_minidump(info);
#endif

            log_exception();
            release_cursor(true);

            const HWND gameWindow = get_game_window();

            if (gameWindow)
                ShowWindow(gameWindow, SW_MINIMIZE);

            if (const HMODULE module = get_module())
            {
                DialogBoxParamA(module, MAKEINTRESOURCEA(IDD_DIALOG1), gameWindow, dialog_proc, 0);
            }

            if (IsDebuggerPresent())
            {
                DebugBreak();
                return EXCEPTION_CONTINUE_SEARCH;
            }

            return EXCEPTION_EXECUTE_HANDLER;
        }
    }

    void install()
    {
#if defined(_DEBUG)
        initialize_minidump();
#endif
        SetUnhandledExceptionFilter(handler);
    }
}
