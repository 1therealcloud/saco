/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

#define HTTP_GET 1
#define HTTP_POST 2
#define HTTP_HEAD 3
#define HTTP_PUT 4
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
        ERROR_OK = HTTP_SUCCESS,
        ERROR_BAD_HOST = HTTP_ERROR_BAD_HOST,
        ERROR_NO_SOCKET = HTTP_ERROR_NO_SOCKET,
        ERROR_CANNOT_CONNECT = HTTP_ERROR_CANT_CONNECT,
        ERROR_CANNOT_WRITE = HTTP_ERROR_CANT_WRITE,
        ERROR_TOO_BIG_CONTENT = HTTP_ERROR_CONTENT_TOO_BIG,
        ERROR_INCORRECT_RESPONSE = HTTP_ERROR_MALFORMED_RESPONSE,
        ERROR_REQUEST_TIMEOUT,
        ERROR_REQUEST_CANCELLED,
        ERROR_FILE_IO,
        ERROR_BAD_HTTP_STATUS,
        ERROR_TLS_FAILED
    };

    CHttpClient();
    ~CHttpClient();
    CHttpClient(const CHttpClient&) = delete;
    CHttpClient& operator=(const CHttpClient&) = delete;

    // Requests block; run transfers on a worker thread, never the game loop.
    ErrorCode ProcessUrl(int type, const char* url, const char* data = "", const char* referer = "");
    ErrorCode DownloadFile(const char* url, const std::filesystem::path& destination, std::uint64_t maxBytes);
    ErrorCode UploadFile(const char* url, const std::filesystem::path& file, const char* contentType = "application/octet-stream", int type = HTTP_POST);
    int ProcessURL(int type, const char* url, const char* data = "", const char* referer = "")
    {
        return ProcessUrl(type, url, data, referer);
    }
    bool GetHeaderValue(const char* name, char* buffer, int size);
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
    const char* GetResponseHeaders() const
    {
        return m_hasResponse ? m_Response.header.c_str() : nullptr;
    }
    char* GetDocument()
    {
        return m_hasResponse ? m_Response.response.data() : nullptr;
    }
    const char* GetDocument() const
    {
        return m_hasResponse ? m_Response.response.c_str() : nullptr;
    }
    int GetDocumentLength() const
    {
        return static_cast<int>(m_Response.response_len);
    }
    ErrorCode GetError() const
    {
        return m_error;
    }
    const char* GetErrorText() const
    {
        return m_errorText.c_str();
    }

    // Configure before starting a transfer; the cancellation flag must outlive it.
    void SetCancellationFlag(const std::atomic<bool>* flag)
    {
        m_externalCancellation = flag;
    }
    void SetUserAgent(std::string agent)
    {
        m_userAgent = std::move(agent);
    }
    void SetProxy(std::string proxy)
    {
        m_proxy = std::move(proxy);
    }
    // These are safe to access while another thread performs the transfer.
    void Cancel()
    {
        m_cancelled.store(true, std::memory_order_relaxed);
    }
    std::uint64_t GetTransferredBytes() const
    {
        return m_transferred.load(std::memory_order_relaxed);
    }
    std::uint64_t GetTotalBytes() const
    {
        return m_total.load(std::memory_order_relaxed);
    }

private:
    struct Transfer;
    ErrorCode Perform(int type, const char* url, const char* data, const char* referer, Transfer& transfer, const char* contentType);
    void Reset();
    HTTP_RESPONSE m_Response;
    ErrorCode m_error = ERROR_OK;
    std::string m_errorText;
    std::string m_proxy;
    std::string m_userAgent{"SAMP/0.2.0"};
    const std::atomic<bool>* m_externalCancellation{};
    bool m_initialized = false;
    bool m_hasResponse = false;
    std::atomic<bool> m_cancelled{false};
    std::atomic<std::uint64_t> m_transferred{0};
    std::atomic<std::uint64_t> m_total{0};
};
