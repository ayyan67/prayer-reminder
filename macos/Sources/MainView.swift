// The popover: header, next-up card, Today / Settings. Mirrors src/ui_main.c,
// but drawn with native controls and the system's light or dark appearance.
import SwiftUI

struct MainView: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Header().padding(.horizontal, 18).padding(.top, 16)
            NextUpCard().padding(.horizontal, 14).padding(.top, 12)
            Picker("", selection: $m.tab) {
                Text("Today").tag(0)
                Text("Settings").tag(1)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .fixedSize()
            .frame(maxWidth: .infinity)
            .padding(.top, 12).padding(.bottom, 4)

            if m.tab == 0 { TodayList() } else { SettingsList() }
            Divider()
            Footer().padding(.horizontal, 14).padding(.vertical, 9)
        }
        .frame(width: 360, height: 590)
    }
}

// ---- header --------------------------------------------------------------

private struct Header: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        HStack(spacing: 12) {
            AppMark(accent: m.cfg.accentRaw).frame(width: 38, height: 38)
            VStack(alignment: .leading, spacing: 1) {
                Text(m.cfg.masjidName)
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundColor(m.cfg.accentColor)
                    .lineLimit(1)
                Text(m.now.formatted(.dateTime.weekday(.wide).month(.wide).day()))
                    .font(.system(size: 17, weight: .semibold))
                Text(m.sched.today?.hijri ?? " ")
                    .font(.system(size: 11))
                    .foregroundColor(T.dim)
            }
        }
    }
}

private struct NextUpCard: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 3) { content }
            .padding(.horizontal, 16)
            .frame(maxWidth: .infinity, minHeight: 96, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 12, style: .continuous).fill(T.fill))
    }

    @ViewBuilder private var content: some View {
        if let ev = m.next {
            Text("Next up \u{00B7} \(ev.prayer.name)")
                .font(.system(size: 11, weight: .semibold))
                .foregroundColor(m.cfg.accentColor)
            Text(fmtTime(ev.iqamahMin, m.cfg))
                .font(.system(size: 32, weight: .semibold, design: .rounded))
                .monospacedDigit()
            // countdown to the prayer itself, not to the reminder
            HStack(spacing: 10) {
                Label("in \(fmtCountdown(ev.target.timeIntervalSince(m.now)))", systemImage: "clock")
                if ev.lead > 0 {
                    Label("reminder \(fmtTime(ev.iqamahMin - ev.lead, m.cfg))", systemImage: "bell")
                }
            }
            .font(.system(size: 11))
            .foregroundColor(T.dim)
            .labelStyle(TightLabel())
        } else {
            Text(m.fetchState == .fetching ? "Fetching times\u{2026}" : "No times yet")
                .font(.system(size: 16, weight: .semibold))
            Text(m.fetchState == .failed ? "Could not reach the site \u{2014} retrying" : "Hang on a moment")
                .font(.system(size: 11))
                .foregroundColor(T.dim)
        }
    }
}

private struct TightLabel: LabelStyle {
    func makeBody(configuration: Configuration) -> some View {
        HStack(spacing: 4) { configuration.icon.imageScale(.small); configuration.title }
    }
}

// ---- today ---------------------------------------------------------------

private struct TodayList: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        VStack(spacing: 2) {
            HStack {
                Text("Prayer")
                Spacer()
                Text("Adhan").frame(width: 70, alignment: .trailing)
                Text("Iqamah").frame(width: 80, alignment: .trailing)
            }
            .font(.system(size: 10.5, weight: .medium))
            .foregroundColor(T.faint)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)

            if let dt = m.sched.today {
                let next = m.next?.prayer
                ForEach(Prayer.allCases.filter { visible(dt, $0) }, id: \.self) { p in
                    row(dt, p, isNext: p == next)
                }
            } else {
                Text("Times unavailable. Try Refresh.")
                    .font(.system(size: 12))
                    .foregroundColor(T.dim)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(12)
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 14)
        .padding(.top, 4)
    }

    private func visible(_ dt: DayTimes, _ p: Prayer) -> Bool {
        Sched.isActive(dt, p, m.cfg) && (Sched.target(dt, p, m.cfg) != nil || dt.adhan[p.rawValue] != nil)
    }

    private func row(_ dt: DayTimes, _ p: Prayer, isNext: Bool) -> some View {
        let on = m.cfg.prayerOn[p.rawValue]
        let adhan = dt.adhan[p.rawValue].map { $0 + m.cfg.timeShift }
        return HStack(spacing: 0) {
            Image(systemName: on ? "bell.fill" : "bell.slash")
                .font(.system(size: 9))
                .foregroundColor(isNext ? m.cfg.accentColor : T.faint)
                .opacity(isNext || !on ? 1 : 0)
                .frame(width: 20)
            Text(p.name)
                .font(.system(size: 13, weight: isNext ? .semibold : .regular))
                .foregroundColor(on ? T.text : T.faint)
            Spacer()
            // Sunrise has no congregation, so its one time sits in the main
            // column rather than being printed twice.
            Text(p == .sunrise || adhan == nil ? "" : fmtTime(adhan, m.cfg))
                .font(.system(size: 11.5))
                .foregroundColor(T.dim)
                .frame(width: 70, alignment: .trailing)
            Text(fmtTime(Sched.target(dt, p, m.cfg), m.cfg))
                .font(.system(size: 13, weight: .semibold))
                .foregroundColor(isNext ? m.cfg.accentColor : (on ? T.text : T.faint))
                .frame(width: 80, alignment: .trailing)
        }
        .monospacedDigit()
        .padding(.trailing, 12)
        .frame(height: 34)
        .background(RoundedRectangle(cornerRadius: 8, style: .continuous)
            .fill(isNext ? m.cfg.accentColor.opacity(0.14) : Color.clear))
    }
}

// ---- settings ------------------------------------------------------------

private struct SettingsList: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        Form {
            Section("Reminders") {
                menuRow("First reminder", "before iqamah", lead(\.lead1On, \.lead1Min),
                        [0, 5, 10, 15, 20, 25, 30, 45, 60], minutes)
                menuRow("Second reminder", "a last nudge", lead(\.lead2On, \.lead2Min),
                        [0, 5, 10, 15, 20, 25, 30, 45, 60], minutes)
                Toggle(isOn: $m.cfg.atIqamah) { rowLabel("Alert at iqamah time", "one more when it starts") }
                Toggle(isOn: $m.cfg.sound) { rowLabel("Play a sound", "a soft chime with the popup") }
                menuRow("Auto-dismiss", "how long the popup stays", $m.cfg.toastSeconds,
                        [0, 15, 30, 45, 60, 90, 120, 300]) { $0 == 0 ? "Never" : "\($0) sec" }
                menuRow("Snooze length", "when you hit Snooze", $m.cfg.snoozeMinutes,
                        [1, 2, 3, 5, 10, 15, 20, 30], minutes)
            }

            Section {
                ForEach(Prayer.allCases, id: \.self) { p in prayerRow(p) }
            } header: {
                Text("Prayers")
            } footer: {
                Text("Default uses the reminders above; a time replaces them for that prayer.")
                    .font(.system(size: 10.5)).foregroundColor(T.dim)
            }

            Section("Friday") {
                Picker(selection: $m.cfg.fridayMode) {
                    ForEach(FridayMode.allCases, id: \.self) { Text($0.label).tag($0) }
                } label: { rowLabel("Remind me for", "replaces Dhuhr on Fridays") }
            }

            Section("General") {
                Toggle(isOn: $m.cfg.autoStart) { rowLabel("Open at login", "start quietly in the menu bar") }
                Toggle(isOn: $m.cfg.menuBarText) { rowLabel("Next prayer in menu bar", "\u{201C}Asr 5:45\u{201D} beside the icon") }
                Toggle(isOn: $m.cfg.use24h) { rowLabel("24-hour clock", "17:45 instead of 5:45 PM") }
                menuRow("Daily refresh", "when to pull fresh times", $m.cfg.refreshHour,
                        Array(0..<24)) { fmtTime($0 * 60, m.cfg) }
                LabeledContent {
                    Stepper(String(format: "%+d min", m.cfg.timeShift),
                            value: $m.cfg.timeShift, in: -60...60)
                        .monospacedDigit()
                } label: { rowLabel("Time offset", "if your clock differs") }
            }

            Section("Masjid") {
                rowLabel(m.cfg.masjidName, "Masjidal id \(m.cfg.masjidalId) \u{00B7} \(m.cfg.website)")
                HStack {
                    Button("Edit config.ini") { ConfigStore.openInEditor(m.cfg) }
                    Button("Open data folder") { NSWorkspace.shared.open(ConfigStore.dir) }
                    Spacer()
                }
            }

            Section {
                HStack {
                    rowLabel("Preview reminder", "see the popup with these settings")
                    Spacer()
                    Button("Preview") { m.preview() }
                }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
        .controlSize(.small)
    }

    // ---- rows --------------------------------------------------------------

    private func rowLabel(_ title: String, _ sub: String) -> some View {
        VStack(alignment: .leading, spacing: 1) {
            Text(title).font(.system(size: 12.5))
            Text(sub).font(.system(size: 10.5)).foregroundColor(T.dim).lineLimit(1)
        }
    }

    private func minutes(_ v: Int) -> String { v <= 0 ? "Off" : "\(v) min" }

    /// A pop-up menu of values. A hand-edited value that is not on the list
    /// (lead1Min=7, say) is added so it shows instead of a blank.
    private func menuRow(_ t: String, _ s: String, _ value: Binding<Int>, _ options: [Int],
                         _ label: @escaping (Int) -> String) -> some View {
        let opts = options.contains(value.wrappedValue) ? options : (options + [value.wrappedValue]).sorted()
        return Picker(selection: value) {
            ForEach(opts, id: \.self) { Text(label($0)).tag($0) }
        } label: { rowLabel(t, s) }
    }

    /// Global leads as one value: 0 means Off.
    private func lead(_ on: WritableKeyPath<Config, Bool>, _ mins: WritableKeyPath<Config, Int>) -> Binding<Int> {
        Binding(get: { m.cfg[keyPath: on] ? m.cfg[keyPath: mins] : 0 },
                set: { v in
                    m.cfg[keyPath: on] = v > 0
                    if v > 0 { m.cfg[keyPath: mins] = v }
                })
    }

    /// Per-prayer lead: Off (-2), Default (-1, the global leads), or a custom
    /// lead that replaces them.
    private func prayerRow(_ p: Prayer) -> some View {
        let i = p.rawValue
        let time = m.sched.today.flatMap { Sched.target($0, p, m.cfg) }
        let value = Binding<Int>(
            get: { m.cfg.prayerOn[i] ? m.cfg.prayerLead[i] : -2 },
            set: { v in
                m.cfg.prayerOn[i] = v != -2
                if v != -2 { m.cfg.prayerLead[i] = v }
            })
        let sub = time.map { (p == .sunrise ? "sunrise " : p == .jumuah1 || p == .jumuah2 ? "khutbah " : "iqamah ") + fmtTime($0, m.cfg) } ?? " "
        return menuRow(p.name, sub, value, [-2, -1, 5, 10, 15, 20, 30, 45, 60]) {
            $0 == -2 ? "Off" : $0 == -1 ? "Default" : "\($0) min before"
        }
    }
}

// ---- footer --------------------------------------------------------------

private struct Footer: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        HStack(spacing: 12) {
            Text(status)
                .font(.system(size: 10.5))
                .foregroundColor(m.fetchState == .failed ? T.warn : T.dim)
                .lineLimit(1)
            Spacer()
            Button { m.refresh() } label: {
                if m.fetchState == .fetching {
                    ProgressView().controlSize(.mini).frame(width: 16, height: 16)
                } else {
                    Image(systemName: "arrow.clockwise").frame(width: 16, height: 16)
                }
            }
            .help("Refresh times now")
            Button { openURL(m.cfg.website) } label: { Image(systemName: "safari").frame(width: 16, height: 16) }
                .help("Open \(siteLabel)")
            Menu {
                if m.paused {
                    Button("Resume reminders") { m.resume() }
                    Divider()
                }
                Button("Pause for 1 hour") { m.pause(hours: 1) }
                Button("Pause for 4 hours") { m.pause(hours: 4) }
                Button("Pause until tomorrow") { m.pauseUntilTomorrow() }
                Divider()
                Button("Quit Prayer Reminder") { NSApp.terminate(nil) }
            } label: {
                Image(systemName: m.paused ? "bell.slash" : "ellipsis.circle")
            }
            .menuStyle(.borderlessButton)
            .menuIndicator(.hidden)
            .fixedSize()
        }
        .buttonStyle(.borderless)
        .font(.system(size: 13))
        .foregroundColor(T.dim)
    }

    private var status: String {
        if m.paused, let u = m.pauseUntil {
            let c = Calendar.current.dateComponents([.hour, .minute], from: u)
            return "Paused until \(fmtTime(c.hour! * 60 + c.minute!, m.cfg))"
        }
        switch m.fetchState {
        case .fetching: return "Refreshing\u{2026}"
        case .failed:   return "Offline \u{00B7} cached times from \(fmtAgo(m.sched.fetchedAt))"
        case .idle:     return "Updated \(fmtAgo(m.sched.fetchedAt)) \u{00B7} \(m.sched.days.count) days cached"
        }
    }

    /// "https://www.mcnjonline.com/" -> "mcnjonline.com"
    private var siteLabel: String {
        var h = URL(string: m.cfg.website)?.host ?? "website"
        if h.hasPrefix("www.") { h.removeFirst(4) }
        return h
    }
}

func openURL(_ s: String) {
    if let u = URL(string: s) { NSWorkspace.shared.open(u) }
}
