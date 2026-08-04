# 3ENotes portable project format

## Extension and MIME type

- Preferred extension: `.3EN`
- MIME type: `application/x-3enotes`
- Uniform type identifier (Apple platforms): `com.3e.3enotes.project`
- Format version: `1`

A `.3EN` file is a ZIP-compatible portable project container. It contains
one internal `.snb` notebook bundle, including its `document.json`, pages,
strokes, objects, embedded images, and bundled PDF material when requested.

The archive root also contains `3enotes-package.json`:

```json
{
  "format": "3ENotes",
  "format_version": 1,
  "application_version": "1.x.x",
  "created_utc": "2026-08-04T00:00:00Z",
  "bundle": "Notebook.snb"
}
```

## Compatibility

The internal notebook representation intentionally remains `.snb`. This keeps
existing documents and the current editor architecture compatible while making
`.3EN` the preferred single-file interchange format.

3ENotes continues to read legacy `.snbx` packages. A current client can open a
`.3EN` project on Windows, Android, iOS/iPadOS, macOS, and Linux because the
archive content is platform-neutral and uses relative paths.

## Platform integration

- Windows installer registers `.3EN` and opens it with 3ENotes.
- Android registers the custom MIME type, accepts the extension from the system
  file picker, and copies external `content://` URIs into private storage before
  import.
- iOS/iPadOS declares the exported UTI and accepts files from Files and the share
  sheet.
- Linux installs a shared MIME definition and desktop association.

## Forward compatibility

Readers should ignore unknown files at the archive root. Future format versions
may add optional metadata while retaining the contained `.snb` bundle. Clients
must inspect `format_version` before relying on new fields.


## Backward compatibility

3ENotes continues to open legacy `.3enotes` and `.snbx` packages. New exports use the shorter `.3EN` extension.
