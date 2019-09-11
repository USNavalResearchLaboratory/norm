#!/usr/bin/env python
'''
Example of using the NORM library directly.
You really shouldn't need to do this.  Use the pretty API instead.
But its here if you need it.
'''

import sys
import os.path
import ctypes
from optparse import OptionParser

from pynorm.core import libnorm, NormEventStruct
import pynorm.constants as c

USAGE = 'usage: %s [options] <cacheDir>' % sys.argv[0]
DEFAULT_ADDR = '224.1.2.3'
DEFAULT_PORT = 6003

def get_option_parser():
    parser = OptionParser(usage=USAGE)
    parser.set_defaults(address=DEFAULT_ADDR, port=DEFAULT_PORT)

    parser.add_option('-a', '--address'
            help='The IP address to bind to (default %s)' % DEFAULT_ADDR)
    parser.add_option('-p', '--port', type=int,
            help='The port number to listen on (default %i)' % DEFAULT_PORT)
    parser.add_option('-i', '--iface',
            help='The inteface to transmit multicast on.')
    return parser

def main(argv):
    (opts, args) = get_option_parser().parse_args(argv)

    if len(args) != 2:
        print get_option_parser().get_usage()
        return 1

    path = os.path.abspath(args[1])

    instance = libnorm.NormCreateInstance(False)
    session = libnorm.NormCreateSession(instance, opts.address, opts.port,
            c.NORM_NODE_ANY)

    if opts.iface:
        libnorm.NormSetMulticastInterface(session, opts.iface)

    libnorm.NormSetCacheDirectory(instance, path)
    libnorm.NormStartReceiver(session, 1024*1024)
    theEvent = NormEventStruct()

    try:
        while libnorm.NormGetNextEvent(instance, ctypes.byref(theEvent)):
            if theEvent.type == c.NORM_RX_OBJECT_NEW:
                print 'rawRecv.py: NORM_RX_OBJECT_NEW event ...'

            elif theEvent.type == c.NORM_RX_OBJECT_INFO:
                print 'rafRecv.py: NORM_RX_OBJECT_INFO event ...'

                if c.NORM_OBJECT_FILE == libnorm.NormObjectGetType(theEvent.object):
                    length = libnorm.NormObjectGetInfoLength(theEvent.object)
                    buffer = ctypes.create_string_buffer(length)
                    recv = libnorm.NormObjectGetInfo(theEvent.object, buffer, length)
                    if recv == 0:
                        print 'Error'
                    filename = os.path.join(path, buffer.value)
                    print 'Filename - %s' % filename
                    libnorm.NormFileRename(theEvent.object, filename)

            elif theEvent.type == c.NORM_RX_OBJECT_UPDATED:
                size = libnorm.NormObjectGetSize(theEvent.object)
                completed = size - libnorm.NormObjectGetBytesPending(theEvent.object)
                percent = 100.0 * (float(completed) / float(size))
                print '%.1f completed' % percent

            elif theEvent.type == c.NORM_RX_OBJECT_COMPLETED:
                print 'Complete'
                return 0

            elif theEvent.type == c.NORM_RX_OBJECT_ABORTED:
                print 'Aborted'
                return 0

            elif theEvent.type == c.NORM_REMOTE_SENDER_NEW:
                print 'New sender'

            elif theEvent.type == c.NORM_REMOTE_SENDER_ACTIVE:
                print 'Sender active'

            elif theEvent.type == c.NORM_REMOTE_SENDER_INACTIVE:
                print 'Sender inactive'
    except KeyboardInterrupt:
        pass

    libnorm.NormStopReceiver(session)
    libnorm.NormDestroySession(session)
    libnorm.NormDestroyInstance(instance)
    print 'Exiting.'
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
