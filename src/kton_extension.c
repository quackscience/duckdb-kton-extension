#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <duckdb_extension.h>

#ifndef idx_t
typedef uint64_t idx_t;
#endif

DUCKDB_EXTENSION_EXTERN

typedef struct duckdb_extension_access duckdb_extension_access;

#if DUCKDB_EXTENSION_API_VERSION_MAJOR >= 1
#define EXTENSION_RETURN(result) return (result)
#else
#define EXTENSION_RETURN(result) return
#endif

typedef struct
{
    FILE *file;
    int initialized;
} KTONBindData;

typedef struct
{
    KTONBindData *bind_data;
    idx_t current_line;
} KTONScanState;

void destroy_bind_data(void *data)
{
    KTONBindData *bind_data = (KTONBindData *)data;
    if (bind_data->file)
    {
        fclose(bind_data->file);
    }
    free(bind_data);
}

void destroy_init_data(void *data)
{
    free(data);
}

duckdb_logical_type create_varchar_type()
{
    return duckdb_create_logical_type(DUCKDB_TYPE_VARCHAR);
}

duckdb_logical_type create_integer_type()
{
    return duckdb_create_logical_type(DUCKDB_TYPE_INTEGER);
}

duckdb_logical_type create_bigint_type()
{
    return duckdb_create_logical_type(DUCKDB_TYPE_BIGINT);
}

duckdb_logical_type create_date_type()
{
    return duckdb_create_logical_type(DUCKDB_TYPE_DATE);
}

duckdb_date parse_yymmdd_date(const char *yymmdd)
{
    int32_t year = (yymmdd[0] - '0') * 10 + (yymmdd[1] - '0') + 2000;
    int32_t month = (yymmdd[2] - '0') * 10 + (yymmdd[3] - '0');
    int32_t day = (yymmdd[4] - '0') * 10 + (yymmdd[5] - '0');

    // Convert to days since 1970-01-01 (Unix epoch)
    int32_t days = 0;

    // Add days for years from 1970 to (year-1)
    for (int y = 1970; y < year; y++)
    {
        days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    }

    // Add days for months in current year
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
    {
        days_in_month[1] = 29;
    }

    for (int m = 0; m < month - 1; m++)
    {
        days += days_in_month[m];
    }

    // Add remaining days
    days += day - 1;

    duckdb_date date;
    date.days = days;
    return date;
}

static void kton_bind(duckdb_bind_info info)
{
    duckdb_value file_path = duckdb_bind_get_parameter(info, 0);
    const char *path = duckdb_get_varchar(file_path);

    KTONBindData *bind_data = malloc(sizeof(KTONBindData));
    bind_data->file = fopen(path, "r");
    if (!bind_data->file)
    {
        duckdb_bind_set_error(info, "Could not open file");
        return;
    }

    duckdb_logical_type varchar_type = create_varchar_type();
    duckdb_logical_type int_type = create_integer_type();
    duckdb_logical_type bigint_type = create_bigint_type();
    duckdb_logical_type date_type = create_date_type();

    // Following KTON spec fields from basic transaction record (T10)
    duckdb_bind_add_result_column(info, "material_code", varchar_type);
    duckdb_bind_add_result_column(info, "record_number", varchar_type);
    duckdb_bind_add_result_column(info, "record_length", varchar_type);
    duckdb_bind_add_result_column(info, "transaction_number", int_type);
    duckdb_bind_add_result_column(info, "filing_id", varchar_type);
    duckdb_bind_add_result_column(info, "booking_date", date_type);
    duckdb_bind_add_result_column(info, "value_date", date_type);
    duckdb_bind_add_result_column(info, "payment_date", date_type);
    duckdb_bind_add_result_column(info, "transaction_amount", bigint_type);
    duckdb_bind_add_result_column(info, "transaction_code", varchar_type);
    duckdb_bind_add_result_column(info, "entry_node_code", varchar_type);
    duckdb_bind_add_result_column(info, "narrative_text", varchar_type);
    duckdb_bind_add_result_column(info, "receipt_code", varchar_type);
    duckdb_bind_add_result_column(info, "transfer_type", varchar_type);
    duckdb_bind_add_result_column(info, "payee_payer_name", varchar_type);
    duckdb_bind_add_result_column(info, "payee_payer_name_source", varchar_type);
    duckdb_bind_add_result_column(info, "payee_account_number", varchar_type);
    duckdb_bind_add_result_column(info, "payee_account_change_info", varchar_type);
    duckdb_bind_add_result_column(info, "reference", bigint_type);
    duckdb_bind_add_result_column(info, "form_number", varchar_type);
    duckdb_bind_add_result_column(info, "level_id", varchar_type);

    duckdb_destroy_logical_type(&varchar_type);
    duckdb_destroy_logical_type(&int_type);
    duckdb_destroy_logical_type(&bigint_type);
    duckdb_destroy_logical_type(&date_type);
    duckdb_bind_set_bind_data(info, bind_data, destroy_bind_data);
}

static void kton_init(duckdb_init_info info)
{
    KTONScanState *state = malloc(sizeof(KTONScanState));
    if (!state)
    {
        duckdb_init_set_error(info, "Memory allocation failed");
        return;
    }
    state->bind_data = (KTONBindData *)duckdb_init_get_bind_data(info);
    state->current_line = 0;

    duckdb_init_set_init_data(info, state, destroy_init_data);
}

static void kton_function(duckdb_function_info info, duckdb_data_chunk output)
{
    KTONScanState *state = (KTONScanState *)duckdb_function_get_init_data(info);
    KTONBindData *bind_data = state->bind_data;
    idx_t output_size = 0;
    char line[1024];

    while (output_size < duckdb_vector_size() && fgets(line, sizeof(line), bind_data->file))
    {
        if (strncmp(line, "T10", 3) == 0)
        {
            char field[256];

            // Material code (T): 1-1
            strncpy(field, line, 1);
            field[1] = '\0';
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 0), output_size, field);

            // Record number (10): 2-3 (alphanumeric)
            strncpy(field, line + 1, 2);
            field[2] = '\0';
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 1), output_size, field);

            // Record length - convert to integer: 4-6 (numeric)
            strncpy(field, line + 3, 3);
            field[3] = '\0';
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 2), output_size, field);

            // Transaction number - convert to integer: 7-12 (number)
            char num_field[7];
            strncpy(num_field, line + 6, 6);
            num_field[6] = '\0';
            int transaction_number = atoi(num_field);

            duckdb_vector output_vector = duckdb_data_chunk_get_vector(output, 3);
            void *data = duckdb_vector_get_data(output_vector);
            int32_t *int_data = (int32_t *)data;
            int_data[output_size] = transaction_number;

            // Filing ID: 13-30 (alphanumeric)
            strncpy(field, line + 12, 18);
            field[18] = '\0';
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 4), output_size, field);

            // Booking date - convert to DATE: 31-36 (number)
            strncpy(field, line + 30, 6);
            field[6] = '\0';
            duckdb_vector booking_vector = duckdb_data_chunk_get_vector(output, 5);
            duckdb_date *booking_data = (duckdb_date *)duckdb_vector_get_data(booking_vector);
            booking_data[output_size] = parse_yymmdd_date(field);

            // Value date - convert to DATE: 37-42 (number)
            strncpy(field, line + 36, 6);
            field[6] = '\0';
            duckdb_vector value_vector = duckdb_data_chunk_get_vector(output, 6);
            duckdb_date *value_data = (duckdb_date *)duckdb_vector_get_data(value_vector);
            value_data[output_size] = parse_yymmdd_date(field);

            // Payment date - convert to DATE: 43-48 (number)
            strncpy(field, line + 42, 6);
            field[6] = '\0';
            duckdb_vector payment_vector = duckdb_data_chunk_get_vector(output, 7);
            duckdb_date *payment_data = (duckdb_date *)duckdb_vector_get_data(payment_vector);
            payment_data[output_size] = parse_yymmdd_date(field);

            // Transaction code: 49-49 (alphanumeric)
            strncpy(field, line + 48, 1);
            field[1] = '\0';
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 9), output_size, field);

            // Entry node code: 50-52 (alphanumeric)
            strncpy(field, line + 49, 3);
            field[3] = '\0';
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 10), output_size, field);

            // Narrative text: 53-87 (alphanumeric)
            strncpy(field, line + 52, 35);
            field[35] = '\0';
            // Trim trailing spaces
            int len = strlen(field);
            while (len > 0 && field[len - 1] == ' ')
            {
                field[--len] = '\0';
            }
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 11), output_size, field);

            // Transaction amount in eurocents, including sign on 88-88
            char amount_str[20];
            strncpy(amount_str, line + 87, 19);
            amount_str[19] = '\0';
            int64_t eurocents = 0;
            int sign = (amount_str[0] == '+') ? 1 : -1;
            for (int i = 1; i < 19; i++)
            {
                if (isdigit(amount_str[i]))
                {
                    eurocents = eurocents * 10 + (amount_str[i] - '0');
                }
            }
            eurocents *= sign;
            duckdb_vector amount_vector = duckdb_data_chunk_get_vector(output, 8);
            int64_t *amount_data = (int64_t *)duckdb_vector_get_data(amount_vector);
            amount_data[output_size] = eurocents;

            // Receipt code: 107-107 (alphanumeric)
            strncpy(field, line + 106, 1);
            field[1] = '\0';
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 12), output_size, field);

            // Transfer type: 108-108 (alphanumeric)
            strncpy(field, line + 107, 1);
            field[1] = '\0';
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 13), output_size, field);

            // Payee/Payer name: 109-143 (alphanumeric)
            strncpy(field, line + 108, 35);
            field[35] = '\0';
            // Trim trailing spaces
            len = strlen(field);
            while (len > 0 && field[len-1] == ' ') {
                field[--len] = '\0';
            }
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 14), output_size, field);

            // Payee/Payer name source: 144-144 (alphanumeric)
            strncpy(field, line + 143, 1);
            field[1] = '\0';
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 15), output_size, field);

            // Payee's account number: 145-158 (alphanumeric)
            strncpy(field, line + 144, 14);
            field[14] = '\0';
            // Trim trailing spaces
            len = strlen(field);
            while (len > 0 && field[len-1] == ' ') {
                field[--len] = '\0';
            }
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 16), output_size, field);

            // Payee's account change information: 159-159 (alphanumeric)
            strncpy(field, line + 158, 1);
            field[1] = '\0';
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 17), output_size, field);

            // Reference: 160-179 (number)
            strncpy(field, line + 159, 20);
            field[20] = '\0';
            // Convert to int64
            int64_t reference = 0;
            for (int i = 0; i < 20 && field[i] != '\0' && field[i] != ' '; i++) {
                if (isdigit(field[i])) {
                    reference = reference * 10 + (field[i] - '0');
                }
            }
            duckdb_vector reference_vector = duckdb_data_chunk_get_vector(output, 18);
            int64_t *reference_data = (int64_t *)duckdb_vector_get_data(reference_vector);
            reference_data[output_size] = reference;

            // Form number: 180-187 (alphanumeric)
            strncpy(field, line + 179, 8);
            field[8] = '\0';
            // Trim trailing spaces
            len = strlen(field);
            while (len > 0 && field[len-1] == ' ') {
                field[--len] = '\0';
            }
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 19), output_size, field);

            // Level ID: 188-188 (alphanumeric)
            strncpy(field, line + 187, 1);
            field[1] = '\0';
            duckdb_vector_assign_string_element(duckdb_data_chunk_get_vector(output, 20), output_size, field);

            output_size++;
        }
    }
    duckdb_data_chunk_set_size(output, output_size);
}

DUCKDB_EXTENSION_ENTRYPOINT(duckdb_connection conn, duckdb_extension_info info, duckdb_extension_access *access)
{
    duckdb_table_function function = duckdb_create_table_function();
    duckdb_table_function_set_name(function, "read_kton");

    duckdb_logical_type varchar_type = create_varchar_type();
    duckdb_table_function_add_parameter(function, varchar_type);
    duckdb_destroy_logical_type(&varchar_type);

    duckdb_table_function_set_bind(function, kton_bind);
    duckdb_table_function_set_init(function, kton_init);
    duckdb_table_function_set_function(function, kton_function);

    duckdb_state result = duckdb_register_table_function(conn, function);
    duckdb_destroy_table_function(&function);

    if (result == DuckDBError)
    {
        access->set_error(info, "Failed to register function");
        EXTENSION_RETURN(false);
    }

    EXTENSION_RETURN(true);
}
