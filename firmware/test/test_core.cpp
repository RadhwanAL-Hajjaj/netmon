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

// --- The Bluetooth link ------------------------------------------------------

#include "../netmon/src/core/ble_link.h"
#include <string>
#include <vector>

static std::vector<uint8_t> test_hex(const char* hex) {
    std::vector<uint8_t> out;
    int hi = -1;
    for (const char* p = hex; *p; ++p) {
        const int v = link_hex(*p);
        if (v < 0) continue;
        if (hi < 0) {
            hi = v;
        } else {
            out.push_back(static_cast<uint8_t>(hi * 16 + v));
            hi = -1;
        }
    }
    return out;
}

static std::string test_hex_of(const uint8_t* b, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        if (i) s += ' ';
        s += d[b[i] >> 4];
        s += d[b[i] & 15];
    }
    return s;
}

// Frames split from a request the way the app splits them (LinkCodec.kt):
// as much as fits in each, first and last flags.
static std::vector<std::vector<uint8_t>> test_request_frames(const std::string& text, uint8_t id, size_t cap) {
    std::vector<std::vector<uint8_t>> frames;
    const size_t room = cap - kLinkHeader;
    size_t off = 0;
    do {
        const size_t n = std::min(room, text.size() - off);
        std::vector<uint8_t> f;
        uint8_t flags = 0;
        if (off == 0) flags |= kLinkFirst;
        if (off + n == text.size()) flags |= kLinkLast;
        f.push_back(flags);
        f.push_back(id);
        f.insert(f.end(), text.begin() + static_cast<long>(off), text.begin() + static_cast<long>(off + n));
        frames.push_back(f);
        off += n;
    } while (off < text.size());
    return frames;
}

struct TestChunks {
    std::vector<std::string> parts;
    size_t count() const { return parts.size(); }
    const uint8_t* data(size_t i) const { return reinterpret_cast<const uint8_t*>(parts[i].data()); }
    size_t size(size_t i) const { return parts[i].size(); }
};

// Every frame of an answer, cut the way the board's pump cuts them.
static std::vector<std::vector<uint8_t>> test_answer_frames(const TestChunks& c, uint8_t id, uint16_t status,
                                                            size_t cap) {
    std::vector<std::vector<uint8_t>> frames;
    size_t chunk = 0, offset = 0;
    bool first = true;
    for (int guard = 0; guard < 100000; ++guard) {
        std::vector<uint8_t> buf(cap);
        const LinkCut cut = link_cut(c, chunk, offset, first, true, id, status, buf.data(), cap);
        if (cut.len == 0) break;
        buf.resize(cut.len);
        frames.push_back(buf);
        chunk = cut.chunk;
        offset = cut.offset;
        first = false;
        if (cut.last) break;
    }
    return frames;
}

// The vectors below are repeated, byte for byte, in the app's own tests
// (android/tools/test/CoreTest.kt, linkTests), so the two ends agree on the
// format and not merely each with itself.
static void test_link_vectors() {
    // A request that fits one 20-byte frame (an ATT MTU of 23, the smallest).
    {
        const auto f = test_request_frames("GET /api/health\n\n", 7, 20);
        CHECK(f.size() == 1);
        CHECK(test_hex_of(f[0].data(), f[0].size()) ==
              "03 07 47 45 54 20 2f 61 70 69 2f 68 65 61 6c 74 68 0a 0a");
    }
    // One that takes two.
    {
        const auto f = test_request_frames("POST /api/nearby/find\n\n{\"stop\":true}", 0x2a, 20);
        CHECK(f.size() == 2);
        CHECK(test_hex_of(f[0].data(), f[0].size()) ==
              "01 2a 50 4f 53 54 20 2f 61 70 69 2f 6e 65 61 72 62 79 2f 66");
        CHECK(test_hex_of(f[1].data(), f[1].size()) ==
              "02 2a 69 6e 64 0a 0a 7b 22 73 74 6f 70 22 3a 74 72 75 65 7d");
    }
    // An answer in one frame: status 200, least significant byte first.
    {
        TestChunks c{{"{\"ok\":true}"}};
        const auto f = test_answer_frames(c, 7, 200, 20);
        CHECK(f.size() == 1);
        CHECK(test_hex_of(f[0].data(), f[0].size()) == "03 07 c8 00 7b 22 6f 6b 22 3a 74 72 75 65 7d");
    }
    // An answer in two, with a frame of 8 bytes.
    {
        TestChunks c{{"not found"}};
        const auto f = test_answer_frames(c, 9, 404, 8);
        CHECK(f.size() == 2);
        CHECK(test_hex_of(f[0].data(), f[0].size()) == "01 09 94 01 6e 6f 74 20");
        CHECK(test_hex_of(f[1].data(), f[1].size()) == "02 09 66 6f 75 6e 64");
    }
    // An empty answer is one frame: both flags, the status, nothing else.
    {
        TestChunks c;
        const auto f = test_answer_frames(c, 1, 204, 20);
        CHECK(f.size() == 1);
        CHECK(test_hex_of(f[0].data(), f[0].size()) == "03 01 cc 00");
    }
    // And the hex helper reads what it writes.
    const auto back = test_hex("03 07 c8 00");
    CHECK(back.size() == 4 && back[2] == 0xc8);
}

static void test_link_assembler() {
    LinkAssembler<64> a;
    // Too short to be a frame, or with a bit nobody defined.
    const uint8_t tiny[1] = {0x03};
    CHECK(a.feed(tiny, 1) == LinkAssembler<64>::Step::Ignored);
    const uint8_t odd[4] = {0x07, 1, 'x', 'y'};
    CHECK(a.feed(odd, 4) == LinkAssembler<64>::Step::Ignored);
    CHECK(a.feed(nullptr, 9) == LinkAssembler<64>::Step::Ignored);

    // One frame, both flags.
    for (const auto& f : test_request_frames("GET /api/map\n\n", 5, 20)) {
        CHECK(a.feed(f.data(), f.size()) == LinkAssembler<64>::Step::Done);
    }
    CHECK(a.id() == 5);
    CHECK(a.size() == 14);
    CHECK_STR(a.data(), "GET /api/map\n\n");
    CHECK(!a.busy());

    // Several frames.
    const std::string text = "POST /api/config\n\n{\"ssid\":\"Home\",\"pass\":\"\"}";
    const auto frames = test_request_frames(text, 200, 9);
    CHECK(frames.size() > 3);
    for (size_t i = 0; i < frames.size(); ++i) {
        const auto step = a.feed(frames[i].data(), frames[i].size());
        CHECK(step == (i + 1 < frames.size() ? LinkAssembler<64>::Step::More : LinkAssembler<64>::Step::Done));
        if (i + 1 < frames.size()) CHECK(a.busy());
    }
    CHECK(a.id() == 200);
    CHECK(std::string(a.data(), a.size()) == text);

    // A continuation with no message open, or with another number, is dropped.
    const uint8_t stray[4] = {kLinkLast, 200, 'z', 'z'};
    CHECK(a.feed(stray, 4) == LinkAssembler<64>::Step::Ignored);
    const auto f1 = test_request_frames(text, 1, 9);
    CHECK(a.feed(f1[0].data(), f1[0].size()) == LinkAssembler<64>::Step::More);
    const uint8_t other[4] = {kLinkLast, 2, 'z', 'z'};
    CHECK(a.feed(other, 4) == LinkAssembler<64>::Step::Ignored);
    CHECK(a.busy());

    // A new first frame drops the half-received request and starts over.
    for (const auto& f : test_request_frames("GET /api/ble\n\n", 3, 20)) a.feed(f.data(), f.size());
    CHECK(a.id() == 3);
    CHECK_STR(a.data(), "GET /api/ble\n\n");

    // Longer than the buffer: swallowed to the end, then reported once, with
    // its number, for a 413.
    const std::string big(100, 'a');
    const auto bf = test_request_frames("POST /api/config\n\n" + big, 77, 20);
    LinkAssembler<64>::Step last = LinkAssembler<64>::Step::Ignored;
    int too = 0;
    for (const auto& f : bf) {
        last = a.feed(f.data(), f.size());
        if (last == LinkAssembler<64>::Step::TooLarge) ++too;
    }
    CHECK(last == LinkAssembler<64>::Step::TooLarge);
    CHECK(too == 1);
    CHECK(a.id() == 77);
    CHECK(a.size() == 0);
    // And the next request is fine.
    for (const auto& f : test_request_frames("GET /api/health\n\n", 78, 20)) {
        CHECK(a.feed(f.data(), f.size()) == LinkAssembler<64>::Step::Done);
    }
    CHECK_STR(a.data(), "GET /api/health\n\n");

    // Exactly the buffer's size fits; the NUL is extra.
    LinkAssembler<16> small;
    const std::string exact = "GET /api/x?a=1\n\n";
    CHECK(exact.size() == 16);
    for (const auto& f : test_request_frames(exact, 1, 6)) small.feed(f.data(), f.size());
    CHECK(std::string(small.data(), small.size()) == exact);
    const std::string over = "GET /api/x?ab=1\n\n";
    LinkAssembler<16>::Step st = LinkAssembler<16>::Step::Ignored;
    for (const auto& f : test_request_frames(over, 2, 6)) st = small.feed(f.data(), f.size());
    CHECK(st == LinkAssembler<16>::Step::TooLarge);
}

static void test_link_parse() {
    LinkRequest r{};
    const char* g = "GET /api/nearby/find?after=12\n\n";
    CHECK(link_parse(g, std::strlen(g), r) == LinkParse::Ok);
    CHECK(!r.post);
    CHECK_STR(r.path, "/api/nearby/find");
    CHECK_STR(r.query, "after=12");
    CHECK(r.body_len == 0);
    CHECK_STR(r.key, "");

    const char* p = "POST /api/config\r\nContent-Type: application/json\r\n\r\n{\"ssid\":\"a b\"}";
    CHECK(link_parse(p, std::strlen(p), r) == LinkParse::Ok);
    CHECK(r.post);
    CHECK_STR(r.path, "/api/config");
    CHECK_STR(r.query, "");
    CHECK(r.body_len == 14);
    CHECK(std::string(r.body, r.body_len) == "{\"ssid\":\"a b\"}");

    // The update password, whatever the case of its name.
    const char* k = "POST /api/update/check\nx-netmon-key:   s3cret pass\nOther: 1\n\n";
    CHECK(link_parse(k, std::strlen(k), r) == LinkParse::Ok);
    CHECK_STR(r.key, "s3cret pass");
    CHECK(r.body_len == 0);

    // " HTTP/1.1" after the target is allowed and ignored; so is a missing
    // blank line.
    const char* h = "GET /api/isp?force HTTP/1.1";
    CHECK(link_parse(h, std::strlen(h), r) == LinkParse::Ok);
    CHECK_STR(r.path, "/api/isp");
    CHECK_STR(r.query, "force");
    CHECK(r.body_len == 0);

    // A body may carry blank lines of its own.
    const char* b = "POST /api/x\n\nline1\n\nline3";
    CHECK(link_parse(b, std::strlen(b), r) == LinkParse::Ok);
    CHECK(std::string(r.body, r.body_len) == "line1\n\nline3");

    const char* bad[] = {"", "GET", "PUT /api/x\n\n", "get /api/x\n\n", "GET api/x\n\n", "GET \n\n",
                         "GET /api/\x01x\n\n"};
    CHECK(link_parse(bad[0], 0, r) == LinkParse::BadMethod);
    CHECK(link_parse(bad[1], 3, r) == LinkParse::BadMethod);
    CHECK(link_parse(bad[2], std::strlen(bad[2]), r) == LinkParse::BadMethod);
    CHECK(link_parse(bad[3], std::strlen(bad[3]), r) == LinkParse::BadMethod);
    CHECK(link_parse(bad[4], std::strlen(bad[4]), r) == LinkParse::BadTarget);
    CHECK(link_parse(bad[5], std::strlen(bad[5]), r) == LinkParse::BadTarget);
    CHECK(link_parse(bad[6], std::strlen(bad[6]), r) == LinkParse::BadTarget);

    std::string longpath = "GET /api/" + std::string(60, 'a') + "\n\n";
    CHECK(link_parse(longpath.c_str(), longpath.size(), r) == LinkParse::TooLong);
    std::string longq = "GET /api/x?" + std::string(200, 'q') + "\n\n";
    CHECK(link_parse(longq.c_str(), longq.size(), r) == LinkParse::TooLong);
    std::string longkey = "POST /api/x\nX-Netmon-Key: " + std::string(100, 'k') + "\n\n";
    CHECK(link_parse(longkey.c_str(), longkey.size(), r) == LinkParse::TooLong);
}

static void test_link_arg() {
    char v[16];
    CHECK(link_arg("after=12", "after", v, sizeof(v)));
    CHECK_STR(v, "12");
    CHECK(link_arg("force", "force", v, sizeof(v)));
    CHECK_STR(v, "");
    CHECK(link_arg("a=1&b=2&c", "b", v, sizeof(v)));
    CHECK_STR(v, "2");
    CHECK(link_arg("a=1&b=2&c", "c", v, sizeof(v)));
    CHECK(!link_arg("a=1&b=2&c", "d", v, sizeof(v)));
    CHECK_STR(v, "");
    CHECK(!link_arg("ab=1", "a", v, sizeof(v)));
    CHECK(!link_arg("", "a", v, sizeof(v)));
    CHECK(!link_arg(nullptr, "a", v, sizeof(v)));
    CHECK(link_arg("n=a%20b+c%2Fd", "n", v, sizeof(v)));
    CHECK_STR(v, "a b c/d");
    // A broken escape is kept as it came.
    CHECK(link_arg("n=50%", "n", v, sizeof(v)));
    CHECK_STR(v, "50%");
    CHECK(link_arg("n=%zz", "n", v, sizeof(v)));
    CHECK_STR(v, "%zz");
    // Cut short, never overrun.
    char s[4];
    CHECK(link_arg("n=abcdefg", "n", s, sizeof(s)));
    CHECK_STR(s, "abc");
    // The first of a repeated name wins.
    CHECK(link_arg("x=1&x=2", "x", v, sizeof(v)));
    CHECK_STR(v, "1");
}

static uint32_t g_link_rng = 12345;
static uint32_t test_link_rand() {
    g_link_rng = g_link_rng * 1103515245u + 12345u;
    return g_link_rng >> 8;
}

// Answers cut from bodies split every which way come back whole, whatever the
// frame size, with the flags in the right places and no empty frame in the
// middle.
static void test_link_cut() {
    bool all_ok = true;
    for (int round = 0; round < 400; ++round) {
        const size_t len = test_link_rand() % 3000;
        std::string body;
        for (size_t i = 0; i < len; ++i) body += static_cast<char>('a' + test_link_rand() % 26);
        TestChunks c;
        size_t at = 0;
        while (at < len) {
            size_t n = test_link_rand() % 600;
            if (n > len - at) n = len - at;
            c.parts.push_back(body.substr(at, n));     // empty pieces too
            at += n;
        }
        if (test_link_rand() % 4 == 0) c.parts.push_back("");
        const size_t cap = 5 + test_link_rand() % 250;
        const uint16_t status = static_cast<uint16_t>(100 + test_link_rand() % 500);
        const uint8_t id = static_cast<uint8_t>(test_link_rand());
        const auto frames = test_answer_frames(c, id, status, cap);
        bool ok = !frames.empty();
        std::string got;
        uint16_t st = 0;
        for (size_t i = 0; ok && i < frames.size(); ++i) {
            const auto& f = frames[i];
            const bool first = i == 0, last = i + 1 == frames.size();
            ok = f.size() <= cap && f[1] == id &&
                 f[0] == ((first ? kLinkFirst : 0) | (last ? kLinkLast : 0));
            size_t from = kLinkHeader;
            if (first) {
                st = static_cast<uint16_t>(f[2] | (f[3] << 8));
                from += kLinkStatus;
            }
            // Only the last frame may be short of data; none but it empty.
            if (!last) ok = ok && f.size() == cap;
            got.append(reinterpret_cast<const char*>(f.data() + from), f.size() - from);
        }
        all_ok = all_ok && ok && st == status && got == body;
    }
    CHECK(all_ok);

    // While the handler is still producing, a used-up body means wait, not
    // an empty frame; and nothing is marked last.
    TestChunks c{{"abc"}};
    uint8_t buf[32];
    LinkCut cut = link_cut(c, 0, 0, true, false, 4, 200, buf, sizeof(buf));
    CHECK(cut.len == 7);
    CHECK(buf[0] == kLinkFirst);
    CHECK(!cut.last);
    cut = link_cut(c, cut.chunk, cut.offset, false, false, 4, 200, buf, sizeof(buf));
    CHECK(cut.len == 0);
    // More arrives, then the handler finishes.
    c.parts.push_back("de");
    cut = link_cut(c, 1, 0, false, false, 4, 200, buf, sizeof(buf));
    CHECK(cut.len == 4);
    CHECK(buf[0] == 0);
    cut = link_cut(c, cut.chunk, cut.offset, false, true, 4, 200, buf, sizeof(buf));
    CHECK(cut.len == 2);
    CHECK(buf[0] == kLinkLast);
    CHECK(cut.last);
    // A frame too small to carry anything is never cut.
    cut = link_cut(c, 0, 0, true, true, 4, 200, buf, 4);
    CHECK(cut.len == 0);
}

static void test_link_pairing() {
    PairWindow w{};
    CHECK(!pair_live(w, 0));
    CHECK(pair_left_ms(w, 0) == 0);
    pair_open(w, 1000, 4123456);
    CHECK(pair_live(w, 1000));
    CHECK(w.code == 123456);
    CHECK(pair_left_ms(w, 1000) == kPairWindowMs);
    CHECK(pair_passkey(w, 5000, 999) == 123456);
    // Asking again while open keeps the code and restarts the two minutes.
    pair_open(w, 61000, 777777);
    CHECK(w.code == 123456);
    CHECK(pair_left_ms(w, 61000) == kPairWindowMs);
    CHECK(pair_live(w, 61000 + kPairWindowMs - 1));
    CHECK(!pair_live(w, 61000 + kPairWindowMs));
    // Closed: any other attempt gets a code nobody was shown.
    CHECK(pair_passkey(w, 61000 + kPairWindowMs, 2000042) == 42);
    // Reopened later: a fresh code.
    pair_open(w, 400000, 31);
    CHECK(w.code == 31);
    char t[7];
    pair_code_text(w.code, t);
    CHECK_STR(t, "000031");
    pair_code_text(999999, t);
    CHECK_STR(t, "999999");
    pair_code_text(1000001, t);
    CHECK_STR(t, "000001");
    // A failure that says something about the code costs one of three tries;
    // the window stays open, with the same code, until the third.
    CHECK(pair_tries_left(w) == kPairTries);
    pair_done(w, 410000, false, PairWhy::WrongCode);
    CHECK(pair_live(w, 410000));
    CHECK(w.code == 31);
    CHECK(pair_tries_left(w) == kPairTries - 1);
    CHECK_STR(pair_result_text(w.result), "failed");
    CHECK_STR(pair_why_key(w.why), "wrong_code");
    // A dropped connection or a timeout never got to the code: free.
    pair_done(w, 411000, false, PairWhy::Dropped);
    pair_done(w, 412000, false, PairWhy::TimedOut);
    CHECK(pair_live(w, 412000));
    CHECK(pair_tries_left(w) == kPairTries - 1);
    CHECK_STR(pair_why_key(w.why), "timed_out");
    pair_done(w, 413000, false, PairWhy::Cancelled);
    CHECK(pair_live(w, 413000));
    pair_done(w, 414000, false, PairWhy::WrongCode);
    CHECK(!pair_live(w, 414000));
    CHECK(pair_tries_left(w) == 0);
    CHECK(pair_passkey(w, 414001, 1000007) == 7);
    // A fresh window starts the count again.
    pair_open(w, 420000, 5);
    CHECK_STR(pair_result_text(w.result), "");
    CHECK_STR(pair_why_key(w.why), "");
    CHECK(pair_tries_left(w) == kPairTries);
    pair_done(w, 421000, true);
    CHECK_STR(pair_result_text(w.result), "paired");
    CHECK_STR(pair_why_key(w.why), "paired");
    CHECK(!pair_live(w, 421000));      // a phone that pairs closes it
    pair_open(w, 430000, 6);
    pair_close(w);
    CHECK(!pair_live(w, 430001));

    // The owner's own code: every window, never a random one.
    PairWindow f{};
    pair_open(f, 1000, 123, 4242);
    CHECK(f.code == 4242);
    CHECK(pair_passkey(f, 2000, 77) == 4242);
    pair_code_text(f.code, t);
    CHECK_STR(t, "004242");
    pair_done(f, 3000, false, PairWhy::WrongCode);
    pair_done(f, 3001, false, PairWhy::WrongCode);
    pair_done(f, 3002, false, PairWhy::WrongCode);
    CHECK(!pair_live(f, 3003));
    CHECK(pair_passkey(f, 3003, 1000077) == 77);     // closed: not the owner's code
    pair_open(f, 5000, 999, 4242);
    CHECK(f.code == 4242);
    CHECK(pair_tries_left(f) == kPairTries);
    // Changed while open: the new code from then on.
    pair_open(f, 6000, 1, 7);
    CHECK(f.code == 7);
    // Back to random codes: the open window keeps the one it has.
    pair_open(f, 7000, 555555);
    CHECK(f.code == 7);
    pair_close(f);
    pair_open(f, 8000, 555555);
    CHECK(f.code == 555555);
    // The clock wrapping is no different from any other two minutes.
    PairWindow z{};
    pair_open(z, 0xFFFFFF00u, 9);
    CHECK(pair_live(z, 0x00000100u));
    CHECK(pair_left_ms(z, 0x00000100u) == kPairWindowMs - 0x200);

    // Unpaired connections are let go; paired ones never.
    CHECK(!link_drop_unpaired(false, 0, kLinkUnpairedMs, false));
    CHECK(link_drop_unpaired(false, 0, kLinkUnpairedMs + 1, false));
    CHECK(!link_drop_unpaired(false, 0, kLinkUnpairedMs + 1, true));
    CHECK(link_drop_unpaired(false, 0, kLinkPairingMs + 1, true));
    CHECK(!link_drop_unpaired(true, 0, 10 * kLinkPairingMs, false));
}

// How the end of an attempt reads, from the statuses NimBLE reports.
static void test_link_pair_why() {
    CHECK(pair_why_of(0, true, true) == PairWhy::Paired);
    CHECK(pair_why_of(0, true, false) == PairWhy::NoCode);
    CHECK(pair_why_of(0, false, false) == PairWhy::Other);
    CHECK(pair_why_of(kNimTimeout, false, false) == PairWhy::TimedOut);
    CHECK(pair_why_of(kNimNotConn, false, false) == PairWhy::Dropped);
    // Confirm value failed, found by the board or by the phone; DHKey check.
    CHECK(pair_why_of(0x404, false, false) == PairWhy::WrongCode);
    CHECK(pair_why_of(0x504, false, false) == PairWhy::WrongCode);
    CHECK(pair_why_of(0x50b, false, false) == PairWhy::WrongCode);
    CHECK(pair_why_of(0x501, false, false) == PairWhy::Cancelled);
    CHECK(pair_why_of(0x509, false, false) == PairWhy::TooMany);
    CHECK(pair_why_of(0x503, false, false) == PairWhy::Refused);
    CHECK(pair_why_of(0x405, false, false) == PairWhy::Refused);
    CHECK(pair_why_of(0x508, false, false) == PairWhy::Other);
    // The controller's own reasons for a connection going.
    CHECK(pair_why_of(0x208, false, false) == PairWhy::Dropped);
    CHECK(pair_why_of(0x213, false, false) == PairWhy::Dropped);
    CHECK(pair_why_of(0x23e, false, false) == PairWhy::Dropped);
    CHECK(pair_why_of(0x206, false, false) == PairWhy::Other);
    CHECK(pair_why_of(0x400, false, false) == PairWhy::Other);
    CHECK(pair_why_of(12345, false, false) == PairWhy::Other);
    // Every reason has a key and words; none has neither.
    for (uint8_t w = 1; w <= static_cast<uint8_t>(PairWhy::Other); ++w) {
        CHECK(pair_why_key(w)[0] != '\0');
        CHECK(pair_why_text(w)[0] != '\0');
    }
    CHECK_STR(pair_why_key(0), "");
    CHECK_STR(pair_why_text(0), "");
    CHECK(!pair_counts(PairWhy::Dropped));
    CHECK(!pair_counts(PairWhy::TimedOut));
    CHECK(pair_counts(PairWhy::WrongCode));
    CHECK(pair_counts(PairWhy::Other));
}

// The session travels with every request over the link, as it does over Wi-Fi.
static void test_link_parse_auth() {
    LinkRequest r;
    const char* a =
        "GET /api/devices\nAuthorization: Bearer 0123456789abcdef0123456789abcdef\nX-Netmon-Key: k1\n\n";
    CHECK(link_parse(a, std::strlen(a), r) == LinkParse::Ok);
    CHECK_STR(r.auth, "Bearer 0123456789abcdef0123456789abcdef");
    CHECK_STR(r.key, "k1");
    const char* b = "GET /api/devices\r\nauthorization:   Bearer x\r\n\r\n";
    CHECK(link_parse(b, std::strlen(b), r) == LinkParse::Ok);
    CHECK_STR(r.auth, "Bearer x");
    CHECK_STR(r.key, "");
    const char* c = "GET /api/devices\n\n";
    CHECK(link_parse(c, std::strlen(c), r) == LinkParse::Ok);
    CHECK_STR(r.auth, "");
    std::string longer = "GET /api/devices\nAuthorization: Bearer " + std::string(kLinkAuthMax, 'a') + "\n\n";
    CHECK(link_parse(longer.c_str(), longer.size(), r) == LinkParse::TooLong);
}

// --- Saved reports -------------------------------------------------------------

#include "../netmon/src/core/report.h"

static void test_clock() {
    Clock c{};
    CHECK(!clock_known(c));
    CHECK(clock_now(c, 100) == 0);
    // Too early, too late, before the board even started, or no source.
    CHECK(!clock_set(c, 1600000000u, 10, ClockSource::Client));
    CHECK(!clock_set(c, 4200000000u, 10, ClockSource::Client));
    CHECK(!clock_set(c, 1791136862u, 10, ClockSource::None));
    CHECK(!clock_known(c));
    // A phone's clock, read at uptime 100.
    CHECK(clock_set(c, 1791136862u, 100, ClockSource::Client));
    CHECK(c.boot_unix == 1791136762u);
    CHECK(clock_now(c, 160) == 1791136922u);
    // Another client may correct it; then the internet's word outranks them.
    CHECK(clock_set(c, 1791136900u, 100, ClockSource::Client));
    CHECK(c.boot_unix == 1791136800u);
    CHECK(clock_set(c, 1791136950u, 200, ClockSource::Internet));
    CHECK(c.boot_unix == 1791136750u);
    CHECK(!clock_set(c, 1791139999u, 200, ClockSource::Client));
    CHECK(c.boot_unix == 1791136750u);
    CHECK(clock_set(c, 1791136960u, 205, ClockSource::Internet));
    CHECK_STR(clock_source_text(c.source), "internet");
    CHECK_STR(clock_source_text(ClockSource::Client), "client");
    CHECK_STR(clock_source_text(ClockSource::None), "none");
}

static void test_http_date() {
    uint32_t t = 0;
    CHECK(http_date_parse("Sun, 04 Oct 2026 18:01:02 GMT", t));
    CHECK(t == 1791136862u);
    CHECK(http_date_parse("Thu, 29 Feb 2024 12:00:00 GMT", t));
    CHECK(t == 1709208000u);
    CHECK(http_date_parse("Thu, 01 Jan 1970 00:00:00 GMT", t));
    CHECK(t == 0);
    CHECK(http_date_parse("Fri, 31 Dec 2099 23:59:59 GMT", t));
    CHECK(t == 4102444799u);
    // The obsolete forms, and broken ones, are refused.
    CHECK(!http_date_parse("Sunday, 04-Oct-26 18:01:02 GMT", t));
    CHECK(!http_date_parse("Sun Oct  4 18:01:02 2026", t));
    CHECK(!http_date_parse("Sun, 04 Okt 2026 18:01:02 GMT", t));
    CHECK(!http_date_parse("Sun, 04 Oct 2026 24:01:02 GMT", t));
    CHECK(!http_date_parse("Sun, 4 Oct 2026 18:01:02 GMT", t));
    CHECK(!http_date_parse("Sun, 04 Oct 2026 18:01:02 UTC", t));
    CHECK(!http_date_parse("", t));
    CHECK(!http_date_parse(nullptr, t));

    const char* h = "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nX-Date: nope\r\n"
                    "date:  Sun, 04 Oct 2026 18:01:02 GMT\r\nContent-Length: 12\r\n";
    CHECK(http_date_header(h, std::strlen(h), t));
    CHECK(t == 1791136862u);
    const char* none = "HTTP/1.0 200 OK\r\nUpdated: Sun, 04 Oct 2026 18:01:02 GMT\r\n";
    CHECK(!http_date_header(none, std::strlen(none), t));
    // Only within the length given.
    CHECK(!http_date_header(h, 40, t));
    CHECK(!http_date_header(nullptr, 10, t));
}

static ReportSlot test_slot(const char* ssid, const char* subnet, uint32_t seq) {
    ReportSlot s{};
    s.used = true;
    std::strncpy(s.ssid, ssid, sizeof(s.ssid) - 1);
    std::strncpy(s.subnet, subnet, sizeof(s.subnet) - 1);
    s.seq = seq;
    return s;
}

static void test_report_slots() {
    ReportSlot s[4]{};
    CHECK(report_slot_for(s, 4, "Home", "192.168.2.0/24") == 0);
    CHECK(report_next_seq(s, 4) == 1);
    s[0] = test_slot("Home", "192.168.2.0/24", 5);
    s[2] = test_slot("Office", "10.0.0.0/16", 9);
    // Its own slot, else the first empty one.
    CHECK(report_slot_for(s, 4, "Home", "192.168.2.0/24") == 0);
    CHECK(report_slot_for(s, 4, "Office", "10.0.0.0/16") == 2);
    CHECK(report_slot_for(s, 4, "Home", "192.168.3.0/24") == 1);   // same name, another subnet
    CHECK(report_slot_for(s, 4, "home", "192.168.2.0/24") == 1);   // names are case sensitive
    CHECK(report_next_seq(s, 4) == 10);
    // All four used: the one saved longest ago gives way.
    s[1] = test_slot("Cafe", "192.168.1.0/24", 7);
    s[3] = test_slot("Lab", "172.16.0.0/24", 3);
    CHECK(report_slot_for(s, 4, "Friend", "192.168.0.0/24") == 3);
    s[3].seq = 11;
    CHECK(report_slot_for(s, 4, "Friend", "192.168.0.0/24") == 0);
    CHECK(report_slot_for(s, 4, "Lab", "172.16.0.0/24") == 3);
    CHECK(!report_same(s[0], nullptr, "x"));

    // Ages and gaps: from the uptime this boot, else from the clock.
    ReportSlot r = test_slot("Home", "192.168.2.0/24", 1);
    r.this_boot = true;
    r.saved_up_s = 1000;
    CHECK(report_age_s(r, 1600, 0) == 600);
    uint32_t gap = 0;
    CHECK(report_gap_s(r, 1600, 0, gap) && gap == 600);
    r.this_boot = false;
    r.saved_unix = 1791130000u;
    CHECK(report_age_s(r, 50, 0) == -1);
    CHECK(!report_gap_s(r, 50, 0, gap));
    CHECK(report_age_s(r, 50, 1791136862u) == 6862);
    CHECK(report_gap_s(r, 50, 1791136862u, gap) && gap == 6862);
    r.saved_unix = 0;
    CHECK(report_age_s(r, 50, 1791136862u) == -1);
    CHECK(!report_gap_s(r, 50, 1791136862u, gap));
    ReportSlot empty{};
    CHECK(report_age_s(empty, 50, 1791136862u) == -1);
}

static ReportRow test_row(uint8_t n, uint32_t ago, uint32_t seen_unix, const char* name = "") {
    ReportRow r{};
    r.mac = test_mac_n(n);
    r.ip = 0xC0A80200u + n;
    r.status = Status::Known;
    r.online = true;
    r.up_s = 50;
    r.ago_s = ago;
    r.seen_unix = seen_unix;
    std::strncpy(r.hostname, name, sizeof(r.hostname) - 1);
    return r;
}

static void test_report_rows() {
    // A device from the board's table, saved at uptime 1000 with the clock
    // known: dated, and aged from the uptime.
    Device d{};
    d.mac = test_mac_n(4);
    d.ip = 0xC0A8022Du;
    d.first_seen = 100;
    d.online_since = 400;
    d.last_seen = 990;
    d.status = Status::Private;
    d.online = true;
    std::strncpy(d.hostname, "Pixel-7", sizeof(d.hostname) - 1);
    Clock clk{};
    clock_set(clk, 1791136862u, 1000, ClockSource::Client);
    ReportRow r{};
    report_row_from(d, 1000, clk, false, r);
    CHECK(r.ago_s == 10);
    CHECK(r.up_s == 600);
    CHECK(r.seen_unix == 1791136852u);
    CHECK(r.first_unix == 1791135962u);
    CHECK(!r.carried);
    CHECK_STR(r.hostname, "Pixel-7");
    // Without a clock: ages only. Offline: no uptime.
    d.online = false;
    report_row_from(d, 1000, Clock{}, true, r);
    CHECK(r.seen_unix == 0 && r.first_unix == 0);
    CHECK(r.up_s == 0);
    CHECK(r.self);

    // Merging: this boot's rows 1 and 2; the last report had 2, 3, 4 and 5.
    ReportRow rows[8]{};
    rows[0] = test_row(1, 5, 0, "desk");
    rows[1] = test_row(2, 0, 0, "");
    rows[2] = test_row(2, 30, 1791130000u, "nas");      // the same device: its name fills the gap
    rows[2].first_unix = 1700000000u;
    rows[3] = test_row(3, 900, 1791129000u, "tv");
    rows[4] = test_row(4, 60, 1791129900u, "phone");
    rows[5] = test_row(5, 200, 0, "");
    rows[5].self = true;
    size_t n = report_merge(rows, 2, 4, true, 600, 8);
    CHECK(n == 5);
    CHECK(mac_equal(rows[0].mac, test_mac_n(1)) && !rows[0].carried);
    CHECK(mac_equal(rows[1].mac, test_mac_n(2)) && !rows[1].carried);
    CHECK_STR(rows[1].hostname, "nas");
    CHECK(rows[1].first_unix == 1700000000u);
    CHECK(rows[1].ago_s == 0);                       // this boot's sighting stands
    // Carried over, most recently seen first, aged by the gap, offline.
    CHECK(mac_equal(rows[2].mac, test_mac_n(4)) && rows[2].carried && rows[2].ago_s == 660);
    CHECK(mac_equal(rows[3].mac, test_mac_n(5)) && rows[3].ago_s == 800 && !rows[3].self);
    CHECK(mac_equal(rows[4].mac, test_mac_n(3)) && rows[4].ago_s == 1500);
    CHECK(!rows[2].online && rows[2].up_s == 0);
    CHECK(rows[2].seen_unix == 1791129900u);         // a date stays as it was

    // A gap that cannot be told leaves the ages as they were; a cap drops the
    // oldest; a device twice in the old report is kept once.
    ReportRow r2[6]{};
    r2[0] = test_row(1, 0, 0);
    r2[1] = test_row(7, 100, 0);
    r2[2] = test_row(8, 50, 0);
    r2[3] = test_row(7, 100, 0);
    r2[4] = test_row(9, 400, 0);
    n = report_merge(r2, 1, 4, false, 0, 3);
    CHECK(n == 3);
    CHECK(mac_equal(r2[1].mac, test_mac_n(8)) && r2[1].ago_s == 50);
    CHECK(mac_equal(r2[2].mac, test_mac_n(7)) && r2[2].ago_s == 100);
    // Ages never wrap.
    ReportRow r3[2]{};
    r3[0] = test_row(1, 0xFFFFFF00u, 0);
    n = report_merge(r3, 0, 1, true, 0x1000, 2);
    CHECK(n == 1 && r3[0].ago_s == 0xFFFFFFFFu);
    // Nothing from before: this boot's rows as they are.
    ReportRow r4[2]{};
    r4[0] = test_row(1, 3, 0);
    CHECK(report_merge(r4, 1, 0, true, 10, 2) == 1);
}

static void test_report_lines() {
    ReportMeta m{};
    std::strcpy(m.ssid, "Home \"5\"");
    std::strcpy(m.subnet, "192.168.2.0/24");
    std::strcpy(m.gateway, "192.168.2.1");
    std::strcpy(m.gateway_mac, "50:91:E3:12:34:56");
    std::strcpy(m.board_ip, "192.168.2.27");
    std::strcpy(m.board_mac, "D4:E9:F4:12:34:56");
    std::strcpy(m.version, "0.12.0-bluetooth");
    m.seq = 7;
    m.saved_unix = 1791136862u;
    m.saved_up_s = 5400;
    m.clock = ClockSource::Client;
    m.passes = 88;
    m.learning = false;
    m.count = 2;
    m.online = 1;
    char buf[kReportLineMax];
    const size_t n = report_header_line(m, buf, sizeof(buf));
    CHECK(n == std::strlen(buf));
    CHECK_STR(buf, "{\"report\":1,\"ssid\":\"Home \\\"5\\\"\",\"subnet\":\"192.168.2.0/24\","
                   "\"gateway\":\"192.168.2.1\",\"gateway_mac\":\"50:91:E3:12:34:56\","
                   "\"board_ip\":\"192.168.2.27\",\"board_mac\":\"D4:E9:F4:12:34:56\","
                   "\"version\":\"0.12.0-bluetooth\",\"seq\":7,\"saved_unix\":1791136862,"
                   "\"saved_up_s\":5400,\"clock\":\"client\",\"passes\":88,\"learning\":false,"
                   "\"count\":2,\"online\":1,\"devices\":[\n");
    // Too small a buffer: nothing, never a broken line.
    char tiny[40];
    CHECK(report_header_line(m, tiny, sizeof(tiny)) == 0);

    ReportRow r = test_row(4, 10, 1791136852u, "Desk\\PC\x01");
    r.mac.b[0] = 0xDA;                      // a private address
    r.status = Status::Private;
    r.first_unix = 1791135962u;
    const size_t k = report_row_line(r, "Acme \"Co\"", false, buf, sizeof(buf));
    CHECK(k == std::strlen(buf));
    CHECK_STR(buf, "{\"mac\":\"DA:00:00:00:00:04\",\"ip\":\"192.168.2.4\",\"hostname\":\"Desk\\\\PC\","
                   "\"vendor\":\"Acme \\\"Co\\\"\",\"status\":\"private\",\"randomised\":true,"
                   "\"self\":false,\"online\":true,\"up_s\":50,\"last_seen_s\":10,"
                   "\"seen_unix\":1791136852,\"first_unix\":1791135962,\"carried\":false},\n");
    r.carried = true;
    report_row_line(r, nullptr, true, buf, sizeof(buf));
    CHECK(std::strstr(buf, "\"vendor\":\"\"") != nullptr);
    CHECK(std::strstr(buf, "\"carried\":true}\n") != nullptr);
    // The longest a row can be still fits a line.
    ReportRow big{};
    big.mac = test_mac_n(1);
    big.ip = 0xFFFFFFFFu;
    std::memset(big.hostname, '"', sizeof(big.hostname) - 1);
    big.ago_s = big.up_s = big.seen_unix = big.first_unix = 0xFFFFFFFFu;
    char vendor[64];
    std::memset(vendor, '"', sizeof(vendor) - 1);
    vendor[sizeof(vendor) - 1] = '\0';
    CHECK(report_row_line(big, vendor, false, buf, sizeof(buf)) > 0);

    CHECK(report_status_of("known") == Status::Known);
    CHECK(report_status_of("private") == Status::Private);
    CHECK(report_status_of("unknown") == Status::Unknown);
    CHECK(report_status_of(nullptr) == Status::Unknown);
    CHECK(report_clock_of("internet") == ClockSource::Internet);
    CHECK(report_clock_of("client") == ClockSource::Client);
    CHECK(report_clock_of("x") == ClockSource::None);
}

// --- Signing in (0.13) ---------------------------------------------------------

#include "../netmon/src/core/auth.h"

static std::string hex_of(const uint8_t* b, size_t n) {
    std::vector<char> s(2 * n + 1);
    hex_encode(b, n, s.data());
    return std::string(s.data());
}

static void test_sha256() {
    uint8_t h[32];
    sha256("", 0, h);
    CHECK_STR(hex_of(h, 32).c_str(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha256("abc", 3, h);
    CHECK_STR(hex_of(h, 32).c_str(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* m448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256(m448, std::strlen(m448), h);
    CHECK_STR(hex_of(h, 32).c_str(), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // Lengths either side of where the padding needs a second block.
    std::string x55(55, 'x'), x56(56, 'x'), x64(64, 'x');
    sha256(x55.data(), x55.size(), h);
    CHECK_STR(hex_of(h, 32).c_str(), "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072");
    sha256(x56.data(), x56.size(), h);
    CHECK_STR(hex_of(h, 32).c_str(), "04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e");
    sha256(x64.data(), x64.size(), h);
    CHECK_STR(hex_of(h, 32).c_str(), "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c");
    // A million a's, fed in uneven pieces.
    Sha256 s;
    sha256_init(s);
    std::string a(1000, 'a');
    size_t fed = 0, step = 1;
    while (fed < 1000000) {
        size_t n = step % 997 + 1;
        if (n > 1000000 - fed) n = 1000000 - fed;
        sha256_update(s, a.data(), n);
        fed += n;
        step = step * 7 + 3;
    }
    sha256_final(s, h);
    CHECK_STR(hex_of(h, 32).c_str(), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // HMAC, RFC 4231 cases 1, 2 and 6 (a key longer than a block).
    uint8_t k1[20];
    std::memset(k1, 0x0b, sizeof(k1));
    hmac_sha256(k1, sizeof(k1), reinterpret_cast<const uint8_t*>("Hi There"), 8, h);
    CHECK_STR(hex_of(h, 32).c_str(), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    const char* q = "what do ya want for nothing?";
    hmac_sha256(reinterpret_cast<const uint8_t*>("Jefe"), 4, reinterpret_cast<const uint8_t*>(q), std::strlen(q), h);
    CHECK_STR(hex_of(h, 32).c_str(), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    uint8_t k6[131];
    std::memset(k6, 0xaa, sizeof(k6));
    const char* m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    hmac_sha256(k6, sizeof(k6), reinterpret_cast<const uint8_t*>(m6), std::strlen(m6), h);
    CHECK_STR(hex_of(h, 32).c_str(), "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

    // PBKDF2-HMAC-SHA-256: the published vectors, two blocks of output and more.
    const uint8_t* pw = reinterpret_cast<const uint8_t*>("password");
    const uint8_t* salt = reinterpret_cast<const uint8_t*>("salt");
    CHECK(pbkdf2_sha256(pw, 8, salt, 4, 1, h, 32));
    CHECK_STR(hex_of(h, 32).c_str(), "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK(pbkdf2_sha256(pw, 8, salt, 4, 2, h, 32));
    CHECK_STR(hex_of(h, 32).c_str(), "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    CHECK(pbkdf2_sha256(pw, 8, salt, 4, 4096, h, 32));
    CHECK_STR(hex_of(h, 32).c_str(), "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    uint8_t d40[40];
    const char* p2 = "passwordPASSWORDpassword";
    const char* s2 = "saltSALTsaltSALTsaltSALTsaltSALTsalt";
    CHECK(pbkdf2_sha256(reinterpret_cast<const uint8_t*>(p2), std::strlen(p2),
                        reinterpret_cast<const uint8_t*>(s2), std::strlen(s2), 4096, d40, sizeof(d40)));
    CHECK_STR(hex_of(d40, 40).c_str(),
              "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9");
    uint8_t d64[64];
    CHECK(pbkdf2_sha256(reinterpret_cast<const uint8_t*>("passwd"), 6, salt, 4, 1, d64, sizeof(d64)));
    CHECK_STR(hex_of(d64, 64).c_str(),
              "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
              "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
    uint8_t long_salt[61] = {0};
    CHECK(!pbkdf2_sha256(pw, 8, long_salt, sizeof(long_salt), 1, h, 32));
    CHECK(!pbkdf2_sha256(pw, 8, salt, 4, 0, h, 32));

    // Comparing and hex.
    uint8_t x[4] = {1, 2, 3, 4}, y[4] = {1, 2, 3, 4};
    CHECK(same_bytes(x, y, 4));
    y[3] = 5;
    CHECK(!same_bytes(x, y, 4));
    CHECK(same_text_ct("netmon-update", "netmon-update"));
    CHECK(!same_text_ct("netmon-updat", "netmon-update"));
    CHECK(!same_text_ct("netmon-update!", "netmon-update"));
    CHECK(!same_text_ct("", "netmon-update"));
    CHECK(!same_text_ct(nullptr, "x"));
    uint8_t back[4];
    CHECK(hex_decode("0aFf10e9", back, 4));
    CHECK(back[0] == 0x0a && back[1] == 0xff && back[2] == 0x10 && back[3] == 0xe9);
    CHECK(!hex_decode("0aFf10e", back, 4));
    CHECK(!hex_decode("0aFf10eg", back, 4));
    CHECK(!hex_decode(nullptr, back, 4));
}

static void test_auth_sessions() {
    CHECK(token_well_formed("0123456789abcdef0123456789abcdef"));
    CHECK(!token_well_formed("0123456789ABCDEF0123456789abcdef"));
    CHECK(!token_well_formed("0123456789abcdef0123456789abcde"));
    CHECK(!token_well_formed("0123456789abcdef0123456789abcdef0"));
    CHECK(!token_well_formed("0123456789abcdef0123456789abcdeg"));
    CHECK(!token_well_formed(nullptr));
    uint8_t h1[32], h2[32], h3[32];
    token_hash("0123456789abcdef0123456789abcdef", h1);
    CHECK_STR(hex_of(h1, 32).c_str(), "3eb1bd439947eb762998e566ccc2e099c791118b2f40579cc4f7da2b5061b7f9");
    token_hash("11111111111111111111111111111111", h2);
    token_hash("22222222222222222222222222222222", h3);

    SessionTable<4> t;
    t.clear();
    CHECK(t.find(h1, 100, 0) == -1);
    CHECK(t.count(100, 0) == 0);
    // A session not kept: alive while used, gone after 12 hours idle.
    const int a = static_cast<int>(t.add(h1, false, 100, 0));
    CHECK(t.find(h1, 101, 0) == a);
    CHECK(t.find(h2, 101, 0) == -1);
    CHECK(t.find(h1, 100 + kSessionIdleS - 1, 0) == a);
    CHECK(t.find(h1, 100 + kSessionIdleS, 0) == -1);
    t.touch(a, 100 + kSessionIdleS - 10);
    CHECK(t.find(h1, 100 + kSessionIdleS + 100, 0) == a);
    // ... and a week at most, however busy.
    uint32_t now = 100;
    while (now < 100 + kSessionShortMaxS - 3600) {
        now += 3600;
        t.touch(a, now);
    }
    CHECK(t.find(h1, 100 + kSessionShortMaxS - 1, 0) == a);
    CHECK(t.find(h1, 100 + kSessionShortMaxS, 0) == -1);

    // Kept ("Remember me"): idle does not matter, 30 days do.
    t.clear();
    const int r = static_cast<int>(t.add(h2, true, 50, 0));
    CHECK(t.find(h2, 50 + kSessionIdleS + 5, 0) == r);
    CHECK(t.find(h2, 50 + kSessionLongS - 1, 0) == r);
    CHECK(t.find(h2, 50 + kSessionLongS, 0) == -1);
    // Dated by the clock when the board has one, across restarts too.
    t.clear();
    const int d = static_cast<int>(t.add(h2, true, 10, 1760000000u));
    t.s[d].earlier_boot = true;           // as loaded after a restart
    CHECK(t.find(h2, 5, 1760000000u + kSessionLongS - 1) == d);
    CHECK(t.find(h2, 5, 1760000000u + kSessionLongS) == -1);
    CHECK(t.find(h2, 5, 0) == d);         // no clock yet after the restart: kept
    // A clock gone backwards does not end it.
    CHECK(t.find(h2, 5, 1700000000u) == d);

    // Undated sessions get their date once the clock is known.
    t.clear();
    const int u = static_cast<int>(t.add(h3, true, 1000, 0));
    CHECK(t.date_undated(1600, 1760000000u));
    CHECK(t.s[u].created_unix == 1760000000u - 600);
    CHECK(!t.date_undated(1700, 1760000100u));     // nothing left to date
    CHECK(!t.date_undated(1700, 0));

    // A full table: a dead slot first, then the session not kept used longest
    // ago, and only then a kept one.
    SessionTable<3> f;
    f.clear();
    uint8_t k[6][32];
    for (int i = 0; i < 6; ++i) {
        char tok[33];
        std::snprintf(tok, sizeof(tok), "%032d", i);
        token_hash(tok, k[i]);
    }
    const size_t s0 = f.add(k[0], true, 10, 0);
    const size_t s1 = f.add(k[1], false, 20, 0);
    const size_t s2 = f.add(k[2], false, 30, 0);
    CHECK(s0 != s1 && s1 != s2 && s0 != s2);
    f.touch(static_cast<int>(s1), 40);                       // s2 now idle longest
    const size_t s3 = f.add(k[3], true, 50, 0);
    CHECK(s3 == s2);
    CHECK(f.find(k[2], 51, 0) == -1);
    CHECK(f.find(k[0], 51, 0) == static_cast<int>(s0));
    const size_t s4 = f.add(k[4], true, 60, 0);              // only s1 is not kept
    CHECK(s4 == s1);
    const size_t s5 = f.add(k[5], true, 70, 0);              // all kept: the oldest goes
    CHECK(s5 == s0);
    CHECK(f.find(k[0], 71, 0) == -1);
    CHECK(f.count(71, 0) == 3);
    // A slot whose session ran out is taken before any live one.
    SessionTable<2> g;
    g.clear();
    g.add(k[0], false, 0, 0);
    g.add(k[1], true, 0, 0);
    const size_t gi = g.add(k[2], true, kSessionIdleS + 1, 0);
    CHECK(g.find(k[1], kSessionIdleS + 2, 0) >= 0);
    CHECK(g.find(k[2], kSessionIdleS + 2, 0) == static_cast<int>(gi));
    // Purging, and signing out everywhere but here.
    CHECK(!g.purge(kSessionIdleS + 2, 0));
    CHECK(g.remove_all_but(static_cast<int>(gi)) == 1);
    CHECK(g.count(kSessionIdleS + 3, 0) == 1);
    g.remove(static_cast<int>(gi));
    CHECK(g.count(kSessionIdleS + 3, 0) == 0);
    g.add(k[0], false, 0, 0);
    CHECK(g.purge(kSessionIdleS, 0));
    CHECK(!g.purge(kSessionIdleS, 0));
}

static void test_auth_headers() {
    char v[40];
    CHECK(cookie_value("nm_s=abc", "nm_s", v, sizeof(v)));
    CHECK_STR(v, "abc");
    CHECK(cookie_value("theme=dark; nm_s=0123; x=1", "nm_s", v, sizeof(v)));
    CHECK_STR(v, "0123");
    CHECK(cookie_value("a=1;nm_s=zz ;b=2", "nm_s", v, sizeof(v)));
    CHECK_STR(v, "zz");
    CHECK(!cookie_value("xnm_s=1; nm_sx=2", "nm_s", v, sizeof(v)));
    CHECK_STR(v, "");
    CHECK(!cookie_value("", "nm_s", v, sizeof(v)));
    CHECK(!cookie_value(nullptr, "nm_s", v, sizeof(v)));
    CHECK(cookie_value("nm_s=", "nm_s", v, sizeof(v)));
    CHECK_STR(v, "");
    char tiny[4];
    CHECK(!cookie_value("nm_s=abcdef", "nm_s", tiny, sizeof(tiny)));
    CHECK(bearer_value("Bearer 0123", v, sizeof(v)));
    CHECK_STR(v, "0123");
    CHECK(bearer_value("  bearer   0123  ", v, sizeof(v)));
    CHECK_STR(v, "0123");
    CHECK(!bearer_value("Basic YWJj", v, sizeof(v)));
    CHECK(!bearer_value("Bearer", v, sizeof(v)));
    CHECK(!bearer_value("Bearer ", v, sizeof(v)));
    CHECK(!bearer_value("Bearerx 1", v, sizeof(v)));
    CHECK(!bearer_value(nullptr, v, sizeof(v)));
    CHECK(!bearer_value("Bearer abcdef", tiny, sizeof(tiny)));

    CHECK(next_path_ok("/"));
    CHECK(next_path_ok("/settings"));
    CHECK(next_path_ok("/?q=192.168.2.4"));
    CHECK(!next_path_ok("//evil.example/"));
    CHECK(!next_path_ok("/\\evil.example"));
    CHECK(!next_path_ok("https://evil.example/"));
    CHECK(!next_path_ok("settings"));
    CHECK(!next_path_ok(""));
    CHECK(!next_path_ok(nullptr));
    CHECK(!next_path_ok("/a\nb"));
}

static void test_auth_password() {
    CHECK(password_rule("12345678") == PasswordRule::Ok);
    CHECK(password_rule("1234567") == PasswordRule::TooShort);
    CHECK(password_rule("") == PasswordRule::TooShort);
    CHECK(password_rule(nullptr) == PasswordRule::TooShort);
    CHECK(password_rule(std::string(64, 'p').c_str()) == PasswordRule::Ok);
    CHECK(password_rule(std::string(65, 'p').c_str()) == PasswordRule::TooLong);
    CHECK(password_rule("tab\there!") == PasswordRule::Control);
    CHECK(password_rule("snowman \xe2\x98\x83 ok") == PasswordRule::Ok);
    CHECK(password_rule_text(PasswordRule::TooShort)[0] != '\0');
    CHECK_STR(password_rule_text(PasswordRule::Ok), "");

    uint8_t salt[16];
    for (int i = 0; i < 16; ++i) salt[i] = static_cast<uint8_t>(i);
    PasswordHash ph;
    password_make(ph, "correct horse battery", salt, kPasswordRounds);
    CHECK(ph.set);
    CHECK(ph.rounds == kPasswordRounds);
    CHECK_STR(hex_of(ph.hash, 32).c_str(), "25ec2e843d040853eb91d6ee9a96d626c48de1b5cb7eb4c549e71a3c990cb674");
    CHECK(password_matches(ph, "correct horse battery"));
    CHECK(!password_matches(ph, "correct horse batterY"));
    CHECK(!password_matches(ph, ""));
    CHECK(!password_matches(ph, nullptr));
    PasswordHash none{};
    CHECK(!password_matches(none, "correct horse battery"));
    PasswordHash silly = ph;
    silly.rounds = 0;
    CHECK(!password_matches(silly, "correct horse battery"));
}

static void test_auth_throttle() {
    LoginThrottle<3> t;
    t.clear();
    const uint32_t A = 0xC0A8021Bu, B = 0xC0A8021Cu;
    // Five wrong passwords are free.
    for (int i = 0; i < 5; ++i) {
        CHECK(t.wait_s(A, 100) == 0);
        t.failed(A, 100);
    }
    CHECK(t.wait_s(A, 100) == 0);
    t.failed(A, 100);                    // the sixth: 30 s
    CHECK(t.wait_s(A, 100) == 30);
    CHECK(t.wait_s(A, 129) == 1);
    CHECK(t.wait_s(A, 130) == 0);
    CHECK(t.wait_s(B, 100) == 0);        // somebody else is not held up
    t.failed(A, 130);                    // seventh: 60 s
    CHECK(t.wait_s(A, 130) == 60);
    for (int i = 0; i < 10; ++i) t.failed(A, 200);
    CHECK(t.wait_s(A, 200) == kLoginWaitMaxS);
    // The right password clears it.
    t.succeeded(A);
    CHECK(t.wait_s(A, 201) == 0);
    // A place quiet for an hour starts afresh.
    for (int i = 0; i < 7; ++i) t.failed(B, 300);
    CHECK(t.wait_s(B, 300) > 0);
    CHECK(t.wait_s(B, 300 + kLoginForgetS) == 0);
    t.failed(B, 300 + kLoginForgetS + 1);
    CHECK(t.wait_s(B, 300 + kLoginForgetS + 1) == 0);
    // Many places: past 30 wrong passwords in ten minutes, everyone waits.
    LoginThrottle<3> g;
    g.clear();
    for (uint32_t i = 0; i < kLoginGlobalMax; ++i) g.failed(1000 + i, 5000);
    CHECK(g.wait_s(99, 5000) == 0);
    g.failed(2000, 5001);
    CHECK(g.wait_s(99, 5001) == kLoginGlobalWaitS);
    CHECK(g.wait_s(99, 5001 + kLoginGlobalWaitS) == 0);
    // Spread out, they never add up to that.
    LoginThrottle<3> s;
    s.clear();
    for (uint32_t i = 0; i < 100; ++i) s.failed(3000 + i, 10000 + i * 30);
    CHECK(s.wait_s(77, 10000 + 100 * 30) == 0);
}

static void test_mac_rule() {
    Mac setup{}, m{};
    mac_parse("D4:E9:F4:A3:B8:AD", setup);
    CHECK(mac_rule("02:1A:2B:3C:4D:5E", setup, m) == MacRule::Ok);
    char out[18];
    mac_format(m, out);
    CHECK_STR(out, "02:1A:2B:3C:4D:5E");
    CHECK(mac_rule("d4e9f4a3b8ac", setup, m) == MacRule::Ok);    // a maker's address may be copied
    CHECK(mac_rule("01:00:5E:00:00:01", setup, m) == MacRule::Group);
    CHECK(mac_rule("FF:FF:FF:FF:FF:FF", setup, m) == MacRule::Group);
    CHECK(mac_rule("00:00:00:00:00:00", setup, m) == MacRule::Zero);
    CHECK(mac_rule("D4:E9:F4:A3:B8:AD", setup, m) == MacRule::SetupClash);
    CHECK(mac_rule("02:1A:2B:3C:4D", setup, m) == MacRule::Unreadable);
    CHECK(mac_rule("", setup, m) == MacRule::Unreadable);
    CHECK(mac_rule("zz:1A:2B:3C:4D:5E", setup, m) == MacRule::Unreadable);
    CHECK(mac_rule_text(MacRule::Group)[0] != '\0');
    CHECK_STR(mac_rule_text(MacRule::Ok), "");
    for (uint32_t i = 0; i < 64; ++i) {
        const Mac r = mac_random_local(i * 2654435761u, ~i * 40503u);
        CHECK((r.b[0] & 0x03) == 0x02);   // locally administered, one device
        char t[18];
        mac_format(r, t);
        Mac back{};
        CHECK(mac_rule(t, setup, back) == MacRule::Ok);
    }
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
    test_link_vectors();
    test_link_assembler();
    test_link_parse();
    test_link_arg();
    test_link_cut();
    test_link_pairing();
    test_link_pair_why();
    test_link_parse_auth();
    test_clock();
    test_http_date();
    test_report_slots();
    test_report_rows();
    test_report_lines();
    test_sha256();
    test_auth_sessions();
    test_auth_headers();
    test_auth_password();
    test_auth_throttle();
    test_mac_rule();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
