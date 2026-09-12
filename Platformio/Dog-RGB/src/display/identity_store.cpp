#include "display/identity_store.h"
#include "storage/nvs_store.h"
#include "util/crc32.h"
#include <stddef.h>
#include <string.h>

namespace identity {
namespace {
constexpr uint32_t kMagic = 0x49475244;
constexpr const char *kKeys[] = {"id_a", "id_b"};
struct __attribute__((packed)) Record {
  uint32_t magic;
  uint16_t version, size;
  uint32_t generation;
  char name[49], phone[17];
  uint8_t kind, reserved;
  uint32_t crc;
};
static_assert(sizeof(Record) == 84, "Identity record v1 layout");
Config current;
uint32_t revision = 0;
int active = -1;
bool loaded = false;
SaveResult validate(const char *name, const char *phone, display::QrContactKind kind, Config &out) {
  using Kind = display::QrContactKind;
  if (kind != Kind::Disabled && kind != Kind::WhatsApp && kind != Kind::Call) return SaveResult::InvalidKind;
  if (name && phone && !name[0] && !phone[0] && kind == Kind::Disabled) return SaveResult::Saved;
  const auto pet = display::format_pet_name(name);
  if (pet.result != display::NameResult::Ready) return SaveResult::InvalidName;
  const auto contact = display::format_contact(phone, kind);
  if (contact.result != display::ContactResult::Ready && contact.result != display::ContactResult::Disabled)
    return SaveResult::InvalidPhone;
  memcpy(out.name, pet.text, sizeof(out.name));
  memcpy(out.phone, contact.phone, sizeof(out.phone));
  out.kind = kind;
  return SaveResult::Saved;
}
bool decode(const Record &record, Config &out) {
  if (record.magic != kMagic || record.version != 1 || record.size != sizeof(Record) || record.reserved ||
      record.crc != util::crc32_ieee(&record, offsetof(Record, crc)) ||
      !memchr(record.name, 0, sizeof(record.name)) || !memchr(record.phone, 0, sizeof(record.phone))) return false;
  if (validate(record.name, record.phone, static_cast<display::QrContactKind>(record.kind), out) != SaveResult::Saved) return false;
  return !memcmp(record.name, out.name, sizeof(record.name)) && !memcmp(record.phone, out.phone, sizeof(record.phone));
}
bool read(int slot, Record &record, Config &out) {
  auto &prefs = storage::prefs_cfg();
  return prefs.getBytesLength(kKeys[slot]) == sizeof(record) &&
         prefs.getBytes(kKeys[slot], &record, sizeof(record)) == sizeof(record) && decode(record, out);
}
}
void load() {
  current = Config{}; revision = 0; active = -1; loaded = true;
  Record records[2]{}; Config candidates[2]{};
  const bool a = read(0, records[0], candidates[0]), b = read(1, records[1], candidates[1]);
  if (!a && !b) return; // Missing/corrupt identity never invents a contact.
  const uint32_t delta = records[1].generation - records[0].generation;
  active = !a || (b && delta != 0 && delta < 0x80000000U) ? 1 : 0;
  current = candidates[active]; revision = records[active].generation;
}
const Config &get() { return current; }
uint32_t generation() { return revision; }
bool configured() { return current.name[0] && current.phone[0]; }
SaveResult save(const char *name, const char *phone, display::QrContactKind kind, uint32_t expected_generation) {
  if (!loaded) return SaveResult::NotLoaded;
  if (expected_generation != revision) return SaveResult::Conflict;
  Config next;
  const auto validity = validate(name, phone, kind, next);
  if (validity != SaveResult::Saved) return validity;
  if (!strcmp(next.name, current.name) && !strcmp(next.phone, current.phone) && next.kind == current.kind)
    return SaveResult::Unchanged;
  Record record{};
  record.magic = kMagic; record.version = 1; record.size = sizeof(record);
  record.generation = revision + 1;
  memcpy(record.name, next.name, sizeof(record.name)); memcpy(record.phone, next.phone, sizeof(record.phone));
  record.kind = static_cast<uint8_t>(next.kind);
  record.crc = util::crc32_ieee(&record, offsetof(Record, crc));
  const int target = active == 0 ? 1 : 0;
  auto &prefs = storage::prefs_cfg();
  if (prefs.putBytes(kKeys[target], &record, sizeof(record)) != sizeof(record)) return SaveResult::Storage;
  Record verified{}; Config decoded;
  if (!read(target, verified, decoded) || memcmp(&record, &verified, sizeof(record))) return SaveResult::Storage;
  current = next; revision = record.generation; active = target;
  return SaveResult::Saved;
}
}
