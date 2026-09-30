#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------- WiFi ----------
const char* ssid = "WIFI";
const char* password = "SENHA";

// ---------- Configuracao do modo AP (fallback) ----------
const char* AP_SSID = "QuickMast";
const char* AP_PASSWORD = "";
const unsigned long WIFI_TIMEOUT_MS = 8000;

// ---------- Pinos ----------
#define ONE_WIRE_BUS D2
#define EC_PIN A0

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
ESP8266WebServer server(80);

// ---------- Parametros de referencia (ajustar apos calibracao real) ----------
const float TEMP_REF = 40.0;
const float COND_REF = 400.0;
const float MARGIN = 1.20;
const float REL_THRESHOLD = 0.20;
const float MIN_ABS_DIFF_TEMP = 1.0;
const float MIN_ABS_DIFF_COND = 50.0;

// ---------- Estado do teste ----------
bool testRunning = false;
int currentTeat = 0;

float temps[4] = {0, 0, 0, 0};
float conds[4] = {0, 0, 0, 0};
bool captured[4] = {false, false, false, false};
bool mastiteDetectado[4] = {false, false, false, false};
bool resultadosCalculados = false;

const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Detector de Mastite</title>
<style>
  body { font-family: Arial, sans-serif; background:#f2f2f2; margin:0; padding:20px; }
  h1 { text-align:center; color:#333; font-size:22px; }
  .status { text-align:center; font-size:16px; margin-bottom:15px; color:#555; }
  .buttons { display:flex; flex-wrap:wrap; gap:10px; justify-content:center; margin-bottom:20px; }
  button { padding:12px 18px; font-size:15px; border:none; border-radius:6px; cursor:pointer; color:#fff; }
  #btnIniciar { background:#2e7d32; }
  #btnCapturar { background:#1565c0; }
  #btnParar { background:#e65100; }
  #btnReiniciar { background:#616161; }
  #btnLeitura { background:#6a1b9a; }
  table { width:100%; border-collapse:collapse; background:#fff; border-radius:6px; overflow:hidden; }
  th, td { padding:10px; text-align:center; border-bottom:1px solid #ddd; }
  th { background:#333; color:#fff; }
  .ok { color:#2e7d32; font-weight:bold; }
  .alerta { color:#c62828; font-weight:bold; }
  .aguardando { color:#999; }
  #warning { display:none; background:#c62828; color:#fff; padding:15px; border-radius:6px; text-align:center; margin-top:15px; font-weight:bold; }
  #leituraPanel { display:none; position:relative; background:#ede7f6; border:1px solid #6a1b9a; border-radius:6px; padding:12px 36px 12px 12px; text-align:center; margin-bottom:20px; font-size:15px; color:#4a148c; }
  #fecharLeitura { position:absolute; top:6px; right:10px; cursor:pointer; font-size:18px; font-weight:bold; color:#4a148c; }
</style>
</head>
<body>
  <h1>Detector de Mastite</h1>
  <div class="status" id="status">Teste nao iniciado</div>

  <div class="buttons">
    <button id="btnLeitura" onclick="lerSensores()">Ler Sensores</button>
  </div>
  <div id="leituraPanel">
    <span id="fecharLeitura" onclick="fecharLeitura()">&times;</span>
    Temperatura atual: <span id="tempAtual">--</span> C | Condutividade atual: <span id="condAtual">--</span>
  </div>

  <div class="buttons">
    <button id="btnIniciar" onclick="iniciar()">Iniciar Teste</button>
    <button id="btnCapturar" onclick="capturar()">Capturar Amostra</button>
    <button id="btnParar" onclick="parar()">Parar Teste</button>
    <button id="btnReiniciar" onclick="reiniciar()">Reiniciar Teste</button>
  </div>

  <table>
    <tr><th>Teto</th><th>Temperatura (C)</th><th>Condutividade</th><th>Status</th></tr>
    <tr><td>1</td><td id="t1">-</td><td id="c1">-</td><td id="s1" class="aguardando">Aguardando</td></tr>
    <tr><td>2</td><td id="t2">-</td><td id="c2">-</td><td id="s2" class="aguardando">Aguardando</td></tr>
    <tr><td>3</td><td id="t3">-</td><td id="c3">-</td><td id="s3" class="aguardando">Aguardando</td></tr>
    <tr><td>4</td><td id="t4">-</td><td id="c4">-</td><td id="s4" class="aguardando">Aguardando</td></tr>
  </table>

  <div id="warning"></div>

<script>
function atualizarTela(data) {
  document.getElementById('status').innerText = data.statusMsg;
  for (let i = 1; i <= 4; i++) {
    let statusEl = document.getElementById('s' + i);
    if (data.captured[i-1]) {
      document.getElementById('t' + i).innerText = data.temps[i-1].toFixed(1);
      document.getElementById('c' + i).innerText = data.conds[i-1];
      if (data.resultadosCalculados) {
        if (data.mastite[i-1]) {
          statusEl.innerText = "MASTITE";
          statusEl.className = "alerta";
        } else {
          statusEl.innerText = "Normal";
          statusEl.className = "ok";
        }
      } else {
        statusEl.innerText = "Capturado";
        statusEl.className = "ok";
      }
    } else {
      document.getElementById('t' + i).innerText = "-";
      document.getElementById('c' + i).innerText = "-";
      statusEl.innerText = "Aguardando";
      statusEl.className = "aguardando";
    }
  }
  let warningDiv = document.getElementById('warning');
  if (data.resultadosCalculados && data.temMastite) {
    warningDiv.style.display = "block";
    warningDiv.innerText = "ALERTA: MASTITE DETECTADA no(s) teto(s): " + data.tetosAfetados.join(", ");
  } else {
    warningDiv.style.display = "none";
  }
}

function chamar(rota) {
  fetch(rota)
    .then(res => res.json())
    .then(data => atualizarTela(data))
    .catch(err => alert("Erro de comunicacao com a placa"));
}

function lerSensores() {
  fetch('/read')
    .then(res => res.json())
    .then(data => {
      document.getElementById('tempAtual').innerText = data.temp.toFixed(1);
      document.getElementById('condAtual').innerText = data.cond;
      document.getElementById('leituraPanel').style.display = "block";
    })
    .catch(err => alert("Erro de comunicacao com a placa"));
}

function fecharLeitura() {
  document.getElementById('leituraPanel').style.display = "none";
}

function iniciar() { chamar('/start'); }
function capturar() { chamar('/capture'); }
function parar() { chamar('/stop'); }
function reiniciar() { chamar('/reset'); }

window.onload = function() { chamar('/status'); };
</script>
</body>
</html>
)rawliteral";

float medianaOutros(float arr[4], int idx) {
  float outros[3];
  int k = 0;
  for (int i = 0; i < 4; i++) {
    if (i != idx) outros[k++] = arr[i];
  }
  for (int i = 0; i < 2; i++) {
    for (int j = i + 1; j < 3; j++) {
      if (outros[j] < outros[i]) {
        float tmp = outros[i];
        outros[i] = outros[j];
        outros[j] = tmp;
      }
    }
  }
  return outros[1];
}

void calcularResultados() {
  for (int i = 0; i < 4; i++) {
    float medTempOutros = medianaOutros(temps, i);
    float medCondOutros = medianaOutros(conds, i);
    float diffTemp = temps[i] - medTempOutros;
    float diffCond = conds[i] - medCondOutros;
    float relTemp = (medTempOutros != 0) ? diffTemp / medTempOutros : 0;
    float relCond = (medCondOutros != 0) ? diffCond / medCondOutros : 0;
    bool tempFlag = (diffTemp > MIN_ABS_DIFF_TEMP) && (relTemp > REL_THRESHOLD);
    bool condFlag = (diffCond > MIN_ABS_DIFF_COND) && (relCond > REL_THRESHOLD);
    bool relFlag = tempFlag || condFlag;
    bool absFlag = (temps[i] > TEMP_REF * MARGIN) || (conds[i] > COND_REF * MARGIN);
    mastiteDetectado[i] = relFlag || absFlag;
  }
  resultadosCalculados = true;
}

String buildStatusJson() {
  String statusMsg;
  if (!testRunning && currentTeat == 0) {
    statusMsg = "Teste nao iniciado";
  } else if (testRunning && currentTeat <= 4) {
    statusMsg = "Aguardando captura do teto " + String(currentTeat);
  } else if (!testRunning && resultadosCalculados) {
    statusMsg = "Teste concluido";
  } else {
    statusMsg = "Teste parado";
  }
  bool temMastite = false;
  String tetosAfetados = "";
  for (int i = 0; i < 4; i++) {
    if (mastiteDetectado[i]) {
      temMastite = true;
      if (tetosAfetados.length() > 0) tetosAfetados += ",";
      tetosAfetados += String(i + 1);
    }
  }
  String json = "{";
  json += "\"statusMsg\":\"" + statusMsg + "\",";
  json += "\"testRunning\":" + String(testRunning ? "true" : "false") + ",";
  json += "\"currentTeat\":" + String(currentTeat) + ",";
  json += "\"resultadosCalculados\":" + String(resultadosCalculados ? "true" : "false") + ",";
  json += "\"temMastite\":" + String(temMastite ? "true" : "false") + ",";
  json += "\"temps\":[" + String(temps[0],1) + "," + String(temps[1],1) + "," + String(temps[2],1) + "," + String(temps[3],1) + "],";
  json += "\"conds\":[" + String(conds[0],0) + "," + String(conds[1],0) + "," + String(conds[2],0) + "," + String(conds[3],0) + "],";
  json += "\"captured\":[" + String(captured[0]?"true":"false") + "," + String(captured[1]?"true":"false") + "," + String(captured[2]?"true":"false") + "," + String(captured[3]?"true":"false") + "],";
  json += "\"mastite\":[" + String(mastiteDetectado[0]?"true":"false") + "," + String(mastiteDetectado[1]?"true":"false") + "," + String(mastiteDetectado[2]?"true":"false") + "," + String(mastiteDetectado[3]?"true":"false") + "],";
  json += "\"tetosAfetados\":[" + tetosAfetados + "]";
  json += "}";
  return json;
}

void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}

void handleStart() {
  testRunning = true;
  currentTeat = 1;
  resultadosCalculados = false;
  for (int i = 0; i < 4; i++) {
    temps[i] = 0; conds[i] = 0; captured[i] = false; mastiteDetectado[i] = false;
  }
  server.send(200, "application/json", buildStatusJson());
}

void handleCapture() {
  if (!testRunning || currentTeat < 1 || currentTeat > 4) {
    server.send(200, "application/json", buildStatusJson());
    return;
  }
  sensors.requestTemperatures();
  float temperatura = sensors.getTempCByIndex(0);
  int condutividade = analogRead(EC_PIN);
  if (temperatura == DEVICE_DISCONNECTED_C) temperatura = 0;
  int idx = currentTeat - 1;
  temps[idx] = temperatura;
  conds[idx] = condutividade;
  captured[idx] = true;
  if (currentTeat == 4) {
    calcularResultados();
    testRunning = false;
    currentTeat = 5;
  } else {
    currentTeat++;
  }
  server.send(200, "application/json", buildStatusJson());
}

void handleStop() {
  testRunning = false;
  server.send(200, "application/json", buildStatusJson());
}

void handleReset() {
  testRunning = false;
  currentTeat = 0;
  resultadosCalculados = false;
  for (int i = 0; i < 4; i++) {
    temps[i] = 0; conds[i] = 0; captured[i] = false; mastiteDetectado[i] = false;
  }
  server.send(200, "application/json", buildStatusJson());
}

void handleStatus() {
  server.send(200, "application/json", buildStatusJson());
}

void handleRead() {
  sensors.requestTemperatures();
  float temperatura = sensors.getTempCByIndex(0);
  int condutividade = analogRead(EC_PIN);
  if (temperatura == DEVICE_DISCONNECTED_C) temperatura = 0;

  String json = "{";
  json += "\"temp\":" + String(temperatura, 1) + ",";
  json += "\"cond\":" + String(condutividade);
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  sensors.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Conectado ao WiFi! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Falha ao conectar. Iniciando modo AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.print("Rede AP criada: ");
    Serial.println(AP_SSID);
    Serial.print("IP do AP: ");
    Serial.println(WiFi.softAPIP());
  }

  if (MDNS.begin("quickmast")) {
    Serial.println("mDNS iniciado: http://quickmast.local");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("Erro ao iniciar mDNS");
  }

  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/capture", handleCapture);
  server.on("/stop", handleStop);
  server.on("/reset", handleReset);
  server.on("/status", handleStatus);
  server.on("/read", handleRead);

  server.begin();
  Serial.println("Servidor web iniciado");
}

void loop() {
  MDNS.update();
  server.handleClient();
}