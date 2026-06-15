#pragma once

#include <cstdint>

namespace kubik {

/// Доступная физическая память (байты), 0 если не удалось определить.
std::uint64_t availablePhysicalBytes();

}  // namespace kubik
