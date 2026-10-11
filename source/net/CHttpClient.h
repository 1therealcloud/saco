#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#   define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// SA-MP-compatible request types.
#define HTTP_GET 1
#define HTTP_POST 2
#define HTTP_HEAD 3

// R5 uses the same 500000-byte Content-Length limit.
#define MAX_ENTITY_LENGTH 500000

#define HTTP_SUCCESS 0
#define HTTP_ERROR_BAD_HOST 1
#define HTTP_ERROR_NO_SOCKET 2
#define HTTP_ERROR_CANT_CONNECT 3
#define HTTP_ERROR_CANT_WRITE 4
#define HTTP_ERROR_CONTENT_TOO_BIG 5
#define HTTP_ERROR_MALFORMED_RESPONSE 6

#define CONTENT_TYPE_UNKNOWN 0
#define CONTENT_TYPE_TEXT 1
#define CONTENT_TYPE_HTML 2

struct HTTP_REQUEST
{
    std::uint16_t port = 80;
    int rtype = HTTP_GET;
    std::string host;
    std::string file;
    std::string data;
    std::string referer;
};

struct HTTP_RESPONSE
{
    std::string header;
    std::string response;
    std::uint32_t header_len = 0;
    std::uint32_t response_len = 0;
    std::uint32_t response_code = 0;
    std::uint32_t content_type = CONTENT_TYPE_UNKNOWN;
};

class CHttpClient
{
public:
    enum ErrorCode
    {
        ERROR_OK = HTTP_SUCCESS, // R5 name is ERROR_SUCCESS; renamed to avoid the WinAPI macro
        ERROR_BAD_HOST = HTTP_ERROR_BAD_HOST,
        ERROR_NO_SOCKET = HTTP_ERROR_NO_SOCKET,
        ERROR_CANNOT_CONNECT = HTTP_ERROR_CANT_CONNECT,
        ERROR_CANNOT_WRITE = HTTP_ERROR_CANT_WRITE,
        ERROR_TOO_BIG_CONTENT = HTTP_ERROR_CONTENT_TOO_BIG,
        ERROR_INCORRECT_RESPONSE = HTTP_ERROR_MALFORMED_RESPONSE
    };

    CHttpClient();
    ~CHttpClient();

    CHttpClient(const CHttpClient&) = delete;
    CHttpClient& operator=(const CHttpClient&) = delete;

    // R5-facing API.
    ErrorCode ProcessUrl(int type, const char* url, const char* data = "", const char* referer = "");
    bool GetHeaderValue(const char* headerName, char* returnBuffer, int bufferSize);

    // Compatibility aliases retained for existing SACO callers.
    int ProcessURL(int type, char* url, char* data, char* referer);

    int GetResponseCode() const
    {
        return static_cast<int>(m_Response.response_code);
    }

    int GetContentType() const
    {
        return static_cast<int>(m_Response.content_type);
    }

    char* GetResponseHeaders()
    {
        return m_hasResponse ? m_Response.header.data() : nullptr;
    }

    char* GetDocument()
    {
        return m_hasResponse ? m_Response.response.data() : nullptr;
    }

    int GetDocumentLength() const
    {
        return static_cast<int>(m_Response.response_len);
    }

    int ProcessURL(int type, const char* url, const char* data = "", const char* referer = "")
    {
        return static_cast<int>(ProcessUrl(type, url, data, referer));
    }

    const char* GetResponseHeaders() const
    {
        return m_hasResponse ? m_Response.header.c_str() : nullptr;
    }

    const char* GetDocument() const
    {
        return m_hasResponse ? m_Response.response.c_str() : nullptr;
    }

    ErrorCode GetError() const
    {
        return static_cast<ErrorCode>(m_iError);
    }

private:
    static constexpr std::size_t RECV_BUFFER_SIZE = 0x1000;

    // This looks old, but it is the literal used by SA-MP 0.3.7-R5
    // at samp.dll + 0xE57A8 and referenced by CHttpClient::Process().
    static constexpr std::string_view USER_AGENT = "SAMP/0.2.0";

public:
    void InitializeRequest(int type, const char* url, const char* postData, const char* referer);
    void HandleEntity();
    bool Connect(const char* host, int port);
    void Process();
    void Disconnect();
    bool Send(const char* buffer);
    int Receive(char* buffer, int bufferSize);

private:
    bool SendView(std::string_view data);
    void Reset();

    static const char* StrIStr(const char* haystack, const char* needle);
    static std::string BuildGetRequest(const HTTP_REQUEST& request);
    static std::string BuildPostRequest(const HTTP_REQUEST& request);
    static std::string BuildHeadRequest(const HTTP_REQUEST& request);

private:
    SOCKET m_iSocket = INVALID_SOCKET;
    HTTP_REQUEST m_Request;
    HTTP_RESPONSE m_Response;
    int m_iError = HTTP_SUCCESS;

    bool m_initialized = false;
    bool m_hasResponse = false;
};
