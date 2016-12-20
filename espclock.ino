#include <FS.h>                   //From the WifiManager AutoConnectWithFSParameters: this needs to be first, or it all crashes and burns... hmmm is this really the case?

#define FASTLED_ESP8266_RAW_PIN_ORDER

#include <TimeLib.h>              //http://www.arduino.cc/playground/Code/Time
#include "CRTC.h"
#include "CNTPClient.h"
#include <Timezone.h>             //https://github.com/JChristensen/Timezone (Use https://github.com/willjoha/Timezone as long as https://github.com/JChristensen/Timezone/pull/8 is not merged.)

#include <FastLED.h>
#include "CFadeAnimation.h"

#include "CClockDisplay.h"

#include <ESP8266WiFi.h>

#include <DNSServer.h>            
#include <ESP8266WebServer.h>     
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <Ticker.h>


Ticker ticker;
WiFiServer server(80);

/*
 * ------------------------------------------------------------------------------
 * LED configuration
 * ------------------------------------------------------------------------------
 */

// Number of LEDs used for the clock (11x10 + 4 minute LEDs + 4 spare LEDs)
#ifdef SMALLCLOCK
#define NUM_LEDS 118
#else
#define NUM_LEDS 122
#endif

// DATA_PIN and CLOCK_PIN used by FastLED to control the APA102C LED strip. 
#define DATA_PIN 13
#define CLOCK_PIN 14

CRGB leds[NUM_LEDS];
CRGB leds_target[NUM_LEDS];

/*
 * ------------------------------------------------------------------------------
 * default RGB init values
 * ------------------------------------------------------------------------------
 */
const int INIT_RED = 255;
const int INIT_GREEN = 127;
const int INIT_BLUE = 36;

/*
 * ------------------------------------------------------------------------------
 * Configuration parameters configured by the WiFiManager and stored in the FS
 * ------------------------------------------------------------------------------
 */
 
//The default values for the NTP server and the Blynk token, if there are different values in config.json, they are overwritten.
char ntp_server[50] = "0.de.pool.ntp.org";
char clock_version[15] = "v16.12.20_002";

/*
 * ------------------------------------------------------------------------------
 * Clock configuration/variables
 * ------------------------------------------------------------------------------
 */

uint8_t brightnessNight =  100;
uint8_t brightnessDay   = 200;
uint8_t brightness = brightnessDay;

bool displayClock = true;
bool isBrightDay = true;


CRTC Rtc;
CNTPClient Ntp;

//Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Standard Time
Timezone CE(CEST, CET);

CFadeAnimation ani;
CClockDisplay clock;



void updateBrightness(bool force=false)
{
	static bool bDay=true;
  
	if(true == force)
	{
		brightness = brightnessDay;
		FastLED.setBrightness(brightness);
		bDay = true;  
	}
  
	time_t local(CE.toLocal(now()));
	if(hour(local) >= 22 || hour(local) < 6)
	{
		isBrightDay = false;
		if(true == bDay)
		{
			Serial.println("Updating the brightness for the night.");
			brightness = brightnessNight;
			FastLED.setBrightness(brightness);
			bDay = false;  
		}
	}
	else if (false == bDay)
	{
		isBrightDay = true;
		Serial.println("Updating the brightness for the day.");
		brightness = brightnessDay;
		FastLED.setBrightness(brightness);
		bDay = true;
	}
}

/*
 * ------------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------------
 */

//------------------------------------------------------------------------------
//Time callback:

time_t getDateTimeFromRTC()
{
  return Rtc.now();
}


//------------------------------------------------------------------------------
//Ticker callback:
void cbTick()
{
  if(0 == leds[0].b) leds[0] = CRGB::Blue;
  else leds[0] = CRGB::Black;

  FastLED.show();
}

//------------------------------------------------------------------------------
//WiFiManager callback:
bool shouldSaveConfig = false;

void cbSaveConfig()
{
  Serial.println("::cbSaveConfig(): Should save config");
  shouldSaveConfig = true;
}

void cbConfigMode(WiFiManager *myWiFiManager)
{
  ticker.attach(0.2, cbTick);
}


/*
* ------------------------------------------------------------------------------
* clock settings
* brightness day/night + color Red/Green/Blue
* NTPserver
* ------------------------------------------------------------------------------
*/
void writeSettings()
{
	Serial.println("Write: writing config");

	StaticJsonBuffer<200> jsonBuffer;
	JsonObject& json = jsonBuffer.createObject();
	if (json == JsonObject::invalid())
	{
		Serial.println("Write: failed to create JSON.");
		return;
	}

	json["ntp_server"] = ntp_server;
	CRGB ledcolor;
	ledcolor = clock.getColor();
	json["red"] = ledcolor.r;
	json["green"] = ledcolor.g;
	json["blue"] = ledcolor.b;
	json["day"] = brightnessDay;
	json["night"] = brightnessNight;

	Serial.println("Write: opening file for writing");

	File configFile = SPIFFS.open("/config.json", "w");
	if (!configFile)
	{
		Serial.println("Write: failed to open config file for writing");
		return;
	}

	json.printTo(configFile);
	json.printTo(Serial); // debug out
	Serial.println();
  
	configFile.close();

	Serial.println("Write: done saving");
}


void readSettings()
{
	if (SPIFFS.exists("/config.json"))
	{
		//file exists, reading and loading
		Serial.println("Read: reading config file");

		if (SPIFFS.exists("/config.json"))
		{
			File configFile = SPIFFS.open("/config.json", "r");
			if (configFile)
			{
				Serial.println("Read: opened config file");
				size_t size = configFile.size();
				// Allocate a buffer to store contents of the file.
				std::unique_ptr<char[]> buf(new char[size]);

				configFile.readBytes(buf.get(), size);

				StaticJsonBuffer<200> jsonBuffer;
				JsonObject& json = jsonBuffer.parseObject(buf.get());

				json.printTo(Serial); // debug out

				if (json.success())
				{
					Serial.println("\nRead: parsed json");
          
					strcpy(ntp_server, json["ntp_server"]);

					brightnessDay = int(json["day"]);
					brightnessNight = int(json["night"]);
					brightness = brightnessDay;

					CRGB ledcolor;
					ledcolor.r = int(json["red"]);
					ledcolor.g = int(json["green"]);
					ledcolor.b = int(json["blue"]);

					if (brightnessDay == 0 || brightnessNight == 0 || ( ledcolor.r == 0 && ledcolor.g == 0 && ledcolor.b == 0)) 
					{      
						Serial.println("Read: invalid config params, using defaults");
						setDefaults();
						shouldSaveConfig = true;
					}
					clock.setColor(ledcolor);
				}
				else
				{
					Serial.println("Read: failed to load json config, using defaults");
					setDefaults();
					shouldSaveConfig = true;
				}
			}
		}
		else
		{
			// no config file found, use defaults
			Serial.println("Read: config not found, using defaults");
			setDefaults();
			shouldSaveConfig = true;
		}
	}
}

void setDefaults()
{
	brightnessDay = 250;
	brightnessNight = 150;
	brightness = brightnessDay;

	CRGB ledcolor;
	ledcolor.r = INIT_RED;
	ledcolor.g = INIT_GREEN;
	ledcolor.b = INIT_BLUE;
  
	clock.setColor(ledcolor);
}

void setup () 
{
	Serial.begin(57600);

	Serial.println();
	Serial.print("compiled: ");
	Serial.print(__DATE__);
	Serial.println(__TIME__);

	FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR>(leds, NUM_LEDS);

	FastLED.setBrightness(brightness);

	fill_solid( &(leds[0]), NUM_LEDS, CRGB::Black);
	FastLED.show();

	ticker.attach(0.6, cbTick);

	//read configuration from FS json
	Serial.println("Setup: mounting FS...");

	if (SPIFFS.begin()) 
	{
		Serial.println("Setup: mounted file system");
		readSettings();
	} 
	else 
	{
		Serial.println("Setup: failed to mount FS");
	}
	//end read

	WiFiManager wifiManager;
	wifiManager.setAPCallback(cbConfigMode);
	wifiManager.setSaveConfigCallback(cbSaveConfig);

	WiFiManagerParameter custom_ntp_server("server", "NTP server", ntp_server, 50);
	wifiManager.addParameter(&custom_ntp_server);

	//reset settings - for testing
	//wifiManager.resetSettings();

	if (!wifiManager.autoConnect("ESPClockAP")) 
	{
		Serial.println("Setup: failed to connect and hit timeout");
		delay(3000);
		//reset and try again, or maybe put it to deep sleep
		ESP.reset();
		delay(5000);
	}

	//if you get here you have connected to the WiFi
	Serial.println("Setup: connected...yeey :)");

	//read updated parameters
	strcpy(ntp_server, custom_ntp_server.getValue());

	//save the custom parameters to FS
	if (shouldSaveConfig) 
	{
		writeSettings();
	}
  
	Serial.print("Setup: IP number assigned by DHCP is ");
	Serial.println(WiFi.localIP());

	IPAddress timeServerIP;
	WiFi.hostByName(ntp_server, timeServerIP);
	Serial.print("NTP Server: ");
	Serial.print(ntp_server);
	Serial.print(" - ");
	Serial.println(timeServerIP);

	Rtc.setup();
	Ntp.setup(timeServerIP);
	Rtc.setSyncProvider(&Ntp);
	setSyncProvider(getDateTimeFromRTC);

	updateBrightness();
  
	clock.setup(&(leds_target[0]), NUM_LEDS);
	clock.setTimezone(&CE);

	// Start the Wifi server
	server.begin();
	Serial.println("Setup: wifi web server started");
  
	ticker.detach();
}

void loop () 
{
	if (timeSet != timeStatus())
	{
		Serial.println("Loop: Time is not set. The time & date are unknown.");
	}

	updateBrightness();

	if(displayClock)
	{
		bool changed = clock.update();
		ani.transform(leds, leds_target, NUM_LEDS, changed);
		FastLED.show();
	}

	/*
	* webserver implementation
	*/

	WiFiClient client = server.available();
	if (!client)
	{
		return;
	}

	// Wait until the client sends some data
	Serial.println("Wifi Server: new client");
	unsigned long ultimeout = millis() + 250;
	while (!client.available() && (millis()<ultimeout))
	{
		delay(1);
	}

	if (millis()>ultimeout)
	{
		Serial.println("Wifi Server: client connection time-out!");
		return;
	}

	// Read the first line of the request
	String sRequest = client.readStringUntil('\r');
	
	client.flush();

	// stop client, if request is empty
	if (sRequest == "")
	{
		Serial.println("Wifi Server: empty request! - stopping client");
		client.stop();
		return;
	}

	// get path; end of path is either space or ?
	// Syntax is e.g. GET /?pin=MOTOR1STOP HTTP/1.1
	String sPath = "", sParam = "", sCmd = "";
	String sGetstart = "GET ";
	int iStart, iEndSpace, iEndQuest;
	iStart = sRequest.indexOf(sGetstart);
	if (iStart >= 0)
	{
		iStart += +sGetstart.length();
		iEndSpace = sRequest.indexOf(" ", iStart);
		iEndQuest = sRequest.indexOf("?", iStart);

		// are there parameters?
		if (iEndSpace>0)
		{
			if (iEndQuest>0)
			{
				// there are parameters
				sPath = sRequest.substring(iStart, iEndQuest);
				sParam = sRequest.substring(iEndQuest, iEndSpace);
			}
			else
			{
				// NO parameters
				sPath = sRequest.substring(iStart, iEndSpace);
			}
		}
	}

  ///////////////////////////////////////////////////////////////////////////////
  // output parameters to serial, you may connect e.g. an Arduino and react on it
  if (sParam.length()>0)
  {
    int iEqu = sParam.indexOf("?");
    if (iEqu >= 0)
    {
      sCmd = sParam.substring(iEqu + 1, sParam.length());
      Serial.print("CMD: ");
      Serial.println(sCmd);
    }
  }


  // format the html response
  String sResponse, sHeader;
  if (sPath != "/")
  {
    // 404 for non-matching path
    sResponse = "<html><head><title>404 Not Found</title></head><body><h1>Not Found</h1><p>The requested URL was not found on this server.</p></body></html>";

    sHeader = "HTTP/1.1 404 Not found\r\n";
    sHeader += "Content-Length: ";
    sHeader += sResponse.length();
    sHeader += "\r\n";
    sHeader += "Content-Type: text/html\r\n";
    sHeader += "Connection: close\r\n";
    sHeader += "\r\n";
  }
  else
  {
    // format the html page

    // react on parameters
    if (sCmd.length()>0)
    {
      int iEqu = sParam.indexOf("=");
      int iVal;
      if (iEqu >= 0)
      {
        iVal = sParam.substring(iEqu + 1, sParam.length()).toInt() ;
      }

      // switch GPIO
      if (sCmd.indexOf("RED") >= 0)
      {
        CRGB ledcolor;
        ledcolor = clock.getColor();
        ledcolor.r = iVal;
        clock.setColor(ledcolor);
        clock.update(true);
      }
      else if (sCmd.indexOf("GREEN") >= 0)
      {
        CRGB ledcolor;
        ledcolor = clock.getColor();
        ledcolor.g = iVal;
        clock.setColor(ledcolor);
        clock.update(true);
      }
      else if (sCmd.indexOf("BLUE") >= 0)
      {
        CRGB ledcolor;
        ledcolor = clock.getColor();
        ledcolor.b = iVal;
        clock.setColor(ledcolor);
        clock.update(true);
      }
      else if (sCmd.indexOf("DAY") >= 0)
      {
        brightnessDay = iVal;
        updateBrightness(true);
      }
      else if (sCmd.indexOf("NIGHT") >= 0)
      {
        brightnessNight = iVal;
        updateBrightness(true);
      }
      writeSettings();
    }

    CRGB ledcolor;
    ledcolor = clock.getColor();

#ifdef SMALLCLOCK
    sResponse = "<html><head><title>Konfiguration WordClock (klein)</title>";
#else
    sResponse = "<html><head><title>Konfiguration WordClock (gross)</title>";
#endif
    sResponse = 
    sResponse += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\">";
#ifdef SMALLCLOCK
    sResponse += "</head><body style='font-family:verdana;background:#FFFFEE'>";
#else
    sResponse += "</head><body style='font-family:verdana;background:#E0E0E0'>";
#endif
    sResponse += "<h1>Word Clock</h1>";
    sResponse += "<h3>Helligkeit</h3>\r\n";
	sResponse += "<table><tr><td width='100'>";
	if (isBrightDay)
		sResponse += "<u>";
	sResponse += "bei Tag";
	if (isBrightDay)
		sResponse += "</u>";
	sResponse += "</td><td><input onchange=\"window.location.href='?DAY='+this.value;\" value='";
    sResponse += brightnessDay;
    sResponse += "' min='0' max='255' type='range'>";
    sResponse += brightnessDay;
	sResponse += "</td></tr>\r\n";
	sResponse += "<tr><td width='100'>";
	if (!isBrightDay)
		sResponse += "<u>";
	sResponse += "bei Nacht";
	if (!isBrightDay)
		sResponse += "</u>";
	sResponse += "</td><td><input onchange=\"window.location.href='?NIGHT='+this.value;\" value='";
    sResponse += brightnessNight;
    sResponse += "' min='0' max='255' type='range'>";
    sResponse += brightnessNight;
	sResponse += "</td></tr></table>\r\n";
	sResponse += "<h3>Farbe</h3>";
    sResponse += "<table><tr><td width='100'>Rot</td>";
    sResponse += "<td><input onchange=\"window.location.href='?RED='+this.value;\" value='";
    sResponse += ledcolor.r;
    sResponse += "' min='0' max='255' type='range'>";
    sResponse += ledcolor.r;
    sResponse += "</td></tr>\r\n";
    sResponse += "<tr><td width='100'>Gr&uuml;n</td>";
    sResponse += "<td><input onchange=\"window.location.href='?GREEN='+this.value;\" value='";
    sResponse += ledcolor.g;
    sResponse += "' min='0' max='255' type='range'>";
    sResponse += ledcolor.g;
    sResponse += "</td></tr>\r\n";
    sResponse += "<tr><td width='100'>Blau</td>";
    sResponse += "<td><input onchange=\"window.location.href='?BLUE='+this.value;\" value='";
    sResponse += ledcolor.b;
    sResponse += "' min='0' max='255' type='range'>";
    sResponse += ledcolor.b;
    sResponse += "</td></tr></table>\r\n";

    sResponse += "<br/><br/><table><tr><td width='100'>Zeitserver</td>";
    sResponse += "<td>";
    sResponse += ntp_server;
    sResponse += "</td></tr>\r\n";
/*
    sResponse += "<tr><td width='100'>Sync</td>";
    sResponse += "<td>";
    time_t lastSync (CE.toLocal(Ntp.getLastSync()));
    sResponse += hour(lastSync);
    sResponse += ":";
    sResponse += minute(lastSync);
    sResponse += ".";
    sResponse += second(lastSync);
    sResponse += "</td></tr>\r\n";
*/
    sResponse += "<tr><td width='100'>Software</td>";
    sResponse += "<td>";
    sResponse += clock_version;
    sResponse += "</td></tr></table>\r\n";
    sResponse += "</body></html>";

    sHeader = "HTTP/1.1 200 OK\r\n";
    sHeader += "Content-Length: ";
    sHeader += sResponse.length();
    sHeader += "\r\n";
    sHeader += "Content-Type: text/html\r\n";
    sHeader += "Connection: close\r\n";
    sHeader += "\r\n";
  }

  // Send the response to the client
  client.print(sHeader);
  client.print(sResponse);

  // and stop the client
  client.stop();
  Serial.println("Wifi Server: Client disonnected");
}



