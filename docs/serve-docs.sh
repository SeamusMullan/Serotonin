#!/bin/bash

# Documentation Server Script for Serotonin
# This script serves the generated HTML documentation locally

set -e  # Exit on any error

# Project configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOCS_DIR="$PROJECT_ROOT/docs"
HTML_DIR="$DOCS_DIR/html"
DEFAULT_PORT=8080

echo "=== Serotonin Documentation Server ==="
echo "Project root: $PROJECT_ROOT"
echo "Documentation directory: $HTML_DIR"
echo

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo
    echo "Options:"
    echo "  -p, --port PORT    Set the port number (default: $DEFAULT_PORT)"
    echo "  -o, --open         Open browser automatically"
    echo "  -h, --help         Show this help message"
    echo
    echo "Examples:"
    echo "  $0                 # Serve on default port $DEFAULT_PORT"
    echo "  $0 -p 3000         # Serve on port 3000"
    echo "  $0 -p 8080 -o      # Serve on port 8080 and open browser"
}

# Parse command line arguments
PORT=$DEFAULT_PORT
OPEN_BROWSER=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--port)
            PORT="$2"
            shift 2
            ;;
        -o|--open)
            OPEN_BROWSER=true
            shift
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Validate port number
if ! [[ "$PORT" =~ ^[0-9]+$ ]] || [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
    echo "Error: Invalid port number '$PORT'. Must be between 1-65535."
    exit 1
fi

# Check if documentation exists
if [ ! -d "$HTML_DIR" ]; then
    echo "Error: Documentation directory not found at $HTML_DIR"
    echo "Please generate documentation first by running:"
    echo "  ./generate-docs.sh"
    exit 1
fi

if [ ! -f "$HTML_DIR/index.html" ]; then
    echo "Error: index.html not found in $HTML_DIR"
    echo "Please generate documentation first by running:"
    echo "  ./generate-docs.sh"
    exit 1
fi

# Check if port is already in use
if command -v netstat &> /dev/null; then
    if netstat -tuln 2>/dev/null | grep -q ":$PORT "; then
        echo "Warning: Port $PORT appears to be in use"
        echo "You may need to choose a different port with -p option"
    fi
elif command -v ss &> /dev/null; then
    if ss -tuln 2>/dev/null | grep -q ":$PORT "; then
        echo "Warning: Port $PORT appears to be in use"
        echo "You may need to choose a different port with -p option"
    fi
fi

# Function to open browser
open_browser() {
    local url="http://localhost:$PORT"
    echo "Attempting to open browser..."
    
    if command -v xdg-open &> /dev/null; then
        xdg-open "$url" &>/dev/null &
    elif command -v open &> /dev/null; then
        open "$url" &>/dev/null &
    elif command -v firefox &> /dev/null; then
        firefox "$url" &>/dev/null &
    elif command -v chromium &> /dev/null; then
        chromium "$url" &>/dev/null &
    elif command -v google-chrome &> /dev/null; then
        google-chrome "$url" &>/dev/null &
    else
        echo "Could not detect a browser to open automatically"
        echo "Please open http://localhost:$PORT in your browser"
    fi
}

# Function to handle cleanup on exit
cleanup() {
    echo
    echo "Shutting down documentation server..."
    exit 0
}

# Set up signal handlers
trap cleanup SIGINT SIGTERM

echo "Starting documentation server..."
echo "URL: http://localhost:$PORT"
echo "Document root: $HTML_DIR"
echo

# Change to HTML directory
cd "$HTML_DIR"

# Open browser if requested
if [ "$OPEN_BROWSER" = true ]; then
    # Small delay to ensure server starts before opening browser
    (sleep 2 && open_browser) &
fi

echo "Server starting on port $PORT..."
echo "Press Ctrl+C to stop the server"
echo

# Try different HTTP server options in order of preference
if command -v python3 &> /dev/null; then
    echo "Using Python 3 HTTP server"
    python3 -m http.server "$PORT"
elif command -v python &> /dev/null; then
    # Check if it's Python 2 or 3
    PYTHON_VERSION=$(python --version 2>&1 | grep -oE '[0-9]+\.[0-9]+' | head -1)
    PYTHON_MAJOR=$(echo $PYTHON_VERSION | cut -d. -f1)
    
    if [ "$PYTHON_MAJOR" = "3" ]; then
        echo "Using Python 3 HTTP server"
        python -m http.server "$PORT"
    else
        echo "Using Python 2 HTTP server"
        python -m SimpleHTTPServer "$PORT"
    fi
elif command -v node &> /dev/null; then
    echo "Using Node.js HTTP server"
    # Create a simple Node.js server
    node -e "
    const http = require('http');
    const fs = require('fs');
    const path = require('path');
    const url = require('url');
    
    const mimeTypes = {
        '.html': 'text/html',
        '.css': 'text/css',
        '.js': 'text/javascript',
        '.png': 'image/png',
        '.jpg': 'image/jpeg',
        '.gif': 'image/gif',
        '.svg': 'image/svg+xml'
    };
    
    const server = http.createServer((req, res) => {
        let pathname = url.parse(req.url).pathname;
        if (pathname === '/') pathname = '/index.html';
        
        const filePath = path.join('.', pathname);
        const ext = path.extname(filePath);
        const mimeType = mimeTypes[ext] || 'text/plain';
        
        fs.readFile(filePath, (err, data) => {
            if (err) {
                res.writeHead(404);
                res.end('404 Not Found');
                return;
            }
            res.writeHead(200, { 'Content-Type': mimeType });
            res.end(data);
        });
    });
    
    server.listen($PORT, () => {
        console.log('Server running on port $PORT');
    });
    "
elif command -v php &> /dev/null; then
    echo "Using PHP built-in server"
    php -S "localhost:$PORT"
else
    echo "Error: No suitable HTTP server found!"
    echo "Please install one of the following:"
    echo "  - Python 3: sudo apt-get install python3"
    echo "  - Python 2: sudo apt-get install python"
    echo "  - Node.js: sudo apt-get install nodejs"
    echo "  - PHP: sudo apt-get install php-cli"
    exit 1
fi