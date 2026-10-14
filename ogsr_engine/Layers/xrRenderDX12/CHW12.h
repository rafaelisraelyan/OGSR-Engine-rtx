#pragma once

// F2.0 — minimal CHW12 interface (empty class)
// F2.1 will add D3D12CreateDevice, swapchain, command queue, fences.

class CHW12
{
public:
    CHW12() = default;
    ~CHW12() = default;

    CHW12(const CHW12&) = delete;
    CHW12& operator=(const CHW12&) = delete;
};
