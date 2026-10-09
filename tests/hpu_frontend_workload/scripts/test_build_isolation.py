from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from build import build_case

ROOT = Path(__file__).resolve().parents[1]


class BuildIsolationTests(unittest.TestCase):
    def test_actual_build_command_keeps_case_paths_out_of_recursive_make(self):
        parent = ROOT / "build/make-tests"
        parent.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix="isolation-", dir=parent))
        case_dir = directory / "artifact/minimal/BUILD_PROBE"
        recorded = []

        class CommandCaptured(Exception):
            pass

        def capture(command, **kwargs):
            recorded.extend(command)
            raise CommandCaptured

        args = SimpleNamespace(output=directory, am_home=ROOT / "test/build-isolation",
                               arch="riscv64-xs", cross="riscv64-linux-gnu-", wait_cycles=1234)
        spec = {"case_id": "BUILD_PROBE", "source": "src/ckks_n128.c"}
        try:
            # Capture the command emitted by build_case, then pass it through
            # the real Makefile.case so both entry points stay under test.
            with patch("build.run", side_effect=capture):
                with self.assertRaises(CommandCaptured):
                    build_case(args, spec, directory, {"program_stem": "probe"}, "minimal")
            result = subprocess.run(recorded, text=True, check=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            values = dict(line.split("=", 1) for line in result.stdout.splitlines()
                          if line.startswith(("parent_dst=", "parent_binary=",
                                              "child_dst=", "child_binary=")))
            self.assertEqual(values["parent_dst"],
                             str(directory / "objects/minimal/1234/BUILD_PROBE"))
            self.assertEqual(values["parent_binary"], str(case_dir / "BUILD_PROBE"))
            self.assertEqual(values["child_dst"], "library-default-objects")
            self.assertEqual(values["child_binary"], "library-default-binary")
        finally:
            # The make probe creates no files; remove only directories owned by this test.
            case_dir.rmdir()
            (directory / "artifact/minimal").rmdir()
            (directory / "artifact").rmdir()
            directory.rmdir()


if __name__ == "__main__":
    unittest.main()
