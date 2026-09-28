#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>

#define REED_PIN 27
#define RELAY_PIN 26
#define RELAY_ON HIGH
#define RELAY_OFF LOW

const char* AP_NAME="ESP32-SETUP";
const char* AP_PASS="12345678";
WebServer server(80);
Preferences prefs;
unsigned long relayTimeMs=7000;
bool relayActive=false;
unsigned long relayStarted=0;
int lastReedState=HIGH, stableReedState=HIGH;
unsigned long debounceStarted=0;
bool otaFailed=false;
String otaError="";

void relayOn(){digitalWrite(RELAY_PIN,RELAY_ON);relayActive=true;relayStarted=millis();}
void relayOff(){digitalWrite(RELAY_PIN,RELAY_OFF);relayActive=false;}
void loadSettings(){prefs.begin("relaycfg",true);relayTimeMs=prefs.getULong("time",7000);prefs.end();}
void saveSettings(){prefs.begin("relaycfg",false);prefs.putULong("time",relayTimeMs);prefs.end();}

String mainPage(){
String h=R"rawliteral(<!DOCTYPE html><html lang="ru"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32 Реле</title><style>body{background:#111;color:#fff;font-family:Arial;text-align:center;padding:20px}.card{max-width:420px;margin:auto;background:#222;padding:25px;border-radius:20px}input{width:85%;height:48px;margin:8px;font-size:20px;border-radius:10px;padding:0 10px}button{width:90%;min-height:58px;margin:8px;font-size:20px;border:none;border-radius:12px;color:#fff}.green{background:#28a745}.black{background:#000;border:1px solid #555}.gray{background:#444}a{text-decoration:none}.small{color:#bbb;font-size:14px}</style></head><body><div class="card"><h1>ESP32 Реле</h1><p>Реле: <b>%RELAY%</b></p><p>Геркон: <b>%REED%</b></p><hr><form action="/save" method="GET"><h3>Время работы реле</h3><input type="number" name="seconds" min="1" max="3600" value="%SECONDS%" required><p>секунд</p><button class="green" type="submit">СОХРАНИТЬ</button></form><hr><a href="/test"><button class="green">ВКЛ НА ЗАДАННОЕ ВРЕМЯ</button></a><a href="/off"><button class="black">ВЫКЛ</button></a><hr><a href="/update"><button class="gray">ОБНОВИТЬ ПРОШИВКУ</button></a><p class="small">Магнит поднесён → реле включается один раз на заданное время.<br>Чтобы сработало снова, магнит нужно убрать и поднести заново.</p></div></body></html>)rawliteral";
h.replace("%SECONDS%",String(relayTimeMs/1000));h.replace("%RELAY%",relayActive?"ВКЛ":"ВЫКЛ");h.replace("%REED%",stableReedState==LOW?"МАГНИТ ПОДНЕСЁН":"МАГНИТА НЕТ");return h;}

String updatePage(){return R"rawliteral(<!DOCTYPE html><html lang="ru"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Обновление ESP32</title><style>body{background:#111;color:#fff;font-family:Arial;text-align:center;padding:20px}.card{max-width:420px;margin:auto;background:#222;padding:25px;border-radius:20px}input{width:90%;margin:20px 0;font-size:17px}button{width:90%;min-height:58px;font-size:20px;color:#fff;background:#28a745;border:0;border-radius:12px}a{color:#28a745}</style></head><body><div class="card"><h1>Обновление ESP32</h1><p>Выберите файл <b>ESP32_Relay_OTA.bin</b></p><form method="POST" action="/update" enctype="multipart/form-data"><input type="file" name="firmware" accept=".bin" required><br><button type="submit">ЗАГРУЗИТЬ ПРОШИВКУ</button></form><br><a href="/">Назад</a></div></body></html>)rawliteral";}

void handleRoot(){server.send(200,"text/html; charset=utf-8",mainPage());}
void handleSave(){if(server.hasArg("seconds")){unsigned long s=server.arg("seconds").toInt();if(s<1)s=1;if(s>3600)s=3600;relayTimeMs=s*1000UL;saveSettings();}server.sendHeader("Location","/");server.send(303,"text/plain","");}
void handleTest(){relayOn();server.sendHeader("Location","/");server.send(303,"text/plain","");}
void handleOff(){relayOff();server.sendHeader("Location","/");server.send(303,"text/plain","");}
void handleUpdatePage(){server.send(200,"text/html; charset=utf-8",updatePage());}

void setOtaError(const char* stage){otaFailed=true;otaError=String(stage)+"; код Update: "+String(Update.getError());Update.printError(Serial);}
void handleFirmwareUpload(){
 HTTPUpload& u=server.upload();
 if(u.status==UPLOAD_FILE_START){relayOff();otaFailed=false;otaError="";Serial.printf("OTA start: %s\n",u.filename.c_str());if(!Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH))setOtaError("Не удалось начать OTA");}
 else if(u.status==UPLOAD_FILE_WRITE && !otaFailed){if(Update.write(u.buf,u.currentSize)!=u.currentSize)setOtaError("Ошибка записи OTA");}
 else if(u.status==UPLOAD_FILE_END && !otaFailed){if(!Update.end(true))setOtaError("Ошибка завершения OTA");else Serial.printf("OTA OK: %u bytes\n",u.totalSize);}
 else if(u.status==UPLOAD_FILE_ABORTED){otaFailed=true;otaError="Загрузка файла прервана браузером";Update.abort();}
}
void handleUpdateFinished(){
 bool ok=!otaFailed && !Update.hasError();
 if(ok){server.send(200,"text/html; charset=utf-8","<meta name='viewport' content='width=device-width,initial-scale=1'><h2>Прошивка обновлена.</h2><p>ESP32 перезагружается...</p>");delay(1000);ESP.restart();}
 else{String m="<meta name='viewport' content='width=device-width,initial-scale=1'><h2>Ошибка обновления прошивки</h2><p>"+otaError+"</p><p>Свободное место OTA: "+String(Update.size())+" байт</p><p><a href='/update'>Назад</a></p>";server.send(500,"text/html; charset=utf-8",m);}
}

void setup(){Serial.begin(115200);loadSettings();pinMode(REED_PIN,INPUT_PULLUP);pinMode(RELAY_PIN,OUTPUT);relayOff();stableReedState=digitalRead(REED_PIN);lastReedState=stableReedState;WiFi.mode(WIFI_AP);WiFi.softAP(AP_NAME,AP_PASS);server.on("/",HTTP_GET,handleRoot);server.on("/save",HTTP_GET,handleSave);server.on("/test",HTTP_GET,handleTest);server.on("/off",HTTP_GET,handleOff);server.on("/update",HTTP_GET,handleUpdatePage);server.on("/update",HTTP_POST,handleUpdateFinished,handleFirmwareUpload);server.begin();}
void loop(){server.handleClient();int r=digitalRead(REED_PIN);if(r!=lastReedState){debounceStarted=millis();lastReedState=r;}if((millis()-debounceStarted)>50&&r!=stableReedState){int p=stableReedState;stableReedState=r;if(p==HIGH&&stableReedState==LOW)relayOn();}if(relayActive&&(millis()-relayStarted>=relayTimeMs))relayOff();}
