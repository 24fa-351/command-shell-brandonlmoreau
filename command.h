#ifndef COMMAND_H
#define COMMAND_H

char** retrieveSystemPathList(void);
void freePathList(char** paths);

void parseAndExecuteCommandPipeline(const char* inputLine, char** pathList);

#endif 