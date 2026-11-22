#pragma once
//
// The AirSense 11's live figures while it streams therapy: the latest leak,
// mask pressure, respiratory rate, tidal volume, minute ventilation, snore
// and flow limitation (and SpO2 and heart rate when the machine's oximeter
// sends them), kept from the one-second blocks the stream already produces.
// Nothing here reads the machine or the broker: a block goes in, a snapshot
// comes out, so the unit suite can feed it blocks and read what the UI and
// MQTT would show. There is no timer; it moves when a block lands.
//
// It serves two readers: GET /api/as11/live for the Dashboard card,
// and the realtime/* MQTT sensors (As11LiveStatsPublisher, below).
//
#include "clients/As11Stream.h"
#include "mqtt/LiveState.h"

#include <json/json.h>

#include <ctime>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace hms_cpap {

class As11LiveStats {
public:
    using Now = std::function<long long()>;   // wall seconds; the unit suite sets it

    explicit As11LiveStats(Now now = [] { return static_cast<long long>(std::time(nullptr)); });

    struct Snapshot {
        bool        streaming = false;      // therapy is on and a block has landed
        std::string serial;
        long long   updated_at = 0;         // wall seconds of the last block; 0: none yet
        std::optional<double> leak_lpm;            // L/min
        std::optional<double> pressure_cmh2o;      // mask pressure
        std::optional<double> expiratory_pressure_cmh2o;   // the pressure it drops to as you breathe out (EPR)
        std::optional<double> therapy_pressure_cmh2o;   // the pressure the machine delivers (its 2 s inspiratory pressure)
        std::optional<double> resp_rate;           // breaths/min
        std::optional<double> tidal_ml;
        std::optional<double> minute_vent_lpm;
        std::optional<double> snore;               // 0..5
        std::optional<double> flow_limitation;     // 0..1, the machine's own signal
        std::optional<double> spo2;                // %
        std::optional<double> heart_rate;          // bpm
        bool any() const;
    };

    /// Therapy started or ended. Either way what was shown is forgotten: a
    /// figure from the last night must not read as live.
    void therapy(bool on);
    /// A block from the stream. A value the block does not carry (the slow
    /// channels land every other second, an oximeter may be absent) keeps its
    /// last reading until therapy ends.
    void block(const as11::StreamBlock& b);

    Snapshot snapshot() const;

    /// What GET /api/as11/live answers. `age_s` says how long ago the last
    /// block landed, so the page can grey out a stream that has stalled.
    Json::Value toJson(bool enabled) const;
    static Json::Value toJson(const Snapshot& s, bool enabled, long long now_s);

private:
    Now                 now_;
    mutable std::mutex  mu_;
    bool                therapy_ = false;
    Snapshot            s_;
};

/// The same figures as Home Assistant sensors of the device every hms-cpap
/// sensor belongs to, on cpap/<device_id>/realtime/as11/<name>. A block lands
/// every second; a message goes out at most every `every_s` seconds (10), and
/// only for a figure that moved, so a broker is not fed 1 Hz of nothing.
class As11LiveStatsPublisher {
public:
    As11LiveStatsPublisher(LiveState::Publish publish, std::string device_id, std::string device_name,
                           int every_s = 10);

    /// The discovery configs, retained. Said again after a reconnect.
    bool announce();
    /// Offers a snapshot; sends what is due. Returns how many messages went out.
    int offer(const As11LiveStats::Snapshot& s, long long now_s);
    /// The option was switched off: the discovery configs are cleared (an
    /// empty retained payload deletes them), so the entities leave Home
    /// Assistant rather than sit unavailable for good.
    bool retract();
    /// Therapy ended: the sensors go unavailable (an availability topic, as a
    /// retained value is not a number), so Home Assistant does not show last
    /// night's leak.
    void ended();

    static std::string topic(const std::string& device_id, const std::string& name);

private:
    LiveState::Publish publish_;
    std::string        device_id_, device_name_;
    int                every_s_;
    long long          last_sent_ = 0;
    std::map<std::string, std::string> last_;
    std::mutex         mu_;
};

}  // namespace hms_cpap
