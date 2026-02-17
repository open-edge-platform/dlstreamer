# gvawatermark

Overlays the metadata on the video frame to visualize the inference
results.

```bash
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-raw
                format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:DMABuf)
                format: { (string)RGBA, (string)I420 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:VASurface)
                format: { (string)NV12 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:VAMemory)
                format: { (string)NV12 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw
                format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:DMABuf)
                format: { (string)RGBA, (string)I420 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:VASurface)
                format: { (string)NV12 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:VAMemory)
                format: { (string)NV12 }
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:
  async-handling      : The bin will handle Asynchronous state changes
                        flags: readable, writable
                        Boolean. Default: false
  displ-cfg           : Comma separated list of KEY=VALUE parameters of displayed notations.
                        Available options:
                        show-labels=<bool> enable or disable displaying text labels, default true
                        font-scale=<double 0.1 to 2.0> scale factor for text labels, default 1.0
                        thickness=<uint> bounding box thickness, default 2
                        color-idx=<int> color index for bounding box, keypoints, and text, default -1 (use default colors: 0 red, 1 green, 2 blue)
                        draw-txt-bg=<bool> enable or disable displaying text labels background, by enabling it the text color is set to white, default false
                        font-type=<string> font type for text labels, default triplex. Supported fonts: simplex, plain, duplex, complex, triplex, complex_small, script_simplex, script_complex
                        enable-blur=<bool> enable ROI blurring for privacy protection, default false
                        blur-filter=<string> colon-separated list of object labels to blur (e.g., "face:person")
                        blur-filterout=<string> colon-separated list of labels to exclude from blurring (blur-filter takes precedence)
                        
                        e.g.: displ-cfg=show-labels=false
                        e.g.: displ-cfg=font-scale=0.5,thickness=3,color-idx=2,font-type=plain
                        e.g.: displ-cfg=enable-blur=true,blur-filter=face:person
                        flags: readable, writable
                        String. Default: null
  device              : Supported devices are CPU and GPU. Default is CPU on system memory and GPU on video memory
                        flags: readable, writable
                        String. Default: null
  displ-avgfps        : If true, display the average FPS read from gvafpscounter element on the output video.
                        The gvafpscounter element must be present in the pipeline.
                        e.g. ... ! gvawatermark displ-avgfps=true ! gvafpscounter ! ...
                        flags: readable, writable
                        Boolean. Default: false
  message-forward     : Forwards all children messages
                        flags: readable, writable
                        Boolean. Default: false
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvawatermark0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
```

## Visual Examples

The following examples demonstrate how different `displ-cfg` parameters affect the watermark appearance:

### Font Scale

Controls the size of text labels displayed on detected objects.

**Small Font Scale (`font-scale=0.7`)**
![Text Scale 0.7](../_images/text-scale-0-7.png)

**Large Font Scale (`font-scale=2.0`)**  
![Text Scale 2.0](../_images/text-scale-2-0.png)

### Label Display

**Labels Disabled (`show-labels=false`)**
![Disabled Labels](../_images/gvawatermark-disabled-labels.png)

*Shows only bounding boxes without text labels*

### Text Background

**Text Background Enabled (`draw-txt-bg=true`)**
![Text Background](../_images/show-text-background.png)

*Blue background makes text more readable over complex backgrounds*

### Color Index

**Red Color (`color-idx=0`)**
![Color Index Red](../_images/color-idx-zero.png)

*Uses red color for bounding boxes and text*

### Font Types

**Font Comparison**
![Font Types](../_images/simple-complex-font.png)

*Comparison of different font types (simplex vs complex)*

**Default Triplex Font**
![Triplex Font](../_images/triplex-font.png)

*The default triplex font provides good readability*

### FPS Display

**Average FPS Display (`displ-avgfps=true`)**
![Show FPS](../_images/show-avg-fps.png)

*Displays average FPS counter when `gvafpscounter` element is present in pipeline*

### Blur Feature

The gvawatermark element supports privacy protection through region of interest (ROI) blurring using OpenCV GaussianBlur with a 15x15 kernel. This feature is useful for anonymizing faces, license plates, or other sensitive content in video streams.

#### Blur Configuration Parameters

Blur functionality is controlled through the `displ-cfg` parameter with these options:

- **enable-blur=<bool>** - Enable or disable blur feature (default: false)
- **blur-filter=<string>** - Colon-separated list of object labels to blur (e.g., "person:face")
- **blur-filterout=<string>** - Colon-separated list of object labels to exclude from blurring

#### Blur Examples

**Basic Blur (All detected objects)**
```bash
# Blur all detected ROIs
displ-cfg=enable-blur=true
```
![Basic Blue](../_images/blur-them-all.png)

**Selective Blur (Include specific labels)**
```bash
# Blur only faces and persons
displ-cfg=enable-blur=true,blur-filter=face:person
```
![Blur Person](../_images/only-person-blured.png)

```bash
# Blur only cars
displ-cfg=enable-blur=true,blur-filter=car
```
![Blur Car](../_images/only-car-blured.png)

**Exclude Blur (Blur all except specific labels)**
```bash
# Blur all objects except cars and trucks
displ-cfg=enable-blur=true,blur-filterout=car:truck

# Blur all except persons
displ-cfg=enable-blur=true,blur-filterout=person
```

**Combined with Display Options**
```bash
# Blur faces with custom display settings
displ-cfg=enable-blur=true,blur-filter=face,show-labels=false,thickness=1

# Blur with labels and colored boxes
displ-cfg=enable-blur=true,blur-filter=person:face,color-idx=0,font-scale=0.8
```

#### Filter Precedence Rules

`blur-filter` takes precedence over `blur-filterout`. When `blur-filter` is specified and not empty, only objects matching those labels will be blurred, and `blur-filterout` is ignored.

1. **blur-filter specified and not empty**: Only labels in blur-filter are blurred
2. **blur-filter empty or unspecified**: All labels are blurred except those in blur-filterout
3. **Neither specified**: All detected ROIs are blurred when enable-blur=true

#### Performance Considerations

- GPU/VAAPI acceleration is used when available for better performance
- CPU fallback ensures compatibility across different hardware configurations
- Blur processing adds computational overhead; consider impact on pipeline throughput

### Configuration Examples

```bash
# Minimal labels with smaller font
displ-cfg=show-labels=false

# Large text with background
displ-cfg=font-scale=2.0,draw-txt-bg=true

# Colored thin boxes with simple font  
displ-cfg=color-idx=0,thickness=1,font-type=simplex

# Complete custom styling
displ-cfg=font-scale=1.5,thickness=3,color-idx=2,font-type=complex,draw-txt-bg=true

# Privacy protection with blur
displ-cfg=enable-blur=true,blur-filter=face:person,show-labels=false

# Blur all except specific objects
displ-cfg=enable-blur=true,blur-filterout=car:truck,thickness=1
```
