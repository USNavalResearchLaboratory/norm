#!/usr/bin/env python
'''
Simple NORM file sender example app using Python NORM API
'''

import sys, os.path
from optparse import OptionParser
from random import randint

import pynorm

USAGE = 'usage: %s [options] <file>' % sys.argv[0]
DEFAULT_ADDR = u'224.1.2.3'
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
        print(get_option_parser().get_usage())
        return 1

    filepath = os.path.abspath(args[1])
    filename = args[1][args[1].rfind('/')+1:]

    instance = pynorm.Instance()

    session = instance.createSession(opts.address, opts.port)
    if opts.iface:
        session.setMulticastInterface(opts.iface)
    session.setTxRate(1.0e+06)
    session.startSender(randint(0, 1000), 1024**2, 1400, 64, 16)

    print(('Sending file %s' % filename))
    session.fileEnqueue(filepath, info=filename.encode())

    try:
        for event in instance:
            print(event)
            if str(event) == 'NORM_TX_FLUSH_COMPLETED':
                print('Flush completed, exiting.')
                return 0
                
    except KeyboardInterrupt:
        pass

    print('Exiting.')
    return 0
# end main

if __name__ == '__main__':
    sys.exit(main(sys.argv))
