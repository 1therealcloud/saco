/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "CCustomModelsPool.h"
#include "CDownloadedFilesPool.h"

class CCustomModels
{
public:
    CCustomModels();  // 0xD890
    ~CCustomModels(); // n/a - saco cleanup, no confirmed dl rva

    CCustomModels(const CCustomModels&) = delete;
    CCustomModels& operator=(const CCustomModels&) = delete;

    bool InitDirectory();                      // 0xBA40
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
                                 DownloadedFileInfo* info);          // 0xC830
    std::int32_t GetMissingFilesCount() const;                       // 0xC8E0
    bool IsFileQueued(std::uint32_t count, std::uint32_t crc) const; // 0xC950
    std::int32_t QueueMissingFiles();                                // 0xC9B0
    bool IsKnownFileCrc(std::uint32_t crc) const;                    // 0xCA40
    void CleanupCache();                                             // 0xCA90
    void RefreshCache();                                             // 0xCBF0

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
    bool PushBack(std::uint32_t index, CCustomModelInfo* modelInfo);                                               // 0xCFF0
    void SetExpectedModelsCount(std::uint32_t count);                                                              // 0xD030
    void ProcessDownloadChunk(std::uint8_t state, std::uint32_t bufferSize, const void* data, std::uint32_t size); // 0xD070
    bool StartDownload(DownloadedFileType type, std::uint32_t crc);                                                // 0xD1C0
    bool RequestFile(std::uint32_t modelIndex, DownloadedFileType type, std::uint32_t crc, std::uint32_t size);    // 0xD270
    void Process();                                                                                                // 0xD390
    std::int32_t SetModelInfo(std::uint32_t index,
                              const CCustomModelInfo* modelInfo); // 0xD550
    std::int32_t DeleteModelInfo(std::uint32_t index);            // 0xD5A0

private:
    void ResetDownloadState(); // 0xB990
    static void MakeSafeDirectoryName(char* destination, std::size_t capacity,
                                      const char* source);                                  // 0xB9C0
    void WriteDownloadedData(DownloadedFileInfo* info, const void* data, std::size_t size); // 0xBC30
    std::int32_t FindDownloadSlot(std::uint32_t id) const;                                  // 0xBCA0
    static std::size_t DownloadWriteCallback(void* data, std::size_t size, std::size_t count,
                                             void* userData); // 0xBCD0
    void InitializeDownloadSlots();                           // 0xBD10
    bool AcquireDownloadSlot();                               // 0xBD40
    void ReleaseDownloadSlot();                               // 0xBD90
    std::uint32_t GetReadyDownloadSlotsCount() const;         // 0xBDE0
    std::uint32_t GetCompletedDownloadSlotsCount() const;     // 0xBEB0
    std::uint32_t GetActiveDownloadSlotsCount() const;        // 0xBF40
    static std::uint32_t ParseFileCrc(const char* fileName);  // 0xC0B0
    void FinishDownload(DownloadedFileInfo* info, std::uint32_t status,
                        std::uint32_t result); // 0xC760

private:
    CCustomModelsPool m_modelsPool{};
    bool m_bPathInitialized{};
    std::string m_serverCachePath{};
    std::string m_localCachePath{};

    std::int32_t m_nDownloadState{};
    std::int32_t m_nVirtualWorld{};
    std::uint32_t m_nExpectedModelsCount{};
    bool m_bModelListReady{};
    bool m_bDownloadsFinished{};
    bool m_bHttpSlotsEnabled{};

    DownloadedFileInfo m_downloadedFile{};
    std::array<std::uint8_t, 4096> m_downloadChunkBuffer{};
    std::size_t m_nDownloadChunkSize{};
    std::array<DownloadSlot, 6> m_downloadSlots{};
};
