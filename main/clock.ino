/*
  Clock using DS1307 RTC + MAX7219 + DHT (mengganti NTP)
  - Serial command to set time: "SET yyyy mm dd hh mm ss"
  - I2C pins for NodeMCU: SDA=D2(GPIO4), SCL=D1(GPIO5)
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>    // tetap ada jika Anda pakai WiFi lain nanti
#include <Wire.h>
#include <RTClib.h>
#include "DHT.h"

// ---------- konfigurasi hardware ----------
#define NUM_MAX 4

// for NodeMCU 1.0
#define DIN_PIN 15  // D8
#define CS_PIN  13  // D7
#define CLK_PIN 12  // D6

#include "max7219.h"
#include "fonts.h"

#define DTYPE DHT11
const int DPIN = 4; // D2
DHT dht (DPIN, DTYPE);

// ---------- RTC (DS1307) ----------
RTC_DS1307 rtc;

// ---------- variabel aplikasi ----------
String date;
uint8_t display = 0;
char mesg[32];
int h=0,m=0,s=0;

int dots = 0;
long dotTime = 0;
long clkTime = 0;
long localEpoc = 0;
long localMillisAtUpdate = 0;
float utcOffset = 7; // WIB +7
int dx=0;
int dy=0;

// ---------- forward declarations ----------
void getTime();
void updateTime();
void handleSerialCommands();

void setup() {
  Serial.begin(115200);
  delay(50);
  Wire.begin(4, 5); // SDA = D2(GPIO4), SCL = D1(GPIO5) -- ubah bila perlu

  // RTC init
  if (! rtc.begin()) {
    Serial.println("RTC DS1307 tidak terdeteksi! cek wiring/power.");
  } else {
    if (!rtc.isrunning()) {
      Serial.println("RTC tidak berjalan. Set waktu dari compile-time.");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }

  initMAX7219();
  dht.begin();
  sendCmdAll(CMD_SHUTDOWN,1);
  sendCmdAll(CMD_INTENSITY,0);

  printStringWithShift("  WELCOME RAFI :v ",10);

  // baca waktu awal dari RTC
  getTime();
  clkTime = millis();
}

void loop() {
  // cek perintah serial (set time)
  handleSerialCommands();

  // scrolling dan sensor tiap periode tertentu (sama seperti sketch asli)
  if(millis()-clkTime > 60000 && !dots) {
    printStringWithShift(date.c_str(),40);
    printStringWithShift(" ",40);

    float temp = dht.readTemperature();
    dtostrf(temp, 3, 1, mesg);
    strcat(mesg, " C");
    printStringWithShift("  Temperatur: ",40);
    printStringWithShift(mesg,40);
    scrollLeft();

    float hum = dht.readHumidity();
    dtostrf(hum, 3, 0, mesg);
    strcat(mesg, " %");
    printStringWithShift("  Kelembaban:",40);
    printStringWithShift(mesg,40);
    scrollLeft();

    delay(2000);
    clkTime = millis();
  }

  if (millis() - dotTime > 500) {
    dotTime = millis();
    dots = !dots;
  }

  updateTime();
  showAnimClock();
}

// ------------------ fungsi display (sama seperti sebelumnya) ------------------
void showSimpleClock() {
  int dx=0, dy=0;
  clr();
  showDigit(h/10,  1, dig6x8);
  showDigit(h%10,  9, dig6x8);
  showDigit(m/10, 18, dig6x8);
  showDigit(m%10, 26, dig6x8);
  setCol(16, dots ? B00100100 : 0);
  setCol(33, dots ? B00100100 : 0);
  refreshAll();
}

void showAnimClock() {
  static byte dig[6], digold[6], digtrans[6];
  static int del = 0;
  byte digPos[6]={1,9,18,26,36,44};
  int digHt = 12;
  int num = 6;
  int i;
  if(del==0) {
    del = digHt;
    for(i=0;i<num;i++) digold[i]=dig[i];
    dig[0] = h/10 ? h/10 : 10;
    dig[1] = h%10;
    dig[2] = m/10;
    dig[3] = m%10;
    dig[4] = s/10;
    dig[5] = s%10;
    for(i=0;i<num;i++) digtrans[i] = (dig[i]==digold[i]) ? 0 : digHt;
  } else del--;

  clr();
  for(i=0;i<num;i++) {
    if(digtrans[i]==0) {
      showDigit(dig[i], digPos[i], dig6x8);
    } else {
      int dy = digHt - digtrans[i];
      showDigit(digold[i], digPos[i], dig6x8);
      dy = -digtrans[i];
      showDigit(dig[i], digPos[i], dig6x8);
      digtrans[i]--;
    }
  }
  setCol(16, dots ? B00100100 : 0);
  setCol(33, dots ? B00100100 : 0);
  refreshAll();
  delay(20);
}


// =======================================================================

void showDigit(char ch, int col, const uint8_t *data)
{
  if(dy<-8 | dy>8) return;
  int len = pgm_read_byte(data);
  int w = pgm_read_byte(data + 1 + ch * len);
  col += dx;
  for (int i = 0; i < w; i++)
    if(col+i>=0 && col+i<8*NUM_MAX) {
      byte v = pgm_read_byte(data + 1 + ch * len + 1 + i);
      if(!dy) scr[col + i] = v; else scr[col + i] |= dy>0 ? v>>dy : v<<-dy;
    }
}

// =======================================================================

void setCol(int col, byte v)
{
  if(dy<-8 | dy>8) return;
  col += dx;
  if(col>=0 && col<8*NUM_MAX)
    if(!dy) scr[col] = v; else scr[col] |= dy>0 ? v>>dy : v<<-dy;
}

// =======================================================================

int showChar(char ch, const uint8_t *data)
{
  int len = pgm_read_byte(data);
  int i,w = pgm_read_byte(data + 1 + ch * len);
  for (i = 0; i < w; i++)
    scr[NUM_MAX*8 + i] = pgm_read_byte(data + 1 + ch * len + 1 + i);
  scr[NUM_MAX*8 + i] = 0;
  return w;
}

// =======================================================================

void printCharWithShift(unsigned char c, int shiftDelay) {
  
  if (c < ' ' || c > '~'+25) return;
  c -= 32;
  int w = showChar(c, font);
  for (int i=0; i<w+1; i++) {
    delay(shiftDelay);
    scrollLeft();
    refreshAll();
  }
}

// =======================================================================

void printStringWithShift(const char* s, int shiftDelay){
  while (*s) {
    printCharWithShift(*s, shiftDelay);
    s++;
  }
}

// =======================================================================


// ------------------ pengganti getTime() : baca dari DS1307 ------------------
void getTime() {
  if (!rtc.begin()) {
    Serial.println("getTime(): RTC tidak tersedia");
    return;
  }
  DateTime now = rtc.now();

  // format tanggal mirip HTTP sebelumnya
  char buf[32];
  const char* days[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
  const char* months[] = {"","JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
  snprintf(buf, sizeof(buf), "     %s, %02d %s %04d", days[now.dayOfTheWeek()], now.day(), months[now.month()], now.year());
  date = String(buf);

  h = now.hour();
  m = now.minute();
  s = now.second();

  localMillisAtUpdate = millis();
  localEpoc = h * 3600 + m * 60 + s;

  Serial.printf("RTC DS1307 time: %02d:%02d:%02d  %s\n", h,m,s, date.c_str());
}

// ------------------ updateTime() (tetap menggunakan millis offset) ------------------
void updateTime() {
  long curEpoch = localEpoc + ((millis() - localMillisAtUpdate) / 1000);
  long epoch = round(curEpoch + 3600 * utcOffset + 86400L);
  h = ((epoch  % 86400L) / 3600) % 24;
  m = (epoch % 3600) / 60;
  s = epoch % 60;
}

// ------------------ Serial command: set RTC waktu ------------------
void handleSerialCommands() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length()==0) return;

  // format: SET yyyy mm dd hh mm ss
  if (line.startsWith("SET")) {
    int Y,mo,d,hh,mi,se;
    int parsed = sscanf(line.c_str(), "SET %d %d %d %d %d %d", &Y,&mo,&d,&hh,&mi,&se);
    if (parsed == 6) {
      DateTime dt(Y, mo, d, hh, mi, se);
      if (rtc.begin()) {
        rtc.adjust(dt);
        Serial.printf("RTC di-set ke %04d-%02d-%02d %02d:%02d:%02d\n", Y,mo,d,hh,mi,se);
        // update local time vars
        getTime();
      } else {
        Serial.println("RTC tidak terdeteksi, gagal set.");
      }
    } else {
      Serial.println("Format SET salah. Gunakan: SET yyyy mm dd hh mm ss");
    }
  } else {
    Serial.println("Perintah tidak dikenali. Gunakan: SET yyyy mm dd hh mm ss");
  }
}
