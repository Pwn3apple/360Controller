/*
 Xbox controller DriverKit port - userspace helper. See XboxDriverClient.h.
 GPL v2 or later, see Licence.txt.
 */
#include "XboxDriverClient.h"

#include <string.h>

int XboxDriverForEachService(XboxDriverServiceCallback callback, void *context)
{
    io_iterator_t iter = IO_OBJECT_NULL;
    // The kernel-side object is an AppleUserHIDDevice; our dext class is
    // recorded in its IOUserClass property.
    CFMutableDictionaryRef match = IOServiceMatching("AppleUserHIDDevice");
    if (match == NULL)
        return 0;
    const void *keys[]   = { CFSTR("IOUserClass") };
    const void *values[] = { CFSTR(kXboxDriverServiceClass) };
    CFDictionaryRef props = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(match, CFSTR(kIOPropertyMatchKey), props);
    CFRelease(props);
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter) != KERN_SUCCESS)
        return 0;

    int count = 0;
    io_service_t service;
    bool keepGoing = true;
    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        if (keepGoing) {
            count++;
            keepGoing = callback(service, context);
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);
    return count;
}

static kern_return_t CallStruct(io_service_t service, uint32_t selector,
                                const void *in, size_t inSize, void *out, size_t outSize)
{
    io_connect_t connect = IO_OBJECT_NULL;
    kern_return_t ret = IOServiceOpen(service, mach_task_self(), 0, &connect);
    if (ret != KERN_SUCCESS)
        return ret;
    size_t outLen = outSize;
    ret = IOConnectCallStructMethod(connect, selector, in, inSize, out, out ? &outLen : NULL);
    if (ret == KERN_SUCCESS && out && outLen != outSize)
        ret = kIOReturnUnderrun;
    IOServiceClose(connect);
    return ret;
}

kern_return_t XboxDriverGetInfo(io_service_t service, XboxDeviceInfo *info)
{
    return CallStruct(service, kXboxSelectorGetInfo, NULL, 0, info, sizeof(*info));
}

kern_return_t XboxDriverGetSettings(io_service_t service, XboxSettings *settings)
{
    return CallStruct(service, kXboxSelectorGetSettings, NULL, 0, settings, sizeof(*settings));
}

kern_return_t XboxDriverSetSettings(io_service_t service, const XboxSettings *settings)
{
    XboxSettings copy = *settings;
    copy.version = kXboxSettingsVersion;
    return CallStruct(service, kXboxSelectorSetSettings, &copy, sizeof(copy), NULL, 0);
}

static void ReadBool(CFDictionaryRef d, CFStringRef key, uint16_t flag, uint16_t *flags)
{
    CFTypeRef v = CFDictionaryGetValue(d, key);
    if (v && CFGetTypeID(v) == CFBooleanGetTypeID()) {
        if (CFBooleanGetValue((CFBooleanRef)v)) *flags |= flag;
        else *flags &= (uint16_t)~flag;
    }
}

static bool ReadInt(CFDictionaryRef d, CFStringRef key, int *out)
{
    CFTypeRef v = CFDictionaryGetValue(d, key);
    return v && CFGetTypeID(v) == CFNumberGetTypeID() && CFNumberGetValue((CFNumberRef)v, kCFNumberIntType, out);
}

void XboxDriverSettingsFromDictionary(CFDictionaryRef d, XboxSettings *s)
{
    if (d == NULL || s == NULL)
        return;
    s->version = kXboxSettingsVersion;

    ReadBool(d, CFSTR("InvertLeftX"),  kXboxFlagInvertLeftX,   &s->flags);
    ReadBool(d, CFSTR("InvertLeftY"),  kXboxFlagInvertLeftY,   &s->flags);
    ReadBool(d, CFSTR("InvertRightX"), kXboxFlagInvertRightX,  &s->flags);
    ReadBool(d, CFSTR("InvertRightY"), kXboxFlagInvertRightY,  &s->flags);
    ReadBool(d, CFSTR("RelativeLeft"), kXboxFlagRelativeLeft,  &s->flags);
    ReadBool(d, CFSTR("RelativeRight"),kXboxFlagRelativeRight, &s->flags);
    ReadBool(d, CFSTR("DeadOffLeft"),  kXboxFlagDeadOffLeft,   &s->flags);
    ReadBool(d, CFSTR("DeadOffRight"), kXboxFlagDeadOffRight,  &s->flags);
    ReadBool(d, CFSTR("SwapSticks"),   kXboxFlagSwapSticks,    &s->flags);
    ReadBool(d, CFSTR("Pretend360"),   kXboxFlagPretend360,    &s->flags);

    int v;
    if (ReadInt(d, CFSTR("DeadzoneLeft"), &v))  s->deadzoneLeft  = (uint16_t)(v < 0 ? 0 : v > 32766 ? 32766 : v);
    if (ReadInt(d, CFSTR("DeadzoneRight"), &v)) s->deadzoneRight = (uint16_t)(v < 0 ? 0 : v > 32766 ? 32766 : v);
    if (ReadInt(d, CFSTR("RumbleType"), &v))    s->rumbleType    = (uint8_t)v;

    static const CFStringRef bindingKeys[kXboxButtonCount] = {
        CFSTR("BindingUp"), CFSTR("BindingDown"), CFSTR("BindingLeft"), CFSTR("BindingRight"),
        CFSTR("BindingStart"), CFSTR("BindingBack"), CFSTR("BindingLSC"), CFSTR("BindingRSC"),
        CFSTR("BindingLB"), CFSTR("BindingRB"), CFSTR("BindingGuide"),
        CFSTR("BindingA"), CFSTR("BindingB"), CFSTR("BindingX"), CFSTR("BindingY"),
    };
    for (int i = 0; i < kXboxButtonCount; i++)
        if (ReadInt(d, bindingKeys[i], &v) && v >= 0 && v <= 15)
            s->mapping[i] = (uint8_t)v;
}
