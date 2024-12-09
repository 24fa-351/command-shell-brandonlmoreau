
#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

void initializeEnvironmentVariables(void);
void cleanupEnvironmentVariables(void);

void addEnvironmentVariable(const char* name, const char* value);
void removeEnvironmentVariable(const char* name);
const char* getEnvironmentVariableValue(const char* name);

#endif