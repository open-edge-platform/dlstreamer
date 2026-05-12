Create a bash script that analyzes four video streams.
For each stream run AI analytics to detect people and perform pose estimation and overlay annotations.
- Read input video from a file (https://videos.pexels.com/video-files/8039289/8039289-hd_1366_720_25fps.mp4) for each stream
- Use YOLO26n,Yolo11n,Yolov8n,Yolov8s models for pose estimation
- Annotate each video stream with keypoints but do not show the labels add average FPS information well visible
- Merge output from multiple streams and store combined output to an output file.

Generate vision AI processing pipeline optimized for Intel Core Ultra 3 processors. 
Save source code in pose_estimation_compose directory, including README.md with setup instructions.
Follow instructions in README.md to run the application and check if it generates the expected output.