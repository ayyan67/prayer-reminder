// The reminder popup: a borderless, non-activating panel in the top-right
// corner, above full-screen apps and on every Space. Mirrors src/ui_toast.c.
import SwiftUI

private let W: CGFloat = 372, H: CGFloat = 156

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
        p.contentView = FirstClickHost(rootView: view)

        // Slide in from the right as it fades in.
        let screen = (NSScreen.screens.first ?? NSScreen.main)!.visibleFrame
        let end = NSRect(x: screen.maxX - W - 14, y: screen.maxY - H - 10, width: W, height: H)
        p.setFrame(end.offsetBy(dx: 24, dy: 0), display: false)
        p.alphaValue = 0
        p.orderFrontRegardless()
        NSAnimationContext.runAnimationGroup { c in
            c.duration = 0.22
            c.timingFunction = CAMediaTimingFunction(name: .easeOut)
            p.animator().setFrame(end, display: true)
            p.animator().alphaValue = 1
        }
        p.invalidateShadow()

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
            c.duration = 0.18
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
        p.appearance = NSAppearance(named: .darkAqua)
        return p
    }
}

struct ToastView: View {
    let ev: Event
    let cfg: Config
    let snooze: () -> Void
    let dismiss: () -> Void
    let open: () -> Void

    private var eyebrow: String {
        ev.lead <= 0 ? "STARTING NOW" : ev.lead == 1 ? "IN 1 MINUTE" : "IN \(ev.lead) MINUTES"
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
        ZStack(alignment: .bottomTrailing) {
            // Clicking the card itself opens the app.
            HStack(spacing: 0) {
                Rectangle().fill(cfg.accentColor).frame(width: 4)
                VStack(alignment: .leading, spacing: 2) {
                    Text(eyebrow)
                        .font(.system(size: 10.5, weight: .bold))
                        .foregroundColor(cfg.accentColor)
                    Text(ev.prayer.name)
                        .font(.system(size: 28, weight: .bold))
                        .foregroundColor(T.text)
                    Text(line)
                        .font(.system(size: 12.5))
                        .foregroundColor(T.dim)
                    Spacer()
                }
                .padding(.leading, 18).padding(.top, 16)
                Spacer()
            }
            .contentShape(Rectangle())
            .onTapGesture(perform: open)

            HStack(spacing: 8) {
                Button("Snooze \(cfg.snoozeMinutes)m", action: snooze).buttonStyle(ToastButton(cfg: cfg))
                Button("Dismiss", action: dismiss).buttonStyle(ToastButton(cfg: cfg, primary: true))
            }
            .padding(16)
        }
        .frame(width: W, height: H)
        .background(T.card)
        .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous)
            .strokeBorder(Color(hex: "363E48"), lineWidth: 1))
        .environment(\.colorScheme, .dark)
    }
}

/// PillButton needs the model from the environment; the popup is standalone.
private struct ToastButton: ButtonStyle {
    let cfg: Config
    var primary = false

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 12.5, weight: .semibold))
            .foregroundColor(primary ? cfg.onAccent : T.text)
            .frame(width: 96, height: 32)
            .background(RoundedRectangle(cornerRadius: 8).fill(primary ? cfg.accentColor : T.bg))
            .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(primary ? Color.clear : T.line, lineWidth: 1))
            .opacity(configuration.isPressed ? 0.75 : 1)
            .contentShape(Rectangle())
    }
}
