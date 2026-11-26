#include "GameDx12UiCommands.h"
#include <cassert>
#include <iostream>
using namespace gameui12;
int main()
{
    Frame f; f.deviceEpoch = 1; f.serial = 2; f.width = 2560; f.height = 1080;
    State s; s.material = {4, 9};
    std::array<Vertex, 7> v{};
    assert(Append(f, s, Primitive::TriStrip, std::span(v).first(2)) == AppendResult::Empty);
    assert(Append(f, s, Primitive::LineStrip, std::span(v).first(1)) == AppendResult::Empty);
    assert(Append(f, s, Primitive::TriList, v) == AppendResult::Added);
    assert(f.draws[0].vertexCount == 6);
    s.scissorEnabled = true; s.scissor = {-20, -10, 200, 100};
    assert(ClipRect(s, f.width, f.height).left == 0);
    assert(Append(f, s, Primitive::LineList, v) == AppendResult::Added);
    assert(f.draws[1].firstVertex == 6 && f.draws[1].state.scissor.left == -20);
    s.scissor = {400, 0, 300, 100};
    assert(Append(f, s, Primitive::TriStrip, v) == AppendResult::Empty);
    s.scissorEnabled = false; s.domain = Domain::World; s.depthTest = true;
    s.transform[12] = 5;
    assert(Append(f, s, Primitive::TriStrip, v) == AppendResult::Added);
    assert(f.draws[2].state.transform[12] == 5 && f.draws[2].state.depthTest);
    s.transform[12] = 0; s.domain = Domain::Screen; s.depthTest = false;
    v[0].x = std::numeric_limits<float>::infinity();
    assert(Append(f, s, Primitive::TriList, v) == AppendResult::Invalid);
    v[0].x = 0;
    for (uint32_t i = 0; i < 100000; ++i)
    {
        s.material = {uint64_t(i) + 1, 9};
        assert(Append(f, s, Primitive::TriList, v) == AppendResult::Added);
    }
    assert(f.draws.size() == 100003 && f.vertices.size() == 600019);
    assert(Validate(f, 1, 2) && !Validate(f, 2, 2));
    assert(f.draws.back().state.material.id == 100000);
    ++f.draws.back().firstVertex; assert(!Validate(f, 1, 2)); --f.draws.back().firstVertex;
    f.draws.back().primitive = static_cast<Primitive>(99); assert(!Validate(f, 1, 2));
    std::cout << "PASS: 100003 ordered draws, 600019 vertices, state, clipping, identity and malformed inputs\n";
}
