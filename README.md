# Audio Video Sync Dock plugin for OBS Studio

## Introduction

This is an OBS Studio plugin to measure latency between audio and video.

## How to use

1. Play the video.
   Choose the appropriate video file for your player and OBS Studio configuration.

   | Video file | Video frame rate | Supported display frame rate |
   | ---------- | ----------------:| --------------------- |
   | [sync-pattern-6000.mp4](https://norihiro.github.io/obs-audio-video-sync-dock/sync-pattern-6000.mp4) | 60 FPS    | 30 FPS, 60 FPS, or 120 FPS |
   | [sync-pattern-5994.mp4](https://norihiro.github.io/obs-audio-video-sync-dock/sync-pattern-5994.mp4) | 59.94 FPS | 29.97 FPS, 59.94 FPS, or 119.88 FPS |
   | [sync-pattern-5000.mp4](https://norihiro.github.io/obs-audio-video-sync-dock/sync-pattern-5000.mp4) | 50 FPS    | 25 FPS or 50 FPS (PAL) |
   | [sync-pattern-2400.mp4](https://norihiro.github.io/obs-audio-video-sync-dock/sync-pattern-2400.mp4) | 24 FPS    | 24 FPS or 48 FPS |
   | [sync-pattern-2398.mp4](https://norihiro.github.io/obs-audio-video-sync-dock/sync-pattern-2398.mp4) | 23.98 FPS | 23.98 FPS (24 FPS NTSC) |

   - Choose the video frame rate that is same as player's frame rate or twice of that. For example, if your player (or display) is 60 FPS or 30 FPS such as iPhone, choose 60 FPS. If your player is 59.94 FPS or 29.97 FPS, choose 59.94 FPS.
   - If there are multiple candidates, try to choose the same frame rate as OBS Studio or twice of that.

2. Use your camera to shoot the display playing the video so that the pattern appears on the program of OBS Studio.
3. Open the Audio Video Sync dock and start measuring.
4. Check the latency and adjust it accordingly:
   - Positive latency indicates audio is lagged, video is early.
   - Negative latency indicates audio is early, video is lagged.
   - To adjust the audio latency, increase or decrease the Sync Offset in the Advanced Audio Properties dialog in OBS Studio.
   - To adjust the video latency, you have two options:
     - Add a "Video Delay (Async)" filter to Audio/Video Filters on your video source (recommended if your audio comes from a different device).
     - Add a "Render Delay" filter to Effect Filters on your video source (not recommended).

## Build flow
See [main.yml](.github/workflows/main.yml) for the exact build flow.
