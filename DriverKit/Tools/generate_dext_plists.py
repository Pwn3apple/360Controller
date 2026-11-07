#!/usr/bin/env python3
"""Generate the dext Info.plist and entitlements from the kext's device list.

The kext (360Controller/Info.plist) holds one IOKit personality per supported
vendor/product ID. This script turns each into a DriverKit personality and
collects the vendor IDs for the USB transport entitlement, so the list only
has to be maintained in one place.

Usage: python3 DriverKit/Tools/generate_dext_plists.py
"""
import os
import plistlib
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
KEXT_PLIST = os.path.join(ROOT, "360Controller", "Info.plist")
DEXT_DIR = os.path.join(ROOT, "DriverKit", "Dext")

BUNDLE_ID = "$(PRODUCT_BUNDLE_IDENTIFIER)"


def personality(name, vid, pid):
    return {
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleIdentifierKernel": "com.apple.kpi.iokit",
        "IOClass": "AppleUserHIDDevice",
        "IOProviderClass": "IOUSBHostDevice",
        "IOUserClass": "XboxGamepadDriver",
        "IOUserServerName": BUNDLE_ID,
        "IOProbeScore": 1000,
        "idVendor": vid,
        "idProduct": pid,
        "UserClientProperties": {
            "IOClass": "IOUserUserClient",
            "IOUserClass": "XboxGamepadUserClient",
        },
    }


def main():
    with open(KEXT_PLIST, "rb") as f:
        kext = plistlib.load(f)

    personalities = {}
    seen = set()
    vendors = set()
    skipped = []
    for name, p in sorted(kext["IOKitPersonalities"].items()):
        if p.get("IOClass") != "Xbox360Peripheral" or "idVendor" not in p or "idProduct" not in p:
            skipped.append(name)
            continue
        key = (int(p["idVendor"]), int(p["idProduct"]))
        if key in seen:
            skipped.append(name + " (duplicate)")
            continue
        seen.add(key)
        vendors.add(key[0])
        personalities[name] = personality(name, *key)

    info = {
        "CFBundleDevelopmentRegion": "$(DEVELOPMENT_LANGUAGE)",
        "CFBundleExecutable": "$(EXECUTABLE_NAME)",
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": "$(PRODUCT_NAME)",
        "CFBundlePackageType": "$(PRODUCT_BUNDLE_PACKAGE_TYPE)",
        "CFBundleShortVersionString": "1.0",
        "CFBundleVersion": "1",
        "OSBundleUsageDescription": "Drives wired Xbox 360, Xbox One and original Xbox controllers.",
        "IOKitPersonalities": personalities,
    }
    with open(os.path.join(DEXT_DIR, "Info.plist"), "wb") as f:
        plistlib.dump(info, f, sort_keys=True)

    entitlements = {
        "com.apple.developer.driverkit": True,
        "com.apple.developer.driverkit.family.hid.device": True,
        "com.apple.developer.driverkit.transport.usb": [{"idVendor": v} for v in sorted(vendors)],
        # Lets the existing preference pane / daemon open the user client
        # without each being listed by bundle ID. Needs Apple approval like
        # the other DriverKit entitlements.
        "com.apple.developer.driverkit.allow-any-userclient-access": True,
    }
    with open(os.path.join(DEXT_DIR, "XboxGamepadDriver.entitlements"), "wb") as f:
        plistlib.dump(entitlements, f, sort_keys=True)

    print(f"{len(personalities)} personalities, {len(vendors)} vendors")
    for s in skipped:
        print("skipped:", s, file=sys.stderr)


if __name__ == "__main__":
    main()
