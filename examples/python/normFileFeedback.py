#!/usr/bin/env python
'''
Simple NORM file sender example app using Python NORM API
'''

import sys
import os.path
import pickle
from optparse import OptionParser
from random import randint

import pynorm
import pynorm.constants as c

DEFAULT_ADDR = '224.1.2.3'
DEFAULT_PORT = 6003

def get_option_parser():
    parser = OptionParser()
    parser.set_defaults(address=DEFAULT_ADDR, port=DEFAULT_PORT)

    parser.add_option('-s', '--send',
            help='The file to send.')
    parser.add_option('-r', '--receive',
            help='The directory to cache recieved files.')
    parser.add_option('-a', '--address',
            help='The IP address to bind to (default %s)' % DEFAULT_ADDR)
    parser.add_option('-p', '--port', type=int,
            help='The port number to listen on (default %i)' % DEFAULT_PORT)
    parser.add_option('-i', '--iface',
            help='The inteface to transmit multicast on.')
    return parser

def main(argv):
    (opts, args) = get_option_parser().parse_args(argv)

    if len(args) != 1:
        print 'Error: Invalid arguments'
        print get_option_parser().format_help()
        return 1

    if opts.send is None and opts.receive is None:
        print 'No operation specified!'
        print 'Must provide a --send or --receive flag!'
        print get_option_parser().format_help()
        return 1

    instance = pynorm.Instance()
    session = instance.createSession(opts.address, opts.port)
    if opts.iface:
        session.setMulticastInterface(opts.iface)

    session.setTxRate(256e10)
    session.startReceiver(1024*1024)
    session.startSender(randint(0, 1000), 1024**2, 1400, 64, 16)

    if opts.receive is not None:
        path = os.path.abspath(opts.receive)
        instance.setCacheDirectory(path)
        print 'Setting cache directory to %s' % path

    if opts.send is not None:
        filepath = os.path.abspath(opts.send)
        filename = opts.send[opts.send.rfind('/')+1:]
        print 'Sending file %s' % filename
        session.fileEnqueue(filepath, filename)

    try:
        for event in instance:
            if event.type == c.NORM_TX_FLUSH_COMPLETED:
                if event.object.type == c.NORM_OBJECT_FILE:
                    print 'Flush completed for file %s' % event.object.filename

            elif event.type == c.NORM_RX_OBJECT_INFO:
                if event.object.type == c.NORM_OBJECT_FILE:
                    event.object.filename = os.path.join(path, event.object.info)
                    print 'Downloading file %s' % event.object.filename

                elif event.object.type == c.NORM_OBJECT_DATA:
                    # I put the sender node ID in the info field
                    # If it doesn't match ours, we dont care about it
                    if int(event.object.info) != session.nodeId:
                        event.object.cancel()

            elif event.type == c.NORM_RX_OBJECT_UPDATED:
                if event.object.type == c.NORM_OBJECT_FILE:
                    print 'File %s - %i bytes left to download' % (
                            event.object.filename, event.object.bytesPending)

                    # Let the sender know how much we have done
                    data = pickle.dumps((event.object.filename, event.object.bytesPending), -1)
                    session.dataEnqueue(data, str(event.object.sender.id))

            elif event == 'NORM_RX_OBJECT_COMPLETED':
                if event.object.type == c.NORM_OBJECT_FILE:
                    print 'File %s completed' % event.object.filename

                elif event.object.type == c.NORM_OBJECT_DATA:
                    try:
                        # This fails sometimes, not sure why yet, so ignore errors
                        filename, pending = pickle.loads(event.object.accessData())
                    except KeyError:
                        continue
                    print 'Node %i - File: %s - Pending: %i' % (event.object.sender.id, filename, pending)

            elif event == 'NORM_RX_OBJECT_ABORTED':
                if event.object.type == c.NORM_OBJECT_FILE:
                    print 'File %s aborted' % event.object.filename
    except KeyboardInterrupt:
        pass

    print 'Exiting.'
    return 0
# end main

if __name__ == '__main__':
    sys.exit(main(sys.argv))
