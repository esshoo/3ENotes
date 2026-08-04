# Identifier migration plan

The first 3ENotes rebrand changes all user-visible names while preserving several internal SpeedyNote identifiers for compatibility.

Temporarily retained identifiers include:

- CMake target and desktop executable: `speedynote` / `speedynote.exe`
- Android package and Java activity: `org.speedynote.app` / `SpeedyNoteActivity`
- Apple bundle identifier: `org.speedynote.speedynote`
- Linux desktop, AppStream, and Flatpak IDs: `org.speedynote.SpeedyNote`
- Notebook file extensions: `.snb` and `.snbx`

Changing these requires a dedicated migration release covering application upgrades, stored settings, file associations, signing/provisioning, Android application IDs, and store identities.
