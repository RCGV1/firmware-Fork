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
