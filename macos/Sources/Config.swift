// config.ini load/save, reload-on-edit, login item. Same keys as Windows,
// so a config.ini can be copied between the two.
import AppKit
import ServiceManagement

struct Config: Equatable {
    var lead1On = true,  lead1Min = 20      // first reminder, minutes before iqamah
    var lead2On = true,  lead2Min = 5       // second reminder
    var atIqamah = true                     // also alert exactly at iqamah time
    var sound = true
    var toastSeconds = 45                   // auto-dismiss; 0 = stay until dismissed
    var snoozeMinutes = 5
    var prayerOn   = [Bool](repeating: true, count: Prayer.allCases.count)
    var prayerLead = [Int](repeating: -1, count: Prayer.allCases.count)  // -1 = use global leads
    var fridayMode = FridayMode.jumuah1
    var autoStart = true
    var use24h = false
    var refreshHour = 3                     // daily background refresh, 0-23
    var timeShift = 0                       // minutes added to every time
    var menuBarText = true                  // show the next prayer beside the icon

    // [masjid] - which masjid this build follows. See README.
    var masjidName = "Muslim Center of New Jersey"
    var website    = "https://mcnjonline.com/"
    var masjidalId = "lJAmBPdR"
    var accent     = "9FB93C"

    init() {
        prayerOn[Prayer.sunrise.rawValue] = false   // informational, not a jama'ah
        prayerOn[Prayer.jumuah2.rawValue] = false   // most people attend the first
    }
}

enum ConfigStore {
    static let dir: URL = {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let d = base.appendingPathComponent("PrayerReminder", isDirectory: true)
        try? FileManager.default.createDirectory(at: d, withIntermediateDirectories: true)
        return d
    }()
    static var iniURL: URL { dir.appendingPathComponent("config.ini") }

    /// Modification date as we last read or wrote it, so a hand edit in a
    /// text editor is noticed and reloaded without a restart.
    private static var stamp: Date?

    private static func diskStamp() -> Date? {
        (try? FileManager.default.attributesOfItem(atPath: iniURL.path))?[.modificationDate] as? Date
    }

    static func changedOnDisk() -> Bool {
        guard let now = diskStamp() else { return false }
        return now != stamp
    }

    // ---- ini ---------------------------------------------------------------

    private static func parse(_ text: String) -> [String: [String: String]] {
        var out: [String: [String: String]] = [:]
        var sec = ""
        for raw in text.components(separatedBy: .newlines) {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.isEmpty || line.hasPrefix(";") || line.hasPrefix("#") { continue }
            if line.hasPrefix("["), line.hasSuffix("]") {
                sec = String(line.dropFirst().dropLast()).lowercased()
                continue
            }
            guard let eq = line.firstIndex(of: "=") else { continue }
            let k = line[..<eq].trimmingCharacters(in: .whitespaces).lowercased()
            let v = line[line.index(after: eq)...].trimmingCharacters(in: .whitespaces)
            out[sec, default: [:]][k] = v
        }
        return out
    }

    static func load() -> Config {
        var c = Config()
        guard let text = try? String(contentsOf: iniURL, encoding: .utf8) else {
            save(c)
            return c
        }
        let ini = parse(text)

        func int(_ s: String, _ k: String, _ def: Int, _ lo: Int, _ hi: Int) -> Int {
            guard let v = ini[s]?[k.lowercased()].flatMap({ Int($0) }) else { return def }
            return min(max(v, lo), hi)
        }
        func bool(_ s: String, _ k: String, _ def: Bool) -> Bool {
            guard let v = ini[s]?[k.lowercased()].flatMap({ Int($0) }) else { return def }
            return v != 0
        }
        func str(_ s: String, _ k: String, _ def: String) -> String {
            guard let v = ini[s]?[k.lowercased()], !v.isEmpty else { return def }
            return v
        }

        c.lead1On       = bool("reminders", "lead1On", c.lead1On)
        c.lead1Min      = int("reminders", "lead1Min", c.lead1Min, 1, 180)
        c.lead2On       = bool("reminders", "lead2On", c.lead2On)
        c.lead2Min      = int("reminders", "lead2Min", c.lead2Min, 1, 180)
        c.atIqamah      = bool("reminders", "atIqamah", c.atIqamah)
        c.sound         = bool("reminders", "sound", c.sound)
        c.toastSeconds  = int("reminders", "toastSeconds", c.toastSeconds, 0, 600)
        c.snoozeMinutes = int("reminders", "snoozeMinutes", c.snoozeMinutes, 1, 60)
        c.fridayMode    = FridayMode(rawValue: int("reminders", "fridayMode", c.fridayMode.rawValue, 0, 3))!

        c.autoStart   = bool("general", "autoStart", c.autoStart)
        c.use24h      = bool("general", "use24h", c.use24h)
        c.refreshHour = int("general", "refreshHour", c.refreshHour, 0, 23)
        c.timeShift   = int("general", "timeShift", c.timeShift, -720, 720)
        c.menuBarText = bool("general", "menuBarText", c.menuBarText)

        for p in Prayer.allCases {
            let s = "prayer.\(p.key)"
            c.prayerOn[p.rawValue]   = bool(s, "enabled", c.prayerOn[p.rawValue])
            c.prayerLead[p.rawValue] = int(s, "lead", c.prayerLead[p.rawValue], -1, 180)
        }

        c.masjidName = str("masjid", "name", c.masjidName)
        c.website    = str("masjid", "website", c.website)
        c.masjidalId = str("masjid", "masjidalId", c.masjidalId)
        let hex = str("masjid", "accent", c.accent).replacingOccurrences(of: "#", with: "")
        if hex.count == 6, UInt32(hex, radix: 16) != nil { c.accent = hex.uppercased() }

        stamp = diskStamp()
        return c
    }

    static func save(_ c: Config) {
        func b(_ v: Bool) -> Int { v ? 1 : 0 }
        var s = """
        ; Prayer Reminder settings. Edit and save; the app reloads it.

        [masjid]
        name=\(c.masjidName)
        website=\(c.website)
        masjidalId=\(c.masjidalId)
        accent=\(c.accent)

        [reminders]
        lead1On=\(b(c.lead1On))
        lead1Min=\(c.lead1Min)
        lead2On=\(b(c.lead2On))
        lead2Min=\(c.lead2Min)
        atIqamah=\(b(c.atIqamah))
        sound=\(b(c.sound))
        toastSeconds=\(c.toastSeconds)
        snoozeMinutes=\(c.snoozeMinutes)
        fridayMode=\(c.fridayMode.rawValue)

        [general]
        autoStart=\(b(c.autoStart))
        use24h=\(b(c.use24h))
        refreshHour=\(c.refreshHour)
        timeShift=\(c.timeShift)
        menuBarText=\(b(c.menuBarText))

        """
        for p in Prayer.allCases {
            s += "\n[prayer.\(p.key)]\nenabled=\(b(c.prayerOn[p.rawValue]))\nlead=\(c.prayerLead[p.rawValue])\n"
        }
        try? s.write(to: iniURL, atomically: true, encoding: .utf8)
        stamp = diskStamp()
    }

    static func openInEditor(_ c: Config) {
        save(c)        // make sure every key is written out first
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/open")
        p.arguments = ["-t", iniURL.path]      // default text editor
        try? p.run()
    }

    static func applyAutoStart(_ c: Config) {
        let svc = SMAppService.mainApp
        if c.autoStart, svc.status != .enabled { try? svc.register() }
        if !c.autoStart, svc.status == .enabled { try? svc.unregister() }
    }
}
