Analyze the DeepStream sample application at https://github.com/NVIDIA-AI-IOT/deepstream_tao_apps/tree/master/apps/tao_others/deepstream_lpr_app. Create an equivalent sample application for DL Streamer.  Verify an output of created sample application.

In the generated README, include a detailed conversion reference:
- Side-by-side diagrams comparing the DeepStream and DL Streamer pipelines with marking with colors the same functional elements
- Element mapping table explaining each substitution and why the change was made
- Application logic table covering probes, callbacks, metadata handling, and CLI parsing
- Model comparison table showing the original model vs. the chosen replacement, format conversion steps, and rationale for the selection

Finally, follow the steps in the generated documentation to verify that the application builds, all resources (models, video files) are downloaded successfully, and the running application produces meaningful results. If any step fails, iterate on the fix until the application works end-to-end.