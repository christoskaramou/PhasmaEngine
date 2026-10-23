"""The pre-commit check follows Harness's single shared entry."""
import sys
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import precommit_validate as validation


class SharedInstructionsTest(unittest.TestCase):
    def test_active_wiki_follows_harness_copy_registration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            store = root / "Phasma Harness" / "Wikis"
            store.mkdir(parents=True)
            key = str(root.resolve())
            if validation.os.name == "nt":
                key = key.lower()
            copy = root / "job-wiki"
            (store / "locations.json").write_text(json.dumps({key: {"copy": True, "root": str(copy)}}), encoding="utf-8")
            with patch.dict(validation.os.environ, {"APPDATA": str(root)}), patch.object(validation, "ROOT", root):
                self.assertEqual(validation.workspace_wiki(), copy)

    def test_shared_entry_needs_no_provider_mirrors(self):
        with tempfile.TemporaryDirectory() as directory:
            entry = Path(directory) / "PhasmaHarness.md"
            entry.write_text("# Shared rules\nUse the active Harness wiki.\n", encoding="utf-8")
            rows = []
            with patch.object(validation, "INSTRUCTION_FILE", entry):
                validation.project_instructions_step(SimpleNamespace(add=lambda *args: rows.append(args)))
            self.assertEqual(rows[0][1], "PASS")
            self.assertEqual([p.name for p in Path(directory).iterdir()], ["PhasmaHarness.md"])

    def test_missing_empty_binary_oversized_and_invalid_utf8_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            entry = Path(directory) / "PhasmaHarness.md"
            for body in (None, b"", b"bad\0text", b"x" * 65537, b"\xff"):
                with self.subTest(body=None if body is None else len(body)):
                    if body is not None:
                        entry.write_bytes(body)
                    rows = []
                    with patch.object(validation, "INSTRUCTION_FILE", entry):
                        with self.assertRaisesRegex(RuntimeError, "project instructions"):
                            validation.project_instructions_step(SimpleNamespace(add=lambda *args: rows.append(args)))
                    self.assertEqual(rows[0][1], "FAIL")


if __name__ == "__main__":
    unittest.main()
