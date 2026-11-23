#ifndef SOLAR_INVERTER_H
#define SOLAR_INVERTER_H

// Fetches http://<host>/status/status.php from a solar inverter, which
// returns one line of comma-separated values, and extracts the AC grid
// power reading from it. Uses HTTP Basic Authentication.

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include <atomic>

// Minimal base64 encoder
inline std::string base64_encode(const std::string& input) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

class SolarInverter {
public:
    explicit SolarInverter(const std::string& host, int port = 80)
        : _host(host), _port(port), _grid_power_w(0), _last_read_ok(false) {}

    void setCredentials(const std::string& username, const std::string& password) {
        _auth_header_value = "Basic " + base64_encode(username + ":" + password);
    }

    // Fetches and parses the latest reading. Returns true on success.
    // Blocks for up to ~3 s per step if the inverter is unreachable.
    bool update() {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(_host.c_str(), std::to_string(_port).c_str(), &hints, &res) != 0) {
            _last_read_ok = false;
            return false;
        }

        int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) {
            freeaddrinfo(res);
            _last_read_ok = false;
            return false;
        }

        timeval tv{3, 0};  // 3 s timeout for send and receive
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
            close(fd);
            freeaddrinfo(res);
            _last_read_ok = false;
            return false;
        }
        freeaddrinfo(res);

        std::string request =
            "GET /status/status.php HTTP/1.1\r\n"
            "Host: " + _host + "\r\n"
            "Accept: text/plain\r\n";
        if (!_auth_header_value.empty())
            request += "Authorization: " + _auth_header_value + "\r\n";
        request += "Connection: close\r\n\r\n";

        if (send(fd, request.c_str(), request.size(), 0) < 0) {
            close(fd);
            _last_read_ok = false;
            return false;
        }

        // "Connection: close" means the server ends the stream when done.
        std::string response;
        char buf[512];
        ssize_t n;
        while ((n = recv(fd, buf, sizeof(buf), 0)) > 0)
            response.append(buf, n);
        close(fd);

        size_t pos = response.find("\r\n\r\n");
        if (pos == std::string::npos) {
            _last_read_ok = false;
            return false;
        }
        return parse(response.substr(pos + 4));
    }

    int getOutputPower() const { return _grid_power_w; }
    int getTodayEnergy() const { return _today_energy; }
    bool lastReadOk() const { return _last_read_ok; }

private:
   bool parse(const std::string& csv_line) {
    std::vector<std::string> fields;
    std::stringstream ss(csv_line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        fields.push_back(item);
    }

    // 1-based indexes of the values we need
    const size_t GRID_POWER_IDX   = 24;
    const size_t TODAY_ENERGY_IDX = 4;

    if (fields.size() < GRID_POWER_IDX) {
        _last_read_ok = false;
        return false;
    }

    try {
        _grid_power_w = std::stoi(fields[GRID_POWER_IDX - 1]);
    } catch (...) {
        _last_read_ok = false;
        return false;
    }

    // Today's energy is optional: if it can't be parsed, keep the old value
    // instead of marking the whole read as failed.
    try {
        _today_energy = std::stoi(fields[TODAY_ENERGY_IDX - 1]);
    } catch (...) {
    }

    _last_read_ok = true;
    return true;
} 

    std::string _host;
    int _port;
    
    std::atomic<int>  _grid_power_w;
    std::atomic<int> _today_energy{0};
    std::atomic<bool> _last_read_ok;
    
    std::string _auth_header_value;  // empty = no auth sent
};

#endif  // SOLAR_INVERTER_H