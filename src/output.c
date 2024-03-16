#include "output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

t_entry* new_entry(char* key, char* value)
{
    t_entry* entry = malloc(sizeof(t_entry));
    entry->key     = strdup(key);
    entry->value   = strdup(value);
    return entry;
}

t_output new_output()
{
    t_output output;
    output.entries = malloc(0);
    output.length  = 0;
    return output;
}

void output_add(t_output* output, t_entry* entry)
{
    output->length++;
    output->entries                     = realloc(output->entries, output->length * sizeof(t_entry));
    output->entries[output->length - 1] = *entry;
}

void output_free(t_output* output)
{
    free(output->entries);
    output->length = 0;
}

char* gen_output(t_output* output)
{
    if (output->length == 0) {
        return strdup("{}");
    }

    size_t max_length = 2; // For '{' and '}'
    for (size_t i = 0; i < output->length; i++) {
        max_length += strlen(output->entries[i].key) + strlen(output->entries[i].value) + 5; // 5 for quotes, colon, and comma
    }

    char* result = (char*)malloc(max_length + 1); // Allocate memory for the result string
    if (result == NULL) {
        // Handle memory allocation failure
        return NULL;
    }

    strcpy(result, "{");
    for (size_t i = 0; i < output->length; i++) {
        char entry[64];
        sprintf(entry, "\"%s\":%s,", output->entries[i].key, output->entries[i].value);
        strcat(result, entry);
    }

    result[strlen(result) - 1] = '}'; // Replace the last comma with '}'
    char* dup_result           = strdup(result);
    free(result);
    return dup_result;
}
