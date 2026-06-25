"""Shared mutable state for the ESP32 connection.

Every module that needs to read or write connection state should do:
    import esp.state as state
    ...
    state.sock = new_socket   # not `from esp.state import sock`
This ensures all modules share the same reference.
"""
import threading

# TCP socket and its text wrapper
sock = None
sock_file = None
sock_lock = threading.Lock()

# True while the ESP is already sending / about to send a frame initiated by firmware
# (for example a LiDAR trigger). Hub routes refuse new commands during this window.
incoming_frame_busy: bool = False
ui_busy_counter: int = 0
ui_busy_reason: str = ""

# Last unsolicited boot/config snapshot received from the ESP32.
device_snapshot = None
device_snapshot_counter: int = 0

# Synchronisation primitives used by receiver / frame waiters
state_condition = threading.Condition()
command_lines: list = []     # pending text lines from ESP32
frame_results: list = []     # (counter, result) tuples kept for SSE / polling
frame_counter: int = 0

# Receiver thread
rx_thread = None
rx_stop: bool = False

# Auto-connect thread
auto_connect_thread = None
auto_connect_stop: bool = False

# Flag: next acquire of sock_lock should close+reopen the socket
connection_reset_requested: bool = False

# Mutable target host (can be overridden at runtime via /esp-host)
ESP_HOST: str = ""