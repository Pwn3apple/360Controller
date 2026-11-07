/*
 Xbox controller DriverKit port - definitions shared by the driver extension,
 the host app and the preference pane / daemon.

 Plain C so it can be included from DriverKit C++, Objective-C and Swift
 (via a bridging header).

 This file is part of Xbox360Controller. GPL v2 or later, see Licence.txt.
 */
#ifndef XBOX_DRIVER_SHARED_H
#define XBOX_DRIVER_SHARED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IOUserClass of the HID driver. Userspace finds it by matching
   AppleUserHIDDevice with IOPropertyMatch { IOUserClass = this }. */
#define kXboxDriverServiceClass "XboxGamepadDriver"

/* External method selectors understood by XboxGamepadUserClient. */
enum {
    kXboxSelectorGetSettings = 0,   /* structure out: XboxSettings          */
    kXboxSelectorSetSettings = 1,   /* structure in:  XboxSettings          */
    kXboxSelectorGetInfo     = 2,   /* structure out: XboxDeviceInfo        */
    kXboxSelectorCount
};

/* Bit flags for XboxSettings.flags */
enum {
    kXboxFlagInvertLeftX   = 1u << 0,
    kXboxFlagInvertLeftY   = 1u << 1,
    kXboxFlagInvertRightX  = 1u << 2,
    kXboxFlagInvertRightY  = 1u << 3,
    kXboxFlagRelativeLeft  = 1u << 4,  /* "Linked" checkbox: radial-ish deadzone */
    kXboxFlagRelativeRight = 1u << 5,
    kXboxFlagDeadOffLeft   = 1u << 6,  /* "Normalize" checkbox                 */
    kXboxFlagDeadOffRight  = 1u << 7,
    kXboxFlagSwapSticks    = 1u << 8,
    kXboxFlagPretend360    = 1u << 9,  /* report MS 360 VID/PID to games       */
};

/* XboxSettings.rumbleType */
enum {
    kXboxRumbleDefault  = 0,
    kXboxRumbleNone     = 1,
    kXboxRumbleTriggers = 2,  /* Xbox One impulse triggers only */
    kXboxRumbleBoth     = 3,
};

/* Logical button indices used by XboxSettings.mapping.
   mapping[i] is the output bit (0-15, bit 11 is unused) that logical
   button i is reported on. */
enum {
    kXboxButtonUp = 0, kXboxButtonDown, kXboxButtonLeft, kXboxButtonRight,
    kXboxButtonStart, kXboxButtonBack, kXboxButtonLeftStick, kXboxButtonRightStick,
    kXboxButtonLB, kXboxButtonRB, kXboxButtonGuide,
    kXboxButtonA, kXboxButtonB, kXboxButtonX, kXboxButtonY,
    kXboxButtonCount /* 15 */
};

#define kXboxSettingsVersion 1

typedef struct __attribute__((packed)) XboxSettings {
    uint16_t version;            /* must be kXboxSettingsVersion */
    uint16_t flags;              /* kXboxFlag* */
    uint16_t deadzoneLeft;       /* 0 - 32767 */
    uint16_t deadzoneRight;
    uint8_t  rumbleType;         /* kXboxRumble* */
    uint8_t  mapping[kXboxButtonCount];
} XboxSettings;

/* XboxDeviceInfo.controllerType */
enum {
    kXboxTypeXbox360      = 0,
    kXboxTypeXboxOriginal = 1,
    kXboxTypeXboxOne      = 2,
};

typedef struct __attribute__((packed)) XboxDeviceInfo {
    uint16_t vendorID;
    uint16_t productID;
    uint8_t  controllerType;     /* kXboxType* */
    uint8_t  reserved[3];
    uint32_t locationID;
} XboxDeviceInfo;

#ifdef __cplusplus
}
#endif

#endif /* XBOX_DRIVER_SHARED_H */
