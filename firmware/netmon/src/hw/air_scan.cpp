#include "air_scan.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>
#include <cstring>
#include <vector>

// Requires the NimBLE-Arduino library (h2zero, 2.x), the one the scanner
// sketch used. It is a fraction of the size of the core's own BLE stack.
#include <NimBLEDevice.h>

// From Arduino-ESP32 3.3.9, start-up frees the Bluetooth controller's memory
// unless something in the firmware has declared that it needs it, by
// including this header. NimBLE-Arduino 2.5.1 declares it itself; it is
// declared here as well so an older NimBLE release, from before the core
// asked for this, still finds the memory there. Including it twice is
// harmless: both only set the same flag. Older cores have no such header.
#if __has_include(<esp32-hal-alloc-ble-mem.h>)
#include <esp32-hal-alloc-ble-mem.h>
#endif

// ---- Wi-Fi --------------------------------------------------------------

namespace {

bool g_wifi_running = false;
uint32_t g_wifi_started_ms = 0;

// 120 ms a channel, asking rather than only listening: an access point
// answers a probe at once, so a short dwell still hears it, and a whole pass
// stays near a second and a half. The scanner sketch spent 300 ms a channel,
// which is four seconds with the board off its own network's channel.
const uint32_t kChannelMs = 120;

}  // namespace

bool air_wifi_begin() {
    if (g_wifi_running) return false;
    const int16_t r = WiFi.scanNetworks(true, true, false, kChannelMs);
    if (r != WIFI_SCAN_RUNNING) {
        WiFi.scanDelete();
        return false;
    }
    g_wifi_running = true;
    g_wifi_started_ms = millis();
    return true;
}

AirScanState air_wifi_poll(uint32_t limit_ms, int& count) {
    count = 0;
    if (!g_wifi_running) return AirScanState::Idle;
    const int16_t r = WiFi.scanComplete();
    if (r == WIFI_SCAN_RUNNING && millis() - g_wifi_started_ms < limit_ms) {
        return AirScanState::Running;
    }
    g_wifi_running = false;
    if (r < 0) {
        // Failed, or still going long past any real scan while the sweep
        // waits. Stopped either way, so the next scan finds the radio free.
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        return AirScanState::Failed;
    }
    count = r;
    return AirScanState::Done;
}

int air_wifi_scan_blocking(uint32_t limit_ms) {
    // The background scan, waited out here. The core's own blocking scan
    // would wait up to a minute on a scan that never finishes, and setting
    // its limit takes a call older 3.0 cores do not have.
    if (!air_wifi_begin()) return -1;
    int count = 0;
    AirScanState st = air_wifi_poll(limit_ms, count);
    while (st == AirScanState::Running) {
        delay(20);
        st = air_wifi_poll(limit_ms, count);
    }
    return st == AirScanState::Done ? count : -1;
}

namespace {
// Read by the Wi-Fi stack for as long as the look runs, after the call that
// starts it has returned, so it cannot live on that call's stack.
uint8_t g_target_bssid[6];
}  // namespace

bool air_wifi_begin_target(const Mac& bssid, uint8_t channel) {
    if (g_wifi_running) return false;
    std::memcpy(g_target_bssid, bssid.b, sizeof(g_target_bssid));
    const int16_t r =
        WiFi.scanNetworks(true, true, false, kChannelMs, channel, nullptr, g_target_bssid);
    if (r != WIFI_SCAN_RUNNING) {
        WiFi.scanDelete();
        return false;
    }
    g_wifi_running = true;
    g_wifi_started_ms = millis();
    return true;
}

bool air_wifi_running() { return g_wifi_running; }

bool air_wifi_reading(int index, AirApReading& out) {
    // The stack's own record rather than WiFi.SSID(i) and friends: no String
    // per field per network, and the SSID arrives as the 32 bytes it is.
    const wifi_ap_record_t* r =
        static_cast<const wifi_ap_record_t*>(WiFi.getScanInfoByIndex(index));
    if (r == nullptr) return false;
    std::memset(&out, 0, sizeof(out));
    std::memcpy(out.bssid.b, r->bssid, 6);
    std::memcpy(out.ssid, r->ssid, sizeof(out.ssid) - 1);
    out.ssid[sizeof(out.ssid) - 1] = '\0';
    out.rssi = r->rssi;
    out.channel = r->primary;
    out.auth = static_cast<uint8_t>(r->authmode);
    return true;
}

void air_wifi_release() { WiFi.scanDelete(); }

// ---- Bluetooth LE -------------------------------------------------------

namespace {

// Deep enough for a burst's worth of new devices while loop() is held up by a
// slow request, such as the Internet page's lookup.
const UBaseType_t kQueueDepth = 48;

QueueHandle_t g_queue = nullptr;
volatile uint32_t g_dropped = 0;
bool g_tried = false;
bool g_ready = false;

// A burst is over when the stack's end-of-scan handler has run, not when
// isScanning() turns false: the stack clears that first, then hands over the
// devices still waiting for a scan response and frees them. A burst started in
// between would free them again under the Bluetooth task's feet. So a burst
// counts as open from start() until onScanEnd(), which also comes after the
// stack resets.
std::atomic<bool> g_burst_open{false};

// While finding: the one address wanted, checked here as well as by the
// controller's filter, so the Finder still works on a controller that will not
// take the filter. Written only between bursts, with g_only_on off while
// g_only changes.
std::atomic<bool> g_only_on{false};
volatile uint8_t g_only[6] = {0};

// Runs in the Bluetooth host task, so it only copies and queues; everything
// else happens in loop().
class Listener : public NimBLEScanCallbacks {
  public:
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (dev == nullptr || g_queue == nullptr) return;
        BleSighting s{};
        const NimBLEAddress& a = dev->getAddress();
        const uint8_t* v = a.getVal();           // least significant byte first
        for (size_t i = 0; i < 6; ++i) s.addr.b[i] = v[5 - i];
        if (g_only_on) {
            for (size_t i = 0; i < 6; ++i) {
                if (s.addr.b[i] != g_only[i]) return;
            }
        }
        s.kind = a.isPublic()   ? BleAddrKind::Public
                 : a.isStatic() ? BleAddrKind::Static
                                : BleAddrKind::Private;
        s.rssi = dev->getRSSI();
        s.at_ms = millis();
        // The raw advertisement, scan response included, as NimBLE keeps it.
        // What it means is worked out in loop(): ble_adv.h and ble_type.h.
        const std::vector<uint8_t>& raw = dev->getPayload();
        const size_t n = raw.size() < sizeof(s.payload) ? raw.size() : sizeof(s.payload);
        if (n > 0) std::memcpy(s.payload, raw.data(), n);
        s.len = static_cast<uint8_t>(n);
        s.focused = g_only_on;
        if (xQueueSend(g_queue, &s, 0) != pdTRUE) g_dropped = g_dropped + 1;
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        (void)results;
        (void)reason;
        g_burst_open = false;
    }
};

Listener g_listener;

// Listening to everyone, as the Nearby bursts do, or to the one device the
// Finder is after. Switched only between bursts: the controller refuses a
// change of filter while it is scanning.
enum class Listen : uint8_t { Everyone, One };
Listen g_listen = Listen::Everyone;
NimBLEAddress g_one;
bool g_one_filtered = false;   // the controller took the filter for g_one

// Half the time during an ordinary burst: Wi-Fi is still carrying the DHCP
// listener and whoever has the dashboard open. Most of it while finding,
// which is short and is what somebody is standing there waiting on.
const uint16_t kIntervalMs = 100;
const uint16_t kWindowMs = 50;
const uint16_t kFindWindowMs = 80;

bool clear_filter() {
    while (NimBLEDevice::getWhiteListCount() > 0) {
        if (!NimBLEDevice::whiteListRemove(NimBLEDevice::getWhiteListAddress(0))) return false;
    }
    return true;
}

bool listen_everyone(NimBLEScan* scan) {
    if (g_listen == Listen::Everyone) return true;
    // Whatever is left in the filter is ignored once the policy says so, so a
    // failure to empty it does not stop the bursts.
    clear_filter();
    g_only_on = false;
    scan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
    scan->setDuplicateFilter(1);         // one report per device per burst
    scan->setActiveScan(true);
    scan->setWindow(kWindowMs);
    g_listen = Listen::Everyone;
    return true;
}

bool listen_one(NimBLEScan* scan, const Mac& addr, BleAddrKind kind) {
    const NimBLEAddress a(addr.b, kind == BleAddrKind::Public ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM);
    // Set up already; or set up without the controller's filter, which is
    // tried again in case that failure was a passing one.
    if (g_listen == Listen::One && g_one == a && g_one_filtered) return true;
    g_only_on = false;
    for (size_t i = 0; i < 6; ++i) g_only[i] = addr.b[i];
    g_only_on = true;
    // The controller's filter keeps everything else from reaching the host at
    // all. If it will not take the address, the check in onResult() still
    // keeps the queue to this one device, at the cost of the host hearing the
    // whole room.
    const bool filtered = clear_filter() && NimBLEDevice::whiteListAdd(a);
    g_one_filtered = filtered;
    scan->setFilterPolicy(filtered ? BLE_HCI_SCAN_FILT_USE_WL : BLE_HCI_SCAN_FILT_NO_WL);
    scan->setDuplicateFilter(0);         // every advertisement
    scan->setActiveScan(false);
    scan->setWindow(kFindWindowMs);
    g_listen = Listen::One;
    g_one = a;
    return true;
}

}  // namespace

bool air_ble_begin() {
    if (g_tried) return g_ready;
    g_tried = true;
    g_queue = xQueueCreate(kQueueDepth, sizeof(BleSighting));
    if (g_queue == nullptr) return false;
    if (!NimBLEDevice::init("")) return false;
    NimBLEScan* scan = NimBLEDevice::getScan();
    // One report per device per burst, kept nowhere but the queue above: the
    // table in loop() is the only list, so the stack need not hold one too.
    scan->setScanCallbacks(&g_listener, false);
    scan->setMaxResults(0);
    // Active, so devices that keep their name for the scan response give it.
    scan->setActiveScan(true);
    // Listening half the time. The scanner sketch listened 99% of it, which
    // left Wi-Fi the scraps; here Wi-Fi is still carrying the DHCP listener and
    // whoever has the dashboard open.
    scan->setInterval(kIntervalMs);
    scan->setWindow(kWindowMs);
    g_ready = true;
    return true;
}

bool air_ble_ready() { return g_ready; }

namespace {

bool start_burst(NimBLEScan* scan, uint32_t ms) {
    g_burst_open = true;
    if (scan->start(ms, false, false)) return true;
    g_burst_open = false;
    return false;
}

}  // namespace

bool air_ble_burst(uint32_t ms) {
    if (!g_ready || air_ble_scanning()) return false;
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (!listen_everyone(scan)) return false;
    return start_burst(scan, ms);
}

bool air_ble_focus_burst(const Mac& addr, BleAddrKind kind, uint32_t ms) {
    if (!g_ready || air_ble_scanning()) return false;
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (!listen_one(scan, addr, kind)) return false;
    return start_burst(scan, ms);
}

bool air_ble_scanning() {
    return g_ready && (g_burst_open || NimBLEDevice::getScan()->isScanning());
}

void air_ble_stop() {
    if (g_ready && NimBLEDevice::getScan()->isScanning()) NimBLEDevice::getScan()->stop();
    // A cancelled scan gets no end-of-scan handler, so nothing else closes it.
    g_burst_open = false;
}

bool air_ble_poll(BleSighting& out) {
    return g_queue != nullptr && xQueueReceive(g_queue, &out, 0) == pdTRUE;
}

uint32_t air_ble_dropped() { return g_dropped; }
