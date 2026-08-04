# 3ENotes feature bundle

## Laser pointer

A presentation-only **Laser Pointer** tool has been added to the main toolbar.
It never creates document strokes, never enters Undo/Redo, and is not included
in saved projects or exported PDFs.

Features:

- Red laser point with a white center and soft glow.
- Trail length adjustable from 0 to 20 cm.
- Independent point size and trail thickness.
- Configurable stay time and fade duration.
- The completed shape stays intact until the configured timer starts, allowing
  boxes, circles, and underlines to be completed before fading.
- Optional press-only operation.
- Optional click/press pulse.
- Optional spotlight mode with configurable radius.
- Mouse, stylus hover/press, and one-finger touch support.
- Two-finger navigation remains available on touch devices.
- Default shortcut: `P`.
- `Escape` clears the current laser point, trail, pulse, and spotlight immediately.
- Settings persist globally between sessions.

## Portable projects

- New default portable save/export extension: `.3enotes`.
- Legacy `.snbx` import remains supported.
- Added archive metadata in `3enotes-package.json`.
- Added Windows, Android, iOS/iPadOS, and Linux file type integration.
- Android external-open intents are copied safely from `content://` storage and
  forwarded to the existing project importer.

## Branding and icon

The approved navy 3ENotes notebook/pencil icon is included for Windows, Android,
iOS generation, Linux, and in-app windows.

## Build policy

Windows, Android, Linux, macOS, and iOS workflows remain manual for branch
builds. They run automatically only for version tags. This allows several
changes to be collected before spending time on full cross-platform builds.

## Validation performed before delivery

- Git whitespace check.
- XML parsing for resources, Arabic TS, Android manifest, MIME metadata, and
  Linux metainfo.
- Apple plist parsing.
- GitHub Actions YAML parsing.
- Arabic placeholder consistency review, excluding valid Qt plural forms.
- File-format and tool wiring review across desktop and mobile input paths.

Full native compilation still needs the GitHub Actions runners for each target.
