#pragma once

#if defined(_DEBUG) && defined(SACO_BUILD)
struct IDirect3DDevice9;

namespace profiler
{
    enum class Section
    {
        Process,
        Network,
        Receive,
        LocalPlayer,
        RemotePlayers,
        Objects,
        Vehicles,
        Pickups,
        Audio,
        Removal,
        Ui,
        Materials,
        Previews,
        Tags,
        Bubbles,
        Labels,
        Chat,
        Scoreboard,
        Controls,
        Dialog,
        DeathWindow,
        ObjectEdit,
        Menus,
        TextDraws,
        Ime,
        PedControl,
        World,
        Fading,
        Present,
        Overlay,
        Count
    };
    enum class Counter
    {
        FontDraws,
        FontVertices,
        GuiDraws,
        GuiVertices,
        SolidDraws,
        SolidVertices,
        ImeDraws,
        CacheHits,
        CacheMisses,
        Packets,
        PacketBytes,
        Count
    };

    class Scope
    {
    public:
        explicit Scope(Section section);
        ~Scope();
        void Stop();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        Section m_section;
        long long m_start = 0;
    };

    void BeginFrame();
    void EndFrame();
    bool Visible();
    void Draw(IDirect3DDevice9* device);
    void Add(Counter counter, unsigned long long amount = 1);
}
#   define PROFILE_JOIN_INNER(a, b) a##b
#   define PROFILE_JOIN(a, b) PROFILE_JOIN_INNER(a, b)
#   define PROFILE_SCOPE(section) profiler::Scope PROFILE_JOIN(profileScope, __LINE__)(profiler::Section::section)
#   define PROFILE_ADD(counter, amount) profiler::Add(profiler::Counter::counter, amount)
#else
#   define PROFILE_SCOPE(section) ((void)0)
#   define PROFILE_ADD(counter, amount) ((void)0)
#endif
