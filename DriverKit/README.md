# DriverKit port (work in progress)

This folder replaces the wired-controller kernel extension (`360Controller/`)
with a **DriverKit system extension (dext)**. A dext runs in user space, so it
works on Apple Silicon without lowering system security, and it keeps working
on macOS versions that no longer load third-party kexts.

> **Status:** the protocol code is unit tested on the host. The dext, host app
> and client library were written without a Mac and **have not been compiled or
> run yet**. Expect a round of fixes for compile errors and API details in Xcode.

## Layout

| Path | What it is |
|---|---|
| `Shared/XboxDriverShared.h` | Settings struct, user-client selectors. Plain C, shared by everything. |
| `Core/XboxProtocol.{h,cpp}` | All controller logic: converting Xbox 360, original Xbox and Xbox One reports, deadzones, inversion, button remapping, stick swap, rumble/LED packets, startup packets. Pure C++ with no IOKit, so it can be tested anywhere. |
| `Tests/` | Host unit tests for `Core/` (`make -C DriverKit/Tests test`). |
| `Dext/XboxGamepadDriver.{iig,cpp}` | `IOUserHIDDevice` that matches the USB device, claims the gamepad interface and publishes the HID gamepad. Replaces `Xbox360Peripheral` and `Xbox360ControllerClass`. |
| `Dext/XboxGamepadUserClient.{iig,cpp}` | User client for reading and writing settings. Replaces `IORegistryEntrySetCFProperties` on the kext. |
| `Dext/Info.plist`, `Dext/XboxGamepadDriver.entitlements` | Generated. Don't edit by hand. |
| `Tools/generate_dext_plists.py` | Rebuilds the two files above from the kext's device list in `360Controller/Info.plist`, which stays the single source of supported devices. |
| `HostApp/` | Minimal SwiftUI app that installs and removes the dext. |
| `Client/XboxDriverClient.{h,c}` | C helper for the preference pane and daemon to find controllers and push settings. |

## What changed compared to the kext

* **The HID report format and descriptor are unchanged**, so games and the
  force-feedback plugin see the same device as before.
* One driver class handles every controller type. The protocol differences are
  data in `Core/`, not a class hierarchy of `IOHIDDevice` subclasses.
* Bug fixes found while porting:
  * With "Normalize" enabled on the right stick, the kext rescaled the *left*
    stick (`fiddleReport`).
  * Xbox One packets with command `0x07` but an unexpected size were passed to
    HID unconverted.
  * Original Xbox pads were sent the Xbox 360 "LED off" packet.
* Settings now travel as a fixed, versioned struct through a user client
  instead of an `OSDictionary` set on the registry entry.

## Not ported yet

* **Xbox 360 chatpad** (`ChatPad.cpp`). It needs a second `IOUserHIDDevice` for
  the keyboard and the toggle/keep-alive timer (an `IODispatchSource` timer).
* **Wireless receiver** (`WirelessGamingReceiver/`, `Wireless360Controller/`).
* **Changing "Pretend 360" at runtime.** A DriverKit HID device's vendor and
  product IDs are fixed once it's published. For now, set `Pretend360 = true` in
  a personality and the change applies when the controller is replugged.
* **Preference pane and daemon.** They still call
  `IORegistryEntrySetCFProperties`. Switch them to `Client/XboxDriverClient.h`.
  `XboxDriverSettingsFromDictionary()` converts the dictionary they already
  build, so the change is small (`Pref360ControlPref.m:988`,
  `360Daemon/ControlPrefs.m:71`).
* **Force-feedback plugin registration.** The plugin code works unchanged,
  because it uses HID `setReport`. The kext registered it with
  `IOCFPlugInTypes` pointing inside the kext bundle. With a dext, install the
  plugin somewhere stable (for example `/Library/Application Support/...`) and
  set `IOCFPlugInTypes` to that path in the personality.

## Building (on a Mac with Xcode 15 or later)

1. In Xcode, add a **DriverKit Driver** target named `XboxGamepadDriver` to
   `360Driver.xcworkspace`. Use bundle ID `com.mice.driver.XboxGamepadDriver`,
   or change `driverIdentifier` in `HostApp/XboxDriverApp.swift` to match.
2. Delete the template sources and add `Dext/*.iig`, `Dext/*.cpp` and
   `Core/XboxProtocol.cpp`. Set *Info.plist File* to `DriverKit/Dext/Info.plist`
   and *Code Signing Entitlements* to
   `DriverKit/Dext/XboxGamepadDriver.entitlements`. Link `HIDDriverKit` and
   `USBDriverKit`.
3. Add a **macOS App** target from `HostApp/`, using
   `HostApp/XboxDriverApp.entitlements`. Add the dext to the app target's
   dependencies and to an *Embed System Extensions* build phase.
4. After changing the device list in `360Controller/Info.plist`, run
   `python3 DriverKit/Tools/generate_dext_plists.py`.

### Signing and entitlements

DriverKit entitlements (`driverkit`, `family.hid.device`,
`transport.usb` with the vendor list, `allow-any-userclient-access`) must be
**requested from Apple** before the dext can be distributed or run with SIP
enabled. Request them at developer.apple.com/system-extensions.

For local development without them:

```bash
csrutil disable    # from Recovery; re-enable when done
```

```bash
systemextensionsctl developer on
```

Then build, run the host app from `/Applications`, click **Install Driver** and
approve it in System Settings → General → Login Items & Extensions.

### Debugging

```bash
log stream --predicate 'eventMessage CONTAINS "XboxGamepadDriver"'
```

```bash
ioreg -l -w0 -r -c AppleUserHIDDevice | grep -E 'IOUserClass|Product|VendorID'
```

## First-run checklist

- [ ] Dext and app compile, and API mismatches are fixed
- [ ] Dext loads and matches a wired 360 pad (`ioreg` shows `XboxGamepadDriver`)
- [ ] Sticks, triggers, buttons and d-pad show up in a gamepad tester and the
      Game Controller framework
- [ ] Xbox One pad initialises, and the Guide button works on 2015 and 2016 pads
- [ ] Rumble works through the force-feedback plugin (360 and One)
- [ ] Settings round-trip through `XboxDriverClient` (deadzone, inversion, remap)
- [ ] Unplugging during input doesn't crash or leak (watch the log)
