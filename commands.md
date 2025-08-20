# Dynamic Video Compositor Commands

## Available Commands

### Source Management
- `add <video_file> <xpos> <ypos>` - Add a new video source
  - Example: `add video.mp4 100 200`
  - Adds video.mp4 at position (100, 200)

- `remove <source_id>` - Remove a video source
  - Example: `remove 2`
  - Removes source with ID 2

- `move <source_id> <xpos> <ypos>` - Move a video source
  - Example: `move 1 300 150`
  - Moves source 1 to position (300, 150)

### Information
- `list` - List all active sources
  - Shows ID, filename, position, and status for each source

- `help` - Show this help information

### Control
- `quit` - Exit the application

## Usage Examples

```bash
# Start compositor with initial sources
./video_compositor video1.mp4 video2.mp4

# Add a third source
> add video3.mp4 640 0

# Move source 1 to new position
> move 1 100 100

# List all sources
> list

# Remove source 0
> remove 0

# Exit
> quit
```

## Notes

- Source IDs are assigned automatically starting from 0
- Positions are in pixels (x, y coordinates)
- Video sources are automatically scaled to 320x240
- Output canvas is 1280x720
- Commands are case-sensitive




