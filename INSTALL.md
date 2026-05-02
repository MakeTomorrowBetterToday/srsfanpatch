# Install Notes

The installer is available at the repository root.

User flow:

1. Extract the repository zip.
2. Run `Install-SRS-Modern-Patch.bat`.
3. Let the installer auto-detect the Steam install or select the folder containing `SRS.EXE`.
4. Launch Street Racing Syndicate normally.

The patch installs these files next to `SRS.EXE`:

- `dinput8.dll`
- `srs-modern-patch.ini`
- `srs-modern-patch-assets\letterbox.bmp`
- `srs-modern-patch-assets\widescreen.bmp`

The installer backs up an existing `dinput8.dll` to:

```text
<SRS Bin>\srs-modern-patch-backups
```

Uninstall with `Uninstall-SRS-Modern-Patch.bat`.
