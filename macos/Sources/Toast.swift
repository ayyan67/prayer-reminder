// The reminder popup: a borderless, non-activating panel in the top-right
// corner, above full-screen apps and on every Space. Mirrors src/ui_toast.c,
// styled like a macOS notification banner (blurred material, app mark).
import SwiftUI

private let W: CGFloat = 360, H: CGFloat = 112, R: CGFloat = 18

/// Buttons should work on the first click even though the panel never
/// becomes key.
private final class FirstClickHost<V: View>: NSHostingView<V> {
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
}

@MainActor
final class Toast {
    private let model: AppModel
    private let openMain: () -> Void
    private var panel: NSPanel?
    private var autoTimer: Timer?
    private var snoozeTimer: Timer?

    init(model: AppModel, openMain: @escaping () -> Void) {
        self.model = model
        self.openMain = openMain
    }

    func show(_ ev: Event) {
        let p = panel ?? makePanel()
        panel = p
        let view = ToastView(ev: ev, cfg: model.cfg,
                             snooze: { [weak self] in self?.snooze(ev) },
                             dismiss: { [weak self] in self?.close() },
                             open: { [weak self] in self?.close(); self?.openMain() })
        let fx = p.contentView!
        fx.subviews.forEach { $0.removeFromSuperview() }
        let host = FirstClickHost(rootView: view)
        host.frame = fx.bounds
        host.autoresizingMask = [.width, .height]
        fx.addSubview(host)

        // Slide in from the right as it fades in, like a notification.
        let screen = (NSScreen.screens.first ?? NSScreen.main)!.visibleFrame
        let end = NSRect(x: screen.maxX - W - 12, y: screen.maxY - H - 10, width: W, height: H)
        p.setFrame(end.offsetBy(dx: W / 3, dy: 0), display: false)
        p.alphaValue = 0
        p.orderFrontRegardless()
        NSAnimationContext.runAnimationGroup { c in
            c.duration = 0.28
            c.timingFunction = CAMediaTimingFunction(controlPoints: 0.2, 0.9, 0.3, 1)
            p.animator().setFrame(end, display: true)
            p.animator().alphaValue = 1
        }

        autoTimer?.invalidate()
        if model.cfg.toastSeconds > 0 {
            autoTimer = Timer.scheduledTimer(withTimeInterval: TimeInterval(model.cfg.toastSeconds),
                                             repeats: false) { [weak self] _ in
                MainActor.assumeIsolated { self?.close() }
            }
        }
        if model.cfg.sound { NSSound(named: "Glass")?.play() }
    }

    func close() {
        autoTimer?.invalidate()
        guard let p = panel, p.isVisible else { return }
        NSAnimationContext.runAnimationGroup({ c in
            c.duration = 0.2
            p.animator().alphaValue = 0
        }, completionHandler: { p.orderOut(nil) })
    }

    private func snooze(_ ev: Event) {
        close()
        var e = ev
        e.lead = 0                       // snoozed alerts say "now"
        snoozeTimer?.invalidate()
        snoozeTimer = Timer.scheduledTimer(withTimeInterval: TimeInterval(model.cfg.snoozeMinutes * 60),
                                           repeats: false) { [weak self] _ in
            MainActor.assumeIsolated { self?.show(e) }
        }
    }

    private func makePanel() -> NSPanel {
        let p = NSPanel(contentRect: NSRect(x: 0, y: 0, width: W, height: H),
                        styleMask: [.borderless, .nonactivatingPanel],
                        backing: .buffered, defer: false)
        p.isOpaque = false
        p.backgroundColor = .clear
        p.hasShadow = true
        p.level = .statusBar
        p.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary, .ignoresCycle]
        p.hidesOnDeactivate = false
        p.isReleasedWhenClosed = false
        p.becomesKeyOnlyIfNeeded = true

        // Blurred, appearance-aware backdrop with continuous rounded corners.
        let fx = NSVisualEffectView(frame: NSRect(x: 0, y: 0, width: W, height: H))
        fx.material = .popover
        fx.blendingMode = .behindWindow
        fx.state = .active
        fx.maskImage = Self.roundedMask(R)
        p.contentView = fx
        return p
    }

    private static func roundedMask(_ r: CGFloat) -> NSImage {
        let d = r * 2 + 1
        let img = NSImage(size: NSSize(width: d, height: d), flipped: false) { rect in
            NSColor.black.setFill()
            NSBezierPath(roundedRect: rect, xRadius: r, yRadius: r).fill()
            return true
        }
        img.capInsets = NSEdgeInsets(top: r, left: r, bottom: r, right: r)
        img.resizingMode = .stretch
        return img
    }
}

struct ToastView: View {
    let ev: Event
    let cfg: Config
    let snooze: () -> Void
    let dismiss: () -> Void
    let open: () -> Void

    private var eyebrow: String {
        ev.lead <= 0 ? "Starting now" : ev.lead == 1 ? "In 1 minute" : "In \(ev.lead) minutes"
    }
    private var line: String {
        let t = fmtTime(ev.iqamahMin, cfg)
        switch ev.prayer {
        case .sunrise:            return "Sunrise at \(t)"
        case .jumuah1, .jumuah2:  return "Khutbah at \(t)"
        default:                  return "Iqamah at \(t)"
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            // Clicking the card itself opens the app.
            HStack(alignment: .top, spacing: 12) {
                AppMark(accent: cfg.accentRaw).frame(width: 38, height: 38)
                VStack(alignment: .leading, spacing: 1) {
                    HStack(spacing: 6) {
                        Text(ev.prayer.name).font(.system(size: 15, weight: .semibold))
                        Text(eyebrow)
                            .font(.system(size: 11, weight: .semibold))
                            .foregroundColor(cfg.accentColor)
                    }
                    Text(line).font(.system(size: 12.5)).foregroundColor(T.dim)
                    Text(cfg.masjidName).font(.system(size: 11)).foregroundColor(T.faint).lineLimit(1)
                }
                Spacer(minLength: 0)
            }
            .contentShape(Rectangle())
            .onTapGesture(perform: open)

            Spacer(minLength: 0)
            HStack(spacing: 8) {
                Spacer()
                Button("Snooze \(cfg.snoozeMinutes)m", action: snooze)
                Button("Dismiss", action: dismiss).keyboardShortcut(.defaultAction)
            }
            .controlSize(.regular)
        }
        .padding(14)
        .frame(width: W, height: H)
        .overlay(RoundedRectangle(cornerRadius: R, style: .continuous)
            .strokeBorder(T.line, lineWidth: 0.5))
    }
}
