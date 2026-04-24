/*
 * MEGA_copy.c
 *
 * Created: 19/04/2026 20.46.16
 *  Author: htink
 */ 

#define F_CPU 16000000UL 
//#define FOSC 16000000UL

// Imported Libraries
#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <string.h>
#include "uart.h"
#include "mcu.h"
#include <stdint.h>
#include "lcd.h"
#include "keypad.h"
#include "delay.h"
#include "avr_gpio.h"
#include "board_config.h"
#include "power_save.h"


//Global Variable
int32_t MIN_FLOOR =  0; 
int32_t MAX_FLOOR = 99;
int16_t CURRENT_FLOOR = 0; 


static int16_t queue_data[99]; // floor storage array
int16_t storage_size; // initial size of the array;


//Given Delay Durations
#define DOOR_OPEN_DURATION_MS (3000) 
#define DOOR_CLOSE_DURATION_MS (2000)
#define FLOOR_MOVING_SPEED_MS (3000)
#define OBSTACLE_DETECTED_DURATION_MS (3000) 

// Button definitions for obstacle detections
#define BUTTON_PIN PB7 // Digital 13 in MEGA
#define BUTTON_DDR DDRB
#define BUTTON_PORT PORTB

#define NO_KEY_PRESSED  (0xFF)
#define LCD_MAX_STRING (32)


void delay_variable_ms(uint16_t ms)
{
    while (ms--) {
        _delay_ms(1);
    }
}


typedef enum {
    IDLE = 0,
    GOINGUP = 1,
    GOINGDOWN = 2,
    DOOR_OPENING = 3,
    DOOR_CLOSING = 4,
    FAULT = 5,
} state_t;


static void handle_error(uint8_t return_code)
{
    if (return_code)
    {
        while(1);
    }
}

static void write_to_lcd(const char *string) {
    uint8_t len = strnlen(string, LCD_MAX_STRING);
    if (LCD_MAX_STRING <= len) {
        printf("Failed to print LCD string. Too big or lacks NULL-terminator.\r\n");
        for (uint8_t i = 0; i < len; i++)
        {
            printf("%c", string[i]);
        }
        printf("\r\n");
        handle_error(1);
        } else {
        printf("LCD output: '%s'\r\n", string);
        lcd_puts(string);
    }
}

void spi_master_send(uint8_t *data, uint8_t length)
{
    PORTB &= ~(1 << PB0); // SS LOW

    for (uint8_t i = 0; i < length; i++)
    {
        SPDR = data[i];

        while (!(SPSR & (1 << SPIF)));

        volatile uint8_t dummy = SPDR; // clear SPDR
    }

    PORTB |= (1 << PB0); // SS HIGH
}

void spi_master_receive(uint8_t *buffer, uint8_t length)
{
    PORTB &= ~(1 << PB0); // SS LOW

    for (uint8_t i = 0; i < length; i++)
    {
        SPDR = 0xFF; // dummy data to clock slave

        while (!(SPSR & (1 << SPIF)));

        buffer[i] = SPDR;
    }

    PORTB |= (1 << PB0); // SS HIGH

    buffer[length] = '\0'; // make it string-safe
}
// Emergency function . It returns 0 and 1 
uint8_t Emergency_Pressed(){
    if (!(PINH & (1<<PH4))) // This checks whether the button is pressed or not . 
    {
        printf("Button for emergency is pressed.");
        // LED is still on D12 (PB6)
        DDRB |= (1 << PB6);   // set as output

        // Turn LED ON  BY MAKING THE PORT HIGH
        PORTB |= (1 << PB6);
        DELAY_ms(2000);

        // Turn LED OFF BY MAKING PORT LOW 
        PORTB &= ~(1 << PB6);
        DELAY_ms(2000);
        
        return 1;
    }
    return 0;
}


// Amount of floor requests (how many floors will be ordered)

static int16_t amount_floor(void)
{
	int16_t storage_size = 0;
	while(1){
		lcd_clrscr();
		write_to_lcd("Floor amount: "); // waits input from the user
		
		while (1) {
			uint8_t key = KEYPAD_GetKey();
			if (key != NO_KEY_PRESSED)
			{
				if (key >= '0' && key <= '9') {
					uint8_t digit = key - '0';

					storage_size *= 10;     //incrementing to the another units
					storage_size += digit;

					char buffer[40];        
					snprintf(buffer, sizeof(buffer), "%d", storage_size);   

					printf("%s", buffer); //Debug Purposes
					lcd_gotoxy(0, 1);
					write_to_lcd(buffer);
				}
				else if (key == '#') {
					if (storage_size >= MIN_FLOOR && storage_size <= MAX_FLOOR) {
						printf("Floor Chosen\r\n");
						return storage_size;
					}    // Need Fail Safe
				}
				else if (key == '*') {
					storage_size = 0;
					char buffer[3];
					snprintf(buffer, sizeof(buffer), "%d", storage_size);
					printf("%s", buffer);
					
					lcd_clrscr();
					lcd_gotoxy(0, 0);
					write_to_lcd("Floor amount:");
					lcd_gotoxy(0, 1);
					write_to_lcd(buffer);
				}
			}
			
			
		}
	}
	
}


static int16_t floor_choice(void)
{
    int16_t destination_floor = 0;
    
    while (1) {
        lcd_clrscr();
        write_to_lcd("Choose floor:");
        
        while (1) {
            uint8_t key = KEYPAD_GetKey();
            if (key != NO_KEY_PRESSED)
            {
                if (key >= '0' && key <= '9') {
                    uint8_t digit = key - '0';

                    destination_floor *= 10;
                    destination_floor += digit;

                    char buffer[40];
                    snprintf(buffer, sizeof(buffer), "%d", destination_floor);

                    printf("%s", buffer);
                    lcd_gotoxy(0, 1);
                    write_to_lcd(buffer);
                }
                else if (key == '#') {
                    if (destination_floor >= MIN_FLOOR && destination_floor <= MAX_FLOOR) {
                        printf("Floor Chosen\r\n");
                        return destination_floor;
                    }
                }
                else if (key == '*') {
                    destination_floor = 0;
                    char buffer[3];
                    snprintf(buffer, sizeof(buffer), "%d", destination_floor);
                    printf("%s", buffer);
                    
                    lcd_clrscr();
                    lcd_gotoxy(0, 0);
                    write_to_lcd("Choose floor:");
                    lcd_gotoxy(0, 1);
                    write_to_lcd(buffer);
                }
            }
        }
    }
}
// This function is for choosing direction . 
Tstate_t choose_direction(int16_t destination_floor)
{
    if (destination_floor > CURRENT_FLOOR) { // If destination floor is higher than current floor
        return GOINGUP; // then it will return to going up 
    }
    else if (destination_floor < CURRENT_FLOOR) { // If destination floor is lower than current floor 
        return GOINGDOWN;
    }
    else if (destination_floor == CURRENT_FLOOR){ // if destination floor is the current floor
        lcd_clrscr();
        write_to_lcd("Same floor"); // in lcd shows same floor
        DELAY_ms(2000);
        return IDLE; // goes to idle
    }
    return IDLE;
}

void lcd_display_floor(int16_t floor)
{
    lcd_clrscr();
    lcd_gotoxy(0, 0);
    write_to_lcd("Current floor:");
    
    lcd_gotoxy(0, 1);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", floor);
    write_to_lcd(buf);
}
// Going up function
state_t going_up(int16_t destination_floor) 
{
    
    while (CURRENT_FLOOR < destination_floor) { // this loop will go on as long as current floor is lower than destination
        CURRENT_FLOOR++; // current floor number increase
        lcd_display_floor(CURRENT_FLOOR); // lcd displays current floor
        DELAY_ms(FLOOR_MOVING_SPEED_MS);
        //printf("After floor choice queue");            
        if (Emergency_Pressed()){ // if emergency is pressed , returns to fault
            return FAULT;
        }
    }
    
    printf("Going up is done\r\n"); // debug purpose 
    
    return DOOR_OPENING; // return to door open 
}

state_t going_down(int16_t destination_floor)
{
    while (CURRENT_FLOOR > destination_floor) {
        CURRENT_FLOOR--;
        lcd_display_floor(CURRENT_FLOOR);
        DELAY_ms(FLOOR_MOVING_SPEED_MS);
        if (Emergency_Pressed()){
            return FAULT;  
        }
    }
    
    return DOOR_OPENING;
}



int main(void)
{   
    int16_t size_floor_order=0; // initial case
	
	int16_t process_counter = 0; // determines when amount of floor is inputted
	
	int16_t count_index_floor=0; // counts until get enough floors
	
	int16_t total_storage_size=0; 
    
    // Button definitions for obstacle detection

    BUTTON_DDR &= ~(1<<BUTTON_PIN); // makes the 6th pin input which is connected to button
    BUTTON_PORT |= (1<<BUTTON_PIN); // Pull-up is active: If button is not pressed HIGH(1) but if it is pressed it is LOW (0)
    
    power_save_init(); // initialize the button foe energy saving mode

	// Button definitions for the emergency situations
    DDRH &= ~(1 << PH4); // D7 (PH4) as input
    PORTH |= (1 << PH4); // Enable pull-up

    int16_t destination; // definition of destination
    uint8_t rc = setup_uart_io();  // from exercise (check it out)
    handle_error(rc);
    state_t state = IDLE; // initial state
    
    DDRB |= (1 << PB0) | (1 << PB1) | (1 << PB2);
    SPCR |= (1 << SPE) | (1 << MSTR) | (1 << SPR0);
    
    //Initialization of the LCD
    printf("Initializing LCD driver\r\n");
    lcd_init(LCD_DISP_ON);
    lcd_clrscr();
    write_to_lcd("Ready");
    printf("LCD Ready.\r\n");
    _delay_ms(1000);
    
    KEYPAD_Init();
    char going_up_command[] = "U";
    char going_down_command[] = "D";
    char door_opening_command[] = "O";
    char obstacle_command[] = "S";
    char door_closing_command[] = "C";
    char buffer[20];
	
    state = IDLE;
    
    while (1)
    {
        
        switch (state)
        { 
            case IDLE:
			
			
            if(total_storage_size == process_counter){
				if(wait_before_sleep() == 1){
					printf("Sleep mode");
					enter_light_sleep(); // sleep mode 
				}
                storage_size = amount_floor(); // storage size floor requests
                total_storage_size+=storage_size;
                printf("Storage size is, %d", total_storage_size);   //Debug Purposes
                
                while(count_index_floor<total_storage_size){
                    queue_data[count_index_floor] = floor_choice(); // add the floor choice to array
                    printf("Added floor is, %d",queue_data[count_index_floor]);   //Debug Purposes
                    count_index_floor++;
                    
                }
                printf("Check point.");   //Debug Purposes
            }
            destination=queue_data[process_counter];
            printf("The destination is:, %d", destination); //Debug Purposes
            printf("Process counter is, %d",process_counter);   //Debug Purposes
            printf("Storage size is, %d", storage_size);    //Debug Purposes
            process_counter++; // incremented by 1 so that after all processes new floor will be considered.
            state=choose_direction(destination); 
            break;

            case GOINGUP:
            spi_master_send((uint8_t*)going_up_command, strlen(going_up_command)); // GOING UP functions
            state = going_up(destination);
            break;
            
            case GOINGDOWN:
            spi_master_send((uint8_t*)going_down_command, strlen(going_down_command)); // GOING DOWN functions
            state = going_down(destination);
            break;
  
            case DOOR_OPENING:
            spi_master_send((uint8_t*)door_opening_command, strlen(door_opening_command));
            lcd_clrscr();
            write_to_lcd("Door Opening");
			DELAY_ms(3000);
            if(!(PINB & (1<<BUTTON_PIN))){
                printf("Button is pressed.");//Debug Purposes
                spi_master_send((uint8_t*)obstacle_command, strlen(obstacle_command));
                printf("Data is sent");//Debug Purposes
                DELAY_ms(2950);
                lcd_clrscr();
                write_to_lcd("Obstacle");
                lcd_gotoxy(0,1);
                write_to_lcd("Detected");
                DELAY_ms(OBSTACLE_DETECTED_DURATION_MS);
                DELAY_ms(2950);// wait for play_melody
                
                state = DOOR_CLOSING;
                break;
                
            }
            else{
                
                state = DOOR_CLOSING;
                break;
            }
			break;
            
            
            
            case DOOR_CLOSING:
			
			printf("State: Door closing.");//Debug Purpose
            if (Emergency_Pressed()){
                state = FAULT;
                break;
            }
            spi_master_send((uint8_t*)door_closing_command, strlen(door_closing_command)); // DOOR CLOSING function
            lcd_clrscr();
            write_to_lcd("Door Closing");
            DELAY_ms(2000);
            state = IDLE;
            break;
            
            case FAULT:
            lcd_clrscr();
            lcd_gotoxy(0,0);
            write_to_lcd("Emergency!!!");
            DELAY_ms(2000);
            
            state = IDLE;
            break;

            default:
            state = IDLE;
            break;
            
        }
        
    }

    return 0;
}
