// Masjidal feed + HTML fallback, caching, background fetch. Mirrors src/times.c.
import Foundation

enum Times {

    // ---- value parsing -----------------------------------------------------

    /// "5:24AM" / "1:15 PM" / "12:52PM" / "1:35" -> minutes from midnight.
    static func parseClock<S: Sequence>(_ s: S, assumePM: Bool) -> Int? where S.Element == UInt8 {
        var it = Array(s.prefix(16))[...]
        while it.first == 32 { it.removeFirst() }
        func digits() -> Int? {
            var v = 0, n = 0
            while let c = it.first, c >= 48, c <= 57 { v = v * 10 + Int(c - 48); n += 1; it.removeFirst() }
            return n > 0 ? v : nil
        }
        guard var h = digits(), it.first == 58 else { return nil }   // ':'
        it.removeFirst()
        guard let m = digits(), h <= 23, m <= 59 else { return nil }
        while it.first == 32 || it.first == 46 { it.removeFirst() }   // ' ' '.'

        var pm = assumePM                       // the feed omits it on some rows
        if let c = it.first {
            if c == 97 || c == 65 { pm = false }       // a A
            else if c == 112 || c == 80 { pm = true }  // p P
        }
        if pm && h < 12 { h += 12 }
        if !pm && h == 12 { h = 0 }
        return h * 60 + m
    }

    static func parseClock(_ s: String?, assumePM: Bool) -> Int? {
        guard let s else { return nil }
        return parseClock(Array(s.utf8), assumePM: assumePM)
    }

    /// "Thursday, Sep 17, 2026" -> YMD.
    static func parseDate(_ s: String) -> YMD? {
        let parts = s.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }
        guard parts.count >= 3 else { return nil }
        let md = parts[1].split(separator: " ")
        let months = ["jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"]
        guard md.count == 2,
              let mi = months.firstIndex(of: String(md[0].prefix(3)).lowercased()),
              let d = Int(md[1]), let y = Int(parts[2].prefix(4)),
              (1...31).contains(d), (2000...2100).contains(y) else { return nil }
        return YMD(y: y, m: mi + 1, d: d)
    }

    // ---- JSON --------------------------------------------------------------

    static func parseJSON(_ data: Data) -> Schedule? {
        guard let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let body = root["data"] as? [String: Any] else { return nil }

        var days: [YMD: DayTimes] = [:]
        func day(_ row: [String: Any]) -> YMD? {
            guard let s = row["date"] as? String, let d = parseDate(s) else { return nil }
            if days[d] == nil { days[d] = DayTimes(day: d) }
            return d
        }

        let adhanKeys: [(String, Prayer, Bool)] = [
            ("fajr", .fajr, false), ("sunrise", .sunrise, false), ("zuhr", .dhuhr, true),
            ("asr", .asr, true), ("maghrib", .maghrib, true), ("isha", .isha, true),
        ]
        for row in body["salah"] as? [[String: Any]] ?? [] {
            guard let d = day(row) else { continue }
            for (k, p, pm) in adhanKeys {
                days[d]!.adhan[p.rawValue] = parseClock(row[k] as? String, assumePM: pm)
            }
            // "6, 1448" + "Rabi Al-Thani" -> "6 Rabi Al-Thani 1448"
            if let hd = row["hijri_date"] as? String, let hm = row["hijri_month"] as? String {
                let bits = hd.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }
                if bits.count == 2 { days[d]!.hijri = "\(bits[0]) \(hm) \(bits[1])" }
            }
        }

        let iqamahKeys: [(String, Prayer, Bool)] = [
            ("fajr", .fajr, false), ("zuhr", .dhuhr, true), ("asr", .asr, true),
            ("maghrib", .maghrib, true), ("isha", .isha, true),
            // jummah1/2 carry the khutbah start time
            ("jummah1", .jumuah1, true), ("jummah2", .jumuah2, true),
        ]
        for row in body["iqamah"] as? [[String: Any]] ?? [] {
            guard let d = day(row) else { continue }
            for (k, p, pm) in iqamahKeys {
                days[d]!.iqamah[p.rawValue] = parseClock(row[k] as? String, assumePM: pm)
            }
        }

        guard !days.isEmpty else { return nil }
        let sorted = days.values.sorted { ($0.day.y, $0.day.m, $0.day.d) < ($1.day.y, $1.day.m, $1.day.d) }
        return Schedule(days: sorted, fetchedAt: Date())
    }

    // ---- HTML fallback -----------------------------------------------------
    // If the JSON feed ever goes away, read today's numbers off the homepage.

    private static func find(_ hay: [UInt8], _ needle: String, from: Int, to: Int) -> Int? {
        let n = Array(needle.lowercased().utf8)
        guard to - n.count >= from else { return nil }
        var i = from
        while i + n.count <= to {
            if hay[i] == n[0] && Array(hay[i..<i + n.count]) == n { return i }
            i += 1
        }
        return nil
    }

    private static func timeAfter(_ h: [UInt8], at: Int, label: String, pm: Bool) -> Int? {
        let lim = min(at + 1200, h.count)
        guard let l = find(h, label, from: at, to: lim) else { return nil }
        // Skip the separator junk between the label and the digits,
        // including any markup the page injects mid-line.
        var q = l + label.utf8.count
        while q < lim && !(h[q] >= 48 && h[q] <= 57) {
            if h[q] == 60 { while q < lim && h[q] != 62 { q += 1 } }   // < ... >
            q += 1
        }
        guard q < lim else { return nil }
        return parseClock(h[q..<lim], assumePM: pm)
    }

    static func parseHTML(_ html: String) -> Schedule? {
        let h = Array(html.lowercased().utf8)
        var dt = DayTimes(day: .today)
        var found = 0
        let rows: [(String, Prayer, Bool)] = [
            ("Fajr", .fajr, false), ("Dhuhr", .dhuhr, true), ("Asr", .asr, true),
            ("Maghrib", .maghrib, true), ("Isha", .isha, true),
        ]
        for (label, p, pm) in rows {
            guard let at = find(h, label, from: 0, to: h.count) else { continue }
            dt.adhan[p.rawValue]  = timeAfter(h, at: at, label: "Adhan", pm: pm)
            dt.iqamah[p.rawValue] = timeAfter(h, at: at, label: "Iqamah", pm: pm)
            if dt.iqamah[p.rawValue] != nil { found += 1 }
        }
        if let sr = find(h, "Sunrise", from: 0, to: h.count) {
            dt.adhan[Prayer.sunrise.rawValue] = timeAfter(h, at: sr, label: "Sunrise", pm: false)
        }
        if let j = find(h, "Jumah 1st", from: 0, to: h.count) {
            dt.iqamah[Prayer.jumuah1.rawValue] = timeAfter(h, at: j, label: "Khutbah", pm: true)
        }
        if let j = find(h, "Jumah 2nd", from: 0, to: h.count) {
            dt.iqamah[Prayer.jumuah2.rawValue] = timeAfter(h, at: j, label: "Khutbah", pm: true)
        }
        guard found >= 3 else { return nil }      // partial scrape is worse than none
        return Schedule(days: [dt], fetchedAt: Date())
    }

    // ---- cache -------------------------------------------------------------

    /// One cache per masjid id, so switching masjids can never show the
    /// previous masjid's times.
    static func cacheURL(_ id: String) -> URL {
        let safe = String(id.unicodeScalars.filter { CharacterSet.alphanumerics.contains($0) }.prefix(42))
        return ConfigStore.dir.appendingPathComponent("cache-\(safe).json")
    }

    static func loadCache(_ id: String) -> Schedule? {
        let url = cacheURL(id)
        guard let data = try? Data(contentsOf: url), var s = parseJSON(data) else { return nil }
        // Report when the data was really fetched, not when we reread it.
        s.fetchedAt = (try? FileManager.default.attributesOfItem(atPath: url.path))?[.modificationDate] as? Date
        return s
    }

    // ---- fetch -------------------------------------------------------------

    private static let session: URLSession = {
        let c = URLSessionConfiguration.ephemeral
        c.timeoutIntervalForRequest = 15
        c.httpAdditionalHeaders = ["User-Agent": "PrayerReminder/1.0 (macOS)"]
        return URLSession(configuration: c)
    }()

    private static func get(_ url: URL) async -> Data? {
        guard let (data, resp) = try? await session.data(from: url),
              (resp as? HTTPURLResponse)?.statusCode == 200 else { return nil }
        return data
    }

    static func fetch(_ cfg: Config) async -> Schedule? {
        // Two weeks ahead: enough to ride out an outage or a spell offline.
        let from = YMD.today, to = from.adding(days: 13)
        func iso(_ d: YMD) -> String { String(format: "%04d-%02d-%02d", d.y, d.m, d.d) }

        var q = URLComponents(string: "https://masjidal.com/api/v1/time/range")!
        q.queryItems = [.init(name: "masjid_id", value: cfg.masjidalId),
                        .init(name: "from_date", value: iso(from)),
                        .init(name: "to_date", value: iso(to))]

        if let url = q.url, let data = await get(url), let s = parseJSON(data) {
            try? data.write(to: cacheURL(cfg.masjidalId), options: .atomic)
            return s
        }
        // fall back to the masjid homepage
        if let site = URL(string: cfg.website), let host = site.host,
           let home = URL(string: "https://\(host)/"),
           let data = await get(home), let html = String(data: data, encoding: .utf8) {
            return parseHTML(html)
        }
        return nil
    }
}
