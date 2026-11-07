// Host-side unit tests for DriverKit/Core. Build & run: make -C DriverKit/Tests
#include "../Core/XboxProtocol.h"

#include <stdio.h>
#include <string.h>

using namespace xbox;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void Put16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }

static void Make360(uint8_t *b, uint16_t buttons, int16_t lx, int16_t ly, int16_t rx, int16_t ry, uint8_t lt = 0, uint8_t rt = 0)
{
    memset(b, 0, 20);
    b[0] = 0x00; b[1] = 0x14;
    Put16(b + 2, buttons); b[4] = lt; b[5] = rt;
    Put16(b + 6, lx); Put16(b + 8, ly); Put16(b + 10, rx); Put16(b + 12, ry);
}

static void TestDescriptor()
{
    CHECK(kReportDescriptorSize == 188);
    CHECK(kReportDescriptor[0] == 0x05 && kReportDescriptor[3] == 0x05);   // GD / Game Pad
    CHECK(kReportDescriptor[kReportDescriptorSize - 1] == 0xc0);
}

static void TestClassify()
{
    ControllerType t;
    CHECK(ClassifyInterface(0xFF, 93, 1, t) && t == ControllerType::Xbox360);
    CHECK(ClassifyInterface(0x58, 66, 0, t) && t == ControllerType::XboxOriginal);
    CHECK(ClassifyInterface(0xFF, 71, 208, t) && t == ControllerType::XboxOne);
    CHECK(!ClassifyInterface(0xFF, 93, 2, t));    // chatpad / headset
    CHECK(!ClassifyInterface(0x03, 71, 208, t));
}

static void Test360Basic()
{
    InputProcessor p(ControllerType::Xbox360);
    uint8_t b[32]; Report360 r; OutPacket reply;
    Make360(b, kBtnA | kBtnUp, 1000, 2000, -3000, -32768, 10, 200);
    CHECK(p.Process(b, 20, r, reply));
    CHECK(reply.length == 0);
    CHECK(r.command == 0 && r.size == 20);
    CHECK(r.buttons == (kBtnA | kBtnUp));
    CHECK(r.trigL == 10 && r.trigR == 200);
    CHECK(r.left.x == 1000 && r.left.y == ~2000);       // Y flipped for HID
    CHECK(r.right.x == -3000 && r.right.y == 32767);     // ~(-32768)

    // Non-input packets (e.g. LED status 01 03 xx) are not published
    uint8_t led[3] = { 0x01, 0x03, 0x06 };
    CHECK(!p.Process(led, 3, r, reply));
    CHECK(!p.Process(b, 19, r, reply));
}

static void TestDeadzones()
{
    InputProcessor p(ControllerType::Xbox360);
    XboxSettings s; DefaultSettings(s);
    s.deadzoneLeft = 8000;
    s.deadzoneRight = 8000;
    s.flags = kXboxFlagInvertLeftY | kXboxFlagInvertRightY;  // disable flip to keep numbers simple
    p.SetSettings(s);

    Stick l { 7000, 9000 }, r { 7000, 9000 };
    p.ApplyStickSettings(l, r);
    CHECK(l.x == 0 && l.y == 9000);          // per-axis

    s.flags |= kXboxFlagRelativeLeft;
    p.SetSettings(s);
    l = { 7000, 7999 }; r = { 0, 0 };
    p.ApplyStickSettings(l, r);
    CHECK(l.x == 0 && l.y == 0);             // linked: both inside
    l = { 7000, 9000 };
    p.ApplyStickSettings(l, r);
    CHECK(l.x == 7000 && l.y == 9000);       // linked: one outside keeps both

    s.flags |= kXboxFlagDeadOffRight;        // normalise right only
    p.SetSettings(s);
    l = { 9000, 0 }; r = { 32767, -32768 };
    p.ApplyStickSettings(l, r);
    CHECK(l.x == 9000);                      // left untouched (kext bug fixed)
    CHECK(r.x == 32767 && r.y == ~32767);
    r = { 8000 + (32767 - 8000) / 2, 0 };
    p.ApplyStickSettings(l, r);
    CHECK(r.x == 16382);   // 32767 * 12383 / 24767, truncated
}

static void TestRemapAndSwap()
{
    InputProcessor p(ControllerType::Xbox360);
    XboxSettings s; DefaultSettings(s);
    CHECK(IsIdentityMapping(s));
    // swap A and B
    s.mapping[kXboxButtonA] = 13; s.mapping[kXboxButtonB] = 12;
    s.flags = kXboxFlagSwapSticks;
    p.SetSettings(s);
    uint8_t b[20]; Report360 r; OutPacket reply;
    Make360(b, kBtnA | kBtnStart, 100, 0, 200, 0);
    CHECK(p.Process(b, 20, r, reply));
    CHECK(r.buttons == (kBtnB | kBtnStart));
    CHECK(r.left.x == 200 && r.right.x == 100);
}

static void TestSanitize()
{
    XboxSettings s; DefaultSettings(s);
    s.deadzoneLeft = 40000; s.rumbleType = 9; s.mapping[3] = 77;
    CHECK(SanitizeSettings(s));
    CHECK(s.deadzoneLeft == 32766 && s.rumbleType == 0 && s.mapping[3] == 3);
    s.version = 99;
    CHECK(!SanitizeSettings(s));
}

static void TestXboxOne()
{
    InputProcessor p(ControllerType::XboxOne);
    uint8_t b[64] = {}; Report360 r; OutPacket reply;
    b[0] = 0x20; b[1] = 0x00; b[2] = 0x05; b[3] = 0x0e;
    Put16(b + 4, 0x0010 | 0x0100 | 0x2000 | 0x0004);  // A, dpad up, RB, menu
    Put16(b + 6, 1023); Put16(b + 8, 512);
    Put16(b + 10, 1234); Put16(b + 12, 0); Put16(b + 14, -5); Put16(b + 16, 100);
    CHECK(p.Process(b, 18, r, reply));
    CHECK(r.buttons == (kBtnA | kBtnUp | kBtnRB | kBtnStart));
    CHECK(r.trigL == 255 && r.trigR == 127);
    CHECK(r.left.x == 1234 && r.left.y == ~0 && r.right.x == -5 && r.right.y == ~100);

    // Guide press on a 2016 pad: published from last report + ack queued
    uint8_t g[6] = { 0x07, 0x30, 0x42, 0x02, 0x01, 0x5b };
    CHECK(p.Process(g, 6, r, reply));
    CHECK(r.buttons == (kBtnA | kBtnUp | kBtnRB | kBtnStart | kBtnGuide));
    CHECK(reply.length == 13 && reply.bytes[0] == 0x01 && reply.bytes[2] == 0x42);
    // Guide stays held through the next input report
    CHECK(p.Process(b, 18, r, reply));
    CHECK((r.buttons & kBtnGuide) && reply.length == 0);
    g[4] = 0; g[1] = 0x20;
    CHECK(p.Process(g, 6, r, reply));
    CHECK(!(r.buttons & kBtnGuide) && reply.length == 0);

    // Other GIP traffic (heartbeat 0x03, announce 0x02) ignored
    uint8_t hb[8] = { 0x03, 0x20, 0x01, 0x04 };
    CHECK(!p.Process(hb, 8, r, reply));

    // Racing wheel
    memset(b, 0, sizeof(b));
    b[0] = 0x20; b[3] = 0x11;
    Put16(b + 6, 32768 + 100); Put16(b + 8, 1023); Put16(b + 10, 0); b[12] = 255;
    CHECK(p.Process(b, 21, r, reply));
    CHECK(r.left.x == 100 && r.trigR == 255 && r.trigL == 0);
    CHECK(r.left.y == static_cast<int16_t>(~(255 * 128)));
}

static void TestOriginal()
{
    InputProcessor p(ControllerType::XboxOriginal);
    uint8_t b[20] = {}; Report360 r; OutPacket reply;
    b[0] = 0x00; b[1] = 0x14; b[2] = 0x01 | 0x10;   // up + start
    b[4] = 200; b[9] = 1;                           // A analog, white
    b[10] = 7; b[11] = 8;
    Put16(b + 12, 300); Put16(b + 18, -300);
    CHECK(p.Process(b, 20, r, reply));
    CHECK(r.buttons == (kBtnUp | kBtnStart | kBtnA | kBtnLB));
    CHECK(r.trigL == 7 && r.trigR == 8 && r.left.x == 300 && r.right.y == ~(-300));
    CHECK(p.Process(b, 20, r, reply));    // duplicate passes
    CHECK(!p.Process(b, 20, r, reply));   // triplicate dropped
    b[4] = 0;
    CHECK(p.Process(b, 20, r, reply));
}

static void TestOutput()
{
    OutputBuilder o; OutPacket out;
    XboxSettings s; DefaultSettings(s);

    uint8_t rumble[4] = { 0x00, 0x04, 0x80, 0x40 };
    CHECK(o.Translate(ControllerType::Xbox360, s, rumble, 4, out) == CommandResult::Send);
    CHECK(out.length == 8 && out.bytes[0] == 0 && out.bytes[1] == 8 && out.bytes[3] == 0x80 && out.bytes[4] == 0x40);

    uint8_t led[3] = { 0x01, 0x03, 0x06 };
    CHECK(o.Translate(ControllerType::Xbox360, s, led, 3, out) == CommandResult::Send);
    CHECK(out.length == 3 && out.bytes[2] == 0x06);
    CHECK(o.Translate(ControllerType::Xbox360, s, led, 2, out) == CommandResult::Unsupported);
    CHECK(o.Translate(ControllerType::XboxOriginal, s, led, 3, out) == CommandResult::Ignored);

    CHECK(o.Translate(ControllerType::XboxOriginal, s, rumble, 4, out) == CommandResult::Send);
    CHECK(out.length == 6 && out.bytes[1] == 6 && out.bytes[3] == 0x80 && out.bytes[5] == 0x40);

    CHECK(o.Translate(ControllerType::XboxOne, s, rumble, 4, out) == CommandResult::Send);
    CHECK(out.length == 13 && out.bytes[0] == 0x09 && out.bytes[2] == 6 && out.bytes[8] == 0x80 && out.bytes[9] == 0x40);
    CHECK(o.Translate(ControllerType::XboxOne, s, rumble, 4, out) == CommandResult::Send);
    CHECK(out.bytes[2] == 7);   // sequence counter advances

    s.rumbleType = kXboxRumbleTriggers;
    CHECK(o.Translate(ControllerType::XboxOne, s, rumble, 4, out) == CommandResult::Send);
    CHECK(out.bytes[6] == 0x40 && out.bytes[7] == 0x20 && out.bytes[8] == 0 && out.bytes[9] == 0);

    s.rumbleType = kXboxRumbleNone;
    CHECK(o.Translate(ControllerType::Xbox360, s, rumble, 4, out) == CommandResult::Ignored);
    CHECK(o.Translate(ControllerType::XboxOne, s, rumble, 4, out) == CommandResult::Ignored);

    uint8_t bogus[2] = { 0x07, 0x02 };
    s.rumbleType = 0;
    CHECK(o.Translate(ControllerType::Xbox360, s, bogus, 2, out) == CommandResult::Unsupported);
}

static void TestStartup()
{
    OutPacket pk[4];
    CHECK(BuildStartupPackets(ControllerType::XboxOne, pk, 4) == 4);
    CHECK(pk[1].length == 5 && pk[1].bytes[0] == 0x05);
    CHECK(BuildStartupPackets(ControllerType::Xbox360, pk, 4) == 1);
    CHECK(pk[0].length == 3 && pk[0].bytes[0] == 0x01 && pk[0].bytes[2] == 0x00);
    CHECK(BuildStartupPackets(ControllerType::XboxOriginal, pk, 4) == 0);
    CHECK(BuildStartupPackets(ControllerType::XboxOne, pk, 2) == 2);
}

static void TestReset()
{
    // Simulate the dext: zeroed storage, then Reset().
    alignas(InputProcessor) unsigned char raw[sizeof(InputProcessor)] = {};
    InputProcessor &p = *reinterpret_cast<InputProcessor *>(raw);
    p.Reset(ControllerType::XboxOne);
    CHECK(p.Type() == ControllerType::XboxOne);
    CHECK(p.Settings().version == kXboxSettingsVersion);
    CHECK(IsIdentityMapping(p.Settings()));

    alignas(OutputBuilder) unsigned char rawOut[sizeof(OutputBuilder)] = {};
    OutputBuilder &o = *reinterpret_cast<OutputBuilder *>(rawOut);
    o.Reset();
    XboxSettings s; DefaultSettings(s);
    uint8_t rumble[4] = { 0x00, 0x04, 1, 2 };
    OutPacket out;
    CHECK(o.Translate(ControllerType::XboxOne, s, rumble, 4, out) == CommandResult::Send);
    CHECK(out.bytes[2] == 6);
}

int main()
{
    TestReset();
    TestDescriptor(); TestClassify(); Test360Basic(); TestDeadzones();
    TestRemapAndSwap(); TestSanitize(); TestXboxOne(); TestOriginal();
    TestOutput(); TestStartup();
    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("all tests passed\n");
    return 0;
}
