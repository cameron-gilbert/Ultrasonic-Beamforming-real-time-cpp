% MATLAB Verification Script for Ultrasonic Recording Data
% This script loads and validates .mat files recorded by UltrasonicHost

function verify_ultrasonic_recording(filename)
    % verify_ultrasonic_recording - Load and verify ultrasonic array recording
    %
    % Usage:
    %   verify_ultrasonic_recording('ultrasonic_test_001_az045_el030_20260216_143052.mat')
    
    if nargin < 1
        error('Usage: verify_ultrasonic_recording(filename)');
    end
    
    fprintf('\n====================================\n');
    fprintf('   ULTRASONIC RECORDING VERIFICATION\n');
    fprintf('====================================\n\n');
    
    % Load data
    try
        data_struct = load(filename);
    catch ME
        error('Failed to load file: %s\nError: %s', filename, ME.message);
    end
    
    fprintf('File: %s\n\n', filename);
    
    % Check required variables
    required_vars = {'data', 'azimuth', 'elevation', 'sample_rate', 'num_mics', 'duration', 'timestamp'};
    for i = 1:length(required_vars)
        if ~isfield(data_struct, required_vars{i})
            warning('Missing variable: %s', required_vars{i});
        end
    end
    
    % Extract data
    data = data_struct.data;
    azimuth = data_struct.azimuth;
    elevation = data_struct.elevation;
    sample_rate = data_struct.sample_rate;
    num_mics = data_struct.num_mics;
    duration = data_struct.duration;
    timestamp = data_struct.timestamp;
    
    % Display metadata
    fprintf('METADATA:\n');
    fprintf('  Azimuth:     %.1f°\n', azimuth);
    fprintf('  Elevation:   %.1f°\n', elevation);
    fprintf('  Sample Rate: %.0f Hz\n', sample_rate);
    fprintf('  Microphones: %.0f\n', num_mics);
    fprintf('  Duration:    %.1f seconds\n', duration);
    fprintf('  Timestamp:   %s\n\n', timestamp);
    
    % Verify data dimensions
    [rows, cols] = size(data);
    fprintf('DATA DIMENSIONS:\n');
    fprintf('  Size: %d × %d (mics × samples)\n', rows, cols);
    fprintf('  Expected: 102 × 480000\n');
    
    if rows ~= 102
        warning('Wrong number of microphones! Expected 102, got %d', rows);
    else
        fprintf('  ✓ Microphone count correct\n');
    end
    
    expected_samples = round(duration * sample_rate);
    if abs(cols - expected_samples) > 100
        warning('Sample count mismatch! Expected %d, got %d', expected_samples, cols);
    else
        fprintf('  ✓ Sample count correct\n');
    end
    
    % Signal quality checks
    fprintf('\nSIGNAL QUALITY:\n');
    rms_levels = sqrt(mean(data.^2, 2));  % RMS per mic
    
    fprintf('  RMS Levels: min=%.4f, max=%.4f, mean=%.4f\n', ...
            min(rms_levels), max(rms_levels), mean(rms_levels));
    
    % Check for dead mics (very low signal)
    dead_threshold = 0.001;
    dead_mics = find(rms_levels < dead_threshold);
    if ~isempty(dead_mics)
        warning('Possible dead microphones (RMS < %.4f): %s', ...
                dead_threshold, mat2str(dead_mics));
    else
        fprintf('  ✓ No dead microphones detected\n');
    end
    
    % Check for clipping (values near ±1.0 for float data)
    max_val = max(abs(data(:)));
    fprintf('  Max Absolute Value: %.4f\n', max_val);
    if max_val > 0.95
        warning('Possible signal clipping detected!');
    else
        fprintf('  ✓ No clipping detected\n');
    end
    
    % Plot first second from first microphone
    fprintf('\nPLOTTING:\n');
    figure('Name', sprintf('%s - Verification', filename), 'Position', [100 100 1200 800]);
    
    % Plot 1: First second of Mic 1
    subplot(2,2,1);
    samples_to_plot = min(round(sample_rate), cols);
    time = (0:samples_to_plot-1) / sample_rate;
    plot(time, data(1, 1:samples_to_plot), 'b-', 'LineWidth', 0.5);
    title(sprintf('Mic 1 - First %.1f second', samples_to_plot/sample_rate));
    xlabel('Time (s)');
    ylabel('Amplitude');
    grid on;
    
    % Plot 2: RMS levels per microphone
    subplot(2,2,2);
    bar(1:num_mics, rms_levels);
    title('RMS Level per Microphone');
    xlabel('Microphone Index');
    ylabel('RMS Amplitude');
    grid on;
    ylim([0 max(rms_levels)*1.2]);
    
    % Plot 3: Spectrogram of first microphone
    subplot(2,2,3);
    spectrogram(data(1, :), 1024, 512, 1024, sample_rate, 'yaxis');
    title('Mic 1 Spectrogram');
    colorbar;
    
    % Plot 4: First 50ms from multiple mics
    subplot(2,2,4);
    samples_50ms = min(round(0.05 * sample_rate), cols);
    time_50ms = (0:samples_50ms-1) / sample_rate * 1000; % in ms
    hold on;
    mics_to_plot = [1, 26, 51, 76, 102]; % Sample every ~25 mics
    colors = lines(length(mics_to_plot));
    for i = 1:length(mics_to_plot)
        mic = mics_to_plot(i);
        if mic <= num_mics
            plot(time_50ms, data(mic, 1:samples_50ms) + (i-1)*0.5, ...
                 'Color', colors(i,:), 'DisplayName', sprintf('Mic %d', mic));
        end
    end
    hold off;
    title('First 50ms - Multiple Microphones (offset for clarity)');
    xlabel('Time (ms)');
    ylabel('Amplitude (offset)');
    legend('Location', 'best');
    grid on;
    
    fprintf('  ✓ Plots generated\n');
    
    fprintf('\n====================================\n');
    fprintf('   VERIFICATION COMPLETE\n');
    fprintf('====================================\n\n');
end
