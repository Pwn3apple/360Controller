/*
 Xbox controller DriverKit port - settings user client.
 GPL v2 or later, see Licence.txt.
 */

#include <os/log.h>

#include <DriverKit/DriverKit.h>
#include <DriverKit/IOUserClient.h>
#include <DriverKit/OSData.h>

#include "XboxGamepadUserClient.h"   // generated from XboxGamepadUserClient.iig
#include "XboxGamepadDriver.h"
#include "../Shared/XboxDriverShared.h"

struct XboxGamepadUserClient_IVars {
    XboxGamepadDriver *driver;
};

bool XboxGamepadUserClient::init()
{
    if (!super::init())
        return false;
    ivars = IONewZero(XboxGamepadUserClient_IVars, 1);
    return ivars != nullptr;
}

void XboxGamepadUserClient::free()
{
    if (ivars != nullptr) {
        OSSafeReleaseNULL(ivars->driver);
        IOSafeDeleteNULL(ivars, XboxGamepadUserClient_IVars, 1);
    }
    super::free();
}

kern_return_t IMPL(XboxGamepadUserClient, Start)
{
    kern_return_t ret = Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess)
        return ret;
    ivars->driver = OSDynamicCast(XboxGamepadDriver, provider);
    if (ivars->driver == nullptr) {
        Stop(provider, SUPERDISPATCH);
        return kIOReturnNoDevice;
    }
    ivars->driver->retain();
    return kIOReturnSuccess;
}

kern_return_t IMPL(XboxGamepadUserClient, Stop)
{
    OSSafeReleaseNULL(ivars->driver);
    return Stop(provider, SUPERDISPATCH);
}

template <typename T>
static kern_return_t ReturnStruct(IOUserClientMethodArguments *arguments, const T &value)
{
    if (arguments->structureOutputMaximumSize < sizeof(T))
        return kIOReturnNoSpace;
    arguments->structureOutput = OSData::withBytes(&value, sizeof(T));
    return arguments->structureOutput ? kIOReturnSuccess : kIOReturnNoMemory;
}

kern_return_t XboxGamepadUserClient::ExternalMethod(uint64_t selector,
                                                    IOUserClientMethodArguments *arguments,
                                                    const IOUserClientMethodDispatch *dispatch,
                                                    OSObject *target,
                                                    void *reference)
{
    (void)dispatch; (void)target; (void)reference;
    XboxGamepadDriver *driver = ivars->driver;
    if (driver == nullptr)
        return kIOReturnNotAttached;

    switch (selector) {
        case kXboxSelectorGetSettings: {
            XboxSettings s;
            kern_return_t ret = driver->CopySettings(&s, sizeof(s));
            return ret == kIOReturnSuccess ? ReturnStruct(arguments, s) : ret;
        }
        case kXboxSelectorSetSettings: {
            OSData *in = arguments->structureInput;
            if (in == nullptr || in->getLength() != sizeof(XboxSettings))
                return kIOReturnBadArgument;
            return driver->ApplySettings(in->getBytesNoCopy(), in->getLength());
        }
        case kXboxSelectorGetInfo: {
            XboxDeviceInfo info;
            kern_return_t ret = driver->CopyDeviceInfo(&info, sizeof(info));
            return ret == kIOReturnSuccess ? ReturnStruct(arguments, info) : ret;
        }
        default:
            return kIOReturnBadArgument;
    }
}
