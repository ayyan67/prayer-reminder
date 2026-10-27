// App state and the one-second scheduler tick. Mirrors src/main.c.
import AppKit

@MainActor
final class AppModel: ObservableObject {
    enum FetchState { case idle, fetching, failed }

    @Published var cfg: Config { didSet { configChanged(from: oldValue) } }
    @Published private(set) var sched = Schedule()
    @Published private(set) var fetchState = FetchState.idle
    @Published private(set) var pauseUntil: Date?
    @Published private(set) var now = Date()
    @Published var tab = 0                       // 0 = Today, 1 = Settings

    /// True while the popover is open; the countdowns only tick then.
    var live = false
    var onReminder: (Event) -> Void = { _ in }
    var onChange: () -> Void = {}

    private var lastCheck = Date()
    private var lastRefreshDay: YMD?
    private var fetchGen = 0
    private var reloading = false

    init() {
        cfg = ConfigStore.load()
        sched = Times.loadCache(cfg.masjidalId) ?? Schedule()   // show something immediately
    }

    var paused: Bool { (pauseUntil ?? .distantPast) > Date() }
    var next: Event? { Sched.next(sched, cfg) }

    // ---- fetching ----------------------------------------------------------

    func refresh() {
        guard fetchState != .fetching else { return }     // one in flight is enough
        fetchState = .fetching
        onChange()
        let cfg = self.cfg, gen = fetchGen
        Task {
            let s = await Times.fetch(cfg)
            guard gen == fetchGen else { return }          // masjid changed meanwhile
            if let s { sched = s; fetchState = .idle } else { fetchState = .failed }
            onChange()
        }
    }

    /// Fetch again if the cache does not cover today or is getting stale.
    func ensureFresh() {
        guard fetchState != .fetching else { return }
        if sched.today == nil || Date().timeIntervalSince(sched.fetchedAt ?? .distantPast) > 24 * 3600 {
            refresh()
        }
    }

    // ---- config ------------------------------------------------------------

    private func configChanged(from old: Config) {
        guard cfg != old else { return }
        if !reloading { ConfigStore.save(cfg) }
        if cfg.masjidalId != old.masjidalId {
            // Never show the previous masjid's times.
            fetchGen += 1
            fetchState = .idle
            sched = Times.loadCache(cfg.masjidalId) ?? Schedule()
            refresh()
        }
        if cfg.autoStart != old.autoStart { ConfigStore.applyAutoStart(cfg) }
        onChange()
    }

    /// Pick up a hand edit of config.ini.
    func reloadIfEdited() {
        guard ConfigStore.changedOnDisk() else { return }
        reloading = true
        cfg = ConfigStore.load()
        reloading = false
    }

    // ---- pause -------------------------------------------------------------

    func pause(hours: Int) { pauseUntil = Date().addingTimeInterval(TimeInterval(hours * 3600)); onChange() }
    func pauseUntilTomorrow() { pauseUntil = YMD.today.adding(days: 1).date(atMinute: 0); onChange() }
    func resume() { pauseUntil = nil; onChange() }

    // ---- tick --------------------------------------------------------------

    func tick() {
        let t = Date()
        if live { now = t }
        checkEvents(t)

        let c = Calendar.current.dateComponents([.hour, .minute, .second], from: t)
        if c.hour == cfg.refreshHour && lastRefreshDay != .today {
            lastRefreshDay = .today
            refresh()
        }
        // Self-heal hourly: the daily refresh may have been missed while the
        // Mac was asleep, or the cache may no longer cover today.
        if c.minute == 0 && c.second! < 2 { ensureFresh() }
        if c.second! % 3 == 0 { reloadIfEdited() }
        onChange()
    }

    private func checkEvents(_ now: Date) {
        if now < lastCheck { lastCheck = now }           // clock moved backwards
        if paused { lastCheck = now; return }

        for ev in Sched.build(sched, cfg) where ev.when > lastCheck && ev.when <= now {
            // After a long sleep, skip anything well past due instead of
            // firing a burst of stale reminders.
            if now.timeIntervalSince(ev.when) > 300 { continue }
            // Show the earliest one now and leave the rest pending, so a
            // second event in the same second is delayed by a tick, not lost.
            onReminder(ev)
            lastCheck = ev.when
            return
        }
        lastCheck = now
    }

    func didWake() {
        lastCheck = Date()           // do not replay missed alerts
        ensureFresh()
    }

    /// A sample popup using the current settings.
    func preview() {
        var ev = next ?? Event(when: Date(), prayer: .asr, lead: 0, iqamahMin: 17 * 60 + 45, day: .today)
        if ev.lead <= 0 { ev.lead = cfg.lead1On ? cfg.lead1Min : 10 }
        onReminder(ev)
    }
}
