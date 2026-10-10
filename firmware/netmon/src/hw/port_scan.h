#pragma once
// Port scan of one device on the LAN (0.15): which of a fixed list of common
// TCP ports accept a connection. Started from the Devices page, one device at
// a time, never on its own.
//
// Driven from loop() like the sweep. Each probe is a non-blocking lwIP socket:
// connect() returns at once and tick() only looks at whether it has finished,
// so a device that ignores the probes (a firewall dropping them) costs the
// board nothing but the wait. WiFiClient::connect() would block loop() for the
// whole timeout on every such port, and the pages and the Bluetooth link with
// it. Two ports are probed at a time, leaving lwIP's other sockets to the web
// server and everything else.
#include <Arduino.h>
#include <lwip/sockets.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>

struct PortEntry {
    uint16_t port;
    const char* name;
};

static const PortEntry kScanPorts[] = {
    {21, "FTP"},       {22, "SSH"},        {23, "Telnet"},   {25, "SMTP"},
    {53, "DNS"},       {80, "HTTP"},       {110, "POP3"},    {443, "HTTPS"},
    {445, "SMB"},      {554, "RTSP"},      {1883, "MQTT"},   {3389, "RDP"},
    {8080, "HTTP-alt"}, {8443, "HTTPS-alt"}, {8883, "MQTT-TLS"}, {9100, "Printer"},
};
static const size_t kScanPortCount = sizeof(kScanPorts) / sizeof(kScanPorts[0]);

static const uint32_t kPortTimeoutMs = 1500;   // no answer by then: not open
static const size_t kPortParallel = 2;

enum class ScanState : uint8_t { Idle, Scanning, Done };

struct PortResult {
    uint16_t port;
    const char* name;
    bool open;
    bool done;   // probed already
};

class PortScanner {
public:
    // Starts a scan of `ip` (host order), dropping any scan still running.
    void begin(uint32_t ip) {
        stop_all();
        _ip = ip;
        _next = 0;
        _finished = 0;
        _open_count = 0;
        _started_ms = millis();
        _took_ms = 0;
        for (size_t i = 0; i < kScanPortCount; ++i) {
            _results[i] = {kScanPorts[i].port, kScanPorts[i].name, false, false};
        }
        _state = ScanState::Scanning;
    }

    // Every loop(). Starts probes while there is room, then checks each.
    void tick() {
        if (_state != ScanState::Scanning) return;
        for (Slot& s : _slots) {
            if (s.fd < 0 && _next < kScanPortCount) start(s, _next++);
        }
        for (Slot& s : _slots) {
            if (s.fd >= 0) poll(s);
        }
        if (_finished >= kScanPortCount) {
            _state = ScanState::Done;
            _took_ms = millis() - _started_ms;
        }
    }

    ScanState state() const { return _state; }
    uint32_t ip() const { return _ip; }
    size_t probed() const { return _finished; }
    size_t total() const { return kScanPortCount; }
    size_t open_count() const { return _open_count; }
    const PortResult* results() const { return _results; }
    uint32_t elapsed_ms() const {
        if (_state == ScanState::Idle) return 0;
        if (_state == ScanState::Done) return _took_ms;
        return millis() - _started_ms;
    }

private:
    struct Slot {
        int fd = -1;
        size_t idx = 0;
        uint32_t since = 0;
    };

    void start(Slot& s, size_t idx) {
        s.idx = idx;
        s.since = millis();
        s.fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s.fd < 0) {   // out of sockets: counted as not open, not retried
            finish(s, false);
            return;
        }
        const int flags = lwip_fcntl(s.fd, F_GETFL, 0);
        lwip_fcntl(s.fd, F_SETFL, flags | O_NONBLOCK);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(kScanPorts[idx].port);
        to.sin_addr.s_addr = htonl(_ip);
        const int r = lwip_connect(s.fd, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        if (r == 0) {
            finish(s, true);
        } else if (errno != EINPROGRESS) {
            finish(s, false);
        }
    }

    void poll(Slot& s) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(s.fd, &w);
        timeval now{0, 0};
        const int r = lwip_select(s.fd + 1, nullptr, &w, nullptr, &now);
        if (r > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            lwip_getsockopt(s.fd, SOL_SOCKET, SO_ERROR, &err, &len);
            finish(s, err == 0);   // ECONNREFUSED: the device said closed
        } else if (r < 0 || millis() - s.since >= kPortTimeoutMs) {
            finish(s, false);
        }
    }

    void finish(Slot& s, bool open) {
        _results[s.idx].open = open;
        _results[s.idx].done = true;
        if (open) ++_open_count;
        ++_finished;
        close_fd(s);
    }

    // A reset rather than a FIN, so no connection lingers in TIME_WAIT
    // holding lwIP's memory after the probe.
    static void close_fd(Slot& s) {
        if (s.fd < 0) return;
        linger lg{1, 0};
        lwip_setsockopt(s.fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        lwip_close(s.fd);
        s.fd = -1;
    }

    void stop_all() {
        for (Slot& s : _slots) close_fd(s);
    }

    ScanState _state = ScanState::Idle;
    uint32_t _ip = 0;
    size_t _next = 0;
    size_t _finished = 0;
    size_t _open_count = 0;
    uint32_t _started_ms = 0;
    uint32_t _took_ms = 0;
    Slot _slots[kPortParallel];
    PortResult _results[kScanPortCount]{};
};
