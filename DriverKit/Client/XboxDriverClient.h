/*
 Xbox controller DriverKit port - userspace helper for talking to the dext.

 Replaces IORegistryEntrySetCFProperties(..., "DeviceData") used by
 Pref360Control and 360Daemon with the kext. Link with IOKit.framework.

 GPL v2 or later, see Licence.txt.
 */
#ifndef XBOX_DRIVER_CLIENT_H
#define XBOX_DRIVER_CLIENT_H

#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include "../Shared/XboxDriverShared.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Calls |callback| for each attached controller driven by the dext.
   Return false from the callback to stop. Returns the number visited. */
typedef bool (*XboxDriverServiceCallback)(io_service_t service, void *context);
int XboxDriverForEachService(XboxDriverServiceCallback callback, void *context);

kern_return_t XboxDriverGetInfo(io_service_t service, XboxDeviceInfo *info);
kern_return_t XboxDriverGetSettings(io_service_t service, XboxSettings *settings);
kern_return_t XboxDriverSetSettings(io_service_t service, const XboxSettings *settings);

/* Converts the dictionary the preference pane already stores per controller
   (keys InvertLeftX, DeadzoneLeft, BindingA, ...) into XboxSettings, starting
   from |settings| as defaults. */
void XboxDriverSettingsFromDictionary(CFDictionaryRef dict, XboxSettings *settings);

#ifdef __cplusplus
}
#endif

#endif /* XBOX_DRIVER_CLIENT_H */
