#include <stdafx.h>

#include "CHttpClient.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace
{
    bool StartsWithHttp(const std::string& value)
    {
        return value.size() >= 4 && value[0] == 'H' && value[1] == 'T' && value[2] == 'T' && value[3] == 'P';
    }
}

CHttpClient::CHttpClient()
{
    WSADATA data{};
    m_initialized = WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

CHttpClient::~CHttpClient()
{
    Disconnect();
    if (m_initialized)
        WSACleanup();
}

int CHttpClient::ProcessURL(int type, char* url, char* data, char* referer)
{
    return static_cast<int>(ProcessUrl(type, static_cast<const char*>(url), static_cast<const char*>(data), static_cast<const char*>(referer)));
}

CHttpClient::ErrorCode CHttpClient::ProcessUrl(int type, const char* url, const char* data, const char* referer)
{
    Log("[http] request type=%d, url=%s", type, url ? url : "");
    Reset();
    if (!m_initialized)
        return static_cast<ErrorCode>(m_iError = HTTP_ERROR_NO_SOCKET);
    InitializeRequest(type, url, data, referer);
    if (m_iError == HTTP_SUCCESS)
        Process();

    return static_cast<ErrorCode>(m_iError);
}

bool CHttpClient::GetHeaderValue(const char* headerName, char* returnBuffer, int bufferSize)
{
    if (!headerName || !returnBuffer || bufferSize <= 0 || !m_hasResponse)
        return false;

    std::string_view name(headerName);
    if (name.ends_with(':'))
        name.remove_suffix(1);
    if (name.empty())
        return false;
    std::string_view headers(m_Response.header);
    while (!headers.empty())
    {
        const auto end = headers.find('\n');
        const auto line = headers.substr(0, end);
        const auto colon = line.find(':');
        if (colon == name.size() && _strnicmp(line.data(), name.data(), name.size()) == 0)
        {
            auto value = line.substr(colon + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                value.remove_prefix(1);
            while (!value.empty() && (value.back() == '\r' || value.back() == ' ' || value.back() == '\t'))
                value.remove_suffix(1);
            if (value.size() + 1 > static_cast<std::size_t>(bufferSize))
                return false;
            std::memcpy(returnBuffer, value.data(), value.size());
            returnBuffer[value.size()] = '\0';
            return true;
        }
        if (end == std::string_view::npos)
            break;
        headers.remove_prefix(end + 1);
    }
    return false;
}

bool CHttpClient::Connect(const char* host, int port)
{
    if (!host || !*host)
    {
        m_iError = HTTP_ERROR_BAD_HOST;
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(host, service.c_str(), &hints, &addresses) != 0 || !addresses)
    {
        m_iError = HTTP_ERROR_BAD_HOST;
        return false;
    }
    sockaddr_in address{};
    std::memcpy(&address, addresses->ai_addr, sizeof(address));
    freeaddrinfo(addresses);

    m_iSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_iSocket == INVALID_SOCKET)
    {
        m_iError = HTTP_ERROR_NO_SOCKET;
        return false;
    }

    const DWORD timeout = 10000;
    setsockopt(m_iSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(m_iSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    if (connect(m_iSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        Disconnect();
        m_iError = HTTP_ERROR_CANT_CONNECT;
        return false;
    }

    return true;
}

void CHttpClient::Disconnect()
{
    if (m_iSocket == INVALID_SOCKET)
        return;

    closesocket(m_iSocket);
    m_iSocket = INVALID_SOCKET;
}

bool CHttpClient::SendView(std::string_view data)
{
    if (m_iSocket == INVALID_SOCKET)
    {
        m_iError = HTTP_ERROR_CANT_WRITE;
        return false;
    }

    // R5 does one send() and only checks for an error. Looping here preserves
    // the wire data but fixes the partial-send case.
    std::size_t offset = 0;

    while (offset < data.size())
    {
        const std::size_t left = data.size() - offset;
        const int amount = static_cast<int>(std::min<std::size_t>(left, static_cast<std::size_t>(std::numeric_limits<int>::max())));

        const int sent = send(m_iSocket, data.data() + offset, amount, 0);
        if (sent == SOCKET_ERROR || sent <= 0)
        {
            m_iError = HTTP_ERROR_CANT_WRITE;
            return false;
        }

        offset += static_cast<std::size_t>(sent);
    }

    return true;
}

bool CHttpClient::Send(const char* buffer)
{
    return buffer && SendView(buffer);
}

int CHttpClient::Receive(char* buffer, int bufferSize)
{
    if (m_iSocket == INVALID_SOCKET || !buffer || bufferSize <= 0)
        return SOCKET_ERROR;

    return recv(m_iSocket, buffer, bufferSize, 0);
}

void CHttpClient::InitializeRequest(int type, const char* url, const char* postData, const char* referer)
{
    if (!url)
    {
        m_iError = HTTP_ERROR_BAD_HOST;
        return;
    }

    m_Request.rtype = type;
    m_Request.referer = referer ? referer : "";

    if (type == HTTP_POST)
        m_Request.data = postData ? postData : "";

    // R5 does not understand "http://". It expects host[:port]/path.
    std::string useUrl = url;

    std::size_t slash = useUrl.find('/');
    if (slash == std::string::npos)
    {
        useUrl.push_back('/');
        slash = useUrl.size() - 1;
    }

    m_Request.host.assign(useUrl.data(), slash);
    m_Request.file.assign(useUrl.data() + slash, useUrl.size() - slash);

    const std::size_t colon = m_Request.host.find(':');
    if (colon != std::string::npos)
    {
        // R5 uses atoi() and stores the result in an unsigned short.
        const int parsedPort = std::atoi(m_Request.host.c_str() + colon + 1);
        m_Request.port = static_cast<std::uint16_t>(parsedPort);
        m_Request.host.resize(colon);
    }
    else
    {
        m_Request.port = 80;
    }
}

void CHttpClient::HandleEntity()
{
    std::array<char, RECV_BUFFER_SIZE> buffer{};
    std::string response;

    bool headerGot = false;
    bool hasContentLength = false;
    std::size_t contentLength = 0;

    while (true)
    {
        const int bytesRead = Receive(buffer.data(), static_cast<int>(buffer.size()));
        if (bytesRead == SOCKET_ERROR)
        {
            Disconnect();
            m_iError = HTTP_ERROR_MALFORMED_RESPONSE;
            return;
        }
        if (bytesRead == 0)
            break;

        response.append(buffer.data(), static_cast<std::size_t>(bytesRead));

        if (!headerGot && response.size() > 64 * 1024)
        {
            Disconnect();
            m_iError = HTTP_ERROR_MALFORMED_RESPONSE;
            return;
        }
        if (!headerGot)
        {
            std::size_t headerEnd = response.find("\r\n\r\n");
            std::size_t separatorLength = 4;

            if (headerEnd == std::string::npos)
            {
                headerEnd = response.find("\n\n");
                separatorLength = 2;
            }

            if (headerEnd != std::string::npos)
            {
                headerGot = true;

                m_Response.header.assign(response.data(), headerEnd);
                m_Response.header_len = static_cast<std::uint32_t>(headerEnd);

                response.erase(0, headerEnd + separatorLength);

                const char* contentLengthHeader = StrIStr(m_Response.header.c_str(), "CONTENT-LENGTH:");
                if (contentLengthHeader)
                {
                    hasContentLength = true;

                    contentLengthHeader += std::strlen("CONTENT-LENGTH:");
                    while (*contentLengthHeader == ' ' || *contentLengthHeader == '\t')
                        ++contentLengthHeader;

                    contentLength = static_cast<std::size_t>(std::max(0, std::atoi(contentLengthHeader)));

                    if (contentLength > MAX_ENTITY_LENGTH)
                    {
                        Disconnect();
                        m_iError = HTTP_ERROR_CONTENT_TOO_BIG;
                        return;
                    }
                }
            }
        }

        if (headerGot && m_Request.rtype == HTTP_HEAD)
        {
            response.clear();
            break;
        }
        if (headerGot && response.size() > MAX_ENTITY_LENGTH)
        {
            Disconnect();
            m_iError = HTTP_ERROR_CONTENT_TOO_BIG;
            return;
        }
        // R5 stops receiving once at least Content-Length body bytes are present.
        // It does not truncate response data if a recv() happened to provide more.
        if (headerGot && hasContentLength && response.size() >= contentLength)
            break;
    }

    Disconnect();

    if (!headerGot || !StartsWithHttp(m_Response.header) || (m_Request.rtype != HTTP_HEAD && hasContentLength && response.size() < contentLength))
    {
        m_iError = HTTP_ERROR_MALFORMED_RESPONSE;
        return;
    }

    char responseCode[4]{};

    if (m_Response.header.size() >= 12)
    {
        responseCode[0] = m_Response.header[9];
        responseCode[1] = m_Response.header[10];
        responseCode[2] = m_Response.header[11];
    }

    m_Response.response_code = static_cast<std::uint32_t>(std::atoi(responseCode));
    m_Response.response = std::move(response);
    m_Response.response_len = static_cast<std::uint32_t>(m_Response.response.size());

    // R5 defaults to HTML if Content-Type is absent.
    m_Response.content_type = CONTENT_TYPE_HTML;
    m_hasResponse = true;

    char contentType[256]{};

    if (GetHeaderValue("CONTENT-TYPE:", contentType, sizeof(contentType)))
    {
        // Keep R5's case-sensitive value checks.
        if (std::strstr(contentType, "text/html"))
        {
            m_Response.content_type = CONTENT_TYPE_HTML;
        }
        else if (std::strstr(contentType, "text/plain"))
        {
            m_Response.content_type = CONTENT_TYPE_TEXT;
        }
        else
        {
            m_Response.content_type = CONTENT_TYPE_UNKNOWN;
        }
    }
}

void CHttpClient::Process()
{
    if (!Connect(m_Request.host.c_str(), m_Request.port))
        return;

    std::string request;

    switch (m_Request.rtype)
    {
        case HTTP_GET:
            request = BuildGetRequest(m_Request);
            break;

        case HTTP_POST:
            request = BuildPostRequest(m_Request);
            break;

        case HTTP_HEAD:
            request = BuildHeadRequest(m_Request);
            break;

        default:
            Disconnect();
            m_iError = HTTP_ERROR_CANT_WRITE;
            return;
    }

    if (!SendView(request))
    {
        Disconnect();
        return;
    }

    HandleEntity();
}

void CHttpClient::Reset()
{
    Disconnect();

    m_Request = {};
    m_Response = {};
    m_iError = HTTP_SUCCESS;
    m_hasResponse = false;
}

const char* CHttpClient::StrIStr(const char* haystack, const char* needle)
{
    if (!haystack || !needle)
        return nullptr;

    if (!*needle)
        return haystack;

    const std::size_t needleLength = std::strlen(needle);

    for (const char* current = haystack; *current; ++current)
    {
        std::size_t index = 0;

        while (index < needleLength)
        {
            const unsigned char left = static_cast<unsigned char>(current[index]);
            const unsigned char right = static_cast<unsigned char>(needle[index]);

            if (!left || std::tolower(left) != std::tolower(right))
                break;

            ++index;
        }

        if (index == needleLength)
            return current;
    }

    return nullptr;
}

std::string CHttpClient::BuildGetRequest(const HTTP_REQUEST& request)
{
    std::string result;
    result.reserve(request.file.size() + request.host.size() + request.referer.size() + 128);

    result += "GET ";
    result += request.file;
    result += " HTTP/1.0\r\nAccept: */*\r\nUser-Agent: ";
    result += USER_AGENT;
    result += "\r\nReferer: http://";
    result += request.referer;
    result += "\r\nHost: ";
    result += request.host;
    result += "\r\n\r\n";

    return result;
}

std::string CHttpClient::BuildPostRequest(const HTTP_REQUEST& request)
{
    std::string result;
    result.reserve(request.file.size() + request.host.size() + request.referer.size() + request.data.size() + 192);

    result += "POST ";
    result += request.file;
    result += " HTTP/1.0\r\nAccept: */*\r\nUser-Agent: ";
    result += USER_AGENT;
    result += "\r\nReferer: http://";
    result += request.referer;
    result += "\r\nHost: ";
    result += request.host;
    result += "\r\nContent-type: application/x-www-form-urlencoded\r\nContent-length: ";
    result += std::to_string(request.data.size());
    result += "\r\n\r\n";
    result += request.data;

    return result;
}

std::string CHttpClient::BuildHeadRequest(const HTTP_REQUEST& request)
{
    std::string result;
    result.reserve(request.file.size() + request.host.size() + request.referer.size() + 128);

    result += "HEAD ";
    result += request.file;
    result += " HTTP/1.0\r\nAccept: */*\r\nUser-Agent: ";
    result += USER_AGENT;
    result += "\r\nReferer: http://";
    result += request.referer;
    result += "\r\nHost: ";
    result += request.host;
    result += "\r\n\r\n";

    return result;
}
