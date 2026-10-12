/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>

class CFont;
class CRender;

enum ObjectEditType
{
    OBJECT_EDIT_TYPE_NONE = 0,
    OBJECT_EDIT_TYPE_OBJECT = 1,
    OBJECT_EDIT_TYPE_ATTACHEDOBJECT = 2
};

enum ObjectEditMode
{
    OBJECT_EDIT_MODE_POSITION = 0,
    OBJECT_EDIT_MODE_ROTATION = 1,
    OBJECT_EDIT_MODE_SCALE = 2
};

enum ObjectEditProcessType
{
    OBJECT_EDIT_PROCESS_TYPE_NONE = -1,
    OBJECT_EDIT_PROCESS_TYPE_XAXIS = 0,
    OBJECT_EDIT_PROCESS_TYPE_YAXIS = 1,
    OBJECT_EDIT_PROCESS_TYPE_ZAXIS = 2,
    OBJECT_EDIT_PROCESS_TYPE_SETPOSITION = 3,
    OBJECT_EDIT_PROCESS_TYPE_SETROTATION = 4,
    OBJECT_EDIT_PROCESS_TYPE_SETSCALE = 5,
    OBJECT_EDIT_PROCESS_TYPE_SAVE = 10
};

class CObjectEdit
{
public:
    using ID = std::uint16_t;

    struct Vector
    {
        float x;
        float y;
        float z;
    };

    struct Matrix
    {
        Vector right;
        std::uint32_t flags;
        Vector up;
        float padUp;
        Vector at;
        float padAt;
        Vector pos;
        float padPos;
    };

    struct ObjectState
    {
        Matrix matrix{};
        Vector rotation{};
        float radius = 1.0f;
        float distanceToCamera = 8.0f;
    };

    struct AttachedObjectState
    {
        Matrix matrix{};
        Vector offset{};
        Vector rotation{};
        Vector scale{1.0f, 1.0f, 1.0f};
        float radius = 1.0f;
        float distanceToCamera = 8.0f;
    };

public:
    POINT m_CharMaxSize;                       // 0x000
    RECT m_xAxisButtonRect;                    // 0x008
    RECT m_yAxisButtonRect;                    // 0x018
    RECT m_zAxisButtonRect;                    // 0x028
    RECT m_PositionButtonRect;                 // 0x038
    RECT m_RotationButtonRect;                 // 0x048
    RECT m_ScaleButtonRect;                    // 0x058
    RECT m_SaveButtonRect;                     // 0x068
    int m_nEditType;                           // 0x078
    int m_nEditMode;                           // 0x07C
    BOOL m_bEnabled;                           // 0x080
    BOOL m_bRenderedThisFrame;                 // 0x084
    ID m_nEditObjectId;                        // 0x088
    std::uint32_t m_nAttachedObjectIndex;      // 0x08A
    BOOL m_bIsPlayerObject;                    // 0x08E
    Vector m_vRotation;                        // 0x092
    std::uint32_t m_nLastSentNotificationTick; // 0x09E
    bool m_bRenderScaleButton;                 // 0x0A2
    bool m_bEditingRightNow;                   // 0x0A3
    bool m_bTopXOfObjectIsOnLeftOfScreen;      // 0x0A4
    bool m_bTopYOfObjectIsOnLeftOfScreen;      // 0x0A5
    bool m_bTopZOfObjectIsOnLeftOfScreen;      // 0x0A6
    POINT m_EditStartPos;                      // 0x0A7
    POINT m_CursorPosInGame;                   // 0x0AF
    BOOL m_bObjectXSizeYCoordDiffMoreThanX;    // 0x0B7
    BOOL m_bObjectYSizeYCoordDiffMoreThanX;    // 0x0BB
    BOOL m_bObjectZSizeYCoordDiffMoreThanX;    // 0x0BF
    Matrix m_entityMatrix;                     // 0x0C3
    IDirect3DDevice9* m_pDevice;               // 0x103
    CRender* m_pLine;                          // 0x107, replaces ID3DXLine
    CFont* m_pIconFontSmall;                   // 0x10B, replaces ID3DXFont
    CFont* m_pIconFontBig;                     // 0x10F, replaces ID3DXFont
    int m_nProcessType;                        // 0x113

public:
    explicit CObjectEdit(IDirect3DDevice9* device);
    ~CObjectEdit();

    CObjectEdit(const CObjectEdit&) = delete;
    CObjectEdit& operator=(const CObjectEdit&) = delete;

    float WorldToScreen(Vector* in, float* out);
    int RenderAxes(Matrix* matrix, float lineSize);
    const char* GetRenderChar(BOOL axis);
    void TryChangeProcessType();
    void SetEditMode(ObjectEditMode mode);
    void ResetMousePos();

    void EnterEditObject(ID objectId, BOOL playerObject);
    void EnterEditAttachedObject(std::uint32_t index);

    void SendEditEndNotification(int response);
    void SendAttachedEditEndNotification(int response);

    void Disable(BOOL result, bool notify = true);
    BOOL RenderControlsForObject(Matrix* objectMatrix, float lineSize);
    void ApplyChanges(ObjectEditProcessType type, float diff);
    void ProcessMouseMove();
    BOOL MsgProc(int uMsg, int wParam, int lParam);
    void Render();
    void SetPaused(bool paused);
    void OnLostDevice();
    void OnResetDevice();

    static const char* GetMaxSizeChar();

private:
    HWND GetGameWindow() const;
    bool GetCursorClientPoint(POINT& point) const;
    void SetCursorClientPoint(const POINT& point) const;

    bool ResolveObject(ObjectState& state) const;
    bool ResolveAttachedObject(AttachedObjectState& state) const;

    void RenderControl(const POINT& center, ObjectEditProcessType type);
    void UpdateAxisDirection(const POINT& origin, const POINT& endpoint, bool& topOnLeft, BOOL& yDiffMoreThanX);
    static bool IsPointInside(const RECT& rect, const POINT& point);

    static float WrapAngle(float angle);
};
