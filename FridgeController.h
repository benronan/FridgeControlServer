#ifndef FRIDGECONTROL_H_
#define FRIDGECONTROL_H_

#include <Wire.h>
#include <SPI.h>
//#include <Adafruit_Sensor.h>
//#include <Adafruit_BMP280.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

enum FridgeControllerErrors {
	NoError = 0,
	TemperatureSensorNotFound
};


class FridgeConfig {
public:
	FridgeConfig() {
		IntranetOnly = false;
		CheckInterval = 1000 * 60 * 5;
		TemperatureSensorPin = 0;
		RelayPin = 0;
		MaxTemperature = 55;
		MinTemperature = 40;
	}
	int CheckInterval;
	bool IntranetOnly;
	int TemperatureSensorPin;
	int RelayPin;
	int MaxTemperature;
	int MinTemperature;
};

class FridgeControls {

public:

	FridgeControls() : FridgeControls(0, 0) {}

	FridgeControls(int _tempPin, int _relayPin) {
		SetTemperaturePin(_relayPin);
		SetRelayPin(_relayPin);
		RelayOnTime = 0;
		RelayOffTime = 0;
	}

	void SetTemperaturePin(int _pin) {
		if (TemperaturePin != _pin) {
			TemperaturePin = _pin;
			if (TemperaturePin > 0) {
				pinMode(TemperaturePin, INPUT);
			}
			InitTemperatureSensor();
		}
	}

	int GetTemperaturePin() {
		return TemperaturePin;
	}

	bool IsRelayOn() {
		return RelayOn;
	}

	void SetRelayPin(int _pin) {
		RelayPin = _pin;
		if (RelayPin > 0) {
			pinMode(RelayPin, OUTPUT);
		}
	}

	int GetRelayPin() {
		return RelayPin;
	}

	bool TurnRelayOn(bool _on = true) {
		if (RelayPin > 0) {
			digitalWrite(RelayPin, _on == true ? HIGH : LOW);
			RelayOn = _on;
			RelayOnTime = millis();
			RelayOffTime = millis();
		}
	}

	long GetRelayOnTime() {
		return RelayOnTime > 0 ? millis() - RelayOnTime : 0;
	}

	long GetRelayOffTime() {
		return RelayOffTime > 0 ? millis() - RelayOffTime : 0;
	}

	float GetTemperature() {
		return Temperature;
	}

	int RefreshTemps() {
		//read temperatures
		if (!TemperatureSensorPresent) {
			return FridgeControllerErrors::TemperatureSensorNotFound;
		}
		sensors->requestTemperatures();
		Temperature = DallasTemperature::toFahrenheit(sensors->getTempC(TemperatureSensorAddress));
	}

	void Begin() {
		InitTemperatureSensor();
	}

private:
	int RelayPin;
	bool RelayOn;
	long RelayOnTime;
	long RelayOffTime;

	int TemperaturePin;
	bool TemperatureSensorPresent;
	float Temperature;


	OneWire *oneWire;
	DallasTemperature *sensors = NULL;
	DeviceAddress TemperatureSensorAddress;
	int TemperatureSensorResolution;


	int InitTemperatureSensor() {
		if (sensors != NULL) {
			delete sensors;
			sensors = NULL;
		}
		if (oneWire != NULL) {
			delete oneWire;
			oneWire = NULL;
		}
		oneWire = new OneWire(GetTemperaturePin());
		sensors = new DallasTemperature(oneWire);
		sensors->begin();
		int deviceCount = sensors->getDeviceCount();
		if (!deviceCount || !sensors->getAddress(TemperatureSensorAddress, 0)) {
			TemperatureSensorPresent = false;
			return FridgeControllerErrors::TemperatureSensorNotFound;
		}
		TemperatureSensorPresent = true;
		sensors->setResolution(TemperatureSensorAddress, TemperatureSensorResolution);
		sensors->begin();
	}

};

class FridgeController {
public:
	FridgeController() : FridgeController(0, 0,50,55) {}

	FridgeController(int _tempPin, int _relayPin, int _minTemp, int _maxTemp) {
		LastRefreshTime = 0;
		IgnoreErrors = false;
		Controls.SetTemperaturePin(_tempPin);
		Controls.SetRelayPin(_relayPin);
		Config.MinTemperature = _minTemp;
		Config.MaxTemperature = _maxTemp;
	}

	~FridgeController() {}

	bool IgnoreErrors;
	FridgeConfig Config;
	FridgeControls Controls;

	void Begin() {
		if (IsSavedConfigValid()) {
			Serial.println("Saved config is valid. loading from EEPROM");
			yield();
			LoadConfig();
			yield();
		}
		else {
			Serial.println("Saved config is not valid");
		}
		Controls.Begin();
	}

	int Refresh() {
		if (millis() > (Config.CheckInterval + LastRefreshTime)) {
			return RefreshInternal();
		}
		return FridgeControllerErrors::NoError;
	}

	void WriteConfig() {
		Serial.println("WriteConfig");
		SaveConfigValidationMark();
		EEPROM.begin(eepromSize);
		EEPROM.put(eepromConfigOffset + 1, Config);
		EEPROM.commit();
		EEPROM.end();
		Serial.println("SaveConfig Complete");
		UpdateControlsConfig();
	}

	void PrintConfig() {
    Serial.println("Temperature Pin" + String(Config.TemperatureSensorPin));
    Serial.println("Relay Pin" + String(Config.RelayPin));
    Serial.println("MinTemperature " + String(Config.MinTemperature));
    Serial.println("MaxTemperature " + String(Config.MaxTemperature));
    Serial.println("CheckInterval " + String(Config.CheckInterval));
    Serial.println("IntranetOnly " + String(Config.IntranetOnly));
	}

	void UpdateControlsConfig() {
		Controls.SetTemperaturePin(Config.TemperatureSensorPin);
		Controls.SetRelayPin(Config.RelayPin);
	}

private:
	long LastRefreshTime;

	int RefreshInternal() {
		Serial.println("\n*** Refresh Internal ***");

		LastRefreshTime = millis();

		//read temperatures
		Serial.println("Refresh Temps");
		int error = Controls.RefreshTemps();
		if (error > 0 && IgnoreErrors != true) {
			return error;
		}

		yield();

		float temperature = Controls.GetTemperature();
		Serial.println("Temperature = " + String(temperature));

		//Check if we need to change relay state
		Serial.println("Checking Relay");
		if (temperature >= Config.MaxTemperature) {
			Serial.println("Max Temperature Exceeded");
			//if (!Controls.IsRelayOn()) {
			Serial.println("Turning ON");
			Controls.TurnRelayOn(true);
			//}
			Serial.println("ON");
		}
		else if (temperature < Config.MinTemperature) {
			//if (Controls.IsRelayOn()) {
			Serial.println("Turning OFF");
			Controls.TurnRelayOn(false);
			//}
			Serial.println("OFF");
		}

		yield();
		Serial.println("*** Refresh Internal Complete ***");
		return FridgeControllerErrors::NoError;
	}

	const char eepromConfigValidationMark = 'g';
	int eepromConfigOffset = 32;
	const int eepromSize = 256;
	void SaveConfigValidationMark() {
		Serial.println("SaveConfigValidationMark");
		EEPROM.begin(eepromSize);
		EEPROM.put(eepromConfigOffset, eepromConfigValidationMark);
		EEPROM.commit();
		EEPROM.end();
		Serial.println("SaveConfigValidationMark Complete");
	}

	bool IsSavedConfigValid() {
		EEPROM.begin(eepromSize);
		char c;
		EEPROM.get(eepromConfigOffset, c);
		EEPROM.end();
		if (c == eepromConfigValidationMark) {
			return true;
		}
		return false;
	}

	void LoadConfig() {
		Serial.println("LoadConfig");
		EEPROM.begin(eepromSize);
		EEPROM.get(eepromConfigOffset + 1, Config);
		EEPROM.end();
		Serial.println("LoadConfig Complete");
	}
};

#endif

