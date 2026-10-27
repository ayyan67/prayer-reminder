// Turns a day's times into reminder events. Mirrors src/sched.c.
import Foundation

enum Sched {
    /// Which congregation actually happens on a given day. Friday swaps Dhuhr
    /// for the Jumu'ah the user attends; the rest of the week has no Jumu'ah.
    static func isActive(_ dt: DayTimes, _ p: Prayer, _ cfg: Config) -> Bool {
        let friday = dt.day.dow == 5
        switch p {
        case .jumuah1: return friday && (cfg.fridayMode == .jumuah1 || cfg.fridayMode == .both)
        case .jumuah2: return friday && (cfg.fridayMode == .jumuah2 || cfg.fridayMode == .both)
        case .dhuhr where friday: return cfg.fridayMode == .dhuhr
        default: return true
        }
    }

    /// The moment that matters for a row: iqamah, except Sunrise, which has
    /// no congregation and is really "Fajr ends".
    static func target(_ dt: DayTimes, _ p: Prayer, _ cfg: Config) -> Int? {
        guard let t = (p == .sunrise ? dt.adhan[p.rawValue] : dt.iqamah[p.rawValue]) else { return nil }
        let s = t + cfg.timeShift
        return (0..<24 * 60).contains(s) ? s : nil
    }

    private static func events(for dt: DayTimes, _ cfg: Config, into out: inout [Event]) {
        for p in Prayer.allCases where cfg.prayerOn[p.rawValue] && isActive(dt, p, cfg) {
            guard let tgt = target(dt, p, cfg) else { continue }

            var leads: [Int] = []
            if cfg.prayerLead[p.rawValue] >= 0 {
                leads.append(cfg.prayerLead[p.rawValue])   // replaces the two global reminders
            } else {
                if cfg.lead1On { leads.append(cfg.lead1Min) }
                if cfg.lead2On { leads.append(cfg.lead2Min) }
            }
            if cfg.atIqamah { leads.append(0) }

            for lead in Set(leads) where tgt - lead >= 0 {   // two leads can collide
                out.append(Event(when: dt.day.date(atMinute: tgt - lead), prayer: p,
                                 lead: lead, iqamahMin: tgt, day: dt.day))
            }
        }
    }

    /// Today and tomorrow, so the late-night Fajr reminder is already queued.
    static func build(_ s: Schedule, _ cfg: Config) -> [Event] {
        var out: [Event] = []
        let today = YMD.today
        if let dt = s.day(today) { events(for: dt, cfg, into: &out) }
        if let dt = s.day(today.adding(days: 1)) { events(for: dt, cfg, into: &out) }
        return out.sorted { $0.when < $1.when }
    }

    static func next(_ s: Schedule, _ cfg: Config, after now: Date = Date()) -> Event? {
        build(s, cfg).first { $0.when > now }
    }
}

// ---- formatting --------------------------------------------------------

func fmtTime(_ minutes: Int?, _ cfg: Config) -> String {
    guard let minutes, minutes >= 0 else { return "--:--" }
    let h = (minutes / 60) % 24, m = minutes % 60
    if cfg.use24h { return String(format: "%02d:%02d", h, m) }
    return String(format: "%d:%02d %@", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM")
}

func fmtCountdown(_ secs: TimeInterval) -> String {
    let s = max(0, Int(secs)), m = s / 60, h = m / 60
    if h > 0 { return "\(h)h \(m % 60)m" }
    if m > 0 { return "\(m)m" }
    return "\(s)s"
}

func fmtAgo(_ when: Date?) -> String {
    guard let when else { return "never" }
    let s = Int(Date().timeIntervalSince(when))
    if s < 90 { return "just now" }
    if s < 5400 { return "\(s / 60) min ago" }
    if s < 172_800 { return "\(s / 3600) h ago" }
    return "\(s / 86400) days ago"
}
