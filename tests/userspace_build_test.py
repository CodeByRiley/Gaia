#!/usr/bin/env python3
"""Exercise manifest registration without needing Gaia's cross toolchains."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAKE = os.environ.get("GAIA_TEST_MAKE", "make")

class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.work = self.root / "userspace"
        self.work.mkdir()
        shutil.copytree(ROOT / "userspace/build", self.work / "build")
        (self.work / "lib").mkdir()
        (self.work / "lib/user.ld").touch()
        (self.work / "Makefile").write_text(""".DEFAULT_GOAL := all
USER_ELF_CC := elf-cc
USER_PE_CC := pe-cc
USER_ELF_LD := elf-ld
USER_AS := nasm
USERSPACE_TARGETS :=
include build/runtimes.mk
include build/application.mk
all: $(USERSPACE_TARGETS)
""")

    def app(self, name="demo", manifest="APP_RUNTIME := native\nAPP_SOURCES := main.c\n"):
        app = self.work / "bin" / name
        app.mkdir(parents=True)
        (app / "main.c").write_text("int main(void) { return 0; }\n")
        (app / "build.mk").write_text(manifest)
        return app

    def make(self, *args, ok=True):
        result = subprocess.run([MAKE, "--no-print-directory", *args], cwd=self.work,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode == 0, ok, result.stdout)
        return result.stdout

    def test_dual_format_isolation(self):
        self.app(manifest="APP_FORMAT := both\nAPP_RUNTIME := native\nAPP_SOURCES := main.c\n")
        out = self.make("-n")
        self.assertIn("elf-cc", out)
        self.assertIn("pe-cc", out)
        self.assertIn("../build/userspace/elf/demo/main.c.o", out)
        self.assertIn("../build/userspace/pe/demo/main.c.o", out)
        self.assertIn("demo.exe", out)
        self.assertIn("demo.elf", out)

    def test_pe_default_is_native(self):
        self.app(manifest="APP_FORMAT := pe\nAPP_SOURCES := main.c\n")
        self.assertIn("pe-cc", self.make("-n"))

    def test_invalid_manifests(self):
        app = self.app()
        for manifest, message in [
            ("APP_FORMAT := coff", "APP_FORMAT"),
            ("APP_NAME := ../escape", "APP_NAME"),
            ("APP_SOURCES := ../escape.c", "relative path"),
            ("APP_SOURCES := missing.c", "missing source"),
            ("APP_SOURCES := main.cpp", "extension"),
            ("APP_SOURCES :=", "required"),
            ("APP_RUNTIME := bogus\nAPP_SOURCES := main.c", "APP_RUNTIME"),
            ("APP_FORMAT := pe\nAPP_RUNTIME := musl\nAPP_SOURCES := main.c", "unsupported runtime"),
            ("APP_RUNTIME := native\nAPP_LIBS := arbitrary.a\nAPP_SOURCES := main.c", "unknown libraries"),
        ]:
            with self.subTest(manifest=manifest):
                (app / "build.mk").write_text(manifest + "\n")
                self.assertIn(message, self.make("-n", ok=False))

    def test_duplicate_names(self):
        self.app("first")
        self.app("second", "APP_NAME := first\nAPP_SOURCES := main.c\n")
        self.assertIn("duplicate APP_NAME", self.make("-n", ok=False))

    def test_flags_and_manifest_isolation(self):
        self.app("first", "APP_RUNTIME := native\nAPP_SOURCES := main.c\nAPP_CFLAGS := -DFIRST\nAPP_LDFLAGS := -custom-link\nAPP_LIBS :=\n")
        self.app("second")
        lines = self.make("-n").splitlines()
        self.assertTrue(any("-DFIRST" in x and "first/main.c" in x for x in lines))
        self.assertFalse(any("-DFIRST" in x and "second/main.c" in x for x in lines))
        self.assertTrue(any("-custom-link" in x for x in lines))

    def test_incremental_header_and_multisource(self):
        app = self.app(manifest="APP_RUNTIME := native\nAPP_SOURCES := main.c other.c\n")
        (app / "other.c").touch()
        header = app / "shared.h"
        header.touch()
        out = self.root / "build/userspace/elf/demo"
        out.mkdir(parents=True)
        for name in ["main", "other"]:
            obj = out / (name + ".c.o")
            obj.touch()
            (out / (name + ".c.d")).write_text(f"../build/userspace/elf/demo/{name}.c.o: bin/demo/{name}.c bin/demo/shared.h\n")
        binary = app / "demo.elf"
        binary.touch()
        self.make("-q")
        os.utime(header, (binary.stat().st_mtime + 5,) * 2)
        result = self.make("-n")
        self.assertIn("-c bin/demo/main.c", result)
        self.assertIn("-c bin/demo/other.c", result)

if __name__ == "__main__":
    unittest.main()
