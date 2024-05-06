#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include <string.h>
#include "printf.h"
#include "wordle.h"

#define MOVE_CURSOR_UP "\033[5A"
#define CLEAR_TERMINAL_BELOW_CURSOR "\033[0J"

#define INVALID_CHAR (-1)

// only ever contains a single byte
volatile uint8_t* from_serial_server;

// can hold up to 1024 bytes (1023 usable + one null byte)
volatile uint8_t* to_serial_server;

struct wordle_char {
    int ch;
    enum character_state state;
};

// Store game state
static struct wordle_char table[NUM_TRIES][WORD_LENGTH];
// Use these global variables to keep track of the character index that the
// player is currently trying to input.
static int curr_row = 0;
static int curr_letter = 0;

void wordle_server_send() {
    // Implement this function to send the word over PPC
    // After doing the PPC, the Wordle server should have updated
    // the message-registers containing the state of each character.
    // Look at the message registers and update the `table` accordingly.


    // We can combine 8 chars (each one byte in size) per register passed via ppc, as each register
    // is 64 bits/ 8 bytes in size.

    // count of registers required to submit one word guess
    uint16_t regs_needed = (WORD_LENGTH + (BYTES_PER_REGISTER - 1)) / BYTES_PER_REGISTER;

    // allocate a new msginfo for our request
    microkit_msginfo req = microkit_msginfo_new(0, regs_needed);

    // register value to work on
    uint64_t value = 0;

    // for each char, push it to the message registers
    for (size_t i = 0; i < WORD_LENGTH; i++){
        // current character from the word
        char current_c = table[curr_row][i].ch;

        // shift operation offset
        uint8_t offset = (i % BYTES_PER_REGISTER ) * BITS_PER_CHAR;

        // punch in the character at the current offset into the register
        value |= ((uint64_t) current_c) << offset;

        // set current message register to final value
        if (
            // if this is the last byte of the message register
            (i % BYTES_PER_REGISTER == (BYTES_PER_REGISTER - 1))
            // or if the last byte of the word
            || (i + 1 == WORD_LENGTH)
        ){
            uint8_t mr = i / 8;
            microkit_mr_set(mr, value);
            value = 0;
        }
    }

    // make the protected procedure call
    microkit_msginfo resp = microkit_ppcall(0, req);


    // read back response
    for (size_t i = 0; i < WORD_LENGTH; i++){
        // read next message register
        if (i % BYTES_PER_REGISTER == 0){
            uint8_t mr = i / 8;
            value = microkit_mr_get(mr);
        }

        // get current state, masking the rest of the message register
        char current_state = value & 0xff;
        // set current state
        table[curr_row][i].state = current_state;
        // shift out current state, so that in the next loop the next state can be read
        value >>= BITS_PER_CHAR;
    }
}

void serial_send(char *str) {
    size_t i = 0;
    for(; str[i] != '\0'; i++)
        to_serial_server[i] = str[i];

    to_serial_server[i] = '\0';

    microkit_notify(2);
}

// This function prints a CLI Wordle using pretty colours for what characters
// are correct, or correct but in the wrong place etc.
void print_table(bool clear_terminal) {
    if (clear_terminal) {
        // Assuming we have already printed a Wordle table, this will clear the
        // table we have already printed and then print the updated one. This
        // is done by moving the cursor up 5 lines and then clearing everything
        // below it.
        serial_send(MOVE_CURSOR_UP);
        serial_send(CLEAR_TERMINAL_BELOW_CURSOR);
    }

    for (int row = 0; row < NUM_TRIES; row++) {
        for (int letter = 0; letter < WORD_LENGTH; letter++) {
            serial_send("[");
            enum character_state state = table[row][letter].state;
            int ch = table[row][letter].ch;
            if (ch != INVALID_CHAR) {
                switch (state) {
                    case INCORRECT: break;
                    case CORRECT_PLACEMENT: serial_send("\x1B[32m"); break;
                    case INCORRECT_PLACEMENT: serial_send("\x1B[33m"); break;
                    default:
                        // Print out error messages/debug info via debug output
                        microkit_dbg_puts("CLIENT|ERROR: unexpected character state\n");
                }
                char ch_str[] = { ch, '\0' };
                serial_send(ch_str);
                // Reset colour
                serial_send("\x1B[0m");
            } else {
                serial_send(" ");
            }
            serial_send("] ");
        }
        serial_send("\n");
    }
}

void init_table() {
    for (int row = 0; row < NUM_TRIES; row++) {
        for (int letter = 0; letter < WORD_LENGTH; letter++) {
            table[row][letter].ch = INVALID_CHAR;
            table[row][letter].state = INCORRECT;
        }
    }
}

bool char_is_backspace(int ch) {
    return (ch == 0x7f);
}

bool char_is_valid(int ch) {
    // Only allow alphabetical letters and do not accept a character if the
    // current word has already been filled.
    return ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) && curr_letter != WORD_LENGTH;
}

void add_char_to_table(char c) {
    if (char_is_backspace(c)) {
        if (curr_letter > 0) {
            curr_letter--;
            table[curr_row][curr_letter].ch = INVALID_CHAR;
        }
    } else if (c != '\r' && c != ' ' && curr_letter != WORD_LENGTH) {
        table[curr_row][curr_letter].ch = c;
        curr_letter++;
    }

    // If the user has finished inputting a word, we want to send the
    // word to the server and move the cursor to the next row.
    if (c == '\r' && curr_letter == WORD_LENGTH) {
        wordle_server_send();
        curr_row += 1;
        curr_letter = 0;
    }
}

void init(void) {
    microkit_dbg_puts("CLIENT: starting\n");
    serial_send("Welcome to the Wordle client!\n");

    init_table();
    // Don't want to clear the terminal yet since this is the first time
    // we are printing it (we want to clear just the Wordle table, not
    // everything on the terminal).
    print_table(false);
}

void notified(microkit_channel channel) {
    switch (channel) {
        case 1:
            // read the byte
            uint8_t char_in = from_serial_server[0];

            // acknowledge the byte was read
            microkit_notify(1);

            // consume the byte
            add_char_to_table(char_in);
            print_table(true);
        break;
        case 2:
        break;
    }
}
