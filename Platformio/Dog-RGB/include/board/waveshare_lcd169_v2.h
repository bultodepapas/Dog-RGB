#pragma once

// Candidate for the official No Touch V2 schematic, not revision detection.
// Provenance and physical gates: docs/boards.md.
namespace board {
constexpr const char *kId = "display-waveshare-lcd169";
constexpr const char *kRevision = "v2-candidate";
constexpr bool kHasDisplay = true;
constexpr bool kHasTouch = false;
constexpr int kStatusLedPin = -1;
constexpr int kLedAData = 17;
constexpr int kLedBData = 18;
constexpr int kGpsRx = 44;
constexpr int kGpsTx = 43;
constexpr int kPowerHoldPin = 41; // SYS_EN: HIGH retains power; never a second button.
constexpr int kPowerButtonPin = 40; // SYS_OUT: observation only in I0.
constexpr int kBacklightPin = 15;
// Official No Touch demo: portrait ST7789, 240x280, offsets 0/20/0/0.
constexpr int kLcdDc = 4, kLcdCs = 5, kLcdSck = 6, kLcdMosi = 7, kLcdReset = 8;
constexpr int kLcdWidth = 240, kLcdHeight = 280;
constexpr int kLcdRowOffset = 20;
constexpr int kLcdSpiHz = 40000000;
constexpr unsigned long kFlashBytes = 16UL * 1024 * 1024;
constexpr unsigned long kPsramBytes = 8UL * 1024 * 1024;
// ADC, LCD, I2C, USB, flash/OPI memory, sensor IRQs, power and buzzer.
// Reserving a pin does not enable its peripheral or driver.
constexpr int kReservedPins[] = {
    1, 4, 5, 6, 7, 8, 10, 11, 15, 19, 20,
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42};
} // namespace board
