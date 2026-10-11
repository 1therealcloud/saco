#include <stdafx.h>

#include "CObjectEdit.h"

#include "main.h"
#include "gui/CInput.h"
#include "render/CFonts.h"
#include "render/CRender.h"
#include "render/render.h"
#include "game/CGame.h"
#include "game/entities/CPed.h"
#include "game/entities/CObject.h"
#include "net/CNetGame.h"
#include "pools/CObjectPool.h"

#include <cmath>

namespace
{
    struct RuntimeState
    {
        bool fontLoaded = false;
        bool paused = false;
        bool hasInitialAttached = false;
        CObjectEdit::AttachedObjectState initialAttached{};
    };
    std::unordered_map<CObjectEdit*, RuntimeState> g_runtime;

    constexpr CObjectEdit::ID INVALID_OBJECT_ID = 0xFFFF;

    constexpr D3DCOLOR AXIS_LINE_COLOR = 0xFFCC9999u;
    constexpr D3DCOLOR BUTTON_NORMAL_COLOR = 0x99222222u;
    constexpr D3DCOLOR BUTTON_HOVER_COLOR = 0xFFAAAAAAu;

    constexpr D3DCOLOR X_AXIS_COLOR = 0xFF9999CCu;
    constexpr D3DCOLOR Y_AXIS_COLOR = 0xFFCC9999u;
    constexpr D3DCOLOR Z_AXIS_COLOR = 0xFF99CC99u;
    constexpr D3DCOLOR MODE_ICON_COLOR = 0xFF999999u;
    constexpr D3DCOLOR MODE_ACTIVE_COLOR = 0xFF774444u;

    constexpr float POSITION_STEP = 0.1f;
    constexpr float ROTATION_STEP = 10.0f;
    constexpr float SCALE_STEP = 0.1f;
    constexpr float MOUSE_STEP = 0.01f;

    CObjectEdit::Vector Add(const CObjectEdit::Vector& a, const CObjectEdit::Vector& b)
    {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }

    CObjectEdit::Vector Scale(const CObjectEdit::Vector& v, float value)
    {
        return {v.x * value, v.y * value, v.z * value};
    }

    void ZeroRect(RECT& rect)
    {
        rect = {};
    }
}

CObjectEdit::CObjectEdit(IDirect3DDevice9* device)
    : m_CharMaxSize{}, m_xAxisButtonRect{}, m_yAxisButtonRect{}, m_zAxisButtonRect{}, m_PositionButtonRect{}, m_RotationButtonRect{}, m_ScaleButtonRect{}, m_SaveButtonRect{},
      m_nEditType(OBJECT_EDIT_TYPE_NONE), m_nEditMode(OBJECT_EDIT_MODE_POSITION), m_bEnabled(FALSE), m_bRenderedThisFrame(FALSE), m_nEditObjectId(INVALID_OBJECT_ID),
      m_nAttachedObjectIndex(0), m_bIsPlayerObject(FALSE), m_vRotation{}, m_nLastSentNotificationTick(GetTickCount()), m_bRenderScaleButton(false), m_bEditingRightNow(false),
      m_bTopXOfObjectIsOnLeftOfScreen(false), m_bTopYOfObjectIsOnLeftOfScreen(false), m_bTopZOfObjectIsOnLeftOfScreen(false), m_EditStartPos{}, m_CursorPosInGame{},
      m_bObjectXSizeYCoordDiffMoreThanX(FALSE), m_bObjectYSizeYCoordDiffMoreThanX(FALSE), m_bObjectZSizeYCoordDiffMoreThanX(FALSE), m_entityMatrix{}, m_pDevice(device),
      m_pLine(nullptr), m_pIconFontSmall(nullptr), m_pIconFontBig(nullptr), m_nProcessType(OBJECT_EDIT_PROCESS_TYPE_NONE)
{
    if (!m_pDevice)
        return;

    auto& state = g_runtime[this];
    state.fontLoaded = AddFontResourceExA("sampaux3.ttf", FR_PRIVATE, nullptr) != 0;
    if (!state.fontLoaded)
    {
        Log("[objectedit] failed to load sampaux3.ttf from the game directory");
        return;
    }
    m_pLine = new CRender(m_pDevice);
    m_pIconFontSmall = new CFont(m_pDevice, 22, 0, FW_NORMAL, "SAMPAUX3", CP_ACP, SYMBOL_CHARSET);
    m_pIconFontBig = new CFont(m_pDevice, 28, 0, FW_NORMAL, "SAMPAUX3", CP_ACP, SYMBOL_CHARSET);
}

CObjectEdit::~CObjectEdit()
{
    if (m_bEnabled)
        Disable(FALSE, false);
    delete m_pIconFontBig;
    delete m_pIconFontSmall;
    delete m_pLine;

    m_pIconFontBig = nullptr;
    m_pIconFontSmall = nullptr;
    m_pLine = nullptr;
    const auto state = g_runtime.find(this);
    if (state != g_runtime.end())
    {
        if (state->second.fontLoaded)
            RemoveFontResourceExA("sampaux3.ttf", FR_PRIVATE, nullptr);
        g_runtime.erase(state);
    }
}

float CObjectEdit::WorldToScreen(Vector* in, float* out)
{
    if (!in || !out || !m_pLine)
        return 1.0f;

    float depth = 1.0f;

    CRender::Vector3 world{in->x, in->y, in->z};
    if (!m_pLine->WorldToScreen(world, out[0], out[1], &depth))
        return 1.0f;

    return depth;
}

int CObjectEdit::RenderAxes(Matrix* matrix, float lineSize)
{
    if (!matrix || !m_pLine)
        return FALSE;

    const Vector origin3 = matrix->pos;
    const Vector x3 = Add(origin3, Scale(matrix->right, lineSize));
    const Vector y3 = Add(origin3, Scale(matrix->up, lineSize));
    const Vector z3 = Add(origin3, Scale(matrix->at, lineSize));

    float origin[2]{};
    float x[2]{};
    float y[2]{};
    float z[2]{};

    const float originDepth = WorldToScreen(const_cast<Vector*>(&origin3), origin);
    const float xDepth = WorldToScreen(const_cast<Vector*>(&x3), x);
    const float yDepth = WorldToScreen(const_cast<Vector*>(&y3), y);
    const float zDepth = WorldToScreen(const_cast<Vector*>(&z3), z);

    if (originDepth >= 1.0f || xDepth >= 1.0f || yDepth >= 1.0f || zDepth >= 1.0f)
        return FALSE;

    m_pLine->BeginRender();
    m_pLine->DrawLine(origin[0], origin[1], x[0], x[1], 2.0f, AXIS_LINE_COLOR);
    m_pLine->DrawLine(origin[0], origin[1], y[0], y[1], 2.0f, AXIS_LINE_COLOR);
    m_pLine->DrawLine(origin[0], origin[1], z[0], z[1], 2.0f, AXIS_LINE_COLOR);
    m_pLine->EndRender();

    return TRUE;
}

const char* CObjectEdit::GetRenderChar(BOOL axis)
{
    switch (axis)
    {
        case 0:
            switch (m_nEditMode)
            {
                case OBJECT_EDIT_MODE_POSITION:
                    return "6";
                case OBJECT_EDIT_MODE_ROTATION:
                    return "3";
                case OBJECT_EDIT_MODE_SCALE:
                    return "9";
            }
            break;

        case 1:
            switch (m_nEditMode)
            {
                case OBJECT_EDIT_MODE_POSITION:
                    return "7";
                case OBJECT_EDIT_MODE_ROTATION:
                    return "4";
                case OBJECT_EDIT_MODE_SCALE:
                    return "A";
            }
            break;

        case 2:
            switch (m_nEditMode)
            {
                case OBJECT_EDIT_MODE_POSITION:
                    return "8";
                case OBJECT_EDIT_MODE_ROTATION:
                    return "5";
                case OBJECT_EDIT_MODE_SCALE:
                    return "B";
            }
            break;
    }

    return "0";
}

void CObjectEdit::TryChangeProcessType()
{
    if (!m_bRenderedThisFrame)
    {
        m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_NONE;
        return;
    }

    POINT point{};
    if (!GetCursorClientPoint(point))
    {
        m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_NONE;
        return;
    }

    if (IsPointInside(m_xAxisButtonRect, point))
        m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_XAXIS;
    else if (IsPointInside(m_yAxisButtonRect, point))
        m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_YAXIS;
    else if (IsPointInside(m_zAxisButtonRect, point))
        m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_ZAXIS;
    else if (IsPointInside(m_PositionButtonRect, point))
        m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_SETPOSITION;
    else if (IsPointInside(m_RotationButtonRect, point))
        m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_SETROTATION;
    else if (IsPointInside(m_ScaleButtonRect, point))
        m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_SETSCALE;
    else if (IsPointInside(m_SaveButtonRect, point))
        m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_SAVE;
    else
        m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_NONE;
}

void CObjectEdit::SetEditMode(ObjectEditMode mode)
{
    if (m_nEditType == OBJECT_EDIT_TYPE_OBJECT && mode == OBJECT_EDIT_MODE_SCALE)
        mode = OBJECT_EDIT_MODE_POSITION;

    m_nEditMode = mode;
}

void CObjectEdit::ResetMousePos()
{
    HWND window = GetGameWindow();
    if (!window)
        return;

    RECT rect{};
    if (!GetClientRect(window, &rect))
        return;

    POINT point{(rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2};

    m_EditStartPos = point;
    SetCursorClientPoint(point);
}

void CObjectEdit::EnterEditObject(ID objectId, BOOL playerObject)
{
    m_bEnabled = TRUE;
    m_nEditType = OBJECT_EDIT_TYPE_OBJECT;
    SetEditMode(OBJECT_EDIT_MODE_POSITION);

    m_nEditObjectId = objectId;
    m_bIsPlayerObject = playerObject;
    m_bRenderScaleButton = false;
    m_bEditingRightNow = false;
    m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_NONE;

    if (g_input && g_input->IsEnabled())
        g_input->Close();
    render::SetCursorMode(2, false);

    ObjectState state{};
    if (ResolveObject(state))
    {
        m_entityMatrix = state.matrix;
        m_vRotation = state.rotation;
    }
}

void CObjectEdit::EnterEditAttachedObject(std::uint32_t index)
{
    m_bEnabled = TRUE;
    m_nEditType = OBJECT_EDIT_TYPE_ATTACHEDOBJECT;
    SetEditMode(OBJECT_EDIT_MODE_POSITION);

    m_nAttachedObjectIndex = index;
    auto& runtime = g_runtime[this];
    runtime.hasInitialAttached = ResolveAttachedObject(runtime.initialAttached);
    if (!runtime.hasInitialAttached)
    {
        m_bEnabled = FALSE;
        m_nEditType = OBJECT_EDIT_TYPE_NONE;
        return;
    }
    m_nEditObjectId = INVALID_OBJECT_ID;
    m_bRenderScaleButton = true;
    m_bEditingRightNow = false;
    m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_NONE;

    if (g_input && g_input->IsEnabled())
        g_input->Close();
    render::SetCursorMode(2, false);
}

void CObjectEdit::SendEditEndNotification(int response)
{
    ObjectState state{};
    if (g_netGame && ResolveObject(state))
        g_netGame->EditObject(m_bIsPlayerObject != FALSE, m_nEditObjectId, response, CVector{state.matrix.pos.x, state.matrix.pos.y, state.matrix.pos.z},
                              CVector{state.rotation.x, state.rotation.y, state.rotation.z});
}

void CObjectEdit::SendAttachedEditEndNotification(int response)
{
    auto* ped = g_game ? g_game->GetPlayerPed() : nullptr;
    if (!g_netGame || !ped || m_nAttachedObjectIndex >= CPed::MAX_ACCESSORIES || !ped->GetAccessoryState(m_nAttachedObjectIndex))
        return;
    const auto& info = ped->m_accessories.m_info[m_nAttachedObjectIndex];
    g_netGame->EditAttachedObject(response, m_nAttachedObjectIndex, info.m_nModel, info.m_nBone, info.m_offset, info.m_rotation, info.m_scale, info.m_firstMaterialColor,
                                  info.m_secondMaterialColor);
}

void CObjectEdit::Disable(BOOL result, bool notify)
{
    const int response = result ? 1 : 0;
    auto& runtime = g_runtime[this];
    if (!result && m_nEditType == OBJECT_EDIT_TYPE_ATTACHEDOBJECT && runtime.hasInitialAttached)
    {
        if (auto* game = g_game.get())
            if (auto* ped = game->GetPlayerPed())
            {
                auto info = ped->m_accessories.m_info[m_nAttachedObjectIndex];
                info.m_offset = CVector{runtime.initialAttached.offset.x, runtime.initialAttached.offset.y, runtime.initialAttached.offset.z};
                info.m_rotation = CVector{runtime.initialAttached.rotation.x, runtime.initialAttached.rotation.y, runtime.initialAttached.rotation.z};
                info.m_scale = CVector{runtime.initialAttached.scale.x, runtime.initialAttached.scale.y, runtime.initialAttached.scale.z};
                ped->UpdateAccessory(static_cast<int>(m_nAttachedObjectIndex), info);
            }
    }
    runtime.hasInitialAttached = false;

    m_bEnabled = FALSE;
    m_bEditingRightNow = false;
    m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_NONE;

    render::SetCursorMode(0, true);

    if (notify)
    {
        if (m_nEditType == OBJECT_EDIT_TYPE_ATTACHEDOBJECT)
            SendAttachedEditEndNotification(response);
        else if (m_nEditType == OBJECT_EDIT_TYPE_OBJECT)
            SendEditEndNotification(response);
    }

    m_nEditType = OBJECT_EDIT_TYPE_NONE;
}

BOOL CObjectEdit::RenderControlsForObject(Matrix* objectMatrix, float lineSize)
{
    if (!objectMatrix || !m_pLine || !m_pIconFontSmall || !m_pIconFontBig)
        return FALSE;

    const SIZE maxSize = m_pIconFontBig->GetTextSize(GetMaxSizeChar(), DT_CALCRECT | DT_NOCLIP);
    m_CharMaxSize.x = maxSize.cx;
    m_CharMaxSize.y = maxSize.cy;

    if (!RenderAxes(objectMatrix, lineSize))
        return FALSE;

    const Vector origin3 = objectMatrix->pos;
    const Vector x3 = Add(origin3, Scale(objectMatrix->right, lineSize));
    const Vector y3 = Add(origin3, Scale(objectMatrix->up, lineSize));
    const Vector z3 = Add(origin3, Scale(objectMatrix->at, lineSize));

    float originf[2]{};
    float xf[2]{};
    float yf[2]{};
    float zf[2]{};

    if (WorldToScreen(const_cast<Vector*>(&origin3), originf) >= 1.0f || WorldToScreen(const_cast<Vector*>(&x3), xf) >= 1.0f ||
        WorldToScreen(const_cast<Vector*>(&y3), yf) >= 1.0f || WorldToScreen(const_cast<Vector*>(&z3), zf) >= 1.0f)
    {
        return FALSE;
    }

    const POINT origin{static_cast<LONG>(std::lround(originf[0])), static_cast<LONG>(std::lround(originf[1]))};
    const POINT x{static_cast<LONG>(std::lround(xf[0])), static_cast<LONG>(std::lround(xf[1]))};
    const POINT y{static_cast<LONG>(std::lround(yf[0])), static_cast<LONG>(std::lround(yf[1]))};
    const POINT z{static_cast<LONG>(std::lround(zf[0])), static_cast<LONG>(std::lround(zf[1]))};

    if (!m_bEditingRightNow)
    {
        UpdateAxisDirection(origin, x, m_bTopXOfObjectIsOnLeftOfScreen, m_bObjectXSizeYCoordDiffMoreThanX);
        UpdateAxisDirection(origin, y, m_bTopYOfObjectIsOnLeftOfScreen, m_bObjectYSizeYCoordDiffMoreThanX);
        UpdateAxisDirection(origin, z, m_bTopZOfObjectIsOnLeftOfScreen, m_bObjectZSizeYCoordDiffMoreThanX);
    }

    RenderControl(x, OBJECT_EDIT_PROCESS_TYPE_XAXIS);
    RenderControl(y, OBJECT_EDIT_PROCESS_TYPE_YAXIS);
    RenderControl(z, OBJECT_EDIT_PROCESS_TYPE_ZAXIS);

    POINT button = origin;
    RenderControl(button, OBJECT_EDIT_PROCESS_TYPE_SAVE);

    const LONG step = m_CharMaxSize.x + 2;

    if (m_bRenderScaleButton)
    {
        button.x -= step;
        RenderControl(button, OBJECT_EDIT_PROCESS_TYPE_SETSCALE);
    }
    else
    {
        ZeroRect(m_ScaleButtonRect);
    }

    button.x -= step;
    RenderControl(button, OBJECT_EDIT_PROCESS_TYPE_SETROTATION);

    button.x -= step;
    RenderControl(button, OBJECT_EDIT_PROCESS_TYPE_SETPOSITION);

    return TRUE;
}

void CObjectEdit::ApplyChanges(ObjectEditProcessType type, float diff)
{
    if (type < OBJECT_EDIT_PROCESS_TYPE_XAXIS || type > OBJECT_EDIT_PROCESS_TYPE_ZAXIS)
        return;

    const int axis = static_cast<int>(type);

    if (m_nEditType == OBJECT_EDIT_TYPE_ATTACHEDOBJECT)
    {
        AttachedObjectState state{};
        if (!ResolveAttachedObject(state))
            return;

        float* position = &state.offset.x;
        float* rotation = &state.rotation.x;
        float* scale = &state.scale.x;

        switch (m_nEditMode)
        {
            case OBJECT_EDIT_MODE_POSITION:
                position[axis] += diff * POSITION_STEP;
                break;

            case OBJECT_EDIT_MODE_ROTATION:
                rotation[axis] = WrapAngle(rotation[axis] + diff * ROTATION_STEP);
                break;

            case OBJECT_EDIT_MODE_SCALE:
                scale[axis] = std::max(0.0f, scale[axis] + diff * SCALE_STEP);
                break;
        }

        if (auto* game = g_game.get())
            if (auto* ped = game->GetPlayerPed())
            {
                auto info = ped->m_accessories.m_info[m_nAttachedObjectIndex];
                info.m_offset = CVector{state.offset.x, state.offset.y, state.offset.z};
                info.m_rotation = CVector{state.rotation.x, state.rotation.y, state.rotation.z};
                info.m_scale = CVector{state.scale.x, state.scale.y, state.scale.z};
                ped->UpdateAccessory(static_cast<int>(m_nAttachedObjectIndex), info);
            }

        SendAttachedEditEndNotification(2);
        return;
    }

    if (m_nEditType != OBJECT_EDIT_TYPE_OBJECT)
        return;

    ObjectState state{};
    if (!ResolveObject(state))
        return;

    if (m_nEditMode == OBJECT_EDIT_MODE_POSITION)
    {
        const Vector* basis = nullptr;

        switch (axis)
        {
            case 0:
                basis = &state.matrix.right;
                break;
            case 1:
                basis = &state.matrix.up;
                break;
            case 2:
                basis = &state.matrix.at;
                break;
        }

        if (basis)
            state.matrix.pos = Add(state.matrix.pos, Scale(*basis, diff * POSITION_STEP));
    }
    else if (m_nEditMode == OBJECT_EDIT_MODE_ROTATION)
    {
        float* rotation = &state.rotation.x;
        rotation[axis] = WrapAngle(rotation[axis] + diff * ROTATION_STEP);
        m_vRotation = state.rotation;
    }

    m_entityMatrix = state.matrix;

    if (auto* pool = g_netGame ? g_netGame->GetObjectPool() : nullptr)
        if (auto* object = pool->Get(m_nEditObjectId))
        {
            object->Teleport(CVector{state.matrix.pos.x, state.matrix.pos.y, state.matrix.pos.z});
            const CVector rotation{state.rotation.x, state.rotation.y, state.rotation.z};
            object->SetRotation(&rotation);
        }

    SendEditEndNotification(2);
}

void CObjectEdit::ProcessMouseMove()
{
    POINT cursor{};
    if (!GetCursorClientPoint(cursor))
        return;

    bool reverse = false;
    BOOL useY = FALSE;

    switch (m_nProcessType)
    {
        case OBJECT_EDIT_PROCESS_TYPE_XAXIS:
            reverse = m_bTopXOfObjectIsOnLeftOfScreen;
            useY = m_bObjectXSizeYCoordDiffMoreThanX;
            break;

        case OBJECT_EDIT_PROCESS_TYPE_YAXIS:
            reverse = m_bTopYOfObjectIsOnLeftOfScreen;
            useY = m_bObjectYSizeYCoordDiffMoreThanX;
            break;

        case OBJECT_EDIT_PROCESS_TYPE_ZAXIS:
            reverse = m_bTopZOfObjectIsOnLeftOfScreen;
            useY = m_bObjectZSizeYCoordDiffMoreThanX;
            break;

        default:
            return;
    }

    int delta = 0;

    if (m_nEditMode == OBJECT_EDIT_MODE_ROTATION)
    {
        if (useY)
            delta = reverse ? m_EditStartPos.y - cursor.y : cursor.y - m_EditStartPos.y;
        else
            delta = reverse ? m_EditStartPos.x - cursor.x : cursor.x - m_EditStartPos.x;
    }
    else
    {
        if (useY)
            delta = reverse ? m_EditStartPos.y - cursor.y : cursor.y - m_EditStartPos.y;
        else
            delta = reverse ? m_EditStartPos.x - cursor.x : cursor.x - m_EditStartPos.x;
    }

    if (!delta)
        return;

    if (m_nEditType == OBJECT_EDIT_TYPE_OBJECT && m_nEditMode == OBJECT_EDIT_MODE_POSITION)
    {
        ObjectState state{};
        if (!m_pLine || !ResolveObject(state))
            return;
        const Vector& axis = m_nProcessType == OBJECT_EDIT_PROCESS_TYPE_XAXIS   ? state.matrix.right
                             : m_nProcessType == OBJECT_EDIT_PROCESS_TYPE_YAXIS ? state.matrix.up
                                                                                : state.matrix.at;
        const float pixels = static_cast<float>(useY ? cursor.y - m_EditStartPos.y : cursor.x - m_EditStartPos.x);
        float offset = 0.0f;
        if (m_pLine->GetAxisDragOffset({state.matrix.pos.x, state.matrix.pos.y, state.matrix.pos.z}, {axis.x, axis.y, axis.z}, pixels, useY != FALSE, offset))
            ApplyChanges(static_cast<ObjectEditProcessType>(m_nProcessType), offset / POSITION_STEP);
        m_EditStartPos = cursor;
        return;
    }

    ApplyChanges(static_cast<ObjectEditProcessType>(m_nProcessType), static_cast<float>(delta) * MOUSE_STEP);
    m_EditStartPos = cursor;
}

BOOL CObjectEdit::MsgProc(int uMsg, int wParam, int lParam)
{
    (void)lParam;

    if (!m_bEnabled)
        return FALSE;

    if (uMsg == WM_KILLFOCUS)
    {
        m_bRenderedThisFrame = FALSE;
        m_bEditingRightNow = false;
        m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_NONE;
        return FALSE;
    }
    if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        Disable(FALSE);
        return TRUE;
    }
    switch (uMsg)
    {
        case WM_LBUTTONDOWN:
        {
            if (m_bEditingRightNow)
                return FALSE;

            if (m_nProcessType < OBJECT_EDIT_PROCESS_TYPE_XAXIS || m_nProcessType > OBJECT_EDIT_PROCESS_TYPE_ZAXIS)
            {
                return FALSE;
            }

            m_bEditingRightNow = true;

            GetCursorClientPoint(m_EditStartPos);
            m_CursorPosInGame = m_EditStartPos;

            ObjectState state{};
            if (m_nEditType == OBJECT_EDIT_TYPE_OBJECT && ResolveObject(state))
                m_entityMatrix = state.matrix;

            return TRUE;
        }

        case WM_LBUTTONUP:
        {
            if (m_bEditingRightNow)
            {
                m_bEditingRightNow = false;
                TryChangeProcessType();
                return TRUE;
            }

            switch (m_nProcessType)
            {
                case OBJECT_EDIT_PROCESS_TYPE_SETPOSITION:
                    SetEditMode(OBJECT_EDIT_MODE_POSITION);
                    return TRUE;

                case OBJECT_EDIT_PROCESS_TYPE_SETROTATION:
                    SetEditMode(OBJECT_EDIT_MODE_ROTATION);
                    return TRUE;

                case OBJECT_EDIT_PROCESS_TYPE_SETSCALE:
                    SetEditMode(OBJECT_EDIT_MODE_SCALE);
                    return TRUE;

                case OBJECT_EDIT_PROCESS_TYPE_SAVE:
                    Disable(TRUE);
                    return TRUE;
            }

            return FALSE;
        }

        case WM_MOUSEMOVE:
            if (m_bEditingRightNow)
            {
                ProcessMouseMove();
                return TRUE;
            }
            break;
    }

    return FALSE;
}

void CObjectEdit::Render()
{
    m_bRenderedThisFrame = FALSE;

    if (!m_bEnabled || g_runtime[this].paused)
        return;
    if (m_nEditType == OBJECT_EDIT_TYPE_OBJECT)
    {
        ObjectState state{};
        if (ResolveObject(state))
        {
            m_entityMatrix = state.matrix;
            m_vRotation = state.rotation;

            if (RenderControlsForObject(&state.matrix, state.distanceToCamera * 0.125f))
                m_bRenderedThisFrame = TRUE;
        }
        else
        {
            Disable(FALSE, false);
            return;
        }
    }
    else if (m_nEditType == OBJECT_EDIT_TYPE_ATTACHEDOBJECT)
    {
        AttachedObjectState state{};
        if (ResolveAttachedObject(state))
        {
            if (RenderControlsForObject(&state.matrix, state.distanceToCamera * 0.125f))
                m_bRenderedThisFrame = TRUE;
        }
        else
            Disable(FALSE);
    }

    if (!m_bEditingRightNow)
        TryChangeProcessType();
}

void CObjectEdit::SetPaused(bool paused)
{
    auto& state = g_runtime[this];
    if (state.paused == paused)
        return;
    state.paused = paused;
    m_bEditingRightNow = false;
    m_nProcessType = OBJECT_EDIT_PROCESS_TYPE_NONE;
    m_bRenderedThisFrame = FALSE;
}

void CObjectEdit::OnLostDevice()
{
    m_bRenderedThisFrame = FALSE;
    m_bEditingRightNow = false;
    if (m_pLine)
        m_pLine->OnLostDevice();
    if (m_pIconFontSmall)
        m_pIconFontSmall->OnLostDevice();
    if (m_pIconFontBig)
        m_pIconFontBig->OnLostDevice();
}

void CObjectEdit::OnResetDevice()
{
    if (m_pLine)
        m_pLine->OnResetDevice();
    if (m_pIconFontSmall)
        m_pIconFontSmall->OnResetDevice();
    if (m_pIconFontBig)
        m_pIconFontBig->OnResetDevice();
}

const char* CObjectEdit::GetMaxSizeChar()
{
    return "C";
}

HWND CObjectEdit::GetGameWindow() const
{
    if (!m_pDevice)
        return nullptr;

    D3DDEVICE_CREATION_PARAMETERS parameters{};
    if (FAILED(m_pDevice->GetCreationParameters(&parameters)))
        return nullptr;

    return parameters.hFocusWindow;
}

bool CObjectEdit::GetCursorClientPoint(POINT& point) const
{
    if (!GetCursorPos(&point))
        return false;

    HWND window = GetGameWindow();
    return window && ScreenToClient(window, &point) != FALSE;
}

void CObjectEdit::SetCursorClientPoint(const POINT& point) const
{
    HWND window = GetGameWindow();
    if (!window)
        return;

    POINT screen = point;
    if (ClientToScreen(window, &screen))
        SetCursorPos(screen.x, screen.y);
}

bool CObjectEdit::ResolveObject(ObjectState& state) const
{
    auto* pool = g_netGame ? g_netGame->GetObjectPool() : nullptr;
    auto* object = pool ? pool->Get(m_nEditObjectId) : nullptr;
    if (!object || !object->IsValid())
        return false;
    CMatrix matrix = CMatrix::Unity();
    object->GetMatrix(&matrix);
    const auto rw = matrix.ToRwMatrix();
    std::memcpy(&state.matrix, &rw, sizeof(rw));
    const auto rotation = object->GetRotation();
    state.rotation = {rotation.x, rotation.y, rotation.z};
    state.radius = object->m_fBoundingRadius;
    state.distanceToCamera = object->GetDistanceToCamera();
    return true;
}

bool CObjectEdit::ResolveAttachedObject(AttachedObjectState& state) const
{
    auto* game = g_game.get();
    auto* ped = game ? game->GetPlayerPed() : nullptr;
    if (!ped || m_nAttachedObjectIndex >= CPed::MAX_ACCESSORIES)
        return false;

    auto* object = ped->GetAccessory(static_cast<int>(m_nAttachedObjectIndex));
    CMatrix matrix = CMatrix::Unity();
    if (!object || !object->IsValid() || !ped->GetAccessoryMatrix(static_cast<int>(m_nAttachedObjectIndex), &matrix))
        return false;

    const auto rw = matrix.ToRwMatrix();
    std::memcpy(&state.matrix, &rw, sizeof(rw));
    const auto& info = ped->m_accessories.m_info[m_nAttachedObjectIndex];
    state.offset = {info.m_offset.x, info.m_offset.y, info.m_offset.z};
    state.rotation = {info.m_rotation.x, info.m_rotation.y, info.m_rotation.z};
    state.scale = {info.m_scale.x, info.m_scale.y, info.m_scale.z};
    state.radius = object->m_fBoundingRadius;
    state.distanceToCamera = object->GetDistanceToCamera();
    return true;
}

void CObjectEdit::RenderControl(const POINT& center, ObjectEditProcessType type)
{
    if (!m_pIconFontSmall || !m_pIconFontBig)
        return;

    RECT rect{center.x - m_CharMaxSize.x / 2, center.y - m_CharMaxSize.y / 2, center.x - m_CharMaxSize.x / 2 + m_CharMaxSize.x, center.y - m_CharMaxSize.y / 2 + m_CharMaxSize.y};

    D3DCOLOR background = BUTTON_NORMAL_COLOR;
    D3DCOLOR iconColor = MODE_ICON_COLOR;
    const char* icon = "0";

    if (m_nProcessType == type && !m_bEditingRightNow)
        background = BUTTON_HOVER_COLOR;

    switch (type)
    {
        case OBJECT_EDIT_PROCESS_TYPE_XAXIS:
            icon = GetRenderChar(0);
            iconColor = X_AXIS_COLOR;
            m_xAxisButtonRect = rect;
            break;

        case OBJECT_EDIT_PROCESS_TYPE_YAXIS:
            icon = GetRenderChar(1);
            iconColor = Y_AXIS_COLOR;
            m_yAxisButtonRect = rect;
            break;

        case OBJECT_EDIT_PROCESS_TYPE_ZAXIS:
            icon = GetRenderChar(2);
            iconColor = Z_AXIS_COLOR;
            m_zAxisButtonRect = rect;
            break;

        case OBJECT_EDIT_PROCESS_TYPE_SETPOSITION:
            icon = "2";
            if (m_nEditMode == OBJECT_EDIT_MODE_POSITION)
                background = MODE_ACTIVE_COLOR;
            m_PositionButtonRect = rect;
            break;

        case OBJECT_EDIT_PROCESS_TYPE_SETROTATION:
            icon = "0";
            if (m_nEditMode == OBJECT_EDIT_MODE_ROTATION)
                background = MODE_ACTIVE_COLOR;
            m_RotationButtonRect = rect;
            break;

        case OBJECT_EDIT_PROCESS_TYPE_SETSCALE:
            icon = "1";
            if (m_nEditMode == OBJECT_EDIT_MODE_SCALE)
                background = MODE_ACTIVE_COLOR;
            m_ScaleButtonRect = rect;
            break;

        case OBJECT_EDIT_PROCESS_TYPE_SAVE:
            icon = "H";
            m_SaveButtonRect = rect;
            break;

        default:
            return;
    }

    m_pIconFontBig->DrawTextA(GetMaxSizeChar(), &rect, DT_CENTER | DT_VCENTER | DT_NOCLIP, background);
    m_pIconFontSmall->DrawTextA(icon, &rect, DT_CENTER | DT_VCENTER | DT_NOCLIP, iconColor);
}

void CObjectEdit::UpdateAxisDirection(const POINT& origin, const POINT& endpoint, bool& topOnLeft, BOOL& yDiffMoreThanX)
{
    const LONG dx = endpoint.x - origin.x;
    const LONG dy = endpoint.y - origin.y;

    if (std::abs(dy) > std::abs(dx))
    {
        yDiffMoreThanX = TRUE;
        topOnLeft = endpoint.y < origin.y;
    }
    else
    {
        yDiffMoreThanX = FALSE;
        topOnLeft = endpoint.x < origin.x;
    }
}

bool CObjectEdit::IsPointInside(const RECT& rect, const POINT& point)
{
    return point.x >= rect.left && point.x <= rect.right && point.y >= rect.top && point.y <= rect.bottom;
}

float CObjectEdit::WrapAngle(float angle)
{
    angle = std::fmod(angle, 360.0f);

    if (angle < 0.0f)
        angle += 360.0f;

    return angle;
}
