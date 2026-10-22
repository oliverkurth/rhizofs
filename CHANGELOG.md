# Changelog

## Unreleased

- Network listeners now require CurveZMQ encryption (`--encrypt`) and a
  client key allowlist (`--authorized-keys-file`). This changes startup for
  existing TCP and other network deployments without authentication. Configure
  both options or explicitly use `-I` / `--insecure` on an independently
  trusted network. Only `ipc://` and `inproc://` remain exempt by default.
  The bundled systemd and launchd templates already supply both options.
