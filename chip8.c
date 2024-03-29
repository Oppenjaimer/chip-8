/* -------------------------------------------------------------------------- */
/*                                  INCLUDES                                  */
/* -------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>

/* -------------------------------------------------------------------------- */
/*                                   MACROS                                   */
/* -------------------------------------------------------------------------- */

#ifdef DEBUG
#define debug_print(...) do { printf(__VA_ARGS__); } while (false)
#else
#define debug_print(...) do {} while (false)
#endif

/* -------------------------------------------------------------------------- */
/*                                    DATA                                    */
/* -------------------------------------------------------------------------- */

// Constants
const uint32_t entry_point = 0x200;
const uint8_t font[] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

// Types
typedef enum {
    RUNNING,
    PAUSED,
    QUIT
} state_t;

// Config
uint32_t width = 64;
uint32_t height = 32;
uint32_t scale = 15;
uint32_t bg_color = 0x00000000;
uint32_t fg_color = 0xFFFFFFFF;
char *args_rom = NULL;
bool pixel_outline = false;
uint32_t insts_per_sec = 700;
uint32_t sound_freq = 440;
uint32_t audio_sample_rate = 44100;
int16_t volume = 3000;
float color_lerp_rate = 0.75f;

// SDL
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_AudioSpec desired, obtained;
SDL_AudioDeviceID audio;

// Emulator
state_t state = RUNNING;
uint8_t memory[4096] = {0};
uint16_t stack[16] = {0};
uint8_t sp = 0;
uint8_t V[16] = {0};
uint16_t PC = entry_point;
uint16_t I = 0;
uint8_t DT = 0;
uint8_t ST = 0;
uint32_t pixel_colors[64 * 32] = {0};
bool display[64 * 32] = {false};
bool keypad[16] = {false};
bool draw_flag = false;
char *rom = NULL;

/* -------------------------------------------------------------------------- */
/*                                 PROTOTYPES                                 */
/* -------------------------------------------------------------------------- */

bool init_emulator(char *rom_name);
void update_screen();

/* -------------------------------------------------------------------------- */
/*                                   CONFIG                                   */
/* -------------------------------------------------------------------------- */

/**
 * Set up emulator config from args
 * @param argc Number of args
 * @param argv Args list
 * @return Whether setup was successful
*/
bool set_config(int argc, char **argv) {
    // Options
    int opt;
    while ((opt = getopt(argc, argv, ":s:i:b:f:h")) != -1) {
        switch (opt) {
            case 's':
                // Scale
                scale = (uint32_t)strtoul(optarg, NULL, 10);
                if (scale == 0) {
                    fprintf(stderr, "[ERROR] Invalid scale value\n");
                    return false;
                }
                break;
            
            case 'i':
                // Instructions per second
                insts_per_sec = (uint32_t)strtoul(optarg, NULL, 10);
                if (insts_per_sec == 0) {
                    fprintf(stderr, "[ERROR] Invalid instructions per second value\n");
                    return false;
                }
                break;
            
            case 'b':
                // Background color
                bg_color = (uint32_t)strtoul(optarg, NULL, 16);
                break;
            
            case 'f':
                // Foreground color
                fg_color = (uint32_t)strtoul(optarg, NULL, 16);
                break;
            
            case 'h':
                // Print help
                printf("Usage: %s [...OPTIONS] ROM_NAME\n", argv[0]);
                printf("\n");
                printf("Options:\n");
                printf("  -s NUM\tSet pixel scale factor (default: 15)\n");
                printf("  -i NUM\tSet instructions per second (default: 700)\n");
                printf("  -b RGBA\tSet background color in hex (default: 00000000)\n");
                printf("  -f RGBA\tSet foreground color in hex (default: FFFFFFFF)\n");
                exit(EXIT_SUCCESS);
            
            case ':':
                fprintf(stderr, "[ERROR] Option requires a value\n");
                return false;
            
            case '?':
                fprintf(stderr, "[ERROR] Unknown option\n");
                return false;
        }
    }

    // Args
    int args_len = 0;
    int arg_pos;
    for (; optind < argc; optind++) {
        if (args_len == 0) arg_pos = optind;
        args_len++;
    }

    if (args_len != 1) {
        fprintf(stderr, "[ERROR] Invalid number of args provided\n");
        return false;
    }

    // Set ROM name
    args_rom = argv[arg_pos];
    return true;
}

/* -------------------------------------------------------------------------- */
/*                                     SDL                                    */
/* -------------------------------------------------------------------------- */

/**
 * SDL audio device callback function
 * @param userdata User data
 * @param stream Pointer to the audio data buffer
 * @param len Length of the buffer
*/
void audio_callback(void *userdata, uint8_t *stream, int len) {
    (void)userdata;

    int16_t *audio_data = (int16_t *)stream;
    static uint32_t sample_index = 0;
    const int32_t sound_period = audio_sample_rate / sound_freq;
    const int32_t half_sound_period = sound_period / 2;

    // Fill audio data buffer 2 bytes at a time
    for (int i = 0; i < len / 2; i++) {
        audio_data[i] = ((sample_index++ / half_sound_period) % 2) ? volume : -volume;
    }
}

/**
 * Initialize SDL subsystems and components
 * @return Whether initialization was successful
*/
bool init_sdl() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "[ERROR] Unable to initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow(
        "CHIP-8 Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width * scale,
        height * scale,
        0
    );
    if (!window) {
        fprintf(stderr, "[ERROR] Unable to create window: %s\n", SDL_GetError());
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "[ERROR] Unable to create renderer: %s\n", SDL_GetError());
        return false;
    }

    desired = (SDL_AudioSpec){
        .freq = audio_sample_rate,
        .format = AUDIO_S16LSB,
        .channels = 1,
        .samples = 512,
        .callback = audio_callback,
    };

    audio = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (!audio) {
        fprintf(stderr, "[ERROR] Unable to open audio device: %s\n", SDL_GetError());
        return false;
    }

    if (desired.format != obtained.format || desired.channels != obtained.channels) {
        fprintf(stderr, "[ERROR] Unable to get desired audio spec\n");
        return false;
    }

    return true;
}

/**
 * SDL event handler
*/
void handle_events() {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                // Quit emulator
                state = QUIT;
                break;
            
            case SDL_KEYDOWN:
                switch (event.key.keysym.scancode) {
                    case SDL_SCANCODE_ESCAPE:
                        // Quit emulator (ESC key)
                        state = QUIT;
                        break;
                    
                    case SDL_SCANCODE_SPACE:
                        // Toggle pause
                        if (state == PAUSED) {
                            state = RUNNING;
                            printf("[INFO] Unpaused\n");
                        } else {
                            state = PAUSED;
                            printf("[INFO] Paused\n");
                        }
                        break;
                    
                    case SDL_SCANCODE_BACKSPACE:
                        // Reset emulator for current ROM
                        init_emulator(rom);
                        break;
                    
                    case SDL_SCANCODE_U:
                        // Decrease lerp rate
                        if (color_lerp_rate > 0.05) color_lerp_rate -= 0.05;
                        break;

                    case SDL_SCANCODE_I:
                        // Increase lerp rate
                        if (color_lerp_rate < 1) color_lerp_rate += 0.05;
                        break;

                    case SDL_SCANCODE_O:
                        // Decrease volume
                        if (volume > 0) volume -= 500;
                        break;

                    case SDL_SCANCODE_P:
                        // Increase volme
                        if (volume < INT16_MAX) volume += 500;
                        break;
                    
                    case SDL_SCANCODE_L:
                        // Toggle pixel outline
                        pixel_outline = !pixel_outline;
                        update_screen();
                        break;
                    
                    // Keypad mappings
                    case SDL_SCANCODE_1: keypad[0x1] = true; break;
                    case SDL_SCANCODE_2: keypad[0x2] = true; break;
                    case SDL_SCANCODE_3: keypad[0x3] = true; break;
                    case SDL_SCANCODE_4: keypad[0xC] = true; break;
                    case SDL_SCANCODE_Q: keypad[0x4] = true; break;
                    case SDL_SCANCODE_W: keypad[0x5] = true; break;
                    case SDL_SCANCODE_E: keypad[0x6] = true; break;
                    case SDL_SCANCODE_R: keypad[0xD] = true; break;
                    case SDL_SCANCODE_A: keypad[0x7] = true; break;
                    case SDL_SCANCODE_S: keypad[0x8] = true; break;
                    case SDL_SCANCODE_D: keypad[0x9] = true; break;
                    case SDL_SCANCODE_F: keypad[0xE] = true; break;
                    case SDL_SCANCODE_Z: keypad[0xA] = true; break;
                    case SDL_SCANCODE_X: keypad[0x0] = true; break;
                    case SDL_SCANCODE_C: keypad[0xB] = true; break;
                    case SDL_SCANCODE_V: keypad[0xF] = true; break;

                    default:
                        break;
                }
                break;
            
            case SDL_KEYUP:
                switch (event.key.keysym.scancode) {
                    // Keypad mappings
                    case SDL_SCANCODE_1: keypad[0x1] = false; break;
                    case SDL_SCANCODE_2: keypad[0x2] = false; break;
                    case SDL_SCANCODE_3: keypad[0x3] = false; break;
                    case SDL_SCANCODE_4: keypad[0xC] = false; break;
                    case SDL_SCANCODE_Q: keypad[0x4] = false; break;
                    case SDL_SCANCODE_W: keypad[0x5] = false; break;
                    case SDL_SCANCODE_E: keypad[0x6] = false; break;
                    case SDL_SCANCODE_R: keypad[0xD] = false; break;
                    case SDL_SCANCODE_A: keypad[0x7] = false; break;
                    case SDL_SCANCODE_S: keypad[0x8] = false; break;
                    case SDL_SCANCODE_D: keypad[0x9] = false; break;
                    case SDL_SCANCODE_F: keypad[0xE] = false; break;
                    case SDL_SCANCODE_Z: keypad[0xA] = false; break;
                    case SDL_SCANCODE_X: keypad[0x0] = false; break;
                    case SDL_SCANCODE_C: keypad[0xB] = false; break;
                    case SDL_SCANCODE_V: keypad[0xF] = false; break;

                    default:
                        break;
                }
                break;
            
            default:
                break;
        }
    }
}

/**
 * Extract color components in RGBA format
 * @param color RGBA color
 * @param r Pointer to R component
 * @param g Pointer to G component
 * @param b Pointer to B component
 * @param a Pointer to A component
*/
void extract_color(uint32_t color, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    *r = (color >> 24) & 0xFF;
    *g = (color >> 16) & 0xFF;
    *b = (color >>  8) & 0xFF;
    *a = (color >>  0) & 0xFF;
}

/**
 * Linear interpolation between two colors
 * @param start_color Initial color
 * @param end_color Final color
 * @param t Color lerp rate
 * @return Intermediate color
*/
uint32_t color_lerp(uint32_t start_color, uint32_t end_color, float t) {
    // Extract start color components
    uint8_t s_r, s_g, s_b, s_a;
    extract_color(start_color, &s_r, &s_g, &s_b, &s_a);

    // Extract end color components
    uint8_t e_r, e_g, e_b, e_a;
    extract_color(end_color, &e_r, &e_g, &e_b, &e_a);

    // Get lerped color components
    uint8_t l_r = ((1 - t) * s_r) + (t * e_r);
    uint8_t l_g = ((1 - t) * s_g) + (t * e_g);
    uint8_t l_b = ((1 - t) * s_b) + (t * e_b);
    uint8_t l_a = ((1 - t) * s_a) + (t * e_a);

    return (l_r << 24) | (l_g << 16) | (l_b << 8) | l_a;
}

/**
 * Draw display contents to the screen
*/
void update_screen() {
    SDL_Rect rect = { .x = 0, .y = 0, .w = scale, .h = scale };

    // Extract background color components
    uint8_t bg_r, bg_g, bg_b, bg_a;
    extract_color(bg_color, &bg_r, &bg_g, &bg_b, &bg_a);

    // Extract foreground color components
    uint8_t fg_r, fg_g, fg_b, fg_a;
    extract_color(fg_color, &fg_r, &fg_g, &fg_b, &fg_a);

    // Loop through display pixels
    for (uint32_t i = 0; i < sizeof(display); i++) {
        rect.x = (i % width) * scale;
        rect.y = (i / width) * scale;

        if (display[i]) {
            // Pixel on, lerp towards foreground color if not already there
            if (pixel_colors[i] != fg_color) {
                pixel_colors[i] = color_lerp(pixel_colors[i], fg_color, color_lerp_rate);
            }
        } else {
            // Pixel off, lerp towards background color if not already there
            if (pixel_colors[i] != bg_color) {
                pixel_colors[i] = color_lerp(pixel_colors[i], bg_color, color_lerp_rate);
            }
        }

        // Extract current pixel color components
        uint8_t r, g, b, a;
        extract_color(pixel_colors[i], &r, &g, &b, &a);

        // Update color and draw pixel
        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_RenderFillRect(renderer, &rect);

        // Draw pixel outline if enabled
        if (pixel_outline) {
            SDL_SetRenderDrawColor(renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderDrawRect(renderer, &rect);
        }
    }

    SDL_RenderPresent(renderer);
}

/**
 * Cap framerate to 60 FPS
 * @param diff Main loop execution time difference
*/
void cap_framerate(uint64_t diff) {
    double elapsed = diff / (double)SDL_GetPerformanceFrequency() * 1000.0;
    double delay = 16.6667 > elapsed ? SDL_floor(16.6667 - elapsed) : 0;
    SDL_Delay(delay);
}

/**
 * Update delay and sound timers at 60 Hz
*/
void update_timers() {
    // Delay timer
    if (DT > 0) DT--;

    // Sound timer
    if (ST > 0) {
        ST--;
        SDL_PauseAudioDevice(audio, false);
    } else {
        SDL_PauseAudioDevice(audio, true);
    }
}

/**
 * Destroy SDL components and quit SDL
*/
void clean_sdl() {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_CloseAudioDevice(audio);
    SDL_Quit();
}

/* -------------------------------------------------------------------------- */
/*                                  EMULATOR                                  */
/* -------------------------------------------------------------------------- */

/**
 * Load ROM file into memory
 * @param rom_name ROM file name
 * @return Whether loading was successful
*/
bool load_rom(char *rom_name) {
    // Open ROM file
    FILE *f = fopen(rom_name, "rb");
    if (!f) {
        fprintf(stderr, "[ERROR] ROM '%s' not found\n", rom_name);
        return false;
    }

    // Get size
    fseek(f, 0, SEEK_END);
    const size_t rom_size = ftell(f);
    const size_t max_size = sizeof(memory) - entry_point;
    rewind(f);

    // Check if size is valid
    if (rom_size > max_size) {
        fprintf(stderr, "[ERROR] ROM '%s' is too large\n", rom_name);
        return false;
    }

    // Read into memory
    if (fread(&memory[entry_point], rom_size, 1, f) != 1) {
        fprintf(stderr, "[ERROR] Unable to read ROM '%s' into memory\n", rom_name);
        return false;
    }
    rom = rom_name;

    // Close ROM file
    fclose(f);

    return true;
}

/**
 * Initialize CHIP-8 emulator
 * @param rom_name ROM file name
 * @return Whether initialization was successful
*/
bool init_emulator(char *rom_name) {
    // Reset emulator
    memset(&memory[0], 0, sizeof(memory));
    memset(&V[0], 0, sizeof(V));
    memset(&stack[0], 0, sizeof(stack));    
    memset(&display[0], 0, sizeof(display));
    sp = 0;
    PC = entry_point;
    I = 0;
    DT = 0;
    ST = 0;
    draw_flag = false;

    // Load font
    memcpy(&memory[0], font, sizeof(font));

    // Load ROM file
    return load_rom(rom_name);
}

/**
 * Emulate current instruction
*/
void emulate_instruction() {
    // Fetch current opcode and increment PC for next one
    const uint16_t opcode = (memory[PC] << 8) | memory[PC + 1];
    PC += 2;

    // Decode instruction
    const uint16_t NNN = opcode & 0x0FFF;
    const uint8_t NN = opcode & 0x00FF;
    const uint8_t N = opcode & 0x000F;
    const uint8_t X = (opcode & 0x0F00) >> 8;
    const uint8_t Y = (opcode & 0x00F0) >> 4;

    // Execute instruction
    debug_print("[DEBUG] Opcode=0x%04X @ PC=0x%04X - ", opcode, PC - 2);
    switch (opcode >> 12) {
        case 0x0:
            switch (NN) {
                case 0xE0:
                    // 00E0: clear the screen
                    debug_print("Clear the screen\n");
                    memset(&display[0], false, sizeof(display));
                    draw_flag = true;
                    break;
                
                case 0xEE:
                    // 00EE: return from subroutine
                    debug_print("Return from subroutine to PC=0x%04X\n", stack[sp - 1]);
                    PC = stack[--sp];
                    break;
                
                default:
                    debug_print("Unimplemented opcode\n");
                    break;
            }
            break;
        
        case 0x1:
            // 1NNN: jump to address NNN
            debug_print("Jump to NNN=0x%03X\n", NNN);
            PC = NNN;
            break;
        
        case 0x2:
            // 2NNN: call subroutine at NNN
            debug_print("Call subroutine at NNN=0x%03X\n", NNN);
            stack[sp++] = PC;
            PC = NNN;
            break;
        
        case 0x3:
            // 3XNN: skip next instruction if VX == NN
            debug_print("Skip next instruction if V%01X equals NN=0x%02X (%d)\n", X, NN, V[X] == NN);
            if (V[X] == NN) PC += 2;
            break;
        
        case 0x4:
            // 4XNN: skip next instruction if VX != NN
            debug_print("Skip next instruction if V%01X doesn't equal NN=0x%02X (%d)\n", X, NN, V[X] != NN);
            if (V[X] != NN) PC += 2;
            break;
        
        case 0x5:
            // 5XY0: skip next instruction if VX == VY
            debug_print("Skip next instruction if V%01X equals V%01X (%d)\n", X, Y, V[X] == V[Y]);
            if (V[X] == V[Y]) PC += 2;
            break;
        
        case 0x6:
            // 6XNN: set VX to NN
            debug_print("Set V%01X to NN=0x%02X\n", X, NN);
            V[X] = NN;
            break;
        
        case 0x7:
            // 7XNN: add NN to VX
            debug_print("Add NN=0x%02X to V%01X\n", NN, X);
            V[X] += NN;
            break;
        
        case 0x8:
            switch (N) {
                case 0x0:
                    // 8XY0: set VX to VY
                    debug_print("Set V%01X to V%01X\n", X, Y);
                    V[X] = V[Y];
                    break;
                
                case 0x1:
                    // 8XY1: set VX to VX OR VY
                    debug_print("Set V%01X to V%01X OR V%01X\n", X, X, Y);
                    V[X] |= V[Y];
                    break;
                
                case 0x2:
                    // 8XY2: set VX to VX AND VY
                    debug_print("Set V%01X to V%01X AND V%01X\n", X, X, Y);
                    V[X] &= V[Y];
                    break;
                
                case 0x3:
                    // 8XY3: set VX to VX XOR VY
                    debug_print("Set V%01X to V%01X XOR V%01X\n", X, X, Y);
                    V[X] ^= V[Y];
                    break;
                
                case 0x4:
                    // 8XY4: add VY to VX; set VF to 1 if carry, and to 0 otherwise
                    debug_print("Add V%01X to V%01X, set VF to %d\n", Y, X, V[X] + V[Y] > 0xFF);
                    V[0xF] = (V[X] + V[Y] > 0xFF);
                    V[X] += V[Y];
                    break;
                
                case 0x5:
                    // 8XY5: subtract VY from VX; set VF to 0 if borrow, and to 1 otherwise
                    debug_print("Subtract V%01X from V%01X, set VF to %d\n", Y, X, V[X] > V[Y]);
                    V[0xF] = V[X] > V[Y];
                    V[X] -= V[Y];
                    break;
                
                case 0x6:
                    // 8XY6: right-shift VX by 1; set VF to LSB of VX
                    debug_print("Right-shift V%01X by 1, set VF to %d\n", X, V[X] & 0xF);
                    V[0xF] = V[X] & 0xF;
                    V[X] >>= 1;
                    break;
                
                case 0x7:
                    // 8XY7: set VX to VY - VX; set VF to 0 if borrow, and to 1 otherwise
                    debug_print("Set V%01X to V%01X - V%01X, set VF to %d\n", X, Y, X, V[Y] > V[X]);
                    V[0xF] = V[Y] > V[X];
                    V[X] = V[Y] - V[X];
                    break;
                
                case 0xE:
                    // 8XYE: left-shift VX by 1; set VF to MSB of VX
                    debug_print("Left-shift V%01X by 1, set VF to %d\n", X, V[X] >> 7);
                    V[0xF] = V[X] >> 7;
                    V[X] <<= 1;
                    break;
                
                default:
                    debug_print("Unimplemented opcode\n");
                    break;
            }
            break;
        
        case 0x9:
            // 9XY0: skip next instruction if VX != VY
            debug_print("Skip next instruction if V%01X doesn't equal V%01X (%d)\n", X, Y, V[X] != V[Y]);
            if (V[X] != V[Y]) PC += 2;
            break;
        
        case 0xA:
            // ANNN: set I to address NNN
            debug_print("Set I to NNN=0x%03X\n", NNN);
            I = NNN;
            break;
        
        case 0xB:
            // BNNN: jump to address NNN + V0
            debug_print("Jump to address NNN=0x%03X + V0 (0x%04X)\n", NNN, NNN + V[0x0]);
            PC = NNN + V[0x0];
            break;
        
        case 0xC:
            ;// CXNN: set VX to rand() AND NN
            uint8_t num = rand() % 256;
            debug_print("Set VX to rand()=0x%02X AND NN=0x%02X (0x%02X)\n", num, NN, num & NN);
            V[X] = num & NN;
            break;
        
        case 0xD:
            // DXYN: draw N-height sprite at coords (VX, VY);
            // set VF to 1 if any pixel is turned off, and to 0 otherwise
            debug_print("Draw %u-height sprite at (V%01X, V%01X) from I 0x%04X\n", N, X, Y, I);

            draw_flag = true;

            uint8_t x = V[X] % width;
            uint8_t y = V[Y] % height;
            const uint8_t original_x = x;
            
            V[0xF] = 0;

            for (uint8_t i = 0; i < N; i++) {
                const uint8_t sprite_row = memory[I + i];
                x = original_x;

                for (int8_t j = 7; j >= 0; j--) {
                    const bool sprite_bit = sprite_row & (1 << j);
                    bool *display_pixel = &display[y * width + x];

                    if (sprite_bit && *display_pixel) V[0xF] = 1;

                    *display_pixel ^= sprite_bit;

                    if (++x >= width) break;
                }

                if (++y >= height) break;
            }

            break;
        
        case 0xE:
            switch (NN) {
                case 0x9E:
                    // EX9E: skip next instruction if key in VX is pressed
                    debug_print("Skip next instruction if key in V%01X is pressed (%d)\n", X, keypad[V[X]]);
                    if (keypad[V[X]]) PC += 2;
                    break;
                
                case 0xA1:
                    // EXA1: skip next instruction if key in VX isn't pressed
                    debug_print("Skip next instruction if key in V%01X isn't pressed (%d)\n", X, !keypad[V[X]]);
                    if (!keypad[V[X]]) PC += 2;
                    break;

                default:
                    debug_print("Unimplemented opcode\n");
                    break;
            }
            break;
        
        case 0xF:
            switch (NN) {
                case 0x07:
                    // FX07: set VX to DT
                    debug_print("Set V%01X to DT=0x%02X\n", X, DT);
                    V[X] = DT;
                    break;
                
                case 0x0A:
                    // FX0A: wait for keypress; store it in VX
                    debug_print("Wait for keypress and store it in V%01X\n", X);

                    static bool key_pressed = false;
                    static uint8_t key = 0xFF;

                    // Check for keypress
                    for (uint8_t i = 0; i < 16 && key == 0xFF; i++) {
                        if (keypad[i]) {
                            key = i;
                            key_pressed = true;
                            break;
                        }
                    }

                    // If no key pressed, execute same instruction
                    if (!key_pressed) PC -= 2;
                    else {
                        // If key is still pressed, wait until it's released
                        if (keypad[key]) PC -= 2;
                        else {
                            V[X] = key;
                            key = 0xFF;
                            key_pressed = false;
                        }
                    }

                    break;
                
                case 0x15:
                    // FX15: set DT to VX
                    debug_print("Set DT to V%01X\n", X);
                    DT = V[X];
                    break;
                
                case 0x18:
                    // FX18: set ST to VX
                    debug_print("Set ST to V%01X\n", X);
                    ST = V[X];
                    break;
                
                case 0x1E:
                    // FX1E: add VX to I
                    debug_print("Add V%01X to I=0x%04X\n", X, I);
                    I += V[X];
                    break;
                
                case 0x29:
                    // FX29: set I to address of sprite for char in VX
                    debug_print("Set I to sprite adress in V%01X (0x%04X)\n", X, V[X] * 5);
                    I = V[X] * 5;
                    break;
                
                case 0x33:
                    // FX33: store BCD representation of VX at locations I, I+1 and I+2
                    debug_print("Store BCD representation of V%01X at I=%04X, I+1 and I+2\n", X, I);
                    memory[I] = V[X] / 100;
                    memory[I + 1] = (V[X] % 100) / 10;
                    memory[I + 2] = V[X] % 10;
                    break;
                
                case 0x55:
                    // FX55: store from V0 to VX in memory starting at address I
                    debug_print("Store from V0 to V%01X in memory starting at I=0x%04X\n", X, I);
                    for (int i = 0; i <= X; i++) {
                        memory[I + i] = V[i];
                    }
                    break;

                case 0x65:
                    // FX65: fill from V0 to VX from memory starting at address I
                    debug_print("Fill from V0 to V%01X from memory starting at I=0x%04X\n", X, I);
                    for (int i = 0; i <= X; i++) {
                        V[i] = memory[I + i];
                    }
                    break;

                default:
                    debug_print("Unimplement opcode\n");
                    break;
            }
            break;

        default:
            debug_print("Unimplemented opcode\n");
            break;
    }
}

/* -------------------------------------------------------------------------- */
/*                                    MAIN                                    */
/* -------------------------------------------------------------------------- */

/**
 * Application entry point
 * @param argc Number of args
 * @param argv Args list
 * @return Exit code
*/
int main(int argc, char **argv) {
    if (!set_config(argc, argv)) return EXIT_FAILURE;
    if (!init_emulator(args_rom)) return EXIT_FAILURE;
    if (!init_sdl()) return EXIT_FAILURE;

    // Initialize random number generator
    srand(time(NULL));

    // Main loop
    while (state != QUIT) {
        handle_events();

        if (state == PAUSED) continue;

        uint64_t start = SDL_GetPerformanceCounter();
        // Execute number of instructions every second at 60 Hz
        for (uint32_t i = 0; i < insts_per_sec / 60; i++) {
            emulate_instruction();
        }
        uint64_t end = SDL_GetPerformanceCounter();

        cap_framerate(end - start);

        if (draw_flag) {
            update_screen();

            // Reset draw flag
            draw_flag = false;
        }

        update_timers();
    }

    clean_sdl();

    return EXIT_SUCCESS;
}