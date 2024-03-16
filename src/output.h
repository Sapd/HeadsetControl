#pragma once

typedef struct {
    char* key;
    char* value;
} t_entry;

t_entry* new_entry(char* key, char* value);

typedef struct {
    t_entry* entries;
    int length;
} t_output;

t_output new_output();

void output_add(t_output* output, t_entry* entry);

void output_free(t_output* output);

char* gen_output(t_output* output);
