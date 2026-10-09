#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum PetStage : uint8_t { STAGE_EGG = 0, STAGE_BABY, STAGE_CHILD, STAGE_TEEN, STAGE_ADULT, STAGE_ELDER, STAGE_COUNT };
enum PetForm : uint8_t { FORM_NORMAL = 0, FORM_ELITE, FORM_FERAL };
enum PetAnim : uint8_t { ANIM_NONE = 0, ANIM_EAT, ANIM_SNACK, ANIM_PLAY, ANIM_PET, ANIM_CLEAN, ANIM_HEAL, ANIM_HATCH, ANIM_VISIT };
// Animated care actions. They go through Pet::request(), which runs them now or queues them
// behind the animation in progress so two quick requests don't cut each other short.
enum PetAction : uint8_t { ACT_FEED = 0, ACT_SNACK, ACT_PLAY, ACT_PET, ACT_CLEAN, ACT_MEDICINE, ACT_HATCH, ACT_COUNT };
enum ReqResult : uint8_t { REQ_APPLIED, REQ_REFUSED, REQ_QUEUED, REQ_FULL };
static const uint8_t PET_QUEUE_MAX = 4;

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
  bool mess();  // force a poop (CLI, to test the sound and the attention state)
  // A pet from another board drops by for VISIT_DURATION_MS: fun +15, the friend is drawn next to
  // the pet. False while the pet is an egg, dead, asleep or busy with another animation.
  bool visitFrom(const char *name, uint8_t species, uint8_t slot);
  bool hasVisitor() const { return animCur == ANIM_VISIT; }
  const char *visitorName() const { return visitor.name; }
  uint8_t visitorSpecies() const { return visitor.species; }
  const char *visitorSpeciesKey() const;
  uint8_t visitorSlot() const { return visitor.slot; }
  // Run `a` now, or queue it while an animation plays. The queue drains one action per tick
  // once the screen is free; a refused queued action still logs why.
  ReqResult request(PetAction a);
  // A visit does not count as busy: a care action simply sends the friend home early.
  bool busy() const { return animCur != ANIM_NONE && animCur != ANIM_VISIT; }
  uint32_t busyMs() const;             // time left on the current animation, 0 when idle
  uint8_t queued() const { return qCount; }
  static const char *actionName(PetAction a);
  static int8_t actionFromKey(const char *key);  // "feed", "snack"... or -1
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
  struct { char name[16]; uint8_t species; uint8_t slot; } visitor{};
  uint8_t queue[PET_QUEUE_MAX] = {};
  uint8_t qHead = 0, qCount = 0;
  bool runAction(PetAction a);
  bool dirty = false;
  uint32_t lastSaveMs = 0;
  PetEvent events[16]{};
  uint8_t evHead = 0, evCount = 0;

  void startAnim(PetAnim a, uint32_t ms);
  void simulateSecond();
  void clampAll();
  void die(const char *why);
};
