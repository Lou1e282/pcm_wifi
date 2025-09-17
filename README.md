ESP32 PCM Streaming Example

This project demonstrates real-time audio capture via I²S on the ESP32 and streaming PCM data over Wi-Fi to a server using HTTP chunked POST. It uses FreeRTOS tasks, a ring buffer, and the ESP-IDF HTTP client.

-------------------------------------------------------------------------------
Features
-------------------------------------------------------------------------------
- Connects to Wi-Fi as a station (STA)
- Configures I²S standard mode to capture PCM audio (mono, 16-bit, 8 kHz)
- Uses DMA + ring buffer for continuous audio transfer
- Streams data to a server using HTTP chunked encoding
- TLS supported via esp_crt_bundle_attach

-------------------------------------------------------------------------------
Requirements
-------------------------------------------------------------------------------
- ESP32 or ESP32-S3 board (with I²S interface)
- ESP-IDF v5.x (with idf.py)
- External I²S microphone (e.g. INMP441, SPH0645, etc.)
- Wi-Fi network
- HTTP(S) server to receive PCM stream

-------------------------------------------------------------------------------
Configuration
-------------------------------------------------------------------------------
Set Wi-Fi credentials and server URL in menuconfig:

    idf.py menuconfig

- CONFIG_TEST_WIFI_SSID – Wi-Fi SSID
- CONFIG_TEST_WIFI_PASS – Wi-Fi Password
- CONFIG_TEST_SERVER_URL – HTTP(S) endpoint to POST PCM data

-------------------------------------------------------------------------------
Pinout (default)
-------------------------------------------------------------------------------
Update pins if needed inside i2s_init():

Signal | GPIO
-------|-----
BCLK   | 26
WS     | 42
DIN    | 21
MCLK   | NC
DOUT   | NC

-------------------------------------------------------------------------------
Build & Flash
-------------------------------------------------------------------------------
    idf.py build flash monitor

-------------------------------------------------------------------------------
Workflow
-------------------------------------------------------------------------------
1. Wi-Fi Init  
   Connects ESP32 to configured Wi-Fi and waits for IP.

2. I²S Init  
   Configures I²S as master receiver, 8 kHz, mono, 16-bit.

3. I²S RX Task  
   Reads DMA buffers from I²S and pushes data into a FreeRTOS ring buffer.

4. HTTP POST Task  
   Opens a chunked POST stream to the server, continuously uploads PCM data.

-------------------------------------------------------------------------------
Example Server (Python Flask)
-------------------------------------------------------------------------------
from flask import Flask, request

app = Flask(__name__)

@app.route("/upload", methods=["POST"])
def upload():
    with open("audio.pcm", "ab") as f:
        f.write(request.data)
    return "OK"

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8000)

Then set CONFIG_TEST_SERVER_URL to http://<server-ip>:8000/upload.

-------------------------------------------------------------------------------
Notes
-------------------------------------------------------------------------------
- PCM format: 16-bit signed, mono, 8 kHz
- Each I²S DMA buffer = 1024 bytes
- Ring buffer size = 16 KB
- If network stalls, old PCM chunks may be dropped
- For testing without server, enable the mock_send_task to just log bytes

-------------------------------------------------------------------------------
License
-------------------------------------------------------------------------------
MIT License. Use at your own risk.
