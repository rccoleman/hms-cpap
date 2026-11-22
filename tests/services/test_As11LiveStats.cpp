#include <gtest/gtest.h>

#include "services/As11LiveStats.h"

#include <vector>

using namespace hms_cpap;

namespace {

as11::StreamBlock blockWith(std::initializer_list<std::pair<as11::Channel, double>> slow,
                            double pressure = -1) {
    as11::StreamBlock b;
    b.start_ms = 1'700'000'000'000;
    b.serial   = "23221234567";
    if (pressure >= 0) {
        as11::StreamChannel p;
        p.id = as11::kMaskPressure;
        p.cadence = 0;
        for (int i = 0; i < as11::kFastPerSecond; ++i) p.values.push_back(as11::quantise(as11::kMaskPressure, pressure));
        b.channels.push_back(p);
    }
    for (auto [ch, v] : slow) b.channels.push_back({static_cast<uint8_t>(ch), 1, {as11::quantise(ch, v)}});
    return b;
}

struct Sent {
    std::string topic, payload;
    bool retain;
};

}  // namespace

TEST(As11LiveStats, NothingBeforeABlock) {
    As11LiveStats st([] { return 100LL; });
    EXPECT_FALSE(st.snapshot().streaming);
    EXPECT_FALSE(st.snapshot().any());
}

TEST(As11LiveStats, ALeakInLitresPerSecondReadsInLitresPerMinute) {
    As11LiveStats st([] { return 100LL; });
    st.block(blockWith({{as11::kLeak, 0.1}}));   // 0.1 L/s
    const auto s = st.snapshot();
    ASSERT_TRUE(s.leak_lpm);
    EXPECT_NEAR(*s.leak_lpm, 6.0, 0.1);
    EXPECT_TRUE(s.streaming);
    EXPECT_EQ(s.serial, "23221234567");
}

TEST(As11LiveStats, EveryFigureIsRead) {
    As11LiveStats st([] { return 100LL; });
    st.block(blockWith({{as11::kLeak, 0.05},
                        {as11::kRespRate, 14.5},
                        {as11::kTidalVolume, 0.45},
                        {as11::kMinuteVent, 6.5},
                        {as11::kSnore, 0.2},
                        {as11::kFlowLimitation, 0.05},
                        {as11::kSpO2, 95.5},
                        {as11::kHeartRate, 62}},
                       9.0));
    const auto s = st.snapshot();
    EXPECT_NEAR(*s.pressure_cmh2o, 9.0, 0.02);
    EXPECT_NEAR(*s.resp_rate, 14.5, 0.1);
    EXPECT_NEAR(*s.tidal_ml, 450.0, 1.5);   // the machine's litres, shown as mL
    EXPECT_NEAR(*s.minute_vent_lpm, 6.5, 0.01);
    EXPECT_NEAR(*s.snore, 0.2, 0.01);
    EXPECT_NEAR(*s.flow_limitation, 0.05, 0.001);
    EXPECT_NEAR(*s.spo2, 95.5, 0.1);
    EXPECT_NEAR(*s.heart_rate, 62, 0.5);
}

TEST(As11LiveStats, ASlowFigureTheNextBlockLacksKeepsItsLastReading) {
    As11LiveStats st([] { return 100LL; });
    st.block(blockWith({{as11::kRespRate, 15.0}}, 8.0));
    st.block(blockWith({}, 8.4));   // the slow channels land every other second
    const auto s = st.snapshot();
    EXPECT_NEAR(*s.resp_rate, 15.0, 0.1);
    EXPECT_NEAR(*s.pressure_cmh2o, 8.4, 0.02);
}

TEST(As11LiveStats, ANullSampleIsNotAReading) {
    As11LiveStats st([] { return 100LL; });
    as11::StreamBlock b = blockWith({});
    b.channels.push_back({as11::kLeak, 1, {as11::kNull}});
    st.block(b);
    EXPECT_FALSE(st.snapshot().leak_lpm);
}

TEST(As11LiveStats, TherapyEndingForgetsTheNight) {
    As11LiveStats st([] { return 100LL; });
    st.block(blockWith({{as11::kLeak, 0.1}}));
    st.therapy(false);
    const auto s = st.snapshot();
    EXPECT_FALSE(s.streaming);
    EXPECT_FALSE(s.any());
}

TEST(As11LiveStats, TherapyStartingForgetsLastNightsFigures) {
    As11LiveStats st([] { return 100LL; });
    st.block(blockWith({{as11::kLeak, 0.1}}));
    st.therapy(true);   // a new night: nothing from the old one is live
    EXPECT_FALSE(st.snapshot().any());
    EXPECT_FALSE(st.snapshot().streaming);   // no block yet
}

// ── the publisher ──────────────────────────────────────────────────────────

class As11LiveStatsPublisherTest : public ::testing::Test {
protected:
    std::vector<Sent> out;
    LiveState::Publish pub = [this](const std::string& t, const std::string& p, int, bool r) {
        out.push_back({t, p, r});
        return true;
    };
    As11LiveStats::Snapshot snap(double leak, double pressure) {
        As11LiveStats::Snapshot s;
        s.streaming = true;
        s.updated_at = 1;
        s.leak_lpm = leak;
        s.pressure_cmh2o = pressure;
        return s;
    }
    bool sent(const std::string& leaf, const std::string& payload) {
        for (auto& m : out)
            if (m.topic == "cpap/dev/realtime/as11/" + leaf && m.payload == payload) return true;
        return false;
    }
};

TEST_F(As11LiveStatsPublisherTest, DiscoveryNamesEachSensorOnTheDevice) {
    As11LiveStatsPublisher p(pub, "dev", "CPAP", 10);
    ASSERT_TRUE(p.announce());
    ASSERT_EQ(out.size(), 9u);
    bool leak = false;
    for (auto& m : out) {
        EXPECT_TRUE(m.retain);
        if (m.topic == "homeassistant/sensor/dev_as11_live_leak/config") {
            leak = true;
            EXPECT_NE(m.payload.find("\"unit_of_measurement\":\"L/min\""), std::string::npos);
            EXPECT_NE(m.payload.find("cpap/dev/realtime/as11/leak"), std::string::npos);
            EXPECT_NE(m.payload.find("cpap/dev/realtime/as11/available"), std::string::npos);
        }
    }
    EXPECT_TRUE(leak);
}

TEST_F(As11LiveStatsPublisherTest, FiguresGoOutRoundedAndTheSensorsComeOnline) {
    As11LiveStatsPublisher p(pub, "dev", "CPAP", 10);
    EXPECT_EQ(p.offer(snap(3.04, 9.06), 1000), 3);   // online + leak + pressure
    EXPECT_TRUE(sent("available", "online"));
    EXPECT_TRUE(sent("leak", "3.0"));
    EXPECT_TRUE(sent("pressure", "9.1"));
}

TEST_F(As11LiveStatsPublisherTest, NotMoreOftenThanTheInterval) {
    As11LiveStatsPublisher p(pub, "dev", "CPAP", 10);
    p.offer(snap(3.0, 9.0), 1000);
    out.clear();
    EXPECT_EQ(p.offer(snap(4.0, 9.0), 1005), 0);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(p.offer(snap(4.0, 9.0), 1010), 1);   // only the leak moved
    EXPECT_TRUE(sent("leak", "4.0"));
}

TEST_F(As11LiveStatsPublisherTest, AFigureThatDidNotMoveIsNotSentAgain) {
    As11LiveStatsPublisher p(pub, "dev", "CPAP", 10);
    p.offer(snap(3.0, 9.0), 1000);
    out.clear();
    EXPECT_EQ(p.offer(snap(3.0, 9.0), 1020), 0);
}

TEST_F(As11LiveStatsPublisherTest, NothingIsSentWhenNotStreamingOrEmpty) {
    As11LiveStatsPublisher p(pub, "dev", "CPAP", 10);
    As11LiveStats::Snapshot s;
    EXPECT_EQ(p.offer(s, 1000), 0);
    s.streaming = true;   // streaming, but no figure yet
    EXPECT_EQ(p.offer(s, 1000), 0);
    EXPECT_TRUE(out.empty());
}

TEST_F(As11LiveStatsPublisherTest, EndingMakesTheSensorsUnavailableAndTheNextNightSendsAgain) {
    As11LiveStatsPublisher p(pub, "dev", "CPAP", 10);
    p.offer(snap(3.0, 9.0), 1000);
    out.clear();
    p.ended();
    EXPECT_TRUE(sent("available", "offline"));
    out.clear();
    EXPECT_EQ(p.offer(snap(3.0, 9.0), 2000), 3);   // a new night says it all again
    EXPECT_TRUE(sent("available", "online"));
}

TEST_F(As11LiveStatsPublisherTest, WithoutAPublishFunctionNothingHappens) {
    As11LiveStatsPublisher p(nullptr, "dev", "CPAP", 10);
    EXPECT_FALSE(p.announce());
    EXPECT_EQ(p.offer(snap(3.0, 9.0), 1000), 0);
    p.ended();
}
