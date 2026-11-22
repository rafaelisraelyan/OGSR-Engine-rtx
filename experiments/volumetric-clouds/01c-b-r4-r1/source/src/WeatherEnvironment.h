#pragma once

// V-WEATHER-01C-A shipping-controls contract adapter.
//
// The real game reads weather from CEnvironment/scripted game logic. This
// standalone harness has no game runtime, so the shipping-controls build uses
// one fixed environment snapshot. Nothing in this header is a manual weather
// control: there is no parser, no console command and no hotkey for it.

#include "CloudTypes.h"

namespace clouds
{
struct EnvironmentWeatherSnapshot
{
    StaticWeatherMode mode = StaticWeatherMode::Overcast;
    uint32_t seed = 7331;
    float timeSeconds = 0.0f;
};

// Contract-only source. When the game adapter exists, replace this function
// with a read of CEnvironment. The standalone build deliberately does not
// claim that integration exists.
inline EnvironmentWeatherSnapshot ReadStandaloneEnvironmentContract()
{
    return EnvironmentWeatherSnapshot{};
}
} // namespace clouds
