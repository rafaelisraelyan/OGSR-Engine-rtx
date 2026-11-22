#requires -Version 5.1
$ErrorActionPreference = 'Stop'

$root = 'C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work'
$harness = Join-Path $root 'build_fault\Release\OGSRCloudsTerminalHarness.exe'
$exe = Join-Path $root 'build_fault\Release\OGSRCloudsTest.exe'
$evidence = Join-Path $root 'evidence\faults'
if (Test-Path $evidence) { Remove-Item $evidence -Recurse -Force }
New-Item -ItemType Directory -Force -Path $evidence | Out-Null

$common = @(
    '--camera=1000,150,-7000', '--camera-yaw=12', '--camera-pitch=10',
    '--fov=62', '--weather-seed=7331', '--time=0', '--hold-cloud-time',
    '--weather-timeline=overcast:rain_sf', '--timeline-duration=1.0',
    '--timeline-time=1.15', '--no-vsync', '--breakup=off',
    '--front-height=on', '--sun-azimuth=65', '--sun-elevation=18',
    '--wind=off')

function FramesArgs {
    return @('--mode=clouds', '--window=800x450', '--frames=1', '--no-vsync')
}

function RuntimeArgs($dir) {
    return @('--mode=clouds', '--window=800x450',
             "--runtime-settings-fault-test=$dir") + $common
}

function Run-Case($name, $childArgs, $settings = @{}, $block = 'none',
                  $blockAt = '', $witnessDisable = $false, $timeout = 120000,
                  $breakHandle = '') {
    $d = Join-Path $evidence $name
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    $a = @('--exe', $exe, '--out', $d, '--name', $name,
           '--timeout', "$timeout")
    if ($block -ne 'none') {
        $a += @('--block', $block, '--block-at', $blockAt)
    }
    if ($breakHandle -ne '') { $a += @('--break-handle', $breakHandle) }
    foreach ($key in $settings.Keys) {
        $a += @('--env', "$key=$($settings[$key])")
    }
    if ($witnessDisable) { $a += '--witness-disable' }
    $a += @('--') + $childArgs
    & $harness @a | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "harness setup failed: $name"
    }
    $exit = (Get-Content (Join-Path $d 'exit_code.txt')).Trim()
    $timeout = (Get-Content (Join-Path $d 'timed_out.txt')).Trim()
    " $name exit=$exit timeout=$timeout"
}

# ---------------------------------------------------------------------------
# Recoverable pre-submit faults and pre-retirement wait failure.
# ---------------------------------------------------------------------------
foreach ($point in @('texture-0', 'texture-1', 'texture-2',
                     'probe-buffer', 'probe-transition')) {
    $name = $point -replace '-', '_'
    $d = Join-Path $evidence $name
    Run-Case $name (RuntimeArgs $d) @{ 'OGSR_CLOUD_FAULT_POINT' = $point }
}

$d = Join-Path $evidence 'wait_for_gpu'
Run-Case 'wait_for_gpu' (RuntimeArgs $d) @{
    'OGSR_CLOUD_FAULT_POINT' = 'wait-for-gpu'
}

# Negative control for the submission witness: the injected pre-submit wait
# failure happens before the first ExecuteCommandLists, so the witness must
# report submit_count = 0 and no execute-command-lists event.
$d = Join-Path $evidence 'pre_submit_no_submit'
Run-Case 'pre_submit_no_submit' (RuntimeArgs $d) @{
    'OGSR_CLOUD_FAULT_POINT' = 'pre-submit-wait'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
}

# ---------------------------------------------------------------------------
# FI-1 / FI-2 at runtime, cloud initialization and resize.
# ---------------------------------------------------------------------------
foreach ($point in @('renderer-post-submit', 'renderer-post-signal')) {
    $suffix = $point -replace '-', '_'

    $name = 'runtime_' + $suffix
    $d = Join-Path $evidence $name
    Run-Case $name (RuntimeArgs $d) @{ 'OGSR_CLOUD_FAULT_POINT' = $point }

    $name = 'cloudinit_' + $suffix
    Run-Case $name (FramesArgs) @{
        'OGSR_CLOUD_FAULT_POINT' = $point
        'OGSR_CLOUD_FAULT_ARM_AT_CLOUD_INIT' = '1'
    }

    $name = 'resize_' + $suffix
    $d = Join-Path $evidence $name
    Run-Case $name (RuntimeArgs $d) @{
        'OGSR_CLOUD_FAULT_POINT' = $point
        'OGSR_CLOUD_FAULT_STAGE' = 'resize'
    }
}

# ---------------------------------------------------------------------------
# L1/L2 logger faults at initialization, cloud init, quality and resize.
# ---------------------------------------------------------------------------
foreach ($logger in @('post-submit', 'post-signal')) {
    $tag = if ($logger -eq 'post-submit') { 'l1' } else { 'l2' }

    Run-Case "$($tag)_init" (FramesArgs) @{
        'OGSR_CLOUD_LOGGER_FAULT' = $logger
    }
    Run-Case "$($tag)_cloudinit" (FramesArgs) @{
        'OGSR_CLOUD_LOGGER_FAULT' = $logger
        'OGSR_CLOUD_LOGGER_FAULT_ARM' = 'cloud-init'
    }
    $d = Join-Path $evidence "$($tag)_quality"
    Run-Case "$($tag)_quality" (RuntimeArgs $d) @{
        'OGSR_CLOUD_LOGGER_FAULT' = $logger
        'OGSR_CLOUD_LOGGER_FAULT_ARM' = 'quality'
    }
    $d = Join-Path $evidence "$($tag)_resize"
    Run-Case "$($tag)_resize" (RuntimeArgs $d) @{
        'OGSR_CLOUD_LOGGER_FAULT' = $logger
        'OGSR_CLOUD_LOGGER_FAULT_ARM' = 'resize'
    }
}

# ---------------------------------------------------------------------------
# L4a/L4b, L5-L7 and the shutdown fail-closed paths.
# ---------------------------------------------------------------------------
Run-Case 'l4_pre_retirement_logger' (FramesArgs) @{
    'OGSR_CLOUD_LOGGER_FAULT' = 'pre-retirement'
}
Run-Case 'l4_wait_error_logger' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'wait-for-gpu'
    'OGSR_CLOUD_FAULT_ARM_AT_CLOUD_INIT' = '1'
    'OGSR_CLOUD_LOGGER_FAULT' = 'wait-error'
}
foreach ($case in @(
    @{ n = 'l5_application_shutdown'; f = 'application-shutdown' },
    @{ n = 'l6_cloud_shutdown'; f = 'cloud-shutdown' },
    @{ n = 'l7_renderer_shutdown'; f = 'renderer-shutdown' })) {
    Run-Case $case.n (FramesArgs) @{
        'OGSR_CLOUD_LOGGER_FAULT' = $case.f
    }
}

# Shutdown fail-closed paths: injected GPU wait failure before the retirement
# wait of each owner. These are the terminal shutdown cases.
Run-Case 'shutdown_application_wait' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'shutdown-application-wait'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
}
Run-Case 'shutdown_cloud_wait' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'shutdown-cloud-wait'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
}
Run-Case 'shutdown_renderer_wait' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'shutdown-renderer-wait'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
}

# ---------------------------------------------------------------------------
# Probe-capacity path.
# ---------------------------------------------------------------------------
Run-Case 'probe_capacity_pre_retirement' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_PROBE_CAPACITY' = '1'
    'OGSR_CLOUD_LOGGER_FAULT' = 'pre-retirement-probe'
    'OGSR_CLOUD_LOGGER_FAULT_ARM' = 'probe-capacity'
}
Run-Case 'probe_capacity_post_submit' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_PROBE_CAPACITY' = '1'
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_PROBE_CAPACITY' = '1'
}
Run-Case 'probe_capacity_wait_error' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_PROBE_CAPACITY' = '1'
    'OGSR_CLOUD_FAULT_POINT' = 'wait-for-gpu'
    'OGSR_CLOUD_FAULT_ARM_AT_PROBE_CAPACITY' = '1'
    'OGSR_CLOUD_LOGGER_FAULT' = 'wait-error'
}

# ---------------------------------------------------------------------------
# Blocked stdout/stderr matrix.
#
# Every blocked case ends at the terminal boundary (exit 1). The pipes are
# switched by the child at the coordinated label, and logging is NOT suppressed:
# if any diagnostic sink remained between the activation label and the terminal
# action, the child would block on the full pipe and the harness would report a
# timeout, which the parser counts as FAIL. Clean shutdown runs are therefore
# intentionally NOT part of the blocked matrix - they emit diagnostics after the
# confirmed idle GPU, which is allowed to block.
#
# 'immediate-terminal-fault' is the label emitted by ExecuteImmediate after the
# pre-submit idle wait and before the command list reset/submit, in the immediate
# that consumes the pending terminal fault.
# ---------------------------------------------------------------------------
$terminalFaultLabel = 'immediate-terminal-fault'
$preRetirementLabel = 'cloud-pre-retirement'

# Mandatory cases 1-3: full stdout, full stderr, both full.
Run-Case 'blocked_stdout_post_submit' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
} 'stdout' $terminalFaultLabel

Run-Case 'blocked_stderr_post_submit' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
} 'stderr' $terminalFaultLabel

# Mandatory cases 4-6: post-submit, post-signal and L1/L2 with both pipes full.
Run-Case 'blocked_both_post_submit' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
} 'both' $terminalFaultLabel

Run-Case 'blocked_both_post_signal' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-signal'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
} 'both' $terminalFaultLabel

Run-Case 'blocked_both_l1' (FramesArgs) @{
    'OGSR_CLOUD_LOGGER_FAULT' = 'post-submit'
} 'both' $terminalFaultLabel

Run-Case 'blocked_both_l2' (FramesArgs) @{
    'OGSR_CLOUD_LOGGER_FAULT' = 'post-signal'
} 'both' $terminalFaultLabel

# Mandatory case 9: cloud initialization, runtime quality switch and resize.
Run-Case 'blocked_both_cloudinit' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_CLOUD_INIT' = '1'
} 'both' $terminalFaultLabel

$d = Join-Path $evidence 'blocked_both_quality'
Run-Case 'blocked_both_quality' (RuntimeArgs $d) @{
    'OGSR_CLOUD_LOGGER_FAULT' = 'post-submit'
    'OGSR_CLOUD_LOGGER_FAULT_ARM' = 'quality'
} 'both' $terminalFaultLabel

$d = Join-Path $evidence 'blocked_both_resize'
Run-Case 'blocked_both_resize' (RuntimeArgs $d) @{
    'OGSR_CLOUD_LOGGER_FAULT' = 'post-submit'
    'OGSR_CLOUD_LOGGER_FAULT_ARM' = 'resize'
} 'both' $terminalFaultLabel

# Mandatory case 8: pre-retirement GPU wait failure.
Run-Case 'blocked_both_pre_retirement_wait' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'wait-for-gpu'
    'OGSR_CLOUD_FAULT_ARM_AT_CLOUD_INIT' = '1'
    'OGSR_CLOUD_LOGGER_FAULT' = 'wait-error'
} 'both' $preRetirementLabel

# Mandatory case 10: probe-capacity path.
Run-Case 'blocked_both_probe' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_PROBE_CAPACITY' = '1'
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_PROBE_CAPACITY' = '1'
} 'both' $terminalFaultLabel

Run-Case 'blocked_both_probe_wait' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_PROBE_CAPACITY' = '1'
    'OGSR_CLOUD_FAULT_POINT' = 'wait-for-gpu'
    'OGSR_CLOUD_FAULT_ARM_AT_PROBE_CAPACITY' = '1'
    'OGSR_CLOUD_LOGGER_FAULT' = 'wait-error'
    'OGSR_CLOUD_LOGGER_FAULT_ARM' = 'probe-capacity'
} 'both' $preRetirementLabel

# Mandatory case 10: shutdown of Application / CloudRayMarcher / Renderer.
Run-Case 'blocked_both_shutdown_application' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'shutdown-application-wait'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
} 'both' 'application-shutdown'
Run-Case 'blocked_both_shutdown_cloud' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'shutdown-cloud-wait'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
} 'both' 'cloud-shutdown'
Run-Case 'blocked_both_shutdown_renderer' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'shutdown-renderer-wait'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
} 'both' 'renderer-shutdown'

# Mandatory case 7: the terminal witness cannot be written at all.
Run-Case 'blocked_both_witness_failure' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
} 'both' $terminalFaultLabel $true

# Sensitivity control: the child performs one deliberate write through the
# freshly installed pipe. Because the pipe has no reader the write must block,
# the harness must hit its timeout, and the parser must see timed_out=1. This
# proves the blocked-pipe setup is real and that a timeout is never accepted as
# a successful fail-closed exit.
$d = Join-Path $evidence 'control_blocked_pipe_probe'
Run-Case 'control_blocked_pipe_probe' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
    'OGSR_CLOUD_BLOCK_PROBE_TARGET' = 'stdout'
} 'both' $terminalFaultLabel

# ---------------------------------------------------------------------------
# P2-B: per-stream pipe installation proof and per-stream sensitivity controls.
#
# Each control deliberately writes into ONE filled pipe, so the timeout proves
# that stream specifically. The `both` variants prove stdout and stderr
# independently instead of proving only whichever stream is written first.
# ---------------------------------------------------------------------------
Run-Case 'control_stdout_probe' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
    'OGSR_CLOUD_BLOCK_PROBE_TARGET' = 'stdout'
} 'stdout' $terminalFaultLabel

Run-Case 'control_stderr_probe' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
    'OGSR_CLOUD_BLOCK_PROBE_TARGET' = 'stderr'
} 'stderr' $terminalFaultLabel

Run-Case 'control_both_stdout_probe' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
    'OGSR_CLOUD_BLOCK_PROBE_TARGET' = 'stdout'
} 'both' $terminalFaultLabel

Run-Case 'control_both_stderr_probe' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
    'OGSR_CLOUD_BLOCK_PROBE_TARGET' = 'stderr'
} 'both' $terminalFaultLabel

# Negative control: an invalid pipe handle must FAIL installation. The child
# must not report activation and must keep running on the real streams.
$d = Join-Path $evidence 'pipe_setup_failure_no_activation'
Run-Case 'pipe_setup_failure_no_activation' (FramesArgs) @{
    'OGSR_CLOUD_FAULT_POINT' = 'renderer-post-submit'
    'OGSR_CLOUD_FAULT_ARM_AT_START' = '1'
} 'stdout' $terminalFaultLabel $false 240000 'stdout'

# ---------------------------------------------------------------------------
# P2-A: interactive multi-frame diagnostics with several frames in flight.
# A real queue gate keeps later frames unconfirmed, the old back buffer is
# reused, the window is really minimized/restored, a present notice is forced,
# and the deferred diagnostics must stay deferred until the GPU is fully idle.
# ---------------------------------------------------------------------------
function InflightArgs {
    # A heavier frame keeps real submissions pending across a back-buffer reuse.
    return @('--mode=clouds', '--window=1600x900', '--frames=7', '--no-vsync')
}

$inflightCommon = @{
    'OGSR_CLOUD_INFLIGHT_TEST' = '1'
    'OGSR_CLOUD_FORCE_PRESENT_NOTICE' = '1'
}

Run-Case 'inflight_no_block' (InflightArgs) $inflightCommon 'none' '' $false 300000
Run-Case 'inflight_blocked_stdout' (InflightArgs) $inflightCommon 'stdout' 'inflight-diagnostics' $false 300000
Run-Case 'inflight_blocked_stderr' (InflightArgs) $inflightCommon 'stderr' 'inflight-diagnostics' $false 300000
Run-Case 'inflight_blocked_both' (InflightArgs) $inflightCommon 'both' 'inflight-diagnostics' $false 300000

"fault matrix complete"
