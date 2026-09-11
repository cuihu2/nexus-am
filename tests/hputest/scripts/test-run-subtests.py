#!/usr/bin/env python3
"""只运行小型 host 假进程，检查隔离、调度、退出记录和清理；不运行 VCS。"""

import csv
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest


RUNNER = Path(__file__).with_name("run-subtests.py")
DUMMY = r'''
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

mode, events, elf = sys.argv[1:]
identity = Path.cwd().name
print("OBS " + json.dumps({"cwd": str(Path.cwd()), "elf": elf,
      "affinity": sorted(os.sched_getaffinity(0))}), flush=True)
def event(kind):
    with open(events, "a") as output:
        output.write(json.dumps([time.monotonic(), kind, identity]) + "\n")
event("start")
if mode == "parallel":
    time.sleep(0.2)
elif mode == "hang":
    child = subprocess.Popen([sys.executable, "-c",
        "import signal,time; signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(30)"])
    Path("child.pid").write_text(str(child.pid))
    Path("parent.pid").write_text(str(os.getpid()))
    time.sleep(30)
event("end")
if mode == "exit":
    sys.exit(int(Path(elf).read_text()))
'''


def live_process(pid):
    """已退出、等待 init 回收的孤儿僵尸不算仍在运行的仿真进程。"""
    try:
        state = Path(f"/proc/{pid}/stat").read_text().split(") ", 1)[1].split()[0]
        return state != "Z"
    except FileNotFoundError:
        return False


@unittest.skipUnless(sys.platform.startswith("linux"), "runner integration checks require Linux")
class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="hpu-subtest-runner-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package = self.root / "package"
        self.package.mkdir()
        self.run_dir = self.root / "run"
        self.events = self.root / "events.jsonl"
        self.dummy = self.root / "dummy.py"
        self.dummy.write_text(DUMMY)

    def package_cases(self, count=3, overrides=None):
        rows = []
        for index in range(count):
            case_id = f"subtest_{index}"
            name = f"elf/case {index}.elf"
            target = self.package / name
            target.parent.mkdir(exist_ok=True)
            target.write_text(str((overrides or {}).get(index, 0)))
            rows.append({"subtest_id": case_id, "elf": name, "parent_case_id": "parent"})
        self.write_index(rows)
        return rows

    def write_index(self, rows):
        with (self.package / "INDEX.tsv").open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=("subtest_id", "elf", "parent_case_id"), delimiter="\t")
            writer.writeheader()
            writer.writerows(rows)

    def command(self, mode="parallel", options=(), template=None):
        return [sys.executable, str(RUNNER), "--package", str(self.package), "--run-dir", str(self.run_dir),
                *options, "--", *(template or [sys.executable, str(self.dummy), mode, str(self.events), "{elf}"])]

    def run_runner(self, **kwargs):
        return subprocess.run(self.command(**kwargs), capture_output=True, text=True, timeout=15)

    def summary(self):
        with (self.run_dir / "summary.tsv").open() as source:
            return list(csv.DictReader(source, delimiter="\t"))

    def assert_no_child_left(self):
        for pid_file in self.run_dir.glob("*/child.pid"):
            pid = int(pid_file.read_text())
            deadline = time.monotonic() + 2
            while live_process(pid) and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertFalse(live_process(pid), f"child {pid} still running")

    def test_parallel_limit_and_separate_workdirs_logs(self):
        self.package_cases(6)
        result = self.run_runner(options=("--jobs", "2"))
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = self.summary()
        self.assertEqual(len(rows), 6)
        active = maximum = 0
        for _, event, _ in sorted(json.loads(line) for line in self.events.read_text().splitlines()):
            active += 1 if event == "start" else -1
            maximum = max(maximum, active)
            self.assertLessEqual(active, 2)
        self.assertEqual((active, maximum), (0, 2))
        for row in rows:
            observation = next(line[4:] for line in (self.run_dir / row["log"]).read_text().splitlines() if line.startswith("OBS "))
            observation = json.loads(observation)
            self.assertEqual(observation["cwd"], str(self.run_dir / row["subtest_id"]))
            self.assertEqual(observation["elf"], str(self.package / row["elf"]))
            self.assertEqual(row["verdict"], "NOT_EVALUATED")

    def test_nonzero_exit_recorded_without_pass_inference(self):
        self.package_cases(2, overrides={1: 7})
        result = self.run_runner(mode="exit", options=("--jobs", "2"))
        self.assertEqual(result.returncode, 1, result.stderr)
        rows = {row["subtest_id"]: row for row in self.summary()}
        self.assertEqual(rows["subtest_0"]["exit_code"], "0")
        self.assertEqual(rows["subtest_1"]["exit_code"], "7")
        self.assertEqual({row["verdict"] for row in rows.values()}, {"NOT_EVALUATED"})
        self.assertEqual({row["status"] for row in rows.values()}, {"EXITED"})

    def test_id_prefix_runs_only_matching_cases_after_full_validation(self):
        rows = self.package_cases(4)
        rows[0]["subtest_id"] = "HPU_IT_DIR_INS_C0_001__round0"
        rows[1]["subtest_id"] = "HPU_IT_DIR_INS_C0_001__round1"
        self.write_index(rows)
        options = ("--id-prefix", "HPU_IT_DIR_INS_C0_001__", "--jobs", "2")
        # 未选中行也必须通过路径验证，不能用过滤隐藏破损/越界清单。
        original = rows[3]["elf"]
        rows[3]["elf"] = "../outside.elf"
        self.write_index(rows)
        self.assertEqual(self.run_runner(mode="exit", options=options).returncode, 2)
        self.assertFalse(self.run_dir.exists())
        rows[3]["elf"] = original
        self.write_index(rows)
        result = self.run_runner(mode="exit", options=options)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual({row["subtest_id"] for row in self.summary()},
                         {rows[0]["subtest_id"], rows[1]["subtest_id"]})
        self.assertFalse((self.run_dir / "subtest_2").exists())
        self.assertFalse((self.run_dir / "subtest_3").exists())

    def test_id_prefix_without_matches_rejects_before_creating_run_dir(self):
        self.package_cases(2)
        result = self.run_runner(options=("--id-prefix", "NOT_PRESENT__"))
        self.assertEqual(result.returncode, 2)
        self.assertIn("no subtest_id matches", result.stderr)
        self.assertFalse(self.run_dir.exists())

    def test_timeout_kills_own_process_group(self):
        self.package_cases(1)
        result = self.run_runner(mode="hang", options=("--timeout-seconds", "0.5"))
        self.assertEqual(result.returncode, 1, result.stderr)
        row = self.summary()[0]
        self.assertEqual((row["status"], row["timed_out"], row["verdict"]), ("TIMEOUT", "1", "NOT_EVALUATED"))
        self.assert_no_child_left()

    def test_sigint_cancels_pending_and_kills_running_group(self):
        self.package_cases(3)
        process = subprocess.Popen(self.command(mode="hang"), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 5
            while not (self.run_dir / "subtest_0/child.pid").exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue((self.run_dir / "subtest_0/child.pid").exists())
            process.send_signal(signal.SIGINT)
            stdout, stderr = process.communicate(timeout=5)
            self.assertEqual(process.returncode, 130, stdout + stderr)
            rows = self.summary()
            self.assertEqual([row["status"] for row in rows].count("INTERRUPTED"), 1)
            self.assertEqual([row["status"] for row in rows].count("CANCELLED"), 2)
            self.assert_no_child_left()
        finally:
            if process.poll() is None:
                process.terminate()
                process.communicate(timeout=5)

    @unittest.skipUnless(shutil.which("taskset"), "taskset unavailable")
    def test_affinity_slots_do_not_share_cpu_concurrently(self):
        allowed = sorted(os.sched_getaffinity(0))
        if len(allowed) < 2:
            self.skipTest("two allowed CPUs required")
        self.package_cases(4)
        result = self.run_runner(options=("--jobs", "2", "--cpus", f"{allowed[0]},{allowed[1]}"))
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = {row["subtest_id"]: row for row in self.summary()}
        occupied = set()
        for _, event, identity in sorted(json.loads(line) for line in self.events.read_text().splitlines()):
            cpu = int(rows[identity]["cpu"])
            if event == "start":
                self.assertNotIn(cpu, occupied)
                occupied.add(cpu)
            else:
                occupied.remove(cpu)
        self.assertFalse(occupied)
        for row in rows.values():
            log = (self.run_dir / row["log"]).read_text()
            observation = json.loads(next(line[4:] for line in log.splitlines() if line.startswith("OBS ")))
            self.assertEqual(observation["affinity"], [int(row["cpu"])])

    def test_rejects_bad_paths_ids_missing_files_and_symlinks(self):
        rows = self.package_cases(1)
        outside = self.root / "outside.elf"
        outside.write_text("outside")
        (self.package / "symlink.elf").symlink_to(outside)
        (self.package / "symlinkdir").symlink_to(outside.parent, target_is_directory=True)
        bad_rows = [
            [{"subtest_id": "safe", "elf": "../outside.elf"}],
            [{"subtest_id": "safe", "elf": str(outside)}],
            [{"subtest_id": "safe", "elf": "symlink.elf"}],
            [{"subtest_id": "safe", "elf": "symlinkdir/outside.elf"}],
            [{"subtest_id": "safe", "elf": "missing.elf"}],
            [{"subtest_id": "../escape", "elf": rows[0]["elf"]}],
            [rows[0], rows[0]],
        ]
        for bad in bad_rows:
            with self.subTest(rows=bad):
                self.write_index(bad)
                result = self.run_runner()
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertFalse(self.run_dir.exists())

    def test_rejects_existing_run_directory(self):
        self.package_cases(1)
        self.run_dir.mkdir()
        original = self.run_dir / "sim.log"
        original.write_text("do not replace")
        result = self.run_runner()
        self.assertEqual(result.returncode, 2)
        self.assertEqual(original.read_text(), "do not replace")

    def test_rejects_invalid_cpuset_and_slot_shortage(self):
        self.package_cases(3)
        allowed = sorted(os.sched_getaffinity(0))
        for options in [
            ("--cpus", str(max(allowed) + 1)),
            ("--cpus", f"{allowed[0]},{allowed[0]}"),
            ("--jobs", "2", "--cpus", str(allowed[0])),
        ]:
            with self.subTest(options=options):
                self.assertEqual(self.run_runner(options=options).returncode, 2)
                self.assertFalse(self.run_dir.exists())

    def test_requires_explicit_absolute_command_and_elf_placeholder(self):
        self.package_cases(1)
        for template in [["python3", "{elf}"], [sys.executable, "-c", "pass"]]:
            with self.subTest(template=template):
                self.assertEqual(self.run_runner(template=template).returncode, 2)
                self.assertFalse(self.run_dir.exists())

    def test_filename_is_not_interpreted_by_shell(self):
        rows = self.package_cases(1)
        target = "elf/$(touch SHOULD_NOT_EXIST);.elf"
        (self.package / target).write_text("0")
        rows[0]["elf"] = target
        self.write_index(rows)
        result = self.run_runner(mode="exit")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.run_dir / "subtest_0/SHOULD_NOT_EXIST").exists())


if __name__ == "__main__":
    unittest.main()
