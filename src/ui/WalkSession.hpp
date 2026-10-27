#pragma once
#include <array>
#include <cstdint>
#include <utility>
#include "render/TorchFlicker.hpp"
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "audio/Cues.hpp"
#include "core/Rng.hpp"
#include "core/Tuning.hpp"
#include "game/Combat.hpp"
#include "game/Fountain.hpp"
#include "game/Dungeon.hpp"
#include "game/Hero.hpp"
#include "game/Store.hpp"
#include "game/Item.hpp"
#include "game/LevelTracker.hpp"
#include "game/Persist.hpp"
#include "game/RunSummary.hpp"
#include "game/WallView.hpp"
#include "game/Walker.hpp"
#include "platform/Theme.hpp"
#include "render/Backend.hpp"
#include "render/CellGrid.hpp"
#include "ui/SystemMenu.hpp"
#include "ui/TitleScreen.hpp"
#include "ui/ViewPainter.hpp"

namespace laby {

// A walkable dungeon: the generated floor, the player, the log, and everything needed to draw the first-person view.
// Used by `labyterm --walk` and by the headless walk_dump tool.
class WalkSession {
 public:
  explicit WalkSession(std::uint64_t seed);

  void newDungeon(std::uint64_t seed);
  // The start screen (rain and lightning, then the character creation screen) comes before the game: a session starts in the game itself
  // (headless tools and tests rely on that), and the app calls startTitle() or newRun().
  void startTitle();
  void newRun(std::uint64_t seed) { newDungeon(seed); startTitle(); }
  // Where saves, the ghost and the scores are kept (empty: nothing is written or read, as in the headless tools and tests).
  void setStateDir(const std::string& dir);
  // Records how the game was started (the terminal or the window) and, until both have been used, sets the start screen's line about the other way.
  // LABYTERM_NO_TIPS=1 (for scripted sessions) shows no line and records nothing.
  void noteLaunchMode(bool terminal);
  bool hasSave() const { return hasSave_; }
  bool onLegends() const { return mode_ == Mode::Legends; }
  const std::vector<ScoreEntry>& legends() const { return scores_; }
  const std::optional<Ghost>& ghost() const { return ghost_; }
  // Town sign post: writes the save and goes back to the title. False if it could not be written (or you are not at the sign post).
  bool saveAndExit();
  bool onTitle() const { return mode_ == Mode::Title; }
  bool onCreation() const { return mode_ == Mode::Creation; }
  bool inGame() const { return mode_ == Mode::Playing; }
  // The name field of the creation screen is taking typed characters (Text events go to textInput, Backspace and Enter to handle).
  bool onStormScreen() const { return mode_ != Mode::Playing; }  // title, creation, game over, legends: the storm animates
  bool editingText() const { return nameEditing_; }
  void textInput(char32_t ch);
  bool levelingUp() const { return perk_; }
  // Step forward / back, turn (or, in a fight, pick and use an action). Updates the log. `ch` is the lowercase
  // letter of the key, if any (combat hotkeys). Returns false for keys it does not handle.
  bool handle(Key key, char32_t ch = 0, bool repeat = false);
  const Monster* fightMonster() const { return combat_ ? &combat_->monster() : nullptr; }
  // For tests: the hero dies in a fight right now (the game-over screen, the ghost and the score follow).
  void debugEndRun() { debugFight(); if (combat_) { hero_.hp = 0; hero_.score = std::max(hero_.score, 1234); endRun(); } }
  bool inCombat() const { return combat_.has_value(); }
  bool atChest() const { return chest_.has_value(); }
  bool inElevator() const { return elevator_.has_value(); }
  int elevatorLevel() const { return elevator_ ? elevator_->level : -1; }  // the floor the ride would take you to (0 = town)
  bool inTown() const { return inTown_; }
  // The secret door: sliding open or shut by itself (keys wait), or open with its fountain on offer (Left/Right picks Drink or Pass, Enter/Z selects, Esc/X closes it).
  bool secretMoving() const { return secret_ && secret_->phase != SecretDoor::Phase::Offer; }
  bool atFountain() const { return secret_ && secret_->phase == SecretDoor::Phase::Offer; }
  FountainEffect fountainEffect() const { return fountain_.effect(); }
  bool alarmOn() const { return alarm_; }
  int pityLeft() const { return pityLeft_; }                  // steps still free of random encounters
  void debugSetPity(int steps) { pityLeft_ = steps; }
  bool invisible() const { return hero_.invisible; }
  bool atPortal() const { return portal_; }
  bool menuOpen() const { return menu_ != Menu::None || sys_.open() || mode_ != Mode::Playing; }  // a sub-menu (Scrolls, Inventory) or the Esc menu is showing
  // Esc menu: waiting for a key to bind (send it raw with captureKey), and whether Quit was chosen.
  bool capturing() const { return sys_.capturing(); }
  void captureKey(Key key, char32_t ch) { sys_.captureKey(key, ch); }
  bool quitRequested() const { return sys_.quitRequested(); }
  int floor() const { return dungeon_.floor(); }
  const LevelTracker& levels() const { return tracker_; }
  bool dead() const { return dead_; }
  bool gameOver() const { return mode_ == Mode::GameOver; }
  const std::optional<RunSummary>& summary() const { return summary_; }  // set once the run has ended
  const Hero& hero() const { return hero_; }
  // For tests and demos: puts the player on a floor tile next to the first tile of class `t` (e.g. the boss or the
  // level chest), facing it. False if there is none.
  bool debugGoNextTo(TileClass t);
  void debugTeleportToRoom(int roomId) { teleportToRoom(roomId); }  // the fountain's teleport, to a chosen room
  const Dungeon& dungeon() const { return dungeon_; }
  void debugExplore(int steps);  // marks what a walk of `steps` tiles out from here would have seen (for map screenshots)
  // For tests: starts a fight where you stand (boss or not).
  // `name` (if given and known) swaps the monster's kind and art, keeping the generated level and stats.
  void debugFight(bool boss = false, const std::string& name = "") { debugMonster_ = name; startCombat(boss); debugMonster_.clear(); }
  // For tests and demos: gives the hero one of every scroll and a random item in every slot (level 5), 500 gold and two charmees, and levels them up a bit.
  void debugGiveKit();
  // For tests and demos: gives the hero enough experience for the next level and shows the perk screen.
  void debugLevelUp() { hero_.experience = Hero::expNeeded(hero_.level); nextPerk(); }
  // For tests and demos: puts the hero in town.
  void debugGoToTown() { goToTown(); }
  void debugPortal() { portal_ = true; portalSel_ = 0; }  // the boss reward's town portal screen
  void debugPotionChest(bool health) {  // an unopened chest holding an instant potion
    Treasure tr;
    tr.item = emptyItem(health ? EquipmentType::Health_Potion : EquipmentType::Mana_Potion);
    startChest(std::move(tr), false);
  }
  void debugScrollChest(int index) {  // an unopened chest holding scroll number `index` (0 = the first scroll)
    Treasure tr;
    const auto& all = ItemData::get().scrolls;
    tr.item = all[static_cast<size_t>(index) % all.size()];
    startChest(std::move(tr), false);
  }
  void debugScrollArt(int variant) { scrollArt_ = variant; }  // which scroll picture the chest panel shows
  // For tests: the hero, to set up a death.
  Hero& debugHero() { return hero_; }
  // For tests: gets into the elevator if you are standing on its tile.
  void debugRideElevator();
  // For tests: wins the fight on the spot (XP, loot and the chest as usual), like the DEBUG key W but in every build.
  void debugWinFight() { if (combat_) { combat_->debugKill(); endCombat(); } }
  // For tests: how many tiles of this class the floor has (the player's own tile counts as what they stand on).
  int debugCountTiles(TileClass t) const;
  // Draws the whole main screen (view, minimap, HUD, log) into `grid`, which is kGridCols x kGridRows.
  // Advances the camera's glide between steps (the first call turns the glide on); true while it is still moving, so the
  // loop should keep drawing. A forward step starts a cell behind and glides in; a random encounter stops it half a cell
  // short, the fight and the chest happen there, and the step is finished afterwards.
  bool tick(std::uint64_t nowMs);
  // The torch (darkness at the picture's edges, flickering now and then): off while the window is not focused, and then it costs no wake-ups.
  void setFocused(bool on) { focused_ = on; torchDirty_ = true; }
  std::uint64_t torchWakeMs() const { return torch_.msUntilNext(now_); }  // how long the loop may sleep before the torch needs it
  bool takeTorchDirty() { return std::exchange(torchDirty_, false); }     // the torch picture changed: draw again
  // After a torch-only change (the breathing), brings an already drawn grid up to date without drawing it all again.
  // False if the picture itself changed too (a flicker frame can reveal a secret door): draw() it then.
  bool updateTorchOnly(CellGrid& grid) const {
    if (!grid.torch().on || (focused_ && torchWanted()) == false || drawnTell_ != (focused_ && torchWanted() && torch_.flickering())) return false;
    CellGrid::Torch& t = grid.torch();
    t.shift = torch_.shift();
    t.pulse = torch_.pulse();
    t.breath = torch_.breath();
    return true;
  }
  void debugTorchBreath(double seconds) { torch_.forceBreath(seconds); }  // the breathing as it is `seconds` into its cycle
  void debugTorch(int frame) { torch_.force(frame); }                      // show flicker frame n (0 = calm), for previews
  void draw(CellGrid& grid, const Theme& theme, const std::string& statusTag) const;

  // The sounds and music changes since the last call, in order (for AudioPlayer; headless runs can ignore them).
  AudioEvents takeAudio() { AudioEvents e; e.swap(audio_); return e; }

  std::uint64_t seed() const { return seed_; }
  const Walker& player() const { return me_; }

 private:
  friend struct BalanceProbe;  // tests/balance_bot.cpp: the headless balance bot reads session state through one adapter
  void say(const std::string& s);
  bool handleKey(Key key, char32_t ch);  // handle() without the music check
  void sfx(Sfx s, int arg = -1, int delayMs = 0) { audio_.push_back({AudioEvent::Kind::Play, s, Music::None, arg, delayMs}); }
  Music wantedMusic() const;
  int bonkPitch() const;
  bool tickGlide(std::uint64_t nowMs);
  void syncMusic();
  void elevatorCranked();
  void startCombat(bool boss);
  void beginCombat(Monster m, bool boss);
  // The secret door and its fountain: the door slides open by itself (it takes the player's step into it), then the fountain is offered.
  void openSecret();
  bool tickSecret(std::uint64_t nowMs);
  void finishSecretOpening();
  void secretKey(Key key, char32_t ch);
  void drinkFountain();
  void teleportToRoom(int roomId);
  void closeSecret();
  void startShadowFight();
  int encounterPercent() const;
  void combatKey(Key key, char32_t ch);
  void useAction(CombatAction a);
  void endCombat();
  void endRun();
  void gameOverKey(Key key);
  void legendsKey(Key key);
  bool continueGame();
  struct TitleItem { std::string label; int id; };
  std::vector<TitleItem> titleItems() const;
  void startChest(Treasure tr, bool levelChest);
  void chestKey(Key key);
  void finishChest();
  enum class Menu { None, Scrolls, Inventory };
  void openMenu(Menu m);
  void titleKey(Key key);
  void creationKey(Key key);
  void beginGame();
  void nextPerk();
  void perkKey(Key key);
  void buildPerkPanel(MenuPanel& m) const;
  enum class Mode { Playing, Title, Creation, GameOver, Legends };
  void menuKey(Key key, char32_t ch);
  void castScroll(const Item& scroll);
  void invisibilityTick();
  std::string scrollBlocked(const Item& scroll) const;  // why it can't be cast now, or empty
  int menuCount() const;
  void startElevator();
  void elevatorKey(Key key);
  void elevatorTick(bool up);
  void leaveElevator();
  void newFloor(int floor);
  void rememberFloor() { memory_[dungeon_.floor()] = dungeon_.remember(me_.x, me_.y, me_.standingOn); }
  XY buildFloor(int floor);  // generates it from its own seed, then lays the remembered state over it
  void placeAtElevator();
  void goToTown();
  void townKey(Key key, char32_t ch);
  bool buyItem();
  void sellCharmee();
  void buildTownGuide(std::vector<OverlayLine>& g) const;
  void buildTownKeys(std::vector<OverlayLine>& g) const;
  // Esc steps back out of where you are (a shop or its item page, the fountain's offer, the town portal's question) instead of opening the
  // Esc menu; the menu opens only where Esc has nothing to back out of.
  bool escBacksOut() const {
    if (mode_ == Mode::Legends && legendsFromTitle_) return true;
    if (mapOpen_) return true;
    if (portal_ && !combat_ && !chest_) return true;
    if (secret_) return secret_->phase == SecretDoor::Phase::Offer;
    return inTown_ && !elevator_ && (spot_ != TownSpot::Sign || shopDetail_);
  }
  void buildTownLabels(std::vector<SceneLabel>& out, int vpCols, int vpRows, bool term) const;
  void portalKey(Key key, char32_t ch);
  void bossBeaten(bool fled);
  void afterRewards(bool bossReward);
  std::shared_ptr<const Image8> monsterImage(const std::string& originalPath) const;

  std::uint64_t seed_;
  WallView wallView_;
  ViewPainter painter_;
  mutable Dungeon dungeon_;  // drawing reads tiles with getTileType(), which marks them seen, as in the original
  mutable Walker me_;
  std::string debugMonster_;
  bool bossIntro_ = false;         // the boss-encounter animation (27 frames, 100 ms each) is playing instead of the view
  std::uint64_t introStart_ = 0;   // when it began (0: at the next tick)
  int introFrame_ = 0;
  bool tickIntro(std::uint64_t nowMs);
  bool animate_ = false;           // the glide is on (tick() has been called)
  int scrollArt_ = 0;              // the scroll chest panel's picture (ScrollArt variant)
  int stepDir_ = 0;                // +1 / -1: the last move was a step forward / back and its glide is not finished
  double lead_ = 0;                // how far the eye lags behind the player now, in cells (+ behind, - ahead)
  double leadFrom_ = 0, leadTo_ = 0, leadDur_ = 1;
  std::uint64_t leadStart_ = 0;
  double leadTarget() const;
  bool wallAhead() const;
  void stopGlide();
  Rng rng_;
  Hero hero_;
  std::optional<Combat> combat_;
  std::optional<Treasure> pending_;  // generated when the fight starts, as in the original
  struct Chest {
    Treasure tr;
    bool levelChest = false, open = false, bossChest = false;
    int pick = 1;        // equipment: 0 = keep what you wear, 1 = take the new item
    int restored = 0;    // potions: how much was restored when the chest was opened
    std::uint64_t openedAt = 0;  // when it was opened (the orb's clock)
  };
  std::optional<Chest> chest_;
  struct Elevator { int level = 1; };
  std::optional<Elevator> elevator_;  // riding the elevator: `level` is the floor it would take you to (0 = town)
  LevelTracker tracker_;
  Fountain fountain_;
  struct SecretDoor {
    enum class Phase { Opening, Offer, Closing } phase = Phase::Opening;
    std::uint64_t start = 0;  // when the slide began (0: at the next tick)
    double slide = 0;         // 0 = shut .. 1 = open
  };
  std::optional<SecretDoor> secret_;  // a secret door is opening, offering its fountain, or closing
  bool alarm_ = false;                // a fountain's alarm sounded: more monsters until the next floor
  std::array<std::uint64_t, kFloorSeeds> floorSeeds_{};  // each floor's own seed: its layout never changes within a run
  std::map<int, FloorMemory> memory_;                    // what the floors left earlier remember (the current one is in dungeon_)
  std::uint64_t floorSeed(int floor) const { return floorSeeds_[static_cast<size_t>(floor - 1) % floorSeeds_.size()]; }
  bool portal_ = false, inTown_ = false, onMap_ = false;
  enum class TownSpot { Sign, Temple, Blacksmith, BeastMart };
  TownSpot spot_ = TownSpot::Sign;  // where in town you are
  int townSel_ = 0;                  // the highlighted row there
  bool shopDetail_ = false;          // Blacksmith: the chosen item's page is open (Enter buys, Esc goes back to the list)
  bool sellNo_ = false;              // BeastMart's question: "No" is the highlighted button
  bool sellArmed_ = false;           // BeastMart: the charmee on offer waits for a second Enter
  std::vector<Item> shop_;           // the Blacksmith's stock (empty until first visited)
  Mode mode_ = Mode::Playing;
  int titleSel_ = 0, creationSel_ = 0;
  bool nameEditing_ = false;
  bool nameFresh_ = false;  // the field was just focused: the first typed character replaces the old name
  bool perk_ = false;            // a level-up perk waits to be chosen
  int perkSel_ = 0;
  bool portalAfterPerk_ = false; // a boss's reward is done: the town portal appears after the perk
  Menu menu_ = Menu::None;
  SystemMenu sys_;
  bool mapOpen_ = false;  // the full-floor map is showing over the view (M)
  bool esp_ = false;     // ESP was cast: the monster's numbers are showing (any key closes them)
  int subSel_ = 0;       // the highlighted row of the open sub-menu
  int menuSel_ = 0;
  int portalSel_ = 0;    // the town portal question's highlighted button: 0 Town, 1 Stay
  int fountainSel_ = 0;  // the fountain offer's highlighted button: 0 Drink, 1 Pass
  bool lockPending_ = false;  // a fight just started: the input lockout begins at the next tick
  std::uint64_t inputLockUntil_ = 0;
  int specialSeen_ = 0;               // Combat::lastSpecial().seq already announced
  std::uint64_t specialAt_ = 0;       // when the banner for the latest special appears (0: none)
  int specialKind_ = 0;
  void noteSpecial();
  bool dying_ = false;                // the hero just died: the fight stays on screen under a "YOU DIED" banner until a key press
  std::uint64_t dyingAt_ = 0;         // when the banner appeared (a short input guard follows)
  void dyingKey(Key key);
  static constexpr std::uint64_t kBannerMs = 1400;
  bool repeat_ = false;  // the key being handled is an auto-repeat
  mutable std::uint64_t viewKey_ = 0;  // the traced view is kept while nothing it shows changes (the torch redraws the picture many times a second)
  mutable ViewLayers viewCache_;
  mutable std::uint64_t cartKey_ = 0;  // the elevator cart is traced once (it never changes) and kept
  mutable ViewLayers cartCache_;
  ViewLayers elevatorPicture(int pw, int ph, int density, std::uint64_t nowMs) const;  // the ride: the cart over the scrolling shaft
  TorchFlicker torch_;
  mutable bool lairGlow_ = false;  // a Boss10's lair is in view (it glows red and breathes)
  mutable bool drawnTell_ = false;  // whether the last draw() showed the secret-door tell
  bool focused_ = true, torchDirty_ = false;
  bool torchWanted() const { return mode_ == Mode::Playing && (onMap_ || elevator_) && (!inTown_ || elevator_) && !dead_; }
  bool dead_ = false;
  std::string modeHint_;               // see noteLaunchMode
  std::string stateDir_;               // see setStateDir
  bool hasSave_ = false;
  bool rebuildFloor_ = false;          // a restored game has no floor built (dungeon_ is stale): the next ride down builds one even if it is the same number
  bool legendsFromTitle_ = false;      // the leaderboard was opened from the title (else from the game-over screen)
  std::optional<Ghost> ghost_;         // a dead hero from an earlier run, waiting on their floor
  std::vector<ScoreEntry> scores_;     // the top 10
  std::optional<RunSummary> summary_;  // the finished run (dead_ and Mode::GameOver go with it)
  int pityLeft_ = 0;  // steps still guaranteed free of random encounters (pity timer; used by release builds too)
#ifdef LABYTERM_DEBUG_KEYS
  bool noSpawns_ = false;  // DEBUG [G]: no random monster encounters
#endif
  XY start_;
  mutable std::map<std::string, std::shared_ptr<const Image8>> images_;
  std::vector<std::string> log_;

  AudioEvents audio_;
  Music music_ = Music::None;           // what the music was last told to be
  std::uint64_t now_ = 0, elevatorRunUntil_ = 0;
  bool hpHurt_ = false;  // took damage since the last key press: the HP bar stays red
  int hurtFrom_ = 0;  // the HP before the damage taken since the last key press
  int lastHp_ = 0;  // the HP seen on the previous frame, to notice damage
  bool elevatorMusicRuns_ = false;
  std::uint64_t elevatorAnimUntil_ = 0, elevatorAnimLast_ = 0;  // the shaft animates until 2 s after the last Up/Down
  double elevatorScroll_ = 0;   // the shaft's scroll (ShaftArt, picture units): grows going up, falls going down
  double elevatorShakeMs_ = 0;  // the cart's shake clock: runs only while the cage is moving
  bool elevatorUp_ = false;
  std::mt19937 ambientRng_{std::random_device{}()};  // random sounds only: never the game's seeded Rng
};

}  // namespace laby
