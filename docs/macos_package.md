# Ava — macOS Apple Silicon test package

Quit Logic Pro and Ava, then run this command in Terminal from this folder:

```bash
bash install.sh
```

The installer copies the AU and VST3 into your user Library, the standalone app
into ~/Applications, and shared INT8 models into
~/Library/Application Support/Ava/generator-native. Existing versions are moved
into ~/Library/Application Support/Ava/backups before replacement. No Python,
research checkout, model-folder selection, downloads or Hugging Face login is
needed. The small fine-tuned mapper is inside each plugin/app bundle.

Open Ava from ~/Applications, or reopen Logic and load Ava as an instrument.
Choose an instrument, optionally describe the timbre, then press Generate.
Generation normally takes around 8–10 seconds on the development Mac; the
button becomes Cancel while working. A successful request replaces the preset;
a failed/cancelled request keeps the previous sound. Preview holds the note for
four seconds; MIDI from your keyboard controls played pitch independently of
the prompt. Load/Save use versioned .ava.json presets; examples are in presets/.
Logic projects preserve the current synth controls, preset name, description
and preview note. To restore an older installation, quit the host and copy the
saved bundles/models from its dated backup to their original locations.

This is a local test build for Apple Silicon, signed ad hoc. It is not a
notarized public installer and has not been checked on other Macs. ONNX Runtime licenses are included inside each bundle.
No neural network or model loading runs on the real-time audio thread.
