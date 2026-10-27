// Generate Resources/AppIcon.icns: the crescent on a dark rounded square.
//   swift tools/make_icon.swift            default accent
//   swift tools/make_icon.swift 2E7D32     your own colour
// The menu bar icon is drawn at runtime, so only the Finder/Login Items
// icon depends on this file.
import AppKit

let hex = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "9FB93C"
let v = UInt32(hex.replacingOccurrences(of: "#", with: ""), radix: 16) ?? 0x9FB93C
let accent = NSColor(srgbRed: CGFloat((v >> 16) & 0xFF) / 255, green: CGFloat((v >> 8) & 0xFF) / 255,
                     blue: CGFloat(v & 0xFF) / 255, alpha: 1)
let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let set = FileManager.default.temporaryDirectory.appendingPathComponent("AppIcon.iconset")
try? FileManager.default.removeItem(at: set)
try FileManager.default.createDirectory(at: set, withIntermediateDirectories: true)

func render(_ px: Int) -> Data {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    let s = CGFloat(px)
    // macOS icon grid: the tile sits inside a ~10% margin.
    let tile = NSRect(x: s * 0.1, y: s * 0.1, width: s * 0.8, height: s * 0.8)
    NSColor(srgbRed: 0x19 / 255, green: 0x1D / 255, blue: 0x23 / 255, alpha: 1).setFill()
    NSBezierPath(roundedRect: tile, xRadius: s * 0.18, yRadius: s * 0.18).fill()

    // Same geometry as gfx_make_icon (y measured from the top there).
    let m = tile.insetBy(dx: tile.width * 0.2, dy: tile.height * 0.2)
    let k = m.width
    func circle(_ cx: CGFloat, _ cy: CGFloat, _ r: CGFloat) -> NSBezierPath {
        NSBezierPath(ovalIn: NSRect(x: m.minX + (cx - r) * k, y: m.maxY - (cy + r) * k, width: 2 * r * k, height: 2 * r * k))
    }
    let clip = NSBezierPath(rect: tile)
    clip.append(circle(0.66, 0.38, 0.38))
    clip.windingRule = .evenOdd
    clip.addClip()
    accent.setFill()
    circle(0.50, 0.50, 0.45).fill()

    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

for pt in [16, 32, 128, 256, 512] {
    try render(pt).write(to: set.appendingPathComponent("icon_\(pt)x\(pt).png"))
    try render(pt * 2).write(to: set.appendingPathComponent("icon_\(pt)x\(pt)@2x.png"))
}
let out = root.appendingPathComponent("Resources/AppIcon.icns")
let p = Process()
p.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
p.arguments = ["-c", "icns", set.path, "-o", out.path]
try p.run(); p.waitUntilExit()
print("wrote \(out.path)")
