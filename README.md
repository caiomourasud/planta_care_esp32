# Planta Care ESP32

## Project Description

The **Planta Care ESP32** project is a plant monitoring system that uses an ESP32 to collect sensor data and send it to Firebase. It can monitor soil moisture, temperature, air humidity, and light levels, and send this data to Firebase's Firestore and Realtime Database.

## Firebase Configuration

To configure Firebase, you will need a Firebase project with Firestore and Realtime Database enabled. The following configurations are required:

- **Firestore**: Used to store sensor readings in a document database.
- **Realtime Database**: Used to enable/disable real-time data sending.

### Configuration Steps:
1. Create a project in Firebase.
2. Enable Firestore and Realtime Database.
3. Obtain the Firebase project ID and API key.
4. Set the `FIREBASE_PROJECT_ID` and `FIREBASE_API_KEY` constants in the code.

## Data Sending

### Sending Every 30 Minutes
The system sends sensor data to Firestore every 30 minutes. This includes soil moisture, temperature, air humidity, and light data.

### Real-Time Sending
When activated, the system sends data in real-time to the Realtime Database for 2 minutes. This is useful for real-time monitoring of plant conditions.

## Sensors Used

- **DHT22**: Temperature and air humidity sensor.
- **Soil Moisture Sensor**: Measures soil moisture.
- **Adafruit VEML7700**: Light sensor.

## Data Conversions

- **Soil Moisture**: The analog reading is converted into a percentage value from 0 to 100%.
- **Temperature and Air Humidity**: Obtained directly from the DHT22 sensor.
- **Light**: Measured in lux by the VEML7700 sensor.

## How to Use
1. Configure Wi-Fi and Firebase credentials in the code.
2. Upload the code to the ESP32.
3. Monitor the data through Firebase.
