// Display the audio frequency spectrum gathered from the ADC on a
// RP2040 board.

// Uses the ApproxFFT functions from here(see sketch tab):
// https://www.instructables.com/ApproxFFT-Fastest-FFT-Function-for-Arduino/

// ADC sampling DMA code from here:
// https://github.com/raspberrypi/pico-examples/tree/master/adc/dma_capture

#include <TFT_eSPI.h>                 // Include the graphics library (this includes the sprite functions)

#include "hardware/adc.h"
#include "hardware/dma.h"

// Ideally the signal should be bandwidth limited to sample_frequency/2
#define SAMPLING_FREQUENCY 14000 // Sampling frequency in Hz

// ADC channel 0
#define CAPTURE_CHANNEL 0
#define SAMPLES 256

// Display options
#define DRAW_SPECTRUM  // Draw the spectrum
//#define DRAW_WATERFALL // Draw a frequency waterfall
#define DRAW_TRACE     // Draw a scope type trace (not good with waterfall!)

// Use just ONE of the following if DRAW_SPECTRUM defined
//#define DRAW_PEAK      // Draw spectrum peak bar
#define DOT_PEAK       //  Draw spectrum peak dot

// Scale factors for the display
#define TRACE_SCALE 7      // Scale divisor for 'scope trace amplitude
#define FFT_SCALE  100     // Scale divisor for FFT bar amplitude
#define EXP_FILTER 0.98    // Exponential peak filter decay rate, 0.9 fast decay, 0.99 very slow

int16_t sampleBuffer[SAMPLES]; // ADC sample DMA buffer
int16_t streamBuffer[SAMPLES]; // Scaled ADC sample working buffer
int approxBuffer[SAMPLES];  // ApproxFFT sample buffer
int peakBuffer[SAMPLES];    // Amplitude peak buffer

uint16_t counter = 0; // Frame counter
long startMillis = millis(); // Timer for FPS
uint16_t interval = 100; // FPS calculated over 100 frames
String fps = "0 fps";
uint32_t dt = 0;

TFT_eSPI    tft = TFT_eSPI();         // Declare object "tft"

TFT_eSprite spr = TFT_eSprite(&tft);  // Declare Sprite object "spr" with pointer to "tft" object

// Sprite width and height
// The width should be an integer multiple of samples/2 where that integer is a minimum of 1.
#define WIDTH  256
#define HEIGHT 160

// Pointer to the sprite image
uint16_t* sptr = nullptr;

dma_channel_config cfg;
int dma_chan;

void setup() {
  Serial.begin(115200);

  // Initialise the TFT library
  tft.init();
  tft.setRotation(1);
  tft.initDMA();
  tft.fillScreen(TFT_NAVY);
  tft.startWrite();

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("ADC input spectrum", tft.width() / 2, 5, 4);

  String str = String((int)SAMPLES) + " samples, " + String((float)SAMPLING_FREQUENCY / SAMPLES) + " fps (max)";
  tft.drawString(str, tft.width() / 2, 30, 2);

  tft.setTextDatum(TL_DATUM);
  tft.drawNumber(0, 31, 55 + HEIGHT + 5, 2);

  tft.setTextDatum(TR_DATUM);
  tft.drawFloat(SAMPLING_FREQUENCY / 4000.0, 1, 31 + 255, 55 + HEIGHT + 5, 2);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(" kHz", 31 + 255, 55 + HEIGHT + 5, 2);

  // Create the sprite and get the image pointer
  sptr = (uint16_t*)spr.createSprite(WIDTH, HEIGHT);

  // Init GPIO for analogue use: hi-Z, no pulls, disable digital input buffer.
  adc_gpio_init(26 + CAPTURE_CHANNEL);

  adc_init();
  adc_select_input(CAPTURE_CHANNEL);
  adc_fifo_setup(
    true,    // Write each completed conversion to the sample FIFO
    true,    // Enable DMA data request (DREQ)
    1,       // DREQ (and IRQ) asserted when at least 1 sample present
    false,   // We won't see the ERR bit because of 8 bit reads; disable.
    false    // Shift each sample to 8 bits when pushing to FIFO
  );

  // Divisor of 0 -> 500kHz. Free-running capture with the divider is
  // equivalent to pressing the ADC_CS_START_ONCE button once per `div + 1`
  // cycles (div not necessarily an integer). Each conversion takes 96
  // cycles, so in general you want a divider of 0 (hold down the button
  // continuously) or > 95 (take samples less frequently than 96 cycle
  // intervals). This is all timed by the 48 MHz ADC clock.
  adc_set_clkdiv((48000000/SAMPLING_FREQUENCY) - 1);

  // Set up the DMA to start transferring data as soon as it appears in FIFO
  dma_chan = dma_claim_unused_channel(true);
  cfg = dma_channel_get_default_config(dma_chan);

  // Reading from constant address, writing to incrementing byte addresses
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, true);

  // Pace transfers based on availability of ADC samples
  channel_config_set_dreq(&cfg, DREQ_ADC);

  dma_channel_configure(dma_chan, &cfg,
                        (uint16_t*)sampleBuffer,   // dst
                        &adc_hw->fifo,  // src
                        SAMPLES,        // transfer count
                        true            // start immediately
                       );

  // Start capture
  adc_run(true);

  // Reset FPS timer
  startMillis = millis();
}

void loop() {
  // Wait for DMA to finish (may have already)
  dma_channel_wait_for_finish_blocking(dma_chan);

  // Stop and clean out FIFO
  adc_run(false);
  adc_fifo_drain();

  // Copy samples into buffer for approxFFT
  for (int i = 0; i < SAMPLES; i++) {
    streamBuffer[i] = sampleBuffer[i]/4 - 512 + 49; // The +49 compensated for a slight DC offset from VCC/2
    //Serial.println(streamBuffer[i]); delay(100);
    approxBuffer[i] = streamBuffer[i];
  }

  // Now we have a copy of the samples we can start capture again
  dma_channel_configure(dma_chan, &cfg,
                        (uint16_t*)sampleBuffer,   // dst
                        &adc_hw->fifo,  // src
                        SAMPLES,        // transfer count
                        true            // start immediately
                       );

  // Restart the ADC capture
  adc_run(true);

  // Do the FFT number crunching
  // approxBuffer contains samples, but will be updated with amplitudes
  Approx_FFT(approxBuffer, SAMPLES, SAMPLING_FREQUENCY);

  // Wait for TFT DMA to complete (probably FFT is slower)
  tft.dmaWait();

  // Pixel width of a frequency band
  uint16_t pw = (4.0 * WIDTH / SAMPLES);

#ifndef DRAW_WATERFALL
  spr.fillSprite(TFT_BLACK);
#endif

#ifdef DRAW_SPECTRUM
  for (uint16_t i = 0; i < SAMPLES / 4; i++) {
#if defined (DRAW_PEAK) || defined (DOT_PEAK)
    // Track the peak and update decay filter
    if (approxBuffer[i] > peakBuffer[i]) {
      if (approxBuffer[i] / FFT_SCALE < HEIGHT) peakBuffer[i] = approxBuffer[i];
    }
    else peakBuffer[i] = peakBuffer[i] * EXP_FILTER + approxBuffer[i] * (1.0 - EXP_FILTER);
#endif
    // Convert amplitude to y height, give higher frequencies a boost
    uint16_t hp = (1 + (i / 10.0)) * peakBuffer[i] / FFT_SCALE;
    uint16_t hr = (1 + (i / 10.0)) * approxBuffer[i] / FFT_SCALE;

    if (hr > HEIGHT) hr = HEIGHT;
#ifdef DRAW_PEAK
    if (hp > HEIGHT) hp = HEIGHT;
    uint16_t col = rainbowColor(127 + min(hp, 96));
    spr.fillRect(pw * i, HEIGHT - hp, pw - 1, hp - hr, col);
#endif
#ifdef DOT_PEAK
    if (hp > HEIGHT) hp = HEIGHT;
    uint16_t col = rainbowColor(127 + min(hp, 96));
    spr.fillRect(pw * i, HEIGHT - hp, pw - 1, 2, col);
#endif
    spr.fillRect(pw * i, HEIGHT - hr, pw - 1, hr, TFT_WHITE);
  }
#endif

#ifdef DRAW_TRACE
  // Find a trigger point in the first half of the buffer to stabilise the trace start
  uint16_t startSample = 0;
  while (startSample < SAMPLES / 2 - 1) {
    // Look for a rising edge near zero
    if (streamBuffer[startSample] > 0 && streamBuffer[startSample] < 4 * TRACE_SCALE && streamBuffer[startSample + 1] > streamBuffer[startSample]) break;
    startSample++;
  }

  // Render the 'scope trace, only a half the buffer is plotted after the trigger point
  for (uint16_t x = 0; x < WIDTH; x += pw) {
    spr.drawLine(x, HEIGHT/2 - (streamBuffer[startSample] / TRACE_SCALE), x + pw, HEIGHT/2 - (streamBuffer[startSample + 1] / TRACE_SCALE), TFT_GREEN);
    startSample++;
    if (startSample >= SAMPLES - 1) break;
  }
#endif

#ifdef DRAW_WATERFALL
  // FFT waterfall
  spr.scroll(0, 1); // Scroll sprite down 1 pixel
  for (uint16_t i = 0; i < SAMPLES / 4; i++) {
    // Conver to y height, give higher frequencies a boost
    uint16_t hr = (1 + (i / 10.0)) * approxBuffer[i] / (FFT_SCALE / 2);
    if (hr > 127) hr = 127;
    uint16_t col = rainbowColor(128 - hr);
    spr.drawFastHLine(pw * i, 0, pw, col);
  }
#endif

  // Can use pushImage instead, but DMA provides better performance
  // tft.pushImage(31, 55, WIDTH, HEIGHT, sptr);
  tft.pushImageDMA(31, 55, WIDTH, HEIGHT, sptr);

  counter++;
  // only calculate the fps every <interval> iterations.
  if (counter % interval == 0) {
    long millisNow = millis();
    fps = String((interval * 1000.0 / (millisNow - startMillis))) + " fps";
    startMillis = millisNow;
    tft.setTextDatum(TC_DATUM);
    tft.setTextPadding(tft.textWidth(" 999.99 fps ", 2));
    tft.drawString(fps, tft.width() / 2, 55 + HEIGHT + 5, 2);
  }
}

// If 'spectrum' is in the range 0-159 it is converted to a spectrum colour
// from 0 = red through to 127 = blue to 159 = violet
// Extending the range to 0-191 adds a further violet to red band
uint16_t rainbowColor(uint8_t spectrum)
{
  spectrum = spectrum % 192;

  uint8_t red   = 0; // Red is the top 5 bits of a 16 bit colour spectrum
  uint8_t green = 0; // Green is the middle 6 bits, but only top 5 bits used here
  uint8_t blue  = 0; // Blue is the bottom 5 bits

  uint8_t sector = spectrum >> 5;
  uint8_t amplit = spectrum & 0x1F;

  switch (sector)
  {
    case 0:
      red   = 0x1F;
      green = amplit; // Green ramps up
      blue  = 0;
      break;
    case 1:
      red   = 0x1F - amplit; // Red ramps down
      green = 0x1F;
      blue  = 0;
      break;
    case 2:
      red   = 0;
      green = 0x1F;
      blue  = amplit; // Blue ramps up
      break;
    case 3:
      red   = 0;
      green = 0x1F - amplit; // Green ramps down
      blue  = 0x1F;
      break;
    case 4:
      red   = amplit; // Red ramps up
      green = 0;
      blue  = 0x1F;
      break;
    case 5:
      red   = 0x1F;
      green = 0;
      blue  = 0x1F - amplit; // Blue ramps down
      break;
  }

  return red << 11 | green << 6 | blue;
}
