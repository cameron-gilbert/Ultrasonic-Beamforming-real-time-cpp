%% Visualize Coordinate System Convention
% This script helps determine how azimuth angles map to physical directions
clear; clc; close all;

%% Load microphone positions
mic_positions = readmatrix('microphoneLocations.csv', 'NumHeaderLines', 2);
x_mm = mic_positions(:, 2);
y_mm = mic_positions(:, 3);

%% Plot microphone array
figure('Position', [100 100 1400 600]);

% Subplot 1: Top view of array
subplot(1,2,1);
scatter(x_mm, y_mm, 50, 'filled', 'MarkerFaceColor', [0.3 0.5 0.8]);
hold on;

% Add coordinate axes
axis equal;
grid on;
xlabel('X (mm)', 'FontSize', 12, 'FontWeight', 'bold');
ylabel('Y (mm)', 'FontSize', 12, 'FontWeight', 'bold');
title('Microphone Array - Top View', 'FontSize', 14);

% Draw coordinate system arrows
arrow_len = 200;
quiver(0, 0, arrow_len, 0, 0, 'r', 'LineWidth', 3, 'MaxHeadSize', 2);
text(arrow_len+20, 0, 'X-axis (Az=0°)', 'Color', 'r', 'FontSize', 12, 'FontWeight', 'bold');

quiver(0, 0, 0, arrow_len, 0, 'g', 'LineWidth', 3, 'MaxHeadSize', 2);
text(0, arrow_len+20, 'Y-axis (Az=90°)', 'Color', 'g', 'FontSize', 12, 'FontWeight', 'bold');

% Draw several azimuth directions
azimuths = [0, 45, 90, 135, 180, 225, 270, 315];
colors = lines(length(azimuths));

for i = 1:length(azimuths)
    az_deg = azimuths(i);
    az_rad = deg2rad(az_deg);
    
    % Direction vector (elevation = 0 for visualization)
    dirX = cos(az_rad);
    dirY = sin(az_rad);
    
    % Draw arrow
    quiver(0, 0, dirX * arrow_len, dirY * arrow_len, 0, ...
           'Color', colors(i,:), 'LineWidth', 2, 'LineStyle', '--', 'MaxHeadSize', 1.5);
    
    % Label
    label_dist = arrow_len + 30;
    text(dirX * label_dist, dirY * label_dist, sprintf('%d°', az_deg), ...
         'Color', colors(i,:), 'FontSize', 10, 'FontWeight', 'bold', ...
         'HorizontalAlignment', 'center');
end

hold off;
xlim([-250 250]);
ylim([-250 250]);

%% Subplot 2: Unit circle representation
subplot(1,2,2);
hold on;

% Draw unit circle
theta = linspace(0, 2*pi, 100);
plot(cos(theta), sin(theta), 'k-', 'LineWidth', 2);

% Draw azimuth directions
for i = 1:length(azimuths)
    az_deg = azimuths(i);
    az_rad = deg2rad(az_deg);
    
    dirX = cos(az_rad);
    dirY = sin(az_rad);
    
    % Arrow
    quiver(0, 0, dirX, dirY, 0, 'Color', colors(i,:), 'LineWidth', 2, 'MaxHeadSize', 0.3);
    
    % Label
    text(dirX * 1.2, dirY * 1.2, sprintf('%d°', az_deg), ...
         'Color', colors(i,:), 'FontSize', 11, 'FontWeight', 'bold', ...
         'HorizontalAlignment', 'center');
end

% Axes
plot([-1.5 1.5], [0 0], 'k--', 'LineWidth', 0.5);
plot([0 0], [-1.5 1.5], 'k--', 'LineWidth', 0.5);

% Highlight 0° and 90°
text(1.4, -0.15, '0° = +X axis', 'Color', 'r', 'FontSize', 12, 'FontWeight', 'bold');
text(-0.15, 1.4, '90° = +Y axis', 'Color', 'g', 'FontSize', 12, 'FontWeight', 'bold', 'Rotation', 90);

axis equal;
xlim([-1.5 1.5]);
ylim([-1.5 1.5]);
grid on;
xlabel('v_x (cos component)', 'FontSize', 12);
ylabel('v_y (sin component)', 'FontSize', 12);
title('Azimuth Convention (Mathematical)', 'FontSize', 14);
hold off;

%% Print explanation
fprintf('\n========================================\n');
fprintf('  COORDINATE SYSTEM CONVENTION\n');
fprintf('========================================\n\n');

fprintf('Based on your code (BeamformingCalculator.cpp):\n\n');

fprintf('  dirX = cos(elevation) × cos(azimuth)\n');
fprintf('  dirY = cos(elevation) × sin(azimuth)\n\n');

fprintf('This means:\n');
fprintf('  Azimuth   0° → Positive X direction\n');
fprintf('  Azimuth  90° → Positive Y direction\n');
fprintf('  Azimuth 180° → Negative X direction\n');
fprintf('  Azimuth 270° → Negative Y direction\n\n');

fprintf('========================================\n');
fprintf('  TO DETERMINE PHYSICAL ORIENTATION:\n');
fprintf('========================================\n\n');

fprintf('Method 1: Look at your physical setup\n');
fprintf('  - Which edge of the array is labeled "front"?\n');
fprintf('  - Is there a connector or marking?\n\n');

fprintf('Method 2: Experimental test (RECOMMENDED)\n');
fprintf('  1. Place sound source at known position\n');
fprintf('  2. Try azimuth angles: 0°, 90°, 180°, 270°\n');
fprintf('  3. Whichever gives LOUDEST audio = actual direction\n\n');

fprintf('Method 3: Check FPGA/Hardware documentation\n');
fprintf('  - Look for PCB silkscreen labels\n');
fprintf('  - Check schematic or datasheet\n\n');

fprintf('========================================\n\n');

%% Example calculation
fprintf('EXAMPLE CALCULATION:\n\n');
fprintf('If you place source at position (x=3m, y=0m) relative to array center:\n\n');

source_x = 3.0;
source_y = 0.0;
source_az = atan2d(source_y, source_x);
fprintf('  Source position: (%.1f m, %.1f m)\n', source_x, source_y);
fprintf('  Calculated azimuth: %.1f°\n\n', source_az);

fprintf('For beam to point at source, set azimuth = %.1f° in GUI\n', source_az);
fprintf('If this gives LOUD audio → coordinate system is correct ✓\n');
fprintf('If this gives QUIET audio → coordinate axes may be swapped or rotated\n\n');
