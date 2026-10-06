"""Read-only per-thread CPU and NVIDIA utilization over a bounded interval.

--no-gpu samples the threads only, without nvidia-smi (machines without NVIDIA's tool, such as
the Steam Deck, or when the GPU must not be woken)."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('pid', type=int)
parser.add_argument('output', type=Path)
parser.add_argument('--seconds', type=int, default=35)
parser.add_argument('--no-gpu', action='store_true', help='do not run nvidia-smi')
args = parser.parse_args()
rows = []
for _ in range(args.seconds):
    start = time.monotonic()
    threads = {}
    for path in Path(f'/proc/{args.pid}/task').glob('*/stat'):
        try:
            raw = path.read_text()
            fields = raw[raw.rfind(')') + 2:].split()
            threads[path.parent.name] = dict(name=raw[raw.find('(')+1:raw.rfind(')')],
                state=fields[0], user=int(fields[11]), system=int(fields[12]))
        except FileNotFoundError:
            pass
    if args.no_gpu:
        rows.append(dict(time=start, threads=threads, gpu='', gpu_error='not sampled (--no-gpu)'))
    else:
        gpu = subprocess.run(['nvidia-smi',
            '--query-gpu=timestamp,pstate,utilization.gpu,utilization.memory,memory.used,clocks.sm,clocks.mem,temperature.gpu',
            '--format=csv,noheader,nounits'], capture_output=True, text=True)
        rows.append(dict(time=start, threads=threads, gpu=gpu.stdout.strip(), gpu_error=gpu.stderr.strip()))
    if not threads:
        break
    time.sleep(max(0, 1 - (time.monotonic() - start)))
args.output.write_text(json.dumps(dict(pid=args.pid, clock_ticks=os.sysconf('SC_CLK_TCK'),
                                      samples=rows), indent=2))
print(f'Saved {len(rows)} CPU/GPU samples to {args.output}')
