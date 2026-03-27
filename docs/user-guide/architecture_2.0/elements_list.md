# Elements 2.0

## meta_overlay

Overlays the metadata on the video frame to visualize the inference
results.

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | async-handling | The bin will handle Asynchronous state<br>changes<br>Default:False<br> |
  | message-forward | Forwards all children messages<br>Default:False<br> |
  | preprocess | Pre-processing element<br>Default: None<br> |
  | process | Main processing element<br>Default: None<br> |
  | postprocess | Post-processing element<br>Default: None<br> |
  | aggregate | (Optional) Element to aggregate<br>preprocess/process/postprocess result<br>and original   frame<br>Default: None<br> |
  | postaggregate | (Optional) Element inserted after<br>aggregation element<br>Default: None<br> |
  | preprocess-queue-size | Size of queue (in number buffers) before<br>pre-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | process-queue-size | Size of queue (in number buffers) before<br>processing element. Special values: -1<br>means   no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postprocess-queue-size | Size of queue (in number buffers) before<br>post-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | aggregate-queue-size | Size of queue (in number buffers) for<br>original frames between 'tee' and<br>aggregate   element. Special values: -1<br>means no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postaggregate-queue-size | Size of queue (in number buffers)<br>between aggregate and   post-aggregate<br>elements. Special values: -1 means no<br>queue element, 0 means queue of<br>unlimited   size<br>Default: 0<br> |
  | device | Target device for meta_overlaying<br>Default: <enum CPU device on system<br>memory of type   MetaOverlayDevice><br> |


## object\_classify

Performs object classification. Accepts the ROI or full frame as an
input and outputs classification results with metadata.

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | async-handling | The bin will handle Asynchronous state<br>changes<br>Default:False<br> |
  | message-forward | Forwards all children messages<br>Default:False<br> |
  | preprocess | Pre-processing element<br>Default: None<br> |
  | process | Main processing element<br>Default: None<br> |
  | postprocess | Post-processing element<br>Default: None<br> |
  | aggregate | (Optional) Element to aggregate<br>preprocess/process/postprocess result<br>and original   frame<br>Default: None<br> |
  | postaggregate | (Optional) Element inserted after<br>aggregation element<br>Default: None<br> |
  | preprocess-queue-size | Size of queue (in number buffers) before<br>pre-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | process-queue-size | Size of queue (in number buffers) before<br>processing element. Special values: -1<br>means   no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postprocess-queue-size | Size of queue (in number buffers) before<br>post-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | aggregate-queue-size | Size of queue (in number buffers) for<br>original frames between 'tee' and<br>aggregate   element. Special values: -1<br>means no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postaggregate-queue-size | Size of queue (in number buffers)<br>between aggregate and   post-aggregate<br>elements. Special values: -1 means no<br>queue element, 0 means queue of<br>unlimited   size<br>Default: 0<br> |
  | model | Path to inference model network file<br>Default: ""<br> |
  | ie-config | Comma separated list of KEY=VALUE<br>parameters for inference configuration<br>Default: ""<br> |
  | device | Target device for inference. Please see<br>inference backend documentation (ex,<br>OpenVINO™ Toolkit)   for list of supported<br>devices.<br>Default: CPU<br> |
  | model-instance-id | Identifier for sharing resources between<br>inference elements of the same type.<br>Elements   with the instance-id will share<br>model and other properties. If not<br>specified, a unique identifier will   be<br>generated.<br>Default: ""<br> |
  | nireq | Maximum number of inference requests<br>running in parallel<br>Default: 0<br> |
  | batch-size | Number of frames batched together for a<br>single inference. If the batch-size is<br>0, then it   will be set by default to be<br>optimal for the device. Not all models<br>support batching. Use model optimizer   to<br>ensure that the model has batching<br>support.<br>Default: 0<br> |
  | model-proc | Path to JSON file with parameters<br>describing how to build pre-process and<br>post-process   sub-pipelines<br>Default: ""<br> |
  | pre-process-backend | Preprocessing backend type<br>Default: <enum Automatic of   type<br>VideoInferenceBackend><br> |
  | inference-interval | Run inference for every Nth frame<br>Default: 1<br> |
  | roi-inference-interval | Determines how often to run inference on<br>each ROI object. Only valid if each   ROI<br>object has unique object id (requires<br>object tracking after object detection)<br>Default: 1<br> |
  | inference-region | Region on which inference will be<br>performed - full-frame or on each ROI<br>(region of   interest)bounding-box area<br>Default: <enum Perform inference for<br>full frame of   type<br>VideoInferenceRegion><br> |
  | object-class | Run inference only on Region-Of-Interest<br>with specified object class<br>Default: ""<br> |
  | labels | Path to file containing model's output<br>layer labels or comma separated list of<br>KEY=VALUE pairs   where KEY is name of<br>output layer and VALUE is path to labels<br>file. If provided, labels from<br>model-proc   won't be loaded<br>Default: ""<br> |
  | labels-file | Path to file containing model's output<br>layer labels. If provided, labels from<br>model-proc   won't be loaded<br>Default: ""<br> |
  | attach-tensor-data | If true, metadata will contain both<br>post-processing results and raw tensor<br>data. If   false, metadata will contain<br>post-processing results only.<br>Default: True<br> |
  | threshold | Threshold for detection results. Only<br>regions of interest with confidence<br>values above the   threshold will be added<br>to the frame. Zero means default<br>(auto-selected) threshold<br>Default: 0.0*<br> |
  | scale-method | Scale method to use in pre-preprocessing<br>before inference<br>Default: <enum Default of   type<br>VideoInferenceScaleMethod><br> |
  | repeat-metadata | If true and inference-interval > 1,<br>metadata with last inference results<br>will be   attached to frames if inference<br>skipped. If true and<br>roi-inference-interval > 1, it requires<br>object-id   for each roi, so requires<br>object tracking element inserted before<br>this element.<br>Default:False<br> |
  | reclassify-interval | Determines how often to reclassify<br>tracked objects. Only valid when used   in<br>conjunction with gvatrack.<br>The following values are acceptable:<br><br>0 - Do not reclassify   tracked<br>objects<br>1 - Always reclassify tracked<br>objects<br>2:N - Tracked objects will be<br>reclassified   every N frames. Note<br>the inference-interval is applied<br>before determining if an object is<br>to be   reclassified (i.e.<br>classification only occurs at a<br>multiple of the inference interval)<br><br>Default:   1<br> |

## object_detect

Performs inference-based object detection

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | async-handling | The bin will handle Asynchronous state<br>changes<br>Default:False<br> |
  | message-forward | Forwards all children messages<br>Default:False<br> |
  | preprocess | Pre-processing element<br>Default: None<br> |
  | process | Main processing element<br>Default: None<br> |
  | postprocess | Post-processing element<br>Default: None<br> |
  | aggregate | (Optional) Element to aggregate<br>preprocess/process/postprocess result<br>and original   frame<br>Default: None<br> |
  | postaggregate | (Optional) Element inserted after<br>aggregation element<br>Default: None<br> |
  | preprocess-queue-size | Size of queue (in number buffers) before<br>pre-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | process-queue-size | Size of queue (in number buffers) before<br>processing element. Special values: -1<br>means   no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postprocess-queue-size | Size of queue (in number buffers) before<br>post-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | aggregate-queue-size | Size of queue (in number buffers) for<br>original frames between 'tee' and<br>aggregate   element. Special values: -1<br>means no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postaggregate-queue-size | Size of queue (in number buffers)<br>between aggregate and   post-aggregate<br>elements. Special values: -1 means no<br>queue element, 0 means queue of<br>unlimited   size<br>Default: 0<br> |
  | model | Path to inference model network file<br>Default: ""<br> |
  | ie-config | Comma separated list of KEY=VALUE<br>parameters for inference configuration<br>Default: ""<br> |
  | device | Target device for inference. Please see<br>inference backend documentation (ex,<br>OpenVINO™ Toolkit)   for list of supported<br>devices.<br>Default: CPU<br> |
  | model-instance-id | Identifier for sharing resources between<br>inference elements of the same type.<br>Elements   with the instance-id will share<br>model and other properties. If not<br>specified, a unique identifier will   be<br>generated.<br>Default: ""<br> |
  | nireq | Maximum number of inference requests<br>running in parallel<br>Default: 0<br> |
  | batch-size | Number of frames batched together for a<br>single inference. If the batch-size is<br>0, then it   will be set by default to be<br>optimal for the device. Not all models<br>support batching. Use model optimizer   to<br>ensure that the model has batching<br>support.<br>Default: 0<br> |
  | model-proc | Path to JSON file with parameters<br>describing how to build pre-process and<br>post-process   sub-pipelines<br>Default: ""<br> |
  | pre-process-backend | Preprocessing backend type<br>Default: <enum Automatic of   type<br>VideoInferenceBackend><br> |
  | inference-interval | Run inference for every Nth frame<br>Default: 1<br> |
  | roi-inference-interval | Determines how often to run inference on<br>each ROI object. Only valid if each   ROI<br>object has unique object id (requires<br>object tracking after object detection)<br>Default: 1<br> |
  | inference-region | Region on which inference will be<br>performed - full-frame or on each ROI<br>(region of   interest)bounding-box area<br>Default: <enum Perform inference for<br>full frame of   type<br>VideoInferenceRegion><br> |
  | object-class | Run inference only on Region-Of-Interest<br>with specified object class<br>Default: ""<br> |
  | labels | Path to file containing model's output<br>layer labels or comma separated list of<br>KEY=VALUE pairs   where KEY is name of<br>output layer and VALUE is path to labels<br>file. If provided, labels from<br>model-proc   won't be loaded<br>Default: ""<br> |
  | labels-file | Path to file containing model's output<br>layer labels. If provided, labels from<br>model-proc   won't be loaded<br>Default: ""<br> |
  | attach-tensor-data | If true, metadata will contain both<br>post-processing results and raw tensor<br>data. If   false, metadata will contain<br>post-processing results only.<br>Default: True<br> |
  | threshold | Threshold for detection results. Only<br>regions of interest with confidence<br>values above the   threshold will be added<br>to the frame. Zero means default<br>(auto-selected) threshold<br>Default: 0.0*<br> |
  | scale-method | Scale method to use in pre-preprocessing<br>before inference<br>Default: <enum Default of   type<br>VideoInferenceScaleMethod><br> |
  | repeat-metadata | If true and inference-interval > 1,<br>metadata with last inference results<br>will be   attached to frames if inference<br>skipped. If true and<br>roi-inference-interval > 1, it requires<br>object-id   for each roi, so requires<br>object tracking element inserted before<br>this element.<br>Default:False<br> |

## object_track

Assigns unique ID to detected objects

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | async-handling | The bin will handle Asynchronous state<br>changes<br>Default:False<br> |
  | message-forward | Forwards all children messages<br>Default:False<br> |
  | preprocess | Pre-processing element<br>Default: None<br> |
  | process | Main processing element<br>Default: None<br> |
  | postprocess | Post-processing element<br>Default: None<br> |
  | aggregate | (Optional) Element to aggregate<br>preprocess/process/postprocess result<br>and original   frame<br>Default: None<br> |
  | postaggregate | (Optional) Element inserted after<br>aggregation element<br>Default: None<br> |
  | preprocess-queue-size | Size of queue (in number buffers) before<br>pre-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | process-queue-size | Size of queue (in number buffers) before<br>processing element. Special values: -1<br>means   no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postprocess-queue-size | Size of queue (in number buffers) before<br>post-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | aggregate-queue-size | Size of queue (in number buffers) for<br>original frames between 'tee' and<br>aggregate   element. Special values: -1<br>means no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postaggregate-queue-size | Size of queue (in number buffers)<br>between aggregate and   post-aggregate<br>elements. Special values: -1 means no<br>queue element, 0 means queue of<br>unlimited   size<br>Default: 0<br> |
  | model | Path to inference model network file<br>Default: ""<br> |
  | ie-config | Comma separated list of KEY=VALUE<br>parameters for inference configuration<br>Default: ""<br> |
  | device | Target device for inference. Please see<br>inference backend documentation (ex,<br>OpenVINO™ Toolkit)   for list of supported<br>devices.<br>Default: CPU<br> |
  | model-instance-id | Identifier for sharing resources between<br>inference elements of the same type.<br>Elements   with the instance-id will share<br>model and other properties. If not<br>specified, a unique identifier will   be<br>generated.<br>Default: ""<br> |
  | nireq | Maximum number of inference requests<br>running in parallel<br>Default: 0<br> |
  | batch-size | Number of frames batched together for a<br>single inference. If the batch-size is<br>0, then it   will be set by default to be<br>optimal for the device. Not all models<br>support batching. Use model optimizer   to<br>ensure that the model has batching<br>support.<br>Default: 0<br> |
  | model-proc | Path to JSON file with parameters<br>describing how to build pre-process and<br>post-process   sub-pipelines<br>Default: ""<br> |
  | pre-process-backend | Preprocessing backend type<br>Default: <enum Automatic of   type<br>VideoInferenceBackend><br> |
  | inference-interval | Run inference for every Nth frame<br>Default: 1<br> |
  | roi-inference-interval | Determines how often to run inference on<br>each ROI object. Only valid if each   ROI<br>object has unique object id (requires<br>object tracking after object detection)<br>Default: 1<br> |
  | inference-region | Region on which inference will be<br>performed - full-frame or on each ROI<br>(region of   interest)bounding-box area<br>Default: <enum Perform inference for<br>full frame of   type<br>VideoInferenceRegion><br> |
  | object-class | Run inference only on Region-Of-Interest<br>with specified object class<br>Default: ""<br> |
  | labels | Path to file containing model's output<br>layer labels or comma separated list of<br>KEY=VALUE pairs   where KEY is name of<br>output layer and VALUE is path to labels<br>file. If provided, labels from<br>model-proc   won't be loaded<br>Default: ""<br> |
  | labels-file | Path to file containing model's output<br>layer labels. If provided, labels from<br>model-proc   won't be loaded<br>Default: ""<br> |
  | attach-tensor-data | If true, metadata will contain both<br>post-processing results and raw tensor<br>data. If   false, metadata will contain<br>post-processing results only.<br>Default: True<br> |
  | threshold | Threshold for detection results. Only<br>regions of interest with confidence<br>values above the   threshold will be added<br>to the frame. Zero means default<br>(auto-selected) threshold<br>Default: 0.0*<br> |
  | scale-method | Scale method to use in pre-preprocessing<br>before inference<br>Default: <enum Default of   type<br>VideoInferenceScaleMethod><br> |
  | repeat-metadata | If true and inference-interval > 1,<br>metadata with last inference results<br>will be   attached to frames if inference<br>skipped. If true and<br>roi-inference-interval > 1, it requires<br>object-id   for each roi, so requires<br>object tracking element inserted before<br>this element.<br>Default:False<br> |
  | generate-objects | If true, generate objects (according to<br>previous trajectory) if not detected on<br>current   frame<br>Default: True<br> |
  | adjust-objects | If true, adjust object position for more<br>smooth trajectory<br>Default: True<br> |
  | tracking-per-class | If true, object association takes into<br>account object class<br>Default:False<br> |
  | spatial-feature | Spatial feature used by object tracking<br>algorithm<br>Default: <enum Spatial feature   not<br>used (only temporal features used, such<br>as object shape and trajectory) of   type<br>SpatialFeatureType><br> |
  | spatial-feature-distance | Method to calculate distance between two<br>spatial features<br>Default: <enum   Spatial feature not<br>used of type<br>SpatialFeatureDistanceType><br> |
  | tracking-type | DEPRECATED - use other properties<br>according to the following   mapping:<br>zero-term-imageless:<br>generate-objects=false<br>adjust-objects=false<br>spatial-feature=none<br>zero-  term:   generate-objects=false<br>adjust-objects=false<br>spatial-feature=sliced-histogram<br>short-term-imageless:<br>gene  rate-objects=true<br>adjust-objects=false<br>spatial-feature=none<br>short-term:   generate-objects=true<br>adjust-objects=false<br>spatial-feature=sliced-histogram<br>Default: ""<br> |


## processbin

Bin element for processing pipelines using branching: tee name=t t. !
\<preprocess\> ! \<process\> ! \<postprocess\> ! \<aggregate\> t. !
aggregate

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always |
  | SRC template: src | Availability: Always |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | async-handling | The bin will handle Asynchronous state<br>changes<br>Default:False<br> |
  | message-forward | Forwards all children messages<br>Default:False<br> |
  | preprocess | Pre-processing element<br>Default: None<br> |
  | process | Main processing element<br>Default: None<br> |
  | postprocess | Post-processing element<br>Default: None<br> |
  | aggregate | (Optional) Element to aggregate<br>preprocess/process/postprocess result<br>and original   frame<br>Default: None<br> |
  | postaggregate | (Optional) Element inserted after<br>aggregation element<br>Default: None<br> |
  | preprocess-queue-size | Size of queue (in number buffers) before<br>pre-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | process-queue-size | Size of queue (in number buffers) before<br>processing element. Special values: -1<br>means   no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postprocess-queue-size | Size of queue (in number buffers) before<br>post-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | aggregate-queue-size | Size of queue (in number buffers) for<br>original frames between 'tee' and<br>aggregate   element. Special values: -1<br>means no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postaggregate-queue-size | Size of queue (in number buffers)<br>between aggregate and   post-aggregate<br>elements. Special values: -1 means no<br>queue element, 0 means queue of<br>unlimited   size<br>Default: 0<br> |


## video_inference

Runs Deep Learning inference on any model with RGB-like input

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | async-handling | The bin will handle Asynchronous state<br>changes<br>Default:False<br> |
  | message-forward | Forwards all children messages<br>Default:False<br> |
  | preprocess | Pre-processing element<br>Default: None<br> |
  | process | Main processing element<br>Default: None<br> |
  | postprocess | Post-processing element<br>Default: None<br> |
  | aggregate | (Optional) Element to aggregate<br>preprocess/process/postprocess result<br>and original   frame<br>Default: None<br> |
  | postaggregate | (Optional) Element inserted after<br>aggregation element<br>Default: None<br> |
  | preprocess-queue-size | Size of queue (in number buffers) before<br>pre-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | process-queue-size | Size of queue (in number buffers) before<br>processing element. Special values: -1<br>means   no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postprocess-queue-size | Size of queue (in number buffers) before<br>post-processing element. Special   values:<br>-1 means no queue element, 0 means queue<br>of unlimited size<br>Default: 0<br> |
  | aggregate-queue-size | Size of queue (in number buffers) for<br>original frames between 'tee' and<br>aggregate   element. Special values: -1<br>means no queue element, 0 means queue of<br>unlimited size<br>Default: 0<br> |
  | postaggregate-queue-size | Size of queue (in number buffers)<br>between aggregate and   post-aggregate<br>elements. Special values: -1 means no<br>queue element, 0 means queue of<br>unlimited   size<br>Default: 0<br> |
  | model | Path to inference model network file<br>Default: ""<br> |
  | ie-config | Comma separated list of KEY=VALUE<br>parameters for inference configuration<br>Default: ""<br> |
  | device | Target device for inference. Please see<br>inference backend documentation (ex,<br>OpenVINO™ Toolkit)   for list of supported<br>devices.<br>Default: CPU<br> |
  | model-instance-id | Identifier for sharing resources between<br>inference elements of the same type.<br>Elements   with the instance-id will share<br>model and other properties. If not<br>specified, a unique identifier will   be<br>generated.<br>Default: ""<br> |
  | nireq | Maximum number of inference requests<br>running in parallel<br>Default: 0<br> |
  | batch-size | Number of frames batched together for a<br>single inference. If the batch-size is<br>0, then it   will be set by default to be<br>optimal for the device. Not all models<br>support batching. Use model optimizer   to<br>ensure that the model has batching<br>support.<br>Default: 0<br> |
  | model-proc | Path to JSON file with parameters<br>describing how to build pre-process and<br>post-process   sub-pipelines<br>Default: ""<br> |
  | pre-process-backend | Preprocessing backend type<br>Default: <enum Automatic of   type<br>VideoInferenceBackend><br> |
  | inference-interval | Run inference for every Nth frame<br>Default: 1<br> |
  | roi-inference-interval | Determines how often to run inference on<br>each ROI object. Only valid if each   ROI<br>object has unique object id (requires<br>object tracking after object detection)<br>Default: 1<br> |
  | inference-region | Region on which inference will be<br>performed - full-frame or on each ROI<br>(region of   interest)bounding-box area<br>Default: <enum Perform inference for<br>full frame of   type<br>VideoInferenceRegion><br> |
  | object-class | Run inference only on Region-Of-Interest<br>with specified object class<br>Default: ""<br> |
  | labels | Path to file containing model's output<br>layer labels or comma separated list of<br>KEY=VALUE pairs   where KEY is name of<br>output layer and VALUE is path to labels<br>file. If provided, labels from<br>model-proc   won't be loaded<br>Default: ""<br> |
  | labels-file | Path to file containing model's output<br>layer labels. If provided, labels from<br>model-proc   won't be loaded<br>Default: ""<br> |
  | attach-tensor-data | If true, metadata will contain both<br>post-processing results and raw tensor<br>data. If   false, metadata will contain<br>post-processing results only.<br>Default: True<br> |
  | threshold | Threshold for detection results. Only<br>regions of interest with confidence<br>values above the   threshold will be added<br>to the frame. Zero means default<br>(auto-selected) threshold<br>Default: 0.0*<br> |
  | scale-method | Scale method to use in pre-preprocessing<br>before inference<br>Default: <enum Default of   type<br>VideoInferenceScaleMethod><br> |
  | repeat-metadata | If true and inference-interval > 1,<br>metadata with last inference results<br>will be   attached to frames if inference<br>skipped. If true and<br>roi-inference-interval > 1, it requires<br>object-id   for each roi, so requires<br>object tracking element inserted before<br>this element.<br>Default:False<br> |


## batch_create

Accumulate multiple buffers into single buffer with multiple GstMemory

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always |
  | SRC template: src | Availability: Always |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | batch-size | Number of frames to batch together<br>Default: 1<br> |


## batch_split

Split input tensor (remove batch dimension from tensor shape)

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always |
  | SRC template: src | Availability: Always |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |

## capsrelax

Pass data without modification, relaxes formats

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always |
  | SRC template: src | Availability: Always |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |

## gvadrop

Pass / drop custom number of frames in pipeline

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always |
  | SRC template: src | Availability: Always |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | pass-frames | Number of frames to pass along the pipeline.<br>Default: 1<br> |
  | drop-frames | Number of frames to drop.<br>Default: 0<br> |
  | mode | Mode defines what to do with dropped frames<br>Default: <enum Default of type GvaDropMode><br> |


## meta_aggregate

Muxes video streams with tensor's ROI into single stream

- **Capabilities**

  | SINK template: [meta]()%u | <br>Availability: On request<br>Capabilities:<br>-   video/x-raw<br><br> |
  |---|---|
  | SINK template: [tensor]()%u | <br>Availability: On request<br>Capabilities:<br>-   other/tensors<br><br> |
  | SINK template: sink | - Availability: Always |
  | SRC template: src | - Availability: Always |


- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | latency | Additional latency in live mode to allow<br>upstream to take longer to produce buffers<br>for the   current position (in nanoseconds)<br>Default: 0<br> |
  | min-upstream-latency | When sources with a higher latency are<br>expected to be plugged in dynamically   after<br>the aggregator has started playing, this<br>allows overriding the minimum latency<br>reported by the   initial source(s). This is<br>only taken into account when larger than the<br>actually reported minimum latency.  <br>(nanoseconds)<br>Default: 0<br> |
  | start-time-selection | Decides which start time is output<br>Default:   <enum<br>GST_AGGREGATOR_START_TIME_SELECTION_ZERO of<br>type GstAggregatorStartTimeSelection><br> |
  | start-time | Start time to use if<br>start-time-selection=set<br>Default: 18446744073709551615<br> |
  | emit-signals | Send signals<br>Default:False<br> |
  | attach-tensor-data | If true, additionally copies tensor data<br>into metadata<br>Default: True<br> |


## meta_smooth

smooth metadata

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always |
  | SRC template: src | Availability: Always |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |

## roi_split

Split buffer with multiple GstVideoRegionOfInterestMeta into multiple
buffers

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always |
  | SRC template: src | Availability: Always |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | object-class | Filter ROI list by object class(es) (comma separated<br>list if multiple). Output only ROIs with specified<br>object class(es)<br>Default: ""<br> |


## video_frames_buffer

Buffer and optionally repeat compressed video frames

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always |
  | SRC template: src | Availability: Always |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | num-input-frames | Number input frames to buffer<br>Default: 0<br> |
  | num-output-frames | Max number output frames in 'loop' mode<br>Default: 0<br> |


## rate_adjust

Adjust frame rate. Output frame rate is input rate multiplied by
(numerator/denominator)

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always |
  | SRC template: src | Availability: Always |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | ratio | Frame rate ratio - output frame rate is input rate<br>multiplied by specified ratio. Current limitation: ratio<br><= 1<br>Default: None<br> |


## tensor_convert

Convert (zero-copy if possible) between video/audio and tensors media
type

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: RGB<br><br><br>video/x-raw<br>format: BGR<br><br><br>video/x-raw<br>format: RGBA<br><br><br>video/x-raw<br>format: BGRA<br><br><br>video/x-raw<br>format: RGBP<br><br><br>video/x-raw<br>format: BGRP<br><br><br><br><br><br> |
  | SRC template: src | <br>Availability: Always<br>Capabilities:<br>other/tensors<br>num_tensors: 1<br>types: uint8<br><br><br><br><br><br> |



- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |


## tensor_histogram

Calculates histogram on tensors of UInt8 data type and NHWC layout

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | <br>Availability: Always<br>Capabilities:<br>other/tensors<br>num_tensors: 1<br>types: uint8<br><br><br><br><br><br> |
  | SRC template: src | <br>Availability: Always<br>Capabilities:<br>other/tensors<br>num_tensors: 1<br>types: float32<br><br><br><br><br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | width | Input tensor width, assuming tensor in NHWC or NCHW<br>layout<br>Default: 64<br> |
  | height | Input tensor height, assuming tensor in NHWC or NCHW<br>layout<br>Default: 64<br> |
  | num-slices-x | Number slices along X-axis<br>Default: 1<br> |
  | num-slices-y | Number slices along Y-axis<br>Default: 1<br> |
  | num-bins | Number bins in histogram calculation. Example, for<br>3-channel tensor (RGB image), output histogram size<br>is equal to (num_bin^3 * num_slices_x *<br>num_slices_y)<br>Default: 8<br> |
  | batch-size | Batch size<br>Default: 1<br> |
  | device | `CPU` or `GPU` or `GPU.0`, `GPU.1`, ..<br>Default: ""<br> |


## tensor_postproc_add_params

Post-processing to only add properties/parameters to metadata

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |


- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | attribute-name | Name for metadata created and attached by this element<br>Default: attribute<br> |
  | format | Format description<br>Default: ""<br> |


## tensor_postproc_detection

Post-processing of object detection inference to extract bounding box
coordinates, confidence, label, mask

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |


- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | labels | Array of object classes<br>Default: None<br> |
  | labels-file | Path to .txt file containing object classes<br>(one per line)<br>Default: ""<br> |
  | threshold | Detection threshold - only objects with<br>confidence values above the threshold will be<br>added to the frame<br>Default: 0.5<br> |
  | box-index | Index of layer containing bounding box data<br>Default: -1<br> |
  | confidence-index | Index of layer containing confidence data<br>Default: -1<br> |
  | label-index | Index of layer containing label data<br>Default: -1<br> |
  | imageid-index | Index of layer containing imageid data<br>Default: -1<br> |
  | mask-index | Index of layer containing mask data<br>Default: -1<br> |
  | box-offset | Offset inside layer containing bounding box<br>data<br>Default: -1<br> |
  | confidence-offset | Offset inside layer containing confidence data<br>Default: -1<br> |
  | label-offset | Offset inside layer containing label data<br>Default: -1<br> |
  | imageid-offset | Offset inside layer containing imageid data<br>Default: -1<br> |


## tensor_postproc_label

Post-processing of classification inference to extract object classes

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |


- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | method | Method used to post-process tensor data<br>Default: <enum max of type method><br> |
  | labels | Array of object classes<br>Default: None<br> |
  | labels-file | Path to .txt file containing object classes<br>(one per line)<br>Default: ""<br> |
  | attribute-name | Name for metadata created and attached by this<br>element<br>Default: ""<br> |
  | layer-name | Name of output layer to process (in case of<br>multiple output tensors)<br>Default: ""<br> |
  | threshold | Threshold for confidence values<br>Default: 0.0*<br> |
  | compound-threshold | Threshold for compound method<br>*Default: `0.5`*<br> |


## tensor_postproc_text

Post-processing to convert tensor data into text

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | text-scale | Scale tensor values before converting to text<br>*Default: `1.0`*<br> |
  | text-precision | Precision for floating-point to text conversion<br>Default: 0<br> |
  | attribute-name | Name for metadata created and attached by this<br>element<br>Default: ""<br> |
  | layer-name | Name of output layer to process (in case of<br>multiple output tensors)<br>Default: ""<br> |


## tensor_postproc_yolo

Post-processing of YOLO models to extract bounding box list

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | version | Yolo's version number. Supported only<br>from 3 to 5<br>Default: 0<br> |
  | labels | Array of object classes<br>Default: None<br> |
  | labels-file | Path to .txt file containing object<br>classes (one per line)<br>Default: ""<br> |
  | threshold | Detection threshold - only objects with<br>confidence value above the threshold<br>will be added to the frame<br>Default: 0.5<br> |
  | anchors | Anchor values array<br>Default: None<br> |
  | masks | Masks values array (1 dimension)<br>Default: None<br> |
  | iou-threshold | IntersectionOverUnion threshold<br>Default: 0.5<br> |
  | do-cls-softmax | If true, perform softmax<br>Default: True<br> |
  | output-sigmoid-activation | output_sigmoid_activation<br>Default: True<br> |
  | cells-number | Number of cells. Use if number of cells<br>along x and y axes is the same (0 =<br>autodetection)<br>Default: 0<br> |
  | cells-number-x | Number of cells along x-axis<br>Default: 0<br> |
  | cells-number-y | Number of cells along y-axis<br>Default: 0<br> |
  | bbox-number-on-cell | Number of bounding boxes that can be<br>predicted per cell (0 = autodetection)<br>Default: 0<br> |
  | classes | Number of classes<br>Default: 0<br> |
  | nms | Apply Non-Maximum Suppression (NMS)<br>filter to bounding boxes<br>Default: True<br> |


## tensor_sliding_window

Sliding aggregation of input tensors

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |

## openvino_tensor_inference

Inference on OpenVINO™ toolkit backend

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   other/tensors<br>-   other/tensors<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |


- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | model | Path to model file in OpenVINO™ toolkit or<br>ONNX format<br>Default: ""<br> |
  | device | Target device for inference. Please see<br>OpenVINO™ toolkit documentation for list of<br>supported devices.<br>Default: CPU<br> |
  | config | Comma separated list of KEY=VALUE parameters<br>for Inference Engine configuration<br>Default: ""<br> |
  | batch-size | Batch size<br>Default: 1<br> |
  | buffer-pool-size | Output buffer pool size (functionally same as<br>OpenVINO™ toolkit nireq parameter)<br>Default: 16<br> |
  | shared-instance-id | Identifier for sharing backend instance<br>between multiple elements, for example in<br>elements processing multiple inputs<br>Default: ""<br> |


## openvino_video_inference

Inference on OpenVINO™ toolkit backend

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: NV12<br><br><br><br><br><br> |
  | SRC template: src | <br>Availability: Always<br>Capabilities:<br>other/tensors<br><br><br><br> |



- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | model | Path to model file in OpenVINO™ toolkit or<br>ONNX format<br>Default: ""<br> |
  | device | Target device for inference. Please see<br>OpenVINO™ toolkit documentation for list of<br>supported devices.<br>Default: CPU<br> |
  | config | Comma separated list of KEY=VALUE parameters<br>for Inference Engine configuration<br>Default: ""<br> |
  | batch-size | Batch size<br>Default: 1<br> |
  | buffer-pool-size | Output buffer pool size (functionally same as<br>OpenVINO™ toolkit nireq parameter)<br>Default: 16<br> |
  | shared-instance-id | Identifier for sharing backend instance<br>between multiple elements, for example in<br>elements processing multiple inputs<br>Default: ""<br> |


## opencv_cropscale

Fused video crop and scale on OpenCV backend. Crop operation supports
GstVideoCropMeta if attached to input buffer

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: RGB<br><br><br>video/x-raw<br>format: BGR<br><br><br>video/x-raw<br>format: RGBA<br><br><br>video/x-raw<br>format: BGRA<br><br><br><br><br><br> |
  | SRC template: src | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: RGB<br><br><br>video/x-raw<br>format: BGR<br><br><br>video/x-raw<br>format: RGBA<br><br><br>video/x-raw<br>format: BGRA<br><br><br><br><br><br> |


- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | add-borders | Add borders if necessary to keep the aspect ratio<br>Default:False<br> |


## opencv_find_contours

Find contour points of given mask using opencv

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always |
  | SRC template: src | Availability: Always |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | mask-metadata-name | Name of metadata containing segmentation<br>mask<br>Default: mask<br> |
  | contour-metadata-name | Name of metadata created by this element to<br>store contour(s)<br>Default: contour<br> |
  | threshold | Mask threshold - only mask pixels with<br>confidence values above the threshold will<br>be used for finding contours<br>Default: 0.5<br> |


## opencv_meta_overlay

Visualize inference results using OpenCV

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: BGRA<br><br><br>video/x-raw<br>format: RGBA<br><br><br>video/x-raw<br>format: BGRA<br><br><br>video/x-raw<br>format: RGBA<br><br><br><br><br><br> |
  | SRC template: src | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: BGRA<br><br><br>video/x-raw<br>format: RGBA<br><br><br>video/x-raw<br>format: BGRA<br><br><br>video/x-raw<br>format: RGBA<br><br><br><br><br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | lines-thickness | Thickness of lines and rectangles<br>Default: 2<br> |
  | font-thickness | Font thickness<br>Default: 1<br> |
  | font-scale | Font scale<br>*Default: `1.0`*<br> |
  | attach-label-mask | Attach label mask as metadata, image not changed<br>Default:False<br> |


## opencv_object_association

Assigns unique ID to ROI objects based on objects trajectory and
(optionally) feature vector obtained from ROI metadata

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always |
  | SRC template: src | Availability: Always |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | generate-objects | If true, generate objects<br>(according to previous trajectory)<br>if not detected on current frame<br>Default: True<br> |
  | adjust-objects | If true, adjust object position for<br>more smooth trajectory<br>Default: True<br> |
  | tracking-per-class | If true, object association takes<br>into account object class<br>Default:False<br> |
  | spatial-feature-metadata-name | Name of metadata containing spatial<br>feature<br>Default: spatial-feature<br> |
  | spatial-feature-distance | Method to calculate distance<br>between two spatial features<br>Default: <enum bhattacharyya of<br>type spatial-feature-distance><br> |
  | shape-feature-weight | Weighting factor for shape-based<br>feature<br>Default: 0.75<br> |
  | trajectory-feature-weight | Weighting factor for<br>trajectory-based feature<br>Default: 0.5<br> |
  | spatial-feature-weight | Weighting factor for spatial<br>feature<br>Default: 0.25<br> |
  | min-region-ratio-in-boundary | > Min region ratio in image<br>> boundary<br>Default: 0.75<br> |


## opencv_remove_background

Remove background using mask

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: RGB<br><br><br>video/x-raw<br>format: BGR<br><br><br>video/x-raw<br>format: RGBA<br><br><br>video/x-raw<br>format: BGRA<br><br><br><br><br><br> |
  | SRC template: src | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: RGB<br><br><br>video/x-raw<br>format: BGR<br><br><br>video/x-raw<br>format: RGBA<br><br><br>video/x-raw<br>format: BGRA<br><br><br><br><br><br> |



- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | mask-metadata-name | Name of metadata containing segmentation mask<br>Default: mask<br> |
  | threshold | Mask threshold - only mask pixels with<br>confidence values above the threshold will be<br>used for setting transparency<br>Default: 0.5<br> |

## opencv_tensor_normalize

Convert U8 tensor to F32 tensor with normalization

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | <br>Availability: Always<br>Capabilities:<br>other/tensors<br>num_tensors: 1<br>types: uint8<br><br><br><br><br><br> |
  | SRC template: src | <br>Availability: Always<br>Capabilities:<br>other/tensors<br>num_tensors: 1<br>types: float32<br><br><br><br><br><br> |



- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | range | Normalization range MIN, MAX. Example: <0,1><br>Default: None<br> |
  | mean | Mean values per channel. Example: <0.485,0.456,0.406><br>Default: None<br> |
  | std | Standard deviation values per channel. Example:<br><0.229,0.224,0.225><br>Default: None<br> |


## opencv_warp_affine

Rotation using cv::warpAffine

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: RGB<br><br><br>video/x-raw<br>format: BGR<br><br><br>video/x-raw<br>format: RGBA<br><br><br>video/x-raw<br>format: BGRA<br><br><br><br><br><br> |
  | SRC template: src | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: RGB<br><br><br>video/x-raw<br>format: BGR<br><br><br>video/x-raw<br>format: RGBA<br><br><br>video/x-raw<br>format: BGRA<br><br><br><br><br><br> |



- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | angle | Angle by which the picture is rotated (in radians)<br>Default: 0.0*<br> |
  | sync | Wait for OpenCL kernel completion (if running on GPU via cv::UMat)<br>Default:False<br> |


## tensor_postproc_human_pose

Post-processing to extract key points from human pose estimation model
output

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |


- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | point-names | Array of key point names<br>Default: None<br> |
  | point-connections | Array of point connections {name-A0, name-B0,<br>name-A1, name-B1, ...}<br>Default: None<br> |


## vaapi_batch_proc

Batched pre-processing with VAAPI memory as input and output

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | add-borders | Add borders if necessary to keep the aspect<br>ratio<br>Default:False<br> |
  | output-format | Image format for output frames: BGR or RGB or<br>GRAY<br>Default: BGR<br> |
  | shared-instance-id | Identifier for sharing backend instance<br>between multiple elements, for example in<br>elements processing multiple inputs<br>Default: ""<br> |

## vaapi_sync

Synchronize VAAPI surfaces (call vaSyncSurface)

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   video/x-raw<br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | timeout | Synchronization timeout (seconds)<br>*Default: `10.0`*<br> |


## opencl_tensor_normalize

Convert U8 tensor to U8 or F32 tensor with normalization

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |


- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | shared-instance-id | Identifier for sharing backend instance<br>between multiple elements, for example in<br>elements processing multiple inputs<br>Default: ""<br> |


## vaapi_to_opencl

Convert memory:VASurface to memory:OpenCL

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   video/x-raw<br>-   other/tensors<br><br> |
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |


- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |

## sycl_meta_overlay

Visualize inference results using DPC++/SYCL backend

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: BGRA<br><br><br>video/x-raw<br>format: RGBA<br><br><br><br><br><br> |
  | SRC template: src | <br>Availability: Always<br>Capabilities:<br>video/x-raw<br>format: BGRA<br><br><br>video/x-raw<br>format: RGBA<br><br><br><br><br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | lines-thickness | Thickness of lines and rectangles<br>Default: 2<br> |


## sycl_tensor_histogram

Calculates histogram on tensors of UInt8 data type and NHWC layout

- **Capabilities**

  |  |  |
  |---|---|
  | SINK template: sink | <br>Availability: Always<br>Capabilities:<br>other/tensors<br>num_tensors: 1<br>types: uint8<br><br><br>other/tensors<br>num_tensors: 1<br>types: uint8<br><br><br><br><br><br> |
  | SRC template: src | <br>Availability: Always<br>Capabilities:<br>other/tensors<br>num_tensors: 1<br>types: float32<br><br><br><br><br><br> |

- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | width | Input tensor width, assuming tensor in NHWC or NCHW<br>layout<br>Default: 64<br> |
  | height | Input tensor height, assuming tensor in NHWC or NCHW<br>layout<br>Default: 64<br> |
  | num-slices-x | Number slices along X-axis<br>Default: 1<br> |
  | num-slices-y | Number slices along Y-axis<br>Default: 1<br> |
  | num-bins | Number bins in histogram calculation. Example, for<br>3-channel tensor (RGB image), output histogram size<br>is equal to (num_bin^3 * num_slices_x *<br>num_slices_y)<br>Default: 8<br> |
  | batch-size | Batch size<br>Default: 1<br> |
  | device | `CPU` or `GPU` or `GPU.0`, `GPU.1`, ..<br>Default: ""<br> |


## inference_openvino

OpenVINO™ toolkit inference element

- **Capabilities**

  |  |  |
  |---|---|
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |


- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | device | Inference device<br>Default: CPU<br> |
  | model | OpenVINO™ toolkit model path<br>Default: ""<br> |
  | nireq | Number inference requests<br>Default: 0<br> |


## pytorch_tensor_inference

PyTorch inference element

- **Capabilities**

  |  |  |
  |---|---|
  | SRC template: src | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |
  | SINK template: sink | Availability: Always<br>Capabilities:<br>-   other/tensors<br><br> |


- **Properties**

  | Name | Description |
  |---|---|
  | name | The name of the object<br>Default: None<br> |
  | parent | The parent of the object<br>Default: None<br> |
  | qos | Handle Quality-of-Service events<br>Default:False<br> |
  | device | Inference device<br>Default: cpu<br> |
  | model | The full module name of the PyTorch model to be<br>imported from torchvision or model path. Ex.<br>'torchvision.models.resnet50' or<br>'/path/to/model.pth'<br>Default: ""<br> |
  | model-weights | PyTorch model weights path. If model-weights is<br>empty, the default weights will be used<br>Default: ""<br> |
