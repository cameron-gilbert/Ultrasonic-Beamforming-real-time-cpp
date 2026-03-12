%% Quick Coordinate System Test
% Run this experiment to determine your physical coordinate system

fprintf('\n');
fprintf('╔══════════════════════════════════════════════════════════╗\n');
fprintf('║     COORDINATE SYSTEM DETERMINATION TEST                ║\n');
fprintf('╚══════════════════════════════════════════════════════════╝\n\n');

fprintf('SETUP:\n');
fprintf('  1. Place ultrasonic transmitter 2-3 meters from array\n');
fprintf('  2. Position it directly in front of what you think is 0°\n');
fprintf('  3. Record audio at several azimuth angles\n');
fprintf('  4. The angle with LOUDEST output = actual source direction\n\n');

fprintf('═════════════════════════════════════════════════════════════\n\n');

% Test matrix
test_angles = [0, 45, 90, 135, 180, 225, 270, 315];

fprintf('TEST PROCEDURE:\n\n');
for i = 1:length(test_angles)
    fprintf('Test %d:\n', i);
    fprintf('  Set azimuth = %d°, elevation = 0°\n', test_angles(i));
    fprintf('  Listen to audio output or record RMS level\n');
    fprintf('  Write down perceived loudness: ___________\n\n');
end

fprintf('═════════════════════════════════════════════════════════════\n\n');

fprintf('INTERPRETATION:\n\n');
fprintf('Whichever angle has MAXIMUM audio output = source azimuth\n\n');

fprintf('Example results:\n');
fprintf('  If 0° is loudest   → Source is on +X axis (standard convention) ✓\n');
fprintf('  If 90° is loudest  → Source is on +Y axis\n');
fprintf('  If 180° is loudest → Source is on -X axis\n');
fprintf('  If 270° is loudest → Source is on -Y axis\n\n');

fprintf('═════════════════════════════════════════════════════════════\n\n');

fprintf('COMMON ISSUES:\n\n');
fprintf('Issue: Peak at (actual + 90°)\n');
fprintf('  → X and Y axes are swapped in your code\n');
fprintf('  → Fix: Swap dirX and dirY in BeamformingCalculator.cpp\n\n');

fprintf('Issue: Peak at (360° - actual)\n');
fprintf('  → Y-axis is inverted\n');
fprintf('  → Fix: Use -qSin(azimuthRad) instead of qSin(azimuthRad)\n\n');

fprintf('Issue: Peak at (180° - actual)\n');
fprintf('  → X-axis is inverted\n');
fprintf('  → Fix: Use -qCos(azimuthRad) instead of qCos(azimuthRad)\n\n');

fprintf('═════════════════════════════════════════════════════════════\n\n');

%% If you have recorded data, analyze it here
fprintf('AUTOMATED ANALYSIS (if you have recordings):\n\n');
fprintf('Uncomment and modify the following code:\n\n');
fprintf('%%{\n');
fprintf('%% recordings = {\n');
fprintf('%%     ''recording_az000.mat'', 0;\n');
fprintf('%%     ''recording_az045.mat'', 45;\n');
fprintf('%%     ''recording_az090.mat'', 90;\n');
fprintf('%%     ''recording_az135.mat'', 135;\n');
fprintf('%%     ''recording_az180.mat'', 180;\n');
fprintf('%%     ''recording_az225.mat'', 225;\n');
fprintf('%%     ''recording_az270.mat'', 270;\n');
fprintf('%%     ''recording_az315.mat'', 315\n');
fprintf('%% };\n\n');
fprintf('%% powers = zeros(size(recordings, 1), 1);\n\n');
fprintf('%% for i = 1:size(recordings, 1)\n');
fprintf('%%     data = load(recordings{i, 1});\n');
fprintf('%%     % Calculate power (sum of squares)\n');
fprintf('%%     powers(i) = sum(data.microphone_data(1,:).^2);\n');
fprintf('%%     fprintf(''Az = %%3d°  →  Power = %%.2e\\n'', recordings{i,2}, powers(i));\n');
fprintf('%% end\n\n');
fprintf('%% [max_power, max_idx] = max(powers);\n');
fprintf('%% fprintf(''\\nMAXIMUM at azimuth = %%d°\\n'', recordings{max_idx, 2});\n');
fprintf('%%}\n\n');

fprintf('═════════════════════════════════════════════════════════════\n\n');
