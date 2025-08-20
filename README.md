# Dynamic Video Compositor

A GStreamer-based video compositor that allows dynamic addition, removal, and positioning of multiple video sources in real-time.

## Features

- **Dynamic Source Management**: Add and remove video sources at runtime
- **Real-time Positioning**: Move video sources to different positions on the canvas
- **Multi-source Support**: Handle multiple video files simultaneously
- **Live Preview**: Real-time video output with composited sources
- **Interactive Command Interface**: Control the compositor via command line

## Building

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

### Basic Usage
```bash
./video_compositor [video_file1] [video_file2] ...
```

### Interactive Commands
Once the compositor is running, you can use these commands:

- `add <video_file> <xpos> <ypos>` - Add a new video source at position (x,y)
- `remove <source_id>` - Remove a video source by ID
- `move <source_id> <xpos> <ypos>` - Move a video source to new position
- `list` - List all active sources
- `help` - Show available commands
- `quit` - Exit the application

### Example
```bash
# Start with two video sources
./video_compositor video1.mp4 video2.mp4

# Add a third source at position (640, 0)
> add video3.mp4 640 0

# Move source 1 to position (100, 100)
> move 1 100 100

# Remove source 0
> remove 0
```

## Architecture

The compositor uses GStreamer's `videomixer` element to combine multiple video streams. Each video source is processed through:

1. **File Source** (`filesrc`) - Reads video file
2. **Decoder** (`decodebin`) - Decodes video/audio streams
3. **Video Processing** (`videoconvert`, `videoscale`, `capsfilter`) - Format conversion and scaling
4. **Clock Sync** (`clocksync`) - Timing synchronization
5. **Mixer Integration** - Connected to `videomixer` for compositing

## Technical Details

- **Output Format**: 1280x720 resolution
- **Source Format**: Automatically scaled to 320x240
- **Video Sink**: Uses `xvimagesink` with fallback to `ximagesink` or `autovideosink`
- **Audio**: Mixed through `audiomixer` (optional)

## Dependencies

- GStreamer 1.0
- GStreamer Video plugins
- GStreamer Audio plugins
- GStreamer Base plugins

## Future Enhancements

- WebRTC live source support
- Video effects and filters
- Recording capabilities
- Network streaming output
- GUI interface 