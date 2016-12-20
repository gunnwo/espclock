#include "CRTC.h"

//TODO move into own header file

// CONNECTIONS:
// DS3231 SDA --> SDA
// DS3231 SCL --> SCL
// DS3231 VCC --> 3.3v or 5v
// DS3231 GND --> GND

//I2C pins
#define SDA 5 
#define SCL 4

CRTC::CRTC():
m_pSyncProvider(0),
syncInterval(86400),
nextSyncTime(0)
{
  
}

CRTC::~CRTC()
{
  
}

bool CRTC::setup()
{
  Wire.begin(SDA, SCL);

  Rtc.Begin();
  
  if (!Rtc.IsDateTimeValid()) 
  {
    Serial.println("CRTC::setup() - RTC lost confidence in the DateTime!");
    // This will most likely happen when the battery is low or the RTC is used the first time.
    // After setting the sync provider the time will be set correctly to utc
  }
  
  if (!Rtc.GetIsRunning())
  {
    Serial.println("CRTC::setup() - RTC was not actively running, starting now");
    Rtc.SetIsRunning(true);
  }
  
  Rtc.Enable32kHzPin(false);
  Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone);
}


time_t CRTC::now()
{
  //Serial.println("CRTC::now() - called");
  if (!Rtc.IsDateTimeValid()) 
  {
    // Common Cuases:
    //    1) the battery on the device is low or even missing and the power line was disconnected
    Serial.println("CRTC::now() - RTC lost confidence in the DateTime! Trying to resync with SyncProvider");

    sync(0, true);
  }

  RtcDateTime now = Rtc.GetDateTime();
  time_t t((time_t)now.Epoch32Time());

  t = sync(t);

  return(t);
}

time_t CRTC::sync(time_t t, bool force)
{
  if(nextSyncTime <= (uint32_t)t || force == true)
  {
    if(0 != m_pSyncProvider)
    {
      Serial.println("CRTC::sync() resyncing");
      time_t tsync(m_pSyncProvider->now());
      if(0 != tsync)
      {
        setTime(tsync);
        t = tsync;
      }
      else
      {
        nextSyncTime = (uint32_t)t + syncInterval;
      }
    }
  }
  return t;
}

void CRTC::setTime(time_t t)
{
  RtcDateTime now;
  now.InitWithEpoch32Time((uint32_t)t);
  Rtc.SetDateTime(now);
  nextSyncTime = (uint32_t)t + syncInterval;
}

void CRTC::setSyncProvider(ISyncProvider* pSyncProvider){
  m_pSyncProvider = pSyncProvider;  
  nextSyncTime = 0;
  now(); // this will sync the clock
}

void CRTC::setSyncInterval(time_t interval){ // set the number of seconds between re-sync
  syncInterval = (uint32_t)interval;
  RtcDateTime now = Rtc.GetDateTime();
  
  nextSyncTime = now.Epoch32Time() + syncInterval;
}


