# gvaaudiotranscribe

Performs audio transcription using OpenVino GenAI Whisper model. for more details on the whisper ASR check out [OpenVino GenAI Documentation](https://docs.openvino.ai/2025/api/genai_api/_autosummary/openvino_genai.WhisperPipeline.html#openvino_genai.WhisperPipeline)


```bash
Element Flags:

Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      audio/x-raw
                 format: S16LE
                   rate: 16000
               channels: 1

  SRC template: 'src'
    Availability: Always
    Capabilities:
      audio/x-raw
                 format: S16LE
                   rate: 16000
               channels: 1

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:

  device              : Device to use for inference (CPU, GPU)
                        flags: readable, writable
                        String. Default: "CPU"

  model               : Path to the model directory
                        flags: readable, writable
                        String. Default: null

  model-type          : model_type value to use whisper for inference: 'whisper' (supported).
                        flags: readable, writable
                        String. Default: "whisper"

  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvaaudiotranscribe0"

  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"

  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false
```