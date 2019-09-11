#!/usr/bin/env python
'''
Simple NORM file receiver example app using Python NORM API
'''

import sys, os.path
from optparse import OptionParser

import pynorm

USAGE = 'usage: %s [options] <cacheDir>' % sys.argv[0]
DEFAULT_ADDR = '224.1.2.3'
DEFAULT_PORT = 6003

def get_option_parser():
    parser = OptionParser(usage=USAGE)
    parser.set_defaults(address=DEFAULT_ADDR, port=DEFAULT_PORT)

    parser.add_option('-a', '--address',
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

    instance = pynorm.Instance()
    instance.setCacheDirectory(path)

    session = instance.createSession(opts.address, opts.port)
    if opts.iface:
        session.setMulticastInterface(opts.iface)
    session.startReceiver(1024*1024)

    try:
        for event in instance:
            if event == 'NORM_RX_OBJECT_INFO':
                event.object.filename = os.path.join(path, event.object.info)
                print 'Downloading file %s' % event.object.filename

            elif event == 'NORM_RX_OBJECT_UPDATED':
                print 'File %s - %i bytes left to download' % (
                        event.object.filename, event.object.bytesPending)

            elif event == 'NORM_RX_OBJECT_COMPLETED':
                print 'File %s completed' % event.object.filename
                return 0

            elif event == 'NORM_RX_OBJECT_ABORTED':
                print 'File %s aborted' % event.object.filename
                return 1

            else:
                print event
    except KeyboardInterrupt:
        pass

    print 'Exiting.'
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
