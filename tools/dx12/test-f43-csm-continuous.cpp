#include "../../ogsr_engine/xr_3da/GameDx12ShadowFilter.h"
#include "test-f43-csm-sampling-reference.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static double depth(int x, int y) { return ((x * 17 + y * 13 + 10000) % 7) / 7.0; }
static double direct(double u, double v, double z, double gx, double gy, int r)
{
    double sum = 0;
    for (int y = -r; y <= r; ++y) for (int x = -r; x <= r; ++x)
    {
        const double px = u + x - .5, py = v + y - .5;
        const int bx = (int)std::floor(px), by = (int)std::floor(py);
        const double fx = px - bx, fy = py - by;
        for (int j = 0; j < 2; ++j) for (int i = 0; i < 2; ++i)
            sum += (i ? fx : 1-fx) * (j ? fy : 1-fy) *
                (z + gx * (bx+i+.5-u) + gy * (by+j+.5-v) <= depth(bx+i,by+j));
    }
    return sum / ((2*r+1)*(2*r+1));
}
static double collapsed(double u, double v, double z, double gx, double gy, int r)
{
    const int bx = (int)std::floor(u-.5), by = (int)std::floor(v-.5);
    const double fx = u-.5-bx, fy = v-.5-by;
    double sum=0;
    for (int y=-r; y<=r+1; ++y) for (int x=-r; x<=r+1; ++x)
    {
        const double wx = x == -r ? 1-fx : x == r+1 ? fx : 1;
        const double wy = y == -r ? 1-fy : y == r+1 ? fy : 1;
        sum += wx*wy*(z+gx*(bx+x+.5-u)+gy*(by+y+.5-v)<=depth(bx+x,by+y));
    }
    return sum / ((2*r+1)*(2*r+1));
}
static double localCollapsed(double u,double v,double z,double stored,double gx,double gy,int r)
{
    const double px=u*8-.5, py=v*8-.5;
    const int bx=(int)std::floor(px), by=(int)std::floor(py);
    const double fx=px-bx, fy=py-by;
    double sum=0;
    for(int y=-r;y<=r+1;++y) for(int x=-r;x<=r+1;++x)
    {
        const double wx=x == -r ? 1-fx : x == r+1 ? fx : 1;
        const double wy=y == -r ? 1-fy : y == r+1 ? fy : 1;
        const int tx=(std::clamp)(bx+x,0,7), ty=(std::clamp)(by+y,0,7);
        sum+=wx*wy*(z+gx*((tx+.5)/8-u)+gy*((ty+.5)/8-v)<=stored);
    }
    return sum/((2*r+1)*(2*r+1));
}
int main()
{
    unsigned checks=0;
    for (int r=1;r<=3;++r) for (int x=0;x<33;++x) for (int y=0;y<33;++y)
        for (double gx : {0., .031, -.047}) for (double gy : {0., .023})
        {
            const double u=x/16., v=y/16.;
            if (std::abs(direct(u,v,.45,gx,gy,r)-collapsed(u,v,.45,gx,gy,r))>1e-12) return 1;
            ++checks;
        }
    for (int r=1;r<=3;++r) for(int x=0;x<=32;++x) for(int y=0;y<=32;++y)
        for (double z : {.45, .5, .55}) for (double gx : {0., .125, -.125})
        {
            const double u=x/32., v=y/32., gy=.0625;
            if (std::abs(csm_reference::LocalContinuous(u,v,z,.5,r,gx,gy)-
                localCollapsed(u,v,z,.5,gx,gy,r))>1e-12) return 5;
            ++checks;
        }
    for (unsigned n : {128u,256u,512u,1024u}) for (float f : {0.2f,1.5707963f,2.8f})
    {
        float guarded=0;
        if (!game_shadow12::GuardedLocalFov(f,n,guarded) ||
            std::abs((1-std::tan(f/2)/std::tan(guarded/2))*n/2-4)>0.001) return 2;
                // Номинальный край и все четыре угла point/spot остаются внутри
        // карты с полной опорой 7x7 даже на минимальном разрешении.
        const double edge = .5 * (1 - std::tan(f/2) / std::tan(guarded/2));
        for (int sx : {-1, 1}) for (int sy : {-1, 1})
        {
            const double u = sx < 0 ? edge : 1-edge;
            const double v = sy < 0 ? edge : 1-edge;
            if ((std::min)(u,1-u)*n < 3.999 || (std::min)(v,1-v)*n < 3.999) return 4;
            ++checks;
        }
        ++checks;
    }
    float out=0;
    if (game_shadow12::GuardedLocalFov(0,128,out) || game_shadow12::GuardedLocalFov(1,8,out)) return 3;
    std::printf("PASS: %u weighted-kernel and projection checks\n",checks);
}
