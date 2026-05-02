param(
    [string]$BinDir = "",
    [switch]$NoPause
)

$ErrorActionPreference = "Stop"

$PatchHash = "D6E197BC8A614FB6EED01001311D8635E956783414722EFAABC7434F467E37E3"
$AssetNames = @("letterbox.bmp", "widescreen.bmp")

function Add-Candidate {
    param(
        [System.Collections.Generic.List[string]]$List,
        [string]$Path
    )
    if ([string]::IsNullOrWhiteSpace($Path)) { return }
    $normalized = $Path.Trim('"')
    if ($List -notcontains $normalized) {
        [void]$List.Add($normalized)
    }
}

function Get-SteamRoots {
    $roots = New-Object "System.Collections.Generic.List[string]"
    $steamRoots = @()
    if (${env:ProgramFiles(x86)}) { $steamRoots += (Join-Path ${env:ProgramFiles(x86)} "Steam") }
    if ($env:ProgramFiles) { $steamRoots += (Join-Path $env:ProgramFiles "Steam") }

    foreach ($steamRoot in $steamRoots) {
        Add-Candidate $roots $steamRoot
        $libraryFile = Join-Path $steamRoot "steamapps\libraryfolders.vdf"
        if (!(Test-Path -LiteralPath $libraryFile)) { continue }

        foreach ($line in Get-Content -LiteralPath $libraryFile) {
            if ($line -match '^\s*"\d+"\s+"([^"]+)"') {
                Add-Candidate $roots ($Matches[1] -replace '\\\\', '\')
            } elseif ($line -match '^\s*"path"\s+"([^"]+)"') {
                Add-Candidate $roots ($Matches[1] -replace '\\\\', '\')
            }
        }
    }
    return $roots
}

function Get-SrsBinCandidates {
    $candidates = New-Object "System.Collections.Generic.List[string]"

    foreach ($root in Get-SteamRoots) {
        Add-Candidate $candidates (Join-Path $root "steamapps\common\Street Racing Syndicate\Bin")
    }

    return @($candidates | Where-Object { Test-Path -LiteralPath (Join-Path $_ "SRS.EXE") })
}

function Resolve-SrsBinDir {
    param([string]$Requested)

    if (![string]::IsNullOrWhiteSpace($Requested)) {
        return $Requested.Trim('"')
    }

    $candidates = @(Get-SrsBinCandidates)
    if ($candidates.Count -eq 1) {
        $answer = Read-Host "SRS Bin folder [$($candidates[0])]"
        if ([string]::IsNullOrWhiteSpace($answer)) { return $candidates[0] }
        return $answer.Trim('"')
    }

    if ($candidates.Count -gt 1) {
        Write-Host "Detected SRS installs:"
        for ($i = 0; $i -lt $candidates.Count; ++$i) {
            Write-Host ("  {0}. {1}" -f ($i + 1), $candidates[$i])
        }
        $choice = Read-Host "Choose install [1]"
        if ([string]::IsNullOrWhiteSpace($choice)) { return $candidates[0] }
        $index = 0
        if ([int]::TryParse($choice, [ref]$index) -and $index -ge 1 -and $index -le $candidates.Count) {
            return $candidates[$index - 1]
        }
    }

    return (Read-Host "Enter your Street Racing Syndicate Bin folder").Trim('"')
}

Write-Host "SRS Modern Patch uninstaller"
Write-Host ""

$BinDir = Resolve-SrsBinDir $BinDir
if (!(Test-Path -LiteralPath $BinDir)) {
    throw "Folder does not exist: $BinDir"
}

$Exe = Join-Path $BinDir "SRS.EXE"
if (!(Test-Path -LiteralPath $Exe)) {
    throw "This does not look like the SRS Bin folder; missing SRS.EXE: $Exe"
}

$Running = Get-Process -Name SRS -ErrorAction SilentlyContinue
if ($Running) {
    if ($NoPause) {
        throw "SRS is running. Close it before uninstalling."
    }
    Write-Host "SRS is running. Please close the game, then press Enter."
    Read-Host | Out-Null
}

$Running = Get-Process -Name SRS -ErrorAction SilentlyContinue
if ($Running) {
    throw "SRS is still running. Close it before uninstalling."
}

$TargetDll = Join-Path $BinDir "dinput8.dll"
$Ini = Join-Path $BinDir "srs-modern-patch.ini"
$Log = Join-Path $BinDir "srs_owned_window.log"
$Assets = Join-Path $BinDir "srs-modern-patch-assets"
$BackupRoot = Join-Path $BinDir "srs-modern-patch-backups"
$LatestBackup = $null
if (Test-Path -LiteralPath $BackupRoot) {
    $LatestBackup = Get-ChildItem -LiteralPath $BackupRoot -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "dinput8.dll") } |
        Sort-Object Name -Descending |
        Select-Object -First 1
}

if ($LatestBackup) {
    Copy-Item -LiteralPath (Join-Path $LatestBackup.FullName "dinput8.dll") -Destination $TargetDll -Force
    Write-Host "Restored previous dinput8.dll from:"
    Write-Host "  $($LatestBackup.FullName)"
} elseif (Test-Path -LiteralPath $TargetDll) {
    $CurrentHash = (Get-FileHash -LiteralPath $TargetDll -Algorithm SHA256).Hash
    if ($CurrentHash -eq $PatchHash) {
        Remove-Item -LiteralPath $TargetDll -Force
        Write-Host "Removed SRS Modern Patch dinput8.dll."
    } else {
        Write-Host "Current dinput8.dll does not match this patch hash, so it was left untouched:"
        Write-Host "  $TargetDll"
    }
} else {
    Write-Host "No dinput8.dll found to uninstall."
}

Remove-Item -LiteralPath $Ini -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $Log -Force -ErrorAction SilentlyContinue
foreach ($AssetName in $AssetNames) {
    Remove-Item -LiteralPath (Join-Path $Assets $AssetName) -Force -ErrorAction SilentlyContinue
}
if (Test-Path -LiteralPath $Assets) {
    $RemainingAssets = @(Get-ChildItem -LiteralPath $Assets -Force -ErrorAction SilentlyContinue)
    if ($RemainingAssets.Count -eq 0) {
        Remove-Item -LiteralPath $Assets -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ""
Write-Host "Uninstall complete."
if (!$NoPause) {
    Write-Host "Press Enter to exit."
    Read-Host | Out-Null
}
