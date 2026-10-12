/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "ime.h"
#include "CGui.h"

#include <imm.h>
#include <windowsx.h>

namespace cgui::ime
{
    namespace
    {
        bool g_enabled = false;
        bool g_composing = false;
        bool g_attached = false;
        HWND g_window = nullptr;
        HIMC g_context = nullptr;
        HIMC g_previousContext = nullptr;
        EditBox* g_edit = nullptr;
        std::array<bool, 256> g_consumedKeys{};
        std::wstring g_composition;
        std::vector<BYTE> g_attributes;
        std::wstring g_indicator;
        HKL g_layout = nullptr;
        int g_indicatorWidth = 0;
        int g_lineHeight = 0;
        int g_textWidth = 0;
        LONG g_anchorWidth = 0;
        bool g_verticalCandidates = true;
        bool g_mouseConsumed = false;
        RECT g_candidateRect{};
        std::vector<RECT> g_candidateHits;
        struct TextRow
        {
            std::wstring text;
            RECT source{};
            int width = 0;
            std::size_t first = 0;
            std::vector<int> positions;
        };
        std::vector<TextRow> g_compositionRows;
        std::vector<TextRow> g_candidateRows;
        RECT g_indicatorSource{};
        std::vector<std::wstring> g_candidates;
        DWORD g_selection = 0;
        DWORD g_pageStart = 0;
        LONG g_cursor = 0;
        bool g_dirty = true;
        IDirect3DTexture9* g_texture = nullptr;
        int g_width = 0;
        int g_height = 0;
        std::string g_fontFace;
        int g_fontHeight = 0;
        int g_fontWeight = 0;

        void ClearComposition()
        {
            ZoneScoped;
            g_composing = false;
            g_composition.clear();
            g_attributes.clear();
            g_candidates.clear();
            g_candidateHits.clear();
            g_candidateRect = {};
            g_cursor = 0;
            g_dirty = true;
        }

        void Detach()
        {
            Cancel();
            const bool attached = g_attached;
            g_attached = false;
            if (attached && IsWindow(g_window))
                ImmAssociateContext(g_window, g_previousContext);
            g_previousContext = nullptr;
        }

        void Attach()
        {
            ZoneScoped;
            if (g_attached || !g_edit || !IsWindow(g_window) || GetForegroundWindow() != GetAncestor(g_window, GA_ROOT))
                return;
            const bool useIme = g_enabled;
            const bool created = useIme && !g_context;
            if (useIme && !g_context)
                g_context = ImmCreateContext();
            if (useIme && !g_context)
                return;
            g_attached = true;
            g_previousContext = ImmAssociateContext(g_window, useIme ? g_context : nullptr);
            if (created && g_previousContext)
            {
                DWORD conversion = 0, sentence = 0;
                if (ImmGetConversionStatus(g_previousContext, &conversion, &sentence))
                    ImmSetConversionStatus(g_context, conversion, sentence);
                ImmSetOpenStatus(g_context, ImmGetOpenStatus(g_previousContext));
            }
        }

        void RefreshFocus()
        {
            ZoneScoped;
            if (!cgui::IsInitialized())
                return;
            constexpr DialogId order[] = {DialogId::GameUI, DialogId::Auxiliary, DialogId::Secondary, DialogId::Dialog};
            for (DialogId id : order)
            {
                auto* dialog = GetDialog(id);
                auto* edit = dynamic_cast<EditBox*>(dialog->GetFocus());
                if (dialog->IsVisible() && edit && edit->IsVisible() && edit->IsEnabled())
                {
                    Focus(edit);
                    return;
                }
            }
            Focus(nullptr);
        }

        std::wstring ReadString(DWORD index)
        {
            ZoneScoped;
            const LONG bytes = ImmGetCompositionStringW(g_context, index, nullptr, 0);
            if (bytes <= 0 || bytes > 65536 || bytes % sizeof(wchar_t))
                return {};
            std::wstring text(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
            if (ImmGetCompositionStringW(g_context, index, text.data(), bytes) != bytes)
                return {};
            return text;
        }

        void ReadCandidates(DWORD index)
        {
            ZoneScoped;
            g_candidates.clear();
            g_candidateHits.clear();
            g_candidateRect = {};
            const DWORD bytes = ImmGetCandidateListW(g_context, index, nullptr, 0);
            if (bytes < sizeof(CANDIDATELIST) || bytes > 1024 * 1024)
                return;
            std::vector<DWORD> storage((bytes + sizeof(DWORD) - 1) / sizeof(DWORD));
            auto* list = reinterpret_cast<CANDIDATELIST*>(storage.data());
            if (!ImmGetCandidateListW(g_context, index, list, bytes) || list->dwSize > bytes || list->dwSize < offsetof(CANDIDATELIST, dwOffset) ||
                list->dwCount > (list->dwSize - offsetof(CANDIDATELIST, dwOffset)) / sizeof(DWORD))
                return;
            if (list->dwPageStart >= list->dwCount || list->dwSelection >= list->dwCount)
                return;
            g_selection = list->dwSelection;
            g_pageStart = list->dwPageStart;
            const DWORD end = g_pageStart + std::min<DWORD>(list->dwPageSize, 10);
            for (DWORD i = g_pageStart; i < list->dwCount && i < end; ++i)
            {
                const DWORD offset = list->dwOffset[i];
                if (offset >= list->dwSize || offset % sizeof(wchar_t))
                    break;
                const auto* text = reinterpret_cast<const wchar_t*>(reinterpret_cast<const BYTE*>(list) + offset);
                const std::size_t available = (list->dwSize - offset) / sizeof(wchar_t);
                const auto* terminator = std::find(text, text + available, L'\0');
                if (terminator == text + available)
                    break;
                g_candidates.emplace_back(text, terminator);
            }
        }

        void ReleaseTexture()
        {
            if (g_texture)
                g_texture->Release();
            g_texture = nullptr;
        }

        LANGID Language()
        {
            return LOWORD(reinterpret_cast<ULONG_PTR>(GetKeyboardLayout(0)));
        }

        void UpdateFont(const char* face, int height, int weight)
        {
            ZoneScoped;
            if (g_fontFace == face && g_fontHeight == height && g_fontWeight == weight)
                return;
            g_fontFace = face;
            g_fontHeight = height;
            g_fontWeight = weight;
            g_indicatorWidth = 0;
            g_dirty = true;
        }

        std::wstring Indicator()
        {
            ZoneScoped;
            const HKL layout = GetKeyboardLayout(0);
            if (!ImmIsIME(layout) || !ImmGetOpenStatus(g_context))
            {
                wchar_t code[16]{};
                if (GetLocaleInfoW(MAKELCID(Language(), SORT_DEFAULT), LOCALE_SISO639LANGNAME, code, static_cast<int>(std::size(code))) && code[0])
                {
                    CharUpperBuffW(code, 1);
                    return code;
                }
                return L"En";
            }
            const LANGID language = Language();
            switch (PRIMARYLANGID(language))
            {
                case LANG_JAPANESE:
                    return L"\x3042";
                case LANG_KOREAN:
                    return L"\xAC00";
                case LANG_CHINESE:
                    return SUBLANGID(language) == SUBLANG_CHINESE_SIMPLIFIED ? L"\x7B80" : L"\x7E41";
                default:
                    return L"En";
            }
        }

        void ReadAttributes()
        {
            ZoneScoped;
            const LONG bytes = ImmGetCompositionStringW(g_context, GCS_COMPATTR, nullptr, 0);
            g_attributes.clear();
            if (bytes <= 0 || bytes > 65536)
                return;
            g_attributes.resize(bytes);
            if (ImmGetCompositionStringW(g_context, GCS_COMPATTR, g_attributes.data(), bytes) != bytes)
                g_attributes.clear();
        }

        int Measure(HDC dc, const std::wstring& text, std::size_t count = std::wstring::npos)
        {
            SIZE size{};
            GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(std::min(count, text.size())), &size);
            return size.cx;
        }

        bool CreateTexture(IDirect3DDevice9* device)
        {
            ZoneScoped;
            HDC dc = CreateCompatibleDC(nullptr);
            if (!dc)
                return false;
            HFONT font = CreateFontA(g_fontHeight, 0, 0, 0, g_fontWeight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                     DEFAULT_PITCH, g_fontFace.c_str());
            if (!font)
            {
                DeleteDC(dc);
                return false;
            }
            HGDIOBJ previousFont = SelectObject(dc, font);
            TEXTMETRICW metrics{};
            GetTextMetricsW(dc, &metrics);
            g_lineHeight = std::max<LONG>(1, metrics.tmHeight);
            if (!g_indicatorWidth)
            {
                for (const auto* label : {L"En", L"\x7B80", L"\x7E41", L"\xAC00", L"\x3042"})
                    g_indicatorWidth = std::max(g_indicatorWidth, Measure(dc, label));
                g_indicatorWidth += 4;
            }
            g_compositionRows.clear();
            g_candidateRows.clear();
            const RECT content = g_edit->GetImeTextRect();
            const RECT anchor = g_edit->GetImeRect();
            g_textWidth = std::max<LONG>(1, content.right - content.left);
            g_anchorWidth = std::max<LONG>(0, content.right - anchor.left);
            g_width = g_indicatorWidth;
            int atlasY = 0;
            const auto addRow = [&](std::vector<TextRow>& rows, const std::wstring& text, std::size_t first) {
                TextRow row;
                row.text = text;
                row.first = first;
                row.width = Measure(dc, text);
                row.source = RECT{0, atlasY, row.width, atlasY + g_lineHeight};
                row.positions.resize(text.size() + 1);
                SIZE extent{};
                if (!text.empty() && !GetTextExtentExPointW(dc, text.data(), static_cast<int>(text.size()), 0, nullptr, row.positions.data() + 1, &extent))
                    for (std::size_t i = 1; i <= text.size(); ++i)
                        row.positions[i] = Measure(dc, text, i);
                g_width = std::max(g_width, row.width);
                atlasY += g_lineHeight;
                rows.push_back(std::move(row));
            };
            // r5: composition starts at the caret and moves below the line only on overflow
            for (std::size_t first = 0; first < g_composition.size();)
            {
                int fit = 0;
                SIZE extent{};
                const int remaining = static_cast<int>(g_composition.size() - first);
                std::size_t last = first;
                if (GetTextExtentExPointW(dc, g_composition.data() + first, remaining, g_textWidth, &fit, nullptr, &extent))
                {
                    last += fit;
                    if (last > first && last < g_composition.size() && g_composition[last - 1] >= 0xD800 && g_composition[last - 1] <= 0xDBFF)
                        --last;
                    if (last == first)
                    {
                        ++last;
                        if (g_composition[first] >= 0xD800 && g_composition[first] <= 0xDBFF && last < g_composition.size())
                            ++last;
                    }
                }
                else
                {
                    while (last < g_composition.size())
                    {
                        std::size_t next = last + 1;
                        if (g_composition[last] >= 0xD800 && g_composition[last] <= 0xDBFF && next < g_composition.size())
                            ++next;
                        if (last > first && Measure(dc, g_composition.substr(first, next - first)) > g_textWidth)
                            break;
                        last = next;
                    }
                }
                addRow(g_compositionRows, g_composition.substr(first, last - first), first);
                first = last;
            }
            const LANGID language = Language();
            g_verticalCandidates = PRIMARYLANGID(language) != LANG_KOREAN && !(PRIMARYLANGID(language) == LANG_CHINESE && SUBLANGID(language) == SUBLANG_CHINESE_SIMPLIFIED);
            for (std::size_t i = 0; i < g_candidates.size(); ++i)
                addRow(g_candidateRows, std::to_wstring((i + 1) % 10) + L" " + g_candidates[i], i);
            g_indicatorSource = RECT{0, atlasY, Measure(dc, g_indicator), atlasY + g_lineHeight};
            g_width = std::max<LONG>(g_width, g_indicatorSource.right);
            atlasY += g_lineHeight;
            g_height = atlasY;
            UINT textureWidth = 1, textureHeight = 1;
            while (textureWidth < static_cast<UINT>(g_width))
                textureWidth *= 2;
            while (textureHeight < static_cast<UINT>(g_height))
                textureHeight *= 2;
            D3DCAPS9 caps{};
            const bool fits = SUCCEEDED(device->GetDeviceCaps(&caps)) && textureWidth <= caps.MaxTextureWidth && textureHeight <= caps.MaxTextureHeight;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = textureWidth;
            info.bmiHeader.biHeight = -static_cast<LONG>(textureHeight);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            void* pixels = nullptr;
            HBITMAP bitmap = fits ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
            bool success = false;
            if (bitmap && pixels)
            {
                HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
                std::memset(pixels, 0, textureWidth * textureHeight * 4);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(255, 255, 255));
                for (const auto& row : g_compositionRows)
                    TextOutW(dc, 0, row.source.top, row.text.c_str(), static_cast<int>(row.text.size()));
                for (const auto& row : g_candidateRows)
                    TextOutW(dc, 0, row.source.top, row.text.c_str(), static_cast<int>(row.text.size()));
                TextOutW(dc, 0, g_indicatorSource.top, g_indicator.c_str(), static_cast<int>(g_indicator.size()));
                GdiFlush();
                auto* colors = static_cast<DWORD*>(pixels);
                for (UINT i = 0; i < textureWidth * textureHeight; ++i)
                {
                    const DWORD alpha = std::max({colors[i] & 0xFF, (colors[i] >> 8) & 0xFF, (colors[i] >> 16) & 0xFF});
                    colors[i] = (alpha << 24) | 0xFFFFFF;
                }
                IDirect3DTexture9* texture = nullptr;
                if (SUCCEEDED(device->CreateTexture(textureWidth, textureHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)))
                {
                    D3DLOCKED_RECT locked{};
                    if (SUCCEEDED(texture->LockRect(0, &locked, nullptr, 0)))
                    {
                        for (UINT y = 0; y < textureHeight; ++y)
                            std::memcpy(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch, colors + y * textureWidth, textureWidth * 4);
                        texture->UnlockRect(0);
                        ReleaseTexture();
                        g_texture = texture;
                        success = true;
                    }
                    else
                        texture->Release();
                }
                SelectObject(dc, previousBitmap);
            }
            if (bitmap)
                DeleteObject(bitmap);
            SelectObject(dc, previousFont);
            DeleteObject(font);
            DeleteDC(dc);
            return success;
        }

        void DrawQuad(IDirect3DDevice9* device, RECT destination, D3DCOLOR color, IDirect3DTexture9* texture = nullptr, const RECT* source = nullptr)
        {
            ZoneScoped;
            color = ui::FadeColor(color);
            if (destination.right <= destination.left || destination.bottom <= destination.top)
                return;
            float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
            if (texture && source)
            {
                D3DSURFACE_DESC desc{};
                if (FAILED(texture->GetLevelDesc(0, &desc)))
                    return;
                u0 = static_cast<float>(source->left) / desc.Width;
                v0 = static_cast<float>(source->top) / desc.Height;
                u1 = static_cast<float>(source->right) / desc.Width;
                v1 = static_cast<float>(source->bottom) / desc.Height;
            }
            struct Vertex
            {
                float x, y, z, rhw;
                D3DCOLOR color;
                float u, v;
            };
            const float l = destination.left - 0.5f, t = destination.top - 0.5f;
            const float r = destination.right - 0.5f, b = destination.bottom - 0.5f;
            const Vertex vertices[] = {{l, t, 0, 1, color, u0, v0}, {r, t, 0, 1, color, u1, v0}, {l, b, 0, 1, color, u0, v1}, {r, b, 0, 1, color, u1, v1}};
            device->SetTexture(0, texture);
            device->SetTextureStageState(0, D3DTSS_COLOROP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, texture ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG1, texture ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
            device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex));

        }

        RECT IndicatorRect()
        {
            RECT rect = g_edit->GetImeTextRect();
            rect.left = rect.right + 6;
            rect.right = rect.left + g_indicatorWidth;
            rect.top -= 2;
            rect.bottom += 2;
            return rect;
        }

        void SendKey(WORD key)
        {
            ZoneScoped;
            INPUT input[2]{};
            input[0].type = input[1].type = INPUT_KEYBOARD;
            input[0].ki.wVk = input[1].ki.wVk = key;
            input[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, input, sizeof(INPUT));
        }

    }

    void SetEnabled(bool enabled)
    {
        if (g_enabled == enabled)
            return;
        Detach();
        g_enabled = enabled;
        Attach();
    }

    bool IsEnabled()
    {
        return g_enabled;
    }

    bool IsComposing()
    {
        return g_composing;
    }

    bool OwnsComposition(const EditBox* edit)
    {
        return g_enabled && g_attached && g_composing && g_edit == edit;
    }

    int GetIndicatorWidth(const EditBox* edit)
    {
        ZoneScoped;
        if (!g_enabled || !edit || !edit->HasFocus())
            return 0;
        UpdateFont(cgui::GetFontFace(), cgui::GetFontHeight(), cgui::GetFontWeight());
        const auto indicator = Indicator();
        if (indicator != g_indicator)
        {
            g_indicator = indicator;
            g_indicatorWidth = 0;
            g_dirty = true;
        }
        if (!g_indicatorWidth)
        {
            HDC dc = CreateCompatibleDC(nullptr);
            HFONT font = CreateFontA(g_fontHeight, 0, 0, 0, g_fontWeight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                     DEFAULT_PITCH, g_fontFace.c_str());
            if (dc && font)
            {
                HGDIOBJ previous = SelectObject(dc, font);
                for (const auto* label : {L"En", L"\x7B80", L"\x7E41", L"\xAC00", L"\x3042"})
                    g_indicatorWidth = std::max(g_indicatorWidth, Measure(dc, label));
                g_indicatorWidth = std::max(g_indicatorWidth, Measure(dc, g_indicator));
                g_indicatorWidth += 4;
                SelectObject(dc, previous);
            }
            if (font)
                DeleteObject(font);
            if (dc)
                DeleteDC(dc);
        }
        return g_indicatorWidth;
    }

    void SetWindow(HWND window)
    {
        if (window == g_window)
            return;
        Detach();
        g_window = window;
        Attach();
    }

    void Focus(EditBox* edit)
    {
        ZoneScoped;
        if (edit == g_edit)
        {
            Attach();
            return;
        }
        Detach();
        g_edit = edit;
        Attach();
    }

    void Cancel()
    {
        if (g_attached && g_composing && g_context)
        {
            g_composing = false;
            ImmNotifyIME(g_context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        }
        ClearComposition();
    }

    void Blur(EditBox* edit)
    {
        if (g_edit == edit)
            Focus(nullptr);
    }

    void Shutdown()
    {
        ZoneScoped;
        Detach();
        if (g_context)
            ImmDestroyContext(g_context);
        g_context = nullptr;
        g_window = nullptr;
        g_edit = nullptr;
        g_consumedKeys.fill(false);
        g_mouseConsumed = false;
        ReleaseTexture();
    }

    bool MsgProc(HWND window, UINT message, WPARAM wParam, LPARAM& lParam)
    {
        ZoneScoped;
        SetWindow(window);
        RefreshFocus();
        if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wParam))
        {
            Detach();
            g_consumedKeys.fill(false);
            g_mouseConsumed = false;
            return false;
        }
        if (message == WM_SETFOCUS || (message == WM_ACTIVATEAPP && wParam))
            Attach();
        if (message == WM_KEYUP && wParam < g_consumedKeys.size() && g_consumedKeys[wParam])
        {
            g_consumedKeys[wParam] = false;
            return true;
        }
        if (message == WM_CHAR && wParam < g_consumedKeys.size() && g_consumedKeys[wParam])
            return true;
        if (!g_enabled || !g_attached || !g_edit)
            return false;
        if (!g_edit->IsVisible() || !g_edit->IsEnabled() || (g_edit->GetDialog() && !g_edit->GetDialog()->IsVisible()))
        {
            Focus(nullptr);
            return false;
        }
        if (message == WM_LBUTTONUP && g_mouseConsumed)
        {
            g_mouseConsumed = false;
            return true;
        }
        if (message == WM_LBUTTONDOWN)
        {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const RECT indicator = IndicatorRect();
            if (PtInRect(&indicator, point))
            {
                ImmSetOpenStatus(g_context, !ImmGetOpenStatus(g_context));
                g_dirty = true;
                g_mouseConsumed = true;
                return true;
            }
            for (std::size_t i = 0; i < g_candidateHits.size(); ++i)
                if (PtInRect(&g_candidateHits[i], point))
                {
                    g_mouseConsumed = true;
                    if (PRIMARYLANGID(Language()) == LANG_JAPANESE)
                    {
                        const int delta = static_cast<int>(i + g_pageStart) - static_cast<int>(g_selection);
                        for (int n = 0; n < std::abs(delta); ++n)
                            SendKey(delta < 0 ? VK_UP : VK_DOWN);
                        SendKey(VK_RIGHT);
                        SendKey(VK_LEFT);
                    }
                    else
                        SendKey(static_cast<WORD>('0' + (i + 1) % 10));
                    return true;
                }
        }
        if (message == WM_KEYDOWN && g_composing)
        {
            if (wParam < g_consumedKeys.size())
                g_consumedKeys[wParam] = true;
            return true;
        }
        switch (message)
        {
            case WM_INPUTLANGCHANGE:
                ClearComposition();
                return false;
            case WM_IME_SETCONTEXT:
                lParam &= ~ISC_SHOWUIALL;
                return false;
            case WM_IME_STARTCOMPOSITION:
                ClearComposition();
                g_composing = true;
                return true;
            case WM_IME_COMPOSITION:
                if (lParam & GCS_RESULTSTR)
                {
                    const auto result = ReadString(GCS_RESULTSTR);
                    g_edit->InsertText(result.c_str());
                    g_composition.clear();
                }
                if (lParam & GCS_COMPSTR)
                    g_composition = ReadString(GCS_COMPSTR);
                if (lParam & (GCS_COMPSTR | GCS_COMPATTR))
                    ReadAttributes();
                if (lParam & GCS_CURSORPOS)
                    g_cursor = ImmGetCompositionStringW(g_context, GCS_CURSORPOS, nullptr, 0);
                g_dirty = true;
                return true;
            case WM_IME_ENDCOMPOSITION:
                for (UINT key : {VK_RETURN, VK_ESCAPE})
                    if (GetKeyState(key) & 0x8000)
                        g_consumedKeys[key] = true;
                ClearComposition();
                return true;
            case WM_IME_NOTIFY:
                if (wParam == IMN_OPENCANDIDATE || wParam == IMN_CHANGECANDIDATE)
                {
                    for (DWORD i = 0; i < 32; ++i)
                        if (static_cast<DWORD>(lParam) & (1u << i))
                        {
                            ReadCandidates(i);
                            break;
                        }
                    g_dirty = true;
                }
                else if (wParam == IMN_CLOSECANDIDATE)
                {
                    g_candidates.clear();
                    g_candidateHits.clear();
                    g_candidateRect = {};
                    g_dirty = true;
                }
                else
                    return false;
                return true;
            case WM_IME_CHAR:
                // the result string was already inserted by WM_IME_COMPOSITION
                return true;
            case WM_IME_KEYDOWN:
                if (g_composing && wParam < g_consumedKeys.size())
                    g_consumedKeys[wParam] = true;
                DefWindowProcA(window, message, wParam, lParam);
                return true;
            case WM_IME_KEYUP:
                DefWindowProcA(window, message, wParam, lParam);
                return true;
        }
        return false;
    }

    void Render(IDirect3DDevice9* device, const char* face, int height, int weight)
    {
        ZoneScoped;

        RefreshFocus();
        if (!device || !g_edit || !g_enabled || !g_attached || !g_edit->IsVisible() || !g_edit->IsEnabled() || (g_edit->GetDialog() && !g_edit->GetDialog()->IsVisible()))
            return;
        UpdateFont(face, height, weight);
        const auto* parent = g_edit->GetDialog();
        ui::OpacityScope fade((parent ? parent->GetOpacity() : 1.0f) * (parent == cgui::GetDialog(DialogId::GameUI) ? g_edit->GetOpacity() : 1.0f));
        const auto indicator = Indicator();
        const HKL layout = GetKeyboardLayout(0);
        const RECT content = g_edit->GetImeTextRect();
        const RECT anchor = g_edit->GetImeRect();
        if (indicator != g_indicator || layout != g_layout || content.right - content.left != g_textWidth || content.right - anchor.left != g_anchorWidth)
        {
            g_indicator = indicator;
            g_layout = layout;
            g_dirty = true;
        }
        if (g_dirty)
        {
            if (!CreateTexture(device))
                return;
            g_dirty = false;
        }
        // r5 0x90660: the locale indicator occupies the right side of the edit box
        const RECT indicatorRect = IndicatorRect();
        if (const auto* skin = cgui::GetDefaultElement(ControlType::ImeEditBox, 9))
            DrawQuad(device, indicatorRect, 0xFFFFFFFF, cgui::GetSkinTexture(), &skin->texture);
        const LONG labelWidth = g_indicatorSource.right;
        const LONG labelX = indicatorRect.left + (indicatorRect.right - indicatorRect.left - labelWidth) / 2;
        const LONG labelY = indicatorRect.top + (indicatorRect.bottom - indicatorRect.top - g_lineHeight) / 2;
        DrawQuad(device, RECT{labelX, labelY, labelX + labelWidth, labelY + g_lineHeight}, ImmIsIME(layout) && ImmGetOpenStatus(g_context) ? 0xFFC8C8FF : 0xFFFFFFFF, g_texture,
                 &g_indicatorSource);

        // r5 0x90020: draw composition at the caret, then highlight conversion targets
        LONG x = anchor.left, y = content.top;
        if (!g_compositionRows.empty() && (g_compositionRows.size() > 1 || g_compositionRows.front().width > content.right - anchor.left))
        {
            x = content.left;
            y = content.bottom;
        }
        const POINT compositionOrigin{x, y};
        POINT candidateAnchor{x, y + content.bottom - content.top};
        const bool japanese = PRIMARYLANGID(Language()) == LANG_JAPANESE;
        std::size_t target = static_cast<std::size_t>(std::clamp<LONG>(g_cursor, 0, g_composition.size()));
        if (japanese)
        {
            const auto found = std::find(g_attributes.begin(), g_attributes.end(), ATTR_TARGET_CONVERTED);
            target = found == g_attributes.end() ? 0 : static_cast<std::size_t>(found - g_attributes.begin());
        }
        for (const auto& row : g_compositionRows)
        {
            const bool koreanBlink = PRIMARYLANGID(Language()) == LANG_KOREAN && ((GetTickCount() / 500) & 1);
            if (!koreanBlink)
                DrawQuad(device, RECT{x, y, x + row.width, y + g_lineHeight}, 0xF0000000);
            for (std::size_t first = 0; first < row.text.size();)
            {
                const auto attribute = [&](std::size_t i) -> BYTE {
                    return row.first + i < g_attributes.size() ? g_attributes[row.first + i] : ATTR_INPUT;
                };
                const BYTE attr = attribute(first);
                std::size_t last = first + 1;
                while (last < row.text.size() && attribute(last) == attr)
                    ++last;
                const RECT destination{x + row.positions[first], y, x + row.positions[last], y + g_lineHeight};
                const RECT source{row.positions[first], row.source.top, row.positions[last], row.source.bottom};
                D3DCOLOR color = koreanBlink ? 0xFFFFFFFF : 0xFFC8C8FF;
                if (attr == ATTR_TARGET_CONVERTED || attr == ATTR_TARGET_NOTCONVERTED)
                {
                    DrawQuad(device, destination, 0xFF969696);
                    color = attr == ATTR_TARGET_CONVERTED ? 0xFFFFFFFF : 0xFFFFFF00;
                }
                DrawQuad(device, destination, color, g_texture, &source);
                first = last;
            }
            if (target >= row.first && target <= row.first + row.text.size())
                candidateAnchor.x = x + row.positions[target - row.first];
            if (g_cursor >= static_cast<LONG>(row.first) &&
                (g_cursor < static_cast<LONG>(row.first + row.text.size()) || (&row == &g_compositionRows.back() && g_cursor == static_cast<LONG>(row.first + row.text.size()))))
            {
                const LONG caret = x + row.positions[g_cursor - row.first];
                if (!japanese)
                    DrawQuad(device, RECT{caret - 1, y, caret + 1, y + g_lineHeight}, 0xFFFFFFFF);
            }
            x = content.left;
            y += g_lineHeight;
        }
        g_candidateHits.clear();
        g_candidateRect = {};
        if (g_candidateRows.empty())
            return;
        LONG candidateWidth = 0;
        for (const auto& row : g_candidateRows)
            candidateWidth = g_verticalCandidates ? std::max<LONG>(candidateWidth, row.width) : candidateWidth + row.width + 4;
        const LONG candidateHeight = g_verticalCandidates ? static_cast<LONG>(g_candidateRows.size()) * g_lineHeight : g_lineHeight;
        D3DVIEWPORT9 viewport{};
        if (FAILED(device->GetViewport(&viewport)))
            return;
        // r5 0x8fa00: candidates stay below the composition, with the right edge shifted left on overflow
        candidateAnchor.x = std::max<LONG>(viewport.X, std::min<LONG>(candidateAnchor.x, viewport.X + viewport.Width - candidateWidth));
        candidateAnchor.y = compositionOrigin.y + content.bottom - content.top;
        g_candidateRect = RECT{candidateAnchor.x, candidateAnchor.y, candidateAnchor.x + candidateWidth, candidateAnchor.y + candidateHeight};
        DrawQuad(device, g_candidateRect, 0xF0000000);
        x = candidateAnchor.x;
        y = candidateAnchor.y;
        for (std::size_t i = 0; i < g_candidateRows.size(); ++i)
        {
            const auto& row = g_candidateRows[i];
            const RECT hit{x, y, x + (g_verticalCandidates ? candidateWidth : row.width + 4), y + g_lineHeight};
            g_candidateHits.push_back(hit);
            const bool selected = i + g_pageStart == g_selection;
            if (selected)
                DrawQuad(device, hit, 0xF09E9E9E);
            DrawQuad(device, RECT{x, y, x + row.width, y + g_lineHeight}, selected ? 0xFFFFFFFF : 0xFFC8C8C8, g_texture, &row.source);
            if (g_verticalCandidates)
                y += g_lineHeight;
            else
                x += row.width + 4;
        }
    }
}
