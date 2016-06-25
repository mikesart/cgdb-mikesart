#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tokenizer.h"

static void usage(void)
{

    printf("tokenizer_driver <file> <c|d|go|rust|ada>\n");
    exit(-1);
}

int main(int argc, char **argv)
{
    struct tokenizer *t = tokenizer_init();
    int ret;
    enum tokenizer_language_support l = TOKENIZER_LANGUAGE_UNKNOWN;
    struct token_data tok_data;

    if (argc != 3)
        usage();

    if (strcmp(argv[2], "c") == 0)
        l = TOKENIZER_LANGUAGE_C;
    else if (strcmp(argv[2], "d") == 0)
        l = TOKENIZER_LANGUAGE_D;
    else if (strcmp(argv[2], "go") == 0)
        l = TOKENIZER_LANGUAGE_GO;
    else if (strcmp(argv[2], "rust") == 0)
        l = TOKENIZER_LANGUAGE_GO;
    else if (strcmp(argv[2], "ada") == 0)
        l = TOKENIZER_LANGUAGE_ADA;
    else
        usage();

//$ TODO:
#if 0
    if (tokenizer_set_file(t, argv[1], l) == -1) {
        printf("%s:%d tokenizer_set_file error\n", __FILE__, __LINE__);
        return -1;
    }
#endif

    while ((ret = tokenizer_get_token(t, &tok_data)) > 0)
    {

        printf("Token:\n");
        printf("\tNumber: %d\n", tok_data.e);
        printf("\tType: %s\n", tokenizer_get_printable_enum(tok_data.e));
        printf("\tData: %s\n", tok_data.data);
    }

    if (ret == 0)
        printf("finished!\n");
    else if (ret == -1)
        printf("Error!\n");

    return 0;
}
