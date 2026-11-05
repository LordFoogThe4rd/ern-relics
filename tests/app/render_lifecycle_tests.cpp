#include "app/render_lifecycle.hpp"
#include "support/check.hpp"

int main() {
    return test::run("renderer initialization, replacement, resize and terminal failure", [] {
        using Phase = ern::RenderLifecycle::Phase;
        const ern::SwapChainId first{1}, second{2};
        const ern::WindowId window{1}, other{2};
        ern::RenderLifecycle state;
        test::require(state.phase() == Phase::Waiting && !state.can_render(first),
                      "Rendering started before resources existed");
        test::require(state.activate(first, window) && state.can_render(first) &&
                          !state.can_render(second),
                      "The active swap chain was not exclusive");
        test::require(!state.activate(second, window), "An active renderer was overwritten");
        test::require(!state.begin_replacement(other) && !state.can_render(first),
                      "Unrelated creation released resources or allowed reentrant rendering");
        test::require(state.begin_replacement(window), "Nested replacement lost the active window");
        state.release_active();
        state.end_replacement();
        test::require(state.phase() == Phase::Replacing, "Nested creation resumed rendering early");
        state.end_replacement();
        test::require(state.phase() == Phase::Waiting && state.activate(first, window),
                      "A rejected replacement could not resume the original chain");
        state.begin_replacement(other);
        state.end_replacement();
        test::require(state.can_render(first), "Unrelated creation discarded the active binding");
        state.release_active();
        test::require(state.activate(second, window) && state.can_render(second),
                      "Resize did not permit resource reconstruction");
        state.begin_replacement(window);
        state.fail();
        state.end_replacement();
        test::require(state.phase() == Phase::Failed && !state.can_render(second) &&
                          !state.activate(first, window) && !state.begin_replacement(window),
                      "GPU failure was not terminal");
    });
}
