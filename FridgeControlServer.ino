#include <ESP8266WiFi.h> 
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WifiExtensions.h>
#include "FridgeController.h"

ESP8266WebServer *webServer = NULL;
bool Connected = false;
String ConnectedTo = "";
int ApHidden = 0;
String ApSSID = "esp_fridge";
String ApPSK = "esp_fridge_pw"; //MUST BE AT LEAST 8 CHARS
String OTA_PASSWORD = "password";
int NumNetworks = 2;
String Networks[][2] = {
};


FridgeController Controller(D8, D4, 50, 55);
//401
const char *AccessDeniedPage = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>UnAuthorized</title></head><body><h2>Authorization Required</h2></body></html>";
//home page
const char *HomePageBegin = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Fridge Controller Settings</title><style type=\"text/css\">body{padding-top: 25px; padding-left: 50px;}.section{padding:0px 0px 0px 25px;}.sub-section{padding:5px 0px 5px 0px;}label{font-weight:bold; min-width: 200px ; padding-right:50px; width: auto; display:inline-block;}</style></head><body><h2>Fridge Controller</h2><div class=\"section\"> <a href=\"settings\">Settings</a> <br/> <br/>";
const char *HomeSection1 = "<div class=\"sub-section\"><label>Temperature</label><span>{{Temperature}}</span> </div>";
const char *HomeSection2 = "<div class=\"sub-section\"><label>Relay State</label><span>{{RelayState}}</span> </div>";
const char *HomePageEnd = "</div></body></html>";
//settings page
const char *SettingsPageBegin = "<!DOCTYPE html><html ><head> <meta charset=\"UTF-8\"> <title>Fridge Controller Settings</title> <style type=\"text/css\">body{padding-top: 25px; padding-left: 50px;}form{margin-top:-20px; padding:0; padding-left: 25px;}form .form-section{padding: 10px; clear:both;}.form-section .section-header{font-size:24px; margin-bottom:10px;}form .form-field{margin-left:20px; display:block; position:relative; clear:both;}.form-field label{font-weight:bold; min-width: 200px ; width: auto; /*display:inline-block;*/ float:left;}.form-field .form-control{font-weight:bold; width: 80px ; float:left; left:0; text-align:left; content-align:left;}.form-field .form-control[type=\"checkbox\"]{width: auto; margin:0;}#submit-button{margin:25px;}</style></head><body> <h2>Fridge Controller Settings</h2><form method=\"get\" action=\"update_settings\">";
String SettingsPageTemperatureSection = "<div class=\"form-section\"><h3 class=\"section-header\">Temperature</h3><div class=\"form-field\"><label>Temperature Sensor Pin</label><input class=\"form-control\" type=\"number\" name=\"TemperatureSensorPin\" min=\"0\" max=\"20\" value=\"{{TemperatureSensorPin}}\"/></div><div class=\"form-field\"><label>Min Temperature</label><input class=\"form-control\" type=\"number\" name=\"MinTemperature\" min=\"32\" max=\"100\" value=\"{{MinTemperature}}\" /></div><div class=\"form-field\"><label>Max Temperature</label><input class=\"form-control\" type=\"number\" name=\"MaxTemperature\" min=\"32\" max=\"100\" value=\"{{MaxTemperature}}\" /></div></div>";
String SettingsPageRelaySection = "<div class=\"form-section\"><h3 class=\"section-header\">Relay</h3><div class=\"form-field\"><label>Pin</label><input class=\"form-control\" type=\"number\" name=\"RelayPin\" min=\"0\" max=\"20\" value=\"{{RelayPin}}\" /></div></div>";
String SettingsPageMiscSection = "<div class=\"form-section\"> <h3 class=\"section-header\">Misc</h3></div><div class=\"form-field\"><label>Check Interval (ms)</label><input class=\"form-control\" type=\"number\" name=\"CheckInterval\" min=\"1\" value=\"{{CheckInterval}}\"/> </div><div class=\"form-field\"><label>Intranet Only</label><input class=\"form-control\" type=\"checkbox\" name=\"IntranetOnly\" {{IntranetOnly}}/> </div></div>";
const char *SettingsPageEnd = "<div class=\"form-section\"><a href=\"/\">Cancel</a><button id=\"submit-button\" type=\"submit\">Save</button> </div></form></body></html>";
//settings changed confirmation page
const char *SettingsUpdatedPage = "<!DOCTYPE html><html ><head> <meta charset='UTF-8'> <title>Fridge Controller Settings Saved</title> </head><body><h3>Settings Saved!</h3><p>You are being redirected...</p><script type='text/javascript'>document.addEventListener('DOMContentLoaded', function(event){setTimeout(function(){window.location=window.location.origin;},3000)});</script></body></html>";

void setup() {
	Serial.begin(115200);
	Serial.println("");
	Controller.Begin();
	SetupAP();
	SetupWebServer();
	ConnectToNetwork();
}

void loop() {
	yield();
	HandleClient();
	yield();
	int error = Controller.Refresh();
}

void SetupAP() {
	WiFi.mode(WIFI_AP_STA);
	Serial.println("");
	Serial.println("Setting up Access Point");
	WiFi.softAPConfig(
		IPAddress(192, 168, 4, 1),
		IPAddress(192, 168, 4, 1),
		IPAddress(255, 255, 255, 0)
	);
	WiFi.softAP(ApSSID.c_str(), ApPSK.c_str(), 1, ApHidden);
	Serial.println(WifiExtensions::IpToString(WiFi.softAPIP()));
	Serial.println("Setting up Access Point complete");
}

bool IsLocalIp(IPAddress &ip) {
	IPAddress myIp = WiFi.softAPIP();
	if (ip[0] == myIp[0] && ip[1] == myIp[1] && ip[2] == myIp[2]) {
		return true;
	}
	return false;
}

void SetupWebServer() {
	if (webServer != NULL) {
		delete webServer;
		webServer = NULL;
	}
	webServer = new ESP8266WebServer(80);
	webServer->on("/", HandleRoot);
	webServer->on("/settings", HandleSettings);
	webServer->on("/update_settings", HandleUpdateSettings);
	webServer->begin();
}

void HandleClient() {
	if (webServer != NULL) {
		webServer->handleClient();
	}
}

void HandleAccessDenied() {
	Serial.println("HandleAccessDenied. Remote IP: " + webServer->client().remoteIP().toString());
	webServer->send(401, "text/html", AccessDeniedPage);
}

void HandleRoot() {
	IPAddress ip = webServer->client().remoteIP();
	bool local = IsLocalIp(ip);
	Serial.println("Client Connected. Remote IP: " + ip.toString() + " IsLocal: " + (local ? "true" : "false"));
	if (!local && Controller.Config.IntranetOnly) {
		return HandleAccessDenied();
	}

	yield();

	//get a new reading
	Serial.print("RefreshTemps");
	int error = Controller.Controls.RefreshTemps();
	Serial.println(" error = " + String(error));
	yield();

	String s1 = HomeSection1;
	s1.replace("{{Temperature}}", String(Controller.Controls.GetTemperature()));
	
	String s2 = HomeSection2;
	s2.replace("{{RelayState}}", (Controller.Controls.IsRelayOn() ? "ON" : "OFF"));

	String page = HomePageBegin;
	page += s1;
	page += s2;
	page += HomePageEnd;
	webServer->send(200, "text/html", page);
}

void HandleSettings() {
	IPAddress ip = webServer->client().remoteIP();
	bool local = IsLocalIp(ip);
	Serial.println("Client Connected. Remote IP: " + ip.toString() + " IsLocal: " + (local ? "true" : "false"));
	if (!local && Controller.Config.IntranetOnly) {
		return HandleAccessDenied();
	}
	yield();
	String s1 = SettingsPageTemperatureSection;
	s1.replace("{{TemperatureSensorPin}}", String(Controller.Config.TemperatureSensorPin));
	s1.replace("{{MinTemperature}}", String(Controller.Config.MinTemperature));
	s1.replace("{{MaxTemperature}}", String(Controller.Config.MaxTemperature));

	yield();
	String s2 = SettingsPageRelaySection;
	s2.replace("{{RelayPin}}", String(Controller.Config.RelayPin));

	yield();
	String s3 = SettingsPageMiscSection;
	s3.replace("{{IntranetOnly}}", Controller.Config.IntranetOnly ? "checked" : "");
	s3.replace("{{CheckInterval}}", String(Controller.Config.CheckInterval));

	String page = SettingsPageBegin;
	page += s1;
	page += s2;
	page += s3;
	page += SettingsPageEnd;
	webServer->send(200, "text/html", page);
}

void HandleUpdateSettings() {
	IPAddress ip = webServer->client().remoteIP();
	bool local = IsLocalIp(ip);
	Serial.println("Client Connected. Remote IP: " + ip.toString() + " IsLocal: " + (local ? "true" : "false"));
	if (!local && Controller.Config.IntranetOnly) {
		return HandleAccessDenied();
	}
	UpdateSettings();
	webServer->send(200, "text/html", SettingsUpdatedPage);
}

void UpdateSettings() {
	Serial.println("TemperatureSensorPin " + webServer->arg("TemperatureSensorPin"));
	Serial.println("MinTemperature " + webServer->arg("MinTemperature"));
	Serial.println("MaxTemperature " + webServer->arg("MaxTemperature"));
	Serial.println("RelayPin " + webServer->arg("RelayPin"));
	Serial.println("IntranetOnly " + webServer->arg("IntranetOnly"));
	Serial.println("CheckInterval " + webServer->arg("CheckInterval"));


	Controller.Config.TemperatureSensorPin = webServer->arg("TemperatureSensorPin").toInt();
	Controller.Config.MinTemperature = webServer->arg("MinTemperature").toInt();
	Controller.Config.MaxTemperature = webServer->arg("MaxTemperature").toInt();
	Controller.Config.RelayPin = webServer->arg("RelayPin").toInt();
	Controller.Config.IntranetOnly = webServer->arg("IntranetOnly") == "on";
	Controller.Config.CheckInterval = webServer->arg("CheckInterval").toInt();

	Controller.WriteConfig();
}

bool ConnectToNetwork() {
	int timeout = 10000;
	int numNetworks = sizeof Networks / sizeof Networks[0];
	String ssid, pass;
	bool connected = false;
	for (int i = 0; i < numNetworks; i++) {
		ssid = Networks[i][0];
		pass = Networks[i][1];
		Serial.println("Attempting to connect to " + ssid);
		WiFi.begin(ssid.c_str(), pass.c_str());
		long _timeout = millis() + timeout;
		do {
			yield();
			delay(500);
			connected = WiFi.status() == WL_CONNECTED;
		} while (!connected && millis() < _timeout);

		if (connected) {
			Serial.println(WifiExtensions::IpToString(WiFi.localIP()));
			break;
		}
		else {
			Serial.println("Connect Failed");
		}
	}

}

