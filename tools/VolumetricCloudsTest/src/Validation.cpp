#include "Validation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

namespace
{
constexpr double kSeamRatioLimit = 2.5;

size_t VoxelIndex(uint32_t x, uint32_t y, uint32_t z, uint32_t n)
{
    return ((static_cast<size_t>(z) * n + y) * n + x);
}
} // namespace

NoiseAnalysis AnalyzeNoise(const std::vector<float>& rgba, uint32_t resolution)
{
    NoiseAnalysis result{};
    result.resolution = resolution;

    const size_t voxels = static_cast<size_t>(resolution) * resolution * resolution;
    if (rgba.size() != voxels * 4)
        return result;

    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        double sum = 0.0;
        double sumSquared = 0.0;
        double minimum = std::numeric_limits<double>::max();
        double maximum = std::numeric_limits<double>::lowest();

        for (size_t i = 0; i < voxels; ++i)
        {
            const double value = rgba[i * 4 + channel];
            sum += value;
            sumSquared += value * value;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }

        ChannelStats& stats = result.channels[channel];
        stats.minimum = minimum;
        stats.maximum = maximum;
        stats.mean = sum / static_cast<double>(voxels);
        const double variance =
            std::max(0.0, sumSquared / static_cast<double>(voxels) - stats.mean * stats.mean);
        stats.stddev = std::sqrt(variance);
    }

    // Seam continuity: compare the two texels that become adjacent when the
    // sampler wraps (0 and N-1) against the average adjacent-texel difference
    // inside the volume. For a periodic field the ratio stays near 1; a real
    // seam defect produces a ratio far above 1.
    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            double interiorSum = 0.0;
            double seamSum = 0.0;
            uint64_t interiorCount = 0;
            uint64_t seamCount = 0;

            for (uint32_t z = 0; z < resolution; ++z)
            {
                for (uint32_t y = 0; y < resolution; ++y)
                {
                    for (uint32_t x = 0; x < resolution; ++x)
                    {
                        const size_t current = VoxelIndex(x, y, z, resolution) * 4 + channel;
                        if (axis == 0 && x + 1 < resolution)
                        {
                            const size_t next = VoxelIndex(x + 1, y, z, resolution) * 4 + channel;
                            interiorSum += std::fabs(rgba[next] - rgba[current]);
                            ++interiorCount;
                        }
                        if (axis == 1 && y + 1 < resolution)
                        {
                            const size_t next = VoxelIndex(x, y + 1, z, resolution) * 4 + channel;
                            interiorSum += std::fabs(rgba[next] - rgba[current]);
                            ++interiorCount;
                        }
                        if (axis == 2 && z + 1 < resolution)
                        {
                            const size_t next = VoxelIndex(x, y, z + 1, resolution) * 4 + channel;
                            interiorSum += std::fabs(rgba[next] - rgba[current]);
                            ++interiorCount;
                        }
                    }
                }
            }

            for (uint32_t v = 0; v < resolution; ++v)
            {
                for (uint32_t w = 0; w < resolution; ++w)
                {
                    size_t first = 0;
                    size_t last = 0;
                    if (axis == 0)
                    {
                        first = VoxelIndex(0, w, v, resolution) * 4 + channel;
                        last = VoxelIndex(resolution - 1, w, v, resolution) * 4 + channel;
                    }
                    else if (axis == 1)
                    {
                        first = VoxelIndex(w, 0, v, resolution) * 4 + channel;
                        last = VoxelIndex(w, resolution - 1, v, resolution) * 4 + channel;
                    }
                    else
                    {
                        first = VoxelIndex(w, v, 0, resolution) * 4 + channel;
                        last = VoxelIndex(w, v, resolution - 1, resolution) * 4 + channel;
                    }
                    seamSum += std::fabs(rgba[last] - rgba[first]);
                    ++seamCount;
                }
            }

            SeamAxisStats& seam = result.seams[channel][axis];
            seam.interiorMeanAbsDiff = interiorSum / static_cast<double>(interiorCount);
            seam.seamMeanAbsDiff = seamSum / static_cast<double>(seamCount);
            seam.ratio = seam.seamMeanAbsDiff /
                         std::max(seam.interiorMeanAbsDiff, 1e-9);
            seam.pass = seam.ratio < kSeamRatioLimit;
        }
    }

    result.valuesInRange = true;
    result.nonConstant = true;
    result.periodic = true;
    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        const ChannelStats& stats = result.channels[channel];
        if (stats.minimum < -1e-4 || stats.maximum > 1.0001)
            result.valuesInRange = false;
        if (stats.maximum - stats.minimum < 0.05)
            result.nonConstant = false;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!result.seams[channel][axis].pass)
                result.periodic = false;
        }
    }

    return result;
}

double MaxAbsDifference(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size() || a.empty())
        return std::numeric_limits<double>::infinity();

    double maximum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        maximum = std::max(maximum, std::fabs(static_cast<double>(a[i] - b[i])));
    return maximum;
}

double MeanAbsDifferenceChannel(
    const std::vector<float>& a,
    const std::vector<float>& b,
    uint32_t channel)
{
    if (a.size() != b.size() || a.empty() || channel > 3)
        return std::numeric_limits<double>::infinity();

    double sum = 0.0;
    size_t count = 0;
    for (size_t i = channel; i < a.size(); i += 4)
    {
        sum += std::fabs(static_cast<double>(a[i] - b[i]));
        ++count;
    }
    return count ? sum / static_cast<double>(count) : 0.0;
}

const char* ChannelLabel(uint32_t channel)
{
    switch (channel)
    {
    case 0: return "Perlin";
    case 1: return "Worley";
    default: return "Combined";
    }
}

std::string FormatAnalysis(const NoiseAnalysis& analysis)
{
    std::ostringstream out;
    out << "resolution: " << analysis.resolution << "^3\n";
    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        const ChannelStats& stats = analysis.channels[channel];
        out << ChannelLabel(channel)
            << ": min=" << stats.minimum
            << " max=" << stats.maximum
            << " mean=" << stats.mean
            << " stddev=" << stats.stddev << "\n";
    }

    const char* axisNames[3] = { "X", "Y", "Z" };
    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        out << ChannelLabel(channel) << " seam ratios:";
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const SeamAxisStats& seam = analysis.seams[channel][axis];
            char buffer[160]{};
            snprintf(buffer, sizeof(buffer),
                     " %s seam=%.6f interior=%.6f ratio=%.3f%s",
                     axisNames[axis], seam.seamMeanAbsDiff,
                     seam.interiorMeanAbsDiff, seam.ratio,
                     seam.pass ? "" : " FAIL");
            out << buffer;
        }
        out << "\n";
    }
    return out.str();
}
