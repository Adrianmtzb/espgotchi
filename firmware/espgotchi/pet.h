#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum PetStage : uint8_t { STAGE_EGG = 0, STAGE_BABY, STAGE_CHILD, STAGE_TEEN, STAGE_ADULT, STAGE_ELDER, STAGE_COUNT };
enum PetForm : uint8_t { FORM_NORMAL = 0, FORM_ELITE, FORM_FERAL };
enum PetAnim : uint8_t { ANIM_NONE = 0, ANIM_EAT, ANIM_SNACK, ANIM_PLAY, ANIM_PET, ANIM_CLEAN, ANIM_HEAL, ANIM_HATCH };

struct PetState {
  uint32_t magic;
  uint16_t version;
  char name[16];
  uint8_t species;      // index into SPECIES[]
  uint8_t stage;
  uint8_t form;          // decided when becoming an adult
  uint32_t ageSec;       // seconds alive since hatching (egg time counts separately)
  uint32_t eggSec;       // seconds spent as an egg
  int16_t hunger;        // 0..100, 100 = full
  int16_t happiness;     // 0..100
  int16_t energy;        // 0..100
  int16_t hygiene;       // 0..100
  int16_t health;        // 0..100
  int16_t weight;        // grams-ish, cosmetic
  uint8_t poops;         // 0..3 on screen
  bool sick;
  bool asleep;
  bool lightsOff;        // user forced lights off (sleep mode)
  bool dead;
  uint16_t careMistakes;
  uint16_t generation;
  uint32_t lastEpoch;    // unix time of last save (0 if no NTP yet)
  uint32_t secSinceMeal;
  uint32_t secSinceRandom;
};

struct PetEvent {
  uint32_t t;  // millis/1000 or epoch when available
  char msg[40];
};

class Pet {
 public:
  void begin();
  void tick(uint32_t nowEpoch);  // call once per second
  void save(uint32_t nowEpoch, bool force = false);
  void catchUp(uint32_t nowEpoch);  // simulate offline time after boot

  // actions, return false if not allowed right now
  bool feed(bool snack);
  bool play();
  bool pet();  // a little affection: small happiness boost, works even when sleepy
  bool clean();
  bool toggleLights();
  bool medicine();
  bool hatch();
  void reset(int8_t species = -1);  // -1 = random
  void setName(const char *n);

  const PetState &state() const { return s; }
  PetAnim anim() const { return animCur; }
  uint8_t animFrame() const { return animStep; }
  uint8_t animItem() const { return animItemIdx; }  // index into MEALS / SNACKS while eating
  const char *animItemName() const;
  bool needsAttention() const;
  const char *moodWord() const;
  const char *stageName() const;
  const char *formName() const;
  uint32_t nextEvolutionSec() const;  // 0 if none
  uint8_t spriteSlot() const;         // index into SpeciesInfo::frames
  const char *speciesKey() const;
  static int8_t speciesFromKey(const char *key);
  void toJson(JsonObject o) const;
  void eventsToJson(JsonArray a) const;
  void logEvent(const char *fmt, ...);

 private:
  PetState s{};
  PetAnim animCur = ANIM_NONE;
  uint8_t animStep = 0;
  uint8_t animItemIdx = 0;
  uint32_t animUntil = 0;
  bool dirty = false;
  uint32_t lastSaveMs = 0;
  PetEvent events[16]{};
  uint8_t evHead = 0, evCount = 0;

  void startAnim(PetAnim a, uint32_t ms);
  void simulateSecond();
  void clampAll();
  void die(const char *why);
};
