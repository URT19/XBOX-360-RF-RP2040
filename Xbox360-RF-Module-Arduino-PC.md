# اتصال ماژول RF ایکس‌باکس ۳۶۰ فت به کامپیوتر با آردوینو

**منبع اصلی:** [eblazecorp.blogspot.com](https://eblazecorp.blogspot.com/2022/11/conectar-modulo-rf-xbox-360-fat-pc.html)  
**ترجمه و آماده‌سازی برای GitHub**

---

## مقدمه

در این آموزش، نحوه اتصال الکترونیکی ماژول RF موجود در کنسول‌های **Xbox 360 Fat** را برای استفاده با کنترلرهای بی‌سیم روی کامپیوتر ویندوزی بررسی می‌کنیم.

این ماژول همان قطعه‌ای است که کنسول‌های Xbox 360 Fat برای اتصال بی‌سیم کنترلرها استفاده می‌کنند. بسیاری از این کنسول‌ها به دلیل خرابی چیپ APU به مراکز بازیافت فرستاده می‌شوند، اما می‌توان این ماژول‌ها را نجات داد و برای اتصال تا **۴ کنترلر** از طریق یک پورت USB به کامپیوتر استفاده کرد.

هدف این پروژه، استفاده مجدد از قطعات بازیافتی با هزینه‌ای کمتر از گیرنده‌های تجاری موجود در بازار است.

> **توجه:** این آموزش فقط برای ماژول نسخه **FAT** است. ماژول‌های Slim با این کد کار نمی‌کنند (در بخش پایانی توضیح داده شده).

---

## عملکرد ماژول

ماژول RF ایکس‌باکس ۳۶۰ فت دارای اتصالات USB است که به راحتی می‌توان به آن کابل وصل کرد. از جلو که به ماژول نگاه کنید، پایه‌های زیر را می‌بینید:

- **B+ / D- / D+ / GND** → اتصال به کابل USB نر

علاوه بر این، سه سیم اضافی نیاز است:

- **Signal Clock** و **Signal Data** برای ارسال دستورات به چیپ RF
- سیم سوم زمین (GND) که با فشار دادن سوئیچ وسط (برای همگام‌سازی) بسته می‌شود

### نکات مهم تغذیه

- ماژول RF با ولتاژ **۳٫۳ ولت** کار می‌کند (نه ۵ ولت).
- از **دو دیود 1N4007** سری برای کاهش ولتاژ ۵ ولت USB به حدود ۳٫۳ ولت استفاده کنید.
- ۵ ولت مستقیم به آردوینو وصل می‌شود.

---

## اتصالات به آردوینو

| سیگنال ماژول RF | پین آردوینو          | توضیحات                              |
|-----------------|----------------------|--------------------------------------|
| Data            | Digital 3            | + مقاومت ۱۰kΩ به GND                 |
| Clock           | Digital 4            | + مقاومت ۱۰kΩ به GND                 |
| Sync (Power Switch) | Digital 2        | برای دکمه همگام‌سازی                 |
| GND             | GND                  | زمین مشترک                           |
| 3.3V (از طریق دیودها) | -                | تغذیه ماژول                          |

**پیشنهاد:** از بردهای کوچک مثل **Arduino Mini** یا **Arduino Nano** استفاده کنید.

---

## کد آردوینو (کامل)

```cpp
/* Arduino code to communicate with xbox 360 RF module.
  Original work by (yaywoop)
  Additional ideas from Alexander Martinez
  Modified by dilandou (www.dilandou.com, www.diru.org/wordpress)
  Controller turn off added by Martin Ballantyne using research from http://tkkrlab.nl/wiki/XBOX_360_RF_Module
First sends LED initialisation code followed by LED startup animation code, then sleeps until a button press for sync command.
If the sync button is held down and released after 1000ms the connected controllers will be turned off. Otherwise the sync command will initiate.
RF module must be powered with 3.3V, two diodes in series with USB 5v will do. Connect the USB wires to a host computer, and the data and serial wires to Arduino.
of course, make sure to have a common ground */

#include <avr/sleep.h>

#define sync_pin 2 //power button repurposed for sync button (pin 5 on the module)
#define data_pin 3 //data line (pin 6 on the module)
#define clock_pin 4 //clock line (pin 7 on module) 

int led_cmd[10] =          {0,0,1,0,0,0,0,1,0,0}; //Activates/initialises the LEDs, leaving the center LED lit.
int led_timer_red_1[10] =  {0,0,1,0,1,1,1,0,0,0}; //Set quadrant 1 to red
int led_timer_red_2[10] =  {0,0,1,0,1,1,1,1,0,0}; //Set quadrant 1 and 2 to red
int led_timer_red_3[10] =  {0,0,1,0,1,1,1,1,0,1}; //Set quadrant 1, 2 and 4 to red
int led_timer_red_4[10] =  {0,0,1,0,1,1,1,1,1,1}; //Set quadrant 1, 2, 3 and 4 to red
int led_red_off[10] =      {0,0,1,0,1,1,0,0,0,0}; //Set quadrant 1, 2, 3 and 4 to off

int anim_cmd[10] =         {0,0,1,0,0,0,0,1,0,1}; //Makes the startup animation on the ring of light.
int sync_cmd[10] =         {0,0,0,0,0,0,0,1,0,0}; //Initiates the sync process.
int turn_off_cmd[10] =     {0,0,0,0,0,0,1,0,0,1}; //Turns off the connected controllers.

volatile boolean sync_pressed = 0;
int sync_hold_time = 0;
boolean turn_off_controllers = false;

void sendData(int command[])
{
  pinMode(data_pin, OUTPUT);
  digitalWrite(data_pin, LOW);    //start sending data.
  
  int previous_clock = 1;  
  for(int i = 0; i < 10; i++)
  {
    while (previous_clock == digitalRead(clock_pin)){} //detects change in clock
    previous_clock = digitalRead(clock_pin);
    
    // should be after downward edge of clock, so send bit of data now
    digitalWrite(data_pin, command[i]);

    while (previous_clock == digitalRead(clock_pin)){} //detects upward edge of clock
    previous_clock = digitalRead(clock_pin);
  }
  
  digitalWrite(data_pin, HIGH);
  pinMode(data_pin, INPUT);
  
  delay(50);
}

void setHeldLEDs(int held_time)
{
  if(held_time >= 1000)
  {
    sendData(led_timer_red_4);
  }
  else if(held_time >= 750)
  {
    sendData(led_timer_red_3);
  }
  else if(held_time >= 500)
  {
    sendData(led_timer_red_2);
  }
  else if(held_time >= 250)
  {
    sendData(led_timer_red_1);
  }
  else
  {
    sendData(led_red_off);
  }
}

void initLEDs()
{
  sendData(led_cmd);
  sendData(anim_cmd);
}

void wakeUp()
{
  sync_pressed = 1;
}

void sleepNow()
{
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // set sleep mode
  sleep_enable(); //enable sleep bit
  attachInterrupt(0, wakeUp, LOW);
  sleep_mode();
  sleep_disable(); //disable sleep bit
  detachInterrupt(0); // disables interrupt 0 on pin 2
}

void setup() 
{
  Serial.begin(9600);
  pinMode(sync_pin, INPUT);
  digitalWrite(sync_pin,HIGH);
  pinMode(data_pin, INPUT);
  pinMode(clock_pin, INPUT);
  delay(2000);

  initLEDs();
}

void loop()
{
  // Only sleep if the sync button is not held down
  if(!sync_pressed)
  {
    sleepNow();
  }
  
  delay(200);
  
  if(sync_pressed)
  {    
    Serial.print("Sync held time (ms): ");
    Serial.println(sync_hold_time, DEC);
    
    setHeldLEDs(sync_hold_time);
    
    // Count 1000ms, if elapsed the user wants to turn off their controllers
    if(sync_hold_time >= 1000)
    {
      turn_off_controllers = true;
      sync_hold_time = 1000;
    }
    
    // Initiate the user's action when they release the sync button
    if (digitalRead(sync_pin))
    {
      setHeldLEDs(0); 
      
      if(turn_off_controllers)
      {
        Serial.println("Turning off controllers.");
        sendData(turn_off_cmd);
        
        turn_off_controllers = false;
      }
      else
      {
        Serial.println("Syncing controllers.");
        sendData(sync_cmd);  
      }
      
      // Action complete, reset hold time and button state
      sync_hold_time = 0;
      sync_pressed = false;
    }
    else
    {    
      sync_hold_time += 200;
    }
  }
}
```

### نحوه استفاده از کد

1. کد را در Arduino IDE کپی کنید.
2. برد آردوینو را انتخاب و آپلود کنید.
3. آردوینو را جدا کنید.
4. کابل USB ماژول RF را به کامپیوتر وصل کنید.

---

## نصب درایور در ویندوز

1. روی دستگاه ناشناخته در Device Manager راست‌کلیک کنید.
2. **Update driver** را انتخاب کنید.
3. **Browse my computer for drivers** را بزنید.
4. **Let me pick from a list of available drivers** را انتخاب کنید.
5. **Xbox 360 Peripherals** را پیدا کنید.
6. **Xbox 360 Wireless Receiver for Windows** را انتخاب کنید.
7. Next بزنید و هشدار را تأیید کنید.

**لینک درایور پیشنهادی:**  
[Microsoft Xbox 360 Controller Driver](https://drivers.plus/es/microsoft-xbox-360-controller-driver-1-2-64-bit/265048/)

پس از نصب، کنترلرها را مانند کنسول اصلی همگام‌سازی کنید (دکمه Sync روی کنترلر + دکمه روی ماژول).

---

## نکات نهایی

- اگر دکمه Sync را بیش از ۱ ثانیه نگه دارید، کنترلرهای متصل خاموش می‌شوند.
- ماژول‌های **Slim** از نظر سخت‌افزاری کار می‌کنند اما چیپ کنترل متفاوتی دارند و دستورات آردوینو را اجرا نمی‌کنند. برای Slim فقط با کابل **Charge & Play** می‌توان همگام‌سازی کرد.
- برای نسخه Slim: فقط پایه‌های D+ / D- / GND را لحیم کنید و ولتاژ را با دو دیود 1N4007 به ۳٫۳ ولت برسانید.

---

## لایسنس و اعتبار

کد اصلی بر اساس کارهای yaywoop، Alexander Martinez، dilandou و Martin Ballantyne است.  
این ترجمه و بازنویسی برای استفاده آموزشی و پروژه‌های شخصی آزاد است.