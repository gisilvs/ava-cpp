# Model assets

`mapper/` contains the final ONNX mapper and its metadata. Its PyTorch weights
are versioned in [ava-torch](https://github.com/gisilvs/ava-torch/tree/main/weights).
The mapper expects raw SAME-S latents of shape `[1, 256, 66]`.

`generator/` records the sampler settings, file checksums and manifest of the
INT8 export used for the blog. The large model files are not included here.
Follow the root README to export them from the upstream model in ava-torch.
Installers use the complete export's own checksums, not these reference files.

Upstream model: `stabilityai/stable-audio-3-small-sfx`, revision
`05ca3857881c94a2b84026b1111c537d78f5f9f0`. The sampler records the source
checkpoint and configuration hashes as well as the eight-step schedule.
