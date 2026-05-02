param(
    [string]$SourceDir = (Join-Path $PSScriptRoot "..\src"),
    [string]$OutDir = (Join-Path $PSScriptRoot "..\build")
)

$ErrorActionPreference = "Stop"

$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

$vcRoots = @()
if (${env:ProgramFiles(x86)}) {
    $vcRoots += Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\BuildTools\VC"
    $vcRoots += Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\Community\VC"
}
if ($env:ProgramFiles) {
    $vcRoots += Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\BuildTools\VC"
    $vcRoots += Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Community\VC"
}

$vcvars = $null
foreach ($root in $vcRoots) {
    $candidate = Join-Path $root "Auxiliary\Build\vcvars32.bat"
    if (Test-Path -LiteralPath $candidate) {
        $vcvars = $candidate
        break
    }
}

if (!$vcvars) {
    throw "Could not find Visual Studio 2022 vcvars32.bat. Install Visual Studio 2022 Build Tools with the C++ toolchain."
}

$cmd = @"
call "$vcvars"
cd /d "$SourceDir"
cl /nologo /LD /EHsc /O2 /W4 /DWIN32 /D_WINDOWS SrsOwnedWindowProxy.cpp /link /DEF:dinput8.def /OUT:"$OutDir\dinput8.dll" /IMPLIB:"$OutDir\SrsOwnedWindowProxy.lib" /MACHINE:X86 user32.lib kernel32.lib gdi32.lib dxguid.lib
"@

$bat = Join-Path $OutDir "build-owned-window-proxy.bat"
Set-Content -LiteralPath $bat -Value $cmd -Encoding ASCII
& cmd.exe /c "`"$bat`""
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Get-Item -LiteralPath (Join-Path $OutDir "dinput8.dll") | Select-Object FullName,Length,LastWriteTime
Get-FileHash -LiteralPath (Join-Path $OutDir "dinput8.dll") -Algorithm SHA256
