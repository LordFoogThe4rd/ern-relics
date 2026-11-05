#pragma once
#include <Windows.h>

namespace ern {
bool install_input_hooks();
// Call from the overlay's serialized UI/render path.
void set_input_capture(bool enabled, HWND window);
} // namespace ern
