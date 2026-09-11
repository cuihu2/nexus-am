#!/usr/bin/env python3
"""在独立进程中并行运行下载包中的 subtest；不改变 DUT 的核数或开启 FGP。

示例（仿真器参数必须按实际 IT 环境填写，本工具不猜测 ELF 参数名）：
  python3 run-subtests.py --package /data/subtests --jobs 4 \
      --run-dir /data/runs/new-run --cpus 0,1,2,3 -- \
      /absolute/path/to/simv <IT环境参数> {elf}

INDEX.tsv 至少包含 subtest_id、elf 列。{elf} 替换为绝对路径；不经过 shell。
每个子项使用自己的工作目录和 sim.log，summary.tsv 记录进程运行状态。
进程退出码 0 不代表 VCS/用例 PASS，verdict 始终为 NOT_EVALUATED。
必须另行使用 IT 环境规定的 PASS/FAIL 判据审核日志。
"""

import argparse
import csv
import json
import math
import os
from pathlib import Path, PurePosixPath
import queue
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor


FIELDS = (
    "subtest_id", "elf", "status", "exit_code", "timed_out", "cpu",
    "elapsed_seconds", "verdict", "log",
)


def relative_file(root, value, label):
    """不允许清单借助绝对路径、.. 或任意一级符号链接越出下载包。"""
    path = PurePosixPath(value)
    if (not value or path.is_absolute() or ".." in path.parts or
            "\\" in value or any(ord(char) < 32 for char in value)):
        raise ValueError(f"{label}: invalid package-relative path: {value!r}")
    target = root
    for part in path.parts:
        target = target / part
        if target.is_symlink():
            raise ValueError(f"{label}: symlinks are not allowed: {value!r}")
    if not target.is_file():
        raise ValueError(f"{label}: file does not exist: {value!r}")
    if not target.resolve().is_relative_to(root):
        raise ValueError(f"{label}: path escapes package: {value!r}")
    return target


def read_index(package):
    index = relative_file(package, "INDEX.tsv", "index")
    cases = []
    seen = set()
    with index.open(newline="", encoding="utf-8-sig") as source:
        reader = csv.DictReader(source, delimiter="\t")
        if not {"subtest_id", "elf"}.issubset(reader.fieldnames or []):
            raise ValueError("INDEX.tsv requires subtest_id and elf columns")
        for row in reader:
            case_id = row.get("subtest_id", "")
            if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", case_id or ""):
                raise ValueError(f"invalid subtest_id on line {reader.line_num}: {case_id!r}")
            if case_id in seen:
                raise ValueError(f"duplicate subtest_id: {case_id}")
            seen.add(case_id)
            elf_name = row.get("elf") or ""
            elf = relative_file(package, elf_name, case_id)
            cases.append((case_id, elf_name, elf))
    if not cases:
        raise ValueError("INDEX.tsv contains no subtests")
    return cases


def cpu_slots(value, jobs):
    if value is None:
        return [None] * jobs, None
    if not sys.platform.startswith("linux") or not hasattr(os, "sched_getaffinity"):
        raise ValueError("--cpus requires Linux CPU affinity support")
    try:
        cpus = [int(item) for item in value.split(",")]
    except ValueError as error:
        raise ValueError("--cpus requires comma-separated CPU numbers") from error
    if any(cpu < 0 for cpu in cpus) or len(set(cpus)) != len(cpus):
        raise ValueError("--cpus requires distinct nonnegative CPU numbers")
    if len(cpus) < jobs:
        raise ValueError(f"--cpus needs at least {jobs} CPUs, one per concurrent slot")
    unavailable = set(cpus) - os.sched_getaffinity(0)
    if unavailable:
        raise ValueError(f"CPUs outside the allowed cpuset: {sorted(unavailable)}")
    taskset = shutil.which("taskset")
    if taskset is None:
        raise ValueError("--cpus requires the taskset executable")
    return cpus[:jobs], taskset


def terminate_group(process):
    """只清理本工具以 start_new_session 创建的进程组，包括仿真器的子进程。"""
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=0.5)
    except subprocess.TimeoutExpired:
        pass
    # 即便父进程已退出，仍清理同组内忽略 SIGTERM 的子进程。
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def execute_case(case, root, template, cpu, taskset, timeout, stop):
    case_id, elf_name, elf = case
    directory = root / case_id
    directory.mkdir()
    command = [arg.replace("{elf}", str(elf)) for arg in template]
    if cpu is not None:
        command = [taskset, "-c", str(cpu), *command]
    result = {
        "subtest_id": case_id, "elf": elf_name, "status": "LAUNCH_FAILED",
        "exit_code": "", "timed_out": "0", "cpu": "" if cpu is None else str(cpu),
        "elapsed_seconds": "0.000", "verdict": "NOT_EVALUATED",
        "log": f"{case_id}/sim.log",
    }
    started = time.monotonic()
    process = None
    try:
        with (directory / "sim.log").open("x", encoding="utf-8") as log:
            log.write("# runner argv: " + json.dumps(command, ensure_ascii=False) + "\n")
            log.write("# runner: process exit status is not a test PASS verdict\n")
            log.flush()
            try:
                process = subprocess.Popen(
                    command, cwd=directory, stdout=log, stderr=subprocess.STDOUT,
                    stdin=subprocess.DEVNULL, start_new_session=True,
                )
            except OSError as error:
                log.write(f"# runner launch failed: {error}\n")
                return result
            while True:
                if stop.is_set():
                    result["status"] = "INTERRUPTED"
                    terminate_group(process)
                    break
                remaining = None if timeout is None else timeout - (time.monotonic() - started)
                if remaining is not None and remaining <= 0:
                    result["status"] = "TIMEOUT"
                    result["timed_out"] = "1"
                    terminate_group(process)
                    break
                try:
                    process.wait(timeout=0.1 if remaining is None else min(0.1, remaining))
                    result["status"] = "EXITED"
                    break
                except subprocess.TimeoutExpired:
                    continue
            result["exit_code"] = str(process.returncode)
    finally:
        if process is not None and process.poll() is None:
            terminate_group(process)
        result["elapsed_seconds"] = f"{time.monotonic() - started:.3f}"
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--id-prefix", help="只运行 subtest_id 以此前缀开头的子项；仍先校验完整清单")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--run-dir", required=True, type=Path)
    parser.add_argument("--cpus", help="每个并行槽固定绑定一个 Linux host CPU，不是 DUT 核号")
    parser.add_argument("--timeout-seconds", type=float, help="每个仿真进程的墙钟超时，不是 VCS cycle 上限")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    template = args.command[1:] if args.command[:1] == ["--"] else args.command
    try:
        if os.name != "posix":
            raise ValueError("this runner requires POSIX process groups")
        if args.jobs < 1:
            raise ValueError("--jobs must be positive")
        if args.timeout_seconds is not None and (not math.isfinite(args.timeout_seconds) or args.timeout_seconds <= 0):
            raise ValueError("--timeout-seconds must be a finite positive number")
        package = args.package.resolve(strict=True)
        if not package.is_dir():
            raise ValueError("--package must be a directory")
        cases = read_index(package)
        if args.id_prefix is not None:
            cases = [case for case in cases if case[0].startswith(args.id_prefix)]
            if not cases:
                raise ValueError(f"no subtest_id matches --id-prefix {args.id_prefix!r}")
        if not template or not any("{elf}" in arg for arg in template[1:]):
            raise ValueError("provide an absolute executable and an argument containing {elf} after --")
        executable = Path(template[0])
        if not executable.is_absolute() or not executable.is_file() or not os.access(executable, os.X_OK):
            raise ValueError("the command must start with an absolute executable path")
        jobs = min(args.jobs, len(cases))
        cpus, taskset = cpu_slots(args.cpus, jobs)
        if args.run_dir.exists() or args.run_dir.is_symlink():
            raise ValueError("--run-dir already exists; choose a new directory (nothing was overwritten)")
        root = args.run_dir.absolute()
        root.mkdir(parents=True, exist_ok=False)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    pending = queue.Queue()
    for case in cases:
        pending.put(case)
    stop = threading.Event()
    lock = threading.Lock()
    results = []
    interrupted = []

    def signal_handler(signum, frame):
        interrupted.append(signum)
        stop.set()

    previous = {sig: signal.signal(sig, signal_handler) for sig in (signal.SIGINT, signal.SIGTERM)}
    print(f"Running {len(cases)} subtests in {jobs} separate simulator processes; logs: {root}")
    print("Exit code 0 is not VCS PASS: all verdicts remain NOT_EVALUATED.")
    try:
        with (root / "summary.tsv").open("x", newline="", encoding="utf-8") as summary:
            writer = csv.DictWriter(summary, fieldnames=FIELDS, delimiter="\t")
            writer.writeheader()
            summary.flush()

            def record(result):
                with lock:
                    results.append(result)
                    writer.writerow(result)
                    summary.flush()

            def worker(cpu):
                while not stop.is_set():
                    try:
                        case = pending.get_nowait()
                    except queue.Empty:
                        return
                    if stop.is_set():
                        pending.put(case)
                        return
                    try:
                        result = execute_case(case, root, template, cpu, taskset, args.timeout_seconds, stop)
                    except Exception as error:
                        stop.set()
                        result = {
                            "subtest_id": case[0], "elf": case[1], "status": "RUNNER_ERROR",
                            "exit_code": "", "timed_out": "0", "cpu": "" if cpu is None else str(cpu),
                            "elapsed_seconds": "", "verdict": "NOT_EVALUATED", "log": "",
                        }
                        print(f"Runner error for {case[0]}: {error}", file=sys.stderr)
                    record(result)

            with ThreadPoolExecutor(max_workers=jobs) as pool:
                futures = [pool.submit(worker, cpu) for cpu in cpus]
                for future in futures:
                    future.result()
            while not pending.empty():
                case = pending.get_nowait()
                record({
                    "subtest_id": case[0], "elf": case[1], "status": "CANCELLED",
                    "exit_code": "", "timed_out": "0", "cpu": "", "elapsed_seconds": "0.000",
                    "verdict": "NOT_EVALUATED", "log": "",
                })
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)
    print(f"Process summary: {root / 'summary.tsv'} (review IT PASS/FAIL markers separately)")
    if interrupted:
        return 128 + interrupted[0]
    return 0 if all(row["status"] == "EXITED" and row["exit_code"] == "0" for row in results) else 1


if __name__ == "__main__":
    sys.exit(main())
