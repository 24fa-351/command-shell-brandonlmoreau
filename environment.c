#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "environment.h"

#ifndef MAX_VARIABLE_NAME_LENGTH
#define MAX_VARIABLE_NAME_LENGTH 256
#endif

typedef struct EnvironmentVariableEntry
{
    char* variableName;
    char* variableValue;
    struct EnvironmentVariableEntry* nextEntry;
} EnvironmentVariableEntry;

static EnvironmentVariableEntry* globalEnvironmentList = NULL;

void initializeEnvironmentVariables(void)
{
    globalEnvironmentList = NULL;
}

void cleanupEnvironmentVariables(void)
{
    EnvironmentVariableEntry* currentEntry = globalEnvironmentList;
    while (currentEntry != NULL)
    {
        EnvironmentVariableEntry* nextEntry = currentEntry->nextEntry;
        free(currentEntry->variableName);
        free(currentEntry->variableValue);
        free(currentEntry);
        currentEntry = nextEntry;
    }
    globalEnvironmentList = NULL;
}

void addEnvironmentVariable(const char* name, const char* value)
{
    removeEnvironmentVariable(name);

    EnvironmentVariableEntry* newEntry;
    newEntry = (EnvironmentVariableEntry*)malloc(sizeof(EnvironmentVariableEntry));
    if (!newEntry)
    {
        fprintf(stderr, "Memory allocation failed in addEnvironmentVariable.\n");
        return;
    }

    newEntry->variableName = _strdup(name);
    newEntry->variableValue = _strdup(value);
    newEntry->nextEntry = globalEnvironmentList;
    globalEnvironmentList = newEntry;
}

void removeEnvironmentVariable(const char* name)
{
    EnvironmentVariableEntry* prevEntry = NULL;
    EnvironmentVariableEntry* currentEntry = globalEnvironmentList;
    while (currentEntry != NULL)
    {
        if (_stricmp(currentEntry->variableName, name) == 0)
        {
            if (prevEntry != NULL)
            {
                prevEntry->nextEntry = currentEntry->nextEntry;
            }
            else
            {
                globalEnvironmentList = currentEntry->nextEntry;
            }
            free(currentEntry->variableName);
            free(currentEntry->variableValue);
            free(currentEntry);
            return;
        }
        prevEntry = currentEntry;
        currentEntry = currentEntry->nextEntry;
    }
}

const char* getEnvironmentVariableValue(const char* name)
{
    EnvironmentVariableEntry* currentEntry = globalEnvironmentList;
    while (currentEntry != NULL)
    {
        if (_stricmp(currentEntry->variableName, name) == 0)
        {
            return currentEntry->variableValue;
        }
        currentEntry = currentEntry->nextEntry;
    }
    return NULL;
}
