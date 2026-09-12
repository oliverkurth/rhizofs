rhizofs
=======

Introduction
------------

rhizofs is a remote filesystem based upon zeromq and the protobuf serialization
format. It is much simpler as for example NFS or samba, but like them it also
allows mounting a remote directory in a local directory. It aims to be a
lightweight solution to exchange and abandons on quite a few features to
keep it usage as simple as possible.

The project was created by Nico Mandery a few years ago. Nico archived it, and
this project was forked, and the detached from it. The original archive is at
https://github.com/nmandery/rhizofs .

Please note that this filesystem is still pretty in development and lacks
excessive testing.

Features
--------

-   **LZ4 compression**: data transferred during read and write operations will 
    be compressed via the [LZ4](http://code.google.com/p/lz4) algorithm. In 
    the case when this compression is unable to compress data significantly
    (for example data already compressed like JPEG images, zip files, videos),
    the data will be send uncompressed.  

-   **pre-caching of attributes of directory entries**: Reading the contents
    of a directory will also fetch the attributes of these files in the same
    request and store them in a client-side cache. This will greatly reduces the
    number of requests send to the server, which will speed up commands like
    `ls -la` by a great amount. This is especially true when the filesystem
    operates over a slow or/and high latency network connection.

    Cached attributes are reused for a few seconds (see
    `--attr-cache-timeout`), so a client can briefly report outdated
    information for a file that was changed on the server *without* going
    through rhizofs - by another process writing into the shared directory
    directly, for example. Because the cached information includes the file
    size, and the size is what determines where a file ends, a read issued
    in that window can come back **silently truncated** rather than failing.
    If something other than rhizofs modifies the shared directory, mount
    with `--attr-cache-timeout=0` to always ask the server. Like the
    attribute timeouts of other network filesystems (compare NFS' `acregmin`
    / `acregmax`), this trades throughput for immediate consistency.

-   **Encryption and Authentication**: rhizofs can use [CurveZMQ](http://curvezmq.org/) for
    encryption and authentication (ZAP).

-   **the shared directory is a boundary**: the server refuses requests for paths that
    leave the directory it was told to share, whether through a `..` component or by
    following a symlink out of it. Symlinks *inside* the shared directory are followed
    as usual, and a client can still create and read back a symlink pointing anywhere -
    it is resolved on the client, like on any other network filesystem - but the server
    will not follow one out of the share on the client's behalf.

Authentication
--------------

`rhizofs` supports encryption and authentication. Encryption is
based on public and private key pairs.

Authentication is based on the client key, using [ZMQ's ZAP](https://rfc.zeromq.org/spec/27/) protocol.

Authentication requires encryption: the client key only exists when the connection uses
CurveZMQ, so `--authorized-keys-file` has to be combined with `--encrypt` and the server
refuses to start without it. Note that `--encrypt` on its own encrypts the connection but
accepts *any* client key - listing the keys you want to allow in an authorized keys file is
what restricts who may connect.


File ownership and user mapping
-------------------------------

This is also implemented in a pretty simple way. There is no correlation of users
between server and client.

The client will show all files as owned by the user who mounted the filesystem
as long as the user who runs the server process is owner of a file.
Groups are pretty much handled the same way. If the user of the server process
is a member of the group a file belongs to, the client will return the main
group of the user who mounted the filesystem as the group of the file.

Changing ownerships is not supported so far.


Usage
-----

**rhizosrv**

This is the server part of the filesystem. It implements a basic multithreaded server,
which binds to a socket and waits for incoming requests
```
rhizosrv SOCKET DIRECTORY [options]

rhizofs and rhizosrv implement a filesytem which allows mounting
remote directories on the local computer.

This program implements the server.

Parameters
==========

The parameters SOCKET and MOUNTPOINT are mandatory.

Socket
------
   Socket specification as understood by ZeroMQ.

   It is possible to specify all socket types supported
   by zeromq, although socket types like inproc (local in-process
   communication) make very little sense for this application.

   Examples:

   - TCP socket:       tcp://[host]:[port]
   - UNIX socket:      ipc://[socket file]

   For a complete list of available socket types see the man page for
   'zmq_connect' or consult the zeromq website at http://zeromq.org.

Directory
---------
   The directory to be shared.

Options
-------
  -a --authorized-keys-file authorized keys file. Requires --encrypt,
                           as clients are only authenticated when the
                           connection is encrypted.
  -e --encrypt
  -f --foreground          foreground operation - do not daemonize.
  -h --help
  -k --keyfile=FILE        File to read for the public key. The secret key
                           will be read from the file with the same name but
                           with '.secret' appended.
  -l --logfile=FILE        Logfile to use. Additionally it will always
                           be logged to the syslog.
  -n --numworkers=NUMBER   Number of worker threads to start [default=5]
  -p --pidfile=FILE        PID-file to write the PID of the daemonized server
                           process to.
                           Has no effect if the server runs in the foreground.
  -P --pubkeyfile          File to store the public key (needs --encrypt).
                           If not set, the public key will be written to stdout.
  -V --verbose
  -v --version

Logging
=======
   In the case of errors or warnings this program will log to the syslog.

```

To enable encryption, use the `-e` option for the server.

When neither the `--pubkeyfile` nor the `--keyfile` options are given, the public key will
be written to stdout.

When given the `--pubkeyfile` option with a file name, a key pair will be generated, and the
public key will be written to the file specified. The secret key will not be exposed, so the
when the server gets started again, it will use another newly generated key pair.

When given the `--keyfile` with a filename, it will be read for the public key, and another
file with the name `.secret` appended will be read for the secret key (see `rhizo-keygen` below).

When given the `--authorized-keys-file` with a filename, it will be read for a list of authenticated
(public) client keys, with one key per line. Only clients that use any of one the keys will be
allowed access.

**use with systemd**

systemd provides a way to start services for non-privileged users. For example, you can start the `rhizosrv` server when you log in, and stop it when your last login session ends. Because you may want to share more than one directory, `rhizosrv` is set up as a systemd
*template* unit, `rhizosrv@.service`. The unit file (see [systemd/rhizosrv@.service](systemd/rhizosrv@.service))
ships with the package and is installed to the standard user-unit directory
(`/usr/lib/systemd/user/rhizosrv@.service`); each instance (e.g. `rhizosrv@home.service`)
reads its own settings from an environment file, `.config/rhizosrv/<name>.env`, which you
create yourself (or with `rhizosrv-setuptool.sh` below). `%h` is your home directory and
`%i` is the instance name (`home` in this example):
```
[Unit]
Description=RhizoFS server (%i)

[Service]
EnvironmentFile=%h/.config/rhizosrv/%i.env
ExecStart=/usr/bin/rhizosrv -f -e --keyfile=${KEYFILE} --authorized-keys-file=${AUTHORIZED_KEYS_FILE} ${LISTEN_URL} ${SHARE_DIR}

[Install]
WantedBy=default.target
```
with `.config/rhizosrv/home.env` containing:
```
KEYFILE=/home/okurth/.config/rhizosrv/home.key
AUTHORIZED_KEYS_FILE=/home/okurth/.config/rhizosrv/home.authorized_keys
LISTEN_URL=tcp://0.0.0.0:1234
SHARE_DIR=/home/okurth
```
You can check the status with `systemctl --user status rhizosrv@home`:
```
● rhizosrv@home.service - RhizoFS server (home)
     Loaded: loaded (/usr/lib/systemd/user/rhizosrv@.service; disabled; vendor preset: enabled)
     Active: active (running) since Sun 2023-07-23 18:06:00 PDT; 16s ago
   Main PID: 44464 (rhizosrv)
      Tasks: 8 (limit: 9430)
     Memory: 800.0K
        CPU: 5ms
     CGroup: /user.slice/user-1000.slice/user@1000.service/app.slice/rhizosrv@home.service
             └─44464 /usr/bin/rhizosrv -f -e --keyfile=... --authorized-keys-file=... tcp://0.0.0.0:1234 /home/okurth
```

Instead of setting this up by hand, the script `rhizosrv-setuptool.sh` automates it: it
generates the server key pair for the instance (if one doesn't already exist), writes the
instance's environment file, and enables and starts `rhizosrv@NAME.service` with
encryption enabled. (The `rhizosrv@.service` unit itself is not generated by the script -
it is installed by the package.)
```
rhizosrv-setuptool.sh NAME [DIRECTORY] [PORT]
```
- `NAME` identifies this server instance. It is used to name the generated
  `.config/rhizosrv/NAME.key`, `.config/rhizosrv/NAME.env` and
  `.config/rhizosrv/NAME.authorized_keys` files, and the resulting service is
  `rhizosrv@NAME.service`.
- `DIRECTORY` is optional. If omitted, the home directory is shared, as in the example
  above. If given and it does not start with `/`, it is taken relative to the home
  directory.
- `PORT` is optional and defaults to `5555`. Each instance needs its own port.

Access is restricted to clients whose public key is listed in
`.config/rhizosrv/NAME.authorized_keys`, which is not managed by the script and needs to
be populated separately (see `rhizo-keygen` below to generate client keys).

**rhizofs**

rhizofs is the client-side component and is used to mount the filesystem on the client.
```
usage: rhizofs SOCKET MOUNTPOINT [options]

rhizofs and rhizosrv implement a filesytem which allows mounting
remote directories on the local computer.

This program implements the client-side filesystem.

Parameters
==========

The parameters SOCKET and MOUNTPOINT are mandatory.

Socket
------
   Socket specification as understood by ZeroMQ.

   It is possible to specify all socket types supported
   by zeromq, although socket types like inproc (local in-process
   communication) make very little sense for this application.

   Examples:

   - TCP socket:       tcp://[host]:[port]
   - UNIX socket:      ipc://[socket file]

   For a complete list of available socket types see the man page for
   'zmq_connect' or consult the zeromq website at http://zeromq.org.

Mountpoint
----------
   The directory to mount the filesystem in.
   The directory has to be empty.

general options
---------------
   --attr-cache-timeout=<seconds>
                             how long file attributes may be served from
                             the local cache [default=3]. 0 disables
                             attribute caching, which is needed to see
                             changes made to the shared directory without
                             going through this filesystem.
   --clientpubkeyfile=<file> set client keypair file
   -h --help                 print help
   -k --pubkey=<key>         set the server public key
   --pubkeyfile=<file>       set to file that contains the public key
   -V --version              print version

Logging
=======
   In the case of errors or warnings this program will log to syslog.

FUSE options:
    -d   -o debug          enable debug output (implies -f)
    -f                     foreground operation
    -s                     disable multi-threaded operation

```

If the server requires encyption, the client needs to have the public key, given with either
the `--pubkey` option on the command line, or with the `--pubkeyfile` option to specify a file
containing the public server key.

Optionally, the client can use a keypair as well. See `rhizo-keygen` below to set up a key pair.
To use it, set the `--clientpubkeyfile` option to specify the name of the public key file. The secret
key will be read from the file with the same name but `.secret` appended.

**use with systemd**

Just like the server, the client can be used with `systemd` for non-provileged users, and
`rhizofs` is likewise set up as a systemd *template* unit, `rhizofs@.service`. The unit
file (see [systemd/rhizofs@.service](systemd/rhizofs@.service)) ships with the package
and is installed to the standard user-unit directory
(`/usr/lib/systemd/user/rhizofs@.service`); each instance (e.g.
`rhizofs@nuc-oliver.service`) reads its own settings from an environment file,
`.config/rhizofs/<name>.env`, which you create yourself (or with `rhizofs-setuptool.sh`
below). As an example, if the server is `nuc-oliver.home`, your key is in
`.config/rhizofs/key`, the public server key in `.config/rhizofs/nuc-oliver` and you want
to mount on `rhizofs/nuc-oliver` in your home dir, `%h` is your home directory and `%i` is
the instance name (`nuc-oliver` in this example). The unit looks like this:
```
[Unit]
Description=RhizoFS client (%i)
After=network-online.target
Wants=network-online.target
StartLimitIntervalSec=300
StartLimitBurst=10

[Install]
WantedBy=default.target

[Service]
Type=exec
EnvironmentFile=%h/.config/rhizofs/%i.env
ExecStart=/usr/bin/rhizofs -f --clientpubkeyfile=${CLIENT_KEYFILE} --pubkeyfile=${SERVER_KEYFILE} ${SERVER_URL} ${MOUNT_POINT}
ExecStop=/usr/bin/umount ${MOUNT_POINT}
Restart=on-failure
RestartSec=20
```
with `.config/rhizofs/nuc-oliver.env` containing:
```
CLIENT_KEYFILE=/home/okurth/.config/rhizofs/key
SERVER_KEYFILE=/home/okurth/.config/rhizofs/nuc-oliver
SERVER_URL=tcp://nuc-oliver.home:5555
MOUNT_POINT=/home/okurth/rhizofs/nuc-oliver
```
Start the service with
```
systemctl --user daemon-reload
systemctl --user start rhizofs@nuc-oliver.service
```
The filesystem can easily be unmounted with
```
systemctl --user stop rhizofs@nuc-oliver.service
```

Instead of setting this up by hand, the script `rhizofs-setuptool.sh` automates it: it
generates a client key pair (if one doesn't already exist), writes the instance's
environment file, and enables and starts the unit to mount the given server. (The
`rhizofs@.service` unit itself is not generated by the script - it is installed by the
package.)
```
rhizofs-setuptool.sh NAME URL MOUNT_POINT
```
- `NAME` identifies the server. The server's public key is expected to already be in
  `.config/rhizofs/NAME` (see `rhizo-keygen` below), and the resulting service is
  `rhizofs@NAME.service` accordingly.
- `URL` is the ZeroMQ socket URL of the server, e.g. `tcp://nuc-oliver.home:5555`.
- `MOUNT_POINT` is the directory to mount the filesystem on, relative to the home
  directory. It must already exist.

Utilities
---------

The utility `rhizo-keygen` is used to generate a key pair. It simply takes the
file name that is to be created to store the public key as an argument, and creates
another file with the same name bit `.secret` appended. Example:
```
okurth@okurth-a01 rhizofs % rhizo-keygen foobar
okurth@okurth-a01 rhizofs % ls -l foobar*
-rw-------  1 okurth  staff  40 Sep  3 10:25 foobar
-rw-------  1 okurth  staff  40 Sep  3 10:25 foobar.secret
```

Building
--------

This software has three main dependencies:

* [FUSE](http://fuse.sourceforge.net/)
* [ZeroMQ](http://www.zeromq.org)
* [protobuf-c](http://code.google.com/p/protobuf-c/)

On MacOS, you can install the dependencies with:
* install macFUSE from https://osxfuse.github.io/
* install zeromq, protobuf-c and pkg-config with `brew install zeromq protobuf-c pkg-config`

Besides that there is only GNU Make and C compiler with support for the C99 standard required.

The project has no `configure` script and the build is simply triggered by calling

    make

in the project directory.

    make install

installs the client and server components on the system, along with the
`rhizofs-setuptool.sh` and `rhizosrv-setuptool.sh` convenience scripts and the
`rhizofs@.service`/`rhizosrv@.service` systemd user units described above.

There is also some rudimentary support for building a debian package by calling `make deb`,
but be aware that the package building might not always be kept up to date with the current
state of the software.

F.A.Q.
------

-   **Why the name rhizofs?**: Well, coming up with a good name for a new project is not
    easy. I actually had the TV running when I needed to come up with a name for this project.
    There was a documentary on the nutrition of trees and plants in general explaining the
    meaning of [rhizomes](http://en.wikipedia.org/wiki/Rhizome). Well, that is where the name
    came from.

Licence
-------

BSD licence. See the LICENCE file in the same directory as this README file.
