#include "net.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <mdns.h>
#include <time.h>
#include "config.h"
#include "hw.h"
#include "web_assets.h"

static WebServer server(80);
static DNSServer dnsServer;
static bool dnsRunning = false;
static bool staFailed = false;  // saved credentials did not work since boot

static String apSsid() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "%s%02X%02X", AP_SSID_PREFIX, (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  return String(buf);
}

static void startCaptiveDns() {
  if (dnsRunning) return;
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  dnsRunning = true;
}
static Preferences netPrefs;
static Net *self = nullptr;

static String htmlEscape(const String &in) {
  String o;
  for (char c : in) {
    if (c == '<') o += "&lt;"; else if (c == '&') o += "&amp;"; else if (c == '"') o += "&quot;"; else o += c;
  }
  return o;
}

// Setup page: lists nearby networks and explains why we are in setup mode.
static String buildSetupPage(const String &savedSsid, bool failed) {
  String page = F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>ESPgotchi setup</title>"
    "<style>body{font-family:system-ui;background:#10132b;color:#eef;display:grid;place-items:center;min-height:100vh;margin:0;padding:16px}"
    "form{background:#1c2048;padding:24px;border-radius:16px;width:min(92vw,380px);box-shadow:0 20px 60px rgba(0,0,0,.4)}"
    "h1{margin:0 0 4px;font-size:22px}p{color:#aab;font-size:14px;margin:6px 0 14px}label{font-size:13px;color:#aab}"
    "select,input,button{width:100%;padding:12px;margin:6px 0 14px;border-radius:10px;border:0;font-size:16px;background:#2a2f63;color:#fff}"
    "button{background:#4d96ff;font-weight:700;cursor:pointer}.warn{background:#3a1f2d;color:#ff9bb3;padding:10px;border-radius:10px;font-size:13px}</style></head><body>"
    "<form method='post' action='/api/wifi'><h1>🥚 ESPgotchi WiFi</h1><p>Pick your home network so the pet can be reached at <b>espgotchi.local</b>.</p>");
  if (failed && savedSsid.length()) {
    page += F("<div class='warn'>Could not join <b>");
    page += htmlEscape(savedSsid);
    page += F("</b>. Check the password or choose another network.</div><br>");
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); n = 0; }
  if (n == WIFI_SCAN_RUNNING) n = 0;
  page += F("<label>Network <a href='/setup' style='float:right;color:#8fd3f4'>rescan</a></label><select name='ssid' id='sel' onchange=\"document.getElementById('o').style.display=this.value?'none':'block'\">");
  for (int i = 0; i < n && i < 20; i++) {
    String ss = WiFi.SSID(i);
    if (!ss.length()) continue;
    page += "<option value=\"" + htmlEscape(ss) + "\"" + (ss == savedSsid ? " selected" : "") + ">" + htmlEscape(ss) + " (" + WiFi.RSSI(i) + " dBm)</option>";
  }
  page += F("<option value=''>Other network…</option></select>");
  page += F("<input id='o' name='network' placeholder='Network name (SSID)' autocomplete='off' style='display:none'>");
  page += F("<label>Password</label><input name='pass' type='password' placeholder='WiFi password' autocomplete='new-password'>"
            "<button>Save and reboot</button>"
            "<p style='text-align:center;margin:0'><a href='/' style='color:#8fd3f4'>Skip for now and open the pet panel</a></p></form>"
            "<script>var f=document.forms[0];var sel=f.ssid;if(!sel.options.length||!sel.options[0].value){document.getElementById('o').style.display='block';}"
            "f.onsubmit=function(){if(!sel.value){var o=document.getElementById('o').value.trim();if(!o){alert('Enter the network name');return false;}sel.disabled=true;document.getElementById('o').name='ssid';}}</script></body></html>");
  if (n > 0) { WiFi.scanDelete(); WiFi.scanNetworks(true); }  // refresh for next visit
  return page;
}

void Net::loadSettings() {
  netPrefs.begin("net", false);
  staSsid = netPrefs.getString("ssid", "");
  staPass = netPrefs.getString("pass", "");
  tzStr = netPrefs.getString("tz", DEFAULT_TZ);
  brightness = netPrefs.getUChar("bl", BACKLIGHT_DAY);
  rotation = netPrefs.getUChar("rot", 1) & 3;
  host = netPrefs.getString("host", MDNS_HOST);
  nightDim = netPrefs.getBool("ndim", true);
}

void Net::begin(Pet *p) {
  pet = p;
  self = this;
  WiFi.persistent(false);
  WiFi.setSleep(false);
  if (staSsid.length()) startSta();
  else startAp();
  setupRoutes();
  server.enableCORS(true);
  server.begin();
}

void Net::startSta() {
  apMode = false;
  connected = false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(host.c_str());
  WiFi.begin(staSsid.c_str(), staPass.c_str());
  connectStartMs = millis();
  Serial.printf("[net] connecting to %s\n", staSsid.c_str());
}

void Net::startAp() {
  apMode = true;
  connected = false;
  String ssid = apSsid();
  WiFi.mode(WIFI_AP_STA);  // STA stays on so the setup page can scan networks
  WiFi.softAP(ssid.c_str());  // open network, no password
  startCaptiveDns();
  startMdns();  // <host>.local answers on the setup network as well
  Serial.printf("[net] AP mode: %s (open) -> http://%s\n", ssid.c_str(), WiFi.softAPIP().toString().c_str());
  WiFi.scanNetworks(true);  // async scan so the setup page has results ready
  if (pet) pet->logEvent("WiFi setup AP: %s", ssid.c_str());
}

void Net::onConnected() {
  connected = true;
  Serial.printf("[net] connected, IP %s\n", WiFi.localIP().toString().c_str());
  if (pet) pet->logEvent("WiFi up: %s", WiFi.localIP().toString().c_str());
  if (apMode) closeAp();  // the saved network came back: the setup network has done its job
  startMdns();
  advertise();
  nextVisitMs = millis() + VISIT_FIRST_MS;
  if (!timeConfigured) {
    configTzTime(tzStr.c_str(), NTP_SERVER);
    timeConfigured = true;
  }
}

void Net::loop() {
  if (dnsRunning) dnsServer.processNextRequest();
  server.handleClient();
  // Catch up on the time the board was off as soon as the clock is valid, whatever set it
  // (NTP on the home network, or a phone on the setup network).
  static bool caughtUp = false;
  if (!caughtUp && epoch()) {
    caughtUp = true;
    if (pet) pet->catchUp(epoch());
  }
  if (apMode && (apHeld || !staSsid.length())) return;  // nothing to retry
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    if (!connected) onConnected();
    visitLoop();
  } else {
    connected = false;
    uint32_t now = millis();
    if (!apMode && now - connectStartMs > 30000) {
      // Give up after 30 s and open the setup AP. STA keeps retrying underneath (AP_STA) every
      // 60 s; if the saved network comes back the AP is closed again in onConnected().
      Serial.println("[net] STA timeout, opening setup AP (STA keeps retrying)");
      staFailed = true;
      startAp();
      connectStartMs = now;
    }
    if (now - lastReconnectMs > (apMode ? 60000u : 15000u)) {
      lastReconnectMs = now;
      WiFi.reconnect();
    }
  }
}

void Net::startMdns() {
  if (mdnsStarted || !MDNS.begin(host.c_str())) return;
  MDNS.addService("http", "tcp", 80);
  MDNS.addServiceTxt("http", "tcp", "device", "espgotchi");
  MDNS.addService("espgotchi", "tcp", 80);  // other boards find us through this one
  mdnsStarted = true;
}

void Net::closeAp() {
  if (!apMode) return;
  if (dnsRunning) { dnsServer.stop(); dnsRunning = false; }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apMode = false;
  staFailed = false;
  Serial.println("[net] setup AP closed");
  if (pet) pet->logEvent("Setup network closed");
}

void Net::openAp() {
  if (apMode) { apHeld = true; return; }
  connected = false;
  WiFi.disconnect();
  startAp();
  apHeld = true;
}

bool Net::setClock(uint32_t e) {
  if (connected) return false;  // on the home network NTP owns the clock; on the setup network the phone does
  if (e < 1700000000u || e > 4000000000u) return false;  // before this firmware existed, or after 2096
  struct timeval tv = {(time_t)e, 0};
  settimeofday(&tv, nullptr);
  Serial.printf("[net] clock set from the network: %u\n", (unsigned)e);
  return true;
}

// ---------- visits ----------
// Every board advertises _espgotchi._tcp with the pet's species, name and stage. Every
// VISIT_INTERVAL_MS a board queries that service, picks another board and fetches its /api/state
// so the friend can be drawn from the species sheets already in flash. Alone on the network the
// query finds nobody and nothing happens.
void Net::advertise() {
  if (!mdnsStarted || !pet) return;
  const PetState &s = pet->state();
  String txt = String(pet->speciesKey()) + "|" + s.name + "|" + pet->stageName();
  if (txt == lastAdvert) return;
  lastAdvert = txt;
  MDNS.addServiceTxt("espgotchi", "tcp", "species", pet->speciesKey());
  MDNS.addServiceTxt("espgotchi", "tcp", "name", s.name);
  MDNS.addServiceTxt("espgotchi", "tcp", "stage", pet->stageName());
}

void Net::visitNow() {
  if (!connected || apMode || !mdnsStarted) { Serial.println("[visit] no WiFi"); return; }
  if (visitSearch) return;  // one already in flight
  visitSearch = mdns_query_async_new(nullptr, "_espgotchi", "_tcp", MDNS_TYPE_PTR, 1500, 8, nullptr);
  if (!visitSearch) Serial.println("[visit] mDNS query failed to start");
}

void Net::visitLoop() {
  uint32_t now = millis();
  if (now - lastAdvertMs > 10000) {  // keep the TXT records in step with renames and evolutions
    lastAdvertMs = now;
    advertise();
  }
  if (!visitSearch && pet && (int32_t)(now - nextVisitMs) >= 0) {
    nextVisitMs = now + VISIT_INTERVAL_MS;
    const PetState &s = pet->state();
    if (!s.dead && s.stage != STAGE_EGG && !s.asleep && !pet->busy()) visitNow();
  }
  if (!visitSearch) return;
  mdns_result_t *results = nullptr;
  uint8_t n = 0;
  if (!mdns_query_async_get_results((mdns_search_once_t *)visitSearch, 0, &results, &n)) return;  // still listening
  mdns_query_async_delete((mdns_search_once_t *)visitSearch);
  visitSearch = nullptr;
  // Collect the other boards (never ourselves) and pick one at random.
  struct { IPAddress ip; uint16_t port; } found[8];
  uint8_t count = 0;
  IPAddress me = WiFi.localIP();
  for (mdns_result_t *r = results; r && count < 8; r = r->next) {
    for (mdns_ip_addr_t *a = r->addr; a; a = a->next) {
      if (a->addr.type != ESP_IPADDR_TYPE_V4) continue;
      IPAddress ip(a->addr.u_addr.ip4.addr);
      if (ip == me) break;
      found[count].ip = ip;
      found[count].port = r->port ? r->port : 80;
      count++;
      break;
    }
  }
  mdns_query_results_free(results);
  if (!count) { Serial.println("[visit] nobody around"); return; }
  uint8_t pick = esp_random() % count;
  String err;
  if (!visitAddr(found[pick].ip, found[pick].port, err)) {
    Serial.printf("[visit] %s: %s\n", found[pick].ip.toString().c_str(), err.c_str());
    nextVisitMs = millis() + 30000;  // busy or unreachable: retry soon rather than in five minutes
  }
}

// Fallback for boards whose /api/state predates `spriteSlot`.
static uint8_t slotFromStage(const char *stage, const char *form) {
  if (!stage) return 3;
  if (!strcmp(stage, "baby")) return 0;
  if (!strcmp(stage, "child")) return 1;
  if (!strcmp(stage, "teen")) return 2;
  if (!strcmp(stage, "elder")) return 6;
  if (form && !strcmp(form, "elite")) return 4;
  if (form && !strcmp(form, "feral")) return 5;
  return 3;
}

bool Net::visitAddr(IPAddress ip, uint16_t port, String &err) {
  if (!pet) { err = "no pet"; return false; }
  if (ip == WiFi.localIP() || ip == IPAddress((uint32_t)0)) { err = "that is this board"; return false; }
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(VISIT_HTTP_TIMEOUT_MS);
  http.setTimeout(VISIT_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  String url = "http://" + ip.toString() + ":" + String(port) + "/api/state";
  if (!http.begin(client, url)) { err = "bad url"; return false; }
  int code = http.GET();
  if (code != 200) { http.end(); err = "http " + String(code); return false; }
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, http.getString());
  http.end();
  if (de) { err = "bad json"; return false; }
  JsonObject p = doc["pet"];
  const char *name = p["name"];
  const char *species = p["species"];
  const char *stage = p["stage"];
  if (!name || !*name || !species) { err = "not an espgotchi"; return false; }
  if (p["dead"] | false) { err = "friend has passed away"; return false; }
  if (stage && !strcmp(stage, "egg")) { err = "friend is still an egg"; return false; }
  int8_t sp = Pet::speciesFromKey(species);
  if (sp < 0) { err = String("unknown species ") + species; return false; }
  int slot = p["spriteSlot"] | -1;
  if (slot < 0) slot = slotFromStage(stage, p["form"] | (const char *)nullptr);
  if (!pet->visitFrom(name, (uint8_t)sp, (uint8_t)slot)) { err = "pet is busy, asleep or not hatched"; return false; }
  Serial.printf("[visit] %s (%s) from %s dropped by\n", name, species, ip.toString().c_str());
  return true;
}

bool Net::visitHost(const char *host, String &err) {
  if (!connected || apMode) { err = "no WiFi"; return false; }
  if (!host || !*host || strlen(host) > 64) { err = "host required"; return false; }
  IPAddress ip;
  if (!ip.fromString(host)) {
    // "name.local" or a bare mDNS name: the DNS resolver does not handle .local, mDNS does
    char name[65];
    strlcpy(name, host, sizeof(name));
    size_t n = strlen(name);
    if (n > 6 && !strcasecmp(name + n - 6, ".local")) name[n - 6] = 0;
    if (!mdnsStarted) { err = "mDNS not running"; return false; }
    ip = MDNS.queryHost(name, 500);
    if (ip == IPAddress((uint32_t)0)) { err = String("cannot resolve ") + host; return false; }
  }
  return visitAddr(ip, 80, err);
}

uint32_t Net::epoch() const {
  time_t now = time(nullptr);
  return now > 1700000000 ? (uint32_t)now : 0;
}

NetInfo Net::info() const {
  NetInfo n{};
  n.connected = connected;
  n.apMode = apMode;
  n.timeValid = epoch() != 0;
  if (connected) {
    strlcpy(n.ssid, WiFi.SSID().c_str(), sizeof(n.ssid));
    strlcpy(n.ip, WiFi.localIP().toString().c_str(), sizeof(n.ip));
    n.rssi = WiFi.RSSI();
  } else if (apMode) {
    strlcpy(n.ssid, WiFi.softAPSSID().c_str(), sizeof(n.ssid));
    strlcpy(n.ip, WiFi.softAPIP().toString().c_str(), sizeof(n.ip));
  }
  strlcpy(n.host, host.c_str(), sizeof(n.host));
  return n;
}

bool Net::saveCredentials(const char *ssid, const char *pass) {
  if (!ssid || !ssid[0]) return false;
  netPrefs.putString("ssid", ssid);
  netPrefs.putString("pass", pass ? pass : "");
  return true;
}

void Net::forgetCredentials() {
  netPrefs.remove("ssid");
  netPrefs.remove("pass");
}

void Net::setTz(const char *tz) {
  tzStr = tz;
  netPrefs.putString("tz", tzStr);
  configTzTime(tzStr.c_str(), NTP_SERVER);
}

String Net::tz() const { return tzStr; }

bool Net::setHostname(const char *name) {
  if (!name) return false;
  size_t n = strlen(name);
  if (n < 1 || n > 24 || name[0] == '-' || name[n - 1] == '-') return false;
  String clean;
  for (size_t i = 0; i < n; i++) {
    char c = tolower((unsigned char)name[i]);
    if (!(isalnum((unsigned char)c) || c == '-')) return false;
    clean += c;
  }
  host = clean;
  netPrefs.putString("host", host);
  return true;
}

// ---------- HTTP API ----------
static void sendJson(JsonDocument &doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void sendError(const char *msg, int code = 400) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg;
  sendJson(doc, code);
}

static bool readBody(JsonDocument &doc) {
  if (!server.hasArg("plain")) return false;
  return deserializeJson(doc, server.arg("plain")) == DeserializationError::Ok;
}

static bool inSetupMode() { NetInfo n = self->info(); return n.apMode && !n.connected; }

// Redirect to the setup page so the OS shows its "sign in to network" sheet
static void captiveRedirect() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

void Net::setupRoutes() {
  // OS connectivity probes (Apple, Android, Windows, Firefox)
  const char *probes[] = {"/hotspot-detect.html", "/library/test/success.html", "/generate_204", "/gen_204",
                          "/connecttest.txt", "/ncsi.txt", "/redirect", "/fwlink", "/success.txt", "/canonical.html"};
  for (const char *path : probes) {
    server.on(path, HTTP_ANY, []() {
      if (inSetupMode()) captiveRedirect();
      else server.send(204);
    });
  }
  // The whole panel is served on the setup network too: the pet works without internet, the
  // panel shows the WiFi picker on top. /setup keeps the 2 KB page for captive sheets that
  // choke on the real one.
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html", (const char *)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
  });
  server.on("/sprites.json", HTTP_GET, []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send_P(200, "application/json", (const char *)WEB_SPRITES_GZ, WEB_SPRITES_GZ_LEN);
  });
  server.on("/setup", HTTP_GET, []() { server.send(200, "text/html", buildSetupPage(self->savedSsid(), staFailed)); });

  // Nearby networks for the panel's WiFi picker. Scans are async: the first call usually
  // answers "scanning" and the list arrives on the next one. ?rescan=1 forces a fresh scan.
  server.on("/api/networks", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED || server.hasArg("rescan")) { WiFi.scanDelete(); WiFi.scanNetworks(true); n = WIFI_SCAN_RUNNING; }
    doc["scanning"] = n == WIFI_SCAN_RUNNING;
    JsonArray arr = doc["networks"].to<JsonArray>();
    for (int i = 0, kept = 0; n > 0 && i < n && kept < 20; i++) {
      if (!WiFi.SSID(i).length()) continue;
      bool dup = false;  // one entry per SSID: results come strongest first, so keep the best AP
      for (JsonObject seen : arr) if (seen["ssid"] == WiFi.SSID(i)) { dup = true; break; }
      if (dup) continue;
      kept++;
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = WiFi.SSID(i);
      o["rssi"] = WiFi.RSSI(i);
      o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    doc["saved"] = self->savedSsid();
    doc["failed"] = staFailed;
    sendJson(doc);
  });

  // The panel sends the phone's clock while the board has no internet, so night mode and the
  // catch-up after a power cut work on the setup network too. Ignored once NTP has synced.
  server.on("/api/time", HTTP_POST, []() {
    JsonDocument body;
    if (!readBody(body) || !body["epoch"].is<uint32_t>()) return sendError("epoch required");
    JsonDocument doc;
    doc["ok"] = true;
    doc["applied"] = self->setClock(body["epoch"].as<uint32_t>());
    doc["epoch"] = self->epoch();
    sendJson(doc);
  });

  server.on("/api/state", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    self->pet->toJson(doc["pet"].to<JsonObject>());
    JsonObject d = doc["device"].to<JsonObject>();
    NetInfo n = self->info();
    d["ip"] = n.ip;
    d["ssid"] = n.ssid;
    d["rssi"] = n.rssi;
    d["apMode"] = n.apMode;
    d["setup"] = n.apMode && !n.connected;  // on its own network, no internet
    d["epoch"] = self->epoch();
    d["uptimeSec"] = millis() / 1000;
    if (self->pet->hasVisitor()) {
      JsonObject v = doc["visitor"].to<JsonObject>();
      v["name"] = self->pet->visitorName();
      v["species"] = self->pet->visitorSpeciesKey();
    }
    sendJson(doc);
  });

  // Visit a given board now (testing), or look for one on the LAN when no host is given.
  server.on("/api/visit", HTTP_POST, []() {
    JsonDocument body;
    readBody(body);
    const char *host = body["host"] | (const char *)nullptr;
    JsonDocument doc;
    if (!host || !*host) {
      self->visitNow();
      doc["ok"] = true;
      doc["searching"] = true;
      return sendJson(doc);
    }
    String err;
    bool ok = self->visitHost(host, err);
    doc["ok"] = ok;
    doc["applied"] = ok;
    if (!ok) doc["error"] = err;
    else {
      JsonObject v = doc["visitor"].to<JsonObject>();
      v["name"] = self->pet->visitorName();
      v["species"] = self->pet->visitorSpeciesKey();
    }
    sendJson(doc, ok ? 200 : 400);
  });

  server.on("/api/events", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    self->pet->eventsToJson(doc["events"].to<JsonArray>());
    sendJson(doc);
  });

  server.on("/api/info", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    doc["board"] = BOARD_NAME;
    doc["boardId"] = BOARD_ID;
    doc["touch"] = HAS_TOUCH != 0;
    if (hwHasBattery()) {
      doc["batteryMv"] = hwBatteryMv();
      doc["batteryPct"] = hwBatteryPct();
      doc["lowBattery"] = hwBatteryLow();  // <= 15 %, clears at >= 20 %
      doc["charging"] = hwBatteryCharging();  // guessed from the voltage trend (no VBUS sense)
    }
    doc["fw"] = FW_VERSION;
    doc["chip"] = ESP.getChipModel();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["hostname"] = self->host;
    doc["mdns"] = self->host + ".local";
    doc["nightDim"] = self->nightDim;
    doc["tz"] = self->tz();
    doc["brightness"] = self->brightness;
    doc["backlightDuty"] = ledcRead(PIN_LCD_BL);
    {
      time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      char buf[32];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &t);
      doc["localTime"] = self->epoch() ? buf : "not synced";
      doc["night"] = self->epoch() && (t.tm_hour >= 22 || t.tm_hour < 7);
    }
    static const char *orient[] = {"portrait", "landscape", "portrait-flipped", "landscape-flipped"};
    doc["orientation"] = orient[self->rotation & 3];
    NetInfo n = self->info();
    doc["ip"] = n.ip;
    doc["ssid"] = n.ssid;
    doc["rssi"] = n.rssi;
    doc["apMode"] = n.apMode;
    doc["setup"] = n.apMode && !n.connected;
    doc["apSsid"] = n.apMode ? WiFi.softAPSSID() : "";
    doc["timeValid"] = n.timeValid;
    sendJson(doc);
  });

  server.on("/api/action", HTTP_POST, []() {
    JsonDocument body;
    if (!readBody(body) || body["type"].isNull()) return sendError("expected JSON {\"type\": ...}");
    const char *type = body["type"];
    bool ok = false, queued = false;
    Pet *p = self->pet;
    int8_t act = Pet::actionFromKey(type);
    if (act >= 0) {
      // Animated actions queue behind the one on screen instead of cutting it short.
      ReqResult r = p->request((PetAction)act);
      if (r == REQ_FULL) {
        JsonDocument doc;
        doc["ok"] = false;
        doc["error"] = "busy";
        doc["busyMs"] = p->busyMs();
        doc["queued"] = p->queued();
        return sendJson(doc, 429);
      }
      ok = r == REQ_APPLIED;
      queued = r == REQ_QUEUED;
    }
    else if (!strcmp(type, "sleep") || !strcmp(type, "lights")) ok = p->toggleLights();
    else if (!strcmp(type, "reset")) { p->reset(Pet::speciesFromKey(body["species"] | (const char *)nullptr)); ok = true; }
    else return sendError("unknown action type");
    p->save(self->epoch(), true);
    JsonDocument doc;
    doc["ok"] = true;
    doc["applied"] = ok;
    doc["queued"] = queued;
    if (queued) doc["position"] = p->queued();
    p->toJson(doc["pet"].to<JsonObject>());
    sendJson(doc);
  });

  server.on("/api/name", HTTP_POST, []() {
    JsonDocument body;
    if (!readBody(body) || body["name"].isNull()) return sendError("expected JSON {\"name\": ...}");
    self->pet->setName(body["name"]);
    self->pet->save(self->epoch(), true);
    JsonDocument doc;
    doc["ok"] = true;
    self->pet->toJson(doc["pet"].to<JsonObject>());
    sendJson(doc);
  });

  server.on("/api/settings", HTTP_POST, []() {
    JsonDocument body;
    if (!readBody(body)) return sendError("expected JSON body");
    if (!body["tz"].isNull()) self->setTz(body["tz"]);
    if (!body["brightness"].isNull()) {
      self->brightness = constrain((int)body["brightness"], 5, 255);
      netPrefs.putUChar("bl", self->brightness);
    }
    if (!body["nightDim"].isNull()) {
      self->nightDim = (bool)body["nightDim"];
      netPrefs.putBool("ndim", self->nightDim);
    }
    bool reboot = false;
    if (!body["hostname"].isNull()) {
      String before = self->host;
      if (!self->setHostname(body["hostname"])) return sendError("hostname must be 1-24 chars of a-z, 0-9 or '-'");
      if (self->host != before) reboot = true;  // WiFi hostname and mDNS are set at connect time
    }
    if (!body["orientation"].isNull()) {
      const char *o = body["orientation"];
      uint8_t rot = !strcmp(o, "portrait") ? 0 : !strcmp(o, "portrait-flipped") ? 2 : !strcmp(o, "landscape-flipped") ? 3 : 1;
      if (rot != self->rotation) {
        netPrefs.putUChar("rot", rot);
        self->rotation = rot;
        reboot = true;  // the framebuffer is allocated at boot
      }
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["tz"] = self->tz();
    doc["brightness"] = self->brightness;
    doc["hostname"] = self->host;
    doc["nightDim"] = self->nightDim;
    doc["rebooting"] = reboot;
    sendJson(doc);
    if (reboot) self->restartRequested = true;
  });

  // Accepts JSON or a classic HTML form post (from the setup page)
  server.on("/api/wifi", HTTP_POST, []() {
    String ssid, pass;
    JsonDocument body;
    if (server.hasArg("ssid") || server.hasArg("network")) {
      ssid = server.hasArg("ssid") && server.arg("ssid").length() ? server.arg("ssid") : server.arg("network");
      pass = server.arg("pass");
    } else if (readBody(body)) {
      ssid = body["ssid"] | "";
      pass = body["pass"] | "";
    }
    ssid.trim();
    if (!ssid.length() || ssid.length() > 32) return sendError("ssid required");
    self->saveCredentials(ssid.c_str(), pass.c_str());
    server.send(200, "text/html", "<meta charset=utf-8><body style='font-family:system-ui;padding:24px'><h1>Saved</h1><p>Rebooting and joining <b>" + ssid + "</b>. Then open http://espgotchi.local</p>");
    self->restartRequested = true;
  });

  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    if (inSetupMode() && !server.uri().startsWith("/api/")) { captiveRedirect(); return; }
    sendError("not found", 404);
  });
}
