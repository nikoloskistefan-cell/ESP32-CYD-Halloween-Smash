# Halloween Smash! – ESP32 CYD Whack-a-Mole Game

A Halloween-themed whack-a-mole style arcade game for the ESP32 CYD (Cheap Yellow Display). Tap the pumpkins and ghosts as they appear, build your score, and try to beat the high score.

## Features

- 320×240 ILI9341 color display
- XPT2046 touchscreen controls
- Halloween pumpkin and ghost targets
- Score and high-score tracking
- Spooky buzzer sound effects
- Original Arduino sketch included

## Hardware

- ESP32-2432S028 / ESP32 CYD
- 2.8-inch 320×240 ILI9341 display
- XPT2046 resistive touchscreen
- On-board buzzer

## Arduino setup

Open `HALLOWEEN_SMASH_.ino` in the Arduino IDE and select **ESP32 Dev Module** (or the matching CYD board profile).

Required libraries:

- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit ILI9341](https://github.com/adafruit/Adafruit_ILI9341)
- [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen)

`SPI` and `Preferences` are provided by the ESP32 Arduino core.

## Display used in the video

👉 [ESP32 touchscreen item link](https://temu.to/k/ea65oy4oun8)

Disclosure: this link may be an affiliate link. It does not change the price for you.

🎬 [Watch the Halloween Smash video on YouTube](https://youtube.com/shorts/5NoCl6Tin7s)

## License

This project is released under the MIT License. See [LICENSE](LICENSE).
