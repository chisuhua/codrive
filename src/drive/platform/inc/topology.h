#pragma once

namespace hcs {
/// @brief Initializes the runtime.
/// Should not be called directly, must be called only from Runtime::Acquire()
bool Load();

/// @brief Shutdown/cleanup of runtime.
/// Should not be called directly, must be called only from Runtime::Release()
bool Unload();
}  // namespace

