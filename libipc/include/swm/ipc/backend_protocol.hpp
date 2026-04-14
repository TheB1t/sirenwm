#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace swm::ipc {

inline constexpr uint32_t kBackendProtocolMagic   = 0x53495043; // "SIPC"
inline constexpr uint16_t kBackendProtocolVersion = 1;

inline constexpr std::size_t kMaxOutputNameBytes    = 64;
inline constexpr std::size_t kMaxSurfaceAppIdBytes  = 96;
inline constexpr std::size_t kMaxSurfaceTitleBytes  = 160;
inline constexpr std::size_t kMaxKeyIntercepts      = 64;

template <std::size_t N>
struct FixedString {
    char bytes[N] = {};

    void assign(const char* value) {
        if (!value || N == 0)
            return;
        auto len = std::strlen(value);
        if (len >= N)
            len = N - 1;
        if (len > 0)
            std::memcpy(bytes, value, len);
        bytes[len] = '\0';
    }
};

enum class BackendPeerRole : uint32_t {
    WmController      = 1,
    DisplayServerHost = 2,
};

enum class BackendMessageType : uint16_t {
    Hello = 1,
    SnapshotRequest,
    SnapshotBegin,
    SnapshotEnd,
    OutputAdded,
    OutputRemoved,
    SurfaceCreated,
    SurfaceMapped,
    SurfaceUnmapped,
    SurfaceDestroyed,
    SurfaceCommitted,
    SurfaceTitleChanged,
    SurfaceAppIdChanged,
    Key,
    Button,
    PointerMotion,
    PointerEnter,
    ConfigureSurface,
    SetSurfaceVisible,
    SetSurfaceBorder,
    SetSurfaceActivated,
    SetSurfaceStacking,
    CloseSurface,
    WarpPointer,
    SetKeyboardIntercepts,
    GrabPointer,
    UngrabPointer,
    CreateOverlay,
    UpdateOverlay,
    DestroyOverlay,
    OverlayExpose,
    OverlayButton,
    OverlayReleased,
};

struct MessageHeader {
    uint32_t           magic   = kBackendProtocolMagic;
    uint16_t           version = kBackendProtocolVersion;
    BackendMessageType type    = BackendMessageType::Hello;
    uint32_t           size    = 0;
};

template <typename T>
inline constexpr bool is_wire_payload_v =
    std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;

struct Hello {
    BackendPeerRole peer_role = BackendPeerRole::WmController;
    uint32_t        flags     = 0;
};

struct SnapshotRequest {
    uint32_t reserved = 0;
};

struct SnapshotBegin {
    uint32_t generation = 0;
};

struct SnapshotEnd {
    uint32_t generation = 0;
};

struct OutputAdded {
    uint32_t                         id      = 0;
    FixedString<kMaxOutputNameBytes> name;
    int32_t                          x       = 0;
    int32_t                          y       = 0;
    int32_t                          width   = 0;
    int32_t                          height  = 0;
    int32_t                          refresh = 0;
};

struct OutputRemoved {
    uint32_t id = 0;
};

struct SurfaceCreated {
    uint32_t                          id    = 0;
    FixedString<kMaxSurfaceAppIdBytes> app_id;
    FixedString<kMaxSurfaceTitleBytes> title;
    uint32_t                          pid   = 0;
};

struct SurfaceMapped {
    uint32_t id = 0;
};

struct SurfaceUnmapped {
    uint32_t id = 0;
};

struct SurfaceDestroyed {
    uint32_t id = 0;
};

struct SurfaceCommitted {
    uint32_t id     = 0;
    int32_t  width  = 0;
    int32_t  height = 0;
};

struct SurfaceTitleChanged {
    uint32_t                           id = 0;
    FixedString<kMaxSurfaceTitleBytes> title;
};

struct SurfaceAppIdChanged {
    uint32_t                           id = 0;
    FixedString<kMaxSurfaceAppIdBytes> app_id;
};

struct Key {
    uint32_t keycode = 0;
    uint32_t keysym  = 0;
    uint32_t mods    = 0;
    uint32_t pressed = 0;
};

struct Button {
    uint32_t surface_id = 0;
    int32_t  x          = 0;
    int32_t  y          = 0;
    uint32_t button     = 0;
    uint32_t mods       = 0;
    uint32_t released   = 0;
};

struct PointerMotion {
    uint32_t surface_id = 0;
    int32_t  x          = 0;
    int32_t  y          = 0;
    uint32_t mods       = 0;
};

struct PointerEnter {
    uint32_t surface_id = 0;
};

struct ConfigureSurface {
    uint32_t surface_id = 0;
    int32_t  x          = 0;
    int32_t  y          = 0;
    int32_t  width      = 0;
    int32_t  height     = 0;
};

struct SetSurfaceVisible {
    uint32_t surface_id = 0;
    uint32_t visible    = 0;
};

struct SetSurfaceBorder {
    uint32_t surface_id = 0;
    uint32_t width      = 0;
    uint32_t color      = 0;
};

struct SetSurfaceActivated {
    uint32_t surface_id = 0;
    uint32_t activated  = 0;
};

struct SetSurfaceStacking {
    uint32_t surface_id = 0;
    uint32_t raised     = 0;
};

struct CloseSurface {
    uint32_t surface_id = 0;
};

struct WarpPointer {
    int32_t x = 0;
    int32_t y = 0;
};

struct KeyIntercept {
    uint32_t keysym = 0;
    uint32_t mods   = 0;
};

struct SetKeyboardIntercepts {
    uint32_t                                      count = 0;
    std::array<KeyIntercept, kMaxKeyIntercepts> intercepts {};
};

struct GrabPointer {
    uint32_t reserved = 0;
};

struct UngrabPointer {
    uint32_t reserved = 0;
};

struct CreateOverlay {
    uint32_t overlay_id = 0;
    int32_t  x          = 0;
    int32_t  y          = 0;
    int32_t  width      = 0;
    int32_t  height     = 0;
};

struct UpdateOverlay {
    uint32_t overlay_id = 0;
    uint32_t bytes      = 0;
};

struct DestroyOverlay {
    uint32_t overlay_id = 0;
};

struct OverlayExpose {
    uint32_t overlay_id = 0;
};

struct OverlayButton {
    uint32_t overlay_id = 0;
    int32_t  x          = 0;
    int32_t  y          = 0;
    uint32_t button     = 0;
    uint32_t released   = 0;
};

struct OverlayReleased {
    uint32_t overlay_id = 0;
    uint32_t serial     = 0;
};

template <typename Payload>
struct MessageTraits;

#define SWM_IPC_BACKEND_SESSION_MESSAGES(X) \
    X(Hello, Hello) \
    X(SnapshotRequest, SnapshotRequest) \
    X(SnapshotBegin, SnapshotBegin) \
    X(SnapshotEnd, SnapshotEnd)

#define SWM_IPC_BACKEND_EVENT_MESSAGES(X) \
    X(OutputAdded, OutputAdded) \
    X(OutputRemoved, OutputRemoved) \
    X(SurfaceCreated, SurfaceCreated) \
    X(SurfaceMapped, SurfaceMapped) \
    X(SurfaceUnmapped, SurfaceUnmapped) \
    X(SurfaceDestroyed, SurfaceDestroyed) \
    X(SurfaceCommitted, SurfaceCommitted) \
    X(SurfaceTitleChanged, SurfaceTitleChanged) \
    X(SurfaceAppIdChanged, SurfaceAppIdChanged) \
    X(Key, Key) \
    X(Button, Button) \
    X(PointerMotion, PointerMotion) \
    X(PointerEnter, PointerEnter) \
    X(OverlayExpose, OverlayExpose) \
    X(OverlayButton, OverlayButton) \
    X(OverlayReleased, OverlayReleased)

#define SWM_IPC_BACKEND_COMMAND_MESSAGES(X) \
    X(ConfigureSurface, ConfigureSurface) \
    X(SetSurfaceVisible, SetSurfaceVisible) \
    X(SetSurfaceBorder, SetSurfaceBorder) \
    X(SetSurfaceActivated, SetSurfaceActivated) \
    X(SetSurfaceStacking, SetSurfaceStacking) \
    X(CloseSurface, CloseSurface) \
    X(WarpPointer, WarpPointer) \
    X(SetKeyboardIntercepts, SetKeyboardIntercepts) \
    X(GrabPointer, GrabPointer) \
    X(UngrabPointer, UngrabPointer) \
    X(CreateOverlay, CreateOverlay) \
    X(UpdateOverlay, UpdateOverlay) \
    X(DestroyOverlay, DestroyOverlay)

#define SWM_IPC_BACKEND_MESSAGES(X) \
    SWM_IPC_BACKEND_SESSION_MESSAGES(X) \
    SWM_IPC_BACKEND_EVENT_MESSAGES(X) \
    SWM_IPC_BACKEND_COMMAND_MESSAGES(X)

#define SWM_IPC_DECLARE_TRAIT(TypeName, MessageKind) \
    template <> \
    struct MessageTraits<TypeName> { \
        static constexpr BackendMessageType type = BackendMessageType::MessageKind; \
    };

SWM_IPC_BACKEND_MESSAGES(SWM_IPC_DECLARE_TRAIT)

#undef SWM_IPC_DECLARE_TRAIT

template <typename Payload>
struct Envelope {
    static_assert(is_wire_payload_v<Payload>, "IPC payload must be POD-like");

    MessageHeader header {
        kBackendProtocolMagic,
        kBackendProtocolVersion,
        MessageTraits<Payload>::type,
        static_cast<uint32_t>(sizeof(Payload))
    };
    Payload payload {};
};

template <typename Payload>
constexpr std::size_t wire_size() {
    return sizeof(MessageHeader) + sizeof(Payload);
}

template <typename Payload>
Envelope<Payload> make_message(const Payload& payload) {
    Envelope<Payload> envelope;
    envelope.payload = payload;
    return envelope;
}

#define SWM_IPC_ASSERT_WIRE(TypeName, MessageKind) \
    static_assert(is_wire_payload_v<TypeName>, #TypeName " must stay trivially copyable");

SWM_IPC_BACKEND_MESSAGES(SWM_IPC_ASSERT_WIRE)

#undef SWM_IPC_ASSERT_WIRE

} // namespace swm::ipc
