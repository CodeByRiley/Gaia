#!/usr/bin/env python3
"""Library manifest graph/format validation using isolated dry-run fixtures."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
MAKE=os.environ.get("GAIA_TEST_MAKE","make")
class LibraryTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.work=Path(self.tmp.name)/"userspace";self.work.mkdir()
        shutil.copytree(ROOT/"userspace/build",self.work/"build")
        (self.work/"Makefile").write_text(""".DEFAULT_GOAL := all
USER_PE_CC := pe-cc
USER_PE_AR := pe-ar
USER_ELF_CC := elf-cc
include build/application.mk
include build/library.mk
all: $(SHIM_TARGETS)
""")
    def library(self,name,extra="",fmt="pe",kind="static"):
        directory=self.work/"shims"/"test"/name;directory.mkdir(parents=True)
        (directory/"code.c").write_text("int Example(void){return 42;}\n")
        (directory/"exports.def").write_text("EXPORTS\nExample\n")
        (directory/"build.mk").write_text(f"LIB_NAME := {name}\nLIB_TYPE := {kind}\nLIB_FORMAT := {fmt}\nLIB_ABI := {'win32' if fmt=='pe' else 'posix'}\nLIB_SOURCES := code.c\n"+extra)
        return directory
    def run_make(self,ok=True):
        p=subprocess.run([MAKE,"--no-print-directory","-n"],cwd=self.work,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.assertEqual(p.returncode==0,ok,p.stdout);return p.stdout
    def test_dll_and_import_library(self):
        self.library("runtime","LIB_EXPORTS := exports.def\n",kind="shared")
        out=self.run_make();self.assertIn("-shared",out);self.assertIn("runtime.dll.a",out);self.assertIn("pe/shims/runtime/",out)
    def test_format_isolation(self):
        self.library("windows");self.library("posix",fmt="elf")
        out=self.run_make();self.assertIn("pe/shims/windows/",out);self.assertIn("elf/shims/posix/",out)
    def test_unknown_dependency(self):
        self.library("one","LIB_DEPS := missing\n");self.assertIn("unknown library dependency",self.run_make(False))
    def test_dependency_cycle(self):
        self.library("one","LIB_DEPS := two\n");self.library("two","LIB_DEPS := one\n");self.assertIn("cyclic",self.run_make(False))
    def test_cross_format_dependency_rejected(self):
        self.library("one","LIB_DEPS := two\n");self.library("two",fmt="elf");self.assertIn("incompatible object format",self.run_make(False))
    def test_escaping_source_rejected(self):
        self.library("one","LIB_SOURCES := ../../../../outside.c\n");self.assertIn("outside userspace",self.run_make(False))
    def test_values_do_not_leak(self):
        self.library("one","LIB_CFLAGS := -DFIRST\n");self.library("two")
        out=self.run_make();self.assertTrue(any("-DFIRST" in x and "one/code.c" in x for x in out.splitlines()))
        self.assertFalse(any("-DFIRST" in x and "two/code.c" in x for x in out.splitlines()))
if __name__=="__main__":unittest.main()
