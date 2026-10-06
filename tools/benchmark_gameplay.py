"""Launch one fixed-save trial; sample existing counters without ptrace/GDB.

After manual gameplay warm-up, create RUN_DIR/ready. The launcher then measures
three 20-second windows after another 10 seconds of settling. No input is sent.
RUN_DIR/inspect requests a GDB attachment by replacing this parent process.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def threads(pid):
    result = {}
    for path in Path(f'/proc/{pid}/task').glob('*/stat'):
        try:
            raw = path.read_text()
            fields = raw[raw.rfind(')') + 2:].split()
            result[path.parent.name] = dict(name=raw[raw.find('(')+1:raw.rfind(')')],
                user=int(fields[11]), system=int(fields[12]), state=fields[0])
        except (FileNotFoundError, ProcessLookupError):
            pass
    return result


def summarize(a, b, samples, ticks):
    dt = b['time'] - a['time']
    cpu = {}
    for tid, t in b['threads'].items():
        old = a['threads'].get(tid, t)
        pct = 100 * (t['user'] + t['system'] - old['user'] - old['system']) / ticks / dt
        cpu[tid] = dict(name=t['name'], percent_one_core=pct)
    gpu = [float(s['gpu'].split(',')[2]) for s in samples if s['gpu']]
    extents = sorted({tuple(s['extent']) for s in samples})
    return dict(seconds=dt, guest_fps=(b['guest']-a['guest'])/dt,
        host_fps=(b['host']-a['host'])/dt, cpu=cpu,
        gpu_percent=sum(gpu)/len(gpu) if gpu else None, gpu_samples=len(gpu), extents=extents)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('run_id')
    parser.add_argument('--build', choices=('debug', 'optimized'), required=True)
    parser.add_argument('--library-dir', type=Path)
    parser.add_argument('--icd', choices=('nvidia', 'intel'), default='nvidia')
    args = parser.parse_args()
    assert args.run_id.replace('-', '').replace('_', '').isalnum()
    root = Path('docs/bringup-artifacts/performance-controlled').resolve()
    run = root / args.run_id
    run.mkdir()  # Never overwrite an earlier trial.
    shutil.copytree(root/'seed', run/'userdata')
    build = 'linux-amd64-debug' if args.build == 'debug' else 'linux-amd64-relwithdebinfo'
    exe = Path('out/build', build, 'torchlight').resolve()
    libraries = (args.library_dir or root/'baseline-lib').resolve()
    sdk_lib = Path.home()/'rexglue-sdk/out/install/linux-amd64/lib'
    env = os.environ.copy()
    env.update(VK_ICD_FILENAMES=f'/usr/share/vulkan/icd.d/{args.icd}_icd.json',
               LD_LIBRARY_PATH=f'{libraries}:{sdk_lib}')
    for key in ('TORCHLIGHT_DIAGNOSTICS', 'TORCHLIGHT_CAPTURE_X11', 'LD_PRELOAD',
                'VK_INSTANCE_LAYERS', 'VK_LAYER_PATH'):
        env.pop(key, None)
    addresses = {}
    nm = subprocess.check_output(['nm', '-a', '--defined-only', str(exe)], text=True)
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) != 3 or parts[1] not in 'bBdD':
            continue
        for key in ('guest_submissions', 'host_presents', 'guest_extent'):
            if key in parts[2]:
                addresses[key] = int(parts[0], 16)
    assert len(addresses) == 3, addresses
    command = [str(exe), '--game_data_root', str(Path.home()/'360tools/extracted/extracted'),
               '--gpu_plugin', 'xenos', '--user_data_root', str(run/'userdata'),
               '--log_file', str(run/'runtime.log')]
    console = (run/'console.log').open('w')
    child = subprocess.Popen(command, env=env, stdout=console, stderr=subprocess.STDOUT)
    metadata = dict(pid=child.pid, command=command, build=args.build,
        executable_sha256=digest(exe), runtime_sha256=digest(libraries/'librexruntime.so'),
        plugin_sha256=digest(exe.parent/'librexgpu-xenos.so'),
        seed_sha256=json.loads((root/'seed-sha256.json').read_text()),
        environment={k:env[k] for k in ('VK_ICD_FILENAMES','LD_LIBRARY_PATH')},
        clock_ticks=os.sysconf('SC_CLK_TCK'), windows=[])
    (run/'metadata.json').write_text(json.dumps(metadata, indent=2))
    print(f'LAUNCHED {args.run_id} PID={child.pid}; waiting for manual gameplay readiness', flush=True)
    mem = None
    settle_until = window_start = None
    window_samples = []
    windows = []
    data = (run/'samples.jsonl').open('w', buffering=1)
    try:
        while child.poll() is None:
            tick = time.monotonic()
            if (run/'inspect').exists():
                data.close()
                console.close()
                # Remain the inferior's parent, satisfying ptrace_scope=1.
                os.execvp('gdb', ['gdb', '-q', '-ex', 'set pagination off',
                    '-ex', f'set logging file {run}/failure-gdb.log',
                    '-ex', 'set logging enabled on', '-p', str(child.pid)])
            if mem is None:
                mappings = Path(f'/proc/{child.pid}/maps').read_text()
                mapping = next((l for l in mappings.splitlines()
                    if l.endswith(str(exe)) and l.split()[2] == '00000000'), None)
                if mapping is None:
                    time.sleep(.1)
                    continue
                bias = int(mapping.split('-')[0], 16)
                mem = os.open(f'/proc/{child.pid}/mem', os.O_RDONLY)
                (run/'initial-maps.txt').write_text(mappings)
            def read(key):
                value = os.pread(mem, 8, bias+addresses[key])
                if len(value) != 8:
                    raise RuntimeError(f'Short counter read: {key}')
                return int.from_bytes(value, 'little')
            extent = read('guest_extent')
            sample = dict(time=time.monotonic(), guest=read('guest_submissions'),
                host=read('host_presents'), extent=[extent >> 32, extent & 0xffffffff],
                threads=threads(child.pid))
            sample.update(gpu='', gpu_error='')
            if args.icd == 'nvidia':
                gpu = subprocess.run(['nvidia-smi',
                    '--query-gpu=timestamp,pstate,utilization.gpu,utilization.memory,memory.used,clocks.sm,clocks.mem,temperature.gpu',
                    '--format=csv,noheader,nounits'], capture_output=True, text=True, timeout=3)
                sample.update(gpu=gpu.stdout.strip(), gpu_error=gpu.stderr.strip())
            data.write(json.dumps(sample)+'\n')
            if settle_until is None and (run/'ready').exists():
                settle_until = sample['time']+10
                print('READY accepted; settling for 10 seconds, then 3 x 20-second windows', flush=True)
            if settle_until and sample['time'] >= settle_until and len(windows) < 3:
                if window_start is None:
                    window_start = sample
                    window_samples = []
                window_samples.append(sample)
                if sample['time']-window_start['time'] >= 20:
                    result = summarize(window_start, sample, window_samples, metadata['clock_ticks'])
                    windows.append(result)
                    metadata['windows'] = windows
                    (run/'metadata.json').write_text(json.dumps(metadata, indent=2))
                    print(f"WINDOW {len(windows)} guest={result['guest_fps']:.3f} "
                          f"host={result['host_fps']:.3f} GPU={result['gpu_percent']}", flush=True)
                    window_start = sample
                    window_samples = []
                    if len(windows) == 3:
                        print('CAPTURE COMPLETE; manual gameplay/audio check and normal exit now allowed', flush=True)
            time.sleep(max(0, 1-(time.monotonic()-tick)))
    finally:
        if mem is not None:
            os.close(mem)
        data.close()
        if child.poll() is not None:
            metadata['exit_code'] = child.returncode
            (run/'metadata.json').write_text(json.dumps(metadata, indent=2))
            print(f'EXIT {child.returncode}; completed windows={len(windows)}', flush=True)
        else:
            print('Sampler stopped unexpectedly; game remains running for inspection', flush=True)


if __name__ == '__main__':
    main()
