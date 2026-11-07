// Xbox controller DriverKit port - host app.
//
// A DriverKit extension can only be installed by an app that embeds it in
// Contents/Library/SystemExtensions. This app activates / deactivates it.
// GPL v2 or later, see Licence.txt.

import SwiftUI
import SystemExtensions

// Must match the dext target's PRODUCT_BUNDLE_IDENTIFIER.
let driverIdentifier = "com.mice.driver.XboxGamepadDriver"

final class ExtensionManager: NSObject, ObservableObject, OSSystemExtensionRequestDelegate {
    @Published var status = "Not installed (or unknown)"

    func activate() {
        status = "Requesting activation…"
        let request = OSSystemExtensionRequest.activationRequest(forExtensionWithIdentifier: driverIdentifier,
                                                                 queue: .main)
        request.delegate = self
        OSSystemExtensionManager.shared.submitRequest(request)
    }

    func deactivate() {
        status = "Requesting removal…"
        let request = OSSystemExtensionRequest.deactivationRequest(forExtensionWithIdentifier: driverIdentifier,
                                                                   queue: .main)
        request.delegate = self
        OSSystemExtensionManager.shared.submitRequest(request)
    }

    // MARK: OSSystemExtensionRequestDelegate

    func request(_ request: OSSystemExtensionRequest,
                 actionForReplacingExtension existing: OSSystemExtensionProperties,
                 withExtension ext: OSSystemExtensionProperties) -> OSSystemExtensionRequest.ReplacementAction {
        status = "Replacing \(existing.bundleShortVersion) with \(ext.bundleShortVersion)"
        return .replace
    }

    func requestNeedsUserApproval(_ request: OSSystemExtensionRequest) {
        status = "Approve the driver in System Settings → General → Login Items & Extensions → Driver Extensions"
    }

    func request(_ request: OSSystemExtensionRequest,
                 didFinishWithResult result: OSSystemExtensionRequest.Result) {
        switch result {
        case .completed:        status = "Done. Unplug and replug your controller."
        case .willCompleteAfterReboot: status = "Done. Restart your Mac to finish."
        @unknown default:       status = "Finished (\(result.rawValue))"
        }
    }

    func request(_ request: OSSystemExtensionRequest, didFailWithError error: Error) {
        status = "Failed: \(error.localizedDescription)"
    }
}

struct ContentView: View {
    @StateObject private var manager = ExtensionManager()

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Xbox Controller Driver").font(.title2).bold()
            Text("Adds support for wired Xbox 360, Xbox One and original Xbox controllers, including many third-party pads.")
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                Button("Install Driver") { manager.activate() }
                    .keyboardShortcut(.defaultAction)
                Button("Uninstall") { manager.deactivate() }
            }
            Text(manager.status)
                .font(.callout)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(24)
        .frame(width: 420)
    }
}

@main
struct XboxDriverApp: App {
    var body: some Scene {
        WindowGroup { ContentView() }
            .windowResizability(.contentSize)
    }
}
