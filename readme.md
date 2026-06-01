# ZiSync

Cross-platform peer-to-peer file synchronization engine (C++11).

ZiSync is a file sync kernel that synchronizes folders across devices on the same LAN or over the internet. It was originally built as the sync core for the ZiSync / Plusync desktop and mobile apps (~2014–2015).

## What it does

- **Peer-to-peer folder sync** — devices discover each other on the LAN or via a tracker, then sync file trees directly without a cloud relay.
- **Phone backup** — Android and iOS devices can back up files to a desktop.
- **Shared syncs** — sync folders can be shared with other devices with read/write permissions.
- **On-demand download/upload** — individual files can be pulled or pushed outside of automatic sync.

## Supported platforms

| Platform   | Build system              |
|------------|---------------------------|
| Linux      | autotools (GCC/Clang)     |
| macOS      | autotools (GCC/Clang)     |
| Windows    | MSVC (`.sln`)             |
| Android    | NDK cross-compile         |
| iOS        | Xcode project             |

## Architecture

```
┌─────────────────────────────────────────┐
│  IZiSyncKernel — public C API           │
├─────────────────────────────────────────┤
│  Router  (ZMQ DEALER/ROUTER)            │
│  ├─ Inner workers   (local FS ops)      │
│  ├─ Outer workers   (network transfer)  │
│  ├─ Refresh workers (tree diff/index)   │
│  └─ Sync workers    (sync scheduling)   │
├─────────────────────────────────────────┤
│  libevent (async I/O, HTTP, transfer)   │
│  SQLite + SQLCipher (metadata store)    │
│  ZMQ (in-process message bus)           │
│  OpenSSL (TLS)                          │
│  Protocol Buffers (wire format)         │
└─────────────────────────────────────────┘
```

Key subsystems:

- **Monitor** — watches local directories for changes (inotify, FSEvents, ReadDirectoryChangesW).
- **Transfer** — packs files into tar archives for transmission, with resume support.
- **Discovery** — finds peers on the LAN via UDP broadcast and remotely via a tracker.
- **Database** — stores sync state, file metadata, device info, and permissions in encrypted SQLite.
- **History** — logs file operations (add, delete, modify, rename) with timestamps.

## Building

```bash
autoreconf -i
./configure [--enable-debug] [--enable-test]
make
```

For Android:
```bash
./build_android.sh
```

For iOS: open `xcode/ZiSyncKernel.xcodeproj`.

## Dependencies

- ZeroMQ (≥ 4.0)
- libevent (≥ 2.0)
- SQLite (≥ 3.7, with encryption extension)
- OpenSSL
- Protocol Buffers
- Google Test (tests only)

Pre-built third-party libraries are expected under `third-part/build/`.

## Origin

This is an early version of the ZiSync kernel (v2.1.1). It was developed during 2014–2015 and is now archived for reference and potential AI-assisted refinement.
