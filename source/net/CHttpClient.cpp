/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>
#include "CHttpClient.h"
#include <curl/curl.h>
#include <io.h>
#include <memory>

struct CHttpClient::Transfer
{
    CHttpClient& client;
    FILE* file = nullptr;
    bool uploading = false;
    std::uint64_t size = 0;
    std::uint64_t received = 0;
    std::uint64_t limit = MAX_ENTITY_LENGTH;
    bool head = false;

    static std::size_t Write(char* data, std::size_t size, std::size_t count, void* context) noexcept
    {
        auto& transfer = *static_cast<Transfer*>(context);
        const auto bytes = size * count;
        if (bytes > transfer.limit - transfer.received)
        {
            transfer.client.m_error = ERROR_TOO_BIG_CONTENT;
            return 0;
        }
        try
        {
            if (transfer.file && !transfer.uploading)
            {
                if (std::fwrite(data, 1, bytes, transfer.file) != bytes)
                {
                    transfer.client.m_error = ERROR_FILE_IO;
                    return 0;
                }
            }
            else
                transfer.client.m_Response.response.append(data, bytes);
            transfer.received += bytes;
            return bytes;
        }
        catch (...)
        {
            transfer.client.m_error = ERROR_CANNOT_WRITE;
            return 0;
        }
    }
    static std::size_t Header(char* data, std::size_t size, std::size_t count, void* context) noexcept
    {
        auto& transfer = *static_cast<Transfer*>(context);
        const auto bytes = size * count;
        try
        {
            auto& header = transfer.client.m_Response.header;
            if (bytes >= 5 && std::memcmp(data, "HTTP/", 5) == 0)
            {
                header.clear();
                // Keep only the final response when curl follows redirects.
                transfer.client.m_Response.response.clear();
                transfer.received = 0;
                if (transfer.file && !transfer.uploading && (_fseeki64(transfer.file, 0, SEEK_SET) != 0 || _chsize_s(_fileno(transfer.file), 0) != 0))
                {
                    transfer.client.m_error = ERROR_FILE_IO;
                    return 0;
                }
                if (transfer.file && !transfer.uploading && _fseeki64(transfer.file, 0, SEEK_SET) != 0)
                {
                    transfer.client.m_error = ERROR_FILE_IO;
                    return 0;
                }
            }
            if (header.size() + bytes > 65536)
            {
                transfer.client.m_error = ERROR_TOO_BIG_CONTENT;
                return 0;
            }
            header.append(data, bytes);
            return bytes;
        }
        catch (...)
        {
            transfer.client.m_error = ERROR_CANNOT_WRITE;
            return 0;
        }
    }
    static std::size_t Read(char* data, std::size_t size, std::size_t count, void* context) noexcept
    {
        auto& transfer = *static_cast<Transfer*>(context);
        const auto bytes = std::fread(data, 1, size * count, transfer.file);
        if (std::ferror(transfer.file))
        {
            transfer.client.m_error = ERROR_FILE_IO;
            return CURL_READFUNC_ABORT;
        }
        return bytes;
    }
    static int Seek(void* context, curl_off_t offset, int origin) noexcept
    {
        return _fseeki64(static_cast<Transfer*>(context)->file, offset, origin) == 0 ? CURL_SEEKFUNC_OK : CURL_SEEKFUNC_FAIL;
    }
    static int Progress(void* context, curl_off_t downloadTotal, curl_off_t downloaded, curl_off_t uploadTotal, curl_off_t uploaded) noexcept
    {
        auto& transfer = *static_cast<Transfer*>(context);
        auto& client = transfer.client;
        client.m_total.store(static_cast<std::uint64_t>(transfer.uploading ? uploadTotal : downloadTotal), std::memory_order_relaxed);
        client.m_transferred.store(static_cast<std::uint64_t>(transfer.uploading ? uploaded : downloaded), std::memory_order_relaxed);
        if (!transfer.uploading && !transfer.head && downloadTotal > 0 && static_cast<std::uint64_t>(downloadTotal) > transfer.limit)
        {
            client.m_error = ERROR_TOO_BIG_CONTENT;
            return 1;
        }
        if (client.m_cancelled.load(std::memory_order_relaxed))
        {
            client.m_error = ERROR_REQUEST_CANCELLED;
            return 1;
        }
        return 0;
    }
};

CHttpClient::CHttpClient() : m_initialized(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK)
{}
CHttpClient::~CHttpClient()
{
    if (m_initialized)
        curl_global_cleanup();
}
void CHttpClient::Reset()
{
    m_Response = {};
    m_error = ERROR_OK;
    m_errorText.clear();
    m_hasResponse = false;
    m_cancelled.store(false, std::memory_order_relaxed);
    m_transferred.store(0, std::memory_order_relaxed);
    m_total.store(0, std::memory_order_relaxed);
}
CHttpClient::ErrorCode CHttpClient::ProcessUrl(int type, const char* url, const char* data, const char* referer)
{
    Reset();
    Transfer transfer{*this};
    return Perform(type, url, data, referer, transfer, "application/x-www-form-urlencoded");
}
CHttpClient::ErrorCode CHttpClient::DownloadFile(const char* url, const std::filesystem::path& destination, std::uint64_t maxBytes)
{
    Reset();
    auto temporary = destination;
    temporary += L".part";
    FILE* file = nullptr;
    if (_wfopen_s(&file, temporary.c_str(), L"wb") != 0)
        return m_error = ERROR_FILE_IO;
    Transfer transfer{*this, file, false, 0, 0, maxBytes};
    Perform(HTTP_GET, url, "", "", transfer, "");
    if (std::fclose(file) != 0 && m_error == ERROR_OK)
        m_error = ERROR_FILE_IO;
    if (m_error == ERROR_OK && (m_Response.response_code < 200 || m_Response.response_code >= 300))
        m_error = ERROR_BAD_HTTP_STATUS;
    if (m_error == ERROR_OK && !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        m_error = ERROR_FILE_IO;
    if (m_error != ERROR_OK)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return m_error;
}
CHttpClient::ErrorCode CHttpClient::UploadFile(const char* url, const std::filesystem::path& path, const char* contentType, int type)
{
    Reset();
    if (type != HTTP_POST && type != HTTP_PUT)
        return m_error = ERROR_CANNOT_WRITE;
    FILE* raw = nullptr;
    if (_wfopen_s(&raw, path.c_str(), L"rb") != 0)
        return m_error = ERROR_FILE_IO;
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(raw, &std::fclose);
    if (_fseeki64(raw, 0, SEEK_END) != 0)
        return m_error = ERROR_FILE_IO;
    const auto length = _ftelli64(raw);
    if (length < 0 || _fseeki64(raw, 0, SEEK_SET) != 0)
        return m_error = ERROR_FILE_IO;
    Transfer transfer{*this, raw, true, static_cast<std::uint64_t>(length)};
    Perform(type, url, "", "", transfer, contentType);
    if (m_error == ERROR_OK && (m_Response.response_code < 200 || m_Response.response_code >= 300))
        m_error = ERROR_BAD_HTTP_STATUS;
    return m_error;
}
CHttpClient::ErrorCode CHttpClient::Perform(int type, const char* url, const char* data, const char* referer, Transfer& transfer, const char* contentType)
{
    ZoneScoped;
    transfer.head = type == HTTP_HEAD;
    if (!m_initialized)
        return m_error = ERROR_NO_SOCKET;
    if (!url || !*url || (referer && std::strpbrk(referer, "\r\n")) || (contentType && std::strpbrk(contentType, "\r\n")))
        return m_error = ERROR_BAD_HOST;
    if (type != HTTP_GET && type != HTTP_POST && type != HTTP_HEAD && type != HTTP_PUT)
        return m_error = ERROR_CANNOT_WRITE;

    const std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> parsed(curl_url(), &curl_url_cleanup);
    const std::string address = std::strstr(url, "://") ? url : std::string("http://") + url;
    if (!parsed || curl_url_set(parsed.get(), CURLUPART_URL, address.c_str(), 0) != CURLUE_OK)
        return m_error = ERROR_BAD_HOST;
    char* scheme = nullptr;
    if (curl_url_get(parsed.get(), CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK)
        return m_error = ERROR_BAD_HOST;
    const bool https = _stricmp(scheme, "https") == 0;
    const bool allowed = _stricmp(scheme, "http") == 0 || https;
    curl_free(scheme);
    if (!allowed)
        return m_error = ERROR_BAD_HOST;

    const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(), &curl_easy_cleanup);
    if (!easy)
        return m_error = ERROR_NO_SOCKET;
    char errors[CURL_ERROR_SIZE]{};
    CURLcode result = CURLE_OK;
    auto set = [&](CURLoption option, auto value) {
        if (result == CURLE_OK)
            result = curl_easy_setopt(easy.get(), option, value);
    };
    set(CURLOPT_CURLU, parsed.get());
    set(CURLOPT_PROTOCOLS_STR, "http,https");
    set(CURLOPT_REDIR_PROTOCOLS_STR, https ? "https" : "http,https");
    set(CURLOPT_SSL_VERIFYPEER, 1L);
    set(CURLOPT_SSL_VERIFYHOST, 2L);
    set(CURLOPT_FOLLOWLOCATION, transfer.uploading ? 0L : 1L);
    set(CURLOPT_MAXREDIRS, 5L);
    set(CURLOPT_CONNECTTIMEOUT, 10L);
    set(CURLOPT_TIMEOUT, 120L);
    set(CURLOPT_LOW_SPEED_LIMIT, 1L);
    set(CURLOPT_LOW_SPEED_TIME, 30L);
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_USERAGENT, "SAMP/0.2.0");
    set(CURLOPT_ERRORBUFFER, errors);
    set(CURLOPT_WRITEFUNCTION, &Transfer::Write);
    set(CURLOPT_WRITEDATA, &transfer);
    set(CURLOPT_HEADERFUNCTION, &Transfer::Header);
    set(CURLOPT_HEADERDATA, &transfer);
    set(CURLOPT_XFERINFOFUNCTION, &Transfer::Progress);
    set(CURLOPT_XFERINFODATA, &transfer);
    set(CURLOPT_NOPROGRESS, 0L);
    if (referer && *referer)
        set(CURLOPT_REFERER, referer);
    if (type == HTTP_HEAD)
        set(CURLOPT_NOBODY, 1L);
    if (type == HTTP_POST)
        set(CURLOPT_POST, 1L);
    if (type == HTTP_PUT)
    {
        if (transfer.uploading)
            set(CURLOPT_UPLOAD, 1L);
        else
            set(CURLOPT_CUSTOMREQUEST, "PUT");
    }

    curl_slist* headers = nullptr;
    if (type == HTTP_POST || type == HTTP_PUT)
    {
        const std::string header = std::string("Content-Type: ") + (contentType ? contentType : "application/octet-stream");
        headers = curl_slist_append(nullptr, header.c_str());
        if (!headers)
            return m_error = ERROR_CANNOT_WRITE;
        set(CURLOPT_HTTPHEADER, headers);
        if (transfer.uploading)
        {
            set(CURLOPT_READFUNCTION, &Transfer::Read);
            set(CURLOPT_READDATA, &transfer);
            set(CURLOPT_SEEKFUNCTION, &Transfer::Seek);
            set(CURLOPT_SEEKDATA, &transfer);
            if (type == HTTP_POST)
                set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(transfer.size));
            else
                set(CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(transfer.size));
        }
        else
        {
            set(CURLOPT_POSTFIELDS, data ? data : "");
            set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(data ? std::strlen(data) : 0));
        }
    }
    if (result == CURLE_OK)
        result = curl_easy_perform(easy.get());
    curl_slist_free_all(headers);
    long status = 0;
    curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &status);
    m_Response.response_code = static_cast<std::uint32_t>(status);
    m_Response.header_len = static_cast<std::uint32_t>(m_Response.header.size());
    m_Response.response_len = static_cast<std::uint32_t>(m_Response.response.size());
    char* mime = nullptr;
    curl_easy_getinfo(easy.get(), CURLINFO_CONTENT_TYPE, &mime);
    if (mime && _strnicmp(mime, "text/html", 9) == 0)
        m_Response.content_type = CONTENT_TYPE_HTML;
    else if (mime && _strnicmp(mime, "text/", 5) == 0)
        m_Response.content_type = CONTENT_TYPE_TEXT;
    m_hasResponse = status != 0;
    if (result != CURLE_OK && m_error == ERROR_OK)
    {
        switch (result)
        {
            case CURLE_COULDNT_RESOLVE_HOST:
                m_error = ERROR_BAD_HOST;
                break;
            case CURLE_OPERATION_TIMEDOUT:
                m_error = ERROR_REQUEST_TIMEOUT;
                break;
            case CURLE_ABORTED_BY_CALLBACK:
                m_error = ERROR_REQUEST_CANCELLED;
                break;
            case CURLE_PEER_FAILED_VERIFICATION:
            case CURLE_SSL_CONNECT_ERROR:
                m_error = ERROR_TLS_FAILED;
                break;
            case CURLE_COULDNT_CONNECT:
                m_error = ERROR_CANNOT_CONNECT;
                break;
            case CURLE_SEND_ERROR:
            case CURLE_READ_ERROR:
            case CURLE_WRITE_ERROR:
                m_error = ERROR_CANNOT_WRITE;
                break;
            default:
                m_error = ERROR_INCORRECT_RESPONSE;
                break;
        }
    }
    if (result != CURLE_OK)
        m_errorText = errors[0] ? errors : curl_easy_strerror(result);
    return m_error;
}

bool CHttpClient::GetHeaderValue(const char* headerName, char* returnBuffer, int bufferSize)
{
    ZoneScoped;
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
