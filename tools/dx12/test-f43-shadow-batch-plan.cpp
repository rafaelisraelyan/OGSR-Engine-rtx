#include "../../ogsr_engine/xr_3da/GameDx12ShadowBatchPlan.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

using namespace game_shadow12;

static void Check(const std::vector<uint32_t>& weights, uint32_t workers)
{
    std::vector<ShadowBatchRange> batches;
    assert(PlanShadowBatches(weights, workers, batches));
    assert(batches.size() == (std::min)(weights.size(), static_cast<size_t>(workers)));
    uint32_t next = 0;
    for (const auto& batch : batches)
    {
        assert(batch.first == next && batch.first < batch.last && batch.last <= weights.size());
        next = batch.last;
    }
    assert(next == weights.size());
    std::vector<ShadowBatchRange> repeat;
    assert(PlanShadowBatches(weights, workers, repeat));
    assert(repeat.size() == batches.size());
    for (size_t i = 0; i < batches.size(); ++i)
        assert(repeat[i].first == batches[i].first && repeat[i].last == batches[i].last);
}

int main()
{
    std::vector<ShadowBatchRange> unchanged{{7, 8}};
    assert(!PlanShadowBatches({}, 0, unchanged));
    assert(unchanged.size() == 1 && unchanged[0].first == 7 && unchanged[0].last == 8);
    Check({}, 2);
    Check({0, 0, 0, 0}, 2);
    Check({1, 1, 1, 1, 1}, 9);
    Check({1, 1, 50, 1, 1}, 2);
    Check({UINT32_MAX, UINT32_MAX, 0, UINT32_MAX}, 3);
    std::vector<ShadowBatchRange> batches;
    assert(PlanShadowBatches(std::vector<uint32_t>{1, 1, 1, 1, 1, 1, 1, 1}, 2, batches));
    assert(batches[0].last == 4);
    for (uint32_t length = 0; length <= 8; ++length)
    {
        uint32_t cases = 1;
        for (uint32_t i = 0; i < length; ++i) cases *= 3;
        for (uint32_t value = 0; value < cases; ++value)
        {
            auto n = value;
            std::vector<uint32_t> weights(length);
            for (auto& weight : weights) { weight = n % 3; n /= 3; }
            for (uint32_t workers = 1; workers <= 9; ++workers) Check(weights, workers);
        }
    }
}
