#ifndef _CCLOCKDISPLAY_H
#define _CCLOCKDISPLAY_H

#define SMALLCLOCK

#include "IDisplay.h"
#include <TimeLib.h>
#include <Timezone.h>

class CClockDisplay : public IDisplay
{
public:
  CClockDisplay();
  virtual ~CClockDisplay();

  virtual bool setup(CRGB* leds, int numLEDs);
  virtual bool update(bool force=false);

  CRGB getColor();

  void setColor(const CRGB& color);
  void setTimezone(Timezone* pTZ);
  

private:
  void compose(const int arrayToAdd[]);
  void display_hour(const int displayHour, const int minute, const int hour);
  void display_time(const int hour, const int minute);

  
  CRGB* m_pLEDs;
  int   m_numLEDs;

  CRGB  m_color;

  int m_currentMinute;
  
  Timezone* m_pTZ;
};

#endif
