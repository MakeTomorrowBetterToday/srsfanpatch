Street Racing Syndicate Modern Patch
====================================

Version: 0.2 prompt build
Patch DLL SHA256:
D6E197BC8A614FB6EED01001311D8635E956783414722EFAABC7434F467E37E3

What this does
--------------

This patch installs a local dinput8.dll proxy beside SRS.EXE. It does not
replace SRS.EXE and does not include any game files or game assets.

Current features:

- Real movable borderless/windowed render target.
- Works with Win+Shift+Arrow monitor movement.
- Avoids exclusive-fullscreen monitor mode switching.
- Startup display chooser:
  - Widescreen fills the current borderless view.
  - Letterbox keeps a centered classic 4:3 frame with black bars.
- Keeps keyboard and mouse input routed to the new render window.
- Keeps the Windows key available for normal Windows shortcuts.
- Adds an XInput controller bridge that maps common controller controls to
  keyboard driving/menu controls.
- Uses a 2048x1536 internal render override by default to match the high
  2048-class SRS profile/video setting.

Default controller mapping:

- Right trigger: accelerate / Up
- Left trigger: brake or reverse / Down
- Left stick or D-pad: steer / Left and Right
- A or Start: Enter
- B or Back: Escape
- X or left bumper: Space

Install
-------

1. Close Street Racing Syndicate.
2. Run Install-SRS-Modern-Patch.bat.
3. If the installer does not auto-detect the game, select your SRS Bin folder.

The SRS Bin folder is the folder containing SRS.EXE. For Steam installs, it is
usually under:

<Steam library>\steamapps\common\Street Racing Syndicate\Bin

The installer backs up any existing dinput8.dll into:

<SRS Bin>\srs-modern-patch-backups

After installing, launch Street Racing Syndicate normally through Steam.

Display mode config
-------------------

The installer creates this file next to SRS.EXE:

srs-modern-patch.ini

Default config:

[Display]
Mode=Prompt

[Render]
BackBufferWidth=2048
BackBufferHeight=1536

Valid Display Mode values:

- Prompt
- Widescreen
- Letterbox

Prompt asks at startup for that launch. Widescreen and Letterbox skip the
startup prompt and always use that mode.

Resolution notes
----------------

The patch is not hardcoded to 4K. It queries the current game window/client size
and maps the 2048x1536 render to whatever monitor size Windows gives it.

This means:

- 3840x2160, 2560x1440, 1920x1080, ultrawide, and smaller screens should all be
  handled by the same scaling path.
- Widescreen stretches the render to the whole borderless view.
- Letterbox preserves the source aspect ratio and fills the unused space black.
- Moving the window between monitors should cause the destination rectangle to
  recalculate for that monitor.

The internal render override defaults to 2048x1536 because that matched the high
texture/profile mode during testing. If a machine or driver cannot use that
backbuffer, edit srs-modern-patch.ini and lower BackBufferWidth/BackBufferHeight
to a 4:3 pair such as 1600x1200.

Uninstall
---------

1. Close Street Racing Syndicate.
2. Run Uninstall-SRS-Modern-Patch.bat.

The uninstaller restores the newest backed-up dinput8.dll if one exists. If no
previous dinput8.dll existed, it removes this patch DLL. It also removes this
patch's two prompt images.

Troubleshooting
---------------

The patch writes a log file next to SRS.EXE:

srs_owned_window.log

If the game starts but something does not work, check that log first.

Notes for redistribution
------------------------

Share the whole SRS-Modern-Patch folder or the SRS-Modern-Patch.zip archive.
Do not add SRS.EXE or any original game files to the archive.
