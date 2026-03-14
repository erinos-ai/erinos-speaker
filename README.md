# ErinOS Speaker

ESP-IDF firmware for the Waveshare ESP32-S3-AUDIO-Board. A push-to-talk voice client that communicates with the [ErinOS API](https://github.com/erinos-ai/erinos-core) over WiFi.


## Hardware

- ESP32-S3 with 8MB PSRAM
- ES8311 speaker codec
- ES7210 microphone codec (dual mic array)
- 7 WS2812 RGB LEDs
- BOOT button (push-to-talk)


## How It Works

1. **Boot**: Connect to WiFi, initialize audio codecs via I2C. LEDs breathe white.
2. **Press BOOT**: LEDs pulse pink, mic records audio into PSRAM (16kHz, 16-bit, mono, up to 10s).
3. **Release BOOT**: LEDs spin pink. Firmware builds a WAV file, sends it as multipart POST to `/api/chat` on the ErinOS server.
4. **Response**: Server returns WAV audio. Firmware strips the header and plays PCM through the speaker. LEDs turn green.
5. **Done**: LEDs return to idle breathing.


## Setup

Requires [ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html):

```bash
brew install cmake ninja dfu-util
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
source ~/esp/esp-idf/export.sh
```


## Build and Flash

```bash
git clone git@github.com:erinos-ai/erinos-speaker.git
cd erinos-speaker

cp main/config.h.example main/config.h
# Edit main/config.h: set WIFI_SSID, WIFI_PASS, ERINOS_HOST, ERINOS_USER_ID

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodem* flash monitor
```


## Configuration

All settings are in `main/config.h`:

| Setting | Description |
|---------|-------------|
| `WIFI_SSID` | WiFi network name |
| `WIFI_PASS` | WiFi password |
| `ERINOS_HOST` | ErinOS server hostname or IP (e.g., `erinos.local`) |
| `ERINOS_PORT` | API port (default: `4567`) |
| `ERINOS_USER_ID` | Your PIN or Telegram ID |
| `RECORD_SECONDS` | Max recording duration (default: `10`) |

Hardware pin assignments match the Waveshare board schematic and should not need changes.
