/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <future>
#include <string_view>
#include <unordered_map>
#include <cstddef>
#include <cstdint>
#include <string>

#include "CCustomModelsPool.h"
#include "CDownloadedFilesPool.h"

class CHttpClient;

class CCustomModels
{
public:
    CCustomModels();  // 0xD890
    ~CCustomModels(); // n/a

    CCustomModels(const CCustomModels&) = delete;
    CCustomModels& operator=(const CCustomModels&) = delete;

    bool InitDirectory(const std::filesystem::path& root, std::string_view host, std::uint16_t port); // 0xBA40
    void Reset();
    void SetVirtualWorld(std::int32_t world);
    bool IsCacheReady() const;
    std::filesystem::path GetCacheFilePath(DownloadedFileType type, std::uint32_t crc) const;
    std::filesystem::path GetCachedFilePath(DownloadedFileType type, std::uint32_t crc) const;
    CCustomModelsPool& GetModelsPool()
    {
        return m_modelsPool;
    }
    const CDownloadedFilesPool& GetDownloadedFilesPool() const
    {
        return m_filesPool;
    }
    DownloadedFileInfo* GetNextDownload();
    bool BeginRequest(DownloadedFileType type, std::uint32_t crc);
    bool StartHttpDownload(DownloadedFileType type, std::uint32_t crc, std::string url, std::string proxy);
    void FinishCurrentDownload(bool success);
    bool ShouldAcknowledgeDownloads() const;
    void AcknowledgeDownloads();
    void ConfirmDownloads();
    bool AreDownloadsConfirmed() const
    {
        return m_downloadConfirmed;
    }
    bool DoesDffExist(std::uint32_t dffCrc);   // 0xBB10
    bool DoesTxdExist(std::uint32_t txdCrc);   // 0xBBA0
    bool DeleteDff(std::uint32_t dffCrc);      // 0xC100
    bool DeleteTxd(std::uint32_t txdCrc);      // 0xC140
    bool DeleteTempFile(const char* fileName); // 0xC180

    bool LoadModel(std::int32_t standardModelId, std::int32_t customModelId, std::uint32_t dffCrc, std::uint32_t txdCrc);       // 0xC1C0
    bool LoadObjectModel(std::int32_t standardModelId, std::int32_t customModelId, std::uint32_t dffCrc, std::uint32_t txdCrc); // 0xC2E0

    CCustomModelInfo* GetModelInfo(std::uint32_t index); // 0xC410
    std::uint32_t GetCount() const;                      // 0xC430
    bool ResizePool(std::uint32_t count);                // 0xC4E0
    bool ShrinkPool();                                   // 0xC6A0

    bool IsDffRequestPending(std::uint32_t dffCrc) const; // 0xC6E0
    bool IsTxdRequestPending(std::uint32_t txdCrc) const; // 0xC720
    std::int32_t FindModelByFile(DownloadedFileType type,
                                 std::uint32_t crc) const; // 0xC7E0
    bool BuildDownloadedFileInfo(DownloadedFileType type, std::uint32_t crc,
                                 DownloadedFileInfo* info);              // 0xC830
    std::int32_t GetMissingFilesCount() const;                           // 0xC8E0
    bool IsFileQueued(DownloadedFileType type, std::uint32_t crc) const; // 0xC950
    std::int32_t QueueMissingFiles();                                    // 0xC9B0
    bool IsKnownFileCrc(std::uint32_t crc) const;                        // 0xCA40
    void CleanupCache();                                                 // 0xCA90
    void RefreshCache();                                                 // 0xCBF0

    int ResolveSkin(int baseModel, std::uint32_t customModel);
    int ResolveObject(int customModel);
    bool IsSkinModel(std::int32_t customModelId) const;              // 0xCC80
    bool IsObjectModel(std::int32_t customModelId) const;            // 0xCCC0
    bool IsModelLoaded(std::int32_t customModelId) const;            // 0xCD00
    void ProcessLoadedModels();                                      // 0xCD50
    std::uint32_t GetObjectDffCrc(std::int32_t customModelId) const; // 0xCDB0
    std::uint32_t GetObjectTxdCrc(std::int32_t customModelId) const; // 0xCDF0
    bool LoadSkin(std::int32_t customModelId);                       // 0xCE30
    bool LoadObject(std::int32_t customModelId);                     // 0xCE90
    std::uint8_t GetObjectTimeOn(std::int32_t customModelId) const;  // 0xCF00
    std::uint8_t GetObjectTimeOff(std::int32_t customModelId) const; // 0xCF40

    bool RemoveModelInfo(std::uint32_t index);                                                                     // 0xCFC0
    bool PushBack(std::uint32_t index, const CCustomModelInfo* modelInfo);                                         // 0xCFF0
    void SetExpectedModelsCount(std::uint32_t count);                                                              // 0xD030
    void ProcessDownloadChunk(std::uint8_t state, std::uint32_t bufferSize, const void* data, std::uint32_t size); // 0xD070
    bool StartDownload(DownloadedFileType type, std::uint32_t crc);                                                // 0xD1C0
    void Process();                                                                                                // 0xD390
    std::int32_t SetModelInfo(std::uint32_t index,
                              const CCustomModelInfo* modelInfo); // 0xD550
    std::int32_t DeleteModelInfo(std::uint32_t index);            // 0xD5A0

private:
    bool DeleteCachedFile(DownloadedFileType type, std::uint32_t crc);
    void CancelDownloads();
    void PublishDownload(bool success);
    void ProcessTransfer();
    static void MakeSafeDirectoryName(char* destination, std::size_t capacity,
                                      const char* source);   // 0xB9C0
    static std::uint32_t ParseFileCrc(const char* fileName); // 0xC0B0

private:
    CCustomModelsPool m_modelsPool{};
    bool m_bPathInitialized{};
    std::filesystem::path m_serverCachePath{};
    std::filesystem::path m_localCachePath{};
    struct CachedFile
    {
        DownloadedFileType type{};
        std::uint32_t crc{}, size{};
        std::filesystem::path path;
    };
    std::future<std::vector<CachedFile>> m_cacheCheck;
    std::atomic<bool> m_cancelCacheCheck{};
    std::unordered_map<std::uint64_t, CachedFile> m_cachedFiles;
    CDownloadedFilesPool m_filesPool;
    std::vector<std::uint32_t> m_loadedModels;
    std::unordered_map<int, std::uint32_t> m_failedLoads;
    std::uint64_t m_poolRevision{}, m_cacheRevision{}, m_jobRevision{};
    bool m_cacheDirty{true};
    bool m_cacheReady{};
    bool m_cacheFailed{};

    std::int32_t m_nVirtualWorld{};
    std::uint32_t m_nExpectedModelsCount{};

    struct BinaryDownload;
    std::shared_ptr<BinaryDownload> m_binaryDownload;
    DownloadedFileInfo* m_currentDownload{};
    std::shared_ptr<CHttpClient> m_httpClient;
    std::future<bool> m_httpDownload;
    std::atomic<bool> m_cancelDownload{};
    std::uint32_t m_requestTime{};
    bool m_downloadAcknowledged{};
    bool m_downloadConfirmed{};
    std::array<std::uint8_t, 4096> m_downloadChunkBuffer{};
    std::size_t m_nDownloadChunkSize{};
    std::uint32_t m_binaryQueuedSize{};
};
