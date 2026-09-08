"""Exercise command and report boundaries without running benchmark hot paths."""

import json
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
BENCHMARK_STUB = r'''
#include "tools/acquisition_benchmark.h"
#include "tools/json_output.h"
#include <limits>
#include <locale>
#include <iostream>

namespace satcomfec::benchmark {
const std::vector<WorkloadDefinition>& acquisition_benchmark_workloads() {
    static const std::vector<WorkloadDefinition> workloads = {{"small", 1, 1, 1, 1}};
    return workloads;
}
BenchmarkArtifacts run_acquisition_benchmark(const BenchmarkOptions& options) {
    std::cerr << "benchmark stub executed\n";
    return {true, "{\"seed\":" + std::to_string(options.deterministic_seed) +
                      ",\"samples\":" + std::to_string(options.timed_sample_count) +
                      "}\n", "seed,samples\n", ""};
}
}
'''
JSON_DRIVER = r'''
#include "tools/json_output.h"
#include <iostream>
#include <limits>
#include <locale>
struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '.'; }
    std::string do_grouping() const override { return "\3"; }
};
int main(int argc, char**) {
    if (argc > 1) {
        std::cout << "\"" << satcomfec::tools::escape_json(u8"café/λ/🛰") << "\"\n";
        return 0;
    }
    std::locale::global(std::locale(std::locale::classic(), new CommaDecimal));
    using satcomfec::tools::format_float;
    using satcomfec::tools::escape_json;
    std::cout << "[" << format_float(1234.25, 3) << ","
              << format_float(std::numeric_limits<double>::infinity()) << ","
              << format_float(-std::numeric_limits<double>::infinity()) << ","
              << format_float(std::numeric_limits<double>::quiet_NaN()) << ",\""
              << escape_json(u8"café/λ/🛰") << "\",\""
              << escape_json(std::string("\xF4\x90\x80\x80\xED\xA0\x80\xC0\xAF\xC3"))
              << "\",\"" << escape_json(std::string("\x01\x7F\n\"\\"))
              << "\"]\n";
}
'''


class CliValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        stub = cls.directory / "benchmark_stub.cpp"
        stub.write_text(BENCHMARK_STUB, encoding="utf-8")
        cls.benchmark = cls.directory / "benchmark_cli"
        cls.json_driver = cls.directory / "json_driver"
        driver = cls.directory / "json_driver.cpp"
        driver.write_text(JSON_DRIVER, encoding="utf-8")
        for output, sources in (
            (cls.benchmark, [ROOT_DIR / "tools/benchmark_acquisition.cpp", stub]),
            (cls.json_driver, [driver]),
        ):
            subprocess.run(
                [*compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT_DIR), *map(str, sources), "-o", str(output)],
                check=True, capture_output=True, text=True,
            )

    def run_benchmark(self, *arguments: str, cwd: Path | None = None):
        return subprocess.run(
            [str(self.benchmark), *arguments], cwd=cwd or self.directory,
            check=False, capture_output=True, text=True, timeout=10,
        )

    def assert_rejected_before_execution(self, result) -> None:
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertNotIn("benchmark stub executed", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_unsigned_arguments_reject_whitespace_prefixed_negatives(self) -> None:
        for option in ("--samples", "--warmup-rounds", "--seed"):
            for value in (" -1", "\t-1", "-1", "18446744073709551616"):
                with self.subTest(option=option, value=value):
                    self.assert_rejected_before_execution(self.run_benchmark(option, value))

    def test_seed_preserves_decimal_hex_and_legacy_octal_formats(self) -> None:
        for value, expected in (("010", 8), ("10", 10),
                                ("0x10", 16), ("0X10", 16), ("+0x10", 16),
                                ("+10", 10), ("0", 0),
                                ("18446744073709551615", 18446744073709551615)):
            with self.subTest(value=value):
                result = self.run_benchmark("--seed", value)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(json.loads(result.stdout)["seed"], expected)

    def test_invalid_numbers_are_rejected(self) -> None:
        for option, values in (
            ("--seed", ("+", "0x", "12x", "1.0", "08")),
            ("--samples", ("2", "3.5", "3x")),
            ("--min-sample-ms", ("nan", "inf", "0", "-1", "1ms")),
        ):
            for value in values:
                with self.subTest(option=option, value=value):
                    self.assert_rejected_before_execution(self.run_benchmark(option, value))

    def test_missing_values_do_not_consume_the_next_option(self) -> None:
        for option in ("--workload", "--warmup-rounds", "--samples",
                       "--min-sample-ms", "--seed", "--json", "--csv"):
            for following in ((), ("",), ("--help",)):
                with self.subTest(option=option, following=following):
                    self.assert_rejected_before_execution(self.run_benchmark(option, *following))

    def test_same_report_path_preserves_existing_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "report.json"
            output.write_text("existing report", encoding="utf-8")
            result = self.run_benchmark("--json", str(output), "--csv", str(output))
            self.assert_rejected_before_execution(result)
            self.assertEqual(output.read_text(encoding="utf-8"), "existing report")

    def test_normalised_report_alias_is_rejected_before_creation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            result = self.run_benchmark("--json", "report.json", "--csv", "./report.json", cwd=directory)
            self.assert_rejected_before_execution(result)
            self.assertFalse((directory / "report.json").exists())

    def test_hardlink_report_alias_preserves_existing_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "report.json"
            alias = Path(temporary) / "report.csv"
            output.write_text("existing report", encoding="utf-8")
            os.link(output, alias)
            result = self.run_benchmark("--json", str(output), "--csv", str(alias))
            self.assert_rejected_before_execution(result)
            self.assertEqual(output.read_text(encoding="utf-8"), "existing report")
            self.assertEqual(alias.read_text(encoding="utf-8"), "existing report")

    def test_symlink_report_alias_preserves_existing_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "report.json"
            alias = Path(temporary) / "report.csv"
            output.write_text("existing report", encoding="utf-8")
            try:
                alias.symlink_to(output)
            except OSError as error:
                self.skipTest(f"symlink creation unavailable: {error}")
            result = self.run_benchmark("--json", str(output), "--csv", str(alias))
            self.assert_rejected_before_execution(result)
            self.assertEqual(output.read_text(encoding="utf-8"), "existing report")

    def test_invalid_report_parent_is_rejected_before_execution(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for output in (directory, directory / "missing" / "report.json"):
                with self.subTest(output=output):
                    result = self.run_benchmark("--json", str(output))
                    self.assert_rejected_before_execution(result)

    def test_distinct_report_outputs_match_stdout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            result = self.run_benchmark("--json", "report.json", "--csv", "report.csv", cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((directory / "report.json").read_text(encoding="utf-8"), result.stdout)
            self.assertEqual((directory / "report.csv").read_text(encoding="utf-8"), "seed,samples\n")

    def test_json_numbers_use_decimal_point_and_null_for_nonfinite_values(self) -> None:
        result = subprocess.run([str(self.json_driver)], capture_output=True, text=True, check=True)
        values = json.loads(result.stdout)
        self.assertEqual(values[:4], [1234.25, None, None, None])
        self.assertTrue(result.stdout.startswith("[1234.250,null,null,null,"))

    def test_json_text_preserves_utf8(self) -> None:
        result = subprocess.run([str(self.json_driver), "utf8"], capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(result.stdout), "café/λ/🛰")

    def test_json_text_round_trips_utf8_controls_and_malformed_bytes(self) -> None:
        result = subprocess.run([str(self.json_driver)], capture_output=True, text=True, check=True)
        values = json.loads(result.stdout)
        self.assertEqual(values[4], "café/λ/🛰")
        self.assertEqual(values[5], bytes.fromhex("f4 90 80 80 ed a0 80 c0 af c3").decode("latin-1"))
        self.assertEqual(values[6], '\x01\x7f\n"\\')


if __name__ == "__main__":
    unittest.main()
