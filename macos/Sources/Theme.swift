// Colours and small shared views. Unlike Windows, the Mac version follows
// the system appearance: semantic label colours and native materials, with
// the masjid's accent as the one brand colour.
import SwiftUI

enum T {
    static let text  = Color.primary
    static let dim   = Color.secondary
    static let faint = Color(nsColor: .tertiaryLabelColor)
    static let fill  = Color.primary.opacity(0.055)      // cards, the next-prayer row
    static let line  = Color.primary.opacity(0.1)
    static let warn  = Color.orange
    static let tile  = Color(hex: "191D23")              // app icon background
}

extension NSColor {
    convenience init(hex: String) {
        let v = UInt32(hex.replacingOccurrences(of: "#", with: ""), radix: 16) ?? 0
        self.init(srgbRed: CGFloat((v >> 16) & 0xFF) / 255, green: CGFloat((v >> 8) & 0xFF) / 255,
                  blue: CGFloat(v & 0xFF) / 255, alpha: 1)
    }
}

extension Color {
    init(hex: String) { self.init(nsColor: NSColor(hex: hex)) }
}

extension Config {
    /// The masjid colour as configured; used where it sits on a dark tile.
    var accentRaw: Color { Color(hex: accent) }

    /// The masjid colour adjusted per appearance: as-is in dark mode, deepened
    /// in light mode so text in it stays readable.
    var accentColor: Color {
        let base = NSColor(hex: accent)
        return Color(nsColor: NSColor(name: nil) { ap in
            ap.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
                ? base : (base.blended(withFraction: 0.38, of: .black) ?? base)
        })
    }
}

/// The app mark: accent crescent on a dark rounded tile, as in AppIcon.icns.
struct AppMark: View {
    let accent: Color
    var body: some View {
        GeometryReader { g in
            RoundedRectangle(cornerRadius: g.size.width * 0.23, style: .continuous)
                .fill(T.tile)
                .overlay(Crescent().fill(accent).padding(g.size.width * 0.2))
        }
    }
}

/// The crescent mark, same geometry as gfx_make_icon / make_icon.py: a disc
/// with an offset disc taken out. Traced as two arcs so it needs no path
/// boolean ops (those are macOS 14+).
struct Crescent: Shape {
    func path(in r: CGRect) -> Path {
        let s = min(r.width, r.height)
        let (x1, y1, r1) = (0.50, 0.50, 0.45)        // outer disc
        let (x2, y2, r2) = (0.66, 0.38, 0.38)        // the bite
        let phi = atan2(y2 - y1, x2 - x1), d = hypot(x2 - x1, y2 - y1)
        let alpha = acos((r1 * r1 - r2 * r2 + d * d) / (2 * d * r1))

        func pt(_ cx: Double, _ cy: Double, _ rad: Double, _ a: Double) -> CGPoint {
            CGPoint(x: r.minX + (cx + rad * cos(a)) * s, y: r.minY + (cy + rad * sin(a)) * s)
        }
        func norm(_ a: Double) -> Double { let m = a.truncatingRemainder(dividingBy: 2 * .pi); return m < 0 ? m + 2 * .pi : m }

        var p = Path()
        // Outer rim, the long way round, away from the bite.
        let a0 = phi + alpha, a1 = phi + 2 * .pi - alpha
        for i in 0...48 { let a = a0 + (a1 - a0) * Double(i) / 48; i == 0 ? p.move(to: pt(x1, y1, r1, a)) : p.addLine(to: pt(x1, y1, r1, a)) }

        // Inner rim back to the start, through the side facing the centre.
        let e = pt(x1, y1, r1, a1), st = pt(x1, y1, r1, a0)
        let gA = atan2((e.y - r.minY) / s - y2, (e.x - r.minX) / s - x2)
        let gB = atan2((st.y - r.minY) / s - y2, (st.x - r.minX) / s - x2)
        let b = norm(gB - gA), mid = norm(phi + .pi - gA)
        let sweep = mid < b ? b : b - 2 * .pi
        for i in 1...48 { p.addLine(to: pt(x2, y2, r2, gA + sweep * Double(i) / 48)) }
        p.closeSubpath()
        return p
    }
}
