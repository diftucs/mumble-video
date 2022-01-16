# mumble-video
Very, very experimental screensharing plugin in Mumble.

## Usage
Run the RTMP server (needs `docker` and `docker-compose`):
```
cd rtmp-server
docker-compose up -d
```

Then open two clients on the same machine as the RTMP server with the plugin loaded and enabled with keyevents. Connect them to the same server and press `0` in one of them. Check stdout for FFmpeg output.

## Compilation
```
mkdir build
cd build
cmake ..
make
```
