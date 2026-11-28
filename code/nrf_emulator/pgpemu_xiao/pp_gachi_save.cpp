#include "pp_gachi_save.h"

#include "pp_gachi_species.h"

namespace {

static constexpr size_t kHeader = 3;          // version + 2 length bytes
static constexpr size_t kTrailer = 2;         // Fletcher-16
static constexpr size_t kFixedPayload = 25;   // everything before the sanctuary records
static constexpr size_t kSanctuaryRecord = 4; // dex, shiny, battlesWon (2)

uint16_t fletcher16(const uint8_t* bytes, size_t n)
{
  uint16_t a = 0;
  uint16_t b = 0;
  for (size_t i = 0; i < n; ++i) {
    a = static_cast<uint16_t>((a + bytes[i]) % 255);
    b = static_cast<uint16_t>((b + a) % 255);
  }
  return static_cast<uint16_t>((b << 8) | a);
}

// Bounds-checked sequential reader over the payload. A field that does not fit in
// what is left is absent: the caller keeps its default and later fields stay absent.
class Reader {
 public:
  Reader(const uint8_t* data, size_t n) : data_(data), n_(n), pos_(0) {}

  size_t left() const { return n_ - pos_; }

  bool u8(uint8_t& v)
  {
    if (left() < 1) {
      return false;
    }
    v = data_[pos_];
    pos_ += 1;
    return true;
  }

  bool u16(uint16_t& v)
  {
    if (left() < 2) {
      return false;
    }
    v = static_cast<uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
    pos_ += 2;
    return true;
  }

  bool u32(uint32_t& v)
  {
    if (left() < 4) {
      return false;
    }
    v = static_cast<uint32_t>(data_[pos_]) | (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
        (static_cast<uint32_t>(data_[pos_ + 2]) << 16) | (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
    pos_ += 4;
    return true;
  }

 private:
  const uint8_t* data_;
  size_t n_;
  size_t pos_;
};

class Writer {
 public:
  explicit Writer(uint8_t* out) : out_(out), pos_(0) {}

  size_t pos() const { return pos_; }
  void u8(uint8_t v) { out_[pos_++] = v; }
  void u16(uint16_t v)
  {
    u8(static_cast<uint8_t>(v & 0xFF));
    u8(static_cast<uint8_t>(v >> 8));
  }
  void u32(uint32_t v)
  {
    u16(static_cast<uint16_t>(v & 0xFFFF));
    u16(static_cast<uint16_t>(v >> 16));
  }

 private:
  uint8_t* out_;
  size_t pos_;
};

uint8_t minU8(uint8_t a, uint8_t b)
{
  return a < b ? a : b;
}

// Makes a loaded state safe for the rest of the game. Never rejects.
void makeSafe(PpGachiState& st, uint32_t freshSeed)
{
  if (st.berries > PP_GACHI_BERRY_CAP) {
    st.berries = PP_GACHI_BERRY_CAP;
  }

  // Opponents: keep the ones in the roster, in order, within the cap.
  uint8_t count = minU8(st.opponentCount, PP_GACHI_OPPONENT_CAP);
  uint8_t kept = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (ppGachiSpeciesByDex(st.opponents[i]) != nullptr) {
      st.opponents[kept] = st.opponents[i];
      ++kept;
    }
  }
  for (uint8_t i = kept; i < PP_GACHI_OPPONENT_CAP; ++i) {
    st.opponents[i] = 0;
  }
  st.opponentCount = kept;

  // Pet and tracks. With no pet there is nothing to track.
  if (!st.hasPet) {
    st.petDex = 0;
    st.petShiny = false;
    st.petBattlesWon = 0;
    st.arrivalPending = false;
  }
  uint8_t need = ppGachiNeed(st);
  st.nutrition = minU8(st.nutrition, need);
  st.experience = minU8(st.experience, need);
  st.happiness = minU8(st.happiness, minU8(st.nutrition, st.experience));

  // Dex bits only for roster indices.
  uint16_t mask = static_cast<uint16_t>((1u << PP_GACHI_SPECIES_COUNT) - 1u);
  st.dexRaised = static_cast<uint16_t>(st.dexRaised & mask);
  st.dexShiny = static_cast<uint16_t>(st.dexShiny & mask);

  // Sanctuary: drop entries not in the roster, keep order.
  uint8_t sCount = minU8(st.sanctuaryCount, PP_GACHI_SANCTUARY_CAP);
  uint8_t sKept = 0;
  for (uint8_t i = 0; i < sCount; ++i) {
    if (ppGachiSpeciesByDex(st.sanctuary[i].dex) != nullptr) {
      st.sanctuary[sKept] = st.sanctuary[i];
      ++sKept;
    }
  }
  for (uint8_t i = sKept; i < PP_GACHI_SANCTUARY_CAP; ++i) {
    st.sanctuary[i].dex = 0;
    st.sanctuary[i].shiny = false;
    st.sanctuary[i].battlesWon = 0;
  }
  st.sanctuaryCount = sKept;

  if (st.rng == 0) {
    PpGachiState fresh;
    ppGachiInit(fresh, freshSeed);
    st.rng = fresh.rng;
  }
}

}  // namespace

size_t ppGachiPack(const PpGachiState& st, uint8_t* out, size_t cap)
{
  uint8_t sCount = minU8(st.sanctuaryCount, PP_GACHI_SANCTUARY_CAP);
  size_t payload = kFixedPayload + kSanctuaryRecord * sCount;
  size_t total = kHeader + payload + kTrailer;
  if (out == nullptr || cap < total) {
    return 0;
  }

  Writer w(out);
  w.u8(PP_GACHI_SAVE_VERSION);
  w.u16(static_cast<uint16_t>(payload));
  w.u8(st.hasPet ? 1 : 0);
  w.u8(st.petDex);
  w.u8(st.petShiny ? 1 : 0);
  w.u8(st.nutrition);
  w.u8(st.happiness);
  w.u8(st.experience);
  w.u16(st.petBattlesWon);
  w.u8(st.arrivalPending ? 1 : 0);
  w.u8(st.berries);
  w.u8(st.opponentCount);
  for (uint8_t i = 0; i < PP_GACHI_OPPONENT_CAP; ++i) {
    w.u8(st.opponents[i]);
  }
  w.u16(st.dexRaised);
  w.u16(st.dexShiny);
  w.u32(st.rng);
  w.u8(sCount);
  for (uint8_t i = 0; i < sCount; ++i) {
    w.u8(st.sanctuary[i].dex);
    w.u8(st.sanctuary[i].shiny ? 1 : 0);
    w.u16(st.sanctuary[i].battlesWon);
  }
  w.u16(fletcher16(out, w.pos()));
  return w.pos();
}

bool ppGachiUnpack(const uint8_t* in, size_t len, PpGachiState& out, uint32_t freshSeed)
{
  ppGachiInit(out, freshSeed);
  if (in == nullptr || len < kHeader + kTrailer) {
    return false;
  }
  if (in[0] != PP_GACHI_SAVE_VERSION) {
    return false;
  }
  size_t n = static_cast<size_t>(in[1]) | (static_cast<size_t>(in[2]) << 8);
  if (kHeader + n + kTrailer > len) {
    return false;
  }
  uint16_t sum = fletcher16(in, kHeader + n);
  uint16_t stored = static_cast<uint16_t>(in[kHeader + n] | (in[kHeader + n + 1] << 8));
  if (sum != stored) {
    return false;
  }

  // Read into a scratch copy so a rejected pet leaves `out` a clean fresh game.
  PpGachiState st = out;
  Reader r(in + kHeader, n);
  uint8_t b = 0;
  uint16_t w = 0;
  uint32_t d = 0;
  do {
    if (!r.u8(b)) { break; }
    st.hasPet = (b != 0);
    if (!r.u8(st.petDex)) { break; }
    if (!r.u8(b)) { break; }
    st.petShiny = (b != 0);
    if (!r.u8(st.nutrition)) { break; }
    if (!r.u8(st.happiness)) { break; }
    if (!r.u8(st.experience)) { break; }
    if (!r.u16(st.petBattlesWon)) { break; }
    if (!r.u8(b)) { break; }
    st.arrivalPending = (b != 0);
    if (!r.u8(st.berries)) { break; }
    if (!r.u8(st.opponentCount)) { break; }
    bool opponentsOk = true;
    for (uint8_t i = 0; i < PP_GACHI_OPPONENT_CAP; ++i) {
      if (!r.u8(st.opponents[i])) {
        opponentsOk = false;
        break;
      }
    }
    if (!opponentsOk) {
      // Opponent slots incomplete: the count is meaningless, keep none.
      st.opponentCount = 0;
      for (uint8_t i = 0; i < PP_GACHI_OPPONENT_CAP; ++i) {
        st.opponents[i] = 0;
      }
      break;
    }
    if (!r.u16(w)) { break; }
    st.dexRaised = w;
    if (!r.u16(w)) { break; }
    st.dexShiny = w;
    if (!r.u32(d)) { break; }
    st.rng = d;
    uint8_t sCount = 0;
    if (!r.u8(sCount)) { break; }
    uint8_t take = minU8(sCount, PP_GACHI_SANCTUARY_CAP);
    uint8_t got = 0;
    while (got < take && r.left() >= kSanctuaryRecord) {
      uint8_t dex = 0;
      uint8_t shiny = 0;
      uint16_t won = 0;
      r.u8(dex);
      r.u8(shiny);
      r.u16(won);
      st.sanctuary[got].dex = dex;
      st.sanctuary[got].shiny = (shiny != 0);
      st.sanctuary[got].battlesWon = won;
      ++got;
    }
    st.sanctuaryCount = got;
  } while (false);

  if (st.hasPet && ppGachiSpeciesByDex(st.petDex) == nullptr) {
    return false;
  }
  makeSafe(st, freshSeed);
  out = st;
  return true;
}
