"""Summarize controlled launches; distinguish windows from independent runs."""
import json
from pathlib import Path
import statistics


def distribution(values):
    return dict(n=len(values), mean=statistics.mean(values),
                variance=statistics.variance(values) if len(values) > 1 else None,
                stddev=statistics.stdev(values) if len(values) > 1 else None)


def window_metrics(window):
    def cpu(prefix):
        matches = [t['percent_one_core'] for t in window['cpu'].values()
                   if t['name'].startswith(prefix)]
        if len(matches) != 1:
            raise ValueError(f'Expected one {prefix} thread, found {len(matches)}')
        return matches[0]
    return dict(guest_fps=window['guest_fps'], host_fps=window['host_fps'],
                main_cpu=cpu('Main XThread'), audio_cpu=cpu('Audio Worker'),
                xenos_cpu=cpu('GPU Commands'), gpu_busy=window['gpu_percent'])


def main():
    root = Path('docs/bringup-artifacts/performance-controlled')
    runs, incomplete, groups = {}, {}, {}
    for path in sorted(root.glob('*/metadata.json')):
        data = json.loads(path.read_text())
        if len(data['windows']) != 3:
            incomplete[path.parent.name] = dict(windows=len(data['windows']),
                                               exit_code=data.get('exit_code'))
            continue
        windows = [window_metrics(w) for w in data['windows']]
        extents = sorted({tuple(e) for w in data['windows'] for e in w['extents']})
        metrics = {k: distribution([w[k] for w in windows]) for k in windows[0]}
        group = data['build'] + ('-fixed' if '/fixed-lib:' in
                                data['environment']['LD_LIBRARY_PATH'] else '-baseline')
        run = dict(group=group, exit_code=data.get('exit_code'), extents=extents,
                   metrics=metrics, executable_sha256=data['executable_sha256'],
                   runtime_sha256=data['runtime_sha256'], plugin_sha256=data['plugin_sha256'])
        runs[path.parent.name] = run
        groups.setdefault(group, []).append(run)
    summary = dict(runs=runs, incomplete=incomplete, groups={})
    for name, group in groups.items():
        for identity in ('executable_sha256', 'runtime_sha256', 'plugin_sha256'):
            assert len({r[identity] for r in group}) == 1, (name, identity)
        summary['groups'][name] = {
            k: distribution([r['metrics'][k]['mean'] for r in group])
            for k in group[0]['metrics']}
        print(name, json.dumps(summary['groups'][name]))
    (root/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')


if __name__ == '__main__':
    main()
