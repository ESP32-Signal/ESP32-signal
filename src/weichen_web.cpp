#include "weichen_web.h"

#if MODULE_BAHNHOF

#include <WebServer.h>
#include <WiFi.h>

#include <Preferences.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "link.h"

namespace bahn {
namespace {

constexpr size_t kPageCap = 12000;
constexpr size_t kTableBytes = 3 * kWeichenAnzahl * 2;
constexpr size_t kSignalBytes = 6;
constexpr size_t kGleisBytes = 3;
constexpr const char* kPrefsName = "weichen";
constexpr const char* kPrefsKey = "tab";
constexpr const char* kPrefsSignal = "sig";
constexpr const char* kPrefsGleis = "on";

constexpr char kPlanScript[] = R"js(<script>
(function(){
var svg=document.getElementById('plan');
var NS='http://www.w3.org/2000/svg';
function node(n){return document.createElementNS(NS,n)}
function add(n,a){var e=node(n);for(var k in a)e.setAttribute(k,a[k]);svg.appendChild(e);return e}
function field(name){return document.querySelector('[name='+name+']')}
function val(name){var e=field(name);return e?e.value:'-1'}
function an(name){var e=field(name);return !!(e&&e.checked)}
function text(x,y,s){var e=add('text',{x:x,y:y,'font-size':14,fill:'#222'});e.textContent=s}
function poly(pts,color,off){
  var s='';
  for(var i=0;i<pts.length;i++){if(i)s+=' ';s+=pts[i][0]+','+pts[i][1]}
  add('polyline',{points:s,fill:'none',stroke:off?'#bbb':color,'stroke-width':off?2:5,
    'stroke-linejoin':'round','stroke-linecap':'round','stroke-dasharray':off?'7 5':'none'});
}
function throat(route,order,xs,x0,x1,step){
  var y=36,pts=[[x0,y]];
  for(var i=0;i<order.length;i++){
    pts.push([xs[i],y]);
    if(val('r'+route+'w'+order[i])=='1')y+=58;
    pts.push([xs[i]+step,y]);
  }
  pts.push([x1,y]);
  return pts;
}
function draw(){
  while(svg.firstChild)svg.removeChild(svg.firstChild);
  text(12,20,'Strecke');
  text(612,20,'Einfahrt');
  text(90,30,'1');text(176,30,'2');text(586,30,'3');text(486,30,'4');
  var color=['#1a73e8','#188038','#e37400'];
  for(var t=0;t<3;t++){
    var off=!an('g'+t);
    var left=throat(t,[0,1],[90,176],16,340,36);
    var right=throat(t+3,[2,3],[604,500],684,360,-36);
    poly(left,color[t],off);
    poly(right,color[t],off);
    var yL=left[left.length-1][1], yR=right[right.length-1][1];
    poly([[340,yL],[360,yR]],color[t],off);
    text(328,Math.min(yL,yR)-8,String(t+1));
  }
}
document.querySelector('form').addEventListener('change',draw);
draw();
})();
</script>)js";

WebServer server(80);
Bahnhof* hof = nullptr;
bool aktiv = false;
char page[kPageCap];

struct Writer {
  size_t used = 0;

  bool add(const char* text) {
    const size_t n = strlen(text);
    if (used + n + 1 > kPageCap) {
      return false;
    }
    memcpy(page + used, text, n);
    used += n;
    page[used] = '\0';
    return true;
  }
};

bool option(Writer& out, const char* value, const char* label, bool selected) {
  char html[96];
  snprintf(html, sizeof html, "<option value=\"%s\"%s>%s</option>", value, selected ? " selected" : "", label);
  return out.add(html);
}

bool selectField(Writer& out, const char* name, int8_t lage) {
  char open[32];
  snprintf(open, sizeof open, "<select name=\"%s\">", name);
  return out.add(open) && option(out, "-1", "bleibt", lage == kWeicheBleibt) && option(out, "0", "0", lage == 0) &&
         option(out, "1", "1", lage == 1) && out.add("</select>");
}

bool signalField(Writer& out, const char* name, Begriff begriff) {
  char open[32];
  snprintf(open, sizeof open, "<select name=\"%s\">", name);
  return out.add(open) && option(out, "frei", "Frei", begriff == Begriff::Frei) &&
         option(out, "frei40", "Frei 40", begriff == Begriff::Frei40) && out.add("</select>");
}

bool buildPage(bool saved) {
  int8_t ausfahrt[3][kWeichenAnzahl];
  int8_t einfahrt[3][kWeichenAnzahl];
  Begriff ausfahrtSignal[3];
  Begriff einfahrtSignal[3];
  bool gleisAn[3];
  hof->tabelle(ausfahrt, einfahrt, ausfahrtSignal, einfahrtSignal, gleisAn);

  Writer out;
  if (!out.add("<!DOCTYPE html><html lang=de><meta charset=utf-8>"
               "<meta name=viewport content=\"width=device-width\">"
               "<title>Weichen</title>"
               "<style>body{font:16px sans-serif;margin:1rem}"
               "table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:.35rem}"
               "button{margin-top:1rem;padding:.5rem 1rem}"
               "label.sw{display:inline-flex;align-items:center;gap:.35rem;margin-right:1rem}"
               "#plan{width:100%;max-width:700px;background:#f6f7f8;border:1px solid #ddd;border-radius:6px}"
               ".hint{color:#444;font-size:14px}</style>"
               "<h1>Weichentabelle</h1>")) {
    return false;
  }
  if (saved && !out.add("<p>Gespeichert.</p>")) {
    return false;
  }
  if (!out.add("<form method=post action=/><p>")) {
    return false;
  }
  for (uint8_t gleis = 0; gleis < 3; ++gleis) {
    char box[96];
    snprintf(box, sizeof box, "<label class=sw><input type=checkbox name=g%u%s> Gleis %u</label>", gleis,
             gleisAn[gleis] ? " checked" : "", gleis + 1);
    if (!out.add(box)) {
      return false;
    }
  }
  if (!out.add("</p><svg id=plan viewBox=\"0 0 700 170\"></svg>"
               "<p class=hint>Stellung 0 ist die Gerade, Stellung 1 die Ablenkung nach unten. "
               "Ein ausgeschaltetes Gleis ist gestrichelt. Weiche 1 und 2 liegen zur Strecke, "
               "Weiche 3 und 4 auf der Einfahrtseite.</p>"
               "<table><tr><th>Fahrstrasse</th><th>Weiche 1</th><th>Weiche 2</th>"
               "<th>Weiche 3</th><th>Weiche 4</th><th>Signal</th></tr>")) {
    return false;
  }

  const char* names[6] = {"Ausfahrt Gleis 1", "Ausfahrt Gleis 2", "Ausfahrt Gleis 3",
                          "Einfahrt Gleis 1", "Einfahrt Gleis 2", "Einfahrt Gleis 3"};
  for (uint8_t route = 0; route < 6; ++route) {
    const int8_t* row = route < 3 ? ausfahrt[route] : einfahrt[route - 3];
    const Begriff signal = route < 3 ? ausfahrtSignal[route] : einfahrtSignal[route - 3];
    char rowHtml[64];
    snprintf(rowHtml, sizeof rowHtml, "<tr><td>%s</td>", names[route]);
    if (!out.add(rowHtml)) {
      return false;
    }
    for (uint8_t weiche = 0; weiche < kWeichenAnzahl; ++weiche) {
      char field[8];
      snprintf(field, sizeof field, "r%uw%u", route, weiche);
      if (!out.add("<td>") || !selectField(out, field, row[weiche]) || !out.add("</td>")) {
        return false;
      }
    }
    char signalName[8];
    snprintf(signalName, sizeof signalName, "r%us", route);
    if (!out.add("<td>") || !signalField(out, signalName, signal) || !out.add("</td></tr>")) {
      return false;
    }
  }
  return out.add("</table><button type=submit>Speichern</button></form>") && out.add(kPlanScript) && out.add("</html>");
}

void sendPage(bool saved) {
  if (!buildPage(saved)) {
    server.send(500, "text/plain", "Seite zu gross");
    return;
  }
  server.send(200, "text/html", page);
}

bool readBegriff(const String& text, Begriff& begriff) {
  if (text == "frei") {
    begriff = Begriff::Frei;
    return true;
  }
  if (text == "frei40") {
    begriff = Begriff::Frei40;
    return true;
  }
  return false;
}

bool readLage(const String& text, int8_t& lage) {
  if (text == "-1") {
    lage = kWeicheBleibt;
    return true;
  }
  if (text == "0") {
    lage = 0;
    return true;
  }
  if (text == "1") {
    lage = 1;
    return true;
  }
  return false;
}

bool storeBytes(Preferences& prefs, const char* key, const void* data, size_t size) {
  return prefs.putBytes(key, data, size) == size;
}

bool speichern() {
  int8_t ausfahrt[3][kWeichenAnzahl];
  int8_t einfahrt[3][kWeichenAnzahl];
  Begriff ausfahrtSignal[3];
  Begriff einfahrtSignal[3];
  bool gleisAn[3];
  for (uint8_t gleis = 0; gleis < 3; ++gleis) {
    char name[4];
    snprintf(name, sizeof name, "g%u", gleis);
    gleisAn[gleis] = server.hasArg(name);
  }
  for (uint8_t route = 0; route < 6; ++route) {
    for (uint8_t weiche = 0; weiche < kWeichenAnzahl; ++weiche) {
      char field[8];
      snprintf(field, sizeof field, "r%uw%u", route, weiche);
      if (!server.hasArg(field)) {
        return false;
      }
      int8_t lage = 0;
      if (!readLage(server.arg(field), lage)) {
        return false;
      }
      if (route < 3) {
        ausfahrt[route][weiche] = lage;
      } else {
        einfahrt[route - 3][weiche] = lage;
      }
    }
    char signalName[8];
    snprintf(signalName, sizeof signalName, "r%us", route);
    if (!server.hasArg(signalName)) {
      return false;
    }
    Begriff begriff = Begriff::Halt;
    if (!readBegriff(server.arg(signalName), begriff)) {
      return false;
    }
    if (route < 3) {
      ausfahrtSignal[route] = begriff;
    } else {
      einfahrtSignal[route - 3] = begriff;
    }
  }
  if (hof == nullptr || !hof->setTabelle(ausfahrt, einfahrt, ausfahrtSignal, einfahrtSignal, gleisAn)) {
    return false;
  }
  int8_t raw[kTableBytes];
  uint8_t signale[kSignalBytes];
  uint8_t gleise[kGleisBytes];
  memcpy(raw, ausfahrt, sizeof ausfahrt);
  memcpy(raw + sizeof ausfahrt, einfahrt, sizeof einfahrt);
  for (uint8_t gleis = 0; gleis < 3; ++gleis) {
    signale[gleis] = static_cast<uint8_t>(ausfahrtSignal[gleis]);
    signale[3 + gleis] = static_cast<uint8_t>(einfahrtSignal[gleis]);
    gleise[gleis] = gleisAn[gleis] ? 1 : 0;
  }
  Preferences prefs;
  prefs.begin(kPrefsName, false);
  const bool stored = storeBytes(prefs, kPrefsKey, raw, sizeof raw) &&
                      storeBytes(prefs, kPrefsSignal, signale, sizeof signale) &&
                      storeBytes(prefs, kPrefsGleis, gleise, sizeof gleise);
  prefs.end();
  return stored;
}

}  // namespace

void weichenLaden(Bahnhof& ziel) {
  Preferences prefs;
  if (!prefs.begin(kPrefsName, false)) {
    return;
  }
  int8_t raw[kTableBytes];
  uint8_t signale[kSignalBytes];
  uint8_t gleise[kGleisBytes];
  const bool present = prefs.getBytes(kPrefsKey, raw, sizeof raw) == sizeof raw;
  const bool signaleDa = prefs.getBytes(kPrefsSignal, signale, sizeof signale) == sizeof signale;
  const bool gleiseDa = prefs.getBytes(kPrefsGleis, gleise, sizeof gleise) == sizeof gleise;
  prefs.end();
  if (!present) {
    return;
  }
  int8_t ausfahrt[3][kWeichenAnzahl];
  int8_t einfahrt[3][kWeichenAnzahl];
  Begriff ausfahrtSignal[3];
  Begriff einfahrtSignal[3];
  Begriff ausfahrtBisher[3];
  Begriff einfahrtBisher[3];
  bool gleisAn[3];
  ziel.tabelle(ausfahrt, einfahrt, ausfahrtSignal, einfahrtSignal, gleisAn);
  memcpy(ausfahrtBisher, ausfahrtSignal, sizeof ausfahrtBisher);
  memcpy(einfahrtBisher, einfahrtSignal, sizeof einfahrtBisher);
  memcpy(ausfahrt, raw, sizeof ausfahrt);
  memcpy(einfahrt, raw + sizeof ausfahrt, sizeof einfahrt);
  if (signaleDa) {
    for (uint8_t gleis = 0; gleis < 3; ++gleis) {
      ausfahrtSignal[gleis] = static_cast<Begriff>(signale[gleis]);
      einfahrtSignal[gleis] = static_cast<Begriff>(signale[3 + gleis]);
    }
  }
  if (gleiseDa) {
    for (uint8_t gleis = 0; gleis < 3; ++gleis) {
      if (gleise[gleis] <= 1) {
        gleisAn[gleis] = gleise[gleis] == 1;
      }
    }
  }
  if (!ziel.setTabelle(ausfahrt, einfahrt, ausfahrtSignal, einfahrtSignal, gleisAn)) {
    ziel.setTabelle(ausfahrt, einfahrt, ausfahrtBisher, einfahrtBisher, gleisAn);
  }
}

void weichenWebStarten(Bahnhof& ziel) {
  if (aktiv) {
    return;
  }
  hof = &ziel;
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(kWebSsid, kWebPass, kWifiChannel)) {
    Serial.println("WLAN-Zugangspunkt fehlgeschlagen");
    return;
  }
  Link::instance().noteAccessPoint();
  server.on("/", HTTP_GET, []() { sendPage(false); });
  server.on("/", HTTP_POST, []() {
    if (!speichern()) {
      server.send(400, "text/plain", "Ungueltige Stellung");
      return;
    }
    sendPage(true);
  });
  server.begin();
  aktiv = true;
  Serial.printf("Weichen-Web an  http://%s/  WLAN %s  Passwort %s\n", WiFi.softAPIP().toString().c_str(), kWebSsid,
                kWebPass);
}

void weichenWebLoop() {
  if (aktiv) {
    server.handleClient();
  }
}

}  // namespace bahn

#endif
