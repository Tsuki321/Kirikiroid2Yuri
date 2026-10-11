"""Regressions for scrolling the real launcher and DocumentsUI hierarchy."""
import importlib.util
from pathlib import Path
import unittest
from unittest import mock
import xml.etree.ElementTree as ET


spec = importlib.util.spec_from_file_location(
    "android_library_return", Path(__file__).parent / "android" / "library_return.py")
library = importlib.util.module_from_spec(spec)
spec.loader.exec_module(library)

TARGET = "Play Second controls playground"


def hierarchy(target_top, target_bottom, view_class="android.widget.ScrollView", scrollable="true"):
    root = ET.Element("hierarchy")
    content = ET.SubElement(root, "node", {
        "class": view_class, "scrollable": scrollable, "bounds": "[0,100][1080,1857]"})
    ET.SubElement(content, "node", {
        "content-desc": TARGET, "bounds": f"[105,{target_top}][691,{target_bottom}]"})
    return root


class LibraryScrollingTest(unittest.TestCase):
    def test_scrolls_a_row_visible_only_as_a_thin_strip(self):
        with mock.patch.object(library, "tree", side_effect=[hierarchy(1845, 1857), hierarchy(1500, 1650)]), \
                mock.patch.object(library, "adb") as adb:
            library.scroll_to(TARGET)
        self.assertEqual(adb.call_count, 1)
        self.assertEqual(adb.call_args.args[:3], ("shell", "input", "swipe"))

    def test_drags_dialog_list_when_scrollable_flag_is_false(self):
        with mock.patch.object(library, "tree", side_effect=[
                hierarchy(2000, 2150, "android.widget.ListView", "false"),
                hierarchy(1500, 1650, "android.widget.ListView", "false")]), \
                mock.patch.object(library, "adb") as adb:
            library.scroll_to(TARGET)
        self.assertEqual(adb.call_count, 1)

    def test_unchanged_scrolled_content_still_fails_for_missing_target(self):
        with mock.patch.object(library, "tree", return_value=hierarchy(2000, 2150)), \
                mock.patch.object(library, "adb") as adb:
            with self.assertRaisesRegex(AssertionError, "Could not reach " + TARGET):
                library.scroll_to(TARGET)
        self.assertEqual(adb.call_count, 2)


if __name__ == "__main__":
    unittest.main()
