#pragma once

#include <stdexcept>
#include <string>

namespace clouds
{
// A recoverable candidate-resource failure: the previously published GPU
// resources remain valid, so the pending public settings change can be
// cancelled and the session may continue.
class CloudResourceError : public std::runtime_error
{
public:
    explicit CloudResourceError(const std::string& message)
        : std::runtime_error(message) {}
};

// GPU completion/device state is no longer trustworthy. No further GPU submit
// or unsafe resource destruction is allowed; the harness must fail closed.
class CloudGpuFatalError : public std::runtime_error
{
public:
    explicit CloudGpuFatalError(const std::string& message)
        : std::runtime_error(message) {}
};
} // namespace clouds
