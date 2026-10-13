#!/usr/bin/env python3
"""Bounded Android smoke receipt; use an explicit adb serial and private output."""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import subprocess
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from private_paths import require_local
sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_apk
from adb_client import ACTIVITY_CLASS, ADB_TRANSFER_TIMEOUT, LAUNCHER_PACKAGE, PACKAGE, device_command, parse_pids, single_pid

ROOT = Path(__file__).resolve().parents[2]
ACTIVITY = PACKAGE + '/' + ACTIVITY_CLASS
# SDL renderer names; the shell reports the active one as render_driver.
BACKENDS = {'opengles': 'opengles2', 'vulkan': 'vulkan'}

def activity_command(backend: str | None, package: str = PACKAGE) -> list[str]:
    command=['shell','am','start','-n',f'{package}/{ACTIVITY_CLASS}']
    if backend:
        if backend not in BACKENDS: raise ValueError('Unknown graphics backend')
        # Sc64Activity.getArguments() forwards this string array to the shell.
        command += ['--esa','com.nomadic.sc64.ARGUMENTS',
                    f'--config=sc64.render_driver={BACKENDS[backend]}']
    return command

def validate_profile(device: dict, required_page_size: int) -> None:
    if device['ro.product.cpu.abi'] != 'arm64-v8a':
        raise ValueError('Runtime probe requires an arm64-v8a device')
    if required_page_size and device['page_size'] != required_page_size:
        raise ValueError(f"Required page size {required_page_size}; device reports {device['page_size']}")

def analyze(log: str) -> dict:
    lines = log.splitlines()
    samples = []
    transitions = []
    for index, line in enumerate(lines):
        if 'SC64 {' in line:
            try: samples.append((index, json.loads(line[line.index('SC64 {') + 5:])))
            except json.JSONDecodeError: continue
        match = re.search(r'SC64 Android suspended=(\d+) fields=(\d+) tasks=(\d+) samples=(\d+)', line)
        if match: transitions.append((index, tuple(map(int, match.groups()))))
    def active(sample: dict) -> bool:
        return sample.get('running') is True and sample.get('paused') is False and not sample.get('error')
    live_rows = [(index,sample) for index,sample in samples if active(sample)]
    live = [sample for _,sample in live_rows]
    intervals = []
    for (a_index,a), (b_index,b) in zip(live_rows, live_rows[1:]):
        if any(a_index < index <= b_index for index,_ in transitions): continue
        delta = b['host_seconds'] - a['host_seconds']
        if delta > 0: intervals.append((b['fields'] - a['fields']) / delta)
    pairs = []
    for (_, pause), (resume_index, resume) in zip(transitions, transitions[1:]):
        if pause[0] != 1 or resume[0] != 0: continue
        # A later pause belongs to a different interval; do not borrow its
        # eventual progress to accept a resume that remained stuck.
        end = next((i for i, _ in transitions if i > resume_index), len(lines))
        after = [s for i, s in samples if resume_index < i < end]
        measured = len(after) >= 2 and all(active(s) for s in after)
        counters = ('fields', 'tasks', 'samples')
        advances = measured and all(
            after[-1].get(key, 0) > after[0].get(key, 0) >= resume[pos]
            for pos, key in enumerate(counters, 1))
        pcm_resumed = measured and after[-1].get('audio_nonzero_frames', 0) > after[0].get('audio_nonzero_frames', 0)
        presentation_resumed = measured and after[-1].get('presented',0) > after[0].get('presented',0)
        pairs.append({'pause': pause, 'resume': resume,
                      'guest_delta': [resume[i] - pause[i] for i in (1, 2, 3)],
                      'post_resume_samples': len(after), 'post_resume_progress': bool(advances),
                      'post_resume_pcm': bool(pcm_resumed),
                      'post_resume_presentation': bool(presentation_resumed)})
    return {'telemetry_samples': len(samples), 'field_rates': intervals,
            'guest_progress': len(live)>=2 and live[-1]['fields']>live[0]['fields'],
            'presentation_progress': len(live)>=2 and live[-1].get('presented',0)>live[0].get('presented',0),
            'render_drivers': sorted(set(s['render_driver'] for s in live if 'render_driver' in s)),
            'pcm_nonzero': any(s.get('audio_nonzero_frames',0)>0 for s in live),
            'guest_errors': [s['error'] for _, s in samples if s.get('error')],
            'android_crash': 'FATAL EXCEPTION' in log or 'Fatal signal' in log,
            'suspend_resume_pairs': pairs,
            'suspension_freezes_guest': bool(pairs) and all(all(abs(delta) <= 1 for delta in p['guest_delta']) for p in pairs),
            'resume_accepted': bool(pairs) and all(p['post_resume_progress'] and p['post_resume_pcm']
                and p['post_resume_presentation'] for p in pairs)
                and bool(samples) and active(samples[-1][1]),
            'physical_device_accepted': False}

def validate_receipt(report: dict) -> None:
    native = report['native']
    if not report.get('process_unchanged'):
        raise ValueError('Android app exited or changed process during the smoke')
    if not native['guest_progress'] or not native['pcm_nonzero'] or native['guest_errors'] or native['android_crash']:
        raise ValueError('Native guest/PCM progress gate failed; inspect the private log (ROM import may be needed)')
    if not native['presentation_progress']:
        raise ValueError('Shell presentation did not advance')
    backend = report.get('requested_backend')
    if backend and native['render_drivers'] != [BACKENDS[backend]]:
        raise ValueError('Actual graphics adapter differs from the requested backend')
    validate_profile(report['device'], report.get('required_page_size',0))
    if report['home_requested'] and (not native['suspension_freezes_guest'] or not native['resume_accepted']):
        raise ValueError('Android freeze/resume gate failed; measured post-return guest/PCM progress is required')

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', default=PACKAGE, help='Application ID of the installed app (default: the personal APK)')
    parser.add_argument('--adb', default='adb', help='adb executable (default: PATH)')
    parser.add_argument('--apk', type=Path, default=ROOT / 'local/dist/StarCraft64-android.apk')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--install', action='store_true', help='Install this personal APK before restarting its app')
    parser.add_argument('--bundled-rom', action='store_true', help='Require and audit a bundled-ROM build')
    parser.add_argument('--home', action='store_true', help='Exercise Home/return for 8 seconds during the run')
    parser.add_argument('--required-page-size', type=int, choices=(4096,16384), default=0,
                        help='Reject the wrong kernel page size before installing or restarting')
    parser.add_argument('--backend', choices=tuple(BACKENDS),
                        help='Request an SDL renderer and verify actual telemetry')
    args = parser.parse_args()
    output=require_local(args.output, new=True, root=ROOT, what='Output')
    if not 20<=args.seconds<=900: raise ValueError('seconds must be in [20,900]')
    if args.home and args.seconds < 30: raise ValueError('Home/return requires at least 30 seconds for post-return telemetry')
    adb = device_command(args.adb, args.serial)
    def read_pid() -> str:
        try: return adb('shell','pidof',args.package)
        except subprocess.CalledProcessError as error:
            if error.returncode != 1: raise
            return ''
    audit=check_apk.audit(args.apk, bundled_rom=args.bundled_rom, launcher=args.package == LAUNCHER_PACKAGE)
    output.mkdir(parents=True)
    device={key:adb('shell','getprop',key) for key in (
        'ro.product.model','ro.build.version.release','ro.build.fingerprint','ro.product.cpu.abi','ro.kernel.qemu')}
    device['page_size']=int(adb('shell','getconf','PAGE_SIZE'))
    validate_profile(device,args.required_page_size)
    if args.install: adb('install','-r',str(args.apk.resolve()),timeout=ADB_TRANSFER_TIMEOUT)
    installed_path=adb('shell','pm','path',args.package).removeprefix('package:')
    installed=output / 'installed.apk'
    adb('pull',installed_path,str(installed),timeout=ADB_TRANSFER_TIMEOUT)
    if hashlib.sha256(installed.read_bytes()).hexdigest()!=audit['apk_sha256']:
        raise ValueError('Installed APK differs from the selected build')
    installed.unlink()
    # PID reuse and retained log buffers must not borrow an earlier run's proof.
    log_start=adb('shell',"date '+%m-%d %H:%M:%S.000'")
    start_command=activity_command(args.backend, args.package)
    adb('shell','am','force-stop',args.package)
    adb(*start_command)
    pid=None
    deadline=time.monotonic()+10
    while pid is None and time.monotonic()<deadline:
        pid = single_pid(read_pid(), args.package)
        if pid is None: time.sleep(.2)
    if pid is None: raise ValueError('Android app did not start')
    start=time.monotonic(); hidden=False; returned=False
    while time.monotonic()-start<args.seconds:
        elapsed=time.monotonic()-start
        if args.home and not hidden and elapsed>=10:
            adb('shell','input','keyevent','KEYCODE_HOME'); hidden=True
        if hidden and not returned and elapsed>=18:
            adb(*start_command); returned=True
        time.sleep(.25)
    final_pid = read_pid()
    final_pids = parse_pids(final_pid)
    log=adb('logcat','-d','-T',log_start,f'--pid={pid}')
    (output / 'engine.log').write_text(log+'\n')
    report={'schema':3,'device':device,'apk':audit,'pid':pid,'requested_seconds':args.seconds,
            'required_page_size':args.required_page_size,'requested_backend':args.backend,'log_start':log_start,
            'final_pid':final_pid, 'process_unchanged':final_pids == (pid,),
            'home_requested':args.home,'native':analyze(log)}
    report['runtime_profile_accepted']=False
    try:
        if hashlib.sha256(args.apk.read_bytes()).hexdigest()!=audit['apk_sha256']:
            raise ValueError('Selected APK changed during runtime probe')
        validate_receipt(report)
    except ValueError as error:
        report['acceptance_error']=str(error)
        (output / 'run.json').write_text(json.dumps(report,indent=2)+'\n')
        raise
    report['runtime_profile_accepted']=True
    (output / 'run.json').write_text(json.dumps(report,indent=2)+'\n')
    print(output / 'run.json')

if __name__ == '__main__': main()
