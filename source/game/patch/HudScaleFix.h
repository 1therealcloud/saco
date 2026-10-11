#pragma once

namespace game::patch::hudscalefix
{
    // Installs a radar entry hook and a scoped crosshair draw wrapper.
    // The hooks are always installed; set_enabled() controls whether scaling is applied.
    bool install();

    void set_enabled(bool enabled);
    bool is_enabled();
}

namespace game::patch::hudscalefix
{
    namespace detail
    {
        using DrawRadarFn = void(__cdecl*)();
        extern DrawRadarFn g_drawRadar;
        struct ScaleState
        {
            float scaleX{};
            float scaleY{};
            float radarWidth{};
            float radarHeight{};
            bool applied{};
        };
    }

    class ScopedScaleFix
    {
    public:
        explicit ScopedScaleFix(bool apply);
        ~ScopedScaleFix();
        ScopedScaleFix(const ScopedScaleFix&) = delete;
        ScopedScaleFix& operator=(const ScopedScaleFix&) = delete;

    private:
        detail::ScaleState m_state{};
    };
}
