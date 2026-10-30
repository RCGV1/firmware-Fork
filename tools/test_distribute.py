"""Regression checks for source selection, complete board coverage and publication gates."""
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import distribute


class DistributionTests(unittest.TestCase):
    def test_bootstrap_includes_latest_stable_prerelease_and_develop(self):
        releases = [
            dict(tag_name="v3", draft=True, prerelease=True, published_at="2026-11-01T00:00:00Z"),
            dict(tag_name="v2", draft=False, prerelease=True, published_at="2026-10-01T00:00:00Z"),
            dict(tag_name="v1", draft=False, prerelease=False, published_at="2026-09-01T00:00:00Z"),
            dict(tag_name="v0", draft=False, prerelease=False, published_at="2025-01-01T00:00:00Z"),
        ]
        result = distribute.candidates(releases, "a" * 40)
        self.assertEqual({r["upstream_ref"] for r in result}, {"v2", "v1", "a" * 40})

    def test_intermediate_new_releases_are_not_lost(self):
        releases = [dict(tag_name=f"v{i}", draft=False, prerelease=True,
                         published_at="2026-11-01T00:00:00Z") for i in range(3)]
        result = distribute.candidates(releases, "a" * 40)
        self.assertEqual(len(result), 4)

    def test_all_boards_fit_matrix_without_dropping_extra_variants(self):
        boards = [dict(platform="esp32s3", board=f"board-{i}") for i in range(300)]
        boards.append(dict(platform="native", board="coverage"))
        groups = distribute.group_boards(boards)
        self.assertLessEqual(len(groups), 256)
        self.assertEqual([b for group in groups for b in group["boards"]],
                         [f"board-{i}" for i in range(300)])

    def test_unknown_upstream_policy_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "src/mesh/PositionPrecision.cpp"
            path.parent.mkdir(parents=True)
            path.write_text("// changed upstream policy\n")
            with self.assertRaisesRegex(ValueError, "Unknown upstream"):
                distribute.customize(root)

    def test_experimental_boards_are_attempted_separately(self):
        matrix = [dict(platform="esp32", board="supported", required=True),
                  dict(platform="esp32", board="experimental", required=False)]
        groups = distribute.group_boards(matrix)
        self.assertEqual(len(groups), 2)
        self.assertEqual({g["boards"][0]: g["required"] for g in groups},
                         {"supported": True, "experimental": False})

    def test_legacy_rp_framework_pin_is_used_without_changing_version(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            url = "https://github.com/earlephilhower/arduino-pico/releases/download/5.4.4/rp2040-5.4.4.zip"
            for platform in ("rp2040", "rp2350"):
                path = root / f"variants/{platform}/{platform}.ini"
                path.parent.mkdir(parents=True)
                path.write_text(f"platform_packages =\n  arduino-pico@{url}\n")
            distribute.fix_legacy_rp_package_names(root)
            for platform in ("rp2040", "rp2350"):
                path = root / f"variants/{platform}/{platform}.ini"
                self.assertEqual(path.read_text(), f"platform_packages =\n  framework-arduinopico@{url}\n")
                before = path.read_bytes()
                distribute.fix_legacy_rp_package_names(root)
                self.assertEqual(path.read_bytes(), before)

    def test_experimental_failure_does_not_remove_supported_board_gate(self):
        matrix = [dict(platform="esp32", board="supported", required=True),
                  dict(platform="esp32", board="experimental", required=False)]
        self.assertEqual(distribute.validate_outputs(matrix, [dict(board="supported")]), ["experimental"])
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            distribute.validate_outputs(matrix, [dict(board="experimental")])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            distribute.validate_outputs(matrix, [dict(board="supported"), dict(board="supported")])

    def test_legacy_esp_core_follows_active_platform_not_commented_url(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "variants/esp32/esp32-common.ini"
            path.parent.mkdir(parents=True)
            legacy = "https://github.com/meshtastic/pioarduino-platform-espressif32/archive/refs/heads/55.03.311.zip"
            path.write_text(f"platform =\n  {legacy}\n")
            self.assertTrue(distribute.legacy_esp_core_needed(root, "esp32s3"))
            self.assertFalse(distribute.legacy_esp_core_needed(root, "rp2040"))
            path.write_text(f"platform =\n  https://example.com/55.03.312-1/platform.zip\n  ; {legacy}\n")
            self.assertFalse(distribute.legacy_esp_core_needed(root, "esp32s3"))

    def test_missing_board_prevents_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            previous = Path.cwd()
            try:
                os.chdir(directory)
                Path("downloads").mkdir()
                Path("matrix.json").write_text(json.dumps([dict(board="missing", platform="esp32")]))
                with patch.object(distribute, "run") as run:
                    with self.assertRaisesRegex(ValueError, "Incomplete"):
                        distribute.publish()
                    run.assert_not_called()
            finally:
                os.chdir(previous)


if __name__ == "__main__":
    unittest.main()
