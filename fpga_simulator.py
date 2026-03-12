#!/usr/bin/env python3
"""
FPGA Simulator - Sends simulated ultrasonic microphone data to UltrasonicHost

This script acts as a simulated FPGA, generating and sending packets that match
the MicrophonePacket format expected by the UltrasonicHost application.

Packet Structure (1094 bytes total):
- Header (70 bytes):
  - micIndex (2 bytes, uint16, little-endian): Microphone index (0-101)
  - frameNumber (4 bytes, uint32, little-endian): Frame number
  - reserved (64 bytes): Reserved/padding bytes (zeros)
- Data (1024 bytes):
  - 512 samples × 2 bytes (int16, little-endian)

Usage:
    python fpga_simulator.py [--host HOST] [--port PORT] [--rate RATE] [--signal SIGNAL]

Arguments:
    --host HOST       Host to connect to (default: 127.0.0.1)
    --port PORT       Port to connect to (default: 5000)
    --rate RATE       Packets per second (default: 100, max ~14000 for real-time)
    --signal SIGNAL   Signal type: sine, chirp, noise, or pulse (default: sine)
    --freq FREQ       Base frequency for sine wave in Hz (default: 40000)
    --amplitude AMP   Signal amplitude (default: 12000)
"""

import socket
import struct
import time
import math
import random
import argparse
from typing import List

class FPGASimulator:
    NUM_MICS = 102
    SAMPLES_PER_PACKET = 512
    HEADER_BYTES = 70
    SAMPLE_SIZE = 2  # int16
    DATA_BYTES = SAMPLES_PER_PACKET * SAMPLE_SIZE
    TOTAL_PACKET_SIZE = HEADER_BYTES + DATA_BYTES
    
    # Realistic FPGA parameters
    SAMPLING_RATE = 250000  # 250 kHz sampling rate
    
    def __init__(self, host: str = "127.0.0.1", port: int = 5000, 
                 packets_per_sec: int = 20, signal_type: str = "sine",
                 base_freq: float = 1000, amplitude: float = 8000):
        self.host = host
        self.port = port
        self.packets_per_sec = packets_per_sec
        self.signal_type = signal_type
        self.base_freq = base_freq
        self.amplitude = amplitude
        
        self.frame_number = 0
        self.mic_index = 0
        self.socket = None
        self.connected = False
        
        # For tracking performance
        self.packets_sent = 0
        self.bytes_sent = 0
        self.start_time = None
        
    def connect(self) -> bool:
        """Connect to the UltrasonicHost application"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            # Set socket options for better performance
            self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 512 * 1024)
            
            print(f"Connecting to {self.host}:{self.port}...")
            self.socket.connect((self.host, self.port))
            self.connected = True
            print(f"✓ Connected to UltrasonicHost at {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"✗ Connection failed: {e}")
            return False
    
    def disconnect(self):
        """Close the connection"""
        if self.socket:
            self.socket.close()
            self.connected = False
            print("Disconnected from UltrasonicHost")
    
    def generate_samples(self, mic_index: int, frame_number: int) -> List[int]:
        """Generate 512 samples matching FPGA test pattern - simplified for speed"""
        # Simple sine wave: 5 complete cycles over 512 samples
        # Phase offset per mic for spatial diversity
        freq = 5.0
        amp = 12000.0
        phase = (mic_index / self.NUM_MICS) * 2.0 * math.pi
        two_pi = 2.0 * math.pi
        
        samples = []
        for i in range(self.SAMPLES_PER_PACKET):
            t = i / self.SAMPLES_PER_PACKET
            value = amp * math.sin(two_pi * freq * t + phase)
            samples.append(int(value))
        
        return samples
    
    def create_packet(self, mic_index: int, frame_number: int) -> bytes:
        """Create a complete packet matching FPGA format (BIG-ENDIAN)"""
        packet = bytearray(self.TOTAL_PACKET_SIZE)
        
        # Header (70 bytes) - BIG-ENDIAN to match FPGA
        # frameNumber (4 bytes, uint32, big-endian)
        struct.pack_into('>I', packet, 0, frame_number)
        
        # micIndex (2 bytes, uint16, big-endian)
        struct.pack_into('>H', packet, 4, mic_index)
        
        # reserved (64 bytes) - already zero-initialized
        
        # Data (1024 bytes = 512 samples × 2 bytes, big-endian)
        samples = self.generate_samples(mic_index, frame_number)
        
        data_offset = self.HEADER_BYTES
        for i, sample in enumerate(samples):
            # Clamp to int16 range and pack as big-endian
            sample = max(-32768, min(32767, sample))
            struct.pack_into('>h', packet, data_offset + i * self.SAMPLE_SIZE, sample)
        
        return bytes(packet)
    
    def send_packet(self) -> bool:
        """Send one packet to the host"""
        if not self.connected:
            return False
        
        try:
            packet = self.create_packet(self.mic_index, self.frame_number)
            self.socket.sendall(packet)
            
            self.packets_sent += 1
            self.bytes_sent += len(packet)
            
            # Advance to next mic
            self.mic_index += 1
            if self.mic_index >= self.NUM_MICS:
                self.mic_index = 0
                self.frame_number += 1
            
            return True
        except Exception as e:
            print(f"Error sending packet: {e}")
            self.connected = False
            return False
    
    def print_stats(self):
        """Print performance statistics"""
        if self.start_time:
            elapsed = time.time() - self.start_time
            if elapsed > 0:
                pkt_rate = self.packets_sent / elapsed
                mbps = (self.bytes_sent * 8 / 1_000_000) / elapsed
                frames_sent = self.packets_sent / self.NUM_MICS
                fps = frames_sent / elapsed
                
                print(f"\n=== Stats @ {elapsed:.1f}s ===")
                print(f"Packets: {self.packets_sent} ({pkt_rate:.1f} pkt/s)")
                print(f"Frames: {frames_sent:.0f} (~{fps:.1f} fps)")
                print(f"Throughput: {mbps:.2f} Mbps")
                print(f"Current frame: {self.frame_number}")
    
    def run(self):
        """Main simulation loop - sends complete frames at 100 FPS"""
        if not self.connect():
            return
        
        frame_period = 0.010  # 10ms per frame = 100 FPS (matches FPGA)
        self.start_time = time.time()
        last_stats_time = self.start_time
        next_frame_time = self.start_time
        
        print(f"\nStarting simulation:")
        print(f"  Signal: 5 Hz sine wave with phase diversity")
        print(f"  Rate: 100 FPS (all 102 mics per frame)")
        print(f"  Frame period: {frame_period*1000:.1f} ms")
        print(f"  Packet size: {self.TOTAL_PACKET_SIZE} bytes")
        print(f"  Frame size: {self.TOTAL_PACKET_SIZE * self.NUM_MICS} bytes")
        print(f"  Bandwidth: {(self.TOTAL_PACKET_SIZE * self.NUM_MICS * 100 * 8 / 1_000_000):.2f} Mbps")
        print(f"\nPress Ctrl+C to stop...\n")
        
        try:
            while self.connected:
                # Send all 102 mics in burst
                for mic in range(self.NUM_MICS):
                    packet = self.create_packet(mic, self.frame_number)
                    self.socket.sendall(packet)
                    self.packets_sent += 1
                    self.bytes_sent += len(packet)
                
                self.frame_number += 1
                
                # Print stats every 5 seconds
                current_time = time.time()
                if current_time - last_stats_time >= 5.0:
                    self.print_stats()
                    last_stats_time = current_time
                
                # Sleep to maintain 100 FPS frame rate
                next_frame_time += frame_period
                sleep_time = next_frame_time - time.time()
                if sleep_time > 0:
                    time.sleep(sleep_time)
                else:
                    # Fell behind, reset timing
                    next_frame_time = time.time()
        
        except KeyboardInterrupt:
            print("\n\nStopping simulation...")
        finally:
            self.print_stats()
            self.disconnect()

def main():
    parser = argparse.ArgumentParser(
        description="FPGA Simulator for UltrasonicHost",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic usage with default settings
  python fpga_simulator.py
  
  # High-speed simulation (realistic FPGA rate ~14kHz = ~137 fps)
  python fpga_simulator.py --rate 14000
  
  # Chirp signal for beamforming testing
  python fpga_simulator.py --signal chirp
  
  # Pulse signal with spatial delay
  python fpga_simulator.py --signal pulse --rate 10000
  
  # Connect to remote host
  python fpga_simulator.py --host 192.168.1.10 --port 5000
        """)
    
    parser.add_argument('--host', type=str, default='127.0.0.1',
                        help='Host to connect to (default: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=5000,
                        help='Port to connect to (default: 5000)')
    parser.add_argument('--rate', type=int, default=100,
                        help='Packets per second (default: 100)')
    parser.add_argument('--signal', type=str, default='sine',
                        choices=['sine', 'chirp', 'noise', 'pulse'],
                        help='Signal type (default: sine)')
    parser.add_argument('--freq', type=float, default=40000,
                        help='Base frequency for sine wave in Hz (default: 40000)')
    parser.add_argument('--amplitude', type=float, default=12000,
                        help='Signal amplitude (default: 12000)')
    
    args = parser.parse_args()
    
    simulator = FPGASimulator(
        host=args.host,
        port=args.port,
        packets_per_sec=args.rate,
        signal_type=args.signal,
        base_freq=args.freq,
        amplitude=args.amplitude
    )
    
    simulator.run()

if __name__ == "__main__":
    main()
