# 3ENotes Rebrand and Arabic Report

## Completed

- Changed the user-visible application and package branding from **SpeedyNote** to **3ENotes** across Windows, Android, Linux, macOS, and iOS packaging surfaces.
- Changed the Qt application identity to organization `3E` and application `3ENotes`.
- Added a one-time migration that imports existing user settings from the legacy `SpeedyNote/App` settings namespace.
- Added Arabic (`ar_SA`) to the language selector as **العربية**.
- Added right-to-left application layout for Arabic while explicitly keeping the document canvas left-to-right so drawing coordinates are not mirrored.
- Added Arabic-aware font fallbacks on Windows, Android, macOS, and iOS.
- Added `qtbase_ar.qm` to desktop and mobile translation deployment lists.
- Added `resources/translations/app_ar.ts` and registered `app_ar.qm` in the Qt resource system.
- Preserved GPLv3 attribution and added/updated `UPSTREAM.md`.
- Added `docs/IDENTIFIER-MIGRATION.md` for the future identifier migration.

## Arabic coverage

- Total messages: **987**
- Translated messages: **891**
- Unfinished messages: **96**
- Placeholder validation errors: **0**

The unfinished entries are primarily long technical, diagnostic, and CLI help text. Qt will safely fall back to English for them.

## Compatibility identifiers retained intentionally

The following internal identifiers remain unchanged in this first release to avoid breaking builds, upgrades, file associations, Android JNI, Apple signing, Linux packages, and existing automation:

- CMake target and executable: `speedynote` / `speedynote.exe`
- CLI command: `speedynote`
- Android package and Java/JNI namespace: `org.speedynote.app`
- Existing Apple bundle identifiers
- Linux desktop/Flatpak IDs: `org.speedynote.SpeedyNote`
- Legacy IPC name and old settings namespace, used only for compatibility/migration

These retained references are listed and classified in `rebrand-audit.txt`.

## Validation performed

- Parsed all application `.ts` translation files as XML.
- Parsed `resources.qrc`, Android manifest template, iOS plists, and AppStream metadata as XML.
- Parsed every GitHub Actions workflow as YAML.
- Ran `bash -n` against Linux, Android, macOS, and iOS shell scripts after normalizing Windows line endings in temporary copies.
- Checked Arabic placeholders (`%1`, `%2`, `%n`, etc.) against source strings: no mismatches.
- Ran CMake configuration parsing. It reached dependency discovery successfully and stopped only because Qt 6 is not installed in this validation container; the GitHub Actions build is the authoritative compilation test.

## Recommended cloud checks

Run the Windows and Android workflows against the rebrand branch. Do not merge into `main` until both artifacts launch and the Arabic selector/RTL layout have been tested.
