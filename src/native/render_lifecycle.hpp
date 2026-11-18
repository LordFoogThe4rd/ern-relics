#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace ern {
struct SwapChainId {
    std::uintptr_t value;
    bool operator==(const SwapChainId &) const = default;
};
struct WindowId {
    std::uintptr_t value;
    bool operator==(const WindowId &) const = default;
};

class RenderLifecycle {
  public:
    struct Active {
        SwapChainId chain;
        WindowId window;
    };
    enum class Phase { Waiting, Active, Replacing, Failed };

    Phase phase() const {
        return std::visit([](const auto &state) { return state.phase; }, state_);
    }
    std::optional<Active> active() const {
        if (const auto *state = std::get_if<ActiveState>(&state_))
            return state->binding;
        if (const auto *state = std::get_if<Replacing>(&state_))
            return state->binding;
        return std::nullopt;
    }
    bool can_render(SwapChainId chain) const {
        const auto *state = std::get_if<ActiveState>(&state_);
        return state && state->binding.chain == chain;
    }
    bool activate(SwapChainId chain, WindowId window) {
        if (!std::holds_alternative<Waiting>(state_))
            return false;
        state_ = ActiveState{{chain, window}};
        return true;
    }
    // The caller releases GPU resources before clearing a matching active binding.
    bool begin_replacement(WindowId window) {
        if (phase() == Phase::Failed)
            return false;
        if (auto *state = std::get_if<Replacing>(&state_)) {
            ++state->depth;
            return state->binding && state->binding->window == window;
        }
        const auto binding = active();
        state_ = Replacing{binding, 1};
        return binding && binding->window == window;
    }
    void release_active() {
        if (auto *state = std::get_if<Replacing>(&state_))
            state->binding.reset();
        else if (phase() == Phase::Active)
            state_ = Waiting{};
    }
    void end_replacement() {
        auto *state = std::get_if<Replacing>(&state_);
        if (!state || --state->depth != 0)
            return;
        if (const auto binding = state->binding)
            state_ = ActiveState{*binding};
        else
            state_ = Waiting{};
    }
    void fail() { state_ = Failed{}; }

  private:
    struct Waiting {
        static constexpr auto phase = Phase::Waiting;
    };
    struct ActiveState {
        Active binding;
        static constexpr auto phase = Phase::Active;
    };
    struct Replacing {
        std::optional<Active> binding;
        std::size_t depth;
        static constexpr auto phase = Phase::Replacing;
    };
    struct Failed {
        static constexpr auto phase = Phase::Failed;
    };
    std::variant<Waiting, ActiveState, Replacing, Failed> state_ = Waiting{};
};
} // namespace ern
