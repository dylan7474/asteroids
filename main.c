/*
 * main.c - A classic Asteroids style game using SDL2
 *
 * This application is designed to be cross-compiled on a Linux system
 * to generate a standalone executable for Windows.
 * It uses custom-drawn vector graphics and procedurally generated sounds.
 *
 * Controls: 
 * Left/Right Arrow Keys: Rotate Ship
 * Up Arrow Key: Apply Thrust
 * Spacebar: Shoot
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

// --- Game Constants ---
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define SHIP_SIZE 20.0f
#define SHIP_ACCELERATION 0.1f
#define SHIP_TURN_SPEED 5.0f
#define SHIP_FRICTION 0.995f
#define BULLET_SPEED 7.0f
#define MAX_BULLETS 10
#define MAX_ASTEROIDS 50
#define SAMPLE_RATE 44100
#define RESPAWN_INVINCIBILITY 180 // 3 seconds at 60fps
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Structs ---
typedef struct {
    float x, y;
    float vx, vy;
    float angle;
    int alive;
    int invincible_timer;
} Ship;

typedef struct {
    float x, y;
    float vx, vy;
    float angle;
    float rotation_speed;
    int size; // 3=large, 2=medium, 1=small
    int num_vertices;
    float vertices[32]; // Increased size to prevent buffer overflow (was 16)
    int alive;
} Asteroid;

typedef struct {
    float x, y;
    float vx, vy;
    int lifetime;
    int active;
} Bullet;

// --- Global Variables ---
SDL_Window* g_window = NULL;
SDL_Renderer* g_renderer = NULL;
Mix_Chunk* g_shoot_sound = NULL;
Mix_Chunk* g_thrust_sound = NULL;
Mix_Chunk* g_bang_large_sound = NULL;
Mix_Chunk* g_bang_medium_sound = NULL;
Mix_Chunk* g_bang_small_sound = NULL;

Ship g_ship;
Bullet g_bullets[MAX_BULLETS];
Asteroid g_asteroids[MAX_ASTEROIDS];

int g_score = 0;
int g_lives = 3;
int g_level = 1;
int g_game_over = 0;

// --- Function Prototypes ---
int initialize();
void create_sounds();
void setup_level();
void spawn_asteroid(float x, float y, int size);
void handle_input(int* is_running);
void update_game();
void wrap_coordinates(float* x, float* y);
void render_game();
void cleanup();
void draw_digit(int digit, int x, int y);
void draw_number(int number, int x, int y);

// --- Main ---
int main(int argc, char* argv[]) {
    if (!initialize()) {
        cleanup();
        return 1;
    }
    g_score = 0;
    g_lives = 3;
    g_level = 1;
    g_game_over = 0;

    setup_level();
    int is_running = 1;
    while (is_running && !g_game_over) {
        handle_input(&is_running);
        update_game();
        render_game();
        SDL_Delay(16);
    }
    cleanup();
    return 0;
}

// --- Implementations ---
int initialize() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) return 0;
    if (Mix_OpenAudio(SAMPLE_RATE, MIX_DEFAULT_FORMAT, 2, 2048) < 0) return 0;
    g_window = SDL_CreateWindow("SDL Asteroids", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!g_window) return 0;
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) return 0;
    srand(time(0));
    create_sounds();
    return 1;
}

void create_sounds() {
    {
        static Sint16 data[SAMPLE_RATE/20]; int len = sizeof(data);
        for(int i=0; i<len/sizeof(Sint16); ++i) { data[i] = (Sint16)(3000 * ((rand() % 256) / 255.0 - 0.5)); }
        g_shoot_sound = Mix_QuickLoad_RAW((Uint8*)data, len);
    }
    {
        static Sint16 data[SAMPLE_RATE/10]; int len = sizeof(data);
        for(int i=0; i<len/sizeof(Sint16); ++i) { data[i] = (Sint16)(1500 * sin(2.0*M_PI*110.0*((double)i/SAMPLE_RATE)) + 1000 * ((rand() % 256) / 255.0 - 0.5)); }
        g_thrust_sound = Mix_QuickLoad_RAW((Uint8*)data, len);
    }
    {
        static Sint16 data[SAMPLE_RATE/4]; int len = sizeof(data);
        for(int i=0; i<len/sizeof(Sint16); ++i) { double t=(double)i/SAMPLE_RATE; double f=110.0-(t*200.0); data[i] = (Sint16)(8000 * sin(2.0*M_PI*f*t)*(1.0-(t*4))); }
        g_bang_large_sound = Mix_QuickLoad_RAW((Uint8*)data, len);
    }
    {
        static Sint16 data[SAMPLE_RATE/6]; int len = sizeof(data);
        for(int i=0; i<len/sizeof(Sint16); ++i) { double t=(double)i/SAMPLE_RATE; double f=220.0-(t*400.0); data[i] = (Sint16)(6000 * sin(2.0*M_PI*f*t)*(1.0-(t*6))); }
        g_bang_medium_sound = Mix_QuickLoad_RAW((Uint8*)data, len);
    }
    {
        static Sint16 data[SAMPLE_RATE/10]; int len = sizeof(data);
        for(int i=0; i<len/sizeof(Sint16); ++i) { double t=(double)i/SAMPLE_RATE; double f=440.0-(t*800.0); data[i] = (Sint16)(4000 * sin(2.0*M_PI*f*t)*(1.0-(t*10))); }
        g_bang_small_sound = Mix_QuickLoad_RAW((Uint8*)data, len);
    }
}

void setup_level() {
    g_ship = (Ship){SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f, 0.0f, 0.0f, -90.0f, 1, RESPAWN_INVINCIBILITY};
    for (int i = 0; i < MAX_BULLETS; i++) g_bullets[i].active = 0;
    for (int i = 0; i < MAX_ASTEROIDS; i++) g_asteroids[i].alive = 0;
    
    for (int i = 0; i < g_level + 3; i++) {
        float x, y;
        do {
            if (rand() % 2 == 0) {
                x = (rand() % 2 == 0) ? 0.0f - SHIP_SIZE*4 : SCREEN_WIDTH + SHIP_SIZE*4;
                y = rand() % SCREEN_HEIGHT;
            } else {
                x = rand() % SCREEN_WIDTH;
                y = (rand() % 2 == 0) ? 0.0f - SHIP_SIZE*4 : SCREEN_HEIGHT + SHIP_SIZE*4;
            }
        } while (hypotf(x - g_ship.x, y - g_ship.y) < 200.0f); // Don't spawn on top of player
        spawn_asteroid(x, y, 3);
    }
}

void spawn_asteroid(float x, float y, int size) {
    // --- BUG FIX ---
    // Added a safety check to prevent creating an asteroid of size 0 or less.
    if (size < 1) return;

    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (!g_asteroids[i].alive) {
            g_asteroids[i].alive = 1;
            g_asteroids[i].x = x;
            g_asteroids[i].y = y;
            g_asteroids[i].vx = ((rand() % 200) - 100) / 100.0f;
            g_asteroids[i].vy = ((rand() % 200) - 100) / 100.0f;
            g_asteroids[i].angle = 0.0f;
            g_asteroids[i].rotation_speed = ((rand() % 100) - 50) / 50.0f;
            g_asteroids[i].size = size;
            g_asteroids[i].num_vertices = 8 + rand() % 9; // Max 16 vertices
            
            for (int j = 0; j < g_asteroids[i].num_vertices; j++) {
                float angle = (float)j / g_asteroids[i].num_vertices * 2.0f * M_PI;
                float radius_variance = (float)(size * 8 + (rand() % (size * 4)));
                g_asteroids[i].vertices[j*2] = radius_variance * cosf(angle);
                g_asteroids[i].vertices[j*2+1] = radius_variance * sinf(angle);
            }
            return;
        }
    }
}

void fire_bullet() {
    if (!g_ship.alive) return;
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].active) {
            g_bullets[i].active = 1;
            g_bullets[i].x = g_ship.x + SHIP_SIZE / 2.0f * cosf(g_ship.angle * M_PI / 180.0f);
            g_bullets[i].y = g_ship.y + SHIP_SIZE / 2.0f * sinf(g_ship.angle * M_PI / 180.0f);
            g_bullets[i].vx = g_ship.vx + BULLET_SPEED * cosf(g_ship.angle * M_PI / 180.0f);
            g_bullets[i].vy = g_ship.vy + BULLET_SPEED * sinf(g_ship.angle * M_PI / 180.0f);
            g_bullets[i].lifetime = 60;
            if(g_shoot_sound) Mix_PlayChannel(-1, g_shoot_sound, 0);
            return;
        }
    }
}

void handle_input(int* is_running) {
    SDL_Event e;
    while (SDL_PollEvent(&e) != 0) {
        if (e.type == SDL_QUIT) *is_running = 0;
    }
    if (!g_ship.alive) return;

    const Uint8* state = SDL_GetKeyboardState(NULL);
    if (state[SDL_SCANCODE_LEFT]) g_ship.angle -= SHIP_TURN_SPEED;
    if (state[SDL_SCANCODE_RIGHT]) g_ship.angle += SHIP_TURN_SPEED;
    if (state[SDL_SCANCODE_UP]) {
        g_ship.vx += SHIP_ACCELERATION * cosf(g_ship.angle * M_PI / 180.0f);
        g_ship.vy += SHIP_ACCELERATION * sinf(g_ship.angle * M_PI / 180.0f);
        if (g_thrust_sound && !Mix_Playing(1)) {
            Mix_PlayChannel(1, g_thrust_sound, -1);
        }
    } else {
        Mix_HaltChannel(1);
    }
    if (state[SDL_SCANCODE_SPACE]) {
        static Uint32 last_shot = 0;
        if (SDL_GetTicks() - last_shot > 200) {
            fire_bullet();
            last_shot = SDL_GetTicks();
        }
    }
}

void wrap_coordinates(float* x, float* y) {
    if (*x < 0) *x += SCREEN_WIDTH;
    if (*x > SCREEN_WIDTH) *x -= SCREEN_WIDTH;
    if (*y < 0) *y += SCREEN_HEIGHT;
    if (*y > SCREEN_HEIGHT) *y -= SCREEN_HEIGHT;
}

void update_game() {
    // Update ship
    if (g_ship.alive) {
        g_ship.x += g_ship.vx;
        g_ship.y += g_ship.vy;
        g_ship.vx *= SHIP_FRICTION;
        g_ship.vy *= SHIP_FRICTION;
        wrap_coordinates(&g_ship.x, &g_ship.y);
        if (g_ship.invincible_timer > 0) {
            g_ship.invincible_timer--;
        }
    }

    // Update bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g_bullets[i].active) {
            g_bullets[i].x += g_bullets[i].vx;
            g_bullets[i].y += g_bullets[i].vy;
            wrap_coordinates(&g_bullets[i].x, &g_bullets[i].y);
            if (--g_bullets[i].lifetime <= 0) g_bullets[i].active = 0;
        }
    }

    // Update asteroids and check collisions
    int asteroids_count = 0;
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (g_asteroids[i].alive) {
            asteroids_count++;
            g_asteroids[i].x += g_asteroids[i].vx;
            g_asteroids[i].y += g_asteroids[i].vy;
            g_asteroids[i].angle += g_asteroids[i].rotation_speed;
            wrap_coordinates(&g_asteroids[i].x, &g_asteroids[i].y);
            
            // Bullet-Asteroid collision
            for (int j = 0; j < MAX_BULLETS; j++) {
                if (g_bullets[j].active) {
                    float dist = hypotf(g_bullets[j].x - g_asteroids[i].x, g_bullets[j].y - g_asteroids[i].y);
                    if (dist < g_asteroids[i].size * 10) {
                        g_asteroids[i].alive = 0;
                        g_bullets[j].active = 0;
                        if (g_asteroids[i].size > 1) {
                            spawn_asteroid(g_asteroids[i].x, g_asteroids[i].y, g_asteroids[i].size - 1);
                            spawn_asteroid(g_asteroids[i].x, g_asteroids[i].y, g_asteroids[i].size - 1);
                        }
                        if (g_asteroids[i].size == 3 && g_bang_large_sound) Mix_PlayChannel(-1, g_bang_large_sound, 0);
                        else if (g_asteroids[i].size == 2 && g_bang_medium_sound) Mix_PlayChannel(-1, g_bang_medium_sound, 0);
                        else if(g_bang_small_sound) Mix_PlayChannel(-1, g_bang_small_sound, 0);

                        g_score += (4 - g_asteroids[i].size) * 20;
                        break;
                    }
                }
            }
            // Player-Asteroid collision (only if not invincible)
            float dist = hypotf(g_ship.x - g_asteroids[i].x, g_ship.y - g_asteroids[i].y);
            if (g_ship.alive && g_ship.invincible_timer <= 0 && dist < g_asteroids[i].size * 8 + SHIP_SIZE / 2.0f) {
                g_ship.alive = 0; 
                if(g_bang_large_sound) Mix_PlayChannel(-1, g_bang_large_sound, 0);
                g_lives--;
                if(g_lives <= 0) { 
                    g_game_over = 1; 
                } else {
                    g_ship.x = SCREEN_WIDTH/2.0f; g_ship.y = SCREEN_HEIGHT/2.0f;
                    g_ship.vx = 0; g_ship.vy = 0; g_ship.angle = -90.0f;
                    g_ship.alive = 1;
                    g_ship.invincible_timer = RESPAWN_INVINCIBILITY;
                }
            }
        }
    }
    if (asteroids_count == 0 && !g_game_over) {
        g_level++;
        setup_level();
    }
}

void render_game() {
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
    
    // Draw score and lives
    draw_number(g_score, 10, 5);
    for(int i=0; i<g_lives; i++) {
        SDL_Point ship_points[] = {
            {(int)(SCREEN_WIDTH - 30 - i*30), 10},
            {(int)(SCREEN_WIDTH - 50 - i*30), 30},
            {(int)(SCREEN_WIDTH - 40 - i*30), 25},
            {(int)(SCREEN_WIDTH - 30 - i*30), 30},
            {(int)(SCREEN_WIDTH - 30 - i*30), 10}
        };
        SDL_RenderDrawLines(g_renderer, ship_points, 5);
    }


    // Draw ship (and blink if invincible)
    if (g_ship.alive && !(g_ship.invincible_timer > 0 && (g_ship.invincible_timer / 10) % 2 == 0)) {
        float angle_rad = g_ship.angle * M_PI / 180.0f;
        SDL_Point points[4];
        points[0].x = (int)(g_ship.x + cosf(angle_rad) * SHIP_SIZE);
        points[0].y = (int)(g_ship.y + sinf(angle_rad) * SHIP_SIZE);
        points[1].x = (int)(g_ship.x + cosf(angle_rad + 2.5) * SHIP_SIZE * 0.8);
        points[1].y = (int)(g_ship.y + sinf(angle_rad + 2.5) * SHIP_SIZE * 0.8);
        points[2].x = (int)(g_ship.x - cosf(angle_rad) * SHIP_SIZE * 0.5f);
        points[2].y = (int)(g_ship.y - sinf(angle_rad) * SHIP_SIZE * 0.5f);
        points[3].x = (int)(g_ship.x + cosf(angle_rad - 2.5) * SHIP_SIZE * 0.8);
        points[3].y = (int)(g_ship.y + sinf(angle_rad - 2.5) * SHIP_SIZE * 0.8);
        SDL_RenderDrawLines(g_renderer, points, 4);
    }
    
    // Draw bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g_bullets[i].active) {
            SDL_RenderDrawPoint(g_renderer, (int)g_bullets[i].x, (int)g_bullets[i].y);
        }
    }
    
    // Draw asteroids
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (g_asteroids[i].alive) {
            SDL_Point points[17];
            for (int j = 0; j < g_asteroids[i].num_vertices; j++) {
                points[j].x = (int)(g_asteroids[i].x + g_asteroids[i].vertices[j*2]);
                points[j].y = (int)(g_asteroids[i].y + g_asteroids[i].vertices[j*2+1]);
            }
            points[g_asteroids[i].num_vertices] = points[0]; // Close the loop
            SDL_RenderDrawLines(g_renderer, points, g_asteroids[i].num_vertices + 1);
        }
    }
    
    SDL_RenderPresent(g_renderer);
}

void cleanup() {
    if(g_shoot_sound) Mix_FreeChunk(g_shoot_sound);
    if(g_thrust_sound) Mix_FreeChunk(g_thrust_sound);
    if(g_bang_large_sound) Mix_FreeChunk(g_bang_large_sound);
    if(g_bang_medium_sound) Mix_FreeChunk(g_bang_medium_sound);
    if(g_bang_small_sound) Mix_FreeChunk(g_bang_small_sound);
    Mix_CloseAudio();
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
}

void draw_digit(int digit, int x, int y) {
    int segments[10][7] = {
        {1,1,1,0,1,1,1}, {0,0,1,0,0,1,0}, {1,0,1,1,1,0,1}, {1,0,1,1,0,1,1}, {0,1,1,1,0,1,0},
        {1,1,0,1,0,1,1}, {1,1,0,1,1,1,1}, {1,0,1,0,0,1,0}, {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1}
    };
    int seg_w = 12, seg_h = 3;
    y += 5; x += 5;
    if (segments[digit][0]) { SDL_Rect r = {x, y, seg_w, seg_h}; SDL_RenderFillRect(g_renderer, &r); }
    if (segments[digit][1]) { SDL_Rect r = {x, y, seg_h, seg_w}; SDL_RenderFillRect(g_renderer, &r); }
    if (segments[digit][2]) { SDL_Rect r = {x + seg_w - seg_h, y, seg_h, seg_w}; SDL_RenderFillRect(g_renderer, &r); }
    if (segments[digit][3]) { SDL_Rect r = {x, y + seg_w - seg_h, seg_w, seg_h}; SDL_RenderFillRect(g_renderer, &r); }
    if (segments[digit][4]) { SDL_Rect r = {x, y + seg_w, seg_h, seg_w}; SDL_RenderFillRect(g_renderer, &r); }
    if (segments[digit][5]) { SDL_Rect r = {x + seg_w - seg_h, y + seg_w, seg_h, seg_w}; SDL_RenderFillRect(g_renderer, &r); }
    if (segments[digit][6]) { SDL_Rect r = {x, y + 2*seg_w - seg_h, seg_w, seg_h}; SDL_RenderFillRect(g_renderer, &r); }
}

void draw_number(int number, int x, int y) {
    if (number == 0) {
        draw_digit(0, x, y);
        return;
    }
    char buffer[12];
    sprintf(buffer, "%d", number);
    for (int i = 0; buffer[i] != '\0'; i++) {
        draw_digit(buffer[i] - '0', x + i * (12 + 4), y);
    }
}
