#!/usr/bin/env python
'''
Example showing reading debug info from a pipe.
'''

import sys
import Queue
from optparse import OptionParser

from pynorm.extra.pipeparser import PipeParser

USAGE = 'usage: %s [options]' % sys.argv[0]
DEFAULT_PIPE = 'normtest'

def get_option_parser():
    parser = OptionParser(usage=USAGE)
    parser.set_defaults(pipe=DEFAULT_PIPE)

    parser.add_option('-p', '--pipe',
            help='The pipe to connect to (default %s)' % DEFAULT_PIPE)
    return parser

def main(argv):
    (opts, args) = get_option_parser().parse_args(argv)

    if len(args) != 1:
        print USAGE
        return 1

    pipep = PipeParser(opts.pipe)
    pipep.start()

    while True:
        try:
            report = pipep.reports.get(True, 3)
        except Queue.Empty:
            continue
        try:
            print report['time'], report['remote'][0]['fec_cur']
        except IndexError:
            pass
        pipep.reports.task_done()

if __name__ == '__main__':
    sys.exit(main(sys.argv))
