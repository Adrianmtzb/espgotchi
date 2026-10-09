#!/usr/bin/env node
// ESPgotchi MCP server: lets an AI assistant look after the pet through the
// board's HTTP API. Transport is stdio; the board address comes from --host,
// ESPGOTCHI_HOST or defaults to espgotchi.local.

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";

const REQUEST_TIMEOUT_MS = 5000;
const SPECIES = ["kawaii", "alien", "dino", "edge", "ghost", "pumpkin", "mimi", "momo", "pingo", "unicorn"];

// ---------------------------------------------------------------------------
// Board address

function resolveBaseUrl() {
  let host = process.env.ESPGOTCHI_HOST || "espgotchi.local";
  const argv = process.argv.slice(2);
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--host" && argv[i + 1]) host = argv[i + 1];
    else if (argv[i].startsWith("--host=")) host = argv[i].slice("--host=".length);
  }
  host = host.trim();
  if (!/^https?:\/\//i.test(host)) host = `http://${host}`;
  return host.replace(/\/+$/, "");
}

const BASE_URL = resolveBaseUrl();

// ---------------------------------------------------------------------------
// HTTP helpers

async function request(path, { method = "GET", body } = {}) {
  const url = `${BASE_URL}${path}`;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  let res;
  try {
    res = await fetch(url, {
      method,
      headers: body ? { "Content-Type": "application/json" } : undefined,
      body: body ? JSON.stringify(body) : undefined,
      signal: controller.signal,
    });
  } catch (err) {
    const why = err?.name === "AbortError"
      ? `no answer after ${REQUEST_TIMEOUT_MS / 1000} s`
      : (err?.cause?.message || err?.message || String(err));
    throw new Error(
      `ESPgotchi board unreachable at ${BASE_URL} (${why}). ` +
      `Check that the board is powered on and on the same network, or set ESPGOTCHI_HOST to its IP.`,
    );
  } finally {
    clearTimeout(timer);
  }
  if (!res.ok) {
    throw new Error(`ESPgotchi board at ${BASE_URL} answered HTTP ${res.status} for ${method} ${path}.`);
  }
  let data;
  try {
    data = await res.json();
  } catch {
    throw new Error(`ESPgotchi board at ${BASE_URL} returned a non-JSON answer for ${method} ${path}.`);
  }
  if (data && data.ok === false) {
    throw new Error(`ESPgotchi board rejected ${method} ${path}: ${data.error || "unknown error"}.`);
  }
  return data;
}

const getState = async () => (await request("/api/state")).pet;
const getEvents = async () => (await request("/api/events")).events ?? [];
const getInfo = async () => {
  const info = await request("/api/info");
  delete info.ok;
  return info;
};

// ---------------------------------------------------------------------------
// Caretaker logic

function adviceFor(pet) {
  const advice = [];
  if (!pet) return advice;
  if (pet.dead) {
    advice.push("The pet has died. Only start over with new_egg if the human agrees.");
    return advice;
  }
  if (pet.stage === "egg") {
    advice.push(
      pet.eggSec > 0
        ? `Still an egg, about ${pet.eggSec} s left before it can hatch. Just wait.`
        : "The egg is ready: call hatch.",
    );
    return advice;
  }
  if (pet.asleep) advice.push("The pet is asleep. Let it rest: feeding, playing and cleaning are refused until it wakes up (petting is fine).");
  if (pet.sick) advice.push("The pet is sick: give medicine.");
  if (pet.hunger < 40) advice.push(`Hungry (${pet.hunger}/100): feed it a meal.`);
  if (pet.happiness < 40) advice.push(`Unhappy (${pet.happiness}/100): play with it or pet it.`);
  if (pet.poops > 0) advice.push(`There ${pet.poops === 1 ? "is 1 poop" : `are ${pet.poops} poops`} on screen: clean.`);
  else if (pet.hygiene < 50) advice.push(`Dirty (${pet.hygiene}/100): clean it.`);
  if (pet.energy < 20 && !pet.asleep) advice.push(`Exhausted (${pet.energy}/100): turn the lights off so it can sleep.`);
  if (pet.lightsOff && !pet.asleep && pet.energy > 80) advice.push("Lights are off but it is awake and rested: turn the lights back on.");
  if (advice.length === 0) advice.push("Everything looks fine. Check again later.");
  return advice;
}

// Explain, from the state, why the board refused an action.
function refusalReason(action, pet) {
  if (!pet) return "Unknown reason.";
  if (pet.dead) return "The pet is dead. Only new_egg (with the human's consent) can start over.";
  if (action === "hatch") {
    if (pet.stage !== "egg") return "There is no egg to hatch; the pet is already born.";
    if (pet.eggSec > 0) return `The egg is not ready yet (about ${pet.eggSec} s left).`;
  }
  if (pet.stage === "egg") return "The pet is still an egg. Hatch it first (or wait until it is ready).";
  if (pet.asleep && action !== "lights" && action !== "pet") return "The pet is asleep. Let it rest or toggle the lights to wake it.";
  if (action === "feed" && pet.hunger >= 100) return "It is already full.";
  if (action === "snack" && pet.hunger >= 100) return "It is too full for a snack.";
  if (action === "play" && pet.energy < 10) return "It is too tired to play. Let it sleep first.";
  if (action === "clean" && pet.poops === 0 && pet.hygiene >= 100) return "It is already spotless.";
  if (action === "medicine" && !pet.sick) return "It is not sick; medicine is not needed.";
  if (action === "hatch") return "The egg could not hatch right now.";
  return "The board refused the action in the current state.";
}

async function doAction(action, extra = {}) {
  let data;
  try {
    data = await request("/api/action", { method: "POST", body: { type: action, ...extra } });
  } catch (e) {
    if (/429/.test(String(e.message))) {
      return { applied: false, queued: false, action, reason: "The board is busy and its action queue is full. Wait a few seconds and try again." };
    }
    throw e;
  }
  const pet = data.pet;
  const applied = Boolean(data.applied);
  const queued = Boolean(data.queued);
  const result = { applied, queued, action, pet, advice: adviceFor(pet) };
  if (queued) result.reason = `Queued at position ${data.position}: an animation is playing, it runs when the screen is free (about ${Math.ceil((pet.busyMs || 0) / 1000) + data.position} s). Check get_events to see whether it was applied.`;
  else if (!applied) result.reason = refusalReason(action, pet);
  return result;
}

// ---------------------------------------------------------------------------
// Text rendering

function bar(value, width = 10) {
  const v = Math.max(0, Math.min(100, Number(value) || 0));
  const filled = Math.round((v / 100) * width);
  return "▓".repeat(filled) + "░".repeat(width - filled);
}

function fmtAge(sec) {
  sec = Math.max(0, Number(sec) || 0);
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  if (d > 0) return `${d}d ${h}h`;
  if (h > 0) return `${h}h ${m}m`;
  return `${m}m`;
}

const MOOD_EMOJI = {
  happy: "😊", neutral: "😐", sad: "😢", angry: "😠", sick: "🤒", asleep: "😴", sleeping: "😴",
  hungry: "😋", dead: "💀", egg: "🥚", excited: "🤩", bored: "😑",
};
const SPECIES_EMOJI = { kawaii: "🐣", alien: "👾", dino: "🦖", edge: "🤖", ghost: "👻", pumpkin: "🎃", mimi: "🎀", momo: "🐰", pingo: "🐧", unicorn: "🦄" };

function renderScreen(pet) {
  const lines = [];
  const face = pet.dead ? "💀" : pet.stage === "egg" ? "🥚" : pet.asleep ? "😴" : (MOOD_EMOJI[pet.mood] ?? SPECIES_EMOJI[pet.species] ?? "🐾");
  lines.push(`${face} ${pet.name}  [${pet.species} · ${pet.stage}${pet.form ? ` · ${pet.form}` : ""}]  gen ${pet.generation ?? 1}`);
  lines.push(`Mood: ${pet.mood}   Age: ${fmtAge(pet.ageSec)}   Weight: ${pet.weight}`);
  if (pet.dead) {
    lines.push("", "The pet has died. ✝");
    return lines.join("\n");
  }
  if (pet.stage === "egg") {
    lines.push("", pet.eggSec > 0 ? `Hatching in ~${fmtAge(pet.eggSec)} (${pet.eggSec} s)` : "Ready to hatch! 🐣");
    return lines.join("\n");
  }
  lines.push("");
  lines.push(`Hunger    ${bar(pet.hunger)} ${String(pet.hunger).padStart(3)}`);
  lines.push(`Happiness ${bar(pet.happiness)} ${String(pet.happiness).padStart(3)}`);
  lines.push(`Energy    ${bar(pet.energy)} ${String(pet.energy).padStart(3)}`);
  lines.push(`Hygiene   ${bar(pet.hygiene)} ${String(pet.hygiene).padStart(3)}`);
  lines.push(`Health    ${bar(pet.health)} ${String(pet.health).padStart(3)}`);
  const flags = [];
  if (pet.asleep) flags.push("😴 asleep");
  if (pet.lightsOff) flags.push("🌙 lights off");
  if (pet.sick) flags.push("🤒 sick");
  if (pet.poops > 0) flags.push(`💩 x${pet.poops}`);
  if (pet.needsAttention) flags.push("❗ needs attention");
  if (pet.careMistakes > 0) flags.push(`⚠ care mistakes: ${pet.careMistakes}`);
  if (flags.length) lines.push("", flags.join("   "));
  if (pet.nextEvolutionSec > 0) lines.push(`Next evolution in ~${fmtAge(pet.nextEvolutionSec)}`);
  return lines.join("\n");
}

// ---------------------------------------------------------------------------
// MCP server

const server = new McpServer({ name: "espgotchi", version: "0.1.0" });

const json = (obj) => ({ content: [{ type: "text", text: JSON.stringify(obj, null, 2) }] });
const text = (s) => ({ content: [{ type: "text", text: s }] });
const failure = (err) => ({ content: [{ type: "text", text: err?.message || String(err) }], isError: true });

const guarded = (fn) => async (args) => {
  try {
    return await fn(args ?? {});
  } catch (err) {
    return failure(err);
  }
};

const readOnly = { readOnlyHint: true, destructiveHint: false, idempotentHint: true, openWorldHint: false };
const careAction = { readOnlyHint: false, destructiveHint: false, idempotentHint: false, openWorldHint: false };

server.registerTool("get_state", {
  title: "Get pet state",
  description:
    "Read the pet's current state (name, species, stage, mood, hunger, happiness, energy, hygiene, health, weight, poops, sick, asleep, dead...) " +
    "plus an `advice` list of what a good caretaker should do right now. Call this first.",
  inputSchema: {},
  annotations: readOnly,
}, guarded(async () => {
  const pet = await getState();
  return json({ pet, advice: adviceFor(pet) });
}));

server.registerTool("get_events", {
  title: "Get recent events",
  description: "Read the pet's recent event log (the last 16 things that happened, newest first): meals, evolutions, poops, sickness, sleep...",
  inputSchema: {},
  annotations: readOnly,
}, guarded(async () => json({ events: await getEvents() })));

server.registerTool("get_info", {
  title: "Get board info",
  description: "Read board and firmware information: version, free heap, WiFi/IP, local time, night mode, brightness and timezone. Boards with a battery gauge also report batteryMv, batteryPct, lowBattery (at or under 15 %, clears at 20 %) and charging (a guess from the voltage trend).",
  inputSchema: {},
  annotations: readOnly,
}, guarded(async () => json(await getInfo())));

server.registerTool("screen_text", {
  title: "Screen as text",
  description: "A compact text rendering of what the pet's screen shows: name, mood and stat bars. Read-only; handy for a quick glance.",
  inputSchema: {},
  annotations: readOnly,
}, guarded(async () => text(renderScreen(await getState()))));

const simpleActions = [
  ["feed", "Feed", "Give the pet a proper meal. Raises hunger (fullness) a lot. Refused while asleep, when full, as an egg or when dead."],
  ["snack", "Snack", "Give the pet a treat. Small fullness boost, cheers it up, but adds weight: do not overdo it."],
  ["play", "Play", "Play ball with the pet. Raises happiness, costs some energy and makes it a little hungrier."],
  ["pet", "Pet", "Pet the pet (a heart appears). Gentle happiness boost with no side effects."],
  ["clean", "Clean", "Clean the pet and remove any poops from the screen (bubbles!). Restores hygiene."],
  ["medicine", "Medicine", "Give medicine. Only useful when the pet is sick; refused otherwise."],
  ["hatch", "Hatch", "Hatch the egg once it is ready (eggSec reaches 0). Refused if there is no egg or it is not ready."],
];
for (const [name, title, description] of simpleActions) {
  server.registerTool(name, {
    title,
    description: `${description} Returns whether it was applied, the reason if not, and the new state. While an animation plays the action is queued (up to ${4} deep) and \`queued\` is true; the board answers busy when the queue is full.`,
    inputSchema: {},
    annotations: careAction,
  }, guarded(async () => json(await doAction(name))));
}

server.registerTool("toggle_lights", {
  title: "Toggle lights",
  description:
    "Turn the room lights off or on. Lights off lets a tired pet fall asleep and recover energy; lights on wakes it up. " +
    "The result's `pet.lightsOff` tells the new state.",
  inputSchema: {},
  annotations: { ...careAction, idempotentHint: false },
}, guarded(async () => json(await doAction("lights"))));

server.registerTool("new_egg", {
  title: "Start over with a new egg",
  description:
    "DESTRUCTIVE: erases the current pet and starts a new egg (next generation). Only use when the pet is dead and the human has explicitly agreed. " +
    "Requires confirm=true. Optionally choose the species.",
  inputSchema: {
    species: z.enum(SPECIES).optional().describe("Species of the new egg: kawaii, alien, dino, edge, the Halloween special edition ghost (Boo) or pumpkin (Jack), the Cute edition mimi (cat with a bow), momo (bunny with a flower) or pingo (penguin with a scarf), or the Fantasy edition unicorn (Nova). Random/default if omitted."),
    confirm: z.literal(true).describe("Must be true. Acknowledges that the current pet will be erased."),
  },
  annotations: { readOnlyHint: false, destructiveHint: true, idempotentHint: false, openWorldHint: false },
}, guarded(async ({ species, confirm }) => {
  if (confirm !== true) return failure(new Error("Refusing to erase the pet: confirm must be true."));
  const extra = species ? { species } : {};
  return json(await doAction("reset", extra));
}));

server.registerTool("rename", {
  title: "Rename the pet",
  description: "Give the pet a new name (1 to 15 characters).",
  inputSchema: {
    name: z.string().trim().min(1).max(15).describe("New name, up to 15 characters."),
  },
  annotations: { ...careAction, idempotentHint: true },
}, guarded(async ({ name }) => {
  await request("/api/name", { method: "POST", body: { name } });
  const pet = await getState();
  return json({ applied: pet.name === name, pet });
}));

server.registerTool("set_settings", {
  title: "Set board settings",
  description: "Change the board's timezone (POSIX TZ string, e.g. CST6CDT,M4.1.0,M10.5.0), screen brightness (5..255) and/or mDNS hostname (reboots the board; useful when two boards share a network).",
  inputSchema: {
    tz: z.string().min(1).max(64).optional().describe("POSIX TZ string used for the clock and night mode."),
    brightness: z.number().int().min(5).max(255).optional().describe("Screen backlight, 5 (dim) to 255 (full)."),
    hostname: z.string().regex(/^[a-z0-9]([a-z0-9-]{0,22}[a-z0-9])?$/).optional().describe("mDNS name without .local, 1-24 chars of a-z 0-9 '-'. The board reboots and answers at <hostname>.local."),
  },
  annotations: { ...careAction, idempotentHint: true },
}, guarded(async ({ tz, brightness, hostname }) => {
  const body = {};
  if (tz !== undefined) body.tz = tz;
  if (brightness !== undefined) body.brightness = brightness;
  if (hostname !== undefined) body.hostname = hostname;
  if (Object.keys(body).length === 0) return failure(new Error("Nothing to change: pass tz, brightness and/or hostname."));
  const r = await request("/api/settings", { method: "POST", body });
  if (r.rebooting) return json({ applied: true, settings: body, rebooting: true, note: `The board is rebooting; reach it at http://${hostname}.local/ in a few seconds.` });
  return json({ applied: true, settings: body, info: await getInfo() });
}));

// Resources ------------------------------------------------------------------

server.registerResource("state", "espgotchi://state", {
  title: "Pet state",
  description: "Current pet state as JSON, with caretaker advice.",
  mimeType: "application/json",
}, async (uri) => {
  const pet = await getState();
  return { contents: [{ uri: uri.href, mimeType: "application/json", text: JSON.stringify({ pet, advice: adviceFor(pet) }, null, 2) }] };
});

server.registerResource("events", "espgotchi://events", {
  title: "Pet events",
  description: "Recent event log as JSON, newest first.",
  mimeType: "application/json",
}, async (uri) => {
  const events = await getEvents();
  return { contents: [{ uri: uri.href, mimeType: "application/json", text: JSON.stringify({ events }, null, 2) }] };
});

// Prompt ---------------------------------------------------------------------

server.registerPrompt("caretaker", {
  title: "Caretaker",
  description: "Instructions for looking after the ESPgotchi responsibly.",
  argsSchema: {},
}, () => ({
  messages: [{
    role: "user",
    content: {
      type: "text",
      text: [
        "You are looking after an ESPgotchi, a small virtual pet living on an ESP32 board.",
        "",
        "Routine:",
        "1. Call get_state and read the `advice` list. Act on it with the matching tools (feed, play, pet, clean, medicine, toggle_lights, hatch).",
        "2. Check again periodically (every 15-30 minutes is plenty); the stats drift slowly.",
        "3. Prefer meals (feed) over snacks: snacks add weight and an overweight pet gets sick more easily. Never feed when it is already full.",
        "4. Respect sleep: when the pet is asleep do not poke it; let it recover energy. Turn the lights off when its energy is very low.",
        "5. Clean poops promptly; they wreck hygiene and lead to sickness.",
        "6. Never call new_egg unless the pet is dead AND the human has explicitly agreed to start over. It erases the pet.",
        "7. Report briefly what you did and how the pet is doing; use screen_text for a quick summary.",
      ].join("\n"),
    },
  }],
}));

// ---------------------------------------------------------------------------

const transport = new StdioServerTransport();
await server.connect(transport);
console.error(`espgotchi-mcp ready, talking to ${BASE_URL}`);
