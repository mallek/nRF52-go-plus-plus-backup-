#include <Arduino.h>
#include <math.h>

#include "pp_feedback_audio.h"

namespace {

static constexpr uint8_t kPinBclk = D8;
static constexpr uint8_t kPinLrc = D9;
static constexpr uint8_t kPinDin = D10;

// MCK = 32 MHz / 21, LRCK = MCK / 32 = 47,619 Hz (within 1% of 48 kHz, which the
// MAX98357A auto-detects).
static constexpr float kSampleRate = 47619.0f;
static constexpr int16_t kAmplitude = 16000;  // ~50%; louder risks 3V3 sag during BLE
static constexpr uint32_t kMaxSamples = 512;

struct Note {
  uint16_t freqHz;  // 0 = rest
  uint16_t ms;
};

static const Note kCaught[] = { { 1047, 100 }, { 1319, 100 }, { 1568, 220 } };
static const Note kFled[] = { { 784, 150 }, { 587, 320 } };
static const Note kPokestopSpun[] = { { 1760, 90 } };
static const Note kConnected[] = { { 1319, 60 }, { 0, 60 }, { 1319, 60 } };
static const Note kDisconnected[] = { { 392, 200 } };
static const Note kFullyCharged[] = { { 1047, 80 }, { 1319, 80 }, { 1568, 80 }, { 2093, 200 } };

// I2S DMA buffers: each 32-bit word is one stereo frame (same sample left and right).
static uint32_t toneBuffers[2][kMaxSamples];
static uint8_t nextBuffer = 0;
static uint32_t silence[16];

static const Note* sequence = nullptr;
static uint8_t sequenceLen = 0;
static uint8_t noteIndex = 0;
static uint32_t noteStartMs = 0;
static bool running = false;

static void i2sStart()
{
  if (running) {
    return;
  }
  NRF_I2S->TXD.PTR = reinterpret_cast<uint32_t>(silence);
  NRF_I2S->RXTXD.MAXCNT = sizeof(silence) / sizeof(silence[0]);
  NRF_I2S->ENABLE = 1;
  NRF_I2S->TASKS_START = 1;
  running = true;
}

static void i2sStop()
{
  if (!running) {
    return;
  }
  NRF_I2S->TASKS_STOP = 1;
  NRF_I2S->ENABLE = 0;
  running = false;
}

static void playSilence()
{
  NRF_I2S->TXD.PTR = reinterpret_cast<uint32_t>(silence);
  NRF_I2S->RXTXD.MAXCNT = sizeof(silence) / sizeof(silence[0]);
}

// Loop one period of a sine; the hardware repeats it until TXD.PTR changes.
static void playTone(uint16_t freqHz)
{
  if (freqHz == 0) {
    playSilence();
    return;
  }
  uint32_t* buf = toneBuffers[nextBuffer];
  nextBuffer ^= 1;
  uint32_t samples = static_cast<uint32_t>(lroundf(kSampleRate / freqHz));
  if (samples < 2) {
    samples = 2;
  }
  if (samples > kMaxSamples) {
    samples = kMaxSamples;
  }
  for (uint32_t i = 0; i < samples; ++i) {
    int16_t s = static_cast<int16_t>(kAmplitude * sinf(2.0f * PI * i / samples));
    uint16_t u = static_cast<uint16_t>(s);
    buf[i] = (static_cast<uint32_t>(u) << 16) | u;
  }
  NRF_I2S->TXD.PTR = reinterpret_cast<uint32_t>(buf);
  NRF_I2S->RXTXD.MAXCNT = samples;
}

static bool sequenceFor(PpFeedbackEvent event, const Note*& notes, uint8_t& len)
{
  switch (event) {
    case PP_FEEDBACK_CAUGHT: {
      notes = kCaught;
      len = sizeof(kCaught) / sizeof(kCaught[0]);
      return true;
    }
    case PP_FEEDBACK_FLED: {
      notes = kFled;
      len = sizeof(kFled) / sizeof(kFled[0]);
      return true;
    }
    case PP_FEEDBACK_POKESTOP_SPUN: {
      notes = kPokestopSpun;
      len = sizeof(kPokestopSpun) / sizeof(kPokestopSpun[0]);
      return true;
    }
    case PP_FEEDBACK_CONNECTED: {
      notes = kConnected;
      len = sizeof(kConnected) / sizeof(kConnected[0]);
      return true;
    }
    case PP_FEEDBACK_DISCONNECTED: {
      notes = kDisconnected;
      len = sizeof(kDisconnected) / sizeof(kDisconnected[0]);
      return true;
    }
    case PP_FEEDBACK_FULLY_CHARGED: {
      notes = kFullyCharged;
      len = sizeof(kFullyCharged) / sizeof(kFullyCharged[0]);
      return true;
    }
    default: {
      // Nearby prompts and unknown results stay silent by design.
      return false;
    }
  }
}

}  // namespace

void ppFeedbackAudioBegin()
{
  NRF_I2S->CONFIG.MODE = I2S_CONFIG_MODE_MODE_Master;
  NRF_I2S->CONFIG.TXEN = I2S_CONFIG_TXEN_TXEN_Enabled;
  NRF_I2S->CONFIG.RXEN = I2S_CONFIG_RXEN_RXEN_Disabled;
  NRF_I2S->CONFIG.MCKEN = I2S_CONFIG_MCKEN_MCKEN_Enabled;
  NRF_I2S->CONFIG.MCKFREQ = I2S_CONFIG_MCKFREQ_MCKFREQ_32MDIV21;
  NRF_I2S->CONFIG.RATIO = I2S_CONFIG_RATIO_RATIO_32X;
  NRF_I2S->CONFIG.SWIDTH = I2S_CONFIG_SWIDTH_SWIDTH_16Bit;
  NRF_I2S->CONFIG.ALIGN = I2S_CONFIG_ALIGN_ALIGN_Left;
  NRF_I2S->CONFIG.FORMAT = I2S_CONFIG_FORMAT_FORMAT_I2S;
  NRF_I2S->CONFIG.CHANNELS = I2S_CONFIG_CHANNELS_CHANNELS_Stereo;

  NRF_I2S->PSEL.MCK = I2S_PSEL_MCK_CONNECT_Disconnected << I2S_PSEL_MCK_CONNECT_Pos;
  NRF_I2S->PSEL.SCK = g_ADigitalPinMap[kPinBclk];
  NRF_I2S->PSEL.LRCK = g_ADigitalPinMap[kPinLrc];
  NRF_I2S->PSEL.SDOUT = g_ADigitalPinMap[kPinDin];
  NRF_I2S->PSEL.SDIN = I2S_PSEL_SDIN_CONNECT_Disconnected << I2S_PSEL_SDIN_CONNECT_Pos;
}

void ppFeedbackAudioPlayEvent(PpFeedbackEvent event, uint32_t nowMs)
{
  const Note* notes = nullptr;
  uint8_t len = 0;
  if (!sequenceFor(event, notes, len)) {
    return;
  }
  sequence = notes;
  sequenceLen = len;
  noteIndex = 0;
  noteStartMs = nowMs;
  i2sStart();
  playTone(sequence[0].freqHz);
}

void ppFeedbackAudioUpdate(uint32_t nowMs)
{
  if (sequence == nullptr) {
    return;
  }
  if (nowMs - noteStartMs < sequence[noteIndex].ms) {
    return;
  }
  ++noteIndex;
  if (noteIndex >= sequenceLen) {
    ppFeedbackAudioStop();
    return;
  }
  noteStartMs = nowMs;
  playTone(sequence[noteIndex].freqHz);
}

void ppFeedbackAudioStop()
{
  sequence = nullptr;
  playSilence();
  i2sStop();
}
