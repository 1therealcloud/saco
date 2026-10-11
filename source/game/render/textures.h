#pragma once

#include <memory>

struct RwTexture;
struct RwTexDictionary;

namespace game::render::textures
{
    using TextureHandle = RwTexture;

    // use on the game thread after RenderWare and CTxdStore are initialized
    struct TextureDeleter
    {
        void operator()(RwTexture* texture) const noexcept;
    };
    using Texture = std::unique_ptr<RwTexture, TextureDeleter>;

    int find_slot(const char* name);
    // creates a new slot; never replaces an existing dictionary with the same name
    int load_dictionary(const char* name, const char* path);
    // fails while the dictionary has external CTxdStore references
    bool remove_dictionary(int slot);
    void add_ref(int slot);
    void remove_ref(int slot);
    Texture read(int slot, const char* name, const char* mask = nullptr);

    class DictionaryScope
    {
    public:
        explicit DictionaryScope(int slot);
        ~DictionaryScope();
        DictionaryScope(const DictionaryScope&) = delete;
        DictionaryScope& operator=(const DictionaryScope&) = delete;
        explicit operator bool() const noexcept
        {
            return m_valid;
        }

    private:
        RwTexDictionary* m_previous = nullptr;
        bool m_valid = false;
    };
}
