#pragma once

namespace wl {

template<typename RawT>
class Protocol {
public:
    using raw_type = RawT;

    Protocol() = default;
    explicit Protocol(raw_type* raw) noexcept : raw_(raw) {}

    explicit operator bool() const noexcept { return raw_ != nullptr; }

    raw_type* raw() const noexcept { return raw_; }
    void reset() noexcept { raw_ = nullptr; }

protected:
    raw_type* raw_ = nullptr;
};

} // namespace wl
