# Runs only from the NSIS installer after the new payload has replaced the old one.
$ErrorActionPreference = 'Stop'
$exe = Join-Path $env:LOCALAPPDATA 'Programs\ChoscorDB\choscordb.exe'
$ready = Join-Path $env:TEMP ("ChoscorDB-update-ready-" + [Guid]::NewGuid().ToString('N') + '.txt')
$child = $null
$accepted = $false
try {
    if (Test-Path -LiteralPath $ready) { exit 2 }
    $env:CHOSCORDB_UPDATE_READY_FILE = $ready
    $child = Start-Process -FilePath $exe -PassThru
    Remove-Item Env:CHOSCORDB_UPDATE_READY_FILE -ErrorAction SilentlyContinue
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($child.HasExited) { break }
        if (Test-Path -LiteralPath $ready) {
            $item = Get-Item -LiteralPath $ready
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { break }
            if ([IO.File]::ReadAllText($ready) -cne "ready`n") { break }
            Start-Sleep -Seconds 2
            if (-not $child.HasExited -and (Test-Path -LiteralPath $ready)) {
                $item = Get-Item -LiteralPath $ready
                if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0 -and
                    [IO.File]::ReadAllText($ready) -ceq "ready`n") {
                    $accepted = $true
                }
            }
            break
        }
        Start-Sleep -Milliseconds 200
    }
} catch {
    $accepted = $false
} finally {
    Remove-Item Env:CHOSCORDB_UPDATE_READY_FILE -ErrorAction SilentlyContinue
    if (-not $accepted -and $null -ne $child) {
        try {
            if (-not $child.HasExited) {
                Stop-Process -Id $child.Id -Force -ErrorAction Stop
                $child.WaitForExit(5000) | Out-Null
            }
        } catch {
            # NSIS verifies rollback and leaves .previous intact if files stay locked.
        }
    }
    Remove-Item -LiteralPath $ready -Force -ErrorAction SilentlyContinue
}
if ($accepted) { exit 0 }
exit 2
