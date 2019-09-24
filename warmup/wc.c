#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include "common.h"
#include "wc.h"

unsigned long  get_index(char* word);
void insert(struct wc* wc, char* word_string);

// global variable
unsigned long hash_array_size = 1;
	
struct wc {
    struct word_struct **hash_array;
};

struct word_struct {
    char* key; 
    int occurences;
};

// hash function: djb2. author: dan bernstein. source: http://www.cse.yorku.ca/~oz/hash.html
unsigned long get_index(char *word_string) {
    unsigned long hash = 5381;
    int c;
    
    while ((c = *word_string++) != '\0')
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash % hash_array_size;
}

void insert(struct wc* wc, char* word_string) {
    if(wc == NULL || word_string == NULL)
      return;

    // get index (hash) of word in hash_array
    unsigned long index = get_index(word_string);
         
    //printf("testing start\n");
    
//    if(wc->hash_array != NULL)
//        printf("1\n");
    
//    if(wc->hash_array[index] == NULL)
//        printf("2\n");
//    
//    if(strcmp(wc->hash_array[index]->key, word_string) != 0)
//        printf("3\n");
    
    
    // avoiding collisions and repeated words
    while(wc->hash_array != NULL && wc->hash_array[index] != NULL && strcmp(wc->hash_array[index]->key, word_string) != 0) {
        //printf("1\n");
        index++;
        index%=hash_array_size;
    }
    
    // duplicate word, just increase count
    if(wc->hash_array[index] != NULL && strcmp(wc->hash_array[index]->key, word_string) == 0) {
        wc->hash_array[index]->occurences++; // just increment this as this word already exists in hash table
    }
    // new word, add to hash array 
    else {
        struct word_struct* word_struct;
        word_struct = (struct word_struct*)malloc(sizeof(struct word_struct));
        assert(word_struct);

        word_struct->key = (char *)malloc((strlen(word_string) + 1) * sizeof(char)); // is strlen allowed to be in here
        strcpy(word_struct->key, word_string); // not allowed to do direct string assignment
        word_struct->key[strlen(word_string)] = '\0';
        
        word_struct->occurences = 1;

        wc->hash_array[index] = word_struct;
    }
    
    //if(wc->hash_array[index] != NULL)
        //printf("w:%s i:%ld c:%d\n", wc->hash_array[index]->key, index, wc->hash_array[index]->occurences);
}

struct wc *wc_init(char *word_array, long size) {
    long INT_MAX = 2147000;
   
    struct wc *wc;
    
    //printf("69\n");
    
    wc = (struct wc *)malloc(sizeof(struct wc)); // this is the hash array 
    assert(wc);

    // count size of words
//    for(int i = 0; i < size; i++)
//        if(isspace(word_array[i]))
//                hash_array_size++;

    hash_array_size = INT_MAX; // for good measure
    
    // now size the hash array
    wc->hash_array = (struct word_struct**)malloc(hash_array_size * sizeof(struct word_struct*));

    // initialize each element in array to point to null 
    for(int i = 0; i < hash_array_size; i++)
        wc->hash_array[i] = NULL;     
    
//    for(int i = 0; i < hash_array_size; i++)
//        wc->hash_array = NULL;

    bool word_start = false;
    long start_index = 0;
    long end_index = 0;
    bool space_found = false;
    
    for(long i = 0; i < size; i++) {
        
        if(isspace(word_array[i]) && space_found == false) { // space           
            // extract the word
            long word_length = (end_index - start_index) + 1; // word_length should at least be one
            char word_string[word_length + 1]; // + 1 for null terminator
            
            for(long j = 0; j < word_length; j++) {
                word_string[j] = word_array[j + start_index];
            }
            
            word_string[word_length] = '\0'; // null terminator
            
            //
            insert(wc, word_string);
            //printf("69\n");
              
            word_start = false;
            space_found = true;
        }
        else if (!isspace(word_array[i])) {
            space_found = false;

            if(word_start == false) {
                word_start = true;
                start_index = i; // start index of the word
                end_index = i;
            }
            else {
                end_index++;
            }
        }
    }

    return wc;
}

void wc_output(struct wc *wc) {
    if(wc == NULL && wc->hash_array)
            return;

    for(int i = 0; i < hash_array_size; i++) {
        if(wc->hash_array[i] != NULL) 
            printf("%s:%d\n", wc->hash_array[i]->key, wc->hash_array[i]->occurences);
    }
}

void wc_destroy(struct wc *wc) {
    // free each element of the hash_array 
    for(int i = 0; i < hash_array_size; i++) {
      if(wc->hash_array[i] != NULL) {
          if(wc->hash_array[i]->key != NULL)
              free(wc->hash_array[i]->key);
          free(wc->hash_array[i]);
      }
    }

    free(wc->hash_array);
    free(wc);
}
