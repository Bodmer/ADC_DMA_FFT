# ADC signal spectrum display on a TFT

This sketch is for RP2040 board connected to a 320 x 240 TFT display. It samples signals on ADC channel 0 and displays the frequency spectra on a TFT display.  The sketch uses FFT analysis to extract the frequency spectrum from the signal.

This is the "tone" square wave from an I/O pin being sampled. The 500Hz tone peakis seen plus the odd harmonics as expected.

      ![Tone FFT](https://i.imgur.com/mkkX7Cf.png)

The spectrum can be displayed as a spectrum (frequency .v. amlitude) format, or as a scrolling waterfall (frequency .v. time).

The peaks are displayed and decay. The waveform can also be displayed.

Screen update rate varies with the number of FFT samples taken, example fps performance with 62.5MHz SPI clock:
* FFT samples = 64  ->  81 fps
* FFT samples = 128 ->  78 fps
* FFT samples = 256 ->  54 fps
* FFT samples = 512 ->  21 fps

The FFT display area is set to 256 x 160, this could be changed. The width should be an integer multiple of samples/2 where that integer is a minimum of 1.

The maximum fps will depend on a number of factors:
1. The number of FFT samples, higher numbers take more time
2. The sample rate (default is 14kHz), higher rates reduce time taken and change frequency display scale
3. The time it takes to gather the N samples (sample rate/N fps)
4. The time it takes to update the screen (depends on sprite size and write speed)

The displayed frequency span is sample rate/4 (so default range is 0-3.5kHz).

There are #define options to alter the defaults:
1. Sample rate
2. FFT sample count
3. FFT display area size
4. Peak marker on/off and decay rate
5. Peak marker as bar or dot
6. Frequency amplitude gain
7. Oscilloscope trace on/off and amplitude gain
8. Waterfall option (default off)

The display update uses DMA so that the display can be updated while an FFT and sample gathering is in progress.

To stabilise the 'scope trace the sketch uses a simple method to determine a rising edge trigger point.

The sketch requires the Earle Philhower RP2040 board package to be used:
https://github.com/earlephilhower/arduino-pico

The TFT_eSPI library is also used, available using the Arduino IDE library manager or here:
https://github.com/Bodmer/TFT_eSPI

A similar example sketch which samples a PDM microphone can be found [here](https://github.com/Bodmer/Audio-Spectrum-FFT).
