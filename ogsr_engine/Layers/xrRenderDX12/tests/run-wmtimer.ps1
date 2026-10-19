# F3.3.0a: WM_TIMER / modal sizing path test.
#
# run-renderprep.ps1 never posted WM_ENTERSIZEMOVE/WM_TIMER, so the timer
# render path added in F3.1-FIX B had no automated coverage at all. This
# script drives it directly and asserts that frames were ACTUALLY rendered
# from the WM_TIMER handler, not merely that messages were delivered:
#
#   - the real 16 ms timer is started by WM_ENTERSIZEMOVE and stopped by
#     WM_EXITSIZEMOVE, so the timer path is exercised for real;
#   - an explicit burst of WM_TIMER messages follows, and the number of
#     successfully rendered timer frames must reach the burst size;
#   - [runtime] timerFrames must stay far below [runtime] rendered, because
#     the main loop is a busy loop: if timerFrames counted main-loop frames
#     it would be orders of magnitude larger, so this also proves the
#     counter isolates the timer path.
#
# Exit code 0 = PASS, 1 = FAIL.
param(
    [string]$Executable = '',
    [string]$Mode = 'parallel',
    [ValidateSet('legacy','multi','parallel')][string]$CommandLists = 'legacy',
    [int]$Burst = 40,
    [int]$TimeoutMs = 60000,
    # F3.3.0a: launch with -timerfault (test-only one-shot WM_TIMER fault) and
    # assert the failure path instead of the success path.
    [switch]$TimerFault
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if (!$Executable) { $Executable = Join-Path $root 'x64/ReleaseDX12/xrDX12Smoke.exe' }
$Executable = (Resolve-Path $Executable).Path
$logPath = Join-Path (Split-Path $Executable) 'xrDX12Smoke.log'

Add-Type -Namespace GRWMTimer -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, char[] buf, int max);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
'@

$WM_ENTERSIZEMOVE = 0x0231
$WM_EXITSIZEMOVE  = 0x0232
$WM_TIMER         = 0x0113
$WM_CLOSE         = 0x0010

$failures = [Collections.Generic.List[string]]::new()
function Fail([string]$m) { $script:failures.Add($m); Write-Output "  FAIL: $m" }

$exeArgs = "-renderprep $Mode -jobworkers 4 -fixedpose -commandlists $CommandLists"
if ($TimerFault) { $exeArgs = "$exeArgs -timerfault" }
$p = Start-Process $Executable -ArgumentList $exeArgs -WindowStyle Hidden -PassThru

# Wait for the window to exist (class name is stable; the title is not).
$hwnd = [IntPtr]::Zero
foreach ($attempt in 1..40) {
    Start-Sleep -Milliseconds 250
    if ($p.HasExited) { break }
    $cb = [GRWMTimer.U+EnumWindowsProc]{ param($h, $l)
        $cls = New-Object char[] 256
        [GRWMTimer.U]::GetClassNameW($h, $cls, 256) | Out-Null
        $name = (-join $cls).Trim([char]0)
        if ($name -eq 'XRDX12Smoke') { $script:hwnd = $h }
        return $true }
    $script:hwnd = [IntPtr]::Zero
    [GRWMTimer.U]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    if ($hwnd -ne [IntPtr]::Zero) { break }
}

if ($hwnd -eq [IntPtr]::Zero) {
    Write-Output "WM_TIMER test: window not found (exited=$($p.HasExited))"
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    exit 1
}
Write-Output "window found: $hwnd (pid $($p.Id))"

# Make the window a real window so WM_SIZE carries a real client area.
[GRWMTimer.U]::SetWindowPos($hwnd, [IntPtr]::Zero, 60, 60, 1296, 759, 0x0040) | Out-Null
Start-Sleep -Milliseconds 300

Write-Output "phase 1: WM_ENTERSIZEMOVE (starts the real 16 ms timer)"
[GRWMTimer.U]::PostMessage($hwnd, $WM_ENTERSIZEMOVE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 1000

if ($TimerFault) {
    # The armed fault is consumed by the first WM_TIMER: RenderFrameFromTimer()
    # returns false without touching device/fence/resources, the handler stops
    # the timer, latches g_timerFrameFailed and asks the main loop to exit via
    # WM_CLOSE. Messages posted AFTER the failure must render nothing.
    Write-Output "phase 2 (fault): post more WM_TIMER messages; they must be ignored"
    for ($i = 0; $i -lt $Burst; $i++) {
        [GRWMTimer.U]::PostMessage($hwnd, $WM_TIMER, [IntPtr]1, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 16
    }
    Start-Sleep -Milliseconds 500
}
else {
    Write-Output "phase 2: WM_EXITSIZEMOVE (kills the timer)"
    [GRWMTimer.U]::PostMessage($hwnd, $WM_EXITSIZEMOVE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 400

    Write-Output "phase 3: explicit burst of $Burst WM_TIMER messages"
    for ($i = 0; $i -lt $Burst; $i++) {
        [GRWMTimer.U]::PostMessage($hwnd, $WM_TIMER, [IntPtr]1, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 16
    }
    Start-Sleep -Milliseconds 400
}

Write-Output "phase 4: WM_CLOSE (ignored if the fault path already exited)"
if (-not $p.HasExited) {
    [GRWMTimer.U]::PostMessage($hwnd, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
}
if (-not $p.WaitForExit($TimeoutMs)) {
    Write-Output "WM_TIMER test: timeout waiting for exit"
    Stop-Process -Id $p.Id -Force
    exit 1
}
$exit = $p.ExitCode
Write-Output "exit code: $exit"

# dx12log releases the file only at process exit, so read it now.
$log = ''
foreach ($attempt in 1..20) {
    try { $log = [IO.File]::ReadAllText($logPath); break }
    catch { Start-Sleep -Milliseconds 200 }
}
if ($log -eq '') { Write-Output 'WM_TIMER test: log unreadable'; exit 1 }

$timerFrames = [int]([regex]::Match($log, 'timerFrames=(\d+)').Groups[1].Value)
$rendered    = [int]([regex]::Match($log, '\[runtime\] rendered=(\d+)').Groups[1].Value)
$prep        = [regex]::Match($log, '\[renderprep\] [^\r\n]+').Value
$validation  = ([regex]::Matches($log, '\[validation\]\[(WARNING|ERROR|CORRUPTION)\]')).Count
$prepFail    = ([regex]::Matches($log, '\[renderprep\] FAIL')).Count
$rfFail      = ([regex]::Matches($log, 'RenderFrame failed')).Count
$enters      = ([regex]::Matches($log, 'WM_ENTERSIZEMOVE')).Count
$exits       = ([regex]::Matches($log, 'WM_EXITSIZEMOVE')).Count
$drained     = $log.Contains('GPU drained before shutdown')

Write-Output "renderprep : $prep"
Write-Output "rendered=$rendered timerFrames=$timerFrames validationWarnErr=$validation prepFail=$prepFail renderFrameFailed=$rfFail"
Write-Output "entersSizemove=$enters exitsSizemove=$exits gpuDrained=$drained"

if ($TimerFault) {
    # Failure-path contract:
    #   - RenderFrame() returns false exactly once, from the WM_TIMER path;
    #   - the timer stops and no further timer rendering happens;
    #   - the process exits non-zero on its own;
    #   - GPU and JobSystem shut down cleanly;
    #   - no real device removal was provoked.
    $timerDiag = ([regex]::Matches($log, 'WM_TIMER: RenderFrame failed')).Count
    $stopped   = $log.Contains('stopping timer rendering')
    $jobs      = [regex]::Match($log, '\[jobs\] stopped[^\r\n]+').Value
    $jobsOk    = ($jobs -match 'failures=0') -and ($jobs -match 'unobserved=0')
    $removed   = $log.Contains('GetDeviceRemovedReason')

    Write-Output "timerDiag=$timerDiag stopped=$stopped jobsOk=$jobsOk deviceRemoved=$removed"
    Write-Output "jobs: $jobs"

    if ($exit -ne 1) { Fail "exit code $exit, expected 1 after a WM_TIMER render failure" }
    if ($timerDiag -ne 1) { Fail "expected exactly 1 WM_TIMER RenderFrame failure, saw $timerDiag" }
    if (-not $stopped) { Fail 'no "stopping timer rendering" diagnostic' }
    if ($timerFrames -ne 0) { Fail "timerFrames=$timerFrames, expected 0: rendering continued after the failure" }
    if (-not $jobsOk) { Fail "JobSystem shutdown not clean: $jobs" }
    if (-not $drained) { Fail 'no GPU drain before shutdown' }
    if ($removed) { Fail 'GetDeviceRemovedReason was called: a real device removal was provoked' }
    if ($validation -ne 0) { Fail "$validation validation WARNING/ERROR/CORRUPTION" }
}
else {
    if ($exit -ne 0) { Fail "exit code $exit" }
    if ($prep -notmatch '\[renderprep\] PASS') { Fail 'no [renderprep] PASS' }
    if ($timerFrames -lt $Burst) { Fail "timerFrames=$timerFrames < burst=${Burst}: WM_TIMER frames were not actually rendered" }
    if ($timerFrames -ge $rendered) { Fail "timerFrames=$timerFrames >= rendered=${rendered}: timer counter does not isolate the timer path" }
    if ($validation -ne 0) { Fail "$validation validation WARNING/ERROR/CORRUPTION" }
    if ($prepFail -ne 0) { Fail "$prepFail [renderprep] FAIL" }
    if ($rfFail -ne 0) { Fail "$rfFail RenderFrame failed" }
    if ($enters -ne 1) { Fail "WM_ENTERSIZEMOVE seen $enters times" }
    if ($exits -ne 1) { Fail "WM_EXITSIZEMOVE seen $exits times" }
    if (-not $drained) { Fail 'no GPU drain before shutdown' }
}

if ($failures.Count -eq 0) {
    if ($TimerFault) {
        Write-Output "WM_TIMER ${Mode} fault: PASS (exit=$exit timerFrames=$timerFrames)"
    }
    else {
        Write-Output "WM_TIMER ${Mode}: PASS (timerFrames=$timerFrames rendered=$rendered)"
    }
    exit 0
}
Write-Output "WM_TIMER ${Mode}: FAIL ($($failures.Count) checks)"
exit 1
