#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ChannelStats
{
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double stddev = 0.0;
};

struct SeamAxisStats
{
    double seamMeanAbsDiff = 0.0;
    double interiorMeanAbsDiff = 0.0;
    double ratio = 0.0;
    bool pass = false;
};

struct NoiseAnalysis
{
    uint32_t resolution = 0;
    ChannelStats channels[3]{};
    SeamAxisStats seams[3][3]{}; // [channel][axis: 0=X, 1=Y, 2=Z]
    bool valuesInRange = false;
    bool nonConstant = false;
    bool periodic = false;
};

// The readback is tightly packed RGBA32F, N^3 voxels, top-down Z/Y/X.
NoiseAnalysis AnalyzeNoise(const std::vector<float>& rgba, uint32_t resolution);

double MaxAbsDifference(const std::vector<float>& a, const std::vector<float>& b);
double MeanAbsDifferenceChannel(
    const std::vector<float>& a,
    const std::vector<float>& b,
    uint32_t channel);

const char* ChannelLabel(uint32_t channel);
std::string FormatAnalysis(const NoiseAnalysis& analysis);
