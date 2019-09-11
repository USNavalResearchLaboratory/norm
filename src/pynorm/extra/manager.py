"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

# For Python 2.5
from __future__ import with_statement

from threading import Thread, Lock
from random import randint

#from .models import get_session, SessionProfile, SendProfile, ReceiveProfile
from .. import constants as c
from ..instance import Instance

class StopManager(Exception): pass

class Manager(Thread):
    """Provides a callback driven method of handling NORM events."""

    def __init__(self, instance=None, *args, **kwargs):
        super(Manager, self).__init__(*args, **kwargs)
        self.setDaemon(True)

        self.instance = instance if instance is not None else Instance()

        self.callbacks = {}
        self.sessions = {}
        self.lock = Lock()
        self.doQuit = False

    def run(self):
        while not self.doQuit:
            event = self.instance.getNextEvent(2)
            if event is None:
                continue

            with self.lock:
                try:
                    cbs = self.callbacks[event.type]
                except KeyError:
                    continue

                try:
                    for func, args, kwargs in cbs:
                        func(event, *args, **kwargs)
                except StopManager:
                    self.doQuit = True

    def register(self, event, func, *args, **kwargs):
        with self.lock:
            self.callbacks.setdefault(event, list()).append(
                    (func, args, kwargs))

    def unregister(self, event, func, *args, **kwargs):
        with self.lock:
            self.callbacks[event].remove((func, args, kwargs))

#    def start_session(self, name):
#        sqlsession = get_session()
#        profile = sqlsession.query(SessionProfile).filter_by(name=name).first()
#        self.instance.setCacheDirectory(profile.cache_directory)
#        session = self.instance.createSession(profile.address, profile.port, profile.node_id)
#        session.userData = name
#
#        if profile.interface is not None:
#            session.setMulticastInterface(profile.interface)
#
#        if profile.ttl is not None:
#            session.setTTL(profile.ttl)
#
#        if profile.tos is not None:
#            session.setTOS(profile.tos)
#
#        if profile.loopback is not None:
#            session.setLoopback(profile.loopback)
#        self.sessions[name] = session
#        sqlsession.close()
#
#    def start_sender(self, session_name, sender_name):
#        session = self.sessions[session_name]
#        sqlsession = get_session()
#        profile = sqlsession.query(SendProfile).filter_by(
#                name=sender_name).first()
#        session.startSender(randint(0, 10000), profile.buffer_space,
#                profile.segment_size, profile.block_size, profile.num_parity)
#
#        if profile.graceful_stop is not None:
#            session.sendGracefulStop = profile.graceful_stop
#
#        if profile.rate is not None:
#            session.setTxRate(profile.rate)
#
#        if profile.socket_buffer is not None:
#            session.setTxSocketBuffer(profile.socket_buffer)
#
#        if profile.congestion_control is not None:
#            session.setCongestionControl(profile.congestion_control)
#
#        if None not in (profile.rate_min, profile.rate_max):
#            session.setTxRateBounds(profile.rate_min, profile.rate_max)
#
#        if None not in (profile.size_max, profile.cache_min,
#                profile.cache_max):
#            session.setTxCacheBounds(profile.size_max, profile.cache_min,
#                    profile.cache_max)
#
#        if profile.auto_parity is not None:
#            session.setAutoParity(profile.auto_parity)
#
#        if profile.grtt is not None:
#            session.setGrttEstimate(profile.grtt)
#
#        if profile.grtt_max is not None:
#            session.setGrttMax(profile.grtt_max)
#
#        if profile.grtt_probing_mode is not None:
#            session.setGrttProbingMode(getattr(c, profile.grtt_probing_mode))
#
#        if None not in (profile.grtt_probing_interval_min,
#                profile.grtt_probing_interval_max):
#            session.setGrttProbingInterval(profile.grtt_probing_interval_min,
#                    profile.grtt_probing_interval_max)
#
#        if profile.backoff_factor is not None:
#            session.setBackoffFactor(profile.backoff_factor)
#
#        if profile.group_size is not None:
#            session.setGroupSize(profile.group_size)
#        sqlsession.close()
#        return session
#
#    def start_receiver(self, session_name, receiver_name):
#        sqlsession = get_session()
#        session = self.sessions[session_name]
#        profile = sqlsession.query(ReceiveProfile).filter_by(
#                name=receiver_name).first()
#        session.startReceiver(profile.buffer_space)
#
#        if profile.grace_period is not None:
#            session.gracePeriod = profile.grace_period
#
#        if profile.socket_buffer is not None:
#            session.setRxSocketBuffer(profile.socket_buffer)
#
#        if profile.is_silent is not None:
#            session.setSilentReceiver(profile.is_silent, profile.silent_max_delay)
#
#        if profile.unicast_nack is not None:
#            session.setDefaultUnicastNack(profile.unicast_nack)
#
#        if profile.nacking_mode is not None:
#            session.setDefaultNackingMode(getattr(c, profile.nacking_mode))
#
#        if profile.repair_boundary is not None:
#            session.setDefaultRepairBoundary(getattr(c, profile.repair_boundary))
#        sqlsession.close()
#        return session
