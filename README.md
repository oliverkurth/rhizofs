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


Authentication
--------------

The authentication system implemented in rhizofs is easily explained: there is 
no authentication. The server will happily answer requests from any client without 
some kind of access credentials.

This is partly because zeromq does not support any kind of authentication as well
as zeromq is not recommended to be used outside of trusted and secured networks.
Under the circumstances  of the second part it makes little sense to implement an
authentication layer on top of zeromq.

If you need authentication or intend to use this filesystem on the internet, the
best option would be using it inside a VPN (openvpn, ...).


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

    ./bin/rhizosrv SOCKET DIRECTORY [options]

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
      -h --help
      -v --version
      -V --verbose
      -f --foreground          foreground operation - do not daemonize.
      -n --numworkers=NUMBER   Number of worker threads to start [default=5]
      -l --logfile=FILE        Logfile to use. Additionally it will always
                               be logged to the syslog.
      -p --pidfile=FILE        PID-file to write the PID of the daemonized server
                               process to.
                               Has no effect if the server runs in the foreground.
    
    Logging
    =======
       In the case of errors or warnings this program will log to the syslog.

**use with systemd**

systemd provides a way to start services for non-privileged users. For example, you can start the `rhizosrv` server when you log in, and stop it when your last login session ends. Create this file in the sub directory `.config/systemd/user/rhizosrv.service` in your home directory. `%h` is your home directory:
```
[Unit]
Description=RhizoFS server

[Service]
ExecStart=/usr/local/bin/rhizosrv -f tcp://0.0.0.0:1234 %h

[Install]
WantedBy=default.target
```
You can check the status with `systemctl --user status rhizosrv`:
```
● rhizosrv.service - RhizoFS server
     Loaded: loaded (/home/okurth/.config/systemd/user/rhizosrv.service; disabled; vendor preset: enabled)
     Active: active (running) since Sun 2023-07-23 18:06:00 PDT; 16s ago
   Main PID: 44464 (rhizosrv)
      Tasks: 8 (limit: 9430)
     Memory: 800.0K
        CPU: 5ms
     CGroup: /user.slice/user-1000.slice/user@1000.service/app.slice/rhizosrv.service
             └─44464 /usr/local/bin/rhizosrv -f tcp://0.0.0.0:1234 /home/okurth
```

**rhizofs**

rhizofs is the client-side component and is used to mount the filesystem on the client.

    usage: ./bin/rhizofs SOCKET MOUNTPOINT [options]
    
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
        -h   --help      print help
        -V   --version   print version
    
    Logging
    =======
       In the case of errors or warnings this programm will log to the syslog.


Building
--------

This software has three main dependencies:

* [FUSE](http://fuse.sourceforge.net/)
* [ZeroMQ](http://www.zeromq.org)
* [protobuf-c](http://code.google.com/p/protobuf-c/)

Besides that there is only GNU Make and C compiler with support for the C99 standard required.

The project has no `configure` script and the build is simply triggered by calling

    make

in the project directory.

    make install

installs the client and server components on the system.

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
