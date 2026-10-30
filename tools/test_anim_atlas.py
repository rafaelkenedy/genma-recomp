"""Asset-free regression checks: python -I tools/test_anim_atlas.py."""
import sys
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.append(str(Path(__file__).resolve().parent))
from convert_anim_atlas import frame_images


class AnimationFrames(unittest.TestCase):
    def run_frames(self, scores, mode='matched', values=None):
        if values is None:
            values = list(range(len(scores)))
        originals = [np.full((2, 3, 4), value, np.uint8) for value in values]
        images = [Image.fromarray(a, 'RGBA') for a in originals]
        out, count = frame_images(images, list(range(len(scores))), mode, np.diag(scores))
        for before, image in zip(originals, images):
            np.testing.assert_array_equal(before, np.asarray(image))
        return out, count, originals

    def test_room001_mild_isolated_dip(self):
        scores = [.9684, .9657, .9653, .9634, .9634, .9613, .9604, .9569, .9539, .9557,
                  .9532, .9544, .9519, .9517, .9499, .9515, .9518, .9528, .8875, .9521,
                  .9499, .9486, .9487, .9482, .9494, .9528, .9533, .9555, .9627, .9651]
        values = [i * 4 for i in range(30)]
        values[18] = 255
        out, count, originals = self.run_frames(scores, values=values)
        self.assertEqual(count, 1)
        np.testing.assert_array_equal(out[18], np.full((2, 3, 4), 72, np.uint8))
        for i in range(30):
            if i != 18:
                np.testing.assert_array_equal(out[i], originals[i])

    def test_previous_large_outlier(self):
        out, count, _ = self.run_frames([.95, .95, .7, .95, .95, .95], values=[10, 20, 255, 40, 50, 60])
        self.assertEqual(count, 1)
        self.assertTrue((out[2] == 30).all())

    def test_smooth_phase_is_preserved(self):
        scores = .95 + .08 * np.sin(np.arange(30) * 2 * np.pi / 30)
        out, count, originals = self.run_frames(scores)
        self.assertEqual(count, 0)
        for a, b in zip(out, originals):
            np.testing.assert_array_equal(a, b)

    def test_adjacent_bad_frames_are_not_interpolated(self):
        for scores in ([.95, .95, .7, .71, .95, .95], [.95, .95, .89, .89, .95, .95]):
            out, count, originals = self.run_frames(scores)
            self.assertEqual(count, 0)  # report actual replacements, not just candidates
            for a, b in zip(out, originals):
                np.testing.assert_array_equal(a, b)

    def test_loop_boundary_and_no_byte_overflow(self):
        out, count, _ = self.run_frames([.7, .95, .95, .95, .95], values=[0, 250, 10, 20, 255])
        self.assertEqual(count, 1)
        self.assertTrue((out[0] == 252).all())

    def test_sequence_and_short_inputs_are_preserved(self):
        for scores, mode in (([.95, .2, .95], 'sequence'), ([.95, .1], 'matched'), ([], 'matched')):
            out, count, originals = self.run_frames(scores, mode)
            self.assertEqual(count, 0)
            for a, b in zip(out, originals):
                np.testing.assert_array_equal(a, b)


if __name__ == '__main__':
    unittest.main()
