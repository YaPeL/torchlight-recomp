#!/usr/bin/env python3
"""Conservative, capped InvalidFunctionTrap bring-up. Run from the project root."""
import argparse
import datetime
import os
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--gpu-plugin', default='', choices=['', 'xenos'])
parser.add_argument('--resume-after-review', action='store_true')
args = parser.parse_args()
ROOT = pathlib.Path(__file__).resolve().parents[1]
ART = ROOT / 'docs/bringup-artifacts'
LOG = ROOT / 'docs/bringup-log.md'
CONFIG = ROOT / 'config/torchlight_functions.toml'
ART.mkdir(parents=True, exist_ok=True)
STOP = ART / 'STOPPED'
if STOP.exists() and not args.resume_after_review:
    raise SystemExit('Bring-up stopped: ' + STOP.read_text() + '\nResolve and review the blocker before removing this marker.')

def run(args, name, timeout=1800):
    path = ART / name
    with path.open('w') as output:
        try:
            result = subprocess.run(args, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
            return result.returncode, path.read_text(errors='replace')
        except subprocess.TimeoutExpired:
            return 124, path.read_text(errors='replace')

def log(text):
    with LOG.open('a') as f:
        f.write(text + '\n')
    print(text, flush=True)

def registered(address):
    return any(re.search(r'(?:SetFunction\(|\{\s*)0x' + address + r'\s*,\s*sub_', (ROOT / 'generated/default' / file).read_text(), re.I)
               for file in ('torchlight_register.cpp', 'torchlight_init.cpp'))

if not LOG.exists():
    log('# Torchlight bring-up log\n\nInstructions: `ASTRA_BRINGUP.md` was absent; read the complete `TORCHLIGHT_RECOMP_ASTRA_HANDOFF.md`, which contains the requested immediate task.\n\nBaseline: hint `0x82A1FE30` already present, included by the manifest, declared, defined, and registered. Initial incremental build passed. Sandbox ptrace was denied; GDB works with approved escalation. No generated code is manually edited.\n')

previous = [int(n) for n in re.findall(r'^## Iteration (\d+)', LOG.read_text(), re.M)]
start = max(previous, default=0) + 1
result, _ = run(['cmake', '--build', '--preset', 'linux-amd64-debug', '-j', '8'], f'preflight-{start}-build.log')
if result:
    log(f'\nSTOP: preflight build failed ({result}); see bringup-artifacts/preflight-{start}-build.log.')
    raise SystemExit(1)
for iteration in range(start, start + 20):
    log(f'\n## Iteration {iteration}\n\n- Date/time: {datetime.datetime.now().astimezone().isoformat()}')
    # The runtime's logs folder (shared by every build: the runs' logs are the new ones).
    state = os.environ.get('XDG_STATE_HOME') or pathlib.Path.home() / '.local/state'
    runtime_logs = pathlib.Path(state) / 'torchlight/logs'
    before = {p: p.stat().st_mtime_ns for p in runtime_logs.glob('*.log')}
    result, output = run(['timeout', '-s', 'INT', '-k', '10s', '60s', 'gdb', '-q', '-batch',
        '-ex', 'set debuginfod enabled off', '-ex', 'set print thread-events off',
        '-ex', 'set breakpoint pending on',
        '-ex', 'handle SIGSEGV nostop noprint pass' if args.gpu_plugin else 'handle SIGSEGV stop print pass',
        '-ex', 'set env LD_LIBRARY_PATH ' + str(pathlib.Path.home() / 'rexglue-sdk/out/install/linux-amd64/lib'),
        '-ex', 'break rex::runtime::InvalidFunctionTrap',
        '-ex', 'run --game_data_root ' + str(pathlib.Path.home() / '360tools/extracted/extracted') + (' --gpu_plugin ' + args.gpu_plugin if args.gpu_plugin else '') + ' --log_level debug',
        '-ex', 'python f=gdb.newest_frame(); print("BRINGUP_TARGET=" + hex(int(gdb.parse_and_eval("((PPCContext*)$rdi)->last_indirect_target")))) if f and "InvalidFunctionTrap" in (f.name() or "") else None', '-ex', 'thread apply all bt 12',
        '--args', './out/build/linux-amd64-debug/torchlight'], f'iteration-{iteration}-gdb.log', 80)
    runtime_output = '\n'.join(p.read_text(errors='replace') for p in runtime_logs.glob('*.log') if p.stat().st_mtime_ns != before.get(p))
    (ART / f'iteration-{iteration}-runtime.log').write_text(runtime_output)
    if re.search(r'\[error\] \[(?:gpu|core|sys)\]', runtime_output) or 'no GPU emulation loaded' in runtime_output:
        log(f'- Runtime result: subsystem error; STOP. See bringup-artifacts/iteration-{iteration}-runtime.log.\n- Hint added: none; codegen/build not run.')
        break
    trap = re.search(r'hit Breakpoint .*InvalidFunctionTrap', output)
    target = re.search(r'BRINGUP_TARGET=(0x[0-9a-f]+)', output)
    if result == 124 or not trap or not target:
        log(f'- Runtime result: stopped; GDB status {result}.\n- Crash class: non-trap, timeout, or debugger failure; inspect artifact.\n- Backtrace summary: see `bringup-artifacts/iteration-{iteration}-gdb.log`.\n- `last_indirect_target`: not validly obtained\n- Target in XEX range: unknown\n- Already registered: unknown\n- Hint added: none\n- Codegen result: not run\n- Build result: not run\n- Next runtime result: automatic loop stopped.')
        break
    value = int(target[1], 16)
    address = f'{value:08X}'
    in_range = 0x82000000 <= value < 0x835C0000 and value % 4 == 0
    hinted = re.search(r'0x' + address + r'\s*=', CONFIG.read_text(), re.I) is not None
    present = registered(address)
    trap_stack = re.search(r'(?m)^#0 .*InvalidFunctionTrap[^\n]*\n(?:#[1-3] [^\n]*\n)*', output)
    frames = trap_stack[0].strip() if trap_stack else 'See GDB artifact.'
    log(f'- Runtime result: InvalidFunctionTrap breakpoint reached\n- Crash class: InvalidFunctionTrap\n- Backtrace summary:\n```text\n{frames}\n```\n- `last_indirect_target`: `0x{address}`\n- Target in XEX range (and aligned): {in_range}\n- Already registered: {present}\n- Already hinted: {hinted}')
    if not in_range or hinted or present:
        log('- Hint added: none\n- Codegen result: not run\n- Build result: not run\n- Next runtime result: STOP — invalid, repeated, or registered target.')
        break
    with CONFIG.open('a') as f:
        f.write(f'0x{address} = {{}}\n')
    log(f'- Hint added: `0x{address}`')
    result, output = run(['cmake', '--build', '--preset', 'linux-amd64-debug', '--target', 'torchlight_codegen'], f'iteration-{iteration}-codegen.log')
    conflict = re.search(r'(?im)^.*(?:overlap|conflicting boundar|validation failed).*$' , output)
    defined = any(f'DEFINE_REX_FUNC(sub_{address})' in p.read_text() for p in (ROOT / 'generated/default').glob('torchlight_recomp.*.cpp'))
    declared = f'DECLARE_REX_FUNC(sub_{address})' in (ROOT / 'generated/default/torchlight_funcs.h').read_text()
    log(f'- Codegen result: exit {result}; registered={registered(address)}, defined={defined}, declared={declared}; artifact `bringup-artifacts/iteration-{iteration}-codegen.log`.')
    if result or conflict or not (registered(address) and defined and declared):
        log('- Build result: not run\n- Next runtime result: STOP — codegen failure, conflict, or missing generated entry.')
        break
    result, _ = run(['cmake', '--preset', 'linux-amd64-debug'], f'iteration-{iteration}-configure.log')
    if not result:
        result, _ = run(['cmake', '--build', '--preset', 'linux-amd64-debug', '-j', '8'], f'iteration-{iteration}-build.log')
    log(f'- Build result: exit {result}')
    if result:
        log('- Next runtime result: STOP — configure/build failed.')
        break
    log(f'- Next runtime result: see iteration {iteration + 1}.' if iteration < start + 19 else '- Next runtime result: STOP — conservative 20-hint cap reached; rerun pending.')

STOP.write_text('Automatic loop stopped; review the last iteration in docs/bringup-log.md before resuming.\n')
