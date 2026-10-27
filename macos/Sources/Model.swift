// Shared types. Mirrors src/app.h on the Windows side.
import Foundation

enum Prayer: Int, CaseIterable {
    case fajr, sunrise, dhuhr, asr, maghrib, isha, jumuah1, jumuah2

    var name: String {
        ["Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha", "Jumu'ah 1", "Jumu'ah 2"][rawValue]
    }
    /* ini section suffix: [prayer.fajr] ... */
    var key: String {
        ["fajr", "sunrise", "dhuhr", "asr", "maghrib", "isha", "jumuah1", "jumuah2"][rawValue]
    }
}

/// A calendar day in the user's local timezone.
struct YMD: Hashable {
    var y: Int, m: Int, d: Int

    static var today: YMD { YMD(Date()) }

    init(y: Int, m: Int, d: Int) { self.y = y; self.m = m; self.d = d }
    init(_ date: Date) {
        let c = Calendar.current.dateComponents([.year, .month, .day], from: date)
        y = c.year!; m = c.month!; d = c.day!
    }

    /// Noon on this day; a safe anchor for day arithmetic across DST.
    var noon: Date { date(atMinute: 12 * 60) }

    /// Minutes from local midnight -> absolute time. Calendar resolves DST.
    func date(atMinute min: Int) -> Date {
        var c = DateComponents()
        c.year = y; c.month = m; c.day = d; c.hour = min / 60; c.minute = min % 60
        return Calendar.current.date(from: c)!
    }

    func adding(days: Int) -> YMD {
        YMD(Calendar.current.date(byAdding: .day, value: days, to: noon)!)
    }

    /// 0 = Sunday ... 5 = Friday.
    var dow: Int { Calendar.current.component(.weekday, from: noon) - 1 }
}

/// Times are minutes from local midnight, nil when the feed has none.
struct DayTimes {
    var day: YMD
    var adhan  = [Int?](repeating: nil, count: Prayer.allCases.count)
    var iqamah = [Int?](repeating: nil, count: Prayer.allCases.count)
    var hijri  = ""                         // e.g. "6 Rabi Al-Thani 1448"
}

struct Schedule {
    var days: [DayTimes] = []
    var fetchedAt: Date?

    func day(_ d: YMD) -> DayTimes? { days.first { $0.day == d } }
    var today: DayTimes? { day(.today) }
}

struct Event: Equatable {
    var when: Date
    var prayer: Prayer
    var lead: Int          // minutes before iqamah; 0 = at iqamah time
    var iqamahMin: Int
    var day: YMD

    var target: Date { day.date(atMinute: iqamahMin) }
}

enum FridayMode: Int, CaseIterable {
    case jumuah1, jumuah2, both, dhuhr
    var label: String { ["Jumu'ah 1", "Jumu'ah 2", "Both", "Dhuhr"][rawValue] }
}
