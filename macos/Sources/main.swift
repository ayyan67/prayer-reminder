// Entry point, menu bar item, popover. Mirrors the tray half of src/main.c.
import SwiftUI

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSPopoverDelegate {
    // Checked before AppModel loads (and so creates) config.ini.
    private let firstRun = !FileManager.default.fileExists(atPath: ConfigStore.iniURL.path)
    private let model = AppModel()
    private let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let popover = NSPopover()
    private var toast: Toast!
    private var anchor: NSWindow?
    private var lastTitle = ""

    func applicationDidFinishLaunching(_ note: Notification) {
        toast = Toast(model: model) { [weak self] in self?.showPopover() }
        model.onReminder = { [weak self] ev in self?.toast.show(ev) }
        model.onChange = { [weak self] in self?.updateItem() }

        if let b = item.button {
            b.image = Self.menuBarIcon()
            b.imagePosition = .imageLeading
            b.font = .monospacedDigitSystemFont(ofSize: NSFont.systemFontSize, weight: .regular)
            b.target = self
            b.action = #selector(clicked)
            b.sendAction(on: [.leftMouseUp, .rightMouseUp])
        }

        popover.behavior = .transient
        popover.animates = false
        popover.delegate = self
        popover.contentViewController = NSHostingController(rootView: MainView().environmentObject(model))

        let t = Timer(timeInterval: 1, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.model.tick() }
        }
        RunLoop.main.add(t, forMode: .common)       // keep ticking while a menu is open

        NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.model.didWake() }
        }

        ConfigStore.applyAutoStart(model.cfg)
        model.refresh()
        updateItem()

        // Say hello once so it is obvious where the app lives.
        if firstRun {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { self.showPopover() }
        }
    }

    /// Launching the app again while it runs opens the popover.
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows: Bool) -> Bool {
        showPopover()
        return false
    }

    // ---- menu bar ----------------------------------------------------------

    private func updateItem() {
        guard let b = item.button else { return }
        var title = "", tip = "Prayer Reminder \u{2014} no times yet"

        if model.paused {
            tip = "Prayer Reminder \u{2014} paused"
        } else if let ev = model.next {
            let t = fmtTime(ev.iqamahMin, model.cfg)
            tip = "\(ev.prayer.name) \(t) \u{2014} in \(fmtCountdown(ev.target.timeIntervalSinceNow))"
            if model.cfg.menuBarText {
                // Compact: "Asr 5:45" - the AM/PM is obvious from context.
                let short = t.replacingOccurrences(of: " AM", with: "").replacingOccurrences(of: " PM", with: "")
                title = " \(ev.prayer.name) \(short)"
            }
        } else if model.fetchState == .fetching {
            tip = "Prayer Reminder \u{2014} fetching times\u{2026}"
        }

        if title != lastTitle { b.title = title; lastTitle = title }
        b.toolTip = tip
        b.appearsDisabled = model.paused
    }

    @objc private func clicked() {
        if NSApp.currentEvent?.type == .rightMouseUp { showMenu() } else { togglePopover() }
    }

    private func togglePopover() {
        popover.isShown ? popover.performClose(nil) : showPopover()
    }

    private func showPopover() {
        guard let b = item.button, !popover.isShown else { return }
        model.reloadIfEdited()
        model.live = true
        model.tick()
        if let w = b.window, NSScreen.screens.contains(where: { $0.frame.intersects(w.frame) }) {
            popover.show(relativeTo: b.bounds, of: b, preferredEdge: .minY)
        } else {
            // The icon is hidden (menu bar manager, or no room beside the
            // notch): hang the popover from the top-right of the screen.
            let a = anchor ?? NSWindow(contentRect: .zero, styleMask: .borderless, backing: .buffered, defer: true)
            anchor = a
            let f = (NSScreen.screens.first ?? NSScreen.main)!.visibleFrame
            a.setFrame(NSRect(x: f.maxX - 200, y: f.maxY - 1, width: 1, height: 1), display: false)
            a.isOpaque = false
            a.backgroundColor = .clear
            a.orderFrontRegardless()
            popover.show(relativeTo: a.contentView!.bounds, of: a.contentView!, preferredEdge: .minY)
        }
        popover.contentViewController?.view.window?.makeKey()
        NSApp.activate(ignoringOtherApps: true)
    }

    func popoverDidClose(_ note: Notification) {
        model.live = false
        anchor?.orderOut(nil)
    }

    private func showMenu() {
        let m = NSMenu()
        if let ev = model.next {
            let head = NSMenuItem(title: "Next:  \(ev.prayer.name)  \(fmtTime(ev.iqamahMin, model.cfg))",
                                  action: nil, keyEquivalent: "")
            head.isEnabled = false
            m.addItem(head)
            m.addItem(.separator())
        }
        m.addItem(menuItem("Open") { [weak self] in self?.model.tab = 0; self?.showPopover() })
        m.addItem(menuItem("Settings") { [weak self] in self?.model.tab = 1; self?.showPopover() })
        m.addItem(menuItem("Refresh times now") { [weak self] in self?.model.refresh() })
        m.addItem(.separator())

        let pause = NSMenu()
        pause.addItem(menuItem("For 1 hour") { [weak self] in self?.model.pause(hours: 1) })
        pause.addItem(menuItem("For 4 hours") { [weak self] in self?.model.pause(hours: 4) })
        pause.addItem(menuItem("Until tomorrow") { [weak self] in self?.model.pauseUntilTomorrow() })
        if model.paused {
            pause.addItem(.separator())
            pause.addItem(menuItem("Resume reminders") { [weak self] in self?.model.resume() })
        }
        let p = NSMenuItem(title: model.paused ? "Reminders paused" : "Pause reminders",
                           action: nil, keyEquivalent: "")
        p.state = model.paused ? .on : .off
        p.submenu = pause
        m.addItem(p)

        m.addItem(.separator())
        m.addItem(menuItem("Open masjid website") { [weak self] in
            if let s = self?.model.cfg.website { openURL(s) }
        })
        m.addItem(.separator())
        m.addItem(menuItem("Quit", key: "q") { NSApp.terminate(nil) })

        item.menu = m
        item.button?.performClick(nil)       // shows the menu under the item
        item.menu = nil                      // left click goes back to the popover
    }

    private func menuItem(_ title: String, key: String = "", _ run: @escaping () -> Void) -> NSMenuItem {
        let i = ClosureMenuItem(title: title, action: #selector(ClosureMenuItem.fire), keyEquivalent: key)
        i.target = i
        i.run = run
        return i
    }

    /// The crescent as a template image, so it follows the menu bar's colour.
    static func menuBarIcon() -> NSImage {
        let img = NSImage(size: NSSize(width: 16, height: 16), flipped: true) { r in
            NSColor.black.setFill()
            Path(Crescent().path(in: r.insetBy(dx: 0.5, dy: 0.5)).cgPath).fill()
            return true
        }
        img.isTemplate = true
        return img
    }
}

private extension Path {
    func fill() {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        ctx.addPath(cgPath)
        ctx.fillPath()
    }
}

private final class ClosureMenuItem: NSMenuItem {
    var run: () -> Void = {}
    @objc func fire() { run() }
}

// ---- entry ---------------------------------------------------------------

MainActor.assumeIsolated {
    let app = NSApplication.shared
    let delegate = AppDelegate()
    app.delegate = delegate
    app.setActivationPolicy(.accessory)      // menu bar only, no Dock icon
    app.run()
}
