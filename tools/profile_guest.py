"""Source in GDB while stopped; read-only, bounded profiling of a live game.

guest-profile OUTPUT_JSON INTERVAL_SECONDS SAMPLE_COUNT
Use count=1, interval=30 for an uninterrupted FPS/CPU interval. Use interval=.4,
count=80 for debugger stack occupancy samples; do not benchmark that FPS.
"""
import gdb
import json
import os
from pathlib import Path
import threading
import time


def counters():
    inferior = gdb.selected_inferior()
    result = {}
    for name in ('guest_submissions', 'host_presents'):
        try:
            address = int(gdb.parse_and_eval("&'(anonymous namespace)::" + name + "'"))
        except gdb.error:
            continue  # counters of an older build; the stacks do not need them
        result[name] = int.from_bytes(inferior.read_memory(address, 8).tobytes(), 'little')
    return result


def proc_threads(pid):
    result = {}
    for path in Path(f'/proc/{pid}/task').glob('*/stat'):
        try:
            raw = path.read_text()
            fields = raw[raw.rfind(')') + 2:].split()
            tid = int(path.parent.name)
            result[tid] = dict(name=raw[raw.find('(')+1:raw.rfind(')')],
                               state=fields[0], user=int(fields[11]), system=int(fields[12]))
        except (FileNotFoundError, ProcessLookupError):
            pass
    return result


class Profile:
    def __init__(self, output, interval, count, followup=None):
        self.output = Path(output)
        self.interval, self.count = interval, count
        self.followup = followup
        self.pid = gdb.selected_inferior().pid
        self.samples = []
        self.start = dict(time=time.monotonic(), counters=counters(),
                          threads=proc_threads(self.pid))
        self.stopped_seconds = 0.0
        self.pre_interrupt = None
        gdb.events.stop.connect(self.on_stop)
        self.resume()

    def resume(self):
        gdb.execute('continue&')
        timer = threading.Timer(self.interval, lambda: gdb.post_event(self.interrupt))
        timer.daemon = True
        timer.start()

    def interrupt(self):
        self.pre_interrupt = dict(time=time.monotonic(), threads=proc_threads(self.pid))
        gdb.execute('interrupt')

    def on_stop(self, event):
        if isinstance(event, gdb.BreakpointEvent) or (
            isinstance(event, gdb.SignalEvent) and event.stop_signal != 'SIGINT'
        ):
            gdb.events.stop.disconnect(self.on_stop)
            print('PROFILE interrupted by unexpected stop; inspect before resuming')
            return
        began = time.monotonic()
        sample = self.pre_interrupt or dict(time=began, threads=proc_threads(self.pid))
        sample['counters'] = counters()
        sample['stacks'] = {}
        for thread in gdb.selected_inferior().threads():
            tid = thread.ptid[1]
            frames = []
            try:
                thread.switch()
                frame = gdb.newest_frame()
                for _ in range(18):
                    if frame is None:
                        break
                    frames.append(dict(pc=hex(frame.pc()), function=frame.name(),
                                       library=gdb.solib_name(frame.pc())))
                    frame = frame.older()
            except gdb.error as error:
                frames.append(dict(error=str(error)))
            sample['stacks'][tid] = dict(name=thread.name, frames=frames)
        self.samples.append(sample)
        self.stopped_seconds += time.monotonic() - began
        if len(self.samples) < self.count:
            gdb.post_event(self.resume)
            return
        gdb.events.stop.disconnect(self.on_stop)
        end = dict(time=time.monotonic(), counters=counters(), threads=proc_threads(self.pid))
        self.output.write_text(json.dumps(dict(pid=self.pid, clock_ticks=os.sysconf('SC_CLK_TCK'),
            interval=self.interval, start=self.start, end=end, samples=self.samples,
            debugger_stopped_seconds=self.stopped_seconds), indent=2))
        dt = self.samples[-1]['time'] - self.start['time']
        delta = lambda name: end['counters'].get(name, 0) - self.start['counters'].get(name, 0)
        guest, host = delta('guest_submissions'), delta('host_presents')
        print(f'PROFILE DONE {self.output}: {dt:.3f}s guest={guest/dt:.3f} host={host/dt:.3f}'
              f' debugger_stack_time={self.stopped_seconds:.3f}s')
        if self.followup:
            gdb.post_event(self.followup)
        else:
            gdb.post_event(lambda: gdb.execute('continue&'))


class Command(gdb.Command):
    def __init__(self):
        super().__init__('guest-profile', gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        parts = gdb.string_to_argv(argument)
        if len(parts) != 3:
            raise gdb.GdbError('usage: guest-profile OUTPUT_JSON INTERVAL_SECONDS SAMPLE_COUNT')
        Profile(parts[0], float(parts[1]), int(parts[2]))


Command()


class SessionCommand(gdb.Command):
    def __init__(self):
        super().__init__('guest-profile-session', gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        stem = gdb.string_to_argv(argument)
        if len(stem) != 1:
            raise gdb.GdbError('usage: guest-profile-session OUTPUT_STEM')
        # First collect uninterrupted CPU/FPS. Only then collect stack samples.
        Profile(stem[0] + '-baseline.json', 30, 1,
                lambda: Profile(stem[0] + '-stacks.json', .35, 60))


SessionCommand()
