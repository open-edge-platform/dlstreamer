Create an application to monitor safety compliance at a construction site:
- Process video input (file path or RTSP URI)
- Detect workers (persons) using a YOLO model and track them across frames to maintain identity
- Select frames where a new worker appears or where a tracked worker has not been checked for a predefined duration
- For each selected worker, crop a tight region around their bounding box (with margin) and scale to a fixed resolution suitable for the VLM
- Send the cropped single-worker image to a VLM model with a prompt to verify safety-rule compliance:
  "There is a worker in the center of an image. Answer two questions:
   Is the worker wearing a helmet? (WEARING / NOT_WEARING / UNCERTAIN)
   Is the worker secured with a safety harness or line? (SECURED / NOT_SECURED / UNCERTAIN)
   Reply with exactly one word for each question, separated by a slash. Nothing else."
- Save an annotated cropped image for each VLM check, log VLM propmts and responses in a JSON file
- Generate alerts only for clear violations (NOT_WEARING / NOT_SECURED); treat UNCERTAIN responses as compliant

Validate the application using:
- Input video: https://www.pexels.com/video/high-rise-workers-on-skyscraper-scaffold-28750724/
- YOLO26m model for worker (person) detection
- Qwen2.5-VL-3B-Instruct model for safety compliance verification
- Run VLM checks on new worker detections or every 30 seconds for tracked workers

Expected results for the test video:
- 4 workers visible, all properly secured with safety harnesses
- 3 workers wearing helmets, 1 worker without a helmet — alert generated
