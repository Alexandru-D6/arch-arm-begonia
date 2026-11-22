"""Host checks for the handset audio test; no access to host audio devices."""
import ctypes
import ctypes.util
import json
import os
import shutil
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]
MOCK = r'''#!/usr/bin/env python3
import json, os, sys, wave
from pathlib import Path
cmd, args = Path(sys.argv[0]).name, sys.argv[1:]
with open(os.environ["AUDIO_TEST_LOG"], "a") as f:
    f.write(json.dumps([cmd, *args]) + "\n")
if os.environ.get("FAIL_CMD") == cmd:
    sys.exit(1)
if cmd == "amixer":
    if "info" in args and os.environ.get("NO_CARD"):
        sys.exit(1)
    if "cget" in args:
        if args[-1] != "iface=CARD,name='AUX Jack'":
            sys.exit("Jack controls use the CARD interface")
        print("  : values=" + ("off" if os.environ.get("NO_JACK") else "on"))
if cmd == "alsactl" and "store" in args:
    Path(args[args.index("-f") + 1]).write_text("previous mixer state")
if cmd in ("arecord", "pw-record"):
    with wave.open(args[-1], "wb") as f:
        f.setparams((2, 2, 48000, 0, "NONE", "not compressed"))
        frames = 48000 if os.environ.get("SHORT_RECORDING") else 240000
        f.writeframes(b"\x00\x01" * 2 * frames)
    if os.environ.get("RECORD_EXIT_ONE"):
        sys.exit(1)
if cmd == "pw-dump":
    nodes = []
    for name, kind in (("begonia_internal_mic", "Audio/Source"),
                       ("begonia_aux_output", "Audio/Sink")):
        if os.environ.get("MISSING_NODE") == name:
            continue
        nodes.append({"type": "PipeWire:Interface:Node", "info": {"props": {
            "node.name": name, "media.class": kind,
            "api.alsa.card.name": "unrelated" if os.environ.get("WRONG_CARD") else "begonia-aux"
        }}})
    print(json.dumps(nodes))
'''


class AudioTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.bin = self.directory / "bin"
        self.bin.mkdir()
        self.work = self.directory / "work"
        self.work.mkdir()
        self.log = self.directory / "calls.jsonl"
        for command in ("amixer", "alsactl", "alsaucm", "arecord", "aplay",
                        "pw-dump", "pw-record", "pw-play", "chroot"):
            path = self.bin / command
            path.write_text(MOCK)
            path.chmod(0o755)
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}",
                        TMPDIR=str(self.work), AUDIO_TEST_LOG=str(self.log))

    def run_audio(self, *args, **env):
        result = subprocess.run(["bash", str(REPO / "scripts/test-mic-aux.sh"), *args],
                                env=dict(self.env, **env), text=True,
                                capture_output=True, timeout=10)
        self.assertEqual(list(self.work.iterdir()), [], "temporary voice data leaked")
        return result

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_alsa_records_then_plays_correct_pcm_and_restores(self):
        result = self.run_audio()
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        record = next(c for c in calls if c[0] == "arecord")
        playback = next(c for c in calls if c[0] == "aplay")
        self.assertEqual(record[record.index("-d") + 1], "5")
        self.assertEqual(record[record.index("-D") + 1], "hw:CARD=begoniaaux,DEV=1")
        self.assertEqual(playback[playback.index("-D") + 1], "hw:CARD=begoniaaux,DEV=0")
        self.assertEqual(record[-1], playback[-1])
        self.assertLess(calls.index(record), calls.index(playback))
        self.assertEqual(calls[-1][-2:], ["restore", "begoniaaux"])

    def test_capture_failure_never_plays_and_restores_mixer(self):
        result = self.run_audio(FAIL_CMD="arecord")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("aplay", [c[0] for c in self.calls()])
        self.assertEqual(self.calls()[-1][-2:], ["restore", "begoniaaux"])

    def test_ucm_failure_restores_mixer(self):
        result = self.run_audio(FAIL_CMD="alsaucm")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls()[-1][-2:], ["restore", "begoniaaux"])

    def test_missing_card_and_unplugged_aux_do_not_record(self):
        for env in ({"NO_CARD": "1"}, {"NO_JACK": "1"}):
            with self.subTest(env=env):
                result = self.run_audio(**env)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("arecord", [c[0] for c in self.calls()])

    def test_incomplete_recording_is_rejected(self):
        for mode in ("--alsa", "--pipewire"):
            with self.subTest(mode=mode):
                result = self.run_audio(mode, SHORT_RECORDING="1")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("incomplete", result.stderr)
                self.assertFalse({"aplay", "pw-play"} & {c[0] for c in self.calls()})

    def test_pipewire_records_exact_samples_and_uses_explicit_targets(self):
        result = self.run_audio("--pipewire")
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        self.assertEqual([c[0] for c in calls], ["pw-dump", "pw-record", "pw-play"])
        self.assertIn("--sample-count=240000", calls[1])
        self.assertIn("--target=begonia_internal_mic", calls[1])
        self.assertIn("--target=begonia_aux_output", calls[2])

    def test_pipewire_does_not_fall_back_to_other_hardware(self):
        for env in ({"WRONG_CARD": "1"}, {"MISSING_NODE": "begonia_aux_output"}):
            with self.subTest(env=env):
                result = self.run_audio("--pipewire", **env)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("pw-record", [c[0] for c in self.calls()])

    def test_pipewire_accepts_sample_limit_status_only_with_complete_audio(self):
        result = self.run_audio("--pipewire", RECORD_EXIT_ONE="1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("pw-play", [c[0] for c in self.calls()])

    def test_pipewire_record_error_without_audio_never_plays(self):
        result = self.run_audio("--pipewire", FAIL_CMD="pw-record")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("pw-play", [c[0] for c in self.calls()])

    def test_rootfs_installer_stages_profile_and_command(self):
        root = self.directory / "rootfs"
        (root / "usr").mkdir(parents=True)
        subprocess.run(["bash", str(REPO / "rootfs/install-audio.sh"), str(root)],
                       check=True, env=self.env, capture_output=True)
        self.assertTrue((root / "usr/local/bin/begonia-mic-aux-test").stat().st_mode & 0o111)
        self.assertTrue((root / "usr/share/alsa/ucm2/conf.d/begonia-aux/begonia-aux.conf").exists())
        self.assertTrue((root / "etc/wireplumber/wireplumber.conf.d/51-begonia-aux.conf").exists())
        self.assertTrue((root / "var/lib/systemd/linger/alarm").exists())
        self.assertIn(["chroot", str(root), "usermod", "-aG", "audio", "alarm"], self.calls())

    def test_ucm_files_parse_with_alsa(self):
        library = ctypes.util.find_library("asound")
        if not library:
            self.skipTest("libasound is not installed")
        alsa = ctypes.CDLL(library)
        for path in (REPO / "rootfs/audio/ucm2").rglob("*.conf"):
            with self.subTest(path=path):
                config, stream = ctypes.c_void_p(), ctypes.c_void_p()
                self.assertEqual(alsa.snd_config_top(ctypes.byref(config)), 0)
                try:
                    self.assertEqual(alsa.snd_input_stdio_open(ctypes.byref(stream),
                                                            os.fsencode(path), b"r"), 0)
                    try:
                        self.assertEqual(alsa.snd_config_load(config, stream), 0)
                    finally:
                        alsa.snd_input_close(stream)
                finally:
                    alsa.snd_config_delete(config)

    def test_wireplumber_rules_have_nested_actions(self):
        parser = shutil.which("spa-json-dump")
        if not parser:
            self.skipTest("spa-json-dump is not installed")
        result = subprocess.check_output([
            parser, str(REPO / "rootfs/audio/wireplumber/51-begonia-aux.conf")])
        rules = json.loads(result)["monitor.alsa.rules"]
        for rule in rules:
            self.assertEqual(set(rule), {"matches", "actions"})
            self.assertIn("update-props", rule["actions"])
        names = {rule["actions"]["update-props"].get("node.name") for rule in rules}
        self.assertTrue({"begonia_internal_mic", "begonia_aux_output"} <= names)

        outputs = {rule["actions"]["update-props"].get("node.name"):
                   rule["actions"]["update-props"] for rule in rules}
        speaker = outputs["begonia_speaker_output"]
        aux = outputs["begonia_aux_output"]
        self.assertEqual(speaker["audio.channels"], 1)
        self.assertEqual(speaker["audio.position"], ["MONO"])
        self.assertGreater(aux["priority.session"], speaker["priority.session"])

    def test_ucm_speaker_uses_mono_pcm_and_aux_mutes_it(self):
        library = ctypes.util.find_library("asound")
        if not library:
            self.skipTest("libasound is not installed")
        alsa = ctypes.CDLL(library)
        config, stream = ctypes.c_void_p(), ctypes.c_void_p()
        path = REPO / "rootfs/audio/ucm2/MediaTek/begonia-aux/HiFi.conf"
        self.assertEqual(alsa.snd_config_top(ctypes.byref(config)), 0)
        try:
            self.assertEqual(alsa.snd_input_stdio_open(ctypes.byref(stream), os.fsencode(path), b"r"), 0)
            try:
                self.assertEqual(alsa.snd_config_load(config, stream), 0)
            finally:
                alsa.snd_input_close(stream)
            node, value = ctypes.c_void_p(), ctypes.c_char_p()
            for key, expected in (
                ("SectionDevice.Speaker.Value.PlaybackPCM", "hw:${CardId},2"),
                ("SectionDevice.Headphones.Value.JackHWMute", "Speaker"),
                ("SectionDevice.Mic1.Value.CapturePCM", "hw:${CardId},1"),
            ):
                self.assertEqual(alsa.snd_config_search(config, key.encode(), ctypes.byref(node)), 0)
                self.assertEqual(alsa.snd_config_get_string(node, ctypes.byref(value)), 0)
                self.assertEqual(value.value.decode(), expected)
        finally:
            alsa.snd_config_delete(config)


if __name__ == "__main__":
    unittest.main()
