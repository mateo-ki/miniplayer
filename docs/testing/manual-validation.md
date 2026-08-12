# Manual Validation Checklist

## Playback
- [ ] Open a supported local video file (MP4, MKV, MOV, AVI)
- [ ] Confirm audio output starts
- [ ] Confirm video appears in the center pane
- [ ] Pause and resume playback
- [ ] Stop and reopen another file

## Synchronization
- [ ] Observe 30 seconds of playback
- [ ] Confirm no visible A/V drift
- [ ] Seek to 25%, 50%, and 80% of the timeline
- [ ] Confirm playback resumes with audio and video aligned

## UI
- [ ] Verify dark theme and panel hierarchy
- [ ] Verify runtime log shows open/play/pause/stop/seek events
- [ ] Verify media info panel shows codec, resolution, sample rate, etc.
- [ ] Verify current file path is displayed in the top bar and left panel

## Error Handling
- [ ] Attempt to open a non-existent file
- [ ] Attempt to open an unsupported file format
- [ ] Verify error messages appear in the runtime log
