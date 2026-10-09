#!/usr/bin/env node
// Cross-checks the lists that must stay in sync between the firmware, the web
// panel, the landing simulator and the MCP server. Pure Node, no dependencies.
//
//   node tools/check_consistency.mjs [--root <dir>]
//
// It parses the sources instead of relying on a shared manifest because the
// firmware is C++ and the other three are JS/HTML: whoever adds a menu entry,
// a species or an action to one side gets a clear message naming the others.
//
// Checks:
//   1. Device menu:   enum MenuItem (ui.h)  ==  const MENU (docs/index.html)
//   2. Species:       shared/sprites.json   ==  SPECIES/SPECIES_EMOJI (mcp/server.mjs),
//                     landing blurbs, web/landing pickers data-driven, seasons known
//   3. Actions:       ACTION_NAMES (pet.cpp) + sleep/lights/reset (net.cpp)  vs
//                     web data-action buttons, MCP tools and the landing simulator
import { readFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
const rootIdx = args.indexOf('--root');
const ROOT = rootIdx >= 0 ? resolve(args[rootIdx + 1]) : resolve(dirname(fileURLToPath(import.meta.url)), '..');

const FILES = {
  uiH: 'firmware/espgotchi/ui.h',
  petCpp: 'firmware/espgotchi/pet.cpp',
  netCpp: 'firmware/espgotchi/net.cpp',
  sprites: 'shared/sprites.json',
  web: 'web/index.html',
  landing: 'docs/index.html',
  mcp: 'mcp/server.mjs',
};

const errors = [];
const fail = (msg) => errors.push(msg);
const read = (key) => readFileSync(resolve(ROOT, FILES[key]), 'utf8');

// The menu enum names the lights entry MENU_SLEEP and the medicine one MENU_MEDS;
// the landing uses the action names the simulator understands.
const MENU_ALIASES = { sleep: 'lights', meds: 'medicine' };
// The web panel sends "sleep" for the lights toggle; the firmware accepts both.
const ACTION_ALIASES = { sleep: 'lights' };

const strList = (src, re, what) => {
  const m = src.match(re);
  if (!m) throw new Error(`could not find ${what}`);
  return [...m[1].matchAll(/["']([^"']+)["']/g)].map((x) => x[1]);
};
const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
const setDiff = (a, b) => a.filter((x) => !b.includes(x));
const fmt = (list) => `[${list.join(', ')}]`;

function compareOrdered(label, expected, expectedFrom, actual, actualFrom) {
  if (same(expected, actual)) return;
  const missing = setDiff(expected, actual), extra = setDiff(actual, expected);
  let why = '';
  if (missing.length) why += ` missing ${fmt(missing)}`;
  if (extra.length) why += ` unexpected ${fmt(extra)}`;
  if (!missing.length && !extra.length) why = ' same entries, different order';
  fail(`${label}: ${actualFrom} has ${fmt(actual)} but ${expectedFrom} has ${fmt(expected)};${why}`);
}

// ---- 1. device menu vs landing simulator ----
function checkMenu() {
  const uiH = read('uiH');
  const m = uiH.match(/enum\s+MenuItem[^{]*\{([^}]*)\}/);
  if (!m) throw new Error(`enum MenuItem not found in ${FILES.uiH}`);
  // Entries guarded by a preprocessor block (#if HAS_TOUCH ... #endif) only exist on some boards;
  // the landing simulates the base board, so they are dropped before comparing.
  const body = m[1].replace(/^[ \t]*#if[^\n]*\n[\s\S]*?^[ \t]*#endif[^\n]*\n?/gm, '');
  const firmware = body.split(',').map((s) => s.trim().replace(/=.*$/, '').trim()).filter(Boolean)
    .filter((n) => n !== 'MENU_COUNT')
    .map((n) => {
      if (!n.startsWith('MENU_')) throw new Error(`unexpected enumerator ${n} in MenuItem`);
      const key = n.slice(5).toLowerCase();
      return MENU_ALIASES[key] ?? key;
    });
  const landing = strList(read('landing'), /const\s+MENU\s*=\s*\[([^\]]*)\]/, `const MENU in ${FILES.landing}`);
  compareOrdered('device menu', firmware, `${FILES.uiH} (enum MenuItem)`, landing, `${FILES.landing} (const MENU)`);
  return firmware;
}

// ---- 2. species ----
function checkSpecies() {
  const sheet = JSON.parse(read('sprites'));
  const species = Object.keys(sheet.species);
  const seasons = [...new Set(Object.values(sheet.species).map((s) => s.season).filter(Boolean))];

  const mcp = read('mcp');
  const mcpList = strList(mcp, /const\s+SPECIES\s*=\s*\[([^\]]*)\]/, `const SPECIES in ${FILES.mcp}`);
  compareOrdered('species', species, FILES.sprites, mcpList, `${FILES.mcp} (const SPECIES)`);
  const emojiBlock = mcp.match(/const\s+SPECIES_EMOJI\s*=\s*\{([^}]*)\}/);
  if (!emojiBlock) fail(`${FILES.mcp}: const SPECIES_EMOJI not found`);
  else {
    const keys = [...emojiBlock[1].matchAll(/(\w+)\s*:/g)].map((x) => x[1]);
    compareOrdered('species', species, FILES.sprites, keys, `${FILES.mcp} (SPECIES_EMOJI)`);
  }

  // The web picker and the landing tabs are built from /sprites.json at runtime,
  // so nothing can drift as long as they keep iterating the sheet. Make sure of it,
  // and that no literal species key outside the sheet sneaks in.
  const web = read('web');
  if (!/function\s+buildPicker[\s\S]*?Object\.entries\(sheet\.species\)/.test(web))
    fail(`${FILES.web}: buildPicker() no longer iterates sheet.species; the picker must be built from /sprites.json`);
  for (const [, key] of web.matchAll(/data-species="([^"]+)"/g))
    if (!species.includes(key)) fail(`${FILES.web}: data-species="${key}" is not a species in ${FILES.sprites}`);
  const webSeasons = read('web').match(/const\s+SEASONS\s*=\s*\{([\s\S]*?)\};/);
  if (!webSeasons) fail(`${FILES.web}: const SEASONS not found`);
  else {
    const known = [...webSeasons[1].matchAll(/(?:^|[{,])\s*(\w+)\s*:\s*\{/g)].map((x) => x[1]);
    for (const s of setDiff(seasons, known)) fail(`${FILES.web}: season "${s}" used in ${FILES.sprites} has no entry in SEASONS (its badge would not render)`);
  }

  const landing = read('landing');
  if (!/function\s+buildSpecies[\s\S]*?Object\.entries\(sheet\.species\)/.test(landing))
    fail(`${FILES.landing}: buildSpecies() no longer iterates sheet.species; the tabs must be built from sprites.json`);
  const blurbs = landing.match(/const\s+blurbs\s*=\s*\{([\s\S]*?)\};/);
  if (!blurbs) fail(`${FILES.landing}: const blurbs not found`);
  else {
    const keys = [...blurbs[1].matchAll(/(?:^|[{,])\s*(\w+)\s*:\s*\[/g)].map((x) => x[1]);
    compareOrdered('species', species, FILES.sprites, keys, `${FILES.landing} (blurbs)`);
  }
  const landingSeasons = landing.match(/const\s+SEASONS\s*=\s*\{([\s\S]*?)\};/);
  if (!landingSeasons) fail(`${FILES.landing}: const SEASONS not found`);
  else {
    const known = [...landingSeasons[1].matchAll(/(?:^|[{,])\s*(\w+)\s*:\s*\{/g)].map((x) => x[1]);
    for (const s of setDiff(seasons, known)) fail(`${FILES.landing}: season "${s}" used in ${FILES.sprites} has no entry in SEASONS (its badge would not render)`);
  }
  return species;
}

// ---- 3. action types ----
function checkActions(menu) {
  const petCpp = read('petCpp');
  const care = strList(petCpp, /ACTION_NAMES\s*\[[^\]]*\]\s*=\s*\{([^}]*)\}/, `ACTION_NAMES in ${FILES.petCpp}`);

  const netCpp = read('netCpp');
  const handler = netCpp.match(/server\.on\("\/api\/action"[\s\S]*?\n  \}\);/);
  if (!handler) throw new Error(`POST /api/action handler not found in ${FILES.netCpp}`);
  const literal = [...handler[0].matchAll(/strcmp\(type,\s*"([^"]+)"\)/g)].map((x) => x[1]);
  if (!handler[0].includes('actionFromKey(type)')) fail(`${FILES.netCpp}: /api/action no longer routes through Pet::actionFromKey (ACTION_NAMES)`);
  const firmware = [...care, ...literal];
  const canon = (t) => ACTION_ALIASES[t] ?? t;
  const accepted = (t) => firmware.includes(t) || firmware.includes(canon(t));

  // Web panel: every button must be accepted, every care action and the lights toggle must have a button.
  const webButtons = [...read('web').matchAll(/data-action="([^"]+)"/g)].map((x) => x[1]);
  for (const b of webButtons) if (!accepted(b)) fail(`${FILES.web}: button data-action="${b}" is not accepted by POST /api/action (${fmt(firmware)})`);
  for (const a of care) if (!webButtons.includes(a)) fail(`${FILES.web}: no button with data-action="${a}" although the firmware accepts it`);
  if (!webButtons.some((b) => canon(b) === 'lights')) fail(`${FILES.web}: no lights/sleep button`);

  // MCP: one tool per care action, plus toggle_lights -> lights and new_egg -> reset.
  const mcp = read('mcp');
  const simple = mcp.match(/const\s+simpleActions\s*=\s*\[([\s\S]*?)\n\];/);
  if (!simple) fail(`${FILES.mcp}: const simpleActions not found`);
  else {
    const tools = [...simple[1].matchAll(/^\s*\["([^"]+)"/gm)].map((x) => x[1]);
    for (const t of tools) if (!care.includes(t)) fail(`${FILES.mcp}: tool "${t}" is not an ACTION_NAMES entry in ${FILES.petCpp}`);
    for (const a of care) if (!tools.includes(a)) fail(`${FILES.mcp}: no tool for action "${a}" (add it to simpleActions)`);
  }
  for (const [, t] of mcp.matchAll(/doAction\("([^"]+)"/g))
    if (!accepted(t)) fail(`${FILES.mcp}: doAction("${t}") is not accepted by POST /api/action (${fmt(firmware)})`);
  for (const [tool, type] of [['toggle_lights', 'lights'], ['new_egg', 'reset']]) {
    if (!mcp.includes(`registerTool("${tool}"`)) fail(`${FILES.mcp}: tool "${tool}" not registered`);
    if (!firmware.includes(type)) fail(`${FILES.netCpp}: /api/action no longer accepts "${type}" (used by MCP tool ${tool})`);
  }

  // Landing simulator: every menu entry except info must be an action the firmware knows.
  for (const item of menu) if (item !== 'info' && !accepted(item)) fail(`device menu entry "${item}" is not an action accepted by POST /api/action`);
  return firmware;
}

let menu = [], species = [], actions = [];
try {
  menu = checkMenu();
  species = checkSpecies();
  actions = checkActions(menu);
} catch (e) {
  fail(e.message);
}

if (errors.length) {
  console.error(`check_consistency: ${errors.length} mismatch${errors.length > 1 ? 'es' : ''}\n`);
  for (const e of errors) console.error(`  - ${e}`);
  console.error('\nFirmware, web/index.html, docs/index.html and mcp/server.mjs must list the same menu entries, species and actions.');
  process.exit(1);
}
console.log(`consistency ok: ${menu.length} menu entries, ${species.length} species, ${actions.length} action types in sync across firmware, web, landing and MCP`);
