#include <Arduino.h>
#include <ArduinoJson.h>

#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define greenLedpin 14
#define redLedpin 12

StaticJsonDocument<200> doc;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char *ssid = "test-esp8266";
const char *password = "password";
char text[100];
int lastPingTimeMs = 0;
int pingIntervalMs = 0;

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
               size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("Client connected [%s][%u]\n", server->url(), client->id());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("Client disconnected [%s][%u]\n", server->url(), client->id());
    } else if (type == WS_EVT_ERROR) {
        Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t *) arg), (char *) data);
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *) arg;
        if (info->final && info->index == 0 && info->len == len) {

            data[len] = 0;
            Serial.printf("input: %s\n", (char *) data);

            DeserializationError error = deserializeJson(doc, (char *) data);

            if (error) {
                Serial.print("deserializeJson() failed: ");
                Serial.println(error.c_str());
                return;
            }

            lastPingTimeMs = millis();
            pingIntervalMs = doc["interval"];
            const char* timestamp = doc["timestamp"];

            sprintf(text, "{\"type\": \"pong\", \"timestamp\": %s}", timestamp);


            Serial.printf("output: %s\n", text);

            client->text(text);

        } else {
            Serial.println("Chunk message");
        }
    }
}

const char indexhtml[]
PROGMEM = R"rawliteral(
<!DOCTYPE html>
<head>
    <meta charset="utf-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge,chrome=1">
    <meta name="viewport" content="width=device-width, initial-scale=1 maximum-scale=1 user-scalable=no">
    <title>Test</title>
    <style>
        html {
            margin: 0;
            padding: 0;
            height: 100%;
        }

        body {
            margin: 0;
            padding: 0;
            height: 100%;
        }

        input {
            width: 100%;
            height: 30px;
            font-size: 14px
        }

        #js-status {
            text-align: center;
            padding-top: 20px;
            padding-bottom: 20px;
        }

        button {
            width: 100%;
            display: inline-block;
            height: 40px;
            font-size: 14px;
        }

        table {
            width: 100%;
            border-collapse: collapse;
        }

        td {
            padding: 5px;
            border: 1px solid black;
        }
    </style>
</head>
<body>
<div id="js-status" style="background-color: red">
    Disconnected
</div>
<br>
<div>
    <label for="js-interval">Req interval(ms)</label>
    <input id="js-interval" type="text" value="200">
</div>
<div>
    <label for="js-req-limit">Req limit</label>
    <input id="js-req-limit" type="text" value="200">
</div>
<br>
<div>
    <table>
        <tr>
            <td colspan="2">Duration(ms)</td>
            <td colspan="2" id="js-table-duration">0</td>
        </tr>
        <tr>
            <td>Req total</td>
            <td id="js-table-req-total">0</td>
            <td>Req failed</td>
            <td id="js-table-req-failed">0</td>
        </tr>
        <tr>
            <td>Req 50%</td>
            <td id="js-table-percent-50">0</td>
            <td>Req 95%</td>
            <td id="js-table-percent-95">0</td>
        </tr>
        <tr>
            <td>Req 75%</td>
            <td id="js-table-percent-75">0</td>
            <td>Req 99%</td>
            <td id="js-table-percent-99">0</td>
        </tr>
        <tr>
            <td>Req 90%</td>
            <td id="js-table-percent-90">0</td>
            <td>Req 100%</td>
            <td id="js-table-percent-100">0</td>
        </tr>
    </table>
</div>
<br>
<div>
    <button id="js-connect-btn" type="button">Connect</button>
</div>
<br>
<div>
    <button id="js-disconnect-btn" type="button" disabled="disabled">Disconnect</button>
</div>
<br>
<div id="logs"></div>
</body>

<script>

    let ws                   = null;
    let statusElement        = document.getElementById('js-status');
    let connectBtnElement    = document.getElementById('js-connect-btn');
    let disconnectBtnElement = document.getElementById('js-disconnect-btn');
    let intervalPing         = null;
    let url                  = 'ws://192.168.4.1/ws';
    let pingIntervalElement  = document.getElementById('js-interval');
    let reqLimitElement      = document.getElementById('js-req-limit');
    let enableLogger         = true;
    let requests             = {};
    let connectTimestamp     = 0;
    let disconnectTimestamp  = 0;

    let renderPacketsStatistic = function() {
        let total     = 0;
        let errors    = 0;
        let durations = [];
        for (let timestamp in requests) {
            if (!requests.hasOwnProperty(timestamp)) {
                continue;
            }

            total++;

            let request = requests[timestamp];

            if (!request.done) {
                errors++;
            }

            if (request.responseDuration !== 0) {
                durations.push(request.responseDuration);
            }
        }

        durations.sort(function(a, b) {
            return a - b;
        });

        let totalDurations = durations.length;

        [50, 75, 90, 95, 99].forEach(function(val) {
            let index = parseInt(val * totalDurations / 100);

            let element = document.getElementById('js-table-percent-' + val);

            if (index in durations) {
                element.innerText = durations[index] + ' ms';
            } else {
                element.innerText = 0;
            }
        });

        let lastIndex = totalDurations - 1;
        if (lastIndex in durations) {
            document.getElementById('js-table-percent-100').innerText = durations[lastIndex] + ' ms';
        } else {
            document.getElementById('js-table-percent-100').innerText = 0;
        }

        let stopTimestamp = disconnectTimestamp !== 0
            ? disconnectTimestamp
            : Date.now();

        document.getElementById('js-table-duration').innerText = (stopTimestamp - connectTimestamp);

        document.getElementById('js-table-req-total').innerText  = total;
        document.getElementById('js-table-req-failed').innerText = errors;
    };

    connectBtnElement.addEventListener('click', function() {
        connect();
        connectBtnElement.setAttribute('disabled', true);
        disconnectBtnElement.removeAttribute('disabled');
    });

    disconnectBtnElement.addEventListener('click', function() {
        setStatusDisconnected();
    });

    let setStatusConnected = function() {
        statusElement.setAttribute('style', 'background-color: green');
        statusElement.innerText = 'Connected';

        connectTimestamp = Date.now();

        let pingIntervalMs = parseInt(pingIntervalElement.value);

        intervalPing = setInterval(function() {

            log('Ping...');
            let timestamp = Date.now();
            ws.send(JSON.stringify({
                type:      'ping',
                interval:  pingIntervalMs,
                timestamp: timestamp.toString(),
            }));
            requests[timestamp] = {
                done:              false,
                requestTimestamp:  timestamp,
                responseTimestamp: 0,
                responseDuration:  0,
            };
            renderPacketsStatistic();

            if (Object.keys(requests).length >= parseInt(reqLimitElement.value)) {
                log('Stopped by limit');
                setStatusDisconnected();
            }

        }, pingIntervalMs);
    };

    let setStatusDisconnected = function() {
        statusElement.setAttribute('style', 'background-color: red');
        statusElement.innerText = 'Disconnected';
        disconnectTimestamp     = Date.now();
        clearInterval(intervalPing);
        disconnectBtnElement.setAttribute('disabled', true);
    };

    let connect = function() {
        ws           = new WebSocket(url);
        ws.onopen    = function(e) {
            log('Connected');
            setStatusConnected();
        };
        ws.onclose   = function(e) {
            log('Closed' + JSON.stringify(e));
            setStatusDisconnected();
        };
        ws.onmessage = function(e) {
            let data = JSON.parse(e.data);
            if (data.type !== 'pong') {
                return;
            }

            let timestamp = data.timestamp;

            if (!(timestamp in requests)) {
                return;
            }

            requests[timestamp].done              = true;
            requests[timestamp].responseTimestamp = Date.now();
            requests[timestamp].responseDuration  = requests[timestamp].responseTimestamp -
                requests[timestamp].requestTimestamp;
            renderPacketsStatistic();

            log('Pong');
        };
        ws.onerror   = function(e) {
            setStatusDisconnected();
            log('Error' + JSON.stringify(e));
        };
    };

    let log = function(message) {
        if (!enableLogger) {
            return;
        }

        let logs        = document.getElementById('logs');
        let block       = document.createElement('DIV');
        block.innerText = Date.now() + ' ' + message;
        logs.appendChild(block);

        while (logs.children.length > 10) {
            logs.removeChild(logs.firstChild);
        }
    };

    window.onerror = function(error, url, line) {
        log('Error:' + error + ' url:' + url + ' line:' + line);
    };
</script>
)rawliteral";


IPAddress IP;

void setup() {

    Serial.begin(115200);
    pinMode(greenLedpin, OUTPUT);
    pinMode(redLedpin, OUTPUT);

    digitalWrite(greenLedpin, LOW);
    digitalWrite(redLedpin, HIGH);

    IPAddress ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 254);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(ip, gateway, subnet);

    WiFi.softAP(ssid, password);

    IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    Serial.println(WiFi.localIP());

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET,[](AsyncWebServerRequest * request)
    {
        request->send(200, "text/html", indexhtml);
    });

    server.begin();
}

void loop() {
  if(millis() - lastPingTimeMs > (pingIntervalMs * 1.5)) {
    digitalWrite(greenLedpin, HIGH);
    digitalWrite(redLedpin, LOW);
  } else {
    digitalWrite(greenLedpin, LOW);
    digitalWrite(redLedpin, HIGH);
  }
}