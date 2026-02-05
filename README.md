# mcp-vnc

An MCP (Model Context Protocol) server that exposes VNC client operations as tools. Allows AI assistants to connect to, view, and control remote desktops over VNC.

## Dependencies

- CMake 3.16+
- Qt 6 (Core, Network, Widgets)
- [qtvncclient](https://github.com/signal-slot/qtvncclient) (Qt6::VncClient)
- [qtmcp](https://github.com/signal-slot/qtmcp) (Qt6::McpServer)

## Build

```bash
cmake -B build \
  -DQt6VncClient_DIR=/path/to/qtvncclient/build/lib64/cmake/Qt6VncClient \
  -DQt6McpServer_DIR=/path/to/qtmcp/build/lib64/cmake/Qt6McpServer
cmake --build build
```

## Usage

mcp-vnc communicates via stdio using the MCP protocol. Configure it as an MCP server in your client:

```json
{
  "mcpServers": {
    "vnc": {
      "command": "/path/to/mcp-vnc"
    }
  }
}
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
| `dragAndDrop` | Drag from current position to a target |
| `sendKey` | Send an X11 keysym key event |
| `sendText` | Type a string of text |
| `setPreview` | Show/hide the live VNC preview window |
| `setInteractive` | Enable/disable forwarding input from the preview window to the VNC server |

## License

LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
