#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstdarg>

class CConfig;
class CFonts;
namespace cgui
{
    class Dialog;
    class EditBox;
    class ScrollBar;
}
struct ID3DXSprite;
struct ID3DXRenderToSurface;

class CChat
{
public:
    enum EntryType
    {
        ENTRY_TYPE_NONE = 0,
        ENTRY_TYPE_CHAT = 1 << 1,
        ENTRY_TYPE_INFO = 1 << 2,
        ENTRY_TYPE_DEBUG = 1 << 3
    };

    enum DisplayMode
    {
        DISPLAY_MODE_OFF,
        DISPLAY_MODE_NOSHADOW,
        DISPLAY_MODE_NORMAL
    };

    enum
    {
        MAX_MESSAGES = 100,
        MAX_PREFIX_LENGTH = 24,
        MAX_TEXT_LENGTH = 144,
        CHAT_SPLIT_LENGTH = 72,
        CHAT_SPLIT_BACKTRACK = 12
    };

    struct ChatEntry
    {
        std::int32_t m_timestamp;
        char m_szPrefix[28];
        char m_szText[144];
        char unused[64];
        std::int32_t m_nType;
        D3DCOLOR m_textColor;
        D3DCOLOR m_prefixColor;
    };

public:
    // r5 layout. keep these fields in this exact order.
    unsigned int m_nPageSize;
    char* m_szLastMessage;
    int m_nMode;
    bool m_bTimestamps;
    BOOL m_bDoesLogExist;
    char m_szLogPath[MAX_PATH + 1];
    cgui::Dialog* m_pGameUi;
    cgui::EditBox* m_pEditbox;
    cgui::ScrollBar* m_pScrollbar;
    D3DCOLOR m_textColor;
    D3DCOLOR m_infoColor;
    D3DCOLOR m_debugColor;
    long m_nWindowBottom;
    ChatEntry m_entry[MAX_MESSAGES];
    CFonts* m_pFontRenderer;
    ID3DXSprite* m_pTextSprite;
    ID3DXSprite* m_pSprite;
    IDirect3DDevice9* m_pDevice;
    BOOL m_bRenderToSurface;
    ID3DXRenderToSurface* m_pRenderToSurface;
    IDirect3DTexture9* m_pTexture;
    IDirect3DSurface9* m_pSurface;
    D3DDISPLAYMODE m_displayMode;
    int m_pad[2];
    BOOL m_bRedraw;
    long m_nScrollbarPos;
    long m_nCharHeight;
    long m_nTimestampWidth;

public:
    CChat(IDirect3DDevice9* device, CFonts* fontRenderer, const char* logPath);
    CChat(IDirect3DDevice9* device, CFonts* fontRenderer, const char* logPath, const CConfig* config);
    ~CChat();

    CChat(const CChat&) = delete;
    CChat& operator=(const CChat&) = delete;

    int GetMode() const;
    void SwitchMode();

    void RecalcFontSize();
    void OnLostDevice();
    void OnResetDevice();

    void UpdateScrollbar();
    void SetPageSize(int value);
    void PageUp();
    void PageDown();
    void ScrollToBottom();
    void Scroll(int delta);

    void SetTimestamps(bool enabled);
    bool GetTimestamps() const;

    void FilterOutInvalidChars(char* string) const;
    void PushBack();

    void RenderEntry(const char* text, RECT rect, D3DCOLOR color);
    void Log(int type, const char* text, const char* prefix);
    void ResetDialogControls(cgui::Dialog* gameUi);
    void SyncScrollbar();
    void UpdateScrollbarVisibility(bool inputActive);
    void Render();

    void AddEntry(int type, const char* text, const char* prefix, D3DCOLOR textColor, D3DCOLOR prefixColor);
    void Draw();
    void RenderToSurface();

    // r5 mutates the supplied text buffer while sanitizing control chars.
    void AddChatMessage(const char* prefix, D3DCOLOR prefixColor, char* text);
    void AddChatMessage(const char* prefix, D3DCOLOR prefixColor, const char* text);
    void AddMessage(D3DCOLOR color, char* text);
    void AddMessage(D3DCOLOR color, const char* text);

    // the two internal variadic chat printers at 0x68070 / 0x680F0.
    void PrintInfo(const char* format, ...);
    void PrintDebug(const char* format, ...);

    const ChatEntry* GetEntries() const;
    std::size_t GetEntryCount() const;

private:
    void Initialize(IDirect3DDevice9* device, CFonts* fontRenderer, const char* logPath, const CConfig* config);
    void ApplyConfig(const CConfig* config);
    void PrintV(int type, D3DCOLOR color, const char* format, va_list args);
    void DrawEntry(const ChatEntry& entry, long& left, long top, long bottom);

    static std::int32_t GetCurrentTimestamp();
    static void FormatTimestamp(std::int32_t timestamp, char (&buffer)[64]);
    static std::size_t FindSplitPosition(const char* text);
    static void CopyCount(char* destination, const char* source, std::size_t count);
    static long ClampScrollPosition(long position, unsigned int pageSize);
    static void ExpandControlKeyTokens(char* text, std::size_t capacity);
};
