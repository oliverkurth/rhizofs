# Changelog

## Unreleased

- Network listeners now require CurveZMQ encryption (`--encrypt`) and a
  client key allowlist (`--authorized-keys-file`). This changes startup for
  existing TCP and other network deployments without authentication. Configure
  both options or explicitly use `-I` / `--insecure` on an independently
  trusted network. Only `ipc://` and `inproc://` remain exempt by default.
  The bundled systemd and launchd templates already supply both options.
- Support chmod with older glibc by changing the checked inode through
  `/proc/self/fd` when native no-follow chmod is unsupported. If neither
  method is available, fail closed rather than retrying by name and risking
  a symlink-swap escape from the shared directory.
