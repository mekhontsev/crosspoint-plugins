import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "plugin_build", Path(__file__).resolve().parents[1] / "scripts" / "build.py"
)
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class CompileTemplateTest(unittest.TestCase):
    def test_changed_build_configuration_invalidates_cached_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            database = root / "compile_commands.json"
            config = root / "platformio.ini"
            self.assertFalse(build.compile_config_is_current(database, root))
            database.touch()
            config.touch()
            os.utime(database, ns=(200, 200))
            os.utime(config, ns=(100, 100))
            self.assertTrue(build.compile_config_is_current(database, root))
            os.utime(config, ns=(300, 300))
            self.assertFalse(build.compile_config_is_current(database, root))

    def test_moved_sdk_headers_invalidate_cached_template(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            headers = root / "sdk"
            headers.mkdir()
            for name in ("Arduino.h", "GfxRenderer.h", "Bitmap.h"):
                (headers / name).touch()
            self.assertTrue(build.template_headers_available(["c++", "-Isdk"], root))
            self.assertTrue(build.template_headers_available(["c++", "-I", str(headers)], root))
            self.assertFalse(build.template_headers_available(["c++", "-Iold-sdk"], root))
            (headers / "Bitmap.h").unlink()
            self.assertFalse(build.template_headers_available(["c++", "-Isdk"], root))


if __name__ == "__main__":
    unittest.main()
