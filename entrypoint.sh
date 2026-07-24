#!/bin/bash
# Start virtual X server
Xvfb :0 -screen 0 1280x800x24 -ac +extension GLX &
sleep 1

# Start fluxbox window manager
DISPLAY=:0 fluxbox &
sleep 1

# Start x11vnc (no password)
x11vnc -display :0 -forever -nopw -quiet -listen 0.0.0.0 &
sleep 2

echo "==================================="
echo "VNC server running on port 5900"
echo "Connect with: vncviewer HOST:5901"
echo "Password: none"
echo "==================================="
echo ""
echo "PDFs available at:"
echo "  /home/testuser/test.pdf (Persian)"
echo "  /home/testuser/test_rtl.pdf"
echo ""

# Keep container alive
tail -f /dev/null
