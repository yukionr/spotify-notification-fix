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
        ('other-pattern', expected_id, '0x1107b66'),
    ):
        exe = tmp / name
        subprocess.run(cc + (['-DBROKEN_PATTERN'] if name == 'other-pattern' else []) + ['-O2', '-Wall', '-Wextra', '-Werror', '-fPIE', '-pie',
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
                ('matching', 'closed', patched), ('other-build', 'fixed', patched),
                ('other-call', 'fixed', patched), ('other-pattern', 'unchanged', patched),
                ('matching', 'unchanged', plain)]
            for name, mode, env in cases:
                subprocess.run([str(binaries[name]), str(tmp / fmt), mode, 'valid'], env=env, check=True)
                checks += 1
    subprocess.run([str(binaries['matching']), str(tmp / 'invalid'), 'fixed', 'invalid'], env=patched, check=True)
    checks += 1
    # An identical instruction pattern in a shared library is not the client call site.
    shared_call = tmp / 'shared-call.so'
    subprocess.run(cc + ['-fPIC', '-shared', str(root / 'test-callsite.S')] + flags +
        ['-o', str(shared_call)], check=True)
    shared_exe = tmp / 'shared-caller'
    subprocess.run(cc + ['-O2', '-Wall', '-Wextra', '-Werror', str(root / 'test-loader.c'),
        str(shared_call)] + flags + ['-o', str(shared_exe)], check=True)
    subprocess.run([str(shared_exe), str(tmp / 'JPEG'), 'unchanged', 'valid'], env=patched, check=True)
    checks += 1
    provider = tmp / 'notify.so'
    subprocess.run(cc + ['-Wall', '-Wextra', '-Werror', '-fPIC', '-shared',
        str(root / 'test-notify-provider.c'), '-o', str(provider)], check=True)
    for name, build_id, lookup, update in (
        ('matching', expected_id, '0x30848b1', '0x11073ea'),
        ('other-build', '00' * 20, '0x30848b1', '0x11073ea'),
        ('other-lookup', expected_id, '0x30849b1', '0x11073ea'),
        ('other-update', expected_id, '0x30848b1', '0x11074ea'),
    ):
        exe = tmp / ('notification-' + name)
        subprocess.run(cc + ['-O2', '-Wall', '-Wextra', '-Werror', '-fPIE', '-pie',
            str(root / 'test-notification.c'), str(root / 'test-notification-callsite.S'),
            '-ldl', '-Wl,--build-id=0x' + build_id,
            '-Wl,--section-start=.notification_lookup=' + lookup,
            '-Wl,--section-start=.notification_update=' + update, '-o', str(exe)], check=True)
        for mode in ('fixed', 'outside'):
            expected = 'cleared' if mode == 'fixed' else 'retained'
            subprocess.run([str(exe), str(provider), mode, expected], env=patched, check=True)
            checks += 1
        if name == 'matching':
            missing_setter = tmp / 'notify-without-setter.so'
            subprocess.run(cc + ['-Wall', '-Wextra', '-Werror', '-fPIC', '-shared',
                '-DOMIT_SET_IMAGE', str(root / 'test-notify-provider.c'), '-o', str(missing_setter)], check=True)
            subprocess.run([str(exe), str(missing_setter), 'fixed', 'retained'], env=patched, check=True)
            subprocess.run([str(exe), str(provider), 'fixed', 'retained'], env=plain, check=True)
            checks += 2
    real_notify = tmp / 'libnotify-test'
    subprocess.run(cc + ['-O2', '-Wall', '-Wextra', '-Werror', '-fPIE', '-pie',
        str(root / 'test-libnotify.c'), str(root / 'test-notification-callsite.S')] + flags + [
        '-ldl', '-Wl,--build-id=0x' + expected_id,
        '-Wl,--section-start=.notification_lookup=0x30848b1',
        '-Wl,--section-start=.notification_update=0x11073ea', '-o', str(real_notify)], check=True)
    result = subprocess.run([str(real_notify), 'cleared'], env=plain, capture_output=True)
    assert result.returncode == 7, (result.returncode, result.stderr)
    print('PASS: reproduced stale artwork ownership with real libnotify without the shim')
    subprocess.run([str(real_notify), 'cleared'], env=patched, check=True)
    checks += 2
    next_test = root / 'test-dlsym-next.c'
    first, second, next_exe = tmp / 'first.so', tmp / 'second.so', tmp / 'next-test'
    for output, define in ((first, 'FIRST_PROVIDER'), (second, 'SECOND_PROVIDER')):
        subprocess.run(cc + ['-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-shared',
            '-D' + define, str(next_test), '-ldl', '-o', str(output)], check=True)
    subprocess.run(cc + ['-O2', '-Wall', '-Wextra', '-Werror', str(next_test),
        '-ldl', '-o', str(next_exe)], check=True)
    subprocess.run([str(next_exe)], env=plain | {
        'LD_PRELOAD': ':'.join(map(str, (first, second, library)))}, check=True)
    checks += 1
    # Use a private bus to verify the actual libnotify image-data payload.
    large_image = tmp / 'large.jpg'
    Image.effect_noise((640, 640), 80).convert('RGB').save(large_image, quality=98)
    assert large_image.stat().st_size >= 131072
    gio_flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'gio-2.0'], text=True))
    artwork_exe = tmp / 'artwork-state'
    subprocess.run(cc + ['-O2', '-Wall', '-Wextra', '-Werror',
        str(root / 'test-artwork-state.c'), str(root / 'test-artwork-callsite.S'),
        str(root / 'test-notification-callsite.S')] + flags + gio_flags +
        ['-ldl', '-o', str(artwork_exe)], check=True)
    subprocess.run([str(artwork_exe), str(tmp / 'PNG'), str(large_image)], env=patched, check=True)
    checks += 2
print(f'PASS: {checks} checks (PNG/JPEG, guards, streaming, invalid images, repeated notification updates, libnotify ownership)')
