#!/usr/bin/env python
'''
Example of live plotting of pipe logging data.
This has some problems still...
'''

import sys
import Queue
from optparse import OptionParser

import Gnuplot

# Include the local pynorm in the module search path
sys.path.insert(0, "../")
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

    g = Gnuplot.Gnuplot()
    g('set data style lines')
    g.title('FEC Buffer Usage')
    g.xlabel('Time (s)')
    g.ylabel('FEC Usage')

    ydata = []
    while True:
        try:
            report = pipep.reports.get(False)
        except Queue.Empty:
            continue
        try:
            ydata.append(report['remote'][0]['fec_cur'])
            g.plot(list(enumerate(ydata)))
        except IndexError:
            continue
        pipep.reports.task_done()
    print "Exiting..."
#    g.reset()
#    g.close()
#    del g

    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
