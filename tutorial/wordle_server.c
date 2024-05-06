#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include <stddef.h>
#include "printf.h"
#include "wordle.h"

/*
 * Here we initialise the word to "hello", but later in the tutorial
 * we will actually randomise the word the user is guessing.
 */
char word[WORD_LENGTH] = { 'h', 'e', 'l', 'l', 'o' };

bool is_character_in_word(char *word, int ch) {
    for (int i = 0; i < WORD_LENGTH; i++) {
        if (word[i] == ch) {
            return true;
        }
    }

    return false;
}

enum character_state char_to_state(int ch, char *word, uint64_t index) {
    if (ch == word[index]) {
        return CORRECT_PLACEMENT;
    } else if (is_character_in_word(word, ch)) {
        return INCORRECT_PLACEMENT;
    } else {
        return INCORRECT;
    }
}

void init(void) {
    microkit_dbg_puts("WORDLE SERVER: starting\n");
}

void notified(microkit_channel channel) {}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo){
    switch (ch) {
        case 0:
            // current message register from request
            uint64_t reg_in = 0;
            // current message register for response
            uint64_t reg_out = 0;
            for (size_t i = 0; i < WORD_LENGTH; i++){
                uint8_t mr = i / 8;

                // poll from next mr once we consumed one completely
                if (i % BYTES_PER_REGISTER == 0){
                    reg_in = microkit_mr_get(mr);
                }

                // get current character, masking out everything else
                char current_c = reg_in & 0xff;
                // shift out current char in prepartion for the next iteration
                reg_in >>= BITS_PER_CHAR;

                // bit offset within the message register for the next response state
                uint8_t offset = (i % BYTES_PER_REGISTER ) * BITS_PER_CHAR;
                // punch in the next response state to the current message register
                reg_out |= ((uint64_t) char_to_state(current_c, word, i)) << offset;

                // submit current message register
                if (
                    // or if next char will come from the next message register
                    (i + 1 % BYTES_PER_REGISTER == 0)
                    // if last char was processed
                    || (i + 1 == WORD_LENGTH)
                 ){
                    microkit_mr_set(mr, reg_out);
                    reg_out = 0;
                }
            }
        break;
        default:
            printf("received unknown ppc on channel %d\n", ch);
        break;
    }

    return msginfo;
}
