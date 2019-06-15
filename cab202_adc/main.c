#include <stdint.h>
#include <stdio.h>
#include <avr/io.h>
#include <stdlib.h>
#include <stdbool.h>
#include <avr/interrupt.h>
#include <lcd.h>
#include <string.h>
#include <util/delay.h>
#include <cpu_speed.h>
#include <graphics.h>
#include <macros.h>
#include "cab202_adc.h"
#include <usb_serial.h>
#include "lcd_model.h"
#include <math.h>


#define SHIELD_Y 39
#define POTENTIOMETER_MAX 1023
#define PLASMA_LENGTH 2
#define MAX_PLASMA 50
#define VANISH 999
#define MAX_ASTEROID 3
#define MAX_BOULDER 6
#define MAX_FRAGMENT 12
#define SHIELD_LIFE 5
#define FREQ     (8000000.0)
#define PRESCALE (1024.0)

#define BIT(x) (1 << (x))
#define OVERFLOW_TOP (1023)
#define ADC_MAX (1023)

///===============================================================
//                     Objects
///===============================================================

struct SpaceShip{
    int x, y;
};
struct Object{
    double x, y, angle;
};

// initialisition of objects
struct SpaceShip ship = {38, 46};

// object lists
struct Object plasma_list[MAX_PLASMA];
struct Object asteroid_list[MAX_ASTEROID];
struct Object boulder_list[MAX_BOULDER];
struct Object fragment_list[MAX_FRAGMENT];
///===============================================================
//                     Variables
///===============================================================

volatile uint32_t overflow_counter = 0;
double leftpotent;
double rightpotent;
// game informations to display
int score = 0;
int shield_life = SHIELD_LIFE;
// game counters
int plasma_counter = 0;
int asteroid_counter = 0;
int boulder_counter = 0;
int fragment_counter = 0;

//
double cx = 40, cy = 40;
bool speedIsSet;
double m_timer = -1;
double o_timer = -1;
bool isNegative;
double plasma_timer = 0;
double time = 0;
double speed = 1;
int leftcounter = 0, rightcounter = 0;
int cheat_x = -1, cheat_y = -1;
int ship_angle;
char list[5];
int converted_number = 0;
int char_counter = 0;
int16_t char_code = 32;
char input;
char char_buffer;
char ingame_buffer;
//test

// setup values
bool isPasued = true;
bool generated = false;
bool warned = false;
bool isSpace = false;
int LED_side;

///===============================================================
//                            SHAPES
///===============================================================

char * spaceship =
"......"
"......"
;

char * asteroid =
"  ...  "
" ..... "
"......."
"......."
" ..... "
"  ...  "
"   .   "
;

char * boulder =
"  .  "
" ... "
"....."
" ... "
"  .  "
;

char * fragment =
" . "
"..."
" . "
;

char * plasma =
".."
".."
;

char * animation =
"....."
;

ISR(TIMER0_OVF_vect) {
    if (!isPasued) {
        overflow_counter++;
    }
}
void setup_bit(){
    set_clock_speed(CPU_8MHz);
    //timer
    TCCR0A = 0;
    TCCR0B = 5;
    TIMSK0 = 1;
    sei();
    
    //Joysticks
    CLEAR_BIT(DDRD, 1);
    CLEAR_BIT(DDRB, 7);
    CLEAR_BIT(DDRB, 1);
    CLEAR_BIT(DDRD, 0);
    CLEAR_BIT(DDRB, 0);
    
    //Buttons
    CLEAR_BIT(DDRF, 6);
    CLEAR_BIT(DDRF, 5);
    
    //LED
    SET_BIT(DDRB, 2);
    SET_BIT(DDRB, 3);
    
    //Potentiometer
    adc_init();
    
    //LCD Screen
    lcd_init(LCD_DEFAULT_CONTRAST);
    
    //USB
    usb_init();
    
    
    TC4H = OVERFLOW_TOP >> 8;
    OCR4C = OVERFLOW_TOP & 0xff;
    
    // Enable PWM
    TCCR4A = BIT(COM4A1) | BIT(PWM4A);
    // Bit set for the LCD backlight
    SET_BIT(DDRC, 7);
    TCCR4B = BIT(CS42) | BIT(CS41) | BIT(CS40);
    TCCR4D = 0;
    // wait until usb is configured
    while (!usb_configured()){};
}

int pixel_collision(int x0, int y0, int w0, int h0, char pixels0[], int x1, int y1, int w1, int h1, char pixels1[]){
    for (int j=y0; j<y0+h0; j++){
        for(int i=x0; i<x0+w0; i++){
            if (i >= x1 && i < x1 + w1 && j >= y1 && j < y1 + h1 && pixels0[(i - x0) + (j - y0)*w0] != ' ' ){
                if (i >= x0 && i < x0 + w0 && j >= y0 && j < y0 + h0 && pixels1[(i - x1) + (j - y1)*w1] != ' '){
                    return 1;
                }
            }
        }
    }
    return 0;
}

void draw_pixels(int left, int top, int width, int height, char bitmap[]){
    for (int j=0; j<height; j++){
        for(int i=0; i<width; i++){
            if (bitmap[i + j * width] != ' '){
                draw_pixel(left + i, top + j, FG_COLOUR);
            }
        }
    }
}

char buffer[20];

void draw_double(uint8_t x, uint8_t y, double value, colour_t colour) {
    snprintf(buffer, sizeof(buffer), "%f", value);
    draw_string(x, y, buffer, colour);
}

void draw_int(uint8_t x, uint8_t y, int value, colour_t colour) {
    snprintf(buffer, sizeof(buffer), "%d", value);
    draw_string(x, y, buffer, colour);
}

void set_duty_cycle(int duty_cycle) {
    TC4H = duty_cycle >> 8;
    OCR4A = duty_cycle & 0xff;
}

void usb_serial_send(char * message) {
    usb_serial_write((uint8_t *) message, strlen(message));
}

bool accept_char(char c){
    return ( c == 't'|| c == 'm' || c == 'l' || c == 'g' || c == 'h'
            || c == 'j' || c== 'k' || c == 'i' || c == 'o');
}

bool ingame_char(char c){
    return (c == 'a' || c == 'd' || c == 'w' || c == 's' || c == 'r' ||
            c == 'p' || c == 'q' || c == '?');
}

void update_time(){
    if (!isPasued) {
        time = ( overflow_counter * 256.0 + TCNT0 ) * PRESCALE  / FREQ;
    }
}


void draw_shield(){
    draw_line(0, SHIELD_Y, LCD_X, SHIELD_Y, FG_COLOUR);
}

void update_spaceship(){
    if (ship_angle != 1 && ship_angle != 0 && ship_angle != 2) {
        ship_angle = rand()%2+0;
    }
    // weather the joystick left or right is triggered
    if (!isPasued) {
        if (ship_angle == 1 && ship.x > 0 && isPasued == 0 && !(ship.x < 1 || cx < 1)) {
            ship.x--;
        }
        else if (ship_angle == 0 && ship.x < LCD_X - 6 && isPasued == 0 && !(ship.x > LCD_X - 7 || cx > LCD_X - 2)) {
            ship.x++;
        }
    }
    if (((BIT_IS_SET(PINB, 1) || ingame_buffer == 'a') && ship_angle == 0) || ((BIT_IS_SET(PIND, 0) || ingame_buffer == 'd') && ship_angle == 1)) {
        ship_angle = 2;
        char_buffer = 32;
    }
    else if (BIT_IS_SET(PINB, 1) || ingame_buffer == 'a') {
        ship_angle = 1;
        char_buffer = 32;
    }else if (BIT_IS_SET(PIND, 0) || ingame_buffer == 'd'){
        ship_angle = 0;
        char_buffer = 32;
    }
}

void draw_plasma(double x, double y){
    draw_pixels(x, y, 2, 2, plasma);
}

void draw_asteroid(double x, double y){
    draw_pixels(x, y, 7, 7, asteroid);
}

void draw_boulder(double x, double y){
    draw_pixels(x, y, 5, 5, boulder);
}

void draw_fragment(double x, double y){
    draw_pixels(x, y, 3, 3, fragment);
}

bool asteroid_collision_asteroid(double x, double y, double x1, double y1){
    return pixel_collision(x, y, 7, 7, asteroid, x1, y1, 7, 7, asteroid);
}

void spawn_asteroid(){
    for (int a = 0; a < 3; a++) {
        int x = rand()%77+0;
        for (int b = 0; b < 3; b++) {
            if (asteroid_collision_asteroid(x, -8, asteroid_list[b].x, -8)) {
                a = 0;
                leftcounter = 0;
                rightcounter = 0;
                break;
            }
        }
        asteroid_list[a].y = -8;
        asteroid_list[a].x = x;
        if (x + 6 < LCD_X / 2 && x > 0) {
            leftcounter++;
        }else if (x > LCD_X / 2 && x + 6 < LCD_X){
            rightcounter++;
        }
    }
    asteroid_counter = 3;
    if (leftcounter > rightcounter) {
        LED_side = 2;
    }else if(leftcounter <= rightcounter){
        LED_side = 3;
    }
}

void update_asteroid(){
    if (time >= 2) {
        for (int a = 0; a < asteroid_counter; a++) {
            if (!isPasued) {
                asteroid_list[a].y += speed;
            }
            draw_asteroid(asteroid_list[a].x, asteroid_list[a].y);
            // if the asteroid touchs the shield, make it vanish
            if (asteroid_list[a].y + 7 >= SHIELD_Y && asteroid_list[a].x >= 0 && asteroid_list[a].x <= LCD_X - 7) {
                asteroid_list[a].y = VANISH;
                asteroid_counter--;
                shield_life--;
            }
        }
    }
}
void set_cannon_angle(){
    // convert the range of angle to (-60 to 60);
    if (o_timer == -1 || time - o_timer > 1) {
        leftpotent = round(adc_read(0) / 8.5) - 60;
        o_timer = -1;
    }
}

// fire plasma
void fire_cannon(){
    // if Joystick up
    if ((BIT_IS_SET(PIND, 1)|| ingame_buffer == 'w') && plasma_counter < MAX_PLASMA && time - plasma_timer >= 0.2 && !isPasued) {
        plasma_counter++;
        plasma_list[plasma_counter - 1].x = cx + (PLASMA_LENGTH * sin(leftpotent * M_PI / 180));
        plasma_list[plasma_counter - 1].y = cy - (PLASMA_LENGTH * cos(leftpotent * M_PI / 180));
        plasma_list[plasma_counter - 1].angle = leftpotent;
        plasma_timer = time;
        ingame_buffer = 32;
    }
}

void update_plasmas(){
    for (int a = 0; a < plasma_counter; a++) {
        draw_plasma(plasma_list[a].x, plasma_list[a].y);
        if (!isPasued) {
            plasma_list[a].x += speed * (PLASMA_LENGTH * sin(plasma_list[a].angle * M_PI / 180));
            plasma_list[a].y -= speed * (PLASMA_LENGTH * cos(plasma_list[a].angle * M_PI / 180));
        }
    }
}

void release_plasma_list(){
    int counter = 0;
    for (int a = 0; a < plasma_counter; a++) {
        // if the plasma goes outside the boarder
        if (plasma_list[a].x > LCD_X || plasma_list[a].x < 0 || plasma_list[a].y < 0) {
            for (int b = a ;b < plasma_counter; b++) {
                plasma_list[b] = plasma_list[b + 1];
            }
            counter ++;
        }
    }
    plasma_counter -= counter;
}

void draw_cannon(){
    double x2 = ship.x + 2;
    double y2 = ship.y;
    int space = ship.y - SHIELD_Y -1;
    cx = ship.x + 2 + (space * sin(leftpotent * M_PI / 180));
    cy = ship.y - (space * cos(leftpotent * M_PI / 180));
    if (cx < 0) {cx = 0; cy = 39;}
    else if (cx > LCD_X){cx = LCD_X - 1; cy = 39;}
    // draw the connon
    draw_line(cx, cy, x2, y2, FG_COLOUR);
    draw_line(cx + 1, cy, x2 + 1, y2, FG_COLOUR);
}

void draw_spaceship(){
    draw_pixels(ship.x, ship.y, 6, 2, spaceship);
    draw_cannon();
}

// asteroid coordinates go first
bool asteroid_hit_by_plasma(double x, double y, double x1, double y1){
    return pixel_collision(x, y, 7, 7, asteroid, x1, y1, 2, 2, plasma);
}

bool boulder_hit_by_plasma(double x, double y, double x1, double y1){
    return pixel_collision(x, y, 5, 5, boulder, x1, y1, 2, 2, plasma);
}

bool fragment_hit_by_plasma(double x, double y, double x1, double y1){
    return pixel_collision(x, y, 3, 3, fragment, x1, y1, 2, 2, plasma);
}

void spawn_boulders(double x, double y){
    while (x < 0) {
        x++;
    }
    while (x + 5 > LCD_X) {
        x--;
    }
    boulder_list[boulder_counter].x = x;
    boulder_list[boulder_counter].y = y;
    boulder_list[boulder_counter].angle = rand()%61+(-30);
    
    boulder_list[boulder_counter + 1].x = x + 2;
    boulder_list[boulder_counter + 1].y = y;
    boulder_list[boulder_counter + 1].angle = rand()%61+(-30);
    
    boulder_counter += 2;
}

void update_boulders(){
    for (int a = 0; a < boulder_counter; a++) {
        draw_boulder(boulder_list[a].x, boulder_list[a].y);
        if (!isPasued) {
            boulder_list[a].x += speed * sin(boulder_list[a].angle * M_PI / 180);
            boulder_list[a].y += speed * cos(boulder_list[a].angle * M_PI / 180);
        }
        // if the boulder hits the boarder, make it bounce
        if (boulder_list[a].x < 1 || boulder_list[a].x > LCD_X - 5) {
            boulder_list[a].angle = - boulder_list[a].angle;
        }
        // if the boulder hits the shield, make it vanish
        if (boulder_list[a].y > SHIELD_Y - 4 && boulder_list[a].x >= 0 && boulder_list[a].x < LCD_X - 5) {
            boulder_list[a].x = VANISH;
            shield_life--;
        }
    }
}

void update_fragments(){
    for (int a = 0; a < fragment_counter; a++) {
        draw_fragment(fragment_list[a].x, fragment_list[a].y);
        if (!isPasued) {
            fragment_list[a].x += speed * sin(fragment_list[a].angle * M_PI / 180);
            fragment_list[a].y += speed * cos(fragment_list[a].angle * M_PI / 180);
        }
        // if the fragment hits the boarder, make it bounce
        if (fragment_list[a].x < 1 || fragment_list[a].x > LCD_X - 3) {
            fragment_list[a].angle = -fragment_list[a].angle;
        }
        // if the fragment hits the shield, make it vanish
        if (fragment_list[a].y > SHIELD_Y - 2 && fragment_list[a].x >= 0 && fragment_list[a].x <= LCD_X - 3) {
            fragment_list[a].x = VANISH;
            shield_life--;
        }
    }
}

void release_asteroid_list(){
    int counter = 0;
    for (int a = 0; a < asteroid_counter; a++) {
        if (asteroid_list[a].x > LCD_X) {
            for (int b = a; b < asteroid_counter; b++) {
                asteroid_list[b] = asteroid_list[b + 1];
            }
            counter++;
        }
    }
    asteroid_counter -= counter;
}

// if the plasma bolt hits the asteroid
void asteroid_detection(){
    for (int a = 0; a < plasma_counter; a++) {
        for (int b = 0; b < asteroid_counter; b++) {
            //if any asteroid is hitted
            if (asteroid_hit_by_plasma(asteroid_list[b].x, asteroid_list[b].y, plasma_list[a].x, plasma_list[a].y)) {
                score++;
                spawn_boulders(asteroid_list[b].x + 1, asteroid_list[b].y);
                asteroid_list[b].x = VANISH;
                plasma_list[a].x =  -999;
            }
        }
    }
}

void spawn_fragment(double x, double y, double angle){
    while (x < 3) {
        x++;
    }
    while (x + 10 > LCD_X) {
        x--;
    }
    fragment_list[fragment_counter].x = x - 3;
    fragment_list[fragment_counter].y = y;
    fragment_list[fragment_counter].angle = angle + rand()%61+(-30);
    
    fragment_list[fragment_counter + 1].x = x + 5;
    fragment_list[fragment_counter + 1].y = y;
    fragment_list[fragment_counter + 1].angle = angle + rand()%61+(-30);
    
    fragment_counter += 2;
}
void fragment_detection(){
    for (int a = 0; a < plasma_counter; a++) {
        for (int b = 0; b < fragment_counter; b++) {
            if (fragment_hit_by_plasma(fragment_list[b].x, fragment_list[b].y, plasma_list[a].x, plasma_list[a].y)) {
                score+=4;
                fragment_list[b].x = VANISH;
                plasma_list[a].x =  -999;
            }
        }
    }
}

void boulder_detection(){
    for (int a = 0; a < plasma_counter; a++) {
        for (int b = 0; b < boulder_counter; b++) {
            // if any boulder is hitted by plasma
            if (boulder_hit_by_plasma(boulder_list[b].x, boulder_list[b].y, plasma_list[a].x, plasma_list[a].y)) {
                score+=2;
                spawn_fragment(boulder_list[b].x, boulder_list[b].y, boulder_list[b].angle);
                boulder_list[b].x = VANISH;
                plasma_list[a].x = -999;
            }
        }
    }
}

void release_boulder_list(){
    int counter = 0;
    for (int a = 0; a < boulder_counter; a++) {
        if (boulder_list[a].x > LCD_X) {
            for (int b = a; b < boulder_counter; b++) {
                boulder_list[b] = boulder_list[b + 1];
            }
            counter++;
        }
    }
    boulder_counter -= counter;
}

void release_fragment_list(){
    int counter = 0;
    for (int a = 0; a < fragment_counter; a++) {
        if (fragment_list[a].x > LCD_X) {
            for (int b = a; b < fragment_counter; b++) {
                fragment_list[b] = fragment_list[b + 1];
            }
            counter++;
        }
    }
    fragment_counter -= counter;
}

void respawn_asteroid(){
    if (asteroid_counter == 0 && boulder_counter == 0 && fragment_counter == 0) {
        spawn_asteroid();
    }
}

void draw_boarder(){
    draw_line(0, 0, LCD_X, 0, FG_COLOUR);
    draw_line(0, 0, 0, LCD_Y, FG_COLOUR);
    draw_line(LCD_X - 1, 0, LCD_X - 1, LCD_Y, FG_COLOUR);
    draw_line(0, LCD_Y - 1, LCD_X, LCD_Y - 1, FG_COLOUR);
}

void introduction_information(){
    draw_string(20, 3, "n10088652", FG_COLOUR);
    draw_string(22, 20, "Asteroid", FG_COLOUR);
    draw_string(18, 27, "Apocalypse", FG_COLOUR);
}

void display_introduction(){
    int brightness = 1023;
    struct Animation{
        int x, y, angle;
    };
    struct Animation a = {-1, 12, 1};
    struct Animation b = {LCD_X, 12, -1};
    while (!BIT_IS_SET(PINF, 6)) {
        if (brightness > 15) {
            brightness -= 15;
        }
        set_duty_cycle(brightness);
        clear_screen();
        introduction_information();
        if (a.angle == 1) {
            a.x++;
        }else if(a.angle == -1){
            a.x--;
        }
        if (b.angle == 1) {
            b.x++;
        }else if(b.angle == -1){
            b.x--;
        }
        if (a.x > LCD_X || a.x < 0) {
            a.angle = -a.angle;
        }
        if (b.x > LCD_X || b.x < 0) {
            b.angle = -b.angle;
        }
        draw_pixels(a.x, a.y, 5, 1, animation);
        draw_pixels(b.x, b.y, 5, 1, animation);
        draw_boarder();
        show_screen();
    }
    usb_serial_send("Game Started\r\n");
}
void release_all_list(){
    release_plasma_list();
    release_asteroid_list();
    release_boulder_list();
    release_fragment_list();
}

void collision_detection(){
    asteroid_detection();
    boulder_detection();
    fragment_detection();
}

void set_pause(){
    if (BIT_IS_SET(PINB, 0) || ingame_buffer == 'p') {
        isPasued = !isPasued;
        ingame_buffer = 32;
    }
}

void display_time(){
    int min = floor(time / 60);
    int sec = floor(time - min * 60);
    if (sec < 10) {
        draw_int(46, 7, 0, FG_COLOUR);
        draw_int(52, 7, sec, FG_COLOUR);
    }else{
        draw_int(46, 7, sec, FG_COLOUR);
    }
    if (min < 10) {
        draw_int(35, 7, min, FG_COLOUR);
        draw_int(29, 7, 0, FG_COLOUR);
    }else{
        draw_int(29, 7, min, FG_COLOUR);
    }
    draw_char(41, 7, ':', FG_COLOUR);
}

void display_statues_teensy(){
    clear_screen();
    draw_string(3, 7, "Time: ", FG_COLOUR);
    draw_string(5, 17, "Life: ", FG_COLOUR);
    draw_int(32, 17, shield_life, FG_COLOUR);
    draw_string(5, 27, "Score: ", FG_COLOUR);
    draw_int(40, 27, score, FG_COLOUR);
    display_time();
    show_screen();
}

void send_to(char * message, int number){
    char snum[5];
    usb_serial_send(message);
    itoa(number, snum, 10);
    usb_serial_send(snum);
    usb_serial_send("\r\n");
}

void send_num_to(int number){
    char snum[5];
    itoa(number, snum, 10);
    usb_serial_send(snum);
}

void send_time(){
    int min = floor(time / 60);
    int sec = floor(time - min * 60);
    usb_serial_send("Game Time: ");
    if (min < 10) {
        send_num_to(0);
        send_num_to(min);
    }else{
        send_num_to(min);
    }
    usb_serial_send(":");
    if (sec < 10) {
        send_num_to(0);
        send_num_to(sec);
    }else{
        send_num_to(sec);
    }
    usb_serial_send("\r\n");
}

void display_statues_computer(){
    send_time();
    send_to("Lives: ", shield_life);
    send_to("Score: ", score);
    send_to("Asteroids: ", asteroid_counter);
    send_to("Boulders: ", boulder_counter);
    send_to("Fragments: ", fragment_counter);
    send_to("Plasma: ", plasma_counter);
    send_to("Turrent: ", leftpotent);
    send_to("Speed: ", speed * 10);
    usb_serial_send(" \r\n");
}

// unfinished
void display_game_statues(){
    // if joystick down
    if (BIT_IS_SET(PINB, 7) || ingame_buffer == 's') {
        display_statues_computer();
        if (isPasued) {
            // joystick centre to escape
            while (isPasued) {
                display_statues_teensy();
                if (BIT_IS_SET(PINB, 0)) {
                    break;
                }
            }
        }
    }
}

bool isNumber(char c)
{
    return (c>='0'&&c<='9');
}

void led_flash(int side){
    if (LED_side != 0) {
        SET_BIT(PORTB, side);
        _delay_ms(150);
        CLEAR_BIT(PORTB, side);
        _delay_ms(150);
        SET_BIT(PORTB, side);
        _delay_ms(150);
        CLEAR_BIT(PORTB, side);
        }
    LED_side = 0;
}
void led_warning(){
    led_flash(LED_side);
}
void clean_char_list(){
    for (int a = 0; a < 5; a++) {
        list[a] = 0;
    }
    char_counter = 0;
}

void restart_game(){
    if(BIT_IS_SET(PINF, 6) || ingame_buffer == 'r'){
        char_counter = 0;
        shield_life = 5;
        score = 0;
        ship.x = 38;
        asteroid_counter = 0;
        boulder_counter = 0;
        fragment_counter = 0;
        plasma_counter = 0;
        overflow_counter = 0;
        plasma_timer = 0;
        input = 0;
        converted_number = 0;
        speed = 1;
        isPasued = true;
        generated = false;
        warned = false;
        ingame_buffer = 32;
        release_all_list();
    }
}

void quit_game(){
    LCD_CMD(lcd_set_display_mode, lcd_display_inverse);
    while (1) {
        draw_string(19, 19, "n10088652", FG_COLOUR);
        show_screen();
        clear_screen();
    }
}

void game_over(){
    if (shield_life <= 0) {
        int temp_counter = 0;
        display_statues_computer();
        usb_serial_send("Game Over\r\n");
        while (temp_counter <= 1023){
            draw_string(15, 19, "Game Over", FG_COLOUR);
            show_screen();
            set_duty_cycle(temp_counter);
            temp_counter += 15;
            clear_screen();
        }
        double temp_timer = time;
        while (time - temp_timer < 4) {
            update_time();
            SET_BIT(PORTB, 2);
            SET_BIT(PORTB, 3);
        }
        CLEAR_BIT(PORTB, 2);
        CLEAR_BIT(PORTB, 3);
        while (1) {
            set_duty_cycle(temp_counter);
            if (temp_counter >= 15) {
                temp_counter -= 15;
            }
            draw_string(5, 13, "LB: Restart", FG_COLOUR);
            draw_string(5, 28, "RB: Quit", FG_COLOUR);
            show_screen();
            clear_screen();
            if (BIT_IS_SET(PINF, 6)) {
                restart_game();
                break;
            }
            else if (BIT_IS_SET(PINF, 5)){
                quit_game();
                break;
            }
        }
    }
}

void reset_char(){
    clean_char_list();
    char_code = 32;
}

void l_isPressed(){
    int buffer = atoi(list);
    if (buffer > 9999 || buffer < 0) {
        buffer = 9999;
    }
    shield_life = buffer;
    reset_char();
}

void g_isPressed(){
    int buffer = atoi(list);
    if (buffer > 9999 || buffer < 0) {
        buffer = 9999;
    }
    score = buffer;
    reset_char();
}

void j_isPressed(){
    if (cheat_x < 0) {
        cheat_x = atoi(list);
        clean_char_list();
    }else if (cheat_y < 0){
        cheat_y = atoi(list);
        if (asteroid_counter < 3) {
            if(cheat_x >=0 && cheat_x < LCD_X - 6){
                asteroid_list[asteroid_counter].x = cheat_x;
            }
            if (cheat_y + 7 < SHIELD_Y) {
                asteroid_list[asteroid_counter].y = cheat_y;
            }
            asteroid_counter++;
            clean_char_list();
            reset_char();
            cheat_x = -1;
            cheat_y = -1;
        }else{
            cheat_x = -1;
            cheat_y = -1;
            clean_char_list();
            reset_char();
        }
    }
}

void k_isPressed(){
    if (cheat_x < 0) {
        cheat_x = atoi(list);
        clean_char_list();
    }else if (cheat_y < 0){
        cheat_y = atoi(list);
        if (boulder_counter < 6) {
            if (cheat_x >= 0 && cheat_x < LCD_X - 4) {
                boulder_list[boulder_counter].x = cheat_x;
            }
            if (cheat_y + 5 < SHIELD_Y) {
                boulder_list[boulder_counter].y = cheat_y;
            }
            boulder_counter++;
            clean_char_list();
            reset_char();
            cheat_x = -1;
            cheat_y = -1;
        }else{
            cheat_x = -1;
            cheat_y = -1;
            clean_char_list();
            reset_char();
        }
    }
}

void i_isPressed(){
    if (cheat_x < 0) {
        cheat_x = atoi(list);
        clean_char_list();
    }else if (cheat_y < 0){
        cheat_y = atoi(list);
        if (fragment_counter < 12) {
            fragment_list[fragment_counter].x = cheat_x;
            fragment_list[fragment_counter].y = cheat_y;
            fragment_counter++;
            reset_char();
            cheat_x = -1;
            cheat_y = -1;
        }
        else{
            cheat_x = -1;
            cheat_y = -1;
            clean_char_list();
            reset_char();
        }
    }
}

void game_quit(){
    if (ingame_buffer == 'q' || BIT_IS_SET(PINF, 5)){
        quit_game();
    }
}


void send_controls(){
    if (ingame_buffer == '?') {
        usb_serial_send("'a' – move spaceship left\r\n"
                        "'d' – move spaceship right\r\n"
                        "'w' – fire plasma bolts\r\n"
                        "'s' – send and display game status\r\n"
                        "'r' – start/reset game\r\n"
                        "'p' – pause game\r\n"
                        "'q' – quit\r\n"
                        "'t' – set aim of the turret\r\n"
                        "'m' – set the speed of the game\r\n"
                        "'l' – set the remaining useful life of the deflector shield\r\n"
                        "'g' – set the score\r\n"
                        "'?' – print controls to computer screen (Putty)\r\n"
                        "'h' – move spaceship to coordinate\r\n"
                        "'j' – place asteroid at coordinate\r\n"
                        "'k' – place boulder at coordinate\r\n"
                        "'i' – place fragment at coordinate\r\n"
                        " \r\n")
        ;
    }
    ingame_buffer = 32;
}

void h_isPressed(){
    int buffer = atoi(list);
    if (buffer > LCD_X - 6) {
        buffer = LCD_X - 6;
    }else if (buffer < 0){
        buffer = 0;
    }
    ship.x = buffer;
    reset_char();
}

void m_isPressed(){
    m_timer = time;
    double buffer = atoi(list);
    if (buffer > 1023) {
        buffer = 1023;
    }else if (buffer < 0){
        buffer = 0;
    }
    speed = buffer / 1023;
    reset_char();
}

void o_isPressed(){
    o_timer = time;
    double buffer = atoi(list);
    if (buffer > 60) {
        buffer = 60;
    }
    if (isNegative) {
        leftpotent = -buffer;
    }else{
        leftpotent = buffer;
    }
    isNegative = false;
    reset_char();
}

void game_cheat_with_number(){
    switch (char_code) {
        case 'm':
            m_isPressed();
            break;
        case 'o':
            o_isPressed();
            break;
        case 'l':
            l_isPressed();
            break;
        case 'g':
            g_isPressed();
            break;
        case 'h':
            h_isPressed();
            break;
        case 'j':
            j_isPressed();
            break;
        case 'k':
            k_isPressed();
            break;
        case 'i':
            i_isPressed();
            break;
    }
}


void get_command(){
    char_buffer = usb_serial_getchar();
    if (isNumber(char_buffer) && char_code != 32 && char_code != 0 && accept_char(char_code)) {
        list[char_counter] = char_buffer;
        char_counter++;
    }else if((accept_char(char_buffer) && (char_code == 32 || char_code == 0))){
        char_code = char_buffer;
    }else if(ingame_char(char_buffer)){
        ingame_buffer = char_buffer;
    }
    if (char_buffer == '-' && char_code == 'o') {
        isNegative = true;
    }
    if (char_buffer == 0x0D){
        game_cheat_with_number();
    }
    
}

void setSpeed(){
    if (m_timer == -1 || time - m_timer > 1) {
        double a = adc_read(1);
        speed = a / 1023;
        m_timer = -1;
    }
}


void do_all(){
    set_pause();
    led_warning();
    respawn_asteroid();
    game_quit();
    display_game_statues();
    set_cannon_angle();
    update_asteroid();
    fire_cannon();
    update_spaceship();
    update_plasmas();
    release_plasma_list();
    update_boulders();
    release_boulder_list();
    update_fragments();
    release_fragment_list();
    release_asteroid_list();
    draw_shield();
    draw_spaceship();
    game_over();
    send_controls();
    get_command();
    setSpeed();
}

void setup_canvas(){
    spawn_asteroid();
    set_duty_cycle(0);
}

int main(int argc, const char * argv[]) {
    setup_bit();
    display_introduction();
    setup_canvas();
    for ( ;; ) {
        update_time();
        clear_screen();
        collision_detection();
        do_all();
        restart_game();
        show_screen();
        _delay_ms(50);
    }
    return 0;
}


