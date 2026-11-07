/*
 Xbox controller DriverKit port - wire protocol and report processing.

 Everything in here is pure, allocation-free C++ with no IOKit or DriverKit
 dependency, so the exact same code runs inside the dext and in the host
 unit tests (DriverKit/Tests). All USB/HID plumbing lives in Dext/.

 Ported from 360Controller/{_60Controller,Controller}.cpp and
 ControlStruct.h. Original code Copyright (C) 2006-2013 Colin Munro and
 contributors. GPL v2 or later, see Licence.txt.
 */
#ifndef XBOX_PROTOCOL_H
#define XBOX_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "../Shared/XboxDriverShared.h"

namespace xbox {

enum class ControllerType : uint8_t {
    Xbox360      = kXboxTypeXbox360,
    XboxOriginal = kXboxTypeXboxOriginal,
    XboxOne      = kXboxTypeXboxOne,
};

// ---------------------------------------------------------------------------
// USB interface identification (class / subclass / protocol)
// ---------------------------------------------------------------------------

struct InterfaceId { uint8_t cls, subclass, protocol; bool anyClass; };

constexpr InterfaceId kIface360Pad     { 0xFF, 93, 1,   true  }; // class ignored, as in the kext
constexpr InterfaceId kIface360ChatPad { 0xFF, 93, 2,   true  };
constexpr InterfaceId kIfaceOriginal   { 0x58, 66, 0,   true  };
constexpr InterfaceId kIfaceXboxOne    { 0xFF, 71, 208, false };

inline bool Matches(const InterfaceId &id, uint8_t cls, uint8_t sub, uint8_t proto)
{
    return (id.anyClass || id.cls == cls) && id.subclass == sub && id.protocol == proto;
}

// Returns true and sets |type| if the interface is a gamepad data interface.
bool ClassifyInterface(uint8_t cls, uint8_t sub, uint8_t proto, ControllerType &type);

// ---------------------------------------------------------------------------
// The canonical report published to HID. Every controller type is converted
// to this layout, which matches kReportDescriptor below. 20 bytes.
// ---------------------------------------------------------------------------

struct __attribute__((packed)) Stick { int16_t x, y; };

struct __attribute__((packed)) Report360 {
    uint8_t  command;     // 0x00
    uint8_t  size;        // 0x14
    uint16_t buttons;     // see Button360
    uint8_t  trigL, trigR;
    Stick    left, right;
    uint8_t  reserved[6];
};
static_assert(sizeof(Report360) == 20, "Report360 must be 20 bytes");

// Bit positions in Report360::buttons (little-endian on the wire).
enum Button360 : uint16_t {
    kBtnUp = 1u << 0, kBtnDown = 1u << 1, kBtnLeft = 1u << 2, kBtnRight = 1u << 3,
    kBtnStart = 1u << 4, kBtnBack = 1u << 5, kBtnLS = 1u << 6, kBtnRS = 1u << 7,
    kBtnLB = 1u << 8, kBtnRB = 1u << 9, kBtnGuide = 1u << 10, /* bit 11 unused */
    kBtnA = 1u << 12, kBtnB = 1u << 13, kBtnX = 1u << 14, kBtnY = 1u << 15,
};

extern const uint8_t kReportDescriptor[];
extern const size_t  kReportDescriptorSize;

// ---------------------------------------------------------------------------
// Settings helpers
// ---------------------------------------------------------------------------

void DefaultSettings(XboxSettings &s);
// Clamps / repairs a settings block received from userspace. Returns false if
// it is unusable (wrong version).
bool SanitizeSettings(XboxSettings &s);
bool IsIdentityMapping(const XboxSettings &s);

// ---------------------------------------------------------------------------
// Outgoing USB packets
// ---------------------------------------------------------------------------

constexpr size_t kMaxOutPacket = 16;

struct OutPacket {
    uint8_t bytes[kMaxOutPacket];
    uint8_t length = 0;
};

// Packets to send on the interrupt OUT pipe right after the device starts.
// Returns the number written to |out| (at most |max|).
size_t BuildStartupPackets(ControllerType type, OutPacket *out, size_t max);

// ---------------------------------------------------------------------------
// Input processing
// ---------------------------------------------------------------------------

class InputProcessor {
public:
    explicit InputProcessor(ControllerType type = ControllerType::Xbox360);

    // Restores the freshly-constructed state. The dext allocates its ivars
    // with IONewZero (no constructors run), so it calls this explicitly.
    void Reset(ControllerType type);

    void SetType(ControllerType type) { type_ = type; }
    ControllerType Type() const { return type_; }

    void SetSettings(const XboxSettings &s);
    const XboxSettings &Settings() const { return settings_; }

    // Feeds one raw interrupt-IN transfer. Returns true when |out| holds a
    // report that should be passed to HID. |reply| may receive a packet the
    // driver must send back to the controller (Xbox One guide-button ack).
    bool Process(const uint8_t *data, size_t length, Report360 &out, OutPacket &reply);

    // Exposed for tests.
    void ApplyStickSettings(Stick &left, Stick &right) const;
    uint16_t RemapButtons(uint16_t buttons) const;

private:
    bool Process360(const uint8_t *data, size_t length, Report360 &out);
    bool ProcessOriginal(const uint8_t *data, size_t length, Report360 &out);
    bool ProcessXboxOne(const uint8_t *data, size_t length, Report360 &out, OutPacket &reply);
    void FinishReport(Report360 &r) const;

    ControllerType type_;
    XboxSettings settings_;
    bool identityMapping_ = true;

    // Xbox One state
    bool guidePressed_ = false;
    Report360 last_ {};

    // Original Xbox state (drops triplicate reports, as the kext did)
    uint8_t lastRaw_[sizeof(Report360)] {};
    uint32_t repeatCount_ = 0;
};

// ---------------------------------------------------------------------------
// Host -> device commands (HID SetReport from the force-feedback plugin or
// apps). Format: [command, length, payload...]
//   command 0x00, length 4: rumble  [0x00, 0x04, a, b]
//   command 0x01, length 3: LED     [0x01, 0x03, pattern]
// ---------------------------------------------------------------------------

enum class CommandResult { Send, Ignored, Unsupported };

class OutputBuilder {
public:
    void Reset() { xboxOneCounter_ = 6; }

    // Translates a SetReport payload into a USB OUT packet.
    CommandResult Translate(ControllerType type, const XboxSettings &s,
                            const uint8_t *data, size_t length, OutPacket &out);
private:
    uint8_t xboxOneCounter_ = 6;  // kext started its sequence counter at 6
};

} // namespace xbox

#endif // XBOX_PROTOCOL_H
