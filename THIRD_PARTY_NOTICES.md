# Third-party notices

Ava's original C++ source is licensed under AGPL-3.0-only. Dependencies and model
assets retain their own terms and are not relicensed by Ava's source-code license.
Copyright (c) 2024–2026 Gianluigi Silvestri.

## JUCE

The build pins JUCE 8.0.16-security, commit
ea77444750de45e036e0279bb0291144bf8e3688. JUCE is dual-licensed under AGPLv3
or its commercial license. The open-source Ava distribution uses the AGPLv3 route.
See [JUCE's license](https://github.com/juce-framework/JUCE/blob/8.0.16-security/LICENSE.md).
JUCE's bundled dependencies retain their own notices. Redistributing a compiled
AGPL build requires providing its corresponding source under the applicable terms.

## ONNX Runtime

ONNX Runtime is MIT-licensed and includes additional third-party components.
See its [license](https://github.com/microsoft/onnxruntime/blob/main/LICENSE).
The build already copies ONNX Runtime's LICENSE and ThirdPartyNotices.txt into
plugin resources; keep both when redistributing a build.

## Neural models

**Powered by Stability AI.** Stable Audio 3 Small SFX and SAME-S retain the
[Stability AI Community License](licenses/STABILITY_AI_COMMUNITY_LICENSE.md).
The T5Gemma text encoder and associated assets also retain the
[Gemma Terms](licenses/GEMMA_TERMS.md). The full agreements govern commercial
use, redistribution, use restrictions and attribution. AGPL licensing of Ava's
code does not remove these model terms or make the models AGPL-licensed.

The large upstream generator weights are not included in this repository.
Download them through the official model repository and export them using
ava-torch. ONNX conversion and INT8 quantization do not remove upstream terms.
[The generator notice](models/generator/NOTICE) describes these modifications.
The installers carry the agreements and notice with the generator assets.

The independently trained mapper is included under separate model terms; see
[models/mapper/LICENSE.md](models/mapper/LICENSE.md). The license's broad
output-derived-model definition is conservatively applied to these weights.
Keep model agreements and notices when redistributing them. This is not a claim
that AGPL and the model licenses can be collapsed into one license for everything.
