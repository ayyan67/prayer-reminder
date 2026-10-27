// Palette and small reusable controls. Same dark palette as src/gfx.h.
import SwiftUI

enum T {
    static let bg     = Color(hex: "101317")
    static let card   = Color(hex: "191D23")
    static let cardHi = Color(hex: "21272F")
    static let line   = Color(hex: "282E37")
    static let text   = Color(hex: "E9EDF2")
    static let dim    = Color(hex: "8C97A5")
    static let faint  = Color(hex: "5B6572")
    static let warn   = Color(hex: "E08B3E")
}

extension Color {
    init(hex: String) {
        let v = UInt32(hex.replacingOccurrences(of: "#", with: ""), radix: 16) ?? 0
        self.init(red: Double((v >> 16) & 0xFF) / 255, green: Double((v >> 8) & 0xFF) / 255,
                  blue: Double(v & 0xFF) / 255)
    }
}

extension Config {
    var accentColor: Color { Color(hex: accent) }
    /// Readable text over the accent colour.
    var onAccent: Color {
        let v = UInt32(accent, radix: 16) ?? 0
        let l = 0.299 * Double((v >> 16) & 0xFF) + 0.587 * Double((v >> 8) & 0xFF) + 0.114 * Double(v & 0xFF)
        return l > 140 ? T.bg : .white
    }
}

struct PillButton: ButtonStyle {
    var primary = false
    @EnvironmentObject var m: AppModel

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 12, weight: .semibold))
            .foregroundColor(primary ? m.cfg.onAccent : T.text)
            .lineLimit(1)
            .fixedSize()
            .padding(.horizontal, 12)
            .frame(height: 30)
            .background(RoundedRectangle(cornerRadius: 8)
                .fill(primary ? m.cfg.accentColor : T.card))
            .overlay(RoundedRectangle(cornerRadius: 8)
                .strokeBorder(primary ? Color.clear : T.line, lineWidth: 1))
            .opacity(configuration.isPressed ? 0.75 : 1)
            .contentShape(Rectangle())
    }
}

/// [ −  20 min  + ], as on Windows.
struct StepperBox: View {
    let text: String
    let minus: () -> Void
    let plus: () -> Void

    var body: some View {
        HStack(spacing: 0) {
            Button(action: minus) { Text("\u{2212}").frame(width: 26, height: 28) }
            Text(text)
                .font(.system(size: 12, weight: .semibold))
                .foregroundColor(T.text)
                .frame(width: 62)
            Button(action: plus) { Text("+").frame(width: 26, height: 28) }
        }
        .buttonStyle(.plain)
        .font(.system(size: 13, weight: .medium))
        .foregroundColor(T.dim)
        .background(RoundedRectangle(cornerRadius: 7).fill(T.card))
        .overlay(RoundedRectangle(cornerRadius: 7).strokeBorder(T.line, lineWidth: 1))
    }
}

/// Pill segmented control used for the tabs and the Friday choice.
struct Segments: View {
    let labels: [String]
    @Binding var selection: Int
    var height: CGFloat = 32

    var body: some View {
        HStack(spacing: 4) {
            ForEach(labels.indices, id: \.self) { i in
                Button { selection = i } label: {
                    Text(labels[i])
                        .font(.system(size: 12, weight: selection == i ? .semibold : .regular))
                        .foregroundColor(selection == i ? T.text : T.dim)
                        .frame(maxWidth: .infinity, minHeight: height)
                        .background(RoundedRectangle(cornerRadius: 8)
                            .fill(selection == i ? T.cardHi : Color.clear))
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
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
