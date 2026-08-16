#include "obd_client.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
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
            return;
        }
    }
    samples.push_back({id, value});
}

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

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    address.sin_addr.s_addr = inet_addr(config_.host.c_str());
    if (address.sin_addr.s_addr == INADDR_NONE) {
        setError("invalid adapter IP");
        disconnect();
        return false;
    }

    int socketFlags = fcntl(socket_, F_GETFL, 0);
    if (socketFlags < 0 || fcntl(socket_, F_SETFL, socketFlags | O_NONBLOCK) < 0) {
        setError(formatSocketError("could not configure socket", errno));
        disconnect();
        return false;
    }

    int connectResult = connect(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (connectResult < 0 && errno == EINPROGRESS) {
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(socket_, &writeSet);
        timeval timeout{};
        timeout.tv_sec = config_.timeout_ms / 1000;
        timeout.tv_usec = (config_.timeout_ms % 1000) * 1000;
        connectResult = select(socket_ + 1, nullptr, &writeSet, nullptr, &timeout);
        if (connectResult > 0) {
            int socketError = 0;
            socklen_t socketErrorSize = sizeof(socketError);
            if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorSize) < 0) {
                connectResult = -1;
            } else if (socketError != 0) {
                errno = socketError;
                connectResult = -1;
            } else {
                connectResult = 0;
            }
        } else if (connectResult == 0) {
            errno = ETIMEDOUT;
            connectResult = -1;
        }
    }

    if (connectResult < 0) {
        setError(formatSocketError("connect failed", errno));
        disconnect();
        return false;
    }
    if (connectResult == 0 && fcntl(socket_, F_SETFL, socketFlags) < 0) {
        setError(formatSocketError("could not restore socket mode", errno));
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
            return false;
        }

        ssize_t received = recv(socket_, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            if (received == 0) setError("adapter closed connection");
            else setError(formatSocketError("receive failed", errno));
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
        assignSample(samples, "boost", std::max(0.0f, (mapKpa - 101.3f) / 6.89476f));
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

    if (!anyValue && error_.empty()) setError("no supported OBD values");
    return anyValue;
}
