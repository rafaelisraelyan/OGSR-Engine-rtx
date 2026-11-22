using System;
using System.Drawing;
using System.Globalization;

// ROI silhouette metrics for cloud defect review.
// mode=dark counts pixels darker than the threshold (cloud/shadow against sky);
// mode=bright counts pixels brighter than the threshold (density/gap analysis).
// The threshold argument is in 0..255 units and is divided by 255 internally.
static class AnalyzeRoi
{
    static int Main(string[] args)
    {
        if (args.Length < 6)
        {
            Console.Error.WriteLine(
                "usage: AnalyzeRoi <image> <x0> <y0> <x1> <y1> <threshold0..255> [dark|bright] [label]");
            return 2;
        }
        var inv = CultureInfo.InvariantCulture;
        using (var image = new Bitmap(args[0]))
        {
            int x0 = Math.Max(0, int.Parse(args[1], inv));
            int y0 = Math.Max(0, int.Parse(args[2], inv));
            int x1 = Math.Min(image.Width, int.Parse(args[3], inv));
            int y1 = Math.Min(image.Height, int.Parse(args[4], inv));
            double threshold = double.Parse(args[5], inv) / 255.0;
            bool dark = args.Length < 7 || args[6] != "bright";
            string label = args.Length > 7 ? args[7] : args[0];

            int width = x1 - x0;
            int height = y1 - y0;
            if (width <= 0 || height <= 0)
            {
                Console.Error.WriteLine("empty ROI");
                return 2;
            }

            var runs = new int[width];
            var starts = new int[width];
            var ends = new int[width];
            double lumaSum = 0.0;
            int occupied = 0;
            for (int x = 0; x < width; ++x)
            {
                int best = 0, bestStart = 0, bestEnd = 0;
                int current = 0, currentStart = 0;
                for (int y = 0; y < height; ++y)
                {
                    Color c = image.GetPixel(x0 + x, y0 + y);
                    double luma = (0.2126 * c.R + 0.7152 * c.G + 0.0722 * c.B) / 255.0;
                    lumaSum += luma;
                    bool inside = dark ? luma < threshold : luma > threshold;
                    if (inside)
                    {
                        ++occupied;
                        if (current == 0) currentStart = y;
                        ++current;
                        if (current > best)
                        {
                            best = current;
                            bestStart = currentStart;
                            bestEnd = y;
                        }
                    }
                    else
                    {
                        current = 0;
                    }
                }
                runs[x] = best;
                starts[x] = bestStart;
                ends[x] = bestEnd;
            }

            var originalRuns = (int[])runs.Clone();
            Array.Sort(runs);
            int median = runs[runs.Length / 2];
            int p95 = runs[Math.Min(runs.Length - 1, (int)(runs.Length * 0.95))];
            int max = runs[runs.Length - 1];
            int long25 = 0, long50 = 0;
            foreach (int r in runs)
            {
                if (r > height * 0.25) ++long25;
                if (r > height * 0.50) ++long50;
            }
            int widest = 0;
            int widestIndex = 0;
            for (int x = 0; x < width; ++x)
            {
                if (originalRuns[x] > widest)
                {
                    widest = originalRuns[x];
                    widestIndex = x;
                }
            }
            int refStart = starts[widestIndex];
            int refEnd = ends[widestIndex];
            int aligned = 0;
            for (int x = 0; x < width; ++x)
            {
                if (originalRuns[x] >= widest * 0.6 &&
                    Math.Abs(starts[x] - refStart) <= 4 &&
                    Math.Abs(ends[x] - refEnd) <= 4)
                    ++aligned;
            }

            Console.WriteLine(string.Format(
                inv,
                "{0}: roi=({1},{2})-({3},{4}) mode={5} threshold={6:F3} " +
                "meanLuma={7:F3} occupancy={8:F3} runMean={9:F1} runMedian={10} " +
                "runP95={11} runMax={12} long25={13} long50={14} straight={15:F3}",
                label, x0, y0, x1, y1, dark ? "dark" : "bright", threshold,
                lumaSum / (width * height), (double)occupied / (width * height),
                Average(originalRuns), median, p95, max, long25, long50,
                (double)aligned / width));
        }
        return 0;
    }

    static double Average(int[] values)
    {
        long sum = 0;
        foreach (int v in values) sum += v;
        return (double)sum / values.Length;
    }
}
