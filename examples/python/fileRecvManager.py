#!/usr/bin/env python
'''
Simple NORM file receiver example app using Python NORM API
Shows the usage of the event manager.
'''

import sys
import os.path
from optparse import OptionParser

sys.path.insert(0, '../')
import pynorm
from pynorm.extra.manager import Manager, StopManager

USAGE = 'usage: %s [options] <cacheDir>' % sys.argv[0]
DEFAULT_ADDR = '224.1.2.3'
DEFAULT_PORT = 6003
DEFAULT_PIPE = 'normtest'

def get_option_parser():
    parser = OptionParser(usage=USAGE)
    parser.set_defaults(address=DEFAULT_ADDR, port=DEFAULT_PORT, debug=0,
            pipe=DEFAULT_PIPE)

    parser.add_option('-a', '--address',
            help='The IP address to bind to (default %s)' % DEFAULT_ADDR)
    parser.add_option('-p', '--port', type=int,
            help='The port number to listen on (default %i)' % DEFAULT_PORT)
    parser.add_option('-i', '--iface',
            help='The inteface to transmit multicast on.')
    parser.add_option('-d', '--debug', type=int, help='Debug level')
    parser.add_option('-e', '--pipe', help='Pipe name for logging.')
    return parser

def main(argv):
    (opts, args) = get_option_parser().parse_args(argv)

    if len(args) != 2:
        print get_option_parser().get_usage()
        return 1

    path = os.path.abspath(args[1])

    instance = pynorm.Instance()
    instance.setCacheDirectory(path)

    try:
        instance.openDebugPipe(opts.pipe)
    except pynorm.NormError:
        print 'Could not connect to pipe, disabling...'
    pynorm.setDebugLevel(opts.debug)

    manager = Manager(instance)
    manager.register(pynorm.NORM_RX_OBJECT_INFO, newObject, path)
#    manager.register(pynorm.NORM_RX_OBJECT_UPDATED, updatedObject)
    manager.register(pynorm.NORM_RX_OBJECT_COMPLETED, complete)
    manager.register(pynorm.NORM_RX_OBJECT_ABORTED, abort)
    manager.start()

    session = instance.createSession(opts.address, opts.port)
    if opts.iface:
        session.setMulticastInterface(opts.iface)
    session.startReceiver(1024*1024)

    print 'Starting listener on %s:%i' % (opts.address, opts.port)
    try:
        while True:
            manager.join(2)
    except KeyboardInterrupt:
        pass
    print 'Exiting...'
    instance.stop()
    manager.join()
    return 0

def newObject(event, path):
    print 'Filename = %s' % event.object.getInfo()
    event.object.filename = os.path.join(path, event.object.getInfo())
    print 'Downloading file %s' % event.object.filename

def updatedObject(event):
    print 'File %s - %i bytes left to download' % (event.object.filename,
            event.object.bytesPending)

def complete(event):
    print 'File %s completed' % event.object.filename
    raise StopManager()

def abort(event):
    print 'File %s aborted' % event.object.filename
    raise StopManager()

if __name__ == '__main__':
    sys.exit(main(sys.argv))
