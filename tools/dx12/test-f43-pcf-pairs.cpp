#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>

// Independent point-comparison grid vs paired bilinear comparison model.
int main()
{
    std::mt19937 rng(431);
    unsigned checks = 0;
    for (int trial = 0; trial < 250; ++trial)
    {
        std::array<float, 64> depth{};
        for (auto& d : depth) d = float(rng() % 17) / 16;
        for (float z : {0.0f, .25f, .5f, .75f, 1.0f})
        {
            const auto point = [&](int x, int y) -> double {
                return z <= (x < 0 || x >= 8 || y < 0 || y >= 8 ? 1.0f : depth[y*8+x]) ? 1 : 0;
            };
            const auto linear = [&](double x, double y) {
                const int i = int(std::floor(x)), j = int(std::floor(y));
                const double u = x-i, v = y-j;
                return (1-v)*((1-u)*point(i,j)+u*point(i+1,j)) +
                    v*((1-u)*point(i,j+1)+u*point(i+1,j+1));
            };
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    for (int r : {1,2,3})
                    {
                        double reference = 0, paired = 0;
                        int calls = 0;
                        for (int j = -r; j <= r; ++j)
                            for (int i = -r; i <= r; ++i) reference += point(x+i,y+j);
                        for (int j = -r; j <= r; j += 2)
                            for (int i = -r; i <= r; i += 2)
                            {
                                const int wx = i < r ? 2 : 1, wy = j < r ? 2 : 1;
                                paired += wx*wy*linear(x+i+(wx-1)*.5, y+j+(wy-1)*.5);
                                ++calls;
                            }
                        if (std::abs(reference-paired)>1e-12 || calls!=(r+1)*(r+1)) return EXIT_FAILURE;
                        ++checks;
                    }
        }
    }
    std::printf("PASS: %u box PCF equivalence checks (3/5/7, borders, random depth, compare equality)\n", checks);
}
