#define OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
#include "../../../xr_3da/GameDx12DetailShadowDraws.h"
#include <array>
#include <cstdlib>
#include <iostream>
using namespace gameaux12;
static void Check(bool value, const char* text)
{
    if (!value) { std::cerr << "FAIL " << text << '\n'; std::exit(1); }
}
int main()
{
    std::array<DetailShadowPlacement,3> placements{};
    for (unsigned i=0; i<3; ++i) { placements[i].slot=1; placements[i].ordinal=i; placements[i].model=i; }
    const std::array<DetailShadowBound,3> bounds{{
        {{1,0},{1,0,0},.1f}, {{1,1},{2,0,0},.1f}, {{1,2},{-2,0,0},.1f}}};
    const std::array<DetailShadowPlane,1> planes{{{{{1,0,0}},0}}};
    std::array<uint32_t,3> meshes{7,9,kInvalidMesh};
    std::vector<DirectX::XMFLOAT4X4> transforms(1);
    std::vector<DetailShadowDraw> draws{{123,0,1}};
    DetailShadowDrawReport r;
    const auto invoke=[&]{return AppendDetailShadowDraws(placements,bounds,planes,meshes,4096,transforms,draws,&r);};
    Check(invoke() && r.status==DetailShadowDrawStatus::Ready && r.selected==2 && r.prepared==2 &&
        r.missingMesh==0 && draws.size()==3 && transforms.size()==3,"valid, outside missing ignored");
    transforms.resize(1); draws.resize(1);
    meshes[1]=kInvalidMesh; // mutation: selected valid GPU mesh disappears
    Check(!invoke() && r.status==DetailShadowDrawStatus::MissingResources && r.selected==2 &&
        r.prepared==0 && r.missingMesh==1 && r.unsupportedMaterial==0 &&
        transforms.size()==1 && draws.size()==1,"selected missing GPU mesh must fail atomically");
    meshes[1]=kUnsupportedDetailShadowMesh;
    Check(!invoke() && r.status==DetailShadowDrawStatus::MissingResources && r.selected==2 &&
        r.missingMesh==0 && r.unsupportedMaterial==1 && transforms.size()==1 && draws.size()==1,
        "unsupported material is distinct");
    placements[1].model=99;
    Check(!invoke() && r.missingMesh==1 && transforms.size()==1 && draws.size()==1,"model outside map");
    placements[1].model=1; meshes[1]=9;
    Check(!AppendDetailShadowDraws(placements,bounds,planes,meshes,sizeof(DirectX::XMFLOAT4X4),transforms,draws,&r) &&
        r.status==DetailShadowDrawStatus::UploadBudget && transforms.size()==1 && draws.size()==1,"budget atomic");
    const std::array<DetailShadowPlane,1> exclude{{{{{1,0,0}},-10}}};
    Check(AppendDetailShadowDraws(placements,bounds,exclude,meshes,4096,transforms,draws,&r) &&
        r.status==DetailShadowDrawStatus::Ready && r.selected==0 && r.prepared==0 &&
        transforms.size()==1 && draws.size()==1,"empty coverage is success");
    Check(!AppendDetailShadowDraws(placements,std::span(bounds).first(1),planes,meshes,4096,transforms,draws,&r) &&
        r.status==DetailShadowDrawStatus::InvalidInput && transforms.size()==1 && draws.size()==1,"invalid input");
    std::cout << "PASS: CR-Q04 selected mesh mutation, coverage reasons and atomicity\n";
}
