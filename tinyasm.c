/*
 ** TinyASM - 8086/8088 assembler for DOS
 **
 ** by Oscar Toledo G.
 **
 ** Creation date: Oct/01/2019.
 */

#include <stdio.h>
#include <ctype.h>

#define DEBUG

char *input_filename;
FILE *input;
int line_number;

char *output_filename;
FILE *output;

int assembler_step;
int start_address;
int address;

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
char undefined_name[MAX_SIZE];
char *prev_p;
char *p;

int errors;

struct label {
    struct label *prev;
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

/*
 ** Define a new label
 */
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
    label_list = label;
    return label;
}

/*
 ** Find a label
 */
struct label *find_label(name)
    char *name;
{
    struct label *explore;
    
    explore = label_list;
    while (explore != NULL) {
        if (strcmp(name, explore->name) == 0)
            return explore;
        explore = explore->prev;
    }
    return NULL;
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
        } else {    /* No valid register, try expression */
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
    } else {
        p = match_register(p, width, &reg);
        if (p == NULL)
            return NULL;
        *bits = 0xc0 | reg;
    }
    return p;
}

/*
 **
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
 ** Match expression
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
        if (*p == '|') {
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
        if (*p == '^') {
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
        if (*p == '&') {
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
        if (*p == '<' && p[1] == '<') {
            p += 2;
            value1 = *value;
            p = match_expression_level4(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 << *value;
        } else if (*p == '>' && p[1] == '>') {
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
        if (*p == '+') {
            p++;
            value1 = *value;
            p = match_expression_level5(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 + *value;
        } else if (*p == '-') {
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
        if (*p == '*') {
            p++;
            value1 = *value;
            p = match_expression_level6(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 * *value;
        } else if (*p == '/') {
            p++;
            value1 = *value;
            p = match_expression_level6(p, value);
            if (p == NULL)
                return NULL;
            if (*value == 0) {
                if (assembler_step == 2)
                    fprintf(stderr, "Error: division by zero\n");
                *value = 1;
            }
            *value = (unsigned) value1 / *value;
        } else if (*p == '%') {
            p++;
            value1 = *value;
            p = match_expression_level6(p, value);
            if (p == NULL)
                return NULL;
            if (*value == 0) {
                if (assembler_step == 2)
                    fprintf(stderr, "Error: modulo by zero\n");
                *value = 1;
            }
            *value = value1 % *value;
        } else {
            return p;
        }
    }
}

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
    if (p[0] == '0' && tolower(p[1]) == 'b') {
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
    if (isalpha(*p) || *p == '_') { /* Label */
        p2 = name;
        while (isalpha(*p) || isdigit(*p) || *p == '_')
            *p2++ = *p++;
        *p2 = '\0';
        for (c = 0; c < 16; c++)
            if (strcmp(name, reg1[c]) == 0)
                return NULL;
        label = find_label(name);
        if (label == NULL) {
            *value = 0;
            undefined++;
            strcpy(undefined_name, name);
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
        buf[0] = byte;
        /* Cannot use fputc because DeSmet C expands to CR LF */
        fwrite(buf, 1, 1, output);
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
    char buf[3];
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
            } else if (*pattern == 's') {
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
            } else if (*pattern == 'f') {
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
                                fprintf(stderr, "Error: short jump too long at line %d\n", line_number);
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
    while (*p && isspace(*p))
        p++;
    *p2 = '\0';
}

/*
 ** Check for end of line
 */
void check_end(p)
    char *p;
{
    p = avoid_spaces(p);
    if (*p && *p != ';')
        fprintf(stderr, "Error: extra characters at end of line %d\n", line_number);
}

/*
 ** Generate a message
 */
void message(error, message)
    int error;
    char *message;
{
    if (error)
        fprintf(stderr, "Error: %s at line %d\n", message, line_number);
    else
        fprintf(stderr, "Warning: %s at line %d\n", message, line_number);
}

/*
 ** Process an instruction
 */
void process_instruction()
{
    char *p2;
    char *p3;
    int c;
    
    if (strcmp(part, "DB") == 0) {
        while (1) {
            p = avoid_spaces(p);
            if (*p == '"') {    /* ASCII text */
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\') {
                        p++;
                        if (*p == '\'') {
                            emit_byte('\'');
                        } else if (*p == '\"') {
                            emit_byte('"');
                        } else if (*p == '\\') {
                            emit_byte('\\');
                        } else if (*p == 'a') {
                            emit_byte(0x07);
                        } else if (*p == 'b') {
                            emit_byte(0x08);
                        } else if (*p == 't') {
                            emit_byte(0x09);
                        } else if (*p == 'n') {
                            emit_byte(0x0a);
                        } else if (*p == 'v') {
                            emit_byte(0x0b);
                        } else if (*p == 'f') {
                            emit_byte(0x0c);
                        } else if (*p == 'r') {
                            emit_byte(0x0d);
                        } else if (*p == 'e') {
                            emit_byte(0x1b);
                        } else if (*p >= '0' && *p <= '7') {
                            c = 0;
                            while (*p >= '0' && *p <= '7') {
                                c = c * 8 + (*p - '0');
                                p++;
                            }
                            emit_byte(c);
                        } else {
                            p--;
                            fprintf(stderr, "Error: bad escape inside string\n");
                        }
                    } else {
                        emit_byte(*p);
                    }
                    p++;
                }
                if (*p) {
                    p++;
                } else {
                    fprintf(stderr, "Error: unterminated string\n");
                }
            } else {
                p2 = match_expression(p, &instruction_value);
                if (p2 == NULL) {
                    fprintf(stderr, "Error: bad expression\n");
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
    if (strcmp(part, "DW") == 0) {
        while (1) {
            p2 = match_expression(p, &instruction_value);
            if (p2 == NULL) {
                fprintf(stderr, "Error: bad expression\n");
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
            char m[256];
            
            sprintf(m, "Undefined instruction '%s %s'", part, p);
            message(1, m);
            errors++;
            break;
        } else {
            p = p2;
            separate();
        }
    }
}

/*
 ** Do an assembler step
 */
void do_assembly()
{
    int c;
    char *p2;
    char *p3;
    int first_time;
    int level;
    int avoid_level;
    int times;
    
    input = fopen(input_filename, "r");
    if (input == NULL) {
        fprintf(stderr, "Error: cannot open '%s' for input\n", input_filename);
        errors++;
        return;
    }
    first_time = 1;
    level = 0;
    avoid_level = -1;
    line_number = 0;
    while (fgets(line, sizeof(line) - 1, input)) {
        line_number++;
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
        if (p > line && *(p - 1) == '\n')
            p--;
        *p = '\0';
        
        p = line;
        separate();
        if (part[0] == '\0' && (*p == '\0' || *p == ';'))    /* Empty line */
            continue;
        if (part[0] != '\0' && part[strlen(part) - 1] == ':') {	/* Label */
            part[strlen(part) - 1] = '\0';
            strcpy(name, part);
            separate();
            if (avoid_level == -1 || level < avoid_level) {
                if (strcmp(part, "EQU") != 0) {
                    if (first_time == 1) {
#ifdef DEBUG
                        fprintf(stderr, "First time '%s' at line %d\n", line, line_number);
#endif
                        first_time = 0;
                        address = 0x0100;
                        start_address = 0x0100;
                    }
                }
                last_label = define_label(name, address);
            }
        }
        if (strcmp(part, "%IF") == 0) {
            level++;
            if (avoid_level != -1 && level >= avoid_level)
                continue;
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
            continue;
        }
        if (strcmp(part, "%IFDEF") == 0) {
            level++;
            if (avoid_level != -1 && level >= avoid_level)
                continue;
            separate();
            if (find_label(part) != NULL) {
                ;
            } else {
                avoid_level = level;
            }
            check_end(p);
            continue;
        }
        if (strcmp(part, "%IFNDEF") == 0) {
            level++;
            if (avoid_level != -1 && level >= avoid_level)
                continue;
            separate();
            if (find_label(part) == NULL) {
                ;
            } else {
                avoid_level = level;
            }
            check_end(p);
            continue;
        }
        if (strcmp(part, "%ELSE") == 0) {
            if (avoid_level != -1 && level > avoid_level)
                continue;
            if (avoid_level == level) {
                avoid_level = -1;
            } else if (avoid_level == -1) {
                avoid_level = level;
            }
            check_end(p);
            continue;
        }
        if (strcmp(part, "%ENDIF") == 0) {
            if (avoid_level == level)
                avoid_level = -1;
            level--;
            check_end(p);
            continue;
        }
        if (avoid_level != -1 && level >= avoid_level) {
#ifdef DEBUG
            /*fprintf(stderr, "Avoiding '%s'\n", line);*/
#endif
            continue;
        }
        if (strcmp(part, "USE16") == 0) {
            continue;
        }
        if (strcmp(part, "CPU") == 0) {
            p = avoid_spaces(p);
            if (memcmp(p, "8086", 4) == 0)
                continue;
            else
                message(1, "Unsupported processor requested");
            continue;
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
            continue;
        }
        if (strcmp(part, "EQU") == 0) {
            p2 = match_expression(p, &instruction_value);
            if (p2 == NULL) {
                message(1, "bad expression");
            } else {
                if (last_label == NULL)
                    message(1, "no label associated to EQU");
                else {
#ifdef DEBUG
                    /*fprintf(stderr, "Debug: label %s assigned %d\n", last_label->name, instruction_value);*/
#endif
                    last_label->value = instruction_value;
                }
                check_end(p2);
            }
            continue;
        }
        if (first_time == 1) {
#ifdef DEBUG
            fprintf(stderr, "First time '%s' at line %d\n", line, line_number);
#endif
            first_time = 0;
            address = 0x0100;
            start_address = 0x0100;
        }
        times = 1;
        if (strcmp(part, "TIMES") == 0) {
            undefined = 0;
            p2 = match_expression(p, &instruction_value);
            if (p2 == NULL) {
                message(1, "bad expression");
                continue;
            }
            if (undefined) {
                message(1, "non-constant expression");
                continue;
            }
            times = instruction_value;
            p = p2;
            separate();
        }
        p3 = prev_p;
        while (times) {
            p = p3;
            separate();
            process_instruction();
            times--;
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
    input_filename = NULL;
    output_filename = NULL;
    c = 1;
    while (c < argc) {
        if (argv[c][0] == '-') {    /* All arguments start with dash */
            d = tolower(argv[c][1]);
            if (d == 'f') { /* Format */
                c++;
                if (c >= argc) {
                    fprintf(stderr, "Error: no argument for -f\n");
                } else {
                    to_lowercase(argv[c]);
                    if (strcmp(argv[c], "bin") != 0)
                        fprintf(stderr, "Error: only 'bin' supported for -f (it is '%s')\n", argv[c]);
                    c++;
                }
            } else if (d == 'o') {  /* Object file name */
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
    
    /*
     ** Do first step of assembly
     */
    assembler_step = 1;
    do_assembly();
    if (!errors) {
        
        /*
         ** Do second step of assembly and generate final output
         */
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
