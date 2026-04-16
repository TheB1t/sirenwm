#include <swm/ipc/backend_protocol.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <type_traits>

using namespace swm::ipc;

// --- FixedString -----------------------------------------------------------

TEST(FixedString, AssignNullIsNoOp) {
    FixedString<16> s{};
    s.assign(nullptr);
    EXPECT_EQ(s.bytes[0], '\0');
}

TEST(FixedString, AssignShortStringNullTerminates) {
    FixedString<16> s{};
    s.assign("abc");
    EXPECT_STREQ(s.bytes, "abc");
    EXPECT_EQ(s.bytes[3], '\0');
}

TEST(FixedString, AssignTruncatesAtCapacityMinusOne) {
    FixedString<4> s{};
    s.assign("abcdef");
    EXPECT_EQ(s.bytes[0], 'a');
    EXPECT_EQ(s.bytes[1], 'b');
    EXPECT_EQ(s.bytes[2], 'c');
    EXPECT_EQ(s.bytes[3], '\0');
}

TEST(FixedString, AssignExactFitLeavesTerminator) {
    FixedString<4> s{};
    s.assign("abc");
    EXPECT_EQ(s.bytes[3], '\0');
}

// --- MessageTraits ---------------------------------------------------------

TEST(MessageTraits, CoreTypesMapToExpectedEnum) {
    EXPECT_EQ(MessageTraits<Hello>::type, BackendMessageType::Hello);
    EXPECT_EQ(MessageTraits<SnapshotRequest>::type, BackendMessageType::SnapshotRequest);
    EXPECT_EQ(MessageTraits<OutputAdded>::type, BackendMessageType::OutputAdded);
    EXPECT_EQ(MessageTraits<SurfaceCreated>::type, BackendMessageType::SurfaceCreated);
    EXPECT_EQ(MessageTraits<ConfigureSurface>::type, BackendMessageType::ConfigureSurface);
}

TEST(MessageTraits, EveryEnumeratedPayloadHasTrait) {
#define SWM_IPC_CHECK_TRAIT(TypeName, MessageKind) \
    EXPECT_EQ(MessageTraits<TypeName>::type, BackendMessageType::MessageKind);
    SWM_IPC_BACKEND_MESSAGES(SWM_IPC_CHECK_TRAIT)
#undef SWM_IPC_CHECK_TRAIT
}

// --- Wire-payload invariants ----------------------------------------------

TEST(WirePayload, EveryEnumeratedPayloadIsTriviallyCopyable) {
#define SWM_IPC_CHECK_POD(TypeName, MessageKind) \
    EXPECT_TRUE(is_wire_payload_v<TypeName>) << # TypeName " must be POD-like";
    SWM_IPC_BACKEND_MESSAGES(SWM_IPC_CHECK_POD)
#undef SWM_IPC_CHECK_POD
}

TEST(WirePayload, MessageHeaderIsTriviallyCopyable) {
    EXPECT_TRUE(is_wire_payload_v<MessageHeader>);
}

// --- Envelope + make_message ---------------------------------------------

TEST(Envelope, MakeMessagePopulatesHeaderWithMagicVersionTypeSize) {
    Hello hello{ BackendPeerRole::WmController, 0 };
    auto  env = make_message(hello);

    EXPECT_EQ(env.header.magic, kBackendProtocolMagic);
    EXPECT_EQ(env.header.version, kBackendProtocolVersion);
    EXPECT_EQ(env.header.type, BackendMessageType::Hello);
    EXPECT_EQ(env.header.size, sizeof(Hello));
    EXPECT_EQ(env.payload.peer_role, BackendPeerRole::WmController);
}

TEST(Envelope, MakeMessagePreservesPayloadBytes) {
    OutputAdded out{};
    out.id     = 7;
    out.x      = -10;
    out.y      = 20;
    out.width  = 1920;
    out.height = 1080;
    out.name.assign("HDMI-A-1");

    auto env = make_message(out);

    EXPECT_EQ(env.payload.id, 7u);
    EXPECT_EQ(env.payload.x, -10);
    EXPECT_EQ(env.payload.width, 1920);
    EXPECT_STREQ(env.payload.name.bytes, "HDMI-A-1");
}

// --- wire_size -------------------------------------------------------------

TEST(WireSize, EqualsHeaderPlusPayload) {
    EXPECT_EQ(wire_size<Hello>(), sizeof(MessageHeader) + sizeof(Hello));
    EXPECT_EQ(wire_size<OutputAdded>(), sizeof(MessageHeader) + sizeof(OutputAdded));
    EXPECT_EQ(wire_size<ConfigureSurface>(), sizeof(MessageHeader) + sizeof(ConfigureSurface));
}

// --- Protocol constants ---------------------------------------------------

TEST(ProtocolConstants, MagicIsSipcLiteral) {
    // "SIPC" in ASCII little-endian.
    EXPECT_EQ(kBackendProtocolMagic, 0x53495043u);
}

TEST(ProtocolConstants, VersionIsOne) {
    EXPECT_EQ(kBackendProtocolVersion, 1u);
}
