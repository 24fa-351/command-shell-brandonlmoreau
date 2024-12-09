#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include <direct.h>

#include "environment.h"
#include "command.h"

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        if (_stricmp(argv[1], "--help") == 0)
        {
            printf("Usage:\n");
            printf("  xsh              - Start the shell interactively.\n");
            printf("  xsh --help        - Show this help message.\n");
            printf("  xsh --run-tests   - Run unit tests.\n");
            printf("\nThis shell supports:\n");
            printf("  Built-ins: cd, pwd, set, unset, echo.\n");
            printf("  Variable substitution: $VAR.\n");
            printf("  Piping with '|', I/O redirection with '<' and '>'\n");
            printf("  Background execution with '&'.\n");
            return EXIT_SUCCESS;
        }
        else if (_stricmp(argv[1], "--run-tests") == 0)
        {
            initializeEnvironmentVariables();
            addEnvironmentVariable("TEST_VAR", "test_value");
            const char* testVal = getEnvironmentVariableValue("TEST_VAR");
            if (testVal == NULL || strcmp(testVal, "test_value") != 0)
            {
                fprintf(stderr, "Test FAILED: environment variable not set correctly.\n");
                cleanupEnvironmentVariables();
                return EXIT_FAILURE;
            }
            removeEnvironmentVariable("TEST_VAR");
            cleanupEnvironmentVariables();
            printf("All tests passed.\n");
            return EXIT_SUCCESS;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[1]);
            printf("Usage:\n");
            printf("  xsh              - Start the shell interactively.\n");
            printf("  xsh --help        - Show help message.\n");
            printf("  xsh --run-tests   - Run unit tests.\n");
            return EXIT_FAILURE;
        }
    }

    initializeEnvironmentVariables();
    char** pathList = retrieveSystemPathList();
    if (pathList == NULL)
    {
        fprintf(stderr, "Failed to retrieve system PATH.\n");
        cleanupEnvironmentVariables();
        return EXIT_FAILURE;
    }

    while(1)
    {
        printf("xsh# ");
        fflush(stdout);

        char inputLine[4096];
        if (fgets(inputLine, sizeof(inputLine), stdin) == NULL)
        {
            break; 
        }

       
        {
            int i = 0;
            while (inputLine[i] != '\0' && (inputLine[i] == ' ' ||
                inputLine[i] == '\t' || inputLine[i] == '\r' ||
                inputLine[i] == '\n'))
            {
                i++;
            }

            if (i > 0)
            {
                memmove(inputLine, inputLine + i, strlen(inputLine + i) + 1);
            }
        }

        if (_stricmp(inputLine, "exit\n") == 0 || _stricmp(inputLine, "quit\n") == 0)
        {
            break;
        }

        if (inputLine[0] != '\0')
        {
            parseAndExecuteCommandPipeline(inputLine, pathList);
        }
    }

    freePathList(pathList);
    cleanupEnvironmentVariables();
    return EXIT_SUCCESS;
}
