#pragma once

#include <Windows.h>
#include <d3d9.h>

class CHelpDialog
{
public:
    IDirect3DDevice9* m_pDevice;

    explicit CHelpDialog(IDirect3DDevice9* device = nullptr);

    void Show();
};
