// Copyright (C) 2025 Signal Slot Inc.
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifdef QT_STATIC
#include <QtPlugin>
Q_IMPORT_PLUGIN(QMcpServerStdioPlugin)
#endif

#include <QtWidgets/QApplication>
#include <QtMcpServer/QMcpServer>
#include <QtMcpServer/QMcpServerSession>
#include <QtMcpCommon/QMcpPrompt>
#include <QtMcpCommon/QMcpPromptMessage>
#include <QtMcpCommon/QMcpTextContent>
#include "tools.h"
#include "vncwidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MCP VNC Server");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("Signal Slot Inc.");
    app.setOrganizationDomain("signal-slot.co.jp");
    app.setQuitOnLastWindowClosed(false);

    QMcpServer server("stdio"_L1);
    QObject::connect(&server, &QMcpServer::finished, &app, &QCoreApplication::quit);
    auto *tools = new Tools(&server);
    server.registerToolSet(tools, {
        { "connect", "Connect to a VNC server. Must be called before any other tool. Establishes a TCP connection and performs VNC handshake. Use status() to verify the connection succeeded." },
        { "connect/host", "Hostname or IP address of the VNC server (e.g., \"localhost\", \"192.168.1.100\")" },
        { "connect/port", "Port number of the VNC server (default: 5900). Standard VNC ports are 5900+N where N is the display number." },
        { "connect/password", "Password for VNC authentication (optional). Required only if the VNC server has password authentication enabled." },
        { "disconnect", "Disconnect from the VNC server. Closes the TCP connection. Safe to call even if not connected." },
        { "screenshot", "Capture the current VNC screen and return as a base64-encoded image. Call with no arguments to capture the full screen, or specify a region with x/y/width/height. Always take a screenshot after performing actions to verify the result. Returns an error message if not connected or the framebuffer is unavailable." },
        { "screenshot/x", "X coordinate of the top-left corner of the capture region in pixels (default: 0)" },
        { "screenshot/y", "Y coordinate of the top-left corner of the capture region in pixels (default: 0)" },
        { "screenshot/width", "Width of the capture region in pixels (default: -1 for full width from x to the right edge)" },
        { "screenshot/height", "Height of the capture region in pixels (default: -1 for full height from y to the bottom edge)" },
        { "save", "Save the current VNC screen to an image file on disk. The image format is determined by the file extension (e.g., .png, .jpg, .bmp). Returns \"true\" on success or \"false\" on failure. Useful for archiving screenshots or when a file path is needed rather than inline image data." },
        { "save/filePath", "Absolute file path to save the screenshot (e.g., /tmp/screenshot.png). The directory must exist. Supported formats: PNG, JPG, BMP, and other Qt-supported image formats." },
        { "save/x", "X coordinate of the top-left corner of the capture region in pixels (default: 0)" },
        { "save/y", "Y coordinate of the top-left corner of the capture region in pixels (default: 0)" },
        { "save/width", "Width of the capture region in pixels (default: -1 for full width from x to the right edge)" },
        { "save/height", "Height of the capture region in pixels (default: -1 for full height from y to the bottom edge)" },
        { "status", "Get the current VNC connection status. Returns \"connected to <host>:<port> (<width>x<height>)\" when connected (including the framebuffer resolution), or \"disconnected\" when not connected. Use this after connect() to verify the connection and to learn the screen dimensions." },
        { "mouseMove", "Move the mouse cursor to the specified position. Also updates the internal cursor position used as the starting point for dragAndDrop. Set the button parameter to simulate dragging while moving." },
        { "mouseMove/x", "Target X coordinate in pixels (0 = left edge of screen)" },
        { "mouseMove/y", "Target Y coordinate in pixels (0 = top edge of screen)" },
        { "mouseMove/button", "Mouse button held during the move for drag simulation (0=none, 1=left, 2=middle, 3=right, default: 0). Use 0 for a simple cursor move." },
        { "mouseClick", "Perform a single mouse click (press and release) at the specified position. This is the standard way to click buttons, links, and UI elements. Sends a button-press immediately followed by a button-release." },
        { "mouseClick/x", "X coordinate to click at in pixels" },
        { "mouseClick/y", "Y coordinate to click at in pixels" },
        { "mouseClick/button", "Mouse button to click (1=left, 2=middle, 3=right, default: 1). Use 1 for normal clicks, 3 for right-click context menus." },
        { "doubleClick", "Perform a double-click at the specified position. Sends the full sequence: press, release, double-click, release. Useful for opening files, selecting words in text editors, or any UI action that requires a double-click." },
        { "doubleClick/x", "X coordinate to double-click at in pixels" },
        { "doubleClick/y", "Y coordinate to double-click at in pixels" },
        { "doubleClick/button", "Mouse button to double-click (1=left, 2=middle, 3=right, default: 1)" },
        { "mousePress", "Press and hold a mouse button at the specified position without releasing it. Use this as the first step of manual drag operations. Pair with mouseRelease to complete the action. For simple drag-and-drop, prefer the dragAndDrop tool instead." },
        { "mousePress/x", "X coordinate to press at in pixels" },
        { "mousePress/y", "Y coordinate to press at in pixels" },
        { "mousePress/button", "Mouse button to press (1=left, 2=middle, 3=right, default: 1)" },
        { "mouseRelease", "Release a previously pressed mouse button at the specified position. Use this after mousePress to complete a manual drag or hold operation." },
        { "mouseRelease/x", "X coordinate to release at in pixels" },
        { "mouseRelease/y", "Y coordinate to release at in pixels" },
        { "mouseRelease/button", "Mouse button to release (1=left, 2=middle, 3=right, default: 1). Must match the button used in mousePress." },
        { "longPress", "Press and hold a mouse button at a position for a specified duration, then automatically release. Useful for triggering long-press context menus, touch-and-hold actions, or tooltip displays." },
        { "longPress/x", "X coordinate to long-press at in pixels" },
        { "longPress/y", "Y coordinate to long-press at in pixels" },
        { "longPress/duration", "How long to hold the button in milliseconds before releasing (default: 1000, i.e., 1 second)" },
        { "longPress/button", "Mouse button to long-press (1=left, 2=middle, 3=right, default: 1)" },
        { "dragAndDrop", "Drag from the current mouse position and drop at the specified position. IMPORTANT: You must call mouseMove first to position the cursor at the drag start point. The sequence is: press at current position → move to target → release at target. The internal cursor position is updated to the drop target after completion." },
        { "dragAndDrop/x", "X coordinate of the drop target in pixels" },
        { "dragAndDrop/y", "Y coordinate of the drop target in pixels" },
        { "dragAndDrop/button", "Mouse button to use for dragging (1=left, 2=middle, 3=right, default: 1)" },
        { "sendKey", "Send a single key press or release event using an X11 keysym code. For typing text, prefer sendText instead. You must send both a press (down=true) and release (down=false) for a complete keystroke. Common keysyms: Return=0xff0d, Escape=0xff1b, BackSpace=0xff08, Tab=0xff09, space=0x0020, Left=0xff51, Up=0xff52, Right=0xff53, Down=0xff54, Home=0xff50, End=0xff57, Page_Up=0xff55, Page_Down=0xff56, Insert=0xff63, Delete=0xffff, F1=0xffbe..F12=0xffc9, Shift_L=0xffe1, Control_L=0xffe3, Alt_L=0xffe9, Super_L=0xffeb, a-z=0x0061-0x007a, A-Z=0x0041-0x005a, 0-9=0x0030-0x0039." },
        { "sendKey/keysym", "X11 keysym value identifying the key. See tool description for common keysym values." },
        { "sendKey/down", "true to press the key down, false to release it. Send both press and release for a complete keystroke. For modifier combinations (e.g., Ctrl+C), press the modifier first, press the key, release the key, then release the modifier." },
        { "sendText", "Type a string of text by sending individual key press and release events for each character. This is the simplest way to enter text into input fields, editors, or terminals. For special keys (Enter, Backspace, arrow keys, etc.) or modifier combinations (Ctrl+C, Alt+Tab), use sendKey instead." },
        { "sendText/text", "The text string to type. Each character is sent as a separate key press/release pair. Supports Unicode characters." },
        { "setPreview", "Show or hide a live preview window that displays the VNC screen in real-time. The preview window is hidden by default. When visible, the screen is continuously updated. Useful for monitoring what's happening on the remote screen." },
        { "setPreview/visible", "true to show the preview window, false to hide it" },
        { "setInteractive", "Enable or disable interactive mode on the preview window. When enabled, mouse clicks and keyboard input on the preview window are forwarded to the VNC server, allowing direct manual interaction. When disabled (default), the preview is view-only. The preview window must be visible (setPreview) for this to have any effect." },
        { "setInteractive/enabled", "true to enable interactive mode (input forwarded to VNC), false for view-only mode" },
        { "setStaysOnTop", "Toggle whether the preview window stays on top of all other windows. Useful for keeping the VNC view visible while working in other applications." },
        { "setStaysOnTop/enabled", "true to keep the preview window always on top, false to allow normal window stacking" },
        { "setPreviewTitle", "Set a custom title for the preview window's title bar. Useful for identifying which VNC session is being displayed when working with multiple connections." },
        { "setPreviewTitle/title", "The title text to display in the preview window's title bar" },
#ifdef HAVE_MULTIMEDIA
        { "startRecording", "Start recording the VNC screen to an H.264/MP4 video file. The recording captures frames at the specified FPS rate until stopRecording is called. Requires an active VNC connection with a valid framebuffer. Returns false if already recording, not connected, or no framebuffer is available." },
        { "startRecording/filePath", "Absolute file path for the output MP4 file (e.g., /tmp/recording.mp4). The directory must exist. The file will be overwritten if it already exists." },
        { "startRecording/fps", "Frames per second for the recording (default: 10, range: 1-60). Higher values produce smoother video but larger files. 10 FPS is usually sufficient for UI interaction recordings." },
        { "stopRecording", "Stop the current screen recording and finalize the MP4 file. The video file is written and closed when this is called. Returns false if no recording is in progress." },
#endif
    });
    QObject::connect(&server, &QMcpServer::newSession, [](QMcpServerSession *session) {
        // setup-qt prompt
        {
            QMcpPrompt prompt;
            prompt.setName("setup-qt"_L1);
            prompt.setDescription("How to run a Qt application as a VNC server for use with mcp-vnc"_L1);
            QMcpPromptMessage message;
            message.setRole(QMcpRole::user);
            message.setContent(QMcpTextContent(
                "# Running a Qt Application as a VNC Server\n"
                "\n"
                "## Basic Setup\n"
                "\n"
                "Qt has a built-in VNC platform plugin (QPA) that turns any Qt application into a VNC server.\n"
                "\n"
                "```bash\n"
                "# Run with VNC QPA on default port 5900\n"
                "./your-qt-app -platform vnc\n"
                "\n"
                "# Specify a custom port (e.g., display :1 = port 5901)\n"
                "./your-qt-app -platform vnc:port=5901\n"
                "\n"
                "# Specify screen size\n"
                "./your-qt-app -platform vnc:size=1280x720\n"
                "\n"
                "# Combine options\n"
                "./your-qt-app -platform vnc:size=1280x720:port=5901\n"
                "```\n"
                "\n"
                "## OpenGL Support with vncgl\n"
                "\n"
                "If the application uses OpenGL (Qt Quick, Qt 3D, etc.), the standard `vnc` QPA may not render correctly.\n"
                "Use the `vncgl` platform plugin if available, which provides proper OpenGL support:\n"
                "\n"
                "```bash\n"
                "./your-qt-app -platform vncgl\n"
                "```\n"
                "\n"
                "The vncgl plugin is available at: https://github.com/signal-slot/qtvncglplugin\n"
                "\n"
                "## Connecting with mcp-vnc\n"
                "\n"
                "Once the Qt application is running as a VNC server, connect using mcp-vnc tools:\n"
                "\n"
                "1. `connect(host=\"localhost\", port=<port>)` — Connect to the VNC server (default port is 5900)\n"
                "2. `status()` — Verify connection and check screen dimensions\n"
                "3. `screenshot()` — Capture the current screen\n"
                "4. Use mouse/keyboard tools to interact with the application\n"_L1
            ));
            session->appendPrompt(prompt, message);
        }

        // setup-slint prompt
        {
            QMcpPrompt prompt;
            prompt.setName("setup-slint"_L1);
            prompt.setDescription("How to run a Slint application as a VNC server for use with mcp-vnc"_L1);
            QMcpPromptMessage message;
            message.setRole(QMcpRole::user);
            message.setContent(QMcpTextContent(
                "# Running a Slint Application as a VNC Server\n"
                "\n"
                "Slint does not have a built-in VNC backend yet. Apply the VNC backend patch from\n"
                "https://github.com/slint-ui/slint/commit/4f8c1064a730425b6308d8c3357aedee5c623ab2\n"
                "to add one.\n"
                "\n"
                "## Setup\n"
                "\n"
                "```bash\n"
                "# Apply the VNC backend patch to your Slint source tree\n"
                "cd /path/to/slint\n"
                "git cherry-pick 4f8c1064a730425b6308d8c3357aedee5c623ab2\n"
                "```\n"
                "\n"
                "Then build your application with the `backend-vnc` feature:\n"
                "\n"
                "```bash\n"
                "cargo run --features slint/backend-vnc\n"
                "```\n"
                "\n"
                "## Running\n"
                "\n"
                "```bash\n"
                "# Run with VNC backend on default port 5900\n"
                "SLINT_BACKEND=vnc ./your-slint-app\n"
                "\n"
                "# Specify display number (port = 5900 + N)\n"
                "SLINT_BACKEND=vnc SLINT_VNC_DISPLAY=:1 ./your-slint-app\n"
                "\n"
                "# Or specify port directly\n"
                "SLINT_BACKEND=vnc SLINT_VNC_PORT=5901 ./your-slint-app\n"
                "\n"
                "# Specify screen size\n"
                "SLINT_BACKEND=vnc SLINT_VNC_SIZE=1280x720 ./your-slint-app\n"
                "```\n"
                "\n"
                "## Alternative: Qt Backend\n"
                "\n"
                "If Slint is built with the Qt backend, you can use Qt's VNC QPA without the patch:\n"
                "\n"
                "```bash\n"
                "SLINT_BACKEND=qt ./your-slint-app -platform vnc\n"
                "SLINT_BACKEND=qt ./your-slint-app -platform vnc:size=1280x720:port=5901\n"
                "```\n"
                "\n"
                "## Connecting with mcp-vnc\n"
                "\n"
                "Once the Slint application is running with VNC access, connect using mcp-vnc tools:\n"
                "\n"
                "1. `connect(host=\"localhost\", port=<port>)` — Connect to the VNC server (default port is 5900)\n"
                "2. `status()` — Verify connection and check screen dimensions\n"
                "3. `screenshot()` — Capture the current screen\n"
                "4. Use mouse/keyboard tools to interact with the application\n"_L1
            ));
            session->appendPrompt(prompt, message);
        }

        // setup-embedded prompt
        {
            QMcpPrompt prompt;
            prompt.setName("setup-embedded"_L1);
            prompt.setDescription("How to expose an embedded device's display via VNC using kmsvnc for use with mcp-vnc"_L1);
            QMcpPromptMessage message;
            message.setRole(QMcpRole::user);
            message.setContent(QMcpTextContent(
                "# Exposing an Embedded Device's Display via VNC\n"
                "\n"
                "## kmsvnc\n"
                "\n"
                "kmsvnc captures the KMS/DRM framebuffer and exposes it as a VNC server.\n"
                "This works with any application rendering to the Linux framebuffer — Qt EGLFS, Weston, Slint LinuxKMS, etc.\n"
                "\n"
                "GitHub: https://github.com/signal-slot/kmsvnc\n"
                "\n"
                "### Installation\n"
                "\n"
                "```bash\n"
                "cargo install kmsvnc\n"
                "```\n"
                "\n"
                "### Usage\n"
                "\n"
                "```bash\n"
                "# Start the VNC server (auto-detects the display device)\n"
                "sudo $(which kmsvnc)\n"
                "\n"
                "# Or grant capabilities instead of running as root\n"
                "sudo setcap cap_sys_admin+ep $(which kmsvnc)\n"
                "sudo usermod -aG input $USER\n"
                "kmsvnc\n"
                "\n"
                "# Specify a custom port\n"
                "kmsvnc --port 5901\n"
                "\n"
                "# Specify a device\n"
                "kmsvnc --device /dev/dri/card0\n"
                "```\n"
                "\n"
                "kmsvnc requires access to the DRM device (`/dev/dri/card*`) or framebuffer (`/dev/fb*`).\n"
                "\n"
                "### Typical Workflow for Embedded Devices\n"
                "\n"
                "```bash\n"
                "# Terminal 1: Run your application on the framebuffer\n"
                "./your-app -platform eglfs  # Qt app example\n"
                "\n"
                "# Terminal 2: Expose the display via VNC\n"
                "kmsvnc\n"
                "```\n"
                "\n"
                "## Connecting with mcp-vnc\n"
                "\n"
                "Once kmsvnc is running on the target device, connect using mcp-vnc tools:\n"
                "\n"
                "1. `connect(host=\"<device-ip>\", port=<port>)` — Connect to the device (default port is 5900)\n"
                "2. `status()` — Verify connection and check screen dimensions\n"
                "3. `screenshot()` — Capture the current screen\n"
                "4. Use mouse/keyboard tools to interact with the application\n"_L1
            ));
            session->appendPrompt(prompt, message);
        }
    });

    server.start();

    VncWidget vncWidget;
    vncWidget.setClient(tools->client());
    vncWidget.setWindowTitle(app.applicationName());
    tools->setPreviewWidget(&vncWidget);

    return app.exec();
}
