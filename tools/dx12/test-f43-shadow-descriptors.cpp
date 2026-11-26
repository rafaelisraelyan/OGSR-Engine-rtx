#include "GameDx12ShadowDescriptorRange.h"

#include <cassert>
#include <cstdio>
#include <limits>

int main()
{
    using namespace game_shadow12;
    for (uint32_t prefix = 0; prefix <= 8; ++prefix)
        for (uint32_t capacity = prefix; capacity <= 16; ++capacity)
            for (uint32_t first = 1; first <= capacity + 1; ++first)
                for (uint32_t second = 1; second <= capacity + 1; ++second)
                {
                    ShadowDescriptorRange low{99, 99}, high{99, 99};
                    const bool fits = first + second <= capacity - prefix;
                    assert(PlaceShadowDescriptors(prefix, capacity, 0, first, second, low) == fits);
                    assert(PlaceShadowDescriptors(prefix, capacity, 1, second, first, high) == fits);
                    if (fits)
                    {
                        assert(low.first == prefix && low.count == first);
                        assert(high.first + high.count == capacity && high.count == second);
                        assert(low.first + low.count <= high.first);
                    }
                    else
                        assert(low.first == 99 && high.first == 99);
                }
    ShadowDescriptorRange range;
    constexpr auto limit = (std::numeric_limits<uint32_t>::max)();
    assert(PlaceShadowDescriptors(2, limit, 1, limit - 2, 0, range) && range.first == 2);
    assert(!PlaceShadowDescriptors(2, limit, 0, limit - 2, 1, range));
    assert(!PlaceShadowDescriptors(2, 1, 0, 1, 0, range));
    assert(!PlaceShadowDescriptors(2, limit, 2, 1, 0, range));
    assert(!PlaceShadowDescriptors(2, limit, 1, 0, 0, range));
    std::puts("PASS: two-slot growth, physical capacity, disjoint placement, denial and checked arithmetic");
}
