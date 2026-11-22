#include "services/As11LiveStats.h"

#include <cmath>
#include <cstdio>

namespace hms_cpap {

namespace {

/// The last real sample of a channel in a block, in the machine's units;
/// nothing when the block does not carry it or it is null.
std::optional<double> lastOf(const as11::StreamBlock& b, as11::Channel ch) {
    const auto* c = b.channel(ch);
    if (!c) return std::nullopt;
    for (auto it = c->values.rbegin(); it != c->values.rend(); ++it)
        if (*it != as11::kNull) return as11::physical(ch, *it);
    return std::nullopt;
}

/// The mean of a fast channel's samples in the block (mask pressure moves
/// 25 times a second; the mean is steadier to read than the last sample).
std::optional<double> meanOf(const as11::StreamBlock& b, as11::Channel ch) {
    const auto* c = b.channel(ch);
    if (!c) return std::nullopt;
    double sum = 0;
    int n = 0;
    for (auto v : c->values)
        if (v != as11::kNull) { sum += as11::physical(ch, v); ++n; }
    if (!n) return std::nullopt;
    return sum / n;
}

void keep(std::optional<double>& into, const std::optional<double>& v) {
    if (v) into = v;
}

std::string fixed(double v, int places) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", places, v);
    return buf;
}

}  // namespace

As11LiveStats::As11LiveStats(Now now) : now_(std::move(now)) {}

bool As11LiveStats::Snapshot::any() const {
    return leak_lpm || pressure_cmh2o || therapy_pressure_cmh2o || expiratory_pressure_cmh2o || resp_rate || tidal_ml || minute_vent_lpm || snore ||
           flow_limitation || spo2 || heart_rate;
}

void As11LiveStats::therapy(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    therapy_ = on;
    s_ = Snapshot{};
}

void As11LiveStats::block(const as11::StreamBlock& b) {
    std::lock_guard<std::mutex> lk(mu_);
    // A block with therapy not announced (the stream began before the first
    // state frame) still counts: the stream is the proof the machine is on.
    therapy_ = true;
    if (!b.serial.empty()) s_.serial = b.serial;
    s_.updated_at = now_();
    s_.streaming  = true;
    // Leak is the machine's L/s; the UI and Home Assistant read L/min.
    if (auto v = lastOf(b, as11::kLeak)) s_.leak_lpm = *v * 60.0;
    keep(s_.pressure_cmh2o, meanOf(b, as11::kMaskPressure));
    if (!b.channel(as11::kMaskPressure)) keep(s_.pressure_cmh2o, lastOf(b, as11::kMaskPressure2s));
    keep(s_.therapy_pressure_cmh2o, lastOf(b, as11::kInspPressure2s));
    keep(s_.expiratory_pressure_cmh2o, lastOf(b, as11::kExpPressure2s));
    keep(s_.resp_rate, lastOf(b, as11::kRespRate));
    // Tidal volume is the machine's litres; shown as mL.
    if (auto v = lastOf(b, as11::kTidalVolume)) s_.tidal_ml = *v * 1000.0;
    keep(s_.minute_vent_lpm, lastOf(b, as11::kMinuteVent));
    keep(s_.snore, lastOf(b, as11::kSnore));
    keep(s_.flow_limitation, lastOf(b, as11::kFlowLimitation));
    keep(s_.spo2, lastOf(b, as11::kSpO2));
    keep(s_.heart_rate, lastOf(b, as11::kHeartRate));
}

As11LiveStats::Snapshot As11LiveStats::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    Snapshot s = s_;
    s.streaming = therapy_ && s_.updated_at != 0;
    return s;
}

Json::Value As11LiveStats::toJson(const Snapshot& s, bool enabled, long long now_s) {
    Json::Value j(Json::objectValue);
    j["enabled"]   = enabled;
    j["streaming"] = enabled && s.streaming;
    if (!enabled || !s.streaming) return j;
    j["serial"]     = s.serial;
    j["updated_at"] = static_cast<Json::Int64>(s.updated_at);
    j["age_s"]      = static_cast<Json::Int64>(std::max<long long>(0, now_s - s.updated_at));
    auto put = [&j](const char* k, const std::optional<double>& v) {
        j[k] = v ? Json::Value(*v) : Json::Value(Json::nullValue);
    };
    put("leak_lpm", s.leak_lpm);
    put("pressure_cmh2o", s.pressure_cmh2o);
    put("resp_rate", s.resp_rate);
    put("tidal_ml", s.tidal_ml);
    put("minute_vent_lpm", s.minute_vent_lpm);
    put("snore", s.snore);
    put("flow_limitation", s.flow_limitation);
    put("spo2", s.spo2);
    put("heart_rate", s.heart_rate);
    return j;
}

Json::Value As11LiveStats::toJson(bool enabled) const {
    return toJson(snapshot(), enabled, now_());
}

// ── the MQTT side ──────────────────────────────────────────────────────────

namespace {

struct Sensor {
    const char* name;      // the topic's leaf and the entity's unique_id suffix
    const char* label;
    const char* unit;
    const char* icon;
    int         places;
    std::optional<double> As11LiveStats::Snapshot::*field;
};

const Sensor kSensors[] = {
    {"leak",            "Live leak",             "L/min",     "mdi:water-percent", 1, &As11LiveStats::Snapshot::leak_lpm},
    {"pressure",        "Live mask pressure",    "cmH2O",     "mdi:gauge",         1, &As11LiveStats::Snapshot::pressure_cmh2o},
    {"therapy_pressure", "Live therapy pressure", "cmH2O",    "mdi:gauge-full",    1, &As11LiveStats::Snapshot::therapy_pressure_cmh2o},
    {"expiratory_pressure", "Live expiratory pressure", "cmH2O", "mdi:gauge-low",    1, &As11LiveStats::Snapshot::expiratory_pressure_cmh2o},
    {"resp_rate",       "Live respiratory rate", "breaths/min", "mdi:lungs",       1, &As11LiveStats::Snapshot::resp_rate},
    {"tidal_volume",    "Live tidal volume",     "mL",        "mdi:lungs",         0, &As11LiveStats::Snapshot::tidal_ml},
    {"minute_vent",     "Live minute ventilation", "L/min",   "mdi:lungs",         1, &As11LiveStats::Snapshot::minute_vent_lpm},
    {"snore",           "Live snore",            "",          "mdi:sleep",         2, &As11LiveStats::Snapshot::snore},
    {"flow_limitation", "Live flow limitation",  "",          "mdi:chart-bell-curve", 2, &As11LiveStats::Snapshot::flow_limitation},
    {"spo2",            "Live SpO2",             "%",         "mdi:water",         1, &As11LiveStats::Snapshot::spo2},
    {"heart_rate",      "Live heart rate",       "bpm",       "mdi:heart-pulse",   0, &As11LiveStats::Snapshot::heart_rate},
};

}  // namespace

As11LiveStatsPublisher::As11LiveStatsPublisher(LiveState::Publish publish, std::string device_id,
                                               std::string device_name, int every_s)
    : publish_(std::move(publish)), device_id_(std::move(device_id)),
      device_name_(std::move(device_name)), every_s_(every_s) {}

std::string As11LiveStatsPublisher::topic(const std::string& device_id, const std::string& name) {
    return "cpap/" + device_id + "/realtime/as11/" + name;
}

bool As11LiveStatsPublisher::announce() {
    if (!publish_) return false;
    Json::Value device(Json::objectValue);
    device["identifiers"].append(device_id_);
    device["name"] = device_name_;
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    bool ok = true;
    for (const auto& s : kSensors) {
        Json::Value c(Json::objectValue);
        c["name"]               = s.label;
        c["unique_id"]          = device_id_ + "_as11_live_" + s.name;
        c["state_topic"]        = topic(device_id_, s.name);
        c["availability_topic"] = topic(device_id_, "available");
        c["state_class"]        = "measurement";
        c["icon"]               = s.icon;
        if (*s.unit) c["unit_of_measurement"] = s.unit;
        c["device"] = device;
        ok &= publish_("homeassistant/sensor/" + device_id_ + "_as11_live_" + s.name + "/config",
                       Json::writeString(w, c), 1, true);
    }
    return ok;
}

int As11LiveStatsPublisher::offer(const As11LiveStats::Snapshot& snap, long long now_s) {
    if (!publish_ || !snap.streaming || !snap.any()) return 0;
    std::lock_guard<std::mutex> lk(mu_);
    if (last_sent_ != 0 && now_s - last_sent_ < every_s_) return 0;
    int sent = 0;
    if (last_.find("available") == last_.end() || last_["available"] != "online") {
        if (publish_(topic(device_id_, "available"), "online", 1, true)) { last_["available"] = "online"; ++sent; }
    }
    for (const auto& s : kSensors) {
        const auto& v = snap.*(s.field);
        if (!v) continue;
        const std::string text = fixed(*v, s.places);
        auto it = last_.find(s.name);
        if (it != last_.end() && it->second == text) continue;
        if (publish_(topic(device_id_, s.name), text, 0, false)) { last_[s.name] = text; ++sent; }
    }
    last_sent_ = now_s;
    return sent;
}

bool As11LiveStatsPublisher::retract() {
    if (!publish_) return false;
    std::lock_guard<std::mutex> lk(mu_);
    last_.clear();
    last_sent_ = 0;
    bool ok = true;
    for (const auto& s : kSensors)
        ok &= publish_("homeassistant/sensor/" + device_id_ + "_as11_live_" + s.name + "/config", "", 1, true);
    ok &= publish_(topic(device_id_, "available"), "", 1, true);
    return ok;
}

void As11LiveStatsPublisher::ended() {
    if (!publish_) return;
    std::lock_guard<std::mutex> lk(mu_);
    last_.clear();
    last_sent_ = 0;
    publish_(topic(device_id_, "available"), "offline", 1, true);
}

}  // namespace hms_cpap
