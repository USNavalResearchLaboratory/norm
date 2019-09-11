"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

import re
from datetime import time

REPORT_RE = re.compile(r'REPORT time>(\d+):(\d+):(\d+)\.(\d+) node>(\d+)')
LOCAL_RATE_RE = re.compile(r'txRate> *(\d+\.\d+) kbps sentRate> *(\d+\.\d+) grtt> *(\d+\.\d+)')
LOCAL_CC_RE = re.compile(r'clr> *(\d+) rate> *(\d+\.\d+) rtt> *(\d+\.\d+) loss> *(\d+\.\d+)')
REMOTE_RE = re.compile(r'Remote sender> *(\d+)')
REMOTE_RATE_RE = re.compile(r'rxRate> *(\d+\.\d+) kbps rx_goodput> *(\d+\.\d+) kbps')
REMOTE_OBJ_RE = re.compile(r'rxObjects> completed> *(\d+) pending> *(\d+) failed> *(\d+)')
REMOTE_FEC_RE = re.compile(r'fecBufferUsage> current> *(\d+) peak> *(\d+) overuns> *(\d+)')
REMOTE_STR_RE = re.compile(r'strBufferUsage> current> *(\d+) peak> *(\d+) overuns> *(\d+)')
REMOTE_RESYNCS_RE = re.compile(r'resyncs> *(\d+) nacks> *(\d+) suppressed> *(\d+)')

END_STR = '***************************************************************************'

class Report(object):
    def __init__(self):
        self.time = None
        self.node = None
        self.local = None
        self.remote = []

class LocalReport(object):
    def __init__(self):
        self.txrate = None
        self.sent_rate = None
        self.grtt = None
        self.clr = None
        self.rate = None
        self.rtt = None
        self.loss = None
        self.slow_start = False

class RemoteReport(object):
    def __init__(self):
        self.node = None
        self.rate = None
        self.goodput = None
        self.completed = None
        self.pending = None
        self.failed = None
        self.fec_cur = None
        self.fec_peak = None
        self.fec_over = None
        self.str_cur = None
        self.str_peak = None
        self.str_over = None
        self.resyncs = None
        self.nacks = None
        self.supressed = None

def parse_log(file):
    rep = None
    sender = None

    for line in file:
        # Get rid of "Proto Info:" header
        line = line[line.find(':')+1:].strip()

        if not line:
            continue

        if line.startswith('REPORT'):
            rep = Report()
            hour, min, sec, usec, rep.node = map(int,
                    REPORT_RE.match(line).groups())
            rep.time = time(hour, min, sec, usec)

        elif line.startswith('Local status'):
            rep.local = LocalReport()

        elif line.startswith('txRate'):
            rep.local.txrate, rep.local.sent_rate, rep.local.grtt = map(float,
                    LOCAL_RATE_RE.match(line).groups())

        elif line.startswith('clr'):
            match = LOCAL_CC_RE.match(line).groups()
            rep.local.clr = int(match[0])
            rep.local.rate, rep.local.rtt, rep.local.loss = map(float,
                    match[1:])
            rep.local.slow_start = True if 'slow_start' in line else False

        elif line.startswith('Remote sender'):
            rep.remote.append(RemoteReport())
            sender = rep.remote[-1]
            sender.node = int(REMOTE_RE.match(line).group(1))

        elif line.startswith('rxRate'):
            sender.rate, sender.goodput = map(float,
                    REMOTE_RATE_RE.match(line).groups())

        elif line.startswith('rxObjects'):
            sender.completed, sender.pending, sender.failed = map(int,
                    REMOTE_OBJ_RE.match(line).groups())

        elif line.startswith('fecBufferUsage'):
            sender.fec_cur, sender.fec_peak, sender.fec_over = map(int,
                    REMOTE_FEC_RE.match(line).groups())

        elif line.startswith('strBufferUsage'):
            sender.str_cur, sender.str_peak, sender.str_over = map(int,
                    REMOTE_STR_RE.match(line).groups())

        elif line.startswith('resyncs'):
            sender.resyncs, sender.nacks, sender.supressed = map(int,
                    REMOTE_RESYNCS_RE.match(line).groups())

        elif line == END_STR:
            yield rep
