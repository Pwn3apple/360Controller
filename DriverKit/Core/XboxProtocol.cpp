/*
 Xbox controller DriverKit port - wire protocol and report processing.
 See XboxProtocol.h. GPL v2 or later, see Licence.txt.
 */
#include "XboxProtocol.h"

#include <string.h>

namespace xbox {

// ---------------------------------------------------------------------------
// HID report descriptor (unchanged from 360Controller/xbox360hid.h so games
// that already have mappings for this driver keep working).
// ---------------------------------------------------------------------------

const uint8_t kReportDescriptor[] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x05,                    // USAGE (Game Pad)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x05, 0x01,                    //   USAGE_PAGE (Generic Desktop)
    0x09, 0x3a,                    //   USAGE (Counted Buffer)
    0xa1, 0x02,                    //   COLLECTION (Logical)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x3f,                    //     USAGE (Reserved)
    0x09, 0x3b,                    //     USAGE (Byte Count)
    0x81, 0x01,                    //     INPUT (Cnst,Ary,Abs)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x35, 0x00,                    //     PHYSICAL_MINIMUM (0)
    0x45, 0x01,                    //     PHYSICAL_MAXIMUM (1)
    0x95, 0x04,                    //     REPORT_COUNT (4)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x0c,                    //     USAGE_MINIMUM (Button 12)
    0x29, 0x0f,                    //     USAGE_MAXIMUM (Button 15)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x35, 0x00,                    //     PHYSICAL_MINIMUM (0)
    0x45, 0x01,                    //     PHYSICAL_MAXIMUM (1)
    0x95, 0x04,                    //     REPORT_COUNT (4)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x09, 0x09,                    //     USAGE (Button 9)
    0x09, 0x0a,                    //     USAGE (Button 10)
    0x09, 0x07,                    //     USAGE (Button 7)
    0x09, 0x08,                    //     USAGE (Button 8)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x35, 0x00,                    //     PHYSICAL_MINIMUM (0)
    0x45, 0x01,                    //     PHYSICAL_MAXIMUM (1)
    0x95, 0x03,                    //     REPORT_COUNT (3)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x09, 0x05,                    //     USAGE (Button 5)
    0x09, 0x06,                    //     USAGE (Button 6)
    0x09, 0x0b,                    //     USAGE (Button 11)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x81, 0x01,                    //     INPUT (Cnst,Ary,Abs)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x35, 0x00,                    //     PHYSICAL_MINIMUM (0)
    0x45, 0x01,                    //     PHYSICAL_MAXIMUM (1)
    0x95, 0x04,                    //     REPORT_COUNT (4)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x04,                    //     USAGE_MAXIMUM (Button 4)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,              //     LOGICAL_MAXIMUM (255)
    0x35, 0x00,                    //     PHYSICAL_MINIMUM (0)
    0x46, 0xff, 0x00,              //     PHYSICAL_MAXIMUM (255)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x32,                    //     USAGE (Z)
    0x09, 0x35,                    //     USAGE (Rz)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x75, 0x10,                    //     REPORT_SIZE (16)
    0x16, 0x00, 0x80,              //     LOGICAL_MINIMUM (-32768)
    0x26, 0xff, 0x7f,              //     LOGICAL_MAXIMUM (32767)
    0x36, 0x00, 0x80,              //     PHYSICAL_MINIMUM (-32768)
    0x46, 0xff, 0x7f,              //     PHYSICAL_MAXIMUM (32767)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x01,                    //     USAGE (Pointer)
    0xa1, 0x00,                    //     COLLECTION (Physical)
    0x95, 0x02,                    //       REPORT_COUNT (2)
    0x05, 0x01,                    //       USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //       USAGE (X)
    0x09, 0x31,                    //       USAGE (Y)
    0x81, 0x02,                    //       INPUT (Data,Var,Abs)
    0xc0,                          //     END_COLLECTION
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x01,                    //     USAGE (Pointer)
    0xa1, 0x00,                    //     COLLECTION (Physical)
    0x95, 0x02,                    //       REPORT_COUNT (2)
    0x05, 0x01,                    //       USAGE_PAGE (Generic Desktop)
    0x09, 0x33,                    //       USAGE (Rx)
    0x09, 0x34,                    //       USAGE (Ry)
    0x81, 0x02,                    //       INPUT (Data,Var,Abs)
    0xc0,                          //     END_COLLECTION
    0xc0,                          //   END_COLLECTION
    0xc0                           // END_COLLECTION
};
const size_t kReportDescriptorSize = sizeof(kReportDescriptor);

// ---------------------------------------------------------------------------
// Small helpers. Reads go through memcpy so unaligned USB buffers are safe.
// ---------------------------------------------------------------------------

namespace {

inline uint16_t ReadU16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }  // LE host
inline int16_t  ReadS16(const uint8_t *p) { int16_t v;  memcpy(&v, p, 2); return v; }

// |value| using one's complement, as the kext did, so -32768 maps to 32767
// instead of overflowing.
inline int16_t Magnitude(int16_t v) { return v < 0 ? static_cast<int16_t>(~v) : v; }

inline int16_t Flip(int16_t v) { return static_cast<int16_t>(~v); }

// Rescale an axis so that the edge of the deadzone becomes 0.
int16_t NormalizeAxis(int16_t axis, uint16_t deadzone)
{
    const int32_t max16 = 32767;
    const int32_t current = Magnitude(axis);
    const int32_t range = max16 - deadzone;   // deadzone is clamped to < 32767
    if (current <= deadzone || range <= 0)
        return 0;
    const int16_t scaled = static_cast<int16_t>((max16 * (current - deadzone)) / range);
    return axis < 0 ? Flip(scaled) : scaled;
}

void ApplyDeadzone(Stick &s, uint16_t deadzone, bool linked, bool normalize)
{
    if (deadzone == 0)
        return;
    if (linked) {
        if (Magnitude(s.x) < deadzone && Magnitude(s.y) < deadzone) {
            s.x = s.y = 0;
        } else if (normalize) {
            s.x = NormalizeAxis(s.x, deadzone);
            s.y = NormalizeAxis(s.y, deadzone);
        }
    } else {
        if (Magnitude(s.x) < deadzone)  s.x = 0;
        else if (normalize)             s.x = NormalizeAxis(s.x, deadzone);
        if (Magnitude(s.y) < deadzone)  s.y = 0;
        else if (normalize)             s.y = NormalizeAxis(s.y, deadzone);
    }
}

inline uint8_t ScaleTrigger10To8(uint16_t v)
{
    uint32_t scaled = (static_cast<uint32_t>(v) * 255u) / 1023u;
    return scaled > 255u ? 255u : static_cast<uint8_t>(scaled);
}

// Logical button index -> source bit in Report360::buttons (bit 11 skipped).
inline unsigned SourceBit(unsigned logical) { return logical < 11 ? logical : logical + 1; }

template <size_t N>
void PutArray(OutPacket &p, const uint8_t (&bytes)[N])
{
    static_assert(N <= kMaxOutPacket, "packet too large");
    memcpy(p.bytes, bytes, N);
    p.length = N;
}
} // namespace

// ---------------------------------------------------------------------------
// Interface classification
// ---------------------------------------------------------------------------

bool ClassifyInterface(uint8_t cls, uint8_t sub, uint8_t proto, ControllerType &type)
{
    if (Matches(kIface360Pad, cls, sub, proto))   { type = ControllerType::Xbox360;      return true; }
    if (Matches(kIfaceOriginal, cls, sub, proto)) { type = ControllerType::XboxOriginal; return true; }
    if (Matches(kIfaceXboxOne, cls, sub, proto))  { type = ControllerType::XboxOne;      return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void DefaultSettings(XboxSettings &s)
{
    memset(&s, 0, sizeof(s));
    s.version = kXboxSettingsVersion;
    s.rumbleType = kXboxRumbleDefault;
    for (unsigned i = 0; i < kXboxButtonCount; i++)
        s.mapping[i] = static_cast<uint8_t>(SourceBit(i));
}

bool SanitizeSettings(XboxSettings &s)
{
    if (s.version != kXboxSettingsVersion)
        return false;
    if (s.deadzoneLeft > 32766)  s.deadzoneLeft = 32766;
    if (s.deadzoneRight > 32766) s.deadzoneRight = 32766;
    if (s.rumbleType > kXboxRumbleBoth) s.rumbleType = kXboxRumbleDefault;
    for (unsigned i = 0; i < kXboxButtonCount; i++)
        if (s.mapping[i] > 15)
            s.mapping[i] = static_cast<uint8_t>(SourceBit(i));
    return true;
}

bool IsIdentityMapping(const XboxSettings &s)
{
    for (unsigned i = 0; i < kXboxButtonCount; i++)
        if (s.mapping[i] != SourceBit(i))
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// Startup packets
// ---------------------------------------------------------------------------

size_t BuildStartupPackets(ControllerType type, OutPacket *out, size_t max)
{
    size_t n = 0;
    auto add = [&](const auto &bytes) { if (n < max) PutArray(out[n++], bytes); };

    switch (type) {
        case ControllerType::XboxOne: {
            static const uint8_t init0[] = { 0x01, 0x20, 0x00, 0x09, 0x00, 0x04, 0x20, 0x3a, 0x00, 0x00, 0x00, 0x80, 0x00 };
            static const uint8_t init1[] = { 0x05, 0x20, 0x00, 0x01, 0x00 };  // power on
            static const uint8_t init2[] = { 0x09, 0x00, 0x00, 0x09, 0x00, 0x0F, 0x00, 0x00, 0x1D, 0x1D, 0xFF, 0x00, 0x00 };
            static const uint8_t init3[] = { 0x09, 0x00, 0x00, 0x09, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
            add(init0); add(init1); add(init2); add(init3);
            break;
        }
        case ControllerType::Xbox360: {
            static const uint8_t ledOff[] = { 0x01, 0x03, 0x00 };
            add(ledOff);
            break;
        }
        case ControllerType::XboxOriginal:
            // The kext also sent the 360 "LED off" packet here; the original
            // controller has no LEDs, so nothing is sent.
            break;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Input processing
// ---------------------------------------------------------------------------

InputProcessor::InputProcessor(ControllerType type)
{
    Reset(type);
}

void InputProcessor::Reset(ControllerType type)
{
    type_ = type;
    XboxSettings s;
    DefaultSettings(s);
    SetSettings(s);
    guidePressed_ = false;
    memset(&last_, 0, sizeof(last_));
    memset(lastRaw_, 0, sizeof(lastRaw_));
    repeatCount_ = 0;
}

void InputProcessor::SetSettings(const XboxSettings &s)
{
    settings_ = s;
    identityMapping_ = IsIdentityMapping(s);
}

void InputProcessor::ApplyStickSettings(Stick &left, Stick &right) const
{
    const uint16_t f = settings_.flags;
    // HID wants +Y = down, the controller sends +Y = up, so Y is flipped
    // unless the user asked to invert it.
    if (f & kXboxFlagInvertLeftX)    left.x = Flip(left.x);
    if (!(f & kXboxFlagInvertLeftY)) left.y = Flip(left.y);
    if (f & kXboxFlagInvertRightX)    right.x = Flip(right.x);
    if (!(f & kXboxFlagInvertRightY)) right.y = Flip(right.y);

    ApplyDeadzone(left, settings_.deadzoneLeft,
                  f & kXboxFlagRelativeLeft, f & kXboxFlagDeadOffLeft);
    // Note: the kext normalised the *left* stick here by mistake.
    ApplyDeadzone(right, settings_.deadzoneRight,
                  f & kXboxFlagRelativeRight, f & kXboxFlagDeadOffRight);
}

uint16_t InputProcessor::RemapButtons(uint16_t buttons) const
{
    uint16_t out = 0;
    for (unsigned i = 0; i < kXboxButtonCount; i++)
        if (buttons & (1u << SourceBit(i)))
            out |= static_cast<uint16_t>(1u << settings_.mapping[i]);
    return out;
}

void InputProcessor::FinishReport(Report360 &r) const
{
    r.command = 0x00;
    r.size = sizeof(Report360);
    ApplyStickSettings(r.left, r.right);
    if (!identityMapping_)
        r.buttons = RemapButtons(r.buttons);
    if (settings_.flags & kXboxFlagSwapSticks) {
        Stick t = r.left; r.left = r.right; r.right = t;
    }
}

bool InputProcessor::Process(const uint8_t *data, size_t length, Report360 &out, OutPacket &reply)
{
    reply.length = 0;
    if (data == nullptr)
        return false;
    switch (type_) {
        case ControllerType::Xbox360:      return Process360(data, length, out);
        case ControllerType::XboxOriginal: return ProcessOriginal(data, length, out);
        case ControllerType::XboxOne:      return ProcessXboxOne(data, length, out, reply);
    }
    return false;
}

bool InputProcessor::Process360(const uint8_t *data, size_t length, Report360 &out)
{
    if (length < sizeof(Report360) || data[0] != 0x00 || data[1] != sizeof(Report360))
        return false;   // LED status, attach notifications, etc.
    memcpy(&out, data, sizeof(Report360));
    FinishReport(out);
    return true;
}

// Original Xbox report layout (see xboxdrv's xbox_controller.cpp):
//  0 cmd, 1 size(0x14), 2 buttons, 3 reserved, 4..9 analog A B X Y Black White,
//  10 trigL, 11 trigR, 12 xL, 14 yL, 16 xR, 18 yR
bool InputProcessor::ProcessOriginal(const uint8_t *data, size_t length, Report360 &out)
{
    if (length < 20 || data[0] != 0x00 || data[1] != 0x14)
        return false;

    // Drop triplicate reports, as the kext did.
    if (memcmp(data, lastRaw_, sizeof(lastRaw_)) == 0) {
        if (++repeatCount_ > 1)
            return false;
    } else {
        repeatCount_ = 0;
    }
    memcpy(lastRaw_, data, sizeof(lastRaw_));

    memset(&out, 0, sizeof(out));
    uint16_t buttons = data[2];               // d-pad, start, back, thumbs: same bits as 360
    if (data[4]) buttons |= kBtnA;
    if (data[5]) buttons |= kBtnB;
    if (data[6]) buttons |= kBtnX;
    if (data[7]) buttons |= kBtnY;
    if (data[8]) buttons |= kBtnRB;           // black
    if (data[9]) buttons |= kBtnLB;           // white
    out.buttons = buttons;
    out.trigL = data[10];
    out.trigR = data[11];
    out.left  = { ReadS16(data + 12), ReadS16(data + 14) };
    out.right = { ReadS16(data + 16), ReadS16(data + 18) };
    FinishReport(out);
    return true;
}

// Xbox One (GIP) input. Header: 0 command, 1 flags, 2 sequence, 3 payload size.
bool InputProcessor::ProcessXboxOne(const uint8_t *data, size_t length, Report360 &out, OutPacket &reply)
{
    if (length < 4)
        return false;
    const uint8_t command = data[0];
    const uint8_t payload = data[3];

    if (command == 0x07 && payload == 2 && length >= 6) {
        // Guide button. Newer (2016+) pads want an acknowledgement.
        if (data[1] == 0x30) {
            const uint8_t ack[] = { 0x01, 0x20, data[2], 0x09, 0x00, 0x07, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };
            PutArray(reply, ack);
        }
        guidePressed_ = data[4] != 0;
        out = last_;
        out.command = 0x00;
        out.size = sizeof(Report360);
        const uint16_t bit = static_cast<uint16_t>(1u << settings_.mapping[kXboxButtonGuide]);
        out.buttons = guidePressed_ ? (out.buttons | bit) : (out.buttons & ~bit);
        last_ = out;
        return true;
    }

    if (command != 0x20 || length < 18)
        return false;

    // GIP button bits -> Report360 bits
    static const struct { uint16_t from, to; } kButtonMap[] = {
        { 0x0004, kBtnStart }, { 0x0008, kBtnBack },
        { 0x0010, kBtnA }, { 0x0020, kBtnB }, { 0x0040, kBtnX }, { 0x0080, kBtnY },
        { 0x0100, kBtnUp }, { 0x0200, kBtnDown }, { 0x0400, kBtnLeft }, { 0x0800, kBtnRight },
        { 0x1000, kBtnLB }, { 0x2000, kBtnRB }, { 0x4000, kBtnLS }, { 0x8000, kBtnRS },
    };
    const uint16_t raw = ReadU16(data + 4);
    uint16_t buttons = guidePressed_ ? kBtnGuide : 0;
    for (const auto &m : kButtonMap)
        if (raw & m.from) buttons |= m.to;

    memset(&out, 0, sizeof(out));
    out.buttons = buttons;

    if (payload == 0x1a && length >= 24) {
        // Fight stick: triggers are digital. Same byte the kext tested.
        if (data[22] & 0x80) out.trigL = 255;
        if (data[22] & 0x40) out.trigR = 255;
        out.left  = { ReadS16(data + 10), ReadS16(data + 12) };
        out.right = { ReadS16(data + 14), ReadS16(data + 16) };
    } else if (payload == 0x11) {
        // Racing wheel: steering u16 @6, accelerator @8, brake @10, clutch u8 @12
        out.trigR = ScaleTrigger10To8(ReadU16(data + 8));
        out.trigL = ScaleTrigger10To8(ReadU16(data + 10));
        out.left.x = static_cast<int16_t>(ReadU16(data + 6) - 32768);
        out.left.y = static_cast<int16_t>(data[12] * 128);
    } else {
        out.trigL = ScaleTrigger10To8(ReadU16(data + 6));
        out.trigR = ScaleTrigger10To8(ReadU16(data + 8));
        out.left  = { ReadS16(data + 10), ReadS16(data + 12) };
        out.right = { ReadS16(data + 14), ReadS16(data + 16) };
    }

    FinishReport(out);
    last_ = out;
    return true;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

CommandResult OutputBuilder::Translate(ControllerType type, const XboxSettings &s,
                                       const uint8_t *data, size_t length, OutPacket &out)
{
    out.length = 0;
    if (data == nullptr || length < 2)
        return CommandResult::Unsupported;

    const uint8_t command = data[0];
    const bool lengthOk = (data[1] == length);

    if (type == ControllerType::XboxOne) {
        if (command == 0x01) return CommandResult::Ignored;   // LED not supported
        if (command != 0x00 || length < 4) return CommandResult::Unsupported;
        if (s.rumbleType == kXboxRumbleNone) return CommandResult::Ignored;

        // The kext treats byte 2 as the light (right) motor and byte 3 as the
        // heavy one for Xbox One pads; kept for compatibility with the
        // force-feedback plugin.
        const uint8_t a = data[2], b = data[3];
        uint8_t trigL = 0, trigR = 0, little = 0, big = 0;
        switch (s.rumbleType) {
            case kXboxRumbleTriggers: trigL = a / 2; trigR = b / 2; break;
            case kXboxRumbleBoth:     trigL = a / 2; trigR = b / 2; little = a; big = b; break;
            default:                  little = a; big = b; break;
        }
        const uint8_t pkt[] = { 0x09, 0x00, xboxOneCounter_++, 0x09,
                                0x00, 0x0F, trigL, trigR, little, big, 0xFF, 0x00, 0x00 };
        PutArray(out, pkt);
        return CommandResult::Send;
    }

    // Xbox 360 and original Xbox
    if (s.rumbleType == kXboxRumbleNone)
        return CommandResult::Ignored;

    if (command == 0x00) {
        if (!lengthOk || length != 4) return CommandResult::Unsupported;
        if (type == ControllerType::XboxOriginal) {
            const uint8_t pkt[] = { 0x00, 0x06, 0x00, data[2], 0x00, data[3] };
            PutArray(out, pkt);
        } else {
            const uint8_t pkt[] = { 0x00, 0x08, 0x00, data[2], data[3], 0x00, 0x00, 0x00 };
            PutArray(out, pkt);
        }
        return CommandResult::Send;
    }
    if (command == 0x01) {
        if (!lengthOk || length != 3) return CommandResult::Unsupported;
        if (type == ControllerType::XboxOriginal) return CommandResult::Ignored;
        const uint8_t pkt[] = { 0x01, 0x03, data[2] };
        PutArray(out, pkt);
        return CommandResult::Send;
    }
    return CommandResult::Unsupported;
}

} // namespace xbox
