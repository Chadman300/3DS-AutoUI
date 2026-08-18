#pragma once

#include <3ds.h>

#include <string>
#include <vector>

#include "dashboard.hpp"

struct ObdConnectionConfig {
    // Default targets the ESP32 bridge AP (see esp32_bridge/). For the legacy
    // iPhone-hotspot setup use 172.20.10.2; for the iCar Pro's own AP use 192.168.0.10.
    std::string host = "192.168.4.1";
    u16 port = 35000;
    int timeout_ms = 4000;
};

class ObdClient {
public:
    explicit ObdClient(const ObdConnectionConfig& config);
    ~ObdClient();

    bool connectToAdapter();
    void disconnect();
    bool isConnected() const;
    const std::string& lastError() const;
    void setConfig(const ObdConnectionConfig& config) { config_ = config; }
    const ObdConnectionConfig& config() const { return config_; }
    bool poll(std::vector<GaugeSample>& samples);
    // Queries one prioritized PID per call, keeping fast gauges ahead of slower telemetry.
    // Sets connectionLost when the socket dies. Returns false on loss.
    bool pollStep(std::vector<GaugeSample>& samples, bool& connectionLost);
    // Reads the 17-char VIN via OBD Mode 09 PID 02 (multi-frame). Returns false if unsupported.
    bool queryVin(std::string& vin);
    void setError(const std::string& message);

private:
    bool sendCommand(const char* command, std::string& response);
    bool queryPid(const char* pid, std::string& response);
    bool parseByte(const std::string& response, const char* pid, int offset, int& value) const;
    bool parseWord(const std::string& response, const char* pid, int offset, int& value) const;

    ObdConnectionConfig config_;
    int socket_ = -1;
    std::string error_;
    size_t pollIndex_ = 0;
    size_t slowPollIndex_ = 0;
    unsigned int pollStepCount_ = 0;
    int consecutiveFail_ = 0;
    bool commLost_ = false;
    float baroKpa_ = 101.3f; // boost zero reference; updated live from PID 0x33 when supported
};
