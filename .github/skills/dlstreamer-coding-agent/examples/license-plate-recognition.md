Use DLStreamer Coding Agent to develop a Python application that implements license plate recognition pipeline: 
- Read input video from a file (https://github.com/open-edge-platform/edge-ai-resources/raw/main/videos/ParkingVideo.mp4) but also allow remote IP cameras
- Run YOLOv11 (https://huggingface.co/morsetechlab/yolov11-license-plate-detection) for object detection and PaddleOCR (https://huggingface.co/PaddlePaddle/PP-OCRv5_server_rec) model for character recognition
- Output license plate text for each detected object as JSON file
- Annotate video stream and store it as an output video file

Generate vision AI processing pipeline optimized for Intel Core Ultra 3 processors. Save source code in license_plate_recognition directory, generate README.md with setup instructions. Follow instructions in README.md to run the application and check if it generates the expected output.