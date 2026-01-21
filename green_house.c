#include <xc.h>
#include <stdio.h>

// Configuration Bits
#pragma config FOSC = HS, WDTE = OFF, PWRTE = ON, BOREN = ON, LVP = OFF
#define _XTAL_FREQ 20000000 

// Tank Level Inputs
#define LEVEL_LOW    RC2
#define LEVEL_MED    RC3
#define LEVEL_HIGH   RC4

// Tank Level LEDs
#define LED_LOW_TANK RC5
#define LED_MED_TANK RC6
#define LED_MAX_TANK RC7

// Pin Definitions
#define RS RC0
#define E  RC1
#define D4 RD4
#define D5 RD5
#define D6 RD6
#define D7 RD7
#define WL1 RC2
#define WL2 RC3
#define WL3 RC4
#define WL1O RC5
#define WL2O RC6
#define WL3O RC7

#define DHT11_PIN  RB0
#define DHT11_DIR  TRISB0
#define BUTTON     RB1
#define LED1       RB2
#define LED2       RB3
#define LED3       RB4
#define PUMP       RB5    // Pump Relay Pin
#define LDR_DIGITAL RA1  // LDR Digital Input

// Global Variables
int display_mode = 0; 

// --- ADC Functions ---
void ADC_Init() {
    ADCON1 = 0x8E; // RA0 Analog, RA1 Digital
    ADCON0 = 0x41; 
}

unsigned int ADC_Read(unsigned char channel) {
    ADCON0 &= 0xC7;         
    ADCON0 |= (channel << 3); 
    __delay_ms(2);          
    GO_nDONE = 1;           
    while(GO_nDONE);        
    return ((ADRESH << 8) + ADRESL);
}

// --- LCD Functions ---
void lcd_enable_pulse(void) { E = 1; 
                             __delay_us(1);
                             E = 0; 
                             __delay_us(100); }
void lcd_send_nibble(unsigned char nibble) {
    D4 = (nibble >> 0) & 0x01;
    D5 = (nibble >> 1) & 0x01;
    D6 = (nibble >> 2) & 0x01;
    D7 = (nibble >> 3) & 0x01;
    lcd_enable_pulse();
}
void lcd_command_4bit(unsigned char cmd) {
    RS = 0; 
    lcd_send_nibble(cmd >> 4); 
    lcd_send_nibble(cmd & 0x0F);
    __delay_ms(2);
}
void lcd_data_4bit(unsigned char data) {
    RS = 1;
    lcd_send_nibble(data >> 4); 
    lcd_send_nibble(data & 0x0F); 
    __delay_ms(2);
}
void lcd_init_4bit(void) {
    TRISC0 = 0; TRISC1 = 0; TRISD = 0x00;
    __delay_ms(20); RS = 0;
    lcd_send_nibble(0x03); 
    __delay_ms(5);
    lcd_send_nibble(0x03); 
    __delay_us(150);
    lcd_send_nibble(0x03);
    __delay_us(150);
    lcd_send_nibble(0x02);
    lcd_command_4bit(0x28); 
    lcd_command_4bit(0x0C);
    lcd_command_4bit(0x01); 
    lcd_command_4bit(0x06);
}
void lcd_set_cursor_4bit(unsigned char row, unsigned char col) {
    unsigned char address = (row == 1) ? (0x80 + col) : (0xC0 + col);
    lcd_command_4bit(address);
}
void lcd_string_4bit(const char *str) { while(*str) lcd_data_4bit(*str++); }

// --- DHT11 Functions ---
void dht11_start(void) {
    DHT11_DIR = 0; DHT11_PIN = 0; 
    __delay_ms(18);
    DHT11_PIN = 1; 
    __delay_us(30);
    DHT11_DIR = 1;
}
unsigned char dht11_read_byte(void) {
    unsigned char i, result = 0;
    for (i = 0; i < 8; i++) {
        while (!DHT11_PIN);
        __delay_us(30);
        if (DHT11_PIN) result |= (1 << (7 - i));
        while (DHT11_PIN);
    }
    return result;
}

void main(void) {
    unsigned char rh_i, temp_i;
    unsigned int soil_raw;
    int soil_percent;
    char buffer[16];

    // Port Configuration
    TRISA = 0x03; // RA0, RA1 as Inputs
    TRISB = 0x03; // RB0, RB1 as Inputs. RB2-RB5 as Outputs
    PORTB = 0x00; // Initialize all LEDs and Pump to OFF
    
    TRISCbits.TRISC2=1;
    TRISCbits.TRISC3=1;
    TRISCbits.TRISC4=1;
    TRISCbits.TRISC5=0;
    TRISCbits.TRISC6=0;
    TRISCbits.TRISC7=0;

    ADC_Init();
    lcd_init_4bit();

    while(1) {
        
        // --- 1. Tank Level LED Logic (Always On) ---
        if(LEVEL_LOW)  LED_LOW_TANK = 1; 
        else LED_LOW_TANK = 0;
        if(LEVEL_MED)  LED_MED_TANK = 1; 
        else LED_MED_TANK = 0;
        if(LEVEL_HIGH) LED_MAX_TANK = 1; 
        else LED_MAX_TANK = 0;
        
        // --- 1. LDR & LED Logic ---
        if(LDR_DIGITAL == 1) { 
            LED1 = 1; LED2 = 1; LED3 = 1; // Day
        } else {
            LED1 = 0; LED2 = 0; LED3 = 0; // Night
        }

        // --- 2. Soil Moisture Calculation ---
        soil_raw = ADC_Read(0);
        int dry_val = 600; 
        int wet_val = 300; 
        soil_percent = ((long)(dry_val - soil_raw) * 100) / (dry_val - wet_val);
        
        if(soil_percent > 100) soil_percent = 100;
        if(soil_percent < 0)   soil_percent = 0;

        // --- 3. Automatic Pump Control ---
        if(soil_percent < 40) {
            PUMP = 0; // Turn ON pump
        } 
        else if(soil_percent > 75) {
            PUMP = 1; // Turn OFF pump once moisture is sufficient
        }

        // --- 4. Button & Display Logic ---
        if(BUTTON == 1) {
            __delay_ms(50);
            if(BUTTON == 1) {
                display_mode = !display_mode;
                lcd_command_4bit(0x01); 
                while(BUTTON == 1);
            }
        }

        if(display_mode == 0) {
            // MODE: Temp & Humidity
            dht11_start();
            __delay_us(40);
            if (!DHT11_PIN) {
                __delay_us(80);
                if (DHT11_PIN) {
                    __delay_us(40);
                    rh_i = dht11_read_byte(); dht11_read_byte();
                    temp_i = dht11_read_byte(); dht11_read_byte();
                    dht11_read_byte(); 

                    sprintf(buffer, "T:%dC H:%d%%", temp_i, rh_i);
                    lcd_set_cursor_4bit(1, 0); lcd_string_4bit(buffer);
                    
                    lcd_set_cursor_4bit(2, 0);
                    if(PUMP == 0) lcd_string_4bit("Pump: RUNNING   ");
                    else          lcd_string_4bit("Pump: STANDBY   ");
                }
            }
        } 
        else {
            // MODE: Soil & Light
            sprintf(buffer, "Soil: %d %%   ", soil_percent);
            lcd_set_cursor_4bit(1, 0); lcd_string_4bit(buffer);
            
            lcd_set_cursor_4bit(2, 0);
            if(LDR_DIGITAL == 1) lcd_string_4bit("Nighty Night");
            else                 lcd_string_4bit("Good Day");
        }

        __delay_ms(100); 
    }
}
