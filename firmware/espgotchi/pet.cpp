#include "pet.h"
#include <Preferences.h>
#include <stdarg.h>
#include "config.h"
#include "sprites.h"

static const uint32_t PET_MAGIC = 0x50455431;  // "PET1"
static const uint16_t PET_VERSION = 3;
static Preferences prefs;

// Stage thresholds (seconds of life). Tuned for a demo, not a 7-day marathon.
static const uint32_t EGG_HATCH_SEC = 60;
// Age (seconds since hatching) at which each stage ends. Index = stage.
static const uint32_t STAGE_END_SEC[STAGE_COUNT] = {
  0,                 // egg (uses eggSec)
  30UL * 60,         // baby   -> child  at 30 min
  2UL * 3600,        // child  -> teen   at 2 h
  8UL * 3600,        // teen   -> adult  at 8 h
  48UL * 3600,       // adult  -> elder  at 48 h
  0xFFFFFFFF,        // elder  stays
};

static int16_t clamp100(int32_t v) { return v < 0 ? 0 : (v > 100 ? 100 : (int16_t)v); }

void Pet::begin() {
  prefs.begin("pet", false);
  size_t len = prefs.getBytesLength("state");
  if (len == sizeof(PetState)) {
    prefs.getBytes("state", &s, sizeof(PetState));
  }
  if (s.magic != PET_MAGIC || s.version != PET_VERSION) {
    reset();
    s.generation = 1;
  }
  animCur = ANIM_NONE;
  logEvent("Booted. Hello %s!", s.name);
}

void Pet::reset(int8_t species) {
  uint16_t gen = s.magic == PET_MAGIC ? s.generation + 1 : 1;
  memset(&s, 0, sizeof(s));
  s.species = (species >= 0 && species < SPECIES_COUNT) ? species : (esp_random() % SPECIES_COUNT);
  s.magic = PET_MAGIC;
  s.version = PET_VERSION;
  strlcpy(s.name, DEFAULT_PET_NAME, sizeof(s.name));
  s.stage = STAGE_EGG;
  s.hunger = 80;
  s.happiness = 80;
  s.energy = 100;
  s.hygiene = 100;
  s.health = 100;
  s.weight = 5;
  s.generation = gen;
  animCur = ANIM_NONE;
  dirty = true;
  logEvent("A new %s egg appeared (gen %u)", SPECIES[s.species].label, gen);
}

void Pet::setName(const char *n) {
  strlcpy(s.name, n, sizeof(s.name));
  if (!s.name[0]) strlcpy(s.name, DEFAULT_PET_NAME, sizeof(s.name));
  dirty = true;
  logEvent("Renamed to %s", s.name);
}

const char *Pet::animItemName() const {
  if (animCur == ANIM_SNACK) return SNACKS_NAMES[animItemIdx % SNACKS_COUNT];
  return MEALS_NAMES[animItemIdx % MEALS_COUNT];
}

void Pet::startAnim(PetAnim a, uint32_t ms) {
  animCur = a;
  animStep = 0;
  animUntil = millis() + ms;
}

bool Pet::hatch() {
  if (s.stage != STAGE_EGG) return false;
  s.stage = STAGE_BABY;
  s.ageSec = 0;
  startAnim(ANIM_HATCH, 2000);
  dirty = true;
  logEvent("%s hatched!", s.name);
  return true;
}

bool Pet::mess() {
  if (s.dead || s.stage == STAGE_EGG || s.poops >= 3) return false;
  s.poops++;
  s.secSinceMeal = 0;
  dirty = true;
  logEvent("Uh oh, %s made a mess", s.name);
  return true;
}

bool Pet::feed(bool snack) {
  if (s.dead || s.stage == STAGE_EGG || s.asleep) return false;
  if (!snack && s.hunger >= 95) {
    logEvent("%s is full and refuses the meal", s.name);
    return false;
  }
  if (snack) {
    s.happiness += 12;
    s.hunger += 8;
    s.weight += 2;
    if (s.weight > 60) s.health -= 2;  // junk food has a price
  } else {
    s.hunger += 30;
    s.weight += 1;
  }
  s.secSinceMeal = 0;
  clampAll();
  animItemIdx = esp_random() % (snack ? SNACKS_COUNT : MEALS_COUNT);
  startAnim(snack ? ANIM_SNACK : ANIM_EAT, 3000);
  dirty = true;
  logEvent("%s %s", snack ? "Snack:" : "Meal:", animItemName() + (snack ? 6 : 5));
  return true;
}

bool Pet::play() {
  if (s.dead || s.stage == STAGE_EGG || s.asleep) return false;
  if (s.energy < 10) {
    logEvent("%s is too tired to play", s.name);
    return false;
  }
  s.happiness += 25;
  s.energy -= 10;
  s.weight -= 1;
  s.hygiene -= 3;
  clampAll();
  startAnim(ANIM_PLAY, 2600);
  dirty = true;
  logEvent("Played with %s", s.name);
  return true;
}

bool Pet::pet() {
  if (s.dead || s.stage == STAGE_EGG) return false;
  if (s.asleep) {
    logEvent("%s purrs in their sleep", s.name);
  } else {
    s.happiness += 8;
    clampAll();
    logEvent("Petted %s", s.name);
  }
  startAnim(ANIM_PET, 1800);
  dirty = true;
  return true;
}

bool Pet::clean() {
  if (s.dead || s.stage == STAGE_EGG) return false;
  s.poops = 0;
  s.hygiene = 100;
  clampAll();
  startAnim(ANIM_CLEAN, 1800);
  dirty = true;
  logEvent("All clean");
  return true;
}

bool Pet::toggleLights() {
  if (s.dead || s.stage == STAGE_EGG) return false;
  s.lightsOff = !s.lightsOff;
  if (s.lightsOff) {
    s.asleep = true;
    logEvent("Lights off, %s goes to sleep", s.name);
  } else {
    s.asleep = false;
    logEvent("Lights on, wake up!");
  }
  dirty = true;
  return true;
}

bool Pet::medicine() {
  if (s.dead || s.stage == STAGE_EGG) return false;
  if (!s.sick) {
    s.happiness -= 5;
    clampAll();
    logEvent("%s did not need medicine (yuck)", s.name);
    dirty = true;
    return false;
  }
  s.sick = false;
  s.health += 25;
  clampAll();
  startAnim(ANIM_HEAL, 2000);
  dirty = true;
  logEvent("%s feels better", s.name);
  return true;
}

void Pet::die(const char *why) {
  if (s.dead) return;
  s.dead = true;
  s.asleep = false;
  dirty = true;
  logEvent("%s passed away (%s)", s.name, why);
}

void Pet::clampAll() {
  s.hunger = clamp100(s.hunger);
  s.happiness = clamp100(s.happiness);
  s.energy = clamp100(s.energy);
  s.hygiene = clamp100(s.hygiene);
  s.health = clamp100(s.health);
  if (s.weight < 1) s.weight = 1;
  if (s.weight > 99) s.weight = 99;
}

// One simulated second of life.
void Pet::simulateSecond() {
  if (s.dead) return;
  if (s.stage == STAGE_EGG) {
    s.eggSec++;
    if (s.eggSec >= EGG_HATCH_SEC) hatch();
    return;
  }
  s.ageSec++;
  s.secSinceMeal++;
  s.secSinceRandom++;

  if (s.stage < STAGE_ELDER && s.ageSec >= STAGE_END_SEC[s.stage]) {
    s.stage++;
    if (s.stage == STAGE_ADULT) {
      // The adult form reflects how well the pet was looked after
      if (s.careMistakes <= 1 && s.health >= 80 && s.happiness >= 60) s.form = FORM_ELITE;
      else if (s.careMistakes >= 5 || s.health < 40) s.form = FORM_FERAL;
      else s.form = FORM_NORMAL;
      logEvent("%s evolved into a%s adult!", s.name, s.form == FORM_ELITE ? "n elite" : (s.form == FORM_FERAL ? " feral" : ""));
    } else {
      logEvent("%s grew into a %s", s.name, stageName());
    }
  }

  // Stat decay (per second, probabilistic to keep ints simple)
  uint32_t r = esp_random();
  if (s.asleep) {
    if ((r % 20) == 0) s.energy += 1;
    if ((r % 240) == 0) s.hunger -= 1;
    if ((r % 300) == 0) s.happiness -= 1;
    if (s.energy >= 100 && !s.lightsOff) {
      s.asleep = false;
      logEvent("%s woke up refreshed", s.name);
    }
  } else {
    if ((r % 90) == 0) s.hunger -= 1;
    if ((r % 120) == 0) s.happiness -= 1;
    if ((r % 150) == 0) s.energy -= 1;
    if ((r % 180) == 0) s.hygiene -= 1;
    if (s.poops > 0 && (r % 60) == 0) s.hygiene -= 1;
    // poop appears some minutes after a meal
    if (s.secSinceMeal > 600 && s.poops < 3 && (r % 900) == 0) mess();
    if (s.energy <= 10) {
      s.asleep = true;
      logEvent("%s fell asleep exhausted", s.name);
    }
  }

  // Sickness: dirty environment or starvation increase the odds
  if (!s.sick) {
    uint32_t odds = 0;
    if (s.poops >= 2) odds += 1;
    if (s.hygiene < 20) odds += 1;
    if (s.hunger < 10) odds += 1;
    if (odds && (esp_random() % (1800 / odds)) == 0) {
      s.sick = true;
      s.careMistakes++;
      logEvent("%s got sick", s.name);
    }
  }

  // Health
  if ((r % 30) == 0) {
    if (s.sick) s.health -= 1;
    if (s.hunger == 0) s.health -= 1;
    if (s.hygiene == 0) s.health -= 1;
    if (!s.sick && s.hunger > 40 && s.hygiene > 40 && s.happiness > 40) s.health += 1;
  }
  clampAll();
  if (s.health == 0) die(s.sick ? "illness" : (s.hunger == 0 ? "hunger" : "neglect"));
  dirty = true;
}

static const char *ACTION_NAMES[ACT_COUNT] = {"feed", "snack", "play", "pet", "clean", "medicine", "hatch"};
const char *Pet::actionName(PetAction a) { return a < ACT_COUNT ? ACTION_NAMES[a] : "?"; }
int8_t Pet::actionFromKey(const char *key) {
  if (!key) return -1;
  for (uint8_t i = 0; i < ACT_COUNT; i++) if (!strcmp(key, ACTION_NAMES[i])) return i;
  return -1;
}

bool Pet::runAction(PetAction a) {
  switch (a) {
    case ACT_FEED: return feed(false);
    case ACT_SNACK: return feed(true);
    case ACT_PLAY: return play();
    case ACT_PET: return pet();
    case ACT_CLEAN: return clean();
    case ACT_MEDICINE: return medicine();
    case ACT_HATCH: return hatch();
    default: return false;
  }
}

ReqResult Pet::request(PetAction a) {
  if (a >= ACT_COUNT) return REQ_REFUSED;
  if (!busy() && !qCount) return runAction(a) ? REQ_APPLIED : REQ_REFUSED;
  if (qCount >= PET_QUEUE_MAX) return REQ_FULL;
  queue[(qHead + qCount) % PET_QUEUE_MAX] = a;
  qCount++;
  return REQ_QUEUED;
}

uint32_t Pet::busyMs() const {
  if (animCur == ANIM_NONE) return 0;
  int32_t left = (int32_t)(animUntil - millis());
  return left > 0 ? (uint32_t)left : 0;
}

void Pet::tick(uint32_t nowEpoch) {
  simulateSecond();
  if (animCur != ANIM_NONE) {
    if (millis() > animUntil) {
      animCur = ANIM_NONE;
    } else {
      animStep++;
    }
  }
  if (animCur == ANIM_NONE && qCount) {  // next queued action, one per tick so each gets its full animation
    PetAction a = (PetAction)queue[qHead];
    qHead = (qHead + 1) % PET_QUEUE_MAX;
    qCount--;
    if (!runAction(a)) logEvent("Queued %s was refused", actionName(a));
    dirty = true;
  }
  if (nowEpoch) s.lastEpoch = nowEpoch;
  save(nowEpoch);
}

void Pet::catchUp(uint32_t nowEpoch) {
  if (!nowEpoch || !s.lastEpoch || nowEpoch <= s.lastEpoch) return;
  uint32_t elapsed = nowEpoch - s.lastEpoch;
  if (elapsed > 8UL * 3600) elapsed = 8UL * 3600;  // be merciful
  if (elapsed < 5) return;
  for (uint32_t i = 0; i < elapsed; i++) simulateSecond();
  logEvent("Caught up %lu min while off", (unsigned long)(elapsed / 60));
  s.lastEpoch = nowEpoch;
  dirty = true;
  save(nowEpoch, true);
}

void Pet::save(uint32_t nowEpoch, bool force) {
  if (!dirty) return;
  if (!force && millis() - lastSaveMs < SAVE_INTERVAL_MS) return;
  if (nowEpoch) s.lastEpoch = nowEpoch;
  prefs.putBytes("state", &s, sizeof(PetState));
  lastSaveMs = millis();
  dirty = false;
}

bool Pet::needsAttention() const {
  if (s.dead || s.stage == STAGE_EGG) return false;
  return s.sick || s.poops > 0 || s.hunger < 25 || s.happiness < 25 || s.hygiene < 25;
}

const char *Pet::moodWord() const {
  if (s.dead) return "gone";
  if (s.stage == STAGE_EGG) return "egg";
  if (s.sick) return "sick";
  if (s.asleep) return "sleeping";
  if (s.hunger < 25) return "hungry";
  if (s.happiness < 25) return "bored";
  if (s.hygiene < 25 || s.poops > 1) return "dirty";
  if (s.energy < 25) return "tired";
  if (s.happiness > 75 && s.hunger > 60) return "happy";
  return "okay";
}

const char *Pet::speciesKey() const { return SPECIES[s.species < SPECIES_COUNT ? s.species : 0].key; }

int8_t Pet::speciesFromKey(const char *key) {
  if (!key) return -1;
  for (uint8_t i = 0; i < SPECIES_COUNT; i++)
    if (!strcasecmp(key, SPECIES[i].key)) return i;
  return -1;
}

const char *Pet::stageName() const {
  static const char *names[STAGE_COUNT] = {"egg", "baby", "child", "teen", "adult", "elder"};
  return names[s.stage < STAGE_COUNT ? s.stage : STAGE_ADULT];
}

const char *Pet::formName() const {
  static const char *names[] = {"normal", "elite", "feral"};
  return names[s.form < 3 ? s.form : 0];
}

uint32_t Pet::nextEvolutionSec() const {
  if (s.dead) return 0;
  if (s.stage == STAGE_EGG) return s.eggSec >= EGG_HATCH_SEC ? 0 : EGG_HATCH_SEC - s.eggSec;
  if (s.stage >= STAGE_ELDER) return 0;
  uint32_t end = STAGE_END_SEC[s.stage];
  return s.ageSec >= end ? 0 : end - s.ageSec;
}

uint8_t Pet::spriteSlot() const {
  switch (s.stage) {
    case STAGE_BABY: return 0;
    case STAGE_CHILD: return 1;
    case STAGE_TEEN: return 2;
    case STAGE_ELDER: return 6;
    default: return s.form == FORM_ELITE ? 4 : (s.form == FORM_FERAL ? 5 : 3);
  }
}

void Pet::logEvent(const char *fmt, ...) {
  PetEvent &e = events[evHead];
  e.t = s.lastEpoch ? s.lastEpoch : millis() / 1000;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.msg, sizeof(e.msg), fmt, ap);
  va_end(ap);
  Serial.printf("[pet] %s\n", e.msg);
  evHead = (evHead + 1) % 16;
  if (evCount < 16) evCount++;
}

void Pet::toJson(JsonObject o) const {
  o["name"] = s.name;
  o["species"] = speciesKey();
  o["stage"] = stageName();
  o["form"] = formName();
  o["nextEvolutionSec"] = nextEvolutionSec();
  o["mood"] = moodWord();
  o["ageSec"] = s.ageSec;
  o["eggSec"] = s.eggSec;
  o["hunger"] = s.hunger;
  o["happiness"] = s.happiness;
  o["energy"] = s.energy;
  o["hygiene"] = s.hygiene;
  o["health"] = s.health;
  o["weight"] = s.weight;
  o["poops"] = s.poops;
  o["sick"] = s.sick;
  o["asleep"] = s.asleep;
  o["lightsOff"] = s.lightsOff;
  o["dead"] = s.dead;
  o["careMistakes"] = s.careMistakes;
  o["generation"] = s.generation;
  o["needsAttention"] = needsAttention();
  const char *animNames[] = {"none", "eat", "snack", "play", "pet", "clean", "heal", "hatch"};
  o["anim"] = animNames[animCur];
  if (animCur == ANIM_EAT || animCur == ANIM_SNACK) o["animItem"] = animItemName();
  o["busy"] = busy();
  o["busyMs"] = busyMs();
  o["queued"] = qCount;
}

void Pet::eventsToJson(JsonArray a) const {
  for (uint8_t i = 0; i < evCount; i++) {
    uint8_t idx = (evHead + 16 - 1 - i) % 16;  // newest first
    JsonObject e = a.add<JsonObject>();
    e["t"] = events[idx].t;
    e["msg"] = events[idx].msg;
  }
}
