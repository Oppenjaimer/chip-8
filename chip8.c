/* -------------------------------------------------------------------------- */
/*                                  INCLUDES                                  */
/* -------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdbool.h>

#include <SDL2/SDL.h>

/* -------------------------------------------------------------------------- */
/*                                    DATA                                    */
/* -------------------------------------------------------------------------- */

// Types
typedef enum {
    RUNNING,
    PAUSED,
    QUIT
} state_t;

// Config
uint32_t width;
uint32_t height;
uint32_t scale;

// SDL
SDL_Window *window;
SDL_Renderer *renderer;

// Emulator
state_t state;

/* -------------------------------------------------------------------------- */
/*                                   CONFIG                                   */
/* -------------------------------------------------------------------------- */

/**
 * Set config defaults and override from args
 * @param argc Number of args
 * @param argv Args list
 * @return Whether setup was successful
*/
bool set_config(int argc, char **argv) {
    // Set defaults
    width = 64;
    height = 32;
    scale = 15;

    // TODO: Override from args
    for (int i = 0; i < argc; i++) {
        (void)argv[i];
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/*                                     SDL                                    */
/* -------------------------------------------------------------------------- */

/**
 * Initialize SDL susbsytems and components
 * @return Whether initialization was successful
*/
bool init_sdl() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
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
        fprintf(stderr, "Unable to create window: %s\n", SDL_GetError());
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Unable to create renderer: %s\n", SDL_GetError());
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
 * Final SDL cleanup
*/
void clean_sdl() {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

/* -------------------------------------------------------------------------- */
/*                                  EMULATOR                                  */
/* -------------------------------------------------------------------------- */

/**
 * Initialize CHIP-8 emulator and set up initial state
 * @return Whether initialization was successful
*/
bool init_emulator() {
    state = RUNNING;

    return true;
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
    if (!init_emulator()) return EXIT_FAILURE;
    if (!init_sdl()) return EXIT_FAILURE;

    // Main loop
    while (state != QUIT) {
        handle_events();

        if (state == PAUSED) continue;

        // TODO: execute instructions
        // TODO: cap framerate
    }

    clean_sdl();

    return EXIT_SUCCESS;
}