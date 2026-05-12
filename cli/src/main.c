#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/time.h>
#include <getopt.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "cisv/parser.h"
#include "cisv/writer.h"

#ifndef CISV_CLI_VERSION
#define CISV_CLI_VERSION "dev"
#endif

/**
 * SECURITY: Safe integer parsing with overflow protection.
 * Returns 0 on success, -1 on error (overflow, invalid input, negative when not allowed).
 */
static int safe_parse_int(const char *str, int *result, int allow_negative) {
    if (!str || !result || *str == '\0') {
        return -1;
    }

    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);

    // Check for conversion errors
    if (errno == ERANGE || val > INT_MAX || val < INT_MIN) {
        fprintf(stderr, "Error: Integer overflow in argument '%s'\n", str);
        return -1;
    }

    // Check for invalid characters
    if (*endptr != '\0') {
        fprintf(stderr, "Error: Invalid integer '%s'\n", str);
        return -1;
    }

    // Check for negative when not allowed
    if (!allow_negative && val < 0) {
        fprintf(stderr, "Error: Negative value not allowed '%s'\n", str);
        return -1;
    }

    *result = (int)val;
    return 0;
}

/**
 * SECURITY: Safe long parsing with overflow protection.
 */
static int safe_parse_long(const char *str, long *result, int allow_negative) {
    if (!str || !result || *str == '\0') {
        return -1;
    }

    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);

    // Check for conversion errors
    if (errno == ERANGE) {
        fprintf(stderr, "Error: Integer overflow in argument '%s'\n", str);
        return -1;
    }

    // Check for invalid characters
    if (*endptr != '\0') {
        fprintf(stderr, "Error: Invalid integer '%s'\n", str);
        return -1;
    }

    // Check for negative when not allowed
    if (!allow_negative && val < 0) {
        fprintf(stderr, "Error: Negative value not allowed '%s'\n", str);
        return -1;
    }

    *result = val;
    return 0;
}

/**
 * Parse byte sizes for resource flags. Accepted suffixes mirror the core
 * runtime parser: K/KB/KiB, M/MB/MiB, G/GB/GiB, and T/TB/TiB.
 */
static int safe_parse_size(const char *str, size_t *result) {
    if (!str || !result || *str == '\0') {
        return -1;
    }

    char *endptr;
    errno = 0;
    unsigned long long val = strtoull(str, &endptr, 10);
    if (errno == ERANGE || endptr == str || val == 0) {
        return -1;
    }

    unsigned long long multiplier = 1;
    if (*endptr != '\0') {
        if ((endptr[0] == 'k' || endptr[0] == 'K') && endptr[1] == '\0') {
            multiplier = 1024ULL;
        } else if ((endptr[0] == 'm' || endptr[0] == 'M') && endptr[1] == '\0') {
            multiplier = 1024ULL * 1024ULL;
        } else if ((endptr[0] == 'g' || endptr[0] == 'G') && endptr[1] == '\0') {
            multiplier = 1024ULL * 1024ULL * 1024ULL;
        } else if ((endptr[0] == 't' || endptr[0] == 'T') && endptr[1] == '\0') {
            multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
        } else if ((endptr[0] == 'k' || endptr[0] == 'K') &&
                   (endptr[1] == 'b' || endptr[1] == 'B') && endptr[2] == '\0') {
            multiplier = 1024ULL;
        } else if ((endptr[0] == 'm' || endptr[0] == 'M') &&
                   (endptr[1] == 'b' || endptr[1] == 'B') && endptr[2] == '\0') {
            multiplier = 1024ULL * 1024ULL;
        } else if ((endptr[0] == 'g' || endptr[0] == 'G') &&
                   (endptr[1] == 'b' || endptr[1] == 'B') && endptr[2] == '\0') {
            multiplier = 1024ULL * 1024ULL * 1024ULL;
        } else if ((endptr[0] == 't' || endptr[0] == 'T') &&
                   (endptr[1] == 'b' || endptr[1] == 'B') && endptr[2] == '\0') {
            multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
        } else if ((endptr[0] == 'k' || endptr[0] == 'K') &&
                   (endptr[1] == 'i' || endptr[1] == 'I') &&
                   (endptr[2] == 'b' || endptr[2] == 'B') && endptr[3] == '\0') {
            multiplier = 1024ULL;
        } else if ((endptr[0] == 'm' || endptr[0] == 'M') &&
                   (endptr[1] == 'i' || endptr[1] == 'I') &&
                   (endptr[2] == 'b' || endptr[2] == 'B') && endptr[3] == '\0') {
            multiplier = 1024ULL * 1024ULL;
        } else if ((endptr[0] == 'g' || endptr[0] == 'G') &&
                   (endptr[1] == 'i' || endptr[1] == 'I') &&
                   (endptr[2] == 'b' || endptr[2] == 'B') && endptr[3] == '\0') {
            multiplier = 1024ULL * 1024ULL * 1024ULL;
        } else if ((endptr[0] == 't' || endptr[0] == 'T') &&
                   (endptr[1] == 'i' || endptr[1] == 'I') &&
                   (endptr[2] == 'b' || endptr[2] == 'B') && endptr[3] == '\0') {
            multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
        } else {
            return -1;
        }
    }

    if (val > (unsigned long long)SIZE_MAX / multiplier) {
        return -1;
    }

    *result = (size_t)(val * multiplier);
    return 0;
}

static int parse_single_byte_option(const char *name, const char *arg, char *out) {
    if (!name || !arg || !out || arg[0] == '\0') {
        fprintf(stderr, "Error: %s cannot be empty\n", name ? name : "Option");
        return -1;
    }
    if (arg[1] != '\0') {
        fprintf(stderr, "Error: %s must be exactly one byte\n", name);
        return -1;
    }
    if (arg[0] == '\n' || arg[0] == '\r') {
        fprintf(stderr, "Error: %s cannot be a newline character\n", name);
        return -1;
    }
    *out = arg[0];
    return 0;
}

typedef struct {
    size_t row_count;
    size_t field_count;
    int count_only;
    int head;
    int tail;
    int *select_cols;
    int select_count;
    FILE *output;
    cisv_writer *csv_writer;

    char ***tail_buffer;
    size_t **tail_field_lengths;
    size_t *tail_field_counts;
    size_t tail_pos;

    char **current_row;
    size_t current_field_count;
    size_t current_field_capacity;
    size_t current_input_col;
    int current_select_pos;
    size_t current_row_num;
    size_t matched_row_count;
    int in_header;
    int no_header;
    int quiet;
    int output_mode; /* 0=csv, 1=json, 2=jsonl */
    int json_started;

    char **header_fields;
    size_t *header_field_lengths;
    size_t header_field_count;

    char **select_names;
    int select_name_count;
    int select_names_resolved;

    int where_enabled;
    char *where_column;
    char *where_value;
    int where_is_name;
    int where_col_idx;
    int where_op; /* 0==,1!=,2>,3>=,4<,5<=,6contains */
    int where_value_is_number;
    double where_value_number;

    char *csv_output_buffer;
    size_t csv_output_buffer_pos;
    size_t csv_output_buffer_size;

    cisv_config *config;
} cli_context;

typedef struct {
    char **fields;
    size_t field_count;
} cli_owned_row;

typedef struct {
    size_t rows;
    int errors;
    int quiet;
} cli_count_context;

static void cli_count_row_cb(void *user) {
    cli_count_context *ctx = (cli_count_context *)user;
    ctx->rows++;
}

static void cli_count_error_cb(void *user, int line, const char *msg) {
    cli_count_context *ctx = (cli_count_context *)user;
    ctx->errors++;
    if (!ctx->quiet) {
        fprintf(stderr, "Parse error at line %d: %s\n", line, msg ? msg : "unknown error");
    }
}

static int count_needs_checked_path(const cisv_config *config) {
    if (config && config->max_row_size > 0) return 1;
    return getenv("CISV_MAX_ROW_SIZE") != NULL ||
           getenv("CISV_MAX_MEMORY") != NULL ||
           getenv("GOMEMLIMIT") != NULL;
}

static int count_rows_checked(const char *filename, const cisv_config *config, int quiet, size_t *out_count) {
    if (!filename || !out_count) return -1;

    cli_count_context count_ctx = {
        .rows = 0,
        .errors = 0,
        .quiet = quiet
    };

    cisv_config count_config;
    if (config) {
        count_config = *config;
    } else {
        cisv_config_init(&count_config);
    }
    count_config.field_cb = NULL;
    count_config.row_cb = cli_count_row_cb;
    count_config.error_cb = cli_count_error_cb;
    count_config.user = &count_ctx;

    cisv_parser *parser = cisv_parser_create_with_config(&count_config);
    if (!parser) return -1;

    int rc = cisv_parser_parse_file(parser, filename);
    cisv_parser_destroy(parser);

    if (rc < 0 || count_ctx.errors > 0) {
        return -1;
    }

    *out_count = count_ctx.rows;
    return 0;
}

static int compare_ints_asc(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

static int parse_double_strict(const char *s, double *out) {
    if (!s || !*s || !out) return 0;
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0') return 0;
    *out = v;
    return 1;
}

static size_t row_field_len(const char *const *row, const size_t *lengths, size_t index) {
    if (lengths) return lengths[index];
    return row[index] ? strlen(row[index]) : 0;
}

static char **duplicate_row_fields(const char *const *row, const size_t *lengths, size_t field_count) {
    char **copy = calloc(field_count, sizeof(char *));
    if (!copy) return NULL;

    for (size_t i = 0; i < field_count; i++) {
        size_t len = row_field_len(row, lengths, i);
        copy[i] = malloc(len + 1);
        if (!copy[i]) {
            for (size_t j = 0; j < i; j++) free(copy[j]);
            free(copy);
            return NULL;
        }
        if (len > 0 && row[i]) {
            memcpy(copy[i], row[i], len);
        }
        copy[i][len] = '\0';
    }

    return copy;
}

static size_t *duplicate_row_lengths(const char *const *row, const size_t *lengths, size_t field_count) {
    size_t *copy = calloc(field_count, sizeof(size_t));
    if (!copy) return NULL;
    for (size_t i = 0; i < field_count; i++) {
        copy[i] = row_field_len(row, lengths, i);
    }
    return copy;
}

static void json_write_escaped_len(FILE *out, const char *s, size_t len) {
    fputc('"', out);
    const unsigned char *bytes = (const unsigned char *)(s ? s : "");
    for (size_t i = 0; i < len; i++) {
        switch (bytes[i]) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\b': fputs("\\b", out); break;
            case '\f': fputs("\\f", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (bytes[i] < 0x20) {
                    fprintf(out, "\\u%04x", bytes[i]);
                } else {
                    fputc(bytes[i], out);
                }
        }
    }
    fputc('"', out);
}

static int parse_name_list(const char *arg, char ***out_names, int *out_count) {
    if (!arg || !out_names || !out_count) return -1;
    char *copy = strdup(arg);
    if (!copy) return -1;
    int count = 1;
    for (char *p = copy; *p; p++) {
        if (*p == ',') count++;
    }
    char **names = calloc((size_t)count, sizeof(char *));
    if (!names) {
        free(copy);
        return -1;
    }
    int i = 0;
    char *tok = strtok(copy, ",");
    while (tok && i < count) {
        names[i] = strdup(tok);
        if (!names[i]) {
            for (int j = 0; j < i; j++) free(names[j]);
            free(names);
            free(copy);
            return -1;
        }
        i++;
        tok = strtok(NULL, ",");
    }
    free(copy);
    *out_names = names;
    *out_count = i;
    return 0;
}

static int resolve_header_index(cli_context *ctx, const char *name) {
    if (!ctx || !name) return -1;
    for (size_t i = 0; i < ctx->header_field_count; i++) {
        if (strcmp(ctx->header_fields[i], name) == 0) return (int)i;
    }
    return -1;
}

static int parse_where_expr(cli_context *ctx, const char *expr) {
    if (!ctx || !expr || !*expr) return -1;
    const char *ops[] = {"==", "!=", ">=", "<=", ">", "<", "~"};
    int op_codes[] = {0, 1, 3, 5, 2, 4, 6};
    const int nops = 7;
    const char *pos = NULL;
    int op = -1;
    int op_len = 0;
    for (int i = 0; i < nops; i++) {
        const char *p = strstr(expr, ops[i]);
        if (p) {
            pos = p;
            op = op_codes[i];
            op_len = (int)strlen(ops[i]);
            break;
        }
    }
    if (!pos || op < 0) return -1;

    size_t lhs_len = (size_t)(pos - expr);
    const char *rhs_start = pos + op_len;
    while (lhs_len > 0 && (expr[lhs_len - 1] == ' ' || expr[lhs_len - 1] == '\t')) lhs_len--;
    while (*rhs_start == ' ' || *rhs_start == '\t') rhs_start++;
    if (lhs_len == 0 || *rhs_start == '\0') return -1;

    char *lhs = strndup(expr, lhs_len);
    char *rhs = strdup(rhs_start);
    if (!lhs || !rhs) {
        free(lhs);
        free(rhs);
        return -1;
    }
    for (char *p = rhs + strlen(rhs) - 1; p >= rhs && (*p == ' ' || *p == '\t'); p--) {
        *p = '\0';
    }
    size_t rhs_len = strlen(rhs);
    if (rhs_len >= 2 && ((rhs[0] == '"' && rhs[rhs_len - 1] == '"') ||
                         (rhs[0] == '\'' && rhs[rhs_len - 1] == '\''))) {
        rhs[rhs_len - 1] = '\0';
        memmove(rhs, rhs + 1, rhs_len - 1);
    }

    ctx->where_column = lhs;
    ctx->where_value = rhs;
    ctx->where_op = op;
    ctx->where_col_idx = -1;
    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(lhs, &endptr, 10);
    if (errno == 0 && endptr && *endptr == '\0' && parsed >= 0 && parsed <= INT_MAX) {
        ctx->where_is_name = 0;
        ctx->where_col_idx = (int)parsed;
    } else {
        ctx->where_is_name = 1;
    }
    ctx->where_value_is_number = parse_double_strict(rhs, &ctx->where_value_number);
    ctx->where_enabled = 1;
    return 0;
}

static int row_matches_where(cli_context *ctx, const char *const *row, size_t field_count) {
    if (!ctx || !ctx->where_enabled) return 1;
    int idx = ctx->where_col_idx;
    if (idx < 0 || (size_t)idx >= field_count) return 0;
    const char *v = row[idx] ? row[idx] : "";

    if (ctx->where_op == 6) {
        return strstr(v, ctx->where_value) != NULL;
    }

    double lhs_num = 0.0;
    int lhs_is_num = parse_double_strict(v, &lhs_num);
    if (lhs_is_num && ctx->where_value_is_number) {
        switch (ctx->where_op) {
            case 0: return lhs_num == ctx->where_value_number;
            case 1: return lhs_num != ctx->where_value_number;
            case 2: return lhs_num > ctx->where_value_number;
            case 3: return lhs_num >= ctx->where_value_number;
            case 4: return lhs_num < ctx->where_value_number;
            case 5: return lhs_num <= ctx->where_value_number;
            default: return 0;
        }
    }

    int cmp = strcmp(v, ctx->where_value);
    switch (ctx->where_op) {
        case 0: return cmp == 0;
        case 1: return cmp != 0;
        case 2: return cmp > 0;
        case 3: return cmp >= 0;
        case 4: return cmp < 0;
        case 5: return cmp <= 0;
        default: return 0;
    }
}

static int ensure_csv_writer(cli_context *ctx) {
    if (!ctx || !ctx->output || !ctx->config) return -1;
    if (ctx->csv_writer) return 0;

    cisv_writer_config writer_config = {
        .delimiter = ctx->config->delimiter,
        .quote_char = ctx->config->quote,
        .always_quote = 0,
        .use_crlf = 0,
        .null_string = "",
        .buffer_size = 1 << 20
    };

    ctx->csv_writer = cisv_writer_create_config(ctx->output, &writer_config);
    if (!ctx->csv_writer) {
        if (!ctx->quiet) {
            fprintf(stderr, "Failed to create CSV writer\n");
        }
        return -1;
    }
    return 0;
}

static int cli_csv_output_flush(cli_context *ctx) {
    if (!ctx || !ctx->output || ctx->csv_output_buffer_pos == 0) return 0;
    if (fwrite(ctx->csv_output_buffer, 1, ctx->csv_output_buffer_pos, ctx->output) != ctx->csv_output_buffer_pos) {
        return -1;
    }
    ctx->csv_output_buffer_pos = 0;
    return 0;
}

static int cli_csv_output_append(cli_context *ctx, const char *data, size_t len) {
    if (!ctx || (!data && len > 0)) return -1;
    if (len == 0) return 0;

    if (!ctx->csv_output_buffer) {
        ctx->csv_output_buffer_size = 1 << 20;
        ctx->csv_output_buffer = malloc(ctx->csv_output_buffer_size);
        if (!ctx->csv_output_buffer) return -1;
    }

    if (len > ctx->csv_output_buffer_size) {
        if (cli_csv_output_flush(ctx) != 0) return -1;
        return fwrite(data, 1, len, ctx->output) == len ? 0 : -1;
    }

    if (ctx->csv_output_buffer_pos + len > ctx->csv_output_buffer_size) {
        if (cli_csv_output_flush(ctx) != 0) return -1;
    }

    memcpy(ctx->csv_output_buffer + ctx->csv_output_buffer_pos, data, len);
    ctx->csv_output_buffer_pos += len;
    return 0;
}

static int cli_csv_output_char(cli_context *ctx, char c) {
    return cli_csv_output_append(ctx, &c, 1);
}

static int cli_csv_field_needs_quote(const char *data, size_t len, char delimiter, char quote) {
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == delimiter || c == quote || c == '\r' || c == '\n') {
            return 1;
        }
    }
    return 0;
}

static int cli_csv_output_field(cli_context *ctx, const char *data, size_t len, int first_field) {
    char delimiter = ctx->config ? ctx->config->delimiter : ',';
    char quote = ctx->config ? ctx->config->quote : '"';

    if (!first_field && cli_csv_output_char(ctx, delimiter) != 0) return -1;
    if (!data) {
        data = "";
        len = 0;
    }

    if (!cli_csv_field_needs_quote(data, len, delimiter, quote)) {
        return cli_csv_output_append(ctx, data, len);
    }

    if (cli_csv_output_char(ctx, quote) != 0) return -1;

    const char *segment = data;
    const char *end = data + len;
    while (segment < end) {
        const char *quote_pos = memchr(segment, quote, (size_t)(end - segment));
        if (!quote_pos) {
            if (cli_csv_output_append(ctx, segment, (size_t)(end - segment)) != 0) return -1;
            break;
        }
        if (cli_csv_output_append(ctx, segment, (size_t)(quote_pos - segment)) != 0) return -1;
        if (cli_csv_output_char(ctx, quote) != 0) return -1;
        if (cli_csv_output_char(ctx, quote) != 0) return -1;
        segment = quote_pos + 1;
    }

    return cli_csv_output_char(ctx, quote);
}

static int cli_csv_output_iterator_row(
    cli_context *ctx,
    const char **fields,
    const size_t *lengths,
    size_t field_count
) {
    int emitted = 0;

    if (ctx->select_cols && ctx->select_count > 0) {
        for (int sel = 0; sel < ctx->select_count; sel++) {
            int col = ctx->select_cols[sel];
            if (col < 0 || (size_t)col >= field_count) continue;
            if (cli_csv_output_field(ctx, fields[col], lengths[col], !emitted) != 0) return -1;
            emitted = 1;
        }
    } else {
        for (size_t i = 0; i < field_count; i++) {
            if (cli_csv_output_field(ctx, fields[i], lengths[i], !emitted) != 0) return -1;
            emitted = 1;
        }
    }

    return cli_csv_output_char(ctx, '\n');
}

static int cli_csv_selected_col(const cli_context *ctx, int col, int *select_pos) {
    while (*select_pos < ctx->select_count && ctx->select_cols[*select_pos] < col) {
        (*select_pos)++;
    }
    return *select_pos < ctx->select_count && ctx->select_cols[*select_pos] == col;
}

static int cli_csv_output_raw_or_quote(
    cli_context *ctx,
    const uint8_t *start,
    const uint8_t *end,
    int already_quoted,
    int first_field
) {
    if (!already_quoted &&
        (memchr(start, ctx->config->quote, (size_t)(end - start)) ||
         memchr(start, '\r', (size_t)(end - start)))) {
        return cli_csv_output_field(ctx, (const char *)start, (size_t)(end - start), first_field);
    }

    if (!first_field && cli_csv_output_char(ctx, ctx->config->delimiter) != 0) return -1;
    return cli_csv_output_append(ctx, (const char *)start, (size_t)(end - start));
}

static int can_use_fast_select_projector(const cisv_config *config, const cli_context *ctx, int parallel) {
    if (!config || !ctx || parallel) return 0;
    if (ctx->select_count <= 0 || (ctx->select_names && ctx->select_name_count > 0)) return 0;
    if (ctx->output_mode != 0 || ctx->where_enabled || ctx->head != 0 || ctx->tail != 0) return 0;
    if (getenv("CISV_STATS") != NULL) return 0;

    if (config->escape != '\0' || config->trim || config->skip_empty_lines ||
        config->comment != '\0' || config->relaxed || config->skip_lines_with_error) {
        return 0;
    }
    if (config->from_line > 1 || config->to_line > 0 || config->max_row_size > 0) return 0;
    if (getenv("CISV_MAX_ROW_SIZE") || getenv("CISV_MAX_MEMORY") || getenv("GOMEMLIMIT")) return 0;

    return 1;
}

static int project_select_file_fast(const char *filename, cisv_config *config, cli_context *ctx) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        close(fd);
        return -1;
    }

    if (st.st_size == 0) {
        close(fd);
        return 0;
    }

    uint8_t *base = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    const uint8_t *p = base;
    const uint8_t *end = base + st.st_size;
    const char delimiter = config->delimiter;
    const char quote = config->quote;
    size_t input_row_num = 0;
    int result = 0;

    while (p < end) {
        int col = 0;
        int select_pos = 0;
        int emitted = 0;

        for (;;) {
            const uint8_t *field_start = p;
            const uint8_t *field_end = p;
            int already_quoted = 0;

            if (p < end && *p == (uint8_t)quote) {
                already_quoted = 1;
                p++;
                while (p < end) {
                    if (*p == (uint8_t)quote) {
                        if (p + 1 < end && p[1] == (uint8_t)quote) {
                            p += 2;
                            continue;
                        }
                        p++;
                        field_end = p;
                        while (p < end && (*p == ' ' || *p == '\t')) p++;
                        break;
                    }
                    p++;
                }
                if (field_end == field_start) {
                    fprintf(stderr, "Parse error: unterminated quoted field\n");
                    result = -1;
                    goto done;
                }
                if (p < end && *p != (uint8_t)delimiter && *p != '\n' &&
                    !(*p == '\r' && p + 1 < end && p[1] == '\n')) {
                    fprintf(stderr, "Parse error: unexpected character after closing quote\n");
                    result = -1;
                    goto done;
                }
            } else {
                while (p < end && *p != (uint8_t)delimiter && *p != '\n') {
                    p++;
                }
                field_end = p;
                if (field_end > field_start && *(field_end - 1) == '\r') {
                    field_end--;
                }
            }

            if (cli_csv_selected_col(ctx, col, &select_pos)) {
                if (!(ctx->no_header && input_row_num == 0)) {
                    if (cli_csv_output_raw_or_quote(ctx, field_start, field_end, already_quoted, !emitted) != 0) {
                        result = -1;
                        goto done;
                    }
                    emitted = 1;
                }
            }

            if (p >= end) {
                if (!(ctx->no_header && input_row_num == 0)) {
                    if (cli_csv_output_char(ctx, '\n') != 0) {
                        result = -1;
                        goto done;
                    }
                    ctx->row_count++;
                }
                input_row_num++;
                break;
            }

            if (*p == (uint8_t)delimiter) {
                p++;
                col++;
                if (p >= end) {
                    if (cli_csv_selected_col(ctx, col, &select_pos) &&
                        !(ctx->no_header && input_row_num == 0)) {
                        if (cli_csv_output_field(ctx, "", 0, !emitted) != 0) {
                            result = -1;
                            goto done;
                        }
                    }
                    if (!(ctx->no_header && input_row_num == 0)) {
                        if (cli_csv_output_char(ctx, '\n') != 0) {
                            result = -1;
                            goto done;
                        }
                        ctx->row_count++;
                    }
                    input_row_num++;
                    break;
                }
                continue;
            }

            if (*p == '\r' && p + 1 < end && p[1] == '\n') {
                p += 2;
            } else if (*p == '\n') {
                p++;
            } else if (*p == '\r') {
                fprintf(stderr, "Parse error: unexpected carriage return after closing quote\n");
                result = -1;
                goto done;
            }

            if (!(ctx->no_header && input_row_num == 0)) {
                if (cli_csv_output_char(ctx, '\n') != 0) {
                    result = -1;
                    goto done;
                }
                ctx->row_count++;
            }
            input_row_num++;
            break;
        }
    }

    if (cli_csv_output_flush(ctx) != 0) result = -1;

done:
    munmap(base, (size_t)st.st_size);
    return result;
}

static int output_row(cli_context *ctx, const char *const *row, const size_t *lengths, size_t field_count) {
    int first = 1;
    if (ctx->output_mode == 1) {
        if (!ctx->json_started) {
            fputc('[', ctx->output);
            ctx->json_started = 1;
        } else {
            fputc(',', ctx->output);
        }
    }

    if (ctx->output_mode == 1 || ctx->output_mode == 2) {
        if (ctx->header_field_count > 0 && !ctx->no_header) {
            fputc('{', ctx->output);
            if (ctx->select_count > 0) {
                for (int i = 0; i < ctx->select_count; i++) {
                    int col = ctx->select_cols[i];
                    if (col < 0 || (size_t)col >= field_count || (size_t)col >= ctx->header_field_count) continue;
                    if (!first) fputc(',', ctx->output);
                    json_write_escaped_len(ctx->output, ctx->header_fields[col],
                                           ctx->header_field_lengths ? ctx->header_field_lengths[col] : strlen(ctx->header_fields[col]));
                    fputc(':', ctx->output);
                    json_write_escaped_len(ctx->output, row[col], row_field_len(row, lengths, (size_t)col));
                    first = 0;
                }
            } else {
                for (size_t i = 0; i < field_count && i < ctx->header_field_count; i++) {
                    if (!first) fputc(',', ctx->output);
                    json_write_escaped_len(ctx->output, ctx->header_fields[i],
                                           ctx->header_field_lengths ? ctx->header_field_lengths[i] : strlen(ctx->header_fields[i]));
                    fputc(':', ctx->output);
                    json_write_escaped_len(ctx->output, row[i], row_field_len(row, lengths, i));
                    first = 0;
                }
            }
            fputc('}', ctx->output);
        } else {
            fputc('[', ctx->output);
            if (ctx->select_count > 0) {
                for (int i = 0; i < ctx->select_count; i++) {
                    int col = ctx->select_cols[i];
                    if (col < 0 || (size_t)col >= field_count) continue;
                    if (!first) fputc(',', ctx->output);
                    json_write_escaped_len(ctx->output, row[col], row_field_len(row, lengths, (size_t)col));
                    first = 0;
                }
            } else {
                for (size_t i = 0; i < field_count; i++) {
                    if (!first) fputc(',', ctx->output);
                    json_write_escaped_len(ctx->output, row[i], row_field_len(row, lengths, i));
                    first = 0;
                }
            }
            fputc(']', ctx->output);
        }
        if (ctx->output_mode == 2) {
            fputc('\n', ctx->output);
        }
        return 0;
    }

    if (ensure_csv_writer(ctx) != 0) return -1;

    if (ctx->select_count > 0) {
        for (int i = 0; i < ctx->select_count; i++) {
            int col = ctx->select_cols[i];
            if (col < 0 || (size_t)col >= field_count) continue;
            if (cisv_writer_field(ctx->csv_writer, row[col], row_field_len(row, lengths, (size_t)col)) < 0) return -1;
        }
    } else {
        for (size_t i = 0; i < field_count; i++) {
            if (cisv_writer_field(ctx->csv_writer, row[i], row_field_len(row, lengths, i)) < 0) return -1;
        }
    }
    if (cisv_writer_row_end(ctx->csv_writer) < 0) return -1;
    return 0;
}

static int prepare_header_state(cli_context *ctx, const char *const *row, const size_t *lengths, size_t field_count) {
    if (ctx->current_row_num == 0) {
        if (ctx->header_fields) {
            for (size_t i = 0; i < ctx->header_field_count; i++) free(ctx->header_fields[i]);
            free(ctx->header_fields);
            ctx->header_fields = NULL;
        }
        free(ctx->header_field_lengths);
        ctx->header_field_lengths = NULL;
        ctx->header_field_count = field_count;
        ctx->header_fields = duplicate_row_fields(row, lengths, field_count);
        ctx->header_field_lengths = duplicate_row_lengths(row, lengths, field_count);
        if (!ctx->header_fields || !ctx->header_field_lengths) {
            fprintf(stderr, "Memory allocation failed\n");
            return -1;
        }

        if (ctx->select_names && ctx->select_name_count > 0 && !ctx->select_names_resolved) {
            if (ctx->select_cols) {
                free(ctx->select_cols);
                ctx->select_cols = NULL;
                ctx->select_count = 0;
            }
            ctx->select_cols = calloc((size_t)ctx->select_name_count, sizeof(int));
            if (!ctx->select_cols) {
                fprintf(stderr, "Memory allocation failed\n");
                return -1;
            }
            int resolved = 0;
            for (int i = 0; i < ctx->select_name_count; i++) {
                int idx = resolve_header_index(ctx, ctx->select_names[i]);
                if (idx < 0) {
                    fprintf(stderr, "Missing selected column: %s\n", ctx->select_names[i]);
                    return -1;
                }
                ctx->select_cols[resolved++] = idx;
            }
            ctx->select_count = resolved;
            qsort(ctx->select_cols, ctx->select_count, sizeof(int), compare_ints_asc);
            ctx->select_names_resolved = 1;
        }

        if (ctx->where_enabled && ctx->where_is_name && ctx->where_col_idx < 0) {
            ctx->where_col_idx = resolve_header_index(ctx, ctx->where_column);
        }
    }
    return 0;
}

static int process_row_for_cli(cli_context *ctx, const char *const *row, const size_t *lengths, size_t field_count) {
    if (prepare_header_state(ctx, row, lengths, field_count) != 0) {
        return -1;
    }

    if (ctx->no_header && ctx->current_row_num == 0) {
        ctx->current_row_num++;
        return 0;
    }
    if (ctx->output_mode != 0 && !ctx->no_header && ctx->current_row_num == 0) {
        ctx->current_row_num++;
        return 0;
    }

    if (!row_matches_where(ctx, row, field_count)) {
        ctx->current_row_num++;
        return 0;
    }

    if (ctx->head > 0 && ctx->matched_row_count >= (size_t)ctx->head) {
        ctx->current_row_num++;
        return 0;
    }
    ctx->matched_row_count++;

    if (ctx->tail > 0) {
        if (ctx->tail_buffer[ctx->tail_pos]) {
            for (size_t i = 0; i < ctx->tail_field_counts[ctx->tail_pos]; i++) {
                free(ctx->tail_buffer[ctx->tail_pos][i]);
            }
            free(ctx->tail_buffer[ctx->tail_pos]);
            free(ctx->tail_field_lengths[ctx->tail_pos]);
        }

        char **row_copy = duplicate_row_fields(row, lengths, field_count);
        size_t *length_copy = duplicate_row_lengths(row, lengths, field_count);
        if (!row_copy || !length_copy) {
            if (row_copy) {
                for (size_t i = 0; i < field_count; i++) free(row_copy[i]);
                free(row_copy);
            }
            free(length_copy);
            fprintf(stderr, "Memory allocation failed\n");
            return -1;
        }

        ctx->tail_buffer[ctx->tail_pos] = row_copy;
        ctx->tail_field_lengths[ctx->tail_pos] = length_copy;
        ctx->tail_field_counts[ctx->tail_pos] = field_count;
        ctx->tail_pos = (ctx->tail_pos + 1) % ctx->tail;
    } else {
        if (output_row(ctx, row, lengths, field_count) != 0) {
            fprintf(stderr, "Failed writing output row\n");
            return -1;
        }
        ctx->row_count++;
    }

    ctx->current_row_num++;
    return 0;
}

static void print_help(const char *prog) {
    printf("cisv - High-performance CSV parser\n\n");
    printf("Usage: %s [COMMAND] [OPTIONS] [FILE]\n\n", prog);
    printf("Commands:\n");
    printf("  parse    Parse CSV file (default if no command given)\n");
    printf("  write    Write/generate CSV files\n\n");
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -v, --version           Show version information\n");
    printf("  -d, --delimiter DELIM   Field delimiter (default: ,)\n");
    printf("  -q, --quote CHAR        Quote character (default: \")\n");
    printf("  -e, --escape CHAR       Escape character (default: RFC4180 style)\n");
    printf("  -m, --comment CHAR      Comment character (default: none)\n");
    printf("  -t, --trim              Trim whitespace from fields\n");
    printf("  -r, --relaxed           Use relaxed parsing rules\n");
    printf("  --skip-empty            Skip empty lines\n");
    printf("  --skip-errors           Skip lines with parse errors\n");
    printf("  --strict                Stop on first parse error (default)\n");
    printf("  --no-header             Skip first row in output\n");
    printf("  --quiet                 Suppress non-data stderr messages\n");
    printf("  --max-row SIZE          Maximum row size in bytes\n");
    printf("  --from-line N           Start from line N (1-based)\n");
    printf("  --to-line N             Stop at line N\n");
    printf("  -s, --select COLS       Select columns (comma-separated indices)\n");
    printf("  --select-name NAMES     Select columns by header names\n");
    printf("  --where EXPR            Filter rows (col==v, col!=v, col>n, col~text)\n");
    printf("  -c, --count             Show only row count\n");
    printf("  --head N                Show first N rows\n");
    printf("  --tail N                Show last N rows\n");
    printf("  -o, --output FILE       Write to FILE instead of stdout\n");
    printf("  --json                  Output JSON array\n");
    printf("  --jsonl                 Output JSON lines\n");
    printf("  --parallel              Parse file using multiple threads\n");
    printf("  --threads N             Number of worker threads (implies --parallel)\n");
    printf("  --max-procs N           Cap CISV worker threads/cores\n");
    printf("  --max-memory SIZE       Cap CISV parser memory budget\n");
    printf("  -b, --benchmark         Run benchmark mode\n");
    printf("\nResource environment:\n");
    printf("  CISV_MAX_PROCS or GOMAXPROCS      Max worker threads/cores\n");
    printf("  CISV_MAX_MEMORY or GOMEMLIMIT     Parser memory budget\n");
    printf("  CISV_MAX_ROW_SIZE                 Default max row size when --max-row is unset\n");
    printf("  CISV_PARALLEL_MIN_BYTES           Minimum file size before auto parallelism\n");
    printf("\nExamples:\n");
    printf("  %s data.csv                    # Parse and display CSV\n", prog);
    printf("  %s -c data.csv                 # Count rows\n", prog);
    printf("  %s -d ';' -q '\\'' data.csv     # Use semicolon delimiter\n", prog);
    printf("  %s -t --skip-empty data.csv    # Trim fields and skip empty lines\n", prog);
    printf("  %s --select-name id,name data.csv\n", prog);
    printf("  %s --where 'status==ok' data.csv\n", prog);
    printf("  %s --parallel --threads 4 data.csv\n", prog);
    printf("  %s --json data.csv\n", prog);
    printf("  cat data.csv | %s -            # Parse from stdin\n", prog);
    printf("\nFor write options, use: %s write --help\n", prog);
}

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static int count_rows_parallel(const char *filename, cisv_config *config, int num_threads, size_t *out_count) {
    if (!out_count) return -1;
    *out_count = 0;

    int result_count = 0;
    cisv_result_t **results = cisv_parse_file_parallel(filename, config, num_threads, &result_count);
    if (!results) {
        return -1;
    }

    for (int i = 0; i < result_count; i++) {
        if (!results[i]) continue;
        if (results[i]->error_code != 0) {
            cisv_results_free(results, result_count);
            return -1;
        }
        *out_count += results[i]->row_count;
    }

    cisv_results_free(results, result_count);
    return 0;
}

static void benchmark_file(const char *filename, cisv_config *config, int parallel, int num_threads) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    double size_mb = size / (1024.0 * 1024.0);
    printf("Benchmarking file: %s\n", filename);
    printf("File size: %.2f MB\n", size_mb);
    printf("Configuration: delimiter='%c', quote='%c', trim=%s, skip_empty=%s\n\n",
           config->delimiter, config->quote,
           config->trim ? "yes" : "no",
           config->skip_empty_lines ? "yes" : "no");
    if (parallel) {
        printf("Mode: parallel (%d threads)%s\n\n",
               num_threads > 0 ? num_threads : 0,
               num_threads > 0 ? "" : " (auto)");
    }

    const int iterations = 5;
    for (int i = 0; i < iterations; i++) {
        double start = get_time_ms();
        size_t count = 0;
        int rc = parallel
            ? count_rows_parallel(filename, config, num_threads, &count)
            : 0;
        if (!parallel) {
            count = cisv_parser_count_rows_with_config(filename, config);
        } else if (rc != 0) {
            fprintf(stderr, "Parallel benchmark parse failed\n");
            return;
        }
        double end = get_time_ms();

        double throughput = size_mb / ((end - start) / 1000.0);
        printf("Run %d: %.2f ms, %zu rows, %.2f MB/s\n",
               i + 1, end - start, count, throughput);
    }
}

static int parse_file_parallel_cli(const char *filename, cisv_config *config, cli_context *ctx, int num_threads) {
    int result_count = 0;
    cisv_result_t **results = cisv_parse_file_parallel(filename, config, num_threads, &result_count);
    if (!results) {
        perror("cisv_parse_file_parallel");
        return -1;
    }

    for (int chunk = 0; chunk < result_count; chunk++) {
        cisv_result_t *result = results[chunk];
        if (!result) continue;
        if (result->error_code != 0) {
            if (!ctx->quiet) {
                fprintf(stderr, "Parse error: %s\n", result->error_message);
            }
            cisv_results_free(results, result_count);
            return -1;
        }

        for (size_t i = 0; i < result->row_count; i++) {
            cisv_row_t *row = &result->rows[i];
            if (process_row_for_cli(ctx, (const char *const *)row->fields, row->field_lengths, row->field_count) != 0) {
                cisv_results_free(results, result_count);
                return -1;
            }
        }
    }

    cisv_results_free(results, result_count);
    return 0;
}

static int parse_file_with_iterator_cli(const char *filename, cisv_config *config, cli_context *ctx) {
    cisv_iterator_t *it = cisv_iterator_open(filename, config);
    if (!it) {
        perror("cisv_iterator_open");
        return -1;
    }

    const char **fields = NULL;
    const size_t *lengths = NULL;
    size_t field_count = 0;
    int rc;

    while ((rc = cisv_iterator_next(it, &fields, &lengths, &field_count)) == CISV_ITER_OK) {
        if (process_row_for_cli(ctx, fields, lengths, field_count) != 0) {
            cisv_iterator_close(it);
            return -1;
        }
    }

    cisv_iterator_close(it);
    if (rc == CISV_ITER_ERROR) {
        if (!ctx->quiet) {
            fprintf(stderr, "Parse error while iterating rows\n");
        }
        return -1;
    }

    return 0;
}

static int stream_rows_with_iterator(const char *filename, cisv_config *config, cli_context *ctx) {
    cisv_iterator_t *it = cisv_iterator_open(filename, config);
    if (!it) {
        perror("cisv_iterator_open");
        return -1;
    }

    const char **fields = NULL;
    const size_t *lengths = NULL;
    size_t field_count = 0;
    int rc;
    size_t input_row_num = 0;

    while ((rc = cisv_iterator_next(it, &fields, &lengths, &field_count)) == CISV_ITER_OK) {
        if (ctx->no_header && input_row_num == 0) {
            input_row_num++;
            continue;
        }
        input_row_num++;

        if (cli_csv_output_iterator_row(ctx, fields, lengths, field_count) != 0) {
            cisv_iterator_close(it);
            return -1;
        }
        ctx->row_count++;
    }

    if (cli_csv_output_flush(ctx) != 0) {
        cisv_iterator_close(it);
        return -1;
    }

    cisv_iterator_close(it);
    if (rc == CISV_ITER_ERROR) {
        fprintf(stderr, "Parse error while iterating rows\n");
        return -1;
    }
    return 0;
}

// Writer CLI implementation
#define DEFAULT_BUFFER_SIZE (1 << 20)

typedef enum {
    MODE_GENERATE,
    MODE_TRANSFORM,
    MODE_CONVERT
} write_mode_t;

static void print_write_help(const char *prog) {
    printf("cisv write - High-performance CSV writer\n\n");
    printf("Usage: %s write [OPTIONS]\n\n", prog);
    printf("Modes:\n");
    printf("  -g, --generate N        Generate N rows of test data\n");
    printf("  -t, --transform FILE    Transform existing CSV\n");
    printf("  -j, --json FILE         Convert JSON to CSV\n\n");
    printf("Options:\n");
    printf("  -o, --output FILE       Output file (default: stdout)\n");
    printf("  -d, --delimiter CHAR    Field delimiter (default: ,)\n");
    printf("  -q, --quote CHAR        Quote character (default: \")\n");
    printf("  -Q, --always-quote      Always quote fields\n");
    printf("  -r, --crlf              Use CRLF line endings\n");
    printf("  -n, --null STRING       String for NULL values (default: empty)\n");
    printf("  -c, --columns LIST      Column names for generation\n");
    printf("  -b, --benchmark         Run in benchmark mode\n");
}

static double get_time_seconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static int generate_csv(cisv_writer *writer, size_t rows, const char *columns) {
    (void)columns;
    const char *default_cols[] = {"id", "name", "email", "value", "timestamp"};
    size_t col_count = 5;

    if (cisv_writer_row(writer, default_cols, col_count) < 0) return -1;

    char buffer[256];
    for (size_t i = 0; i < rows; i++) {
        if (cisv_writer_field_int(writer, i + 1) < 0) return -1;

        snprintf(buffer, sizeof(buffer), "User_%zu", i);
        if (cisv_writer_field_str(writer, buffer) < 0) return -1;

        snprintf(buffer, sizeof(buffer), "user%zu@example.com", i);
        if (cisv_writer_field_str(writer, buffer) < 0) return -1;

        if (cisv_writer_field_double(writer, (double)(i * 1.23), 2) < 0) return -1;

        time_t now = time(NULL) + i;
        struct tm *tm = localtime(&now);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm);
        if (cisv_writer_field_str(writer, buffer) < 0) return -1;

        if (cisv_writer_row_end(writer) < 0) return -1;

        if ((i + 1) % 1000000 == 0) {
            fprintf(stderr, "Generated %zu rows...\n", i + 1);
        }
    }

    return 0;
}

static int cisv_writer_main(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"generate", required_argument, 0, 'g'},
        {"transform", required_argument, 0, 't'},
        {"json", required_argument, 0, 'j'},
        {"output", required_argument, 0, 'o'},
        {"delimiter", required_argument, 0, 'd'},
        {"quote", required_argument, 0, 'q'},
        {"always-quote", no_argument, 0, 'Q'},
        {"crlf", no_argument, 0, 'r'},
        {"null", required_argument, 0, 'n'},
        {"columns", required_argument, 0, 'c'},
        {"benchmark", no_argument, 0, 'b'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    write_mode_t mode = MODE_GENERATE;
    size_t generate_rows = 0;
    const char *output_file = NULL;
    const char *columns = NULL;
    int benchmark = 0;

    cisv_writer_config config = {
        .delimiter = ',',
        .quote_char = '"',
        .always_quote = 0,
        .use_crlf = 0,
        .null_string = "",
        .buffer_size = DEFAULT_BUFFER_SIZE
    };

    optind = 1;  // Reset getopt
    int opt;
    while ((opt = getopt_long(argc, argv, "g:t:j:o:d:q:Qrn:c:bh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'g': {
                mode = MODE_GENERATE;
                char *endptr;
                errno = 0;
                unsigned long long val = strtoull(optarg, &endptr, 10);
                if (errno == ERANGE || *endptr != '\0' || val == 0) {
                    fprintf(stderr, "Error: Invalid row count '%s'\n", optarg);
                    return 1;
                }
                // Reasonable limit to prevent accidental resource exhaustion
                if (val > 1000000000ULL) {
                    fprintf(stderr, "Error: Row count too large (max 1 billion)\n");
                    return 1;
                }
                generate_rows = (size_t)val;
                break;
            }
            case 't':
                mode = MODE_TRANSFORM;
                break;
            case 'j':
                mode = MODE_CONVERT;
                break;
            case 'o':
                output_file = optarg;
                break;
            case 'd':
                config.delimiter = optarg[0];
                break;
            case 'q':
                config.quote_char = optarg[0];
                break;
            case 'Q':
                config.always_quote = 1;
                break;
            case 'r':
                config.use_crlf = 1;
                break;
            case 'n':
                config.null_string = optarg;
                break;
            case 'c':
                columns = optarg;
                break;
            case 'b':
                benchmark = 1;
                break;
            case 'h':
                print_write_help(argv[0]);
                return 0;
            default:
                fprintf(stderr, "Try '%s write --help' for more information.\n", argv[0]);
                return 1;
        }
    }

    FILE *output = stdout;
    if (output_file) {
        output = fopen(output_file, "wb");
        if (!output) {
            perror("fopen");
            return 1;
        }
    }

    cisv_writer *writer = cisv_writer_create_config(output, &config);
    if (!writer) {
        fprintf(stderr, "Failed to create writer\n");
        if (output != stdout) fclose(output);
        return 1;
    }

    double start_time = 0;
    if (benchmark) {
        start_time = get_time_seconds();
    }

    int result = 0;
    switch (mode) {
        case MODE_GENERATE:
            if (generate_rows == 0) {
                fprintf(stderr, "Error: Must specify number of rows to generate\n");
                result = 1;
            } else {
                result = generate_csv(writer, generate_rows, columns);
            }
            break;

        case MODE_TRANSFORM:
        case MODE_CONVERT:
            fprintf(stderr, "Transform/convert modes not yet implemented\n");
            result = 1;
            break;
    }

    cisv_writer_flush(writer);

    if (benchmark && result == 0) {
        double elapsed = get_time_seconds() - start_time;
        size_t bytes = cisv_writer_bytes_written(writer);
        size_t rows = cisv_writer_rows_written(writer);
        double mb = bytes / (1024.0 * 1024.0);
        double throughput = mb / elapsed;

        fprintf(stderr, "\nBenchmark Results:\n");
        fprintf(stderr, "  Rows written: %zu\n", rows);
        fprintf(stderr, "  Bytes written: %zu (%.2f MB)\n", bytes, mb);
        fprintf(stderr, "  Time: %.3f seconds\n", elapsed);
        fprintf(stderr, "  Throughput: %.2f MB/s\n", throughput);
        fprintf(stderr, "  Rows/sec: %.0f\n", rows / elapsed);
    }

    cisv_writer_destroy(writer);
    if (output != stdout) fclose(output);

    return result;
}

static int materialize_stdin_to_temp(char *out_path, size_t out_path_len) {
    if (!out_path || out_path_len == 0) {
        return -1;
    }

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || tmpdir[0] == '\0') {
        tmpdir = "/tmp";
    }

    char tmpl[PATH_MAX];
    if (snprintf(tmpl, sizeof(tmpl), "%s/cisv-stdin-XXXXXX", tmpdir) >= (int)sizeof(tmpl)) {
        fprintf(stderr, "Error: Temporary path is too long\n");
        return -1;
    }

    int fd = mkstemp(tmpl);
    if (fd < 0) {
        perror("mkstemp");
        return -1;
    }

    FILE *tmpf = fdopen(fd, "wb");
    if (!tmpf) {
        perror("fdopen");
        close(fd);
        unlink(tmpl);
        return -1;
    }

    unsigned char buffer[1 << 16];
    while (!feof(stdin)) {
        size_t n = fread(buffer, 1, sizeof(buffer), stdin);
        if (n > 0 && fwrite(buffer, 1, n, tmpf) != n) {
            perror("fwrite");
            fclose(tmpf);
            unlink(tmpl);
            return -1;
        }
        if (ferror(stdin)) {
            perror("fread");
            fclose(tmpf);
            unlink(tmpl);
            return -1;
        }
    }

    if (fclose(tmpf) != 0) {
        perror("fclose");
        unlink(tmpl);
        return -1;
    }

    if (snprintf(out_path, out_path_len, "%s", tmpl) >= (int)out_path_len) {
        unlink(tmpl);
        return -1;
    }
    return 0;
}

static int open_atomic_output_file(const char *output_file, char *tmp_path, size_t tmp_path_len, FILE **out) {
    if (!output_file || !tmp_path || tmp_path_len == 0 || !out) {
        return -1;
    }

    char tmpl[PATH_MAX];
    int n = snprintf(tmpl, sizeof(tmpl), "%s.tmp.XXXXXX", output_file);
    if (n < 0 || n >= (int)sizeof(tmpl) || (size_t)n >= tmp_path_len) {
        fprintf(stderr, "Error: Output path is too long\n");
        return -1;
    }

    int fd = mkstemp(tmpl);
    if (fd < 0) {
        perror("mkstemp");
        return -1;
    }

    FILE *f = fdopen(fd, "wb");
    if (!f) {
        perror("fdopen");
        close(fd);
        unlink(tmpl);
        return -1;
    }

    memcpy(tmp_path, tmpl, (size_t)n + 1);
    *out = f;
    return 0;
}

static int finalize_atomic_output_file(cli_context *ctx,
                                       const char *tmp_path,
                                       const char *output_file,
                                       int success,
                                       int quiet) {
    int close_failed = 0;

    if (ctx && ctx->csv_writer) {
        cisv_writer_destroy(ctx->csv_writer);
        ctx->csv_writer = NULL;
    }

    if (ctx && ctx->output && ctx->output != stdout) {
        if (fclose(ctx->output) != 0) {
            close_failed = 1;
            if (!quiet) perror("fclose");
        }
        ctx->output = NULL;
    }

    if (!output_file || !tmp_path || tmp_path[0] == '\0') {
        return close_failed ? -1 : 0;
    }

    if (success && !close_failed) {
        if (rename(tmp_path, output_file) != 0) {
            if (!quiet) perror("rename");
            unlink(tmp_path);
            return -1;
        }
        return 0;
    }

    unlink(tmp_path);
    return close_failed ? -1 : 0;
}

static void cleanup_cli_context(cli_context *ctx) {
    if (!ctx) {
        return;
    }

    free(ctx->current_row);
    free(ctx->select_cols);

    if (ctx->select_names) {
        for (int i = 0; i < ctx->select_name_count; i++) {
            free(ctx->select_names[i]);
        }
        free(ctx->select_names);
    }

    free(ctx->where_column);
    free(ctx->where_value);

    if (ctx->header_fields) {
        for (size_t i = 0; i < ctx->header_field_count; i++) {
            free(ctx->header_fields[i]);
        }
        free(ctx->header_fields);
    }
    free(ctx->header_field_lengths);

    if (ctx->tail_buffer) {
        for (int i = 0; i < ctx->tail; i++) {
            if (!ctx->tail_buffer[i]) continue;
            for (size_t j = 0; j < ctx->tail_field_counts[i]; j++) {
                free(ctx->tail_buffer[i][j]);
            }
            free(ctx->tail_buffer[i]);
            if (ctx->tail_field_lengths) {
                free(ctx->tail_field_lengths[i]);
            }
        }
        free(ctx->tail_buffer);
    }
    free(ctx->tail_field_lengths);

    free(ctx->tail_field_counts);
    free(ctx->csv_output_buffer);

    if (ctx->csv_writer) {
        cisv_writer_destroy(ctx->csv_writer);
        ctx->csv_writer = NULL;
    }

    if (ctx->output && ctx->output != stdout) {
        fclose(ctx->output);
    }
}

static int finish_cli(cli_context *ctx,
                      const char *stdin_tmp_path,
                      const char *output_tmp_path,
                      const char *output_file,
                      int success) {
    int finalize_rc = finalize_atomic_output_file(
        ctx,
        output_tmp_path,
        output_file,
        success,
        ctx ? ctx->quiet : 0
    );

    if (stdin_tmp_path && stdin_tmp_path[0]) {
        unlink(stdin_tmp_path);
    }

    cleanup_cli_context(ctx);
    return (success && finalize_rc == 0) ? 0 : 1;
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "write") == 0) {
        return cisv_writer_main(argc - 1, argv + 1);
    }

    if (argc > 1 && strcmp(argv[1], "parse") == 0) {
        argc--;
        argv++;
    }

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"delimiter", required_argument, 0, 'd'},
        {"quote", required_argument, 0, 'q'},
        {"escape", required_argument, 0, 'e'},
        {"comment", required_argument, 0, 'm'},
        {"trim", no_argument, 0, 't'},
        {"relaxed", no_argument, 0, 'r'},
        {"skip-empty", no_argument, 0, 1},
        {"skip-errors", no_argument, 0, 2},
        {"strict", no_argument, 0, 8},
        {"no-header", no_argument, 0, 9},
        {"quiet", no_argument, 0, 10},
        {"max-row", required_argument, 0, 3},
        {"from-line", required_argument, 0, 4},
        {"to-line", required_argument, 0, 5},
        {"select", required_argument, 0, 's'},
        {"select-name", required_argument, 0, 11},
        {"where", required_argument, 0, 12},
        {"count", no_argument, 0, 'c'},
        {"head", required_argument, 0, 6},
        {"tail", required_argument, 0, 7},
        {"output", required_argument, 0, 'o'},
        {"json", no_argument, 0, 13},
        {"jsonl", no_argument, 0, 14},
        {"parallel", no_argument, 0, 15},
        {"threads", required_argument, 0, 16},
        {"max-procs", required_argument, 0, 17},
        {"max-memory", required_argument, 0, 18},
        {"benchmark", no_argument, 0, 'b'},
        {0, 0, 0, 0}
    };

    cisv_config config;
    cisv_config_init(&config);

    cli_context ctx = {0};
    ctx.output = stdout;
    ctx.current_field_capacity = 16;
    ctx.current_row = calloc(ctx.current_field_capacity, sizeof(char *));
    ctx.config = &config;

    if (!ctx.current_row) {
        fprintf(stderr, "Failed to allocate initial row buffer\n");
        return 1;
    }

    int opt;
    int option_index = 0;
    const char *filename = NULL;
    const char *output_file = NULL;
    int benchmark = 0;
    int parallel = 0;
    int num_threads = 0;
    char stdin_tmp_path[PATH_MAX] = {0};
    char output_tmp_path[PATH_MAX] = {0};

    while ((opt = getopt_long(argc, argv, "hvd:q:e:m:trs:co:b", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                print_help(argv[0]);
                cleanup_cli_context(&ctx);
                return 0;

            case 'v':
                printf("cisv version %s\n", CISV_CLI_VERSION);
                printf("Features: configurable parsing, SIMD optimizations\n");
                cleanup_cli_context(&ctx);
                return 0;

            case 'd':
                if (parse_single_byte_option("Delimiter", optarg, &config.delimiter) != 0) {
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                break;

            case 'q':
                if (parse_single_byte_option("Quote character", optarg, &config.quote) != 0) {
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                break;

            case 'e':
                if (parse_single_byte_option("Escape character", optarg, &config.escape) != 0) {
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                break;

            case 'm':
                if (parse_single_byte_option("Comment character", optarg, &config.comment) != 0) {
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                break;

            case 't':
                config.trim = true;
                break;

            case 'r':
                config.relaxed = true;
                break;

            case 1:
                config.skip_empty_lines = true;
                break;

            case 2:
                config.skip_lines_with_error = true;
                break;
            case 8:
                config.skip_lines_with_error = false;
                break;
            case 9:
                ctx.no_header = 1;
                break;
            case 10:
                ctx.quiet = 1;
                break;

            case 3: {
                long max_row;
                if (safe_parse_long(optarg, &max_row, 0) != 0) {
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                config.max_row_size = (size_t)max_row;
                break;
            }

            case 4: {
                int from_line;
                if (safe_parse_int(optarg, &from_line, 0) != 0) {
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                config.from_line = from_line;
                break;
            }

            case 5: {
                int to_line;
                if (safe_parse_int(optarg, &to_line, 0) != 0) {
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                config.to_line = to_line;
                break;
            }

            case 's': {
                char *cols_copy = strdup(optarg);
                if (!cols_copy) {
                    fprintf(stderr, "Memory allocation failed\n");
                    cleanup_cli_context(&ctx);
                    return 1;
                }

                int count = 1;
                for (char *p = cols_copy; *p; p++) {
                    if (*p == ',') count++;
                }

                ctx.select_cols = calloc(count, sizeof(int));
                if (!ctx.select_cols) {
                    fprintf(stderr, "Memory allocation failed\n");
                    free(cols_copy);
                    cleanup_cli_context(&ctx);
                    return 1;
                }

                char *tok = strtok(cols_copy, ",");
                int i = 0;
                while (tok && i < count) {
                    int col_idx;
                    if (safe_parse_int(tok, &col_idx, 0) != 0) {
                        fprintf(stderr, "Error: Invalid column index '%s'\n", tok);
                        free(cols_copy);
                        cleanup_cli_context(&ctx);
                        return 1;
                    }
                    ctx.select_cols[i++] = col_idx;
                    tok = strtok(NULL, ",");
                }
                ctx.select_count = i;
                qsort(ctx.select_cols, ctx.select_count, sizeof(int), compare_ints_asc);

                free(cols_copy);
                break;
            }

            case 11: {
                if (parse_name_list(optarg, &ctx.select_names, &ctx.select_name_count) != 0) {
                    fprintf(stderr, "Error: Invalid --select-name value\n");
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                break;
            }

            case 12:
                if (parse_where_expr(&ctx, optarg) != 0) {
                    fprintf(stderr, "Error: Invalid --where expression\n");
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                break;

            case 13:
                ctx.output_mode = 1;
                break;

            case 14:
                ctx.output_mode = 2;
                break;

            case 'c':
                ctx.count_only = 1;
                break;

            case 'o':
                output_file = optarg;
                break;

            case 'b':
                benchmark = 1;
                break;
            case 15:
                parallel = 1;
                break;
            case 16:
                if (safe_parse_int(optarg, &num_threads, 0) != 0) {
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                parallel = 1;
                break;

            case 17: {
                int max_procs;
                if (safe_parse_int(optarg, &max_procs, 0) != 0 || max_procs <= 0) {
                    fprintf(stderr, "Error: --max-procs must be a positive integer\n");
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                char value[32];
                snprintf(value, sizeof(value), "%d", max_procs);
                if (setenv("CISV_MAX_PROCS", value, 1) != 0) {
                    perror("setenv");
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                break;
            }

            case 18: {
                size_t parsed_size;
                if (safe_parse_size(optarg, &parsed_size) != 0) {
                    fprintf(stderr, "Error: --max-memory must be a positive byte size\n");
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                if (setenv("CISV_MAX_MEMORY", optarg, 1) != 0) {
                    perror("setenv");
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                break;
            }

            case 6: {
                int head_val;
                if (safe_parse_int(optarg, &head_val, 0) != 0) {
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                // SECURITY: Limit head value to prevent excessive memory allocation
                if (head_val > 10000000) {
                    fprintf(stderr, "Error: --head value too large (max 10000000)\n");
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                ctx.head = head_val;
                break;
            }

            case 7: {
                int tail_val;
                if (safe_parse_int(optarg, &tail_val, 0) != 0) {
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                // SECURITY: Limit tail value to prevent excessive memory allocation
                // tail buffer allocates tail * sizeof(char**) bytes
                if (tail_val > 10000000) {
                    fprintf(stderr, "Error: --tail value too large (max 10000000)\n");
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                ctx.tail = tail_val;
                ctx.tail_buffer = calloc(ctx.tail, sizeof(char **));
                ctx.tail_field_lengths = calloc(ctx.tail, sizeof(size_t *));
                ctx.tail_field_counts = calloc(ctx.tail, sizeof(size_t));
                if (!ctx.tail_buffer || !ctx.tail_field_lengths || !ctx.tail_field_counts) {
                    fprintf(stderr, "Memory allocation failed\n");
                    cleanup_cli_context(&ctx);
                    return 1;
                }
                break;
            }

            default:
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                cleanup_cli_context(&ctx);
                return 1;
        }
    }

    // SECURITY: Validate configuration for conflicts after all options are parsed
    if (config.delimiter == config.quote) {
        fprintf(stderr, "Error: Delimiter and quote character cannot be the same ('%c')\n",
                config.delimiter);
        cleanup_cli_context(&ctx);
        return 1;
    }
    if (config.escape != '\0' && config.escape == config.delimiter) {
        fprintf(stderr, "Error: Escape and delimiter cannot be the same ('%c')\n",
                config.escape);
        cleanup_cli_context(&ctx);
        return 1;
    }
    if (config.escape != '\0' && config.escape == config.quote) {
        fprintf(stderr, "Error: Escape and quote character cannot be the same ('%c')\n",
                config.escape);
        cleanup_cli_context(&ctx);
        return 1;
    }

    if (optind < argc) {
        filename = argv[optind];
    }

    if (!filename && !isatty(STDIN_FILENO)) {
        filename = "-";
    }

    if (!filename) {
        if (!ctx.quiet) {
            fprintf(stderr, "Error: No input file specified\n");
        }
        print_help(argv[0]);
        cleanup_cli_context(&ctx);
        return 1;
    }

    if (strcmp(filename, "-") == 0) {
        if (materialize_stdin_to_temp(stdin_tmp_path, sizeof(stdin_tmp_path)) < 0) {
            if (!ctx.quiet) {
                fprintf(stderr, "Error: Failed to read CSV data from stdin\n");
            }
            cleanup_cli_context(&ctx);
            return 1;
        }
        filename = stdin_tmp_path;
    }

    if (benchmark) {
        benchmark_file(filename, &config, parallel, num_threads);
        if (stdin_tmp_path[0]) unlink(stdin_tmp_path);
        cleanup_cli_context(&ctx);
        return 0;
    }

    if (ctx.count_only) {
        size_t count = 0;
        if (parallel) {
            if (count_rows_parallel(filename, &config, num_threads, &count) != 0) {
                if (!ctx.quiet) {
                    fprintf(stderr, "Parallel count failed\n");
                }
                if (stdin_tmp_path[0]) unlink(stdin_tmp_path);
                cleanup_cli_context(&ctx);
                return 1;
            }
        } else if (count_needs_checked_path(&config)) {
            if (count_rows_checked(filename, &config, ctx.quiet, &count) != 0) {
                if (!ctx.quiet) {
                    fprintf(stderr, "Count failed\n");
                }
                if (stdin_tmp_path[0]) unlink(stdin_tmp_path);
                cleanup_cli_context(&ctx);
                return 1;
            }
        } else {
            count = cisv_parser_count_rows_with_config(filename, &config);
        }
        printf("%zu\n", count);
        if (stdin_tmp_path[0]) unlink(stdin_tmp_path);
        cleanup_cli_context(&ctx);
        return 0;
    }

    if (output_file) {
        if (open_atomic_output_file(output_file, output_tmp_path, sizeof(output_tmp_path), &ctx.output) != 0) {
            if (stdin_tmp_path[0]) unlink(stdin_tmp_path);
            cleanup_cli_context(&ctx);
            return 1;
        }
    }
    setvbuf(ctx.output, NULL, _IOFBF, 1 << 20);

    if (can_use_fast_select_projector(&config, &ctx, parallel)) {
        int project_result = project_select_file_fast(filename, &config, &ctx);
        if (project_result < 0) {
            return finish_cli(&ctx, stdin_tmp_path, output_tmp_path, output_file, 0);
        }
        return finish_cli(&ctx, stdin_tmp_path, output_tmp_path, output_file, 1);
    }

    // Fast path: iterator avoids per-field allocations for common full-stream output.
    if (!parallel && ctx.head == 0 && ctx.tail == 0 && getenv("CISV_STATS") == NULL &&
        ctx.output_mode == 0 && !ctx.where_enabled &&
        !(ctx.select_names && ctx.select_name_count > 0)) {
        int iter_result = stream_rows_with_iterator(filename, &config, &ctx);
        if (iter_result < 0) {
            return finish_cli(&ctx, stdin_tmp_path, output_tmp_path, output_file, 0);
        }
        return finish_cli(&ctx, stdin_tmp_path, output_tmp_path, output_file, 1);
    }

    if (parallel) {
        int parallel_result = parse_file_parallel_cli(filename, &config, &ctx, num_threads);
        if (parallel_result < 0) {
            return finish_cli(&ctx, stdin_tmp_path, output_tmp_path, output_file, 0);
        }
    } else {
        int iter_result = parse_file_with_iterator_cli(filename, &config, &ctx);
        if (iter_result < 0) {
            return finish_cli(&ctx, stdin_tmp_path, output_tmp_path, output_file, 0);
        }
    }

    if (ctx.tail > 0 && ctx.tail_buffer) {
        size_t start = ctx.tail_pos;
        for (int i = 0; i < ctx.tail; i++) {
            size_t idx = (start + i) % ctx.tail;
            if (!ctx.tail_buffer[idx]) continue;

            if (output_row(&ctx,
                           (const char *const *)ctx.tail_buffer[idx],
                           ctx.tail_field_lengths[idx],
                           ctx.tail_field_counts[idx]) != 0) {
                return finish_cli(&ctx, stdin_tmp_path, output_tmp_path, output_file, 0);
            }
            ctx.row_count++;
        }
    }
    if (ctx.output_mode == 1) {
        if (!ctx.json_started) {
            fputs("[]\n", ctx.output);
        } else {
            fputs("]\n", ctx.output);
        }
    }
    return finish_cli(&ctx, stdin_tmp_path, output_tmp_path, output_file, 1);
}
