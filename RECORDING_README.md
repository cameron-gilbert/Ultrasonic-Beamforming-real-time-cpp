# Ultrasonic Recording System - README

## Overview
This recording system captures 10-second ultrasonic array data from the FPGA and saves it in MATLAB-compatible .mat format for offline analysis.

## Recording Workflow

### 1. Setup Physical Test
- Position sound source at known azimuth/elevation angles
- Measure distance from array center
- Verify FPGA is streaming data (check diagnostics every 10 seconds)

### 2. Start Recording in Qt Application
1. Connect to FPGA (click "Connect to FPGA" button)
2. Wait for data stream to stabilize (~5 seconds)
3. Set test parameters in "Data Recording for MATLAB" panel:
   - **Test Number**: Sequential test ID (1, 2, 3, ...)
   - **Azimuth**: Angle in degrees (-180 to +180)
   - **Elevation**: Angle in degrees (-90 to +90)
   - **Save Directory**: Where to save .mat files
4. Click "🔴 Start Recording (10 sec)"
5. Watch progress bar (0% to 100%)
6. Recording automatically stops and saves at 10 seconds

### 3. File Output
Files are saved with format:
```
ultrasonic_test_001_az045_el030_20260216_143052.mat
                │     │     │           │
                │     │     │           └─ Timestamp
                │     │     └─ Elevation angle
                │     └─ Azimuth angle
                └─ Test number
```

## MATLAB Analysis

### Loading Data
```matlab
% Load recording
data = load('ultrasonic_test_001_az045_el030_20260216_143052.mat');

% Access variables
signals = data.data;           % 102 × 480000 matrix
az = data.azimuth;             % e.g., 45.0 degrees
el = data.elevation;           % e.g., 30.0 degrees
fs = data.sample_rate;         % 48000 Hz
num_mics = data.num_mics;      % 102
duration = data.duration;      % 10.0 seconds
timestamp = data.timestamp;    % ISO 8601 timestamp string
```

### Verification Script
Run the included MATLAB verification script:
```matlab
verify_ultrasonic_recording('ultrasonic_test_001_az045_el030_20260216_143052.mat');
```

This will:
- ✅ Check file integrity and dimensions
- ✅ Verify sample counts match expected values
- ✅ Detect dead microphones or signal clipping
- ✅ Plot waveforms, RMS levels, and spectrograms
- ✅ Display metadata

### Basic Analysis Examples

**Plot single microphone:**
```matlab
mic_num = 1;
time = (0:size(data.data, 2)-1) / data.sample_rate;
plot(time, data.data(mic_num, :));
xlabel('Time (s)'); ylabel('Amplitude');
title(sprintf('Microphone %d', mic_num));
```

**Compute spectrogram:**
```matlab
mic_num = 50;
spectrogram(data.data(mic_num, :), 1024, 512, 1024, data.sample_rate, 'yaxis');
title(sprintf('Mic %d Spectrogram (Az=%.1f°, El=%.1f°)', ...
      mic_num, data.azimuth, data.elevation));
```

**Check RMS levels:**
```matlab
rms_levels = sqrt(mean(data.data.^2, 2));
bar(1:102, rms_levels);
xlabel('Microphone'); ylabel('RMS Level');
title('Signal Strength per Microphone');
```

## Recommended Test Sequence

### Phase 1: Verification (Tests 1-4)
Test distances and angles with known geometry:
- Test 001: Az=0°,   El=0°,  R=1.0m
- Test 002: Az=45°,  El=0°,  R=1.0m
- Test 003: Az=90°,  El=0°,  R=1.0m
- Test 004: Az=-45°, El=0°,  R=1.0m

### Phase 2: Distance Comparison (Tests 5-10)
Repeat same angles at 1.5m distance:
- Test 006: Az=0°,   El=0°,  R=1.5m
- Test 007: Az=45°,  El=0°,  R=1.5m
- Test 008: Az=90°,  El=0°,  R=1.5m
- Test 009: Az=-45°, El=0°,  R=1.5m
- Test 010: Az=-90°, El=0°,  R=1.5m

## File Format Specification

### MAT-File Level 5 Format
The .mat files use MATLAB's Level 5 format (binary) which is compatible with:
- MATLAB R12 and later (any modern version)
- Octave 3.0+
- Python (scipy.io.loadmat)
- Julia (MAT.jl package)

### Variables Saved
| Variable | Type | Size | Description |
|----------|------|------|-------------|
| `data` | double | 102 × 480000 | Microphone samples (rows=mics, cols=time) |
| `azimuth` | double | 1 × 1 | Source azimuth angle (degrees) |
| `elevation` | double | 1 × 1 | Source elevation angle (degrees) |
| `sample_rate` | double | 1 × 1 | Sampling rate (Hz) |
| `num_mics` | double | 1 × 1 | Number of microphones |
| `duration` | double | 1 × 1 | Recording duration (seconds) |
| `timestamp` | char | 1 × N | Recording timestamp (ISO 8601) |
| `mic_positions` | double | 102 × 3 | Microphone (x,y,z) positions (optional) |

## Troubleshooting

### Recording fails to start
- ✅ Check FPGA connection (must be streaming data)
- ✅ Verify save directory exists and is writable
- ✅ Check disk space (each file is ~186 MB)

### File size incorrect
- Expected: ~186 MB per 10-second recording
- If smaller: Recording stopped early or data loss
- If larger: Check if multiple tests were merged

### Dead microphones detected
- Some channels consistently show very low signal
- Check FPGA hardware connections
- Verify microphone array physical condition

### Signal clipping
- Amplitude reaches ±1.0 (maximum float range)
- Reduce sound source volume
- Move source farther away

## Support Files
- `verify_ultrasonic_recording.m` - MATLAB verification script
- `microphoneLocations.csv` - Array geometry (102 microphone positions)

## Data Collection Statistics
The Qt application displays real-time diagnostics every 10 seconds:
- Frame Rate: Should be ~93.75 FPS
- Sample Rate: Should be ~48000 Hz per microphone
- 10-Second Estimate: Should be ~186.8 MB

These verify data collection is working correctly before recording.
