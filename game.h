#ifndef __GAME_H__
#define __GAME_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct letter_set
{
    uint32_t bitfield; // a is bit 0, b is bit 1, etc.
} letter_set_t;

letter_set_t letter_set_empty();
letter_set_t letter_set_full();
char normalize(char letter);
bool is_letter(char character);
bool letter_set_contains(letter_set_t set, char letter);
letter_set_t letter_set_add(letter_set_t set, char letter);
letter_set_t letter_set_remove(letter_set_t set, char letter);
letter_set_t letter_set_intersect(letter_set_t left, letter_set_t right);
letter_set_t letter_set_union(letter_set_t left, letter_set_t right);
size_t letter_set_size(letter_set_t set);
bool letter_set_is_empty(letter_set_t set);
bool letter_set_equals(letter_set_t left, letter_set_t right);

typedef struct secret_word
{
    char *word;
    uint8_t *revealed_bitfield;
    size_t word_length;
    size_t revealed_bitfield_length;
    letter_set_t incorrect_guesses;
    letter_set_t all_guesses;
} secret_word_t;

bool secret_word_init_from_c_string(secret_word_t *word, const char *string);
bool secret_word_init_from_buffer_and_size(secret_word_t *word, const char *buffer, size_t size);
void secret_word_free(secret_word_t *word);

typedef enum secret_word_guess_result
{
    SECRET_WORD_GUESS_CORRECT = 0,
    SECRET_WORD_GUESS_INCORRECT = 1,
    SECRET_WORD_GUESS_INVALID_LETTER = 2,
    SECRET_WORD_GUESS_ALREADY_GUESSED_LETTER = 3,
} secret_word_guess_result_t;

secret_word_guess_result_t secret_word_guess(secret_word_t *word, char guess);

typedef enum secret_word_letter_at_result
{
    SECRET_WORD_LETTER_REVEALED = 0,
    SECRET_WORD_LETTER_HIDDEN = 1,
    SECRET_WORD_LETTER_INDEX_OUT_OF_BOUNDS = 2,
} secret_word_letter_at_result_t;

secret_word_letter_at_result_t secret_word_letter_at(const secret_word_t *word, size_t index, char *result);
size_t secret_word_incorrect_guess_count(const secret_word_t *word);
bool secret_word_is_solved(const secret_word_t* word);

#endif
