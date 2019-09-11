#!/usr/bin/env python
'''
Simple NORM file receiver example app using Python NORM API
Shows off streaming with a super simple chat app.
'''

import sys
import os.path
import curses
import curses.textpad
from threading import Thread
from optparse import OptionParser
from random import randint

import pynorm
from pynorm.extra.manager import Manager, StopManager

USAGE = 'usage: %s [options] name' % sys.argv[0]
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

    instance = pynorm.Instance()
    session = instance.createSession(opts.address, opts.port)

    if opts.iface:
        session.setMulticastInterface(opts.iface)

    session.startReceiver(1024*1024)
    session.startSender(randint(0, 1000), 1024**2, 1400, 64, 16)
    stream = session.streamOpen(1024*1024)

    gui = Gui(stream, args[1])

    manager = Manager(instance)
    manager.register(pynorm.NORM_RX_OBJECT_UPDATED,
            lambda e: gui.showText(e.object.streamRead(1024)[1]))
#    manager.register(pynorm.NORM_RX_OBJECT_INFO,
#            lambda e: gui.showText('%s joined the chat' % e.object.info))
    manager.start()

    try:
        curses.wrapper(gui)
    except KeyboardInterrupt:
        pass

    print 'Exiting...'
    stream.streamClose(True)
    instance.stop()
    manager.join()
    return 0

class Gui(object):
    def __init__(self, stream, name):
        self.stream = stream
        self.name = name
        self.curline = 0

    def __call__(self, stdscr):
        self.stdscr = stdscr
        maxy, maxx = stdscr.getmaxyx()

        self.chatwin = curses.newwin(maxy - 2, maxx, 0, 0)
        self.chatwin.scrollok(True)

        typewin = curses.newwin(1, maxx, maxy-1, 0)

        textbox = curses.textpad.Textbox(typewin)
        while True:
            self.send(textbox.edit())
            typewin.erase()

    def send(self, text):
        msg = '%s: %s' % (self.name, text)
        self.stream.streamWrite(msg)
        self.stream.streamFlush(True)
        self.showText(msg)

    def showText(self, msg):
        maxy, maxx = self.stdscr.getmaxyx()
        if self.curline >= maxy:
            self.curline = maxy - 1

        self.chatwin.addstr(self.curline, 0, msg)
        self.curline += 1
        self.chatwin.refresh()

if __name__ == '__main__':
    sys.exit(main(sys.argv))
