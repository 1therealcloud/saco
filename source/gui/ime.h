#pragma once

#include <Windows.h>
#include <d3d9.h>

namespace cgui
{
    class EditBox;

    namespace ime
    {
        void SetEnabled(bool enabled);
        bool IsEnabled();
        bool IsComposing();
        bool OwnsComposition(const EditBox* edit);
        int GetIndicatorWidth(const EditBox* edit);
        void SetWindow(HWND window);
        void Focus(EditBox* edit);
        void Blur(EditBox* edit);
        void Cancel();
        void Shutdown();
        bool MsgProc(HWND window, UINT message, WPARAM wParam, LPARAM& lParam);
        void Render(IDirect3DDevice9* device, const char* face, int height, int weight);
    }
}
