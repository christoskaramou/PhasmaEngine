"""The pre-commit check reads the workspace's own AGENTS.md and docs/wiki."""
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import precommit_validate as validation


class SharedInstructionsTest(unittest.TestCase):
    def test_agents_entry_needs_no_provider_mirrors(self):
        with tempfile.TemporaryDirectory() as directory:
            entry = Path(directory) / "AGENTS.md"
            entry.write_text("# Shared rules\nUse docs/wiki.\n", encoding="utf-8")
            rows = []
            with patch.object(validation, "INSTRUCTION_FILE", entry):
                validation.project_instructions_step(SimpleNamespace(add=lambda *args: rows.append(args)))
            self.assertEqual(rows[0][1], "PASS")
            self.assertEqual([p.name for p in Path(directory).iterdir()], ["AGENTS.md"])

    def test_missing_empty_binary_oversized_and_invalid_utf8_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            entry = Path(directory) / "AGENTS.md"
            # Past 32 KiB Codex would silently truncate the rules.
            for body in (None, b"", b"bad\0text", b"x" * (32 * 1024 + 1), b"\xff"):
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
