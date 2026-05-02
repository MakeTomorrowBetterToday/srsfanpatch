param(
    [string]$BinDir = "",
    [ValidateSet("Prompt", "Widescreen", "Letterbox")]
    [string]$Mode = "Prompt",
    [int]$BackBufferWidth = 2048,
    [int]$BackBufferHeight = 1536,
    [switch]$NoPause
)

$ErrorActionPreference = "Stop"

$PatchHash = "D6E197BC8A614FB6EED01001311D8635E956783414722EFAABC7434F467E37E3"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$PatchDll = Join-Path $Root "patch\dinput8.dll"
$AssetDir = Join-Path $Root "srs-modern-patch-assets"
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

function Resolve-SelectedSrsBinDir {
    param([string]$Selected)

    if ([string]::IsNullOrWhiteSpace($Selected)) { return "" }
    $trimmed = $Selected.Trim('"')
    if (Test-Path -LiteralPath (Join-Path $trimmed "SRS.EXE")) {
        return $trimmed
    }

    $bin = Join-Path $trimmed "Bin"
    if (Test-Path -LiteralPath (Join-Path $bin "SRS.EXE")) {
        return $bin
    }

    return $trimmed
}

function Select-SrsBinDir {
    $shell = New-Object -ComObject Shell.Application
    $folder = $shell.BrowseForFolder(0, "Select your Street Racing Syndicate Bin folder", 0)
    if (!$folder) {
        throw "No SRS folder selected."
    }
    return Resolve-SelectedSrsBinDir $folder.Self.Path
}

function Resolve-SrsBinDir {
    param([string]$Requested)

    if (![string]::IsNullOrWhiteSpace($Requested)) {
        return Resolve-SelectedSrsBinDir $Requested
    }

    $candidates = @(Get-SrsBinCandidates)
    if ($candidates.Count -eq 1) {
        Write-Host "Detected SRS install:"
        Write-Host "  $($candidates[0])"
        return $candidates[0]
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

    Write-Host "No SRS install was auto-detected. Opening folder picker..."
    return Select-SrsBinDir
}

Write-Host "SRS Modern Patch installer"
Write-Host ""

if (!(Test-Path -LiteralPath $PatchDll)) {
    throw "Missing patch DLL: $PatchDll"
}

if (!(Test-Path -LiteralPath $AssetDir)) {
    throw "Missing patch assets folder: $AssetDir"
}

foreach ($AssetName in $AssetNames) {
    $AssetPath = Join-Path $AssetDir $AssetName
    if (!(Test-Path -LiteralPath $AssetPath)) {
        throw "Missing patch prompt asset: $AssetPath"
    }
}

$ActualHash = (Get-FileHash -LiteralPath $PatchDll -Algorithm SHA256).Hash
if ($ActualHash -ne $PatchHash) {
    throw "Patch DLL hash mismatch. Expected $PatchHash but found $ActualHash"
}

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
        throw "SRS is running. Close it before installing."
    }
    Write-Host "SRS is running. Please close the game, then press Enter."
    Read-Host | Out-Null
}

$Running = Get-Process -Name SRS -ErrorAction SilentlyContinue
if ($Running) {
    throw "SRS is still running. Close it before installing."
}

$TargetDll = Join-Path $BinDir "dinput8.dll"
$BackupRoot = Join-Path $BinDir "srs-modern-patch-backups"
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$BackupDir = Join-Path $BackupRoot $Stamp
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null

if (Test-Path -LiteralPath $TargetDll) {
    $ExistingHash = (Get-FileHash -LiteralPath $TargetDll -Algorithm SHA256).Hash
    if ($ExistingHash -ne $PatchHash) {
        Copy-Item -LiteralPath $TargetDll -Destination (Join-Path $BackupDir "dinput8.dll") -Force
        Set-Content -LiteralPath (Join-Path $BackupDir "existing-dinput8.sha256.txt") -Value $ExistingHash -Encoding ASCII
        Write-Host "Backed up existing dinput8.dll to:"
        Write-Host "  $BackupDir"
    } else {
        Set-Content -LiteralPath (Join-Path $BackupDir "already-installed.txt") -Value "The same SRS Modern Patch DLL was already installed." -Encoding ASCII
    }
} else {
    Set-Content -LiteralPath (Join-Path $BackupDir "no-existing-dinput8.txt") -Value "No dinput8.dll was present before install." -Encoding ASCII
}

Copy-Item -LiteralPath $PatchDll -Destination $TargetDll -Force
$TargetAssetDir = Join-Path $BinDir "srs-modern-patch-assets"
New-Item -ItemType Directory -Force -Path $TargetAssetDir | Out-Null
foreach ($AssetName in $AssetNames) {
    Copy-Item -LiteralPath (Join-Path $AssetDir $AssetName) -Destination (Join-Path $TargetAssetDir $AssetName) -Force
}
Remove-Item -LiteralPath (Join-Path $BinDir "srs_owned_window.log") -Force -ErrorAction SilentlyContinue

$Ini = Join-Path $BinDir "srs-modern-patch.ini"
Set-Content -LiteralPath $Ini -Value @(
    "[Display]"
    "Mode=$Mode"
    ""
    "[Render]"
    "BackBufferWidth=$BackBufferWidth"
    "BackBufferHeight=$BackBufferHeight"
    ""
    "; Mode can be Prompt, Widescreen, or Letterbox."
    "; Prompt asks at startup for this launch."
    "; Widescreen fills the owned borderless view."
    "; Letterbox keeps a centered 4:3 frame with black bars."
    "; The default render override matches the high 2048-class SRS profile canvas."
) -Encoding ASCII

Write-Host ""
Write-Host "Installed SRS Modern Patch."
Write-Host "Target: $TargetDll"
Write-Host "Hash:   $PatchHash"
Write-Host "Mode:   $Mode"
Write-Host "Render: ${BackBufferWidth}x${BackBufferHeight}"
Write-Host "Assets: $TargetAssetDir"
Write-Host ""
Write-Host "Launch Street Racing Syndicate normally through Steam."
if (!$NoPause) {
    Write-Host "Press Enter to exit."
    Read-Host | Out-Null
}
