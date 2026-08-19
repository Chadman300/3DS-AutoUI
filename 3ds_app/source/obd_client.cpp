#include "obd_client.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

const char* findPidPayload(const std::string& response, const char* pid) {
    std::string marker = "41";
    marker += pid;
    size_t position = response.find(marker);
    if (position == std::string::npos) {
        return nullptr;
    }
    return response.c_str() + position + marker.size();
}

bool isHex(char value) {
    return std::isxdigit(static_cast<unsigned char>(value)) != 0;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return value - 'a' + 10;
}

bool readHex(const char* payload, int digits, int& value) {
    if (payload == nullptr) return false;
    for (int i = 0; i < digits; ++i) {
        if (!isHex(payload[i])) return false;
    }

    value = 0;
    for (int i = 0; i < digits; ++i) {
        value = (value << 4) | hexValue(payload[i]);
    }
    return true;
}

void assignSample(std::vector<GaugeSample>& samples, const char* id, float value) {
    for (auto& sample : samples) {
        if (sample.id == id) {
            sample.value = value;
            sample.valid = true;
            return;
        }
    }
    samples.push_back({id, value, true});
}

void markInvalid(std::vector<GaugeSample>& samples, const char* id) {
    for (auto& sample : samples) {
        if (sample.id == id) {
            sample.valid = false;
            return;
        }
    }
}

// One PID per round-robin step. scale/offset convert the raw byte/word to display units.
struct PidSpec {
    const char* pid;
    const char* id;
    bool word;
    float scale;
    float offset;
};

const PidSpec kFastPollPids[] = {
    {"0C", "rpm", true, 0.25f, 0.0f},
    {"0D", "speed", false, 1.0f, 0.0f},
    {"0B", "boost", false, 1.0f / 6.89476f, 0.0f},
    {"11", "throttle_position", false, 100.0f / 255.0f, 0.0f},
};

const PidSpec kSlowPollPids[] = {
    {"05", "coolant_temp", false, 1.0f, -40.0f},
    {"0F", "intake_temp", false, 1.0f, -40.0f},
    {"33", "__baro", false, 1.0f, 0.0f},
    {"04", "engine_load", false, 100.0f / 255.0f, 0.0f},
    {"2F", "fuel_level", false, 100.0f / 255.0f, 0.0f},
    {"42", "battery_voltage", true, 0.001f, 0.0f},
    {"5C", "oil_temp", false, 1.0f, -40.0f},
};

// Indices into kPollPids. Fast-changing gauges (rpm/speed/boost/throttle) are polled
// twice as often as slow-changing ones (temps/fuel/voltage) so the responsive gauges
// don't wait behind a full round-robin of everything else.
const size_t kHighPriorityIdx[] = {0, 1, 4, 5};   // rpm, speed, boost, throttle_position
const size_t kLowPriorityIdx[] = {2, 3, 6, 7, 8, 9};  // coolant, intake, load, fuel, voltage, oil_temp
const size_t kHighPriorityCount = sizeof(kHighPriorityIdx) / sizeof(kHighPriorityIdx[0]);
const size_t kLowPriorityCount = sizeof(kLowPriorityIdx) / sizeof(kLowPriorityIdx[0]);

std::string formatSocketError(const char* prefix, int err) {
    char text[160];
    const char* reason = strerror(err);
    if (reason != nullptr && reason[0] != '\0') {
        snprintf(text, sizeof(text), "%s: %s (errno %d)", prefix, reason, err);
    } else {
        snprintf(text, sizeof(text), "%s (errno %d)", prefix, err);
    }
    return text;
}

} // namespace

ObdClient::ObdClient(const ObdConnectionConfig& config) : config_(config) {}

ObdClient::~ObdClient() {
    disconnect();
}

void ObdClient::setError(const std::string& message) {
    error_ = message;
}

const std::string& ObdClient::lastError() const {
    return error_;
}

bool ObdClient::isConnected() const {
    return socket_ >= 0;
}

void ObdClient::disconnect() {
    if (socket_ >= 0) {
        close(socket_);
        socket_ = -1;
    }
}

bool ObdClient::connectToAdapter() {
    disconnect();
    error_.clear();
    highIndex_ = 0;
    lowIndex_ = 0;
    stepCounter_ = 0;
    consecutiveFail_ = 0;
    commLost_ = false;

    // 3DS's SOC service does not auto-substitute a default protocol for SOCK_STREAM
    // like desktop kernels do; IPPROTO_IP (0) gets rejected, so pass TCP explicitly.
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ < 0) {
        char detail[192];
        const char* reason = strerror(errno);
        if (reason != nullptr && reason[0] != '\0') {
            snprintf(detail, sizeof(detail), "socket failed: %s (fd %d, errno %d)", reason, socket_, errno);
        } else {
            snprintf(detail, sizeof(detail), "socket failed (fd %d, errno %d)", socket_, errno);
        }
        setError(detail);
        return false;
    }

    // Disable Nagle's algorithm: this protocol is a tiny request/reply ping-pong
    // (5-byte commands), and Nagle + delayed ACK on either end can add tens of ms
    // of invisible latency per query, which compounds badly across a polling cycle.
    int nodelay = 1;
    setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    address.sin_addr.s_addr = inet_addr(config_.host.c_str());
    if (address.sin_addr.s_addr == INADDR_NONE) {
        setError("invalid adapter IP");
        disconnect();
        return false;
    }

    // Blocking connect: the non-blocking + select() completion path is unreliable on the
    // 3DS SOC service and returned spurious in-progress errno values (-26). The adapter is
    // on the same subnet, so a blocking connect completes quickly on success.
    int connectResult = connect(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (connectResult < 0) {
        setError(formatSocketError("connect failed", errno));
        disconnect();
        return false;
    }

    const char* setup[] = {"ATZ", "ATE0", "ATL0", "ATS0", "ATH0"};
    for (const char* command : setup) {
        std::string response;
        if (!sendCommand(command, response)) {
            disconnect();
            return false;
        }
    }

    return true;
}

bool ObdClient::sendCommand(const char* command, std::string& response) {
    response.clear();
    if (!isConnected()) {
        setError("adapter is not connected");
        return false;
    }

    std::string request(command);
    request.push_back('\r');
    ssize_t sent = send(socket_, request.c_str(), request.size(), 0);
    if (sent != static_cast<ssize_t>(request.size())) {
        setError(formatSocketError("send failed", errno));
        commLost_ = true;
        return false;
    }

    char buffer[256];
    for (;;) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket_, &readSet);
        timeval timeout{};
        timeout.tv_sec = config_.timeout_ms / 1000;
        timeout.tv_usec = (config_.timeout_ms % 1000) * 1000;
        int ready = select(socket_ + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready == 0) {
            setError("adapter response timeout");
            return false;
        }
        if (ready < 0) {
            setError(formatSocketError("select failed", errno));
            commLost_ = true;
            return false;
        }

        ssize_t received = recv(socket_, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            if (received == 0) setError("adapter closed connection");
            else setError(formatSocketError("receive failed", errno));
            commLost_ = true;
            return false;
        }

        response.append(buffer, static_cast<size_t>(received));
        if (response.find('>') != std::string::npos) break;
        if (response.size() > 4096) {
            setError("adapter response too long");
            return false;
        }
    }

    response.erase(std::remove(response.begin(), response.end(), '\r'), response.end());
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    response.erase(std::remove(response.begin(), response.end(), ' '), response.end());
    response.erase(std::remove(response.begin(), response.end(), '>'), response.end());
    return true;
}

bool ObdClient::queryPid(const char* pid, std::string& response) {
    char command[8];
    snprintf(command, sizeof(command), "01%s", pid);
    if (!sendCommand(command, response)) return false;
    if (response.find("NODATA") != std::string::npos || response.find("ERROR") != std::string::npos) {
        return false;
    }
    return true;
}

bool ObdClient::parseByte(const std::string& response, const char* pid, int offset, int& value) const {
    const char* payload = findPidPayload(response, pid);
    return payload != nullptr && readHex(payload + offset * 2, 2, value);
}

bool ObdClient::parseWord(const std::string& response, const char* pid, int offset, int& value) const {
    const char* payload = findPidPayload(response, pid);
    return payload != nullptr && readHex(payload + offset * 2, 4, value);
}

bool ObdClient::poll(std::vector<GaugeSample>& samples) {
    if (!isConnected()) return false;

    bool anyValue = false;
    std::string response;
    int value = 0;

    if (queryPid("0C", response) && parseWord(response, "0C", 0, value)) {
        assignSample(samples, "rpm", value / 4.0f);
        anyValue = true;
    }
    if (queryPid("0D", response) && parseByte(response, "0D", 0, value)) {
        assignSample(samples, "speed", static_cast<float>(value));
        anyValue = true;
    }
    if (queryPid("05", response) && parseByte(response, "05", 0, value)) {
        assignSample(samples, "coolant_temp", static_cast<float>(value - 40));
        anyValue = true;
    }
    if (queryPid("0F", response) && parseByte(response, "0F", 0, value)) {
        assignSample(samples, "intake_temp", static_cast<float>(value - 40));
        anyValue = true;
    }

    int mapKpa = 0;
    if (queryPid("0B", response) && parseByte(response, "0B", 0, mapKpa)) {
        // Gauge (relative) boost in psi: below 0 = vacuum, above 0 = boost. No clamp, so the
        // gauge visibly tracks vacuum at idle and positive boost under load.
        assignSample(samples, "boost", (mapKpa - 101.3f) / 6.89476f);
        anyValue = true;
    }
    if (queryPid("11", response) && parseByte(response, "11", 0, value)) {
        assignSample(samples, "throttle_position", value * 100.0f / 255.0f);
        anyValue = true;
    }
    if (queryPid("04", response) && parseByte(response, "04", 0, value)) {
        assignSample(samples, "engine_load", value * 100.0f / 255.0f);
        anyValue = true;
    }
    if (queryPid("2F", response) && parseByte(response, "2F", 0, value)) {
        assignSample(samples, "fuel_level", value * 100.0f / 255.0f);
        anyValue = true;
    }
    if (queryPid("42", response) && parseWord(response, "42", 0, value)) {
        assignSample(samples, "battery_voltage", value / 1000.0f);
        anyValue = true;
    }
    // Engine oil temperature (SAE standard PID 0x5C): A - 40 degrees C.
    if (queryPid("5C", response) && parseByte(response, "5C", 0, value)) {
        assignSample(samples, "oil_temp", static_cast<float>(value - 40));
        anyValue = true;
    }
    // Note: oil PRESSURE has no SAE-standard OBD2 PID, so it cannot be read from a stock
    // ELM327. The oil_pressure gauge stays at its placeholder value unless a
    // manufacturer-specific PID is added later.

    if (!anyValue && error_.empty()) setError("no supported OBD values");
    return anyValue;
}

bool ObdClient::pollStep(std::vector<GaugeSample>& samples, bool& connectionLost) {
    connectionLost = false;
    if (!isConnected()) {
        connectionLost = true;
        return false;
    }

    const size_t pidCount = sizeof(kPollPids) / sizeof(kPollPids[0]);

    // Every 3rd step polls the next low-priority PID; the other 2 out of 3 poll the
    // next high-priority PID. That gives high-priority gauges ~2x the refresh rate
    // without adding extra queries per second overall. Disabled/unsupported PIDs are
    // skipped without sending anything, freeing that slot for the next enabled one.
    size_t pidIndex = 0;
    bool found = false;
    if ((stepCounter_ % 3) == 0) {
        for (size_t attempt = 0; attempt < kLowPriorityCount; ++attempt) {
            size_t candidate = kLowPriorityIdx[lowIndex_];
            lowIndex_ = (lowIndex_ + 1) % kLowPriorityCount;
            if (pidEnabled_[candidate]) { pidIndex = candidate; found = true; break; }
        }
    } else {
        for (size_t attempt = 0; attempt < kHighPriorityCount; ++attempt) {
            size_t candidate = kHighPriorityIdx[highIndex_];
            highIndex_ = (highIndex_ + 1) % kHighPriorityCount;
            if (pidEnabled_[candidate]) { pidIndex = candidate; found = true; break; }
        }
    }
    ++stepCounter_;
    if (!found) {
        // Every PID in this priority tier is disabled/unsupported right now; nothing to do.
        return true;
    }
    const PidSpec& spec = kPollPids[pidIndex];

    commLost_ = false;
    std::string response;
    bool gotValue = false;
    if (queryPid(spec.pid, response)) {
        int raw = 0;
        const bool parsed = spec.word ? parseWord(response, spec.pid, 0, raw)
                                      : parseByte(response, spec.pid, 0, raw);
        if (parsed) {
            if (strcmp(spec.id, "__baro") == 0) {
                // Barometric pressure: the live zero reference for boost, not a gauge itself.
                baroKpa_ = static_cast<float>(raw);
            } else if (strcmp(spec.id, "boost") == 0) {
                // Gauge boost = (manifold absolute - barometric) in psi.
                assignSample(samples, "boost", (static_cast<float>(raw) - baroKpa_) / 6.89476f);
            } else {
                assignSample(samples, spec.id, static_cast<float>(raw) * spec.scale + spec.offset);
            }
            gotValue = true;
        }
    }

    if (commLost_) {
        connectionLost = true;
        return false;
    }
    if (gotValue) {
        consecutiveFail_ = 0;
        pidNoDataStreak_[pidIndex] = 0;
    } else {
        // Unsupported PID (NODATA) or parse miss: hide the gauge rather than show a stale value.
        markInvalid(samples, spec.id);
        // After enough consecutive misses the vehicle almost certainly doesn't support this
        // PID at all (e.g. no boost/MAP sensor) -- stop wasting round trips asking for it.
        static constexpr int kAutoDisableThreshold = 8;
        if (++pidNoDataStreak_[pidIndex] >= kAutoDisableThreshold) {
            pidEnabled_[pidIndex] = false;
        }
        if (++consecutiveFail_ >= static_cast<int>(pidCount)) {
            // A full cycle with nothing readable means the adapter/link is effectively gone.
            connectionLost = true;
            consecutiveFail_ = 0;
            return false;
        }
    }
    return true;
}

void ObdClient::setPidEnabled(const char* gaugeId, bool enabled) {
    const size_t pidCount = sizeof(kPollPids) / sizeof(kPollPids[0]);
    for (size_t i = 0; i < pidCount; ++i) {
        if (strcmp(kPollPids[i].id, gaugeId) == 0) {
            pidEnabled_[i] = enabled;
            if (enabled) pidNoDataStreak_[i] = 0;  // give a re-enabled gauge a fresh chance
            return;
        }
    }
}
