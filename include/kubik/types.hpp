#pragma once

#include <cstddef>
#include <vector>

#include <segyio/segy.h>

namespace kubik {

constexpr std::size_t kTraceHeaderSize = SEGY_TRACE_HEADER_SIZE;
constexpr std::size_t kTextHeaderSize = SEGY_TEXT_HEADER_SIZE;
constexpr std::size_t kBinaryHeaderSize = SEGY_BINARY_HEADER_SIZE;

struct FileHeaders {
    char textual[kTextHeaderSize] = {};
    char binary[kBinaryHeaderSize] = {};
};

enum class SliceMode {
    Inline,
    Crossline,
    Time
};

}  // namespace kubik
