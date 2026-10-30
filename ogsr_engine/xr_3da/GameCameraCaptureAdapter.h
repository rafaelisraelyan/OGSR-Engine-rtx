#pragma once

// F4.0-B2.3 experimental CPU-only adapter. The implementation is compiled only
// when OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE is defined. Runtime
// opt-in is the startup parameter -b2_camera_capture.
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE

void B2CameraCapture_Initialize();

void B2CameraCapture_OnLevelReady(const char* levelName);
void B2CameraCapture_OnLevelTeardownBegin();
void B2CameraCapture_OnLevelTeardownComplete();

void B2CameraCapture_OnCameraFrameReady();
void B2CameraCapture_OnFrameRenderBeginSucceeded();
void B2CameraCapture_OnFrameRenderBeginFailed();

void B2CameraCapture_OnDeviceResetBegin();
void B2CameraCapture_OnEngineShutdown();

#ifdef OGSR_B2_CAMERA_TEST_HOOKS
// Test-only fault injection for the B4 diagnostic-throttling checks. These
// entry points are compiled only into dedicated verification builds.
void B2CameraCapture_TestArmPublishRejects(unsigned count);
unsigned B2CameraCapture_TestPublishRejectsRemaining();
void B2CameraCapture_TestExhaustCaptureSequence();
void B2CameraCapture_TestLogState(const char* phase);
#endif

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_CAMERA_CAPTURE
