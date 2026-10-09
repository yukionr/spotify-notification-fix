#!/usr/bin/env python3
import os
import shlex
import subprocess
import tempfile
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parent
flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'gdk-pixbuf-2.0'], text=True))
cc = shlex.split(os.environ.get('CC', 'cc'))
expected_id = 'ab6bc2bb7cde82bf437c8d19c673ae1c044f7e25'
checks = 0
source = root.parent / 'src/spotify-notification-fix.c'
with tempfile.TemporaryDirectory(prefix='spotify-notification-test.') as tmp:
    tmp = Path(tmp)
    for fmt in ('PNG', 'JPEG'):
        Image.new('RGB', (64, 64), (60, 120, 180)).save(tmp / fmt, format=fmt)
    (tmp / 'invalid').write_bytes(b'not an image')
    library = tmp / 'fix.so'
    subprocess.run(cc + ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-shared', str(source)] + shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'gdk-pixbuf-2.0'], text=True)) + ['-ldl', '-pthread', '-o', str(library)], check=True)
    binaries = {}
    for name, build_id, address in (
        ('matching', expected_id, '0x1107b66'),
        ('other-build', '00' * 20, '0x1107b66'),
        ('other-call', expected_id, '0x1107c66'),
    ):
        exe = tmp / name
        subprocess.run(cc + ['-O2', '-Wall', '-Wextra', '-Werror', '-fPIE', '-pie',
            str(root / 'test-loader.c'), str(root / 'test-callsite.S')] + flags + [
            '-Wl,--build-id=0x' + build_id, '-Wl,--section-start=.notification_call=' + address,
            '-o', str(exe)], check=True)
        binaries[name] = exe
    plain = os.environ.copy()
    plain.pop('LD_PRELOAD', None)
    patched = plain | {'LD_PRELOAD': str(library)}
    # This is the original compatibility failure, without the shim.
    result = subprocess.run([str(binaries['matching']), str(tmp / 'JPEG'), 'fixed', 'valid'], env=plain, capture_output=True)
    assert result.returncode == 7, (result.returncode, result.stderr)
    print('PASS: reproduced the image lookup failure without the shim')
    checks += 1
    for fmt in ('PNG', 'JPEG'):
        for repeat in range(2):
            cases = [('matching', 'fixed', patched), ('matching', 'outside', patched),
                ('matching', 'closed', patched), ('other-build', 'unchanged', patched),
                ('other-call', 'unchanged', patched), ('matching', 'unchanged', plain)]
            for name, mode, env in cases:
                subprocess.run([str(binaries[name]), str(tmp / fmt), mode, 'valid'], env=env, check=True)
                checks += 1
    subprocess.run([str(binaries['matching']), str(tmp / 'invalid'), 'fixed', 'invalid'], env=patched, check=True)
    checks += 1
print(f'PASS: {checks} checks (PNG/JPEG, ASLR, build/call-site mismatch, streaming, closed loader, invalid image)')
