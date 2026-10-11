#pragma once

#include <cstddef>
#include <cstdint>

#include "CCustomModelsPool.h"
#include "CDownloadedFilesPool.h"

struct IDirect3DDevice9;

class CCustomModels
{
public:
    explicit CCustomModels(IDirect3DDevice9* device); // 0xD890 - initializes custom model and download state
    ~CCustomModels() = default;                       // n/a - no standalone custom destructor body identified in 0.3.DL

    CCustomModels(const CCustomModels&) = delete;
    CCustomModels& operator=(const CCustomModels&) = delete;

    bool InitDirectory();                      // 0xBA40 - initializes server/local cache directories and model stream hooks
    bool DoesDffExist(std::uint32_t dffCrc);   // 0xBB10 - checks the server cache and local cache for a dff crc
    bool DoesTxdExist(std::uint32_t txdCrc);   // 0xBBA0 - checks the server cache and local cache for a txd crc
    bool DeleteDff(std::uint32_t dffCrc);      // 0xC100 - deletes a cached dff by crc
    bool DeleteTxd(std::uint32_t txdCrc);      // 0xC140 - deletes a cached txd by crc
    bool DeleteTempFile(const char* fileName); // 0xC180 - deletes a temporary downloaded file
    bool LoadModel(std::uint32_t standardModelId, std::uint32_t customModelId, std::uint32_t dffCrc,
                   std::uint32_t txdCrc); // 0xC1C0 - loads a skin/clump custom model from cached dff and txd
    bool LoadObjectModel(std::uint32_t standardModelId, std::uint32_t customModelId, std::uint32_t dffCrc,
                         std::uint32_t txdCrc); // 0xC2E0 - loads an object/atomic custom model from cached dff and txd

    CCustomModelInfo* GetModelInfo(std::uint32_t index);                                                // 0xC410 - returns a model record by pool index
    std::uint32_t GetCount() const;                                                                     // 0xC430 - returns the current model record count
    bool ResizePool(std::uint32_t count);                                                               // 0xC4E0 - resizes the dynamic model pointer table
    bool ShrinkPool();                                                                                  // 0xC6A0 - trims trailing empty entries from the model pointer table
    bool IsDffRequestPending(std::uint32_t dffCrc) const;                                               // 0xC6E0 - checks whether a dff crc already has a pending request
    bool IsTxdRequestPending(std::uint32_t txdCrc) const;                                               // 0xC720 - checks whether a txd crc already has a pending request
    std::int32_t FindModelByFile(DownloadedFileType type, std::uint32_t crc) const;                     // 0xC7E0 - finds the model pool index that owns a dff or txd crc
    bool BuildDownloadedFileInfo(DownloadedFileType type, std::uint32_t crc, DownloadedFileInfo* info); // 0xC830 - fills download metadata for a requested dff or txd
    std::int32_t GetMissingFilesCount() const;                        // 0xC8E0 - counts missing files for models visible in the active virtual world
    bool IsFileQueued(std::uint32_t count, std::uint32_t crc) const;  // 0xC950 - checks the first count model records for an already queued crc
    std::int32_t QueueMissingFiles();                                 // 0xC9B0 - marks missing dff and txd files for download and returns the number queued
    bool IsKnownFileCrc(std::uint32_t crc) const;                     // 0xCA40 - checks whether a crc belongs to any registered dff or txd
    void CleanupCache();                                              // 0xCA90 - removes temporary or orphaned cached model files
    void RefreshCache();                                              // 0xCBF0 - refreshes dff/txd existence flags and cleans unused cache files
    bool IsSkinModel(std::uint32_t customModelId) const;              // 0xCC80 - checks whether a custom model is type 1
    bool IsObjectModel(std::uint32_t customModelId) const;            // 0xCCC0 - checks whether a custom model is type 2
    bool IsModelLoaded(std::uint32_t customModelId) const;            // 0xCD00 - returns the runtime loaded flag for a custom model
    void ProcessLoadedModels();                                       // 0xCD50 - processes delayed unload counters and unloads expired custom models
    std::uint32_t GetObjectDffCrc(std::uint32_t customModelId) const; // 0xCDB0 - returns the dff crc for an object custom model
    std::uint32_t GetObjectTxdCrc(std::uint32_t customModelId) const; // 0xCDF0 - returns the txd crc for an object custom model
    bool LoadSkin(std::uint32_t customModelId);                       // 0xCE30 - resolves a type 1 record and installs its cached dff/txd into gta
    bool LoadObject(std::uint32_t customModelId);                     // 0xCE90 - resolves a type 2 record and installs its cached dff/txd into gta
    std::uint8_t GetObjectTimeOn(std::uint32_t customModelId) const;  // 0xCF00 - returns the timed-object start hour
    std::uint8_t GetObjectTimeOff(std::uint32_t customModelId) const; // 0xCF40 - returns the timed-object end hour
    bool RemoveModelInfo(std::uint32_t index);                        // 0xCFC0 - removes an entry from the model pointer table
    void PushBack(std::uint32_t index, CCustomModelInfo* modelInfo);  // 0xCFF0 - stores a model record at an index and grows the table when needed
    void SetExpectedModelsCount(std::uint32_t count);                 // 0xD030 - stores the expected model count and finalizes the model list when complete
    void ProcessDownloadChunk(std::uint8_t state, std::uint32_t bufferSize, const void* data,
                              std::uint32_t size);                  // 0xD070 - assembles incoming download chunks and forwards complete payloads to the file writer
    bool StartDownload(DownloadedFileType type, std::uint32_t crc); // 0xD1C0 - prepares a download record and opens its temporary output file
    bool RequestFile(std::uint32_t modelIndex, DownloadedFileType type, std::uint32_t crc, std::uint32_t size); // 0xD270 - sends rpc 181/182 for a missing dff or txd
    void Process();                                                            // 0xD390 - drives cache checks, file requests, download state and model readiness
    void SetModelInfo(std::uint32_t index, const CCustomModelInfo* modelInfo); // 0xD550 - copies a 0.3.DL model record into the dynamic model pool
    std::int32_t DeleteModelInfo(std::uint32_t index);                         // 0xD5A0 - frees a model record, compacts the table and returns the resulting index

private:
    void ResetDownloadState();                                                                     // 0xB990 - clears the active file record and six download slots
    void MakeSafeDirectoryName(char* destination, std::size_t capacity, const char* source);       // 0xB9C0 - filters a server name into a filesystem-safe directory name
    std::size_t WriteDownloadedData(DownloadedFileInfo* info, const void* data, std::size_t size); // 0xBC30 - writes a downloaded chunk and updates byte count and running crc
    std::int32_t FindDownloadSlot(std::uint32_t id) const;                                         // 0xBCA0 - finds one of the six internal download slots by id
    static std::size_t DownloadWriteCallback(void* data, std::size_t size, std::size_t count,
                                             void* userData);                                  // 0xBCD0 - forwards an http write callback into the active custom model download
    void InitializeDownloadSlots();                                                            // 0xBD10 - initializes the six internal download slot states
    std::int32_t AcquireDownloadSlot();                                                        // 0xBD40 - marks and returns an available internal download slot
    void ReleaseDownloadSlot();                                                                // 0xBD90 - releases the currently claimed internal download slot
    std::uint32_t GetReadyDownloadSlotsCount() const;                                          // 0xBDE0 - counts valid download slots that are neither completed nor active
    std::uint32_t GetCompletedDownloadSlotsCount() const;                                      // 0xBEB0 - counts valid download slots with the completion flag set
    std::uint32_t GetActiveDownloadSlotsCount() const;                                         // 0xBF40 - counts valid download slots with the active flag set
    static std::uint32_t ParseFileCrc(const char* fileName);                                   // 0xC0B0 - parses a hexadecimal crc from a cached model filename
    void FinishDownload(DownloadedFileInfo* info, std::uint32_t status, std::uint32_t result); // 0xC760 - closes a transfer, validates its result and updates model failure state

private:
    CCustomModelsPool m_modelsPool{};
    bool m_bPathInitialized{};
    char m_szPathName[261]{};
    char m_szLocalPath[261]{};

    std::int32_t m_nDownloadState{};
    std::int32_t m_nVirtualWorld{};
    std::uint32_t m_nExpectedModelsCount{};
    bool m_bModelListReady{};
    bool m_bDownloadsFinished{};
    bool field_download{};

    DownloadedFileInfo m_downloadedFile{};
    DownloadSlot m_downloadSlots[6]{};
};
