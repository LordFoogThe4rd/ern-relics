#pragma once
#include "ui/editor.hpp"
#include "ui/presets.hpp"
#include <memory>

namespace ern {
// Presentation state belongs to the ImGui context that renders it.
class EditorUi {
  public:
    void initialize(const EditorView &view, float display_height);
    void prepare_frame(const EditorView &view);
    void draw(const EditorView &view, bool commands_blocked, std::optional<EditorAction> &action,
              bool &open);

  private:
    void consume(const EditorView &view);
    enum class Page { Editor, Presets };
    ui::Appearance appearance_;
    std::shared_ptr<const GameAssets> resources_;
    ui::EditorState state_;
    ui::PresetBrowser browser_;
    Page page_ = Page::Editor;
    SessionId session_;
    std::uint64_t completed_ = 0;
};
} // namespace ern
