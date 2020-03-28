#include <mcp_can.h>
#include <mcp_can_dfs.h>

// CAN-BUS Shield: Meant for A123 Modules

#include <SPI.h>

#include "a123_can_dfs.h"

// Install the can bus seeed shield software on arduino library

// TODO the balancing commands don't work, or it's not clear if it's working
// Doesn't show cell level BMS reads, always returning only frame 2
// This might be related to the wrong command going out for detailed response.


unsigned char Flag_Recv = 0;
unsigned char unwritten_data = 0;

unsigned char len[8];
long unsigned int id[8];
unsigned char buf[64];
unsigned char buffersUsed = 0;

unsigned char key = 0;

unsigned char no_balance[8] = {0, 0xFF, 0XF0, 0, 0, 0, 0, 0};
unsigned char no_balance_e[8] = {0x01, 0xFF, 0XF0, 0, 0, 0, 0, 0};
unsigned char balance[8] = {0, 0x8F, 0xC0, 0, 0, 0, 0, 0};
unsigned char balance_32[8] = {0, 0x89, 0x80, 0, 0, 0, 0, 0};
unsigned char balance_328[8] = {0, 0x8E, 0x80, 0, 0, 0, 0, 0};
unsigned char balance_e[8] = {0x01, 0x8F, 0xC0, 0, 0, 0, 0, 0};
unsigned char empty_message[8];


MCP_CAN CAN(9); // Set CS to pin 10 for Sparkfun, pin 9 for Seeed


//Run main power and enable charging on same realy? nneds 12v not gate
#define MAIN_POWER 4 //Main Drive Relay, high on, low off
#define ENABLE_CHARGING 5 //BMS cutoff for charing, high on, low off
#define CHARGING 6 //???? maybe show if charge is coming in?
#define OVER_VOLTAGE_ALARM 7 //A battery is above OVER_VOLTAGE_ALARM_LEVEL

#define OVER_VOLTAGE_ALARM_LEVEL 3.57 //volts to trigger an over voltage alarm event

void setup() {


    //LED & Relay Controls
    pinMode(MAIN_POWER, OUTPUT);
    pinMode(ENABLE_CHARGING, OUTPUT);
    pinMode(CHARGING, OUTPUT);
    pinMode(OVER_VOLTAGE_ALARM, OUTPUT);

    //High is off, low on
    digitalWrite(MAIN_POWER,HIGH);
    digitalWrite(ENABLE_CHARGING,LOW);
    digitalWrite(CHARGING,HIGH);
    digitalWrite(OVER_VOLTAGE_ALARM,HIGH);


    Serial.begin(115200);

    //On CANBus: Currently the 4 BMS modules of A123
    //ALSO 1 charger 1 charge controller, can they live on 500 kbps canbus?
    while (CAN_OK != CAN.begin(CAN_500KBPS)) {
        Serial.println("CAN BUS Shield init fail");
        Serial.println("Init CAN BUS Shield again");
        delay(100);
    }

    Serial.println("CAN BUS Shield init ok!");

    attachInterrupt(0, MCP2515_ISR, FALLING); // start interrupt
}

void MCP2515_ISR()
{
     Flag_Recv = 1;
}


unsigned int getBits(int startBit, int length, unsigned char *buf) {
    unsigned int val = 0;
    unsigned char startBitByte = startBit / 8;
    unsigned char bitShift = 0;
    unsigned char currentBit = startBit % 8;
    unsigned char currentByte = buf[startBitByte];
    
    if (length <= 8) {
        unsigned char mask = 0;
        for (char i = 0; i < length; i++) {
           mask += 1 << (currentBit + i);
        }
        val = (currentByte & mask) >> currentBit ;
    } else {
        while (length > 0) {
            val += (currentByte >> currentBit) << (bitShift);
            bitShift += 8 - currentBit;
            length -= 8 - currentBit;
            currentBit = 0;
            startBitByte -= 1;
            currentByte = buf[startBitByte];
        }
    }
    return val;
}

void setBits(unsigned int startBit, unsigned int length, unsigned char *buf, unsigned int data) {
    unsigned int endBit = startBit + length - 1;
    for  (int i = 0; i < length; i++) {
      unsigned maskedData = data & 1;
      if (maskedData) {
        unsigned char mask = maskedData << (7 - ((endBit - i) % 8));
        buf[(endBit - i) / 8] = buf[(endBit - i) / 8] | mask;
      } else {
        unsigned char mask = ~(~maskedData << (7 - ((endBit - i) % 8)));
        buf[(endBit - i) / 8] = buf[(endBit - i) / 8] & mask;
      }
      data = data >> 1;
    }
}

/*
* Turns off when an over voltage is detected
* CONFIRM this is not adjusting buf pointer
*/


void safetyShutoff(unsigned char *buf) {
    
    unsigned char mod_cell_undervolt = getBits(3, 1, buf);
    unsigned char mod_cell_overvolt = getBits(2, 1, buf);
    unsigned char mod_response_id = getBits(4, 4, buf);
    unsigned char mod_tmp_chn = getBits(24, 3, buf);
    unsigned char mod_therm_x = getBits(8, 8, buf) + MOD_THERM_OFFSET;

    float mod_v_min = getBits(27, 13, buf) * 0.0005 + MOD_V_OFFSET;
    float mod_v_max = getBits(43, 13, buf) * 0.0005 + MOD_V_OFFSET;
    float mod_v_avg = getBits(59, 13, buf) * 0.0005 + MOD_V_OFFSET;

    unsigned char mod_bal_cnt = getBits(40, 3, buf);
    unsigned char mod_v_compare_oor = getBits(58, 1, buf);

    //over voltage alarm
    if (mod_v_max > OVER_VOLTAGE_ALARM_LEVEL) {
        Serial.println("********* ALARM **********");
        Serial.println("OVER VOLTAGE EVENT!!!: ");
        Serial.println(mod_v_max);
        
        disableTheCharger(); //TURN off charger
        turnOnAlarm(); //SOUND the alarm
        Serial.println("********* ALARM **********");
    }
}

void turnOnAlarm(){
    digitalWrite(OVER_VOLTAGE_ALARM,LOW);
}

void turnOffAlarm(){
    digitalWrite(OVER_VOLTAGE_ALARM,HIGH);
}

void enableTheCharger(){
    digitalWrite(ENABLE_CHARGING,LOW);
}

void disableTheCharger(){
    digitalWrite(ENABLE_CHARGING,HIGH);
}

void enableMotorController(){
    digitalWrite(MAIN_POWER,HIGH);
}
void disableMotorController(){
    digitalWrite(MAIN_POWER,LOW);
}




void print200Message(unsigned char *buf) {
    
    unsigned char mod_cell_undervolt = getBits(3, 1, buf);
    unsigned char mod_cell_overvolt = getBits(2, 1, buf);
    unsigned char mod_response_id = getBits(4, 4, buf);
    unsigned char mod_tmp_chn = getBits(24, 3, buf);
    unsigned char mod_therm_x = getBits(8, 8, buf) + MOD_THERM_OFFSET;

    float mod_v_min = getBits(27, 13, buf) * 0.0005 + MOD_V_OFFSET;
    float mod_v_max = getBits(43, 13, buf) * 0.0005 + MOD_V_OFFSET;
    float mod_v_avg = getBits(59, 13, buf) * 0.0005 + MOD_V_OFFSET;

    unsigned char mod_bal_cnt = getBits(40, 3, buf);
    unsigned char mod_v_compare_oor = getBits(58, 1, buf);

    Serial.print("Message Response ID: ");
    Serial.println(mod_response_id);

    Serial.print("Cell Undervoltage detected: ");
    Serial.println(mod_cell_undervolt);

    Serial.print("Cell Overvoltage detected: ");
    Serial.println(mod_cell_overvolt);

    Serial.print("Temperature Channel: ");
    Serial.print(mod_tmp_chn);
    Serial.print("; Temperature: ");
    Serial.println(mod_therm_x);

    Serial.print("Minimum Cell Voltage: ");
    Serial.println(mod_v_min);
    Serial.print("Average Cell Voltage: ");
    Serial.println(mod_v_avg);
    Serial.print("Maximum Cell Voltage: ");
    Serial.println(mod_v_max);

    Serial.print("Number of Active Balance Circuits: ");
    Serial.println(mod_bal_cnt);

    Serial.print("Voltage Mismatch Detected: ");
    Serial.println(mod_v_compare_oor);
}

void printExtendedMessage(unsigned char offset, unsigned char *buf) {
    unsigned char mod_bal_cell_01 = getBits(10, 1, buf);
    unsigned char mod_bal_cell_02 = getBits(26, 1, buf);
    unsigned char mod_bal_cell_03 = getBits(42, 1, buf);
    unsigned char mod_bal_cell_04 = getBits(58, 1, buf);
    
    float mod_v_cell_01 = getBits(11, 13, buf) * 0.0005 + MOD_V_OFFSET;
    float mod_v_cell_02 = getBits(27, 13, buf) * 0.0005 + MOD_V_OFFSET;
    float mod_v_cell_03 = getBits(43, 13, buf) * 0.0005 + MOD_V_OFFSET;
    float mod_v_cell_04 = getBits(59, 13, buf) * 0.0005 + MOD_V_OFFSET;
    
    Serial.print("Cell "); Serial.print(offset + 1); Serial.print(" State: "); Serial.print(mod_bal_cell_01); Serial.print(" Voltage: "); Serial.println(mod_v_cell_01);
    Serial.print("Cell "); Serial.print(offset + 2); Serial.print(" State: "); Serial.print(mod_bal_cell_02); Serial.print(" Voltage: "); Serial.println(mod_v_cell_02);
    Serial.print("Cell "); Serial.print(offset + 3); Serial.print(" State: "); Serial.print(mod_bal_cell_03); Serial.print(" Voltage: "); Serial.println(mod_v_cell_03);
    Serial.print("Cell "); Serial.print(offset + 4); Serial.print(" State: "); Serial.print(mod_bal_cell_04); Serial.print(" Voltage: "); Serial.println(mod_v_cell_04);
}




unsigned long CurrentTime = millis();

void loop() {

    //Each battery has 2 BMS, the car shipped with 4 Modules hence 8 BMS'
    //Relly only want ID 206 and 207 for this cellI believe the car in total has:
    //201,202,203,204,205,206,207

    //Poll every 10 seconds to see if high volatge state
    if (millis() >= CurrentTime) {
      CurrentTime = millis() + 10000; //wait 10 seconds
      //this should only be polling values, not setting balance!
      CAN.sendMsgBuf(BCM_CMD_ID, 0, 3, no_balance_e);
      Serial.println("Sending Do Not Balance Message, Reqeusting Extended Response");
    }

  
    if (Serial.available()) {
        char key1 = Serial.read();
        // send data:  id = 0x00, standard flame, data len = 8, stmp: data buf
        switch (key1) {
            case 98: //b
                CAN.sendMsgBuf(BCM_CMD_ID, 0, 3, balance);
                Serial.println("Sending Balance to 3.3V Message");
                break;
            case 'c':
                CAN.sendMsgBuf(BCM_CMD_ID, 0, 3, balance_32);
                Serial.println("Sending Balance to 3.2V Message");
                break;
            case 'd':
                CAN.sendMsgBuf(BCM_CMD_ID, 0, 3, balance_328);
                Serial.println("Sending Balance to 3.28V Message");
                break;
            case 109: //m
                CAN.sendMsgBuf(BCM_CMD_ID, 0, 3, balance_e);
                Serial.println("Sending Balance to 3.3V Message, Reqeusting Extended Response");            
                break;
            case 110: //n
                CAN.sendMsgBuf(BCM_CMD_ID, 0, 3, no_balance);
                Serial.println("Sending Do Not Balance Message");
                break;
            case 101: //e
                CAN.sendMsgBuf(BCM_CMD_ID, 0, 3, no_balance_e);
                Serial.println("Sending Do Not Balance Message, Reqeusting Extended Response");
                break;
            default:
                Serial.println("Error: Command not defined");
        }  
    }
    // If we get data write to buffer
    if (Flag_Recv && !unwritten_data) {                   // check if get data
        digitalWrite(12, LOW); 
        digitalWrite(13, LOW); 

        Flag_Recv = 0;
        unwritten_data = 1;
        buffersUsed = 0;
        while (CAN_MSGAVAIL == CAN.checkReceive()) {
            CAN.readMsgBufID(&id[buffersUsed], &len[buffersUsed], buf + 8 * buffersUsed);    // read data,  id: message id, len: data length, buf: data buf            
            buffersUsed += 1;
            if (buffersUsed > 7) {
                Serial.print("Error");
                break;
            }
        }
    } else {
        for (int i = 0 ; i < buffersUsed; i++) {

            String id_string = String(id[i],HEX);
            int id_int = id_string.toInt();
            if (id_int >= 201 && id_int <= 208) {

                safetyShutoff(buf + 8 * i); //check if over voltage event
    
    
    
                Serial.print("Buffers Used: ");
                Serial.println(buffersUsed);
                Serial.print("Message: ");
                Serial.println(i);
                Serial.print("ID: ");
                Serial.print(id[i], HEX);
                Serial.print("; Len: ");
                Serial.println(len[i]);
                unsigned char id_mod = id[i] >> 8;
                //Why is the id_mod never coming up w/ detailed view?    
                //Serial.print ("id_mod: ");
                //Serial.println(id_mod);
               
    
                if (id_mod == 0x2) {
                    Serial.println();
                    print200Message(buf + 8 * i);
                } else if (id_mod == 0x3 || id_mod == 0x4 || id_mod == 0x5 || id_mod == 0x6) {
                    Serial.println();
                    printExtendedMessage((id_mod - 3) * 4, buf + 8 * i);
                } else {
                    Serial.print("; Data: ");
                    for (int j = 0; j<len[i]; j++) {
                        Serial.print(buf[j + (8 * i)], HEX);Serial.print("\t");
                    }
                }
                Serial.println();
            } else {
                Serial.println("--------------------BUFFER USED----------------");
                Serial.print ("IGNORING BUFFER: ");
                Serial.println(id[i], HEX);
                Serial.println("-----------------------------------------------");

            }

        }
        buffersUsed = 0;
        unwritten_data = 0;
    }
}
