% beamform_recorded_data.m
% Process recorded ultrasonic array data that has delays applied
%
% This script shows how to work with the recorded data. The .mat files
% contain microphone data with BOTH fractional delays (from FPGA) and
% integer delays (from Qt app) already applied. To get beamformed output,
% you just need to SUM the channels!
%
% If integer delays were NOT applied during recording, you can still
% apply them manually using this script.

clear; clc;

%% Load recorded data
filename = 'ultrasonic_recordings/ultrasonic_test_001_az+000_el+000_20260216_143052.mat';
data = load(filename);

fprintf('Loaded: %s\n', filename);
fprintf('Data shape: %d mics × %d samples\n', size(data.microphone_data));
fprintf('Sample rate: %.0f Hz\n', data.sample_rate);
fprintf('Duration: %.2f seconds\n', size(data.microphone_data, 2) / data.sample_rate);
fprintf('Target azimuth: %.1f°, elevation: %.1f°\n', data.azimuth, data.elevation);

%% Check if integer delays were already applied during recording
% If data was recorded with recent version of Qt app, delays are pre-applied
% You can tell by checking if all mics have the same number of samples as
% expected (480,000), or if it's slightly less (delays applied)

num_mics = size(data.microphone_data, 1);
num_samples = size(data.microphone_data, 2);

% If delays were already applied, samples per mic will be less than 480,000
delays_already_applied = (num_samples < 480000);

fprintf('\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n');
if delays_already_applied
    fprintf('✓ Recording has PRE-APPLIED DELAYS (newer Qt version)\n');
    fprintf('  Samples per mic: %d (reduced by max delay)\n', num_samples);
    fprintf('  Simply SUM channels to get beamformed output!\n');
else
    fprintf('⚠ Recording does NOT have integer delays applied\n');
    fprintf('  Samples per mic: %d (full length)\n', num_samples);
    fprintf('  Need to manually apply delays before summing\n');
end
fprintf('━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n');

    % Load microphone geometry
        fs = data.sample_rate;
    c = 343.0;  % Speed of sound (m/s) at 20°C
    
    % Target direction
    azimuth_deg = data.azimuth;
    elevation_deg = data.elevation;
    azimuth = deg2rad(azimuth_deg);
    elevation = deg2rad(elevation_deg);
    
    % Direction vector
    direction = [cos(elevation) * cos(azimuth);
                 cos(elevation) * sin(azimuth);
                 sin(elevation)]ne_data, 1) / num_mics;
    fprintf('✓ Beamformed output ready!\n');
    
    
    if ~isfile(geometry_file)
        error('Microphone geometry file not found: %s', geometry_file);
    end
    
    mic_positions = readmatrix(geometry_file);
    
    if size(mic_positions, 1) ~= 102
        error('Expected 102 microphones, found %d', size(mic_positions, 1));
    end
    
    fprintf('\\nMicrophone array bounds:\\n');
    fprintf('  X: [%.4f, %.4f] m\\n', min(mic_positions(:,1)), max(mic_positions(:,1)));
    fprintf('  Y: [%.4f, %.4f] m\\n', min(mic_positions(:,2)), max(mic_positions(:,2)));
    fprintf('  Z: [%.4f, %.4f] m\\n', min(mic_positions(:,3)), max(mic_positions(:,3)));
    
    % Calculate delays
    delays_seconds = (mic_positions * direction) / c;
    delays_samples = delays_seconds * fs;
    delays_samples = delays_samples - min(delays_samples);
    integer_delays = floor(delays_samples);
    
    fprintf('\\nDelay statistics:\\n');
    fprintf('  Max integer delay: %d samples (%.4f ms)\\n', ...
        max(integer_delays), max(integer_delays)/fs*1000);
    
    % Apply delays
    max_delay = max(integer_delays);
    output_length = num_samples - max_delay;
    beamformed_output = zeros(1, output_length);
    
    for mic = 1:num_mics
        delay = integer_delays(mic);
        beamformed_output = beamformed_output + ...
            data.microphone_data(mic, (1:output_length) + delay);
    end
    
    beamformed_output = beamformed_output / num_mics;
    fprintf('✓ Applied delays and summed!\\n');
endready applied by FPGA hardware)
fprintf('  Applying integer delays and summing...\n');
for mic = 1:num_mics
    delay = integer_delays(mic);
    % Shift this mic's data by the INTEGER delay and add to output
    % Fractional delay was already applied by FPGA interpolation hardware
    beamformed_output = beamformed_output + data.microphone_data(mic, (1:output_length) + delay);
end

% Average (normalize by number of mics)
beamformed_output = beamformed_output / num_mics;

%% Plot comparison
figure('Position', [100 100 1200 800]);

% Plot first 10000 samples for visualization
plot_samples = min(10000, output_length);
time_vec = (0:plot_samples-1) / fs * 1000;  % Time in milliseconds

subplot(3,1,1);
plot(time_vec, data.microphone_data(1, 1:plot_samples));
title('Raw Signal - Microphone 1');
xlabel('Time (ms)');
ylabel('Amplitude');
grid on;

subplot(3,1,2);
plot(time_vec, beamformed_output(1:plot_samples));
title('Beamformed Output - Integer Delays');
xlabel('Time (ms)');
ylabel('Amplitude');
grid on;

subplot(3,1,3);
plot(time_vec, beamformed_output_interp(1:plot_samples));
title('Beamformed Output - Fractional Delays (Interpolated)');
xlabel('Time (ms)');
ylabel('Amplitude');
grid on;

%% Save beamformed output
output_filename = strrep(filename, '.mat', '_beamformed.mat');
save(output_filename, 'beamformed_output', 'fs', 'azimuth_deg', 'elevation_deg', ...
     'integer_delays', 'delays_samples', '-v7.3');
fprintf(2,1,1);
plot(time_vec, data.microphone_data(1, 1:plot_samples));
title('Raw Signal - Microphone 1 (with FPGA fractional delays already applied)');
xlabel('Time (ms)');
ylabel('Amplitude');
grid on;

subplot(2,1,2);
plot(time_vec, beamformed_output(1:plot_samples));
title('Beamformed Output (Integer delays applied in MATLAB
P_beam = abs(Y_beam / output_length);
P_beam = P_beam(1:NFFT/2+1);
P_beam(2:end-1) = 2 * P_beam(2:end-1);

% Beamformed (fractional)
Y_beam_interp = fft(b delays only - fractional already done by FPGA)
Y_beam = fft(beamformed_output, NFFT);
P_beam = abs(Y_beam / output_length);
P_beam = P_beam(1:NFFT/2+1);
P_beam(2:end-1) = 2 * P_beam(2:end-1);

subplot(1,2,1);
hold on;
plot(freq_axis/1000, 20*log10(P_raw), 'DisplayName', 'Raw Mic 1 (FPGA frac delays)');
plot(freq_axis/1000, 20*log10(P_beam), 'DisplayName', 'Beamformed (+ int delays)');
hold off;
xlabel('Frequency (kHz)');
ylabel('Magnitude (dB)');
title('Frequency Spectrum');
legend('Location', 'best');
grid on;
xlim([0 24]);  % Show up to 24 kHz (half of 48 kHz)

subplot(1,2,2);
hold on;
plot(freq_axis/1000, 20*log10(P_raw), 'DisplayName', 'Raw Mic 1 (FPGA frac delays)');
plot(freq_axis/1000, 20*log10(P_beam), 'DisplayName', 'Beamformed (+ int delays)');
hold off;
xlabel('Frequency (kHz)');
ylabel('Magnitude (dB)');
title('Frequency Spectrum (Ultrasonic Band)');
legend('Location', 'best');
grid on;
xlim([10 24]);  % Focus on ultrasonic range
ylim([-80 max(20*log10(P_beam