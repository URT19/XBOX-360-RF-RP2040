# اتصال ماژول RF ایکس‌باکس ۳۶۰ فت به کامپیوتر با Digispark (ATtiny85)

**نسخه فشرده و ارزان‌قیمت**  
بر اساس پروژه‌های Instructables و کدهای جامعه  
آماده برای GitHub




<img width="3892" height="2304" alt="image" src="https://github.com/user-attachments/assets/3fe78514-b9be-45e0-83ea-4641fdcd86a5" />




<img width="722" height="337" alt="image" src="https://github.com/user-attachments/assets/b41dca24-5121-4cfa-9b1d-d978ce52845a" />

<img width="1024" height="768" alt="image" src="https://github.com/user-attachments/assets/b1a704c4-8e48-472f-b430-1bed33cb36e7" />

----
<img width="1647" height="955" alt="image" src="https://github.com/user-attachments/assets/f6ab5acc-4877-42cc-a3e5-cbc5a417e71d" />



---


---

## مقدمه

در این آموزش، نحوه ساخت گیرنده بی‌سیم Xbox 360 با استفاده از **Digispark (ATtiny85)** را یاد می‌گیرید. این روش بسیار جمع‌وجورتر و ارزان‌تر از استفاده از آردوینو معمولی است و برای ساخت یک گیرنده قابل‌حمل عالی است.

ماژول RF از کنسول‌های **Xbox 360 Fat** استخراج می‌شود و با یک Digispark کنترل می‌شود. یک ماژول می‌تواند تا **۴ کنترلر** را پشتیبانی کند.

> **نکته مهم:** این آموزش فقط برای ماژول نسخه **FAT** است.

---

## قطعات مورد نیاز

| قطعه                          | تعداد | توضیحات                                      |
|-------------------------------|-------|----------------------------------------------|
| ماژول RF Xbox 360 Fat         | ۱     | از کنسول Fat استخراج شده                     |
| Digispark ATtiny85            | ۱     | نسخه معمولی یا Pro                           |
| رگولاتور ولتاژ ۳٫۳ ولت (مثل AMS1117-3.3) | ۱ | **توصیه می‌شود** (بهتر از دیود)              |
| دیود 1N4007 (اختیاری)         | ۲     | اگر رگولاتور ندارید                          |
| مقاومت ۱۰kΩ یا ۴٫۷kΩ          | ۲     | برای pull-up روی Data و Clock (توصیه قوی)    |
| کابل USB نر                   | ۱     | برای اتصال ماژول به کامپیوتر                 |
| سیم‌های نازک                  | -     | برای لحیم‌کاری                               |
| جعبه یا کیس (اختیاری)         | ۱     | می‌توانید پرینت سه‌بعدی کنید                 |

---

## اتصالات (Pinout)

### پایه‌های Digispark (ATtiny85)

| پین Digispark | عملکرد                  | اتصال به ماژول RF      |
|---------------|-------------------------|------------------------|
| P0 (PB0)      | Clock                   | Clock (پین ۷ ماژول)    |
| P1 (PB1)      | Data                    | Data (پین ۶ ماژول)     |
| P2 (PB2)      | Sync Button             | سوئیچ Power/Sync       |
| 5V            | تغذیه Digispark         | از USB                 |
| GND           | زمین مشترک              | GND ماژول              |

### تغذیه ماژول RF

- ماژول **حتماً باید با ۳٫۳ ولت** تغذیه شود.
- **بهترین روش:** استفاده از رگولاتور ۳٫۳ ولت (AMS1117-3.3 + خازن‌های ورودی/خروجی).
- روش جایگزین (کمتر توصیه شده): دو دیود 1N4007 سری بین ۵ ولت USB و تغذیه ماژول.

**هشدار:** هرگز ۵ ولت مستقیم به ماژول RF وصل نکنید!

### نکته مهم در مورد Digispark

روی برخی بردهای Digispark، پایه‌های P3 و P4 برای USB استفاده می‌شوند. اگر از پورت micro-USB خود Digispark برای برنامه‌ریزی استفاده می‌کنید، بعد از آپلود کد باید پایه‌های مربوطه را از مدار جدا کنید تا تداخل پیش نیاید (در Instructables توصیه شده پین‌های ۲ و ۳ را بلند کنید).

---

## کد کامل برای Digispark (ATtiny85)

این کد ساده‌شده است چون در Digispark به دلیل بوت‌لودر، sleep و interrupt به خوبی کار نمی‌کنند.

```cpp
/*
  Xbox 360 RF Module Controller for Digispark (ATtiny85)
  Based on original work by yaywoop, Alexander Martinez, dilandou
  Adapted for Digispark by community (Instructables version)

  IMPORTANT:
  - Install Digispark drivers and board support in Arduino IDE
  - RF Module MUST be powered with 3.3V
  - Common GND is required
*/

// Pin definitions for Digispark
#define sync_pin  2   // P2 - Power/Sync button (pin 5 on RF module)
#define data_pin  1   // P1 - Data line (pin 6 on RF module)
#define clock_pin 0   // P0 - Clock line (pin 7 on RF module)

// Commands
int led_cmd[10]      = {0,0,1,0,0,0,0,1,0,0}; // Initialize LEDs (center LED on)
int anim_cmd[10]     = {0,0,1,0,0,0,0,1,0,1}; // Startup ring animation
int sync_cmd[10]     = {0,0,0,0,0,0,0,1,0,0}; // Start sync process
int CLEAR_ERROR[10]  = {0,0,1,1,0,0,0,0,0,0}; // Clear any error state

volatile boolean sync_enable = 0;

void setup() {
  pinMode(sync_pin, INPUT_PULLUP);
  digitalWrite(sync_pin, HIGH);
  
  pinMode(data_pin, INPUT);
  pinMode(clock_pin, INPUT);
  
  delay(2000);          // Wait for module to stabilize
  initLEDs();
}

void sendData(int cmd_do[]) {
  pinMode(data_pin, OUTPUT);
  digitalWrite(data_pin, LOW);    // Start transmission

  int prev = 1;
  for(int i = 0; i < 10; i++) {
    // Wait for clock edge (falling)
    while (prev == digitalRead(clock_pin)) {}
    prev = digitalRead(clock_pin);

    // Send bit after falling edge
    digitalWrite(data_pin, cmd_do[i]);

    // Wait for rising edge
    while (prev == digitalRead(clock_pin)) {}
    prev = digitalRead(clock_pin);
  }

  digitalWrite(data_pin, HIGH);
  // Optionally set back to INPUT: pinMode(data_pin, INPUT);
}

void initLEDs() {
  sendData(led_cmd);
  delay(50);
  sendData(anim_cmd);
  delay(50);
  sendData(CLEAR_ERROR);
  delay(50);
}

void loop() {
  // Detect button press (active LOW because of INPUT_PULLUP)
  sync_enable = !digitalRead(sync_pin);
  
  if (sync_enable == 1) {
    sendData(sync_cmd);
    sync_enable = 0;
    delay(10000);                 // Prevent multiple triggers
    digitalWrite(sync_pin, HIGH); // Restore pull-up state
  }
}
```

### نسخه پیشرفته‌تر (با قابلیت خاموش کردن کنترلرها)

اگر می‌خواهید با نگه‌داشتن طولانی دکمه، کنترلرها خاموش شوند، می‌توانید از منطق زمان‌سنجی مشابه نسخه آردوینو استفاده کنید (اما به دلیل محدودیت‌های ATtiny85 ممکن است نیاز به تنظیم داشته باشد).

---

## مراحل ساخت

### ۱. برنامه‌ریزی Digispark

1. در Arduino IDE، پشتیبانی از Digispark را نصب کنید:
   - File → Preferences → Additional Boards Manager URLs:
     ```
     http://digistump.com/package_digistump_index.json
     ```
   - Tools → Board → Boards Manager → جستجوی Digistump و نصب.
2. برد را روی **Digispark (Default - 16.5MHz)** تنظیم کنید.
3. کد را آپلود کنید (Digispark در زمان آپلود باید به USB وصل باشد و معمولاً چند ثانیه صبر می‌کند).

### ۲. لحیم‌کاری

1. پایه‌های Data، Clock و Sync را از ماژول RF به پین‌های مربوطه Digispark وصل کنید.
2. GND مشترک را وصل کنید.
3. تغذیه ۳٫۳ ولت ماژول را از رگولاتور تأمین کنید.
4. کابل USB را به پایه‌های D+، D-، ۵V و GND ماژول RF وصل کنید (USB ماژول به کامپیوتر می‌رود).

**توصیه:** مقاومت‌های pull-up ۴٫۷kΩ یا ۱۰kΩ روی خطوط Data و Clock اضافه کنید (برخی ماژول‌ها این مقاومت را ندارند و باعث مشکل می‌شوند).

### ۳. نصب درایور در ویندوز

1. دستگاه را وصل کنید.
2. در Device Manager روی دستگاه ناشناخته راست‌کلیک کنید.
3. Update driver → Browse my computer → Let me pick from a list.
4. **Xbox 360 Peripherals** → **Xbox 360 Wireless Receiver for Windows**.

در ویندوز ۱۰ و ۱۱ معمولاً درایور به‌صورت خودکار یا با این روش نصب می‌شود.

---

## همگام‌سازی کنترلرها

1. دکمه Sync روی ماژول (یا سوئیچ Power اصلی) را فشار دهید.
2. دکمه Sync روی کنترلر Xbox 360 را فشار دهید.
3. کنترلر باید شناسایی شود.

> **نکته:** در برخی پیاده‌سازی‌ها LEDهای سبز مثل کنسول اصلی روشن نمی‌شوند، اما کنترلر در Device Manager و بازی‌ها کار می‌کند.

---

## نکات و عیب‌یابی

| مشکل                          | راه‌حل پیشنهادی                                      |
|-------------------------------|------------------------------------------------------|
| ماژول شناسایی نمی‌شود         | بررسی تغذیه ۳٫۳ ولت و GND مشترک                      |
| همگام‌سازی کار نمی‌کند        | اضافه کردن مقاومت pull-up روی Data و Clock           |
| تداخل با Digispark            | جدا کردن پایه‌های USB Digispark بعد از برنامه‌ریزی  |
| کنترلر وصل می‌شود اما کار نمی‌کند | نصب مجدد درایور Xbox 360 Wireless Receiver         |
| LEDها انیمیشن ندارند          | دستور CLEAR_ERROR و initLEDs را دوباره اجرا کنید     |

---

## تفاوت با نسخه آردوینو کامل

- کد ساده‌تر است (بدون sleep و interrupt پیچیده).
- اندازه بسیار کوچک‌تر.
- مصرف برق کمتر.
- قابلیت خاموش کردن کنترلرها با نگه‌داشتن دکمه در نسخه پایه وجود ندارد (می‌توان اضافه کرد).

---

## منابع و اعتبار

- کد پایه: yaywoop، Alexander Martinez، dilandou
- نسخه Digispark: Instructables (XBOX 360 RF MODULE a USB Con ATTINY85)
- تحقیقات پروتکل: tkkrlab.nl و جامعه Xbox homebrew

این پروژه برای استفاده شخصی و آموزشی آزاد است.  
اگر بهبود یا باگی پیدا کردید، خوشحال می‌شوم مشارکت کنید.

---

**ساخته شده با عشق برای جامعه بازیافت سخت‌افزار** 🎮
