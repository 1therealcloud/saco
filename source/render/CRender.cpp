#include <stdafx.h>
#include "game/Profiler.h"

#include "CRender.h"
#include "game/d3d9hook.h"

#include <cmath>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    template <typename T>
    void SafeRelease(T*& object)
    {
        if (object)
        {
            object->Release();
            object = nullptr;
        }
    }
}

CRender::CRender(IDirect3DDevice9* device) : m_device(device)
{
    ZoneScoped;
    if (m_device)
        m_device->AddRef();

    m_vertices.reserve(2048);
}

CRender::~CRender()
{
    ZoneScoped;
    if (m_rendering)
        EndRender();

    ReleaseVertexBuffer();
    ReleaseSavedState();
    SafeRelease(m_device);
}

bool CRender::IsValid() const
{
    ZoneScoped;
    return m_device != nullptr;
}

HRESULT CRender::BeginRender()
{
    ZoneScoped;
    if (!m_device)
        return D3DERR_INVALIDCALL;

    if (m_rendering)
        return D3D_OK;

    m_vertices.clear();
    SaveState();
    ApplyState();
    m_rendering = true;
    return D3D_OK;
}

HRESULT CRender::EndRender()
{
    ZoneScoped;
    if (!m_device)
        return D3DERR_INVALIDCALL;

    if (!m_rendering)
        return D3D_OK;

    const HRESULT result = Flush();
    RestoreState();
    ReleaseSavedState();
    m_rendering = false;
    return result;
}

void CRender::OnLostDevice()
{
    ZoneScoped;
    if (m_rendering)
    {
        RestoreState();
        ReleaseSavedState();
        m_rendering = false;
    }

    m_vertices.clear();
    ReleaseVertexBuffer();
}

void CRender::OnResetDevice()
{
    ZoneScoped;
    m_vertices.clear();
}

void CRender::DrawLine(float x1, float y1, float x2, float y2, float width, D3DCOLOR color)
{
    ZoneScoped;
    if (!m_rendering || width <= 0.0f)
        return;

    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.000001f)
    {
        const float half = width * 0.5f;
        AddQuad(x1 - half, y1 - half, x1 + half, y1 + half, color);
        return;
    }

    const float length = std::sqrt(lengthSquared);
    const float half = width * 0.5f;
    const float nx = -dy / length * half;
    const float ny = dx / length * half;

    const Vertex a{x1 + nx, y1 + ny, 0.0f, 1.0f, color};
    const Vertex b{x2 + nx, y2 + ny, 0.0f, 1.0f, color};
    const Vertex c{x2 - nx, y2 - ny, 0.0f, 1.0f, color};
    const Vertex d{x1 - nx, y1 - ny, 0.0f, 1.0f, color};

    AddTriangle(a, b, c);
    AddTriangle(a, c, d);
}

void CRender::DrawBox(float x, float y, float width, float height, D3DCOLOR color)
{
    ZoneScoped;
    if (!m_rendering || width == 0.0f || height == 0.0f)
        return;

    const float right = x + width;
    const float bottom = y + height;
    AddQuad(std::min(x, right), std::min(y, bottom), std::max(x, right), std::max(y, bottom), color);
}

void CRender::DrawBorder(float x, float y, float width, float height, float borderWidth, D3DCOLOR color)
{
    ZoneScoped;
    if (!m_rendering || borderWidth <= 0.0f || width == 0.0f || height == 0.0f)
        return;

    float left = x;
    float top = y;
    float right = x + width;
    float bottom = y + height;
    if (left > right)
        std::swap(left, right);
    if (top > bottom)
        std::swap(top, bottom);

    const float clamped = std::min(borderWidth, std::min((right - left) * 0.5f, (bottom - top) * 0.5f));
    if (clamped <= 0.0f)
        return;

    AddQuad(left, top, right, top + clamped, color);
    AddQuad(left, bottom - clamped, right, bottom, color);
    AddQuad(left, top + clamped, left + clamped, bottom - clamped, color);
    AddQuad(right - clamped, top + clamped, right, bottom - clamped, color);
}

void CRender::DrawBorderedBox(float x, float y, float width, float height, float borderWidth, D3DCOLOR color, D3DCOLOR borderColor)
{
    ZoneScoped;
    DrawBox(x, y, width, height, color);
    DrawBorder(x, y, width, height, borderWidth, borderColor);
}

void CRender::DrawPolygon(float x, float y, float width, float height, float rotation, unsigned int sides, D3DCOLOR color)
{
    ZoneScoped;
    if (!m_rendering || sides < 3 || width == 0.0f || height == 0.0f)
        return;

    sides = std::min(sides, 512u);

    const float rx = width * 0.5f;
    const float ry = height * 0.5f;
    const float radians = rotation * (kPi / 180.0f);
    const float step = (2.0f * kPi) / static_cast<float>(sides);
    const Vertex center{x, y, 0.0f, 1.0f, color};

    auto point = [&](unsigned int index) {
        const float angle = radians + step * static_cast<float>(index);
        return Vertex{x + std::cos(angle) * rx, y + std::sin(angle) * ry, 0.0f, 1.0f, color};
    };

    Vertex previous = point(0);
    for (unsigned int i = 1; i <= sides; ++i)
    {
        const Vertex current = point(i % sides);
        AddTriangle(center, previous, current);
        previous = current;
    }
}

void CRender::DrawCircle(float x, float y, float radius, unsigned int sides, D3DCOLOR color)
{
    ZoneScoped;
    DrawPolygon(x, y, radius * 2.0f, radius * 2.0f, 0.0f, sides, color);
}

bool CRender::WorldToScreen(const Vector3& world, float& x, float& y, float* depth) const
{
    ZoneScoped;
    if (!m_device)
        return false;

    D3DMATRIX view{};
    D3DMATRIX projection{};
    if (!d3d9hook::matrices(view, projection))
        return false;

    const D3DMATRIX matrix = Multiply(view, projection);
    const float clipX = world.x * matrix._11 + world.y * matrix._21 + world.z * matrix._31 + matrix._41;
    const float clipY = world.x * matrix._12 + world.y * matrix._22 + world.z * matrix._32 + matrix._42;
    const float clipZ = world.x * matrix._13 + world.y * matrix._23 + world.z * matrix._33 + matrix._43;
    const float clipW = world.x * matrix._14 + world.y * matrix._24 + world.z * matrix._34 + matrix._44;

    if (clipW <= 0.0001f)
        return false;

    D3DVIEWPORT9 viewport{};
    if (FAILED(m_device->GetViewport(&viewport)))
        return false;

    const float invW = 1.0f / clipW;
    const float ndcX = clipX * invW;
    const float ndcY = clipY * invW;
    const float ndcZ = clipZ * invW;

    x = static_cast<float>(viewport.X) + (ndcX + 1.0f) * static_cast<float>(viewport.Width) * 0.5f;
    y = static_cast<float>(viewport.Y) + (1.0f - ndcY) * static_cast<float>(viewport.Height) * 0.5f;

    if (depth)
        *depth = static_cast<float>(viewport.MinZ) + ndcZ * (static_cast<float>(viewport.MaxZ) - static_cast<float>(viewport.MinZ));

    return ndcZ >= 0.0f && ndcZ <= 1.0f;
}

bool CRender::ScreenRay(POINT point, Vector3& origin, Vector3& target) const
{
    ZoneScoped;
    D3DMATRIX view{}, projection{};
    D3DVIEWPORT9 viewport{};
    if (!m_device || !d3d9hook::matrices(view, projection) || FAILED(m_device->GetViewport(&viewport)) || !viewport.Width || !viewport.Height)
        return false;
    const auto matrix = Multiply(view, projection);
    // invert the view/projection matrix, including its perspective component
    double rows[4][8]{};
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
            rows[row][col] = matrix.m[row][col];
        rows[row][row + 4] = 1.0;
    }
    for (int col = 0; col < 4; ++col)
    {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (std::abs(rows[row][col]) > std::abs(rows[pivot][col]))
                pivot = row;
        if (std::abs(rows[pivot][col]) < 1e-12)
            return false;
        for (int i = 0; i < 8; ++i)
            std::swap(rows[col][i], rows[pivot][i]);
        const double divisor = rows[col][col];
        for (double& value : rows[col])
            value /= divisor;
        for (int row = 0; row < 4; ++row)
        {
            if (row == col)
                continue;
            const double factor = rows[row][col];
            for (int i = 0; i < 8; ++i)
                rows[row][i] -= factor * rows[col][i];
        }
    }
    const auto unproject = [&](double z, Vector3& result) {
        const double clip[4]{2.0 * (point.x - static_cast<double>(viewport.X)) / viewport.Width - 1.0, 1.0 - 2.0 * (point.y - static_cast<double>(viewport.Y)) / viewport.Height, z,
                             1.0};
        double world[4]{};
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                world[col] += clip[row] * rows[row][col + 4];
        if (std::abs(world[3]) < 1e-12)
            return false;
        result = {static_cast<float>(world[0] / world[3]), static_cast<float>(world[1] / world[3]), static_cast<float>(world[2] / world[3])};
        return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z);
    };
    return unproject(0.0, origin) && unproject(1.0, target);
}

bool CRender::GetAxisDragOffset(const Vector3& world, const Vector3& axis, float pixels, bool vertical, float& offset) const
{
    ZoneScoped;
    D3DMATRIX view{}, projection{};
    D3DVIEWPORT9 viewport{};
    if (!m_device || !d3d9hook::matrices(view, projection) || FAILED(m_device->GetViewport(&viewport)))
        return false;
    const auto matrix = Multiply(view, projection);
    const float w = world.x * matrix._14 + world.y * matrix._24 + world.z * matrix._34 + matrix._44;
    const float axisW = axis.x * matrix._14 + axis.y * matrix._24 + axis.z * matrix._34;
    const float size = static_cast<float>(vertical ? viewport.Height : viewport.Width);
    if (w <= 0.0001f || size <= 0.0f)
        return false;
    const float coordinate = vertical ? world.x * matrix._12 + world.y * matrix._22 + world.z * matrix._32 + matrix._42
                                      : world.x * matrix._11 + world.y * matrix._21 + world.z * matrix._31 + matrix._41;
    const float axisCoordinate = vertical ? axis.x * matrix._12 + axis.y * matrix._22 + axis.z * matrix._32 : axis.x * matrix._11 + axis.y * matrix._21 + axis.z * matrix._31;
    const float target = coordinate / w + pixels * (vertical ? -2.0f : 2.0f) / size;
    const float denominator = axisCoordinate - target * axisW;
    if (std::abs(denominator) <= 0.000001f)
        return false;
    offset = (target * w - coordinate) / denominator;
    return std::isfinite(offset) && w + offset * axisW > 0.0001f;
}

bool CRender::EnsureVertexBuffer(std::size_t vertexCount)
{
    ZoneScoped;
    if (!m_device)
        return false;

    if (m_vertexBuffer && m_vertexCapacity >= vertexCount)
        return true;

    ReleaseVertexBuffer();

    m_vertexCapacity = std::max<std::size_t>(vertexCount, 2048);
    while (m_vertexCapacity < vertexCount)
        m_vertexCapacity *= 2;

    const UINT size = static_cast<UINT>(m_vertexCapacity * sizeof(Vertex));
    if (FAILED(m_device->CreateVertexBuffer(size, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, FVF, D3DPOOL_DEFAULT, &m_vertexBuffer, nullptr)))
    {
        m_vertexCapacity = 0;
        return false;
    }

    return true;
}

HRESULT CRender::Flush()
{
    ZoneScoped;
    if (!m_device || m_vertices.empty())
        return D3D_OK;

    if (!EnsureVertexBuffer(m_vertices.size()))
        return E_FAIL;

    void* data = nullptr;
    const UINT size = static_cast<UINT>(m_vertices.size() * sizeof(Vertex));
    HRESULT result = m_vertexBuffer->Lock(0, size, &data, D3DLOCK_DISCARD);
    if (FAILED(result))
        return result;

    std::memcpy(data, m_vertices.data(), size);
    m_vertexBuffer->Unlock();

    result = m_device->SetStreamSource(0, m_vertexBuffer, 0, sizeof(Vertex));
    if (SUCCEEDED(result))
        result = m_device->SetFVF(FVF);
    if (SUCCEEDED(result))
    {
        result = m_device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(m_vertices.size() / 3));
        PROFILE_ADD(SolidDraws, 1);
        PROFILE_ADD(SolidVertices, m_vertices.size());
    }

    m_vertices.clear();
    return result;
}

void CRender::AddTriangle(const Vertex& a, const Vertex& b, const Vertex& c)
{
    ZoneScoped;
    m_vertices.push_back(a);
    m_vertices.push_back(b);
    m_vertices.push_back(c);
}

void CRender::AddQuad(float left, float top, float right, float bottom, D3DCOLOR color)
{
    ZoneScoped;
    const Vertex a{left, top, 0.0f, 1.0f, color};
    const Vertex b{right, top, 0.0f, 1.0f, color};
    const Vertex c{right, bottom, 0.0f, 1.0f, color};
    const Vertex d{left, bottom, 0.0f, 1.0f, color};

    AddTriangle(a, b, c);
    AddTriangle(a, c, d);
}

void CRender::SaveState()
{
    ZoneScoped;
    ReleaseSavedState();

    m_device->GetTexture(0, &m_savedState.texture0);
    m_device->GetVertexShader(&m_savedState.vertexShader);
    m_device->GetPixelShader(&m_savedState.pixelShader);
    m_device->GetVertexDeclaration(&m_savedState.declaration);
    m_device->GetStreamSource(0, &m_savedState.stream, &m_savedState.streamOffset, &m_savedState.streamStride);
    m_device->GetFVF(&m_savedState.fvf);

    m_device->GetRenderState(D3DRS_ALPHABLENDENABLE, &m_savedState.alphaBlendEnable);
    m_device->GetRenderState(D3DRS_SRCBLEND, &m_savedState.srcBlend);
    m_device->GetRenderState(D3DRS_DESTBLEND, &m_savedState.destBlend);
    m_device->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &m_savedState.separateAlphaBlendEnable);
    m_device->GetRenderState(D3DRS_SRCBLENDALPHA, &m_savedState.srcBlendAlpha);
    m_device->GetRenderState(D3DRS_DESTBLENDALPHA, &m_savedState.destBlendAlpha);
    m_device->GetRenderState(D3DRS_BLENDOP, &m_savedState.blendOp);
    m_device->GetRenderState(D3DRS_BLENDOPALPHA, &m_savedState.blendOpAlpha);
    m_device->GetRenderState(D3DRS_ALPHATESTENABLE, &m_savedState.alphaTestEnable);
    m_device->GetRenderState(D3DRS_ZENABLE, &m_savedState.zEnable);
    m_device->GetRenderState(D3DRS_ZWRITEENABLE, &m_savedState.zWriteEnable);
    m_device->GetRenderState(D3DRS_CULLMODE, &m_savedState.cullMode);
    m_device->GetRenderState(D3DRS_LIGHTING, &m_savedState.lighting);
    m_device->GetRenderState(D3DRS_FOGENABLE, &m_savedState.fogEnable);
    m_device->GetRenderState(D3DRS_SCISSORTESTENABLE, &m_savedState.scissorTestEnable);
    m_device->GetRenderState(D3DRS_MULTISAMPLEANTIALIAS, &m_savedState.multisampleAntialias);
    m_device->GetRenderState(D3DRS_ANTIALIASEDLINEENABLE, &m_savedState.antialiasedLineEnable);
    m_device->GetRenderState(D3DRS_COLORWRITEENABLE, &m_savedState.colorWriteEnable);

    m_device->GetTextureStageState(0, D3DTSS_COLOROP, &m_savedState.colorOp);
    m_device->GetTextureStageState(0, D3DTSS_COLORARG1, &m_savedState.colorArg1);
    m_device->GetTextureStageState(0, D3DTSS_COLORARG2, &m_savedState.colorArg2);
    m_device->GetTextureStageState(0, D3DTSS_ALPHAOP, &m_savedState.alphaOp);
    m_device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &m_savedState.alphaArg1);
    m_device->GetTextureStageState(0, D3DTSS_ALPHAARG2, &m_savedState.alphaArg2);
    m_device->GetTextureStageState(1, D3DTSS_COLOROP, &m_savedState.colorOp1);
    m_device->GetTextureStageState(1, D3DTSS_ALPHAOP, &m_savedState.alphaOp1);
}

void CRender::ApplyState()
{
    ZoneScoped;
    m_device->SetTexture(0, nullptr);
    m_device->SetVertexShader(nullptr);
    m_device->SetPixelShader(nullptr);
    m_device->SetVertexDeclaration(nullptr);
    m_device->SetFVF(FVF);

    m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    m_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    m_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    m_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    m_device->SetRenderState(D3DRS_ZENABLE, FALSE);
    m_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_device->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    m_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    m_device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
    m_device->SetRenderState(D3DRS_ANTIALIASEDLINEENABLE, FALSE);
    m_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);

    m_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    m_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    m_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    m_device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    m_device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

void CRender::RestoreState()
{
    ZoneScoped;
    if (!m_device)
        return;

    m_device->SetTexture(0, m_savedState.texture0);
    m_device->SetVertexShader(m_savedState.vertexShader);
    m_device->SetPixelShader(m_savedState.pixelShader);

    if (m_savedState.declaration)
        m_device->SetVertexDeclaration(m_savedState.declaration);
    else
        m_device->SetFVF(m_savedState.fvf);

    m_device->SetStreamSource(0, m_savedState.stream, m_savedState.streamOffset, m_savedState.streamStride);

    m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, m_savedState.alphaBlendEnable);
    m_device->SetRenderState(D3DRS_SRCBLEND, m_savedState.srcBlend);
    m_device->SetRenderState(D3DRS_DESTBLEND, m_savedState.destBlend);
    m_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, m_savedState.separateAlphaBlendEnable);
    m_device->SetRenderState(D3DRS_SRCBLENDALPHA, m_savedState.srcBlendAlpha);
    m_device->SetRenderState(D3DRS_DESTBLENDALPHA, m_savedState.destBlendAlpha);
    m_device->SetRenderState(D3DRS_BLENDOP, m_savedState.blendOp);
    m_device->SetRenderState(D3DRS_BLENDOPALPHA, m_savedState.blendOpAlpha);
    m_device->SetRenderState(D3DRS_ALPHATESTENABLE, m_savedState.alphaTestEnable);
    m_device->SetRenderState(D3DRS_ZENABLE, m_savedState.zEnable);
    m_device->SetRenderState(D3DRS_ZWRITEENABLE, m_savedState.zWriteEnable);
    m_device->SetRenderState(D3DRS_CULLMODE, m_savedState.cullMode);
    m_device->SetRenderState(D3DRS_LIGHTING, m_savedState.lighting);
    m_device->SetRenderState(D3DRS_FOGENABLE, m_savedState.fogEnable);
    m_device->SetRenderState(D3DRS_SCISSORTESTENABLE, m_savedState.scissorTestEnable);
    m_device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, m_savedState.multisampleAntialias);
    m_device->SetRenderState(D3DRS_ANTIALIASEDLINEENABLE, m_savedState.antialiasedLineEnable);
    m_device->SetRenderState(D3DRS_COLORWRITEENABLE, m_savedState.colorWriteEnable);

    m_device->SetTextureStageState(0, D3DTSS_COLOROP, m_savedState.colorOp);
    m_device->SetTextureStageState(0, D3DTSS_COLORARG1, m_savedState.colorArg1);
    m_device->SetTextureStageState(0, D3DTSS_COLORARG2, m_savedState.colorArg2);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, m_savedState.alphaOp);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, m_savedState.alphaArg1);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, m_savedState.alphaArg2);
    m_device->SetTextureStageState(1, D3DTSS_COLOROP, m_savedState.colorOp1);
    m_device->SetTextureStageState(1, D3DTSS_ALPHAOP, m_savedState.alphaOp1);
}

void CRender::ReleaseSavedState()
{
    ZoneScoped;
    SafeRelease(m_savedState.texture0);
    SafeRelease(m_savedState.vertexShader);
    SafeRelease(m_savedState.pixelShader);
    SafeRelease(m_savedState.declaration);
    SafeRelease(m_savedState.stream);
    m_savedState = {};
}

void CRender::ReleaseVertexBuffer()
{
    ZoneScoped;
    SafeRelease(m_vertexBuffer);
    m_vertexCapacity = 0;
}

D3DMATRIX CRender::Multiply(const D3DMATRIX& a, const D3DMATRIX& b)
{
    ZoneScoped;
    D3DMATRIX out{};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            float value = 0.0f;
            for (int i = 0; i < 4; ++i)
                value += a.m[row][i] * b.m[i][column];
            out.m[row][column] = value;
        }
    }
    return out;
}
