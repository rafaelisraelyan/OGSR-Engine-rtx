#include "CloudProfiler.h"

#include "Renderer.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr UINT kTimestampQueryCount =
    CloudProfiler::kFrameCount * CloudProfiler::kQueriesPerFrame;
} // namespace

void CloudProfiler::Initialize(Renderer& renderer)
{
    renderer_ = &renderer;
    if (!renderer.Queue() || !renderer.Device())
        return;

    D3D12_QUERY_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = kTimestampQueryCount;
    heapDesc.NodeMask = 0;
    if (FAILED(renderer.Device()->CreateQueryHeap(
            &heapDesc, IID_PPV_ARGS(&queryHeap_))))
    {
        LogWarn("cloud profiler: timestamp query heap is unavailable");
        return;
    }

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = kFrameCount * kReadbackStride;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    if (FAILED(renderer.Device()->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&readback_))))
    {
        LogWarn("cloud profiler: timestamp readback buffer is unavailable");
        queryHeap_.Reset();
        return;
    }

    if (FAILED(renderer.Queue()->GetTimestampFrequency(&timestampFrequency_)) ||
        timestampFrequency_ == 0)
    {
        LogWarn("cloud profiler: the command queue has no timestamp frequency");
        readback_.Reset();
        queryHeap_.Reset();
        return;
    }

    enabled_ = true;
    Log("cloud profiler: %llu Hz timestamps, %u queries",
        static_cast<unsigned long long>(timestampFrequency_),
        kTimestampQueryCount);
}

void CloudProfiler::Shutdown()
{
    readback_.Reset();
    queryHeap_.Reset();
    enabled_ = false;
    renderer_ = nullptr;
    pending_.fill(false);
}

void CloudProfiler::BeginFrame(ID3D12GraphicsCommandList* list,
                               uint32_t frameIndex)
{
    if (!enabled_ || !list || frameIndex >= kFrameCount)
        return;
    list->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                   frameIndex * kQueriesPerFrame + 0);
}

void CloudProfiler::EndCloud(ID3D12GraphicsCommandList* list,
                             uint32_t frameIndex)
{
    if (!enabled_ || !list || frameIndex >= kFrameCount)
        return;
    list->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                   frameIndex * kQueriesPerFrame + 1);
}

void CloudProfiler::EndComposite(ID3D12GraphicsCommandList* list,
                                 uint32_t frameIndex)
{
    if (!enabled_ || !list || frameIndex >= kFrameCount)
        return;
    list->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                   frameIndex * kQueriesPerFrame + 2);
}

void CloudProfiler::Resolve(ID3D12GraphicsCommandList* list,
                            uint32_t frameIndex)
{
    if (!enabled_ || !list || frameIndex >= kFrameCount)
        return;
    list->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                           frameIndex * kQueriesPerFrame, kQueriesPerFrame,
                           readback_.Get(),
                           static_cast<UINT64>(frameIndex) * kReadbackStride);
    pending_[frameIndex] = true;
}

void CloudProfiler::Collect(uint32_t frameIndex)
{
    if (!enabled_ || !readback_ || frameIndex >= kFrameCount ||
        !pending_[frameIndex])
        return;

    const UINT64 offset = static_cast<UINT64>(frameIndex) * kReadbackStride;
    const D3D12_RANGE readRange{
        static_cast<SIZE_T>(offset),
        static_cast<SIZE_T>(offset + kQueriesPerFrame * sizeof(uint64_t))
    };
    void* mapped = nullptr;
    if (FAILED(readback_->Map(0, &readRange, &mapped)))
    {
        LogWarn("cloud profiler: failed to map timestamp readback");
        pending_[frameIndex] = false;
        return;
    }

    const auto* values = reinterpret_cast<const uint64_t*>(
        static_cast<const uint8_t*>(mapped) + offset);
    const uint64_t start = values[0];
    const uint64_t afterCloud = values[1];
    const uint64_t afterComposite = values[2];

    const D3D12_RANGE writtenRange{ 0, 0 };
    readback_->Unmap(0, &writtenRange);
    pending_[frameIndex] = false;

    if (start == 0 || afterCloud < start || afterComposite < afterCloud)
        return;

    const double frequency = static_cast<double>(timestampFrequency_);
    lastCloudMs_ = static_cast<double>(afterCloud - start) * 1000.0 / frequency;
    lastCompositeMs_ =
        static_cast<double>(afterComposite - afterCloud) * 1000.0 / frequency;
    lastTotalMs_ =
        static_cast<double>(afterComposite - start) * 1000.0 / frequency;

    sumCloudMs_ += lastCloudMs_;
    sumCompositeMs_ += lastCompositeMs_;
    sumTotalMs_ += lastTotalMs_;
    ++samples_;

    if (samples_ == 1 || samples_ % 30 == 0)
    {
        Log("gpu timestamps: cloud %.3f ms, composite %.3f ms, total %.3f ms",
            lastCloudMs_, lastCompositeMs_, lastTotalMs_);
    }
}

void CloudProfiler::CollectAll()
{
    for (uint32_t frame = 0; frame < kFrameCount; ++frame)
        Collect(frame);
}

void CloudProfiler::LogSummary() const
{
    if (samples_ == 0)
    {
        Log("gpu timestamps: no completed samples");
        return;
    }

    const double count = static_cast<double>(samples_);
    Log("gpu timestamps average over %llu samples: cloud %.3f ms, "
        "composite %.3f ms, total %.3f ms",
        static_cast<unsigned long long>(samples_),
        sumCloudMs_ / count, sumCompositeMs_ / count, sumTotalMs_ / count);
}
