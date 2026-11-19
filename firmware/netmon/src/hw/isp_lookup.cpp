#include "isp_lookup.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include <cstring>

#include "../core/report.h"

namespace {

// Plain HTTP on purpose. TLS would cost flash on a partition already at 80%,
// and the payload is descriptive rather than sensitive — the one thing worth
// protecting, this connection's public address, is exactly what the service
// has to be told in order to answer. The page says so in as many words.
const char* kHost = "ip-api.com";
const uint16_t kPort = 80;
const uint32_t kConnectMs = 4000;
const uint32_t kReadMs = 4000;

void copy_field(char* dst, size_t cap, JsonVariantConst v) {
    const char* s = v.is<const char*>() ? v.as<const char*>() : "";
    if (s == nullptr) s = "";
    strncpy(dst, s, cap - 1);
    dst[cap - 1] = '\0';
}

void fail(IspInfo& out, const char* why) {
    out.valid = false;
    strncpy(out.error, why, sizeof(out.error) - 1);
    out.error[sizeof(out.error) - 1] = '\0';
}

}  // namespace

bool isp_fetch(IspInfo& out) {
    out.error[0] = '\0';
    out.date_unix = 0;
    if (WiFi.status() != WL_CONNECTED) {
        fail(out, "Not connected to a network.");
        return false;
    }

    WiFiClient client;
    const uint32_t started = millis();
    if (!client.connect(kHost, kPort, kConnectMs)) {
        fail(out, "Could not reach the lookup service. Is the internet up?");
        return false;
    }
    const uint32_t rtt = millis() - started;

    // HTTP/1.0 so the reply cannot come back chunked; there is no value in
    // teaching this client to reassemble chunks for one small JSON object.
    // The field list keeps the response under 400 bytes.
    client.print(F("GET /json/?fields=status,message,query,isp,org,as,city,"
                   "regionName,country,timezone HTTP/1.0\r\nHost: "));
    client.print(kHost);
    client.print(F("\r\nUser-Agent: netmon\r\nConnection: close\r\n\r\n"));

    String response;
    response.reserve(768);
    const uint32_t deadline = millis() + kReadMs;
    while (client.connected() && millis() < deadline) {
        while (client.available()) {
            response += static_cast<char>(client.read());
            if (response.length() > 4096) break;   // refuse to grow unbounded
        }
        if (response.length() > 4096) break;
        delay(5);
    }
    client.stop();

    const int split = response.indexOf("\r\n\r\n");
    if (split < 0) {
        fail(out, "The lookup service sent no readable reply.");
        return false;
    }
    // The reply's Date header is the time to the second: the board has no
    // clock of its own, and saved reports are dated by it. No extra request.
    http_date_header(response.c_str(), static_cast<size_t>(split), out.date_unix);

    JsonDocument doc;
    if (deserializeJson(doc, response.substring(split + 4))) {
        fail(out, "Could not read the lookup service's reply.");
        return false;
    }
    if (strcmp(doc["status"] | "", "success") != 0) {
        fail(out, doc["message"] | "The lookup service refused the request.");
        return false;
    }

    copy_field(out.ip, sizeof(out.ip), doc["query"]);
    copy_field(out.isp, sizeof(out.isp), doc["isp"]);
    copy_field(out.org, sizeof(out.org), doc["org"]);
    copy_field(out.asn, sizeof(out.asn), doc["as"]);
    copy_field(out.city, sizeof(out.city), doc["city"]);
    copy_field(out.region, sizeof(out.region), doc["regionName"]);
    copy_field(out.country, sizeof(out.country), doc["country"]);
    copy_field(out.tz, sizeof(out.tz), doc["timezone"]);
    out.rtt_ms = rtt;
    out.valid = true;
    return true;
}
