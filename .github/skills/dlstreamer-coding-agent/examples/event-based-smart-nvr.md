Develop a vision AI application that implements an event-based smart video recording pipeline:
- Read input video from an RTSP camera, but allow also video file input 
(use https://www.pexels.com/video/a-man-wearing-a-face-mask-walks-into-a-building-9492063/ for testing)
- Run an AI model to detect people in camera view
- Trigger recording of a video stream to a local file when a person is detected and stop recording when person is out of view
- Output a sequence of files: save-1, save-2, save-3, ... for each sequence when a person is visible

Optimize the application for Intel Core Ultra 3 processors. Save source code in smart_nvr directory, generate README.md with setup instructions. Validate the application works as expected and generate performance numbers (fps).
