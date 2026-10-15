// QrBake - a QR code in the radar's look, for videos and print
//   QrBake <url> <out-dir>
// writes qr-radar.png (1080x1080, the whole picture) and qr-plate.png (just the code plate, on
// transparency). the encoder is here too (byte mode, error correction H, no library), so the
// modules can be drawn one by one in the radar's style. H keeps the code readable with the
// little radar drawn in its middle.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Text;

static class Qr
{
    // error correction H, versions 1..10: codewords per block, number of blocks (ISO 18004 table 9)
    static readonly int[] EccPerBlock = { -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28 };
    static readonly int[] Blocks = { -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8 };
    const int FormatBitsH = 2;

    public static bool[,] Encode(byte[] data, out int version, out int mask, out bool[,] function)
    {
        // smallest version that holds it: mode (4 bits) + count (8 bits, versions 1-9) + the bytes
        version = 0;
        for (int v = 1; v <= 9; v++)
            if (4 + 8 + data.Length * 8 <= DataCodewords(v) * 8) { version = v; break; }
        if (version == 0) throw new Exception("too long for versions 1-9");

        var bits = new List<bool>();
        void Put(int val, int n) { for (int i = n - 1; i >= 0; i--) bits.Add(((val >> i) & 1) != 0); }
        Put(0b0100, 4);
        Put(data.Length, 8);
        foreach (byte b in data) Put(b, 8);
        int cap = DataCodewords(version) * 8;
        Put(0, Math.Min(4, cap - bits.Count));            // terminator
        Put(0, (8 - bits.Count % 8) % 8);
        for (int pad = 0xEC; bits.Count < cap; pad ^= 0xEC ^ 0x11) Put(pad, 8);
        var cw = new byte[bits.Count / 8];
        for (int i = 0; i < bits.Count; i++) if (bits[i]) cw[i >> 3] |= (byte)(0x80 >> (i & 7));

        byte[] all = AddEcc(cw, version);
        int size = version * 4 + 17;
        var m = new bool[size, size];                       // [y, x]
        function = new bool[size, size];
        DrawFunction(m, function, version);
        DrawCodewords(m, function, all);

        // try the 8 masks, keep the one with the lowest penalty
        int best = -1, bestPen = int.MaxValue;
        for (int k = 0; k < 8; k++) {
            ApplyMask(m, function, k);
            DrawFormat(m, function, k);
            int p = Penalty(m);
            if (p < bestPen) { bestPen = p; best = k; }
            ApplyMask(m, function, k);                       // xor again = undo
        }
        mask = best;
        ApplyMask(m, function, best);
        DrawFormat(m, function, best);
        return m;
    }

    static int RawModules(int v)
    {
        int r = (16 * v + 128) * v + 64;
        if (v >= 2) {
            int na = v / 7 + 2;
            r -= (25 * na - 10) * na - 55;
            if (v >= 7) r -= 36;
        }
        return r;
    }

    static int DataCodewords(int v) => RawModules(v) / 8 - EccPerBlock[v] * Blocks[v];

    static int Mul(int x, int y)                            // GF(2^8), x^8 + x^4 + x^3 + x^2 + 1
    {
        int z = 0;
        for (int i = 7; i >= 0; i--) {
            z = (z << 1) ^ ((z >> 7) * 0x11D);
            z ^= ((y >> i) & 1) * x;
        }
        return z;
    }

    static byte[] Divisor(int degree)
    {
        var r = new byte[degree];
        r[degree - 1] = 1;
        int root = 1;
        for (int i = 0; i < degree; i++) {
            for (int j = 0; j < r.Length; j++) {
                r[j] = (byte)Mul(r[j], root);
                if (j + 1 < r.Length) r[j] ^= r[j + 1];
            }
            root = Mul(root, 0x02);
        }
        return r;
    }

    static byte[] Remainder(byte[] data, byte[] div)
    {
        var r = new byte[div.Length];
        foreach (byte b in data) {
            int f = b ^ r[0];
            Array.Copy(r, 1, r, 0, r.Length - 1);
            r[^1] = 0;
            for (int i = 0; i < r.Length; i++) r[i] ^= (byte)Mul(div[i], f);
        }
        return r;
    }

    // split into blocks, error correction for each, interleave
    static byte[] AddEcc(byte[] data, int v)
    {
        int nb = Blocks[v], ecc = EccPerBlock[v], raw = RawModules(v) / 8;
        int nShort = nb - raw % nb, shortLen = raw / nb;
        var div = Divisor(ecc);
        var blocks = new List<byte[]>();
        for (int i = 0, k = 0; i < nb; i++) {
            int len = shortLen - ecc + (i < nShort ? 0 : 1);
            var dat = new byte[len];
            Array.Copy(data, k, dat, 0, len);
            k += len;
            var e = Remainder(dat, div);
            var blk = new byte[shortLen + 1];
            Array.Copy(dat, 0, blk, 0, len);
            Array.Copy(e, 0, blk, shortLen + 1 - ecc, ecc);   // short blocks keep a hole before their ecc
            blocks.Add(blk);
        }
        var res = new List<byte>();
        for (int i = 0; i < shortLen + 1; i++)
            for (int j = 0; j < nb; j++)
                if (i != shortLen - ecc || j >= nShort) res.Add(blocks[j][i]);
        return res.ToArray();
    }

    static void Set(bool[,] m, bool[,] f, int x, int y, bool dark) { m[y, x] = dark; f[y, x] = true; }

    static void DrawFunction(bool[,] m, bool[,] f, int v)
    {
        int size = m.GetLength(0);
        for (int i = 0; i < size; i++) { Set(m, f, 6, i, i % 2 == 0); Set(m, f, i, 6, i % 2 == 0); }
        foreach (var (cx, cy) in new[] { (3, 3), (size - 4, 3), (3, size - 4) })
            for (int dy = -4; dy <= 4; dy++)
                for (int dx = -4; dx <= 4; dx++) {
                    int x = cx + dx, y = cy + dy;
                    if (x < 0 || y < 0 || x >= size || y >= size) continue;
                    int d = Math.Max(Math.Abs(dx), Math.Abs(dy));
                    Set(m, f, x, y, d != 2 && d != 4);
                }
        int[] pos = Align(v);
        for (int i = 0; i < pos.Length; i++)
            for (int j = 0; j < pos.Length; j++) {
                if ((i == 0 && j == 0) || (i == 0 && j == pos.Length - 1) || (i == pos.Length - 1 && j == 0)) continue;
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++)
                        Set(m, f, pos[i] + dx, pos[j] + dy, Math.Max(Math.Abs(dx), Math.Abs(dy)) != 1);
            }
        DrawFormat(m, f, 0);                                  // reserves the area, real bits come later
    }

    static int[] Align(int v)
    {
        if (v == 1) return Array.Empty<int>();
        int na = v / 7 + 2, size = v * 4 + 17;
        int step = (int)Math.Ceiling((v * 4 + 4) / (double)(na * 2 - 2)) * 2;
        var r = new int[na];
        r[0] = 6;
        for (int i = na - 1, p = size - 7; i >= 1; i--, p -= step) r[i] = p;
        return r;
    }

    static void DrawFormat(bool[,] m, bool[,] f, int mask)
    {
        int size = m.GetLength(0);
        int data = FormatBitsH << 3 | mask, rem = data;
        for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        int bits = (data << 10 | rem) ^ 0x5412;
        bool B(int i) => ((bits >> i) & 1) != 0;
        for (int i = 0; i <= 5; i++) Set(m, f, 8, i, B(i));
        Set(m, f, 8, 7, B(6));
        Set(m, f, 8, 8, B(7));
        Set(m, f, 7, 8, B(8));
        for (int i = 9; i < 15; i++) Set(m, f, 14 - i, 8, B(i));
        for (int i = 0; i < 8; i++) Set(m, f, size - 1 - i, 8, B(i));
        for (int i = 8; i < 15; i++) Set(m, f, 8, size - 15 + i, B(i));
        Set(m, f, 8, size - 8, true);                         // the dark module
    }

    static void DrawCodewords(bool[,] m, bool[,] f, byte[] data)
    {
        int size = m.GetLength(0), i = 0;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < size; vert++)
                for (int j = 0; j < 2; j++) {
                    int x = right - j;
                    bool up = ((right + 1) & 2) == 0;
                    int y = up ? size - 1 - vert : vert;
                    if (!f[y, x] && i < data.Length * 8) {
                        m[y, x] = ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0;
                        i++;
                    }
                }
        }
    }

    static void ApplyMask(bool[,] m, bool[,] f, int k)
    {
        int size = m.GetLength(0);
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                if (f[y, x]) continue;
                bool inv = k switch {
                    0 => (x + y) % 2 == 0,
                    1 => y % 2 == 0,
                    2 => x % 3 == 0,
                    3 => (x + y) % 3 == 0,
                    4 => (x / 3 + y / 2) % 2 == 0,
                    5 => x * y % 2 + x * y % 3 == 0,
                    6 => (x * y % 2 + x * y % 3) % 2 == 0,
                    _ => ((x + y) % 2 + x * y % 3) % 2 == 0,
                };
                if (inv) m[y, x] = !m[y, x];
            }
    }

    // the four penalty rules of the standard
    static int Penalty(bool[,] m)
    {
        int size = m.GetLength(0), p = 0, dark = 0;
        for (int pass = 0; pass < 2; pass++)
            for (int a = 0; a < size; a++) {
                int run = 1;
                for (int b = 1; b <= size; b++) {
                    bool same = b < size && Get(m, pass, a, b) == Get(m, pass, a, b - 1);
                    if (same) { run++; continue; }
                    if (run >= 5) p += 3 + run - 5;
                    run = 1;
                }
                for (int b = 0; b + 11 <= size; b++) {         // 1:1:3:1:1 with 4 light on one side
                    string s = "";
                    for (int k = 0; k < 11; k++) s += Get(m, pass, a, b + k) ? '1' : '0';
                    if (s == "10111010000" || s == "00001011101") p += 40;
                }
            }
        for (int y = 0; y + 1 < size; y++)
            for (int x = 0; x + 1 < size; x++)
                if (m[y, x] == m[y, x + 1] && m[y, x] == m[y + 1, x] && m[y, x] == m[y + 1, x + 1]) p += 3;
        foreach (bool d in m) if (d) dark++;
        int total = size * size;
        int kdev = (Math.Abs(dark * 20 - total * 10) + total - 1) / total - 1;
        return p + Math.Max(0, kdev) * 10;
    }

    static bool Get(bool[,] m, int pass, int a, int b) => pass == 0 ? m[a, b] : m[b, a];
}

static class Program
{
    // the radar's ARCTIC theme
    static readonly Color Bg0 = Color.FromArgb(3, 8, 18), Bg1 = Color.FromArgb(9, 22, 46),
        Glass = Color.FromArgb(16, 42, 82), Prim = Color.FromArgb(92, 225, 255), Quiet = Color.FromArgb(46, 130, 178),
        Focus = Color.FromArgb(234, 247, 255), Dim = Color.FromArgb(120, 160, 196), Ink = Color.FromArgb(4, 12, 28);

    static int Main(string[] args)
    {
        string url = args.Length > 0 ? args[0] : "https://github.com/KirinDenis/OWLOS-WIFI-RADAR";
        string dir = args.Length > 1 ? args[1] : ".";
        Directory.CreateDirectory(dir);
        var m = Qr.Encode(Encoding.UTF8.GetBytes(url), out int ver, out int mask, out bool[,] fn);
        int n = m.GetLength(0);
        Console.WriteLine($"{url}: version {ver} ({n}x{n}), error correction H, mask {mask}");

        const int W = 1080, Mod = 13, QuietMods = 4;
        int plate = (n + 2 * QuietMods) * Mod;               // 637 px for 41 modules
        int px = (W - plate) / 2, py = 150;

        using (var bmp = new Bitmap(W, W, PixelFormat.Format32bppArgb))
        using (var g = Graphics.FromImage(bmp)) {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.PixelOffsetMode = PixelOffsetMode.Half;
            Background(g, W, px + plate / 2f, py + plate / 2f);
            Plate(g, m, fn, px, py, plate, Mod, QuietMods);
            Captions(g, W, py + plate, url);
            Brackets(g, W);
            bmp.Save(Path.Combine(dir, "qr-radar.png"), ImageFormat.Png);
        }
        int pad = 40;
        using (var bmp = new Bitmap(plate + 2 * pad, plate + 2 * pad, PixelFormat.Format32bppArgb))
        using (var g = Graphics.FromImage(bmp)) {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.PixelOffsetMode = PixelOffsetMode.Half;
            g.Clear(Color.Transparent);
            Plate(g, m, fn, pad, pad, plate, Mod, QuietMods);
            bmp.Save(Path.Combine(dir, "qr-plate.png"), ImageFormat.Png);
        }
        Console.WriteLine($"wrote {Path.Combine(dir, "qr-radar.png")} and qr-plate.png");
        return 0;
    }

    static Color A(Color c, int a) => Color.FromArgb(Math.Clamp(a, 0, 255), c);

    static void Background(Graphics g, int w, float cx, float cy)
    {
        using (var path = new GraphicsPath()) {
            path.AddEllipse(-w * 0.35f, -w * 0.35f, w * 1.7f, w * 1.7f);
            using var br = new PathGradientBrush(path) { CenterColor = Bg1, SurroundColors = new[] { Bg0 }, CenterPoint = new PointF(cx, cy) };
            g.FillRectangle(br, 0, 0, w, w);
        }
        using (var grid = new Pen(A(Quiet, 26), 1)) {
            for (int x = (int)cx % 27; x < w; x += 27) g.DrawLine(grid, x, 0, x, w);
            for (int y = (int)cy % 27; y < w; y += 27) g.DrawLine(grid, 0, y, w, y);
        }
        // the radar behind the plate: a sweep, rings, ticks every 5 degrees
        for (int i = 0; i < 70; i++) {
            float a0 = -100 + i;
            using var b = new SolidBrush(A(Prim, (int)(42 * (i / 70f) * (i / 70f))));
            g.FillPie(b, cx - 520, cy - 520, 1040, 1040, a0, 1.2f);
        }
        using (var sweep = new Pen(A(Prim, 170), 2)) {
            double r = -30 * Math.PI / 180;
            g.DrawLine(sweep, cx, cy, cx + (float)(520 * Math.Cos(r)), cy + (float)(520 * Math.Sin(r)));
        }
        foreach (int r in new[] { 380, 450, 520 })
            using (var ring = new Pen(A(Prim, r == 520 ? 110 : 55), r == 520 ? 2 : 1.2f))
                g.DrawEllipse(ring, cx - r, cy - r, 2 * r, 2 * r);
        using (var tick = new Pen(A(Prim, 120), 1.4f))
            for (int d = 0; d < 360; d += 5) {
                double a = d * Math.PI / 180;
                float r0 = d % 30 == 0 ? 500 : 510;
                g.DrawLine(tick, cx + (float)(r0 * Math.Cos(a)), cy + (float)(r0 * Math.Sin(a)),
                           cx + (float)(520 * Math.Cos(a)), cy + (float)(520 * Math.Sin(a)));
            }
        // a few networks on the rings, like blips
        var rnd = new Random(82);
        for (int i = 0; i < 9; i++) {
            double a = rnd.NextDouble() * Math.PI * 2;
            float r = 400 + (float)rnd.NextDouble() * 110, x = cx + (float)(r * Math.Cos(a)), y = cy + (float)(r * Math.Sin(a));
            for (int k = 6; k >= 1; k--)
                using (var gl = new SolidBrush(A(Prim, 14 * (7 - k)))) g.FillEllipse(gl, x - k * 2.6f, y - k * 2.6f, k * 5.2f, k * 5.2f);
            using var dot = new SolidBrush(Focus);
            g.FillEllipse(dot, x - 3, y - 3, 6, 6);
        }
        var r2 = new Random(7);                               // dust
        for (int i = 0; i < 240; i++)
            using (var d = new SolidBrush(A(Dim, r2.Next(20, 90)))) g.FillRectangle(d, r2.Next(w), r2.Next(w), 1.4f, 1.4f);
    }

    static GraphicsPath Chamfer(float x, float y, float w, float h, float k)
    {
        var p = new GraphicsPath();
        p.AddPolygon(new[] { new PointF(x + k, y), new PointF(x + w, y), new PointF(x + w, y + h - k),
                             new PointF(x + w - k, y + h), new PointF(x, y + h), new PointF(x, y + k) });
        return p;
    }

    /* the plate: a glowing cyan screen with the code on it in dark ink. dark on light is what every
     * scanner reads; light on dark (the obvious sci-fi way) old phone apps can't */
    static void Plate(Graphics g, bool[,] m, bool[,] fn, int x0, int y0, int plate, int mod, int quiet)
    {
        int n = m.GetLength(0);
        for (int k = 14; k >= 1; k--)                         // outer glow
            using (var p = Chamfer(x0 - k * 2, y0 - k * 2, plate + k * 4, plate + k * 4, 26 + k * 2))
            using (var pen = new Pen(A(Prim, 9 + (14 - k)), 2)) g.DrawPath(pen, p);
        using (var p = Chamfer(x0, y0, plate, plate, 26)) {
            using (var br = new LinearGradientBrush(new PointF(0, y0), new PointF(0, y0 + plate),
                                                   Color.FromArgb(150, 236, 255), Color.FromArgb(104, 214, 248)))
                g.FillPath(br, p);
            using var edge = new Pen(Focus, 3);
            g.DrawPath(edge, p);
        }
        using (var scan = new Pen(Color.FromArgb(10, 0, 40, 80), 1))   // faint scan lines, a screen not paper
            for (int y = y0 + 3; y < y0 + plate - 3; y += 4) g.DrawLine(scan, x0 + 4, y, x0 + plate - 4, y);

        float ox = x0 + quiet * mod, oy = y0 + quiet * mod;
        int c = n / 2;
        using var ink = new SolidBrush(Ink);
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                if (!m[y, x] || InFinder(n, x, y)) continue;
                if (Math.Abs(x - c) <= 3 && Math.Abs(y - c) <= 3) continue;      // the emblem goes there
                float s = fn[y, x] ? mod : mod * 0.86f, o = (mod - s) / 2;     // data as dots, structure solid
                using var p = Round(ox + x * mod + o, oy + y * mod + o, s, s, fn[y, x] ? 2.2f : s * 0.32f);
                g.FillPath(ink, p);
            }
        foreach (var (fx, fy) in new[] { (0, 0), (n - 7, 0), (0, n - 7) }) Finder(g, ox + fx * mod, oy + fy * mod, mod);
        Emblem(g, ox + (c + 0.5f) * mod, oy + (c + 0.5f) * mod, mod);
    }

    static bool InFinder(int n, int x, int y) => (x < 7 && y < 7) || (x >= n - 7 && y < 7) || (x < 7 && y >= n - 7);

    static GraphicsPath Round(float x, float y, float w, float h, float r)
    {
        var p = new GraphicsPath();
        float d = r * 2;
        p.AddArc(x, y, d, d, 180, 90);
        p.AddArc(x + w - d, y, d, d, 270, 90);
        p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
        p.AddArc(x, y + h - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }

    // the three corner eyes, chamfered like the radar's boxes, ratios kept exactly 1:1:3:1:1
    static void Finder(Graphics g, float x, float y, int mod)
    {
        using var ink = new SolidBrush(Ink);
        using (var outer = Chamfer(x, y, 7 * mod, 7 * mod, 2.2f * mod))
        using (var hole = Chamfer(x + mod, y + mod, 5 * mod, 5 * mod, 1.4f * mod)) {
            var region = new Region(outer);
            region.Exclude(hole);
            g.FillRegion(ink, region);
        }
        using (var core = Chamfer(x + 2 * mod, y + 2 * mod, 3 * mod, 3 * mod, 0.9f * mod)) g.FillPath(ink, core);
    }

    // a small radar in the middle: ring, sweep, one blip. error correction H covers it easily
    static void Emblem(Graphics g, float cx, float cy, int mod)
    {
        float r = 3.2f * mod;
        using (var ring = new Pen(Ink, mod * 0.55f)) {
            g.DrawEllipse(ring, cx - r, cy - r, 2 * r, 2 * r);
            float r2 = r * 0.55f;
            ring.Width = mod * 0.3f;
            g.DrawEllipse(ring, cx - r2, cy - r2, 2 * r2, 2 * r2);
        }
        using (var sweep = new SolidBrush(Ink)) g.FillPie(sweep, cx - r, cy - r, 2 * r, 2 * r, -90, 55);
        using (var dot = new SolidBrush(Ink)) g.FillEllipse(dot, cx + r * 0.25f - mod * 0.45f, cy + r * 0.4f - mod * 0.45f, mod * 0.9f, mod * 0.9f);
    }

    static void Captions(Graphics g, int w, int plateBottom, string url)
    {
        Micro(g, "OWLOS WIFI RADAR", w / 2f, plateBottom + 52, 7, Prim, true);
        string shortUrl = url.Replace("https://", "").ToUpperInvariant();
        Micro(g, shortUrl, w / 2f, plateBottom + 118, 3, Focus, false);
        Micro(g, "FREE AND OPEN SOURCE   MIT", w / 2f, plateBottom + 158, 3, Dim, false);
        Micro(g, "SCAN FOR THE CODE", w / 2f, 92, 4, Focus, false);
        using var hatch = new HatchBrush(HatchStyle.WideUpwardDiagonal, A(Quiet, 150), Color.Transparent);
        float tw = MicroWidth("SCAN FOR THE CODE", 4);
        g.FillRectangle(hatch, 70, 99, w / 2f - tw / 2 - 90, 10);
        g.FillRectangle(hatch, w / 2f + tw / 2 + 20, 99, w / 2f - tw / 2 - 90, 10);
    }

    static void Brackets(Graphics g, int w)
    {
        using var pen = new Pen(A(Prim, 200), 3);
        foreach (var (x, y, sx, sy) in new[] { (30f, 30f, 1, 1), (w - 30f, 30f, -1, 1), (30f, w - 30f, 1, -1), (w - 30f, w - 30f, -1, -1) }) {
            g.DrawLines(pen, new[] { new PointF(x, y + sy * 70), new PointF(x, y + sy * 18), new PointF(x + sx * 18, y), new PointF(x + sx * 70, y) });
        }
    }

    /* --- the 3x5 HUD font from OWL FLY, the same one the radar draws its labels with --- */
    static readonly byte[][] MicroGlyphs = {
        new byte[] {0xE0,0xA0,0xA0,0xA0,0xE0}, new byte[] {0x40,0xC0,0x40,0x40,0xE0}, new byte[] {0xE0,0x20,0xE0,0x80,0xE0},
        new byte[] {0xE0,0x20,0xE0,0x20,0xE0}, new byte[] {0xA0,0xA0,0xE0,0x20,0x20}, new byte[] {0xE0,0x80,0xE0,0x20,0xE0},
        new byte[] {0xE0,0x80,0xE0,0xA0,0xE0}, new byte[] {0xE0,0x20,0x40,0x40,0x40}, new byte[] {0xE0,0xA0,0xE0,0xA0,0xE0},
        new byte[] {0xE0,0xA0,0xE0,0x20,0xE0}, new byte[] {0x40,0xA0,0xE0,0xA0,0xA0}, new byte[] {0xC0,0xA0,0xC0,0xA0,0xC0},
        new byte[] {0x60,0x80,0x80,0x80,0x60}, new byte[] {0xC0,0xA0,0xA0,0xA0,0xC0}, new byte[] {0xE0,0x80,0xC0,0x80,0xE0},
        new byte[] {0xE0,0x80,0xC0,0x80,0x80}, new byte[] {0x60,0x80,0xA0,0xA0,0x60}, new byte[] {0xA0,0xA0,0xE0,0xA0,0xA0},
        new byte[] {0xE0,0x40,0x40,0x40,0xE0}, new byte[] {0x20,0x20,0x20,0xA0,0x40}, new byte[] {0xA0,0xC0,0x80,0xC0,0xA0},
        new byte[] {0x80,0x80,0x80,0x80,0xE0}, new byte[] {0xA0,0xE0,0xA0,0xA0,0xA0}, new byte[] {0xE0,0xA0,0xA0,0xA0,0xA0},
        new byte[] {0xE0,0xA0,0xA0,0xA0,0xE0}, new byte[] {0xE0,0xA0,0xE0,0x80,0x80}, new byte[] {0xE0,0xA0,0xA0,0xE0,0x20},
        new byte[] {0xC0,0xA0,0xC0,0xA0,0xA0}, new byte[] {0xE0,0x80,0xE0,0x20,0xE0}, new byte[] {0xE0,0x40,0x40,0x40,0x40},
        new byte[] {0xA0,0xA0,0xA0,0xA0,0xE0}, new byte[] {0xA0,0xA0,0xA0,0xA0,0x40}, new byte[] {0xA0,0xA0,0xA0,0xE0,0xA0},
        new byte[] {0xA0,0xA0,0x40,0xA0,0xA0}, new byte[] {0xA0,0xA0,0x40,0x40,0x40}, new byte[] {0xE0,0x20,0x40,0x80,0xE0},
    };
    static readonly Dictionary<char, byte[]> MicroSigns = new() {
        ['-'] = new byte[] { 0x00, 0x00, 0xE0, 0x00, 0x00 }, ['.'] = new byte[] { 0x00, 0x00, 0x00, 0x00, 0x40 },
        ['/'] = new byte[] { 0x20, 0x20, 0x40, 0x80, 0x80 }, [':'] = new byte[] { 0x00, 0x40, 0x00, 0x40, 0x00 },
    };

    static byte[]? Glyph(char ch)
    {
        if (ch >= '0' && ch <= '9') return MicroGlyphs[ch - '0'];
        if (ch >= 'A' && ch <= 'Z') return MicroGlyphs[ch - 'A' + 10];
        return MicroSigns.TryGetValue(ch, out var gl) ? gl : null;
    }

    static float MicroWidth(string s, int scale) => (s.Length * 4 - 1) * scale;

    static void Micro(Graphics g, string s, float cx, float y, int scale, Color col, bool glow)
    {
        float x = cx - MicroWidth(s, scale) / 2;
        for (int pass = glow ? 0 : 1; pass < 2; pass++) {
            float x1 = x;
            foreach (char ch in s) {
                var gl = Glyph(ch);
                if (gl != null)
                    for (int r = 0; r < 5; r++)
                        for (int c = 0; c < 3; c++) {
                            if ((gl[r] & (0x80 >> c)) == 0) continue;
                            float px = x1 + c * scale, py = y + r * scale;
                            if (pass == 0) {
                                using var b = new SolidBrush(A(col, 40));
                                g.FillRectangle(b, px - scale * 0.5f, py - scale * 0.5f, scale * 2, scale * 2);
                            } else {
                                using var b = new SolidBrush(col);
                                g.FillRectangle(b, px, py, scale, scale);
                            }
                        }
                x1 += 4 * scale;
            }
        }
    }
}
