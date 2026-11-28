#include "../GameDx12DetailShadowStreaming.h"

#include <cassert>
#include <numeric>
#include <unordered_map>
#include <vector>

int main()
{
    std::unordered_map<uint32_t, size_t> cache;
    size_t bytes = 0, calls = 0;
    const auto cost = [](size_t value) { return value; };
    const auto decode = [&](uint32_t) { ++calls; return size_t{1}; };
    auto update = [&](const std::vector<uint32_t>& ids, size_t budget, size_t limit)
    {
        return gameaux12::UpdateDetailShadowCache(ids, budget, limit, 1, cache, bytes, cost, decode);
    };
    // Объединение двух пересекающихся объёмов, включая слоты вне камеры.
    std::vector<uint32_t> regions(400);
    std::iota(regions.begin(), regions.end(), 0);
    assert(update(regions, 1000, 256) == 144 && calls == 256 && bytes == 256);
    assert(update(regions, 1000, 256) == 0 && calls == 400 && bytes == 400);
    assert(update(regions, 1000, 256) == 0 && calls == 400);

    // Новый объём удаляет устаревшие слоты и постепенно достраивается.
    std::vector<uint32_t> moved(400);
    std::iota(moved.begin(), moved.end(), 300);
    assert(update(moved, 1000, 256) == 44 && bytes == 356);
    assert(cache.contains(300) && !cache.contains(299));
    assert(update(moved, 1000, 256) == 0 && bytes == 400);
    // Уменьшение памяти не допускает сверхбюджетный готовый кэш.
    assert(update(moved, 10, 256) == 390 && bytes == 10);
    assert(update(moved, 1000, 256) == 134);

    // Неуспешная вставка тоже расходует лимит декодирований.
    cache.clear(); bytes = calls = 0;
    const auto oversized = [&](uint32_t) { ++calls; return size_t{2}; };
    assert(gameaux12::UpdateDetailShadowCache(regions, 1, 3, 1, cache, bytes, cost, oversized) == 400);
    assert(calls == 3 && cache.empty() && bytes == 0);
}
