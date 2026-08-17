#pragma once

#include <3ds.h>

#include <string>
#include <vector>

#include "dashboard.hpp"

struct ObdConnectionConfig {
    std::string host = "172.20.10.2";
    u16 port = 35000;
    int timeout_ms = 800;
};

class ObdClient {
public:
    explicit ObdClient(const ObdConnectionConfig& config);
    ~ObdClient();

    bool connectToAdapter();
    void disconnect();
    bool isConnected() const;
    const std::string& lastError() const;
    bool poll(std::vector<GaugeSample>& samples);
    void setError(const std::string& message);

private:
    bool sendCommand(const char* command, std::string& response);
    bool queryPid(const char* pid, std::string& response);
    bool parseByte(const std::string& response, const char* pid, int offset, int& value) const;
    bool parseWord(const std::string& response, const char* pid, int offset, int& value) const;

    ObdConnectionConfig config_;
    int socket_ = -1;
    std::string error_;
};
