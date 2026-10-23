#pragma once

#include "Common.h"

#include <array>
#include <cstdint>

class Renderer;

// Owns timestamp queries for the cloud pass. One independent query range is
// kept per back buffer, so a query is never reused before its fence completed.
class CloudProfiler
{
public:
    static constexpr UINT kFrameCount = 3;
    static constexpr UINT kQueriesPerFrame = 3;
    static constexpr UINT kReadbackStride = 256;

    void Initialize(Renderer& renderer);
    void Shutdown();

    bool Enabled() const { return enabled_; }

    void BeginFrame(ID3D12GraphicsCommandList* list, uint32_t frameIndex);
    void EndCloud(ID3D12GraphicsCommandList* list, uint32_t frameIndex);
    void EndComposite(ID3D12GraphicsCommandList* list, uint32_t frameIndex);
    void Resolve(ID3D12GraphicsCommandList* list, uint32_t frameIndex);

    // Call only after the fence for frameIndex has completed.
    void Collect(uint32_t frameIndex);
    void CollectAll();
    void LogSummary() const;

    double LastCloudMs() const { return lastCloudMs_; }
    double LastCompositeMs() const { return lastCompositeMs_; }
    double LastTotalMs() const { return lastTotalMs_; }

private:
    Renderer* renderer_ = nullptr;
    ComPtr<ID3D12QueryHeap> queryHeap_;
    ComPtr<ID3D12Resource> readback_;
    uint64_t timestampFrequency_ = 0;
    bool enabled_ = false;
    std::array<bool, kFrameCount> pending_{};

    double lastCloudMs_ = 0.0;
    double lastCompositeMs_ = 0.0;
    double lastTotalMs_ = 0.0;
    double sumCloudMs_ = 0.0;
    double sumCompositeMs_ = 0.0;
    double sumTotalMs_ = 0.0;
    uint64_t samples_ = 0;
};
