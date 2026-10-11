#include <stdafx.h>

#include "main.h"

#include "Scripting.h"
#include "patch/hooks.h"
#include "net/CNetGame.h"
#include "pools/CVehiclePool.h"

#include <cstdarg>
#include <new>

namespace
{
    constexpr std::uintptr_t kProcessOneCommand = 0x469EB0;
    constexpr std::uintptr_t kScmDispatchPatch = 0x469EF5;

    constexpr std::uintptr_t kOpcode09C6Slot = 0x47BF54;
    constexpr std::uintptr_t kOpcode09D3Slot = 0x47BF88;

    constexpr std::uint16_t kScmEnabled = 0xD2FF;
    constexpr std::uint16_t kScmDisabled = 0xD08B;

    struct RunningScript
    {
        std::uint8_t pad00[0x14];
        std::uint32_t currentIp;
        std::uint8_t pad18[0x24];
        std::uint32_t locals[18];
        std::uint8_t pad84[0x41];
        std::uint8_t conditionResult;
        std::uint8_t padC6[0x12];
        std::uint32_t index;
        std::uint8_t hasLocalCopy;
        std::uint8_t padDD[0x03];
    };

    static_assert(sizeof(RunningScript) == 0xE0);
    static_assert(offsetof(RunningScript, currentIp) == 0x14);
    static_assert(offsetof(RunningScript, locals) == 0x3C);
    static_assert(offsetof(RunningScript, conditionResult) == 0xC5);

    using ProcessOneCommandFn = int(__thiscall*)(RunningScript*);

    RunningScript* g_thread = nullptr;
    std::uint8_t g_buffer[256]{};
    bool g_executing = false;
    bool g_bufferReady = false;
    ProcessOneCommandFn g_processOneCommand = nullptr;
}

namespace scm
{
    void initialize()
    {
        if (g_thread)
            return;

        g_thread = new (std::nothrow) RunningScript{};

        if (!g_thread)
            return;

        g_processOneCommand = reinterpret_cast<ProcessOneCommandFn>(kProcessOneCommand);
    }

    int ExecBuffer()
    {
        if (!g_thread || !g_processOneCommand || !g_bufferReady || g_executing)
            return 0;

        std::uint16_t previous = 0;
        if (!patch::read(kScmDispatchPatch, &previous, sizeof(previous)) || (previous != kScmEnabled && previous != kScmDisabled) || !patch::write(kScmDispatchPatch, kScmEnabled))
        {
            Log("[scm] failed to enable opcode dispatch");
            ExitProcess(1);
        }
        g_executing = true;
        g_thread->currentIp = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_buffer));
        g_thread->conditionResult = 0;
        g_processOneCommand(g_thread);
        const int result = g_thread->conditionResult;
        g_executing = false;
        // keep gta's existing dispatch mode instead of disabling the game's scm
        if (!patch::write(kScmDispatchPatch, previous))
        {
            Log("[scm] failed to restore opcode dispatch");
            ExitProcess(1);
        }
        return result;
    }

    int ProcessCommand(const OpcodeInfo* opcode, ...)
    {
        if (!opcode || !g_thread || !g_processOneCommand || g_executing || !std::memchr(opcode->params, 0, sizeof(opcode->params)))
            return 0;

        std::uint8_t buffer[sizeof(g_buffer)]{};
        std::uint32_t locals[18]{};
        void* variables[18]{};
        std::memcpy(buffer, &opcode->opcode, sizeof(opcode->opcode));
        std::size_t position = sizeof(opcode->opcode);
        std::uint16_t variableCount = 0;
        auto append = [&](const void* data, std::size_t size) {
            if (size > sizeof(buffer) - position)
                return false;
            std::memcpy(buffer + position, data, size);
            position += size;
            return true;
        };
        auto tag = [&](std::uint8_t type) {
            return append(&type, sizeof(type));
        };

        va_list args;
        va_start(args, opcode);
        bool valid = true;
        for (const char* param = opcode->params; *param && valid; ++param)
        {
            switch (*param)
            {
                case 'i':
                {
                    const int value = va_arg(args, int);
                    valid = tag(0x01) && append(&value, sizeof(value));
                    break;
                }
                case 'f':
                {
                    const float value = static_cast<float>(va_arg(args, double));
                    valid = tag(0x06) && append(&value, sizeof(value));
                    break;
                }
                case 'v':
                {
                    void* value = va_arg(args, void*);
                    if (!value || variableCount >= std::size(locals))
                    {
                        valid = false;
                        break;
                    }
                    variables[variableCount] = value;
                    std::memcpy(&locals[variableCount], value, sizeof(locals[variableCount]));
                    valid = tag(0x03) && append(&variableCount, sizeof(variableCount));
                    ++variableCount;
                    break;
                }
                case 's':
                {
                    const char* value = va_arg(args, const char*);
                    if (!value)
                    {
                        valid = false;
                        break;
                    }
                    const std::size_t length = strnlen_s(value, sizeof(buffer));
                    if (length > 255)
                    {
                        valid = false;
                        break;
                    }
                    const auto encodedLength = static_cast<std::uint8_t>(length);
                    valid = tag(0x0E) && append(&encodedLength, sizeof(encodedLength)) && append(value, length);
                    break;
                }
                case 'z':
                    valid = tag(0x00);
                    break;
                default:
                    valid = false;
                    break;
            }
        }
        va_end(args);
        if (!valid)
            return 0;

        std::memcpy(g_buffer, buffer, sizeof(buffer));
        std::memcpy(g_thread->locals, locals, sizeof(locals));
        g_bufferReady = true;
        const int result = ExecBuffer();
        for (std::uint16_t i = 0; i < variableCount; ++i)
            std::memcpy(variables[i], &g_thread->locals[i], sizeof(g_thread->locals[i]));
        return result;
    }

    void handle_09c6(EventType type, std::uint32_t param1, std::uint32_t param2, std::uint32_t param3)
    {
        auto* netGame = g_netGame.get();
        auto* vehiclePool = netGame && netGame->m_pPools ? netGame->m_pPools->m_pVehicle : nullptr;
        if (vehiclePool)
        {
            for (int id = 0; id <= vehiclePool->m_nCount; ++id)
            {
                auto* vehicle = vehiclePool->Get(static_cast<CVehiclePool::ID>(id));
                if (!vehicle || vehicle->m_handle != param1 || !vehicle->DoesExist())
                    continue;
                if (type >= EventType::Paintjob && type <= EventType::EnterExitModShop)
                    netGame->ScmEvent(static_cast<std::uint32_t>(id), param2, param3, static_cast<std::uint32_t>(type));
                if (type == EventType::CarColor)
                    vehicle->SetColor(static_cast<unsigned char>(param2), static_cast<unsigned char>(param3));
                else if (type == EventType::EnterExitModShop)
                {
                    vehicle->pad_71 = param2 ? 1 : 0;
                    if (!param2)
                        vehicle->SetColor(vehicle->GetPrimaryColor(), vehicle->GetSecondaryColor());
                }
                break;
            }
        }
        // r5 sends events 1-4; scm already applies paintjobs and components
        switch (type)
        {
            case EventType::Paintjob:
                Log("[scm:09c6] paintjob: vehicle=%u, paintjob=%d, param3=%u", param1, static_cast<std::int32_t>(param2), param3);
                break;

            case EventType::CarComponent:
                Log("[scm:09c6] component: vehicle=%u, model=%u, param3=%u", param1, param2, param3);
                break;

            case EventType::CarColor:
            {
                const auto primary = static_cast<std::uint8_t>(param2);
                const auto secondary = static_cast<std::uint8_t>(param3);
                Log("[scm:09c6] color: vehicle=%u, primary=%u, secondary=%u", param1, static_cast<unsigned>(primary), static_cast<unsigned>(secondary));

                auto* pool = *reinterpret_cast<std::uint8_t**>(0xB74494);
                // gta's GetAt checks the generation but does not check the slot bounds
                if (!pool || static_cast<std::int32_t>(param1) < 0 || (param1 >> 8) >= *reinterpret_cast<const std::uint32_t*>(pool + 8))
                {
                    Log("[scm:09c6] color skipped: invalid vehicle handle");
                    break;
                }
                const auto getVehicle = reinterpret_cast<std::uint8_t*(__thiscall*)(void*, int)>(0x4048E0);
                auto* vehicle = getVehicle(pool, static_cast<int>(param1));
                if (!vehicle)
                {
                    Log("[scm:09c6] color skipped: vehicle no longer exists");
                    break;
                }
                // native part of r5 CVehicle::SetColor; wrapper caching needs CVehicle later
                vehicle[0x434] = primary;
                vehicle[0x435] = secondary;
                break;
            }

            case EventType::EnterExitModShop:
                Log("[scm:09c6] modshop: vehicle=%u, state=%s, param2=%u, param3=%u", param1, param2 ? "enter" : "exit", param2, param3);
                // r5 caches the vehicle colors on modshop exit
                break;

            default:
                // r5 ignores other types, including CASHWIN's event 5
                Log("[scm:09c6] ignored: type=%d, param1=%u, param2=%u, param3=%u", static_cast<std::int32_t>(type), param1, param2, param3);
                break;
        }
    }

    bool handle_09d3()
    {
        const auto findPlayerPed = reinterpret_cast<void*(__cdecl*)(int)>(0x56E210);
        const auto* ped = static_cast<const std::uint8_t*>(findPlayerPed(-1));
        if (!ped)
            return true;

        const auto flags = *reinterpret_cast<const std::uint32_t*>(ped + 0x46C);
        if (!(flags & 0x100))
            return true;

        const auto* vehicle = *reinterpret_cast<const std::uint8_t* const*>(ped + 0x58C);
        // r5 treats freight and passenger train drivers as passengers
        if (vehicle)
        {
            const auto* driver = *reinterpret_cast<const void* const*>(vehicle + 0x460);
            const auto model = *reinterpret_cast<const std::uint16_t*>(vehicle + 0x22);
            if (driver != ped || model == 537 || model == 570)
                return true;
        }
        // this opcode's compare result is false for a driver
        return false;
    }

    bool install()
    {
        initialize();

        if (!g_thread || !g_processOneCommand)
            return false;

        const std::uint32_t opcode09C6 = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&Opcode09C6));
        const std::uint32_t opcode09D3 = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&Opcode09D3));

        if (!patch::write(kOpcode09C6Slot, opcode09C6))
            return false;

        if (!patch::write(kOpcode09D3Slot, opcode09D3))
            return false;

        Log("[scm] installed");

        return true;
    }
}
