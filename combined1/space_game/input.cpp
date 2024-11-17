#include "input.h"

// Create a struct_message called myData
struct_message myData;

// Create a structure to hold the readings from each board
struct_message board1;
struct_message board2;

// Create an array with all the structures
struct_message boardsStruct[2] = {board1, board2};

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incomingData, int len) {
  char macStr[18];
  Serial.print("Packet received from: ");
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.printf("Board ID %u: %u bytes\n", myData.id, len);
  // Update the structures with the new incoming data
  boardsStruct[myData.id-1].yaw = myData.yaw;
  boardsStruct[myData.id-1].pitch = myData.pitch;
  boardsStruct[myData.id-1].roll = myData.roll;
  boardsStruct[myData.id-1].x = myData.x;
  boardsStruct[myData.id-1].y = myData.y;
  boardsStruct[myData.id-1].button = myData.button;
  Serial.printf("yaw value: %d \n", boardsStruct[myData.id-1].yaw);
  Serial.printf("pitch value: %d \n", boardsStruct[myData.id-1].pitch);
  Serial.printf("roll value: %d \n", boardsStruct[myData.id-1].roll);
  Serial.printf("x value: %d \n", boardsStruct[myData.id-1].x);
  Serial.printf("y value: %d \n", boardsStruct[myData.id-1].y);
  Serial.printf("button value: %d \n", boardsStruct[myData.id-1].button);
  Serial.println();
}

// Function to initialize ESP-NOW
void initESPNow() {
    // Set device as a Wi-Fi Station
    WiFi.mode(WIFI_STA);

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    // Register the receive callback
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}