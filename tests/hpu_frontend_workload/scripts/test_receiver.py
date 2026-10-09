import copy
from pathlib import Path
import subprocess
import tempfile
import unittest

from import_package import validate_dma, validate_host

ROOT = Path(__file__).resolve().parents[1]


class ReceiverTests(unittest.TestCase):
    def setUp(self):
        # 范围校验单测：真指令字段与完整native包的对应另由producer validator把关。
        self.word = 0x00b5202b
        self.allocations = [{"allocation_id": "$compiler/scratch_arena", "line_offset": "10",
                             "line_count": "20", "read_only": "false"}]
        self.dma = [{"instruction_index": "0", "dma_index": "0", "direction": "dstore",
                     "allocation_id": "$compiler/scratch_arena", "line_offset": "16", "line_count": "2",
                     "word_hex": hex(self.word), "operation_index": "0", "operation_id": "node_0"}]
        self.ops = [{"id": "node_0"}]

    def validate(self):
        return validate_dma([self.word], self.dma, self.allocations, self.ops, 40)

    def test_arena_subspan_is_allowed(self):
        writable, first = self.validate()
        self.assertEqual(writable, {16, 17})
        self.assertEqual(first, {16: "dstore", 17: "dstore"})

    def test_arena_overflow_is_rejected(self):
        self.dma[0]["line_offset"] = "29"
        with self.assertRaisesRegex(ValueError, "outside"):
            self.validate()

    def test_readonly_store_is_rejected(self):
        self.allocations[0]["read_only"] = "true"
        with self.assertRaisesRegex(ValueError, "readonly"):
            self.validate()

    def test_zero_length_is_rejected(self):
        self.dma[0]["line_count"] = "0"
        with self.assertRaisesRegex(ValueError, "outside"):
            self.validate()

    def test_instruction_register_mismatch_is_rejected(self):
        self.word = 0x5a80012b
        self.dma[0]["word_hex"] = hex(self.word)
        with self.assertRaisesRegex(ValueError, "register"):
            self.validate()

    def test_missing_dma_is_rejected(self):
        self.dma = []
        with self.assertRaisesRegex(ValueError, "order"):
            self.validate()

    def test_host_diagnostic_export_is_rejected(self):
        report = {key: True for key in ("oracle_verified", "golden_matches_oracle", "model_verified",
                  "raw_physical_words_equal", "host_instruction_model_verified", "optimization_equivalence_verified")}
        report.update({key: False for key in ("instruction_execution_verified", "rtl_verified", "hardware_verified")})
        report.update(overall_status="pass", host_instruction_model={
            "status": "pass", "required": True, "verified": True,
            "ntt_contract": "natural_coefficient_boundaries", "hardware_contract_verified": False})
        validate_host(report)
        broken = copy.deepcopy(report)
        broken["host_instruction_model"]["required"] = False
        with self.assertRaisesRegex(ValueError, "strict"):
            validate_host(broken)
        report["host_instruction_model"]["hardware_contract_verified"] = True
        with self.assertRaisesRegex(ValueError, "hardware"):
            validate_host(report)


class RuntimeTests(unittest.TestCase):
    def test_memory_and_mmio_runtime(self):
        parent = ROOT / "build/host-tests"
        parent.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix="runtime-", dir=parent))
        binary = directory / "runtime-test"
        try:
            subprocess.run(["gcc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-DWORKLOAD_HOST_TEST", "-DWAIT_CYCLES=512",
                            "-I" + str(ROOT / "test/host/include"), "-I" + str(ROOT / "include"),
                            str(ROOT / "test/host/runtime_test.c"),
                            *(str(ROOT / "runtime" / name) for name in ("image.c", "wait.c", "check.c")),
                            "-o", str(binary)], check=True)
            subprocess.run([binary], check=True)
        finally:
            # 只删除本测试明确创建且已运行结束的一个文件，不递归删除目录。
            if binary.exists():
                binary.unlink()
            directory.rmdir()


if __name__ == "__main__":
    unittest.main()
