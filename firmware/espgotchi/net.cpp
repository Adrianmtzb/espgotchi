#include "net.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
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
            "<button>Save and reboot</button></form>"
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
  WiFi.setHostname(MDNS_HOST);
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
  Serial.printf("[net] AP mode: %s (open) -> http://%s\n", ssid.c_str(), WiFi.softAPIP().toString().c_str());
  WiFi.scanNetworks(true);  // async scan so the setup page has results ready
  if (pet) pet->logEvent("WiFi setup AP: %s", ssid.c_str());
}

void Net::onConnected() {
  connected = true;
  Serial.printf("[net] connected, IP %s\n", WiFi.localIP().toString().c_str());
  if (pet) pet->logEvent("WiFi up: %s", WiFi.localIP().toString().c_str());
  if (!mdnsStarted && MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "device", "espgotchi");
    mdnsStarted = true;
  }
  if (!timeConfigured) {
    configTzTime(tzStr.c_str(), NTP_SERVER);
    timeConfigured = true;
  }
}

void Net::loop() {
  if (dnsRunning) dnsServer.processNextRequest();
  server.handleClient();
  if (apMode) return;
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    if (!connected) onConnected();
    static bool caughtUp = false;
    if (!caughtUp && epoch()) {
      caughtUp = true;
      if (pet) pet->catchUp(epoch());
    }
  } else {
    connected = false;
    uint32_t now = millis();
    if (now - connectStartMs > 30000 && WiFi.getMode() != WIFI_AP) {
      // Give up after 30s and open the setup AP; keep retrying STA in background every 60s
      Serial.println("[net] STA timeout, opening setup AP (STA keeps retrying)");
      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP(apSsid().c_str());
      WiFi.scanNetworks(true);
      staFailed = true;
      startCaptiveDns();
      apMode = true;
      connectStartMs = now;
    }
    if (now - lastReconnectMs > 15000) {
      lastReconnectMs = now;
      WiFi.reconnect();
    }
  }
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
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/setup", true);
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
  server.on("/", HTTP_GET, []() {
    if (self->apMode && !self->connected) {
      server.send(200, "text/html", buildSetupPage(self->savedSsid(), staFailed));
      return;
    }
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
    d["epoch"] = self->epoch();
    d["uptimeSec"] = millis() / 1000;
    sendJson(doc);
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
    doc["board"] = "Waveshare ESP32-C6-LCD-1.47";
    doc["fw"] = FW_VERSION;
    doc["chip"] = ESP.getChipModel();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["mdns"] = String(MDNS_HOST) + ".local";
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
    doc["timeValid"] = n.timeValid;
    sendJson(doc);
  });

  server.on("/api/action", HTTP_POST, []() {
    JsonDocument body;
    if (!readBody(body) || body["type"].isNull()) return sendError("expected JSON {\"type\": ...}");
    const char *type = body["type"];
    bool ok = false;
    Pet *p = self->pet;
    if (!strcmp(type, "feed")) ok = p->feed(false);
    else if (!strcmp(type, "snack")) ok = p->feed(true);
    else if (!strcmp(type, "play")) ok = p->play();
    else if (!strcmp(type, "pet")) ok = p->pet();
    else if (!strcmp(type, "clean")) ok = p->clean();
    else if (!strcmp(type, "sleep") || !strcmp(type, "lights")) ok = p->toggleLights();
    else if (!strcmp(type, "medicine")) ok = p->medicine();
    else if (!strcmp(type, "hatch")) ok = p->hatch();
    else if (!strcmp(type, "reset")) { p->reset(Pet::speciesFromKey(body["species"] | (const char *)nullptr)); ok = true; }
    else return sendError("unknown action type");
    p->save(self->epoch(), true);
    JsonDocument doc;
    doc["ok"] = true;
    doc["applied"] = ok;
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
    bool reboot = false;
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
