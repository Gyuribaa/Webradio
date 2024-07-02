#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <Audio.h>
#include <IRremote.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// LCD I2C cím és méret
LiquidCrystal_I2C lcd(0x27, 16, 2);

// WiFi adatok
const char* ssid = "SSID";
const char* password = "Jelszó";

// Rádió stream URL-ek és állomásnevek
const char* radio_urls[] = {
  "https://icast.connectmedia.hu/5001/live.mp3", // RETRO
  "https://icast.connectmedia.hu/4738/mr2.mp3",  // PETOFI
  "https://slagerfm.netregator.hu:7813/slagerfm128.mp3", // SLAGER
  "https://stream.42netmedia.com:8443/sc_gyor1", // GYOR PLUSZ
  "https://s2.audiostream.hu/roxy_192k", // ROXI RADIO
  "http://stream.mercyradio.eu:80/mercyradio.mp3", // MERSYRADIO
  "http://wssgd.gdsinfo.com:8200/;stream", // BALATON
  "https://oxygenmusic.hu:8443/radiopapa.mp3", // RADIO_PAPA
  "https://icast.connectmedia.hu/5201/live.mp3", // RADIO 1
  "https://stream.danubiusradio.hu:443/danubius_192k", // DANUBIUS
  "http://cast3.my-control-panel.com:8451/live" // MAGYAR MULATOS
};
const char* station_names[] = {
  "1.RETRO RADIO ",
  "2.PETOFI RADIO",
  "3.SLAGER RADIO",
  "4.GYOR PLUSZ  ",
  "5.ROXI RADIO  ",
  "6.MERSY RADIO ", 
  "7.BALATON RAD.",
  "8.RADIO PAPA  ",
  "9.RADIO 1     ",
  "10.DANUBIUS R.",
  "11.MAGYAR MUL."
};
const int num_stations = sizeof(radio_urls) / sizeof(radio_urls[0]);
int current_station = 0;

// Audio objektum
Audio audio;

// IR vevő pin
const int RECV_PIN = 15;

// IR vevő objektum létrehozása
IRrecv irrecv(RECV_PIN);
decode_results results;

// Hangerő változó
int volume = 2; // Kezdő hangerő

// Időzítők
unsigned long lastVolumeChange = 0;
unsigned long lastIrCodeTime = 0;
unsigned long irRepeatDelay = 200; // 200 ms késleltetés az ismétlődő IR parancsokhoz

// NTP kliens beállítás
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600 * 2); // CET időzóna

// Időjárás API beállítások
const char* weatherApiKey = "APIKEY";
const char* weatherCity = "Város,hu";
const char* weatherApiUrl = "http://api.openweathermap.org/data/2.5/weather?q=Papa,hu&appid=5e5c410ce5d2b47d2eabcea2aae78765&units=metric";

// Időjárás változók
float temperature = 0;
int humidity = 0;
int windSpeed = 0;
bool displayWeather = false;

// Időjárás frissítési időzítő
unsigned long lastWeatherUpdate = 0;
const unsigned long weatherUpdateInterval = 180000; // 3 perc

void updateWeather();
void lcdPrintWeather();
void lcdPrint(const char* message, bool clear = false);
void lcdPrintVolume();
void lcdPrintStation();

void setup() {
  // Soros port hibakereséshez
  Serial.begin(115200);

  // LCD inicializálás
  lcd.init();
  lcd.backlight();
  lcdPrint("Csatlakozas...");

  // WiFi csatlakozás
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Csatlakozas a WiFi-hez...");
    lcd.setCursor(0, 1);
    lcd.print(".");
  }
  Serial.println("WiFi csatlakoztatva");
  lcdPrint("WiFi csatlakozva", true);

  // I2S konfiguráció
  audio.setPinout(26, 25, 27);
  audio.setVolume(volume); // Kezdő hangerő beállítása

  // Kezdő rádióállomás csatlakoztatása
  audio.connecttohost(radio_urls[current_station]);
  lcdPrintStation();
  lcdPrintVolume();

  // IR vevő indítása
  irrecv.enableIRIn();

  // NTP kliens indítása
  timeClient.begin();
  timeClient.setTimeOffset(3600 * 2); // CET időzóna, nyári időszámítással

  // Kezdő időjárás adat letöltése
  updateWeather();
}

void loop() {
  audio.loop();

  // Hiba kezelés
  if (WiFi.status() != WL_CONNECTED) {
    lcdPrint("WiFi megszakadt", true);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.println("Ujra csatlakozas a WiFi-hez...");
      lcd.setCursor(0, 1);
      lcd.print(".");
    }
    Serial.println("WiFi Ujracsatlakoztatva");
    lcdPrint("WiFi Ujracsatlakozva", true);
  }

  if (!audio.isRunning()) {
    Serial.println("Ujra csatlakozas a streamhez...");
    lcdPrint("Ujra csatlakozas stream", true);
    audio.connecttohost(radio_urls[current_station]);
  }

  // IR bemenet ellenőrzése
  if (irrecv.decode(&results)) {
    unsigned long irCode = results.value;

    // Ellenőrzi, hogy elegendő idő telt-e el az utolsó IR kód óta
    if (millis() - lastIrCodeTime > irRepeatDelay) {
      if (irCode == 0xFF10EF) { // Hangerő le
        if (volume > 0) {
          volume--;
          audio.setVolume(volume);
          lcdPrintVolume();
        }
      } else if (irCode == 0xFF5AA5) { // Hangerő fel
        if (volume < 21) {
          volume++;
          audio.setVolume(volume);
          lcdPrintVolume();
        }
      } else if (irCode == 0xFF18E7) { // Következő állomás
        current_station = (current_station + 1) % num_stations;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFF4AB5) { // Előző állomás
        current_station = (current_station - 1 + num_stations) % num_stations;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFFA25D) { // RETRO radio
        current_station = 0;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFF629D) { // PETOFI radio
        current_station = 1;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFFE21D) { // SLAGER radio
        current_station = 2;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFF22DD) { // GYOR PLUSZ radio
        current_station = 3;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFF02FD) { // ROXI radio
        current_station = 4;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFFC23D) { // MERSYRADIO
        current_station = 5;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFFE01F) { // BALATON radio
        current_station = 6;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFFA857) { // RADIO_PAPA
        current_station = 7;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFF906F) { // RADIO 1
        current_station = 8;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFF9867) { // DANUBIUS
        current_station = 9;
        audio.connecttohost(radio_urls[current_station]);
        lcdPrintStation();
      } else if (irCode == 0xFFB04F) { // Időjárás adatok megjelenítése
        displayWeather = !displayWeather;
        if (displayWeather) {
          updateWeather();
          lcdPrintWeather();
        } else {
          lcdPrintStation();
        }
      }

      // Az utolsó IR kód idejének frissítése
      lastIrCodeTime = millis();
    }

    irrecv.resume(); // A következő érték fogadása
  }

  // Hangerő kijelző törlése 5 másodperc után
  if (millis() - lastVolumeChange > 5000) {
    lcd.setCursor(14, 0); // 15. karakterhez állítja a kurzort (index 14)
    lcd.print("  "); // Törli a 15. és 16. pozíció karaktereit
    lastVolumeChange = millis(); // Újraindítja az utolsó hangerőváltoztatás időzítőt, hogy elkerülje az ismétlődő törléseket
  }

  // Idő frissítése másodpercenként
  static unsigned long lastTimeUpdate = 0;
  if (millis() - lastTimeUpdate >= 1000) {
    lastTimeUpdate = millis();
    timeClient.update();
    
    unsigned long epochTime = timeClient.getEpochTime();
    time_t rawTime = epochTime; // Átalakítja time_t formátumra
    tm *currentTime = localtime(&rawTime); // Lekéri a helyi időt

    int currentHour = currentTime->tm_hour;
    int currentMinute = currentTime->tm_min;
    int currentDay = currentTime->tm_mday;
    int currentMonth = currentTime->tm_mon + 1; // Hónapok januártól (0-11)
    int currentWeekday = currentTime->tm_wday; // Napok vasárnaptól (0-6)
    
    const char* weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    // Idő és dátum megjelenítése az LCD második sorában
    lcd.setCursor(0, 1);
    if (currentHour < 10) lcd.print("0");
    lcd.print(currentHour);
    lcd.print(":");
    if (currentMinute < 10) lcd.print("0");
    lcd.print(currentMinute);
    lcd.print("  "); // Két szóköz

    // Hónap és nap megjelenítése
    if (currentMonth < 10) lcd.print("0");
    lcd.print(currentMonth);
    lcd.print("/");
    if (currentDay < 10) lcd.print("0");
    lcd.print(currentDay);
    lcd.print(" ");

    // Hét napjának megjelenítése
    lcd.print(weekdays[currentWeekday]);

    lcd.print("      "); // Törli az esetlegesen megmaradt karaktereket
  }

  // Időjárás adatok frissítése 10 percenként
  if (displayWeather && millis() - lastWeatherUpdate >= weatherUpdateInterval) {
    updateWeather();
    lcdPrintWeather();
  }
}

void updateWeather() {
  if ((WiFi.status() == WL_CONNECTED)) {
    HTTPClient http;
    http.begin(weatherApiUrl);
    int httpCode = http.GET();

    if (httpCode > 0) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);

      temperature = doc["main"]["temp"];
      humidity = doc["main"]["humidity"];
      windSpeed = doc["wind"]["speed"]; // m/s 


      lastWeatherUpdate = millis();
    }
    http.end();
  }
}

void lcdPrintWeather() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("");
  lcd.print((int)temperature+1);
  lcd.print((char)223); // Fokjel
  lcd.print("C ");
  
  lcd.print(humidity);
  lcd.print("% ");

  lcd.print((int)windSpeed);
  lcd.print("m/s   ");

}

void lcdPrint(const char* message, bool clear) {
  if (clear) lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(message);
}

void lcdPrintVolume() {
  lcd.setCursor(14, 0);
  lcd.print(volume < 10 ? " " : "");
  lcd.print(volume);
  lastVolumeChange = millis();
}

void lcdPrintStation() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(station_names[current_station]);
}
