/*
 ** TinyASM - 8086/8088 assembler for DOS
 **
 ** by Oscar Toledo G.
 **
 ** Creation date: Oct/01/2019.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEBUG

char *input_filename;
int line_number;

char *output_filename;
FILE *output;

char *listing_filename;
FILE *listing;

int assembler_step;
int default_start_address;
int start_address;
int address;
int first_time;

int instruction_addressing;
int instruction_offset;
int instruction_offset_width;

int instruction_register;

int instruction_value;
int instruction_value2;

#define MAX_SIZE        256

char line[MAX_SIZE];
char part[MAX_SIZE];
char name[MAX_SIZE];
char expr_name[MAX_SIZE];
char undefined_name[MAX_SIZE];
char global_label[MAX_SIZE];
char *prev_p;
char *p;

char *g;
char generated[8];

int errors;
int warnings;
int bytes;
int change;
int change_number;

struct label {
    struct label *left;
    struct label *right;
    int value;
    char name[1];
};

struct label *label_list;
struct label *last_label;
int undefined;

extern char *instruction_set[];

char *reg1[16] = {
    "AL",
    "CL",
    "DL",
    "BL",
    "AH",
    "CH",
    "DH",
    "BH",
    "AX",
    "CX",
    "DX",
    "BX",
    "SP",
    "BP",
    "SI",
    "DI"
};

void message();
char *match_register(), *match_expression(),
     *match_expression_level1(), *match_expression_level2(),
     *match_expression_level3(), *match_expression_level4(),
     *match_expression_level5(), *match_expression_level6();

#ifdef __DESMET__
/* Work around bug in DeSmet 3.1N runtime: closeall() overflows buffer and clobbers exit status */
#define exit(status)	_exit(status)
#endif

/*
 ** Define a new label
 */
struct label *define_label(name, value)
    char *name;
    int value;
{
    struct label *label;
    struct label *explore;
    int c;
    
    /* Allocate label */
    label = malloc(sizeof(struct label) + strlen(name));
    if (label == NULL) {
        fprintf(stderr, "Out of memory for label\n");
        exit(1);
        return NULL;
    }
    
    /* Fill label */
    label->left = NULL;
    label->right = NULL;
    label->value = value;
    strcpy(label->name, name);
    
    /* Populate binary tree */
    if (label_list == NULL) {
        label_list = label;
    } else {
        explore = label_list;
        while (1) {
            c = strcmp(label->name, explore->name);
            if (c < 0) {
                if (explore->left == NULL) {
                    explore->left = label;
                    break;
                }
                explore = explore->left;
            } else if (c > 0) {
                if (explore->right == NULL) {
                    explore->right = label;
                    break;
                }
                explore = explore->right;
            }
        }
    }
    return label;
}

/*
 ** Find a label
 */
struct label *find_label(name)
    char *name;
{
    struct label *explore;
    int c;
    
    /* Follows a binary tree */
    explore = label_list;
    while (explore != NULL) {
        c = strcmp(name, explore->name);
        if (c == 0)
            return explore;
        if (c < 0)
            explore = explore->left;
        else
            explore = explore->right;
    }
    return NULL;
}

/*
 ** Sort recursively labels (already done by binary tree)
 */
void sort_labels(node)
    struct label *node;
{
    if (node->left != NULL)
        sort_labels(node->left);
    fprintf(listing, "%-20s %04x\n", node->name, node->value);
    if (node->right != NULL)
        sort_labels(node->right);
}

/*
 ** Avoid spaces in input
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
char *match_addressing(p, width)
    char *p;
    int width;
{
    int reg;
    int reg2;
    char *p2;
    int *bits;
    
    bits = &instruction_addressing;
    instruction_offset = 0;
    instruction_offset_width = 0;
    
    p = avoid_spaces(p);
    if (*p == '[') {
        p = avoid_spaces(p + 1);
        p2 = match_register(p, 16, &reg);
        if (p2 != NULL) {
            p = avoid_spaces(p2);
            if (*p == ']') {
                p++;
                if (reg == 3) {   /* BX */
                    *bits = 0x07;
                } else if (reg == 5) {  /* BP */
                    *bits = 0x46;
                    instruction_offset = 0;
                    instruction_offset_width = 1;
                } else if (reg == 6) {  /* SI */
                    *bits = 0x04;
                } else if (reg == 7) {  /* DI */
                    *bits = 0x05;
                } else {    /* Not valid */
                    return NULL;
                }
            } else if (*p == '+' || *p == '-') {
                if (*p == '+') {
                    p = avoid_spaces(p + 1);
                    p2 = match_register(p, 16, &reg2);
                } else {
                    p2 = NULL;
                }
                if (p2 != NULL) {
                    if ((reg == 3 && reg2 == 6) || (reg == 6 && reg2 == 3)) {   /* BX+SI / SI+BX */
                        *bits = 0x00;
                    } else if ((reg == 3 && reg2 == 7) || (reg == 7 && reg2 == 3)) {    /* BX+DI / DI+BX */
                        *bits = 0x01;
                    } else if ((reg == 5 && reg2 == 6) || (reg == 6 && reg2 == 5)) {    /* BP+SI / SI+BP */
                        *bits = 0x02;
                    } else if ((reg == 5 && reg2 == 7) || (reg == 7 && reg2 == 5)) {    /* BP+DI / DI+BP */
                        *bits = 0x03;
                    } else {    /* Not valid */
                        return NULL;
                    }
                    p = avoid_spaces(p2);
                    if (*p == ']') {
                        p++;
                    } else if (*p == '+' || *p == '-') {
                        p2 = match_expression(p, &instruction_offset);
                        if (p2 == NULL)
                            return NULL;
                        p = avoid_spaces(p2);
                        if (*p != ']')
                            return NULL;
                        p++;
                        if (instruction_offset >= -0x80 && instruction_offset <= 0x7f) {
                            instruction_offset_width = 1;
                            *bits |= 0x40;
                        } else {
                            instruction_offset_width = 2;
                            *bits |= 0x80;
                        }
                    } else {    /* Syntax error */
                        return NULL;
                    }
                } else {
                    if (reg == 3) {   /* BX */
                        *bits = 0x07;
                    } else if (reg == 5) {  /* BP */
                        *bits = 0x06;
                    } else if (reg == 6) {  /* SI */
                        *bits = 0x04;
                    } else if (reg == 7) {  /* DI */
                        *bits = 0x05;
                    } else {    /* Not valid */
                        return NULL;
                    }
                    p2 = match_expression(p, &instruction_offset);
                    if (p2 == NULL)
                        return NULL;
                    p = avoid_spaces(p2);
                    if (*p != ']')
                        return NULL;
                    p++;
                    if (instruction_offset >= -0x80 && instruction_offset <= 0x7f) {
                        instruction_offset_width = 1;
                        *bits |= 0x40;
                    } else {
                        instruction_offset_width = 2;
                        *bits |= 0x80;
                    }
                }
            } else {    /* Syntax error */
                return NULL;
            }
        } else {    /* No valid register, try expression (absolute addressing) */
            p2 = match_expression(p, &instruction_offset);
            if (p2 == NULL)
                return NULL;
            p = avoid_spaces(p2);
            if (*p != ']')
                return NULL;
            p++;
            *bits = 0x06;
            instruction_offset_width = 2;
        }
    } else {    /* Register */
        p = match_register(p, width, &reg);
        if (p == NULL)
            return NULL;
        *bits = 0xc0 | reg;
    }
    return p;
}

/*
 ** Check for a label character
 */
int islabel(c)
    int c;
{
    return isalpha(c) || isdigit(c) || c == '_' || c == '.';
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
    if (!isalpha(p[0]) || !isalpha(p[1]) || islabel(p[2]))
        return NULL;
    reg[0] = p[0];
    reg[1] = p[1];
    reg[2] = '\0';
    if (width == 8) {   /* 8-bit */
        for (c = 0; c < 8; c++)
            if (strcmp(reg, reg1[c]) == 0)
                break;
        if (c < 8) {
            *value = c;
            return p + 2;
        }
    } else {    /* 16-bit */
        for (c = 0; c < 8; c++)
            if (strcmp(reg, reg1[c + 8]) == 0)
                break;
        if (c < 8) {
            *value = c;
            return p + 2;
        }
    }
    return NULL;
}

/*
 ** Read character for string or character literal
 */
char *read_character(p, c)
    char *p;
    int *c;
{
    if (*p == '\\') {
        p++;
        if (*p == '\'') {
            *c = '\'';
            p++;
        } else if (*p == '\"') {
            *c = '"';
            p++;
        } else if (*p == '\\') {
            *c = '\\';
            p++;
        } else if (*p == 'a') {
            *c = 0x07;
            p++;
        } else if (*p == 'b') {
            *c = 0x08;
            p++;
        } else if (*p == 't') {
            *c = 0x09;
            p++;
        } else if (*p == 'n') {
            *c = 0x0a;
            p++;
        } else if (*p == 'v') {
            *c = 0x0b;
            p++;
        } else if (*p == 'f') {
            *c = 0x0c;
            p++;
        } else if (*p == 'r') {
            *c = 0x0d;
            p++;
        } else if (*p == 'e') {
            *c = 0x1b;
            p++;
        } else if (*p >= '0' && *p <= '7') {
            *c = 0;
            while (*p >= '0' && *p <= '7') {
                *c = *c * 8 + (*p - '0');
                p++;
            }
        } else {
            p--;
            message(1, "bad escape inside string");
        }
    } else {
        *c = *p;
        p++;
    }
    return p;
}

/*
 ** Match expression (top tier)
 */
char *match_expression(p, value)
    char *p;
    int *value;
{
    int value1;
    
    p = match_expression_level1(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '|') {    /* Binary OR */
            p++;
            value1 = *value;
            p = match_expression_level1(p, value);
            if (p == NULL)
                return NULL;
            *value |= value1;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression
 */
char *match_expression_level1(p, value)
    char *p;
    int *value;
{
    int value1;
    
    p = match_expression_level2(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '^') {    /* Binary XOR */
            p++;
            value1 = *value;
            p = match_expression_level2(p, value);
            if (p == NULL)
                return NULL;
            *value ^= value1;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression
 */
char *match_expression_level2(p, value)
    char *p;
    int *value;
{
    int value1;
    
    p = match_expression_level3(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '&') {    /* Binary AND */
            p++;
            value1 = *value;
            p = match_expression_level3(p, value);
            if (p == NULL)
                return NULL;
            *value &= value1;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression
 */
char *match_expression_level3(p, value)
    char *p;
    int *value;
{
    int value1;
    
    p = match_expression_level4(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '<' && p[1] == '<') { /* Shift to left */
            p += 2;
            value1 = *value;
            p = match_expression_level4(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 << *value;
        } else if (*p == '>' && p[1] == '>') {  /* Shift to right */
            p += 2;
            value1 = *value;
            p = match_expression_level4(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 >> *value;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression
 */
char *match_expression_level4(p, value)
    char *p;
    int *value;
{
    int value1;
    
    p = match_expression_level5(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '+') {    /* Add operator */
            p++;
            value1 = *value;
            p = match_expression_level5(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 + *value;
        } else if (*p == '-') { /* Subtract operator */
            p++;
            value1 = *value;
            p = match_expression_level5(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 - *value;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression
 */
char *match_expression_level5(p, value)
    char *p;
    int *value;
{
    int value1;
    
    p = match_expression_level6(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '*') {    /* Multiply operator */
            p++;
            value1 = *value;
            p = match_expression_level6(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 * *value;
        } else if (*p == '/') { /* Division operator */
            p++;
            value1 = *value;
            p = match_expression_level6(p, value);
            if (p == NULL)
                return NULL;
            if (*value == 0) {
                if (assembler_step == 2)
                    message(1, "division by zero");
                *value = 1;
            }
            *value = (unsigned) value1 / *value;
        } else if (*p == '%') { /* Modulo operator */
            p++;
            value1 = *value;
            p = match_expression_level6(p, value);
            if (p == NULL)
                return NULL;
            if (*value == 0) {
                if (assembler_step == 2)
                    message(1, "modulo by zero");
                *value = 1;
            }
            *value = value1 % *value;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression (bottom tier)
 */
char *match_expression_level6(p, value)
    char *p;
    int *value;
{
    int number;
    int c;
    char *p2;
    struct label *label;
    
    p = avoid_spaces(p);
    if (*p == '(') {    /* Handle parenthesized expressions */
        p++;
        p = match_expression(p, value);
        if (p == NULL)
            return NULL;
        p = avoid_spaces(p);
        if (*p != ')')
            return NULL;
        p++;
        return p;
    }
    if (*p == '-') {    /* Simple negation */
        p++;
        p = match_expression_level6(p, value);
        if (p == NULL)
            return NULL;
        *value = -*value;
        return p;
    }
    if (*p == '+') {    /* Unary */
        p++;
        p = match_expression_level6(p, value);
        if (p == NULL)
            return NULL;
        return p;
    }
    if (p[0] == '0' && tolower(p[1]) == 'b') {  /* Binary */
        p += 2;
        number = 0;
        while (p[0] == '0' || p[0] == '1' || p[0] == '_') {
            if (p[0] != '_') {
                number <<= 1;
                if (p[0] == '1')
                    number |= 1;
            }
            p++;
        }
        *value = number;
        return p;
    }
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
    }
    if (p[0] == '$' && isdigit(p[1])) {	/* Hexadecimal */
        /* This is nasm syntax, notice no letter is allowed after $ */
        /* So it's preferrable to use prefix 0x for hexadecimal */
        p += 1;
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
    }
    if (p[0] == '\'') { /* Character constant */
        p++;
        p = read_character(p, value);
        if (p[0] != '\'') {
            message(1, "Missing apostrophe");
        } else {
            p++;
        }
        return p;
    }
    if (isdigit(*p)) {   /* Decimal */
        number = 0;
        while (isdigit(p[0])) {
            c = p[0] - '0';
            number = number * 10 + c;
            p++;
        }
        *value = number;
        return p;
    }
    if (*p == '$' && p[1] == '$') { /* Start address */
        p += 2;
        *value = start_address;
        return p;
    }
    if (*p == '$') { /* Current address */
        p++;
        *value = address;
        return p;
    }
    if (isalpha(*p) || *p == '_' || *p == '.') { /* Label */
        if (*p == '.') {
            strcpy(expr_name, global_label);
            p2 = expr_name;
            while (*p2)
                p2++;
        } else {
            p2 = expr_name;
        }
        while (isalpha(*p) || isdigit(*p) || *p == '_' || *p == '.')
            *p2++ = *p++;
        *p2 = '\0';
        for (c = 0; c < 16; c++)
            if (strcmp(expr_name, reg1[c]) == 0)
                return NULL;
        label = find_label(expr_name);
        if (label == NULL) {
            *value = 0;
            undefined++;
            strcpy(undefined_name, expr_name);
        } else {
            *value = label->value;
        }
        return p;
    }
    return NULL;
}

/*
 ** Emit one byte to output
 */
void emit_byte(int byte)
{
    char buf[1];
    
    if (assembler_step == 2) {
        if (g != NULL && g < generated + sizeof(generated))
            *g++ = byte;
        buf[0] = byte;
        /* Cannot use fputc because DeSmet C expands to CR LF */
        fwrite(buf, 1, 1, output);
        bytes++;
    }
    address++;
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
    int qualifier;
    char *base;
    
    undefined = 0;
    while (*pattern) {
/*        fputc(*pattern, stdout);*/
        if (*pattern == '%') {	/* Special */
            pattern++;
            if (*pattern == 'd') {  /* Addressing */
                pattern++;
                qualifier = 0;
                if (memcmp(p, "WORD", 4) == 0 && !isalpha(p[4])) {
                    p = avoid_spaces(p + 4);
                    if (*p != '[')
                        return NULL;
                    qualifier = 16;
                } else if (memcmp(p, "BYTE", 4) == 0 && !isalpha(p[4])) {
                    p = avoid_spaces(p + 4);
                    if (*p != '[')
                        return NULL;
                    qualifier = 8;
                }
                if (*pattern == 'w') {
                    pattern++;
                    if (qualifier != 16 && match_register(p, 16, &d) == 0)
                        return NULL;
                } else if (*pattern == 'b') {
                    pattern++;
                    if (qualifier != 8 && match_register(p, 8, &d) == 0)
                        return NULL;
                } else {
                    if (qualifier == 8 && *pattern != '8')
                        return NULL;
                    if (qualifier == 16 && *pattern != '1')
                        return NULL;
                }
                if (*pattern == '8') {
                    pattern++;
                    p2 = match_addressing(p, 8);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else if (*pattern == '1' && pattern[1] == '6') {
                    pattern += 2;
                    p2 = match_addressing(p, 16);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 'r') {   /* Register */
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
            } else if (*pattern == 'i') {   /* Immediate */
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
            } else if (*pattern == 'a') {   /* Address for jump */
                pattern++;
                if (*pattern == '8') {
                    pattern++;
                    p = avoid_spaces(p);
                    qualifier = 0;
                    if (memcmp(p, "SHORT", 5) == 0 && isspace(p[5])) {
                        p += 5;
                        qualifier = 1;
                    }
                    p2 = match_expression(p, &instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    if (qualifier == 0) {
                        c = instruction_value - (address + 2);
                        if (undefined == 0 && (c < -128 || c > 127) && memcmp(decode, "xeb", 3) == 0)
                            return NULL;
                    }
                    p = p2;
                } else if (*pattern == '1' && pattern[1] == '6') {
                    pattern += 2;
                    p = avoid_spaces(p);
                    if (memcmp(p, "SHORT", 5) == 0 && isspace(p[5]))
                        p2 = NULL;
                    else
                        p2 = match_expression(p, &instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 's') {   /* Signed immediate */
                pattern++;
                if (*pattern == '8') {
                    pattern++;
                    p = avoid_spaces(p);
                    qualifier = 0;
                    if (memcmp(p, "BYTE", 4) == 0 && isspace(p[4])) {
                        p += 4;
                        qualifier = 1;
                    }
                    p2 = match_expression(p, &instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    if (qualifier == 0) {
                        c = instruction_value;
                        if (undefined != 0)
                            return NULL;
                        if (undefined == 0 && (c < -128 || c > 127))
                            return NULL;
                    }
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 'f') {   /* FAR pointer */
                pattern++;
                if (*pattern == '3' && pattern[1] == '2') {
                    pattern += 2;
                    p2 = match_expression(p, &instruction_value2);
                    if (p2 == NULL)
                        return NULL;
                    if (*p2 != ':')
                        return NULL;
                    p = p2 + 1;
                    p2 = match_expression(p, &instruction_value);
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
        if (*pattern == ',')    /* Allow spaces after comma */
            p = avoid_spaces(p);
        pattern++;
    }

    /*
     ** Instruction properly matched, now generate binary
     */
    base = decode;
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
            emit_byte(c);
            decode += 3;
        } else {    /* Binary */
            if (*decode == 'b')
                decode++;
            bit = 0;
            c = 0;
            d = 0;
            while (bit < 8) {
                if (decode[0] == '0') { /* Zero */
                    decode++;
                    bit++;
                } else if (decode[0] == '1') {  /* One */
                    c |= 0x80 >> bit;
                    decode++;
                    bit++;
                } else if (decode[0] == '%') {  /* Special */
                    decode++;
                    if (decode[0] == 'r') { /* Register field */
                        decode++;
                        if (decode[0] == '8')
                            decode++;
                        else if (decode[0] == '1' && decode[1] == '6')
                            decode += 2;
                        c |= instruction_register << (5 - bit);
                        bit += 3;
                    } else if (decode[0] == 'd') {  /* Addressing field */
                        if (decode[1] == '8')
                            decode += 2;
                        else
                            decode += 3;
                        if (bit == 0) {
                            c |= instruction_addressing & 0xc0;
                            bit += 2;
                        } else {
                            c |= instruction_addressing & 0x07;
                            bit += 3;
                            d = 1;
                        }
                    } else if (decode[0] == 'i' || decode[0] == 's') {
                        if (decode[1] == '8') {
                            decode += 2;
                            c = instruction_value;
                            break;
                        } else {
                            decode += 3;
                            c = instruction_value;
                            instruction_offset = instruction_value >> 8;
                            instruction_offset_width = 1;
                            d = 1;
                            break;
                        }
                    } else if (decode[0] == 'a') {
                        if (decode[1] == '8') {
                            decode += 2;
                            c = instruction_value - (address + 1);
                            if (assembler_step == 2 && (c < -128 || c > 127))
                                message(1, "short jump too long");
                            break;
                        } else {
                            decode += 3;
                            c = instruction_value - (address + 2);
                            instruction_offset = c >> 8;
                            instruction_offset_width = 1;
                            d = 1;
                            break;
                        }
                    } else if (decode[0] == 'f') {
                        decode += 3;
                        emit_byte(instruction_value);
                        c = instruction_value >> 8;
                        instruction_offset = instruction_value2;
                        instruction_offset_width = 2;
                        d = 1;
                        break;
                    } else {
                        fprintf(stderr, "decode: internal error 2\n");
                    }
                } else {
                    fprintf(stderr, "decode: internal error 1 (%s)\n", base);
                    break;
                }
            }
            emit_byte(c);
            if (d == 1) {
                d = 0;
                if (instruction_offset_width >= 1) {
                    emit_byte(instruction_offset);
                }
                if (instruction_offset_width >= 2) {
                    emit_byte(instruction_offset >> 8);
                }
            }
        }
    }
    if (assembler_step == 2) {
        if (undefined) {
            fprintf(stderr, "Error: undefined label '%s' at line %d\n", undefined_name, line_number);
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
    prev_p = p;
    p2 = part;
    while (*p && !isspace(*p) && *p != ';')
        *p2++ = *p++;
    *p2 = '\0';
    while (*p && isspace(*p))
        p++;
}

/*
 ** Check for end of line
 */
void check_end(p)
    char *p;
{
    p = avoid_spaces(p);
    if (*p && *p != ';') {
        fprintf(stderr, "Error: extra characters at end of line %d\n", line_number);
        errors++;
    }
}

/*
 ** Generate a message
 */
void message(error, message)
    int error;
    char *message;
{
    if (error) {
        fprintf(stderr, "Error: %s at line %d\n", message, line_number);
        errors++;
    } else {
        fprintf(stderr, "Warning: %s at line %d\n", message, line_number);
        warnings++;
    }
    if (listing != NULL) {
        if (error) {
            fprintf(listing, "Error: %s at line %d\n", message, line_number);
        } else {
            fprintf(listing, "Warning: %s at line %d\n", message, line_number);
        }
    }
}

/*
 ** Process an instruction
 */
void process_instruction()
{
    char *p2 = NULL;
    char *p3;
    int c;
    
    if (strcmp(part, "DB") == 0) {  /* Define byte */
        while (1) {
            p = avoid_spaces(p);
            if (*p == '"') {    /* ASCII text */
                p++;
                while (*p && *p != '"') {
                    p = read_character(p, &c);
                    emit_byte(c);
                }
                if (*p) {
                    p++;
                } else {
                    fprintf(stderr, "Error: unterminated string at line %d\n", line_number);
                }
            } else {
                p2 = match_expression(p, &instruction_value);
                if (p2 == NULL) {
                    fprintf(stderr, "Error: bad expression at line %d\n", line_number);
                    break;
                }
                emit_byte(instruction_value);
                p = p2;
            }
            p = avoid_spaces(p);
            if (*p == ',') {
                p++;
                continue;
            }
            check_end(p);
            break;
        }
        return;
    }
    if (strcmp(part, "DW") == 0) {  /* Define word */
        while (1) {
            p2 = match_expression(p, &instruction_value);
            if (p2 == NULL) {
                fprintf(stderr, "Error: bad expression at line %d\n", line_number);
                break;
            }
            emit_byte(instruction_value);
            emit_byte(instruction_value >> 8);
            p = avoid_spaces(p2);
            if (*p == ',') {
                p++;
                continue;
            }
            check_end(p);
            break;
        }
        return;
    }
    while (part[0]) {   /* Match against instruction set */
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
            char m[25 + MAX_SIZE];
            
            sprintf(m, "Undefined instruction '%s %s'", part, p);
            message(1, m);
            break;
        } else {
            p = p2;
            separate();
        }
    }
}

/*
 ** Reset current address.
 ** Called anytime the assembler needs to generate code.
 */
void reset_address()
{
    address = start_address = default_start_address;
}

/*
 ** Include a binary file
 */
void incbin(fname)
    char *fname;
{
    FILE *input;
    char buf[256];
    int size;
    int i;
    
    input = fopen(fname, "r");
    if (input == NULL) {
        sprintf(buf, "Error: Cannot open '%s' for input", fname);
        message(1, buf);
        return;
    }
    
    while (size = fread(buf, 1, 30, input)) {
        for (i = 0; i < size; i++) {
            emit_byte(buf[i]);
        }
    }
    
    fclose(input);
}

/*
 ** Do an assembler step
 */
void do_assembly(fname)
    char *fname;
{
    FILE *input;
    char *p2;
    char *p3;
    char *pfname;
    int level;
    int avoid_level;
    int times;
    int base;
    int pline;
    int include;
    int align;

    input = fopen(fname, "r");
    if (input == NULL) {
        fprintf(stderr, "Error: cannot open '%s' for input\n", fname);
        errors++;
        return;
    }

    pfname = input_filename;
    pline = line_number;
    input_filename = fname;
    level = 0;
    avoid_level = -1;
    global_label[0] = '\0';
    line_number = 0;
    base = 0;
    while (fgets(line, sizeof(line), input)) {
        line_number++;
        p = line;
        while (*p) {
            if (*p == '\'' && *(p - 1) != '\\') {
                p++;
                while (*p && *p != '\'' && *(p - 1) != '\\')
                    p++;
            } else if (*p == '"' && *(p - 1) != '\\') {
                p++;
                while (*p && *p != '"' && *(p - 1) != '\\')
                    p++;
            } else if (*p == ';') {
                while (*p)
                    p++;
                break;
            }
            *p = toupper(*p);
            p++;
        }
        if (p > line && *(p - 1) == '\n')
            p--;
        *p = '\0';

        base = address;
        g = generated;
        include = 0;

        while (1) {
            p = line;
            separate();
            if (part[0] == '\0' && (*p == '\0' || *p == ';'))    /* Empty line */
                break;
            if (part[0] != '\0' && part[strlen(part) - 1] == ':') {	/* Label */
                part[strlen(part) - 1] = '\0';
                if (part[0] == '.') {
                    strcpy(name, global_label);
                    strcat(name, part);
                } else {
                    strcpy(name, part);
                    strcpy(global_label, name);
                }
                separate();
                if (avoid_level == -1 || level < avoid_level) {
                    if (strcmp(part, "EQU") == 0) {
                        p2 = match_expression(p, &instruction_value);
                        if (p2 == NULL) {
                            message(1, "bad expression");
                        } else {
                            if (assembler_step == 1) {
                                if (find_label(name)) {
                                    char m[18 + MAX_SIZE];
                                    
                                    sprintf(m, "Redefined label '%s'", name);
                                    message(1, m);
                                } else {
                                    last_label = define_label(name, instruction_value);
                                }
                            } else {
                                last_label = find_label(name);
                                if (last_label == NULL) {
                                    char m[33 + MAX_SIZE];
                                    
                                    sprintf(m, "Inconsistency, label '%s' not found", name);
                                    message(1, m);
                                } else {
                                    if (last_label->value != instruction_value) {
#ifdef DEBUG
/*                                        fprintf(stderr, "Woops: label '%s' changed value from %04x to %04x\n", last_label->name, last_label->value, instruction_value);*/
#endif
                                        change = 1;
                                    }
                                    last_label->value = instruction_value;
                                }
                            }
                            check_end(p2);
                        }
                        break;
                    }
                    if (first_time == 1) {
#ifdef DEBUG
                        /*                        fprintf(stderr, "First time '%s' at line %d\n", line, line_number);*/
#endif
                        first_time = 0;
                        reset_address();
                    }
                    if (assembler_step == 1) {
                        if (find_label(name)) {
                            char m[18 + MAX_SIZE];
                            
                            sprintf(m, "Redefined label '%s'", name);
                            message(1, m);
                        } else {
                            last_label = define_label(name, address);
                        }
                    } else {
                        last_label = find_label(name);
                        if (last_label == NULL) {
                            char m[33 + MAX_SIZE];
                            
                            sprintf(m, "Inconsistency, label '%s' not found", name);
                            message(1, m);
                        } else {
                            if (last_label->value != address) {
#ifdef DEBUG
/*                                fprintf(stderr, "Woops: label '%s' changed value from %04x to %04x\n", last_label->name, last_label->value, address);*/
#endif
                                change = 1;
                            }
                            last_label->value = address;
                        }
                        
                    }
                }
            }
            if (strcmp(part, "%IF") == 0) {
                level++;
                if (avoid_level != -1 && level >= avoid_level)
                    break;
                undefined = 0;
                p = match_expression(p, &instruction_value);
                if (p == NULL) {
                    message(1, "Bad expression");
                } else if (undefined) {
                    message(1, "Undefined labels");
                }
                if (instruction_value != 0) {
                    ;
                } else {
                    avoid_level = level;
                }
                check_end(p);
                break;
            }
            if (strcmp(part, "%IFDEF") == 0) {
                level++;
                if (avoid_level != -1 && level >= avoid_level)
                    break;
                separate();
                if (find_label(part) != NULL) {
                    ;
                } else {
                    avoid_level = level;
                }
                check_end(p);
                break;
            }
            if (strcmp(part, "%IFNDEF") == 0) {
                level++;
                if (avoid_level != -1 && level >= avoid_level)
                    break;
                separate();
                if (find_label(part) == NULL) {
                    ;
                } else {
                    avoid_level = level;
                }
                check_end(p);
                break;
            }
            if (strcmp(part, "%ELSE") == 0) {
                if (avoid_level != -1 && level > avoid_level)
                    break;
                if (avoid_level == level) {
                    avoid_level = -1;
                } else if (avoid_level == -1) {
                    avoid_level = level;
                }
                check_end(p);
                break;
            }
            if (strcmp(part, "%ENDIF") == 0) {
                if (avoid_level == level)
                    avoid_level = -1;
                level--;
                check_end(p);
                break;
            }
            if (avoid_level != -1 && level >= avoid_level) {
#ifdef DEBUG
                /*fprintf(stderr, "Avoiding '%s'\n", line);*/
#endif
                break;
            }
            if (strcmp(part, "USE16") == 0) {
                break;
            }
            if (strcmp(part, "CPU") == 0) {
                p = avoid_spaces(p);
                if (memcmp(p, "8086", 4) != 0)
                    message(1, "Unsupported processor requested");
                break;
            }
            if (strcmp(part, "%INCLUDE") == 0) {
                separate();
                check_end(p);
                if (part[0] != '"' || part[strlen(part) - 1] != '"') {
                    message(1, "Missing quotes on %include");
                    break;
                }
                include = 1;
                break;
            }
            if (strcmp(part, "INCBIN") == 0) {
                separate();
                check_end(p);
                if (part[0] != '"' || part[strlen(part) - 1] != '"') {
                    message(1, "Missing quotes on incbin");
                    break;
                }
                include = 2;
                break;
            }
            if (strcmp(part, "ORG") == 0) {
                p = avoid_spaces(p);
                undefined = 0;
                p2 = match_expression(p, &instruction_value);
                if (p2 == NULL) {
                    message(1, "Bad expression");
                } else if (undefined) {
                    message(1, "Cannot use undefined labels");
                } else {
                    if (first_time == 1) {
                        first_time = 0;
                        address = instruction_value;
                        start_address = instruction_value;
                        base = address;
                    } else {
                        if (instruction_value < address) {
                            message(1, "Backward address");
                        } else {
                            while (address < instruction_value)
                                emit_byte(0);
                            
                        }
                    }
                    check_end(p2);
                }
                break;
            }
            if (strcmp(part, "ALIGN") == 0) {
                p = avoid_spaces(p);
                undefined = 0;
                p2 = match_expression(p, &instruction_value);
                if (p2 == NULL) {
                    message(1, "Bad expression");
                } else if (undefined) {
                    message(1, "Cannot use undefined labels");
                } else {
                    align = address / instruction_value;
                    align = align * instruction_value;
                    align = align + instruction_value;
		    while (address < align)
		        emit_byte(0x90);
                    check_end(p2);
                }
                break;
            }
            if (first_time == 1) {
#ifdef DEBUG
                /*fprintf(stderr, "First time '%s' at line %d\n", line, line_number);*/
#endif
                first_time = 0;
                reset_address();
            }
            times = 1;
            if (strcmp(part, "TIMES") == 0) {
                undefined = 0;
                p2 = match_expression(p, &instruction_value);
                if (p2 == NULL) {
                    message(1, "bad expression");
                    break;
                }
                if (undefined) {
                    message(1, "non-constant expression");
                    break;
                }
                times = instruction_value;
                p = p2;
                separate();
            }
            base = address;
            g = generated;
            p3 = prev_p;
            while (times) {
                p = p3;
                separate();
                process_instruction();
                times--;
            }
            break;
        }
        if (assembler_step == 2 && listing != NULL) {
            if (first_time)
                fprintf(listing, "      ");
            else
                fprintf(listing, "%04X  ", base);
            p = generated;
            while (p < g) {
                fprintf(listing, "%02X", *p++ & 255);
            }
            while (p < generated + sizeof(generated)) {
                fprintf(listing, "  ");
                p++;
            }
            fprintf(listing, "  %05d %s\n", line_number, line);
        }
        if (include == 1) {
            part[strlen(part) - 1] = '\0';
            do_assembly(part + 1);
        }
        if (include == 2) {
            part[strlen(part) - 1] = '\0';
            incbin(part + 1);
        }
    }
    fclose(input);
    line_number = pline;
    input_filename = pfname;
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
    char *p;
    char *ifname;
    
    /*
     ** If ran without arguments then show usage
     */
    if (argc == 1) {
        fprintf(stderr, "Typical usage:\n");
        fprintf(stderr, "tinyasm -f bin input.asm -o input.bin\n");
        exit(1);
    }
    
    /*
     ** Start to collect arguments
     */
    ifname = NULL;
    output_filename = NULL;
    listing_filename = NULL;
    default_start_address = 0;
    c = 1;
    while (c < argc) {
        if (argv[c][0] == '-') {    /* All arguments start with dash */
            d = tolower(argv[c][1]);
            if (d == 'f') { /* Format */
                c++;
                if (c >= argc) {
                    fprintf(stderr, "Error: no argument for -f\n");
                    exit(1);
                } else {
                    to_lowercase(argv[c]);
                    if (strcmp(argv[c], "bin") == 0) {
                        default_start_address = 0;
                    } else if (strcmp(argv[c], "com") == 0) {
                        default_start_address = 0x0100;
                    } else {
                        fprintf(stderr, "Error: only 'bin', 'com' supported for -f (it is '%s')\n", argv[c]);
                        exit(1);
                    }
                    c++;
                }
            } else if (d == 'o') {  /* Object file name */
                c++;
                if (c >= argc) {
                    fprintf(stderr, "Error: no argument for -o\n");
                    exit(1);
                } else if (output_filename != NULL) {
                    fprintf(stderr, "Error: already a -o argument is present\n");
                    exit(1);
                } else {
                    output_filename = argv[c];
                    c++;
                }
            } else if (d == 'l') {  /* Listing file name */
                c++;
                if (c >= argc) {
                    fprintf(stderr, "Error: no argument for -l\n");
                    exit(1);
                } else if (listing_filename != NULL) {
                    fprintf(stderr, "Error: already a -l argument is present\n");
                    exit(1);
                } else {
                    listing_filename = argv[c];
                    c++;
                }
            } else if (d == 'd') {  /* Define label */
                p = argv[c] + 2;
                while (*p && *p != '=') {
                    *p = toupper(*p);
                    p++;
                }
                if (*p == '=') {
                    *p++ = 0;
                    undefined = 0;
                    p = match_expression(p, &instruction_value);
                    if (p == NULL) {
                        fprintf(stderr, "Error: wrong label definition\n");
                        exit(1);
                    } else if (undefined) {
                        fprintf(stderr, "Error: non-constant label definition\n");
                        exit(1);
                    } else {
                        define_label(argv[c] + 2, instruction_value);
                    }
                }
                c++;
            } else {
                fprintf(stderr, "Error: unknown argument %s\n", argv[c]);
                exit(1);
            }
        } else {
            if (ifname != NULL) {
                fprintf(stderr, "Error: more than one input file name: %s\n", argv[c]);
                exit(1);
            } else {
                ifname = argv[c];
            }
            c++;
        }
    }
    
    if (ifname == NULL) {
        fprintf(stderr, "No input filename provided\n");
        exit(1);
    }
    
    /*
     ** Do first step of assembly
     */
    assembler_step = 1;
    first_time = 1;
    do_assembly(ifname);
    if (!errors) {
        
        /*
         ** Do second step of assembly and generate final output
         */
        if (output_filename == NULL) {
            fprintf(stderr, "No output filename provided\n");
            exit(1);
        }
        change_number = 0;
        do {
            change = 0;
            if (listing_filename != NULL) {
                listing = fopen(listing_filename, "w");
                if (listing == NULL) {
                    fprintf(stderr, "Error: couldn't open '%s' as listing file\n", output_filename);
                    exit(1);
                }
            }
            output = fopen(output_filename, "wb");
            if (output == NULL) {
                fprintf(stderr, "Error: couldn't open '%s' as output file\n", output_filename);
                exit(1);
            }
            assembler_step = 2;
            first_time = 1;
            do_assembly(ifname);
            
            if (listing != NULL && change == 0) {
                fprintf(listing, "\n%05d ERRORS FOUND\n", errors);
                fprintf(listing, "%05d WARNINGS FOUND\n\n", warnings);
                fprintf(listing, "%05d PROGRAM BYTES\n\n", bytes);
                if (label_list != NULL) {
                    fprintf(listing, "%-20s VALUE/ADDRESS\n\n", "LABEL");
                    sort_labels(label_list);
                }
            }
            fclose(output);
            if (listing_filename != NULL)
                fclose(listing);
            if (change) {
                change_number++;
                if (change_number == 5) {
                    fprintf(stderr, "Aborted: Couldn't stabilize moving label\n");
                    errors++;
                }
            }
            if (errors) {
                remove(output_filename);
                if (listing_filename != NULL)
                    remove(listing_filename);
                exit(1);
            }
        } while (change) ;

        exit(0);
    }

    exit(1);
}
