# srsfanpatch
This is a fan and AI made modernization patch for the game Street Racing Syndicate, I am one person, not a studio of developers, there will be bugs, I did not write the code, AI did. If it works for you please share it, otherwise report any bugs, there shouldn't be anything bad, AI made it so proceed at your own risk and evaluate the code, enjoy!


# Street Racing Syndicate Modern Patch

Modern borderless-window patch for the Windows/Steam version of **Street Racing Syndicate**.

This repository does not contain `SRS.EXE`, original game assets, or any files from the commercial game. The downloadable patch is a local `dinput8.dll` proxy plus installer scripts and config.

## Features

- Real movable borderless/windowed render target.
- Works with `Win+Shift+Arrow` monitor movement.
- Avoids exclusive-fullscreen monitor mode switching.
- Startup display chooser:
  - `Widescreen` fills the current borderless view.
  - `Letterbox` keeps a centered classic 4:3 frame with black bars.
  - The chooser uses two scrubbed image buttons with no text prompt.
- Keyboard and mouse input routed to the owned render window.
- Windows key remains available for normal Windows shortcuts.
- XInput controller bridge for common driving/menu controls.
- Default `2048x1536` internal render override for the high 2048-class SRS video/profile mode.

## Download

The repository itself includes the installer at the root:

- `Install-SRS-Modern-Patch.bat`
- `Uninstall-SRS-Modern-Patch.bat`
- `patch/dinput8.dll`
- `srs-modern-patch-assets/`

There is also a packaged release zip:

- [`release/SRS-Modern-Patch.zip`](release/SRS-Modern-Patch.zip)

For the simplest install, download the repository or release zip, extract it, run `Install-SRS-Modern-Patch.bat`, and launch the game normally through Steam.

## Install

1. Close Street Racing Syndicate.
2. Download and extract either the repository zip or `release/SRS-Modern-Patch.zip`.
3. Run `Install-SRS-Modern-Patch.bat`.
4. If the installer does not auto-detect the game, enter the folder containing `SRS.EXE`.

For Steam installs, the target folder is usually:

```text
<Steam library>\steamapps\common\Street Racing Syndicate\Bin
```

The installer backs up any existing `dinput8.dll` into:

```text
<SRS Bin>\srs-modern-patch-backups
```

## Configuration

The installer writes `srs-modern-patch.ini` next to `SRS.EXE`.

Default:

```ini
[Display]
Mode=Prompt

[Render]
BackBufferWidth=2048
BackBufferHeight=1536
```

`Mode` values:

- `Prompt`: ask at startup for this launch.
- `Widescreen`: fill the owned borderless view.
- `Letterbox`: preserve a centered 4:3 frame with black bars.

If a machine or driver cannot use the default `2048x1536` backbuffer, try a lower 4:3 pair such as `1600x1200`.

## Build From Source

Requirements:

- Windows
- Visual Studio 2022 Build Tools with the C++ toolchain
- PowerShell

Build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-SrsModernPatch.ps1
```

The built DLL will be written to `build\dinput8.dll`.

## Repository Layout

- `src/`: C++ proxy source and module definition file.
- `tools/`: build helper scripts.
- `package/`: unpacked redistributable package.
- `release/`: ready-to-share release zip.
- `docs/`: install, release, and privacy-audit notes.
- `patch/`: root-level copy of the release DLL used by the root installer.

## Notes

This is a community patch and is not affiliated with or endorsed by the game's publishers, developers, or rights holders. It is intended for legally owned copies of Street Racing Syndicate.
