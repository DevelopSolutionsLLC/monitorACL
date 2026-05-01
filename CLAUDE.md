# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A C++ daemon that uses Linux `inotify` to recursively watch directories for new file/folder creation and automatically applies POSIX ACLs via `setfacl`. Built to fix Active Directory permission inheritance issues on QNAP NAS devices.

## How it works

1. `getDirList()` recursively enumerates all subdirectories under the target path
2. `inotify_add_watch()` registers each directory for `IN_CREATE` events
3. Main loop blocks on `read(fd, ...)` waiting for inotify events
4. On file/folder creation: `setFileACL()` runs `setfacl -R` on the parent directory
5. New subdirectories are dynamically added to the watch list

## Build

```bash
g++ -o monitorACL main.cpp
```

No external dependencies beyond standard Linux headers (`sys/inotify.h`, `dirent.h`).

## Usage

```bash
# Foreground
./monitorACL /path/to/watch

# Daemon mode — forks to background, writes PID to /var/run/monitorACL.pid
./monitorACL -d /path/to/watch

# Stop the daemon
kill $(cat /var/run/monitorACL.pid)
```

Typically launched from QNAP's `autostart.sh` with `-d` to persist across reboots.

## Platform

Linux only — requires `inotify` (not available on macOS/Windows). Originally deployed on QNAP NAS running Entware for the build toolchain.

## Key details

- ACL template is hardcoded in `setFileACL()`: sets rwx for `AD\administrator`, `AD\domain users`, and `AD\domain admins`
- Uses `system()` to shell out to `setfacl`
- `folderTracker` vector maps inotify watch descriptors to directory paths
- The watch descriptor index has a +1 offset (`event->wd+1`) due to the initial vector size of 2
- `-d` flag daemonizes via `fork()/setsid()`, writes PID to `/var/run/monitorACL.pid`
- SIGTERM/SIGINT trigger clean shutdown: removes watches, closes fd, deletes PID file
