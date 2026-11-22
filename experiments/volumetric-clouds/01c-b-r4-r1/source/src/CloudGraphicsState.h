#pragma once

// V-WEATHER-01C-B: public graphics settings state machine.
//
// This adapter owns only public cloud graphics controls. It never stores or
// applies weather, timeline, seed, density, lighting or debug state.

#include "CloudSettings.h"

#include <cstdint>
#include <string>

namespace clouds
{
struct CloudGraphicsState
{
    bool cloudsEnabled = true;
    CloudQuality quality = CloudQuality::Quality;
    CloudRenderMode renderMode = CloudRenderMode::Volumetric3D;

    bool operator==(const CloudGraphicsState& other) const
    {
        return cloudsEnabled == other.cloudsEnabled &&
               quality == other.quality &&
               renderMode == other.renderMode;
    }
    bool operator!=(const CloudGraphicsState& other) const
    {
        return !(*this == other);
    }
};

enum class CloudSettingsRequest
{
    Accepted = 0,
    NoChange,
    Cancelled,
    Unsupported,
};

class CloudGraphicsStateMachine
{
public:
    void Initialize(const CloudGraphicsState& initial)
    {
        current_ = initial;
        pending_ = initial;
        hasPending_ = false;
        generation_ = 0;
    }

    CloudSettingsRequest Request(const CloudGraphicsState& next)
    {
        if (next.renderMode == CloudRenderMode::Classic2D)
        {
            // Unsupported requests must not disturb a pending valid change.
            lastRequest_ = CloudSettingsRequest::Unsupported;
            return lastRequest_;
        }
        if (next == current_)
        {
            if (hasPending_)
            {
                // Returning to the committed state cancels the pending change.
                pending_ = current_;
                hasPending_ = false;
                lastRequest_ = CloudSettingsRequest::Cancelled;
            }
            else
            {
                lastRequest_ = CloudSettingsRequest::NoChange;
            }
            return lastRequest_;
        }
        if (hasPending_ && next == pending_)
        {
            // Repeating the same pending request is a no-op.
            lastRequest_ = CloudSettingsRequest::NoChange;
            return lastRequest_;
        }
        pending_ = next;
        hasPending_ = true;
        lastRequest_ = CloudSettingsRequest::Accepted;
        return lastRequest_;
    }

    bool HasPending() const { return hasPending_; }
    const CloudGraphicsState& Current() const { return current_; }
    const CloudGraphicsState& Pending() const { return pending_; }
    const CloudGraphicsState& Effective() const
    {
        return hasPending_ ? pending_ : current_;
    }

    uint64_t Commit()
    {
        if (!hasPending_)
            return generation_;
        current_ = pending_;
        hasPending_ = false;
        ++generation_;
        return generation_;
    }

    void CancelPending()
    {
        pending_ = current_;
        hasPending_ = false;
    }

    uint64_t Generation() const { return generation_; }

    static const char* RequestName(CloudSettingsRequest request)
    {
        switch (request)
        {
        case CloudSettingsRequest::Accepted: return "accepted";
        case CloudSettingsRequest::NoChange: return "no-change";
        case CloudSettingsRequest::Cancelled: return "cancelled";
        case CloudSettingsRequest::Unsupported: return "unsupported";
        }
        return "unknown";
    }

private:
    CloudGraphicsState current_{};
    CloudGraphicsState pending_{};
    bool hasPending_ = false;
    uint64_t generation_ = 0;
    CloudSettingsRequest lastRequest_ = CloudSettingsRequest::NoChange;
};
} // namespace clouds
