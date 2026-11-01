# Shared status contract for the two verifier harnesses.
# A baseline is valid only when exit=0 and both statuses are PASS.
# A negative case is valid only when exit=3, evidence=FAIL and acceptance=UNVERIFIED.

function Invoke-VerifierProcess {
    param(
        [Parameter(Mandatory=$true)][string]$Exe,
        [Parameter(Mandatory=$true)][string]$Directory,
        [Parameter(Mandatory=$true)][string]$LogPath
    )
    $output = & $Exe --verify-evidence $Directory 2>&1 | Out-String
    $exitCode = $LASTEXITCODE
    if ($null -eq $exitCode) { $exitCode = -1 }
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($LogPath, $output, $utf8)
    # A verdict is a unique structured line.  Duplicate, empty or unknown
    # statuses are contract violations, not first/last-wins input.
    $evidenceMatches = [regex]::Matches(
        $output, '(?m)^evidence_status=([^\r\n]*)\r?$')
    $acceptanceMatches = [regex]::Matches(
        $output, '(?m)^acceptance_status=([^\r\n]*)\r?$')
    $evidence = if ($evidenceMatches.Count -eq 1) {
        $evidenceMatches[0].Groups[1].Value.Trim()
    } else { '' }
    $acceptance = if ($acceptanceMatches.Count -eq 1) {
        $acceptanceMatches[0].Groups[1].Value.Trim()
    } else { '' }
    [pscustomobject]@{
        Exit = [int]$exitCode
        Evidence = $evidence
        Acceptance = $acceptance
        EvidenceCount = $evidenceMatches.Count
        AcceptanceCount = $acceptanceMatches.Count
        Output = $output
    }
}

function Test-BaselineResult {
    param([Parameter(Mandatory=$true)]$Result)
    return $Result.Exit -eq 0 -and $Result.Evidence -eq 'PASS' -and
           $Result.Acceptance -eq 'PASS'
}

function Test-NegativeResult {
    param([Parameter(Mandatory=$true)]$Result)
    return $Result.Exit -eq 3 -and $Result.Evidence -eq 'FAIL' -and
           $Result.Acceptance -eq 'UNVERIFIED'
}
