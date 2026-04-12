#pragma once

// ---------------------------------------------------------------------------
// MessageEnvelope — neutral transport for protocol messages.
//
// The core only ever sees this envelope type. Concrete payload types live
// under core/include/protocol/ and describe contracts between modules.
// No module is obligated to implement a given protocol — envelopes with an
// unknown tag simply go nowhere.
//
// Payload invariant (enforced by the `Message` concept):
//   - compile-time uint32_t `kTag` (unique channel key, typically fnv1a of
//     a "namespace:name" string)
//   - trivially copyable (flat, no owning members)
//   - sizeof <= 32, alignof <= alignof(max_align_t) (fits the SBO buffer)
//
// Usage:
//   struct TrayRebalance {
//       static constexpr uint32_t kTag = fnv1a("bar:tray_rebalance");
//       int monitor_index;
//       int icon_count;
//   };
//   static_assert(Message<TrayRebalance>);
//
//   runtime.post_event(event::CustomEvent{ MessageEnvelope::pack(TrayRebalance{0,3}) });
//
//   void on(const event::CustomEvent& ev) override {
//       if (auto* p = ev.msg.unpack<TrayRebalance>())
//           handle(p->monitor_index, p->icon_count);
//   }
// ---------------------------------------------------------------------------

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

// Compile-time FNV-1a 32-bit hash.
// Used to turn "namespace:name" strings into a stable uint32_t tag at
// compile time. Collisions are effectively impossible with sane naming.
constexpr uint32_t fnv1a(const char* s, uint32_t h = 2166136261u) {
    return (*s == 0) ? h : fnv1a(s + 1, (h ^ static_cast<uint8_t>(*s)) * 16777619u);
}

// Protocol message invariant: every payload type that travels through a
// MessageEnvelope must satisfy this concept. Compilation fails at `pack`
// time otherwise.
template<class T>
concept Message = requires {
    { T::kTag } -> std::convertible_to<uint32_t>;
}
&& std::is_trivially_copyable_v<T>
&& (sizeof(T)  <= 32)
&& (alignof(T) <= alignof(std::max_align_t));

class MessageEnvelope {
    public:
        MessageEnvelope() = default;

        template<Message T>
        static MessageEnvelope pack(const T& payload) {
            MessageEnvelope e;
            e.tag_ = T::kTag;
            std::memcpy(e.storage_, &payload, sizeof(T));
            return e;
        }

        // Returns a pointer to the typed payload iff the tag matches, else null.
        // Caller is expected to inspect multiple types via a cascade of unpack<T>()
        // checks when interested in several protocols on the same channel.
        template<Message T>
        const T* unpack() const {
            if (tag_ != T::kTag)
                return nullptr;
            return std::launder(reinterpret_cast<const T*>(storage_));
        }

        uint32_t tag() const { return tag_; }
        bool     empty() const { return tag_ == 0; }

    private:
        uint32_t tag_ = 0;
        alignas(std::max_align_t) std::byte storage_[32]{};
};
