// Renders the project's SVG icon (packaging/linux/torchlight-recomp.svg) into the PNG sizes of a
// macOS .iconset, for iconutil (make_app.sh). AppKit reads SVG since macOS 13.
// Usage: swift make_icon.swift ICON.svg OUTPUT.iconset
import AppKit

let arguments = CommandLine.arguments
guard arguments.count == 3, let image = NSImage(contentsOfFile: arguments[1]) else {
  FileHandle.standardError.write(Data("usage: make_icon.swift ICON.svg OUTPUT.iconset\n".utf8))
  exit(2)
}
let output = URL(fileURLWithPath: arguments[2])
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
for points in [16, 32, 128, 256, 512] {
  for scale in [1, 2] {
    let pixels = points * scale
    guard let bitmap = NSBitmapImageRep(
      bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels, bitsPerSample: 8,
      samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
      bytesPerRow: 0, bitsPerPixel: 0)
    else { exit(1) }
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
    image.draw(in: NSRect(x: 0, y: 0, width: pixels, height: pixels))
    NSGraphicsContext.restoreGraphicsState()
    let name = scale == 1 ? "icon_\(points)x\(points).png" : "icon_\(points)x\(points)@2x.png"
    let png = bitmap.representation(using: .png, properties: [:])!
    try png.write(to: output.appendingPathComponent(name))
  }
}
