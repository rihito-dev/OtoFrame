"""Exercise the compiled CLI using generated WAV fixtures, not recorded music."""
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

BINARY = Path(sys.argv.pop(1)).resolve()


class CLITest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.report = self.root / "report.json"

    def tearDown(self):
        self.temp.cleanup()

    def run_wav(self, raw, expected=0):
        path = self.root / "fixture.wav"
        path.write_bytes(raw)
        run = subprocess.run([str(BINARY), "--wav", str(path), "--output", str(self.report)], capture_output=True, text=True)
        self.assertEqual(run.returncode, expected, run.stderr)
        return json.loads(self.report.read_text()) if expected == 0 else run.stderr

    def wav(self, channels=1, width=2, frames=4096, phase_cancel=False):
        path = self.root / "input.wav"
        with wave.open(str(path), "wb") as output:
            output.setnchannels(channels)
            output.setsampwidth(width)
            output.setframerate(48000)
            samples = []
            for i in range(frames):
                sample = round(16384 * math.sin(2 * math.pi * 1500 * i / 48000))
                for c in range(channels):
                    value = -sample if phase_cancel and c else sample
                    samples.append(struct.pack("<h", value) if width == 2 else bytes((128,)))
            output.writeframes(b"".join(samples))
        return path.read_bytes()

    def test_mono_pcm16(self):
        report = self.run_wav(self.wav())
        self.assertFalse(report["synthetic"])
        self.assertEqual(len(report["frames"]), 4)
        self.assertAlmostEqual(report["frames"][0]["rms"], 0.5 / math.sqrt(2), places=4)
        self.assertAlmostEqual(report["frames"][0]["centroid_hz"], 1500, delta=1)
        self.assertEqual(report["frames"][-1]["valid_samples"], 1024)
        self.assertEqual(len(report["frames"][0]["spectrum"]), 1025)

    def test_stereo_downmix(self):
        report = self.run_wav(self.wav(channels=2))
        self.assertAlmostEqual(report["frames"][0]["rms"], 0.5 / math.sqrt(2), places=4)

    def test_antiphase_downmix_is_silence(self):
        report = self.run_wav(self.wav(channels=2, phase_cancel=True))
        self.assertEqual(report["frames"][0]["rms"], 0)

    def test_short_frame(self):
        report = self.run_wav(self.wav(frames=17))
        self.assertEqual(len(report["frames"]), 1)
        self.assertEqual(report["frames"][0]["valid_samples"], 17)

    def test_unknown_odd_chunk(self):
        raw = self.wav()
        extra = b"JUNK" + struct.pack("<I", 3) + b"abc\0"
        raw = raw[:12] + extra + raw[12:]
        raw = raw[:4] + struct.pack("<I", len(raw) - 8) + raw[8:]
        self.run_wav(raw)

    def test_not_wave(self):
        self.run_wav(b"x" * 44, 1)

    def test_truncated_chunk(self):
        raw = self.wav()[:-1]
        raw = raw[:4] + struct.pack("<I", len(raw) - 8) + raw[8:]
        self.run_wav(raw, 1)

    def test_riff_length_mismatch(self):
        self.run_wav(self.wav() + b"excess", 1)

    def test_empty_data(self):
        self.run_wav(self.wav(frames=0), 1)

    def test_unsupported_pcm8(self):
        self.run_wav(self.wav(width=1), 1)

    def test_invalid_format(self):
        raw = bytearray(self.wav())
        raw[32:34] = struct.pack("<H", 9)
        self.run_wav(bytes(raw), 1)

    def test_truncated_chunk_header(self):
        raw = self.wav() + b"tail"
        raw = raw[:4] + struct.pack("<I", len(raw) - 8) + raw[8:]
        self.run_wav(raw, 1)

    def test_usage_and_missing_input(self):
        self.assertEqual(subprocess.run([str(BINARY)], capture_output=True).returncode, 2)
        self.assertEqual(subprocess.run([str(BINARY), "--wav", str(self.root / "missing.wav"), "--output", str(self.report)], capture_output=True).returncode, 1)


if __name__ == "__main__":
    unittest.main()
