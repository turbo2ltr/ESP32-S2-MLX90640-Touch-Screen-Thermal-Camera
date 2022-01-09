# ESP32-S2-MLX90640-Touch-Screen-Thermal-Camera
Forked from https://github.com/Makerfabs/ESP32-S2-MLX90640-Touch-Screen-Thermal-Camera

This is a modification to the V3 example from Makerfabs with the following changes:
* Removed wifi option and related code
* Removed option and related code for non-interpolated output
* Removed unused SD Card functions
* Added min/max/mean display
* Upper and lower bounds of the color map are no longer hard coded. They are now auto-scaling based on the min/max temperatures of the frame to get the best dynamic range
* Added button to lock upper and lower bounds (i.e. turn off auto-scaling)
* Added crosshairs and temp readout in center of the image
* Added button to save screenshot BMP to SD card.  (based off http://www.technoblogy.com/show?398X)

![screen](https://github.com/turbo2ltr/ESP32-S2-MLX90640-Touch-Screen-Thermal-Camera/blob/main/md_pic/screen1.jpg?raw=true)

Saved bitmap:

![Saved Bitmap](https://raw.githubusercontent.com/turbo2ltr/ESP32-S2-MLX90640-Touch-Screen-Thermal-Camera/main/md_pic/image7.bmp)

Useful links:
* https://github.com/Makerfabs/ESP32-S2-MLX90640-Touch-Screen-Thermal-Camera
* https://learn.adafruit.com/adafruit-gfx-graphics-library/graphics-primitives
* https://github.com/lovyan03/LovyanGFX
* http://www.technoblogy.com/list?39BW
