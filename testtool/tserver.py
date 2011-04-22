#!/usr/bin/env python

import rhizofs_pb2 as pb
import zmq
import sys


def build_request(requesttype):
    r = pb.Request();
    r.requesttype = requesttype
    #r.version = pb.Version()
    r.version.major = 0
    r.version.minor = 1
    r.endpoint = '/tmp/'
    return r

def dump_response(resp):
    print "Requesttype %d (version %d.%d)" % (resp.requesttype, resp.version.major, resp.version.minor)

    if resp.errnotype != pb.ERRNO_NONE:
        print "  error: %d" % resp.errnotype
    if len(resp.directory_entries) > 0:
        print "  directoriy_entries :"
        for de in resp.directory_entries:
            print "    %s" % de

def send_request(req):
    sock.send(req.SerializeToString())
    resp = pb.Response()
    data = sock.recv()
    #sys.stdout.write(data)
    resp.ParseFromString(data)
    dump_response(resp)




def ping():
    r = build_request(pb.PING)
    send_request(r)


def readdir_wo_path():
    r = build_request(pb.READDIR)
    send_request(r)


def readdir():
    r = build_request(pb.READDIR)
    r.path = "usr/bin"
    send_request(r)

def mkdir():
    r = build_request(pb.MKDIR)
    r.path = "tmp/testdir"
    r.modemask = 0555
    send_request(r)

def rmdir():
    r = build_request(pb.RMDIR)
    r.path = "tmp/testdir"
    send_request(r)



def stat():
    r = build_request(pb.STAT)
    send_request(r)




FUNCS={
    "ping"  : ping,
    "readdir" : readdir,
    "readdir_wo_path" : readdir_wo_path,
    "stat"  : stat,
    "rmdir"  : rmdir,
    "mkdir"  : mkdir,
}

SOCKET="tcp://0.0.0.0:11555"

context = zmq.Context()
sock = context.socket(zmq.REQ)
sock.connect(SOCKET)

for func in sys.argv[1:]:
    if FUNCS.has_key(func):
        FUNCS[func]()
    else:
        print "Unknown function : %s" % func


