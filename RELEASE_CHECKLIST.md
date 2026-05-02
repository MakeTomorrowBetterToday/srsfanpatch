# Release Checklist

Before publishing a release:

- Build `src/SrsOwnedWindowProxy.cpp` with `tools/Build-SrsModernPatch.ps1`.
- Copy the release DLL into `package/SRS-Modern-Patch/patch/dinput8.dll`.
- Copy the release DLL into root `patch/dinput8.dll`.
- Copy scrubbed prompt BMP assets into `package/SRS-Modern-Patch/srs-modern-patch-assets`.
- Copy scrubbed prompt BMP assets into root `srs-modern-patch-assets`.
- Copy the current installer/uninstaller files to the repository root.
- Update `package/SRS-Modern-Patch/README.txt` if behavior changed.
- Rebuild `release/SRS-Modern-Patch.zip` from `package/SRS-Modern-Patch`.
- Verify the zip contains no original game files.
- Verify the zip contains no local paths, usernames, PDB paths, or workspace names.
- Install into a temporary dummy `Bin` folder containing a placeholder `SRS.EXE`.
- Install into a real local SRS install for gameplay smoke testing.

Current release DLL SHA256:

```text
D6E197BC8A614FB6EED01001311D8635E956783414722EFAABC7434F467E37E3
```

Current release zip SHA256:

```text
5A5E5BEF09A1342441CD87ACB1210EDBE443CA9EFD9A16D1373AEDC46F644874
```
