// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Renders the Tablarion application/plug-in icon.
// Usage: swift DrawIcon.swift <output.png> [size]
//
// Concept: a generic half-rack sound module with a guitar-pick volume knob and an amber dot-matrix
// LCD showing a part display and level bars, plugged in with a 1/4" jack into a glowing "PLUG-IN"
// socket - the plug-in metaphor. Original artwork; no third-party logos or trademarks.
import AppKit
import CoreGraphics

let args = CommandLine.arguments
let outPath = args.count > 1 ? args[1] : "icon_1024.png"
let size = args.count > 2 ? Int(args[2]) ?? 1024 : 1024

func rgb(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
    CGColor(red: CGFloat((hex >> 16) & 0xff) / 255, green: CGFloat((hex >> 8) & 0xff) / 255, blue: CGFloat(hex & 0xff) / 255, alpha: a)
}

let cs = CGColorSpaceCreateDeviceRGB()
guard let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0, space: cs,
                          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { fatalError("no context") }
let scale = CGFloat(size) / 1024
ctx.scaleBy(x: scale, y: scale)
// flip to top-left origin
ctx.translateBy(x: 0, y: 1024)
ctx.scaleBy(x: 1, y: -1)
ctx.setShouldAntialias(true)
ctx.interpolationQuality = .high

let nsctx = NSGraphicsContext(cgContext: ctx, flipped: true)
NSGraphicsContext.current = nsctx

func linear(_ path: CGPath, _ c0: CGColor, _ c1: CGColor, from p0: CGPoint, to p1: CGPoint) {
    ctx.saveGState()
    ctx.addPath(path); ctx.clip()
    let g = CGGradient(colorsSpace: cs, colors: [c0, c1] as CFArray, locations: [0, 1])!
    ctx.drawLinearGradient(g, start: p0, end: p1, options: [.drawsBeforeStartLocation, .drawsAfterEndLocation])
    ctx.restoreGState()
}

func radial(center: CGPoint, radius: CGFloat, _ c0: CGColor, _ c1: CGColor) {
    let g = CGGradient(colorsSpace: cs, colors: [c0, c1] as CFArray, locations: [0, 1])!
    ctx.drawRadialGradient(g, startCenter: center, startRadius: 0, endCenter: center, endRadius: radius, options: [])
}

func rrect(_ r: CGRect, _ rad: CGFloat) -> CGPath { CGPath(roundedRect: r, cornerWidth: rad, cornerHeight: rad, transform: nil) }

func fill(_ p: CGPath, _ c: CGColor) { ctx.addPath(p); ctx.setFillColor(c); ctx.fillPath() }
func stroke(_ p: CGPath, _ c: CGColor, _ w: CGFloat) { ctx.addPath(p); ctx.setStrokeColor(c); ctx.setLineWidth(w); ctx.strokePath() }

func text(_ s: String, at p: CGPoint, size: CGFloat, weight: NSFont.Weight, color: CGColor, mono: Bool = false, kern: CGFloat = 0) {
    let font = mono ? NSFont.monospacedSystemFont(ofSize: size, weight: weight) : NSFont.systemFont(ofSize: size, weight: weight)
    let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: NSColor(cgColor: color)!, .kern: kern]
    NSAttributedString(string: s, attributes: attrs).draw(at: p)
}

func shadow(_ blur: CGFloat, _ dy: CGFloat, _ c: CGColor) { ctx.setShadow(offset: CGSize(width: 0, height: dy), blur: blur, color: c) }
func noShadow() { ctx.setShadow(offset: .zero, blur: 0, color: nil) }

// ---------------------------------------------------------------------------------------------
// 1. Background squircle
let bg = rrect(CGRect(x: 100, y: 100, width: 824, height: 824), 186)
ctx.saveGState()
shadow(30, 18, rgb(0x000000, 0.45))
fill(bg, rgb(0x111318))
ctx.restoreGState()
linear(bg, rgb(0x2a3142), rgb(0x0a0c11), from: CGPoint(x: 512, y: 100), to: CGPoint(x: 512, y: 924))

ctx.saveGState()
ctx.addPath(bg); ctx.clip()
radial(center: CGPoint(x: 512, y: 430), radius: 430, rgb(0x3fd0c9, 0.22), rgb(0x3fd0c9, 0.0))
radial(center: CGPoint(x: 300, y: 250), radius: 320, rgb(0xffa53a, 0.10), rgb(0xffa53a, 0.0))
// subtle grid
ctx.setStrokeColor(rgb(0xffffff, 0.025)); ctx.setLineWidth(1)
for i in stride(from: 100, through: 924, by: 32) {
    ctx.move(to: CGPoint(x: CGFloat(i), y: 100)); ctx.addLine(to: CGPoint(x: CGFloat(i), y: 924))
    ctx.move(to: CGPoint(x: 100, y: CGFloat(i))); ctx.addLine(to: CGPoint(x: 924, y: CGFloat(i)))
}
ctx.strokePath()
ctx.restoreGState()
stroke(rrect(CGRect(x: 101.5, y: 101.5, width: 821, height: 821), 185), rgb(0xffffff, 0.09), 3)

ctx.translateBy(x: 0, y: -48) // centre the composition

// ---------------------------------------------------------------------------------------------
// 2. Sound module (3D half-rack box)
let front = CGRect(x: 150, y: 392, width: 724, height: 268)

// floor shadow
ctx.saveGState()
ctx.addEllipse(in: CGRect(x: 170, y: 640, width: 690, height: 60))
ctx.setFillColor(rgb(0x000000, 0.55))
shadow(40, 0, rgb(0x000000, 0.8))
ctx.fillPath()
ctx.restoreGState()

// top face
let top = CGMutablePath()
top.move(to: CGPoint(x: 160, y: 398)); top.addLine(to: CGPoint(x: 864, y: 398))
top.addLine(to: CGPoint(x: 826, y: 338)); top.addLine(to: CGPoint(x: 198, y: 338)); top.closeSubpath()
linear(top, rgb(0x575b66), rgb(0x30333a), from: CGPoint(x: 512, y: 338), to: CGPoint(x: 512, y: 398))
// vent slots on the top
ctx.setStrokeColor(rgb(0x1a1b1f, 0.8)); ctx.setLineWidth(4); ctx.setLineCap(.round)
for i in 0..<9 {
    let x0 = 560 + CGFloat(i) * 26
    ctx.move(to: CGPoint(x: x0, y: 352)); ctx.addLine(to: CGPoint(x: x0 + 6, y: 384))
}
ctx.strokePath()
stroke(top, rgb(0xffffff, 0.08), 2)

// front face
let frontPath = rrect(front, 22)
ctx.saveGState(); shadow(18, 10, rgb(0x000000, 0.6)); fill(frontPath, rgb(0x202227)); ctx.restoreGState()
linear(frontPath, rgb(0x3a3d45), rgb(0x16171b), from: CGPoint(x: 512, y: front.minY), to: CGPoint(x: 512, y: front.maxY))
// brushed lines
ctx.saveGState(); ctx.addPath(frontPath); ctx.clip()
for y in stride(from: Int(front.minY) + 2, to: Int(front.maxY), by: 3) {
    ctx.setStrokeColor(rgb(0xffffff, (y % 7 == 0) ? 0.035 : 0.015)); ctx.setLineWidth(1)
    ctx.move(to: CGPoint(x: front.minX, y: CGFloat(y))); ctx.addLine(to: CGPoint(x: front.maxX, y: CGFloat(y))); ctx.strokePath()
}
ctx.restoreGState()
// top edge highlight
ctx.setStrokeColor(rgb(0xffffff, 0.18)); ctx.setLineWidth(2)
ctx.move(to: CGPoint(x: front.minX + 22, y: front.minY + 1.5)); ctx.addLine(to: CGPoint(x: front.maxX - 22, y: front.minY + 1.5)); ctx.strokePath()
stroke(frontPath, rgb(0x000000, 0.75), 2.5)

// rack screws
for p in [CGPoint(x: 172, y: 414), CGPoint(x: 852, y: 414), CGPoint(x: 172, y: 638), CGPoint(x: 852, y: 638)] {
    let r = CGRect(x: p.x - 8, y: p.y - 8, width: 16, height: 16)
    let path = CGPath(ellipseIn: r, transform: nil)
    linear(path, rgb(0x9a9ea7), rgb(0x2a2c31), from: CGPoint(x: r.minX, y: r.minY), to: CGPoint(x: r.maxX, y: r.maxY))
    ctx.setStrokeColor(rgb(0x000000, 0.7)); ctx.setLineWidth(2.2)
    ctx.move(to: CGPoint(x: p.x - 5, y: p.y + 2)); ctx.addLine(to: CGPoint(x: p.x + 5, y: p.y - 2)); ctx.strokePath()
}

// ---------------------------------------------------------------------------------------------
// 3. Guitar-pick volume knob
let kc = CGPoint(x: 270, y: 522)
ctx.saveGState()
let recess = CGPath(ellipseIn: CGRect(x: kc.x - 84, y: kc.y - 84, width: 168, height: 168), transform: nil)
linear(recess, rgb(0x0d0e10), rgb(0x2c2e34), from: CGPoint(x: kc.x, y: kc.y - 84), to: CGPoint(x: kc.x, y: kc.y + 84))
ctx.restoreGState()
// scale ticks around the knob
for i in 0..<11 {
    let a = (-225.0 + Double(i) * 27.0) * Double.pi / 180
    let p = CGPoint(x: kc.x + CGFloat(cos(a)) * 98, y: kc.y + CGFloat(sin(a)) * 98)
    ctx.setFillColor(rgb(0xffa53a, i == 5 ? 1.0 : 0.75))
    ctx.fillEllipse(in: CGRect(x: p.x - 3.5, y: p.y - 3.5, width: 7, height: 7))
}
// pick shape (rotated slightly, tip = pointer)
let pick = CGMutablePath()
pick.move(to: CGPoint(x: 0, y: -62))
pick.addCurve(to: CGPoint(x: 66, y: -30), control1: CGPoint(x: 40, y: -66), control2: CGPoint(x: 70, y: -58))
pick.addCurve(to: CGPoint(x: 0, y: 74), control1: CGPoint(x: 60, y: 6), control2: CGPoint(x: 22, y: 56))
pick.addCurve(to: CGPoint(x: -66, y: -30), control1: CGPoint(x: -22, y: 56), control2: CGPoint(x: -60, y: 6))
pick.addCurve(to: CGPoint(x: 0, y: -62), control1: CGPoint(x: -70, y: -58), control2: CGPoint(x: -40, y: -66))
pick.closeSubpath()
var tf = CGAffineTransform(translationX: kc.x, y: kc.y).rotated(by: -0.32)
let pickPath = pick.copy(using: &tf)!
ctx.saveGState(); shadow(14, 8, rgb(0x000000, 0.7)); fill(pickPath, rgb(0xe08a1e)); ctx.restoreGState()
linear(pickPath, rgb(0xffcf7a), rgb(0xd7700c), from: CGPoint(x: kc.x - 60, y: kc.y - 60), to: CGPoint(x: kc.x + 60, y: kc.y + 60))
// celluloid sheen
ctx.saveGState(); ctx.addPath(pickPath); ctx.clip()
radial(center: CGPoint(x: kc.x - 30, y: kc.y - 30), radius: 70, rgb(0xffffff, 0.35), rgb(0xffffff, 0))
ctx.restoreGState()
stroke(pickPath, rgb(0x5a2c00, 0.8), 2.5)
// centre cap
let cap = CGPath(ellipseIn: CGRect(x: kc.x - 20, y: kc.y - 20, width: 40, height: 40), transform: nil)
linear(cap, rgb(0x4a4d55), rgb(0x1a1b1f), from: CGPoint(x: kc.x, y: kc.y - 20), to: CGPoint(x: kc.x, y: kc.y + 20))
stroke(cap, rgb(0x000000, 0.6), 2)

// ---------------------------------------------------------------------------------------------
// 4. Amber dot-matrix LCD
let lcd = CGRect(x: 380, y: 428, width: 318, height: 168)
fill(rrect(lcd.insetBy(dx: -12, dy: -12), 18), rgb(0x07080a))
let glass = rrect(lcd, 10)
linear(glass, rgb(0x241406), rgb(0x0e0703), from: CGPoint(x: lcd.midX, y: lcd.minY), to: CGPoint(x: lcd.midX, y: lcd.maxY))
ctx.saveGState(); ctx.addPath(glass); ctx.clip()
radial(center: CGPoint(x: lcd.midX, y: lcd.midY), radius: 200, rgb(0xffa53a, 0.12), rgb(0xffa53a, 0))
// ghost dots
ctx.setFillColor(rgb(0xffa53a, 0.07))
for y in stride(from: lcd.minY + 6, to: lcd.maxY - 4, by: 6) {
    for x in stride(from: lcd.minX + 6, to: lcd.maxX - 4, by: 6) { ctx.fill(CGRect(x: x, y: y, width: 4, height: 4)) }
}
// part display text
shadow(10, 0, rgb(0xffa53a, 0.9))
text("PART 01", at: CGPoint(x: lcd.minX + 14, y: lcd.minY + 8), size: 25, weight: .bold, color: rgb(0xffb04a), mono: true)
text("GTR", at: CGPoint(x: lcd.minX + 150, y: lcd.minY + 8), size: 25, weight: .heavy, color: rgb(0xffb04a), mono: true)
// level bars (16-part bar graph)
let heights: [CGFloat] = [62, 92, 74, 108, 54, 98, 80, 66, 102, 46, 84, 70]
let bw: CGFloat = 18, gap: CGFloat = 7.5, bottom = lcd.maxY - 14
for (i, h) in heights.enumerated() {
    let x = lcd.minX + 14 + CGFloat(i) * (bw + gap)
    var y = bottom
    var segIndex = 0
    while y > bottom - h {
        let alpha: CGFloat = (y - 9 <= bottom - h) ? 1.0 : 0.82
        ctx.setFillColor(rgb(segIndex > 9 ? 0xffd27a : 0xffa53a, alpha))
        ctx.fill(CGRect(x: x, y: y - 7, width: bw, height: 6))
        y -= 9; segIndex += 1
    }
}
noShadow()
// dot gaps for an authentic matrix look
ctx.setFillColor(rgb(0x120a04, 0.55))
for x in stride(from: lcd.minX, to: lcd.maxX, by: 6) { ctx.fill(CGRect(x: x + 4.5, y: lcd.minY, width: 1.5, height: lcd.height)) }
// glass reflection
let refl = CGMutablePath()
refl.move(to: CGPoint(x: lcd.minX, y: lcd.minY)); refl.addLine(to: CGPoint(x: lcd.maxX, y: lcd.minY))
refl.addLine(to: CGPoint(x: lcd.maxX, y: lcd.minY + 40)); refl.addLine(to: CGPoint(x: lcd.minX, y: lcd.minY + 78)); refl.closeSubpath()
linear(refl, rgb(0xffffff, 0.10), rgb(0xffffff, 0.0), from: CGPoint(x: lcd.midX, y: lcd.minY), to: CGPoint(x: lcd.midX, y: lcd.minY + 78))
ctx.restoreGState()
stroke(glass, rgb(0x000000, 0.9), 2)

// silkscreen under the LCD
// two-tone wordmark: "Tab" amber + "larion" silver
let wordFont = NSFont.systemFont(ofSize: 25, weight: .heavy)
let tabWidth = NSAttributedString(string: "Tab", attributes: [.font: wordFont]).size().width
let fullWidth = NSAttributedString(string: "Tablarion", attributes: [.font: wordFont]).size().width
text("Tab", at: CGPoint(x: 382, y: 610), size: 25, weight: .heavy, color: rgb(0xffa53a))
text("larion", at: CGPoint(x: 382 + tabWidth, y: 610), size: 25, weight: .heavy, color: rgb(0xd6d9df, 0.92))
text("TABLATURE SOUND MODULE", at: CGPoint(x: 382 + fullWidth + 18, y: 618), size: 13, weight: .bold, color: rgb(0x8c919b), kern: 1.2)

// ---------------------------------------------------------------------------------------------
// 5. Part / transport buttons (2 x 3) with LEDs
for row in 0..<3 {
    for col in 0..<2 {
        let r = CGRect(x: 734 + CGFloat(col) * 62, y: 440 + CGFloat(row) * 50, width: 50, height: 30)
        let p = rrect(r, 7)
        ctx.saveGState(); shadow(4, 3, rgb(0x000000, 0.7)); fill(p, rgb(0x2c2e34)); ctx.restoreGState()
        linear(p, rgb(0x55585f), rgb(0x2a2c31), from: CGPoint(x: r.midX, y: r.minY), to: CGPoint(x: r.midX, y: r.maxY))
        stroke(p, rgb(0x000000, 0.7), 1.5)
        let lit = (row == 0 && col == 0) || (row == 1 && col == 1)
        let ledColor: UInt32 = (row == 1 && col == 1) ? 0x3fd0c9 : 0xffa53a
        let led = CGRect(x: r.minX + 8, y: r.midY - 4, width: 8, height: 8)
        if lit {
            ctx.saveGState(); shadow(10, 0, rgb(ledColor, 1)); ctx.setFillColor(rgb(ledColor)); ctx.fillEllipse(in: led); ctx.restoreGState()
        } else {
            ctx.setFillColor(rgb(0x3a2a18)); ctx.fillEllipse(in: led)
        }
    }
}
// MIDI activity LED
ctx.saveGState(); shadow(12, 0, rgb(0x59f07a, 1)); ctx.setFillColor(rgb(0x59f07a)); ctx.fillEllipse(in: CGRect(x: 750, y: 600, width: 10, height: 10)); ctx.restoreGState()
text("MIDI", at: CGPoint(x: 766, y: 595), size: 13, weight: .bold, color: rgb(0x8c919b))

// ---------------------------------------------------------------------------------------------
// 6. Cable + 1/4" jack plugging into a "PLUG-IN" socket
let cable = CGMutablePath()
cable.move(to: CGPoint(x: 812, y: 655))
cable.addCurve(to: CGPoint(x: 846, y: 800), control1: CGPoint(x: 820, y: 720), control2: CGPoint(x: 900, y: 780))
cable.addCurve(to: CGPoint(x: 790, y: 812), control1: CGPoint(x: 830, y: 808), control2: CGPoint(x: 810, y: 812))
ctx.saveGState(); shadow(10, 8, rgb(0x000000, 0.7))
ctx.addPath(cable); ctx.setStrokeColor(rgb(0x0f1014)); ctx.setLineWidth(26); ctx.setLineCap(.round); ctx.strokePath()
ctx.restoreGState()
ctx.addPath(cable); ctx.setStrokeColor(rgb(0xffffff, 0.12)); ctx.setLineWidth(5); ctx.strokePath()

let cy: CGFloat = 812
// grip body
let grip = rrect(CGRect(x: 680, y: cy - 28, width: 118, height: 56), 18)
ctx.saveGState(); shadow(10, 6, rgb(0x000000, 0.6)); fill(grip, rgb(0x1d1f24)); ctx.restoreGState()
linear(grip, rgb(0x4a4e57), rgb(0x16171b), from: CGPoint(x: 0, y: cy - 28), to: CGPoint(x: 0, y: cy + 28))
ctx.setStrokeColor(rgb(0x000000, 0.45)); ctx.setLineWidth(3)
for i in 0..<5 { let x = 712 + CGFloat(i) * 15; ctx.move(to: CGPoint(x: x, y: cy - 22)); ctx.addLine(to: CGPoint(x: x, y: cy + 22)) }
ctx.strokePath()
fill(rrect(CGRect(x: 690, y: cy - 28, width: 10, height: 56), 3), rgb(0xffa53a, 0.9)) // amber band
stroke(grip, rgb(0x000000, 0.7), 2)
// sleeve / ring / tip
func metal(_ r: CGRect, _ rad: CGFloat) {
    let p = rrect(r, rad)
    linear(p, rgb(0xf3f5f8), rgb(0x6e737c), from: CGPoint(x: 0, y: r.minY), to: CGPoint(x: 0, y: r.maxY))
    ctx.saveGState(); ctx.addPath(p); ctx.clip()
    ctx.setFillColor(rgb(0xffffff, 0.55)); ctx.fill(CGRect(x: r.minX, y: r.minY + r.height * 0.22, width: r.width, height: r.height * 0.12))
    ctx.restoreGState()
    stroke(p, rgb(0x2a2c31, 0.6), 1)
}
metal(CGRect(x: 612, y: cy - 16, width: 70, height: 32), 4)    // sleeve
fill(rrect(CGRect(x: 600, y: cy - 13, width: 12, height: 26), 2), rgb(0x0b0c0e))
metal(CGRect(x: 572, y: cy - 13, width: 28, height: 26), 3)    // ring
fill(rrect(CGRect(x: 562, y: cy - 11, width: 10, height: 22), 2), rgb(0x0b0c0e))
let tip = CGMutablePath()
tip.move(to: CGPoint(x: 562, y: cy - 12)); tip.addLine(to: CGPoint(x: 540, y: cy - 12))
tip.addCurve(to: CGPoint(x: 518, y: cy), control1: CGPoint(x: 528, y: cy - 12), control2: CGPoint(x: 518, y: cy - 8))
tip.addCurve(to: CGPoint(x: 540, y: cy + 12), control1: CGPoint(x: 518, y: cy + 8), control2: CGPoint(x: 528, y: cy + 12))
tip.addLine(to: CGPoint(x: 562, y: cy + 12)); tip.closeSubpath()
ctx.saveGState(); shadow(18, 0, rgb(0x3fd0c9, 0.9)); fill(tip, rgb(0xcfd3da)); ctx.restoreGState()
linear(tip, rgb(0xf6f8fb), rgb(0x7c818a), from: CGPoint(x: 0, y: cy - 12), to: CGPoint(x: 0, y: cy + 12))

// socket badge
let sock = CGRect(x: 196, y: cy - 42, width: 300, height: 84)
let sockPath = rrect(sock, 42)
ctx.saveGState(); shadow(22, 0, rgb(0x3fd0c9, 0.55)); fill(sockPath, rgb(0x0b0d12)); ctx.restoreGState()
linear(sockPath, rgb(0x161a22), rgb(0x07080b), from: CGPoint(x: 0, y: sock.minY), to: CGPoint(x: 0, y: sock.maxY))
stroke(sockPath, rgb(0x3fd0c9, 0.95), 4)
ctx.saveGState(); shadow(12, 0, rgb(0x3fd0c9, 0.9))
text("PLUG-IN", at: CGPoint(x: sock.minX + 26, y: sock.minY + 20), size: 38, weight: .heavy, color: rgb(0x5fe6df), kern: 1)
ctx.restoreGState()
// jack hole
let hole = CGRect(x: sock.maxX - 64, y: cy - 22, width: 44, height: 44)
fill(CGPath(ellipseIn: hole, transform: nil), rgb(0x1e222b))
fill(CGPath(ellipseIn: hole.insetBy(dx: 9, dy: 9), transform: nil), rgb(0x000000))
ctx.saveGState(); shadow(10, 0, rgb(0x3fd0c9, 1)); stroke(CGPath(ellipseIn: hole.insetBy(dx: 3, dy: 3), transform: nil), rgb(0x3fd0c9), 3); ctx.restoreGState()

NSGraphicsContext.current = nil
guard let img = ctx.makeImage() else { fatalError("no image") }
let rep = NSBitmapImageRep(cgImage: img)
rep.size = NSSize(width: size, height: size)
let data = rep.representation(using: .png, properties: [:])!
try! data.write(to: URL(fileURLWithPath: outPath))
print("wrote \(outPath)")
