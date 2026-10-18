#include <stdafx.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <span>

namespace rcon
{

    struct Endpoint
    {
        std::array<std::uint8_t, 4> address{};
        std::uint16_t port = 0;
    };

    // udp payloads cannot exceed 65507 bytes for ipv4.
    constexpr std::size_t max_udp_payload = 65507;

    namespace
    {

        void WriteWord(std::vector<std::uint8_t>& bytes, std::uint16_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value));
            bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        }

        void WriteHeader(std::vector<std::uint8_t>& bytes, const Endpoint& server)
        {
            bytes.insert(bytes.end(), {'S', 'A', 'M', 'P'});
            bytes.insert(bytes.end(), server.address.begin(), server.address.end());
            WriteWord(bytes, server.port);
            bytes.push_back('x');
        }

    } // namespace

    bool BuildCommand(const Endpoint& server, std::string_view password, std::string_view command, std::vector<std::uint8_t>& packet)
    {
        packet.clear();

        constexpr std::size_t header_size = 11;
        constexpr std::size_t length_size = 4;
        const auto max_string = std::numeric_limits<std::uint16_t>::max();

        if (password.size() > max_string || command.size() > max_string || password.size() > max_udp_payload - header_size - length_size ||
            command.size() > max_udp_payload - header_size - length_size - password.size())
        {
            return false;
        }

        packet.reserve(header_size + length_size + password.size() + command.size());
        WriteHeader(packet, server);
        WriteWord(packet, static_cast<std::uint16_t>(password.size()));
        packet.insert(packet.end(), password.begin(), password.end());
        WriteWord(packet, static_cast<std::uint16_t>(command.size()));
        packet.insert(packet.end(), command.begin(), command.end());
        return true;
    }

    bool ParseReply(const Endpoint& server, std::span<const std::uint8_t> packet, std::string& message)
    {
        message.clear();
        constexpr std::size_t min_size = 13;
        if (packet.size() < min_size || !std::equal(packet.begin(), packet.begin() + 4, "SAMP") || !std::equal(server.address.begin(), server.address.end(), packet.begin() + 4) ||
            packet[8] != static_cast<std::uint8_t>(server.port) || packet[9] != static_cast<std::uint8_t>(server.port >> 8) || packet[10] != 'x')
        {
            return false;
        }

        const auto length = static_cast<std::size_t>(packet[11]) | (static_cast<std::size_t>(packet[12]) << 8);
        if (length != packet.size() - min_size)
        {
            return false;
        }

        message.assign(reinterpret_cast<const char*>(packet.data() + min_size), length);
        return true;
    }

} // namespace rcon

namespace
{

    class UniqueHandle
    {
    public:
        explicit UniqueHandle(HANDLE handle = nullptr) noexcept : handle_(handle)
        {}
        ~UniqueHandle()
        {
            reset();
        }

        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle& operator=(const UniqueHandle&) = delete;

        UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release())
        {}
        UniqueHandle& operator=(UniqueHandle&& other) noexcept
        {
            if (this != &other)
                reset(other.release());
            return *this;
        }

        [[nodiscard]] HANDLE get() const noexcept
        {
            return handle_;
        }
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return handle_ && handle_ != INVALID_HANDLE_VALUE;
        }
        void reset(HANDLE handle = nullptr) noexcept
        {
            if (*this)
                CloseHandle(handle_);
            handle_ = handle;
        }
        [[nodiscard]] HANDLE release() noexcept
        {
            HANDLE value = handle_;
            handle_ = nullptr;
            return value;
        }

    private:
        HANDLE handle_ = nullptr;
    };

    class UniqueSocket
    {
    public:
        explicit UniqueSocket(SOCKET socket = INVALID_SOCKET) noexcept : socket_(socket)
        {}
        ~UniqueSocket()
        {
            if (socket_ != INVALID_SOCKET)
                closesocket(socket_);
        }
        UniqueSocket(const UniqueSocket&) = delete;
        UniqueSocket& operator=(const UniqueSocket&) = delete;
        [[nodiscard]] SOCKET get() const noexcept
        {
            return socket_;
        }
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return socket_ != INVALID_SOCKET;
        }

    private:
        SOCKET socket_ = INVALID_SOCKET;
    };

    class WinsockSession
    {
    public:
        explicit WinsockSession(int& error) noexcept
        {
            WSADATA data{};
            error = WSAStartup(MAKEWORD(2, 2), &data);
            active_ = error == 0;
        }
        ~WinsockSession()
        {
            if (active_)
                WSACleanup();
        }
        WinsockSession(const WinsockSession&) = delete;
        WinsockSession& operator=(const WinsockSession&) = delete;

    private:
        bool active_ = false;
    };

    class AddrInfo
    {
    public:
        explicit AddrInfo(ADDRINFOA* value = nullptr) noexcept : value_(value)
        {}
        ~AddrInfo()
        {
            if (value_)
                freeaddrinfo(value_);
        }
        AddrInfo(const AddrInfo&) = delete;
        AddrInfo& operator=(const AddrInfo&) = delete;
        [[nodiscard]] ADDRINFOA* get() const noexcept
        {
            return value_;
        }

    private:
        ADDRINFOA* value_ = nullptr;
    };

    std::atomic_bool g_stopping{false};
    std::atomic<HANDLE> g_stop_event{nullptr};
    std::mutex g_output_mutex;

    void PrintLine(std::string_view line)
    {
        std::lock_guard lock(g_output_mutex);
        if (!line.empty())
            std::fwrite(line.data(), 1, line.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

    void PrintSocketError(std::string_view action, int code)
    {
        std::lock_guard lock(g_output_mutex);
        std::fprintf(stderr, "ERROR: %.*s (WSA %d).\n", static_cast<int>(action.size()), action.data(), code);
    }

    void Stop() noexcept
    {
        g_stopping.store(true, std::memory_order_relaxed);
        if (HANDLE event = g_stop_event.load(std::memory_order_acquire))
            SetEvent(event);
    }

    BOOL WINAPI ConsoleHandler(DWORD kind)
    {
        switch (kind)
        {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
            case CTRL_CLOSE_EVENT:
            case CTRL_LOGOFF_EVENT:
            case CTRL_SHUTDOWN_EVENT:
                Stop();
                return TRUE;
            default:
                return FALSE;
        }
    }

    class ConsoleHandlerScope
    {
    public:
        ConsoleHandlerScope() noexcept : installed_(SetConsoleCtrlHandler(ConsoleHandler, TRUE) != FALSE)
        {}
        ~ConsoleHandlerScope()
        {
            if (installed_)
                SetConsoleCtrlHandler(ConsoleHandler, FALSE);
        }
        [[nodiscard]] bool installed() const noexcept
        {
            return installed_;
        }

    private:
        bool installed_ = false;
    };

    struct Session
    {
        SOCKET socket = INVALID_SOCKET;
        sockaddr_in destination{};
        rcon::Endpoint endpoint{};
        std::string password;
    };

    bool SendCommand(const Session& session, std::string_view command)
    {
        std::vector<std::uint8_t> packet;
        if (!rcon::BuildCommand(session.endpoint, session.password, command, packet))
        {
            PrintLine("ERROR: RCON command or password exceeds UDP packet limits.");
            return false;
        }

        const int result = sendto(session.socket, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
                                  reinterpret_cast<const sockaddr*>(&session.destination), static_cast<int>(sizeof(session.destination)));
        if (result != static_cast<int>(packet.size()))
        {
            PrintSocketError("sendto failed", WSAGetLastError());
            return false;
        }
        return true;
    }

    DWORD WINAPI ReceiveThread(void* param)
    {
        auto& session = *static_cast<Session*>(param);
        std::array<std::uint8_t, 65536> buffer{};

        while (!g_stopping.load(std::memory_order_relaxed))
        {
            sockaddr_in peer{};
            int peer_size = sizeof(peer);
            const int length = recvfrom(session.socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&peer), &peer_size);
            if (length == SOCKET_ERROR)
            {
                const int code = WSAGetLastError();
                if (g_stopping.load(std::memory_order_relaxed))
                    break;
                if (code == WSAETIMEDOUT || code == WSAEWOULDBLOCK || code == WSAEINTR)
                    continue;
                PrintSocketError("recvfrom failed", code);
                Stop();
                break;
            }

            if (peer_size < static_cast<int>(sizeof(sockaddr_in)) || peer.sin_family != AF_INET || peer.sin_addr.s_addr != session.destination.sin_addr.s_addr ||
                peer.sin_port != session.destination.sin_port)
            {
                continue;
            }

            std::string message;
            if (rcon::ParseReply(session.endpoint, std::span<const std::uint8_t>(buffer.data(), length), message))
            {
                PrintLine(message);
            }
        }
        return 0;
    }

    DWORD WINAPI InputThread(void* param)
    {
        auto& session = *static_cast<Session*>(param);
        HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        if (input == INVALID_HANDLE_VALUE || !input)
        {
            PrintLine("ERROR: Cannot open standard input.");
            Stop();
            return 1;
        }

        std::string pending;
        bool skip_line = false;
        std::array<char, 512> buffer{};
        while (!g_stopping.load(std::memory_order_relaxed))
        {
            DWORD count = 0;
            if (!ReadFile(input, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
            {
                if (!g_stopping.load(std::memory_order_relaxed))
                {
                    PrintLine("ERROR: Unable to read standard input.");
                }
                break;
            }
            if (count == 0)
                break;

            for (DWORD i = 0; i < count; ++i)
            {
                const char c = buffer[i];
                if (c == '\r' || c == '\n')
                {
                    if (!skip_line && !pending.empty())
                    {
                        SendCommand(session, pending);
                    }
                    pending.clear();
                    skip_line = false;
                    continue;
                }
                if (skip_line)
                    continue;
                if (pending.size() == 4096)
                {
                    PrintLine("ERROR: Command is too long (maximum 4096 bytes).");
                    pending.clear();
                    skip_line = true;
                    continue;
                }
                pending.push_back(c);
            }
        }
        if (!g_stopping.load(std::memory_order_relaxed) && !pending.empty() && !skip_line)
        {
            SendCommand(session, pending);
        }
        Stop();
        return 0;
    }

    bool ParsePort(const char* text, std::uint16_t& value)
    {
        unsigned int parsed = 0;
        const auto length = std::char_traits<char>::length(text);
        const auto [end, ec] = std::from_chars(text, text + length, parsed);
        if (ec != std::errc{} || end != text + length || parsed == 0 || parsed > 65535)
            return false;
        value = static_cast<std::uint16_t>(parsed);
        return true;
    }

    bool ResolveHost(const char* host, sockaddr_in& result)
    {
        ADDRINFOA hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        ADDRINFOA* raw = nullptr;
        if (getaddrinfo(host, nullptr, &hints, &raw) != 0)
            return false;
        AddrInfo addresses(raw);
        if (!addresses.get() || !addresses.get()->ai_addr || addresses.get()->ai_addrlen < static_cast<int>(sizeof(sockaddr_in)))
            return false;
        result = *reinterpret_cast<const sockaddr_in*>(addresses.get()->ai_addr);
        return true;
    }

} // namespace

int main(int argc, char** argv)
{
    std::puts("\n SA:MP Command Line Remote Console Client");
    std::puts(" ----------------------------------------");
    std::puts(" (C) Copyright 2005-2006 SA:MP Team, v1.0\n");

    if (argc != 4)
    {
        std::puts(" Usage:");
        std::puts("   rcon <ip/host> <port> <password>");
        return 1;
    }

    std::uint16_t port = 0;
    if (!ParsePort(argv[2], port))
    {
        std::fputs("ERROR: Bad port.\n", stderr);
        return 1;
    }

    int init_error = 0;
    WinsockSession winsock(init_error);
    if (init_error != 0)
    {
        PrintSocketError("WSAStartup failed", init_error);
        return 1;
    }

    Session session;
    if (!ResolveHost(argv[1], session.destination))
    {
        PrintLine("ERROR: Bad host.");
        return 1;
    }
    session.destination.sin_port = htons(port);
    session.endpoint.port = port;
    session.password = argv[3];
    const auto* ip = reinterpret_cast<const unsigned char*>(&session.destination.sin_addr.s_addr);
    for (std::size_t i = 0; i < 4; ++i)
        session.endpoint.address[i] = ip[i];

    if (session.password.size() > 65535)
    {
        std::fputs("ERROR: RCON password is too long.\n", stderr);
        return 1;
    }

    UniqueSocket udp_socket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!udp_socket)
    {
        PrintSocketError("socket failed", WSAGetLastError());
        return 1;
    }
    session.socket = udp_socket.get();

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;
    if (bind(session.socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
    {
        PrintSocketError("bind failed", WSAGetLastError());
        return 1;
    }

    DWORD receive_timeout_ms = 250;
    if (setsockopt(session.socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receive_timeout_ms), sizeof(receive_timeout_ms)) == SOCKET_ERROR)
    {
        PrintSocketError("setsockopt failed", WSAGetLastError());
        return 1;
    }

    {
        std::lock_guard lock(g_output_mutex);
        std::printf("Remote Console: %u.%u.%u.%u:%u...\n\n", ip[0], ip[1], ip[2], ip[3], port);
    }

    UniqueHandle shutdown_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!shutdown_event)
    {
        std::fputs("ERROR: CreateEvent failed.\n", stderr);
        return 1;
    }
    g_stop_event.store(shutdown_event.get(), std::memory_order_release);
    ConsoleHandlerScope console_handler;
    if (!console_handler.installed())
    {
        std::fputs("ERROR: SetConsoleCtrlHandler failed.\n", stderr);
        g_stop_event.store(nullptr, std::memory_order_release);
        return 1;
    }

    UniqueHandle receive(CreateThread(nullptr, 0, ReceiveThread, &session, 0, nullptr));
    if (!receive)
    {
        std::fputs("ERROR: Cannot create network thread.\n", stderr);
        g_stop_event.store(nullptr, std::memory_order_release);
        return 1;
    }

    if (!SendCommand(session, "echo RCON admin connected to server."))
    {
        Stop();
    }

    UniqueHandle input;
    if (!g_stopping.load(std::memory_order_relaxed))
    {
        input.reset(CreateThread(nullptr, 0, InputThread, &session, 0, nullptr));
        if (!input)
        {
            std::fputs("ERROR: Cannot create console thread.\n", stderr);
            Stop();
        }
    }

    WaitForSingleObject(shutdown_event.get(), INFINITE);
    g_stopping.store(true, std::memory_order_relaxed);
    if (input)
    {
        // cancel a pending ReadFile without forcibly terminating the thread.
        CancelSynchronousIo(input.get());
        // ReadFile may not be cancellable on every console host.
        WaitForSingleObject(input.get(), 1000);
    }
    WaitForSingleObject(receive.get(), INFINITE);
    g_stop_event.store(nullptr, std::memory_order_release);
    return 0;
}
