// Dependency-free harness: no gtest, no network, no Arduino.
#include <cstdio>
#include <cstring>
#include <cstdint>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            ++g_failures;                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                  \
    } while (0)

#define CHECK_STR(actual, expected)                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (std::strcmp((actual), (expected)) != 0) {                      \
            ++g_failures;                                                  \
            std::printf("FAIL %s:%d  got \"%s\" want \"%s\"\n",            \
                        __FILE__, __LINE__, (actual), (expected));         \
        }                                                                  \
    } while (0)

#include "../netmon/src/core/mac.h"
#include "../netmon/src/core/event_log.h"

static void test_mac() {
    Mac m{};
    CHECK(mac_parse("aa:bb:cc:dd:ee:ff", m));
    char out[18];
    mac_format(m, out);
    CHECK_STR(out, "AA:BB:CC:DD:EE:FF");

    CHECK(mac_parse("AA-BB-CC-DD-EE-FF", m));
    mac_format(m, out);
    CHECK_STR(out, "AA:BB:CC:DD:EE:FF");

    CHECK(mac_parse("aabbccddeeff", m));
    mac_format(m, out);
    CHECK_STR(out, "AA:BB:CC:DD:EE:FF");

    CHECK(!mac_parse("", m));
    CHECK(!mac_parse("nonsense", m));
    CHECK(!mac_parse("AA:BB:CC", m));
    CHECK(!mac_parse("AA:BB:CC:DD:EE:FG", m));

    Mac a{}, b{};
    mac_parse("00:11:22:33:44:55", a);
    mac_parse("00:11:22:33:44:55", b);
    CHECK(mac_equal(a, b));
    mac_parse("00:11:22:33:44:56", b);
    CHECK(!mac_equal(a, b));

    // Locally administered = bit 1 of the first octet.
    Mac local{}, global{};
    mac_parse("FE:1E:6F:54:16:79", local);
    mac_parse("B8:27:EB:12:34:56", global);
    CHECK(mac_is_local(local));
    CHECK(!mac_is_local(global));

    mac_parse("B8:27:EB:12:34:56", global);
    CHECK(mac_oui(global) == 0xB827EBu);
}


static void test_event_log() {
    EventLog<3> log;
    Mac a{}, b{}, c{}, d{};
    mac_parse("AA:BB:CC:00:00:01", a);
    mac_parse("AA:BB:CC:00:00:02", b);
    mac_parse("AA:BB:CC:00:00:03", c);
    mac_parse("AA:BB:CC:00:00:04", d);

    log.add(EventType::DeviceSeen, a, 1, "one");
    log.stamp_last(10);
    log.add(EventType::DeviceOffline, b, 2, "two");
    log.stamp_last(20);
    log.add(EventType::DeviceBack, c, 3, "three");
    log.stamp_last(30);
    CHECK(log.size() == 3);
    CHECK(log.newest(0).at_s == 30);
    CHECK(log.newest(0).type == EventType::DeviceBack);
    CHECK(log.newest(1).type == EventType::DeviceOffline);
    CHECK(log.newest(2).type == EventType::DeviceSeen);

    log.add(EventType::Hostname, d, 4, "four");
    log.stamp_last(40);
    CHECK(log.size() == 3);
    CHECK(log.newest(0).type == EventType::Hostname);
    CHECK(log.newest(2).type == EventType::DeviceOffline);
}

#include "../netmon/src/core/cidr.h"

static void test_cidr() {
    uint32_t ip = 0;
    CHECK(ipv4_parse("192.168.2.11", ip));
    CHECK(ip == ipv4_from_octets(192, 168, 2, 11));

    char buf[20];
    ipv4_format(ip, buf);
    CHECK_STR(buf, "192.168.2.11");

    CHECK(!ipv4_parse("192.168.2", ip));
    CHECK(!ipv4_parse("192.168.2.256", ip));
    CHECK(!ipv4_parse("a.b.c.d", ip));
    CHECK(!ipv4_parse("", ip));

    const uint32_t mask24 = ipv4_from_octets(255, 255, 255, 0);
    CHECK(mask_to_prefix(mask24) == 24);
    CHECK(mask_to_prefix(ipv4_from_octets(255, 255, 0, 0)) == 16);
    CHECK(mask_to_prefix(ipv4_from_octets(255, 255, 255, 252)) == 30);

    const uint32_t host = ipv4_from_octets(192, 168, 2, 11);
    CHECK(subnet_of(host, mask24) == ipv4_from_octets(192, 168, 2, 0));

    cidr_format(host, mask24, buf);
    CHECK_STR(buf, "192.168.2.0/24");

    const uint32_t net = ipv4_from_octets(192, 168, 2, 0);
    CHECK(subnet_contains(net, mask24, ipv4_from_octets(192, 168, 2, 1)));
    CHECK(subnet_contains(net, mask24, ipv4_from_octets(192, 168, 2, 254)));
    CHECK(!subnet_contains(net, mask24, ipv4_from_octets(192, 168, 3, 1)));

    // Sweep range excludes network and broadcast addresses.
    CHECK(host_first(net, mask24) == ipv4_from_octets(192, 168, 2, 1));
    CHECK(host_last(net, mask24) == ipv4_from_octets(192, 168, 2, 254));

    // A /30 leaves exactly two usable hosts.
    const uint32_t mask30 = ipv4_from_octets(255, 255, 255, 252);
    const uint32_t net30 = ipv4_from_octets(10, 0, 0, 4);
    CHECK(host_first(net30, mask30) == ipv4_from_octets(10, 0, 0, 5));
    CHECK(host_last(net30, mask30) == ipv4_from_octets(10, 0, 0, 6));
}

#include "../netmon/src/core/dhcp_parse.h"

// Assemble a plausible captured frame: 802.11 data header, LLC/SNAP,
// IPv4, UDP 68->67, then a DHCP REQUEST carrying Option 12.
static size_t build_dhcp_frame(uint8_t* buf, size_t cap, bool qos,
                               const char* hostname) {
    std::memset(buf, 0, cap);
    size_t o = 0;
    buf[0] = 0x08;                 // type data, subtype data
    if (qos) buf[0] = 0x88;        // QoS data
    buf[1] = 0x01;                 // ToDS
    o = 24;
    if (qos) o += 2;               // QoS control field

    const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00};
    std::memcpy(buf + o, snap, 8);
    o += 8;

    buf[o + 0] = 0x45;             // IPv4, IHL 5
    buf[o + 9] = 17;               // UDP
    o += 20;

    buf[o + 0] = 0x00; buf[o + 1] = 68;   // src 68
    buf[o + 2] = 0x00; buf[o + 3] = 67;   // dst 67
    o += 8;

    const size_t dhcp_off = o;
    buf[o + 0] = 1;                // BOOTREQUEST
    buf[o + 1] = 1;                // ethernet
    buf[o + 2] = 6;                // hlen
    const uint8_t chaddr[6] = {0xB8, 0x27, 0xEB, 0x12, 0x34, 0x56};
    std::memcpy(buf + o + 28, chaddr, 6);
    const uint8_t cookie[4] = {0x63, 0x82, 0x53, 0x63};
    std::memcpy(buf + o + 236, cookie, 4);

    size_t opt = dhcp_off + 240;
    buf[opt++] = 53; buf[opt++] = 1; buf[opt++] = 3;      // REQUEST
    buf[opt++] = 50; buf[opt++] = 4;                       // requested IP
    buf[opt++] = 192; buf[opt++] = 168; buf[opt++] = 2; buf[opt++] = 51;
    const size_t hlen = std::strlen(hostname);
    buf[opt++] = 12; buf[opt++] = static_cast<uint8_t>(hlen);
    std::memcpy(buf + opt, hostname, hlen);
    opt += hlen;
    buf[opt++] = 255;                                      // END

    return opt;
}

static void test_dhcp() {
    uint8_t frame[512];
    DhcpInfo info{};

    size_t n = build_dhcp_frame(frame, sizeof frame, false, "raspberrypi");
    CHECK(dhcp_from_ieee80211(frame, n, info));
    CHECK(info.has_hostname);
    CHECK_STR(info.hostname, "raspberrypi");
    CHECK(info.msg_type == 3);
    CHECK(info.requested_ip == ipv4_from_octets(192, 168, 2, 51));
    char mac[18];
    mac_format(info.client, mac);
    CHECK_STR(mac, "B8:27:EB:12:34:56");

    // QoS data frames carry two extra header bytes; offset must adapt.
    DhcpInfo qinfo{};
    n = build_dhcp_frame(frame, sizeof frame, true, "laptop");
    CHECK(dhcp_from_ieee80211(frame, n, qinfo));
    CHECK_STR(qinfo.hostname, "laptop");

    // A frame truncated mid-options must be rejected, not read past.
    DhcpInfo tinfo{};
    n = build_dhcp_frame(frame, sizeof frame, false, "truncated");
    CHECK(!dhcp_from_ieee80211(frame, 40, tinfo));

    // Non-DHCP UDP ports are ignored.
    DhcpInfo pinfo{};
    n = build_dhcp_frame(frame, sizeof frame, false, "other");
    frame[24 + 8 + 20 + 3] = 53;   // dst port 53 instead of 67
    CHECK(!dhcp_from_ieee80211(frame, n, pinfo));

    // A management frame is not a data frame.
    DhcpInfo minfo{};
    n = build_dhcp_frame(frame, sizeof frame, false, "beacon");
    frame[0] = 0x80;               // beacon
    CHECK(!dhcp_from_ieee80211(frame, n, minfo));

    // An option claiming more length than the frame holds must be rejected.
    DhcpInfo binfo{};
    n = build_dhcp_frame(frame, sizeof frame, false, "overflow");
    frame[24 + 8 + 20 + 8 + 240 + 1] = 200;   // msg-type option len 200
    CHECK(!dhcp_from_ieee80211(frame, n, binfo));

    // An oversized hostname is truncated, never overflowed.
    char big[200];
    std::memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    DhcpInfo ginfo{};
    n = build_dhcp_frame(frame, sizeof frame, false, big);
    if (dhcp_from_ieee80211(frame, n, ginfo)) {
        CHECK(std::strlen(ginfo.hostname) < sizeof ginfo.hostname);
    }
}

#include "../netmon/src/core/classify.h"

static void test_classify() {
    CHECK(in_learning_window(1000, 1000, 600));
    CHECK(in_learning_window(1599, 1000, 600));
    CHECK(!in_learning_window(1600, 1000, 600));
    CHECK(!in_learning_window(99999, 1000, 600));
    CHECK(!in_learning_window(1000, 1000, 0));

    CHECK(classify_new(1100, 1000, 600) == Status::Known);
    CHECK(classify_new(9000, 1000, 600) == Status::Unknown);

    // No successful scan yet: the window has not started, so nothing can
    // be judged an intruder. This is the NAS bug that flagged 33 devices.
    CHECK(in_learning_window(50000, 0, 600));
    CHECK(classify_new(50000, 0, 600) == Status::Known);
}

#include "../netmon/src/core/device_table.h"

static Mac mk(const char* s) {
    Mac m{};
    mac_parse(s, m);
    return m;
}

static void test_table() {
    DeviceTable<4> t;
    bool created = false;

    Device* d = t.upsert(mk("AA:BB:CC:00:00:01"), ipv4_from_octets(192,168,2,5),
                         100, Status::Known, created);
    CHECK(d != nullptr);
    CHECK(created);
    CHECK(t.size() == 1);
    CHECK(d->status == Status::Known);
    CHECK(d->first_seen == 100);
    CHECK(d->online);

    // Seeing it again updates rather than duplicating.
    d = t.upsert(mk("AA:BB:CC:00:00:01"), ipv4_from_octets(192,168,2,6),
                 200, Status::Unknown, created);
    CHECK(!created);
    CHECK(t.size() == 1);
    CHECK(d->last_seen == 200);
    CHECK(d->ip == ipv4_from_octets(192,168,2,6));
    CHECK(d->status == Status::Known);   // status is not re-decided on update
    CHECK(d->first_seen == 100);

    CHECK(t.set_hostname(mk("AA:BB:CC:00:00:01"), "raspberrypi"));
    CHECK_STR(t.find(mk("AA:BB:CC:00:00:01"))->hostname, "raspberrypi");
    CHECK(!t.set_hostname(mk("11:22:33:44:55:66"), "nope"));

    t.upsert(mk("AA:BB:CC:00:00:02"), 0, 210, Status::Known, created);
    t.upsert(mk("AA:BB:CC:00:00:03"), 0, 220, Status::Known, created);
    t.upsert(mk("AA:BB:CC:00:00:04"), 0, 230, Status::Known, created);
    CHECK(t.size() == 4);

    // At capacity the least-recently-seen entry is evicted: .01 at t=200.
    t.upsert(mk("AA:BB:CC:00:00:05"), 0, 240, Status::Known, created);
    CHECK(t.size() == 4);
    CHECK(t.find(mk("AA:BB:CC:00:00:01")) == nullptr);
    CHECK(t.find(mk("AA:BB:CC:00:00:05")) != nullptr);

    // Presence: silence beyond the threshold flips online to false.
    // At now=415 with a 180s threshold, .02 (210) .03 (220) .04 (230) are
    // stale by 205/195/185s, while .05 (240) is stale by only 175s.
    CHECK(t.mark_offline(415, 180) == 3);
    CHECK(!t.find(mk("AA:BB:CC:00:00:02"))->online);
    CHECK(t.find(mk("AA:BB:CC:00:00:05"))->online);
    CHECK(t.mark_offline(415, 180) == 0);   // already offline, not recounted
}

// A return is reported once, on the sighting that ends an absence, whichever
// path saw it. The DHCP path used to refresh the row without asking.
static void test_table_sighting() {
    DeviceTable<4> t;
    bool created = false, returned = true;
    const Mac a = mk("AA:BB:CC:00:00:01");

    t.sight(a, ipv4_from_octets(192,168,2,5), 100, Status::Known, created, returned);
    CHECK(created);
    CHECK(!returned);                 // a first sighting is not a return
    t.sight(a, 0, 110, Status::Unknown, created, returned);
    CHECK(!created);
    CHECK(!returned);                 // still online: an ordinary refresh

    CHECK(t.mark_offline(400, 180) == 1);
    Device* d = t.sight(a, ipv4_from_octets(192,168,2,9), 420, Status::Unknown,
                        created, returned);
    CHECK(!created);
    CHECK(returned);                  // it had gone offline
    CHECK(d != nullptr && d->online);
    CHECK(d != nullptr && d->online_since == 420);
    CHECK(d != nullptr && d->status == Status::Known);   // not re-judged
    CHECK(d != nullptr && d->ip == ipv4_from_octets(192,168,2,9));
    t.sight(a, 0, 430, Status::Unknown, created, returned);
    CHECK(!returned);                 // once, not on every later sighting
}

// A full table gives up an offline row before an online one, a randomised
// address before an unknown one, and a known device last. Dropping a known
// device made it come back as unknown once the learning window had closed.
static void test_table_eviction() {
    DeviceTable<4> t;
    bool c = false;
    t.upsert(mk("00:11:22:00:00:01"), 0, 100, Status::Known, c);
    t.upsert(mk("02:11:22:00:00:02"), 0, 300, Status::Private, c);
    t.upsert(mk("00:11:22:00:00:03"), 0, 200, Status::Unknown, c);
    t.upsert(mk("00:11:22:00:00:04"), 0, 900, Status::Known, c);
    CHECK(t.mark_offline(1000, 180) == 3);          // all but .04

    // The randomised address goes first, though it is the newest offline row.
    t.upsert(mk("00:11:22:00:00:05"), 0, 1000, Status::Unknown, c);
    CHECK(c);
    CHECK(t.size() == 4);
    CHECK(t.find(mk("02:11:22:00:00:02")) == nullptr);
    CHECK(t.find(mk("00:11:22:00:00:01")) != nullptr);

    // Then the offline unknown device, still ahead of the older known one.
    t.upsert(mk("00:11:22:00:00:06"), 0, 1010, Status::Unknown, c);
    CHECK(t.find(mk("00:11:22:00:00:03")) == nullptr);
    CHECK(t.find(mk("00:11:22:00:00:01")) != nullptr);

    // The known device only once nothing else is offline.
    t.upsert(mk("00:11:22:00:00:07"), 0, 1020, Status::Unknown, c);
    CHECK(t.find(mk("00:11:22:00:00:01")) == nullptr);
    CHECK(t.find(mk("00:11:22:00:00:04")) != nullptr);   // online, kept

    // Everything online: trust first, then the oldest sighting. .04 is the
    // oldest row but known, so the oldest unknown (.05) gives way.
    t.upsert(mk("00:11:22:00:00:08"), 0, 1030, Status::Unknown, c);
    CHECK(t.find(mk("00:11:22:00:00:05")) == nullptr);
    CHECK(t.find(mk("00:11:22:00:00:04")) != nullptr);
    CHECK(t.find(mk("00:11:22:00:00:08")) != nullptr);
    CHECK(t.size() == 4);
}

// What a pass found, for anchoring the learning window. This board and the
// gateway turn up whether or not the sweep reaches anything, so they do not
// count as finding something.
static void test_table_seen_since() {
    DeviceTable<8> t;
    bool c = false;
    const Mac self = mk("D4:E9:F4:12:34:56");
    const uint32_t gw = ipv4_from_octets(192,168,2,1);
    size_t others = 99;

    CHECK(t.seen_since(0, self, gw, others) == 0);
    CHECK(others == 0);

    t.upsert(self, ipv4_from_octets(192,168,2,27), 500, Status::Known, c);
    t.upsert(mk("50:C7:BF:00:00:01"), gw, 505, Status::Known, c);
    CHECK(t.seen_since(500, self, gw, others) == 2);
    CHECK(others == 0);

    t.upsert(mk("00:11:22:00:00:09"), ipv4_from_octets(192,168,2,40), 506,
             Status::Known, c);
    CHECK(t.seen_since(500, self, gw, others) == 3);
    CHECK(others == 1);

    // Seen before the pass began: not part of it.
    CHECK(t.seen_since(507, self, gw, others) == 0);
    CHECK(others == 0);
}

#include "../netmon/src/core/oui_lookup.h"

static void test_oui() {
    static const OuiEntry table[] = {
        {0x001A11u, "Google"},
        {0x240AC4u, "Espressif"},
        {0x5091E3u, "TP-Link"},
        {0xB827EBu, "Raspberry Pi Foundation"},
    };
    const size_t n = sizeof(table) / sizeof(table[0]);

    Mac m{};
    mac_parse("B8:27:EB:12:34:56", m);
    CHECK_STR(oui_lookup(mac_oui(m), table, n), "Raspberry Pi Foundation");

    mac_parse("50:91:e3:ab:cd:ef", m);
    CHECK_STR(oui_lookup(mac_oui(m), table, n), "TP-Link");

    mac_parse("00:1A:11:00:00:01", m);
    CHECK_STR(oui_lookup(mac_oui(m), table, n), "Google");

    mac_parse("AA:AA:AA:00:00:01", m);
    CHECK(oui_lookup(mac_oui(m), table, n) == nullptr);

    CHECK(oui_lookup(0xB827EBu, table, 0) == nullptr);
    CHECK(oui_lookup(0xB827EBu, nullptr, n) == nullptr);

    // A randomised MAC belongs to no vendor; callers should not even ask.
    mac_parse("FE:1E:6F:54:16:79", m);
    CHECK(mac_is_local(m));
    CHECK(oui_lookup(mac_oui(m), table, n) == nullptr);
}

#include "../netmon/src/core/validate.h"

static void test_validate() {
    // same_text: netmon.ino's build-time guard against the placeholder
    // update password in secrets.example.h.
    static_assert(same_text("change-me", "change-me"), "same_text: equal");
    static_assert(!same_text("change-me", "change-me!"), "same_text: prefix");
    CHECK(same_text("", ""));
    CHECK(same_text("abc", "abc"));
    CHECK(!same_text("abc", "abd"));
    CHECK(!same_text("ab", "abc"));
    CHECK(!same_text("abc", "ab"));
    CHECK(!same_text("", "a"));

    // Credentials
    CHECK(validate_credentials("home", "secret123") == ConfigError::None);
    CHECK(validate_credentials("open-net", "") == ConfigError::None);
    CHECK(validate_credentials("", "x") == ConfigError::SsidEmpty);
    CHECK(validate_credentials(nullptr, "x") == ConfigError::SsidEmpty);
    char long_ssid[40];
    std::memset(long_ssid, 'a', sizeof long_ssid - 1);
    long_ssid[sizeof long_ssid - 1] = '\0';
    CHECK(validate_credentials(long_ssid, "x") == ConfigError::SsidTooLong);
    char long_pass[80];
    std::memset(long_pass, 'b', sizeof long_pass - 1);
    long_pass[sizeof long_pass - 1] = '\0';
    CHECK(validate_credentials("net", long_pass) == ConfigError::PasswordTooLong);

    // A netmask must be contiguous ones followed by zeros.
    CHECK(mask_is_contiguous(ipv4_from_octets(255, 255, 255, 0)));
    CHECK(mask_is_contiguous(ipv4_from_octets(255, 255, 255, 252)));
    CHECK(mask_is_contiguous(0));
    CHECK(mask_is_contiguous(0xFFFFFFFFu));
    CHECK(!mask_is_contiguous(ipv4_from_octets(255, 0, 255, 0)));
    CHECK(!mask_is_contiguous(ipv4_from_octets(255, 255, 0, 1)));

    // Static config
    CHECK(validate_static("192.168.2.50", "255.255.255.0", "192.168.2.1") ==
          ConfigError::None);
    CHECK(validate_static("nope", "255.255.255.0", "192.168.2.1") ==
          ConfigError::BadIp);
    CHECK(validate_static("192.168.2.50", "255.0.255.0", "192.168.2.1") ==
          ConfigError::BadMask);
    CHECK(validate_static("192.168.2.50", "255.255.255.0", "junk") ==
          ConfigError::BadGateway);
    // The classic misconfiguration: a gateway outside the subnet leaves
    // the device unreachable, so refuse it rather than bricking access.
    CHECK(validate_static("192.168.2.50", "255.255.255.0", "192.168.9.1") ==
          ConfigError::GatewayOutsideSubnet);

    // Intervals
    CHECK(validate_intervals(60, 30, 180, 600) == ConfigError::None);
    CHECK(validate_intervals(4, 30, 180, 600) == ConfigError::IntervalOutOfRange);
    CHECK(validate_intervals(60, 30, 180, 100000) ==
          ConfigError::IntervalOutOfRange);
    CHECK(validate_intervals(60, 30, 5, 600) == ConfigError::IntervalOutOfRange);

    // The 0.0.0.0/0 guard: SoftAP reports a zero mask, and a sweeper
    // handed that would try to walk the whole address space.
    CHECK(sweepable(ipv4_from_octets(192, 168, 2, 0),
                    ipv4_from_octets(255, 255, 255, 0)));
    CHECK(!sweepable(0, 0));
    CHECK(!sweepable(ipv4_from_octets(10, 0, 0, 0),
                     ipv4_from_octets(255, 0, 0, 0)));   // /8 is too large
    CHECK(sweepable(ipv4_from_octets(172, 16, 0, 0),
                    ipv4_from_octets(255, 255, 0, 0)));  // /16 is the limit

    CHECK_STR(config_error_text(ConfigError::None), "ok");
    CHECK(std::strlen(config_error_text(ConfigError::GatewayOutsideSubnet)) > 0);
}

#include "../netmon/src/core/sweep.h"

static void test_sweep() {
    SweepPlan<8> plan;
    const uint32_t net24 = ipv4_from_octets(192, 168, 2, 0);
    const uint32_t mask24 = ipv4_from_octets(255, 255, 255, 0);

    CHECK(plan.begin(net24, mask24));
    CHECK(plan.active());
    CHECK(plan.total() == 254);
    CHECK(plan.remaining() == 254);

    uint32_t buf[16];
    size_t n = plan.next_batch(buf, 16);
    CHECK(n == 8);                                   // capped by MAX_BATCH
    CHECK(buf[0] == ipv4_from_octets(192, 168, 2, 1));   // .0 is excluded
    CHECK(buf[7] == ipv4_from_octets(192, 168, 2, 8));
    CHECK(plan.remaining() == 246);

    // Walk the rest and confirm every host is visited exactly once, with
    // the network and broadcast addresses never appearing.
    size_t seen = n;
    uint32_t lastaddr = buf[7];
    while (plan.active()) {
        n = plan.next_batch(buf, 16);
        CHECK(n > 0);
        for (size_t i = 0; i < n; ++i) {
            CHECK(buf[i] == lastaddr + 1);           // strictly sequential
            lastaddr = buf[i];
        }
        seen += n;
    }
    CHECK(seen == 254);
    CHECK(lastaddr == ipv4_from_octets(192, 168, 2, 254));   // .255 excluded
    CHECK(!plan.active());
    CHECK(plan.next_batch(buf, 16) == 0);            // exhausted stays exhausted
    CHECK(plan.remaining() == 0);

    // A caller-supplied cap below MAX_BATCH is honoured.
    CHECK(plan.begin(net24, mask24));
    CHECK(plan.next_batch(buf, 3) == 3);

    // /16 is the largest sweepable range.
    SweepPlan<8> wide;
    CHECK(wide.begin(ipv4_from_octets(172, 16, 0, 0),
                     ipv4_from_octets(255, 255, 0, 0)));
    CHECK(wide.total() == 65534);

    // Refused ranges: SoftAP's zero mask, and anything wider than /16.
    SweepPlan<8> bad;
    CHECK(!bad.begin(0, 0));
    CHECK(!bad.active());
    CHECK(bad.total() == 0);
    CHECK(bad.next_batch(buf, 16) == 0);
    CHECK(!bad.begin(ipv4_from_octets(10, 0, 0, 0),
                     ipv4_from_octets(255, 0, 0, 0)));

    // A /30 leaves two usable hosts; a /32 leaves none.
    SweepPlan<8> tiny;
    CHECK(tiny.begin(ipv4_from_octets(10, 0, 0, 4),
                     ipv4_from_octets(255, 255, 255, 252)));
    CHECK(tiny.total() == 2);
    CHECK(tiny.next_batch(buf, 16) == 2);
    CHECK(buf[0] == ipv4_from_octets(10, 0, 0, 5));
    CHECK(buf[1] == ipv4_from_octets(10, 0, 0, 6));
    CHECK(!tiny.active());

    SweepPlan<8> single;
    CHECK(!single.begin(ipv4_from_octets(10, 0, 0, 1), 0xFFFFFFFFu));
    CHECK(single.total() == 0);
}

#include "../netmon/src/core/oui_table.h"

static void test_oui_table_is_sorted() {
    // oui_lookup binary-searches, so an out-of-order table fails silently
    // on some prefixes and works on others — the worst kind of bug.
    CHECK(BUILTIN_OUI_COUNT > 20);
    for (size_t i = 1; i < BUILTIN_OUI_COUNT; ++i) {
        CHECK(BUILTIN_OUI[i - 1].prefix < BUILTIN_OUI[i].prefix);
    }
    for (size_t i = 0; i < BUILTIN_OUI_COUNT; ++i) {
        CHECK(BUILTIN_OUI[i].vendor != nullptr);
        CHECK(BUILTIN_OUI[i].vendor[0] != '\0');
        CHECK(BUILTIN_OUI[i].prefix <= 0xFFFFFFu);   // three octets only
    }

    // Every entry must be findable through the real lookup path.
    for (size_t i = 0; i < BUILTIN_OUI_COUNT; ++i) {
        CHECK(oui_lookup(BUILTIN_OUI[i].prefix, BUILTIN_OUI, BUILTIN_OUI_COUNT) !=
              nullptr);
    }

    // Spot-checks against real hardware.
    Mac m{};
    mac_parse("B8:27:EB:12:34:56", m);
    CHECK_STR(oui_lookup(mac_oui(m), BUILTIN_OUI, BUILTIN_OUI_COUNT),
              "Raspberry Pi Foundation");
    mac_parse("D4:E9:F4:12:34:56", m);           // the monitor board itself
    CHECK(oui_lookup(mac_oui(m), BUILTIN_OUI, BUILTIN_OUI_COUNT) != nullptr);
    mac_parse("AA:AA:AA:00:00:01", m);
    CHECK(oui_lookup(mac_oui(m), BUILTIN_OUI, BUILTIN_OUI_COUNT) == nullptr);
}

#include "../netmon/src/hw/config_store.h"
#include "../netmon/src/core/net_list.h"

// The remembered-network list, on the firmware's own NetworkCred. Order is
// what these tests guard: every entry ahead of the reachable one costs a
// 12 second timeout at start-up.
static void netlist_fill(NetworkCred (&n)[4], uint8_t& c,
                         const char* const* ssids, size_t k) {
    std::memset(n, 0, sizeof(n));
    c = 0;
    for (size_t i = 0; i < k; ++i) netlist_remember(n, c, ssids[i], ssids[i]);
}

static bool netlist_is(const NetworkCred (&n)[4], uint8_t c,
                       const char* const* want, size_t k) {
    if (c != k) return false;
    for (size_t i = 0; i < k; ++i) {
        if (std::strcmp(n[i].ssid, want[i]) != 0) return false;
    }
    return true;
}

static void test_netlist() {
    NetworkCred n[4];
    uint8_t c = 0;
    std::memset(n, 0, sizeof(n));

    // A new network goes on the front.
    CHECK(netlist_remember(n, c, "home", "hpass"));
    CHECK(netlist_remember(n, c, "Office-WiFi", "wpass"));
    {
        const char* want[] = {"Office-WiFi", "home"};
        CHECK(netlist_is(n, c, want, 2));
    }

    // The field report: re-saving the network the board lives on used to
    // update it in place, leaving a network visited once ahead of it. It
    // must move to the front, keep its own password when the field is left
    // blank, and leave the other entry's password alone.
    CHECK(netlist_remember(n, c, "home", ""));
    {
        const char* want[] = {"home", "Office-WiFi"};
        CHECK(netlist_is(n, c, want, 2));
    }
    CHECK_STR(n[0].pass, "hpass");
    CHECK_STR(n[1].pass, "wpass");

    // Re-saving the entry already at the front changes nothing.
    CHECK(netlist_remember(n, c, "home", ""));
    {
        const char* want[] = {"home", "Office-WiFi"};
        CHECK(netlist_is(n, c, want, 2));
    }

    // A typed password replaces the stored one.
    CHECK(netlist_remember(n, c, "Office-WiFi", "new"));
    CHECK_STR(n[0].ssid, "Office-WiFi");
    CHECK_STR(n[0].pass, "new");
    CHECK_STR(n[1].pass, "hpass");

    // Blank on a network never seen is an open network, never a neighbour's
    // password.
    CHECK(netlist_remember(n, c, "cafe", ""));
    CHECK(c == 3 && std::strcmp(n[0].ssid, "cafe") == 0 && n[0].pass[0] == '\0');

    // The password argument may point INTO the list, at an entry the shift
    // is about to overwrite. It has to be read before anything moves.
    CHECK(netlist_remember(n, c, "home", n[2].pass));
    {
        const char* want[] = {"home", "cafe", "Office-WiFi"};
        CHECK(netlist_is(n, c, want, 3));
    }
    CHECK_STR(n[0].pass, "hpass");
    // ... and so may the SSID.
    CHECK(netlist_remember(n, c, n[2].ssid, ""));
    {
        const char* want[] = {"Office-WiFi", "home", "cafe"};
        CHECK(netlist_is(n, c, want, 3));
    }
    CHECK_STR(n[0].pass, "new");

    // Full list: a fifth network pushes the oldest off the end.
    {
        const char* four[] = {"a", "b", "c", "d"};
        netlist_fill(n, c, four, 4);
        const char* want[] = {"d", "c", "b", "a"};
        CHECK(netlist_is(n, c, want, 4));
        CHECK(netlist_remember(n, c, "e", "x"));
        const char* after[] = {"e", "d", "c", "b"};
        CHECK(netlist_is(n, c, after, 4));
        CHECK(netlist_find(n, c, "a") == -1);
    }

    // Promoting the LAST entry of a full list drops nothing.
    CHECK(netlist_remember(n, c, "b", ""));
    {
        const char* want[] = {"b", "e", "d", "c"};
        CHECK(netlist_is(n, c, want, 4));
    }
    CHECK_STR(n[0].pass, "b");

    // find
    CHECK(netlist_find(n, c, "b") == 0);
    CHECK(netlist_find(n, c, "c") == 3);
    CHECK(netlist_find(n, c, "zzz") == -1);
    CHECK(netlist_find(n, c, nullptr) == -1);
    CHECK(netlist_find(n, 2, "d") == -1);          // beyond count is not found

    // Forget closes the gap, keeps the order, and wipes the freed slot.
    CHECK(netlist_forget(n, c, "e"));
    {
        const char* want[] = {"b", "d", "c"};
        CHECK(netlist_is(n, c, want, 3));
    }
    CHECK(n[3].ssid[0] == '\0' && n[3].pass[0] == '\0');
    CHECK(!netlist_forget(n, c, "e"));
    CHECK(!netlist_forget(n, c, "nope"));
    CHECK(!netlist_forget(n, c, nullptr));
    CHECK(c == 3);
    CHECK(netlist_forget(n, c, "c"));                 // the last entry
    CHECK(netlist_forget(n, c, "b"));                 // the first entry
    CHECK(c == 1 && std::strcmp(n[0].ssid, "d") == 0);
    CHECK(netlist_forget(n, c, "d"));
    CHECK(c == 0 && n[0].ssid[0] == '\0');
    CHECK(!netlist_forget(n, c, "d"));

    // Refused input leaves the list untouched.
    CHECK(!netlist_remember(n, c, "", "x"));
    CHECK(!netlist_remember(n, c, nullptr, "x"));
    CHECK(c == 0);

    // Field limits: a 32 character SSID and a 64 character password are
    // stored whole and terminated.
    {
        char ssid[33], pass[65];
        std::memset(ssid, 'S', 32); ssid[32] = '\0';
        std::memset(pass, 'P', 64); pass[64] = '\0';
        CHECK(netlist_remember(n, c, ssid, pass));
        CHECK(std::strlen(n[0].ssid) == 32 && std::strcmp(n[0].ssid, ssid) == 0);
        CHECK(std::strlen(n[0].pass) == 64 && std::strcmp(n[0].pass, pass) == 0);
        CHECK(netlist_remember(n, c, ssid, ""));        // blank keeps all 64
        CHECK(std::strlen(n[0].pass) == 64);
    }

    // A count corrupted past capacity is clamped, not trusted.
    {
        const char* four[] = {"a", "b", "c", "d"};
        netlist_fill(n, c, four, 4);
        c = 9;
        CHECK(netlist_remember(n, c, "z", ""));
        CHECK(c == 4 && std::strcmp(n[0].ssid, "z") == 0);
        c = 200;
        CHECK(netlist_forget(n, c, "z") && c == 3);
    }
}

#include "../netmon/src/core/name_cache.h"
#include "../netmon/src/core/scan_list.h"

static const char* nz(const char* s) { return s != nullptr ? s : "(none)"; }

// The same REQUEST as build_dhcp_frame makes, as a UDP socket delivers it:
// no 802.11, IPv4 or UDP header in front.
static void test_dhcp_bootp() {
    uint8_t frame[512];
    const size_t n = build_dhcp_frame(frame, sizeof frame, false, "Galaxy-S21");
    const size_t off = 24 + 8 + 20 + 8;
    DhcpInfo info{};
    CHECK(dhcp_from_bootp(frame + off, n - off, info));
    CHECK(info.has_hostname);
    CHECK_STR(info.hostname, "Galaxy-S21");
    CHECK(info.msg_type == 3);
    CHECK(info.requested_ip == ipv4_from_octets(192, 168, 2, 51));
    char mac[18];
    mac_format(info.client, mac);
    CHECK_STR(mac, "B8:27:EB:12:34:56");

    // Both entry points read the same message the same way.
    DhcpInfo air{};
    CHECK(dhcp_from_ieee80211(frame, n, air));
    CHECK_STR(air.hostname, info.hostname);
    CHECK(air.msg_type == info.msg_type && air.requested_ip == info.requested_ip);
    CHECK(mac_equal(air.client, info.client));

    // Shorter than the fixed header, no buffer, or the wrong cookie: refused.
    DhcpInfo x{};
    CHECK(!dhcp_from_bootp(frame + off, 239, x));
    CHECK(!dhcp_from_bootp(nullptr, 400, x));
    uint8_t bad[300];
    std::memcpy(bad, frame + off, sizeof bad);
    bad[236] = 0;
    CHECK(!dhcp_from_bootp(bad, sizeof bad, x));

    // A client rebinding broadcasts a REQUEST that names its address in
    // ciaddr and carries no option 50.
    uint8_t rb[300];
    std::memset(rb, 0, sizeof rb);
    rb[0] = 1; rb[1] = 1; rb[2] = 6;
    rb[12] = 192; rb[13] = 168; rb[14] = 2; rb[15] = 77;
    const uint8_t chaddr[6] = {0x3C, 0x22, 0xFB, 0x10, 0x20, 0x30};
    std::memcpy(rb + 28, chaddr, 6);
    const uint8_t cookie[4] = {0x63, 0x82, 0x53, 0x63};
    std::memcpy(rb + 236, cookie, 4);
    size_t o = 240;
    rb[o++] = 53; rb[o++] = 1; rb[o++] = 3;
    rb[o++] = 12; rb[o++] = 6;
    std::memcpy(rb + o, "laptop", 6);
    o += 6;
    rb[o++] = 255;
    DhcpInfo r{};
    CHECK(dhcp_from_bootp(rb, o, r));
    CHECK(r.requested_ip == ipv4_from_octets(192, 168, 2, 77));
    CHECK_STR(r.hostname, "laptop");
    // Option 50, when present, wins over ciaddr.
    rb[o - 1] = 50; rb[o++] = 4;
    rb[o++] = 192; rb[o++] = 168; rb[o++] = 2; rb[o++] = 88;
    rb[o++] = 255;
    CHECK(dhcp_from_bootp(rb, o, r));
    CHECK(r.requested_ip == ipv4_from_octets(192, 168, 2, 88));
}

static void test_name_clean() {
    char out[kNameMax];
    CHECK(name_clean("raspberrypi", out));
    CHECK_STR(out, "raspberrypi");
    CHECK(name_clean("  Ahmed's iPhone  ", out));
    CHECK_STR(out, "Ahmed's iPhone");
    CHECK(name_clean("lap\ttop\r\n", out));              // control bytes dropped
    CHECK_STR(out, "laptop");
    CHECK(!name_clean("", out));
    CHECK(!name_clean("   ", out));
    CHECK(!name_clean("\r\n\t", out));
    CHECK(!name_clean(nullptr, out));
    char big[200];
    std::memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    CHECK(name_clean(big, out));
    CHECK(std::strlen(out) == kNameMax - 1);

    // UTF-8 survives whole: an Arabic name, as your storeroom network is called.
    const char* ar = "\xD8\xA7\xD9\x84\xD9\x85\xD8\xAE\xD8\xB2\xD9\x86";
    CHECK(name_clean(ar, out));
    CHECK_STR(out, ar);
    // A length cut through a character drops the half, never keeps it.
    char cut[80];
    std::memset(cut, 'a', 62);
    cut[62] = '\xD8';
    cut[63] = '\xA7';
    cut[64] = '\0';
    CHECK(name_clean(cut, out));
    CHECK(std::strlen(out) == 62);
    CHECK(name_utf8_tail("ab\xE2\x80", 4) == 2);        // em dash, two of three bytes
    CHECK(name_utf8_tail("ab\xE2\x80\x94", 5) == 5);    // em dash complete
    CHECK(name_utf8_tail("abc", 3) == 3);
    CHECK(name_utf8_tail("a\x80", 2) == 1);             // stray continuation byte
    CHECK(name_utf8_tail("\x80\x80", 2) == 0);
}

static void test_name_cache() {
    NameCache<3> c;
    Mac a{}, b{}, d{}, e{};
    mac_parse("B8:27:EB:12:34:56", a);
    mac_parse("50:91:E3:AB:CD:EF", b);
    mac_parse("D4:E9:F4:12:34:56", d);
    mac_parse("3C:22:FB:10:20:30", e);

    CHECK(c.get(a) == nullptr);
    CHECK(c.put(a, "raspberrypi"));          // new: the file needs writing
    CHECK(!c.put(a, "raspberrypi"));         // unchanged: no write
    CHECK(!c.put(a, "  raspberrypi "));      // the same name once cleaned
    CHECK(c.put(a, "pi-hole"));              // renamed: write
    CHECK_STR(nz(c.get(a)), "pi-hole");
    CHECK(!c.put(b, ""));                    // nothing to keep
    CHECK(!c.put(b, "\r\n"));
    CHECK(c.get(b) == nullptr);
    CHECK(c.size() == 1);
    CHECK(c.put(b, "tplink"));
    CHECK(c.put(d, "netmon"));
    CHECK(c.size() == 3 && c.capacity() == 3);

    // Full: the least recently used entry makes room. Reading a refreshes it.
    CHECK_STR(nz(c.get(a)), "pi-hole");
    CHECK(c.put(e, "laptop"));
    CHECK(c.size() == 3);
    CHECK(c.get(b) == nullptr);              // b was the stalest
    CHECK_STR(nz(c.get(d)), "netmon");
    CHECK_STR(nz(c.get(e)), "laptop");
    CHECK_STR(nz(c.get(a)), "pi-hole");

    // The file format, one line per device.
    char line[96];
    CHECK(name_line_format(a, "pi-hole", line, sizeof line) == 12 + 1 + 7 + 1);
    CHECK_STR(line, "B827EB123456\tpi-hole\n");
    Mac back{};
    char name[kNameMax];
    CHECK(name_line_parse(line, back, name));
    CHECK(mac_equal(back, a));
    CHECK_STR(name, "pi-hole");
    CHECK(name_line_parse("B827EB123456\tpi-hole\r\n", back, name));   // CRLF
    CHECK_STR(name, "pi-hole");
    CHECK(name_line_parse("B827EB123456\tpi-hole", back, name));       // last line
    // Damage is refused line by line, and never half-applied.
    Mac keep = back;
    CHECK(!name_line_parse("B827EB02B02\tshort", back, name));         // 11 digits
    CHECK(!name_line_parse("B827EB02B0ZZ\tbadhex", back, name));
    CHECK(!name_line_parse("B827EB123456 notab", back, name));
    CHECK(!name_line_parse("B827EB123456\t\n", back, name));           // empty name
    CHECK(!name_line_parse("", back, name));
    CHECK(!name_line_parse(nullptr, back, name));
    CHECK(mac_equal(back, keep));
    CHECK(name_line_format(a, "x", line, 15) == 0);                    // needs 16
    CHECK(name_line_format(a, "x", line, 16) == 15);
    CHECK(name_line_format(a, "", line, sizeof line) == 0);
    const char* ar = "\xD8\xA7\xD9\x84\xD9\x85\xD8\xAE\xD8\xB2\xD9\x86";
    CHECK(name_line_format(e, ar, line, sizeof line) > 0);
    CHECK(name_line_parse(line, back, name));
    CHECK_STR(name, ar);
    CHECK(mac_equal(back, e));
}

static void test_scan_list() {
    // A real-world scan, strongest first: a hidden network, and the same
    // name heard from a second, far weaker access point.
    ScanEntry l[4];
    size_t n = 0;
    n = scan_add(l, n, 4, "HomeNet 2.4GHz", -21);
    n = scan_add(l, n, 4, "", -23);
    n = scan_add(l, n, 4, "Neighbour-1", -61);
    n = scan_add(l, n, 4, "Neighbour-2", -66);
    n = scan_add(l, n, 4, "HomeNet 2.4GHz", -73);
    CHECK(n == 3);
    CHECK_STR(l[0].ssid, "HomeNet 2.4GHz");
    CHECK(l[0].rssi == -21);
    CHECK_STR(l[2].ssid, "Neighbour-2");
    n = scan_add(l, n, 4, "Cafe", -80);
    CHECK(n == 4);
    // Full: a weaker newcomer is dropped...
    n = scan_add(l, n, 4, "Corner-Shop", -81);
    CHECK(n == 4);
    CHECK_STR(l[3].ssid, "Cafe");
    // ...a stronger one pushes out the weakest and lands in order.
    n = scan_add(l, n, 4, "Closer", -40);
    CHECK(n == 4);
    CHECK_STR(l[1].ssid, "Closer");
    CHECK_STR(l[3].ssid, "Neighbour-2");
    // A stronger sighting of a name already listed moves it up.
    n = scan_add(l, n, 4, "Neighbour-2", -30);
    CHECK(n == 4);
    CHECK_STR(l[1].ssid, "Neighbour-2");
    CHECK(l[1].rssi == -30);
    for (size_t i = 1; i < n; ++i) CHECK(l[i - 1].rssi >= l[i].rssi);
    // The longest name Wi-Fi allows is kept whole.
    ScanEntry m[2];
    size_t k = 0;
    char longest[33];
    std::memset(longest, 'N', 32);
    longest[32] = '\0';
    k = scan_add(m, k, 2, longest, -50);
    CHECK(k == 1);
    CHECK(std::strlen(m[0].ssid) == 32);
    CHECK(scan_add(m, k, 2, nullptr, -10) == 1);
    CHECK(scan_add(nullptr, 0, 2, "x", -10) == 0);
    CHECK(scan_add(m, 0, 0, "x", -10) == 0);
}

#include "../netmon/src/core/origin.h"

// Which POSTs may change settings or restart the board.
static void test_origin() {
    // Not a browser: no Origin at all. The Android app and curl.
    CHECK(origin_allowed(nullptr, "192.168.2.27"));
    CHECK(origin_allowed("", "192.168.2.27"));

    // The board's own pages, however they were reached.
    CHECK(origin_allowed("http://192.168.2.27", "192.168.2.27"));
    CHECK(origin_allowed("http://netmon.local", "netmon.local"));
    CHECK(origin_allowed("HTTP://NetMon.Local", "netmon.local"));
    CHECK(origin_allowed("http://192.168.4.1", "192.168.4.1"));      // setup mode
    CHECK(origin_allowed("http://192.168.2.27:8080", "192.168.2.27:8080"));

    // Anything else is a page on another site.
    CHECK(!origin_allowed("http://evil.example", "192.168.2.27"));
    CHECK(!origin_allowed("null", "192.168.2.27"));                  // sandboxed page
    CHECK(!origin_allowed("https://192.168.2.27", "192.168.2.27"));
    CHECK(!origin_allowed("http://192.168.2.2", "192.168.2.27"));    // a prefix
    CHECK(!origin_allowed("http://192.168.2.27.evil.example", "192.168.2.27"));
    CHECK(!origin_allowed("http://192.168.2.27:8080", "192.168.2.27"));
    CHECK(!origin_allowed("http://", "192.168.2.27"));
    CHECK(!origin_allowed("http:/", "192.168.2.27"));

    // A request with an Origin but no Host cannot be matched to this board.
    CHECK(!origin_allowed("http://192.168.2.27", ""));
    CHECK(!origin_allowed("http://192.168.2.27", nullptr));
}

#include "../netmon/src/core/air_text.h"
#include "../netmon/src/core/air_signal.h"
#include "../netmon/src/core/air_wifi.h"
#include "../netmon/src/core/air_ble.h"
#include "../netmon/src/core/ble_vendor.h"
#include "../netmon/src/core/air_plan.h"

// Names heard over the air are cleaned like hostnames.
static void test_air_text() {
    char out[8];
    CHECK(air_text_clean("Home", 4, out, sizeof(out)));
    CHECK_STR(out, "Home");
    // Controls go, surrounding spaces go, inner spaces stay.
    CHECK(air_text_clean("  a\tb c \x01 ", 11, out, sizeof(out)));
    CHECK_STR(out, "ab c");
    // Bounded by the input length as well as by the NUL...
    CHECK(air_text_clean("abcdef", 3, out, sizeof(out)));
    CHECK_STR(out, "abc");
    // ...and by the output, without splitting a character: "é" is 2 bytes.
    char tiny[4];
    CHECK(air_text_clean("ab\xC3\xA9", 4, tiny, sizeof(tiny)));
    CHECK_STR(tiny, "ab");
    // A cut through a character in the input itself is dropped too.
    CHECK(air_text_clean("x\xE2\x82", 3, out, sizeof(out)));
    CHECK_STR(out, "x");
    // Nothing printable: false and empty.
    CHECK(!air_text_clean("\x01\x02  ", 4, out, sizeof(out)));
    CHECK_STR(out, "");
    CHECK(!air_text_clean(nullptr, 4, out, sizeof(out)));
    CHECK_STR(out, "");
    CHECK(!air_text_clean("abc", 3, nullptr, 4));
    CHECK(!air_text_clean("abc", 3, out, 0));
    // An SSID is 32 bytes with no terminator of its own.
    char ssid[33];
    char raw[32];
    std::memset(raw, 'S', sizeof(raw));
    CHECK(air_text_clean(raw, sizeof(raw), ssid, sizeof(ssid)));
    CHECK(std::strlen(ssid) == 32);
}

static void test_air_signal() {
    CHECK(air_rssi(-61) == -61);
    CHECK(air_rssi(5) == 0);
    CHECK(air_rssi(-200) == -127);
    // Recent: averaged with the previous reading.
    CHECK(air_smooth_rssi(-60, 100, -70, 110) == -65);
    CHECK(air_smooth_rssi(-60, 100, -70, 160) == -65);      // 60 s is still recent
    // Stale, or a clock that stepped back: the new reading alone.
    CHECK(air_smooth_rssi(-60, 100, -70, 161) == -70);
    CHECK(air_smooth_rssi(-60, 100, -70, 99) == -70);
    CHECK(air_smooth_rssi(-60, 100, -300, 101) == (-60 + -127) / 2);
}

static void test_air_wifi_text() {
    CHECK_STR(air_auth_text(0), "Open");
    CHECK_STR(air_auth_text(3), "WPA2");
    CHECK_STR(air_auth_text(4), "WPA/WPA2");
    CHECK_STR(air_auth_text(5), "WPA2-Enterprise");
    CHECK_STR(air_auth_text(6), "WPA3");
    CHECK_STR(air_auth_text(7), "WPA2/WPA3");
    CHECK_STR(air_auth_text(11), "WPA3");
    CHECK_STR(air_auth_text(16), "WPA-Enterprise");
    CHECK_STR(air_auth_text(17), "Unknown");
    CHECK_STR(air_auth_text(255), "Unknown");
    CHECK(air_channel_mhz(1) == 2412);
    CHECK(air_channel_mhz(6) == 2437);
    CHECK(air_channel_mhz(13) == 2472);
    CHECK(air_channel_mhz(14) == 2484);
    CHECK(air_channel_mhz(0) == 0);
    CHECK(air_channel_mhz(36) == 0);
}

static Mac test_mac_n(uint8_t n) {
    Mac m{};
    m.b[0] = 0x10;
    m.b[5] = n;
    return m;
}

static void test_air_wifi_table() {
    WifiAirTable<4> t;
    CHECK(t.size() == 0);
    CHECK(t.scans() == 0);
    const Mac a = test_mac_n(1), b = test_mac_n(2), c = test_mac_n(3);

    // First scan hears two networks.
    CHECK(t.heard(a, "Home", -50, 6, 3, 100));
    CHECK(t.heard(b, "", -80, 11, 4, 100));
    // Nothing is "heard last" until a scan has closed.
    CHECK(!t.heard_last(t.at(0)));
    t.scan_end(102);
    CHECK(t.scans() == 1);
    CHECK(t.last_scan_s() == 102);
    CHECK(t.size() == 2);
    CHECK(t.live_count() == 2);
    CHECK(t.heard_last(t.at(0)));
    CHECK_STR(t.at(0).ssid, "Home");
    CHECK(t.at(0).channel == 6);
    CHECK(t.at(0).auth == 3);
    CHECK(t.at(0).first_seen_s == 100);
    CHECK_STR(t.at(1).ssid, "");                 // hidden

    // Second scan: the hidden one gives its name, Home is missed once.
    CHECK(t.heard(b, "Attic", -70, 11, 4, 130));
    t.scan_end(131);
    CHECK_STR(t.at(1).ssid, "Attic");
    CHECK(t.at(1).rssi == -75);                  // averaged: -80 and -70
    CHECK(t.at(0).misses == 1);
    CHECK(WifiAirTable<4>::live(t.at(0)));       // one miss is not absence
    CHECK(!t.heard_last(t.at(0)));
    CHECK(t.live_count() == 2);

    // Third scan: the name is not blanked by a nameless answer; Home is now
    // missed twice in a row and moves to the history.
    CHECK(t.heard(b, "", -71, 11, 4, 160));
    t.scan_end(161);
    CHECK_STR(t.at(1).ssid, "Attic");
    CHECK(t.at(0).misses == 2);
    CHECK(!WifiAirTable<4>::live(t.at(0)));
    CHECK(t.live_count() == 1);

    // Home is back: live again at once, its first sighting unchanged.
    CHECK(t.heard(a, "Home", -52, 6, 3, 400));
    t.scan_end(401);
    CHECK(t.at(0).misses == 0);
    CHECK(t.at(0).first_seen_s == 100);
    CHECK(t.at(0).last_seen_s == 400);
    CHECK(t.at(0).rssi == -52);                  // long gap: not averaged
    CHECK(t.at(1).misses == 1);

    // A scan that fails is never closed, so it ages nothing.
    CHECK(t.heard(c, "Next door", -85, 1, 7, 500));
    CHECK(t.at(1).misses == 1);
    CHECK(t.size() == 3);
}

static void test_air_wifi_room() {
    WifiAirTable<3> t;
    const Mac a = test_mac_n(1), b = test_mac_n(2), c = test_mac_n(3),
              d = test_mac_n(4), e = test_mac_n(5);
    t.heard(a, "A", -40, 1, 3, 10);
    t.heard(b, "B", -60, 1, 3, 10);
    t.heard(c, "C", -80, 1, 3, 10);
    t.scan_end(11);
    // Full, nothing in the history: a weaker newcomer is turned away...
    CHECK(!t.heard(d, "D", -90, 1, 3, 20));
    CHECK(t.size() == 3);
    // ...a stronger one takes the weakest slot.
    CHECK(t.heard(d, "D", -50, 1, 3, 20));
    bool has_c = false, has_d = false;
    for (size_t i = 0; i < t.size(); ++i) {
        if (mac_equal(t.at(i).bssid, c)) has_c = true;
        if (mac_equal(t.at(i).bssid, d)) has_d = true;
    }
    CHECK(!has_c);
    CHECK(has_d);
    t.scan_end(21);

    // Two scans that hear only A and D put B in the history...
    t.heard(a, "A", -40, 1, 3, 30);
    t.heard(d, "D", -50, 1, 3, 30);
    t.scan_end(31);
    t.heard(a, "A", -40, 1, 3, 40);
    t.heard(d, "D", -50, 1, 3, 40);
    t.scan_end(41);
    // ...and history gives way first, even to a newcomer weaker than it.
    CHECK(t.heard(e, "E", -95, 1, 3, 50));
    bool has_b = false;
    for (size_t i = 0; i < t.size(); ++i) {
        if (mac_equal(t.at(i).bssid, b)) has_b = true;
    }
    CHECK(!has_b);
    CHECK(t.size() == 3);
}

// A sighting carrying `name` in a complete-name field, and when `company`
// is not 0xFFFF, manufacturer data with that company and `mfr` after it.
static BleSighting test_sighting(uint8_t n, int rssi, const char* name,
                                 BleAddrKind kind = BleAddrKind::Private,
                                 uint16_t company = 0xFFFF,
                                 const uint8_t* mfr = nullptr, size_t mfr_len = 0) {
    BleSighting s{};
    s.addr = test_mac_n(n);
    s.kind = kind;
    s.rssi = static_cast<int8_t>(rssi);
    size_t at = 0;
    if (company != 0xFFFF) {
        s.payload[at++] = static_cast<uint8_t>(3 + mfr_len);
        s.payload[at++] = 0xFF;
        s.payload[at++] = static_cast<uint8_t>(company & 0xFF);
        s.payload[at++] = static_cast<uint8_t>(company >> 8);
        for (size_t i = 0; i < mfr_len; ++i) s.payload[at++] = mfr[i];
    }
    const size_t nl = name != nullptr ? std::strlen(name) : 0;
    if (nl > 0) {
        s.payload[at++] = static_cast<uint8_t>(nl + 1);
        s.payload[at++] = 0x09;
        std::memcpy(s.payload + at, name, nl);
        at += nl;
    }
    s.len = static_cast<uint8_t>(at);
    return s;
}

static void test_air_ble_table() {
    BleAirTable<3> t;
    // Apple, with a Nearby Info message: who made it, but not what it is.
    const uint8_t nearby[] = {0x10, 0x05, 0x01, 0x18, 0x00, 0x00, 0x00};
    t.sight(test_sighting(1, -70, "", BleAddrKind::Private, 0x004C, nearby, sizeof(nearby)), 100);
    CHECK(t.size() == 1);
    CHECK(t.at(0).has_company);
    CHECK(t.at(0).company == 0x004C);
    CHECK(t.at(0).type == BleType::Unknown);
    CHECK_STR(t.at(0).name, "");
    CHECK(t.at(0).sightings == 1);
    CHECK(t.at(0).first_seen_s == 100);

    // The scan response brings the name; the company is not forgotten by a
    // packet that does not carry it, nor the name by one without a name.
    t.sight(test_sighting(1, -60, " Pixel\x01 Buds "), 105);
    CHECK_STR(t.at(0).name, "Pixel Buds");
    CHECK(t.at(0).company == 0x004C);
    CHECK(t.at(0).rssi == -65);
    t.sight(test_sighting(1, -64, ""), 110);
    CHECK_STR(t.at(0).name, "Pixel Buds");
    CHECK(t.at(0).sightings == 3);
    CHECK(t.at(0).first_seen_s == 100);
    CHECK(t.at(0).last_seen_s == 110);

    // Full: the device heard longest ago makes room.
    t.sight(test_sighting(2, -50, "B", BleAddrKind::Public), 120);
    t.sight(test_sighting(3, -50, "C", BleAddrKind::Public), 130);
    t.sight(test_sighting(4, -50, "D", BleAddrKind::Public), 140);
    CHECK(t.size() == 3);
    bool has1 = false;
    for (size_t i = 0; i < t.size(); ++i) {
        if (mac_equal(t.at(i).addr, test_mac_n(1))) has1 = true;
    }
    CHECK(!has1);

    // Expiry keeps the rest in order. D took the evicted device's slot at the
    // front; B was heard at 120, 80 s before, and goes.
    CHECK(mac_equal(t.at(0).addr, test_mac_n(4)));
    CHECK(t.expire(200, 75) == 1);
    CHECK(t.size() == 2);
    CHECK(mac_equal(t.at(0).addr, test_mac_n(4)));
    CHECK(mac_equal(t.at(1).addr, test_mac_n(3)));
    CHECK(t.expire(200, 1000) == 0);
    CHECK(t.expire(100, 0) == 0);                // a clock behind them expires nothing
    CHECK(t.expire(5000, 10) == 2);
    CHECK(t.size() == 0);
}

static void test_air_ble_eviction_ties() {
    BleAirTable<2> t;
    // Heard in the same second: the private address goes before the public.
    t.sight(test_sighting(1, -80, "", BleAddrKind::Public), 50);
    t.sight(test_sighting(2, -40, "", BleAddrKind::Private), 50);
    t.sight(test_sighting(3, -40, "", BleAddrKind::Public), 60);
    bool has1 = false, has2 = false;
    for (size_t i = 0; i < t.size(); ++i) {
        if (mac_equal(t.at(i).addr, test_mac_n(1))) has1 = true;
        if (mac_equal(t.at(i).addr, test_mac_n(2))) has2 = true;
    }
    CHECK(has1);
    CHECK(!has2);
    // Same second, same kind: the weaker goes.
    BleAirTable<2> u;
    u.sight(test_sighting(1, -80, "", BleAddrKind::Public), 50);
    u.sight(test_sighting(2, -40, "", BleAddrKind::Public), 50);
    u.sight(test_sighting(3, -40, "", BleAddrKind::Public), 60);
    bool k1 = false;
    for (size_t i = 0; i < u.size(); ++i) {
        if (mac_equal(u.at(i).addr, test_mac_n(1))) k1 = true;
    }
    CHECK(!k1);
    CHECK_STR(ble_kind_text(BleAddrKind::Public), "public");
    CHECK_STR(ble_kind_text(BleAddrKind::Static), "static");
    CHECK_STR(ble_kind_text(BleAddrKind::Private), "private");
}

static void test_ble_vendor_table_is_sorted() {
    for (size_t i = 1; i < BLE_VENDOR_COUNT; ++i) {
        CHECK(BLE_VENDORS[i - 1].id < BLE_VENDORS[i].id);
    }
    // Every row can be found by the search that relies on the order.
    for (size_t i = 0; i < BLE_VENDOR_COUNT; ++i) {
        CHECK(ble_vendor(BLE_VENDORS[i].id) == BLE_VENDORS[i].name);
    }
    CHECK_STR(ble_vendor(0x004C), "Apple");
    CHECK_STR(ble_vendor(0x0006), "Microsoft");
    CHECK_STR(ble_vendor(0x0075), "Samsung");
    CHECK_STR(ble_vendor(0x02E5), "Espressif");
    CHECK(ble_vendor(0x0000) == nullptr);
    CHECK(ble_vendor(0xFFFF) == nullptr);
    CHECK(ble_vendor(0x004D) == nullptr);
    CHECK(ble_vendor(0x004C, nullptr, 3) == nullptr);
}

static AirInputs test_air_inputs() {
    AirInputs in{};
    in.now_ms = 100000;
    in.on_lan = true;
    in.background_s = 120;
    in.wifi.enabled = true;
    in.ble.enabled = true;
    return in;
}

static void test_air_plan() {
    CHECK(!air_watching(5000, 0));
    CHECK(air_watching(5000, 1));
    CHECK(air_watching(20000, 1));
    CHECK(!air_watching(20001, 1));
    // millis() wrapping between the read and now is still 20 s apart.
    CHECK(air_watching(5000u, 0xFFFFF000u));

    CHECK(air_background_valid(0));
    CHECK(air_background_valid(30));
    CHECK(air_background_valid(3600));
    CHECK(!air_background_valid(29));
    CHECK(!air_background_valid(3601));

    CHECK(air_interval_ms(true, false, 15000, 120) == 15000);   // watched, even in setup
    CHECK(air_interval_ms(false, true, 15000, 120) == 120000);
    CHECK(air_interval_ms(false, true, 15000, 0) == 0);          // background off
    CHECK(air_interval_ms(false, false, 15000, 120) == 0);       // setup mode, unwatched

    // Nothing has run yet: Wi-Fi goes first, then Bluetooth.
    AirInputs in = test_air_inputs();
    CHECK(air_next(in) == AirJob::Wifi);
    in.wifi.ever = true;
    in.wifi.last_ms = in.now_ms;
    CHECK(air_next(in) == AirJob::Ble);
    in.ble.ever = true;
    in.ble.last_ms = in.now_ms;
    CHECK(air_next(in) == AirJob::None);

    // The radio in use, or netmon's own work due: nothing starts.
    in.wifi.requested = true;
    in.radio_busy = true;
    CHECK(air_next(in) == AirJob::None);
    in.radio_busy = false;
    in.netmon_due = true;
    CHECK(air_next(in) == AirJob::None);
    in.netmon_due = false;
    // A request skips the timer.
    CHECK(air_next(in) == AirJob::Wifi);
    in.wifi.requested = false;

    // Unwatched: the background interval.
    in.now_ms += 119999;
    CHECK(air_next(in) == AirJob::None);
    in.now_ms += 1;
    // Both due at the same moment, waited the same: Wi-Fi.
    CHECK(air_next(in) == AirJob::Wifi);
    // Bluetooth has waited longer: Bluetooth.
    in.ble.last_ms -= 1000;
    CHECK(air_next(in) == AirJob::Ble);

    // Watched: the quick cadence, Bluetooth every 8 s and Wi-Fi every 15 s.
    AirInputs w = test_air_inputs();
    w.wifi.ever = w.ble.ever = true;
    w.wifi.last_ms = w.ble.last_ms = w.now_ms;
    w.last_watch_ms = w.now_ms;
    w.now_ms += kAirLiveBleMs;
    CHECK(air_next(w) == AirJob::Ble);
    w.ble.last_ms = w.now_ms;
    w.now_ms += kAirLiveWifiMs - kAirLiveBleMs;
    CHECK(air_next(w) == AirJob::Wifi);
    // The watch lapses 20 s after the last read; back to the background.
    w.now_ms = w.last_watch_ms + kAirWatchMs;
    w.wifi.last_ms = w.ble.last_ms = w.now_ms - 1;
    CHECK(air_next(w) == AirJob::None);

    // Switched off: never, not even on request.
    AirInputs off = test_air_inputs();
    off.wifi.enabled = false;
    off.ble.enabled = false;
    off.wifi.requested = off.ble.requested = true;
    CHECK(air_next(off) == AirJob::None);

    // Setup mode: only while watched, or on request.
    AirInputs setup = test_air_inputs();
    setup.on_lan = false;
    CHECK(air_next(setup) == AirJob::None);
    setup.ble.requested = true;
    CHECK(air_next(setup) == AirJob::Ble);
    setup.ble.requested = false;
    setup.last_watch_ms = setup.now_ms;
    CHECK(air_next(setup) == AirJob::Wifi);

    // Background off: only while watched, or on request.
    AirInputs quiet = test_air_inputs();
    quiet.background_s = 0;
    CHECK(air_next(quiet) == AirJob::None);
    quiet.wifi.requested = true;
    CHECK(air_next(quiet) == AirJob::Wifi);

    // A timer across the millis() wrap.
    AirInputs wrap = test_air_inputs();
    wrap.wifi.ever = wrap.ble.ever = true;
    wrap.wifi.last_ms = 0xFFFFFF00u;
    wrap.ble.last_ms = 0xFFFFFF00u;
    wrap.now_ms = 1000;                  // 1256 ms later
    CHECK(air_next(wrap) == AirJob::None);
    wrap.now_ms = 0xFFFFFF00u + 120000u; // wrapped, exactly one interval
    CHECK(air_next(wrap) == AirJob::Wifi);
}

// Advertisements taken apart: fields, padding, a scan response after it.
static void test_ble_adv_parse() {
    BleAdvInfo a;
    CHECK(!ble_adv_parse(nullptr, 5, a));
    const uint8_t empty[] = {0};
    CHECK(!ble_adv_parse(empty, 0, a));

    // Flags; 16-bit UUIDs 0x180D and 0xFEED; a shortened name; tx power;
    // zero padding; then the scan response: the complete name, appearance,
    // service data for 0xFE2C, manufacturer data for Apple.
    const uint8_t p[] = {
        0x02, 0x01, 0x06,
        0x05, 0x03, 0x0D, 0x18, 0xED, 0xFE,
        0x04, 0x08, 'B', 'u', 'd',
        0x02, 0x0A, 0xF6,
        0x00, 0x00,
        0x0A, 0x09, 'B', 'u', 'd', 's', ' ', 'P', 'r', 'o', '2',
        0x03, 0x19, 0xC1, 0x03,
        0x06, 0x16, 0x2C, 0xFE, 0x01, 0x02, 0x03,
        0x05, 0xFF, 0x4C, 0x00, 0x12, 0x00,
    };
    CHECK(ble_adv_parse(p, sizeof(p), a));
    CHECK(a.uuid_count == 3);
    CHECK(a.uuid16[0] == 0x180D);
    CHECK(a.uuid16[1] == 0xFEED);
    CHECK(a.uuid16[2] == 0xFE2C);
    CHECK(a.name_full);
    CHECK(a.name_len == 9);
    CHECK(std::memcmp(a.name, "Buds Pro2", 9) == 0);
    CHECK(a.has_tx_power && a.tx_power == -10);
    CHECK(a.has_appearance && a.appearance == 0x03C1);
    CHECK(a.has_company && a.company == 0x004C);
    CHECK(a.mfr_len == 2 && a.mfr[0] == 0x12);

    // A field running past the end stops the reading; what came before stays.
    const uint8_t cut[] = {0x03, 0x09, 'H', 'i', 0x09, 0xFF, 0x06, 0x00};
    CHECK(ble_adv_parse(cut, sizeof(cut), a));
    CHECK(a.name_len == 2);
    CHECK(!a.has_company);
    // The first manufacturer field is the one kept; repeated UUIDs once.
    const uint8_t two[] = {0x03, 0xFF, 0x06, 0x00, 0x03, 0xFF, 0x4C, 0x00,
                           0x05, 0x02, 0x0D, 0x18, 0x0D, 0x18};
    CHECK(ble_adv_parse(two, sizeof(two), a));
    CHECK(a.company == 0x0006);
    CHECK(a.mfr_len == 0);
    CHECK(a.uuid_count == 1);
    // A shortened name after the complete one does not replace it.
    const uint8_t names[] = {0x03, 0x09, 'A', 'B', 0x02, 0x08, 'A'};
    CHECK(ble_adv_parse(names, sizeof(names), a));
    CHECK(a.name_len == 2 && a.name_full);
    // More than the legacy maximum is read only up to it.
    uint8_t big[80];
    std::memset(big, 0, sizeof(big));
    big[70] = 0x03;
    big[71] = 0x09;
    big[72] = 'Z';
    big[73] = 'Z';
    CHECK(!ble_adv_parse(big, sizeof(big), a));
}

static BleGuess test_guess(const uint8_t* p, size_t n) {
    BleAdvInfo a;
    ble_adv_parse(p, n, a);
    return ble_classify(a);
}

static void test_ble_classify() {
    // Apple Find My: an AirTag away from its owner.
    // The Find My message is cut short here; its type alone settles it.
    const uint8_t findmy[] = {0x07, 0xFF, 0x4C, 0x00, 0x12, 0x19, 0x10, 0x00};
    BleGuess g = test_guess(findmy, sizeof(findmy));
    CHECK(g.type == BleType::Tracker);
    CHECK(g.strength == 4);
    CHECK_STR(g.model, "Find My tracker");
    // Handoff first, then Find My: the whole chain is walked.
    const uint8_t chain[] = {0x0B, 0xFF, 0x4C, 0x00, 0x0C, 0x02, 0xAA, 0xBB, 0x12, 0x02, 0x00, 0x00};
    CHECK(test_guess(chain, sizeof(chain)).type == BleType::Tracker);
    // A zero length ends the chain.
    const uint8_t stop[] = {0x09, 0xFF, 0x4C, 0x00, 0x0C, 0x00, 0x12, 0x02, 0x00, 0x00};
    CHECK(test_guess(stop, sizeof(stop)).type == BleType::Unknown);
    // Proximity Pairing: AirPods Pro, a model not in the list, a new AirTag.
    const uint8_t pods[] = {0x0A, 0xFF, 0x4C, 0x00, 0x07, 0x19, 0x01, 0x0E, 0x20, 0x55, 0x00};
    g = test_guess(pods, sizeof(pods));
    CHECK(g.type == BleType::Audio);
    CHECK_STR(g.model, "AirPods Pro");
    const uint8_t pods2[] = {0x09, 0xFF, 0x4C, 0x00, 0x07, 0x19, 0x01, 0x14, 0x20, 0x55};
    CHECK_STR(test_guess(pods2, sizeof(pods2)).model, "AirPods or Beats");
    const uint8_t newtag[] = {0x07, 0xFF, 0x4C, 0x00, 0x07, 0x19, 0x05, 0x00};
    g = test_guess(newtag, sizeof(newtag));
    CHECK(g.type == BleType::Tracker);
    CHECK_STR(g.model, "AirTag (not set up)");
    // Nearby Info alone: Apple, kind unknown, and no weak guess from Apple.
    const uint8_t nearby[] = {0x07, 0xFF, 0x4C, 0x00, 0x10, 0x02, 0x01, 0x18};
    g = test_guess(nearby, sizeof(nearby));
    CHECK(g.type == BleType::Unknown);
    CHECK(g.strength == 0);
    // iBeacon, and Tesla's.
    uint8_t beacon[4 + 2 + 21] = {0x1A, 0xFF, 0x4C, 0x00, 0x02, 0x15};
    g = test_guess(beacon, sizeof(beacon));
    CHECK(g.type == BleType::Beacon);
    std::memcpy(beacon + 6, kTeslaBeaconUuid, 16);
    g = test_guess(beacon, sizeof(beacon));
    CHECK(g.type == BleType::Vehicle);
    CHECK_STR(g.model, "Tesla key");
    // HomeKit, AirPrint.
    const uint8_t homekit[] = {0x06, 0xFF, 0x4C, 0x00, 0x06, 0x01, 0x00};
    CHECK(test_guess(homekit, sizeof(homekit)).type == BleType::SmartHome);
    const uint8_t airprint[] = {0x06, 0xFF, 0x4C, 0x00, 0x03, 0x01, 0x00};
    CHECK(test_guess(airprint, sizeof(airprint)).type == BleType::Printer);

    // Microsoft Swift Pair: a Windows laptop, an Android phone, and a
    // scenario this does not know.
    const uint8_t laptop[] = {0x06, 0xFF, 0x06, 0x00, 0x01, 0x0F, 0x20};
    g = test_guess(laptop, sizeof(laptop));
    CHECK(g.type == BleType::Computer);
    CHECK_STR(g.model, "Windows laptop");
    const uint8_t droid[] = {0x06, 0xFF, 0x06, 0x00, 0x01, 0x28, 0x20};
    CHECK(test_guess(droid, sizeof(droid)).type == BleType::Phone);
    const uint8_t ms_other[] = {0x06, 0xFF, 0x06, 0x00, 0x03, 0x0F, 0x20};
    CHECK(test_guess(ms_other, sizeof(ms_other)).type == BleType::Unknown);

    // Makers of one kind of thing.
    const uint8_t flipper[] = {0x04, 0xFF, 0x29, 0x0E, 0x00};
    g = test_guess(flipper, sizeof(flipper));
    CHECK(g.type == BleType::Flipper);
    CHECK_STR(g.model, "Flipper Zero");
    const uint8_t yale[] = {0x04, 0xFF, 0xDE, 0x0B, 0x00};
    CHECK(test_guess(yale, sizeof(yale)).type == BleType::Lock);
    const uint8_t meta[] = {0x04, 0xFF, 0x8E, 0x05, 0x00};
    CHECK(test_guess(meta, sizeof(meta)).type == BleType::Glasses);

    // Services: Tile, heart rate.
    const uint8_t tile[] = {0x03, 0x03, 0xED, 0xFE};
    g = test_guess(tile, sizeof(tile));
    CHECK(g.type == BleType::Tracker);
    CHECK(g.strength == 3);
    const uint8_t hr[] = {0x03, 0x02, 0x0D, 0x18};
    CHECK(test_guess(hr, sizeof(hr)).type == BleType::Fitness);
    // Fast Pair alone says nothing about the kind of device.
    const uint8_t fp[] = {0x06, 0x16, 0x2C, 0xFE, 0x01, 0x02, 0x03};
    CHECK(test_guess(fp, sizeof(fp)).type == BleType::Unknown);

    // Appearance: a watch (category 3), a keyboard (HID, category 15).
    const uint8_t watch[] = {0x03, 0x19, 0xC0, 0x00};
    g = test_guess(watch, sizeof(watch));
    CHECK(g.type == BleType::Watch);
    CHECK(g.strength == 2);
    const uint8_t kbd[] = {0x03, 0x19, 0xC1, 0x03};
    CHECK(test_guess(kbd, sizeof(kbd)).type == BleType::Input);

    // Names: earbuds before the phone they share a name with, whole words
    // where it matters, and Tesla's key pattern.
    CHECK(ble_name_type("Pixel Buds Pro", 14) == BleType::Audio);
    CHECK(ble_name_type("Galaxy Buds2 (A1B2)", 19) == BleType::Audio);
    CHECK(ble_name_type("Pixel 8", 7) == BleType::Phone);
    CHECK(ble_name_type("iPhone", 6) == BleType::Phone);
    CHECK(ble_name_type("[TV] Samsung Q60", 16) == BleType::Tv);
    CHECK(ble_name_type("Android TV", 10) == BleType::Tv);
    CHECK(ble_name_type("TVS Motor", 9) == BleType::Unknown);
    CHECK(ble_name_type("Tile", 4) == BleType::Tracker);
    CHECK(ble_name_type("Tiles kitchen", 13) == BleType::Unknown);
    CHECK(ble_name_type("Smart Clock", 11) == BleType::Unknown);
    CHECK(ble_name_type("Nuki_1A2B3C", 11) == BleType::Lock);
    CHECK(ble_name_type("Xbox Wireless Controller", 24) == BleType::Input);
    CHECK(ble_name_type("Flipper Uw1n1p", 14) == BleType::Flipper);
    CHECK(ble_name_type("MacBook Pro", 11) == BleType::Computer);
    CHECK(ble_name_type("ESP32", 5) == BleType::Unknown);
    CHECK(ble_name_type(nullptr, 3) == BleType::Unknown);
    CHECK(ble_tesla_key_name("S60845be37d53334eC", 18));
    CHECK(!ble_tesla_key_name("S60845BE37D53334EC", 18));
    CHECK(!ble_tesla_key_name("S60845be37d53334e", 17));
    const uint8_t tesla[] = {0x13, 0x09, 'S', '6', '0', '8', '4', '5', 'b', 'e', '3', '7',
                             'd', '5', '3', '3', '3', '4', 'e', 'C'};
    g = test_guess(tesla, sizeof(tesla));
    CHECK(g.type == BleType::Vehicle);
    CHECK(g.strength == 4);

    // Only the maker: a weak guess, and only for makers of one kind of thing.
    const uint8_t garmin[] = {0x04, 0xFF, 0x87, 0x00, 0x00};
    g = test_guess(garmin, sizeof(garmin));
    CHECK(g.type == BleType::Watch);
    CHECK(g.strength == 1);
    const uint8_t samsung[] = {0x04, 0xFF, 0x75, 0x00, 0x00};
    CHECK(test_guess(samsung, sizeof(samsung)).type == BleType::Unknown);
    // The maker's specific data beats a name that says otherwise.
    const uint8_t mixed[] = {0x07, 0xFF, 0x4C, 0x00, 0x12, 0x19, 0x10, 0x00,
                             0x07, 0x09, 'i', 'P', 'h', 'o', 'n', 'e'};
    CHECK(test_guess(mixed, sizeof(mixed)).type == BleType::Tracker);

    for (int t = 0; t <= static_cast<int>(BleType::Flipper); ++t) {
        CHECK(std::strlen(ble_type_text(static_cast<BleType>(t))) > 0);
    }
    CHECK_STR(ble_type_text(BleType::SmartHome), "smarthome");
    CHECK_STR(ble_type_text(static_cast<BleType>(200)), "unknown");
}

// A device keeps the strongest guess any of its packets gave.
static void test_ble_type_sticks() {
    BleAirTable<4> t;
    const uint8_t garmin[] = {0x00};
    t.sight(test_sighting(1, -60, "Forerunner 255", BleAddrKind::Public, 0x0087, garmin, 1), 10);
    CHECK(t.at(0).type == BleType::Watch);
    CHECK(t.at(0).type_strength == 2);              // from the name
    t.sight(test_sighting(1, -60, "", BleAddrKind::Public, 0x0087, garmin, 1), 20);
    CHECK(t.at(0).type == BleType::Watch);
    CHECK(t.at(0).type_strength == 2);              // a weaker guess does not replace it
    // AirPods: the model arrives with the stronger guess.
    const uint8_t pods[] = {0x07, 0x19, 0x01, 0x0E, 0x20};
    t.sight(test_sighting(2, -50, "", BleAddrKind::Private), 10);
    CHECK(t.at(1).type == BleType::Unknown);
    CHECK(t.at(1).model == nullptr);
    t.sight(test_sighting(2, -50, "", BleAddrKind::Private, 0x004C, pods, sizeof(pods)), 12);
    CHECK(t.at(1).type == BleType::Audio);
    CHECK_STR(t.at(1).model, "AirPods Pro");
    // A later packet with only Nearby Info leaves it as it was.
    const uint8_t nearby[] = {0x10, 0x02, 0x01, 0x18};
    t.sight(test_sighting(2, -50, "", BleAddrKind::Private, 0x004C, nearby, sizeof(nearby)), 14);
    CHECK_STR(t.at(1).model, "AirPods Pro");
}


#include "../netmon/src/core/air_find.h"

// The Finder's readings: numbered, newest kept, collected in pieces.
static void test_air_find_trace() {
    FindTrace<4> t;
    FindReading out[8];
    CHECK(!t.any());
    CHECK(t.size() == 0);
    CHECK(t.since(0, out, 8) == 0);

    t.add(1000, -70);
    t.add(2000, -65);
    CHECK(t.any());
    CHECK(t.last_seq() == 2);
    CHECK(t.size() == 2);
    CHECK(t.newest().rssi == -65);
    size_t n = t.since(0, out, 8);
    CHECK(n == 2);
    CHECK(out[0].seq == 1 && out[0].rssi == -70 && out[0].at_ms == 1000);
    CHECK(out[1].seq == 2 && out[1].rssi == -65);
    // Only what the page has not had yet.
    n = t.since(1, out, 8);
    CHECK(n == 1 && out[0].seq == 2);
    CHECK(t.since(2, out, 8) == 0);
    // A page that was told about later readings than exist gets none, and
    // one from the future is not a crash either.
    CHECK(t.since(99, out, 8) == 0);

    // Past capacity: the oldest drop out, numbers carry on.
    t.add(3000, -60);
    t.add(4000, -55);
    t.add(5000, -50);
    t.add(6000, -45);
    CHECK(t.size() == 4);
    CHECK(t.last_seq() == 6);
    n = t.since(0, out, 8);
    CHECK(n == 4);
    CHECK(out[0].seq == 3 && out[3].seq == 6);
    CHECK(out[0].rssi == -60 && out[3].rssi == -45);
    // A page that fell behind gets what is still there.
    n = t.since(1, out, 8);
    CHECK(n == 4 && out[0].seq == 3);
    // And no more than it asked for: the newest of them.
    n = t.since(0, out, 2);
    CHECK(n == 2 && out[0].seq == 5 && out[1].seq == 6);
    n = t.since(3, out, 2);
    CHECK(n == 2 && out[0].seq == 5 && out[1].seq == 6);

    // Readings kept as heard, clamped to a dBm figure.
    t.add(7000, 20);
    CHECK(t.newest().rssi == 0);
    t.add(7500, -200);
    CHECK(t.newest().rssi == -127);

    // A new target: nothing from the last one, numbering carries on.
    t.restart();
    CHECK(!t.any());
    CHECK(t.size() == 0);
    CHECK(t.since(0, out, 8) == 0);
    t.add(8000, -80);
    CHECK(t.last_seq() == 9);
    n = t.since(0, out, 8);
    CHECK(n == 1 && out[0].seq == 9 && out[0].rssi == -80);
    // A page still holding the old numbers gets the new reading.
    n = t.since(8, out, 8);
    CHECK(n == 1 && out[0].seq == 9);

    // A page whose last number is older than the restart, after more than
    // a buffer's worth of new readings: it gets the newest, all new.
    for (int i = 0; i < 6; ++i) t.add(9000 + static_cast<uint32_t>(i), -70 + i);
    n = t.since(2, out, 8);
    CHECK(n == 4);
    CHECK(out[0].seq == 12 && out[3].seq == 15 && out[3].rssi == -65);

    CHECK(air_ms_ago(5000, 4000) == 1000);
    CHECK(air_ms_ago(4000, 4005) == 0);
    CHECK(air_ms_ago(100, 0xFFFFFF00u) == 356);   // across the millis() wrap
}

// Looking on every channel, and what a request to find a device does.
static void test_air_find_decisions() {
    CHECK(!air_find_wide(0, 999999));
    CHECK(!air_find_wide(kAirFindWideAfterMs, 999999));
    CHECK(air_find_wide(kAirFindWideAfterMs + 1, kAirFindWideAfterMs + 1));
    // Not again straight after the last one.
    CHECK(!air_find_wide(60000, 5000));

    CHECK(air_find_start(true, true, true) == FindStart::Continue);
    CHECK(air_find_start(true, false, false) == FindStart::Continue);   // forgotten, still found
    CHECK(air_find_start(false, true, true) == FindStart::Fresh);       // the tables know more
    CHECK(air_find_start(false, true, false) == FindStart::Fresh);
    CHECK(air_find_start(false, false, true) == FindStart::Resume);
    CHECK(air_find_start(false, false, false) == FindStart::Unknown);
}

// A turn holding the sweep off: limited in length, and how often.
static void test_air_find_hold() {
    FindHold h{};
    CHECK(!air_find_holding(h, 1000));
    CHECK(air_find_held_for(h, 1000) == 0);
    CHECK(!air_find_hold(h, 1000, 0));
    CHECK(air_find_hold(h, 1000, 30000));
    CHECK(air_find_holding(h, 1000));
    CHECK(air_find_held_for(h, 11000) == 20000);
    CHECK(air_find_holding(h, 30999));
    CHECK(!air_find_holding(h, 31000));
    // Not another within two minutes of the last, even once it has ended.
    CHECK(!air_find_hold(h, 50000, 30000));
    CHECK(!air_find_holding(h, 50000));
    CHECK(air_find_hold(h, 1000 + kAirFindHoldEveryMs, 30000));
    // Never longer than a minute, whatever is asked.
    FindHold g{};
    CHECK(air_find_hold(g, 5000, 600000));
    CHECK(air_find_held_for(g, 5000) == kAirFindHoldMaxMs);
    CHECK(!air_find_holding(g, 5000 + kAirFindHoldMaxMs));
    // Ended early when the turn is over; it still counts towards the next.
    FindHold e{};
    CHECK(air_find_hold(e, 7000, 40000));
    air_find_unhold(e);
    CHECK(!air_find_holding(e, 7001));
    CHECK(!air_find_hold(e, 8000, 40000));
    // Across the millis() wrap.
    FindHold w{};
    CHECK(air_find_hold(w, 0xFFFFF000u, 20000));
    CHECK(air_find_holding(w, 0x00000F00u));
    CHECK(!air_find_holding(w, 0xFFFFF000u + 20000u));
}

static void test_air_find_live() {
    FindTarget t{};
    CHECK(!air_find_live(t, 1000));
    t.kind = FindKind::Ble;
    t.asked_ms = 1000;
    CHECK(air_find_live(t, 1000));
    CHECK(air_find_live(t, 1000 + kAirFindIdleMs - 1));
    CHECK(!air_find_live(t, 1000 + kAirFindIdleMs));
    t.asked_ms = 0xFFFFF000u;
    CHECK(air_find_live(t, 0x00000F00u));          // 7.7 s later, across the wrap
}

// Finding has the scans to itself, after the sweep and the probe.
static void test_air_plan_find() {
    AirInputs in = test_air_inputs();
    in.wifi.requested = in.ble.requested = true;
    in.last_watch_ms = in.now_ms;

    // Bluetooth: straight away, then back to back.
    in.find.kind = FindKind::Ble;
    CHECK(air_next(in) == AirJob::FindBle);
    in.find.ever = true;
    in.find.last_ms = in.now_ms;
    CHECK(air_next(in) == AirJob::FindBle);
    in.now_ms += kAirFindBurstMs;
    CHECK(air_next(in) == AirJob::FindBle);

    // The sweep and the probe still come first, and a scan running is waited for.
    in.netmon_due = true;
    CHECK(air_next(in) == AirJob::None);
    in.netmon_due = false;
    in.radio_busy = true;
    CHECK(air_next(in) == AirJob::None);
    in.radio_busy = false;

    // A listen that would not start is not retried on every pass of loop().
    in.find.failed = true;
    in.find.last_ms = in.now_ms;
    CHECK(air_next(in) == AirJob::None);
    in.now_ms += kAirFindRetryMs - 1;
    CHECK(air_next(in) == AirJob::None);
    in.now_ms += 1;
    CHECK(air_next(in) == AirJob::FindBle);
    in.find.failed = false;

    // Wi-Fi: one look every 800 ms.
    AirInputs w = test_air_inputs();
    w.find.kind = FindKind::Wifi;
    CHECK(air_next(w) == AirJob::FindWifi);
    w.find.ever = true;
    w.find.last_ms = w.now_ms;
    CHECK(air_next(w) == AirJob::None);
    w.now_ms += kAirFindWifiGapMs - 1;
    CHECK(air_next(w) == AirJob::None);
    w.now_ms += 1;
    CHECK(air_next(w) == AirJob::FindWifi);
    // Across the millis() wrap.
    w.find.last_ms = 0xFFFFFF00u;
    w.now_ms = 0xFFFFFF00u + kAirFindWifiGapMs;
    CHECK(air_next(w) == AirJob::FindWifi);
    // A failed look waits the retry time, which is longer than the gap.
    w.find.failed = true;
    w.find.last_ms = 5000;
    w.now_ms = 5000 + kAirFindWifiGapMs;
    CHECK(air_next(w) == AirJob::None);
    w.now_ms = 5000 + kAirFindRetryMs;
    CHECK(air_next(w) == AirJob::FindWifi);

    // Finding over: the ordinary scans pick up where they were, requests first.
    in.find.kind = FindKind::None;
    CHECK(air_next(in) == AirJob::Wifi);
    CHECK(!air_find_due(in.find, in.now_ms));
}

// The page names a device by address; the tables find it.
static void test_air_table_get() {
    BleAirTable<4> b;
    CHECK(b.get(test_mac_n(1)) == nullptr);
    BleSighting s = test_sighting(1, -60, "Tile", BleAddrKind::Static);
    s.at_ms = 1234;
    b.sight(s, 100);
    b.sight(test_sighting(2, -70, ""), 100);
    const BleDevice* d = b.get(test_mac_n(1));
    CHECK(d != nullptr);
    CHECK(d != nullptr && std::strcmp(d->name, "Tile") == 0);
    CHECK(d != nullptr && d->kind == BleAddrKind::Static);
    CHECK(b.get(test_mac_n(2)) != nullptr);
    CHECK(b.get(test_mac_n(3)) == nullptr);

    WifiAirTable<4> w;
    CHECK(w.get(test_mac_n(5)) == nullptr);
    w.heard(test_mac_n(5), "Home", -50, 6, 3, 100);
    w.scan_end(100);
    const AirAp* a = w.get(test_mac_n(5));
    CHECK(a != nullptr && a->channel == 6);
    // A one-channel look, with no scan_end(): the others are not marked as
    // missed, and the scan count does not move.
    w.heard(test_mac_n(6), "Shop", -80, 1, 3, 100);
    w.scan_end(110);
    w.heard(test_mac_n(5), "Home", -40, 6, 3, 115);
    CHECK(w.scans() == 2);
    const AirAp* shop = w.get(test_mac_n(6));
    CHECK(shop != nullptr && shop->misses == 0 && WifiAirTable<4>::live(*shop));
    CHECK(w.get(test_mac_n(5))->last_seen_s == 115);
}

int main() {
    test_mac();
    test_event_log();
    test_cidr();
    test_dhcp();
    test_classify();
    test_table();
    test_table_sighting();
    test_table_eviction();
    test_table_seen_since();
    test_oui();
    test_validate();
    test_sweep();
    test_oui_table_is_sorted();
    test_netlist();
    test_dhcp_bootp();
    test_name_clean();
    test_name_cache();
    test_scan_list();
    test_origin();
    test_air_text();
    test_air_signal();
    test_air_wifi_text();
    test_air_wifi_table();
    test_air_wifi_room();
    test_air_ble_table();
    test_air_ble_eviction_ties();
    test_ble_vendor_table_is_sorted();
    test_air_plan();
    test_ble_adv_parse();
    test_ble_classify();
    test_ble_type_sticks();
    test_air_find_trace();
    test_air_find_live();
    test_air_find_decisions();
    test_air_find_hold();
    test_air_plan_find();
    test_air_table_get();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
