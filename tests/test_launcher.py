import contextlib
import importlib.machinery
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / 'bin/spotify-launcher'

class LauncherTest(unittest.TestCase):
    def setUp(self):
        loader = importlib.machinery.SourceFileLoader('spotify_fix_launcher', str(WRAPPER))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        self.module = importlib.util.module_from_spec(spec)
        with patch.object(sys, "dont_write_bytecode", True):
            loader.exec_module(self.module)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.home = Path(self.tmp.name)
        self.binary = self.home / '.local/share/spotify-launcher/install/usr/share/spotify/spotify'
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(b'test executable')

    def run_main(self, args, preload='', prepare_error=None):
        with patch.object(self.module.Path, 'home', return_value=self.home), patch.object(self.module.os, 'execve') as execute, patch.object(self.module, 'prepare_library', return_value=self.home / 'fix.so', side_effect=prepare_error) as prepare, patch.dict(os.environ, {'LD_PRELOAD': preload}, clear=False), contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            result = self.module.main(args)
        return result, execute, prepare

    def test_preserves_launcher_arguments_and_preload(self):
        result, execute, prepare = self.run_main(['--', 'spotify:track:fixture'], '/existing.so')
        prepare.assert_called_once()
        args = execute.call_args.args
        self.assertEqual(args[0], '/usr/bin/spotify-launcher')
        self.assertEqual(args[1], ['spotify-launcher', '--', 'spotify:track:fixture'])
        self.assertEqual(args[2]['LD_PRELOAD'], str(self.home / 'fix.so') + ':/existing.so')

    def test_updated_executable_still_loads_patch(self):
        self.binary.write_bytes(b'updated executable')
        result, execute, prepare = self.run_main(['--version'], '/existing.so')
        prepare.assert_called_once()
        self.assertEqual(execute.call_args.args[2]['LD_PRELOAD'], str(self.home / 'fix.so') + ':/existing.so')

    def test_prepare_does_not_launch(self):
        with contextlib.redirect_stdout(io.StringIO()):
            result, execute, prepare = self.run_main(['--prepare-notification-fix'])
        self.assertEqual(result, 0)
        prepare.assert_called_once()
        execute.assert_not_called()

    def test_missing_client_delegates_install_to_standard_launcher(self):
        self.binary.unlink()
        result, execute, prepare = self.run_main([])
        prepare.assert_not_called()
        execute.assert_called_once()

    def test_prepare_accepts_updated_executable(self):
        self.binary.write_bytes(b'updated executable')
        result, execute, prepare = self.run_main(['--prepare-notification-fix'])
        self.assertEqual(result, 0)
        execute.assert_not_called()
        prepare.assert_called_once()

    def test_compilation_failure_preserves_normal_startup(self):
        result, execute, prepare = self.run_main([], '/existing.so', OSError('compiler unavailable'))
        self.assertEqual(execute.call_args.args[2]['LD_PRELOAD'], '/existing.so')

    def test_prepare_reports_compilation_failure_without_launching(self):
        result, execute, prepare = self.run_main(['--prepare-notification-fix'], prepare_error=OSError('compiler unavailable'))
        self.assertEqual(result, 1)
        execute.assert_not_called()

if __name__ == '__main__':
    subprocess.run(['python3', str(ROOT / 'tests/run_native.py')], check=True)
    unittest.main()
