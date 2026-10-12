#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ui
{
    extern float opacity;

    class Appearance
    {
    public:
        void SetVisible(bool visible, ULONGLONG now = GetTickCount64());
        float GetOpacity(ULONGLONG now = GetTickCount64()) const;

    private:
        bool m_visible = false;
        ULONGLONG m_started = 0;
    };

    class OpacityScope
    {
    public:
        explicit OpacityScope(float value);
        ~OpacityScope();
        OpacityScope(const OpacityScope&) = delete;
        OpacityScope& operator=(const OpacityScope&) = delete;

    private:
        float m_previous;
    };

    DWORD FadeColor(DWORD color);
}

namespace cgui
{
    constexpr unsigned int EVENT_BUTTON_CLICKED = 0x0101;
    constexpr unsigned int EVENT_COMBOBOX_SELECTION_CHANGED = 0x0201;
    constexpr unsigned int EVENT_RADIOBUTTON_CHANGED = 0x0301;
    constexpr unsigned int EVENT_CHECKBOX_CHANGED = 0x0401;
    constexpr unsigned int EVENT_SLIDER_VALUE_CHANGED = 0x0501;
    constexpr unsigned int EVENT_SLIDER_VALUE_CHANGED_UP = 0x0502;
    constexpr unsigned int EVENT_EDITBOX_STRING = 0x0601;
    constexpr unsigned int EVENT_EDITBOX_CHANGE = 0x0602;
    constexpr unsigned int EVENT_LISTBOX_ITEM_DBLCLK = 0x0701;
    constexpr unsigned int EVENT_LISTBOX_SELECTION = 0x0702;
    constexpr unsigned int EVENT_LISTBOX_SELECTION_END = 0x0703;

    enum class DialogId : std::uint8_t
    {
        GameUI,
        Scoreboard,
        ClassSelection,
        Dialog,
        Secondary,
        Auxiliary,
        Count
    };

    enum class ControlType : std::uint8_t
    {
        Button = 0,
        Static = 1,
        CheckBox = 2,
        RadioButton = 3,
        ComboBox = 4,
        Slider = 5,
        EditBox = 6,
        ImeEditBox = 7,
        ListBox = 8,
        ScrollBar = 9
    };

    enum class ControlState : std::uint8_t
    {
        Normal,
        Disabled,
        Hidden,
        Focus,
        MouseOver,
        Pressed
    };

    struct SkinElement
    {
        RECT texture{};
        D3DCOLOR textureColor = 0xFFFFFFFFu;
        D3DCOLOR fontColor = 0xFFFFFFFFu;
        DWORD textFormat = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
        D3DCOLOR textureStates[6]{};
        D3DCOLOR fontStates[6]{};
    };

    class Dialog;
    class Control;
    class Button;
    class Static;
    class CheckBox;
    class RadioButton;
    class ComboBox;
    class Slider;
    class EditBox;
    class ListBox;
    class ScrollBar;

    using EventCallback = void (*)(unsigned int event, int controlId, Control* control, void* userContext);

    class Control
    {
    public:
        virtual ~Control() = default;

        int GetId() const
        {
            return m_id;
        }
        ControlType GetType() const
        {
            return m_type;
        }
        Dialog* GetDialog() const
        {
            return m_dialog;
        }

        void SetLocation(int x, int y);
        void SetSize(int width, int height);
        RECT GetRect() const;

        void SetVisible(bool visible)
        {
            m_appearance.SetVisible(visible);
            m_visible = visible;
        }
        bool IsVisible() const
        {
            return m_visible;
        }
        float GetOpacity() const
        {
            m_appearance.SetVisible(m_visible);
            return m_appearance.GetOpacity();
        }
        void SetEnabled(bool enabled)
        {
            m_enabled = enabled;
        }
        bool IsEnabled() const
        {
            return m_enabled;
        }
        bool HasFocus() const
        {
            return m_focus;
        }
        bool IsMouseOver() const
        {
            return m_mouseOver;
        }
        bool IsPressed() const
        {
            return m_pressed;
        }

        void SetText(const char* text);
        const char* GetText() const
        {
            return m_text.c_str();
        }
        void SetUserData(void* data)
        {
            m_userData = data;
        }
        void* GetUserData() const
        {
            return m_userData;
        }

        bool Contains(int x, int y) const;
        virtual void Render();
        virtual bool MsgProc(UINT message, WPARAM wParam, LPARAM lParam);

    protected:
        friend class Dialog;
        Control(ControlType type, int id, const char* text, int x, int y, int width, int height);

        virtual void OnFocus(bool focus);
        ControlState GetState() const;
        RECT GetScreenRect() const;
        void SendEvent(unsigned int event);

        Dialog* m_dialog = nullptr;
        ControlType m_type{};
        int m_id = 0;
        int m_x = 0;
        int m_y = 0;
        int m_width = 0;
        int m_height = 0;
        bool m_visible = true;
        mutable ui::Appearance m_appearance;
        bool m_enabled = true;
        bool m_focus = false;
        bool m_mouseOver = false;
        bool m_pressed = false;
        std::string m_text;
        void* m_userData = nullptr;
    };

    class Button final : public Control
    {
    public:
        Button(int id, const char* text, int x, int y, int width, int height);
        void Render() override;
        bool MsgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

    private:
        float m_colors[2][2][4]{};
        ULONGLONG m_renderTick = 0;
    };

    class Static final : public Control
    {
    public:
        Static(int id, const char* text, int x, int y, int width, int height);
        void Render() override;
    };

    class CheckBox final : public Control
    {
    public:
        CheckBox(int id, const char* text, int x, int y, int width, int height, bool checked = false);
        bool IsChecked() const
        {
            return m_checked;
        }
        void SetChecked(bool checked)
        {
            m_checked = checked;
        }
        void Render() override;
        bool MsgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

    private:
        bool m_checked = false;
    };

    class RadioButton final : public Control
    {
    public:
        RadioButton(int id, unsigned int group, const char* text, int x, int y, int width, int height, bool checked = false);
        unsigned int GetGroup() const
        {
            return m_group;
        }
        bool IsChecked() const
        {
            return m_checked;
        }
        void SetChecked(bool checked)
        {
            m_checked = checked;
        }
        void Render() override;
        bool MsgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

    private:
        unsigned int m_group = 0;
        bool m_checked = false;
    };

    class ComboBox final : public Control
    {
    public:
        ComboBox(int id, int x, int y, int width, int height);
        void AddItem(const char* text, void* data = nullptr);
        void RemoveAllItems();
        int GetSelectedIndex() const
        {
            return m_selected;
        }
        const char* GetSelectedText() const;
        void SetSelectedIndex(int index);
        void Render() override;
        bool MsgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

    private:
        struct Item
        {
            std::string text;
            void* data = nullptr;
        };
        std::vector<Item> m_items;
        int m_selected = -1;
        bool m_open = false;
    };

    class Slider final : public Control
    {
    public:
        Slider(int id, int x, int y, int width, int height, int minValue = 0, int maxValue = 100, int value = 50);
        void SetRange(int minValue, int maxValue);
        void SetValue(int value);
        int GetValue() const
        {
            return m_value;
        }
        void Render() override;
        bool MsgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

    private:
        void SetValueFromPoint(int x);
        int m_min = 0;
        int m_max = 100;
        int m_value = 50;
        bool m_dragging = false;
    };

    class EditBox final : public Control
    {
    public:
        EditBox(int id, const char* text, int x, int y, int width, int height);
        ~EditBox() override;

        void SetText(const char* text);
        bool InsertText(const wchar_t* text);
        RECT GetImeRect() const;
        RECT GetImeTextRect() const;
        void OnFocus(bool focus) override;
        const char* GetText() const
        {
            return m_buffer.c_str();
        }
        void SetMaxLength(std::size_t length);
        void SetPassword(bool password);
        bool IsPassword() const
        {
            return m_password;
        }
        std::size_t GetMaxLength() const
        {
            return m_maxLength;
        }
        std::size_t GetCaret() const
        {
            return m_caret;
        }
        void SetCaret(std::size_t caret);
        void SelectAll();
        void ClearSelection();
        void SetTextColor(D3DCOLOR color)
        {
            m_textColor = color;
        }
        void SetSelectionColor(D3DCOLOR color)
        {
            m_selectionColor = color;
        }
        void SetCaretColor(D3DCOLOR color)
        {
            m_caretColor = color;
        }

        void Render() override;
        bool MsgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

    private:
        bool HasSelection() const
        {
            return m_selectionAnchor != m_caret;
        }
        bool CopySelection();
        void PasteText();
        void DeleteSelection();
        void InsertChar(char value);
        void MoveCaret(int delta, bool selecting);
        std::size_t WordBoundary(int direction) const;
        int TextWidth(std::size_t bytes) const;
        void UpdateScroll() const;
        mutable std::size_t m_firstVisible = 0;
        mutable std::string m_widthText;
        mutable std::string m_masked;
        mutable std::vector<int> m_prefixWidths;
        mutable unsigned int m_widthRevision = 0;
        mutable int m_widthPasswordSize = -1;
        IDirect3DTexture9* m_textTexture = nullptr;
        std::string m_cachedText;
        std::string m_cachedFace;
        int m_cachedHeight = 0;
        int m_cachedWeight = 0;
        SIZE m_textExtent{};

        std::string m_buffer;
        std::size_t m_maxLength = 128;
        std::size_t m_caret = 0;
        std::size_t m_selectionAnchor = 0;
        D3DCOLOR m_textColor = 0xFFFFFFFFu;
        D3DCOLOR m_selectionColor = 0xFFB92228u;
        D3DCOLOR m_caretColor = 0xFF969696u;
        bool m_password = false;
    };

    class ListBox final : public Control
    {
    public:
        void SetVisibleRowCount(int count);
        ListBox(int id, int x, int y, int width, int height);
        void AddItem(const char* text, void* data = nullptr);
        void SetItemText(int index, unsigned int column, const char* text);
        void SetColumnWidth(unsigned int column, int width);
        void SetTextPadding(int padding);
        void RemoveAllItems();
        void RemoveItem(int index);
        int GetTopIndex() const
        {
            return m_top;
        }
        void SetTopIndex(int index);
        int GetSelectedIndex() const
        {
            return m_selected;
        }
        void SetSelectedIndex(int index);
        const char* GetSelectedText(unsigned int column = 0) const;
        void Render() override;
        bool MsgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

    private:
        struct Item
        {
            std::string text;
            void* data = nullptr;
            std::vector<std::string> columns;
        };
        std::vector<Item> m_items;
        std::vector<int> m_columnWidths;
        int m_textPadding = 4;
        int m_visibleRowCount = 0;
        int GetVisibleRowCount() const;
        int m_selected = -1;
        int m_top = 0;
        DWORD m_lastClickTick = 0;
        int m_lastClickIndex = -1;
    };

    class ScrollBar final : public Control
    {
    public:
        ScrollBar(int id, int x, int y, int width, int height);
        void SetRange(int start, int end);
        void SetPageSize(int pageSize);
        void SetTrackPos(int position);
        int GetTrackPos() const
        {
            return m_position;
        }
        int GetPageSize() const
        {
            return m_pageSize;
        }
        void Scroll(int delta);
        void Render() override;
        bool MsgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

    private:
        int ClampPosition(int value) const;
        RECT GetUpRect() const;
        RECT GetDownRect() const;
        RECT GetTrackRect() const;
        RECT GetThumbRect() const;
        void SetFromPoint(int y);
        void RepeatArrow();

        int m_start = 0;
        int m_end = 100;
        int m_pageSize = 10;
        int m_position = 0;
        bool m_dragging = false;
        int m_dragOffset = 0;
        int m_arrowDirection = 0;
        bool m_arrowRepeating = false;
        ULONGLONG m_arrowTick = 0;
        POINT m_lastMouse{};
    };

    class Dialog
    {
    public:
        Dialog() = default;
        ~Dialog() = default;

        void Reset();
        void SetLocation(int x, int y);
        void GetLocation(POINT& point) const;
        void SetSize(int width, int height);
        int GetWidth() const
        {
            return m_width;
        }
        int GetHeight() const
        {
            return m_height;
        }
        RECT GetRect() const;
        void SetVisible(bool visible)
        {
            m_appearance.SetVisible(visible);
            m_visible = visible;
        }
        float GetOpacity() const
        {
            m_appearance.SetVisible(m_visible);
            return m_appearance.GetOpacity();
        }
        void RestartAppearance()
        {
            m_appearance.SetVisible(false);
            m_appearance.SetVisible(m_visible);
        }
        bool IsVisible() const
        {
            return m_visible;
        }
        void EnableMouseInput(bool enabled)
        {
            m_mouseInput = enabled;
        }
        void EnableKeyboardInput(bool enabled)
        {
            m_keyboardInput = enabled;
        }
        void SetBackgroundColors(D3DCOLOR color);
        void SetBackgroundColors(D3DCOLOR topLeft, D3DCOLOR topRight, D3DCOLOR bottomLeft, D3DCOLOR bottomRight);
        void SetCallback(EventCallback callback, void* userContext = nullptr);

        Button* AddButton(int id, const char* text, int x, int y, int width, int height);
        Static* AddStatic(int id, const char* text, int x, int y, int width, int height);
        CheckBox* AddCheckBox(int id, const char* text, int x, int y, int width, int height, bool checked = false);
        RadioButton* AddRadioButton(int id, unsigned int group, const char* text, int x, int y, int width, int height, bool checked = false);
        ComboBox* AddComboBox(int id, int x, int y, int width, int height);
        Slider* AddSlider(int id, int x, int y, int width, int height, int minValue = 0, int maxValue = 100, int value = 50);
        EditBox* AddEditBox(int id, const char* text, int x, int y, int width, int height);
        ListBox* AddListBox(int id, int x, int y, int width, int height);
        ScrollBar* AddScrollBar(int id, int x, int y, int width, int height);

        Control* GetControl(int id) const;
        template <typename T>
        T* GetControl(int id) const
        {
            return dynamic_cast<T*>(GetControl(id));
        }
        void RemoveControl(int id);
        void RemoveAllControls();
        void ClearRadioButtonGroup(unsigned int group, RadioButton* except = nullptr);
        void RequestFocus(Control* control);
        Control* GetFocus() const
        {
            return m_focus;
        }

        bool MsgProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        void Render();
        void SendEvent(unsigned int event, Control* control);

    private:
        template <typename T, typename... Args>
        T* AddControl(Args&&... args)
        {
            auto control = std::make_unique<T>(std::forward<Args>(args)...);
            T* result = control.get();
            result->m_dialog = this;
            m_controls.emplace_back(std::move(control));
            return result;
        }

        Control* GetControlAtPoint(int x, int y) const;

        int m_x = 0;
        int m_y = 0;
        int m_width = 0;
        int m_height = 0;
        bool m_visible = true;
        mutable ui::Appearance m_appearance;
        bool m_mouseInput = true;
        bool m_keyboardInput = true;
        D3DCOLOR m_background[4]{};
        std::vector<std::unique_ptr<Control>> m_controls;
        Control* m_focus = nullptr;
        Control* m_mouseOver = nullptr;
        Control* m_pressed = nullptr;
        EventCallback m_callback = nullptr;
        void* m_callbackContext = nullptr;
    };

    bool Initialize(IDirect3DDevice9* device, const char* assetDirectory = nullptr);
    void Shutdown();
    void Render();
    void RenderCursor(int x, int y);
    void BeginFrame();
    RECT GetClientRect();
    void OnLostDevice();
    void OnResetDevice();
    bool MsgProc(DialogId id, HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    bool MsgProcAll(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    bool IsInitialized();
    IDirect3DDevice9* GetDevice();
    IDirect3DTexture9* GetSkinTexture();
    IDirect3DTexture9* GetCursorTexture();
    Dialog* GetDialog(DialogId id);
    const SkinElement* GetDefaultElement(ControlType type, unsigned int element);
    void SetAssetDirectory(const char* directory);
    const char* GetAssetDirectory();
    void SetFont(const char* face, int height, int weight);
    void ApplyR5FontConfig(int fontSize, int fontWeight, const char* fontFace);
    const char* GetFontFace();
    int GetFontHeight();
    int GetFontWeight();

}

class CGui final
{
public:
    using DialogId = cgui::DialogId;
    using ControlType = cgui::ControlType;
    using ControlState = cgui::ControlState;
    using SkinElement = cgui::SkinElement;
    using Control = cgui::Control;
    using Button = cgui::Button;
    using Static = cgui::Static;
    using CheckBox = cgui::CheckBox;
    using RadioButton = cgui::RadioButton;
    using ComboBox = cgui::ComboBox;
    using Slider = cgui::Slider;
    using EditBox = cgui::EditBox;
    using ListBox = cgui::ListBox;
    using ScrollBar = cgui::ScrollBar;
    using Dialog = cgui::Dialog;
    using EventCallback = cgui::EventCallback;

    static constexpr unsigned int EVENT_BUTTON_CLICKED = cgui::EVENT_BUTTON_CLICKED;
    static constexpr unsigned int EVENT_COMBOBOX_SELECTION_CHANGED = cgui::EVENT_COMBOBOX_SELECTION_CHANGED;
    static constexpr unsigned int EVENT_RADIOBUTTON_CHANGED = cgui::EVENT_RADIOBUTTON_CHANGED;
    static constexpr unsigned int EVENT_CHECKBOX_CHANGED = cgui::EVENT_CHECKBOX_CHANGED;
    static constexpr unsigned int EVENT_SLIDER_VALUE_CHANGED = cgui::EVENT_SLIDER_VALUE_CHANGED;
    static constexpr unsigned int EVENT_SLIDER_VALUE_CHANGED_UP = cgui::EVENT_SLIDER_VALUE_CHANGED_UP;
    static constexpr unsigned int EVENT_EDITBOX_STRING = cgui::EVENT_EDITBOX_STRING;
    static constexpr unsigned int EVENT_EDITBOX_CHANGE = cgui::EVENT_EDITBOX_CHANGE;
    static constexpr unsigned int EVENT_LISTBOX_ITEM_DBLCLK = cgui::EVENT_LISTBOX_ITEM_DBLCLK;
    static constexpr unsigned int EVENT_LISTBOX_SELECTION = cgui::EVENT_LISTBOX_SELECTION;
    static constexpr unsigned int EVENT_LISTBOX_SELECTION_END = cgui::EVENT_LISTBOX_SELECTION_END;

    static bool Initialize(IDirect3DDevice9* device, const char* assetDirectory = nullptr)
    {
        return cgui::Initialize(device, assetDirectory);
    }

    static void BeginFrame()
    {
        cgui::BeginFrame();
    }
    static RECT GetClientRect()
    {
        return cgui::GetClientRect();
    }

    static void Shutdown()
    {
        cgui::Shutdown();
    }
    static void Render()
    {
        cgui::Render();
    }
    static void RenderCursor(int x, int y)
    {
        cgui::RenderCursor(x, y);
    }
    static void OnLostDevice()
    {
        cgui::OnLostDevice();
    }
    static void OnResetDevice()
    {
        cgui::OnResetDevice();
    }

    static bool MsgProc(DialogId id, HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        return cgui::MsgProc(id, window, message, wParam, lParam);
    }

    static bool MsgProcAll(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        return cgui::MsgProcAll(window, message, wParam, lParam);
    }

    static bool IsInitialized()
    {
        return cgui::IsInitialized();
    }
    static IDirect3DDevice9* GetDevice()
    {
        return cgui::GetDevice();
    }
    static IDirect3DTexture9* GetSkinTexture()
    {
        return cgui::GetSkinTexture();
    }
    static IDirect3DTexture9* GetCursorTexture()
    {
        return cgui::GetCursorTexture();
    }
    static Dialog* GetDialog(DialogId id)
    {
        return cgui::GetDialog(id);
    }
    static const SkinElement* GetDefaultElement(ControlType type, unsigned int element)
    {
        return cgui::GetDefaultElement(type, element);
    }

    static void SetAssetDirectory(const char* directory)
    {
        cgui::SetAssetDirectory(directory);
    }
    static const char* GetAssetDirectory()
    {
        return cgui::GetAssetDirectory();
    }
    static void SetFont(const char* face, int height, int weight)
    {
        cgui::SetFont(face, height, weight);
    }
    static void ApplyR5FontConfig(int fontSize, int fontWeight, const char* fontFace)
    {
        cgui::ApplyR5FontConfig(fontSize, fontWeight, fontFace);
    }
    static const char* GetFontFace()
    {
        return cgui::GetFontFace();
    }
    static int GetFontHeight()
    {
        return cgui::GetFontHeight();
    }
    static int GetFontWeight()
    {
        return cgui::GetFontWeight();
    }

    CGui() = delete;
};
