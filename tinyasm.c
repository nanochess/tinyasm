/*
 ** TinyASM - 8086/8088 assembler for DOS
 **
 ** by Oscar Toledo G.
 **
 ** Creation date: Oct/01/2019.
 */

#include <stdio.h>
#include <ctype.h>

char *input_filename;
FILE *input;
char *output_filename;
FILE *output;

char line[256];
char part[256];
char *p;

int errors;

struct label {
    struct label *prev;
    int value;
    char name[1];
};

struct label *label_list;
struct label *last_label;

extern char *instruction_set[];

char *reg1[8] = {
    "AL",
    "CL",
    "DL",
    "BL",
    "AH",
    "CH",
    "DH",
    "BH"
};

char *reg2[8] = {
    "AX",
    "CX",
    "DX",
    "BX",
    "SP",
    "BP",
    "SI",
    "DI"
};

char *addr1[8] = {
    "BX+SI",
    "BX+DI",
    "BP+SI",
    "BP+DI",
    "SI",
    "DI",
    "BP",
    "BX"
};

int assembler_step;
int position;

int instruction_addressing;
int instruction_register;
int instruction_value;
int instruction_value2;

struct label *define_label(name, value)
    char *name;
    int value;
{
    struct label *label;
    
    label = malloc(sizeof(struct label) + strlen(name));
    if (label == NULL)
        return NULL;
    label->prev = label_list;
    label->value = value;
    strcpy(label->name, name);
    return label;
}

/*
 ** Avoid spaces
 */
char *avoid_spaces(p)
    char *p;
{
    while (isspace(*p))
        p++;
    return p;
}

/*
 ** Match addressing
 */
char *match_addressing(p, width, bits)
    char *p;
    int width;
    int *bits;
{
    p = avoid_spaces(p);
    if (*p == '[') {
        p = avoid_spaces(p + 1);
    }
    return NULL;
}

/*
 ** Match register
 */
char *match_register(p, width, value)
    char *p;
    int width;
    int *value;
{
    char reg[3];
    int c;
    
    p = avoid_spaces(p);
    reg[0] = toupper(p[0]);
    reg[1] = toupper(p[1]);
    reg[2] = '\0';
    if (width == 8) {
        for (c = 0; c < 8; c++)
            if (strcmp(reg, reg1[c]) == 0)
                break;
        if (c < 8) {
            *value = c;
            return p + 2;
        }
    } else {
        for (c = 0; c < 8; c++)
            if (strcmp(reg, reg2[c]) == 0)
                break;
        if (c < 8) {
            *value = c;
            return p + 2;
        }
    }
    return NULL;
}

/*
 ** Match expression
 */
char *match_expression(p, value)
    char *p;
    int *value;
{
    int number;
    int c;
    
    p = avoid_spaces(p);
    if (p[0] == '0' && tolower(p[1]) == 'x' && isxdigit(p[2])) {	/* Hexadecimal */
        p += 2;
        number = 0;
        while (isxdigit(p[0])) {
            c = toupper(p[0]);
            c = c - '0';
            if (c > 9)
                c -= 7;
            number = (number << 4) | c;
            p++;
        }
        *value = number;
        return p;
    } else if (isdigit(*p)) {
        number = 0;
        while (isdigit(p[0])) {
            c = p[0] - '0';
            number = number * 10 + c;
            p++;
        }
        *value = number;
        return p;
    }
    return NULL;
}

/*
 ** Search for a match with instruction
 */
char *match(p, pattern, decode)
    char *p;
    char *pattern;
    char *decode;
{
    char *p2;
    int c;
    int d;
    int bit;
    
    while (*pattern) {
        fputc(*pattern, stdout);
        if (*pattern == '%') {	/* Special */
            pattern++;
            if (*pattern == 'd') {
                pattern++;
                if (*pattern == 'w') {
                    pattern++;
                    if (memcmp(p, "WORD", 4) == 0 && isspace(p[4])) {
                        p2 = p + 4;
                        while (isspace(*p2))
                            p2++;
                        if (*p2 != '[')
                            return NULL;
                    }
                } else if (*pattern == 'b') {
                    pattern++;
                    if (memcmp(p, "BYTE", 4) == 0 && isspace(p[4])) {
                        p2 = p + 4;
                        while (isspace(*p2))
                            p2++;
                        if (*p2 != '[')
                            return NULL;
                    }
                }
                if (*pattern == '8') {
                    pattern++;
                    p2 = match_addressing(p, 8, &instruction_addressing);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else if (*pattern == '1' && pattern[1] == '6') {
                    pattern += 2;
                    p2 = match_addressing(p, 16, &instruction_addressing);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 'r') {
                pattern++;
                if (*pattern == '8') {
                    pattern++;
                    p2 = match_register(p, 8, &instruction_register);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else if (*pattern == '1' && pattern[1] == '6') {
                    pattern += 2;
                    p2 = match_register(p, 16, &instruction_register);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 'i') {
                pattern++;
                if (*pattern == '8') {
                    pattern++;
                    p2 = match_expression(p, &instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else if (*pattern == '1' && pattern[1] == '6') {
                    pattern += 2;
                    p2 = match_expression(p, &instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 'a') {
                pattern++;
                if (*pattern == '8') {
                    pattern++;
                    p2 = match_expression(p, &instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else if (*pattern == '1' && pattern[1] == '6') {
                    pattern += 2;
                    p2 = match_expression(p, &instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 's') {
                pattern++;
                if (*pattern == '8') {
                    pattern++;
                    p2 = match_expression(p, &instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 'f') {
                pattern++;
                if (*pattern == '3' && pattern[1] == '2') {
                    pattern += 2;
                    p2 = match_expression(p, &instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    if (*p2 != ':')
                        return NULL;
                    p = p2 + 1;
                    p2 = match_expression(p, &instruction_value2);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else {
                return NULL;
            }
            continue;
        }
        if (toupper(*p) != *pattern)
            return NULL;
        p++;
        pattern++;
    }

    /*
     ** Instruction properly matched, now generate binary
     */
    while (*decode) {
        decode = avoid_spaces(decode);
        if (decode[0] == 'x') { /* Byte */
            c = toupper(decode[1]);
            c -= '0';
            if (c > 9)
                c -= 7;
            d = toupper(decode[2]);
            d -= '0';
            if (d > 9)
                d -= 7;
            c = (c << 4) | d;
            if (assembler_step == 2)
                fputc(c, output);
            position++;
            decode += 3;
        } else {    /* Binary */
            if (*decode == 'b')
                decode++;
            bit = 0;
            c = 0;
            while (bit < 8) {
                if (decode[0] == '0') {
                    decode++;
                    bit++;
                } else if (decode[0] == '1') {
                    c |= 0x80 >> bit;
                    decode++;
                    bit++;
                } else if (decode[0] == '%') {
                    decode++;
                    if (decode[0] == 'r') {
                        decode++;
                        if (decode[0] == '8')
                            decode++;
                        else if (decode[0] == '1' && decode[1] == '6')
                            decode += 2;
                        c |= instruction_register << (5 - bit);
                        bit += 3;
                    } else {
                        fprintf(stderr, "decode: internal error 2\n");
                    }
                } else {
                    fprintf(stderr, "decode: internal error 1\n");
                    break;
                }
            }
            if (assembler_step == 2)
                fputc(c, output);
        }
    }
    return p;
}

/*
 ** Make a string lowercase
 */
void to_lowercase(p)
    char *p;
{
    while (*p) {
        *p = tolower(*p);
        p++;
    }
}

/*
 ** Separate a portion of entry up to the first space
 */
void separate(void)
{
    char *p2;
    
    while (*p && isspace(*p))
        p++;
    p2 = part;
    while (*p && !isspace(*p) && *p != ';')
        *p2++ = *p++;
    while (*p && isspace(*p))
        p++;
    *p2 = '\0';
}

/*
 ** Do an assembler step
 */
void do_assembly()
{
    int c;
    char *p2;
    char *p3;
    
    input = fopen(input_filename, "r");
    if (input == NULL) {
        fprintf(stderr, "Error: cannot open '%s' for input\n", input_filename);
        errors++;
        return;
    }
    while (fgets(line, sizeof(line) - 1, input)) {
        p = line;
        c = 0;
        while (*p) {
            if (*p == '"' && *(p - 1) != '\\')
                c = !c;
            if (c == 0) {
                *p = toupper(*p);
                p++;
            } else {
                p++;
            }
        }
        
        p = line;
        separate();
        if (part[0] != '\0' && part[strlen(part) - 1] == ':') {	/* Label */
            part[strlen(part) - 1] = '\0';
            last_label = define_label(part, position);
            separate();
        }
        while (part[0]) {
            c = 0;
            while (instruction_set[c] != NULL) {
                if (strcmp(part, instruction_set[c]) == 0) {
                    p2 = instruction_set[c];
                    while (*p2++) ;
                    p3 = p2;
                    while (*p3++) ;
                    
                    p2 = match(p, p2, p3);
                    if (p2 != NULL) {
                        p = p2;
                        break;
                    }
                }
                c++;
            }
            if (instruction_set[c] == NULL) {
                fprintf(stderr, "Error: undefined instruction '%s'\n", part);
                errors++;
                break;
            } else {
                p = p2;
                separate();
            }
        }
    }
    fclose(input);
}

/*
 ** Main program
 */
int main(argc, argv)
    int argc;
    char *argv[];
{
    int c;
    int d;
    
    if (argc == 1) {
        fprintf(stderr, "Typical usage:\n");
        fprintf(stderr, "tinyasm -f bin input.asm -o input.bin\n");
        exit(1);
    }
    input_filename = NULL;
    output_filename = NULL;
    c = 1;
    while (c < argc) {
        if (argv[c][0] == '-') {
            d = tolower(argv[c][1]);
            if (d == 'f') {
                c++;
                if (c >= argc) {
                    fprintf(stderr, "Error: no argument for -f\n");
                } else {
                    to_lowercase(argv[c]);
                    if (strcmp(argv[c], "bin") != 0)
                        fprintf(stderr, "Error: only 'bin' supported for -f (it is '%s')\n", argv[c]);
                    c++;
                }
            } else if (d == 'o') {
                c++;
                if (c >= argc) {
                    fprintf(stderr, "Error: no argument for -o\n");
                } else if (output_filename != NULL) {
                    fprintf(stderr, "Error: already a -o argument is present\n");
                    c++;
                } else {
                    output_filename = argv[c];
                    c++;
                }
            } else {
                fprintf(stderr, "Error: unknown argument %s\n", argv[c]);
                c++;
            }
        } else {
            if (input_filename != NULL) {
                fprintf(stderr, "Error: more than one input file name: %s\n", argv[c]);
            } else {
                input_filename = argv[c];
            }
            c++;
        }
    }
    
    if (input_filename == NULL) {
        fprintf(stderr, "No input filename provided\n");
        exit(1);
    }
    assembler_step = 1;
    do_assembly();
    if (!errors) {
        if (output_filename == NULL) {
            fprintf(stderr, "No output filename provided\n");
            exit(1);
        }
        output = fopen(output_filename, "wb");
        if (output == NULL) {
            fprintf(stderr, "Error: couldn't open '%s' as output file\n", output_filename);
            exit(1);
        }
        assembler_step = 2;
        do_assembly();
        fclose(output);
    }
}
