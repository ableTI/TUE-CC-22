#include <stdio.h>
#include <stdlib.h>
#include <libpynq.h>
#include "fortune.h"

int main(void)
{
    pynq_init();

    printf("Initializing Fortune Generator...\n");

    // Initialize fortune generator (NULL auto-locates the fortunes file)
    if (!fortune_init(NULL)) {
        fprintf(stderr, "Error: Could not load fortune file.\n");
        pynq_destroy();
        return EXIT_FAILURE;
    }

    char quote[4096];
    if (fortune_get_random(quote, sizeof(quote))) {
        printf("\n================ RANDOM FORTUNE ================\n");
        printf("%s\n", quote);
        printf("================================================\n\n");
    } else {
        fprintf(stderr, "Error: Failed to fetch random fortune.\n");
    }

    fortune_cleanup();
    pynq_destroy();
    return EXIT_SUCCESS;
}
