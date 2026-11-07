/*
 Xbox controller DriverKit port - HID driver implementation.

 Protocol handling (report conversion, deadzones, remapping, rumble/LED
 packets) lives in Core/XboxProtocol.cpp and is unit tested on the host.
 This file only does USB and HID plumbing.

 Ported from 360Controller/_60Controller.cpp and Controller.cpp.
 Original code Copyright (C) 2006-2013 Colin Munro and contributors.
 GPL v2 or later, see Licence.txt.
 */

#include <os/log.h>
#include <string.h>

#include <DriverKit/DriverKit.h>
#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <DriverKit/IOMemoryMap.h>
#include <DriverKit/OSCollections.h>
#include <HIDDriverKit/HIDDriverKit.h>
#include <USBDriverKit/USBDriverKit.h>

#include "XboxGamepadDriver.h"   // generated from XboxGamepadDriver.iig
#include "../Core/XboxProtocol.h"

#define LOG_PREFIX "XboxGamepadDriver: "
#define Log(fmt, ...) os_log(OS_LOG_DEFAULT, LOG_PREFIX fmt, ##__VA_ARGS__)

static constexpr uint32_t kOutTimeoutMs     = 100;
static constexpr uint32_t kOutBufferSize    = 64;
static constexpr uint32_t kReportBufferSize = 64;

struct XboxGamepadDriver_IVars {
    IOLock *lock;                       // guards processor/output/settings
    bool stopping;

    IOUSBHostDevice *device;
    IOUSBHostInterface *interface;
    IOUSBHostPipe *inPipe;
    IOUSBHostPipe *outPipe;
    uint16_t inMaxPacket;

    IOBufferMemoryDescriptor *inBuffer;
    IOBufferMemoryDescriptor *outBuffer;
    IOBufferMemoryDescriptor *reportBuffer;
    OSAction *readAction;

    xbox::InputProcessor processor;     // initialised with Reset(), see init()
    xbox::OutputBuilder output;

    // Device identity, captured in handleStart for newDeviceDescription
    uint16_t vendorID, productID, release;
    uint32_t locationID;
    bool pretend360;                    // from the matching personality
    char manufacturer[64];
    char product[64];
    char serial[64];
};

#pragma mark - helpers

// Copies a USB string descriptor into |out| as ASCII (non-ASCII -> '?').
static void CopyUSBString(IOUSBHostDevice *device, uint8_t index, char *out, size_t outSize, const char *fallback)
{
    strlcpy(out, fallback, outSize);
    if (index == 0)
        return;
    const IOUSBStringDescriptor *sd = device->CopyStringDescriptor(index, 0x0409);
    if (sd == nullptr)
        return;
    size_t chars = sd->bLength >= 2 ? (sd->bLength - 2) / 2 : 0;
    size_t n = 0;
    for (size_t i = 0; i < chars && n + 1 < outSize; i++) {
        uint16_t c = static_cast<uint16_t>(sd->bString[i * 2] | (sd->bString[i * 2 + 1] << 8));
        out[n++] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    if (n > 0)
        out[n] = '\0';
    IOUSBHostFreeDescriptor(sd);
}

static kern_return_t WritePacket(XboxGamepadDriver_IVars *iv, const xbox::OutPacket &pkt)
{
    if (iv->outPipe == nullptr || iv->outBuffer == nullptr || pkt.length == 0)
        return kIOReturnNotReady;

    IOAddressSegment range = {};
    iv->outBuffer->GetAddressRange(&range);
    if (range.address == 0 || range.length < pkt.length)
        return kIOReturnNoMemory;
    memcpy(reinterpret_cast<void *>(range.address), pkt.bytes, pkt.length);
    iv->outBuffer->SetLength(pkt.length);

    uint32_t transferred = 0;
    kern_return_t ret = iv->outPipe->IO(iv->outBuffer, pkt.length, &transferred, kOutTimeoutMs);
    if (ret != kIOReturnSuccess)
        Log("write failed: 0x%x", ret);
    return ret;
}

static kern_return_t QueueRead(XboxGamepadDriver_IVars *iv)
{
    if (iv->stopping || iv->inPipe == nullptr)
        return kIOReturnNotReady;
    kern_return_t ret = iv->inPipe->AsyncIO(iv->inBuffer, iv->inMaxPacket, iv->readAction, 0);
    if (ret != kIOReturnSuccess)
        Log("failed to queue read: 0x%x", ret);
    return ret;
}

static void SetNumber(OSDictionary *dict, const char *key, uint64_t value)
{
    OSNumber *n = OSNumber::withNumber(value, 32);
    if (n) { OSDictionarySetValue(dict, key, n); n->release(); }
}

static void SetString(OSDictionary *dict, const char *key, const char *value)
{
    OSString *s = OSString::withCString(value);
    if (s) { OSDictionarySetValue(dict, key, s); s->release(); }
}

// Aborts I/O and releases every USB object. Safe to call more than once.
static void ReleaseUSB(IOService *self, XboxGamepadDriver_IVars *iv)
{
    iv->stopping = true;

    if (iv->inPipe)
        iv->inPipe->Abort(0, kIOReturnAborted, self);
    if (iv->outPipe)
        iv->outPipe->Abort(0, kIOReturnAborted, self);

    OSSafeReleaseNULL(iv->readAction);
    OSSafeReleaseNULL(iv->inPipe);
    OSSafeReleaseNULL(iv->outPipe);
    OSSafeReleaseNULL(iv->inBuffer);
    OSSafeReleaseNULL(iv->outBuffer);
    OSSafeReleaseNULL(iv->reportBuffer);
    if (iv->interface) {
        iv->interface->Close(self, 0);
        OSSafeReleaseNULL(iv->interface);
    }
    if (iv->device) {
        iv->device->Close(self, 0);
        OSSafeReleaseNULL(iv->device);
    }
}

#pragma mark - lifecycle

bool XboxGamepadDriver::init()
{
    if (!super::init())
        return false;
    ivars = IONewZero(XboxGamepadDriver_IVars, 1);
    if (ivars == nullptr)
        return false;
    ivars->lock = IOLockAlloc();
    if (ivars->lock == nullptr)
        return false;
    ivars->processor.Reset(xbox::ControllerType::Xbox360);
    ivars->output.Reset();
    return true;
}

void XboxGamepadDriver::free()
{
    if (ivars != nullptr) {
        ReleaseUSB(this, ivars);
        if (ivars->lock)
            IOLockFree(ivars->lock);
        IOSafeDeleteNULL(ivars, XboxGamepadDriver_IVars, 1);
    }
    super::free();
}

// Runs inside IOUserHIDDevice::Start, before the HID device is published.
// Opens the USB device, selects its configuration and claims the gamepad
// interface. Equivalent to Xbox360Peripheral::start in the kext.
bool XboxGamepadDriver::handleStart(IOService *provider)
{
    if (!super::handleStart(provider))
        return false;

    XboxGamepadDriver_IVars *iv = ivars;
    iv->device = OSDynamicCast(IOUSBHostDevice, provider);
    if (iv->device == nullptr) {
        Log("provider is not an IOUSBHostDevice");
        return false;
    }
    iv->device->retain();

    kern_return_t ret = iv->device->Open(this, 0, 0);
    if (ret != kIOReturnSuccess) {
        Log("unable to open device: 0x%x", ret);
        return false;
    }

    // Identity
    const IOUSBDeviceDescriptor *dd = iv->device->CopyDeviceDescriptor();
    if (dd == nullptr) {
        Log("no device descriptor");
        return false;
    }
    iv->vendorID  = dd->idVendor;
    iv->productID = dd->idProduct;
    iv->release   = dd->bcdDevice;
    const uint8_t iManufacturer = dd->iManufacturer, iProduct = dd->iProduct, iSerial = dd->iSerialNumber;
    IOUSBHostFreeDescriptor(dd);
    CopyUSBString(iv->device, iManufacturer, iv->manufacturer, sizeof(iv->manufacturer), "Unknown");
    CopyUSBString(iv->device, iProduct, iv->product, sizeof(iv->product), "Xbox Controller");
    CopyUSBString(iv->device, iSerial, iv->serial, sizeof(iv->serial), "");

    OSDictionary *props = nullptr;
    if (provider->CopyProperties(&props) == kIOReturnSuccess && props) {
        if (OSNumber *loc = OSDynamicCast(OSNumber, props->getObject("locationID")))
            iv->locationID = static_cast<uint32_t>(loc->unsigned32BitValue());
        props->release();
    }
    props = nullptr;
    if (CopyProperties(&props) == kIOReturnSuccess && props) {
        iv->pretend360 = (props->getObject("Pretend360") == kOSBooleanTrue);
        props->release();
    }

    // Select the first configuration. Xbox pads are bDeviceClass 0xFF, so
    // nothing else in the system configures them.
    const IOUSBConfigurationDescriptor *cd = iv->device->CopyConfigurationDescriptor(static_cast<uint8_t>(0));
    if (cd == nullptr) {
        Log("no configuration descriptor");
        return false;
    }
    const uint8_t configValue = cd->bConfigurationValue;
    IOUSBHostFreeDescriptor(cd);
    ret = iv->device->SetConfiguration(configValue, false);
    if (ret != kIOReturnSuccess) {
        Log("unable to set configuration: 0x%x", ret);
        return false;
    }

    // Find the gamepad interface and its interrupt endpoints.
    uintptr_t iter = 0;
    ret = iv->device->CreateInterfaceIterator(&iter);
    if (ret != kIOReturnSuccess) {
        Log("unable to iterate interfaces: 0x%x", ret);
        return false;
    }
    uint8_t inAddr = 0, outAddr = 0;
    for (;;) {
        IOUSBHostInterface *candidate = nullptr;
        if (iv->device->CopyInterface(iter, &candidate) != kIOReturnSuccess || candidate == nullptr)
            break;

        bool keep = false;
        const IOUSBConfigurationDescriptor *config = candidate->CopyConfigurationDescriptor();
        const IOUSBInterfaceDescriptor *id = config ? candidate->GetInterfaceDescriptor(config) : nullptr;
        xbox::ControllerType type;
        if (iv->interface == nullptr && id != nullptr &&
            xbox::ClassifyInterface(id->bInterfaceClass, id->bInterfaceSubClass, id->bInterfaceProtocol, type)) {
            uint8_t foundIn = 0, foundOut = 0;
            uint16_t maxIn = 0;
            const IOUSBEndpointDescriptor *ep = nullptr;
            while ((ep = IOUSBGetNextEndpointDescriptor(config, id, reinterpret_cast<const IOUSBDescriptorHeader *>(ep))) != nullptr) {
                if ((ep->bmAttributes & 0x03) != 0x03)          // interrupt only
                    continue;
                if (ep->bEndpointAddress & 0x80) {
                    if (!foundIn) { foundIn = ep->bEndpointAddress; maxIn = ep->wMaxPacketSize & 0x07FF; }
                } else if (!foundOut) {
                    foundOut = ep->bEndpointAddress;
                }
            }
            if (foundIn && foundOut) {
                keep = true;
                iv->interface = candidate;
                iv->inMaxPacket = maxIn ? maxIn : 32;
                inAddr = foundIn;
                outAddr = foundOut;
                iv->processor.Reset(type);
            }
        }
        if (config)
            IOUSBHostFreeDescriptor(config);
        if (!keep)
            candidate->release();
    }
    iv->device->DestroyInterfaceIterator(iter);

    if (iv->interface == nullptr) {
        Log("no supported gamepad interface on %04x:%04x", iv->vendorID, iv->productID);
        return false;
    }

    ret = iv->interface->Open(this, 0, 0);
    if (ret != kIOReturnSuccess) {
        Log("unable to open interface: 0x%x", ret);
        return false;
    }
    if (iv->interface->CopyPipe(inAddr, &iv->inPipe) != kIOReturnSuccess ||
        iv->interface->CopyPipe(outAddr, &iv->outPipe) != kIOReturnSuccess) {
        Log("unable to open pipes");
        return false;
    }
    if (iv->interface->CreateIOBuffer(kIOMemoryDirectionIn, iv->inMaxPacket, &iv->inBuffer) != kIOReturnSuccess ||
        iv->interface->CreateIOBuffer(kIOMemoryDirectionOut, kOutBufferSize, &iv->outBuffer) != kIOReturnSuccess ||
        IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut, kReportBufferSize, 0, &iv->reportBuffer) != kIOReturnSuccess) {
        Log("unable to allocate buffers");
        return false;
    }
    if (CreateActionReadComplete(0, &iv->readAction) != kIOReturnSuccess) {
        Log("unable to create read action");
        return false;
    }

    // TODO: Xbox 360 chatpad (interface subclass 93 / protocol 2) is not
    // ported yet; it needs a second IOUserHIDDevice for the keyboard.

    Log("claimed %04x:%04x type %u", iv->vendorID, iv->productID,
        static_cast<unsigned>(iv->processor.Type()));
    return true;
}

kern_return_t IMPL(XboxGamepadDriver, Start)
{
    // IOUserHIDDevice::Start calls handleStart(), then publishes the HID
    // device using newDeviceDescription() / newReportDescriptor().
    kern_return_t ret = Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess) {
        Log("start failed: 0x%x", ret);
        ReleaseUSB(this, ivars);   // handleStart may have failed half-way
        Stop(provider, SUPERDISPATCH);
        return ret;
    }

    xbox::OutPacket packets[4];
    const size_t count = xbox::BuildStartupPackets(ivars->processor.Type(), packets, 4);
    for (size_t i = 0; i < count; i++)
        WritePacket(ivars, packets[i]);

    ret = QueueRead(ivars);
    if (ret != kIOReturnSuccess) {
        ReleaseUSB(this, ivars);
        Stop(provider, SUPERDISPATCH);
        return ret;
    }

    RegisterService();   // lets the preference pane find us
    return kIOReturnSuccess;
}

kern_return_t IMPL(XboxGamepadDriver, Stop)
{
    ReleaseUSB(this, ivars);
    return Stop(provider, SUPERDISPATCH);
}

#pragma mark - HID

OSDictionary *XboxGamepadDriver::newDeviceDescription(void)
{
    const XboxGamepadDriver_IVars *iv = ivars;
    OSDictionary *dict = OSDictionary::withCapacity(12);
    if (dict == nullptr)
        return nullptr;

    // Original pads and "Pretend 360" pads present as a Microsoft 360 pad
    // (045e:028e) so games with built-in mappings recognise them.
    const bool fakeID = iv->pretend360 || iv->processor.Type() == xbox::ControllerType::XboxOriginal;
    const char *productName;
    switch (iv->processor.Type()) {
        case xbox::ControllerType::XboxOriginal: productName = "Xbox Original Wired Controller"; break;
        case xbox::ControllerType::XboxOne:      productName = iv->pretend360 ? "Xbox 360 Wired Controller"
                                                                              : "Xbox One Wired Controller"; break;
        default:                                 productName = "Xbox 360 Wired Controller"; break;
    }

    SetNumber(dict, "VendorID",  fakeID ? 0x045E : iv->vendorID);
    SetNumber(dict, "ProductID", fakeID ? 0x028E : iv->productID);
    SetNumber(dict, "VersionNumber", iv->release);
    SetString(dict, "Transport", "USB");
    SetString(dict, "Manufacturer",
              iv->processor.Type() == xbox::ControllerType::XboxOriginal ? "Holtek" : iv->manufacturer);
    SetString(dict, "Product", productName);
    if (iv->serial[0])
        SetString(dict, "SerialNumber", iv->serial);
    if (iv->locationID)
        SetNumber(dict, "LocationID", iv->locationID);
    SetNumber(dict, "PrimaryUsagePage", xbox::kReportDescriptor[1]);   // Generic Desktop
    SetNumber(dict, "PrimaryUsage",     xbox::kReportDescriptor[3]);   // Game Pad
    OSDictionarySetValue(dict, "RegisterService", kOSBooleanTrue);
    return dict;
}

OSData *XboxGamepadDriver::newReportDescriptor(void)
{
    return OSData::withBytes(xbox::kReportDescriptor, xbox::kReportDescriptorSize);
}

// Rumble / LED commands from the force-feedback plugin or apps.
kern_return_t XboxGamepadDriver::setReport(IOMemoryDescriptor *report,
                                           IOHIDReportType reportType,
                                           IOOptionBits options,
                                           uint32_t completionTimeout,
                                           OSAction *action)
{
    (void)reportType; (void)options; (void)completionTimeout;

    kern_return_t ret = kIOReturnUnsupported;
    uint64_t length = 0;
    IOMemoryMap *map = nullptr;

    if (report != nullptr && report->CreateMapping(0, 0, 0, 0, 0, &map) == kIOReturnSuccess && map) {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(map->GetAddress());
        length = map->GetLength();

        xbox::OutPacket pkt;
        IOLockLock(ivars->lock);
        xbox::CommandResult result = ivars->output.Translate(ivars->processor.Type(),
                                                             ivars->processor.Settings(),
                                                             bytes, static_cast<size_t>(length), pkt);
        IOLockUnlock(ivars->lock);

        switch (result) {
            case xbox::CommandResult::Send:        ret = WritePacket(ivars, pkt); break;
            case xbox::CommandResult::Ignored:     ret = kIOReturnSuccess; break;
            case xbox::CommandResult::Unsupported: ret = kIOReturnUnsupported; break;
        }
        map->release();
    }

    // Asynchronous requests carry an action and must be completed.
    if (action != nullptr)
        CompleteReport(action, ret, static_cast<uint32_t>(length));
    return ret;
}

kern_return_t XboxGamepadDriver::getReport(IOMemoryDescriptor *report,
                                           IOHIDReportType reportType,
                                           IOOptionBits options,
                                           uint32_t completionTimeout,
                                           OSAction *action)
{
    (void)report; (void)reportType; (void)options; (void)completionTimeout;
    if (action != nullptr)
        CompleteReport(action, kIOReturnUnsupported, 0);
    return kIOReturnUnsupported;
}

#pragma mark - USB input

void IMPL(XboxGamepadDriver, ReadComplete)
{
    (void)action;
    XboxGamepadDriver_IVars *iv = ivars;
    if (iv->stopping)
        return;

    bool requeue = true;
    switch (status) {
        case kIOReturnOverrun:
            Log("read overrun, clearing stall");
            if (iv->inPipe)
                iv->inPipe->ClearStall(false);
            [[fallthrough]];
        case kIOReturnSuccess: {
            IOAddressSegment range = {};
            iv->inBuffer->GetAddressRange(&range);
            const uint8_t *data = reinterpret_cast<const uint8_t *>(range.address);

            xbox::Report360 hidReport;
            xbox::OutPacket reply;
            IOLockLock(iv->lock);
            const bool publish = data && iv->processor.Process(data, actualByteCount, hidReport, reply);
            IOLockUnlock(iv->lock);

            if (reply.length)
                WritePacket(iv, reply);

            if (publish && iv->reportBuffer) {
                IOAddressSegment out = {};
                iv->reportBuffer->GetAddressRange(&out);
                memcpy(reinterpret_cast<void *>(out.address), &hidReport, sizeof(hidReport));
                iv->reportBuffer->SetLength(sizeof(hidReport));
                handleReport(completionTimestamp, iv->reportBuffer, sizeof(hidReport),
                             kIOHIDReportTypeInput, 0);
            }
            break;
        }
        case kIOReturnAborted:
        case kIOReturnNoDevice:
        case kIOReturnNotResponding:
        case kIOReturnOffline:
            requeue = false;
            break;
        default:
            Log("read error 0x%x", status);
            if (iv->inPipe)
                iv->inPipe->ClearStall(true);
            break;
    }

    if (requeue)
        QueueRead(iv);
}

#pragma mark - settings (called from XboxGamepadUserClient)

kern_return_t XboxGamepadDriver::CopySettings(void *buffer, size_t length)
{
    if (buffer == nullptr || length != sizeof(XboxSettings))
        return kIOReturnBadArgument;
    IOLockLock(ivars->lock);
    memcpy(buffer, &ivars->processor.Settings(), sizeof(XboxSettings));
    IOLockUnlock(ivars->lock);
    if (ivars->pretend360)
        static_cast<XboxSettings *>(buffer)->flags |= kXboxFlagPretend360;
    return kIOReturnSuccess;
}

kern_return_t XboxGamepadDriver::ApplySettings(const void *buffer, size_t length)
{
    if (buffer == nullptr || length != sizeof(XboxSettings))
        return kIOReturnBadArgument;
    XboxSettings s;
    memcpy(&s, buffer, sizeof(s));
    if (!xbox::SanitizeSettings(s))
        return kIOReturnBadArgument;

    // The HID identity is fixed once the device is published, so Pretend360
    // can only be changed through the personality (takes effect on replug).
    if (((s.flags & kXboxFlagPretend360) != 0) != ivars->pretend360)
        Log("Pretend360 changes take effect only after reconnecting the controller");

    IOLockLock(ivars->lock);
    ivars->processor.SetSettings(s);
    IOLockUnlock(ivars->lock);
    return kIOReturnSuccess;
}

kern_return_t XboxGamepadDriver::CopyDeviceInfo(void *buffer, size_t length)
{
    if (buffer == nullptr || length != sizeof(XboxDeviceInfo))
        return kIOReturnBadArgument;
    XboxDeviceInfo info = {};
    info.vendorID = ivars->vendorID;
    info.productID = ivars->productID;
    info.controllerType = static_cast<uint8_t>(ivars->processor.Type());
    info.locationID = ivars->locationID;
    memcpy(buffer, &info, sizeof(info));
    return kIOReturnSuccess;
}

kern_return_t IMPL(XboxGamepadDriver, NewUserClient)
{
    (void)type;
    IOService *client = nullptr;
    kern_return_t ret = Create(this, "UserClientProperties", &client);
    if (ret != kIOReturnSuccess) {
        Log("failed to create user client: 0x%x", ret);
        return ret;
    }
    *userClient = OSDynamicCast(IOUserClient, client);
    if (*userClient == nullptr) {
        client->release();
        return kIOReturnError;
    }
    return kIOReturnSuccess;
}
