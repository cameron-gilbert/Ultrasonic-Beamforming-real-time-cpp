% mic_welch.m
% Welch power spectral density estimate for a single microphone.
% Analyses a window centred on the middle of the recording to avoid
% any startup transients at the edges.

matFile = 'C:/Users/Cam/AX7010_Work/UltrasonicHost/recordings/test6.mat'; % update as needed
micIndex = 51;  % 1-based mic index (centre of the 102-mic array)
fs       = 48000;

% ── Load ────────────────────────────────────────────────────────────────────
if ~exist(matFile, 'file')
    error('File not found: %s', matFile);
end
d = load(matFile);
data = d.data;  % [numMics x numSamples]

[numMics, numSamples] = size(data);
fprintf('Loaded %d mics x %d samples (%.2f s)\n', numMics, numSamples, numSamples / fs);

if micIndex < 1 || micIndex > numMics
    error('micIndex %d out of range [1, %d]', micIndex, numMics);
end

% ── Extract centre window ───────────────────────────────────────────────────
% Use the middle 25 % of the recording to stay well clear of edges.
windowSamples = round(0.25 * numSamples);
startSample   = round((numSamples - windowSamples) / 2) + 1;
endSample     = startSample + windowSamples - 1;

signal = data(micIndex, startSample:endSample);

fprintf('Mic %d | analysis window: samples %d – %d  (%.3f – %.3f s)\n', ...
    micIndex, startSample, endSample, startSample / fs, endSample / fs);

% ── Welch PSD ───────────────────────────────────────────────────────────────
% Segment length: 2048 samples (~43 ms), 50 % overlap, Hann window.
segLen  = 2048;
overlap = segLen / 2;
[pxx, f] = pwelch(signal, hann(segLen), overlap, segLen, fs);

% ── Plot ────────────────────────────────────────────────────────────────────
figure;
plot(f / 1000, 10 * log10(pxx), 'LineWidth', 1.2);
xlabel('Frequency (kHz)');
ylabel('Power / Frequency (dB/Hz)');
title(sprintf('Welch PSD — Mic %d (centre %.2f s window)', micIndex, windowSamples / fs));
grid on;
xlim([0, fs / 2000]);  % x-axis in kHz up to Nyquist
