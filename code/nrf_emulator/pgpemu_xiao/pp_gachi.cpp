#include <Arduino.h>
#include <string.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "pp_gachi.h"
#include "pp_gachi_save.h"
#include "pp_gachi_tuning.h"
#include "pp_settings.h"

using namespace Adafruit_LittleFS_Namespace;

namespace {

static constexpr char kSavePath[] = "/gachi.bin";
static constexpr char kNewPath[] = "/gachi.new";

static PpGachiState game;
static bool dirty = false;
static uint32_t dirtySince = 0;
// What the last catcher event granted; the display reads it to tell a spin whose
// berry did not fit.
static PpGachiGrant lastGrant = PP_GACHI_GRANT_NONE;

static uint32_t hardwareSeed()
{
  uint32_t seed = NRF_FICR->DEVICEID[0] ^ (NRF_FICR->DEVICEID[1] * 2654435761u);
  seed ^= micros();
  return seed == 0 ? 1u : seed;
}

static void markDirty()
{
  if (!dirty) {
    dirty = true;
    dirtySince = millis();
  }
}

// Reads one save file. False when missing, short or invalid; `out` is then a fresh game.
static bool loadFile(const char* path, PpGachiState& out)
{
  uint8_t record[PP_GACHI_SAVE_MAX];
  int n = 0;
  File file(InternalFS);
  if (file.open(path, FILE_O_READ)) {
    n = file.read(record, sizeof(record));
    file.close();
  }
  if (n <= 0) {
    return false;
  }
  return ppGachiUnpack(record, static_cast<size_t>(n), out, hardwareSeed());
}

// Never destroys the last good save: the new record is complete on /gachi.new before
// /gachi.bin is touched, and a load falls back to /gachi.new if the rename was cut short.
static bool writeSave()
{
  uint8_t record[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(game, record, sizeof(record));
  if (n == 0) {
    Serial.println("Gachi: could not save");
    return false;
  }
  // With no /gachi.bin, /gachi.new may hold the last good save (an earlier swap
  // was cut short): make it /gachi.bin before /gachi.new is written over.
  if (!InternalFS.exists(kSavePath) && InternalFS.exists(kNewPath) && !InternalFS.rename(kNewPath, kSavePath)) {
    Serial.println("Gachi: could not save, kept /gachi.new");
    return false;
  }
  InternalFS.remove(kNewPath);
  File file(InternalFS);
  if (!file.open(kNewPath, FILE_O_WRITE)) {
    Serial.println("Gachi: could not save");
    return false;
  }
  size_t written = file.write(record, n);
  file.close();
  if (written != n) {
    InternalFS.remove(kNewPath);
    Serial.println("Gachi: could not save");
    return false;
  }
  // Only a record that reads back valid replaces the save.
  PpGachiState check;
  if (!loadFile(kNewPath, check)) {
    InternalFS.remove(kNewPath);
    Serial.println("Gachi: could not save");
    return false;
  }
  InternalFS.remove(kSavePath);
  if (!InternalFS.rename(kNewPath, kSavePath)) {
    Serial.println("Gachi: rename failed, kept /gachi.new");
    return false;
  }
  dirty = false;
  return true;
}

// A failed write leaves the state marked unsaved so the next flush or timer retries it.
static bool saveNow()
{
  if (writeSave()) {
    return true;
  }
  markDirty();
  return false;
}

// Saves at once when the pack of the state differs from `before`.
static void saveIfChanged(const uint8_t* before, size_t beforeLen)
{
  uint8_t after[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(game, after, sizeof(after));
  if (n != beforeLen || memcmp(before, after, n) != 0) {
    saveNow();
  }
}

}  // namespace

void ppGachiBegin()
{
  PpGachiState loaded;
  bool fromSave = loadFile(kSavePath, loaded);
  bool ok = fromSave || loadFile(kNewPath, loaded);
  if (ok && !fromSave) {
    // Only /gachi.new was good (a swap cut short, or a bad /gachi.bin): promote
    // it, so the next save cannot start by deleting the only good copy.
    InternalFS.remove(kSavePath);
    if (!InternalFS.rename(kNewPath, kSavePath)) {
      Serial.println("Gachi: rename failed, kept /gachi.new");
    }
  }
  if (ok) {
    game = loaded;
  } else {
    ppGachiInit(game, hardwareSeed());
  }
  dirty = false;
  Serial.println(ok ? "Gachi: loaded" : "Gachi: new game");
}

void ppGachiOnEvent(PpFeedbackEvent event)
{
  PpGachiGrant grant = ppGachiOnEvent(game, event, ppGachiEnabled());
  lastGrant = grant;
  if (grant != PP_GACHI_GRANT_NONE) {
    markDirty();
  }
  if (event == PP_FEEDBACK_DISCONNECTED) {
    ppGachiFlush();
  }
}

PpGachiGrant ppGachiLastGrant()
{
  return lastGrant;
}

void ppGachiUpdate(uint32_t now)
{
  if (dirty && static_cast<uint32_t>(now - dirtySince) >= PP_GACHI_GRANT_SAVE_MS) {
    saveNow();
    if (dirty) {
      dirtySince = now;  // a failed write retries after another interval
    }
  }
}

void ppGachiFlush()
{
  if (dirty) {
    saveNow();
  }
}

bool ppGachiEnabled()
{
  return ppSettings().pokegachi;
}

void ppGachiSetEnabled(bool on)
{
  ppSettingsSetPokegachi(on);
  ppGachiFlush();
}

const PpGachiState& ppGachiState()
{
  return game;
}

PpGachiResult ppGachiDoFeed()
{
  if (!ppGachiEnabled()) {
    return PP_GACHI_NO_PET;
  }
  uint8_t before[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(game, before, sizeof(before));
  PpGachiResult r = ppGachiFeed(game);
  saveIfChanged(before, n);
  return r;
}

PpGachiResult ppGachiDoPlay()
{
  if (!ppGachiEnabled()) {
    return PP_GACHI_NO_PET;
  }
  uint8_t before[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(game, before, sizeof(before));
  PpGachiResult r = ppGachiPlay(game);
  saveIfChanged(before, n);
  return r;
}

PpGachiResult ppGachiDoAdvance()
{
  if (!ppGachiEnabled()) {
    return PP_GACHI_NO_PET;
  }
  uint8_t before[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(game, before, sizeof(before));
  PpGachiResult r = ppGachiAdvance(game);
  saveIfChanged(before, n);
  return r;
}

bool ppGachiBattleOpponent(uint8_t& dex)
{
  if (!ppGachiEnabled() || !game.hasPet || game.opponentCount == 0) {
    return false;
  }
  dex = game.opponents[0];
  return true;
}

PpGachiPick ppGachiOpponentPick()
{
  return static_cast<PpGachiPick>(ppGachiRandom(game, PP_PICK_COUNT));
}

void ppGachiBattleFinish(bool won)
{
  if (!ppGachiEnabled()) {
    return;
  }
  uint8_t before[PP_GACHI_SAVE_MAX];
  size_t n = ppGachiPack(game, before, sizeof(before));
  ppGachiBattleResult(game, won);
  saveIfChanged(before, n);
}

void ppGachiAckArrival()
{
  if (!game.arrivalPending) {
    return;
  }
  game.arrivalPending = false;
  saveNow();
}

void ppGachiResetGame()
{
  ppGachiReset(game);
  saveNow();
}
