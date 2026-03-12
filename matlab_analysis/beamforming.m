% beamforming.m
% Beamforming and SSL analysis for UltrasonicHost data
% Place this file in UltrasonicHost/matlab_analysis/

% Parameters
matFile = 'C:/Users/Cam/AX7010_Work/UltrasonicHost/recordings/ultrasonic_test_013_az+000_el+000_20260310_170525.mat'; % Update with actual .mat filename
micPosFile = 'C:/Users/Cam/AX7010_Work/UltrasonicHost/microphoneLocations.csv'; % Update with actual mic positions file
fs = 48000; % Sampling frequency (Hz)
analysisDurationSec = 0.001; % Analyze only a short window for fast SSL
gridRes = 0.01; %making 1681 origninally before concensing to unit circle

% Load data
if exist(matFile, 'file')
    dataStruct = load(matFile);
    data = dataStruct.data; % [numMics x numSamples] — rows = mics, cols = time
else
    error('Data file not found: %s', matFile);
end

% Load microphone positions from CSV.
% CSV format: mic_name, x_mm, y_mm
% The array lies in the xy plane: x+ = right, y+ = up, z+ = forward (pointing direction).
% loadMicPositions returns [numMics x 3] in metres: [x, y, 0].
if exist(micPosFile, 'file')
    micPos = loadMicPositions(micPosFile);
else
    error('Mic position file not found: %s', micPosFile);
end

[numMics, numSamples] = size(data);

analysisSamples = min(round(analysisDurationSec * fs), numSamples);
data = data(:, 1:analysisSamples);
numSamples = analysisSamples;

% Define grid for directions (unit circle in xy plane: x=right, y=up, z=forward)
vxList = -1:gridRes:1;  % left/right
vyList = -1:gridRes:1;  % up/down 
beamPower = zeros(length(vxList), length(vyList));

c = 343; % Speed of sound (m/s)

% Compute worst-case delay across all mics and all steering directions.
% The maximum possible projection of any mic onto any unit vector is the mic's distance from origin.
maxAbsDelay = ceil(max(vecnorm(micPos, 2, 2)) / c * fs); % worst-case sample shift
safeRange = (maxAbsDelay + 1):(numSamples - maxAbsDelay); % valid samples free of circshift wrap-around

fprintf('Loaded %d microphones x %d samples (%.3f s) for SSL analysis.\n', ...
    numMics, numSamples, numSamples / fs);
fprintf('Scanning %d x %d steering grid points...\n', length(vxList), length(vyList));
fprintf('Pre-computed maxAbsDelay = %d samples\n', maxAbsDelay);

% Pre-scale mic positions: [numMics x 3] in samples, matching C++ BeamformerWorker.
% micPos is in metres; micPosScaled(i,:) = micPos(i,:) / c * fs
micPosScaled = micPos / c * fs;

%Step 1 imagine incoming wave 
%Allow T to be the plane wave prop time for that plan
%We are simulating each possible dir value of T
for ix = 1:length(vxList)
    fprintf('Processing row %d / %d\n', ix, length(vxList));
    for iy = 1:length(vyList)
        vx = vxList(ix);
        vy = vyList(iy);
        if vx^2 + vy^2 > 1
            continue; % Outside unit circle
        end
        vz = sqrt(1 - vx^2 - vy^2);  % z = forward component
        dirVec = [vx, vy, vz];
        %Step 2 Project microphone positions onto that direction and convert to samples in one step
        sampleDelaysRounded = round(micPosScaled * dirVec'); % [numMics x 1] integer shifts

        %Step 3 Accumulate sum directly — loop per mic but just slice the row with offset
        beamformed = zeros(1, length(safeRange));
        for mic = 1:numMics
            beamformed = beamformed + data(mic, safeRange - sampleDelaysRounded(mic));
        end

        %Step 4 Find power
        % Total energy in the beamformed waveform for this steering direction.
        beamPower(ix, iy) = sum(beamformed .^ 2);
    end
end

% Plot heatmap
figure;
imagesc(vxList, vyList, beamPower');
axis xy;
xlabel('Vx (right +)');
ylabel('Vy (up +)');
title('Beamforming Power Heatmap With Source to the Left (POV behind the array)');
colorbar;

% Straight ahead = az=0, el=0 -> vx=0, vy=0, vz=1 (already included in grid)



function micPos = loadMicPositions(csvFile)
raw = readcell(csvFile, 'Delimiter', ',');

if isempty(raw)
    error('Microphone position file is empty: %s', csvFile);
end

numRows = size(raw, 1);
coordsMm = zeros(0, 2);

for row = 1:numRows
    if size(raw, 2) >= 3
        xVal = str2double(string(raw{row, 2}));
        yVal = str2double(string(raw{row, 3}));
    elseif size(raw, 2) >= 2
        xVal = str2double(string(raw{row, 1}));
        yVal = str2double(string(raw{row, 2}));
    else
        continue;
    end

    if ~isnan(xVal) && ~isnan(yVal)
        coordsMm(end + 1, :) = [xVal, yVal]; %#ok<AGROW>
    end
end

if isempty(coordsMm)
    error('No numeric microphone coordinates were found in: %s', csvFile);
end

% Convert mm to metres. Array lies in the xy plane (z=0).
% Coordinate system matches BeamformingCalculator: x+ = right, y+ = up, z+ = forward.
micPos = [coordsMm(:, 1) / 1000.0, coordsMm(:, 2) / 1000.0, zeros(size(coordsMm, 1), 1)];
end
