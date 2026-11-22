#!/usr/bin/env python3
"""Verify the R4 non-blocking terminal / shared-memory witness matrix.

Rules enforced here:
  * a child killed by the harness timeout is a FAIL, never a pass;
  * every terminal case must exit with code 1 on its own;
  * every post-submit terminal case must show a real GPU submission
    (submit_count >= 1, non-null submitted command list pointer,
    execute-command-lists before the terminal action);
  * the pre-submit negative control must show submit_count == 0 and no
    execute-command-lists event, proving the submission witness is not a
    blindly emitted text marker;
  * no GPU resource owner may be released before the terminal action;
  * blocked-pipe cases must run WITHOUT logging suppression, so a diagnostic
    sink left in the critical interval would hang the child and be reported as
    a timeout;
  * the sensitivity control must time out, proving the full pipe really blocks
    and that a timeout is never accepted as fail-closed.
"""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work")
E = ROOT / "evidence" / "faults"
OUT_JSON = ROOT / "evidence" / "01c_b_r4_faults.json"
OUT_TXT = ROOT / "evidence" / "01c_b_r4_faults.txt"

RECOVERABLE = ("texture_0", "texture_1", "texture_2", "probe_buffer",
               "probe_transition")
HEALTHY = (
    "l4_pre_retirement_logger", "l5_application_shutdown",
    "l6_cloud_shutdown", "l7_renderer_shutdown",
    "probe_capacity_pre_retirement",
)
WITNESS_FAILURE = "blocked_both_witness_failure"
CONTROL = "control_blocked_pipe_probe"
# R4-R1: per-stream pipe controls (each must time out on its own stream).
# case -> probe target (1 stdout, 2 stderr)
PIPE_CONTROLS = {
    "control_stdout_probe": 1,
    "control_stderr_probe": 2,
    "control_both_stdout_probe": 1,
    "control_both_stderr_probe": 2,
}
# case -> which streams the runner blocks (1 stdout, 2 stderr, 3 both)
PIPE_CONTROL_REQUESTED = {
    "control_stdout_probe": 1,
    "control_stderr_probe": 2,
    "control_both_stdout_probe": 3,
    "control_both_stderr_probe": 3,
}
INFLIGHT_REQUESTED = {
    "inflight_no_block": 0,
    "inflight_blocked_stdout": 1,
    "inflight_blocked_stderr": 2,
    "inflight_blocked_both": 3,
}
# R4-R1: interactive multi-frame diagnostics (healthy, exit 0).
INFLIGHT = (
    "inflight_no_block", "inflight_blocked_stdout",
    "inflight_blocked_stderr", "inflight_blocked_both",
)
PIPE_FAILURE = "pipe_setup_failure_no_activation"
WAIT_TERMINAL = (
    "wait_for_gpu", "l4_wait_error_logger",
    "probe_capacity_wait_error", "blocked_both_pre_retirement_wait",
    "blocked_both_probe_wait",
)
PRE_SUBMIT_TERMINAL = ("pre_submit_no_submit",)
SHUTDOWN_TERMINAL = (
    "shutdown_application_wait", "shutdown_cloud_wait", "shutdown_renderer_wait",
    "blocked_both_shutdown_application", "blocked_both_shutdown_cloud",
    "blocked_both_shutdown_renderer",
)
SIGNAL_TERMINAL = (
    "runtime_renderer_post_signal", "cloudinit_renderer_post_signal",
    "resize_renderer_post_signal", "l2_init", "l2_cloudinit", "l2_quality",
    "l2_resize", "blocked_both_post_signal", "blocked_both_l2",
)
LOGGER_TERMINAL = tuple(
    case for case in (
        "l1_init", "l1_cloudinit", "l1_quality", "l1_resize",
        "l2_init", "l2_cloudinit", "l2_quality", "l2_resize",
        "l4_wait_error_logger",
        "blocked_both_l1", "blocked_both_l2",
        "blocked_both_quality", "blocked_both_resize",
        "blocked_both_pre_retirement_wait", "blocked_both_probe_wait",
    ))
TERMINAL = (
    "wait_for_gpu", "pre_submit_no_submit",
    "runtime_renderer_post_submit", "runtime_renderer_post_signal",
    "cloudinit_renderer_post_submit", "cloudinit_renderer_post_signal",
    "resize_renderer_post_submit", "resize_renderer_post_signal",
    "l1_init", "l1_cloudinit", "l1_quality", "l1_resize",
    "l2_init", "l2_cloudinit", "l2_quality", "l2_resize",
    "l4_wait_error_logger",
    "shutdown_application_wait", "shutdown_cloud_wait", "shutdown_renderer_wait",
    "probe_capacity_post_submit", "probe_capacity_wait_error",
    "blocked_stdout_post_submit", "blocked_stderr_post_submit",
    "blocked_both_post_submit", "blocked_both_post_signal",
    "blocked_both_l1", "blocked_both_l2", "blocked_both_cloudinit",
    "blocked_both_quality", "blocked_both_resize",
    "blocked_both_pre_retirement_wait", "blocked_both_probe",
    "blocked_both_probe_wait", "blocked_both_shutdown_application",
    "blocked_both_shutdown_cloud", "blocked_both_shutdown_renderer",
)
BLOCKED = tuple(case for case in TERMINAL if case.startswith("blocked_"))
ALL_CASES = (list(RECOVERABLE) + list(HEALTHY) + [WITNESS_FAILURE, CONTROL] +
             list(TERMINAL) + list(PIPE_CONTROLS) + list(INFLIGHT) +
             [PIPE_FAILURE])


def read_text(case: str, name: str) -> str:
    path = E / case / name
    return path.read_text(encoding="utf-8", errors="replace") \
        if path.exists() else ""


def read_json(case: str, name: str):
    text = read_text(case, name).lstrip("\ufeff")
    return json.loads(text) if text.strip() else None


def witness(case: str) -> dict:
    return read_json(case, "witness.json") or {}


def events(case: str) -> list[str]:
    return [item.get("name", "") for item in witness(case).get("events", [])]


def exit_code(case: str) -> int:
    text = read_text(case, "exit_code.txt").lstrip("\ufeff").strip()
    return int(text or "-1")


def timed_out(case: str) -> bool:
    return read_text(case, "timed_out.txt").lstrip("\ufeff").strip() == "1"


def index_of_last(items: list[str], name: str) -> int:
    for index in range(len(items) - 1, -1, -1):
        if items[index] == name:
            return index
    return -1


def terminal_checks(case: str) -> dict:
    ev = events(case)
    data = witness(case)
    terminal_index = index_of_last(ev, "terminal-action")
    execute_index = index_of_last(ev, "execute-command-lists")
    # The invariant is about the interval that starts at the failure trigger and
    # ends at the terminal action. Submissions that were confirmed normally
    # before the trigger legitimately release their owners and confirm fences.
    trigger_candidates = [
        index for index, event in enumerate(ev)
        if event in ("fault-injected", "logger-fault-injected", "wait-failed")
    ]
    trigger_index = max(trigger_candidates, default=-1)
    between = ev[trigger_index + 1:terminal_index] \
        if 0 <= trigger_index < terminal_index else []
    submit_count = int(data.get("submit_count") or 0)
    submitted_list = int(data.get("last_submitted_list") or 0)

    checks = {
        "not_timed_out": not timed_out(case),
        "exit_one": exit_code(case) == 1,
        "terminal_flag": bool(data.get("terminal")),
        "terminal_action_last": terminal_index >= 0
        and terminal_index == len(ev) - 1,
        "no_release_after_trigger": not any(item.startswith("release-")
                                            for item in between),
        "no_fence_after_trigger": "fence-confirmed" not in between,
        "trigger_recorded": trigger_index >= 0,
    }

    if case in PRE_SUBMIT_TERMINAL:
        checks["no_submission"] = (submit_count == 0
                                   and execute_index < 0
                                   and submitted_list == 0)
    else:
        checks["submission_observed"] = (submit_count >= 1
                                        and submitted_list != 0
                                        and execute_index >= 0)
        checks["fault_after_submission"] = (execute_index >= 0
                                            and trigger_index > execute_index)

    if case in WAIT_TERMINAL:
        checks["wait_failed"] = "wait-failed" in ev[:terminal_index]
    if case in SIGNAL_TERMINAL:
        checks["signal_before_terminal"] = "signal-issued" in ev[:terminal_index]
    if case in LOGGER_TERMINAL:
        checks["logger_fault_injected"] = (
            "logger-fault-injected" in ev[:terminal_index])
    if case in SHUTDOWN_TERMINAL:
        checks["fault_injected"] = "fault-injected" in ev[:terminal_index]
    if case.startswith("blocked_") and case != WITNESS_FAILURE:
        blocked_index = index_of_last(ev, "blocked-pipes-activated")
        checks["blocked_pipes_activated"] = blocked_index >= 0
        checks["blocked_before_terminal"] = 0 <= blocked_index < terminal_index
        checks["blocked_before_fault"] = (
            blocked_index >= 0 and blocked_index < trigger_index)
    return checks


def recoverable_checks(case: str) -> dict:
    log = read_text(case, "stdout.txt")
    return {
        "not_timed_out": not timed_out(case),
        "exit_zero": exit_code(case) == 0,
        "recoverable": "cloud settings GPU apply failed (recoverable)" in log,
        "pending_cancelled": "cloud settings pending change cancelled" in log,
        "old_state": (
            "fault test after failed apply: current_quality=quality "
            "pending=0 divisor=2 output=400x225 resource_generation=1"
            in log),
        "retry": (
            "fault test after retry: current_quality=reference pending=0 "
            "divisor=1 output=800x450 resource_generation=2" in log),
        "resize_after": (
            "fault test complete: settings_generation=3 "
            "resource_generation=4" in log),
        "shutdown": "cloud ray marcher shutdown: generation=4" in log,
    }


def healthy_checks(case: str) -> dict:
    log = read_text(case, "stdout.txt")
    ev = events(case)
    checks = {
        "not_timed_out": not timed_out(case),
        "exit_zero": exit_code(case) == 0,
        "no_terminal": "terminal-action" not in ev,
    }
    if case == "l4_pre_retirement_logger":
        checks["logger_fault"] = "logger-fault-injected" in ev
        checks["wait_started"] = "wait-started" in ev
        checks["published"] = "cloud output resources published:" in log
    elif case.startswith("probe_capacity_pre"):
        checks["logger_fault"] = "logger-fault-injected" in ev
        checks["probe_capacity"] = "probe-capacity" in log
    elif case.startswith("l5_"):
        checks["logger_fault"] = "logger-fault-injected" in ev
        checks["renderer_released"] = (
            "renderer shutdown: releasing resources" in log)
    elif case.startswith("l6_"):
        checks["logger_fault"] = "logger-fault-injected" in ev
        checks["cloud_released"] = (
            "cloud ray marcher shutdown: complete" in log)
    elif case.startswith("l7_"):
        checks["logger_fault"] = "logger-fault-injected" in ev
        checks["renderer_complete"] = "renderer shutdown complete" in log
    return checks


def witness_failure_checks(case: str) -> dict:
    data = witness(case)
    return {
        "not_timed_out": not timed_out(case),
        "exit_one": exit_code(case) == 1,
        "no_witness_events": not data.get("events"),
        "no_terminal_flag": not data.get("terminal"),
    }


def control_checks(case: str) -> dict:
    """The sensitivity control must hang and be reported as a timeout."""
    return {
        "timed_out": timed_out(case),
        "killed_not_exited": exit_code(case) != 1,
        "not_counted_as_fail_closed": not (
            exit_code(case) == 1 and not timed_out(case)),
    }



def pipe_status(case: str) -> dict:
    data = read_json(case, "pipe_status.json")
    return data if isinstance(data, dict) else {}


def pipe_checks(case: str, requested: int) -> dict:
    """A blocked-pipe case is only evidence if each requested stream really
    installed. `requested` is 1=stdout, 2=stderr, 3=both."""
    status = pipe_status(case)
    want_stdout = (requested & 1) != 0
    want_stderr = (requested & 2) != 0
    checks = {
        "pipe_status_present": bool(status),
        "stdout_requested_matches": int(status.get("stdout_requested") or 0)
        == (1 if want_stdout else 0),
        "stderr_requested_matches": int(status.get("stderr_requested") or 0)
        == (1 if want_stderr else 0),
        "activated": int(status.get("activated") or 0) == 1,
    }
    if want_stdout:
        checks["stdout_installed"] = int(status.get("stdout_ok") or 0) == 1
        checks["stdout_no_error"] = int(status.get("stdout_error") or 0) == 0
    if want_stderr:
        checks["stderr_installed"] = int(status.get("stderr_ok") or 0) == 1
        checks["stderr_no_error"] = int(status.get("stderr_error") or 0) == 0
    return checks


def pipe_control_checks(case: str, target: int, requested: int) -> dict:
    checks = {
        "timed_out": timed_out(case),
        "killed_not_exited": exit_code(case) != 1,
        "probe_started": int(pipe_status(case).get("probe_started") or 0) == 1,
        "probe_target_matches": int(pipe_status(case).get("probe_target") or 0)
        == target,
    }
    checks.update(pipe_checks(case, requested))
    return checks


def pipe_failure_checks(case: str) -> dict:
    """An invalid handle must fail installation and must never be reported as a
    successful activation."""
    status = pipe_status(case)
    ev = events(case)
    return {
        "not_timed_out": not timed_out(case),
        "stdout_requested": int(status.get("stdout_requested") or 0) == 1,
        "stdout_not_ok": int(status.get("stdout_ok") or 0) == 0,
        "installation_error_recorded":
            int(status.get("stdout_error") or 0) != 0,
        "not_activated": int(status.get("activated") or 0) == 0,
        "no_activation_event": "blocked-pipes-activated" not in ev,
        "failure_event": "blocked-pipes-failed" in ev,
    }


def inflight_checks(case: str, requested: int) -> dict:
    """Interactive multi-frame invariant: several REAL frame submissions were in
    flight, the diagnostics gate deferred instead of touching a stream, and it
    never ran a sink while work was unconfirmed."""
    data = witness(case)
    ev = events(case)
    checks = {
        "not_timed_out": not timed_out(case),
        "exit_zero": exit_code(case) == 0,
        "no_terminal": "terminal-action" not in ev,
        "frames_submitted": int(data.get("frame_submit_count") or 0) >= 4,
        "deferred_at_least_once": int(data.get("diag_defer_count") or 0) >= 1,
        "flushed_after_idle": int(data.get("diag_flush_count") or 0) >= 1,
        "never_unsafe": int(data.get("diag_unsafe_count") or 0) == 0,
        "inflight_observed": int(data.get("inflight_at_defer") or 0) >= 1,
        "inflight_confirmed_at_check":
            int(data.get("inflight_at_check") or 0) >= 1,
        "idle_confirmation_events": "gpu-idle-confirmed" in ev,
        "diagnostics_flushed_events": "diagnostics-flushed" in ev,
    }
    if requested:
        checks.update(pipe_checks(case, requested))
        checks["deactivated"] = int(
            pipe_status(case).get("deactivated") or 0) == 1
    return checks
def main() -> int:
    failures = []
    rows = []
    for case in ALL_CASES:
        if not (E / case / "exit_code.txt").exists():
            failures.append(f"{case}: missing harness output")
            continue
        blocked_request = 0  # 1 stdout, 2 stderr, 3 both
        if case in PIPE_CONTROLS:
            checks = pipe_control_checks(case, PIPE_CONTROLS[case],
                                         PIPE_CONTROL_REQUESTED[case])
        elif case == PIPE_FAILURE:
            checks = pipe_failure_checks(case)
        elif case in INFLIGHT:
            checks = inflight_checks(case, INFLIGHT_REQUESTED[case])
        elif case in RECOVERABLE:
            checks = recoverable_checks(case)
        elif case in HEALTHY:
            checks = healthy_checks(case)
        elif case == WITNESS_FAILURE:
            checks = witness_failure_checks(case)
        elif case == CONTROL:
            checks = control_checks(case)
        else:
            checks = terminal_checks(case)

        # Every blocked-pipe terminal case must also prove, per stream, that the
        # redirection really happened: a witness event alone is not enough.
        if case in BLOCKED and case != WITNESS_FAILURE:
            if case in ("blocked_stdout_post_submit", "blocked_stderr_post_submit",
                        "blocked_both_post_submit", "blocked_both_post_signal",
                        "blocked_both_l1", "blocked_both_l2",
                        "blocked_both_cloudinit", "blocked_both_quality",
                        "blocked_both_resize", "blocked_both_probe",
                        "blocked_both_witness_failure"):
                requested = 3 if case != "blocked_stdout_post_submit" and \
                    case != "blocked_stderr_post_submit" else (
                        1 if case == "blocked_stdout_post_submit" else 2)
                if case != "blocked_both_witness_failure":
                    checks.update(pipe_checks(case, requested))
            elif case.startswith("blocked_both_shutdown"):
                checks.update(pipe_checks(case, 3))
            elif case in ("blocked_both_pre_retirement_wait",
                          "blocked_both_probe_wait"):
                checks.update(pipe_checks(case, 3))

        ok = all(checks.values())
        data = witness(case)
        expectation = (
            "pipe_timeout_control" if case in PIPE_CONTROLS else
            "pipe_setup_failure" if case == PIPE_FAILURE else
            "interactive_inflight" if case in INFLIGHT else
            "timeout_control" if case == CONTROL else
            "recoverable" if case in RECOVERABLE else
            "healthy" if case in HEALTHY else
            "witness_failure" if case == WITNESS_FAILURE else
            "terminal")
        rows.append({
            "case": case,
            "exit_code": exit_code(case),
            "timed_out": timed_out(case),
            "submit_count": int(data.get("submit_count") or 0),
            "last_submitted_list": int(data.get("last_submitted_list") or 0),
            "last_fence_value": int(data.get("last_fence_value") or 0),
            "last_completed_value": int(data.get("last_completed_value") or 0),
            "frame_submit_count": int(data.get("frame_submit_count") or 0),
            "diag_defer_count": int(data.get("diag_defer_count") or 0),
            "diag_flush_count": int(data.get("diag_flush_count") or 0),
            "diag_unsafe_count": int(data.get("diag_unsafe_count") or 0),
            "inflight_at_defer": int(data.get("inflight_at_defer") or 0),
            "inflight_at_check": int(data.get("inflight_at_check") or 0),
            "blocked": bool(pipe_status(case).get("activated")),
            "expectation": expectation,
            "checks": checks,
            "result": "PASS" if ok else "FAIL",
        })
        if not ok:
            failures.append(f"{case}: {checks}")

    terminal_rows = [row for row in rows if row["expectation"] == "terminal"]
    blocked_rows = [row for row in rows if row["blocked"]]
    payload = {
        "format": "V_WEATHER_01C_B_R4_FAULTS_V2",
        "date": "2026-11-20",
        "result": "PASS" if not failures else "FAIL",
        "counts": {
            "cases": len(rows),
            "terminal": len(terminal_rows),
            "blocked_pipe_terminal": len([r for r in rows
                                          if r["blocked"]
                                          and r["expectation"] == "terminal"]),
            "recoverable": len([r for r in rows
                                if r["expectation"] == "recoverable"]),
            "healthy": len([r for r in rows
                            if r["expectation"] == "healthy"]),
            "witness_failure": len([r for r in rows
                                    if r["expectation"] == "witness_failure"]),
            "controls": len([r for r in rows
                             if r["expectation"] == "timeout_control"]),
            "pipe_timeout_controls": len([r for r in rows
                                          if r["expectation"] ==
                                          "pipe_timeout_control"]),
            "pipe_setup_failures": len([r for r in rows
                                        if r["expectation"] ==
                                        "pipe_setup_failure"]),
            "interactive_inflight": len([r for r in rows
                                         if r["expectation"] ==
                                         "interactive_inflight"]),
            "timeouts": len([r for r in rows if r["timed_out"]]),
            "failures": len(failures),
        },
        "rows": rows,
        "failures": failures,
        "notes": [
            "The child performs no pipe/file I/O for the witness; the parent "
            "reads a pre-created shared-memory event ring after process exit.",
            "A child terminated by the harness timeout is a FAIL, not a "
            "successful fail-closed result; the sensitivity control proves the "
            "timeout detection works on this machine.",
            "Logging is NOT suppressed in blocked-pipe runs: the child runs with "
            "its normal logger, and the pipes are switched to a full pipe at the "
            "coordinated child label. Any diagnostic sink in the critical "
            "interval would therefore hang the child and be reported as FAIL.",
            "Clean shutdown runs are not part of the blocked-pipe matrix because "
            "their post-idle diagnostics are allowed to block; the blocked "
            "shutdown cases inject a GPU wait failure and end at the terminal "
            "boundary instead.",
            "submit_count/last_submitted_list are written only after the real "
            "ID3D12CommandQueue::ExecuteCommandLists returned; the pre-submit "
            "negative control shows submit_count == 0.",
            "Real hardware device removal/OOM and a physical GPU hot-unplug are "
            "NOT RUN.",
        ],
    }
    OUT_JSON.write_text(json.dumps(payload, indent=2) + "\n",
                        encoding="utf-8")
    lines = ["V-WEATHER-01C-B-R4 non-blocking terminal matrix", "=" * 60, ""]
    lines.append(f"Result: {payload['result']}")
    counts = payload["counts"]
    lines.append(f"cases={counts['cases']} terminal={counts['terminal']} "
                 f"blocked_terminal={counts['blocked_pipe_terminal']} "
                 f"timeouts={counts['timeouts']} failures={counts['failures']}")
    lines.append("")
    for row in rows:
        lines.append(
            f"{row['case']:40} exit={row['exit_code']:6d} "
            f"timeout={int(row['timed_out'])} submits={row['submit_count']:2d} "
            f"{row['result']}")
        for key, value in row["checks"].items():
            lines.append(f"  {key}={value}")
    if failures:
        lines.append("FAILURES:")
        lines.extend(" - " + failure for failure in failures)
    OUT_TXT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(payload["result"], "cases", len(rows), "failures", len(failures))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
