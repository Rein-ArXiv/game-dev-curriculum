"""Local regression checks for audit tools; no network access."""

from pathlib import Path
import unittest

from check_links import extract_urls, is_article


class LinkExtractionTests(unittest.TestCase):
    def test_markdown_suffix_is_not_part_of_url(self):
        self.assertEqual(
            list(extract_urls("[title](https://example.org/page)Suffix")),
            ["https://example.org/page"],
        )

    def test_balanced_parentheses_inside_url(self):
        self.assertEqual(
            list(extract_urls("[title](https://example.org/a_(b))Suffix")),
            ["https://example.org/a_(b)"],
        )

    def test_fragment_and_multiple_links(self):
        self.assertEqual(
            list(extract_urls("[a](https://example.org/#a) [b](https://example.org/b).")),
            ["https://example.org/#a", "https://example.org/b"],
        )

    def test_review_reports_are_not_publication_sources(self):
        self.assertFalse(is_article(Path("REVIEW_NEW_2026-09-13.md")))
        self.assertFalse(is_article(Path("REDESIGN_NOTES.md")))
        self.assertTrue(is_article(Path("part01_analysis_correctness.md")))


if __name__ == "__main__":
    unittest.main()
