#include <stdafx.h>

#include "CObjectSelection.h"

#include "main.h"
#include "gui/CInput.h"
#include "render/CFonts.h"
#include "CObjectEdit.h"
#include "render/render.h"
#include "render/CRender.h"
#include "game/CGame.h"
#include "net/CNetGame.h"
#include "pools/CObjectPool.h"

#include <cstdio>

namespace
{
    constexpr CObjectSelection::ID INVALID_OBJECT_ID = 0xFFFF;
    constexpr float MAX_LABEL_DISTANCE = 150.0f;
    constexpr D3DCOLOR LABEL_COLOR = 0xFFD47B75u;

}

CObjectSelection::CObjectSelection() : m_bIsActive(FALSE), m_nHoveredObject(INVALID_OBJECT_ID)
{}

CObjectSelection::~CObjectSelection()
{
    Enable(FALSE);
}

CObjectSelection::ID CObjectSelection::DefineObject()
{
    ZoneScoped;
    auto* pool = g_netGame ? g_netGame->GetObjectPool() : nullptr;
    auto* window = g_game ? static_cast<HWND>(g_game->GetWindowHandle()) : nullptr;
    POINT point{};
    CRender::Vector3 origin{}, target{};
    if (!pool || !g_render || !window || !GetCursorPos(&point) || !ScreenToClient(window, &point) || !g_render->ScreenRay(point, origin, target))
        return INVALID_OBJECT_ID;
    const auto id = pool->GetId(CGame::TraceObjectRay(CVector{origin.x, origin.y, origin.z}, CVector{target.x, target.y, target.z}));
    return id >= 0 ? static_cast<ID>(id) : INVALID_OBJECT_ID;
}

void CObjectSelection::DrawLabels()
{
    ZoneScoped;
    if (m_nHoveredObject == INVALID_OBJECT_ID)
        return;

    auto* pool = g_netGame ? g_netGame->GetObjectPool() : nullptr;
    auto* selected = pool ? pool->Get(m_nHoveredObject) : nullptr;
    if (!selected || !selected->IsValid())
        return;
    ObjectInfo object{};
    object.id = m_nHoveredObject;
    object.type = SELECT_OBJECT_GLOBAL_OBJECT;
    object.modelId = selected->GetModelIndex();
    object.distance = selected->GetDistanceToCamera();
    CMatrix matrix = CMatrix::Unity();
    selected->GetMatrix(&matrix);
    const auto rw = matrix.ToRwMatrix();
    std::memcpy(&object.matrix, &rw, sizeof(rw));

    if (object.distance > MAX_LABEL_DISTANCE)
        return;

    if (!g_fonts || !g_objectEdit)
        return;

    g_objectEdit->RenderAxes(&object.matrix, 1.0f);

    CObjectEdit::Vector position = object.matrix.pos;
    float screen[2]{};

    if (g_objectEdit->WorldToScreen(&position, screen) >= 1.0f)
        return;

    char text[160]{};
    std::snprintf(text, sizeof(text), "[id: %u, type: %d]\nDistance: %.2fm\n", static_cast<unsigned>(object.id), object.type, object.distance);

    RECT rect{static_cast<LONG>(screen[0]), static_cast<LONG>(screen[1]), static_cast<LONG>(screen[0]) + 320, static_cast<LONG>(screen[1]) + 64};

    g_fonts->DrawLittleText(text, rect, DT_LEFT | DT_NOCLIP, LABEL_COLOR, true);
}

void CObjectSelection::Enable(BOOL enable)
{
    ZoneScoped;
    enable = enable != FALSE;
    if (m_bIsActive == enable)
        return;
    m_bIsActive = enable;
    m_nHoveredObject = INVALID_OBJECT_ID;
    if (enable && g_input && g_input->IsEnabled())
        g_input->Close();
    render::SetCursorMode(enable ? 2 : 0, !enable);
}

void CObjectSelection::Draw()
{
    ZoneScoped;
    if (!m_bIsActive)
        return;

    m_nHoveredObject = INVALID_OBJECT_ID;

    const ID objectId = DefineObject();
    if (objectId == INVALID_OBJECT_ID)
        return;

    m_nHoveredObject = objectId;
    DrawLabels();
}

void CObjectSelection::SendNotification()
{
    ZoneScoped;
    m_nHoveredObject = DefineObject();
    auto* pool = g_netGame ? g_netGame->GetObjectPool() : nullptr;
    auto* object = pool ? pool->Get(m_nHoveredObject) : nullptr;
    if (!object || !object->IsValid())
        return;
    CMatrix matrix = CMatrix::Unity();
    object->GetMatrix(&matrix);
    const auto& pos = matrix.GetPosition();
    g_netGame->SelectObject(SELECT_OBJECT_GLOBAL_OBJECT, m_nHoveredObject, object->GetModelIndex(), pos.x, pos.y, pos.z);
}

BOOL CObjectSelection::MsgProc(int uMsg, int wParam, int lParam)
{
    ZoneScoped;
    (void)wParam;
    (void)lParam;

    if (!m_bIsActive)
        return FALSE;

    if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        Enable(FALSE);
        return TRUE;
    }
    if (uMsg == WM_LBUTTONUP)
    {
        SendNotification();
        return TRUE;
    }

    return FALSE;
}
