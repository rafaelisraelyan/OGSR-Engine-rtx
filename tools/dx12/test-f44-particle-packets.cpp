#include "GameDx12ParticlePackets.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <thread>

using namespace gameparticle12;
static uint64_t checks = 0;
static void Check(bool ok)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL check %llu\n", static_cast<unsigned long long>(checks)); std::exit(1); }
}
static Quad Sprite(float x)
{
    return {{{x,0,1,0x11223344,0,1},{x,1,1,0x11223344,0,0},
        {x+1,0,1,0x11223344,1,1},{x+1,1,1,0x11223344,1,0}}};
}
static Snapshot Make(uint32_t n, uint32_t groups)
{
    Snapshot s;
    s.session=7; s.epoch=3; s.frame=42;
    for (uint32_t i=0; i<n; ++i) s.quads.push_back(Sprite(static_cast<float>(i)));
    for (uint32_t e=0; e<groups; ++e)
    {
        const uint32_t first=n*e/groups, last=n*(e+1)/groups;
        s.emitters.push_back({"effect", "particle", "fx\\texture", e, Cull::None, first, last-first});
    }
    return s;
}
static void Verify(const Snapshot& s, unsigned workers)
{
    std::vector<uint8_t> serial(s.quads.size()), parallel(s.quads.size(), 255);
    ClassifyRange(s.quads, serial, 0, static_cast<uint32_t>(serial.size()));
    std::vector<std::jthread> pool;
    for (unsigned w=0; w<workers; ++w)
        pool.emplace_back([&,w] {
            ClassifyRange(s.quads, parallel, static_cast<uint32_t>(w*parallel.size()/workers),
                static_cast<uint32_t>((w+1)*parallel.size()/workers));
        });
    pool.clear();
    Check(serial == parallel);
    Prepared a,b;
    Check(BuildPackets(s,7,3,serial,a));
    Check(BuildPackets(s,7,3,parallel,b));
    Check(a==b);
    uint32_t cursor=0, vertex=0, packet=0, rejected=0;
    // Independent sequential walk over source emitter spans and original R4 triangles.
    for (uint32_t e=0; e<s.emitters.size(); ++e)
    {
        const uint32_t start=cursor;
        for (uint32_t q=s.emitters[e].first; q<s.emitters[e].first+s.emitters[e].count; ++q)
        {
            if (!serial[q]) { ++rejected; continue; }
            Check(vertex+4<=a.vertices.size() && cursor+6<=a.indices.size());
            for (uint32_t k=0;k<4;++k) Check(a.vertices[vertex+k]==s.quads[q][k]);
            const uint32_t expected[]{vertex,vertex+1,vertex+2,vertex+3,vertex+2,vertex+1};
            for (uint32_t k=0;k<6;++k) Check(a.indices[cursor+k]==expected[k]);
            vertex+=4; cursor+=6;
        }
        if (cursor!=start)
        {
            Check(packet<a.packets.size());
            Check(a.packets[packet++]==Packet{e,start,cursor-start});
        }
    }
    Check(a.invalidQuads==rejected && a.vertices.size()==vertex && a.indices.size()==cursor && a.packets.size()==packet);
    Check(a.session==7 && a.epoch==3 && a.frame==42);
}
int main()
{
    Verify(Make(0,0),4);
    Verify(Make(1,1),2);
    Verify(Make(MaxQuads,MaxEmitters),8);
    std::mt19937 rng(44);
    for (unsigned i=0;i<200;++i)
    {
        const uint32_t n=1+rng()%512;
        auto s=Make(n,1+rng()%n);
        for (auto& q:s.quads) if (rng()%5==0) q[2].u=std::numeric_limits<float>::quiet_NaN();
        Verify(s,1+rng()%8);
    }
    auto s=Make(4,2);
    std::vector<uint8_t> valid(4,1);
    Prepared p;
    Check(BuildPackets(s,7,3,valid,p));
    Check(!BuildPackets(s,8,3,valid,p) && p.vertices.empty());
    Check(!BuildPackets(s,7,4,valid,p));
    Check(!BuildPackets(s,7,3,std::span(valid).first(3),p));
    valid[3]=2; Check(!BuildPackets(s,7,3,valid,p) && p.packets.empty()); valid[3]=1;
    s.emitters[1].first=1; Check(!BuildPackets(s,7,3,valid,p));
    s.emitters[1].first=2; s.emitters[1].count=3; Check(!BuildPackets(s,7,3,valid,p));
    s.emitters[1].count=2; s.emitters[0].texture.clear(); Check(!BuildPackets(s,7,3,valid,p));
    s=Make(4,2); s.emitters[0].shader=std::string(MaxName+1,'x'); Check(!BuildPackets(s,7,3,valid,p));
    s=Make(4,2); s.emitters[0].cull=static_cast<Cull>(255); Check(!BuildPackets(s,7,3,valid,p));
    s=Make(4,2); s.quads[0][0].x=std::numeric_limits<float>::infinity();
    Check(!ValidQuad(s.quads[0]));
    s=Make(MaxQuads+1,1); Check(!ValidLayout(s));
    s=Make(MaxEmitters+1,MaxEmitters+1); Check(!ValidLayout(s));
    std::printf("PASS: %llu assertions; 200 randomized snapshots, bounded capacity and threaded parity\n",static_cast<unsigned long long>(checks));
}
