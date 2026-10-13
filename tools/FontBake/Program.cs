// FontBake - bakes a TTF into a 4-bit antialiased C font for core/font.h
//   FontBake <font.ttf> <NAME> <advance-px> <ranges> <out.c> [sheet.png]
//   advance-px = monospace cell width. em size is picked so the advance is exactly
//                that, glyphs sit on whole pixels and stay crisp
//   ranges = code points, e.g. "32-126,160-383,1024-1119,8230"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

static class Program
{
    record Glyph(int Cp, int W, int H, int Xo, int Yo, int Adv, byte[] Bits);

    static int Main(string[] args)
    {
        if (args.Length < 5)
        {
            Console.Error.WriteLine("usage: FontBake <font.ttf> <NAME> <advance-px> <ranges> <out.c> [sheet.png]");
            return 2;
        }
        string ttf = Path.GetFullPath(args[0]);
        string name = args[1];
        int advPx = int.Parse(args[2], CultureInfo.InvariantCulture);
        var cps = ParseRanges(args[3]);
        string outC = args[4];
        string? sheet = args.Length > 5 ? args[5] : null;

        // WPF GlyphTypeface gives exact advances and wich chars the font has, missing ones are skipped
        var gt = new System.Windows.Media.GlyphTypeface(new Uri(ttf));
        if (!gt.CharacterToGlyphMap.TryGetValue('0', out ushort zero))
            throw new Exception("font has no '0'");
        double advEm = gt.AdvanceWidths[zero];
        double em = advPx / advEm;

        using var pfc = new PrivateFontCollection();
        pfc.AddFontFile(ttf);
        using var font = new Font(pfc.Families[0], (float)em, FontStyle.Regular, GraphicsUnit.Pixel);
        var fam = pfc.Families[0];
        double unitsPerEm = fam.GetEmHeight(FontStyle.Regular);
        int ascent = (int)Math.Round(fam.GetCellAscent(FontStyle.Regular) / unitsPerEm * em);
        int descent = (int)Math.Round(fam.GetCellDescent(FontStyle.Regular) / unitsPerEm * em);

        const int Pad = 32, Size = 128;
        using var bmp = new Bitmap(Size, Size, PixelFormat.Format32bppArgb);
        using var g = Graphics.FromImage(bmp);
        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;   // hinted. white on black, green channel = coverage
        g.SmoothingMode = SmoothingMode.None;
        using var white = new SolidBrush(Color.White);
        var fmt = StringFormat.GenericTypographic;

        var glyphs = new List<Glyph>();
        int top = int.MaxValue, bottom = int.MinValue;
        foreach (int cp in cps)
        {
            if (!gt.CharacterToGlyphMap.TryGetValue(cp, out ushort gi)) continue;
            int adv = (int)Math.Round(gt.AdvanceWidths[gi] * em);
            g.Clear(Color.Black);
            g.DrawString(char.ConvertFromUtf32(cp), font, white, Pad, Pad, fmt);
            int x0 = Size, y0 = Size, x1 = -1, y1 = -1;
            var cov = new byte[Size, Size];
            for (int y = 0; y < Size; y++)
                for (int x = 0; x < Size; x++)
                {
                    byte v = bmp.GetPixel(x, y).G;
                    cov[y, x] = v;
                    if (v >= 9) { x0 = Math.Min(x0, x); x1 = Math.Max(x1, x); y0 = Math.Min(y0, y); y1 = Math.Max(y1, y); }
                }
            if (x1 < 0) { glyphs.Add(new Glyph(cp, 0, 0, 0, 0, adv, Array.Empty<byte>())); continue; }
            int w = x1 - x0 + 1, h = y1 - y0 + 1, stride = (w + 1) / 2;
            var bits = new byte[stride * h];
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                {
                    int q = (cov[y0 + y, x0 + x] * 15 + 127) / 255;
                    bits[y * stride + x / 2] |= (byte)((x & 1) == 0 ? q << 4 : q);
                }
            glyphs.Add(new Glyph(cp, w, h, x0 - Pad, y0 - Pad, adv, bits));
            top = Math.Min(top, y0 - Pad); bottom = Math.Max(bottom, y1 - Pad);
        }

        // shift everything up so the top of H/d/0 lands right under the line top.
        // accents on capitals can stick out above, they get clipped at the line top
        int capTop = glyphs.Where(q => q.Cp == 'H' || q.Cp == 'd' || q.Cp == '0').Min(q => q.Yo);
        int lift = Math.Max(0, capTop - 1);
        int lineH = ascent + descent - lift;
        glyphs = glyphs.Select(q => q with { Yo = q.Yo - lift }).ToList();

        WriteC(outC, name, Path.GetFileName(ttf), em, advPx, lineH, ascent - lift, glyphs);
        if (sheet != null) WriteSheet(sheet, glyphs, lineH, advPx);
        Console.WriteLine($"{name}: {glyphs.Count} glyphs, em {em:F2}px, advance {advPx}, line {lineH}, ascent {ascent - lift}, ink rows {top - lift}..{bottom - lift}");
        return 0;
    }

    static List<int> ParseRanges(string spec)
    {
        var list = new List<int>();
        foreach (var part in spec.Split(',', StringSplitOptions.RemoveEmptyEntries))
        {
            var ab = part.Split('-');
            int a = int.Parse(ab[0], CultureInfo.InvariantCulture);
            int b = ab.Length > 1 ? int.Parse(ab[1], CultureInfo.InvariantCulture) : a;
            for (int c = a; c <= b; c++) list.Add(c);
        }
        return list.Distinct().OrderBy(c => c).ToList();
    }

    static void WriteC(string path, string name, string ttf, double em, int adv, int lineH, int ascent, List<Glyph> glyphs)
    {
        var sb = new StringBuilder();
        sb.Append($"/* Generated by tools/FontBake from {ttf} at {em.ToString("F2", CultureInfo.InvariantCulture)} px (advance {adv}). Do not edit. */\r\n");
        sb.Append("#include \"font.h\"\r\n\r\n");
        sb.Append("static const uint8_t BITS[] = {\r\n");
        int off = 0;
        var offs = new List<int>();
        foreach (var q in glyphs)
        {
            offs.Add(off);
            if (q.Bits.Length == 0) continue;
            sb.Append("    ");
            sb.Append(string.Join(",", q.Bits.Select(b => "0x" + b.ToString("X2"))));
            sb.Append($",  /* U+{q.Cp:X4} */\r\n");
            off += q.Bits.Length;
        }
        if (off == 0) sb.Append("    0\r\n");
        sb.Append("};\r\n\r\nstatic const glyph_t GLYPHS[] = {\r\n");
        for (int i = 0; i < glyphs.Count; i++)
        {
            var q = glyphs[i];
            sb.Append($"    {{ 0x{q.Cp:X4}, {q.W}, {q.H}, {q.Xo}, {q.Yo}, {q.Adv}, 0, {offs[i]} }},\r\n");
        }
        sb.Append("};\r\n\r\n");
        sb.Append($"const font_t FONT_{name} = {{ GLYPHS, {glyphs.Count}, {lineH}, {ascent}, BITS }};\r\n");
        File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
    }

    // contact sheet at 4x, white on navy, just to eyeball what was baked
    static void WriteSheet(string path, List<Glyph> glyphs, int lineH, int adv)
    {
        const int Cols = 32, Z = 4;
        int rows = (glyphs.Count + Cols - 1) / Cols;
        int cw = adv + 2, ch = lineH + 2;
        using var bmp = new Bitmap(Cols * cw * Z, rows * ch * Z);
        using (var g = Graphics.FromImage(bmp)) g.Clear(Color.FromArgb(6, 16, 34));
        for (int i = 0; i < glyphs.Count; i++)
        {
            var q = glyphs[i];
            int ox = (i % Cols) * cw + 1, oy = (i / Cols) * ch + 1;
            int stride = (q.W + 1) / 2;
            for (int y = 0; y < q.H; y++)
                for (int x = 0; x < q.W; x++)
                {
                    int v = (q.Bits[y * stride + x / 2] >> ((x & 1) == 0 ? 4 : 0)) & 15;
                    if (v == 0) continue;
                    int px = ox + q.Xo + x, py = oy + q.Yo + y;
                    if (px < 0 || py < 0) continue;
                    int c = v * 17;
                    var col = Color.FromArgb(6 + (220 - 6) * c / 255, 16 + (240 - 16) * c / 255, 34 + (255 - 34) * c / 255);
                    for (int dy = 0; dy < Z; dy++)
                        for (int dx = 0; dx < Z; dx++)
                        {
                            int X = px * Z + dx, Y = py * Z + dy;
                            if (X < bmp.Width && Y < bmp.Height) bmp.SetPixel(X, Y, col);
                        }
                }
        }
        bmp.Save(path, ImageFormat.Png);
    }
}
