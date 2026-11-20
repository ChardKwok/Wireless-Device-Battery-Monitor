// Synthetic replies from the public Pelta/Delta II protocol, not hardware captures.
// Collection layout matches the owner's 0B05:1AFA Windows --dump.
#include "../src/drivers/drivers.h"

#include <cstdio>
#include <map>

namespace {
int failures = 0, opens = 0, controls = 0, writes = 0, reads = 0;
int controlDelay = 0, largestReadTimeout = 0;
bool openOk = true, controlOk = true, writeOk = true, repeat = false;
BYTE command = 0;
std::map<BYTE, std::vector<std::vector<BYTE>>> responses;
std::vector<BYTE> commands;
const char* current = "";

void Check(bool ok, const char* expr, int line) {
    if (!ok) { std::printf("FAIL %s:%d %s\n", current, line, expr); ++failures; }
}
#define CHECK(x) Check((x), #x, __LINE__)

HidColl Collection(USHORT pid = 0x1AFA) {
    HidColl c;
    c.vid = 0x0B05; c.pid = pid; c.iface = 3; c.path = L"rog-test";
    c.caps.UsagePage = 0xFF00; c.caps.Usage = 1;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 64;
    return c;
}
PollContext Context(USHORT pid = 0x1AFA) {
    opens = controls = writes = reads = controlDelay = largestReadTimeout = 0;
    openOk = controlOk = writeOk = true; repeat = false;
    command = 0; responses.clear(); commands.clear();
    PollContext ctx; ctx.colls = { Collection(pid) }; return ctx;
}
std::vector<BYTE> Battery(BYTE pct = 83) {
    std::vector<BYTE> r(64, 0);
    r[0] = 0xCC; r[1] = 0x12; r[2] = 7;
    r[5] = 10; r[6] = pct; r[7] = 20;
    return r;
}
std::vector<BYTE> Charging(BYTE value = 0) {
    auto r = Battery(0); r[2] = 8; r[5] = value; return r;
}
std::vector<BYTE> Offline() {
    auto r = Battery(); r[5] = 0xFF; r[6] = 0xAA; return r;
}
void Ready(BYTE pct = 83, BYTE charge = 0) {
    responses[7] = { Battery(pct) }; responses[8] = { Charging(charge) };
}
void Connected(PollContext& ctx, int pct, bool charge) {
    PollRog(ctx);
    CHECK(ctx.out.size() == 1 && ctx.IsClaimed(0x0B05, 0x1AFA));
    if (ctx.out.size() == 1) {
        const auto& s = ctx.out[0];
        CHECK(s.name == L"ROG DELTA II" && s.kind == DeviceKind::Headset);
        CHECK(s.key == L"rog:1AFA:hs" && s.via == L"2.4 GHz receiver");
        CHECK(s.percent == pct && s.connected && s.charging == charge);
    }
}
int Next(std::vector<BYTE>& out) {
    auto& queue = responses[command];
    if (queue.empty()) return -1;
    out = queue.front();
    if (!repeat) queue.erase(queue.begin());
    return static_cast<int>(out.size());
}
void Command(const std::vector<BYTE>& data) {
    CHECK(data.size() == 3);
    if (data.size() != 3) return;
    CHECK(data[0] == 0xCC && data[1] == 0x12 && (data[2] == 7 || data[2] == 8));
    command = data[2]; commands.push_back(command);  // only battery/charging GETs
}
}  // namespace

std::wstring Hex4(USHORT value) {
    wchar_t s[8]; std::swprintf(s, 8, L"%04X", value); return s;
}
bool HidDev::Open(const HidColl& c) { ++opens; c_ = c; return openOk; }
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>& data, int timeout) {
    ++writes; CHECK(timeout > 0 && timeout <= 250); Command(data); return writeOk;
}
int HidDev::SetOutputAndRead(const std::vector<BYTE>& data, std::vector<BYTE>& out, int timeout) {
    ++controls; CHECK(timeout > 0 && timeout <= 1200); Command(data);
    if (controlDelay) Sleep(controlDelay);
    return controlOk ? Next(out) : -1;
}
int HidDev::Read(std::vector<BYTE>& out, int timeout) {
    ++reads; CHECK(timeout > 0 && timeout <= 1200);
    if (timeout > largestReadTimeout) largestReadTimeout = timeout;
    return Next(out);
}

int main() {
    current = "percentage and charging, including real zero";
    for (BYTE pct : { BYTE(0), BYTE(1), BYTE(15), BYTE(83), BYTE(100) }) {
        for (bool charging : { false, true }) {
            auto ctx = Context(); Ready(pct, charging);
            Connected(ctx, pct, charging);
            CHECK(opens == 1 && controls == 2 && writes == 0);
            CHECK(commands == std::vector<BYTE>({7, 8}));
            PollRog(ctx); CHECK(opens == 1 && ctx.out.size() == 1);
        }
    }
    current = "all four owner-dump collections: use only FF00";
    auto ctx = Context(); Ready();
    auto vendor = Collection(); vendor.caps.UsagePage = 0xFF13;
    vendor.caps.InputReportByteLength = vendor.caps.OutputReportByteLength = 62;
    auto consumer = Collection(); consumer.caps.UsagePage = 0x000C;
    consumer.caps.InputReportByteLength = 2; consumer.caps.OutputReportByteLength = 0;
    auto phone = consumer; phone.caps.UsagePage = 0x000B;
    phone.caps.Usage = 5; phone.caps.OutputReportByteLength = 2;
    ctx.colls = {vendor, consumer, phone, Collection()};
    Connected(ctx, 83, false); CHECK(opens == 1);

    current = "strict identity and collection matching";
    for (int field = 0; field < 8; ++field) {
        ctx = Context(); auto& c = ctx.colls[0]; Ready();
        switch (field) {
            case 0: c.vid = 0x1532; break;
            case 1: c.pid = 0x1AFB; break;
            case 2: c.iface = 2; break;
            case 3: c.caps.UsagePage = 0xFF13; break;
            case 4: c.caps.Usage = 2; break;
            case 5: c.caps.InputReportByteLength = 63; break;
            case 6: c.caps.OutputReportByteLength = 63; break;
            case 7: c.caps.FeatureReportByteLength = 64; break;
        }
        PollRog(ctx); CHECK(ctx.out.empty() && ctx.claimed.empty() && opens == 0);
    }
    current = "receiver already claimed or unplugged";
    ctx = Context(); ctx.Claim(0x0B05, 0x1AFA); PollRog(ctx);
    CHECK(opens == 0 && ctx.out.empty());
    ctx = Context(); ctx.colls.clear(); PollRog(ctx); CHECK(opens == 0 && ctx.out.empty());

    current = "offline signatures hide device, no charging query";
    for (const auto& offline : { Offline(), std::vector<BYTE>{0xCC, 0xFF, 0xAA} }) {
        ctx = Context(); responses[7] = { offline }; PollRog(ctx);
        CHECK(ctx.out.size() == 1 && ctx.IsClaimed(0x0B05, 0x1AFA));
        if (ctx.out.size() == 1)
            CHECK(!ctx.out[0].connected && ctx.out[0].percent == -1 && !ctx.out[0].charging);
        CHECK(commands == std::vector<BYTE>({7}));
    }
    current = "power off between battery and charging replies";
    ctx = Context(); Ready(); auto offCharge = Offline(); offCharge[2] = 8;
    responses[8] = { offCharge }; PollRog(ctx);
    CHECK(ctx.out.size() == 1);
    if (ctx.out.size() == 1) CHECK(!ctx.out[0].connected && ctx.out[0].percent == -1);

    current = "reconnect returns stable identity and new level";
    ctx = Context(); Ready(57); Connected(ctx, 57, false);
    current = "unknown FE never displayed as zero";
    ctx = Context(); Ready(0xFE); Connected(ctx, -1, false);
    if (ctx.out.size() == 1) CHECK(ctx.out[0].asleep);
    current = "invalid battery values rejected";
    for (BYTE pct : { BYTE(101), BYTE(0xFD), BYTE(0xFF) }) {
        ctx = Context(); Ready(pct); PollRog(ctx);
        CHECK(ctx.out.empty() && ctx.IsClaimed(0x0B05, 0x1AFA));
    }

    current = "short, wrong report and unrelated command skipped";
    auto wrongId = Battery(); wrongId[0] = 0xCB;
    auto wrongCommand = Battery(); wrongCommand[1] = 0x13;
    auto event = Offline(); event[2] = 0x0E; // not a response to battery GET
    ctx = Context(); Ready(); responses[7] = {{}, {0xCC}, wrongId, wrongCommand, event, Battery()};
    Connected(ctx, 83, false);
    current = "unanswered and failed transport stay claimed";
    ctx = Context(); openOk = false; PollRog(ctx);
    CHECK(ctx.out.empty() && controls == 0 && ctx.IsClaimed(0x0B05, 0x1AFA));
    ctx = Context(); controlOk = writeOk = false; PollRog(ctx);
    CHECK(ctx.out.empty() && controls == 1 && writes == 1 && ctx.IsClaimed(0x0B05, 0x1AFA));
    ctx = Context(); PollRog(ctx);
    CHECK(ctx.out.empty() && ctx.IsClaimed(0x0B05, 0x1AFA));

    current = "interrupt OUT fallback uses same read-only queries";
    ctx = Context(); Ready(83, 1); controlOk = false;
    Connected(ctx, 83, true); CHECK(writes == 2 && controls == 2);
    current = "charging failure does not discard valid battery";
    ctx = Context(); responses[7] = { Battery() }; Connected(ctx, 83, false);
    current = "fallback does not reset absolute budget";
    ctx = Context(); Ready(); controlOk = false; controlDelay = 150;
    Connected(ctx, 83, false);
    CHECK(largestReadTimeout > 0 && largestReadTimeout <= 900);
    current = "continuous events bounded by report cap";
    ctx = Context(); responses[7] = { event }; repeat = true;
    auto start = GetTickCount64(); PollRog(ctx);
    CHECK(ctx.out.empty() && reads <= 128 && GetTickCount64() - start < 1300);

    current = "Pelta regression: connected and asleep behavior preserved";
    ctx = Context(0x1B84); Ready(71, 1); PollRog(ctx);
    CHECK(ctx.out.size() == 1 && writes == 0);
    if (ctx.out.size() == 1)
        CHECK(ctx.out[0].name == L"ROG Pelta" && ctx.out[0].percent == 71 && ctx.out[0].charging);
    ctx = Context(0x1B84); responses[7] = { Offline() }; PollRog(ctx);
    CHECK(ctx.out.size() == 1);
    if (ctx.out.size() == 1) CHECK(ctx.out[0].connected && ctx.out[0].asleep);
    current = "Pelta and Delta II coexist";
    ctx = Context(); ctx.colls.push_back(Collection(0x1B84));
    responses[7] = { Battery(71), Battery(83) }; responses[8] = {Charging(), Charging(1)};
    PollRog(ctx); CHECK(ctx.out.size() == 2 && opens == 2);
    CHECK(ctx.IsClaimed(0x0B05, 0x1B84) && ctx.IsClaimed(0x0B05, 0x1AFA));

    if (failures) { std::printf("ROG headsets: %d failed checks\n", failures); return 1; }
    std::puts("ROG headset tests passed (Delta II, Pelta, strict matching, battery, charging, offline, fallback, deadlines).");
}
