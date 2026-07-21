// Archive.cpp — MemoryStream 구현 (M3-A 골격)
#include "mye/ser/Archive.h"

#include <cstring>

namespace mye::ser {

void MemoryStream::Write(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::byte*>(data);
    m_buffer.insert(m_buffer.end(), p, p + size);
}

Expected<void, Error> MemoryStream::Read(void* out, std::size_t size) {
    if (m_cursor + size > m_buffer.size()) {
        return Error{"MemoryStream::Read: out of range", 1};
    }
    std::memcpy(out, m_buffer.data() + m_cursor, size);
    m_cursor += size;
    return {};
}

} // namespace mye::ser
