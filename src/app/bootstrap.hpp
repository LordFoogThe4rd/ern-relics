#pragma once

#include <Windows.h>

namespace ern {
bool bootstrap_application(HMODULE module);
bool test_host_attached();
} // namespace ern
