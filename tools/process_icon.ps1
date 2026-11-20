Add-Type -AssemblyName System.Drawing

$code = @"
using System;
using System.IO;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
using System.Collections.Generic;

public class IconProcessor {
    public static void ColorToHsv(Color color, out double hue, out double saturation, out double value) {
        int max = Math.Max(color.R, Math.Max(color.G, color.B));
        int min = Math.Min(color.R, Math.Min(color.G, color.B));

        hue = color.GetHue();
        saturation = (max == 0) ? 0 : 1d - (1d * min / max);
        value = max / 255d;
    }

    public static Color ColorFromHsv(double hue, double saturation, double value, int alpha) {
        int hi = Convert.ToInt32(Math.Floor(hue / 60)) % 6;
        double f = hue / 60 - Math.Floor(hue / 60);

        value = value * 255;
        int v = Convert.ToInt32(value);
        int p = Convert.ToInt32(value * (1 - saturation));
        int q = Convert.ToInt32(value * (1 - f * saturation));
        int t = Convert.ToInt32(value * (1 - (1 - f) * saturation));

        if (hi == 0) return Color.FromArgb(alpha, v, t, p);
        if (hi == 1) return Color.FromArgb(alpha, q, v, p);
        if (hi == 2) return Color.FromArgb(alpha, p, v, t);
        if (hi == 3) return Color.FromArgb(alpha, p, q, v);
        if (hi == 4) return Color.FromArgb(alpha, t, p, v);
        return Color.FromArgb(alpha, v, p, q);
    }

    public static Bitmap TransformBitmap(Bitmap src) {
        Bitmap dst = new Bitmap(src.Width, src.Height, PixelFormat.Format32bppArgb);
        Rectangle rect = new Rectangle(0, 0, src.Width, src.Height);

        BitmapData srcData = src.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        BitmapData dstData = dst.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);

        int bytes = Math.Abs(srcData.Stride) * src.Height;
        byte[] srcBuffer = new byte[bytes];
        byte[] dstBuffer = new byte[bytes];

        System.Runtime.InteropServices.Marshal.Copy(srcData.Scan0, srcBuffer, 0, bytes);

        // Process pixels
        for (int i = 0; i < bytes; i += 4) {
            byte b = srcBuffer[i];
            byte g = srcBuffer[i + 1];
            byte r = srcBuffer[i + 2];
            byte a = srcBuffer[i + 3];

            if (a == 0) {
                dstBuffer[i] = 0;
                dstBuffer[i + 1] = 0;
                dstBuffer[i + 2] = 0;
                dstBuffer[i + 3] = 0;
                continue;
            }

            Color c = Color.FromArgb(a, r, g, b);
            double h, s, v;
            ColorToHsv(c, out h, out s, out v);

            // Orange/amber/warm hue range:
            // Notice: at the glow edge, hue can wrap into [0, 50] or [340, 360].
            // But we must NOT affect the cyan needle (h ~ 170..210) or grey background.
            // Notice neutral dark background has R ~ 20, G ~ 21, B ~ 23 (s < 0.05).
            // A warm pixel has R noticeably > G and R > B.
            bool isWarm = false;
            double warmWeight = 0;

            if (h >= 0 && h <= 55) {
                // Hue 0..55: strong warm
                if (s > 0.08) {
                    isWarm = true;
                    warmWeight = Math.Min(1.0, (s - 0.08) / 0.1);
                }
            } else if (h >= 340 && h < 360) {
                // Red edge
                if (s > 0.08) {
                    isWarm = true;
                    warmWeight = Math.Min(1.0, (s - 0.08) / 0.1);
                }
            }

            if (isWarm && warmWeight > 0) {
                // Target emerald green: 155 deg
                // In original, orange center is ~30-35 deg.
                double normH = (h >= 340) ? (h - 360) : h; // -20 to 55
                double targetH = 155.0 + (normH - 30.0) * 0.4;
                if (targetH < 0) targetH += 360;
                if (targetH >= 360) targetH -= 360;

                // Blend hue smoothly
                double finalH = targetH;
                Color newColor = ColorFromHsv(finalH, s, v, a);

                if (warmWeight < 1.0) {
                    dstBuffer[i] = (byte)(b * (1 - warmWeight) + newColor.B * warmWeight);
                    dstBuffer[i + 1] = (byte)(g * (1 - warmWeight) + newColor.G * warmWeight);
                    dstBuffer[i + 2] = (byte)(r * (1 - warmWeight) + newColor.R * warmWeight);
                    dstBuffer[i + 3] = a;
                } else {
                    dstBuffer[i] = newColor.B;
                    dstBuffer[i + 1] = newColor.G;
                    dstBuffer[i + 2] = newColor.R;
                    dstBuffer[i + 3] = a;
                }
            } else {
                dstBuffer[i] = b;
                dstBuffer[i + 1] = g;
                dstBuffer[i + 2] = r;
                dstBuffer[i + 3] = a;
            }
        }

        System.Runtime.InteropServices.Marshal.Copy(dstBuffer, 0, dstData.Scan0, bytes);
        src.UnlockBits(srcData);
        dst.UnlockBits(dstData);

        return dst;
    }

    public static void SaveMultiSizeIcon(Bitmap baseImg, string icoPath, int[] sizes) {
        List<byte[]> pngBuffers = new List<byte[]>();
        foreach (int size in sizes) {
            using (Bitmap resized = new Bitmap(size, size, PixelFormat.Format32bppArgb)) {
                using (Graphics g = Graphics.FromImage(resized)) {
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.SmoothingMode = SmoothingMode.HighQuality;
                    g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    g.Clear(Color.Transparent);
                    g.DrawImage(baseImg, new Rectangle(0, 0, size, size));
                }
                using (MemoryStream ms = new MemoryStream()) {
                    resized.Save(ms, ImageFormat.Png);
                    pngBuffers.Add(ms.ToArray());
                }
            }
        }

        using (FileStream fs = new FileStream(icoPath, FileMode.Create)) {
            using (BinaryWriter bw = new BinaryWriter(fs)) {
                // ICONDIR header
                bw.Write((short)0); // reserved
                bw.Write((short)1); // icon type (1 = icon)
                bw.Write((short)sizes.Length); // count

                int offset = 6 + 16 * sizes.Length;

                for (int i = 0; i < sizes.Length; i++) {
                    int sz = sizes[i];
                    byte bWidth = (byte)(sz >= 256 ? 0 : sz);
                    byte bHeight = (byte)(sz >= 256 ? 0 : sz);
                    bw.Write(bWidth);
                    bw.Write(bHeight);
                    bw.Write((byte)0); // color count
                    bw.Write((byte)0); // reserved
                    bw.Write((short)1); // planes
                    bw.Write((short)32); // bit count
                    bw.Write((int)pngBuffers[i].Length); // bytes in res
                    bw.Write((int)offset); // image offset

                    offset += pngBuffers[i].Length;
                }

                // Write PNG image payloads
                for (int i = 0; i < sizes.Length; i++) {
                    bw.Write(pngBuffers[i]);
                }
            }
        }
    }
}
"@

Add-Type -TypeDefinition $code -ReferencedAssemblies System.Drawing

$srcPath = "D:\project\cpp\miniplayer\ui\qml\assets\appicon.png"
$emeraldPngPath = "D:\project\cpp\miniplayer\ui\qml\assets\appicon_emerald.png"
$icoPath = "D:\project\cpp\miniplayer\resources\windows\appicon_emerald.ico"

$src = [System.Drawing.Bitmap]::FromFile($srcPath)
$emeraldBmp = [IconProcessor]::TransformBitmap($src)
$emeraldBmp.Save($emeraldPngPath, [System.Drawing.Imaging.ImageFormat]::Png)

$sizes = @(16, 24, 32, 48, 64, 128, 256)
[IconProcessor]::SaveMultiSizeIcon($emeraldBmp, $icoPath, $sizes)

$src.Dispose()
$emeraldBmp.Dispose()
Write-Output "Successfully generated emerald PNG and multi-resolution ICO!"
