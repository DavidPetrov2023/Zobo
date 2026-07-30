/**
 * Pin map of the AI-Thinker ESP32-CAM board.
 *
 * The pins are fixed by the board layout, not chosen - the camera sits on a
 * parallel bus and every line is already routed on the PCB.
 *
 * Two of them are worth remembering when something behaves oddly:
 *   GPIO0  doubles as the camera clock and as the bootloader select pin, which
 *          is why it has to be pulled low to flash and released afterwards.
 *   GPIO4  drives the very bright white flash LED and is shared with the SD card
 *          slot, so the two cannot be used at the same time.
 */

#ifndef CAMERA_PINS_H
#define CAMERA_PINS_H

#define CAM_PIN_PWDN     32
#define CAM_PIN_RESET    -1   // not routed on this board
#define CAM_PIN_XCLK      0
#define CAM_PIN_SIOD     26   // SCCB data (I2C-like control bus)
#define CAM_PIN_SIOC     27   // SCCB clock

#define CAM_PIN_D7       35
#define CAM_PIN_D6       34
#define CAM_PIN_D5       39
#define CAM_PIN_D4       36
#define CAM_PIN_D3       21
#define CAM_PIN_D2       19
#define CAM_PIN_D1       18
#define CAM_PIN_D0        5

#define CAM_PIN_VSYNC    25
#define CAM_PIN_HREF     23
#define CAM_PIN_PCLK     22

#define CAM_PIN_FLASH_LED 4

#endif // CAMERA_PINS_H
