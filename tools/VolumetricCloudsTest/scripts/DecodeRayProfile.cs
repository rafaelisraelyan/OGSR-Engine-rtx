using System;
using System.Globalization;
using System.IO;
using System.Text;

// Decoder for --debug-mode=ray-profile captures.
//
// The cloud compute pass writes four 256-column channel blocks per output row:
//   0: (density, height, profile)
//   1: (macro.r, mid.g, weather.r)
//   2: (coverage, normalized t, profile*shaped)
//   3: (shaped, typeValue)
// The composite pass applies sRGB; this tool converts back to linear and
// reconstructs the world sample positions from the same camera/slab math as
// CloudRayMarch.hlsl.
static class DecodeRayProfile
{
    static double SrgbToLinear(double v)
    {
        return v <= 0.04045 ? v / 12.92 : Math.Pow((v + 0.055) / 1.055, 2.4);
    }

    static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine(
                "usage: DecodeRayProfile <bmp> <csv> [probeColumn] [rows]");
            return 2;
        }

        var inv = CultureInfo.InvariantCulture;
        string bmp = args[0];
        string csv = args[1];
        double probeColumn = args.Length > 2
            ? double.Parse(args[2], inv) : 0.35;
        int[] rows = { 180, 260, 340, 420, 500, 580, 660 };
        if (args.Length > 3)
        {
            string[] parts = args[3].Split(',');
            rows = new int[parts.Length];
            for (int i = 0; i < parts.Length; ++i)
                rows[i] = int.Parse(parts[i], inv);
        }

        // Defaults: sunset problem camera (unknown yaw/pitch is approximate).
        double cameraX = 476.0, cameraY = -248.0, cameraZ = 3925.0;
        double yawDeg = 0.0, pitchDeg = 45.0, fovYDeg = 57.0;
        double aspect = 16.0 / 9.0;
        double layerBottom = 650.0, layerTop = 2000.0, margin = 527.5;
        int steps = 128;
        double maxDistance = 14000.0;

        byte[] bytes = File.ReadAllBytes(bmp);
        int width = BitConverter.ToInt32(bytes, 18);
        int height = BitConverter.ToInt32(bytes, 22);
        int offBits = BitConverter.ToInt32(bytes, 10);
        int rowBytes = width * 4;

        double yaw = yawDeg * Math.PI / 180.0;
        double pitch = pitchDeg * Math.PI / 180.0;
        double cp = Math.Cos(pitch);
        double fx = Math.Sin(yaw) * cp;
        double fy = Math.Sin(pitch);
        double fz = Math.Cos(yaw) * cp;
        double rx = Math.Cos(yaw), ry = 0.0, rz = -Math.Sin(yaw);
        double ux = fy * rz - fz * ry;
        double uy = fz * rx - fx * rz;
        double uz = fx * ry - fy * rx;
        double tanHalfFov = Math.Tan(fovYDeg * Math.PI / 360.0);
        int probeX = Math.Min((int)(probeColumn * width), width - 1);
        double volumeBottom = layerBottom - margin;
        double volumeTop = layerTop + margin;

        var sb = new StringBuilder();
        sb.AppendLine(
            "row,step,t,x,y,z,density,height,profile,shaped," +
            "macroR,midG,weatherR,typeValue,coverage");

        foreach (int row in rows)
        {
            double uvx = (probeX + 0.5) / width;
            double uvy = (row + 0.5) / height;
            double ndcX = uvx * 2.0 - 1.0;
            double ndcY = 1.0 - uvy * 2.0;
            double dx = fx + rx * (ndcX * tanHalfFov * aspect) +
                        ux * (ndcY * tanHalfFov);
            double dy = fy + ry * (ndcX * tanHalfFov * aspect) +
                        uy * (ndcY * tanHalfFov);
            double dz = fz + rz * (ndcX * tanHalfFov * aspect) +
                        uz * (ndcY * tanHalfFov);
            double len = Math.Sqrt(dx * dx + dy * dy + dz * dz);
            dx /= len;
            dy /= len;
            dz /= len;

            double t0, t1;
            if (Math.Abs(dy) < 1e-5)
            {
                if (cameraY < volumeBottom || cameraY > volumeTop)
                    continue;
                t0 = 0.0;
                t1 = maxDistance;
            }
            else
            {
                double a = (volumeBottom - cameraY) / dy;
                double b = (volumeTop - cameraY) / dy;
                t0 = Math.Max(Math.Min(a, b), 0.0);
                t1 = Math.Min(Math.Max(a, b), maxDistance);
            }
            if (t1 <= t0)
                continue;

            double step = (t1 - t0) / steps;
            for (int i = 0; i < steps; ++i)
            {
                double t = t0 + (i + 0.5) * step;
                double x = cameraX + dx * t;
                double y = cameraY + dy * t;
                double z = cameraZ + dz * t;
                int o0 = offBits + (height - 1 - row) * rowBytes + i * 4;
                int o1 = o0 + 256 * 4;
                int o2 = o0 + 512 * 4;
                int o3 = o0 + 768 * 4;
                double density = SrgbToLinear(bytes[o0 + 2] / 255.0);
                double h = SrgbToLinear(bytes[o0 + 1] / 255.0);
                double profile = SrgbToLinear(bytes[o0] / 255.0);
                double macroR = SrgbToLinear(bytes[o1 + 2] / 255.0);
                double midG = SrgbToLinear(bytes[o1 + 1] / 255.0);
                double weatherR = SrgbToLinear(bytes[o1] / 255.0);
                double coverage = SrgbToLinear(bytes[o2 + 2] / 255.0);
                double shaped = SrgbToLinear(bytes[o3 + 2] / 255.0);
                double typeValue = SrgbToLinear(bytes[o3 + 1] / 255.0);
                sb.AppendFormat(
                    inv,
                    "{0},{1},{2:F3},{3:F2},{4:F2},{5:F2},{6:F5},{7:F5}," +
                    "{8:F5},{9:F5},{10:F5},{11:F5},{12:F5},{13:F5},{14:F5}\n",
                    row, i, t, x, y, z, density, h, profile, shaped,
                    macroR, midG, weatherR, typeValue, coverage);
            }
        }

        File.WriteAllText(csv, sb.ToString(), new UTF8Encoding(false));
        Console.WriteLine(
            "wrote " + csv + " (probe x=" + probeX + " of " + width +
            ", rows " + string.Join(",", rows) + ")");
        return 0;
    }
}
