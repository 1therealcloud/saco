/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "CCustomModels.h"

#include <chrono>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <cstdlib>
#include <cerrno>
#include <LzmaDec.h>
#include "net/CHttpClient.h"
#include "game/render/models.h"
#include "game/ModelInfo.h"
#include <cstdio>
#include <charconv>
#include <unordered_set>

namespace
{
    std::uint64_t FileKey(DownloadedFileType type, std::uint32_t crc)
    {
        return (static_cast<std::uint64_t>(type) << 32) | crc;
    }
    bool ValidType(DownloadedFileType type)
    {
        return type == DownloadedFileType::Dff || type == DownloadedFileType::Txd;
    }
    std::filesystem::path FileName(DownloadedFileType type, std::uint32_t crc)
    {
        if (!ValidType(type))
            return {};
        char name[32]{};
        std::snprintf(name, sizeof(name), "0x%X.%s", crc, type == DownloadedFileType::Dff ? "dff" : "txd");
        return name;
    }
    bool ParseNumber(std::string_view text, std::uint32_t& number, int base)
    {
        if (text.empty())
            return false;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), number, base);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }
    bool ParseCrc(std::string_view text, std::uint32_t& crc)
    {
        return text.size() >= 3 && text.size() <= 10 && text.substr(0, 2) == "0x" && ParseNumber(text.substr(2), crc, 16);
    }
    bool ParseCacheName(std::string_view name, DownloadedFileType& type, std::uint32_t& crc)
    {
        const auto dot = name.find('.');
        if (dot == name.npos || !ParseCrc(name.substr(0, dot), crc))
            return false;
        const auto extension = name.substr(dot);
        if (_stricmp(std::string(extension).c_str(), ".dff") == 0)
            type = DownloadedFileType::Dff;
        else if (_stricmp(std::string(extension).c_str(), ".txd") == 0)
            type = DownloadedFileType::Txd;
        else
            return false;
        return true;
    }
    bool ParseTemporaryName(std::string_view name, DWORD& processId)
    {
        std::uint32_t crc{};
        if (name.ends_with(".temp") && ParseCrc(name.substr(0, name.size() - 5), crc))
        {
            processId = 0;
            return true;
        }
        const auto marker = name.find(".download.");
        DownloadedFileType type{};
        if (marker == name.npos || !ParseCacheName(name.substr(0, marker), type, crc))
            return false;
        auto suffix = name.substr(marker + 10);
        const auto dot = suffix.find('.');
        std::uint32_t pid{}, sequence{};
        if (dot == suffix.npos || !ParseNumber(suffix.substr(0, dot), pid, 10) || !pid)
            return false;
        suffix.remove_prefix(dot + 1);
        const auto part = suffix.find('.');
        if (!ParseNumber(suffix.substr(0, part), sequence, 10) || !sequence)
            return false;
        if (part != suffix.npos)
        {
            suffix.remove_prefix(part + 1);
            const auto separator = suffix.find('.');
            std::uint32_t httpPid{};
            if (separator == suffix.npos || !ParseNumber(suffix.substr(0, separator), httpPid, 10) || httpPid != pid || !suffix.ends_with(".part"))
                return false;
            suffix.remove_prefix(separator + 1);
            if (suffix.size() <= 5 || !ParseNumber(suffix.substr(0, suffix.size() - 5), sequence, 10))
                return false;
        }
        processId = pid;
        return true;
    }
    bool OtherProcessIsRunning(DWORD processId)
    {
        if (!processId || processId == GetCurrentProcessId())
            return false;
        const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
        if (!process)
            return GetLastError() != ERROR_INVALID_PARAMETER;
        const bool running = WaitForSingleObject(process, 0) != WAIT_OBJECT_0;
        CloseHandle(process);
        return running;
    }
    constexpr auto MakeCrcTable()
    {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t i = 0; i < table.size(); ++i)
        {
            auto value = i;
            for (int bit = 0; bit < 8; ++bit)
                value = (value >> 1) ^ ((value & 1) ? 0xEDB88320u : 0u);
            table[i] = value;
        }
        return table;
    }
    constexpr auto crcTable = MakeCrcTable();
    bool ValidateFile(const std::filesystem::path& path, std::uint32_t size, std::uint32_t crc, const std::atomic<bool>& cancel)
    {
        if (!size || size > CCustomModelsPool::MAX_FILE_SIZE || cancel.load(std::memory_order_relaxed))
            return false;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
            return false;
        std::FILE* raw = nullptr;
        if (_wfopen_s(&raw, path.c_str(), L"rb") != 0)
            return false;
        std::unique_ptr<std::FILE, DownloadedFileCloser> file(raw);
        if (_fseeki64(raw, 0, SEEK_END) != 0 || _ftelli64(raw) != size || _fseeki64(raw, 0, SEEK_SET) != 0)
            return false;
        std::array<unsigned char, 65536> buffer{};
        std::uint64_t total = 0;
        std::uint32_t running = 0xFFFFFFFFu;
        while (!cancel.load(std::memory_order_relaxed))
        {
            const auto count = std::fread(buffer.data(), 1, buffer.size(), raw);
            if (!count)
                return !std::ferror(raw) && total == size && (running ^ 0xFFFFFFFFu) == crc;
            total += count;
            if (total > size)
                return false;
            for (std::size_t i = 0; i < count; ++i)
                running = crcTable[(running ^ buffer[i]) & 255u] ^ (running >> 8);
        }
        return false;
    }
    bool CreateTemporaryFile(const std::filesystem::path& destination, DownloadedFileInfo& file)
    {
        static std::atomic<std::uint32_t> sequence{};
        for (int attempt = 0; attempt < 32; ++attempt)
        {
            auto path = destination;
            path += L".download." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++sequence);
            FILE* raw = nullptr;
            const auto error = _wfopen_s(&raw, path.c_str(), L"wbx");
            if (!error)
            {
                file.m_tempPath = std::move(path);
                file.m_pFile.reset(raw);
                return true;
            }
            if (error != EEXIST)
                break;
        }
        return false;
    }
    bool Applies(const CCustomModelInfo& model, std::int32_t world)
    {
        return model.m_nType == CustomModelType::Skin || model.m_nVirtualWorld == -1 || model.m_nVirtualWorld == world;
    }
}

struct CCustomModels::BinaryDownload
{
    struct Chunk
    {
        std::vector<Byte> bytes;
        std::uint32_t decodedSize;
    };
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Chunk> chunks;
    std::size_t queuedBytes{};
    bool finished{}, failed{};
    std::atomic<std::uint32_t> progress{};

    void Finish(bool success)
    {
        std::lock_guard lock(mutex);
        finished = true;
        failed |= !success;
        ready.notify_one();
    }
    bool Run(std::unique_ptr<std::FILE, DownloadedFileCloser> output, std::uint32_t expectedSize, std::uint32_t expectedCrc, const std::filesystem::path& temporary,
             const std::filesystem::path& destination, const std::atomic<bool>& cancelled)
    {
        ZoneScopedN("Binary DL decode and publish");
        std::uint32_t crc = 0xFFFFFFFFu, downloaded = 0;
        bool success = true;
        for (;;)
        {
            Chunk chunk;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [&] {
                    return finished || !chunks.empty();
                });
                if (failed || cancelled.load(std::memory_order_relaxed))
                {
                    success = false;
                    break;
                }
                if (chunks.empty())
                    break;
                chunk = std::move(chunks.front());
                queuedBytes -= chunk.bytes.size();
                chunks.pop_front();
            }
            std::array<Byte, 4096> decoded{};
            SizeT outputSize = chunk.decodedSize, inputSize = chunk.bytes.size();
            const Byte properties[] = {0x5D, 0, 0, 1, 0};
            const ISzAlloc allocator = {[](ISzAllocPtr, size_t bytes) -> void* {
                                            return std::malloc(bytes);
                                        },
                                        [](ISzAllocPtr, void* memory) {
                                            std::free(memory);
                                        }};
            ELzmaStatus status{};
            const auto result = LzmaDecode(decoded.data(), &outputSize, chunk.bytes.data(), &inputSize, properties, sizeof(properties), LZMA_FINISH_ANY, &status, &allocator);
            if ((result != SZ_OK && result != SZ_ERROR_INPUT_EOF) || outputSize != chunk.decodedSize || outputSize > expectedSize - downloaded ||
                std::fwrite(decoded.data(), 1, outputSize, output.get()) != outputSize)
            {
                success = false;
                break;
            }
            for (std::size_t i = 0; i < outputSize; ++i)
                crc = crcTable[(crc ^ decoded[i]) & 255u] ^ (crc >> 8);
            downloaded += static_cast<std::uint32_t>(outputSize);
            progress.store(downloaded, std::memory_order_relaxed);
        }
        success = success && downloaded == expectedSize && (crc ^ 0xFFFFFFFFu) == expectedCrc;
        success = std::fflush(output.get()) == 0 && success;
        success = std::fclose(output.release()) == 0 && success;
        if (success && !cancelled.load(std::memory_order_relaxed))
            success = MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        else
            success = false;
        std::error_code error;
        std::filesystem::remove(temporary, error);
        return success;
    }
};

CCustomModels::CCustomModels() = default;
CCustomModels::~CCustomModels()
{
    Reset();
}

void CCustomModels::MakeSafeDirectoryName(char* destination, std::size_t capacity, const char* source)
{
    if (!destination || !capacity)
        return;
    std::size_t length = 0;
    if (source)
        for (const auto* current = reinterpret_cast<const unsigned char*>(source); *current && length + 1 < capacity; ++current)
            if (*current == '.' || (*current >= 'a' && *current <= 'z') || (*current >= 'A' && *current <= 'Z') || (*current >= '0' && *current <= '9'))
                destination[length++] = static_cast<char>(*current);
    destination[length] = '\0';
}
bool CCustomModels::InitDirectory(const std::filesystem::path& root, std::string_view host, std::uint16_t port)
{
    if (root.empty() || host.empty() || !port)
        return false;
    char name[26]{};
    const std::string source(host);
    MakeSafeDirectoryName(name, sizeof(name), source.c_str());
    const std::string safeHost(name);
    if (safeHost.empty())
        return false;
    const auto server = root / (safeHost + "." + std::to_string(port));
    if (m_bPathInitialized && m_serverCachePath == server)
        return true;
    Reset();
    m_bPathInitialized = false;
    std::error_code error;
    std::filesystem::create_directories(server, error);
    if (error)
        return false;
    std::filesystem::create_directories(root / "local", error);
    if (error)
        return false;
    m_serverCachePath = server;
    m_localCachePath = root / "local";
    m_bPathInitialized = true;
    return true;
}
void CCustomModels::Reset()
{
    CancelDownloads();
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (const auto* model = GetModelInfo(i))
            game::render::models::unload_custom(model->m_nCustomModelId);
    m_cancelCacheCheck.store(true, std::memory_order_relaxed);
    if (m_cacheCheck.valid())
    {
        m_cacheCheck.wait();
        m_cacheCheck = {};
    }
    m_failedLoads.clear();
    m_loadedModels.clear();
    m_modelsPool.Clear();
    m_nExpectedModelsCount = 0;
    m_filesPool.Clear();
    m_cachedFiles.clear();
    m_nVirtualWorld = 0;
    m_poolRevision = m_modelsPool.GetRevision();
    ++m_cacheRevision;
    m_cacheDirty = true;
    m_cacheReady = false;
    m_cacheFailed = false;
}
void CCustomModels::SetVirtualWorld(std::int32_t world)
{
    if (m_nVirtualWorld == world)
        return;
    m_nVirtualWorld = world;
    RefreshCache();
}
void CCustomModels::RefreshCache()
{
    m_failedLoads.clear();
    CancelDownloads();
    ++m_cacheRevision;
    m_cacheDirty = true;
    m_cacheReady = false;
    m_cacheFailed = false;
    m_cancelCacheCheck.store(true, std::memory_order_relaxed);
    m_filesPool.Clear();
}
bool CCustomModels::IsCacheReady() const
{
    return m_cacheReady && !m_cacheDirty && m_poolRevision == m_modelsPool.GetRevision();
}
std::filesystem::path CCustomModels::GetCacheFilePath(DownloadedFileType type, std::uint32_t crc) const
{
    return m_bPathInitialized && ValidType(type) ? m_serverCachePath / FileName(type, crc) : std::filesystem::path{};
}
std::filesystem::path CCustomModels::GetCachedFilePath(DownloadedFileType type, std::uint32_t crc) const
{
    if (!IsCacheReady())
        return {};
    const auto found = m_cachedFiles.find(FileKey(type, crc));
    return found != m_cachedFiles.end() ? found->second.path : std::filesystem::path{};
}
CCustomModelInfo* CCustomModels::GetModelInfo(std::uint32_t index)
{
    return m_modelsPool.Get(index);
}
std::uint32_t CCustomModels::GetCount() const
{
    return m_modelsPool.GetCount();
}
bool CCustomModels::ResizePool(std::uint32_t count)
{
    const auto revision = m_modelsPool.GetRevision();
    if (!m_modelsPool.Resize(count))
        return false;
    m_nExpectedModelsCount = count;
    if (m_modelsPool.GetRevision() != revision)
        RefreshCache();
    return true;
}
bool CCustomModels::ShrinkPool()
{
    auto count = GetCount();
    while (count && !GetModelInfo(count - 1))
        --count;
    return ResizePool(count);
}
bool CCustomModels::RemoveModelInfo(std::uint32_t index)
{
    if (!m_modelsPool.Remove(index))
        return false;
    ShrinkPool();
    RefreshCache();
    return true;
}
bool CCustomModels::PushBack(std::uint32_t index, const CCustomModelInfo* info)
{
    return SetModelInfo(index, info) >= 0;
}
void CCustomModels::SetExpectedModelsCount(std::uint32_t count)
{
    if (count <= CCustomModelsPool::MAX_MODELS && count >= GetCount())
        m_nExpectedModelsCount = count;
}
std::int32_t CCustomModels::SetModelInfo(std::uint32_t index, const CCustomModelInfo* info)
{
    if (!info || index >= CCustomModelsPool::MAX_MODELS)
        return -1;
    const auto count = std::max({GetCount(), m_nExpectedModelsCount, index + 1});
    return m_modelsPool.Set(index, count, *info) ? static_cast<std::int32_t>(index) : -1;
}
std::int32_t CCustomModels::DeleteModelInfo(std::uint32_t index)
{
    return RemoveModelInfo(index) ? static_cast<std::int32_t>(index) : -1;
}
std::int32_t CCustomModels::FindModelByFile(DownloadedFileType type, std::uint32_t crc) const
{
    if (!ValidType(type))
        return -1;
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (const auto* model = m_modelsPool.Get(i); model && Applies(*model, m_nVirtualWorld) && (type == DownloadedFileType::Dff ? model->m_nDffCrc : model->m_nTxdCrc) == crc)
            return static_cast<std::int32_t>(i);
    return -1;
}
bool CCustomModels::IsDffRequestPending(std::uint32_t crc) const
{
    return m_currentDownload && m_currentDownload->m_nType == DownloadedFileType::Dff && m_currentDownload->m_nExpectedCrc == crc;
}
bool CCustomModels::IsTxdRequestPending(std::uint32_t crc) const
{
    return m_currentDownload && m_currentDownload->m_nType == DownloadedFileType::Txd && m_currentDownload->m_nExpectedCrc == crc;
}
bool CCustomModels::IsFileQueued(DownloadedFileType type, std::uint32_t crc) const
{
    const auto* file = m_filesPool.Find(type, crc);
    return file && file->m_state != DownloadState::Complete && file->m_state != DownloadState::Failed;
}
bool CCustomModels::BuildDownloadedFileInfo(DownloadedFileType type, std::uint32_t crc, DownloadedFileInfo* output)
{
    if (!output || !ValidType(type))
        return false;
    const auto* queued = m_filesPool.Find(type, crc);
    const int index = queued ? static_cast<int>(queued->m_nModelIndex) : FindModelByFile(type, crc);
    const auto* model = index >= 0 ? GetModelInfo(static_cast<std::uint32_t>(index)) : nullptr;
    if (!model || !Applies(*model, m_nVirtualWorld) || (type == DownloadedFileType::Dff ? model->m_nDffCrc : model->m_nTxdCrc) != crc)
        return false;
    DownloadedFileInfo file{};
    file.m_nType = type;
    file.m_nExpectedCrc = crc;
    file.m_nFileSize = type == DownloadedFileType::Dff ? model->m_nDffSize : model->m_nTxdSize;
    file.m_nModelIndex = static_cast<std::uint32_t>(index);
    file.m_nRunningCrc = 0xFFFFFFFFu;
    file.m_bProgressDirty = true;
    *output = std::move(file);
    return true;
}
bool CCustomModels::DoesDffExist(std::uint32_t crc)
{
    return !GetCachedFilePath(DownloadedFileType::Dff, crc).empty();
}
bool CCustomModels::DoesTxdExist(std::uint32_t crc)
{
    return !GetCachedFilePath(DownloadedFileType::Txd, crc).empty();
}
bool CCustomModels::DeleteCachedFile(DownloadedFileType type, std::uint32_t crc)
{
    const auto path = GetCacheFilePath(type, crc);
    if (path.empty())
        return false;
    std::error_code error;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) || error)
        return false;
    RefreshCache();
    if (m_cacheCheck.valid())
    {
        m_cacheCheck.wait();
        m_cacheCheck = {};
    }
    return std::filesystem::remove(path, error) && !error;
}
bool CCustomModels::DeleteDff(std::uint32_t crc)
{
    return DeleteCachedFile(DownloadedFileType::Dff, crc);
}
bool CCustomModels::DeleteTxd(std::uint32_t crc)
{
    return DeleteCachedFile(DownloadedFileType::Txd, crc);
}
bool CCustomModels::DeleteTempFile(const char* fileName)
{
    if (!m_bPathInitialized || !fileName || !*fileName)
        return false;
    const std::string_view name(fileName);
    DWORD processId{};
    if (name.find_first_of("/\\:") != name.npos || !ParseTemporaryName(name, processId) || OtherProcessIsRunning(processId))
        return false;
    const auto path = m_serverCachePath / fileName;
    if (m_currentDownload && !m_currentDownload->m_tempPath.empty())
    {
        const auto active = m_currentDownload->m_tempPath.filename().u8string();
        const std::string_view activeName(reinterpret_cast<const char*>(active.data()), active.size());
        if (name == activeName || name.starts_with(std::string(activeName) + "."))
            return false;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) && !error && std::filesystem::remove(path, error) && !error;
}
std::uint32_t CCustomModels::ParseFileCrc(const char* fileName)
{
    if (!fileName)
        return 0;
    const std::string_view name(fileName);
    std::uint32_t crc{};
    return ParseCrc(name.substr(0, name.find('.')), crc) ? crc : 0;
}
bool CCustomModels::IsKnownFileCrc(std::uint32_t crc) const
{
    if (!m_modelsPool.IsReady())
        return true;
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (const auto* model = m_modelsPool.Get(i); model && (model->m_nDffCrc == crc || model->m_nTxdCrc == crc))
            return true;
    return false;
}
void CCustomModels::CleanupCache()
{
    if (!m_bPathInitialized || !m_modelsPool.IsReady() || m_cacheCheck.valid())
        return;
    std::error_code error;
    std::unordered_set<std::uint32_t> known;
    known.reserve(static_cast<std::size_t>(GetCount()) * 2);
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (const auto* model = m_modelsPool.Get(i))
        {
            known.insert(model->m_nDffCrc);
            known.insert(model->m_nTxdCrc);
        }
    std::filesystem::directory_iterator entry(m_serverCachePath, error), end;
    for (; !error && entry != end; entry.increment(error))
    {
        std::error_code fileError;
        if (!std::filesystem::is_regular_file(entry->symlink_status(fileError)) || fileError)
            continue;
        const auto utf8 = entry->path().filename().u8string();
        const std::string name(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        DWORD processId{};
        if (ParseTemporaryName(name, processId))
            DeleteTempFile(name.c_str());
        else
        {
            DownloadedFileType type{};
            std::uint32_t crc{};
            if (ParseCacheName(name, type, crc) && !known.contains(crc) && std::filesystem::remove(entry->path(), fileError) && !fileError)
                m_cachedFiles.erase(FileKey(type, crc));
        }
    }
}
std::int32_t CCustomModels::QueueMissingFiles()
{
    if (!IsCacheReady())
        return -1;
    CancelDownloads();
    m_filesPool.Clear();
    bool failed = false;
    for (std::uint32_t i = 0; i < GetCount(); ++i)
    {
        auto* model = m_modelsPool.Get(i);
        if (!model)
            continue;
        model->m_bDffQueued = model->m_bTxdQueued = false;
        if (!Applies(*model, m_nVirtualWorld))
            continue;
        if (model->m_bDownloadFailed)
        {
            failed = true;
            continue;
        }
        if (!model->m_bDffExists)
            model->m_bDffQueued = m_filesPool.Queue(DownloadedFileType::Dff, model->m_nDffCrc, model->m_nDffSize, i);
        if (!model->m_bTxdExists)
            model->m_bTxdQueued = m_filesPool.Queue(DownloadedFileType::Txd, model->m_nTxdCrc, model->m_nTxdSize, i);
        failed |= (!model->m_bDffExists && !model->m_bDffQueued) || (!model->m_bTxdExists && !model->m_bTxdQueued);
    }
    m_cacheFailed = failed;
    return failed ? -1 : static_cast<std::int32_t>(m_filesPool.GetCount());
}
std::int32_t CCustomModels::GetMissingFilesCount() const
{
    if (!IsCacheReady() || m_cacheFailed)
        return -1;
    std::int32_t count = 0;
    for (std::uint32_t i = 0; i < m_filesPool.GetCount(); ++i)
        if (const auto state = m_filesPool.Get(i)->m_state; state != DownloadState::Complete && state != DownloadState::Failed)
            ++count;
    return count;
}
void CCustomModels::Process()
{
    if (m_poolRevision != m_modelsPool.GetRevision())
    {
        m_poolRevision = m_modelsPool.GetRevision();
        RefreshCache();
    }
    ProcessLoadedModels();
    ProcessTransfer();
    if (m_cacheCheck.valid())
    {
        if (m_cacheCheck.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        std::vector<CachedFile> checked;
        try
        {
            checked = m_cacheCheck.get();
        }
        catch (const std::exception& error)
        {
#if defined(_DEBUG)
            Log("[dl:cache] check failed: %s", error.what());
#endif // _DEBUG
            m_cacheFailed = true;
            return;
        }
        if (m_jobRevision == m_cacheRevision)
        {
            m_cachedFiles.clear();
            for (auto& file : checked)
                m_cachedFiles.emplace(FileKey(file.type, file.crc), std::move(file));
            for (std::uint32_t i = 0; i < GetCount(); ++i)
            {
                auto* model = m_modelsPool.Get(i);
                if (!model || !Applies(*model, m_nVirtualWorld))
                    continue;
                const auto& dff = m_cachedFiles.at(FileKey(DownloadedFileType::Dff, model->m_nDffCrc));
                const auto& txd = m_cachedFiles.at(FileKey(DownloadedFileType::Txd, model->m_nTxdCrc));
                model->m_bDffExists = !dff.path.empty();
                model->m_bTxdExists = !txd.path.empty();
                model->m_bDownloadFailed = !dff.size || !txd.size;
            }
            m_cacheReady = true;
            QueueMissingFiles();
        }
    }
    if (!m_cacheDirty || !m_bPathInitialized || !m_modelsPool.IsReady())
        return;
    CleanupCache();
    std::unordered_map<std::uint64_t, CachedFile> files;
    for (std::uint32_t i = 0; i < GetCount(); ++i)
    {
        const auto* model = m_modelsPool.Get(i);
        if (!model || !Applies(*model, m_nVirtualWorld))
            continue;
        for (const auto type : {DownloadedFileType::Dff, DownloadedFileType::Txd})
        {
            const auto crc = type == DownloadedFileType::Dff ? model->m_nDffCrc : model->m_nTxdCrc;
            const auto size = type == DownloadedFileType::Dff ? model->m_nDffSize : model->m_nTxdSize;
            const auto [it, inserted] = files.emplace(FileKey(type, crc), CachedFile{type, crc, size, {}});
            if (!inserted && it->second.size != size)
                it->second.size = 0;
        }
    }
    const auto server = m_serverCachePath, local = m_localCachePath;
    m_jobRevision = m_cacheRevision;
    m_cacheDirty = false;
    m_cancelCacheCheck.store(false, std::memory_order_relaxed);
    try
    {
        m_cacheCheck = std::async(std::launch::async, [this, server, local, files = std::move(files)]() mutable {
            std::vector<CachedFile> result;
            result.reserve(files.size());
            for (auto& [key, file] : files)
            {
                if (m_cancelCacheCheck.load(std::memory_order_relaxed))
                    break;
                const auto name = FileName(file.type, file.crc);
                if (ValidateFile(server / name, file.size, file.crc, m_cancelCacheCheck))
                    file.path = server / name;
                else if (ValidateFile(local / name, file.size, file.crc, m_cancelCacheCheck))
                    file.path = local / name;
                result.push_back(std::move(file));
            }
            return result;
        });
    }
    catch (const std::exception& error)
    {
#if defined(_DEBUG)
        Log("[dl:cache] worker failed: %s", error.what());
#endif // _DEBUG
        m_cacheFailed = true;
    }
}

DownloadedFileInfo* CCustomModels::GetNextDownload()
{
    return IsCacheReady() && !m_currentDownload ? m_filesPool.GetNextQueued() : nullptr;
}

bool CCustomModels::BeginRequest(DownloadedFileType type, std::uint32_t crc)
{
    auto* file = m_filesPool.Find(type, crc);
    if (!IsCacheReady() || m_currentDownload || !IsFileQueued(type, crc) || file->m_state != DownloadState::Queued)
        return false;
    m_currentDownload = file;
    file->m_state = DownloadState::Requested;
    file->m_bProgressDirty = true;
    m_requestTime = GetTickCount();
    m_downloadAcknowledged = m_downloadConfirmed = false;
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (auto* model = GetModelInfo(i); model && (type == DownloadedFileType::Dff ? model->m_nDffCrc : model->m_nTxdCrc) == crc)
            (type == DownloadedFileType::Dff ? model->m_bDffRequestPending : model->m_bTxdRequestPending) = true;
    return true;
}
void CCustomModels::CancelDownloads()
{
    m_cancelDownload.store(true, std::memory_order_relaxed);
    if (m_httpClient)
        m_httpClient->Cancel();
    if (m_binaryDownload)
        m_binaryDownload->Finish(false);
    if (m_httpDownload.valid())
    {
        m_httpDownload.wait();
        m_httpDownload = {};
    }
    m_httpClient.reset();
    m_binaryDownload.reset();
    if (m_currentDownload)
    {
        m_currentDownload->m_pFile.reset();
        std::error_code error;
        if (!m_currentDownload->m_tempPath.empty())
            std::filesystem::remove(m_currentDownload->m_tempPath, error);
    }
    m_currentDownload = nullptr;
    m_nDownloadChunkSize = 0;
    m_downloadAcknowledged = m_downloadConfirmed = false;
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (auto* model = GetModelInfo(i))
        {
            model->m_bDffRequestPending = model->m_bTxdRequestPending = false;
            model->m_bDffQueued = model->m_bTxdQueued = false;
        }
}
bool CCustomModels::StartDownload(DownloadedFileType type, std::uint32_t crc)
{
    auto* file = m_currentDownload;
    if (!file || file->m_state != DownloadState::Requested || file->m_nType != type || file->m_nExpectedCrc != crc || !IsCacheReady())
        return false;
    if (!CreateTemporaryFile(GetCacheFilePath(type, crc), *file))
    {
        PublishDownload(false);
        return false;
    }
    m_cancelDownload.store(false, std::memory_order_relaxed);
    try
    {
        m_binaryDownload = std::make_shared<BinaryDownload>();
        m_httpDownload = std::async(std::launch::async, [this, worker = m_binaryDownload, output = std::move(file->m_pFile), size = file->m_nFileSize, crc,
                                                         temporary = file->m_tempPath, destination = GetCacheFilePath(type, crc)]() mutable {
            return worker->Run(std::move(output), size, crc, temporary, destination, m_cancelDownload);
        });
    }
    catch (...)
    {
        m_binaryDownload.reset();
        PublishDownload(false);
        return false;
    }
    m_binaryQueuedSize = 0;
    file->m_nRunningCrc = 0xFFFFFFFFu;
    file->m_nDownloadedSize = 0;
    file->m_state = DownloadState::Binary;
    m_nDownloadChunkSize = 0;
    m_requestTime = GetTickCount();
    return true;
}
void CCustomModels::ProcessDownloadChunk(std::uint8_t state, std::uint32_t bufferSize, const void* data, std::uint32_t size)
{
    auto* file = m_currentDownload;
    if (!file || file->m_state != DownloadState::Binary)
        return;
    if ((state != 1 && state != 2) || (!bufferSize && m_binaryQueuedSize != file->m_nFileSize) || bufferSize > 4096 || size > 4096 - m_nDownloadChunkSize || (size && !data))
    {
        FinishCurrentDownload(false);
        return;
    }
    if (size)
        std::memcpy(m_downloadChunkBuffer.data() + m_nDownloadChunkSize, data, size);
    m_nDownloadChunkSize += size;
    m_requestTime = GetTickCount();
    if (state == 1)
        return;
    bool accepted = false;
    if (m_binaryDownload && bufferSize <= file->m_nFileSize - m_binaryQueuedSize)
    {
        std::lock_guard lock(m_binaryDownload->mutex);
        if (!m_binaryDownload->finished && m_binaryDownload->chunks.size() < 8192 && m_binaryDownload->queuedBytes + m_nDownloadChunkSize <= 8 * 1024 * 1024)
        {
            m_binaryDownload->chunks.push_back({{m_downloadChunkBuffer.begin(), m_downloadChunkBuffer.begin() + m_nDownloadChunkSize}, bufferSize});
            m_binaryDownload->queuedBytes += m_nDownloadChunkSize;
            m_binaryQueuedSize += bufferSize;
            accepted = true;
        }
    }
    m_nDownloadChunkSize = 0;
    if (accepted)
        m_binaryDownload->ready.notify_one();
    else
        FinishCurrentDownload(false);
}
void CCustomModels::FinishCurrentDownload(bool success)
{
    if (!m_currentDownload || m_currentDownload->m_state == DownloadState::Http)
        return;
    if (m_binaryDownload)
        m_binaryDownload->Finish(success && !m_nDownloadChunkSize && m_binaryQueuedSize == m_currentDownload->m_nFileSize);
    else
        PublishDownload(false);
}

void CCustomModels::PublishDownload(bool success)
{
    auto* file = m_currentDownload;
    if (!file)
        return;
    file->m_pFile.reset();
    success = success && IsCacheReady();
    if (success && !m_binaryDownload)
        success = MoveFileExW(file->m_tempPath.c_str(), GetCacheFilePath(file->m_nType, file->m_nExpectedCrc).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    std::error_code error;
    if (!file->m_tempPath.empty())
        std::filesystem::remove(file->m_tempPath, error);
    file->m_state = success ? DownloadState::Complete : DownloadState::Failed;
    file->m_bProgressDirty = true;
    if (success)
        file->m_nDownloadedSize = file->m_nFileSize;
#if defined(_DEBUG)
    if (!success)
        Log("[dl:download] failed: type=%u crc=%08X bytes=%u/%u", static_cast<unsigned>(file->m_nType), file->m_nExpectedCrc, file->m_nDownloadedSize, file->m_nFileSize);
#endif // _DEBUG
    if (success)
        m_cachedFiles[FileKey(file->m_nType, file->m_nExpectedCrc)] = {file->m_nType, file->m_nExpectedCrc, file->m_nFileSize,
                                                                       GetCacheFilePath(file->m_nType, file->m_nExpectedCrc)};
    for (std::uint32_t i = 0; i < GetCount(); ++i)
    {
        auto* model = GetModelInfo(i);
        if (!model || (file->m_nType == DownloadedFileType::Dff ? model->m_nDffCrc : model->m_nTxdCrc) != file->m_nExpectedCrc)
            continue;
        (file->m_nType == DownloadedFileType::Dff ? model->m_bDffRequestPending : model->m_bTxdRequestPending) = false;
        (file->m_nType == DownloadedFileType::Dff ? model->m_bDffQueued : model->m_bTxdQueued) = false;
        (file->m_nType == DownloadedFileType::Dff ? model->m_bDffExists : model->m_bTxdExists) = success;
        if (!success)
            model->m_bDownloadFailed = true;
    }
    m_currentDownload = nullptr;
    m_nDownloadChunkSize = 0;
}
bool CCustomModels::StartHttpDownload(DownloadedFileType type, std::uint32_t crc, std::string url, std::string proxy)
{
    auto* file = m_currentDownload;
    if (!file || file->m_state != DownloadState::Requested || file->m_nType != type || file->m_nExpectedCrc != crc || !IsCacheReady())
        return false;
    if (url.find('\0') != std::string::npos || (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0))
    {
        PublishDownload(false);
        return false;
    }
    if (!CreateTemporaryFile(GetCacheFilePath(type, crc), *file))
    {
        PublishDownload(false);
        return false;
    }
    if (std::fclose(file->m_pFile.release()) != 0)
    {
        PublishDownload(false);
        return false;
    }
    m_cancelDownload.store(false, std::memory_order_relaxed);
    try
    {
        m_httpClient = std::make_shared<CHttpClient>();
        m_httpClient->SetProxy(std::move(proxy));
        m_httpClient->SetUserAgent("SAMP/0.3");
        m_httpClient->SetCancellationFlag(&m_cancelDownload);
        file->m_state = DownloadState::Http;
        m_httpDownload = std::async(std::launch::async, [this, client = m_httpClient, url = std::move(url), path = file->m_tempPath, size = file->m_nFileSize, crc] {
            return !m_cancelDownload.load(std::memory_order_relaxed) && client->DownloadFile(url.c_str(), path, size) == CHttpClient::ERROR_OK &&
                   ValidateFile(path, size, crc, m_cancelDownload);
        });
    }
    catch (...)
    {
        m_httpClient.reset();
        PublishDownload(false);
        return false;
    }
    return true;
}
void CCustomModels::ProcessTransfer()
{
    if (m_httpDownload.valid())
    {
        if (m_currentDownload && m_binaryDownload)
        {
            const auto bytes = m_binaryDownload->progress.load(std::memory_order_relaxed);
            m_currentDownload->m_bProgressDirty |= bytes != m_currentDownload->m_nDownloadedSize;
            m_currentDownload->m_nDownloadedSize = bytes;
            if (GetTickCount() - m_requestTime > 30000)
                m_binaryDownload->Finish(false);
        }
        if (m_currentDownload && m_httpClient)
        {
            const auto bytes = static_cast<std::uint32_t>(m_httpClient->GetTransferredBytes());
            if (bytes != m_currentDownload->m_nDownloadedSize)
            {
                m_currentDownload->m_nDownloadedSize = bytes;
                m_currentDownload->m_bProgressDirty = true;
            }
        }
        if (m_httpDownload.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            bool success = false;
            try
            {
                success = m_httpDownload.get();
            }
            catch (...)
            {}
            m_httpClient.reset();
            PublishDownload(success);
            m_binaryDownload.reset();
        }
    }
    else if (m_currentDownload && GetTickCount() - m_requestTime > 30000)
        FinishCurrentDownload(false);
}
bool CCustomModels::ShouldAcknowledgeDownloads() const
{
    if (!IsCacheReady() || m_currentDownload || m_downloadAcknowledged)
        return false;
    for (std::uint32_t i = 0; i < m_filesPool.GetCount(); ++i)
        if (const auto state = m_filesPool.Get(i)->m_state; state != DownloadState::Complete && state != DownloadState::Failed)
            return false;
    return true;
}
void CCustomModels::AcknowledgeDownloads()
{
    m_downloadAcknowledged = true;
}
void CCustomModels::ConfirmDownloads()
{
    if (m_downloadAcknowledged)
        m_downloadConfirmed = true;
}

namespace
{
    CCustomModelInfo* FindCustomModel(CCustomModels& models, int id, CustomModelType type)
    {
        for (std::uint32_t i = 0; i < models.GetCount(); ++i)
            if (auto* model = models.GetModelInfo(i); model && model->m_nCustomModelId == id && model->m_nType == type)
                return model;
        return nullptr;
    }
}
bool CCustomModels::LoadModel(std::int32_t base, std::int32_t id, std::uint32_t dffCrc, std::uint32_t txdCrc)
{
    if (!ModelInfo::IsReady(base))
    {
        if (ModelInfo::GetRelocated(base))
            reinterpret_cast<void(__cdecl*)(int, int)>(0x4087E0)(base, 0);
        return false;
    }

    if (const auto it = m_failedLoads.find(id); it != m_failedLoads.end() && GetTickCount() - it->second < 1000)
        return false;
    const auto dff = GetCachedFilePath(DownloadedFileType::Dff, dffCrc), txd = GetCachedFilePath(DownloadedFileType::Txd, txdCrc);
    if (dff.empty() || txd.empty())
        return false;
    const bool loaded = game::render::models::load_custom(base, id, dff, txd, true);
    if (loaded)
        m_failedLoads.erase(id);
    else
    {
#if defined(_DEBUG)
        if (!m_failedLoads.contains(id))
            Log("[dl:model] loading failed: id=%d base=%d", id, base);
#endif // _DEBUG
        m_failedLoads[id] = GetTickCount();
    }
    return loaded;
}
bool CCustomModels::LoadObjectModel(std::int32_t base, std::int32_t id, std::uint32_t dffCrc, std::uint32_t txdCrc)
{
    if (!ModelInfo::IsReady(base))
    {
        if (ModelInfo::GetRelocated(base))
            reinterpret_cast<void(__cdecl*)(int, int)>(0x4087E0)(base, 0);
        return false;
    }

    if (const auto it = m_failedLoads.find(id); it != m_failedLoads.end() && GetTickCount() - it->second < 1000)
        return false;
    const auto dff = GetCachedFilePath(DownloadedFileType::Dff, dffCrc), txd = GetCachedFilePath(DownloadedFileType::Txd, txdCrc);
    if (dff.empty() || txd.empty())
        return false;
    const bool loaded = game::render::models::load_custom(base, id, dff, txd, false);
    if (loaded)
        m_failedLoads.erase(id);
    else
    {
#if defined(_DEBUG)
        if (!m_failedLoads.contains(id))
            Log("[dl:model] loading failed: id=%d base=%d", id, base);
#endif // _DEBUG
        m_failedLoads[id] = GetTickCount();
    }
    return loaded;
}
bool CCustomModels::IsSkinModel(std::int32_t id) const
{
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (const auto* model = m_modelsPool.Get(i); model && model->m_nCustomModelId == id && model->m_nType == CustomModelType::Skin)
            return true;
    return false;
}
bool CCustomModels::IsObjectModel(std::int32_t id) const
{
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (const auto* model = m_modelsPool.Get(i); model && model->m_nCustomModelId == id && model->m_nType == CustomModelType::Object && Applies(*model, m_nVirtualWorld))
            return true;
    return false;
}
bool CCustomModels::IsModelLoaded(std::int32_t id) const
{
    return ModelInfo::IsOwnedClone(id) && ModelInfo::IsReady(id);
}
bool CCustomModels::LoadSkin(std::int32_t id)
{
    auto* model = FindCustomModel(*this, id, CustomModelType::Skin);
    if (!model || model->m_bDownloadFailed || !IsCacheReady())
        return false;
    const bool loaded = LoadModel(model->m_nStandardModelId, id, model->m_nDffCrc, model->m_nTxdCrc);
    const bool resident = IsModelLoaded(id);
    if (resident && !model->m_bLoaded)
        for (std::uint32_t i = 0; i < GetCount(); ++i)
            if (GetModelInfo(i) == model)
                m_loadedModels.push_back(i);
    model->m_bLoaded = resident;
    if (loaded)
        model->m_bPendingUnload = false;
    return loaded;
}
bool CCustomModels::LoadObject(std::int32_t id)
{
    if (!IsCacheReady())
        return false;
    for (std::uint32_t i = 0; i < GetCount(); ++i)
    {
        auto* model = GetModelInfo(i);
        if (!model || model->m_nCustomModelId != id || model->m_nType != CustomModelType::Object || !Applies(*model, m_nVirtualWorld) || model->m_bDownloadFailed)
            continue;
        const bool loaded = LoadObjectModel(model->m_nStandardModelId, id, model->m_nDffCrc, model->m_nTxdCrc);
        const bool resident = IsModelLoaded(id);
        if (resident && !model->m_bLoaded)
            m_loadedModels.push_back(i);
        model->m_bLoaded = resident;
        if (loaded)
            model->m_bPendingUnload = false;
        return loaded;
    }
    return false;
}
int CCustomModels::ResolveSkin(int base, std::uint32_t id)
{
    return id >= 20001 && id <= 30000 && LoadSkin(static_cast<int>(id)) ? static_cast<int>(id) : base;
}
int CCustomModels::ResolveObject(int id)
{
    if (id >= 0)
        return id;
    if (LoadObject(id))
        return id;
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (const auto* model = m_modelsPool.Get(i); model && model->m_nCustomModelId == id && model->m_nType == CustomModelType::Object && Applies(*model, m_nVirtualWorld))
            return model->m_nStandardModelId;
    return 18631;
}
std::uint32_t CCustomModels::GetObjectDffCrc(std::int32_t id) const
{
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (const auto* model = m_modelsPool.Get(i); model && model->m_nCustomModelId == id && model->m_nType == CustomModelType::Object && Applies(*model, m_nVirtualWorld))
            return model->m_nDffCrc;
    return 0;
}
std::uint32_t CCustomModels::GetObjectTxdCrc(std::int32_t id) const
{
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (const auto* model = m_modelsPool.Get(i); model && model->m_nCustomModelId == id && model->m_nType == CustomModelType::Object && Applies(*model, m_nVirtualWorld))
            return model->m_nTxdCrc;
    return 0;
}
std::uint8_t CCustomModels::GetObjectTimeOn(std::int32_t id) const
{
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (const auto* model = m_modelsPool.Get(i); model && model->m_nCustomModelId == id && model->m_nType == CustomModelType::Object && Applies(*model, m_nVirtualWorld))
            return model->m_nTimeOn;
    return 0;
}
std::uint8_t CCustomModels::GetObjectTimeOff(std::int32_t id) const
{
    for (std::uint32_t i = 0; i < GetCount(); ++i)
        if (const auto* model = m_modelsPool.Get(i); model && model->m_nCustomModelId == id && model->m_nType == CustomModelType::Object && Applies(*model, m_nVirtualWorld))
            return model->m_nTimeOff;
    return 0;
}
void CCustomModels::ProcessLoadedModels()
{
    for (auto it = m_loadedModels.begin(); it != m_loadedModels.end();)
    {
        auto* model = GetModelInfo(*it);
        if (!model || !IsModelLoaded(model->m_nCustomModelId))
        {
            if (model)
                model->m_bLoaded = model->m_bPendingUnload = false;
            it = m_loadedModels.erase(it);
            continue;
        }
        if (!Applies(*model, m_nVirtualWorld))
        {
            model->m_bPendingUnload = true;
            if (game::render::models::unload_custom(model->m_nCustomModelId))
            {
                model->m_bLoaded = model->m_bPendingUnload = false;
                it = m_loadedModels.erase(it);
                continue;
            }
        }
        ++it;
    }
}
