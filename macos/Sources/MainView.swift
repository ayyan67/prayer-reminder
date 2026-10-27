// The popover: header, next-up card, Today / Settings. Mirrors src/ui_main.c.
import SwiftUI

struct MainView: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Header().padding(.horizontal, 20).padding(.top, 18)
            NextUpCard().padding(.horizontal, 16).padding(.top, 14)
            Segments(labels: ["Today", "Settings"], selection: $m.tab)
                .padding(.horizontal, 16).padding(.top, 14)
            Rectangle().fill(T.line).frame(height: 1)
                .padding(.horizontal, 16).padding(.top, 8)
            if m.tab == 0 { TodayList() } else { SettingsList() }
            Footer().padding(.horizontal, 16).padding(.bottom, 14)
        }
        .frame(width: 360, height: 610)
        .background(T.bg)
        .environment(\.colorScheme, .dark)
    }
}

// ---- header --------------------------------------------------------------

private struct Header: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        let f = DateFormatter()
        f.dateFormat = "EEEE, MMMM d"
        return VStack(alignment: .leading, spacing: 3) {
            Text(m.cfg.masjidName.uppercased())
                .font(.system(size: 10.5, weight: .bold))
                .foregroundColor(m.cfg.accentColor)
                .lineLimit(1)
            Text(f.string(from: m.now))
                .font(.system(size: 21, weight: .semibold))
                .foregroundColor(T.text)
            Text(m.sched.today?.hijri ?? " ")
                .font(.system(size: 11.5))
                .foregroundColor(T.dim)
        }
    }
}

private struct NextUpCard: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        HStack(spacing: 0) {
            RoundedRectangle(cornerRadius: 1.5).fill(m.cfg.accentColor).frame(width: 3)
                .padding(.vertical, 14)
            VStack(alignment: .leading, spacing: 2) { content }
                .padding(.leading, 16)
            Spacer(minLength: 0)
        }
        .frame(height: 104)
        .background(RoundedRectangle(cornerRadius: 12).fill(T.card))
    }

    @ViewBuilder private var content: some View {
        if let ev = m.next {
            Text("NEXT UP  \u{00B7}  \(ev.prayer.name)")
                .font(.system(size: 10.5, weight: .bold))
                .foregroundColor(m.cfg.accentColor)
            Text(fmtTime(ev.iqamahMin, m.cfg))
                .font(.system(size: 34, weight: .bold))
                .foregroundColor(T.text)
            // countdown to the prayer itself, not to the reminder
            let cd = fmtCountdown(ev.target.timeIntervalSince(m.now))
            Text(ev.lead > 0 ? "in \(cd)  \u{00B7}  reminder at \(fmtTime(ev.iqamahMin - ev.lead, m.cfg))"
                             : "in \(cd)")
                .font(.system(size: 11.5))
                .foregroundColor(T.dim)
        } else {
            Text(m.fetchState == .fetching ? "Fetching times\u{2026}" : "No times yet")
                .font(.system(size: 17, weight: .semibold))
                .foregroundColor(T.text)
            Text(m.fetchState == .failed ? "Could not reach the site \u{2014} retrying" : "Hang on a moment")
                .font(.system(size: 11.5))
                .foregroundColor(T.dim)
        }
    }
}

// ---- today ---------------------------------------------------------------

private struct TodayList: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("PRAYER")
                Spacer()
                Text("ADHAN").frame(width: 70, alignment: .trailing)
                Text("IQAMAH").frame(width: 82, alignment: .trailing)
            }
            .font(.system(size: 9.5, weight: .bold))
            .foregroundColor(T.faint)
            .padding(.horizontal, 12)
            .padding(.vertical, 8)

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
        .padding(.horizontal, 16)
        .padding(.top, 2)
    }

    private func visible(_ dt: DayTimes, _ p: Prayer) -> Bool {
        Sched.isActive(dt, p, m.cfg) && (Sched.target(dt, p, m.cfg) != nil || dt.adhan[p.rawValue] != nil)
    }

    private func row(_ dt: DayTimes, _ p: Prayer, isNext: Bool) -> some View {
        let on = m.cfg.prayerOn[p.rawValue]
        let adhan = dt.adhan[p.rawValue].map { $0 + m.cfg.timeShift }
        return HStack(spacing: 0) {
            Circle()
                .fill(isNext ? m.cfg.accentColor : (on ? Color.clear : Color(hex: "2C323A")))
                .frame(width: 6, height: 6)
                .frame(width: 20)
            Text(p.name)
                .font(.system(size: 13.5, weight: isNext ? .semibold : .regular))
                .foregroundColor(on ? T.text : T.faint)
            Spacer()
            // Sunrise has no congregation, so its one time sits in the main
            // column rather than being printed twice.
            Text(p == .sunrise || adhan == nil ? "" : fmtTime(adhan, m.cfg))
                .font(.system(size: 11.5))
                .foregroundColor(T.dim)
                .frame(width: 70, alignment: .trailing)
            Text(fmtTime(Sched.target(dt, p, m.cfg), m.cfg))
                .font(.system(size: 13.5, weight: .semibold))
                .foregroundColor(isNext ? m.cfg.accentColor : (on ? T.text : T.faint))
                .frame(width: 82, alignment: .trailing)
        }
        .padding(.trailing, 12)
        .frame(height: 38)
        .background(RoundedRectangle(cornerRadius: 9).fill(isNext ? T.card : Color.clear))
    }
}

// ---- settings ------------------------------------------------------------

private struct SettingsList: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                section("Reminders")
                stepRow("First reminder", "minutes before iqamah",
                        text: m.cfg.lead1On ? "\(m.cfg.lead1Min) min" : "Off",
                        minus: { stepLead(\.lead1On, \.lead1Min, -5) },
                        plus: { stepLead(\.lead1On, \.lead1Min, 5) })
                stepRow("Second reminder", "a last nudge",
                        text: m.cfg.lead2On ? "\(m.cfg.lead2Min) min" : "Off",
                        minus: { stepLead(\.lead2On, \.lead2Min, -5) },
                        plus: { stepLead(\.lead2On, \.lead2Min, 5) })
                stepRow("Auto-dismiss", "how long the popup stays",
                        text: m.cfg.toastSeconds > 0 ? "\(m.cfg.toastSeconds) sec" : "Never",
                        minus: { m.cfg.toastSeconds = max(0, m.cfg.toastSeconds - 15) },
                        plus: { m.cfg.toastSeconds = min(300, m.cfg.toastSeconds + 15) })
                stepRow("Snooze length", "when you hit snooze",
                        text: "\(m.cfg.snoozeMinutes) min",
                        minus: { m.cfg.snoozeMinutes = max(1, m.cfg.snoozeMinutes - 1) },
                        plus: { m.cfg.snoozeMinutes = min(30, m.cfg.snoozeMinutes + 1) })
                toggleRow("Alert at iqamah time", "one more when it starts", $m.cfg.atIqamah)
                toggleRow("Play a sound", "a soft chime with the popup", $m.cfg.sound)

                section("Prayers")
                ForEach(Prayer.allCases, id: \.self) { p in prayerRow(p) }

                section("Friday")
                Segments(labels: FridayMode.allCases.map(\.label),
                         selection: Binding(get: { m.cfg.fridayMode.rawValue },
                                            set: { m.cfg.fridayMode = FridayMode(rawValue: $0)! }),
                         height: 30)
                    .padding(4)
                    .background(RoundedRectangle(cornerRadius: 10).fill(T.card))

                section("General")
                toggleRow("Open at login", "start quietly in the menu bar", $m.cfg.autoStart)
                toggleRow("Next prayer in menu bar", "show \u{201C}Asr 5:45\u{201D} beside the icon", $m.cfg.menuBarText)
                toggleRow("24-hour clock", "show 17:45 instead of 5:45 PM", $m.cfg.use24h)
                stepRow("Daily refresh", "when to pull fresh times",
                        text: fmtTime(m.cfg.refreshHour * 60, m.cfg),
                        minus: { m.cfg.refreshHour = (m.cfg.refreshHour + 23) % 24 },
                        plus: { m.cfg.refreshHour = (m.cfg.refreshHour + 1) % 24 })
                stepRow("Time offset", "if your clock differs",
                        text: String(format: "%+d min", m.cfg.timeShift),
                        minus: { m.cfg.timeShift = max(-60, m.cfg.timeShift - 1) },
                        plus: { m.cfg.timeShift = min(60, m.cfg.timeShift + 1) })

                section("Masjid")
                label(m.cfg.masjidName, "Masjidal id \(m.cfg.masjidalId) \u{00B7} \(m.cfg.website)")
                    .padding(.vertical, 6)
                HStack(spacing: 8) {
                    Button("Edit config.ini") { ConfigStore.openInEditor(m.cfg) }
                    Button("Open data folder") { NSWorkspace.shared.open(ConfigStore.dir) }
                }
                .buttonStyle(PillButton())
                .padding(.top, 4)

                section("Try it")
                Button("Preview reminder") { m.preview() }
                    .buttonStyle(PillButton())
                    .padding(.bottom, 14)
            }
            .padding(.horizontal, 20)
        }
    }

    // ---- rows --------------------------------------------------------------

    private func section(_ s: String) -> some View {
        Text(s.uppercased())
            .font(.system(size: 9.5, weight: .bold))
            .foregroundColor(T.faint)
            .padding(.top, 16).padding(.bottom, 4)
    }

    private func label(_ title: String, _ sub: String) -> some View {
        VStack(alignment: .leading, spacing: 1) {
            Text(title).font(.system(size: 13, weight: .medium)).foregroundColor(T.text)
            Text(sub).font(.system(size: 10.5)).foregroundColor(T.faint).lineLimit(1)
        }
    }

    private func stepRow(_ t: String, _ s: String, text: String,
                         minus: @escaping () -> Void, plus: @escaping () -> Void) -> some View {
        HStack { label(t, s); Spacer(); StepperBox(text: text, minus: minus, plus: plus) }
            .frame(height: 44)
    }

    private func toggleRow(_ t: String, _ s: String, _ b: Binding<Bool>) -> some View {
        HStack {
            label(t, s)
            Spacer()
            Toggle("", isOn: b).toggleStyle(.switch).tint(m.cfg.accentColor).labelsHidden()
        }
        .frame(height: 44)
    }

    /// Per-prayer lead: Off, Default (the global leads), or a custom lead
    /// that replaces them.
    private func prayerRow(_ p: Prayer) -> some View {
        let i = p.rawValue
        let v = m.cfg.prayerOn[i] ? m.cfg.prayerLead[i] : -2
        let text = v == -2 ? "Off" : v == -1 ? "Default" : "\(v) min"
        let time = m.sched.today.flatMap { Sched.target($0, p, m.cfg) }
        return HStack {
            Text(p.name)
                .font(.system(size: 13, weight: .medium))
                .foregroundColor(m.cfg.prayerOn[i] ? T.text : T.faint)
            Spacer()
            Text(time.map { fmtTime($0, m.cfg) } ?? "")
                .font(.system(size: 11)).foregroundColor(T.faint)
                .padding(.trailing, 6)
            StepperBox(text: text,
                       minus: { setPrayerLead(i, v == -2 ? -2 : v == -1 ? -2 : v <= 5 ? -1 : v - 5) },
                       plus: { setPrayerLead(i, v == -2 ? -1 : v == -1 ? 5 : min(120, v + 5)) })
        }
        .frame(height: 40)
    }

    private func setPrayerLead(_ i: Int, _ v: Int) {
        m.cfg.prayerOn[i] = v != -2
        if v != -2 { m.cfg.prayerLead[i] = v }
    }

    /// Global leads step by 5; 0 means Off.
    private func stepLead(_ on: WritableKeyPath<Config, Bool>, _ min_: WritableKeyPath<Config, Int>, _ d: Int) {
        let cur = m.cfg[keyPath: on] ? m.cfg[keyPath: min_] : 0
        let v = max(0, min(120, cur + d))
        m.cfg[keyPath: on] = v > 0
        if v > 0 { m.cfg[keyPath: min_] = v }
    }
}

// ---- footer --------------------------------------------------------------

private struct Footer: View {
    @EnvironmentObject var m: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(status)
                .font(.system(size: 10.5))
                .foregroundColor(m.fetchState == .failed ? T.warn : T.faint)
                .lineLimit(1)
            HStack(spacing: 8) {
                Button("Refresh now") { m.refresh() }.buttonStyle(PillButton(primary: true))
                Button("Open \(siteLabel)") { openURL(m.cfg.website) }.buttonStyle(PillButton())
                Spacer()
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
                    Image(systemName: m.paused ? "bell.slash" : "ellipsis")
                }
                .menuStyle(.borderlessButton)
                .menuIndicator(.hidden)
                .fixedSize()
                .frame(width: 30, height: 30)
                .background(RoundedRectangle(cornerRadius: 8).fill(T.card))
                .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(T.line, lineWidth: 1))
            }
        }
        .padding(.top, 10)
    }

    private var status: String {
        if m.paused, let u = m.pauseUntil {
            return "Reminders paused until \(fmtTime(Calendar.current.component(.hour, from: u) * 60 + Calendar.current.component(.minute, from: u), m.cfg))"
        }
        switch m.fetchState {
        case .fetching: return "Refreshing\u{2026}"
        case .failed:   return "Offline \u{2014} showing cached times (updated \(fmtAgo(m.sched.fetchedAt)))"
        case .idle:     return "Updated \(fmtAgo(m.sched.fetchedAt)) \u{00B7} \(m.sched.days.count) days cached"
        }
    }

    /// "https://www.mcnjonline.com/" -> "mcnjonline"
    private var siteLabel: String {
        var h = URL(string: m.cfg.website)?.host ?? "website"
        if h.hasPrefix("www.") { h.removeFirst(4) }
        return String(h.split(separator: ".").first ?? "website")
    }
}

func openURL(_ s: String) {
    if let u = URL(string: s) { NSWorkspace.shared.open(u) }
}
