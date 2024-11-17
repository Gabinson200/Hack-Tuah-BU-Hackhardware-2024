#ifndef INPUT_H
#define INPUT_H

#include <esp_now.h>
#include <WiFi.h>

typedef struct struct_message {
    int id; // must be unique for each sender board
    float yaw;
    float pitch;
    float roll;
    float x;
    float y;
    bool button;
} struct_message;



// Function declaration for initializing ESP-NOW
void initESPNow();

// Function declaration for the callback that handles received data
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);



#endif
