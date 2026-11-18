// ============================================================
//  Horta inteligente — ESP32 + DHT22 + sensor capacitivo + relé
//  Painel local: rede "HortaInteligente" (senha cacto2026),
//  abrir http://192.168.4.1  (funciona sem internet)
//  Painel online: MQTT no broker.hivemq.com (precisa do Wi-Fi
//  com internet configurado em segredos.h)
//  Biblioteca extra: PubSubClient (Nick O'Leary)
// ============================================================
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <DHT.h>

// ---------- Pinos ----------
#define PINO_DHT  4
#define PINO_SOLO 34
#define PINO_RELE 26        // relé aciona com LOW (desliga com o pino "solto")

// ---------- Wi-Fi do próprio ESP32 ----------
const char* WIFI_NOME  = "HortaInteligente";
const char* WIFI_SENHA = "cacto2026";        // mínimo 8 caracteres

// ---------- Wi-Fi com internet (roteador do celular) ----------
// Nome e senha ficam em segredos.h (fora do Git). Copie segredos.exemplo.h
// para segredos.h e preencha com a rede que o ESP32 vai usar.
#include "segredos.h"

// ---------- MQTT ----------
const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORTA  = 1883;
const String TOPICO     = "uninassau/hortacacto-0e80";   // base dos tópicos
// Tópicos: TOPICO/estado (JSON), TOPICO/status (online/offline), TOPICO/comando

// ---------- Calibração do sensor de solo ----------
const int SECO    = 3290;   // leitura no ar
const int MOLHADO = 1025;   // leitura na água

// ---------- Regras de rega (cacto: pouca água) ----------
const int LIGA_ACIMA     = 2950;            // ~15% -> terra seca, liga
const int DESLIGA_ABAIXO = 2500;            // ~35% -> já molhou, desliga
const unsigned long MAX_LIGADA   = 10000;   // bomba no máximo 10 s por vez
const unsigned long PAUSA        = 20000;   // espera 20 s antes de regar de novo
const unsigned long REGA_MANUAL  = 3000;    // botão do dashboard: 3 s

// ---------- Objetos ----------
DHT dht(PINO_DHT, DHT22);
WebServer server(80);
WiFiClient redeCliente;
PubSubClient mqtt(redeCliente);
bool publicarJa = false;          // força envio imediato (ex.: bomba mudou)

// ---------- Estado ----------
float temperatura = NAN, umidadeAr = NAN;
int   soloBruto = 0, soloPct = 0;
bool  sensorOk = false;
bool  bombaLigada = false, regaManual = false, modoAuto = true;
unsigned long inicioBomba = 0, pausaAte = 0, ultimaRega = 0;
unsigned int  totalRegas = 0;
String motivo = "Aguardando primeira leitura";

// Histórico: 72 pontos, 1 a cada 10 s = últimos 12 minutos
const int HIST_N = 72;
uint8_t historico[HIST_N];
int histQtd = 0;

// ---------- Relé ----------
void releLigar()    { pinMode(PINO_RELE, OUTPUT); digitalWrite(PINO_RELE, LOW); }
void releDesligar() { pinMode(PINO_RELE, INPUT); }

void ligarBomba(bool manual) {
  if (bombaLigada) return;
  releLigar();
  bombaLigada = true;
  regaManual  = manual;
  inicioBomba = millis();
  totalRegas++;
  motivo = manual ? "Rega manual pelo painel" : "Terra seca: rega automática";
  publicarJa = true;
  Serial.println(manual ? ">> Bomba ON (manual)" : ">> Bomba ON (solo seco)");
}

void desligarBomba(const String& porque) {
  if (!bombaLigada) return;
  releDesligar();
  bombaLigada = false;
  ultimaRega  = millis();
  pausaAte    = millis() + PAUSA;
  motivo      = porque;
  publicarJa = true;
  Serial.println(">> Bomba OFF (" + porque + ")");
}

// ---------- Leituras ----------
void lerSolo() {
  long soma = 0;
  for (int i = 0; i < 16; i++) { soma += analogRead(PINO_SOLO); delay(2); }
  soloBruto = soma / 16;
  soloPct   = constrain(map(soloBruto, SECO, MOLHADO, 0, 100), 0, 100);
  sensorOk  = (soloBruto > 700 && soloBruto < 3700);
}

void lerDHT() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) { temperatura = t; umidadeAr = h; }
  else { temperatura = NAN; umidadeAr = NAN; }
}

// ---------- Controle da bomba ----------
void controlarBomba() {
  unsigned long agora = millis();

  if (bombaLigada) {
    unsigned long limite = regaManual ? REGA_MANUAL : MAX_LIGADA;
    if (agora - inicioBomba >= limite)
      desligarBomba(regaManual ? "Rega manual concluída" : "Limite de 10 s atingido");
    else if (!regaManual && soloBruto < DESLIGA_ABAIXO)
      desligarBomba("Terra molhada o suficiente");
    else if (!regaManual && !sensorOk)
      desligarBomba("Sensor de solo sem leitura");
    return;
  }

  if (!sensorOk) { motivo = "Sensor de solo sem leitura: confira o fio no D34"; return; }
  if (!modoAuto) { motivo = "Rega automática pausada"; return; }
  if ((long)(agora - pausaAte) < 0) { motivo = "Esperando a água infiltrar"; return; }

  if (soloBruto > LIGA_ACIMA) ligarBomba(false);
  else motivo = "Umidade boa, sem necessidade de regar";
}

void guardarHistorico() {
  uint8_t v = sensorOk ? soloPct : 255;          // 255 = sem leitura
  if (histQtd < HIST_N) historico[histQtd++] = v;
  else { memmove(historico, historico + 1, HIST_N - 1); historico[HIST_N - 1] = v; }
}

// ---------- API ----------
String num(float v, int casas) { return isnan(v) ? "null" : String(v, casas); }

String montarJSON() {
  unsigned long agora = millis();
  String j; j.reserve(900);
  j += "{\"t\":" + num(temperatura, 1);
  j += ",\"h\":" + num(umidadeAr, 1);
  j += ",\"raw\":" + String(soloBruto);
  j += ",\"pct\":" + String(soloPct);
  j += ",\"ok\":" + String(sensorOk ? "true" : "false");
  j += ",\"pump\":" + String(bombaLigada ? "true" : "false");
  j += ",\"auto\":" + String(modoAuto ? "true" : "false");
  j += ",\"regas\":" + String(totalRegas);
  j += ",\"ultima\":" + String(ultimaRega ? (long)((agora - ultimaRega) / 1000) : -1);
  j += ",\"pausa\":" + String((long)(pausaAte - agora) > 0 ? (long)((pausaAte - agora) / 1000) : 0);
  j += ",\"liga\":" + String(constrain(map(LIGA_ACIMA, SECO, MOLHADO, 0, 100), 0, 100));
  j += ",\"desliga\":" + String(constrain(map(DESLIGA_ABAIXO, SECO, MOLHADO, 0, 100), 0, 100));
  j += ",\"motivo\":\"" + motivo + "\"";
  j += ",\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  j += ",\"mqtt\":" + String(mqtt.connected() ? "true" : "false");
  j += ",\"hist\":[";
  for (int i = 0; i < histQtd; i++) { if (i) j += ","; j += String(historico[i]); }
  j += "]}";
  return j;
}

// ---------- Página do dashboard ----------
const char PAGINA[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Horta do cacto</title>
<style>
:root{--fundo:#DCE4D2;--tinta:#1E2D1C;--suave:#56664F;--cacto:#3E7B4F;--terra:#7A5536;--agua:#3D8FD6;--sol:#D98E1F;--painel:#EDF1E6;--alerta:#B4412E}
*{box-sizing:border-box;margin:0}
body{background:var(--fundo);color:var(--tinta);font-family:"Segoe UI Variable Text","Segoe UI",system-ui,-apple-system,Roboto,sans-serif;line-height:1.5}
main{max-width:860px;margin:0 auto;padding:28px 20px 48px}
header{display:flex;justify-content:space-between;align-items:baseline;flex-wrap:wrap;gap:8px;margin-bottom:20px}
h1{font-size:1.6rem;font-weight:600;letter-spacing:-.01em}
#st{font-size:.9rem;color:var(--suave)}#st.err{color:var(--alerta)}
.vaso{display:grid;grid-template-columns:minmax(180px,260px) 1fr;gap:28px;align-items:center;background:var(--painel);border-radius:28px;padding:24px}
.vaso svg{width:100%;height:auto}
.grande{font-size:clamp(4rem,14vw,7.5rem);font-weight:300;line-height:1;font-variant-numeric:tabular-nums;letter-spacing:-.04em}
.grande small{font-size:.35em;font-weight:400;letter-spacing:0;margin-left:4px}
.sub{color:var(--suave);margin-top:4px}
#msg{margin-top:16px;font-size:1.1rem;font-weight:600}
#motivo{color:var(--suave);font-size:.95rem}
.dados{display:grid;grid-template-columns:repeat(4,1fr);gap:1px;background:#C4CEB9;border-radius:18px;overflow:hidden;margin:18px 0}
.dados div{background:var(--painel);padding:16px}
.dados span{display:block;color:var(--suave);font-size:.85rem}
.dados b{font-size:1.6rem;font-weight:500;font-variant-numeric:tabular-nums}
#bomba.on{color:var(--agua)}
.hist{background:var(--painel);border-radius:18px;padding:18px}
.hist h2,.ctl h2{font-size:1rem;font-weight:600;margin-bottom:8px}
.hist svg{width:100%;height:110px;display:block}
.ctl{display:flex;flex-wrap:wrap;gap:16px;align-items:center;margin-top:18px}
button{font:inherit;font-weight:600;font-size:1.05rem;background:var(--agua);color:#fff;border:0;border-radius:999px;padding:14px 26px;cursor:pointer}
button:disabled{background:#9DB3C6;cursor:not-allowed}
button:focus-visible,input:focus-visible{outline:3px solid var(--tinta);outline-offset:3px}
label{display:flex;gap:10px;align-items:center;cursor:pointer}
input[type=checkbox]{width:22px;height:22px;accent-color:var(--cacto)}
.aviso{flex-basis:100%;color:var(--suave);font-size:.9rem}
#aguaNivel{transition:y .8s ease,height .8s ease}
@media (max-width:620px){.vaso{grid-template-columns:1fr;text-align:center}.vaso svg{max-width:220px;margin:0 auto}.dados{grid-template-columns:repeat(2,1fr)}}
@media (prefers-reduced-motion:reduce){#aguaNivel{transition:none}}
</style></head><body><main>
<header><h1>Horta do cacto</h1><p id="st" role="status">Conectando à horta…</p></header>

<section class="vaso" aria-label="Umidade do solo">
<svg viewBox="0 0 220 230" role="img" aria-label="Vaso com nível de umidade">
<defs><clipPath id="corte"><path d="M48 112H172L156 218H64Z"/></clipPath></defs>
<rect x="100" y="34" width="22" height="82" rx="11" fill="#3E7B4F"/>
<path d="M100 78H86a9 9 0 0 1-9-9V54a6 6 0 0 1 12 0v12h11" fill="#3E7B4F"/>
<path d="M122 66h12V48a6 6 0 0 1 12 0v18a9 9 0 0 1-9 9h-15" fill="#3E7B4F"/>
<g clip-path="url(#corte)">
<rect x="0" y="112" width="220" height="110" fill="#7A5536"/>
<rect id="aguaNivel" x="0" y="218" width="220" height="0" fill="#3D8FD6" opacity=".55"/>
<line id="lLiga" x1="40" x2="180" stroke="#F4E7D0" stroke-width="1.5" stroke-dasharray="4 4"/>
<line id="lDesl" x1="40" x2="180" stroke="#F4E7D0" stroke-width="1.5" stroke-dasharray="4 4"/>
</g>
<path d="M44 106H176V116H44Z" fill="#9C6B45"/>
<path d="M48 112H172L156 218H64Z" fill="none" stroke="#5B3E27" stroke-width="3"/>
<text id="tLiga" x="182" font-size="11" fill="#56664F">rega</text>
<text id="tDesl" x="182" font-size="11" fill="#56664F">para</text>
</svg>
<div>
<div class="grande"><span id="pct">--</span><small>%</small></div>
<p class="sub">umidade da terra no vaso</p>
<p id="msg">Lendo o sensor…</p>
<p id="motivo"></p>
</div>
</section>

<section class="dados">
<div><span>Temperatura do ar</span><b id="t">--</b></div>
<div><span>Umidade do ar</span><b id="h">--</b></div>
<div><span>Bomba</span><b id="bomba">--</b></div>
<div><span>Regas desde que ligou</span><b id="regas">--</b></div>
</section>

<section class="hist">
<h2>Umidade da terra nos últimos 12 minutos</h2>
<svg viewBox="0 0 300 100" preserveAspectRatio="none" aria-label="Gráfico de umidade do solo">
<rect id="faixa" x="0" width="300" fill="#3D8FD6" opacity=".12"/>
<polyline id="linha" fill="none" stroke="#7A5536" stroke-width="2" vector-effect="non-scaling-stroke"/>
</svg>
<p id="ultima" class="sub"></p>
</section>

<section class="ctl">
<button id="regar">Regar agora por 3 s</button>
<label><input type="checkbox" id="auto" checked> Rega automática</label>
<p class="aviso">Antes de regar, confira se a bomba está coberta de água na garrafa.</p>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
const nivelY=p=>218-p*1.06;
function tempo(s){if(s<0)return'Ainda não regou desde que ligou.';if(s<60)return'Última rega há '+s+' s.';return'Última rega há '+Math.round(s/60)+' min.';}
function desenhar(d){
  $('pct').textContent=d.ok?d.pct:'--';
  const a=$('aguaNivel'),y=nivelY(d.ok?d.pct:0);a.setAttribute('y',y);a.setAttribute('height',218-y);
  for(const[k,v]of[['Liga',d.liga],['Desl',d.desliga]]){const yy=nivelY(v);$('l'+k).setAttribute('y1',yy);$('l'+k).setAttribute('y2',yy);$('t'+k).setAttribute('y',yy+4);}
  $('msg').textContent=!d.ok?'Sensor de solo sem leitura: confira o fio amarelo no D34.':d.pump?'Regando agora.':d.pct<d.liga?'Terra seca.':d.pct<d.desliga?'Terra levemente úmida, boa para o cacto.':'Terra molhada: o cacto não precisa de água.';
  $('motivo').textContent=d.motivo+(d.pausa>0?' ('+d.pausa+' s)':'');
  $('t').textContent=d.t===null?'sem leitura':d.t.toFixed(1)+' °C';
  $('h').textContent=d.h===null?'sem leitura':Math.round(d.h)+' %';
  $('bomba').textContent=d.pump?'Ligada':'Desligada';$('bomba').className=d.pump?'on':'';
  $('regas').textContent=d.regas;
  $('ultima').textContent=tempo(d.ultima);
  $('regar').disabled=d.pump;
  if(document.activeElement!==$('auto'))$('auto').checked=d.auto;
  const f=$('faixa');f.setAttribute('y',100-d.desliga);f.setAttribute('height',d.desliga-d.liga);
  const pts=[];d.hist.forEach((v,i)=>{if(v<=100)pts.push((i*300/71).toFixed(1)+','+(100-v));});
  $('linha').setAttribute('points',pts.join(' '));
}
async function atualizar(){
  try{const r=await fetch('/api',{cache:'no-store'});const d0=await r.json();desenhar(d0);$('st').textContent=d0.mqtt?'Atualizado agora. Enviando dados por MQTT.':d0.wifi?'Atualizado agora. Wi-Fi ok, MQTT conectando.':'Atualizado agora. Sem internet: rega local ativa.';$('st').className='';}
  catch(e){$('st').textContent='Sem conexão: confira se o celular está na rede HortaInteligente.';$('st').className='err';}
}
$('regar').onclick=async()=>{$('regar').disabled=true;try{desenhar(await(await fetch('/regar',{method:'POST'})).json());}catch(e){}};
$('auto').onchange=async e=>{try{desenhar(await(await fetch('/auto?on='+(e.target.checked?1:0),{method:'POST'})).json());}catch(x){}};
atualizar();setInterval(atualizar,2000);
</script></body></html>)HTML";

// ---------- MQTT ----------
void aoReceberMQTT(char* topico, byte* dados, unsigned int tam) {
  String msg; for (unsigned int i = 0; i < tam; i++) msg += (char)dados[i];
  msg.trim();
  Serial.println("MQTT comando: " + msg);
  if (msg == "regar") ligarBomba(true);
  else if (msg == "auto:1") modoAuto = true;
  else if (msg == "auto:0") { modoAuto = false; if (bombaLigada && !regaManual) desligarBomba("Rega automática pausada"); }
  publicarJa = true;
}

void cuidarDaRede() {
  static unsigned long tWifi = 0, tMqtt = 0, tPub = 0;
  unsigned long agora = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (agora - tWifi >= 30000) { tWifi = agora; WiFi.disconnect(); WiFi.begin(REDE_NOME, REDE_SENHA); }
    return;                                   // sem internet: a rega local segue normal
  }

  if (!mqtt.connected()) {
    // nunca tenta conectar com a bomba ligada (a conexão pode travar uns segundos)
    if (!bombaLigada && agora - tMqtt >= 5000) {
      tMqtt = agora;
      String id = "horta-" + String((uint32_t)ESP.getEfuseMac(), HEX);
      if (mqtt.connect(id.c_str(), (TOPICO + "/status").c_str(), 1, true, "offline")) {
        mqtt.publish((TOPICO + "/status").c_str(), "online", true);
        mqtt.subscribe((TOPICO + "/comando").c_str());
        Serial.println("MQTT conectado em " + String(MQTT_BROKER));
        publicarJa = true;
      } else {
        Serial.println("MQTT falhou (codigo " + String(mqtt.state()) + "), tentando de novo em 5 s");
      }
    }
    return;
  }

  mqtt.loop();
  if (publicarJa || agora - tPub >= 5000) {
    tPub = agora; publicarJa = false;
    mqtt.publish((TOPICO + "/estado").c_str(), montarJSON().c_str(), true);
  }
}

// ---------- Rotas ----------
void rotaPagina() { server.send_P(200, "text/html; charset=utf-8", PAGINA); }
void rotaAPI()    { server.send(200, "application/json", montarJSON()); }
void rotaRegar()  { ligarBomba(true); rotaAPI(); }
void rotaAuto() {
  modoAuto = server.arg("on") == "1";
  if (!modoAuto && bombaLigada && !regaManual) desligarBomba("Rega automática pausada");
  rotaAPI();
}

// ---------- Setup / Loop ----------
void setup() {
  releDesligar();                       // primeira coisa: bomba desligada
  Serial.begin(115200);
  dht.begin();
  analogReadResolution(12);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_NOME, WIFI_SENHA);
  WiFi.begin(REDE_NOME, REDE_SENHA);
  mqtt.setServer(MQTT_BROKER, MQTT_PORTA);
  mqtt.setCallback(aoReceberMQTT);
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(3);
  Serial.println();
  Serial.println("Horta inteligente iniciada");
  Serial.print("Wi-Fi: "); Serial.print(WIFI_NOME);
  Serial.print(" | senha: "); Serial.print(WIFI_SENHA);
  Serial.print(" | painel: http://"); Serial.println(WiFi.softAPIP());

  server.on("/", rotaPagina);
  server.on("/api", rotaAPI);
  server.on("/regar", HTTP_POST, rotaRegar);
  server.on("/auto", HTTP_POST, rotaAuto);
  server.begin();

  lerSolo(); lerDHT(); guardarHistorico();
}

void loop() {
  server.handleClient();
  cuidarDaRede();
  unsigned long agora = millis();
  static unsigned long tSolo = 0, tDHT = 0, tHist = 0, tSerial = 0;

  if (agora - tSolo >= 500)    { tSolo = agora;   lerSolo(); controlarBomba(); }
  if (agora - tDHT >= 2500)    { tDHT = agora;    lerDHT(); }
  if (agora - tHist >= 10000)  { tHist = agora;   guardarHistorico(); }
  if (agora - tSerial >= 2000) {
    tSerial = agora;
    Serial.print("Temp: ");  Serial.print(isnan(temperatura) ? String("erro") : String(temperatura, 1));
    Serial.print(" C | Ar: "); Serial.print(isnan(umidadeAr) ? String("erro") : String(umidadeAr, 1));
    Serial.print(" % | Solo: "); Serial.print(soloBruto);
    Serial.print(" ("); Serial.print(soloPct); Serial.print("%)");
    if (!sensorOk) Serial.print(" SENSOR?");
    Serial.print(" | Bomba: "); Serial.print(bombaLigada ? "ON" : "OFF");
    Serial.print(" | Internet: "); Serial.print(WiFi.status() == WL_CONNECTED ? "ok" : "sem");
    Serial.print(" | MQTT: "); Serial.print(mqtt.connected() ? "ok" : "sem");
    Serial.print(" | "); Serial.println(motivo);
  }
}
