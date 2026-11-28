#include "../../../xr_3da/GameDx12DetailGeometryPolicy.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace gameaux12;
static void Check(bool ok, const char* text) { if (!ok) { std::cerr << "FAIL " << text << '\n'; std::exit(1); } }
int main()
{
    const auto state=[](float distance, bool noFade, float threshold=0.f) {
        return EvaluateDetailGeometry(2.f,distance,4.f,100.f,1.f,threshold,noFade);
    };
    const auto color=state(25.f,false), shadow=state(25.f,false);
    Check(color.Eligible() && shadow.Eligible() && std::abs(color.scale-150.f/99.f)<1e-6f &&
        color.scale==shadow.scale,"shared fade scale");
    Check(state(25.f,true).Eligible() && state(25.f,true).scale==2.f,"no-scale fade");
    Check(state(3.f,false).cull==DetailGeometryCull::Near,"near exclusion");
    Check(state(100.f,false).cull==DetailGeometryCull::Distance &&
        state(100.f,true).cull==DetailGeometryCull::Distance,"far exclusion both settings");
    Check(state(25.f,false,1.f).cull==DetailGeometryCull::Lod,"shared LOD");
    // Камерный frustum не аргумент policy: внекамерный caster может
    // остаться в теневом membership и иметь ту же геометрию.
    Check(state(81.f,false).Eligible() && state(81.f,false).scale>0.f,"off-camera candidate");
    Check(EvaluateDetailGeometry(1.f,std::numeric_limits<float>::quiet_NaN(),0.f,100.f,1.f,0.f,false).cull==
        DetailGeometryCull::Invalid,"nonfinite");
    Check(EvaluateDetailGeometry(1.f,1.f,0.f,1.f,1.f,0.f,false).cull==DetailGeometryCull::Invalid,"degenerate fade");
    std::cout << "PASS: CR-Q03 shared geometry scale, fade and LOD\n";
}
