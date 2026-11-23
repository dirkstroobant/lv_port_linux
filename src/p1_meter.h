#ifndef P1_METER_H
#define P1_METER_H

// Reads a DSMR/P1 telegram from a HomeWizard P1 meter via
// http://<host>/api/v1/telegram and extracts the main values.

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

struct meter_data {
    long active_power_W = 0;   // 1-0:1.7.0  import
    long return_power_W = 0;   // 1-0:2.7.0  export
    long max_demand_W   = 0;   // 1-0:1.6.0
    long energy_T1      = 0;   // 1-0:1.8.1  Wh
    long energy_T2      = 0;   // 1-0:1.8.2  Wh
};

class P1Meter {
public:
    // host = IP address or name (e.g. "192.168.1.60" or "p1meter-xxxxxx.local").
    // Leave empty to look the meter up with avahi-browse on the first update().
    explicit P1Meter(const std::string& host = "")
        : _host(host), _discovered(!host.empty()) {}

    // Optional: needs "sudo apt install avahi-utils"
    bool discover() {
        FILE* p = popen("avahi-browse -rtp _hwenergy._tcp 2>/dev/null", "r");
        if (!p) return false;

        char line[512];
        std::string found;
        while (fgets(line, sizeof(line), p)) {
            // Resolved lines: =;iface;proto;name;type;domain;hostname;address;port;txt
            if (line[0] != '=') continue;
            std::string s(line), f[10];
            size_t pos = 0;
            int n = 0;
            while (n < 9) {
                size_t e = s.find(';', pos);
                if (e == std::string::npos) break;
                f[n++] = s.substr(pos, e - pos);
                pos = e + 1;
            }
            if (n < 9) continue;
            if (f[6].rfind("p1meter", 0) != 0) continue;
            found = f[7];
            if (f[2] == "IPv4") break;   // prefer IPv4
        }
        pclose(p);

        if (found.empty()) return false;
        _host = found;
        _discovered = true;
        return true;
    }

    // Fetches and parses one telegram. Blocks up to ~3 s per step if unreachable.
    bool update() {
        if (!_discovered && !discover()) {
            recordResult(false);
            return false;
        }

        std::string body;
        if (!httpGet("/api/v1/telegram", body) || body.empty()) {
            recordResult(false);
            return false;
        }

        meter_data d;
        d.active_power_W = extractInt(body.c_str(), "1-0:1.7.0", "*kW");
        d.return_power_W = extractInt(body.c_str(), "1-0:2.7.0", "*kW");
        d.max_demand_W   = extractInt(body.c_str(), "1-0:1.6.0", "*kW");
        d.energy_T1      = extractInt(body.c_str(), "1-0:1.8.1", "*kWh");
        d.energy_T2      = extractInt(body.c_str(), "1-0:1.8.2", "*kWh");

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _data = d;
        }
        recordResult(true);
        return true;
    }

    meter_data getData() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _data;
    }
    bool lastReadOk() const { return _last_ok; }
    std::string getHost() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _host;
    }

    // Percentage of failed reads over the last WINDOW_SIZE updates (0..100)
    float getFailureRate() const {
        std::lock_guard<std::mutex> lock(_mutex);
        int count = _filled ? WINDOW_SIZE : _idx;
        if (count == 0) return 0.0f;
        return (float)_sum / count * 100.0f;
    }

private:
    static const int WINDOW_SIZE = 200;

    bool httpGet(const char* path, std::string& body) {
        std::string host = getHost();

        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), "80", &hints, &res) != 0) return false;

        int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) { freeaddrinfo(res); return false; }

        timeval tv{3, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
            close(fd);
            freeaddrinfo(res);
            return false;
        }
        freeaddrinfo(res);

        std::string req = std::string("GET ") + path + " HTTP/1.1\r\n"
                          "Host: " + host + "\r\n"
                          "Accept: text/plain\r\n"
                          "Connection: close\r\n\r\n";
        if (send(fd, req.c_str(), req.size(), 0) < 0) { close(fd); return false; }

        std::string response;
        char buf[512];
        ssize_t n;
        while ((n = recv(fd, buf, sizeof(buf), 0)) > 0)
            response.append(buf, n);
        close(fd);

        size_t pos = response.find("\r\n\r\n");
        if (pos == std::string::npos) return false;
        body = response.substr(pos + 4);
        return true;
    }

    // Same logic as the original: find the header, then the unit after it,
    // walk back to '(' and collect the digits (decimal point ignored, so
    // "00.335*kW" gives 335, i.e. watts).
    static long extractInt(const char* data, const char* header, const char* unit) {
        const char* headerPtr = strstr(data, header);
        if (!headerPtr) return 0;
        const char* unitPtr = strstr(headerPtr, unit);
        if (!unitPtr) return 0;

        const char* p = unitPtr;
        while (p > headerPtr && *p != '(') p--;
        if (*p != '(') return 0;
        p++;

        long result = 0;
        while (p < unitPtr) {
            if (*p >= '0' && *p <= '9') result = result * 10 + (*p - '0');
            p++;
        }
        return result;
    }

    void recordResult(bool ok) {
        _last_ok = ok;
        std::lock_guard<std::mutex> lock(_mutex);
        int v = ok ? 0 : 1;               // 1 = failed
        _sum -= _meas[_idx];
        _meas[_idx] = v;
        _sum += v;
        if (++_idx >= WINDOW_SIZE) { _idx = 0; _filled = true; }
    }

    mutable std::mutex _mutex;
    std::string _host;
    bool _discovered;
    meter_data _data;
    std::atomic<bool> _last_ok{false};

    int _meas[WINDOW_SIZE] = {0};
    int _sum = 0;
    int _idx = 0;
    bool _filled = false;
};

#endif  // P1_METER_H