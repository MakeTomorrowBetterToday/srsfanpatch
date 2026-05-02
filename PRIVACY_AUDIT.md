# Privacy Audit

The public release package was scanned for local machine identifiers before being staged here.

Checked:

- packaged text files
- raw zip bytes
- DLL ASCII strings
- DLL UTF-16 strings
- BMP prompt asset bytes
- PDB/source markers such as `.pdb`, `RSDS`, and source filename paths
- zip entry names and comments

Patterns checked included local drive roots, user folders, workspace names, dated workspace paths, profile/cache folders, cloud-sync folders, and project scratch-folder names.

Result:

- No local user paths or usernames were found.
- No original SRS game files are included.
- Zip entries are relative package paths.
- Prompt images were re-encoded as BMPs and scrubbed before packaging.

The release package contains only:

- installer batch/script
- uninstaller batch/script
- README
- patch DLL
- scrubbed prompt BMP assets

The repository root also includes a copy of the installer/uninstaller and
`patch/dinput8.dll`, plus `srs-modern-patch-assets`, so GitHub source-zip
downloads are directly installable.
