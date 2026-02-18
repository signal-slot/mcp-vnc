# mcp-vnc

An MCP (Model Context Protocol) server that exposes VNC client operations as tools. Allows AI assistants to connect to, view, and control remote desktops over VNC.

## Quick Start (Docker)

```json
{
  "mcpServers": {
    "vnc": {
      "command": "docker",
      "args": ["run", "--rm", "-i", "--network=host", "signalslot/mcp-vnc"]
    }
  }
}
```

## Build from Source

### Dependencies

- CMake 3.16+
- Qt 6 (Core, Network, Widgets, Multimedia)
- [qtvncclient](https://github.com/signal-slot/qtvncclient) (Qt6::VncClient)
- [qtmcp](https://github.com/signal-slot/qtmcp) (Qt6::McpServer)

### Build

```bash
git submodule update --init
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build
```

### Usage

```json
{
  "mcpServers": {
    "vnc": {
      "command": "/path/to/build/mcp-vnc/mcp-vnc"
    }
  }
}
```

### Docker

```bash
docker build -t mcp-vnc .
docker run --rm -i --network=host mcp-vnc
```

## Tools

| Tool | Description |
|------|-------------|
| `connect` | Connect to a VNC server (host, port, password) |
| `disconnect` | Disconnect from the VNC server |
| `screenshot` | Capture the screen (full or region) |
| `save` | Save a screenshot to a file |
| `status` | Get connection status |
| `mouseMove` | Move the mouse cursor |
| `mouseClick` | Click a mouse button (left/middle/right) |
| `doubleClick` | Double-click at a position |
| `mousePress` | Press a mouse button without releasing |
| `mouseRelease` | Release a mouse button |
| `longPress` | Press and hold a mouse button for a duration |
| `dragAndDrop` | Drag from current position to a target |
| `sendKey` | Send an X11 keysym key event |
| `sendText` | Type a string of text |
| `setPreview` | Show/hide the live VNC preview window |
| `setPreviewTitle` | Set the title of the preview window |
| `setInteractive` | Enable/disable forwarding input from the preview window to the VNC server |
| `setStaysOnTop` | Toggle whether the preview window stays on top of other windows |
| `startRecording` | Start recording the VNC screen to an MP4 file |
| `stopRecording` | Stop the current screen recording |

## License

LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
